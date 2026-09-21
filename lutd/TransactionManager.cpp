#include "TransactionManager.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "liblut/backend/Validation.h"
#include <QCoreApplication>
#include <QDebug>
#include "liblut/detect/DistroDetect.h"
#include "liblut/backend/dnf5/Dnf5Backend.h"
#include "liblut/backend/alpm/AlpmBackend.h"
#include "liblut/backend/apt/AptBackend.h"

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
    QString error;
    m_backend = Backend::createForHost(&error);
    if (!m_backend) { qCritical() << error; return false; }

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
    m_backendBusy = true;
    m_planReady = false;
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

    if (std::holds_alternative<PlanReady>(event)) {
        m_backendBusy = false;
        m_planReady = true;
    }
    if (std::holds_alternative<TransactionDone>(event)) {
        m_backendBusy = false;
        m_planReady = false;
    }
    // Inhibitor-Lock ab Download bis Cleanup/Done halten
    if (std::holds_alternative<PhaseChanged>(event)) {
        Phase phase = std::get<PhaseChanged>(event).phase;
        if (phase == Phase::Idle && !m_planReady) {
            m_backendBusy = false;
            m_hasActiveTransaction = false;
            resetIdleTimer();
        }
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
    QJsonArray commands;
    if (qobject_cast<Dnf5Backend *>(m_backend.get()))
        for (const auto &command : Dnf5Backend::transactionCommands()) commands.append(command);
    obj[QStringLiteral("dnf5Commands")] = commands;
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

bool TransactionManager::ownsTransaction() {
    if (message().service() == m_owner) return true;
    if (m_policyGate.checkAuthorization(PolicyGate::ActionManageOthers, message().service())) return true;
    sendErrorReply(QDBusError::AccessDenied, QStringLiteral("Die Transaktion gehört einem anderen Aufrufer."));
    return false;
}

bool TransactionManager::beginAuthorized(const QString &action) {
    if (m_backendBusy) {
        sendErrorReply(QDBusError::Failed, QStringLiteral("Eine Paketoperation läuft bereits.")); return false;
    }
    if (m_hasActiveTransaction && !ownsTransaction()) return false;
    if (!m_policyGate.checkAuthorization(action, message().service())) {
        sendErrorReply(QDBusError::AccessDenied, QStringLiteral("Polkit authorization denied")); return false;
    }
    m_owner = message().service(); m_commitAction = action;
    registerNewTransaction(); return true;
}

QDBusObjectPath TransactionManager::RefreshMetadata() {
    if (!beginAuthorized(PolicyGate::ActionRefresh)) return QDBusObjectPath(QStringLiteral("/"));
    QMetaObject::invokeMethod(m_backend.get(), &Backend::refreshMetadata, Qt::QueuedConnection);
    return m_currentTransactionPath;
}

QDBusObjectPath TransactionManager::PlanUpgrade(const QVariantMap &options) {
    UpgradeOptions opt;
    opt.includeSecurityOnly = options.value(QStringLiteral("includeSecurityOnly"), false).toBool();
    opt.excludeKernel = options.value(QStringLiteral("excludeKernel"), false).toBool();
    opt.allowDowngrade = options.value(QStringLiteral("allowDowngrade"), false).toBool();
    opt.refreshFirst = options.value(QStringLiteral("refreshFirst"), true).toBool();
    opt.packages = options.value(QStringLiteral("packages")).toStringList();
    if (!opt.packages.isEmpty() && (!m_backend->capabilities().partialUpgrade || !Validation::areValidPackageNames(opt.packages))) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Ungültige Paketauswahl.")); return QDBusObjectPath(QStringLiteral("/"));
    }
    if (!beginAuthorized(PolicyGate::ActionUpgrade)) return QDBusObjectPath(QStringLiteral("/"));
    QMetaObject::invokeMethod(m_backend.get(), [this, opt] { m_backend->planUpgradeAll(opt); }, Qt::QueuedConnection);
    return m_currentTransactionPath;
}

QDBusObjectPath TransactionManager::PlanInstall(const QStringList &names) {
    if (!Validation::areValidPackageNames(names)) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Ungültige Paketnamen.")); return QDBusObjectPath(QStringLiteral("/"));
    }
    if (!beginAuthorized(PolicyGate::ActionInstall)) return QDBusObjectPath(QStringLiteral("/"));
    QMetaObject::invokeMethod(m_backend.get(), [this, names] { m_backend->planInstall(names); }, Qt::QueuedConnection);
    return m_currentTransactionPath;
}

QDBusObjectPath TransactionManager::PlanRemove(const QStringList &names) {
    if (!Validation::areValidPackageNames(names)) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Ungültige Paketnamen.")); return QDBusObjectPath(QStringLiteral("/"));
    }
    if (!beginAuthorized(PolicyGate::ActionRemove)) return QDBusObjectPath(QStringLiteral("/"));
    QMetaObject::invokeMethod(m_backend.get(), [this, names] { m_backend->planRemove(names); }, Qt::QueuedConnection);
    return m_currentTransactionPath;
}

QDBusObjectPath TransactionManager::PlanDnf5(const QString &command, const QStringList &arguments, const QVariantMap &options) {
    auto *backend = qobject_cast<Dnf5Backend *>(m_backend.get());
    if (!backend || !Dnf5Backend::transactionCommands().contains(command)) {
        sendErrorReply(QDBusError::NotSupported, QStringLiteral("DNF5-Aktion nicht verfügbar.")); return QDBusObjectPath(QStringLiteral("/"));
    }
    const QString action = command.contains(QLatin1String("remove")) ? PolicyGate::ActionRemove :
                           command.contains(QLatin1String("install")) ? PolicyGate::ActionInstall : PolicyGate::ActionUpgrade;
    if (!beginAuthorized(action)) return QDBusObjectPath(QStringLiteral("/"));
    UpgradeOptions opt;
    opt.refreshFirst = options.value(QStringLiteral("refreshFirst"), true).toBool();
    opt.includeSecurityOnly = options.value(QStringLiteral("includeSecurityOnly"), false).toBool();
    opt.excludeKernel = options.value(QStringLiteral("excludeKernel"), false).toBool();
    opt.allowDowngrade = options.value(QStringLiteral("allowDowngrade"), false).toBool();
    QMetaObject::invokeMethod(backend, [backend, command, arguments, opt] { backend->planCommand(command, arguments, opt); }, Qt::QueuedConnection);
    return m_currentTransactionPath;
}

QDBusObjectPath TransactionManager::CleanCache() {
    auto *backend = qobject_cast<Dnf5Backend *>(m_backend.get());
    if (!backend) {
        sendErrorReply(QDBusError::NotSupported, QStringLiteral("Cachebereinigung ist für dieses Backend nicht angebunden.")); return QDBusObjectPath(QStringLiteral("/"));
    }
    if (!beginAuthorized(PolicyGate::ActionRemove)) return QDBusObjectPath(QStringLiteral("/"));
    QMetaObject::invokeMethod(backend, &Dnf5Backend::cleanCache, Qt::QueuedConnection);
    return m_currentTransactionPath;
}

void TransactionManager::Commit(const QDBusObjectPath &transactionPath) {
    resetIdleTimer();
    if (transactionPath.path() != m_currentTransactionPath.path()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid or expired transaction path"));
        return;
    }

    if (!m_planReady || m_backendBusy || !m_hasActiveTransaction) {
        sendErrorReply(QDBusError::Failed, QStringLiteral("Der Plan ist noch nicht bereit oder wurde bereits ausgeführt.")); return;
    }
    if (!ownsTransaction()) return;
    QString caller = message().service();
    if (!m_policyGate.checkAuthorization(m_commitAction, caller)) {
        sendErrorReply(QDBusError::AccessDenied, QStringLiteral("Polkit authorization denied for Commit"));
        return;
    }

    m_backendBusy = true; m_planReady = false;
    QMetaObject::invokeMethod(m_backend.get(), &Backend::commit, Qt::QueuedConnection);
}

void TransactionManager::Cancel(const QDBusObjectPath &transactionPath) {
    resetIdleTimer();
    if (transactionPath.path() != m_currentTransactionPath.path()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid or expired transaction path"));
        return;
    }

    if (!ownsTransaction()) return;
    if (!m_progressModel.isCancellable()) {
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

    if (!ownsTransaction()) return;
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
