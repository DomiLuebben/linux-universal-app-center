#pragma once

#include <QObject>
#include <QDBusContext>
#include <QDBusObjectPath>
#include <QTimer>
#include <QThread>
#include <memory>
#include "PolicyGate.h"
#include "Inhibitor.h"
#include "liblut/backend/Backend.h"
#include "liblut/backend/flatpak/FlatpakBackend.h"
#include "liblut/backend/snap/SnapBackend.h"
#include "liblut/backend/pacstall/PacstallBackend.h"
#include "liblut/progress/ProgressModel.h"
#include "liblut/transaction/TransactionTypes.h"

namespace lut {

class TransactionManager : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.linuxupdatetool.Daemon1")

public:
    explicit TransactionManager(QObject *parent = nullptr);
    ~TransactionManager() override;

    bool init();
    void setBackendForTest(std::unique_ptr<Backend> backend, bool threaded = false);
    void setFlatpakBackendForTest(std::unique_ptr<FlatpakBackend> backend);
    void setSnapBackendForTest(std::unique_ptr<SnapBackend> backend);
    void setPacstallBackendForTest(std::unique_ptr<PacstallBackend> backend);
    void setCallerUidForTest(quint32 uid);
    quint32 callerUidForTest() const { return m_ownerUid.value_or(0); }
    quint64 sequenceCounter() const { return m_sequenceCounter; }

public slots:
    // D-Bus Methoden
    QString GetCapabilities();
    QDBusObjectPath RefreshMetadata();
    QDBusObjectPath PlanUpgrade(const QVariantMap &options);
    QDBusObjectPath PlanInstall(const QStringList &names);
    QDBusObjectPath PlanRemove(const QStringList &names);
    QDBusObjectPath PlanDnf5(const QString &command, const QStringList &arguments, const QVariantMap &options);
    QDBusObjectPath CleanCache();
    QDBusObjectPath PlanPackageTransaction(const QString &action, const QVariantList &targets, const QVariantMap &options);
    QString PacstallCheck();
    QDBusObjectPath PlanPacstallUpgrade(const QStringList &names);
    QString GetTransactionSnapshot(const QDBusObjectPath &transactionPath);
    void Commit(const QDBusObjectPath &transactionPath);
    void CommitPlan(const QDBusObjectPath &transactionPath, const QString &planRevision);
    void DiscardPlan(const QDBusObjectPath &transactionPath);
    void Cancel(const QDBusObjectPath &transactionPath);
    QString AttachTransaction(const QDBusObjectPath &transactionPath, qint64 lastSeenSequence);
    void AnswerQuestion(const QDBusObjectPath &transactionPath, const QString &id, const QString &json);
    QList<QDBusObjectPath> GetActiveTransactions();
    QStringList GetEventHistory(const QDBusObjectPath &transactionPath);

    // Repository Management
    QString GetRepositories();
    QString GetRepositoryPresets();
    bool AddRepository(const QString &repoJson);
    bool AddRepositoryPreset(const QString &presetId);
    bool RemoveRepository(const QString &repoId);
    bool ToggleRepository(const QString &repoId, bool enabled);

signals:
    // D-Bus Signal
    void TransactionEvent(const QDBusObjectPath &transactionPath, const QString &eventJson);

protected:
    virtual bool authorize(const QString &action, const QString &service);

private slots:
    void onBackendEvent(const lut::Event &event);
    void onIdleTimeout();

private:
    void startBackendThread();
    void resetIdleTimer();
    void registerNewTransaction();
    bool beginAuthorized(const QString &action);
    bool ownsTransaction();
    quint32 getCallerUid() const;
    void replyError(QDBusError::ErrorType type, const QString &msg);

    PolicyGate m_policyGate;
    Inhibitor m_inhibitor;
    std::unique_ptr<Backend> m_backend;
    std::unique_ptr<FlatpakBackend> m_flatpakBackend;
    std::unique_ptr<SnapBackend> m_snapBackend;
    std::unique_ptr<PacstallBackend> m_pacstallBackend;
    PacstallBackend *ensurePacstallBackend();
    int m_pendingPacstallChecks = 0; // hält den Daemon wach, solange -Lu läuft
    Backend *m_activeBackend = nullptr;
    QThread m_backendThread;
    Capabilities m_capabilities;
    ProgressModel m_progressModel;

    QTimer m_idleTimer;
    qint64 m_transactionCounter = 0;
    quint64 m_sequenceCounter = 0;
    QDBusObjectPath m_currentTransactionPath;
    bool m_hasActiveTransaction = false;
    bool m_backendBusy = false;
    bool m_planReady = false;
    bool m_commitStarted = false;
    QString m_owner;
    std::optional<quint32> m_ownerUid;
    std::optional<quint32> m_testCallerUid;
    QString m_commitAction;

    // Ring-Buffer für Reattach (letzte 5000 Events)
    QStringList m_eventHistory;
    TransactionPlan m_currentPlan;
    TransactionIntent m_currentIntent;
};

} // namespace lut
