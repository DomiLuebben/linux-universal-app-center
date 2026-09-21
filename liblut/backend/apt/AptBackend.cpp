#include "AptBackend.h"
#include <QRegularExpression>
#include <QDebug>

namespace lut {

AptBackend::AptBackend(QObject *parent)
    : Backend(parent) {}

AptBackend::~AptBackend() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

Capabilities AptBackend::capabilities() const {
    Capabilities cap;
    cap.partialUpgrade = true;
    cap.downgrade = true;
    cap.historyUndo = false;
    cap.changelogs = true;
    cap.securityFlag = true;
    cap.offlineUpdate = false;
    cap.autoremove = true;
    cap.parallelDownloads = true;
    return cap;
}

void AptBackend::refreshMetadata() {
    emit eventEmitted(PhaseChanged{Phase::RefreshMetadata, QStringLiteral("Paketlisten aktualisieren (apt update)"), true});

    QProcess proc;
    proc.start(QStringLiteral("apt-get"), {QStringLiteral("update"), QStringLiteral("-q")});
    proc.waitForFinished(60000);

    emit eventEmitted(PhaseChanged{Phase::Idle, QStringLiteral("Bereit"), false});
}

void AptBackend::planUpgradeAll(const UpgradeOptions &) {
    emit eventEmitted(PhaseChanged{Phase::Resolve, QStringLiteral("Abhängigkeiten berechnen (apt dist-upgrade --simulate)"), true});

    QProcess proc;
    proc.start(QStringLiteral("apt-get"), {
        QStringLiteral("-s"),
        QStringLiteral("-o"), QStringLiteral("APT::Status-Fd=1"),
        QStringLiteral("dist-upgrade")
    });
    proc.waitForFinished(30000);

    QList<PackageOp> ops;
    qint64 totalDownload = 0;
    qint64 totalInstallDelta = 0;

    // Simulationsausgabe parsen:
    // Inst <name> [<current_version>] (<new_version> <distribution> [<arch>])
    static const QRegularExpression re(QStringLiteral(R"(^Inst\s+([^\s]+)\s+\[([^\]]*)\]\s+\(([^\s]+)\s+([^\s]+)\s+\[([^\]]+)\]\))"));

    while (proc.canReadLine()) {
        QString line = QString::fromUtf8(proc.readLine()).trimmed();
        auto match = re.match(line);
        if (match.hasMatch()) {
            PackageOp op;
            op.name = match.captured(1);
            op.version = match.captured(2);
            op.newVersion = match.captured(3);
            op.repo = match.captured(4);
            op.arch = match.captured(5);
            op.id = QStringLiteral("%1_%2_%3").arg(op.name, op.newVersion, op.arch);
            op.isSecurity = op.repo.contains(QLatin1String("security"), Qt::CaseInsensitive);
            op.isKernel = PackageOp::detectIsKernel(op.name);
            op.kind = PackageOp::Kind::Upgrade;
            op.downloadSize = 5 * 1024 * 1024; // Schätzung bis genaue Werte gelesen sind
            op.installedSize = 15 * 1024 * 1024;

            totalDownload += op.downloadSize;
            totalInstallDelta += op.installedSize;
            ops.append(op);
        }
    }

    m_plannedOps = ops;

    PlanReady plan;
    plan.ops = ops;
    plan.downloadBytes = totalDownload;
    plan.installedSizeDelta = totalInstallDelta;

    emit eventEmitted(plan);
    emit eventEmitted(PhaseChanged{Phase::Idle, QStringLiteral("Plan fertig"), true});
}

void AptBackend::planInstall(const QStringList &) {}
void AptBackend::planRemove(const QStringList &) {}

void AptBackend::commit() {
    emit eventEmitted(PhaseChanged{Phase::Download, QStringLiteral("Pakete herunterladen"), true});

    if (!m_process) {
        m_process = new QProcess(this);
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
            while (m_process->canReadLine()) {
                handleStatusFdLine(QString::fromUtf8(m_process->readLine()).trimmed());
            }
        });
        connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int exitCode, QProcess::ExitStatus) {
            if (exitCode == 0) {
                emit eventEmitted(PhaseChanged{Phase::Cleanup, QStringLiteral("Aufräumen"), false});
                emit eventEmitted(TransactionDone{Result::Success, QStringLiteral("Systemaktualisierung erfolgreich abgeschlossen"), false, {}, 0});
            } else {
                emit eventEmitted(PhaseChanged{Phase::Failed, QStringLiteral("Fehlgeschlagen"), false});
                emit eventEmitted(TransactionDone{Result::Failed, QStringLiteral("Apt beendet mit Code %1").arg(exitCode), false, {}, 0});
            }
        });
    }

    // APT mit Status-Fd=1 für direkte Prozess-Pipe
    m_process->start(QStringLiteral("apt-get"), {
        QStringLiteral("-o"), QStringLiteral("APT::Status-Fd=1"),
        QStringLiteral("-o"), QStringLiteral("Dpkg::Use-Pty=0"),
        QStringLiteral("-q"),
        QStringLiteral("-y"),
        QStringLiteral("dist-upgrade")
    });
}

void AptBackend::cancel() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
    }
    emit eventEmitted(PhaseChanged{Phase::Cancelled, QStringLiteral("Abgebrochen"), false});
    emit eventEmitted(TransactionDone{Result::Cancelled, QStringLiteral("Durch Benutzer abgebrochen"), false, {}, 0});
}

void AptBackend::answerQuestion(const QString &, const QJsonObject &) {}

void AptBackend::handleStatusFdLine(const QString &line) {
    auto parsed = StatusFdParser::parseLine(line);
    if (!parsed.has_value()) {
        emit eventEmitted(LogLine{LogLevel::Info, QStringLiteral("apt"), line});
        return;
    }

    auto ev = StatusFdParser::toEvent(*parsed);
    if (ev.has_value()) {
        emit eventEmitted(*ev);
    }
}

QList<PackageOp> AptBackend::availableUpdates() {
    return m_plannedOps;
}

QList<InstalledPackage> AptBackend::installedPackages(const QString &) {
    return {};
}

QList<ChangelogEntry> AptBackend::changelog(const QString &) {
    return {};
}

QList<HistoryEntry> AptBackend::history(int) {
    return {};
}

} // namespace lut
