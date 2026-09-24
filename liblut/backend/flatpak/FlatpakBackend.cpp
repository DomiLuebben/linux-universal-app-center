#include "FlatpakBackend.h"
#include "liblut/backend/Validation.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QThread>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

namespace lut {

FlatpakBackend::FlatpakBackend(ProcessRunner runner, QObject *parent)
    : Backend(parent)
    , m_runner(std::move(runner))
{
}

FlatpakBackend::~FlatpakBackend()
{
    if (m_activeProcess && m_activeProcess->state() != QProcess::NotRunning) {
        m_activeProcess->terminate();
        if (!m_activeProcess->waitForFinished(3000)) {
            m_activeProcess->kill();
        }
    }
}

bool FlatpakBackend::isFlatpakAvailable()
{
    return QFile::exists(QStringLiteral("/usr/bin/flatpak"));
}

int FlatpakBackend::runCommand(const QStringList &args, QString &stdoutOut, QString &stderrOut)
{
    if (m_runner) {
        return m_runner(QStringLiteral("/usr/bin/flatpak"), args, stdoutOut, stderrOut);
    }

    if (!isFlatpakAvailable()) {
        stderrOut = QStringLiteral("flatpak executable not found");
        return -1;
    }

    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    proc.setProcessEnvironment(env);
    proc.start(QStringLiteral("/usr/bin/flatpak"), args);

    if (!proc.waitForStarted(5000)) {
        stderrOut = QStringLiteral("Failed to start flatpak process");
        return -1;
    }

    if (!proc.waitForFinished(30000)) {
        proc.kill();
        stderrOut = QStringLiteral("flatpak process timed out");
        return -1;
    }

    stdoutOut = QString::fromUtf8(proc.readAllStandardOutput());
    stderrOut = QString::fromUtf8(proc.readAllStandardError());
    return proc.exitCode();
}

Capabilities FlatpakBackend::capabilities() const
{
    Capabilities caps;
    caps.install = true;
    caps.remove = true;
    caps.installRequiresFullUpgrade = false; // Never couple Flatpak to ALPM system upgrade
    caps.typedPackageTargets = true;

    SourceCapabilities src;
    src.source = QStringLiteral("flatpak");
    src.available = m_runner ? true : isFlatpakAvailable();
    src.install = true;
    src.remove = true;
    src.systemScope = true;
    src.userScope = false; // daemon executes system scope only
    src.boundRevision = true;
    src.installRequiresFullUpgrade = false;
    caps.sources.append(src);

    return caps;
}

void FlatpakBackend::refreshMetadata()
{
    emit eventEmitted(PhaseChanged{Phase::RefreshMetadata, QStringLiteral("Flatpak-Metadaten aktualisieren"), false, true});
    QString out, err;
    int rc = runCommand({QStringLiteral("update"), QStringLiteral("--appstream")}, out, err);
    if (rc == 0) {
        emit eventEmitted(TransactionDone{Result::Success, QStringLiteral("Flatpak-Metadaten aktualisiert"), false, {}, 0});
    } else {
        emit eventEmitted(LogLine{LogLevel::Error, QStringLiteral("flatpak"), err});
        emit eventEmitted(TransactionDone{Result::Failed, err.isEmpty() ? QStringLiteral("Metadaten-Aktualisierung fehlgeschlagen") : err, false, {}, 0});
    }
}

void FlatpakBackend::planUpgradeAll(const UpgradeOptions &options)
{
    Q_UNUSED(options);
    emit eventEmitted(LogLine{LogLevel::Error, QStringLiteral("flatpak"), QStringLiteral("Pauschales Upgrade wird über dieses Backend nicht geplant.")});
    emit eventEmitted(TransactionDone{Result::Failed, QStringLiteral("Pauschales Upgrade wird über dieses Backend nicht geplant."), false, {}, 0});
}

void FlatpakBackend::planInstall(const QStringList &names)
{
    TransactionIntent intent;
    intent.type = TransactionIntent::Type::Install;
    for (const auto &name : names) {
        PackageRef pref;
        pref.backend = QStringLiteral("flatpak");
        pref.name = name;
        pref.repoId = QStringLiteral("flathub");
        intent.targets.append(pref);
    }
    planPackageTransaction(intent);
}

void FlatpakBackend::planRemove(const QStringList &names)
{
    TransactionIntent intent;
    intent.type = TransactionIntent::Type::Remove;
    for (const auto &name : names) {
        PackageRef pref;
        pref.backend = QStringLiteral("flatpak");
        pref.name = name;
        intent.targets.append(pref);
    }
    planPackageTransaction(intent);
}

void FlatpakBackend::planPackageTransaction(const TransactionIntent &intent)
{
    m_currentIntent = intent;
    m_currentPlan = TransactionPlan();
    m_boundCommit.clear();
    m_targetRef.clear();
    m_targetRepo.clear();

    if (intent.targets.isEmpty()) {
        const QString err = QStringLiteral("Keine Ziele für Flatpak-Transaktion angegeben.");
        emit eventEmitted(LogLine{LogLevel::Error, QStringLiteral("flatpak"), err});
        emit eventEmitted(TransactionDone{Result::Failed, err, false, {}, 0});
        return;
    }

    const PackageRef target = intent.targets.first();
    if (!Validation::isValidFlatpakAppId(target.name)) {
        const QString err = QStringLiteral("Ungültige Flatpak-App-ID: %1").arg(target.name);
        emit eventEmitted(LogLine{LogLevel::Error, QStringLiteral("flatpak"), err});
        emit eventEmitted(TransactionDone{Result::Failed, err, false, {}, 0});
        return;
    }

    m_targetRef = target.name;
    m_targetRepo = target.repoId.isEmpty() ? QStringLiteral("flathub") : target.repoId;

    emit eventEmitted(PhaseChanged{Phase::Resolve, QStringLiteral("Flatpak-Transaktion vorbereiten"), false, true});

    if (intent.type == TransactionIntent::Type::Install) {
        QString commitOut, commitErr;
        int rc = runCommand({QStringLiteral("remote-info"), QStringLiteral("-c"), m_targetRepo, m_targetRef}, commitOut, commitErr);
        if (rc != 0 || commitOut.trimmed().isEmpty()) {
            const QString err = QStringLiteral("Flatpak-Referenz %1 in Remote %2 nicht gefunden.").arg(m_targetRef, m_targetRepo);
            emit eventEmitted(LogLine{LogLevel::Error, QStringLiteral("flatpak"), err});
            emit eventEmitted(TransactionDone{Result::Failed, err, false, {}, 0});
            return;
        }

        m_boundCommit = commitOut.trimmed();
        m_currentPlan.planRevision = m_boundCommit;
        m_currentPlan.backendRevision = m_boundCommit;

        // Query runtime dependency
        QString rtOut, rtErr;
        runCommand({QStringLiteral("remote-info"), QStringLiteral("--show-runtime"), m_targetRepo, m_targetRef}, rtOut, rtErr);
        const QString runtimeRef = rtOut.trimmed();

        // Check if runtime is already installed
        bool runtimeInstalled = true;
        if (!runtimeRef.isEmpty()) {
            QString listOut, listErr;
            runCommand({QStringLiteral("list"), QStringLiteral("--runtime"), QStringLiteral("--columns=ref:f")}, listOut, listErr);
            runtimeInstalled = listOut.contains(runtimeRef);
        }

        // Query sizes if available
        qint64 dlSize = 0;
        qint64 instSize = 0;
        QString infoOut, infoErr;
        if (runCommand({QStringLiteral("remote-info"), m_targetRepo, m_targetRef}, infoOut, infoErr) == 0) {
            static const QRegularExpression dlRegex(QStringLiteral(R"(Download\s+Size:\s*([\d,\.]+)\s*(\S+))"), QRegularExpression::CaseInsensitiveOption);
            static const QRegularExpression instRegex(QStringLiteral(R"(Installed\s+Size:\s*([\d,\.]+)\s*(\S+))"), QRegularExpression::CaseInsensitiveOption);
            auto parseSize = [](const QString &text, const QRegularExpression &regex) -> qint64 {
                auto match = regex.match(text);
                if (!match.hasMatch()) return 0;
                double val = match.captured(1).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
                QString unit = match.captured(2).toUpper();
                if (unit.startsWith(QLatin1String("GB"))) return static_cast<qint64>(val * 1024 * 1024 * 1024);
                if (unit.startsWith(QLatin1String("MB"))) return static_cast<qint64>(val * 1024 * 1024);
                if (unit.startsWith(QLatin1String("KB"))) return static_cast<qint64>(val * 1024);
                return static_cast<qint64>(val);
            };
            dlSize = parseSize(infoOut, dlRegex);
            instSize = parseSize(infoOut, instRegex);
        }

        PackageOp appOp;
        appOp.id = QStringLiteral("flatpak:%1").arg(m_targetRef);
        appOp.name = m_targetRef;
        appOp.kind = PackageOp::Kind::Install;
        appOp.repo = m_targetRepo;
        appOp.newVersion = m_boundCommit.left(12);
        if (dlSize > 0) appOp.downloadSize = dlSize;
        if (instSize > 0) appOp.installedSize = instSize;
        m_currentPlan.ops.append(appOp);

        if (!runtimeInstalled && !runtimeRef.isEmpty()) {
            PackageOp rtOp;
            rtOp.id = QStringLiteral("flatpak:%1").arg(runtimeRef);
            rtOp.name = runtimeRef;
            rtOp.kind = PackageOp::Kind::Install;
            rtOp.repo = m_targetRepo;
            rtOp.summary = QStringLiteral("Laufzeitumgebung");
            m_currentPlan.ops.append(rtOp);
        }

        m_currentPlan.downloadBytes = dlSize;
        m_currentPlan.installedSizeDelta = instSize;

        PlanReady planReady;
        planReady.ops = m_currentPlan.ops;
        planReady.downloadBytes = m_currentPlan.downloadBytes;
        planReady.installedSizeDelta = m_currentPlan.installedSizeDelta;
        planReady.warnings = m_currentPlan.warnings;
        planReady.planRevision = m_boundCommit;

        emit eventEmitted(planReady);
        emit eventEmitted(PhaseChanged{Phase::Idle, QStringLiteral("Plan bereit"), true});

    } else if (intent.type == TransactionIntent::Type::Remove) {
        // Query scope of installed application
        QString listOut, listErr;
        runCommand({QStringLiteral("list"), QStringLiteral("--app"), QStringLiteral("--columns=application:f,installation:f")}, listOut, listErr);

        bool found = false;
        bool isUser = false;
        const QStringList lines = listOut.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const auto &line : lines) {
            const QStringList parts = line.split(QLatin1Char('\t'));
            if (!parts.isEmpty() && parts[0].trimmed() == m_targetRef) {
                found = true;
                if (parts.size() > 1 && parts[1].trimmed() == QLatin1String("user")) {
                    isUser = true;
                }
                break;
            }
        }

        if (isUser) {
            const QString err = QStringLiteral("Benutzerinstallationen (user) können nicht über den systemweiten Dienst entfernt werden.");
            emit eventEmitted(LogLine{LogLevel::Error, QStringLiteral("flatpak"), err});
            emit eventEmitted(TransactionDone{Result::Failed, err, false, {}, 0});
            return;
        }

        if (!found) {
            const QString err = QStringLiteral("Anwendung %1 ist nicht systemweit installiert.").arg(m_targetRef);
            emit eventEmitted(LogLine{LogLevel::Error, QStringLiteral("flatpak"), err});
            emit eventEmitted(TransactionDone{Result::Failed, err, false, {}, 0});
            return;
        }

        m_currentPlan.planRevision = QStringLiteral("remove-") + m_targetRef;

        PackageOp appOp;
        appOp.id = QStringLiteral("flatpak:%1").arg(m_targetRef);
        appOp.name = m_targetRef;
        appOp.kind = PackageOp::Kind::Remove;
        m_currentPlan.ops.append(appOp);

        PlanReady planReady;
        planReady.ops = m_currentPlan.ops;
        planReady.planRevision = m_currentPlan.planRevision;

        emit eventEmitted(planReady);
        emit eventEmitted(PhaseChanged{Phase::Idle, QStringLiteral("Plan bereit"), true});
    }
}

void FlatpakBackend::commit()
{
    commitPlan(m_currentPlan.planRevision);
}

void FlatpakBackend::commitPlan(const QString &planRevision)
{
    if (m_currentPlan.planRevision.isEmpty() || planRevision != m_currentPlan.planRevision) {
        const QString err = QStringLiteral("Planrevision stimmt nicht überein (Plan geändert). Bitte neu bestätigen.");
        emit eventEmitted(LogLine{LogLevel::Error, QStringLiteral("flatpak"), err});
        emit eventEmitted(TransactionDone{Result::Failed, err, false, {}, 0});
        return;
    }

    if (m_currentIntent.type == TransactionIntent::Type::Install) {
        // Section 6.4: Bind to confirmed version via commit hash
        QString commitOut, commitErr;
        int rc = runCommand({QStringLiteral("remote-info"), QStringLiteral("-c"), m_targetRepo, m_targetRef}, commitOut, commitErr);
        if (rc != 0 || commitOut.trimmed().isEmpty()) {
            const QString err = QStringLiteral("Remote-Commit für %1 konnte nicht verifiziert werden.").arg(m_targetRef);
            emit eventEmitted(LogLine{LogLevel::Error, QStringLiteral("flatpak"), err});
            emit eventEmitted(TransactionDone{Result::Failed, err, false, {}, 0});
            return;
        }

        const QString currentCommit = commitOut.trimmed();
        if (currentCommit != m_boundCommit) {
            const QString err = QStringLiteral("Commit-Hash hat sich zwischen Planung und Ausführung geändert (%1 != %2). Bitte neu planen.")
                .arg(currentCommit.left(12), m_boundCommit.left(12));
            emit eventEmitted(LogLine{LogLevel::Error, QStringLiteral("flatpak"), err});
            emit eventEmitted(TransactionDone{Result::Failed, err, false, {}, 0});
            return;
        }

        emit eventEmitted(PhaseChanged{Phase::Commit, QStringLiteral("Flatpak wird installiert …"), false, false});
        emit eventEmitted(ItemStarted{m_targetRef, PackageOp::Kind::Install, 100});

        QString installOut, installErr;
        const QStringList installArgs = {
            QStringLiteral("install"),
            QStringLiteral("--system"),
            QStringLiteral("-y"),
            QStringLiteral("--noninteractive"),
            m_targetRepo,
            m_targetRef
        };

        rc = runCommand(installArgs, installOut, installErr);
        if (rc == 0) {
            emit eventEmitted(ItemFinished{m_targetRef, true, QString()});
            emit eventEmitted(PhaseChanged{Phase::Finished, QStringLiteral("Installation abgeschlossen"), false, false});
            emit eventEmitted(TransactionDone{Result::Success, QStringLiteral("Flatpak-Installation erfolgreich"), false, {}, 0});
        } else {
            emit eventEmitted(ItemFinished{m_targetRef, false, installErr});
            emit eventEmitted(PhaseChanged{Phase::Failed, QStringLiteral("Installation fehlgeschlagen"), false, false});
            emit eventEmitted(LogLine{LogLevel::Error, QStringLiteral("flatpak"), installErr});
            emit eventEmitted(TransactionDone{Result::Failed, installErr.isEmpty() ? QStringLiteral("Flatpak-Installation fehlgeschlagen.") : installErr, false, {}, 0});
        }

    } else if (m_currentIntent.type == TransactionIntent::Type::Remove) {
        emit eventEmitted(PhaseChanged{Phase::Commit, QStringLiteral("Flatpak wird deinstalliert …"), false, false});
        emit eventEmitted(ItemStarted{m_targetRef, PackageOp::Kind::Remove, 100});

        QString removeOut, removeErr;
        // Section 6.3: Do NOT use --unused automatically
        const QStringList removeArgs = {
            QStringLiteral("uninstall"),
            QStringLiteral("--system"),
            QStringLiteral("-y"),
            QStringLiteral("--noninteractive"),
            m_targetRef
        };

        int rc = runCommand(removeArgs, removeOut, removeErr);
        if (rc == 0) {
            emit eventEmitted(ItemFinished{m_targetRef, true, QString()});
            emit eventEmitted(PhaseChanged{Phase::Finished, QStringLiteral("Deinstallation abgeschlossen"), false, false});
            emit eventEmitted(TransactionDone{Result::Success, QStringLiteral("Flatpak-Deinstallation erfolgreich"), false, {}, 0});
        } else {
            emit eventEmitted(ItemFinished{m_targetRef, false, removeErr});
            emit eventEmitted(PhaseChanged{Phase::Failed, QStringLiteral("Deinstallation fehlgeschlagen"), false, false});
            emit eventEmitted(LogLine{LogLevel::Error, QStringLiteral("flatpak"), removeErr});
            emit eventEmitted(TransactionDone{Result::Failed, removeErr.isEmpty() ? QStringLiteral("Flatpak-Deinstallation fehlgeschlagen.") : removeErr, false, {}, 0});
        }
    }
}

void FlatpakBackend::discardPlan()
{
    m_currentPlan = TransactionPlan();
    m_currentIntent = TransactionIntent();
    m_boundCommit.clear();
    m_targetRef.clear();
    m_targetRepo.clear();
    emit eventEmitted(PhaseChanged{Phase::Cancelled, QStringLiteral("Plan verworfen"), false, false});
    emit eventEmitted(TransactionDone{Result::Cancelled, QStringLiteral("Plan verworfen"), false, {}, 0});
}

void FlatpakBackend::cancel()
{
    m_cancelled = true;
    if (m_activeProcess && m_activeProcess->state() != QProcess::NotRunning) {
        m_activeProcess->terminate();
        if (!m_activeProcess->waitForFinished(3000)) {
            m_activeProcess->kill();
        }
    }
    emit eventEmitted(PhaseChanged{Phase::Cancelled, QStringLiteral("Transaktion abgebrochen"), false, false});
    emit eventEmitted(TransactionDone{Result::Cancelled, QStringLiteral("Transaktion abgebrochen"), false, {}, 0});
}

void FlatpakBackend::answerQuestion(const QString &id, const QJsonObject &answer)
{
    Q_UNUSED(id);
    Q_UNUSED(answer);
}

QList<PackageOp> FlatpakBackend::availableUpdates()
{
    QString stdoutOut, stderrOut;
    int rc = runCommand({QStringLiteral("remote-ls"), QStringLiteral("--system"), QStringLiteral("--app"), QStringLiteral("--updates"), QStringLiteral("--columns=application:f,origin:f,version:f,ref:f,download-size:f")}, stdoutOut, stderrOut);
    if (rc != 0) {
        return {};
    }
    return parseUpdates(stdoutOut);
}

QList<PackageOp> FlatpakBackend::parseUpdates(const QString &remoteLsOutput)
{
    QList<PackageOp> ops;
    const QStringList lines = remoteLsOutput.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        const QStringList parts = trimmed.split(QLatin1Char('\t'));
        if (parts.size() < 4) continue;

        const QString app = parts.at(0).trimmed();
        const QString origin = parts.at(1).trimmed();
        const QString version = parts.at(2).trimmed();
        const QString ref = parts.at(3).trimmed();
        qint64 dlSize = 0;
        if (parts.size() >= 5) {
            dlSize = parts.at(4).trimmed().toLongLong();
        }

        PackageOp op;
        op.name = app;
        op.newVersion = version;
        op.repo = origin;
        op.summary = ref;
        op.downloadSize = dlSize;
        op.kind = PackageOp::Kind::Upgrade;
        ops.append(op);
    }
    return ops;
}

QList<InstalledPackage> FlatpakBackend::installedPackages(const QString &query)
{
    Q_UNUSED(query);
    return {};
}

QList<ChangelogEntry> FlatpakBackend::changelog(const QString &pkgId)
{
    Q_UNUSED(pkgId);
    return {};
}

QList<HistoryEntry> FlatpakBackend::history(int limit)
{
    QString stdoutOut, stderrOut;
    int rc = runCommand({QStringLiteral("history"), QStringLiteral("-j"), QStringLiteral("--reverse")}, stdoutOut, stderrOut);
    if (rc != 0) {
        return {};
    }
    return parseHistoryJson(stdoutOut.toUtf8(), limit);
}

QList<HistoryEntry> FlatpakBackend::parseHistoryJson(const QByteArray &json, int limit)
{
    QList<HistoryEntry> result;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        return result;
    }

    const QJsonArray arr = doc.array();
    for (int i = 0; i < arr.size() && result.size() < limit; ++i) {
        const QJsonObject obj = arr.at(i).toObject();
        HistoryEntry entry;
        entry.id = i + 1;

        const QString timeStr = obj.value(QStringLiteral("time")).toString();
        entry.timestamp = QDateTime::fromString(timeStr, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (!entry.timestamp.isValid()) {
            entry.timestamp = QDateTime::fromString(timeStr, Qt::ISODate);
        }

        const QString change = obj.value(QStringLiteral("change")).toString();
        QString app = obj.value(QStringLiteral("application")).toString();
        if (app.isEmpty()) {
            app = obj.value(QStringLiteral("ref")).toString();
        }

        entry.command = QStringLiteral("[Flatpak] %1 %2").arg(change, app).trimmed();
        entry.result = QStringLiteral("Success");
        entry.packagesAltered = 1;
        entry.canUndo = false;
        result.append(entry);
    }
    return result;
}

} // namespace lut
