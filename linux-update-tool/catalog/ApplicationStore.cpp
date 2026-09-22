#include "linux-update-tool/catalog/ApplicationStore.h"
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QDir>
#include <QCoreApplication>
#include <QVersionNumber>

#ifdef HAVE_ALPM
#include <alpm.h>
#endif

namespace lut {

ApplicationStore::ApplicationStore(CatalogService *catalogService,
                                   PackageCatalog *packageCatalog,
                                   QObject *parent)
    : QObject(parent)
    , m_catalogService(catalogService)
    , m_packageCatalog(packageCatalog)
{
    loadCuratedCollections();
    if (m_catalogService) {
        connect(m_catalogService, &CatalogService::loadingChanged, this, &ApplicationStore::loadingChanged);
        connect(m_catalogService, &CatalogService::loadedChanged, this, &ApplicationStore::loadedChanged);
        connect(m_catalogService, &CatalogService::loaded, this, &ApplicationStore::onCatalogServiceLoaded);
    }
}

ApplicationStore::~ApplicationStore() = default;

bool ApplicationStore::isLoading() const
{
    return m_catalogService ? m_catalogService->isLoading() : false;
}

bool ApplicationStore::isLoaded() const
{
    return m_catalogService ? m_catalogService->isLoaded() : false;
}

QString ApplicationStore::lastError() const
{
    return m_catalogService ? m_catalogService->lastError() : QString();
}

int ApplicationStore::totalAppCount() const
{
    return allApps().size();
}

int ApplicationStore::installedAppCount() const
{
    int count = 0;
    const auto apps = allApps();
    for (const auto &app : apps) {
        if (installedState(app.appKey).isFullyInstalled) {
            count++;
        }
    }
    return count;
}

QList<AppRecord> ApplicationStore::allApps() const
{
    return m_catalogService ? m_catalogService->allApps() : QList<AppRecord>();
}

std::optional<AppRecord> ApplicationStore::appRecord(const QString &appKey) const
{
    return m_catalogService ? m_catalogService->appByKey(appKey) : std::nullopt;
}

std::optional<PackageOffer> ApplicationStore::candidateOffer(const QString &appKey) const
{
    if (!m_packageCatalog || !m_catalogService) {
        return std::nullopt;
    }

    auto app = m_catalogService->appByKey(appKey);
    if (!app || app->defaultPackageName.isEmpty()) {
        return std::nullopt;
    }

    return m_packageCatalog->candidateOffer(app->defaultPackageName);
}

QList<PackageOffer> ApplicationStore::allOffers(const QString &appKey) const
{
    if (!m_packageCatalog || !m_catalogService) {
        return {};
    }

    auto app = m_catalogService->appByKey(appKey);
    if (!app || app->defaultPackageName.isEmpty()) {
        return {};
    }

    return m_packageCatalog->offersForPackage(app->defaultPackageName);
}

InstalledState ApplicationStore::installedState(const QString &appKey) const
{
    if (!m_packageCatalog || !m_catalogService) {
        return {};
    }

    auto app = m_catalogService->appByKey(appKey);
    if (!app || app->defaultPackageName.isEmpty()) {
        return {};
    }

    return m_packageCatalog->installedStateForPackage(app->defaultPackageName);
}

int ApplicationStore::compareNativeVersions(const QString &backend, const QString &v1, const QString &v2, const PackageCatalog *catalog)
{
    if (catalog) {
        return catalog->compareVersions(v1, v2);
    }
#ifdef HAVE_ALPM
    if (backend.isEmpty() || backend == QLatin1String("alpm")) {
        return alpm_pkg_vercmp(v1.toUtf8().constData(), v2.toUtf8().constData());
    }
#else
    Q_UNUSED(backend);
#endif
    const QVersionNumber n1 = QVersionNumber::fromString(v1);
    const QVersionNumber n2 = QVersionNumber::fromString(v2);
    if (!n1.isNull() && !n2.isNull()) {
        return QVersionNumber::compare(n1, n2);
    }
    return QString::compare(v1, v2);
}

AppActionState ApplicationStore::actionState(const QString &appKey) const
{
    InstalledState inst = installedState(appKey);
    auto cand = candidateOffer(appKey);

    if (inst.isFullyInstalled) {
        if (cand && !cand->packages.isEmpty() && !inst.installedPackages.isEmpty()) {
            const QString backend = cand->packages.first().backend;
            const QString candVer = cand->packages.first().version;
            const QString instVer = inst.installedPackages.first().version;

            if (compareNativeVersions(backend, candVer, instVer, m_packageCatalog) > 0) {
                return AppActionState::UpdateAvailable;
            }
        }

        if (!inst.launchableDesktopIds.isEmpty()) {
            return AppActionState::Installed;
        }
        return AppActionState::InstalledNoLaunch;
    }

    if (inst.isPartiallyInstalled) {
        return AppActionState::PartiallyInstalled;
    }

    if (cand && cand->available) {
        return AppActionState::Available;
    }

    return AppActionState::Unavailable;
}

QList<AppRecord> ApplicationStore::searchApps(const QString &query) const
{
    return m_catalogService ? m_catalogService->search(query) : QList<AppRecord>();
}

QList<AppRecord> ApplicationStore::appsByCategory(const QString &category) const
{
    return m_catalogService ? m_catalogService->appsByCategory(category) : QList<AppRecord>();
}

QList<PackageOffer> ApplicationStore::searchPackagesOnly(const QString &query) const
{
    if (!m_packageCatalog) {
        return {};
    }

    QList<PackageOffer> rawOffers = m_packageCatalog->searchPackages(query);
    QList<PackageOffer> filtered;

    for (const auto &offer : rawOffers) {
        if (offer.packages.isEmpty()) continue;
        const QString pkgName = offer.packages.first().name;
        // In package-only mode, only include packages that DO NOT have an AppStream AppRecord
        if (m_catalogService && m_catalogService->appByPackageName(pkgName).has_value()) {
            continue;
        }
        filtered.append(offer);
    }

    return filtered;
}

void ApplicationStore::refresh()
{
    if (m_packageCatalog) {
        m_packageCatalog->reload();
    }
    if (m_catalogService) {
        m_catalogService->reload();
    } else {
        emit catalogLoaded();
    }
}

void ApplicationStore::onCatalogServiceLoaded(bool success)
{
    if (success) {
        logDiagnostics();
    }
    emit catalogLoaded();
}

void ApplicationStore::logDiagnostics()
{
    const auto apps = allApps();
    int total = apps.size();
    int available = 0;
    int installed = 0;
    int unresolvable = 0;

    for (const auto &app : apps) {
        InstalledState inst = installedState(app.appKey);
        if (inst.isFullyInstalled) {
            installed++;
        }

        auto cand = candidateOffer(app.appKey);
        if (cand && cand->available) {
            available++;
        } else {
            unresolvable++;
        }
    }

    qInfo() << "Store catalog loaded:" << total << "components ("
            << available << "with native candidate offer,"
            << installed << "installed,"
            << unresolvable << "unresolvable/no package candidate)";
}

void ApplicationStore::loadCuratedCollections(const QString &filePath)
{
    m_curatedCollections.clear();

    QStringList candidates;
    if (!filePath.isEmpty()) {
        candidates.append(filePath);
    }
    candidates.append(QStringLiteral(":/LinuxUpdateTool/store/curated.json"));
    candidates.append(QStringLiteral("data/store/curated.json"));
    candidates.append(QCoreApplication::applicationDirPath() + QStringLiteral("/../data/store/curated.json"));
    candidates.append(QStringLiteral("/usr/share/linux-update-tool/curated.json"));

    QByteArray data;
    for (const QString &path : candidates) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            data = file.readAll();
            break;
        }
    }

    if (data.isEmpty()) {
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        return;
    }

    const QJsonArray cols = doc.object().value(QStringLiteral("collections")).toArray();
    for (const auto &val : cols) {
        if (!val.isObject()) continue;
        QJsonObject obj = val.toObject();
        CuratedCollection c;
        c.id = obj.value(QStringLiteral("id")).toString();
        c.title = obj.value(QStringLiteral("title")).toString();
        c.description = obj.value(QStringLiteral("description")).toString();
        const QJsonArray ids = obj.value(QStringLiteral("appIds")).toArray();
        for (const auto &idVal : ids) {
            c.appIds.append(idVal.toString());
        }
        m_curatedCollections.append(c);
    }
}

QVariantMap ApplicationStore::getApp(const QString &appKey) const
{
    auto app = appRecord(appKey);
    if (app) {
        return app->toJson().toVariantMap();
    }
    return {};
}

QString ApplicationStore::getActionState(const QString &appKey) const
{
    return appActionStateToString(actionState(appKey));
}

QVariantMap ApplicationStore::getCandidateOffer(const QString &appKey) const
{
    auto cand = candidateOffer(appKey);
    if (cand) {
        return cand->toJson().toVariantMap();
    }
    return {};
}

QVariantMap ApplicationStore::getInstalledState(const QString &appKey) const
{
    InstalledState inst = installedState(appKey);
    return inst.toJson().toVariantMap();
}

QVariantList ApplicationStore::getCuratedCollection(const QString &collectionKey) const
{
    QVariantList result;
    for (const auto &col : m_curatedCollections) {
        if (col.id == collectionKey) {
            for (const QString &appId : col.appIds) {
                auto app = appRecord(appId);
                if (app.has_value()) {
                    auto cand = candidateOffer(appId);
                    InstalledState inst = installedState(appId);
                    if ((cand && cand->available) || inst.isFullyInstalled) {
                        result.append(app->toJson().toVariantMap());
                    }
                }
            }
            break;
        }
    }
    return result;
}

QVariantList ApplicationStore::curatedCollections() const
{
    QVariantList result;
    for (const auto &col : m_curatedCollections) {
        int availableCount = 0;
        for (const QString &appId : col.appIds) {
            auto app = appRecord(appId);
            if (app.has_value()) {
                auto cand = candidateOffer(appId);
                InstalledState inst = installedState(appId);
                if ((cand && cand->available) || inst.isFullyInstalled) {
                    availableCount++;
                }
            }
        }
        if (availableCount > 0) {
            QVariantMap map;
            map[QStringLiteral("id")] = col.id;
            map[QStringLiteral("title")] = col.title;
            map[QStringLiteral("description")] = col.description;
            map[QStringLiteral("appCount")] = availableCount;
            result.append(map);
        }
    }
    return result;
}

QStringList ApplicationStore::curatedCollectionKeys() const
{
    QStringList keys;
    for (const auto &col : m_curatedCollections) {
        keys.append(col.id);
    }
    return keys;
}

void ApplicationStore::requestInstall(const QString &appKey)
{
    auto cand = candidateOffer(appKey);
    QString pkgName;
    QString repoId;
    if (cand && !cand->packages.isEmpty()) {
        pkgName = cand->packages.first().name;
        repoId = cand->packages.first().repoId;
    } else {
        auto app = appRecord(appKey);
        if (app && !app->defaultPackageName.isEmpty()) {
            pkgName = app->defaultPackageName;
        }
    }
    if (!pkgName.isEmpty()) {
        emit installRequested(pkgName, repoId);
        emit appStateChanged(appKey);
    }
}

void ApplicationStore::requestRemove(const QString &appKey)
{
    QString pkgName;
    InstalledState inst = installedState(appKey);
    if (!inst.installedPackages.isEmpty()) {
        pkgName = inst.installedPackages.first().name;
    } else {
        auto app = appRecord(appKey);
        if (app && !app->defaultPackageName.isEmpty()) {
            pkgName = app->defaultPackageName;
        }
    }
    if (!pkgName.isEmpty()) {
        emit removeRequested(pkgName);
        emit appStateChanged(appKey);
    }
}

bool ApplicationStore::launchApp(const QString &appKey)
{
    auto app = appRecord(appKey);
    QString desktopId;
    InstalledState inst = installedState(appKey);
    if (!inst.launchableDesktopIds.isEmpty()) {
        desktopId = inst.launchableDesktopIds.first();
    } else if (app && !app->launchableDesktopIds.isEmpty()) {
        desktopId = app->launchableDesktopIds.first();
    } else if (app) {
        desktopId = app->componentId;
    }
    if (desktopId.isEmpty()) {
        const QString err = QStringLiteral("Kein Desktop-Eintrag für '%1' bekannt.").arg(appKey);
        emit appLaunchFailed(appKey, err);
        return false;
    }

    QString errorMsg;
    bool ok = m_launcher.launchDesktopId(desktopId, &errorMsg);
    if (ok) {
        emit appLaunched(appKey);
    } else {
        emit appLaunchFailed(appKey, errorMsg);
    }
    return ok;
}

} // namespace lut
