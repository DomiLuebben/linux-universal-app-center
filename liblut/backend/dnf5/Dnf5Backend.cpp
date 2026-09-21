#include "Dnf5Backend.h"
#include <QDBusMessage>
#include <QDBusReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

namespace lut {

Dnf5Backend::Dnf5Backend(QObject *parent)
    : Backend(parent) {
    m_usingDaemon = connectToDnf5Daemon();
}

Dnf5Backend::~Dnf5Backend() {
    if (m_cliProcess && m_cliProcess->state() != QProcess::NotRunning) {
        m_cliProcess->kill();
        m_cliProcess->waitForFinished(1000);
    }
}

Capabilities Dnf5Backend::capabilities() const {
    Capabilities cap;
    cap.partialUpgrade = true;
    cap.downgrade = true;
    cap.historyUndo = true;
    cap.changelogs = true;
    cap.securityFlag = true;
    cap.offlineUpdate = true;
    cap.autoremove = true;
    cap.parallelDownloads = true;
    cap.degraded = !m_usingDaemon;
    return cap;
}

bool Dnf5Backend::connectToDnf5Daemon() {
    auto bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        return false;
    }

    QDBusInterface sessionManager(
        QStringLiteral("org.rpm.dnf.v0"),
        QStringLiteral("/org/rpm/dnf/v0"),
        QStringLiteral("org.rpm.dnf.v0.SessionManager"),
        bus
    );

    return sessionManager.isValid();
}

void Dnf5Backend::refreshMetadata() {
    emit eventEmitted(PhaseChanged{Phase::RefreshMetadata, QStringLiteral("Metadaten aktualisieren"), true});
    // Lesender Aufruf über dnf5 check-upgrade / repo refresh
    QProcess proc;
    proc.start(QStringLiteral("dnf5"), {QStringLiteral("makecache")});
    proc.waitForFinished(60000);
    emit eventEmitted(PhaseChanged{Phase::Idle, QStringLiteral("Bereit"), false});
}

void Dnf5Backend::planUpgradeAll(const UpgradeOptions &options) {
    emit eventEmitted(PhaseChanged{Phase::Resolve, QStringLiteral("Abhängigkeiten prüfen"), true});

    // Abfrage der verfügbaren Updates als JSON
    QProcess proc;
    QStringList args = {QStringLiteral("check-upgrade"), QStringLiteral("--json")};
    if (options.includeSecurityOnly) {
        args.append(QStringLiteral("--security"));
    }
    if (options.excludeKernel) {
        args << QStringLiteral("--exclude=kernel*") << QStringLiteral("--exclude=linux*");
    }

    proc.start(QStringLiteral("dnf5"), args);
    proc.waitForFinished(30000);

    QByteArray out = proc.readAllStandardOutput();
    QJsonDocument doc = QJsonDocument::fromJson(out);
    QList<PackageOp> ops;
    qint64 totalDownload = 0;
    qint64 totalInstallDelta = 0;

    if (doc.isObject()) {
        QJsonArray pkgs = doc.object().value(QStringLiteral("upgrade")).toArray();
        for (const auto &val : pkgs) {
            QJsonObject obj = val.toObject();
            PackageOp op;
            op.name = obj.value(QStringLiteral("name")).toString();
            op.version = obj.value(QStringLiteral("evr")).toString();
            op.newVersion = obj.value(QStringLiteral("new_evr")).toString();
            op.arch = obj.value(QStringLiteral("arch")).toString();
            op.repo = obj.value(QStringLiteral("repo")).toString();
            op.summary = obj.value(QStringLiteral("summary")).toString();
            op.id = QStringLiteral("%1-%2.%3").arg(op.name, op.newVersion, op.arch);
            op.downloadSize = obj.value(QStringLiteral("download_size")).toInteger();
            op.installedSize = obj.value(QStringLiteral("installed_size")).toInteger();
            op.isSecurity = obj.value(QStringLiteral("security")).toBool();
            op.isKernel = PackageOp::detectIsKernel(op.name);
            op.kind = PackageOp::Kind::Upgrade;

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

void Dnf5Backend::planInstall(const QStringList &) {}
void Dnf5Backend::planRemove(const QStringList &) {}

void Dnf5Backend::commit() {
    emit eventEmitted(PhaseChanged{Phase::Download, QStringLiteral("Pakete herunterladen"), true});

    // Subprozess dnf5 upgrade -y
    if (!m_cliProcess) {
        m_cliProcess = new QProcess(this);
        connect(m_cliProcess, &QProcess::readyReadStandardOutput, this, [this]() {
            while (m_cliProcess->canReadLine()) {
                QString line = QString::fromUtf8(m_cliProcess->readLine()).trimmed();
                onDnfTransactionProgress(line);
            }
        });
        connect(m_cliProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int exitCode, QProcess::ExitStatus) {
            if (exitCode == 0) {
                emit eventEmitted(PhaseChanged{Phase::Cleanup, QStringLiteral("Aufräumen"), false});
                emit eventEmitted(TransactionDone{Result::Success, QStringLiteral("Aktualisierung erfolgreich"), false, {}, 0});
            } else {
                emit eventEmitted(PhaseChanged{Phase::Failed, QStringLiteral("Fehlgeschlagen"), false});
                emit eventEmitted(TransactionDone{Result::Failed, QStringLiteral("DNF5-Prozess beendet mit Exit-Code %1").arg(exitCode), false, {}, 0});
            }
        });
    }

    m_cliProcess->start(QStringLiteral("dnf5"), {QStringLiteral("upgrade"), QStringLiteral("-y")});
}

void Dnf5Backend::cancel() {
    if (m_cliProcess && m_cliProcess->state() != QProcess::NotRunning) {
        m_cliProcess->terminate();
    }
    emit eventEmitted(PhaseChanged{Phase::Cancelled, QStringLiteral("Abgebrochen"), false});
    emit eventEmitted(TransactionDone{Result::Cancelled, QStringLiteral("Vom Benutzer abgebrochen"), false, {}, 0});
}

void Dnf5Backend::answerQuestion(const QString &, const QJsonObject &) {}

void Dnf5Backend::onDnfDownloadProgress(const QString &) {}

void Dnf5Backend::onDnfTransactionProgress(const QString &line) {
    emit eventEmitted(LogLine{LogLevel::Info, QStringLiteral("dnf5"), line});
    if (line.contains(QLatin1String("Installing")) || line.contains(QLatin1String("Upgrading"))) {
        emit eventEmitted(PhaseChanged{Phase::Commit, QStringLiteral("Pakete installieren"), false});
    } else if (line.contains(QLatin1String("Running scriptlet")) || line.contains(QLatin1String("dracut"))) {
        emit eventEmitted(PhaseChanged{Phase::PostTransaction, QStringLiteral("Systemskripte ausführen"), false});
    }
}

QList<PackageOp> Dnf5Backend::availableUpdates() {
    return m_plannedOps;
}

QList<InstalledPackage> Dnf5Backend::installedPackages(const QString &) {
    return {};
}

QList<ChangelogEntry> Dnf5Backend::changelog(const QString &) {
    return {};
}

QList<HistoryEntry> Dnf5Backend::history(int) {
    return {};
}

} // namespace lut
