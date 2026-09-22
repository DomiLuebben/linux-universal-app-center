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
#include "models/TransactionPlanModel.h"

namespace lut {

class DaemonClient : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(bool isReplayMode READ isReplayMode NOTIFY modeChanged)
    Q_PROPERTY(bool partialUpgradeSupported READ partialUpgradeSupported NOTIFY capabilitiesChanged)
    Q_PROPERTY(QString lastCheckedString READ lastCheckedString NOTIFY statusChanged)
    Q_PROPERTY(bool hasPlan READ hasPlan NOTIFY statusChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY statusChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY statusChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(QStringList dnf5Commands READ dnf5Commands NOTIFY capabilitiesChanged)
    Q_PROPERTY(TransactionPlanModel *planModel READ planModel CONSTANT)
    Q_PROPERTY(bool installSupported READ installSupported NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool removeSupported READ removeSupported NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool installRequiresFullUpgrade READ installRequiresFullUpgrade NOTIFY capabilitiesChanged)
    Q_PROPERTY(int protocolVersion READ protocolVersion NOTIFY capabilitiesChanged)

public:
    explicit DaemonClient(QObject *parent = nullptr);
    ~DaemonClient() override;

    bool init(const QString &replayFixture = QString(), double replaySpeed = 1.0);

    bool isConnected() const { return m_connected || m_replayBackend != nullptr; }
    bool isReplayMode() const { return m_replayBackend != nullptr; }
    bool partialUpgradeSupported() const { return m_capabilities.partialUpgrade; }
    bool installSupported() const { return m_capabilities.install; }
    bool removeSupported() const { return m_capabilities.remove; }
    bool installRequiresFullUpgrade() const { return m_capabilities.installRequiresFullUpgrade; }
    int protocolVersion() const { return m_capabilities.protocolVersion; }
    QString lastCheckedString() const { return m_lastCheckedString; }
    bool hasPlan() const { return m_hasPlan; }
    bool isBusy() const { return m_busy; }
    bool hasError() const { return m_hasError; }
    QString statusMessage() const { return m_statusMessage; }
    QStringList dnf5Commands() const { return m_dnf5Commands; }

    ProgressModel *progressModel() { return &m_progressModel; }
    UpdatesModel *updatesModel() { return &m_updatesModel; }
    LogModel *logModel() { return &m_logModel; }
    InstalledModel *installedModel() { return &m_installedModel; }
    HistoryModel *historyModel() { return &m_historyModel; }
    TransactionPlanModel *planModel() { return &m_planModel; }

public slots:
    void refreshUpdates();
    void startUpgrade();
    void planDnf5(const QString &command, const QString &arguments, bool securityOnly = false, bool excludeKernel = false);
    void cleanCache();
    void cancelTransaction();
    void answerQuestion(const QString &id, const QJsonObject &answer);

    // Store-Operationen
    void planStoreInstall(const QString &packageName, const QString &repoId = QString());
    void planStoreRemove(const QString &packageName);
    void commitStorePlan(const QString &planRevision = QString());
    void discardStorePlan();
    void reconnect();

    void setCapabilitiesForTest(const Capabilities &caps) { m_capabilities = caps; }
    void setPendingTransactionForTest(bool pending, bool busy = true) {
        m_pendingTransaction = pending;
        m_busy = busy;
    }

signals:
    void connectionChanged();
    void modeChanged();
    void capabilitiesChanged();
    void statusChanged();
    void transactionStarted();
    void transactionFinished(lut::Result result);
    void questionPrompt(const lut::Question &question);

private slots:
    void onDbusTransactionEvent(const QDBusObjectPath &path, const QString &jsonStr);
    void onDbusNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void handleEvent(const lut::Event &event);

private:
    void connectToDaemon();
    void queryCapabilities();
    void checkForActiveTransaction();
    bool reattachToTransaction(const QDBusObjectPath &path);
    void reportError(const QString &message);

    bool m_connected = false;
    bool m_hasPlan = false;
    bool m_busy = false;
    bool m_hasError = false;
    bool m_isUpgradePlan = false;
    bool m_pendingTransaction = false;
    bool m_terminalEventProcessed = false;
    quint64 m_lastSeenSequence = 0;
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
    TransactionPlanModel m_planModel;
};

} // namespace lut
