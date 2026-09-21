#include "AlpmBackend.h"
#include <QRegularExpression>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>

namespace lut {

AlpmBackend::AlpmBackend(QObject *parent)
    : Backend(parent) {}

AlpmBackend::~AlpmBackend() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

Capabilities AlpmBackend::capabilities() const {
    Capabilities cap;
    cap.partialUpgrade = false; // Auf Arch/Pacman AUSDRÜCKLICH verboten!
    cap.downgrade = false;
    cap.historyUndo = false;
    cap.changelogs = false;
    cap.securityFlag = false;
    cap.offlineUpdate = false;
    cap.autoremove = true;
    cap.parallelDownloads = true;
    return cap;
}

void AlpmBackend::refreshMetadata() {
    emit eventEmitted(PhaseChanged{Phase::RefreshMetadata, QStringLiteral("Synchronisiere Paketdatenbanken"), true});

    // Checkupdates-Prinzip: Temporäre Datenbank synchronisieren ohne Root
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/alpm-db");
    QDir().mkpath(cacheDir);

    QProcess proc;
    proc.start(QStringLiteral("checkupdates"), {QStringLiteral("-d")});
    if (!proc.waitForStarted(2000)) {
        // Fallback pacman -Sy mit temp dbpath falls checkupdates nicht installiert
        proc.start(QStringLiteral("pacman"), {QStringLiteral("-Sy"), QStringLiteral("--dbpath"), cacheDir});
    }
    proc.waitForFinished(60000);

    emit eventEmitted(PhaseChanged{Phase::Idle, QStringLiteral("Bereit"), false});
}

void AlpmBackend::planUpgradeAll(const UpgradeOptions &) {
    emit eventEmitted(PhaseChanged{Phase::Resolve, QStringLiteral("Prüfe Systemaktualisierungen"), true});

    QProcess proc;
    // checkupdates oder pacman -Qu
    proc.start(QStringLiteral("checkupdates"));
    if (!proc.waitForStarted(2000)) {
        proc.start(QStringLiteral("pacman"), {QStringLiteral("-Qu")});
    }
    proc.waitForFinished(30000);

    QList<PackageOp> ops;
    qint64 totalDownload = 0;
    qint64 totalInstalled = 0;

    // Format von pacman -Qu / checkupdates:
    // name alt -> neu [repo]
    // z.B.: linux 6.17.3-arch1 -> 6.17.4-arch1
    QRegularExpression re(QStringLiteral(R"(^([a-zA-Z0-9._+-]+)\s+([^\s]+)\s+->\s+([^\s]+))"));

    while (proc.canReadLine()) {
        QString line = QString::fromUtf8(proc.readLine()).trimmed();
        auto match = re.match(line);
        if (match.hasMatch()) {
            PackageOp op;
            op.name = match.captured(1);
            op.version = match.captured(2);
            op.newVersion = match.captured(3);
            op.id = QStringLiteral("%1-%2").arg(op.name, op.newVersion);
            op.kind = PackageOp::Kind::Upgrade;
            op.isKernel = PackageOp::detectIsKernel(op.name);

            // Typische Schätzgrößen falls keine Metadaten vorhanden
            op.downloadSize = op.isKernel ? (75 * 1024 * 1024) : (5 * 1024 * 1024);
            op.installedSize = op.isKernel ? (150 * 1024 * 1024) : (15 * 1024 * 1024);

            totalDownload += op.downloadSize;
            totalInstalled += op.installedSize;
            ops.append(op);
        }
    }

    m_plannedOps = ops;

    PlanReady plan;
    plan.ops = ops;
    plan.downloadBytes = totalDownload;
    plan.installedSizeDelta = totalInstalled;
    if (!capabilities().partialUpgrade) {
        plan.warnings.append(QStringLiteral("Arch Linux unterstützt nur vollständige Systemaktualisierungen."));
    }

    emit eventEmitted(plan);
    emit eventEmitted(PhaseChanged{Phase::Idle, QStringLiteral("Aktualisierungsplan bereit"), true});
}

void AlpmBackend::planInstall(const QStringList &) {}
void AlpmBackend::planRemove(const QStringList &) {}

void AlpmBackend::commit() {
    emit eventEmitted(PhaseChanged{Phase::Download, QStringLiteral("Pakete holen"), true});

    if (!m_process) {
        m_process = new QProcess(this);
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
            while (m_process->canReadLine()) {
                parsePacmanOutput(QString::fromUtf8(m_process->readLine()).trimmed());
            }
        });
        connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int exitCode, QProcess::ExitStatus) {
            if (exitCode == 0) {
                emit eventEmitted(PhaseChanged{Phase::Cleanup, QStringLiteral("Aufräumen"), false});
                emit eventEmitted(TransactionDone{Result::Success, QStringLiteral("System erfolgreich aktualisiert"), false, {}, 0});
            } else {
                emit eventEmitted(PhaseChanged{Phase::Failed, QStringLiteral("Fehlgeschlagen"), false});
                emit eventEmitted(TransactionDone{Result::Failed, QStringLiteral("Pacman beendet mit Code %1").arg(exitCode), false, {}, 0});
            }
        });
    }

    m_process->start(QStringLiteral("pacman"), {QStringLiteral("-Syu"), QStringLiteral("--noconfirm")});
}

void AlpmBackend::cancel() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
    }
    emit eventEmitted(PhaseChanged{Phase::Cancelled, QStringLiteral("Abgebrochen"), false});
    emit eventEmitted(TransactionDone{Result::Cancelled, QStringLiteral("Vom Benutzer abgebrochen"), false, {}, 0});
}

void AlpmBackend::answerQuestion(const QString &, const QJsonObject &) {}

void AlpmBackend::parsePacmanOutput(const QString &line) {
    emit eventEmitted(LogLine{LogLevel::Info, QStringLiteral("pacman"), line});

    if (line.contains(QLatin1String("downloading")) || line.contains(QLatin1String("Lade Pakete"))) {
        emit eventEmitted(PhaseChanged{Phase::Download, QStringLiteral("Pakete herunterladen"), true});
    } else if (line.contains(QLatin1String("checking keyring")) || line.contains(QLatin1String("checking package integrity"))) {
        emit eventEmitted(PhaseChanged{Phase::Verify, QStringLiteral("Signaturen prüfen"), true});
    } else if (line.contains(QLatin1String("upgrading")) || line.contains(QLatin1String("installing"))) {
        emit eventEmitted(PhaseChanged{Phase::Commit, QStringLiteral("Pakete installieren"), false});
    } else if (line.contains(QLatin1String("Running post-transaction hooks")) || line.contains(QLatin1String("Arming ConditionNeedsUpdate"))) {
        emit eventEmitted(PhaseChanged{Phase::PostTransaction, QStringLiteral("ALPM-Hooks ausführen"), false});
    }
}

QList<PackageOp> AlpmBackend::availableUpdates() {
    return m_plannedOps;
}

QList<InstalledPackage> AlpmBackend::installedPackages(const QString &) {
    return {};
}

QList<ChangelogEntry> AlpmBackend::changelog(const QString &) {
    return {};
}

QList<HistoryEntry> AlpmBackend::history(int) {
    return {};
}

} // namespace lut
