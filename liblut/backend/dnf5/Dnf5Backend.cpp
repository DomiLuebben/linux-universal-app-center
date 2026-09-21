#include "Dnf5Backend.h"
#include "liblut/backend/Validation.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace lut {
namespace {
QProcessEnvironment environment() {
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    env.insert(QStringLiteral("TERM"), QStringLiteral("dumb"));
    return env;
}
const QString format = QStringLiteral("%{name}\x1f%{evr}\x1f%{arch}\x1f%{installsize}\x1f%{from_repo}\x1f%{installtime}\x1f%{summary}\x1e");
QList<InstalledPackage> packages(const QByteArray &data) {
    QList<InstalledPackage> result;
    for (const auto &record : QString::fromUtf8(data).split(QChar(0x1e), Qt::SkipEmptyParts)) {
        const auto fields = record.trimmed().split(QChar(0x1f));
        if (fields.size() != 7) continue;
        InstalledPackage pkg;
        pkg.name = fields[0]; pkg.version = fields[1]; pkg.arch = fields[2];
        pkg.installedSize = fields[3].toLongLong(); pkg.repo = fields[4];
        pkg.installDate = QDateTime::fromSecsSinceEpoch(fields[5].toLongLong()).toString(Qt::ISODate);
        pkg.summary = fields[6]; pkg.description = pkg.summary;
        pkg.id = pkg.name + QLatin1Char('-') + pkg.version + QLatin1Char('.') + pkg.arch;
        result.append(pkg);
    }
    return result;
}
}

Dnf5Backend::Dnf5Backend(QObject *parent, const QString &program) : Backend(parent), m_program(program) {
    m_process.setProcessEnvironment(environment());
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        // Only preparation is timed out. Never kill an RPM transaction.
        m_timedOut = true;
        m_process.kill();
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &Dnf5Backend::readOutput);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_timeout.stop(); m_busy = false;
            fail(QStringLiteral("DNF5 konnte nicht gestartet werden: %1").arg(m_process.errorString()));
        }
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        readOutput(); m_timeout.stop(); m_busy = false;
        if (m_cancelled) {
            m_ready = false; m_plan.reset();
            emit eventEmitted(TransactionDone{Result::Cancelled, QStringLiteral("Vorbereitung abgebrochen"), false, {}, 0});
        } else if (m_timedOut || status != QProcess::NormalExit || code != 0) {
            fail(QStringLiteral("DNF5 %1: %2").arg(m_timedOut ? QStringLiteral("Zeitüberschreitung") : QStringLiteral("Fehler %1").arg(code), m_output.right(8192)));
        } else if (m_success) {
            auto success = std::move(m_success);
            success();
        }
    });
}
Dnf5Backend::~Dnf5Backend() {
    if (m_process.state() != QProcess::NotRunning) {
        if (m_cancellable) m_process.kill();
        m_process.waitForFinished(-1);
    }
}
Capabilities Dnf5Backend::capabilities() const {
    Capabilities cap;
    cap.downgrade = true; cap.historyUndo = true; cap.changelogs = true;
    cap.securityFlag = true; cap.offlineUpdate = false;
    // The CLI exposes logs but no stable byte-progress protocol.
    cap.degraded = true;
    return cap;
}
void Dnf5Backend::fail(const QString &message) {
    m_ready = false; m_plannedOps.clear();
    emit eventEmitted(LogLine{LogLevel::Error, QStringLiteral("dnf5"), message});
    emit eventEmitted(TransactionDone{Result::Failed, message, false, {}, 0});
}
void Dnf5Backend::readOutput() {
    const QString text = QString::fromUtf8(m_process.readAllStandardOutput());
    m_output = (m_output + text).right(65536);
    for (const auto &line : text.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts))
        emit eventEmitted(LogLine{LogLevel::Info, QStringLiteral("dnf5"), line});
}
void Dnf5Backend::start(const QStringList &args, bool cancellable, std::function<void()> success) {
    m_busy = true; m_cancelled = false; m_timedOut = false;
    m_cancellable = cancellable; m_success = std::move(success); m_output.clear();
    m_process.start(m_program, args);
    m_process.closeWriteChannel();
    if (cancellable) m_timeout.start(15 * 60 * 1000);
}
QByteArray Dnf5Backend::query(const QStringList &args, bool *ok) {
    QProcess process;
    process.setProcessEnvironment(environment());
    process.start(m_program, args);
    process.closeWriteChannel();
    bool success = process.waitForStarted(5000) && process.waitForFinished(120000) &&
                   process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (!success) {
        process.kill(); process.waitForFinished();
        emit eventEmitted(LogLine{LogLevel::Error, QStringLiteral("dnf5"), QString::fromUtf8(process.readAllStandardError()) + process.errorString()});
    }
    if (ok) *ok = success;
    return success ? process.readAllStandardOutput() : QByteArray{};
}
void Dnf5Backend::refreshMetadata() {
    if (m_busy) return;
    m_ready = false;
    emit eventEmitted(PhaseChanged{Phase::RefreshMetadata, QStringLiteral("DNF5-Metadaten aktualisieren"), true, true});
    start({QStringLiteral("--refresh"), QStringLiteral("makecache")}, true, [this] {
        emit eventEmitted(TransactionDone{Result::Success, QStringLiteral("Metadaten aktualisiert"), false, {}, 0});
    });
}
void Dnf5Backend::cleanCache() {
    if (m_busy) return;
    m_ready = false; m_plan.reset();
    emit eventEmitted(PhaseChanged{Phase::Cleanup, QStringLiteral("DNF5-Paketcache leeren"), false, true});
    start({QStringLiteral("clean"), QStringLiteral("packages")}, false, [this] {
        emit eventEmitted(TransactionDone{Result::Success, QStringLiteral("Paketcache geleert"), false, {}, 0});
    });
}
QStringList Dnf5Backend::transactionCommands() {
    return {QStringLiteral("upgrade"), QStringLiteral("install"), QStringLiteral("remove"),
        QStringLiteral("reinstall"), QStringLiteral("downgrade"), QStringLiteral("distro-sync"),
        QStringLiteral("swap"), QStringLiteral("autoremove"), QStringLiteral("history undo"),
        QStringLiteral("history redo"), QStringLiteral("history rollback"),
        QStringLiteral("group install"), QStringLiteral("group remove"), QStringLiteral("group upgrade"),
        QStringLiteral("environment install"), QStringLiteral("environment remove"), QStringLiteral("environment upgrade")};
}
void Dnf5Backend::planUpgradeAll(const UpgradeOptions &options) { planCommand(QStringLiteral("upgrade"), options.packages, options); }
void Dnf5Backend::planInstall(const QStringList &names) { planCommand(QStringLiteral("install"), names); }
void Dnf5Backend::planRemove(const QStringList &names) { planCommand(QStringLiteral("remove"), names); }
void Dnf5Backend::planCommand(const QString &command, const QStringList &arguments, const UpgradeOptions &options) {
    if (m_busy) return;
    m_ready = false; m_plannedOps.clear(); m_plan.reset();
    const bool optional = command == QLatin1String("upgrade") || command == QLatin1String("distro-sync") || command == QLatin1String("autoremove");
    if (!transactionCommands().contains(command) || (!optional && arguments.isEmpty()) ||
        (!arguments.isEmpty() && !Validation::areValidPackageNames(arguments)) ||
        (command == QLatin1String("swap") && arguments.size() != 2) ||
        (command == QLatin1String("autoremove") && !arguments.isEmpty()) ||
        (command.startsWith(QLatin1String("history ")) && (arguments.size() != 1 || !QRegularExpression(QStringLiteral("^[1-9][0-9]*$")).match(arguments.first()).hasMatch()))) {
        fail(QStringLiteral("Ungültiger DNF5-Befehl oder ungültige Paket-/Transaktionsangabe.")); return;
    }
    if ((options.includeSecurityOnly || options.excludeKernel || options.allowDowngrade) && command != QLatin1String("upgrade")) {
        fail(QStringLiteral("Updatefilter sind nur für upgrade verfügbar.")); return;
    }
    m_plan = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/lut-dnf5-XXXXXX"));
    if (!m_plan->isValid()) { fail(QStringLiteral("Transaktionsverzeichnis konnte nicht erstellt werden.")); return; }
    QStringList args{QStringLiteral("--assumeyes")};
    if (options.refreshFirst) args << QStringLiteral("--refresh");
    if (options.excludeKernel) args << QStringLiteral("--exclude=kernel*") << QStringLiteral("--exclude=linux*");
    args << command.split(QLatin1Char(' '));
    args << QStringLiteral("--store=%1/transaction").arg(m_plan->path());
    if (command == QLatin1String("upgrade")) {
        args << (options.allowDowngrade ? QStringLiteral("--allow-downgrade") : QStringLiteral("--no-allow-downgrade"));
        if (options.includeSecurityOnly) args << QStringLiteral("--security");
    }
    args << arguments;
    emit eventEmitted(PhaseChanged{Phase::Resolve, QStringLiteral("Transaktion vorbereiten und Pakete herunterladen"), true, true});
    start(args, true, [this] { loadStoredPlan(); });
}
void Dnf5Backend::loadStoredPlan() {
    QFile file(m_plan->path() + QStringLiteral("/transaction/transaction.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        // DNF5 creates no file when the resolver finds nothing to do.
        if (m_output.contains(QLatin1String("Nothing to do."))) {
            m_ready = true;
            emit eventEmitted(PlanReady{});
            emit eventEmitted(PhaseChanged{Phase::Idle, QStringLiteral("Keine Änderungen erforderlich"), false});
            return;
        }
        fail(QStringLiteral("DNF5 hat keinen lesbaren Transaktionsplan erzeugt.")); return;
    }
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (!doc.isObject() || doc[QStringLiteral("version")].toString() != QLatin1String("1.0") ||
        (doc.object().contains(QStringLiteral("rpms")) && !doc[QStringLiteral("rpms")].isArray())) {
        fail(QStringLiteral("Unbekanntes DNF5-Transaktionsformat: %1").arg(error.errorString())); return;
    }
    bool installedOk = false;
    const auto installed = packages(query({QStringLiteral("repoquery"), QStringLiteral("--installed"), QStringLiteral("--queryformat"), format}, &installedOk));
    if (!installedOk) { fail(QStringLiteral("Installierte RPM-Pakete konnten nicht abgefragt werden.")); return; }
    QStringList packagePaths;
    const QString root = m_plan->path() + QStringLiteral("/transaction/");
    for (const auto &value : doc[QStringLiteral("rpms")].toArray()) {
        const auto record = value.toObject();
        if (!record.contains(QStringLiteral("package_path"))) continue;
        const QString path = QFileInfo(root + record[QStringLiteral("package_path")].toString()).canonicalFilePath();
        if (path.isEmpty() || !path.startsWith(root)) { fail(QStringLiteral("Ungültiger Paketpfad im DNF5-Plan.")); return; }
        packagePaths.append(path);
    }
    QMap<QString, InstalledPackage> incoming;
    if (!packagePaths.isEmpty()) {
        bool ok = false;
        auto metadata = packages(query(QStringList{QStringLiteral("--disable-repo=*"), QStringLiteral("repoquery"), QStringLiteral("--queryformat"), format} + packagePaths, &ok));
        if (!ok || metadata.size() != packagePaths.size()) { fail(QStringLiteral("RPM-Metadaten fehlen im DNF5-Plan.")); return; }
        for (const auto &pkg : metadata) incoming.insert(pkg.id, pkg);
    }
    PlanReady plan;
    QMap<QString, InstalledPackage> previous;
    for (const auto &pkg : installed) previous.insert(pkg.name + QLatin1Char('.') + pkg.arch, pkg);
    for (const auto &value : doc[QStringLiteral("rpms")].toArray()) {
        const auto record = value.toObject();
        const QString action = record[QStringLiteral("action")].toString();
        const QString nevra = record[QStringLiteral("nevra")].toString();
        static const QRegularExpression re(QStringLiteral("^(.+)-([^-]+-[^-]+)\\.([^.]+)$"));
        const auto match = re.match(nevra);
        if (!match.hasMatch()) { fail(QStringLiteral("Ungültige RPM-Kennung im Plan: %1").arg(nevra)); return; }
        PackageOp op;
        op.id = nevra; op.name = match.captured(1); op.arch = match.captured(3);
        const QString key = op.name + QLatin1Char('.') + op.arch;
        op.version = previous.value(key).version; op.newVersion = match.captured(2);
        op.isKernel = PackageOp::detectIsKernel(op.name);
        op.userRequested = record[QStringLiteral("reason")].toString() == QLatin1String("User");
        op.repo = record[QStringLiteral("repo_id")].toString();
        op.repo.remove(QRegularExpression(QStringLiteral("^@+stored_transaction\\("))); if (op.repo.endsWith(QLatin1Char(')'))) op.repo.chop(1);
        if (action == QLatin1String("Remove") || action == QLatin1String("Replaced")) {
            // Match the exact installed version (installonly kernels can coexist).
            auto old = std::find_if(installed.begin(), installed.end(), [&](const auto &pkg) {
                auto evr = pkg.version; if (evr.startsWith(QLatin1String("0:"))) evr.remove(0, 2);
                return pkg.name == op.name && pkg.arch == op.arch && evr == op.newVersion;
            });
            if (old == installed.end()) { fail(QStringLiteral("Installierter Paketstand passt nicht zum DNF5-Plan: %1").arg(nevra)); return; }
            op.version = old->version; op.newVersion.clear(); op.installedSize = old->installedSize;
            op.installedSizeDelta = -old->installedSize; op.summary = old->summary;
            op.kind = PackageOp::Kind::Remove;
        } else {
            if (action != QLatin1String("Install") && action != QLatin1String("Upgrade") && action != QLatin1String("Downgrade") && action != QLatin1String("Reinstall")) {
                fail(QStringLiteral("Nicht unterstützte DNF5-Planaktion: %1").arg(action)); return;
            }
            auto found = incoming.constFind(nevra);
            if (found == incoming.cend()) { fail(QStringLiteral("RPM-Metadaten passen nicht zum Plan: %1").arg(nevra)); return; }
            op.summary = found->summary; op.installedSize = found->installedSize;
            op.installedSizeDelta = op.installedSize;
            op.kind = PackageOp::kindFromString(action);
            // --store has already downloaded the archives into the private plan.
            op.downloadSize = 0;
        }
        plan.installedSizeDelta += op.installedSizeDelta.value_or(0);
        plan.ops.append(op);
    }
    m_plannedOps = plan.ops; m_ready = true;
    plan.warnings << QStringLiteral("Pakete sind bereits vorbereitet. DNF5 meldet während der Ausführung keinen verlässlichen Byte-Fortschritt.");
    emit eventEmitted(plan);
    emit eventEmitted(PhaseChanged{Phase::Idle, QStringLiteral("Plan bereit zur Bestätigung"), true});
}
void Dnf5Backend::commit() {
    if (m_busy) return;
    if (!m_ready || !m_plan) { fail(QStringLiteral("Kein gültiger DNF5-Plan vorhanden. Bitte erneut prüfen.")); return; }
    m_ready = false;
    if (!QFileInfo::exists(m_plan->path() + QStringLiteral("/transaction/transaction.json"))) {
        emit eventEmitted(TransactionDone{Result::Success, QStringLiteral("Keine Änderungen erforderlich"), false, {}, 0}); return;
    }
    emit eventEmitted(PhaseChanged{Phase::Commit, QStringLiteral("DNF5 führt die vorbereitete Transaktion aus"), false, true});
    start({QStringLiteral("--assumeyes"), QStringLiteral("--setopt=localpkg_gpgcheck=1"),
           QStringLiteral("--setopt=*.pkg_gpgcheck=1"), QStringLiteral("replay"),
           m_plan->path() + QStringLiteral("/transaction")}, false, [this] {
        const bool reboot = std::any_of(m_plannedOps.begin(), m_plannedOps.end(), [](const auto &op) { return op.isKernel && op.kind != PackageOp::Kind::Remove; });
        m_plan.reset();
        emit eventEmitted(TransactionDone{Result::Success, QStringLiteral("DNF5-Transaktion erfolgreich abgeschlossen"), reboot, {}, 0});
    });
}
void Dnf5Backend::cancel() {
    if (m_busy) {
        if (m_cancellable) { m_cancelled = true; m_process.kill(); }
        return;
    }
    m_ready = false; m_plan.reset(); m_plannedOps.clear();
    emit eventEmitted(TransactionDone{Result::Cancelled, QStringLiteral("Plan verworfen"), false, {}, 0});
}
QList<PackageOp> Dnf5Backend::availableUpdates() { return m_plannedOps; }
QList<InstalledPackage> Dnf5Backend::installedPackages(const QString &filter) {
    auto result = packages(query({QStringLiteral("repoquery"), QStringLiteral("--installed"), QStringLiteral("--queryformat"), format}));
    const auto orphanData = query({QStringLiteral("repoquery"), QStringLiteral("--unneeded"), QStringLiteral("--queryformat"), QStringLiteral("%{name}.%{arch}\\n")});
    const auto orphanList = QString::fromUtf8(orphanData).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const QSet<QString> orphans(orphanList.begin(), orphanList.end());
    for (auto &pkg : result) pkg.isOrphan = orphans.contains(pkg.name + QLatin1Char('.') + pkg.arch);
    result.removeIf([&](const auto &pkg) { return !filter.isEmpty() && !pkg.name.contains(filter, Qt::CaseInsensitive) && !pkg.summary.contains(filter, Qt::CaseInsensitive); });
    return result;
}
QList<ChangelogEntry> Dnf5Backend::changelog(const QString &pkgId) {
    if (!Validation::isValidPackageName(pkgId)) return {};
    const auto text = QString::fromUtf8(query({QStringLiteral("repoquery"), QStringLiteral("--installed"), QStringLiteral("--changelogs"), pkgId})).trimmed();
    return text.isEmpty() ? QList<ChangelogEntry>{} : QList<ChangelogEntry>{{{}, {}, {}, text}};
}
QList<HistoryEntry> Dnf5Backend::parseHistory(const QByteArray &json, int limit, QString *error) {
    const auto doc = QJsonDocument::fromJson(json);
    if (!doc.isArray()) { if (error) *error = QStringLiteral("Ungültige DNF5-Verlaufsausgabe."); return {}; }
    QList<HistoryEntry> result;
    for (const auto &value : doc.array()) {
        const auto obj = value.toObject();
        HistoryEntry entry;
        entry.id = obj[QStringLiteral("id")].toInteger();
        if (entry.id <= 0) continue;
        entry.timestamp = QDateTime::fromSecsSinceEpoch(obj[QStringLiteral("start_time")].toInteger());
        entry.command = obj[QStringLiteral("command_line")].toString();
        entry.result = obj[QStringLiteral("status")].toString();
        entry.packagesAltered = obj[QStringLiteral("altered_count")].toInt();
        entry.canUndo = entry.result == QLatin1String("Ok");
        result.append(entry);
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.id > b.id; });
    if (limit >= 0 && result.size() > limit) result.resize(limit);
    return result;
}
QList<HistoryEntry> Dnf5Backend::history(int limit) {
    return parseHistory(query({QStringLiteral("history"), QStringLiteral("list"), QStringLiteral("--json")}), limit);
}
}
