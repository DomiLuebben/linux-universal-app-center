#pragma once

#include <QObject>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <memory>
#include "liblut/backend/Backend.h"
#include "liblut/backend/replay/ReplayBackend.h"
#include "liblut/progress/ProgressModel.h"
#include "models/UpdatesModel.h"
#include "models/LogModel.h"

namespace lut {

class DaemonClient : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(bool isReplayMode READ isReplayMode NOTIFY modeChanged)
    Q_PROPERTY(bool partialUpgradeSupported READ partialUpgradeSupported NOTIFY capabilitiesChanged)
    Q_PROPERTY(QString lastCheckedString READ lastCheckedString NOTIFY statusChanged)

public:
    explicit DaemonClient(QObject *parent = nullptr);
    ~DaemonClient() override;

    bool init(const QString &replayFixture = QString(), double replaySpeed = 1.0);

    bool isConnected() const { return m_connected || m_replayBackend != nullptr; }
    bool isReplayMode() const { return m_replayBackend != nullptr; }
    bool partialUpgradeSupported() const { return m_capabilities.partialUpgrade; }
    QString lastCheckedString() const { return m_lastCheckedString; }

    ProgressModel *progressModel() { return &m_progressModel; }
    UpdatesModel *updatesModel() { return &m_updatesModel; }
    LogModel *logModel() { return &m_logModel; }

public slots:
    void refreshUpdates();
    void startUpgrade();
    void cancelTransaction();
    void answerQuestion(const QString &id, const QJsonObject &answer);

signals:
    void connectionChanged();
    void modeChanged();
    void capabilitiesChanged();
    void statusChanged();
    void transactionStarted();
    void questionPrompt(const lut::Question &question);

private slots:
    void onDbusTransactionEvent(const QDBusObjectPath &path, const QString &jsonStr);
    void handleEvent(const lut::Event &event);

private:
    void connectToDaemon();
    void queryCapabilities();

    bool m_connected = false;
    Capabilities m_capabilities;
    QString m_lastCheckedString;
    QDBusObjectPath m_activeTransactionPath;

    std::unique_ptr<QDBusInterface> m_daemonIface;
    std::unique_ptr<ReplayBackend> m_replayBackend;

    ProgressModel m_progressModel;
    UpdatesModel m_updatesModel;
    LogModel m_logModel;
};

} // namespace lut
