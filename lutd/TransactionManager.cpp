#include "TransactionManager.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusArgument>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <unistd.h>
#include <limits>
#include "liblut/backend/Validation.h"
#include <QCoreApplication>
#include <QDebug>
#include "liblut/detect/DistroDetect.h"
#include "liblut/backend/dnf5/Dnf5Backend.h"
#include "liblut/backend/alpm/AlpmBackend.h"
#include "liblut/backend/apt/AptBackend.h"
#include "liblut/repository/RepoManager.h"

namespace lut {

TransactionManager::TransactionManager(QObject *parent)
    : QObject(parent) {
    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(120000); // 120 Sekunden Idle-Exit
    connect(&m_idleTimer, &QTimer::timeout, this, &TransactionManager::onIdleTimeout);
}

TransactionManager::~TransactionManager() {
    m_inhibitor.releaseLock();
    if (m_backendThread.isRunning()) {
        auto *backend = m_backend.release();
        auto *flatpakBackend = m_flatpakBackend.release();
        auto *snapBackend = m_snapBackend.release();
        auto *pacstallBackend = m_pacstallBackend.release();
        QMetaObject::invokeMethod(backend, [backend, flatpakBackend, snapBackend, pacstallBackend] {
            delete backend;
            delete flatpakBackend;
            delete snapBackend;
            delete pacstallBackend;
        }, Qt::BlockingQueuedConnection);
        m_backendThread.quit();
        m_backendThread.wait();
    }
}

bool TransactionManager::init() {
    QString error;
    m_backend = Backend::createForHost(&error);
    if (!m_backend) { qCritical() << error; return false; }

    connect(m_backend.get(), &Backend::eventEmitted, this, &TransactionManager::onBackendEvent);
    connect(m_backend.get(), &Backend::eventEmitted, &m_progressModel, &ProgressModel::processEvent);

    if (FlatpakBackend::isFlatpakAvailable()) {
        m_flatpakBackend = std::make_unique<FlatpakBackend>();
        connect(m_flatpakBackend.get(), &Backend::eventEmitted, this, &TransactionManager::onBackendEvent);
        connect(m_flatpakBackend.get(), &Backend::eventEmitted, &m_progressModel, &ProgressModel::processEvent);
    }

    if (SnapBackend::isSnapAvailable()) {
        m_snapBackend = std::make_unique<SnapBackend>();
        connect(m_snapBackend.get(), &Backend::eventEmitted, this, &TransactionManager::onBackendEvent);
        connect(m_snapBackend.get(), &Backend::eventEmitted, &m_progressModel, &ProgressModel::processEvent);
    }

    if (PacstallBackend::isPacstallAvailable()) {
        m_pacstallBackend = std::make_unique<PacstallBackend>();
        connect(m_pacstallBackend.get(), &Backend::eventEmitted, this, &TransactionManager::onBackendEvent);
        connect(m_pacstallBackend.get(), &Backend::eventEmitted, &m_progressModel, &ProgressModel::processEvent);
    }

    startBackendThread();
    resetIdleTimer();
    return true;
}

void TransactionManager::setBackendForTest(std::unique_ptr<Backend> backend, bool threaded) {
    m_backend = std::move(backend);
    if (m_backend) m_capabilities = m_backend->capabilities();
    if (m_backend) {
        connect(m_backend.get(), &Backend::eventEmitted, this, &TransactionManager::onBackendEvent);
        connect(m_backend.get(), &Backend::eventEmitted, &m_progressModel, &ProgressModel::processEvent);
        if (threaded) startBackendThread();
    }
}

void TransactionManager::setFlatpakBackendForTest(std::unique_ptr<FlatpakBackend> backend) {
    m_flatpakBackend = std::move(backend);
    if (m_flatpakBackend) {
        connect(m_flatpakBackend.get(), &Backend::eventEmitted, this, &TransactionManager::onBackendEvent);
        connect(m_flatpakBackend.get(), &Backend::eventEmitted, &m_progressModel, &ProgressModel::processEvent);
        for (const auto &src : m_flatpakBackend->capabilities().sources) {
            bool found = false;
            for (const auto &existing : m_capabilities.sources) {
                if (existing.source == src.source) { found = true; break; }
            }
            if (!found) {
                m_capabilities.sources.append(src);
            }
        }
    }
}

void TransactionManager::setSnapBackendForTest(std::unique_ptr<SnapBackend> backend) {
    m_snapBackend = std::move(backend);
    if (m_snapBackend) {
        connect(m_snapBackend.get(), &Backend::eventEmitted, this, &TransactionManager::onBackendEvent);
        connect(m_snapBackend.get(), &Backend::eventEmitted, &m_progressModel, &ProgressModel::processEvent);
        for (const auto &src : m_snapBackend->capabilities().sources) {
            bool found = false;
            for (const auto &existing : m_capabilities.sources) {
                if (existing.source == src.source) { found = true; break; }
            }
            if (!found) {
                m_capabilities.sources.append(src);
            }
        }
    }
}

void TransactionManager::setPacstallBackendForTest(std::unique_ptr<PacstallBackend> backend) {
    m_pacstallBackend = std::move(backend);
    if (m_pacstallBackend) {
        connect(m_pacstallBackend.get(), &Backend::eventEmitted, this, &TransactionManager::onBackendEvent);
        connect(m_pacstallBackend.get(), &Backend::eventEmitted, &m_progressModel, &ProgressModel::processEvent);
        for (const auto &src : m_pacstallBackend->capabilities().sources) {
            bool found = false;
            for (const auto &existing : m_capabilities.sources) {
                if (existing.source == src.source) { found = true; break; }
            }
            if (!found) {
                m_capabilities.sources.append(src);
            }
        }
    }
}

void TransactionManager::startBackendThread() {
    m_capabilities = m_backend->capabilities();
    if (m_flatpakBackend) {
        for (const auto &src : m_flatpakBackend->capabilities().sources) {
            bool found = false;
            for (const auto &existing : m_capabilities.sources) {
                if (existing.source == src.source) { found = true; break; }
            }
            if (!found) {
                m_capabilities.sources.append(src);
            }
        }
    }
    if (m_snapBackend) {
        for (const auto &src : m_snapBackend->capabilities().sources) {
            bool found = false;
            for (const auto &existing : m_capabilities.sources) {
                if (existing.source == src.source) { found = true; break; }
            }
            if (!found) {
                m_capabilities.sources.append(src);
            }
        }
    }
    if (m_pacstallBackend) {
        for (const auto &src : m_pacstallBackend->capabilities().sources) {
            bool found = false;
            for (const auto &existing : m_capabilities.sources) {
                if (existing.source == src.source) { found = true; break; }
            }
            if (!found) {
                m_capabilities.sources.append(src);
            }
        }
    }
    qRegisterMetaType<lut::Event>();
    m_backendThread.setObjectName(QStringLiteral("native-package-transactions"));
    m_backend->moveToThread(&m_backendThread);
    if (m_flatpakBackend) {
        m_flatpakBackend->moveToThread(&m_backendThread);
    }
    if (m_snapBackend) {
        m_snapBackend->moveToThread(&m_backendThread);
    }
    if (m_pacstallBackend) {
        m_pacstallBackend->moveToThread(&m_backendThread);
    }
    m_backendThread.start();
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
    return std::numeric_limits<quint32>::max(); // Unknown callers must never inherit the daemon UID.
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
    if (!m_hasActiveTransaction && m_pendingPacstallChecks == 0) {
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
    m_commitStarted = false;
    // Plan und Absicht gehören zur einzelnen Transaktion. Blieben sie stehen,
    // meldete ein Wiederanbinden einen Systemupdate-Plan als Store-Plan.
    m_currentPlan = TransactionPlan();
    m_currentIntent = TransactionIntent();
    m_eventHistory.clear();
    m_progressModel.reset();
    resetIdleTimer();
}

void TransactionManager::onBackendEvent(const lut::Event &backendEvent) {
    m_sequenceCounter++;
    lut::Event event = backendEvent;

    if (auto *ready = std::get_if<PlanReady>(&event)) {
        const auto &plan = *ready;
        m_backendBusy = false;
        m_planReady = true;
        m_currentPlan.ops = plan.ops;
        m_currentPlan.downloadBytes = plan.downloadBytes;
        m_currentPlan.installedSizeDelta = plan.installedSizeDelta;
        m_currentPlan.warnings = plan.warnings;
        m_currentPlan.planRevision = plan.planRevision.isEmpty() ? m_currentPlan.calculateFingerprint() : plan.planRevision;
        // Backends ohne eigene Revision (DNF5): der Client muss genau diese
        // Revision an CommitPlan zurückgeben, also mit dem Ereignis senden.
        ready->planRevision = m_currentPlan.planRevision;
        // Ein leerer Plan hat nichts zu bestätigen. Hielte der Daemon ihn fest,
        // lehnte er jede weitere Aktion mit "läuft bereits" ab – bis zum Neustart.
        if (plan.ops.isEmpty()) {
            m_planReady = false;
            m_hasActiveTransaction = false;
            resetIdleTimer();
        }
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

    QJsonObject json = serializeEvent(event, m_sequenceCounter, m_currentTransactionPath.path());
    QString jsonStr = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));

    // Ring-Puffer füllen
    m_eventHistory.append(jsonStr);
    if (m_eventHistory.size() > 5000) {
        m_eventHistory.removeFirst();
    }

    emit TransactionEvent(m_currentTransactionPath, jsonStr);
}

QString TransactionManager::GetCapabilities() {
    resetIdleTimer();
    Capabilities cap = m_capabilities;
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
    QJsonArray sourcesArr;
    for (const auto &src : cap.sources) {
        sourcesArr.append(src.toJson());
    }
    obj[QStringLiteral("sources")] = sourcesArr;
    QJsonArray commands;
    if (qobject_cast<Dnf5Backend *>(m_backend.get()))
        for (const auto &command : Dnf5Backend::transactionCommands()) commands.append(command);
    obj[QStringLiteral("dnf5Commands")] = commands;
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

bool TransactionManager::authorize(const QString &action, const QString &service) {
    return m_policyGate.checkAuthorization(action, service);
}

bool TransactionManager::ownsTransaction() {
    if (calledFromDBus() && message().service() == m_owner) return true;
    quint32 callerUid = getCallerUid();
    if (m_ownerUid.has_value() && callerUid == *m_ownerUid) return true;
    if (calledFromDBus() && authorize(PolicyGate::ActionManageOthers, message().service())) return true;
    replyError(QDBusError::AccessDenied, QStringLiteral("Die Transaktion gehört einem anderen Aufrufer."));
    return false;
}

bool TransactionManager::beginAuthorized(const QString &action) {
    if (getCallerUid() == std::numeric_limits<quint32>::max()) {
        replyError(QDBusError::AccessDenied, QStringLiteral("Aufrufer konnte nicht ermittelt werden."));
        return false;
    }
    if (m_backendBusy) {
        replyError(QDBusError::Failed, QStringLiteral("Eine Paketoperation läuft bereits.")); return false;
    }
    // Ein noch unbestätigter Plan läuft nicht: sein Eigentümer darf ihn durch einen
    // neuen ersetzen (erneutes "Prüfen", Tray-Check). Fremde Aufrufer bleiben gesperrt.
    if (m_hasActiveTransaction && !ownsTransaction()) return false;
    if (calledFromDBus() && !authorize(action, message().service())) {
        replyError(QDBusError::AccessDenied, QStringLiteral("Polkit authorization denied")); return false;
    }
    m_owner = calledFromDBus() ? message().service() : QStringLiteral("local");
    m_ownerUid = getCallerUid();
    m_commitAction = action;
    registerNewTransaction(); return true;
}

QDBusObjectPath TransactionManager::RefreshMetadata() {
    if (!beginAuthorized(PolicyGate::ActionRefresh)) return QDBusObjectPath(QStringLiteral("/"));
    m_activeBackend = m_backend.get();
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
    if (!opt.packages.isEmpty() && (!m_capabilities.partialUpgrade || !Validation::areValidPackageNames(opt.packages))) {
        replyError(QDBusError::InvalidArgs, QStringLiteral("Ungültige Paketauswahl.")); return QDBusObjectPath(QStringLiteral("/"));
    }
    if (!beginAuthorized(PolicyGate::ActionRefresh)) return QDBusObjectPath(QStringLiteral("/"));
    m_commitAction = PolicyGate::ActionUpgrade;
    m_currentIntent.type = TransactionIntent::Type::UpgradeAll;
    m_activeBackend = m_backend.get();
    QMetaObject::invokeMethod(m_backend.get(), [this, opt] { m_backend->planUpgradeAll(opt); }, Qt::QueuedConnection);
    return m_currentTransactionPath;
}

QDBusObjectPath TransactionManager::PlanInstall(const QStringList &names) {
    if (!Validation::areValidPackageNames(names)) {
        replyError(QDBusError::InvalidArgs, QStringLiteral("Ungültige Paketnamen.")); return QDBusObjectPath(QStringLiteral("/"));
    }
    if (!beginAuthorized(PolicyGate::ActionInstall)) return QDBusObjectPath(QStringLiteral("/"));
    m_currentIntent.type = TransactionIntent::Type::Install;
    m_activeBackend = m_backend.get();
    QMetaObject::invokeMethod(m_backend.get(), [this, names] { m_backend->planInstall(names); }, Qt::QueuedConnection);
    return m_currentTransactionPath;
}

QDBusObjectPath TransactionManager::PlanRemove(const QStringList &names) {
    if (!Validation::areValidPackageNames(names)) {
        replyError(QDBusError::InvalidArgs, QStringLiteral("Ungültige Paketnamen.")); return QDBusObjectPath(QStringLiteral("/"));
    }
    if (!beginAuthorized(PolicyGate::ActionRemove)) return QDBusObjectPath(QStringLiteral("/"));
    m_currentIntent.type = TransactionIntent::Type::Remove;
    m_activeBackend = m_backend.get();
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
    m_currentIntent.type = TransactionIntent::Type::CustomCommand;
    m_activeBackend = backend;
    QMetaObject::invokeMethod(backend, [backend, command, arguments, opt] { backend->planCommand(command, arguments, opt); }, Qt::QueuedConnection);
    return m_currentTransactionPath;
}

QDBusObjectPath TransactionManager::CleanCache() {
    auto *backend = qobject_cast<Dnf5Backend *>(m_backend.get());
    if (!backend) {
        replyError(QDBusError::NotSupported, QStringLiteral("Cachebereinigung ist für dieses Backend nicht angebunden.")); return QDBusObjectPath(QStringLiteral("/"));
    }
    if (!beginAuthorized(PolicyGate::ActionRemove)) return QDBusObjectPath(QStringLiteral("/"));
    m_activeBackend = backend;
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
        if (item.canConvert<QVariantMap>() || item.metaType().id() == qMetaTypeId<QDBusArgument>()) {
            QVariantMap map = item.metaType().id() == qMetaTypeId<QDBusArgument>()
                ? qdbus_cast<QVariantMap>(item.value<QDBusArgument>()) : item.toMap();
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

    Backend *selectedBackend = m_backend.get();
    if (!intent.targets.isEmpty()) {
        const QString backendType = intent.targets.first().backend;
        for (const auto &t : intent.targets) {
            if (!t.backend.isEmpty() && t.backend != backendType) {
                replyError(QDBusError::InvalidArgs, QStringLiteral("Transaktionen über mehrere Paketquellen gleichzeitig werden nicht unterstützt."));
                return QDBusObjectPath(QStringLiteral("/"));
            }
        }
        if (backendType == QLatin1String("flatpak")) {
            if (!m_flatpakBackend) {
                replyError(QDBusError::NotSupported, QStringLiteral("Flatpak-Backend ist auf diesem System nicht verfügbar."));
                return QDBusObjectPath(QStringLiteral("/"));
            }
            selectedBackend = m_flatpakBackend.get();
        } else if (backendType == QLatin1String("snap")) {
            if (!m_snapBackend) {
                replyError(QDBusError::NotSupported, QStringLiteral("Snap-Backend ist auf diesem System nicht verfügbar."));
                return QDBusObjectPath(QStringLiteral("/"));
            }
            selectedBackend = m_snapBackend.get();
        } else if (backendType == QLatin1String("pacstall")) {
            if (!m_pacstallBackend && !PacstallBackend::isPacstallAvailable()) {
                replyError(QDBusError::NotSupported, QStringLiteral("Pacstall-Backend ist auf diesem System nicht verfügbar."));
                return QDBusObjectPath(QStringLiteral("/"));
            }
            selectedBackend = ensurePacstallBackend();
        }
    }
    m_activeBackend = selectedBackend;

    m_currentIntent = intent;
    m_currentPlan = TransactionPlan();
    m_currentPlan.id = m_currentTransactionPath.path();

    QMetaObject::invokeMethod(m_activeBackend, [this, intent]() {
        m_activeBackend->planPackageTransaction(intent);
    }, Qt::QueuedConnection);

    return m_currentTransactionPath;
}

PacstallBackend *TransactionManager::ensurePacstallBackend() {
    if (!m_pacstallBackend) {
        // Erst nachträglich angelegt (pacstall wurde nach dem Start des Daemons
        // installiert): trotzdem in den Backend-Thread, sonst liefen Download
        // und Bau im Hauptthread und blockierten den ganzen D-Bus-Dienst.
        m_pacstallBackend = std::make_unique<PacstallBackend>();
        connect(m_pacstallBackend.get(), &Backend::eventEmitted, this, &TransactionManager::onBackendEvent);
        connect(m_pacstallBackend.get(), &Backend::eventEmitted, &m_progressModel, &ProgressModel::processEvent);
        if (m_backendThread.isRunning()) m_pacstallBackend->moveToThread(&m_backendThread);
    }
    return m_pacstallBackend.get();
}

QString TransactionManager::PacstallCheck() {
    resetIdleTimer();
    if (!PacstallBackend::isPacstallAvailable() && !m_pacstallBackend) {
        return QStringLiteral("[]");
    }
    if (calledFromDBus() && !authorize(PolicyGate::ActionRefresh, message().service())) {
        replyError(QDBusError::AccessDenied, QStringLiteral("Polkit authorization denied for PacstallCheck"));
        return QStringLiteral("[]");
    }
    PacstallBackend *backend = ensurePacstallBackend();
    if (!calledFromDBus()) {
        QString error;
        const auto updates = backend->checkUpdates(&error);
        return error.isEmpty() ? PacstallBackend::updatesToJson(updates) : QString();
    }

    // pacstall -Lu fragt jedes Paket im Netz ab und dauert. Im Backend-Thread
    // ausführen und verzögert antworten, statt den Dienst anzuhalten.
    setDelayedReply(true);
    const QDBusMessage request = message();
    QDBusConnection bus = connection();
    ++m_pendingPacstallChecks;
    QMetaObject::invokeMethod(backend, [this, backend, request, bus]() mutable {
        QString error;
        const auto updates = backend->checkUpdates(&error);
        bus.send(error.isEmpty() ? request.createReply(PacstallBackend::updatesToJson(updates))
                                 : request.createErrorReply(QDBusError::Failed, error));
        QMetaObject::invokeMethod(this, [this] {
            --m_pendingPacstallChecks;
            resetIdleTimer();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    return QString();
}

QDBusObjectPath TransactionManager::PlanPacstallUpgrade(const QStringList &names) {
    resetIdleTimer();
    if (names.isEmpty() || !Validation::areValidPackageNames(names)) {
        replyError(QDBusError::InvalidArgs, QStringLiteral("Ungültige Paketnamen."));
        return QDBusObjectPath(QStringLiteral("/"));
    }
    if (!PacstallBackend::isPacstallAvailable() && !m_pacstallBackend) {
        replyError(QDBusError::NotSupported, QStringLiteral("Pacstall ist auf diesem System nicht verfügbar."));
        return QDBusObjectPath(QStringLiteral("/"));
    }
    if (!beginAuthorized(PolicyGate::ActionRefresh)) return QDBusObjectPath(QStringLiteral("/"));
    m_commitAction = PolicyGate::ActionPacstall;
    m_activeBackend = ensurePacstallBackend();
    m_currentIntent = TransactionIntent();
    m_currentIntent.type = TransactionIntent::Type::UpgradeAll;
    for (const auto &n : names) {
        PackageRef ref;
        ref.backend = QStringLiteral("pacstall");
        ref.name = n;
        m_currentIntent.targets.append(ref);
    }
    m_currentPlan = TransactionPlan();
    m_currentPlan.id = m_currentTransactionPath.path();
    quint32 callerUid = getCallerUid();
    auto *backend = m_pacstallBackend.get();
    QMetaObject::invokeMethod(backend, [backend, names, callerUid] {
        backend->planPacstallUpgrade(names, callerUid);
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

    if (planRevision.isEmpty() || planRevision != m_currentPlan.planRevision) {
        replyError(QDBusError::Failed, QStringLiteral("Planrevision stimmt nicht überein (Plan geändert). Bitte neu bestätigen."));
        return;
    }

    if (calledFromDBus() && !authorize(m_commitAction, message().service())) {
        replyError(QDBusError::AccessDenied, QStringLiteral("Polkit authorization denied for Commit"));
        return;
    }

    m_backendBusy = true; m_planReady = false; m_commitStarted = true;
    Backend *backend = m_activeBackend ? m_activeBackend : m_backend.get();
    QMetaObject::invokeMethod(backend, [backend, planRevision]() {
        backend->commitPlan(planRevision);
    }, Qt::QueuedConnection);
}

void TransactionManager::DiscardPlan(const QDBusObjectPath &transactionPath) {
    resetIdleTimer();
    if (m_backendBusy) {
        replyError(QDBusError::Failed, QStringLiteral("Eine laufende Transaktion kann nicht verworfen werden."));
        return;
    }
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

    Backend *backend = m_activeBackend ? m_activeBackend : m_backend.get();
    m_activeBackend = nullptr;
    QMetaObject::invokeMethod(backend, &Backend::discardPlan, Qt::QueuedConnection);
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
    // Zwischen Bestätigung und erstem Backend-Ereignis steht die Phase noch
    // auf Idle; ein neu verbundenes Fenster zeigte sonst den Plan statt "läuft".
    if (m_backendBusy && snap.phase == Phase::Idle)
        snap.phase = m_commitStarted ? Phase::Commit : Phase::Resolve;
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
    if (calledFromDBus() && !authorize(m_commitAction, message().service())) {
        replyError(QDBusError::AccessDenied, QStringLiteral("Polkit authorization denied for Commit"));
        return;
    }

    m_backendBusy = true; m_planReady = false; m_commitStarted = true;
    Backend *backend = m_activeBackend ? m_activeBackend : m_backend.get();
    QMetaObject::invokeMethod(backend, &Backend::commit, Qt::QueuedConnection);
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

    Backend *backend = m_activeBackend ? m_activeBackend : m_backend.get();
    QMetaObject::invokeMethod(backend, &Backend::cancel, Qt::QueuedConnection);
}

void TransactionManager::AnswerQuestion(const QDBusObjectPath &transactionPath, const QString &id, const QString &json) {
    resetIdleTimer();
    if (transactionPath.path() != m_currentTransactionPath.path()) {
        replyError(QDBusError::InvalidArgs, QStringLiteral("Invalid transaction path"));
        return;
    }

    if (!ownsTransaction()) return;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    Backend *backend = m_activeBackend ? m_activeBackend : m_backend.get();
    QMetaObject::invokeMethod(backend, [backend, id, doc]() {
        backend->answerQuestion(id, doc.object());
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

QString TransactionManager::GetRepositories() {
    resetIdleTimer();
    return RepoManager::instance().getRepositoriesJson();
}

QString TransactionManager::GetRepositoryPresets() {
    resetIdleTimer();
    return RepoManager::instance().getPresetsJson();
}

bool TransactionManager::AddRepository(const QString &repoJson) {
    resetIdleTimer();
    if (calledFromDBus() && !authorize(PolicyGate::ActionManageRepositories, message().service())) {
        replyError(QDBusError::AccessDenied, QStringLiteral("Polkit authorization denied"));
        return false;
    }
    QString error;
    bool ok = RepoManager::instance().addRepositoryFromJson(repoJson, &error);
    if (!ok) {
        replyError(QDBusError::Failed, error);
        return false;
    }
    return true;
}

bool TransactionManager::AddRepositoryPreset(const QString &presetId) {
    resetIdleTimer();
    if (calledFromDBus() && !authorize(PolicyGate::ActionManageRepositories, message().service())) {
        replyError(QDBusError::AccessDenied, QStringLiteral("Polkit authorization denied"));
        return false;
    }
    // Einrichten und Entfernen installieren bzw. entfernen Pakete (RPM Fusion,
    // Terra): nicht parallel zu einer laufenden Paketoperation.
    if (m_backendBusy || m_hasActiveTransaction) {
        replyError(QDBusError::Failed, QStringLiteral("Eine Paketoperation läuft bereits."));
        return false;
    }
    QString error;
    bool ok = RepoManager::instance().addPreset(presetId, &error);
    if (!ok) {
        replyError(QDBusError::Failed, error);
        return false;
    }
    return true;
}

bool TransactionManager::RemoveRepository(const QString &repoId) {
    resetIdleTimer();
    if (calledFromDBus() && !authorize(PolicyGate::ActionManageRepositories, message().service())) {
        replyError(QDBusError::AccessDenied, QStringLiteral("Polkit authorization denied"));
        return false;
    }
    // Einrichten und Entfernen installieren bzw. entfernen Pakete (RPM Fusion,
    // Terra): nicht parallel zu einer laufenden Paketoperation.
    if (m_backendBusy || m_hasActiveTransaction) {
        replyError(QDBusError::Failed, QStringLiteral("Eine Paketoperation läuft bereits."));
        return false;
    }
    QString error;
    bool ok = RepoManager::instance().removeRepository(repoId, &error);
    if (!ok) {
        replyError(QDBusError::Failed, error);
        return false;
    }
    return true;
}

bool TransactionManager::ToggleRepository(const QString &repoId, bool enabled) {
    resetIdleTimer();
    if (calledFromDBus() && !authorize(PolicyGate::ActionManageRepositories, message().service())) {
        replyError(QDBusError::AccessDenied, QStringLiteral("Polkit authorization denied"));
        return false;
    }
    QString error;
    bool ok = RepoManager::instance().toggleRepository(repoId, enabled, &error);
    if (!ok) {
        replyError(QDBusError::Failed, error);
        return false;
    }
    return true;
}

} // namespace lut
