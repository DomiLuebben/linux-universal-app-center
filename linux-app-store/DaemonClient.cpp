#include <QDBusReply>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include "DaemonClient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QRegularExpression>

namespace lut {

DaemonClient::DaemonClient(QObject *parent) : DaemonClient(QDBusConnection::systemBus(), parent) {}

DaemonClient::DaemonClient(const QDBusConnection &connection, QObject *parent)
    : QObject(parent), m_bus(connection), m_repositoriesModel(this, this) {
    // Einziger Weg für LogLine ins Protokoll. handleEvent darf LogLine NICHT
    // zusätzlich anhängen: processEvent() läuft dort am Ende ohnehin durch und
    // löst logAdded aus – sonst steht jede Worker-Zeile doppelt im Protokoll.
    connect(&m_progressModel, &ProgressModel::logAdded, &m_logModel, &LogModel::appendLog);
    connect(&m_progressModel, &ProgressModel::questionReceived, this, &DaemonClient::questionPrompt);
    connect(&m_installedModel, &InstalledModel::cleanupRequested, this, [this](const QString &command) {
        if (command == QLatin1String("clean")) { cleanCache(); return; }
        // "autoremove" gibt es nur als DNF5-Befehl. Unter Arch lief der Knopf
        // "Waisen entfernen" deshalb ins Leere ("Diese DNF5-Aktion ist nicht
        // verfügbar"). Dort werden die erkannten Waisen über den normalen
        // Entfernen-Weg geplant – mit Vorschau und Bestätigung.
        if (command == QLatin1String("autoremove") && !m_dnf5Commands.contains(command)) {
            const QStringList orphans = m_installedModel.orphanNames();
            if (orphans.isEmpty()) return;
            planStoreRemove(orphans);
            return;
        }
        planDnf5(command, QString());
    });
    connect(&m_historyModel, &HistoryModel::undoRequested, this, [this](int id) { planDnf5(QStringLiteral("history undo"), QString::number(id)); });
}

DaemonClient::~DaemonClient() = default;

bool DaemonClient::init(const QString &replayFixture, double replaySpeed) {
    if (!replayFixture.isEmpty()) {
        qInfo() << "DaemonClient: Running in REPLAY mode with fixture:" << replayFixture;
        m_replayBackend = std::make_unique<ReplayBackend>(replayFixture, replaySpeed, this);
        connect(m_replayBackend.get(), &Backend::eventEmitted, this, &DaemonClient::handleEvent);
        m_capabilities = m_replayBackend->capabilities();
        m_lastCheckedString = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm"));
        emit modeChanged();
        emit capabilitiesChanged();
        emit connectionChanged();
        return true;
    }

    connectToDaemon();
    return isConnected();
}

void DaemonClient::connectToDaemon() {
    auto bus = m_bus;

    m_daemonIface = std::make_unique<QDBusInterface>(
        QStringLiteral("org.linuxupdatetool.Daemon1"),
        QStringLiteral("/org/linuxupdatetool/Daemon1"),
        QStringLiteral("org.linuxupdatetool.Daemon1"),
        bus,
        this
    );

    bus.connect(
        QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("NameOwnerChanged"),
        this,
        SLOT(onDbusNameOwnerChanged(QString, QString, QString))
    );

    if (m_daemonIface->isValid()) {
        m_connected = true;
        bus.connect(
            QStringLiteral("org.linuxupdatetool.Daemon1"),
            QStringLiteral("/org/linuxupdatetool/Daemon1"),
            QStringLiteral("org.linuxupdatetool.Daemon1"),
            QStringLiteral("TransactionEvent"),
            this,
            SLOT(onDbusTransactionEvent(QDBusObjectPath, QString))
        );
        queryCapabilities();
        checkForActiveTransaction();
        m_repositoriesModel.refresh();
        emit connectionChanged();
    } else {
        qWarning() << "DaemonClient: Could not connect to lutd on D-Bus:" << m_daemonIface->lastError().message();
    }
}

void DaemonClient::queryCapabilities() {
    if (!m_daemonIface || !m_daemonIface->isValid()) return;
    QDBusReply<QString> reply = m_daemonIface->call(QStringLiteral("GetCapabilities"));
    if (reply.isValid()) {
        QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            m_capabilities.partialUpgrade = obj.value(QStringLiteral("partialUpgrade")).toBool(true);
            m_capabilities.downgrade = obj.value(QStringLiteral("downgrade")).toBool();
            m_capabilities.historyUndo = obj.value(QStringLiteral("historyUndo")).toBool();
            m_capabilities.changelogs = obj.value(QStringLiteral("changelogs")).toBool();
            m_capabilities.securityFlag = obj.value(QStringLiteral("securityFlag")).toBool();
            m_capabilities.offlineUpdate = obj.value(QStringLiteral("offlineUpdate")).toBool();
            m_capabilities.autoremove = obj.value(QStringLiteral("autoremove")).toBool();
            m_capabilities.parallelDownloads = obj.value(QStringLiteral("parallelDownloads")).toBool();
            m_capabilities.degraded = obj.value(QStringLiteral("degraded")).toBool();
            m_capabilities.catalogQuery = obj.value(QStringLiteral("catalogQuery")).toBool(false);
            m_capabilities.install = obj.value(QStringLiteral("install")).toBool(false);
            m_capabilities.remove = obj.value(QStringLiteral("remove")).toBool(false);
            m_capabilities.installRequiresFullUpgrade = obj.value(QStringLiteral("installRequiresFullUpgrade")).toBool(false);
            m_capabilities.typedPackageTargets = obj.value(QStringLiteral("typedPackageTargets")).toBool(false);
            m_capabilities.transactionReattach = obj.value(QStringLiteral("transactionReattach")).toBool(false);
            m_capabilities.protocolVersion = obj.value(QStringLiteral("protocolVersion")).toInt(1);
            m_capabilities.sources.clear();
            const QJsonArray sourcesArr = obj.value(QStringLiteral("sources")).toArray();
            for (const auto &val : sourcesArr) {
                if (val.isObject()) {
                    m_capabilities.sources.append(SourceCapabilities::fromJson(val.toObject()));
                }
            }
            m_dnf5Commands.clear();
            for (const auto &command : obj.value(QStringLiteral("dnf5Commands")).toArray()) m_dnf5Commands.append(command.toString());
            emit capabilitiesChanged();
        }
    }
}

void DaemonClient::reconnect() {
    connectToDaemon();
}

void DaemonClient::onDbusNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner) {
    if (name != QLatin1String("org.linuxupdatetool.Daemon1")) return;
    if (newOwner.isEmpty()) {
        // lutd beendet sich nach 120 s Leerlauf und wird per D-Bus-Aktivierung
        // beim nächsten Aufruf neu gestartet. Das ist kein Verbindungsverlust:
        // früher galt danach jede App als "Aktion nicht unterstützt", bis der
        // Store neu gestartet wurde. Verloren ist die Verbindung nur, wenn der
        // Daemon nicht aktivierbar ist oder mitten in einer Operation verschwindet.
        if (daemonActivatable() && !m_busy && !m_hasPlan && !m_pendingTransaction) {
            qInfo() << "DaemonClient: lutd im Leerlauf beendet; startet beim nächsten Aufruf neu.";
            m_activeTransactionPath = {};
            return;
        }
        qWarning() << "DaemonClient: D-Bus service org.linuxupdatetool.Daemon1 disconnected.";
        m_connected = false;
        m_statusMessage = QStringLiteral("Verbindung zum Systemdienst verloren.");
        emit connectionChanged();
        emit statusChanged();
    } else if (oldOwner.isEmpty() && !newOwner.isEmpty()) {
        qInfo() << "DaemonClient: D-Bus service org.linuxupdatetool.Daemon1 appeared. Reconnecting...";
        connectToDaemon();
    }
}

// lutd beendet sich im Leerlauf. Danach meldet QDBusInterface::isValid() false,
// und der Aufruf ginge gar nicht erst raus. Vor jeder neuen Operation deshalb
// den Daemon per D-Bus-Aktivierung starten (lutd.service) und neu verbinden.
bool DaemonClient::ensureDaemon() {
    if (m_replayBackend) return false;
    // Nie verbunden (init() nicht gerufen, etwa in Unit-Tests): nichts starten.
    // Sonst aktivierten Tests auf dem Entwicklungsrechner den echten lutd.
    if (!m_daemonIface) return false;
    if (m_daemonIface->isValid()) return true;
    auto *bus = m_bus.interface();
    if (!bus) return false;
    const QString name = QStringLiteral("org.linuxupdatetool.Daemon1");
    const QDBusReply<bool> registered = bus->isServiceRegistered(name);
    if (!registered.isValid() || !registered.value()) {
        if (!daemonActivatable()) return false;
        const QDBusReply<void> started = bus->startService(name);
        if (!started.isValid()) {
            qWarning().noquote() << "DaemonClient: lutd konnte nicht gestartet werden:" << started.error().message();
            return false;
        }
    }
    connectToDaemon();
    return m_daemonIface && m_daemonIface->isValid();
}

bool DaemonClient::daemonActivatable() const {
    if (m_activatableOverride.has_value()) return *m_activatableOverride;
    auto *iface = m_bus.interface();
    if (!iface) return false;
    const QDBusReply<QStringList> names = iface->activatableServiceNames();
    return names.isValid() && names.value().contains(QStringLiteral("org.linuxupdatetool.Daemon1"));
}

void DaemonClient::checkForActiveTransaction() {
    if (!m_daemonIface || !m_daemonIface->isValid()) return;
    QDBusReply<QList<QDBusObjectPath>> reply = m_daemonIface->call(QStringLiteral("GetActiveTransactions"));
    if (reply.isValid() && !reply.value().isEmpty()) {
        const QDBusObjectPath path = reply.value().first();
        reattachToTransaction(path);
    }
}

bool DaemonClient::reattachToTransaction(const QDBusObjectPath &path) {
    if (!m_daemonIface || !m_daemonIface->isValid() || path.path().isEmpty()) return false;

    QDBusReply<QString> reply = m_daemonIface->call(QStringLiteral("AttachTransaction"), QVariant::fromValue(path), static_cast<qint64>(0));
    if (!reply.isValid()) {
        reply = m_daemonIface->call(QStringLiteral("GetTransactionSnapshot"), QVariant::fromValue(path));
    }
    if (!reply.isValid() || reply.value().isEmpty() || reply.value() == QLatin1String("{}")) {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
    if (!doc.isObject()) return false;

    TransactionSnapshot snap = TransactionSnapshot::fromJson(doc.object());
    m_activeTransactionPath = path;
    m_isUpgradePlan = snap.intent.type == TransactionIntent::Type::UpgradeAll;
    m_lastSeenSequence = snap.sequenceNumber;
    m_terminalEventProcessed = false;

    m_planModel.setPlan(snap.plan, m_isUpgradePlan ? QStringLiteral("System aktualisieren") : QStringLiteral("Paketoperation"));
    m_progressModel.reset();
    m_logModel.clear();

    for (const QString &evStr : snap.missedEvents) {
        QJsonDocument evDoc = QJsonDocument::fromJson(evStr.toUtf8());
        if (evDoc.isObject()) {
            auto ev = deserializeEvent(evDoc.object());
            if (ev.has_value()) {
                m_progressModel.processEvent(*ev);
            }
        }
    }

    const Phase p = snap.phase;
    if (!snap.active || p == Phase::Finished || p == Phase::Failed || p == Phase::Cancelled) {
        m_busy = false;
        m_hasPlan = false;
        m_hasError = (p == Phase::Failed);
        m_statusMessage = snap.statusMessage;
        m_terminalEventProcessed = true;
    } else if (p == Phase::Idle && !snap.plan.planRevision.isEmpty()) {
        // Nach einem Neustart des Fensters hält der Daemon den Plan noch: die
        // Liste muss ihn zeigen, sonst meldet die Statusleiste "0 bereit".
        if (m_isUpgradePlan) m_updatesModel.setPackages(snap.plan.ops);
        m_busy = false;
        m_hasPlan = true;
        m_hasError = false;
        m_statusMessage = snap.plan.warnings.join(QLatin1Char(' '));
    } else if (p != Phase::Idle) {
        m_busy = true;
        m_hasPlan = false;
        m_hasError = false;
        m_statusMessage = phaseToString(p);
        emit transactionStarted();
    }
    if (m_hasPlan) emit planReady();
    emit statusChanged();
    return true;
}

void DaemonClient::onDbusTransactionEvent(const QDBusObjectPath &path, const QString &jsonStr) {
    quint64 seq = 0;
    QString eventTxPath;
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    if (!doc.isObject()) return;

    auto ev = deserializeEvent(doc.object(), &seq, &eventTxPath);
    if (!ev.has_value()) return;

    if (m_activeTransactionPath.path().isEmpty() && m_pendingTransaction) {
        m_activeTransactionPath = path;
    }

    if (m_activeTransactionPath.path().isEmpty() || m_activeTransactionPath.path() != path.path()) {
        return;
    }

    if (seq > 0 && seq <= m_lastSeenSequence) {
        return;
    }
    if (seq > 0) {
        m_lastSeenSequence = seq;
    }

    handleEvent(*ev);
}

void DaemonClient::handleEvent(const Event &event) {
    if (std::holds_alternative<PlanReady>(event)) {
        const auto &plan = std::get<PlanReady>(event);
        if (m_isUpgradePlan) {
            m_updatesModel.setPackages(plan.ops);
            m_lastCheckedString = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm"));
            // Nichts zu aktualisieren: der Daemon hält leere Pläne nicht fest,
            // also gibt es auch nichts zu bestätigen oder anzuzeigen.
            if (plan.ops.isEmpty()) {
                m_planModel.clear();
                m_busy = false; m_hasPlan = false; m_hasError = false;
                m_statusMessage = QStringLiteral("Das System ist aktuell.");
                emit statusChanged();
                return;
            }
        }
        TransactionPlan confirmedPlan;
        confirmedPlan.ops = plan.ops;
        confirmedPlan.downloadBytes = plan.downloadBytes;
        confirmedPlan.installedSizeDelta = plan.installedSizeDelta;
        confirmedPlan.warnings = plan.warnings;
        confirmedPlan.planRevision = plan.planRevision;
        m_planModel.setPlan(confirmedPlan, m_isUpgradePlan ? QStringLiteral("System aktualisieren") : QStringLiteral("Paketoperation"));
        m_busy = false; m_hasPlan = true; m_hasError = false;
        m_statusMessage = plan.warnings.join(QLatin1Char(' '));
        emit planReady();
        emit statusChanged();
    } else if (std::holds_alternative<PhaseChanged>(event)) {
        const auto &phase = std::get<PhaseChanged>(event);
        LogLevel l = (phase.phase == Phase::Failed) ? LogLevel::Error : LogLevel::Info;
        m_logModel.appendLog(LogLine{l, QStringLiteral("Phase"), phase.label});
    } else if (std::holds_alternative<TransactionDone>(event)) {
        if (m_terminalEventProcessed) {
            return;
        }
        m_terminalEventProcessed = true;
        const auto &done = std::get<TransactionDone>(event);
        m_busy = false; m_hasPlan = false; m_hasError = (done.result == Result::Failed); m_statusMessage = done.summary;

        // Erfolgreich angewendete Systemaktualisierungen verschwinden aus der Liste.
        // Vorher blieben sie nach "Fertig" stehen, samt Zähler im Tray.
        const bool succeeded = done.result == Result::Success || done.result == Result::SuccessWithWarnings;
        if (m_isUpgradePlan && succeeded) {
            m_updatesModel.removeApplied(!m_capabilities.partialUpgrade);
            m_lastCheckedString = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm"));
        }

        // IMMER Bestandsabgleich auslösen bei terminalem Ergebnis
        QTimer::singleShot(0, &m_installedModel, &InstalledModel::refresh);
        QTimer::singleShot(0, &m_historyModel, &HistoryModel::refresh);
        emit transactionFinished(done.result);
        emit statusChanged();

        LogLevel l = (done.result == Result::Success) ? LogLevel::Info : (done.result == Result::Cancelled ? LogLevel::Warning : LogLevel::Error);
        m_logModel.appendLog(LogLine{l, QStringLiteral("Ergebnis"), done.summary});
    } else if (std::holds_alternative<ScriptletStarted>(event)) {
        const auto &sc = std::get<ScriptletStarted>(event);
        m_logModel.appendLog(LogLine{LogLevel::Info, sc.scriptletName, sc.pkgId});
    } else if (std::holds_alternative<ItemStarted>(event)) {
        const auto &item = std::get<ItemStarted>(event);
        m_logModel.appendLog(LogLine{LogLevel::Info, QStringLiteral("Paket"), item.pkgId});
    }
    m_progressModel.processEvent(event);
}

void DaemonClient::refreshUpdates() {
    ensureDaemon();
    if (isBusy()) return;
    m_hasPlan = false; m_busy = true; m_hasError = false; m_isUpgradePlan = true; m_statusMessage = QStringLiteral("Aktualisierungen werden vorbereitet …");
    m_activeTransactionPath = {}; m_lastSeenSequence = 0; m_terminalEventProcessed = false; m_pendingTransaction = true;
    m_progressModel.reset(); emit statusChanged();
    if (m_replayBackend) {
        m_pendingTransaction = false;
        m_replayBackend->refreshMetadata();
        return;
    }
    if (m_daemonIface && m_daemonIface->isValid()) {
        QVariantMap opts;
        opts[QStringLiteral("includeSecurityOnly")] = false;
        opts[QStringLiteral("refreshFirst")] = true;
        QDBusReply<QDBusObjectPath> reply = m_daemonIface->call(QStringLiteral("PlanUpgrade"), opts);
        m_pendingTransaction = false;
        if (reply.isValid()) {
            if (m_activeTransactionPath.path().isEmpty()) {
                m_activeTransactionPath = reply.value();
            }
        } else {
            reportError(reply.error().message());
        }
    } else {
        m_pendingTransaction = false;
        reportError(QStringLiteral("Keine Verbindung zum Systemdienst."));
    }
}

void DaemonClient::reportError(const QString &message) {
    m_busy = false; m_hasPlan = false; m_hasError = true; m_statusMessage = message;
    m_logModel.appendLog(LogLine{LogLevel::Error, QStringLiteral("lutd"), message});
    emit statusChanged();
}

void DaemonClient::planDnf5(const QString &command, const QString &arguments, bool securityOnly, bool excludeKernel) {
    ensureDaemon();
    if (isBusy()) return;
    if (m_replayBackend || !m_daemonIface || !m_dnf5Commands.contains(command)) { reportError(QStringLiteral("Diese DNF5-Aktion ist nicht verfügbar.")); return; }
    const QStringList names = arguments.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    QVariantMap options;
    options[QStringLiteral("includeSecurityOnly")] = securityOnly;
    options[QStringLiteral("excludeKernel")] = excludeKernel;
    m_hasPlan = false; m_busy = true; m_hasError = false; m_isUpgradePlan = false;
    m_statusMessage = QStringLiteral("Transaktion wird vorbereitet …");
    m_activeTransactionPath = {}; m_lastSeenSequence = 0; m_terminalEventProcessed = false; m_pendingTransaction = true;
    m_progressModel.reset(); emit statusChanged();
    QDBusReply<QDBusObjectPath> reply = m_daemonIface->call(QStringLiteral("PlanDnf5"), command, names, options);
    m_pendingTransaction = false;
    if (reply.isValid()) {
        if (m_activeTransactionPath.path().isEmpty()) {
            m_activeTransactionPath = reply.value();
        }
    } else reportError(reply.error().message());
}

void DaemonClient::cleanCache() {
    ensureDaemon();
    if (isBusy()) return;
    if (m_replayBackend || !m_daemonIface) { reportError(QStringLiteral("Keine Verbindung zum Systemdienst.")); return; }
    m_busy = true; m_hasPlan = false; m_activeTransactionPath = {};
    m_lastSeenSequence = 0; m_terminalEventProcessed = false; m_pendingTransaction = true;
    m_statusMessage = QStringLiteral("Paketcache wird geleert …"); emit statusChanged();
    QDBusReply<QDBusObjectPath> reply = m_daemonIface->call(QStringLiteral("CleanCache"));
    m_pendingTransaction = false;
    if (reply.isValid()) {
        if (m_activeTransactionPath.path().isEmpty()) {
            m_activeTransactionPath = reply.value();
        }
    } else reportError(reply.error().message());
}

void DaemonClient::startUpgrade() {
    if (m_busy) return;
    // "System aktualisieren" ist immer eine Systemaktualisierung – auch nach
    // einem Wiederanbinden oder im Replay, wo kein "Prüfen" vorausging.
    m_isUpgradePlan = true;
    if (m_replayBackend) { emit transactionStarted(); m_replayBackend->commit(); return; }
    if (!m_hasPlan || !m_daemonIface) { reportError(QStringLiteral("Bitte zunächst einen Plan erstellen und prüfen.")); return; }
    if (m_updatesModel.selectedCount() != m_updatesModel.totalCount()) {
        if (!m_isUpgradePlan) { m_statusMessage = QStringLiteral("Dieser aufgelöste Plan wird vollständig ausgeführt. Bitte alle Einträge auswählen oder einen neuen Plan erstellen."); emit statusChanged(); return; }
        QStringList packages;
        for (const auto &op : m_updatesModel.selectedPackages())
            if (op.kind != PackageOp::Kind::Remove) packages.append(m_dnf5Commands.isEmpty() ? op.name : op.name + QLatin1Char('.') + op.arch);
        packages.removeDuplicates();
        if (packages.isEmpty()) return;
        QVariantMap options;
        options[QStringLiteral("packages")] = packages;
        options[QStringLiteral("refreshFirst")] = false;
        m_busy = true; m_hasPlan = false; m_activeTransactionPath = {};
        m_statusMessage = QStringLiteral("Die Auswahl wird neu aufgelöst. Anschließend den neuen Plan prüfen und bestätigen."); emit statusChanged();
        QDBusReply<QDBusObjectPath> reply = m_daemonIface->call(QStringLiteral("PlanUpgrade"), options);
        if (reply.isValid()) m_activeTransactionPath = reply.value(); else reportError(reply.error().message());
        return;
    }
    sendCommit(QStringLiteral("Commit"), {QVariant::fromValue(m_activeTransactionPath)});
}

void DaemonClient::sendCommit(const QString &method, const QVariantList &arguments) {
    static constexpr int kCommitTimeoutMs = 10 * 60 * 1000;
    QDBusMessage message = QDBusMessage::createMethodCall(m_daemonIface->service(), m_daemonIface->path(),
                                                          m_daemonIface->interface(), method);
    message.setArguments(arguments);
    m_busy = true; m_hasPlan = false; m_hasError = false;
    m_statusMessage = QStringLiteral("Warte auf Freigabe …");
    emit statusChanged();

    auto *watcher = new QDBusPendingCallWatcher(m_daemonIface->connection().asyncCall(message, kCommitTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QDBusPendingReply<> reply = *call;
        if (reply.isError()) {
            // Bei verweigerter Freigabe hält der Daemon den Plan weiter bereit:
            // erneut bestätigen muss möglich bleiben.
            const bool planKept = reply.error().type() == QDBusError::AccessDenied;
            reportError(reply.error().message());
            m_hasPlan = planKept;
            emit statusChanged();
            return;
        }
        emit transactionStarted();
    });
}

void DaemonClient::cancelTransaction() {
    if (m_replayBackend) {
        m_replayBackend->cancel();
        return;
    }
    if (m_daemonIface && m_daemonIface->isValid() && !m_activeTransactionPath.path().isEmpty()) {
        m_daemonIface->call(QStringLiteral("Cancel"), QVariant::fromValue(m_activeTransactionPath));
    }
}

void DaemonClient::answerQuestion(const QString &id, const QJsonObject &answer) {
    if (m_replayBackend) {
        m_replayBackend->answerQuestion(id, answer);
        return;
    }
    if (m_daemonIface && m_daemonIface->isValid() && !m_activeTransactionPath.path().isEmpty()) {
        QString jsonStr = QString::fromUtf8(QJsonDocument(answer).toJson(QJsonDocument::Compact));
        m_daemonIface->call(QStringLiteral("AnswerQuestion"), QVariant::fromValue(m_activeTransactionPath), id, jsonStr);
    }
}

void DaemonClient::planStoreInstall(const QString &packageName, const QString &repoId) {
    planStoreInstall(QStringList{packageName}, repoId);
}

void DaemonClient::planStoreInstall(const QStringList &packageNames, const QString &repoId) {
    QList<PackageRef> refs;
    for (const auto &pkg : packageNames) {
        if (pkg.isEmpty()) continue;
        PackageRef ref;
        ref.name = pkg;
        ref.repoId = repoId;
        refs.append(ref);
    }
    planStoreInstall(refs);
}

void DaemonClient::planStoreInstall(const QList<PackageRef> &targets) {
    ensureDaemon();
    if (isBusy()) return;
    if (!m_capabilities.install && !m_replayBackend) {
        reportError(QStringLiteral("Installation wird vom aktuellen Backend nicht unterstützt."));
        return;
    }
    m_hasPlan = false; m_busy = true; m_hasError = false; m_isUpgradePlan = false;
    m_statusMessage = QStringLiteral("Installation wird vorbereitet …");
    m_activeTransactionPath = {}; m_lastSeenSequence = 0; m_terminalEventProcessed = false; m_pendingTransaction = true;
    m_progressModel.reset(); m_planModel.clear(); emit statusChanged();

    if (m_replayBackend) {
        m_pendingTransaction = false;
        QStringList names;
        for (const auto &ref : targets) if (!ref.name.isEmpty()) names.append(ref.name);
        m_replayBackend->planInstall(names);
        return;
    }

    if (m_daemonIface && m_daemonIface->isValid()) {
        QVariantList targetsVariant;
        QStringList legacyNames;
        for (const auto &ref : targets) {
            if (ref.name.isEmpty()) continue;
            targetsVariant.append(ref.toMap());
            legacyNames.append(ref.name);
        }

        QVariantMap options;
        QDBusReply<QDBusObjectPath> reply = m_daemonIface->call(QStringLiteral("PlanPackageTransaction"),
                                                                 QStringLiteral("Install"),
                                                                 targetsVariant,
                                                                 options);
        m_pendingTransaction = false;
        if (reply.isValid()) {
            if (m_activeTransactionPath.path().isEmpty()) {
                m_activeTransactionPath = reply.value();
            }
        } else {
            // Fallback auf PlanInstall falls alter Daemon
            QDBusReply<QDBusObjectPath> legacyReply = m_daemonIface->call(QStringLiteral("PlanInstall"), legacyNames);
            if (legacyReply.isValid()) {
                if (m_activeTransactionPath.path().isEmpty()) {
                    m_activeTransactionPath = legacyReply.value();
                }
            } else {
                reportError(reply.error().message());
            }
        }
    } else {
        m_pendingTransaction = false;
        reportError(QStringLiteral("Keine Verbindung zum Systemdienst."));
    }
}

void DaemonClient::planStoreRemove(const QString &packageName) {
    planStoreRemove(QStringList{packageName});
}

void DaemonClient::planStoreRemove(const QStringList &packageNames) {
    QList<PackageRef> refs;
    for (const auto &pkg : packageNames) {
        if (pkg.isEmpty()) continue;
        PackageRef ref;
        ref.name = pkg;
        refs.append(ref);
    }
    planStoreRemove(refs);
}

void DaemonClient::planStoreRemove(const QList<PackageRef> &targets) {
    ensureDaemon();
    if (isBusy()) return;
    if (!m_capabilities.remove && !m_replayBackend) {
        reportError(QStringLiteral("Entfernen wird vom aktuellen Backend nicht unterstützt."));
        return;
    }
    m_hasPlan = false; m_busy = true; m_hasError = false; m_isUpgradePlan = false;
    m_statusMessage = QStringLiteral("Entfernen wird vorbereitet …");
    m_activeTransactionPath = {}; m_lastSeenSequence = 0; m_terminalEventProcessed = false; m_pendingTransaction = true;
    m_progressModel.reset(); m_planModel.clear(); emit statusChanged();

    if (m_replayBackend) {
        m_pendingTransaction = false;
        QStringList names;
        for (const auto &ref : targets) if (!ref.name.isEmpty()) names.append(ref.name);
        m_replayBackend->planRemove(names);
        return;
    }

    if (m_daemonIface && m_daemonIface->isValid()) {
        QVariantList targetsVariant;
        QStringList legacyNames;
        for (const auto &ref : targets) {
            if (ref.name.isEmpty()) continue;
            targetsVariant.append(ref.toMap());
            legacyNames.append(ref.name);
        }

        QVariantMap options;
        QDBusReply<QDBusObjectPath> reply = m_daemonIface->call(QStringLiteral("PlanPackageTransaction"),
                                                                 QStringLiteral("Remove"),
                                                                 targetsVariant,
                                                                 options);
        m_pendingTransaction = false;
        if (reply.isValid()) {
            if (m_activeTransactionPath.path().isEmpty()) {
                m_activeTransactionPath = reply.value();
            }
        } else {
            // Fallback auf PlanRemove falls alter Daemon
            QDBusReply<QDBusObjectPath> legacyReply = m_daemonIface->call(QStringLiteral("PlanRemove"), legacyNames);
            if (legacyReply.isValid()) {
                if (m_activeTransactionPath.path().isEmpty()) {
                    m_activeTransactionPath = legacyReply.value();
                }
            } else {
                reportError(reply.error().message());
            }
        }
    } else {
        m_pendingTransaction = false;
        reportError(QStringLiteral("Keine Verbindung zum Systemdienst."));
    }
}

void DaemonClient::commitStorePlan(const QString &planRevision) {
    if (m_busy) return;
    if (m_replayBackend) {
        emit transactionStarted();
        m_replayBackend->commit();
        return;
    }
    if (!m_hasPlan || !m_daemonIface || !m_daemonIface->isValid() || m_activeTransactionPath.path().isEmpty()) {
        reportError(QStringLiteral("Kein gültiger Plan zur Ausführung vorhanden."));
        return;
    }
    const QString rev = planRevision.isEmpty() ? m_planModel.planRevision() : planRevision;
    if (rev.isEmpty()) { reportError(QStringLiteral("Die bestätigte Planrevision fehlt.")); return; }
    sendCommit(QStringLiteral("CommitPlan"), {QVariant::fromValue(m_activeTransactionPath), rev});
}

void DaemonClient::discardStorePlan() {
    if (isBusy()) return;
    if (m_replayBackend) {
        m_replayBackend->cancel();
    } else if (m_daemonIface && m_daemonIface->isValid() && !m_activeTransactionPath.path().isEmpty()) {
        m_daemonIface->call(QStringLiteral("DiscardPlan"), QVariant::fromValue(m_activeTransactionPath));
    }
    m_hasPlan = false;
    m_activeTransactionPath = {};
    m_planModel.clear();
    m_statusMessage.clear();
    emit statusChanged();
}

void DaemonClient::setExternalBusy(bool busy) {
    if (m_externalBusy != busy) {
        m_externalBusy = busy;
        emit statusChanged();
    }
}

} // namespace lut

