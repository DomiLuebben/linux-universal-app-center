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
    } else if (std::holds_alternative<LogLine>(event)) {
        m_logModel.appendLog(std::get<LogLine>(event));
    } else if (std::holds_alternative<PhaseChanged>(event)) {
        const auto &phase = std::get<PhaseChanged>(event);
        LogLevel l = (phase.phase == Phase::Failed) ? LogLevel::Error : LogLevel::Info;
        m_logModel.appendLog(LogLine{l, QStringLiteral("Phase"), phase.label});
    } else if (std::holds_alternative<TransactionDone>(event)) {
        const auto &done = std::get<TransactionDone>(event);
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
            qWarning() << "PlanUpgrade call failed:" << reply.error().message();
            m_logModel.appendLog(LogLine{LogLevel::Error, QStringLiteral("D-Bus"), reply.error().message()});
        }
    }
}

void DaemonClient::startUpgrade() {
    emit transactionStarted();
    m_logModel.clear();
    m_logModel.appendLog(LogLine{LogLevel::Info, QStringLiteral("lutd"), QStringLiteral("Transaktion wird vorbereitet...")});

    if (m_replayBackend) {
        m_replayBackend->commit();
        return;
    }
    if (m_daemonIface && m_daemonIface->isValid()) {
        if (m_activeTransactionPath.path().isEmpty()) {
            QVariantMap opts;
            opts[QStringLiteral("includeSecurityOnly")] = false;
            opts[QStringLiteral("refreshFirst")] = false;
            QDBusReply<QDBusObjectPath> planReply = m_daemonIface->call(QStringLiteral("PlanUpgrade"), opts);
            if (planReply.isValid()) {
                m_activeTransactionPath = planReply.value();
            }
        }

        if (!m_activeTransactionPath.path().isEmpty()) {
            QDBusReply<void> reply = m_daemonIface->call(QStringLiteral("Commit"), QVariant::fromValue(m_activeTransactionPath));
            if (!reply.isValid()) {
                QString err = reply.error().message();
                qWarning() << "Commit call failed:" << err;
                m_logModel.appendLog(LogLine{LogLevel::Error, QStringLiteral("D-Bus"), err});
                m_progressModel.processEvent(PhaseChanged{Phase::Failed, err, false});
                m_progressModel.processEvent(TransactionDone{Result::Failed, err, false, {}, 0});
            }
        } else {
            QString err = QStringLiteral("Kein gültiger Transaktionspfad vorhanden.");
            m_logModel.appendLog(LogLine{LogLevel::Error, QStringLiteral("lutd"), err});
            m_progressModel.processEvent(PhaseChanged{Phase::Failed, err, false});
            m_progressModel.processEvent(TransactionDone{Result::Failed, err, false, {}, 0});
        }
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
