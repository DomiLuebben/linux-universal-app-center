#pragma once

#include <QObject>
#include <QDBusContext>
#include <QDBusObjectPath>
#include <QTimer>
#include <memory>
#include "PolicyGate.h"
#include "Inhibitor.h"
#include "liblut/backend/Backend.h"
#include "liblut/progress/ProgressModel.h"

namespace lut {

class TransactionManager : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.linuxupdatetool.Daemon1")

public:
    explicit TransactionManager(QObject *parent = nullptr);
    ~TransactionManager() override;

    bool init();

public slots:
    // D-Bus Methoden
    QString GetCapabilities();
    QDBusObjectPath RefreshMetadata();
    QDBusObjectPath PlanUpgrade(const QVariantMap &options);
    QDBusObjectPath PlanInstall(const QStringList &names);
    QDBusObjectPath PlanRemove(const QStringList &names);
    void Commit(const QDBusObjectPath &transactionPath);
    void Cancel(const QDBusObjectPath &transactionPath);
    void AnswerQuestion(const QDBusObjectPath &transactionPath, const QString &id, const QString &json);
    QList<QDBusObjectPath> GetActiveTransactions();
    QStringList GetEventHistory(const QDBusObjectPath &transactionPath);

signals:
    // D-Bus Signal
    void TransactionEvent(const QDBusObjectPath &transactionPath, const QString &eventJson);

private slots:
    void onBackendEvent(const lut::Event &event);
    void onIdleTimeout();

private:
    void resetIdleTimer();
    void registerNewTransaction();

    PolicyGate m_policyGate;
    Inhibitor m_inhibitor;
    std::unique_ptr<Backend> m_backend;
    ProgressModel m_progressModel;

    QTimer m_idleTimer;
    qint64 m_transactionCounter = 0;
    QDBusObjectPath m_currentTransactionPath;
    bool m_hasActiveTransaction = false;

    // Ring-Buffer für Reattach (letzte 5000 Events)
    QStringList m_eventHistory;
};

} // namespace lut
