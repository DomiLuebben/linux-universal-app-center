#include <QDBusReply>
#include "DaemonClient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QRegularExpression>

namespace lut {

DaemonClient::DaemonClient(QObject *parent)
    : QObject(parent) {
    connect(&m_progressModel, &ProgressModel::logAdded, &m_logModel, &LogModel::appendLog);
    connect(&m_progressModel, &ProgressModel::questionReceived, this, &DaemonClient::questionPrompt);
    connect(&m_installedModel, &InstalledModel::cleanupRequested, this, [this](const QString &command) {
        if (command == QLatin1String("clean")) cleanCache();
        else planDnf5(command, QString());
    });
    connect(&m_historyModel, &HistoryModel::undoRequested, this, [this](int id) { planDnf5(QStringLiteral("history undo"), QString::number(id)); });
}

DaemonClient::~DaemonClient() = default;

bool DaemonClient::init(const QString &replayFixture, double replaySpeed) {
    if (!replayFixture.isEmpty()) {
        qInfo() << "DaemonClient: Running in REPLAY mode with fixture:" << replayFixture;
        m_replayBackend = std::make_unique<ReplayBackend>(replayFixture, replaySpeed, this);
        connect(m_replayBackend.get(), &Backend::eventEmitted, this, &DaemonClient::handleEvent);
        m_capabilities = m_replayBackend->capabilities();
        m_lastCheckedString = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm"));
        emit modeChanged();
        emit capabilitiesChanged();
        emit connectionChanged();
        return true;
    }

    connectToDaemon();
    return isConnected();
}

void DaemonClient::connectToDaemon() {
    auto bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        bus = QDBusConnection::sessionBus();
    }

    m_daemonIface = std::make_unique<QDBusInterface>(
        QStringLiteral("org.linuxupdatetool.Daemon1"),
        QStringLiteral("/org/linuxupdatetool/Daemon1"),
        QStringLiteral("org.linuxupdatetool.Daemon1"),
        bus,
        this
    );

    if (m_daemonIface->isValid()) {
        m_connected = true;
        bus.connect(
            QStringLiteral("org.linuxupdatetool.Daemon1"),
            QStringLiteral("/org/linuxupdatetool/Daemon1"),
            QStringLiteral("org.linuxupdatetool.Daemon1"),
            QStringLiteral("TransactionEvent"),
            this,
            SLOT(onDbusTransactionEvent(QDBusObjectPath, QString))
        );
        queryCapabilities();
        emit connectionChanged();
    } else {
        qWarning() << "DaemonClient: Could not connect to lutd on D-Bus:" << m_daemonIface->lastError().message();
    }
}

void DaemonClient::queryCapabilities() {
    if (!m_daemonIface || !m_daemonIface->isValid()) return;
    QDBusReply<QString> reply = m_daemonIface->call(QStringLiteral("GetCapabilities"));
    if (reply.isValid()) {
        QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            m_capabilities.partialUpgrade = obj.value(QStringLiteral("partialUpgrade")).toBool(true);
            m_capabilities.downgrade = obj.value(QStringLiteral("downgrade")).toBool();
            m_capabilities.historyUndo = obj.value(QStringLiteral("historyUndo")).toBool();
            m_capabilities.changelogs = obj.value(QStringLiteral("changelogs")).toBool();
            m_capabilities.securityFlag = obj.value(QStringLiteral("securityFlag")).toBool();
            m_capabilities.offlineUpdate = obj.value(QStringLiteral("offlineUpdate")).toBool();
            m_capabilities.autoremove = obj.value(QStringLiteral("autoremove")).toBool();
            m_capabilities.parallelDownloads = obj.value(QStringLiteral("parallelDownloads")).toBool();
            m_capabilities.degraded = obj.value(QStringLiteral("degraded")).toBool();
            m_dnf5Commands.clear();
            for (const auto &command : obj.value(QStringLiteral("dnf5Commands")).toArray()) m_dnf5Commands.append(command.toString());
            emit capabilitiesChanged();
        }
    }
}

void DaemonClient::onDbusTransactionEvent(const QDBusObjectPath &path, const QString &jsonStr) {
    if (!m_activeTransactionPath.path().isEmpty() && m_activeTransactionPath.path() != path.path()) return;
    m_activeTransactionPath = path;
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    if (doc.isObject()) {
        auto ev = deserializeEvent(doc.object());
        if (ev.has_value()) {
            handleEvent(*ev);
        }
    }
}

void DaemonClient::handleEvent(const Event &event) {
    if (std::holds_alternative<PlanReady>(event)) {
        const auto &plan = std::get<PlanReady>(event);
        m_updatesModel.setPackages(plan.ops);
        m_busy = false; m_hasPlan = true;
        m_statusMessage = plan.warnings.join(QLatin1Char(' '));
        m_lastCheckedString = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm"));
        emit statusChanged();
    } else if (std::holds_alternative<LogLine>(event)) {
        m_logModel.appendLog(std::get<LogLine>(event));
    } else if (std::holds_alternative<PhaseChanged>(event)) {
        const auto &phase = std::get<PhaseChanged>(event);
        LogLevel l = (phase.phase == Phase::Failed) ? LogLevel::Error : LogLevel::Info;
        m_logModel.appendLog(LogLine{l, QStringLiteral("Phase"), phase.label});
    } else if (std::holds_alternative<TransactionDone>(event)) {
        const auto &done = std::get<TransactionDone>(event);
        m_busy = false; m_hasPlan = false; m_statusMessage = done.summary;
        if (done.result == Result::Success) {
            QTimer::singleShot(0, &m_installedModel, &InstalledModel::refresh);
            QTimer::singleShot(0, &m_historyModel, &HistoryModel::refresh);
        }
        emit statusChanged();
        LogLevel l = (done.result == Result::Success) ? LogLevel::Info : (done.result == Result::Cancelled ? LogLevel::Warning : LogLevel::Error);
        m_logModel.appendLog(LogLine{l, QStringLiteral("Ergebnis"), done.summary});
    } else if (std::holds_alternative<ScriptletStarted>(event)) {
        const auto &sc = std::get<ScriptletStarted>(event);
        m_logModel.appendLog(LogLine{LogLevel::Info, sc.scriptletName, sc.pkgId});
    } else if (std::holds_alternative<ItemStarted>(event)) {
        const auto &item = std::get<ItemStarted>(event);
        m_logModel.appendLog(LogLine{LogLevel::Info, QStringLiteral("Paket"), item.pkgId});
    }
    m_progressModel.processEvent(event);
}

void DaemonClient::refreshUpdates() {
    if (m_busy) return;
    m_hasPlan = false; m_busy = true; m_isUpgradePlan = true; m_statusMessage = QStringLiteral("Aktualisierungen werden vorbereitet …");
    m_activeTransactionPath = {}; m_progressModel.reset(); emit statusChanged();
    if (m_replayBackend) {
        m_replayBackend->refreshMetadata();
        return;
    }
    if (m_daemonIface && m_daemonIface->isValid()) {
        QVariantMap opts;
        opts[QStringLiteral("includeSecurityOnly")] = false;
        opts[QStringLiteral("refreshFirst")] = true;
        QDBusReply<QDBusObjectPath> reply = m_daemonIface->call(QStringLiteral("PlanUpgrade"), opts);
        if (reply.isValid()) {
            m_activeTransactionPath = reply.value();
        } else {
            reportError(reply.error().message());
        }
    } else reportError(QStringLiteral("Keine Verbindung zum Systemdienst."));
}

void DaemonClient::reportError(const QString &message) {
    m_busy = false; m_hasPlan = false; m_statusMessage = message;
    m_logModel.appendLog(LogLine{LogLevel::Error, QStringLiteral("lutd"), message});
    emit statusChanged();
}

void DaemonClient::planDnf5(const QString &command, const QString &arguments, bool securityOnly, bool excludeKernel) {
    if (m_busy) return;
    if (m_replayBackend || !m_daemonIface || !m_dnf5Commands.contains(command)) { reportError(QStringLiteral("Diese DNF5-Aktion ist nicht verfügbar.")); return; }
    const QStringList names = arguments.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    QVariantMap options;
    options[QStringLiteral("includeSecurityOnly")] = securityOnly;
    options[QStringLiteral("excludeKernel")] = excludeKernel;
    m_hasPlan = false; m_busy = true; m_isUpgradePlan = false;
    m_statusMessage = QStringLiteral("Transaktion wird vorbereitet …");
    m_activeTransactionPath = {}; m_progressModel.reset(); emit statusChanged();
    QDBusReply<QDBusObjectPath> reply = m_daemonIface->call(QStringLiteral("PlanDnf5"), command, names, options);
    if (reply.isValid()) m_activeTransactionPath = reply.value();
    else reportError(reply.error().message());
}

void DaemonClient::cleanCache() {
    if (m_busy) return;
    if (m_replayBackend || !m_daemonIface) { reportError(QStringLiteral("Keine Verbindung zum Systemdienst.")); return; }
    m_busy = true; m_hasPlan = false; m_activeTransactionPath = {};
    m_statusMessage = QStringLiteral("Paketcache wird geleert …"); emit statusChanged();
    QDBusReply<QDBusObjectPath> reply = m_daemonIface->call(QStringLiteral("CleanCache"));
    if (reply.isValid()) m_activeTransactionPath = reply.value();
    else reportError(reply.error().message());
}

void DaemonClient::startUpgrade() {
    if (m_busy) return;
    if (m_replayBackend) { emit transactionStarted(); m_replayBackend->commit(); return; }
    if (!m_hasPlan || !m_daemonIface) { reportError(QStringLiteral("Bitte zunächst einen Plan erstellen und prüfen.")); return; }
    if (m_updatesModel.selectedCount() != m_updatesModel.totalCount()) {
        if (!m_isUpgradePlan) { m_statusMessage = QStringLiteral("Dieser aufgelöste Plan wird vollständig ausgeführt. Bitte alle Einträge auswählen oder einen neuen Plan erstellen."); emit statusChanged(); return; }
        QStringList packages;
        for (const auto &op : m_updatesModel.selectedPackages())
            if (op.kind != PackageOp::Kind::Remove) packages.append(m_dnf5Commands.isEmpty() ? op.name : op.name + QLatin1Char('.') + op.arch);
        packages.removeDuplicates();
        if (packages.isEmpty()) return;
        QVariantMap options;
        options[QStringLiteral("packages")] = packages;
        options[QStringLiteral("refreshFirst")] = false;
        m_busy = true; m_hasPlan = false; m_activeTransactionPath = {};
        m_statusMessage = QStringLiteral("Die Auswahl wird neu aufgelöst. Anschließend den neuen Plan prüfen und bestätigen."); emit statusChanged();
        QDBusReply<QDBusObjectPath> reply = m_daemonIface->call(QStringLiteral("PlanUpgrade"), options);
        if (reply.isValid()) m_activeTransactionPath = reply.value(); else reportError(reply.error().message());
        return;
    }
    QDBusReply<void> reply = m_daemonIface->call(QStringLiteral("Commit"), QVariant::fromValue(m_activeTransactionPath));
    if (!reply.isValid()) { reportError(reply.error().message()); return; }
    m_busy = true; m_hasPlan = false; emit statusChanged(); emit transactionStarted();
}

void DaemonClient::cancelTransaction() {
    if (m_replayBackend) {
        m_replayBackend->cancel();
        return;
    }
    if (m_daemonIface && m_daemonIface->isValid() && !m_activeTransactionPath.path().isEmpty()) {
        m_daemonIface->call(QStringLiteral("Cancel"), QVariant::fromValue(m_activeTransactionPath));
    }
}

void DaemonClient::answerQuestion(const QString &id, const QJsonObject &answer) {
    if (m_replayBackend) {
        m_replayBackend->answerQuestion(id, answer);
        return;
    }
    if (m_daemonIface && m_daemonIface->isValid() && !m_activeTransactionPath.path().isEmpty()) {
        QString jsonStr = QString::fromUtf8(QJsonDocument(answer).toJson(QJsonDocument::Compact));
        m_daemonIface->call(QStringLiteral("AnswerQuestion"), QVariant::fromValue(m_activeTransactionPath), id, jsonStr);
    }
}

} // namespace lut
