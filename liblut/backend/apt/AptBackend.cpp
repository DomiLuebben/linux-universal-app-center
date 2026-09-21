#include "AptBackend.h"
#include "PackageMetadata.h"
#include "liblut/backend/Validation.h"
#include <QRegularExpression>
#include <QProcessEnvironment>
#include <QFile>
#include <QMap>

namespace lut {
namespace {
QProcessEnvironment environment() {
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    env.insert(QStringLiteral("DEBIAN_FRONTEND"), QStringLiteral("noninteractive"));
    env.insert(QStringLiteral("PAGER"), QStringLiteral("cat"));
    return env;
}
bool run(const QString &program, const QStringList &args, QByteArray &out, QString &error, int timeout = 120000) {
    QProcess process; process.setProcessEnvironment(environment()); process.start(program, args); process.closeWriteChannel();
    if (!process.waitForStarted(5000) || !process.waitForFinished(timeout)) {
        error = process.errorString(); process.kill(); process.waitForFinished(); return false;
    }
    out = process.readAllStandardOutput();
    error = QString::fromUtf8(process.readAllStandardError());
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}
}
AptBackend::AptBackend(QObject *parent) : Backend(parent) {}
AptBackend::~AptBackend() {
    // Let an active dpkg operation finish rather than killing it mid-transaction.
    if (m_process && m_process->state() != QProcess::NotRunning) m_process->waitForFinished(-1);
}
Capabilities AptBackend::capabilities() const {
    Capabilities cap; cap.changelogs = true; cap.degraded = true; cap.autoremove = false;
    return cap;
}
void AptBackend::fail(const QString &error) {
    m_ready = false; m_plannedOps.clear();
    emit eventEmitted(LogLine{LogLevel::Error, QStringLiteral("apt"), error});
    emit eventEmitted(TransactionDone{Result::Failed, error, false, {}, 0});
}
void AptBackend::refreshMetadata() {
    if (m_running) return;
    m_ready = false;
    emit eventEmitted(PhaseChanged{Phase::RefreshMetadata, QStringLiteral("APT-Paketlisten aktualisieren"), false, true});
    QByteArray out; QString error;
    if (!run(QStringLiteral("/usr/bin/apt-get"), {QStringLiteral("update"), QStringLiteral("--error-on=any")}, out, error)) { fail(error); return; }
    emit eventEmitted(TransactionDone{Result::Success, QStringLiteral("Paketlisten aktualisiert"), false, {}, 0});
}
void AptBackend::planUpgradeAll(const UpgradeOptions &options) {
    if (m_running) return;
    if (options.includeSecurityOnly || options.excludeKernel || options.allowDowngrade) {
        fail(QStringLiteral("Diese Updatefilter werden vom APT-Backend noch nicht unterstützt.")); return;
    }
    if (!options.packages.isEmpty() && !Validation::areValidPackageNames(options.packages)) { fail(QStringLiteral("Ungültige Paketnamen.")); return; }
    if (options.refreshFirst) {
        QByteArray out; QString error;
        emit eventEmitted(PhaseChanged{Phase::RefreshMetadata, QStringLiteral("APT-Paketlisten aktualisieren"), false, true});
        if (!run(QStringLiteral("/usr/bin/apt-get"), {QStringLiteral("update"), QStringLiteral("--error-on=any")}, out, error)) { fail(error); return; }
    }
    if (options.packages.isEmpty()) plan({QStringLiteral("dist-upgrade")});
    else plan(QStringList{QStringLiteral("install"), QStringLiteral("--only-upgrade")} + options.packages);
}
void AptBackend::planInstall(const QStringList &names) {
    if (!Validation::areValidPackageNames(names)) { fail(QStringLiteral("Ungültige Paketnamen.")); return; }
    plan(QStringList{QStringLiteral("install")} + names);
}
void AptBackend::planRemove(const QStringList &names) {
    if (!Validation::areValidPackageNames(names)) { fail(QStringLiteral("Ungültige Paketnamen.")); return; }
    plan(QStringList{QStringLiteral("remove")} + names);
}
void AptBackend::plan(const QStringList &arguments) {
    if (m_running) return;
    m_ready = false; m_plannedOps.clear(); m_arguments.clear();
    emit eventEmitted(PhaseChanged{Phase::Resolve, QStringLiteral("APT-Transaktion und Paketgrößen ermitteln"), false, true});
    QByteArray output; QString error;
    if (!run(QStringLiteral("/usr/bin/apt-get"), QStringList{QStringLiteral("--simulate")} + arguments, output, error)) { fail(error); return; }
    const auto installed = installedPackages();
    static const QRegularExpression install(QStringLiteral(R"(^Inst\s+(\S+)(?:\s+\[([^\]]+)\])?\s+\((\S+)\s+(.+)\s+\[([^\]]+)\]\))"));
    static const QRegularExpression remove(QStringLiteral(R"(^Remv\s+(\S+)\s+\[([^\]]+)\])"));
    PlanReady plan;
    QStringList specs{QStringLiteral("show")};
    for (const auto &line : QString::fromUtf8(output).split(QLatin1Char('\n'))) {
        auto match = install.match(line); auto removed = remove.match(line);
        if (!match.hasMatch() && !removed.hasMatch()) {
            if (line.startsWith(QLatin1String("Inst ")) || line.startsWith(QLatin1String("Remv "))) { fail(QStringLiteral("Unbekanntes APT-Planformat: %1").arg(line)); return; }
            continue;
        }
        PackageOp op;
        op.name = (match.hasMatch() ? match : removed).captured(1).section(QLatin1Char(':'), 0, 0);
        op.version = (match.hasMatch() ? match : removed).captured(2);
        if (match.hasMatch()) {
            op.newVersion = match.captured(3); op.repo = match.captured(4); op.arch = match.captured(5);
            op.kind = op.version.isEmpty() ? PackageOp::Kind::Install : PackageOp::Kind::Upgrade;
            specs << op.name + QLatin1Char(':') + op.arch + QLatin1Char('=') + op.newVersion;
        } else op.kind = PackageOp::Kind::Remove;
        qint64 previousSize = 0;
        for (const auto &pkg : installed) if (pkg.name == op.name && pkg.version == op.version && (op.arch.isEmpty() || pkg.arch == op.arch || pkg.arch == QLatin1String("all"))) {
            previousSize = pkg.installedSize; if (op.arch.isEmpty()) op.arch = pkg.arch; break;
        }
        op.installedSizeDelta = -previousSize;
        if (op.kind == PackageOp::Kind::Remove) op.installedSize = previousSize;
        op.isKernel = PackageOp::detectIsKernel(op.name);
        op.isSecurity = op.repo.contains(QLatin1String("security"), Qt::CaseInsensitive);
        op.id = op.name + QLatin1Char('_') + (op.newVersion.isEmpty() ? op.version : op.newVersion) + QLatin1Char('_') + op.arch;
        plan.ops.append(op);
    }
    QList<AptPackageMetadata> metadata;
    if (specs.size() > 1) {
        if (!run(QStringLiteral("/usr/bin/apt-cache"), specs, output, error)) { fail(error); return; }
        metadata = parseAptMetadata(QString::fromUtf8(output));
    }
    for (auto &op : plan.ops) {
        if (op.kind != PackageOp::Kind::Remove) {
            bool found = false;
            for (const auto &item : metadata) if (item.name == op.name && item.version == op.newVersion && (item.arch == op.arch || item.arch == QLatin1String("all"))) {
                if (item.downloadSize < 0 || item.installedSize < 0) continue;
                op.downloadSize = item.downloadSize; op.installedSize = item.installedSize;
                op.installedSizeDelta = op.installedSizeDelta.value_or(0) + item.installedSize;
                op.summary = item.summary; found = true; break;
            }
            if (!found) { fail(QStringLiteral("Keine verlässlichen Größen für %1 verfügbar.").arg(op.id)); return; }
        }
        plan.downloadBytes += op.downloadSize;
        plan.installedSizeDelta += op.installedSizeDelta.value_or(0);
    }
    m_arguments = arguments; m_plannedOps = plan.ops; m_ready = true;
    emit eventEmitted(plan);
    emit eventEmitted(PhaseChanged{Phase::Idle, QStringLiteral("Plan bereit"), true});
}
void AptBackend::commit() {
    if (m_running) return;
    if (!m_ready) { fail(QStringLiteral("Kein gültiger APT-Plan vorhanden.")); return; }
    m_ready = false;
    if (m_plannedOps.isEmpty()) { emit eventEmitted(TransactionDone{Result::Success, QStringLiteral("Keine Änderungen erforderlich"), false, {}, 0}); return; }
    if (!m_process) {
        m_process = new QProcess(this); m_process->setProcessEnvironment(environment());
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
            while (m_process->canReadLine()) handleStatusFdLine(QString::fromUtf8(m_process->readLine()).trimmed());
        });
        connect(m_process, &QProcess::readyReadStandardError, this, [this] {
            emit eventEmitted(LogLine{LogLevel::Warning, QStringLiteral("apt"), QString::fromUtf8(m_process->readAllStandardError())});
        });
        connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) { m_running = false; fail(m_process->errorString()); }
        });
        connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            m_running = false;
            if (status != QProcess::NormalExit || code != 0) { fail(QStringLiteral("APT fehlgeschlagen (Code %1), Details im Protokoll.").arg(code)); return; }
            emit eventEmitted(TransactionDone{Result::Success, QStringLiteral("APT-Transaktion abgeschlossen"), QFile::exists(QStringLiteral("/var/run/reboot-required")), {}, 0});
        });
    }
    m_running = true;
    emit eventEmitted(PhaseChanged{Phase::Commit, QStringLiteral("APT führt die Transaktion aus"), false, true});
    m_process->start(QStringLiteral("/usr/bin/apt-get"), QStringList{QStringLiteral("-o"), QStringLiteral("APT::Status-Fd=1"), QStringLiteral("-o"), QStringLiteral("Dpkg::Use-Pty=0"), QStringLiteral("-y")} + m_arguments);
    m_process->closeWriteChannel();
}
void AptBackend::cancel() {
    if (m_running) return;
    m_ready = false; m_plannedOps.clear();
    emit eventEmitted(TransactionDone{Result::Cancelled, QStringLiteral("Plan verworfen"), false, {}, 0});
}
void AptBackend::answerQuestion(const QString &, const QJsonObject &) {}
void AptBackend::handleStatusFdLine(const QString &line) {
    // dpkg percentages describe operations, not transferred bytes.
    emit eventEmitted(LogLine{LogLevel::Info, QStringLiteral("apt"), line});
}
QList<PackageOp> AptBackend::availableUpdates() { return m_plannedOps; }
QList<InstalledPackage> AptBackend::installedPackages(const QString &filter) {
    QByteArray output; QString error;
    const QString format = QStringLiteral("${db:Status-Status}\t${Package}\t${Version}\t${Architecture}\t${Installed-Size}\t${binary:Summary}\\n");
    if (!run(QStringLiteral("/usr/bin/dpkg-query"), {QStringLiteral("-W"), QStringLiteral("-f=") + format}, output, error)) {
        emit eventEmitted(LogLine{LogLevel::Error, QStringLiteral("dpkg"), error}); return {};
    }
    QList<InstalledPackage> result;
    for (const auto &line : QString::fromUtf8(output).split(QLatin1Char('\n'))) {
        const auto fields = line.split(QLatin1Char('\t'));
        if (fields.size() < 6 || fields[0] != QLatin1String("installed")) continue;
        InstalledPackage pkg;
        pkg.name = fields[1]; pkg.version = fields[2]; pkg.arch = fields[3]; pkg.installedSize = fields[4].toLongLong() * 1024;
        pkg.summary = fields[5]; pkg.description = pkg.summary; pkg.id = pkg.name + QLatin1Char(':') + pkg.arch;
        if (filter.isEmpty() || pkg.name.contains(filter, Qt::CaseInsensitive) || pkg.summary.contains(filter, Qt::CaseInsensitive)) result.append(pkg);
    }
    return result;
}
QList<ChangelogEntry> AptBackend::changelog(const QString &pkgId) {
    if (!Validation::isValidPackageName(pkgId)) return {};
    QByteArray out; QString error;
    if (!run(QStringLiteral("/usr/bin/apt-get"), {QStringLiteral("changelog"), pkgId}, out, error)) return {};
    return {{{}, {}, {}, QString::fromUtf8(out)}};
}
QList<HistoryEntry> AptBackend::history(int limit) {
    QFile file(QStringLiteral("/var/log/apt/history.log"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    QList<HistoryEntry> result;
    HistoryEntry entry;
    for (const auto &line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("Start-Date:"))) {
            entry = {}; const auto date = line.mid(11).simplified();
            entry.timestamp = QDateTime::fromString(date, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            entry.id = entry.timestamp.toSecsSinceEpoch(); entry.result = QStringLiteral("Failed");
        } else if (line.startsWith(QLatin1String("Commandline:"))) entry.command = line.mid(12).trimmed();
        else if (line.startsWith(QLatin1String("End-Date:"))) { entry.result = QStringLiteral("Success"); result.prepend(entry); }
        else if (QRegularExpression(QStringLiteral("^(Install|Upgrade|Remove|Purge|Downgrade|Reinstall):")).match(line).hasMatch()) entry.packagesAltered += line.count(QLatin1Char('('));
    }
    if (limit >= 0 && result.size() > limit) result.resize(limit);
    return result;
}
}
