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
#include "models/InstalledModel.h"
#include "models/HistoryModel.h"

namespace lut {

class DaemonClient : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(bool isReplayMode READ isReplayMode NOTIFY modeChanged)
    Q_PROPERTY(bool partialUpgradeSupported READ partialUpgradeSupported NOTIFY capabilitiesChanged)
    Q_PROPERTY(QString lastCheckedString READ lastCheckedString NOTIFY statusChanged)
    Q_PROPERTY(bool hasPlan READ hasPlan NOTIFY statusChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY statusChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(QStringList dnf5Commands READ dnf5Commands NOTIFY capabilitiesChanged)

public:
    explicit DaemonClient(QObject *parent = nullptr);
    ~DaemonClient() override;

    bool init(const QString &replayFixture = QString(), double replaySpeed = 1.0);

    bool isConnected() const { return m_connected || m_replayBackend != nullptr; }
    bool isReplayMode() const { return m_replayBackend != nullptr; }
    bool partialUpgradeSupported() const { return m_capabilities.partialUpgrade; }
    QString lastCheckedString() const { return m_lastCheckedString; }
    bool hasPlan() const { return m_hasPlan; }
    bool isBusy() const { return m_busy; }
    QString statusMessage() const { return m_statusMessage; }
    QStringList dnf5Commands() const { return m_dnf5Commands; }

    ProgressModel *progressModel() { return &m_progressModel; }
    UpdatesModel *updatesModel() { return &m_updatesModel; }
    LogModel *logModel() { return &m_logModel; }
    InstalledModel *installedModel() { return &m_installedModel; }
    HistoryModel *historyModel() { return &m_historyModel; }

public slots:
    void refreshUpdates();
    void startUpgrade();
    void planDnf5(const QString &command, const QString &arguments, bool securityOnly = false, bool excludeKernel = false);
    void cleanCache();
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
    void reportError(const QString &message);

    bool m_connected = false;
    bool m_hasPlan = false;
    bool m_busy = false;
    bool m_isUpgradePlan = false;
    QString m_statusMessage;
    QStringList m_dnf5Commands;
    Capabilities m_capabilities;
    QString m_lastCheckedString;
    QDBusObjectPath m_activeTransactionPath;

    std::unique_ptr<QDBusInterface> m_daemonIface;
    std::unique_ptr<ReplayBackend> m_replayBackend;

    ProgressModel m_progressModel;
    UpdatesModel m_updatesModel;
    LogModel m_logModel;
    InstalledModel m_installedModel;
    HistoryModel m_historyModel;
};

} // namespace lut
