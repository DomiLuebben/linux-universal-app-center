#include "RepositoriesModel.h"
#include "linux-app-store/DaemonClient.h"
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusMessage>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include "liblut/repository/RepoManager.h"

namespace lut {

RepositoriesModel::RepositoriesModel(DaemonClient *client, QObject *parent)
    : QAbstractListModel(parent), m_client(client) {
    m_pacstallInstalled = QFile::exists(QStringLiteral("/usr/bin/pacstall"));
    refresh();
}

int RepositoriesModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return m_repos.size();
}

QVariant RepositoriesModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_repos.size()) {
        return QVariant();
    }

    const RepoEntry &entry = m_repos.at(index.row());
    switch (role) {
    case IdRole: return entry.id;
    case NameRole: return entry.name;
    case UrlRole: return entry.url;
    case EnabledRole: return entry.enabled;
    case IsSystemRole: return entry.isSystem;
    case BackendRole: return entry.backend;
    case DescriptionRole: return entry.description;
    case FilePathRole: return entry.filePath;
    default: return QVariant();
    }
}

QHash<int, QByteArray> RepositoriesModel::roleNames() const {
    return {
        { IdRole, "id" },
        { NameRole, "name" },
        { UrlRole, "url" },
        { EnabledRole, "enabled" },
        { IsSystemRole, "isSystem" },
        { BackendRole, "backend" },
        { DescriptionRole, "description" },
        { FilePathRole, "filePath" }
    };
}

QString RepositoriesModel::distroName() const {
    if (RepoManager::instance().currentFamily() == DistroFamily::Fedora && DistroDetect::detectFamily() != DistroFamily::Fedora) {
        return QStringLiteral("Fedora Linux");
    }
    if (RepoManager::instance().currentFamily() == DistroFamily::Debian && DistroDetect::detectFamily() != DistroFamily::Debian) {
        return QStringLiteral("Debian GNU/Linux");
    }
    return DistroDetect::prettyName();
}

QString RepositoriesModel::distroFamily() const {
    switch (RepoManager::instance().currentFamily()) {
    case DistroFamily::Arch: return QStringLiteral("arch");
    case DistroFamily::Fedora: return QStringLiteral("fedora");
    case DistroFamily::Debian: return QStringLiteral("debian");
    default: return QStringLiteral("unknown");
    }
}

QString RepositoriesModel::backendName() const {
    switch (RepoManager::instance().currentFamily()) {
    case DistroFamily::Arch: return QStringLiteral("Pacman");
    case DistroFamily::Fedora: return QStringLiteral("DNF5");
    case DistroFamily::Debian: return QStringLiteral("APT");
    default: return QStringLiteral("Unbekannt");
    }
}

bool RepositoriesModel::pacstallInstalled() const {
    return m_pacstallInstalled;
}

bool RepositoriesModel::pacstallLicensesMissing() const {
    return m_pacstallInstalled && !QFileInfo(QStringLiteral("/usr/share/spdx-licenses")).isDir();
}

void RepositoriesModel::refreshPacstallState() {
    const bool installed = QFile::exists(QStringLiteral("/usr/bin/pacstall"));
    if (installed == m_pacstallInstalled) return;
    m_pacstallInstalled = installed;
    emit pacstallInstalledChanged();
}

void RepositoriesModel::setBusy(bool busy) {
    if (m_busy != busy) {
        m_busy = busy;
        emit isBusyChanged();
    }
}

void RepositoriesModel::setError(const QString &err) {
    m_errorMessage = err;
    emit errorChanged();
}

void RepositoriesModel::refresh() {
    // Vor der ersten Verbindung (Konstruktor, Unit-Tests ohne init()) nur lokal
    // lesen: jede Abfrage über den System-Bus startete sonst den echten lutd.
    // Nach dem Verbinden ruft DaemonClient::connectToDaemon() refresh() erneut.
    if (m_client && !m_client->isConnected()) {
        loadDirectly();
        return;
    }
    setBusy(true);
    setError(QString());

    QDBusInterface iface(QStringLiteral("org.linuxupdatetool.Daemon1"),
                         QStringLiteral("/org/linuxupdatetool/Daemon1"),
                         QStringLiteral("org.linuxupdatetool.Daemon1"),
                         (m_client ? m_client->bus() : QDBusConnection::systemBus()));

    if (iface.isValid()) {
        QDBusReply<QString> repoReply = iface.call(QStringLiteral("GetRepositories"));
        QDBusReply<QString> presetReply = iface.call(QStringLiteral("GetRepositoryPresets"));

        if (repoReply.isValid()) {
            QJsonDocument doc = QJsonDocument::fromJson(repoReply.value().toUtf8());
            if (doc.isArray()) {
                beginResetModel();
                m_repos.clear();
                for (const auto &val : doc.array()) {
                    if (val.isObject()) {
                        m_repos.append(RepoEntry::fromJson(val.toObject()));
                    }
                }
                endResetModel();
                emit countChanged();
            }
        } else {
            loadDirectly();
        }

        if (presetReply.isValid()) {
            QJsonDocument pdoc = QJsonDocument::fromJson(presetReply.value().toUtf8());
            if (pdoc.isArray()) {
                m_presets.clear();
                for (const auto &val : pdoc.array()) {
                    if (val.isObject()) {
                        m_presets.append(val.toObject().toVariantMap());
                    }
                }
                emit presetsChanged();
            }
        }
    } else {
        loadDirectly();
    }

    setBusy(false);
}

void RepositoriesModel::loadDirectly() {
    beginResetModel();
    m_repos = RepoManager::instance().getRepositories();
    endResetModel();
    emit countChanged();

    m_presets.clear();
    for (const auto &preset : RepoManager::instance().getPresets()) {
        m_presets.append(preset.toJson().toVariantMap());
    }
    emit presetsChanged();
}

void RepositoriesModel::callDaemon(const QString &method, const QVariantList &args, const QString &action,
                                   const std::function<bool(QString *)> &fallback) {
    setBusy(true);
    setError(QString());

    QDBusInterface iface(QStringLiteral("org.linuxupdatetool.Daemon1"),
                         QStringLiteral("/org/linuxupdatetool/Daemon1"),
                         QStringLiteral("org.linuxupdatetool.Daemon1"),
                         (m_client ? m_client->bus() : QDBusConnection::systemBus()));
    if (!iface.isValid()) {
        QString err;
        const bool ok = fallback(&err);
        setBusy(false);
        if (!ok && !err.isEmpty()) setError(err);
        refresh();
        emit repoOperationFinished(action, ok, err);
        if (ok && action == QLatin1String("addPreset")) emit presetAdded(m_pendingPresetId);
        return;
    }

    // Asynchron und mit großzügigem Zeitlimit: das Einrichten von RPM Fusion
    // oder Terra installiert Pakete und lädt Metadaten. Mit dem Standardlimit
    // von 25 s meldete die Oberfläche einen Fehler, während der Daemon weiterarbeitete,
    // und das Fenster war bis dahin eingefroren.
    iface.setTimeout(15 * 60 * 1000);
    QDBusPendingCall pending = iface.asyncCallWithArgumentList(method, args);
    auto *watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, action](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<bool> reply = *w;
        const bool ok = !reply.isError() && reply.value();
        const QString err = reply.isError() ? reply.error().message() : QString();
        setBusy(false);
        if (!ok && !err.isEmpty()) setError(err);
        refresh();
        emit repoOperationFinished(action, ok, err);
        if (ok && action == QLatin1String("addPreset")) emit presetAdded(m_pendingPresetId);
    });
}

void RepositoriesModel::addPreset(const QString &presetId) {
    m_pendingPresetId = presetId;
    callDaemon(QStringLiteral("AddRepositoryPreset"), {presetId}, QStringLiteral("addPreset"),
               [presetId](QString *err) { return RepoManager::instance().addPreset(presetId, err); });
}

void RepositoriesModel::addCustom(const QString &id, const QString &name, const QString &url) {
    RepoEntry entry;
    entry.id = id.trimmed();
    entry.name = name.trimmed().isEmpty() ? entry.id : name.trimmed();
    entry.url = url.trimmed();
    entry.enabled = true;
    entry.isSystem = false;
    const QString jsonStr = QString::fromUtf8(QJsonDocument(entry.toJson()).toJson(QJsonDocument::Compact));
    callDaemon(QStringLiteral("AddRepository"), {jsonStr}, QStringLiteral("addCustom"),
               [entry](QString *err) { return RepoManager::instance().addRepository(entry, err); });
}

void RepositoriesModel::removeRepo(const QString &repoId) {
    callDaemon(QStringLiteral("RemoveRepository"), {repoId}, QStringLiteral("removeRepo"),
               [repoId](QString *err) { return RepoManager::instance().removeRepository(repoId, err); });
}

void RepositoriesModel::toggleRepo(const QString &repoId, bool enable) {
    callDaemon(QStringLiteral("ToggleRepository"), {repoId, enable}, QStringLiteral("toggleRepo"),
               [repoId, enable](QString *err) { return RepoManager::instance().toggleRepository(repoId, enable, err); });
}

} // namespace lut
