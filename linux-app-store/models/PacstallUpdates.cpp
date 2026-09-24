#include "PacstallUpdates.h"
#include "linux-app-store/AppSettings.h"
#include "liblut/backend/pacstall/PacstallBackend.h"
#include "liblut/detect/DistroDetect.h"
#include "liblut/protocol/events.h"
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>

namespace lut {

PacstallUpdates::PacstallUpdates(QObject *parent)
    : QAbstractListModel(parent)
    , m_bus(QDBusConnection::systemBus())
{
    m_lastSupported = supported();
}

PacstallUpdates::~PacstallUpdates() = default;

int PacstallUpdates::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return m_items.size();
}

QVariant PacstallUpdates::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }
    const auto &item = m_items.at(index.row());
    switch (role) {
    case NameRole:
    case Qt::DisplayRole:
        return item.name;
    case InstalledVersionRole:
        return item.installed;
    case AvailableVersionRole:
        return item.available;
    case RepoRole:
        return item.repo;
    default:
        return {};
    }
}

QHash<int, QByteArray> PacstallUpdates::roleNames() const {
    return {
        { NameRole, "name" },
        { InstalledVersionRole, "installedVersion" },
        { AvailableVersionRole, "availableVersion" },
        { RepoRole, "repo" }
    };
}

bool PacstallUpdates::supported() const {
    if (m_forceSupported.has_value()) {
        return *m_forceSupported;
    }
    return (DistroDetect::detectFamily() == DistroFamily::Debian) &&
           PacstallBackend::isPacstallAvailable();
}

void PacstallUpdates::setAppSettings(AppSettings *settings) {
    m_settings = settings;
    if (m_settings) {
        m_enabled = m_settings->pacstallEnabled();
        connect(m_settings, &AppSettings::pacstallEnabledChanged, this, [this](bool enabled) {
            setEnabled(enabled);
        });
    }
}

void PacstallUpdates::setForceSupported(std::optional<bool> supported) {
    if (m_forceSupported != supported) {
        m_forceSupported = supported;
        emit supportedChanged();
        emit availableChanged();
    }
}

void PacstallUpdates::setDbusConnection(const QDBusConnection &connection) {
    m_bus = connection;
}

void PacstallUpdates::setEntriesForTest(const QList<Entry> &entries) {
    beginResetModel();
    m_items = entries;
    endResetModel();
    emit countChanged();
}

void PacstallUpdates::setBusy(bool busy) {
    if (m_busy != busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

void PacstallUpdates::setStatus(const QString &message) {
    if (m_statusMessage != message) {
        m_statusMessage = message;
        emit statusChanged();
    }
}

void PacstallUpdates::setEnabled(bool enabled) {
    if (m_settings && m_settings->pacstallEnabled() != enabled) {
        m_settings->setPacstallEnabled(enabled);
    }
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    emit enabledChanged(m_enabled);
    emit availableChanged();

    if (!m_enabled) {
        beginResetModel();
        m_items.clear();
        endResetModel();
        emit countChanged();
    } else if (available()) {
        check();
    }
}

void PacstallUpdates::refreshSupport() {
    const bool now = supported();
    if (now == m_lastSupported) return;
    m_lastSupported = now;
    emit supportedChanged();
    emit availableChanged();
    if (available()) check();
}

void PacstallUpdates::check() {
    if (!available()) return;

    setBusy(true);
    setStatus(tr("Prüfe Pacstall-Aktualisierungen …"));

    // pacstall -Lu fragt jedes Paket einzeln im Netz ab: das Standardlimit von
    // 25 s würde eine laufende Prüfung als Fehler melden.
    const QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.linuxupdatetool.Daemon1"),
                                                             QStringLiteral("/org/linuxupdatetool/Daemon1"),
                                                             QStringLiteral("org.linuxupdatetool.Daemon1"),
                                                             QStringLiteral("PacstallCheck"));
    auto watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 10 * 60 * 1000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        setBusy(false);
        m_hasChecked = true;

        QDBusPendingReply<QString> reply = *watcher;
        if (reply.isError()) {
            // Kein "alles aktuell": die Liste bleibt, wie sie war, und der Fehler steht da.
            setStatus(tr("Pacstall-Prüfung fehlgeschlagen: %1").arg(reply.error().message()));
            return;
        }

        auto updates = PacstallBackend::jsonToUpdates(reply.value());
        beginResetModel();
        m_items.clear();
        for (const auto &u : updates) {
            Entry e;
            e.name = u.name;
            e.repo = u.repo;
            e.installed = u.installed;
            e.available = u.available;
            m_items.append(e);
        }
        endResetModel();

        emit countChanged();
        setStatus(m_items.isEmpty() ? tr("Alle Pacstall-Pakete sind aktuell.")
                                    : tr("%1 Pacstall-Aktualisierung(en) verfügbar.").arg(m_items.size()));
    });
}

void PacstallUpdates::upgradeAll() {
    QStringList names;
    for (const auto &it : m_items) {
        names.append(it.name);
    }
    upgradePackages(names);
}

void PacstallUpdates::upgradePackages(const QStringList &names) {
    if (!available() || names.isEmpty()) return;

    setBusy(true);
    m_committed = false;
    m_planRevision.clear();
    emit operationStarted(tr("Pacstall-Aktualisierung"));

    QDBusInterface iface(QStringLiteral("org.linuxupdatetool.Daemon1"),
                         QStringLiteral("/org/linuxupdatetool/Daemon1"),
                         QStringLiteral("org.linuxupdatetool.Daemon1"),
                         m_bus);

    auto watcher = new QDBusPendingCallWatcher(iface.asyncCall(QStringLiteral("PlanPacstallUpgrade"), names), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        if (reply.isError() || reply.value().path() == QLatin1String("/")) {
            setBusy(false);
            QString err = reply.isError() ? reply.error().message() : tr("Plan konnte nicht erstellt werden.");
            emit operationFinished(2, err);
            return;
        }

        m_activeTransactionPath = reply.value();
        m_bus.connect(QStringLiteral("org.linuxupdatetool.Daemon1"),
                      QStringLiteral("/org/linuxupdatetool/Daemon1"),
                      QStringLiteral("org.linuxupdatetool.Daemon1"),
                      QStringLiteral("TransactionEvent"),
                      this,
                      SLOT(onDbusTransactionEvent(QDBusObjectPath,QString)));
    });
}

void PacstallUpdates::onDbusTransactionEvent(const QDBusObjectPath &path, const QString &eventJson) {
    if (path.path() != m_activeTransactionPath.path()) return;

    quint64 seq = 0;
    QString txPath;
    auto maybeEvent = deserializeEvent(QJsonDocument::fromJson(eventJson.toUtf8()).object(), &seq, &txPath);
    if (!maybeEvent) return;

    const auto &event = *maybeEvent;

    if (std::holds_alternative<PlanReady>(event)) {
        const auto &plan = std::get<PlanReady>(event);
        m_planRevision = plan.planRevision;
        QVariantList reviewList;
        for (const auto &op : plan.ops) {
            QVariantMap item;
            item[QStringLiteral("base")] = op.name;
            item[QStringLiteral("names")] = QStringList{op.name};
            item[QStringLiteral("fromVersion")] = op.version;
            item[QStringLiteral("toVersion")] = op.newVersion;
            item[QStringLiteral("review")] = op.summary; // Pacscript-Inhalt zur Prüfung
            item[QStringLiteral("firstBuild")] = false;
            item[QStringLiteral("error")] = QString();
            reviewList.append(item);
        }
        emit reviewReady(reviewList);
    } else if (std::holds_alternative<PhaseChanged>(event)) {
        const auto &pc = std::get<PhaseChanged>(event);
        emit operationProgress(0.0, pc.indeterminate, 0, 0, pc.label);
    } else if (std::holds_alternative<ItemStarted>(event)) {
        const auto &is = std::get<ItemStarted>(event);
        emit operationProgress(0.0, true, 0, 0, is.pkgId);
    } else if (std::holds_alternative<LogLine>(event)) {
        const auto &ll = std::get<LogLine>(event);
        emit logLine(ll.text);
    } else if (std::holds_alternative<TransactionDone>(event)) {
        const auto &td = std::get<TransactionDone>(event);
        int outcome = (td.result == Result::Success) ? 1 : (td.result == Result::Cancelled) ? 3 : 2;
        emit operationFinished(outcome, td.summary);
        setBusy(false);
        m_bus.disconnect(QStringLiteral("org.linuxupdatetool.Daemon1"),
                         QStringLiteral("/org/linuxupdatetool/Daemon1"),
                         QStringLiteral("org.linuxupdatetool.Daemon1"),
                         QStringLiteral("TransactionEvent"),
                         this,
                         SLOT(onDbusTransactionEvent(QDBusObjectPath,QString)));
        if (td.result == Result::Success) {
            check();
        }
    }
}

void PacstallUpdates::confirmReview() {
    QDBusInterface iface(QStringLiteral("org.linuxupdatetool.Daemon1"),
                         QStringLiteral("/org/linuxupdatetool/Daemon1"),
                         QStringLiteral("org.linuxupdatetool.Daemon1"),
                         m_bus);
    m_committed = true;
    iface.asyncCall(QStringLiteral("CommitPlan"), QVariant::fromValue(m_activeTransactionPath), m_planRevision);
}

void PacstallUpdates::cancel() {
    QDBusInterface iface(QStringLiteral("org.linuxupdatetool.Daemon1"),
                         QStringLiteral("/org/linuxupdatetool/Daemon1"),
                         QStringLiteral("org.linuxupdatetool.Daemon1"),
                         m_bus);
    // Ein noch nicht bestätigter Plan wird verworfen; "Cancel" setzte im Backend nur
    // ein Flag, und der Plan blieb im Daemon als aktive Transaktion stehen.
    iface.asyncCall(m_committed ? QStringLiteral("Cancel") : QStringLiteral("DiscardPlan"),
                    QVariant::fromValue(m_activeTransactionPath));
    if (m_committed) return; // das Ergebnis meldet der Daemon mit TransactionDone
    setBusy(false);
    emit operationFinished(3, tr("Abgebrochen"));
}

} // namespace lut
