#include <QDBusReply>
#include "DaemonClient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QDebug>

namespace lut {

DaemonClient::DaemonClient(QObject *parent)
    : QObject(parent) {
    connect(&m_progressModel, &ProgressModel::logAdded, &m_logModel, &LogModel::appendLog);
    connect(&m_progressModel, &ProgressModel::questionReceived, this, &DaemonClient::questionPrompt);
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
            emit capabilitiesChanged();
        }
    }
}

void DaemonClient::onDbusTransactionEvent(const QDBusObjectPath &path, const QString &jsonStr) {
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
        m_updatesModel.setPackages(std::get<PlanReady>(event).ops);
        m_lastCheckedString = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm"));
        emit statusChanged();
    }
    m_progressModel.processEvent(event);
}

void DaemonClient::refreshUpdates() {
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
        }
    }
}

void DaemonClient::startUpgrade() {
    emit transactionStarted();
    if (m_replayBackend) {
        m_replayBackend->commit();
        return;
    }
    if (m_daemonIface && m_daemonIface->isValid() && !m_activeTransactionPath.path().isEmpty()) {
        m_daemonIface->call(QStringLiteral("Commit"), QVariant::fromValue(m_activeTransactionPath));
    }
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
