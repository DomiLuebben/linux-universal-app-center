#pragma once
#include <optional>

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
#include "models/RepositoriesModel.h"

namespace lut {

class DaemonClient : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(bool isReplayMode READ isReplayMode NOTIFY modeChanged)
    Q_PROPERTY(bool partialUpgradeSupported READ partialUpgradeSupported NOTIFY capabilitiesChanged)
    Q_PROPERTY(QString lastCheckedString READ lastCheckedString NOTIFY statusChanged)
    Q_PROPERTY(bool hasPlan READ hasPlan NOTIFY statusChanged)
    // Trennt den Systemupdate-Plan von einer Store-Installation/-Entfernung,
    // damit die Vorschau die richtige Bestätigung anbietet.
    Q_PROPERTY(bool isStorePlan READ isStorePlan NOTIFY statusChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY statusChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY statusChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(QStringList dnf5Commands READ dnf5Commands NOTIFY capabilitiesChanged)
    Q_PROPERTY(TransactionPlanModel *planModel READ planModel CONSTANT)
    Q_PROPERTY(RepositoriesModel *repositoriesModel READ repositoriesModel CONSTANT)
    Q_PROPERTY(bool installSupported READ installSupported NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool removeSupported READ removeSupported NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool installRequiresFullUpgrade READ installRequiresFullUpgrade NOTIFY capabilitiesChanged)
    Q_PROPERTY(int protocolVersion READ protocolVersion NOTIFY capabilitiesChanged)

public:
    explicit DaemonClient(QObject *parent = nullptr);
    explicit DaemonClient(const QDBusConnection &connection, QObject *parent = nullptr);
    ~DaemonClient() override;

    bool init(const QString &replayFixture = QString(), double replaySpeed = 1.0);

    bool isConnected() const { return m_connected || m_replayBackend != nullptr; }
    /// Nur für Tests: ob der Daemon als D-Bus-aktivierbar gilt (sonst echte Abfrage).
    void setDaemonActivatableForTest(std::optional<bool> activatable) { m_activatableOverride = activatable; }
    bool isReplayMode() const { return m_replayBackend != nullptr; }
    bool partialUpgradeSupported() const { return m_capabilities.partialUpgrade; }
    bool installSupported() const { return m_capabilities.install; }
    bool removeSupported() const { return m_capabilities.remove; }
    bool installRequiresFullUpgrade() const { return m_capabilities.installRequiresFullUpgrade; }
    int protocolVersion() const { return m_capabilities.protocolVersion; }
    QString lastCheckedString() const { return m_lastCheckedString; }
    bool hasPlan() const { return m_hasPlan; }
    bool isStorePlan() const { return m_hasPlan && !m_isUpgradePlan; }
    bool isBusy() const { return m_busy || m_externalBusy; }
    // true, solange die laufende oder zuletzt geplante Transaktion eine Systemaktualisierung ist.
    bool isUpgradeTransaction() const { return m_isUpgradePlan; }
    void setExternalBusy(bool busy);
    bool hasError() const { return m_hasError; }
    QString statusMessage() const { return m_statusMessage; }
    QStringList dnf5Commands() const { return m_dnf5Commands; }

    ProgressModel *progressModel() { return &m_progressModel; }
    UpdatesModel *updatesModel() { return &m_updatesModel; }
    LogModel *logModel() { return &m_logModel; }
    InstalledModel *installedModel() { return &m_installedModel; }
    HistoryModel *historyModel() { return &m_historyModel; }
    TransactionPlanModel *planModel() { return &m_planModel; }
    RepositoriesModel *repositoriesModel() { return &m_repositoriesModel; }
    /// Bus, über den dieser Client den Daemon erreicht (System-Bus, in Tests der Session-Bus).
    QDBusConnection bus() const { return m_bus; }

public slots:
    void refreshUpdates();
    void startUpgrade();
    void planDnf5(const QString &command, const QString &arguments, bool securityOnly = false, bool excludeKernel = false);
    void cleanCache();
    void cancelTransaction();
    void answerQuestion(const QString &id, const QJsonObject &answer);

    // Store-Operationen
    void planStoreInstall(const QList<lut::PackageRef> &targets);
    void planStoreInstall(const QStringList &packageNames, const QString &repoId = QString());
    void planStoreInstall(const QString &packageName, const QString &repoId = QString());
    void planStoreRemove(const QList<lut::PackageRef> &targets);
    void planStoreRemove(const QStringList &packageNames);
    void planStoreRemove(const QString &packageName);
    void commitStorePlan(const QString &planRevision = QString());
    void discardStorePlan();
    void reconnect();

    void setCapabilitiesForTest(const Capabilities &caps) { m_capabilities = caps; }
    void setPendingTransactionForTest(bool pending, bool busy = true) {
        m_pendingTransaction = pending;
        m_busy = busy;
    }
    void refreshUpdatesStateForTest() { m_isUpgradePlan = true; }

signals:
    void connectionChanged();
    void modeChanged();
    void capabilitiesChanged();
    void statusChanged();
    void transactionStarted();
    void transactionFinished(lut::Result result);
    // Der aufgelöste Plan steht in planModel bereit und kann angereichert werden.
    void planReady();
    void questionPrompt(const lut::Question &question);

private slots:
    void onDbusTransactionEvent(const QDBusObjectPath &path, const QString &jsonStr);
    void onDbusNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void handleEvent(const lut::Event &event);

private:
    // Commit/CommitPlan laufen asynchron: die Polkit-Passwortabfrage darf
    // länger dauern als das D-Bus-Standardtimeout von 25 s.
    void sendCommit(const QString &method, const QVariantList &arguments);
    void connectToDaemon();
    void queryCapabilities();
    void checkForActiveTransaction();
    bool reattachToTransaction(const QDBusObjectPath &path);
    void reportError(const QString &message);

    QDBusConnection m_bus;
    bool m_connected = false;
    std::optional<bool> m_activatableOverride;
    bool daemonActivatable() const;
    bool ensureDaemon();
    bool m_hasPlan = false;
    bool m_busy = false;
    bool m_hasError = false;
    bool m_isUpgradePlan = false;
    bool m_externalBusy = false;
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
    RepositoriesModel m_repositoriesModel;
};

} // namespace lut
