#include "TransactionManager.h"
#include <QDBusConnection>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QDebug>
#include "liblut/detect/DistroDetect.h"
#include "liblut/backend/dnf5/Dnf5Backend.h"
#include "liblut/backend/alpm/AlpmBackend.h"

namespace lut {

TransactionManager::TransactionManager(QObject *parent)
    : QObject(parent) {
    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(120000); // 120 Sekunden Idle-Exit
    connect(&m_idleTimer, &QTimer::timeout, this, &TransactionManager::onIdleTimeout);
}

TransactionManager::~TransactionManager() {
    m_inhibitor.releaseLock();
}

bool TransactionManager::init() {
    // Backend basierend auf Host-Distribution wählen
    auto family = DistroDetect::detectFamily();
    qInfo() << "TransactionManager: Detected distribution family:" << DistroDetect::familyToString(family);

    if (family == DistroFamily::Fedora) {
        m_backend = std::make_unique<Dnf5Backend>(this);
    } else {
        // Arch / CachyOS oder Default
        m_backend = std::make_unique<AlpmBackend>(this);
    }

    connect(m_backend.get(), &Backend::eventEmitted, this, &TransactionManager::onBackendEvent);
    connect(m_backend.get(), &Backend::eventEmitted, &m_progressModel, &ProgressModel::processEvent);

    resetIdleTimer();
    return true;
}

void TransactionManager::resetIdleTimer() {
    if (!m_hasActiveTransaction) {
        m_idleTimer.start();
    } else {
        m_idleTimer.stop();
    }
}

void TransactionManager::onIdleTimeout() {
    if (!m_hasActiveTransaction) {
        qInfo() << "lutd: No active transactions after 120s idle. Exiting cleanly...";
        QCoreApplication::quit();
    }
}

void TransactionManager::registerNewTransaction() {
    m_transactionCounter++;
    m_currentTransactionPath = QDBusObjectPath(QStringLiteral("/org/linuxupdatetool/Transaction/%1").arg(m_transactionCounter));
    m_hasActiveTransaction = true;
    m_eventHistory.clear();
    m_progressModel.reset();
    resetIdleTimer();
}

void TransactionManager::onBackendEvent(const lut::Event &event) {
    QJsonObject json = serializeEvent(event);
    QString jsonStr = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));

    // Ring-Puffer füllen
    m_eventHistory.append(jsonStr);
    if (m_eventHistory.size() > 5000) {
        m_eventHistory.removeFirst();
    }

    // Inhibitor-Lock ab Download bis Cleanup/Done halten
    if (std::holds_alternative<PhaseChanged>(event)) {
        Phase phase = std::get<PhaseChanged>(event).phase;
        if (phase == Phase::Download || phase == Phase::Commit || phase == Phase::PostTransaction) {
            m_inhibitor.takeLock(QStringLiteral("Paketaktualisierung läuft"));
        } else if (phase == Phase::Finished || phase == Phase::Failed || phase == Phase::Cancelled) {
            m_inhibitor.releaseLock();
            m_hasActiveTransaction = false;
            resetIdleTimer();
        }
    } else if (std::holds_alternative<TransactionDone>(event)) {
        m_inhibitor.releaseLock();
        m_hasActiveTransaction = false;
        resetIdleTimer();
    }

    emit TransactionEvent(m_currentTransactionPath, jsonStr);
}

QString TransactionManager::GetCapabilities() {
    resetIdleTimer();
    Capabilities cap = m_backend->capabilities();
    QJsonObject obj;
    obj[QStringLiteral("partialUpgrade")] = cap.partialUpgrade;
    obj[QStringLiteral("downgrade")] = cap.downgrade;
    obj[QStringLiteral("historyUndo")] = cap.historyUndo;
    obj[QStringLiteral("changelogs")] = cap.changelogs;
    obj[QStringLiteral("securityFlag")] = cap.securityFlag;
    obj[QStringLiteral("offlineUpdate")] = cap.offlineUpdate;
    obj[QStringLiteral("autoremove")] = cap.autoremove;
    obj[QStringLiteral("parallelDownloads")] = cap.parallelDownloads;
    obj[QStringLiteral("degraded")] = cap.degraded;
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QDBusObjectPath TransactionManager::RefreshMetadata() {
    resetIdleTimer();
    QString caller = message().service();
    if (!m_policyGate.checkAuthorization(PolicyGate::ActionRefresh, caller)) {
        sendErrorReply(QDBusError::AccessDenied, QStringLiteral("Polkit authorization denied for RefreshMetadata"));
        return QDBusObjectPath();
    }

    registerNewTransaction();
    QMetaObject::invokeMethod(m_backend.get(), &Backend::refreshMetadata, Qt::QueuedConnection);
    return m_currentTransactionPath;
}

QDBusObjectPath TransactionManager::PlanUpgrade(const QVariantMap &options) {
    resetIdleTimer();
    registerNewTransaction();

    UpgradeOptions opt;
    opt.includeSecurityOnly = options.value(QStringLiteral("includeSecurityOnly"), false).toBool();
    opt.excludeKernel = options.value(QStringLiteral("excludeKernel"), false).toBool();
    opt.allowDowngrade = options.value(QStringLiteral("allowDowngrade"), false).toBool();
    opt.refreshFirst = options.value(QStringLiteral("refreshFirst"), true).toBool();

    QMetaObject::invokeMethod(m_backend.get(), [this, opt]() {
        m_backend->planUpgradeAll(opt);
    }, Qt::QueuedConnection);

    return m_currentTransactionPath;
}

QDBusObjectPath TransactionManager::PlanInstall(const QStringList &names) {
    resetIdleTimer();
    registerNewTransaction();
    QMetaObject::invokeMethod(m_backend.get(), [this, names]() {
        m_backend->planInstall(names);
    }, Qt::QueuedConnection);
    return m_currentTransactionPath;
}

QDBusObjectPath TransactionManager::PlanRemove(const QStringList &names) {
    resetIdleTimer();
    QString caller = message().service();
    if (!m_policyGate.checkAuthorization(PolicyGate::ActionRemove, caller)) {
        sendErrorReply(QDBusError::AccessDenied, QStringLiteral("Polkit authorization denied for PlanRemove"));
        return QDBusObjectPath();
    }

    registerNewTransaction();
    QMetaObject::invokeMethod(m_backend.get(), [this, names]() {
        m_backend->planRemove(names);
    }, Qt::QueuedConnection);
    return m_currentTransactionPath;
}

void TransactionManager::Commit(const QDBusObjectPath &transactionPath) {
    resetIdleTimer();
    if (transactionPath.path() != m_currentTransactionPath.path()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid or expired transaction path"));
        return;
    }

    QString caller = message().service();
    if (!m_policyGate.checkAuthorization(PolicyGate::ActionUpgrade, caller)) {
        sendErrorReply(QDBusError::AccessDenied, QStringLiteral("Polkit authorization denied for Commit"));
        return;
    }

    QMetaObject::invokeMethod(m_backend.get(), &Backend::commit, Qt::QueuedConnection);
}

void TransactionManager::Cancel(const QDBusObjectPath &transactionPath) {
    resetIdleTimer();
    if (transactionPath.path() != m_currentTransactionPath.path()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid or expired transaction path"));
        return;
    }

    if (!isPhaseCancellable(m_progressModel.currentPhase())) {
        sendErrorReply(QDBusError::Failed, QStringLiteral("Installation cannot be cancelled in current phase"));
        return;
    }

    QMetaObject::invokeMethod(m_backend.get(), &Backend::cancel, Qt::QueuedConnection);
}

void TransactionManager::AnswerQuestion(const QDBusObjectPath &transactionPath, const QString &id, const QString &json) {
    resetIdleTimer();
    if (transactionPath.path() != m_currentTransactionPath.path()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid transaction path"));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    QMetaObject::invokeMethod(m_backend.get(), [this, id, doc]() {
        m_backend->answerQuestion(id, doc.object());
    }, Qt::QueuedConnection);
}

QList<QDBusObjectPath> TransactionManager::GetActiveTransactions() {
    resetIdleTimer();
    QList<QDBusObjectPath> list;
    if (m_hasActiveTransaction) {
        list.append(m_currentTransactionPath);
    }
    return list;
}

QStringList TransactionManager::GetEventHistory(const QDBusObjectPath &transactionPath) {
    resetIdleTimer();
    if (transactionPath.path() == m_currentTransactionPath.path()) {
        return m_eventHistory;
    }
    return {};
}

} // namespace lut
