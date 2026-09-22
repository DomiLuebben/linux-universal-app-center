#include "TransactionManager.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <unistd.h>
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

void TransactionManager::setBackendForTest(std::unique_ptr<Backend> backend) {
    m_backend = std::move(backend);
    if (m_backend) {
        connect(m_backend.get(), &Backend::eventEmitted, this, &TransactionManager::onBackendEvent);
        connect(m_backend.get(), &Backend::eventEmitted, &m_progressModel, &ProgressModel::processEvent);
    }
}

void TransactionManager::setCallerUidForTest(quint32 uid) {
    m_testCallerUid = uid;
}

quint32 TransactionManager::getCallerUid() const {
    if (m_testCallerUid.has_value()) {
        return *m_testCallerUid;
    }
    if (!calledFromDBus()) {
        return static_cast<quint32>(::getuid());
    }
    auto iface = connection().interface();
    if (!iface) {
        auto sysIface = QDBusConnection::systemBus().interface();
        if (sysIface) iface = sysIface;
    }
    if (iface) {
        auto reply = iface->serviceUid(message().service());
        if (reply.isValid()) {
            return reply.value();
        }
    }
    return static_cast<quint32>(::getuid());
}

void TransactionManager::replyError(QDBusError::ErrorType type, const QString &msg) {
    if (calledFromDBus()) {
        sendErrorReply(type, msg);
    } else {
        qWarning() << "TransactionManager direct invocation error:" << type << msg;
    }
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
    m_sequenceCounter++;
    QJsonObject json = serializeEvent(event, m_sequenceCounter, m_currentTransactionPath.path());
    QString jsonStr = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));

    // Ring-Puffer füllen
    m_eventHistory.append(jsonStr);
    if (m_eventHistory.size() > 5000) {
        m_eventHistory.removeFirst();
    }

    if (std::holds_alternative<PlanReady>(event)) {
        const auto &plan = std::get<PlanReady>(event);
        m_backendBusy = false;
        m_planReady = true;
        m_currentPlan.ops = plan.ops;
        m_currentPlan.downloadBytes = plan.downloadBytes;
        m_currentPlan.installedSizeDelta = plan.installedSizeDelta;
        m_currentPlan.warnings = plan.warnings;
        m_currentPlan.planRevision = plan.planRevision.isEmpty() ? m_currentPlan.calculateFingerprint() : plan.planRevision;
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
    obj[QStringLiteral("catalogQuery")] = cap.catalogQuery;
    obj[QStringLiteral("install")] = cap.install;
    obj[QStringLiteral("remove")] = cap.remove;
    obj[QStringLiteral("installRequiresFullUpgrade")] = cap.installRequiresFullUpgrade;
    obj[QStringLiteral("typedPackageTargets")] = cap.typedPackageTargets;
    obj[QStringLiteral("transactionReattach")] = cap.transactionReattach;
    obj[QStringLiteral("protocolVersion")] = cap.protocolVersion;
    QJsonArray commands;
    if (qobject_cast<Dnf5Backend *>(m_backend.get()))
        for (const auto &command : Dnf5Backend::transactionCommands()) commands.append(command);
    obj[QStringLiteral("dnf5Commands")] = commands;
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

bool TransactionManager::ownsTransaction() {
    if (calledFromDBus() && message().service() == m_owner) return true;
    quint32 callerUid = getCallerUid();
    if (m_ownerUid.has_value() && callerUid == *m_ownerUid) return true;
    if (calledFromDBus() && m_policyGate.checkAuthorization(PolicyGate::ActionManageOthers, message().service())) return true;
    replyError(QDBusError::AccessDenied, QStringLiteral("Die Transaktion gehört einem anderen Aufrufer."));
    return false;
}

bool TransactionManager::beginAuthorized(const QString &action) {
    if (m_backendBusy || (m_hasActiveTransaction && m_planReady)) {
        replyError(QDBusError::Failed, QStringLiteral("Eine Paketoperation läuft bereits.")); return false;
    }
    if (m_hasActiveTransaction && !ownsTransaction()) return false;
    if (calledFromDBus() && !m_policyGate.checkAuthorization(action, message().service())) {
        replyError(QDBusError::AccessDenied, QStringLiteral("Polkit authorization denied")); return false;
    }
    m_owner = calledFromDBus() ? message().service() : QStringLiteral("local");
    m_ownerUid = getCallerUid();
    m_commitAction = action;
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
        replyError(QDBusError::InvalidArgs, QStringLiteral("Ungültige Paketauswahl.")); return QDBusObjectPath(QStringLiteral("/"));
    }
    if (!beginAuthorized(PolicyGate::ActionUpgrade)) return QDBusObjectPath(QStringLiteral("/"));
    QMetaObject::invokeMethod(m_backend.get(), [this, opt] { m_backend->planUpgradeAll(opt); }, Qt::QueuedConnection);
    return m_currentTransactionPath;
}

QDBusObjectPath TransactionManager::PlanInstall(const QStringList &names) {
    if (!Validation::areValidPackageNames(names)) {
        replyError(QDBusError::InvalidArgs, QStringLiteral("Ungültige Paketnamen.")); return QDBusObjectPath(QStringLiteral("/"));
    }
    if (!beginAuthorized(PolicyGate::ActionInstall)) return QDBusObjectPath(QStringLiteral("/"));
    QMetaObject::invokeMethod(m_backend.get(), [this, names] { m_backend->planInstall(names); }, Qt::QueuedConnection);
    return m_currentTransactionPath;
}

QDBusObjectPath TransactionManager::PlanRemove(const QStringList &names) {
    if (!Validation::areValidPackageNames(names)) {
        replyError(QDBusError::InvalidArgs, QStringLiteral("Ungültige Paketnamen.")); return QDBusObjectPath(QStringLiteral("/"));
    }
    if (!beginAuthorized(PolicyGate::ActionRemove)) return QDBusObjectPath(QStringLiteral("/"));
    QMetaObject::invokeMethod(m_backend.get(), [this, names] { m_backend->planRemove(names); }, Qt::QueuedConnection);
    return m_currentTransactionPath;
}

QDBusObjectPath TransactionManager::PlanDnf5(const QString &command, const QStringList &arguments, const QVariantMap &options) {
    auto *backend = qobject_cast<Dnf5Backend *>(m_backend.get());
    if (!backend || !Dnf5Backend::transactionCommands().contains(command)) {
        replyError(QDBusError::NotSupported, QStringLiteral("DNF5-Aktion nicht verfügbar.")); return QDBusObjectPath(QStringLiteral("/"));
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
        replyError(QDBusError::NotSupported, QStringLiteral("Cachebereinigung ist für dieses Backend nicht angebunden.")); return QDBusObjectPath(QStringLiteral("/"));
    }
    if (!beginAuthorized(PolicyGate::ActionRemove)) return QDBusObjectPath(QStringLiteral("/"));
    QMetaObject::invokeMethod(backend, &Dnf5Backend::cleanCache, Qt::QueuedConnection);
    return m_currentTransactionPath;
}

QDBusObjectPath TransactionManager::PlanPackageTransaction(const QString &action, const QVariantList &targets, const QVariantMap &options) {
    if (targets.isEmpty() || targets.size() > 128) {
        replyError(QDBusError::InvalidArgs, QStringLiteral("Ungültige Anzahl an Zielen (1-128 erlaubt)."));
        return QDBusObjectPath(QStringLiteral("/"));
    }

    TransactionIntent intent;
    if (action == QLatin1String("Install")) {
        intent.type = TransactionIntent::Type::Install;
    } else if (action == QLatin1String("Remove")) {
        intent.type = TransactionIntent::Type::Remove;
    } else if (action == QLatin1String("UpgradeAll")) {
        intent.type = TransactionIntent::Type::UpgradeAll;
    } else {
        replyError(QDBusError::InvalidArgs, QStringLiteral("Unbekannte Aktionsart."));
        return QDBusObjectPath(QStringLiteral("/"));
    }

    for (const auto &item : targets) {
        PackageRef ref;
        if (item.canConvert<QVariantMap>()) {
            QVariantMap map = item.toMap();
            ref.name = map.value(QStringLiteral("name")).toString();
            ref.arch = map.value(QStringLiteral("arch")).toString();
            ref.repoId = map.value(QStringLiteral("repoId")).toString();
            ref.version = map.value(QStringLiteral("version")).toString();
            ref.backend = map.value(QStringLiteral("backend")).toString();
        } else if (item.canConvert<QString>()) {
            ref.name = item.toString();
        }
        if (!ref.isValid()) {
            replyError(QDBusError::InvalidArgs, QStringLiteral("Ungültige Paketangabe im Ziel."));
            return QDBusObjectPath(QStringLiteral("/"));
        }
        intent.targets.append(ref);
    }
    intent.options = options;

    QString polkitAction = (intent.type == TransactionIntent::Type::Remove) ? PolicyGate::ActionRemove :
                           (intent.type == TransactionIntent::Type::Install) ? PolicyGate::ActionInstall :
                                                                               PolicyGate::ActionUpgrade;

    if (!beginAuthorized(polkitAction)) return QDBusObjectPath(QStringLiteral("/"));

    m_currentIntent = intent;
    m_currentPlan = TransactionPlan();
    m_currentPlan.id = m_currentTransactionPath.path();

    QMetaObject::invokeMethod(m_backend.get(), [this, intent]() {
        m_backend->planPackageTransaction(intent);
    }, Qt::QueuedConnection);

    return m_currentTransactionPath;
}

QString TransactionManager::GetTransactionSnapshot(const QDBusObjectPath &transactionPath) {
    return AttachTransaction(transactionPath, static_cast<qint64>(m_sequenceCounter));
}

void TransactionManager::CommitPlan(const QDBusObjectPath &transactionPath, const QString &planRevision) {
    resetIdleTimer();
    if (transactionPath.path() != m_currentTransactionPath.path()) {
        replyError(QDBusError::InvalidArgs, QStringLiteral("Invalid or expired transaction path"));
        return;
    }

    if (!m_planReady || m_backendBusy || !m_hasActiveTransaction) {
        replyError(QDBusError::Failed, QStringLiteral("Der Plan ist noch nicht bereit oder wurde bereits ausgeführt."));
        return;
    }
    if (!ownsTransaction()) return;

    if (!planRevision.isEmpty() && !m_currentPlan.planRevision.isEmpty() && planRevision != m_currentPlan.planRevision) {
        replyError(QDBusError::Failed, QStringLiteral("Planrevision stimmt nicht überein (Plan geändert). Bitte neu bestätigen."));
        return;
    }

    if (calledFromDBus() && !m_policyGate.checkAuthorization(m_commitAction, message().service())) {
        replyError(QDBusError::AccessDenied, QStringLiteral("Polkit authorization denied for Commit"));
        return;
    }

    m_backendBusy = true; m_planReady = false;
    QMetaObject::invokeMethod(m_backend.get(), [this, planRevision]() {
        m_backend->commitPlan(planRevision);
    }, Qt::QueuedConnection);
}

void TransactionManager::DiscardPlan(const QDBusObjectPath &transactionPath) {
    resetIdleTimer();
    if (transactionPath.path() != m_currentTransactionPath.path()) {
        replyError(QDBusError::InvalidArgs, QStringLiteral("Invalid or expired transaction path"));
        return;
    }
    if (!ownsTransaction()) return;

    m_backendBusy = false;
    m_planReady = false;
    m_hasActiveTransaction = false;
    m_currentPlan = TransactionPlan();
    m_currentIntent = TransactionIntent();
    m_ownerUid.reset();

    QMetaObject::invokeMethod(m_backend.get(), &Backend::discardPlan, Qt::QueuedConnection);
    resetIdleTimer();
}

QString TransactionManager::AttachTransaction(const QDBusObjectPath &transactionPath, qint64 lastSeenSequence) {
    resetIdleTimer();
    if (transactionPath.path() != m_currentTransactionPath.path()) {
        return QStringLiteral("{}");
    }

    TransactionSnapshot snap;
    snap.transactionPath = m_currentTransactionPath.path();
    snap.intent = m_currentIntent;
    snap.phase = m_progressModel.currentPhase();
    snap.plan = m_currentPlan;
    snap.canCancel = m_progressModel.isCancellable();
    snap.sequenceNumber = m_sequenceCounter;
    snap.progressPercent = static_cast<int>(m_progressModel.totalProgress() * 100);
    snap.timestamp = QDateTime::currentDateTime();
    snap.callerUid = m_ownerUid.value_or(0);
    snap.active = m_hasActiveTransaction && (m_backendBusy || m_planReady);

    if (lastSeenSequence >= 0 && lastSeenSequence < static_cast<qint64>(m_sequenceCounter)) {
        for (const QString &evStr : m_eventHistory) {
            QJsonDocument doc = QJsonDocument::fromJson(evStr.toUtf8());
            if (doc.isObject()) {
                qint64 seq = doc.object().value(QStringLiteral("seq")).toInteger(0);
                if (seq > lastSeenSequence) {
                    snap.missedEvents.append(evStr);
                }
            }
        }
    }

    return QString::fromUtf8(QJsonDocument(snap.toJson()).toJson(QJsonDocument::Compact));
}

void TransactionManager::Commit(const QDBusObjectPath &transactionPath) {
    resetIdleTimer();
    if (transactionPath.path() != m_currentTransactionPath.path()) {
        replyError(QDBusError::InvalidArgs, QStringLiteral("Invalid or expired transaction path"));
        return;
    }

    if (!m_planReady || m_backendBusy || !m_hasActiveTransaction) {
        replyError(QDBusError::Failed, QStringLiteral("Der Plan ist noch nicht bereit oder wurde bereits ausgeführt.")); return;
    }
    if (!ownsTransaction()) return;
    if (calledFromDBus() && !m_policyGate.checkAuthorization(m_commitAction, message().service())) {
        replyError(QDBusError::AccessDenied, QStringLiteral("Polkit authorization denied for Commit"));
        return;
    }

    m_backendBusy = true; m_planReady = false;
    QMetaObject::invokeMethod(m_backend.get(), &Backend::commit, Qt::QueuedConnection);
}

void TransactionManager::Cancel(const QDBusObjectPath &transactionPath) {
    resetIdleTimer();
    if (transactionPath.path() != m_currentTransactionPath.path()) {
        replyError(QDBusError::InvalidArgs, QStringLiteral("Invalid or expired transaction path"));
        return;
    }

    if (!ownsTransaction()) return;
    if (!m_progressModel.isCancellable()) {
        replyError(QDBusError::Failed, QStringLiteral("Die Transaktion kann in der aktuellen Phase nicht abgebrochen werden."));
        return;
    }

    QMetaObject::invokeMethod(m_backend.get(), &Backend::cancel, Qt::QueuedConnection);
}

void TransactionManager::AnswerQuestion(const QDBusObjectPath &transactionPath, const QString &id, const QString &json) {
    resetIdleTimer();
    if (transactionPath.path() != m_currentTransactionPath.path()) {
        replyError(QDBusError::InvalidArgs, QStringLiteral("Invalid transaction path"));
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
    if (m_hasActiveTransaction && (m_backendBusy || m_planReady)) {
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
