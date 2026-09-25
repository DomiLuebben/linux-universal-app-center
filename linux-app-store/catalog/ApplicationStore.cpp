#include "linux-app-store/catalog/ApplicationStore.h"
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
#include <QRunnable>
#include <utility>

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
    if (packageCatalog) {
        m_packageCatalogs.append(packageCatalog);
    }
    m_worker.setMaxThreadCount(1);
    loadCuratedCollections();
    if (m_catalogService) {
        connect(m_catalogService, &CatalogService::loadingChanged, this, &ApplicationStore::loadingChanged);

        connect(m_catalogService, &CatalogService::loaded, this, &ApplicationStore::onCatalogServiceLoaded);
        if (m_catalogService->isLoaded()) buildSnapshot();
    }
}

void ApplicationStore::setPackageCatalog(PackageCatalog *packageCatalog)
{
    m_packageCatalog = packageCatalog;
    m_packageCatalogs.clear();
    if (packageCatalog) {
        m_packageCatalogs.append(packageCatalog);
    }
}

void ApplicationStore::addPackageCatalog(PackageCatalog *packageCatalog)
{
    if (packageCatalog && !m_packageCatalogs.contains(packageCatalog)) {
        m_packageCatalogs.append(packageCatalog);
        if (!m_packageCatalog) {
            m_packageCatalog = packageCatalog;
        }
    }
}

QList<PackageCatalog*> ApplicationStore::packageCatalogs() const
{
    return m_packageCatalogs;
}

void ApplicationStore::setOffersForApp(const QString &appKey, const QList<PackageOffer> &offers)
{
    const QString normKey = CatalogService::normalizedAppKey(appKey);
    m_snapshot.appOffers.insert(normKey, offers);
}

void ApplicationStore::setInstalledForApp(const QString &appKey, const InstalledState &state)
{
    const QString normKey = CatalogService::normalizedAppKey(appKey);
    m_snapshot.appInstalled.insert(normKey, state);
}

ApplicationStore::~ApplicationStore()
{
    // The package catalog must outlive pending workers; callbacks are bound to this QObject.
    m_worker.clear();
    m_worker.waitForDone();
}

bool ApplicationStore::isLoading() const
{
    return m_snapshotLoading || (m_catalogService && m_catalogService->isLoading());
}

bool ApplicationStore::isLoaded() const
{
    return m_snapshotLoaded;
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
    if (auto it = m_packageRecords.constFind(appKey); it != m_packageRecords.cend()) return *it;
    if (!m_catalogService) return std::nullopt;
    auto app = m_catalogService->appByKey(appKey);
    if (app && app->defaultPackageName.isEmpty()) {
        QStringList resolved = m_resolvedPackages.value(appKey);
        if (resolved.isEmpty()) {
            resolved = m_resolvedPackages.value(CatalogService::normalizedAppKey(appKey));
        }
        if (!resolved.isEmpty()) {
            app->packageNames = resolved;
            app->defaultPackageName = resolved.first();
        }
    }
    return app;
}

std::optional<PackageOffer> ApplicationStore::candidateOffer(const QString &appKey) const
{
    if (!m_catalogService) {
        return std::nullopt;
    }

    const auto offers = allOffers(appKey);
    if (offers.isEmpty()) {
        return std::nullopt;
    }

    // Abschnitt 3.2: "Ist die Anwendung bereits aus einer Quelle installiert,
    // ist diese Quelle vorausgewählt — unabhängig vom Rang.
    // Hat die installierte Quelle kein Angebot mehr (deaktiviertes Repo),
    // bleibt sie vorausgewählt mit dem Zustand „Installiert, Quelle nicht verfügbar“."
    InstalledState inst = installedState(appKey);
    if (inst.isFullyInstalled && !inst.installedPackages.isEmpty()) {
        for (const auto &pkg : inst.installedPackages) {
            const QString instBackend = pkg.backend;
            for (const auto &offer : offers) {
                if (offer.source() == instBackend && offer.available) {
                    return offer;
                }
            }
        }
        return std::nullopt;
    }

    // Abschnitt 3.1: "Die Vorauswahl ist immer das verfügbare Angebot mit dem höchsten Rang.
    // Sind alle drei verfügbar, ist nativ vorausgewählt. Fehlt nativ, ist Flatpak vorausgewählt."
    for (const auto &offer : offers) {
        if (offer.isCandidate && offer.available) {
            return offer;
        }
    }
    for (const auto &offer : offers) {
        if (offer.available) {
            return offer;
        }
    }

    return offers.first();
}

QList<PackageOffer> ApplicationStore::allOffers(const QString &appKey) const
{
    if (!m_catalogService) {
        return {};
    }

    auto app = appRecord(appKey);
    QList<PackageOffer> combinedOffers;

    // 1. Direkt an der AppKey hinterlegte Angebote (z.B. Flatpak/Snap oder Mocks)
    const QString normKey = CatalogService::normalizedAppKey(appKey);
    if (m_snapshot.appOffers.contains(normKey)) {
        combinedOffers.append(m_snapshot.appOffers.value(normKey));
    }
    if (m_snapshot.appOffers.contains(appKey) && appKey != normKey) {
        for (const auto &o : m_snapshot.appOffers.value(appKey)) {
            if (!combinedOffers.contains(o)) {
                combinedOffers.append(o);
            }
        }
    }

    // 2. Paketbezogene Angebote aus m_snapshot.offers
    if (app) {
        QStringList names;
        if (!app->defaultPackageName.isEmpty()) {
            names.append(app->defaultPackageName);
        }
        for (const QString &pkgName : app->packageNames) {
            if (!pkgName.isEmpty() && !names.contains(pkgName)) {
                names.append(pkgName);
            }
        }
        for (const QString &name : names) {
            const auto pkgOffers = m_snapshot.offers.value(name);
            for (const auto &po : pkgOffers) {
                if (!combinedOffers.contains(po)) {
                    combinedOffers.append(po);
                }
            }
        }
    } else {
        const auto directOffers = m_snapshot.offers.value(appKey);
        for (const auto &po : directOffers) {
            if (!combinedOffers.contains(po)) {
                combinedOffers.append(po);
            }
        }
    }

    sortPackageOffers(combinedOffers);
    return combinedOffers;
}

QStringList ApplicationStore::packageSet(const QString &appKey) const
{
    if (!m_catalogService) {
        return {};
    }

    auto app = appRecord(appKey);
    if (!app) {
        return {};
    }

    QStringList names;
    if (!app->defaultPackageName.isEmpty()) {
        names.append(app->defaultPackageName);
    }
    for (const QString &name : app->packageNames) {
        if (!name.isEmpty() && !names.contains(name)) {
            names.append(name);
        }
    }
    if (names.isEmpty()) {
        if (m_resolvedPackages.contains(appKey)) {
            names = m_resolvedPackages.value(appKey);
        } else {
            const QString normKey = CatalogService::normalizedAppKey(appKey);
            if (m_resolvedPackages.contains(normKey)) {
                names = m_resolvedPackages.value(normKey);
            }
        }
    }
    return names;
}

QStringList ApplicationStore::appsProvidedByPackages(const QStringList &packageNames) const
{
    if (!m_catalogService) {
        return {};
    }

    QStringList appNames;
    for (const AppRecord &app : m_catalogService->allApps()) {
        bool matches = false;
        for (const QString &pkg : packageNames) {
            if (app.defaultPackageName == pkg || app.packageNames.contains(pkg)) {
                matches = true;
                break;
            }
        }
        if (!matches) continue;

        const QString label = app.name.isEmpty() ? app.appKey : app.name;
        if (!label.isEmpty() && !appNames.contains(label)) {
            appNames.append(label);
        }
    }
    appNames.sort(Qt::CaseInsensitive);
    return appNames;
}

InstalledState ApplicationStore::installedState(const QString &appKey) const
{
    if (!m_catalogService) {
        return {};
    }

    const QString normKey = CatalogService::normalizedAppKey(appKey);
    InstalledState merged;

    if (m_snapshot.appInstalled.contains(normKey)) {
        merged = m_snapshot.appInstalled.value(normKey);
    } else if (m_snapshot.appInstalled.contains(appKey)) {
        merged = m_snapshot.appInstalled.value(appKey);
    }

    const QStringList names = packageSet(appKey);
    if (names.isEmpty()) {
        return merged;
    }

    // Eine Anwendung gilt erst als vollständig installiert, wenn jedes Paket
    // ihres Satzes installiert ist. Fehlt eines, ist sie nur teilweise da.
    int installedCount = 0;
    for (const QString &name : names) {
        const InstalledState part = m_snapshot.installed.value(name);
        if (part.isFullyInstalled) {
            ++installedCount;
        }
        for (const PackageRef &ref : part.installedPackages) {
            if (!merged.installedPackages.contains(ref)) {
                merged.installedPackages.append(ref);
            }
        }
        for (const QString &desktopId : part.launchableDesktopIds) {
            if (!merged.launchableDesktopIds.contains(desktopId)) {
                merged.launchableDesktopIds.append(desktopId);
            }
        }
        if (merged.origin.isEmpty()) {
            merged.origin = part.origin;
        }
        merged.inventoryRevision = std::max(merged.inventoryRevision, part.inventoryRevision);
    }

    if (installedCount == names.size() && installedCount > 0) {
        merged.isFullyInstalled = true;
    } else if (installedCount > 0 && installedCount < names.size()) {
        if (!merged.isFullyInstalled) {
            merged.isPartiallyInstalled = true;
        }
    }
    return merged;
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

bool ApplicationStore::isSourceInstalled(const QString &appKey, const QString &source) const
{
    const InstalledState inst = installedState(appKey);
    if (!inst.isFullyInstalled && !inst.isPartiallyInstalled) {
        return false;
    }

    const QString s = source.toLower();
    for (const PackageRef &ref : inst.installedPackages) {
        const QString b = ref.backend.toLower();
        if (s == QLatin1String("flatpak")) {
            if (b == QLatin1String("flatpak")) return true;
        } else if (s == QLatin1String("snap")) {
            if (b == QLatin1String("snap")) return true;
        } else if (s == QLatin1String("native") || s == QLatin1String("alpm") || s == QLatin1String("dnf5") || s == QLatin1String("apt")) {
            if (b.isEmpty() || b == QLatin1String("alpm") || b == QLatin1String("dnf5") || b == QLatin1String("apt")) {
                return true;
            }
        } else {
            if (b == s || ref.repoId.compare(s, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
    }
    return false;
}

AppActionState ApplicationStore::actionState(const QString &appKey, const QString &selectedSource) const
{
    const auto names = packageSet(appKey);
    bool involved = false;
    for (const auto &name : names) if (m_operationPackages.contains(name)) involved = true;
    for (const auto &pkg : m_operationPackages) {
        if (appKey == pkg || CatalogService::normalizedAppKey(appKey) == CatalogService::normalizedAppKey(pkg)) {
            involved = true;
            break;
        }
    }
    if (involved) return m_operationState;
    if (m_transactionBusy || m_transactionPlan) return AppActionState::OtherTransactionRunning;

    const InstalledState inst = installedState(appKey);
    const auto offers = allOffers(appKey);

    std::optional<PackageOffer> targetOffer;
    if (!selectedSource.isEmpty()) {
        const QString s = selectedSource.toLower();
        for (const auto &o : offers) {
            const QString src = o.source().toLower();
            QString repo;
            QString bk;
            if (!o.packages.isEmpty()) {
                repo = o.packages.first().repoId.toLower();
                bk = o.packages.first().backend.toLower();
            }
            if (src == s || bk == s || repo == s ||
                (s == QLatin1String("native") && (src == QLatin1String("alpm") || src == QLatin1String("dnf5") || src == QLatin1String("apt") || src.isEmpty()))) {
                targetOffer = o;
                break;
            }
        }
    }
    if (!targetOffer.has_value()) {
        targetOffer = candidateOffer(appKey);
    }

    if (!inst.isFullyInstalled && targetOffer && targetOffer->available && !m_installSupported)
        return AppActionState::ActionUnsupported;

    if (inst.isFullyInstalled) {
        // Section 8.1: Prüfen, ob die gewählte Quelle installiert ist oder eine andere
        if (!selectedSource.isEmpty()) {
            bool thisSourceInstalled = isSourceInstalled(appKey, selectedSource);
            if (!thisSourceInstalled) {
                return AppActionState::InstalledOtherSource;
            }
        }

        if (targetOffer && !targetOffer->packages.isEmpty() && !inst.installedPackages.isEmpty()) {
            const QString backend = targetOffer->packages.first().backend;
            const QString candVer = targetOffer->packages.first().version;
            QString instVer;
            for (const auto &ip : inst.installedPackages) {
                if (ip.backend == backend || (backend.isEmpty() && (ip.backend == QLatin1String("alpm") || ip.backend == QLatin1String("dnf5") || ip.backend == QLatin1String("apt")))) {
                    instVer = ip.version;
                    break;
                }
            }
            if (instVer.isEmpty()) {
                instVer = inst.installedPackages.first().version;
            }

            if (!candVer.isEmpty() && !instVer.isEmpty() && compareNativeVersions(backend, candVer, instVer, m_packageCatalog) > 0) {
                return AppActionState::UpdateAvailable;
            }
        }

        // Installiert, aber in den aktivierten Quellen nicht mehr angeboten.
        // Starten bleibt möglich, eine Neuinstallation darf nicht versprochen werden.
        if (!targetOffer || !targetOffer->available) {
            return AppActionState::MissingSource;
        }

        if (!inst.launchableDesktopIds.isEmpty()) {
            return AppActionState::Installed;
        }
        return AppActionState::InstalledNoLaunch;
    }

    if (inst.isPartiallyInstalled) {
        return AppActionState::PartiallyInstalled;
    }

    if (targetOffer && targetOffer->available) {
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

    if (query != m_searchQuery) {
        auto *self = const_cast<ApplicationStore *>(this);
        m_searchQuery = query;
        m_searchResults.clear();
        const quint64 request = ++m_searchRequest;
        m_latestSearch.store(request);
        self->m_worker.start(QRunnable::create([self, query, request] {
            if (self->m_latestSearch.load() != request) return;
            const auto offers = self->m_packageCatalog->searchPackages(query);
            QStringList names;
            for (const auto &offer : offers) if (!offer.packages.isEmpty()) names.append(offer.packages.first().name);
            self->m_packageCatalog->prepareSnapshot(names);
            QHash<QString, InstalledState> installed;
            for (const auto &name : names) installed.insert(name, self->m_packageCatalog->installedStateForPackage(name));
            QMetaObject::invokeMethod(self, [self, request, offers, installed] {
                if (request != self->m_searchRequest) return;
                self->m_searchResults = offers;
                for (const auto &offer : offers) {
                    if (offer.packages.isEmpty()) continue;
                    const auto &pkg = offer.packages.first();
                    AppRecord record;
                    record.appKey = QStringLiteral("pkg:") + pkg.name;
                    record.name = pkg.name;
                    record.defaultPackageName = pkg.name;
                    record.packageNames = {pkg.name};
                    record.summary = QStringLiteral("Repository-Paket: %1").arg(pkg.repoId);
                    record.iconSource = QStringLiteral("package-x-generic");
                    record.origin = pkg.backend;
                    self->m_packageRecords.insert(record.appKey, record);
                    self->m_snapshot.candidates.insert(pkg.name, offer);
                    self->m_snapshot.offers.insert(pkg.name, {offer});
                    self->m_snapshot.installed.insert(pkg.name, installed.value(pkg.name));
                }
                ++self->m_revision;
                emit self->catalogLoaded();
            }, Qt::QueuedConnection);
        }));
    }
    const auto rawOffers = m_searchResults;
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

const QSet<QString> &ApplicationStore::installedPackageNames() const
{
    const quint64 generation = m_packageCatalog ? m_packageCatalog->catalogGeneration() : 0;
    if (m_installedNamesLoaded && generation == m_installedNamesGeneration) {
        return m_installedNames;
    }

    m_installedNames.clear();
    if (m_packageCatalog) {
        for (const InstalledPackage &pkg : m_packageCatalog->allInstalledPackages()) {
            if (!pkg.name.isEmpty()) {
                m_installedNames.insert(pkg.name);
            }
        }
    }
    m_installedNamesGeneration = generation;
    m_installedNamesLoaded = true;
    return m_installedNames;
}

void ApplicationStore::refresh()
{
    if (isLoading()) { m_refreshPending = true; return; }
    if (m_catalogService) m_catalogService->reload();
    else buildSnapshot();
}

void ApplicationStore::refreshInstalledState()
{
    if (isLoading()) { m_refreshPending = true; return; }
    if (m_catalogService && !m_catalogService->isLoaded()) { refresh(); return; }
    buildSnapshot();
}

void ApplicationStore::onCatalogServiceLoaded(bool success)
{
    if (success) buildSnapshot();
    else {
        emit loadingChanged(false);
        emit catalogLoaded();
    }
}

void ApplicationStore::resolvePackageNamesForApps(const QList<AppRecord> &apps,
                                                  const QSet<QString> &installedPackageNames,
                                                  QHash<QString, QStringList> &resolvedOut)
{
    if (!m_packageCatalog) {
        return;
    }

    for (const auto &app : apps) {
        QStringList currentPkgs = app.packageNames;
        if (currentPkgs.isEmpty() && !app.defaultPackageName.isEmpty()) {
            currentPkgs.append(app.defaultPackageName);
        }
        if (currentPkgs.isEmpty() && m_resolvedPackages.contains(app.appKey)) {
            currentPkgs = m_resolvedPackages.value(app.appKey);
        }

        if (!currentPkgs.isEmpty()) {
            continue;
        }

        QString resolvedName;

        // 1. Check installed desktop files owner via findPackagesProvidingFile
        QStringList desktopCandidates = app.launchableDesktopIds;
        if (!app.appKey.isEmpty()) {
            desktopCandidates.append(app.appKey);
            if (!app.appKey.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) {
                desktopCandidates.append(app.appKey + QStringLiteral(".desktop"));
            }
        }
        if (!app.componentId.isEmpty() && app.componentId != app.appKey) {
            desktopCandidates.append(app.componentId);
            if (!app.componentId.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) {
                desktopCandidates.append(app.componentId + QStringLiteral(".desktop"));
            }
        }
        desktopCandidates.removeDuplicates();

        for (const QString &dtId : desktopCandidates) {
            QString filePath = AppLauncher::resolveDesktopFilePath(dtId);
            if (!filePath.isEmpty() && QFile::exists(filePath)) {
                auto owners = m_packageCatalog->findPackagesProvidingFile(filePath);
                if (!owners.isEmpty() && !owners.first().name.isEmpty()) {
                    resolvedName = owners.first().name;
                    break;
                }
            }
        }

        // 2. Check installed metainfo files owner
        if (resolvedName.isEmpty()) {
            QStringList metainfoCandidates = {
                QStringLiteral("/usr/share/metainfo/%1.metainfo.xml").arg(app.appKey),
                QStringLiteral("/usr/share/metainfo/%1.xml").arg(app.appKey),
                QStringLiteral("/usr/share/metainfo/%1.metainfo.xml").arg(app.componentId),
                QStringLiteral("/usr/share/metainfo/%1.xml").arg(app.componentId),
                QStringLiteral("/usr/share/appdata/%1.appdata.xml").arg(app.appKey),
                QStringLiteral("/usr/share/appdata/%1.appdata.xml").arg(app.componentId)
            };
            for (const QString &mPath : metainfoCandidates) {
                if (QFile::exists(mPath)) {
                    auto owners = m_packageCatalog->findPackagesProvidingFile(mPath);
                    if (!owners.isEmpty() && !owners.first().name.isEmpty()) {
                        resolvedName = owners.first().name;
                        break;
                    }
                }
            }
        }

        // 3. Nur noch für bereits installierte Anwendungen: Namensableitung aus der
        //    Reverse-DNS-Kennung, dem Schlüssel und dem Anzeigenamen. Abschnitt 4.5
        //    des Plans verbietet solche Ableitungen als Paketnamen ausdrücklich; für
        //    den Bestand sind sie zulässig, weil nur ein tatsächlich installiertes
        //    Paket den Abgleich besteht (Abschnitt 4.5, Punkt 10).
        if (resolvedName.isEmpty()) {
            QStringList candidateNames;

            // Last segment of reverse DNS: e.g. "com.usebottles.bottles" -> "bottles"
            QString norm = CatalogService::normalizedAppKey(app.appKey);
            int lastDot = norm.lastIndexOf(QLatin1Char('.'));
            if (lastDot >= 0 && lastDot < norm.length() - 1) {
                candidateNames.append(norm.mid(lastDot + 1).toLower());
            }

            // Normalized key itself (lowercased)
            candidateNames.append(norm.toLower());

            // From launchables (last segment)
            for (const QString &lId : app.launchableDesktopIds) {
                QString lNorm = CatalogService::normalizedAppKey(lId);
                int lDot = lNorm.lastIndexOf(QLatin1Char('.'));
                if (lDot >= 0 && lDot < lNorm.length() - 1) {
                    candidateNames.append(lNorm.mid(lDot + 1).toLower());
                }
                candidateNames.append(lNorm.toLower());
            }

            // App name itself (e.g. "Bottles" -> "bottles")
            QString cleanName = app.name.trimmed().toLower();
            cleanName.replace(QRegularExpression(QStringLiteral(R"([^a-z0-9+.-])")), QStringLiteral("-"));
            cleanName.remove(QRegularExpression(QStringLiteral(R"(^-+|-+$)")));
            if (!cleanName.isEmpty() && cleanName.length() >= 2) {
                candidateNames.append(cleanName);
            }

            candidateNames.removeDuplicates();

            // Match against installed packages first
            for (const QString &cand : candidateNames) {
                if (cand.isEmpty() || cand == QLatin1String("app") || cand == QLatin1String("desktop")) {
                    continue;
                }
                if (installedPackageNames.contains(cand)) {
                    resolvedName = cand;
                    break;
                }
            }

            // Bewusst kein Abgleich gegen verfügbare Repository-Angebote: Damit
            // würde aus einer Komponente ohne <pkgname> ein erfundenes
            // Installationsziel. Eine Anwendung namens "Files" oder mit der Kennung
            // org.example.git träfe sonst ein gleichnamiges, völlig unbeteiligtes
            // Paket und böte dessen Installation unter fremdem Namen und Symbol an.
            // Abschnitt 4.5 Punkt 8 und 9 verlangen eine eindeutige Auflösung;
            // Punkt 10 erlaubt die Ableitung ausdrücklich nur für den Bestand.
        }

        if (!resolvedName.isEmpty()) {
            resolvedOut.insert(app.appKey, {resolvedName});
        }
    }
}

void ApplicationStore::buildSnapshot()
{
    if (m_snapshotLoading) { m_refreshPending = true; return; }
    m_snapshotLoading = true;
    emit loadingChanged(true);

    const auto apps = allApps();
    const auto packageRecords = m_packageRecords;
    const auto flatpakOffers = m_catalogService ? m_catalogService->allFlatpakOffers() : QHash<QString, QList<PackageOffer>>();
    const auto packageCatalogs = m_packageCatalogs;

    m_worker.start(QRunnable::create([this, apps, packageRecords, flatpakOffers, packageCatalogs] {
        Snapshot result;
        result.appOffers = flatpakOffers;
        QHash<QString, QStringList> resolved;

        if (m_packageCatalog) {
            m_packageCatalog->reload();

            QSet<QString> installed;
            for (const auto &pkg : m_packageCatalog->allInstalledPackages()) {
                if (!pkg.name.isEmpty()) {
                    installed.insert(pkg.name);
                }
            }

            resolvePackageNamesForApps(apps, installed, resolved);

            QStringList names;
            for (const auto &app : apps) {
                if (resolved.contains(app.appKey)) {
                    names.append(resolved.value(app.appKey));
                } else if (!app.defaultPackageName.isEmpty() || !app.packageNames.isEmpty()) {
                    if (!app.defaultPackageName.isEmpty()) names.append(app.defaultPackageName);
                    for (const auto &p : app.packageNames) if (!p.isEmpty()) names.append(p);
                } else if (m_resolvedPackages.contains(app.appKey)) {
                    names.append(m_resolvedPackages.value(app.appKey));
                }
            }
            for (const auto &record : packageRecords) {
                names.append(record.packageNames);
            }
            names.removeDuplicates();

            m_packageCatalog->prepareSnapshot(names);

            for (const auto &name : names) {
                result.offers.insert(name, m_packageCatalog->offersForPackage(name));
                if (auto candidate = m_packageCatalog->candidateOffer(name)) {
                    result.candidates.insert(name, *candidate);
                }
                if (installed.contains(name)) {
                    result.installed.insert(name, m_packageCatalog->installedStateForPackage(name));
                }
            }
        }

        for (auto *catalog : packageCatalogs) {
            if (catalog && catalog != m_packageCatalog) {
                catalog->reload();
                for (const auto &pkg : catalog->allInstalledPackages()) {
                    if (!pkg.name.isEmpty()) {
                        const QString normKey = CatalogService::normalizedAppKey(pkg.name);
                        auto state = catalog->installedStateForPackage(pkg.name);
                        if (result.appInstalled.contains(normKey)) {
                            auto &existing = result.appInstalled[normKey];
                            for (const auto &ref : state.installedPackages) {
                                if (!existing.installedPackages.contains(ref)) existing.installedPackages.append(ref);
                            }
                            for (const auto &dId : state.launchableDesktopIds) {
                                if (!existing.launchableDesktopIds.contains(dId)) existing.launchableDesktopIds.append(dId);
                            }
                            existing.isFullyInstalled = existing.isFullyInstalled || state.isFullyInstalled;
                        } else {
                            result.appInstalled.insert(normKey, state);
                        }

                        if (auto cand = catalog->candidateOffer(pkg.name)) {
                            auto &offers = result.appOffers[normKey];
                            bool hasOffer = false;
                            for (const auto &off : offers) {
                                if (off.source() == cand->source()) {
                                    hasOffer = true;
                                    break;
                                }
                            }
                            if (!hasOffer) {
                                offers.append(*cand);
                                sortPackageOffers(offers);
                            }
                        }
                    }
                }
            }
        }

        QMetaObject::invokeMethod(this, [this, result = std::move(result), resolved = std::move(resolved)]() mutable {
            for (auto it = resolved.constBegin(); it != resolved.constEnd(); ++it) {
                m_resolvedPackages.insert(it.key(), it.value());
                if (m_catalogService) {
                    m_catalogService->setPackageNamesForApp(it.key(), it.value());
                }
            }
            m_snapshot = std::move(result);
            m_snapshotLoading = false;
            m_snapshotLoaded = true;
            if (m_operationState == AppActionState::Reconciling) {
                if (m_operationFailed) m_operationState = AppActionState::ErrorOrCancelled;
                else m_operationPackages.clear();
            }
            ++m_revision;
            m_latestSearch.store(++m_searchRequest);
            m_searchQuery.clear();
            m_searchResults.clear();
            emit loadingChanged(false);
            emit loadedChanged(true);
            logDiagnostics();
            emit catalogLoaded();
            if (std::exchange(m_refreshPending, false)) refresh();
        }, Qt::QueuedConnection);
    }));
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
    candidates.append(QStringLiteral(":/LinuxAppStore/store/curated.json"));
    candidates.append(QStringLiteral(":/LinuxUpdateTool/store/curated.json"));
    candidates.append(QStringLiteral("data/store/curated.json"));
    candidates.append(QCoreApplication::applicationDirPath() + QStringLiteral("/../data/store/curated.json"));
    candidates.append(QStringLiteral("/usr/share/linux-app-store/curated.json"));
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

QString ApplicationStore::getActionState(const QString &appKey, const QString &selectedSource) const
{
    return appActionStateToString(actionState(appKey, selectedSource));
}

QVariantMap ApplicationStore::getCandidateOffer(const QString &appKey) const
{
    auto cand = candidateOffer(appKey);
    if (cand) {
        return cand->toJson().toVariantMap();
    }
    return {};
}

QVariantList ApplicationStore::getAllOffers(const QString &appKey) const
{
    QVariantList list;
    const auto offers = allOffers(appKey);
    for (const auto &offer : offers) {
        if (!offer.available) continue; // Section 3.2: "Es listet ausschließlich Quellen mit tatsächlich auflösbarem Angebot."
        QVariantMap map = offer.toJson().toVariantMap();
        map[QStringLiteral("source")] = offer.source();
        map[QStringLiteral("sourceRank")] = offer.sourceRank();
        map[QStringLiteral("priority")] = offer.priority;
        map[QStringLiteral("isCandidate")] = offer.isCandidate;
        map[QStringLiteral("available")] = offer.available;
        map[QStringLiteral("downloadSize")] = offer.downloadSize.value_or(0);
        map[QStringLiteral("installedSize")] = offer.installedSize.value_or(0);

        QString backend;
        QString repoId;
        QString name;
        QString version;
        QVariantList pkgsList;
        for (const auto &ref : offer.packages) {
            pkgsList.append(ref.toMap());
        }
        map[QStringLiteral("packages")] = pkgsList;

        if (!offer.packages.isEmpty()) {
            backend = offer.packages.first().backend;
            repoId = offer.packages.first().repoId;
            name = offer.packages.first().name;
            version = offer.packages.first().version;
        }
        map[QStringLiteral("backend")] = backend;
        map[QStringLiteral("repoId")] = repoId;
        map[QStringLiteral("name")] = name;
        map[QStringLiteral("version")] = version;

        QString srcLabel;
        if (offer.source() == QLatin1String("flatpak")) {
            if (repoId.compare(QLatin1String("flathub"), Qt::CaseInsensitive) == 0) {
                srcLabel = QStringLiteral("Flathub");
            } else if (!repoId.isEmpty()) {
                srcLabel = repoId;
            } else {
                srcLabel = QStringLiteral("Flatpak");
            }
        } else if (offer.source() == QLatin1String("snap")) {
            srcLabel = QStringLiteral("Snap Store");
        } else {
            srcLabel = !repoId.isEmpty() ? repoId : QStringLiteral("Nativ");
        }
        map[QStringLiteral("sourceLabel")] = srcLabel;

        bool isInst = isSourceInstalled(appKey, offer.source());
        map[QStringLiteral("isInstalled")] = isInst;
        map[QStringLiteral("displayText")] = isInst ? QStringLiteral("%1 (Installiert)").arg(srcLabel) : srcLabel;

        list.append(map);
    }
    return list;
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

void ApplicationStore::notifyState() {
    ++m_revision;
    emit catalogLoaded();
}

void ApplicationStore::updateTransactionStatus(bool installSupported, bool removeSupported, bool busy, bool hasPlan, bool error) {
    m_installSupported = installSupported;
    m_removeSupported = removeSupported;
    m_transactionBusy = busy;
    m_transactionPlan = hasPlan;
    if (!m_operationPackages.isEmpty() && m_operationState != AppActionState::Reconciling) {
        if (error) m_operationState = AppActionState::ErrorOrCancelled;
        else if (hasPlan) m_operationState = AppActionState::AwaitingConfirmation;
        else if (!busy && m_operationState == AppActionState::AwaitingConfirmation) m_operationPackages.clear();
    }
    notifyState();
}

void ApplicationStore::transactionStarted() {
    if (!m_operationPackages.isEmpty()) m_operationState = AppActionState::Progressing;
    notifyState();
}

void ApplicationStore::transactionFinished(Result result) {
    m_operationFailed = result != Result::Success;
    if (!m_operationPackages.isEmpty()) m_operationState = AppActionState::Reconciling;
    notifyState();
    refreshInstalledState();
}

void ApplicationStore::requestInstall(const QString &appKey, int offerIndex)
{
    if (isLoading() || !m_installSupported || m_transactionBusy || m_transactionPlan) return;

    std::optional<PackageOffer> targetOffer;
    if (offerIndex >= 0) {
        const auto offers = allOffers(appKey);
        QList<PackageOffer> availableOffers;
        for (const auto &o : offers) {
            if (o.available) availableOffers.append(o);
        }
        if (offerIndex < availableOffers.size()) {
            targetOffer = availableOffers.at(offerIndex);
        }
    }
    if (!targetOffer.has_value()) {
        targetOffer = candidateOffer(appKey);
    }
    if (!targetOffer || !targetOffer->available) return;

    QList<PackageRef> targetRefs = targetOffer->packages;
    QStringList pkgNames;
    QString repoId;

    if (!targetRefs.isEmpty()) {
        repoId = targetRefs.first().repoId;
        const QString backend = targetRefs.first().backend;
        // For native packages, also make sure full packageSet is included
        if (backend != QLatin1String("flatpak") && backend != QLatin1String("snap")) {
            const QStringList names = packageSet(appKey);
            for (const QString &name : names) {
                bool found = false;
                for (const auto &ref : targetRefs) {
                    if (ref.name == name) { found = true; break; }
                }
                if (!found) {
                    targetRefs.append(PackageRef{backend, repoId, name, QString(), QString()});
                }
            }
        }
    }

    for (const PackageRef &ref : targetRefs) {
        if (!ref.name.isEmpty() && !pkgNames.contains(ref.name)) {
            pkgNames.append(ref.name);
        }
    }

    if (!pkgNames.isEmpty()) {
        m_operationPackages = pkgNames;
        m_operationState = AppActionState::PreparingPlan;
        notifyState();
        emit installRequested(pkgNames, repoId);
        emit installPackageRefsRequested(targetRefs);
        emit appStateChanged(appKey);
    }
}

void ApplicationStore::requestRemove(const QString &appKey, const QString &source)
{
    if (isLoading() || !m_removeSupported || m_transactionBusy || m_transactionPlan) return;

    const InstalledState inst = installedState(appKey);
    QList<PackageRef> targetRefs;
    QStringList pkgNames;

    const QString s = source.toLower();
    for (const PackageRef &ref : inst.installedPackages) {
        if (ref.name.isEmpty()) continue;
        const QString b = ref.backend.toLower();
        bool matches = false;
        if (s.isEmpty()) {
            matches = true;
        } else if (s == QLatin1String("flatpak")) {
            matches = (b == QLatin1String("flatpak"));
        } else if (s == QLatin1String("snap")) {
            matches = (b == QLatin1String("snap"));
        } else if (s == QLatin1String("native") || s == QLatin1String("alpm") || s == QLatin1String("dnf5") || s == QLatin1String("apt")) {
            matches = (b.isEmpty() || b == QLatin1String("alpm") || b == QLatin1String("dnf5") || b == QLatin1String("apt"));
        } else {
            matches = (b == s || ref.repoId.compare(s, Qt::CaseInsensitive) == 0);
        }

        if (matches) {
            targetRefs.append(ref);
            if (!pkgNames.contains(ref.name)) {
                pkgNames.append(ref.name);
            }
        }
    }

    // Fallback: if targetRefs is empty (e.g. source was not specified or matched), remove native package set
    if (targetRefs.isEmpty() && source.isEmpty()) {
        for (const PackageRef &ref : inst.installedPackages) {
            targetRefs.append(ref);
            if (!pkgNames.contains(ref.name)) {
                pkgNames.append(ref.name);
            }
        }
    }

    if (!pkgNames.isEmpty()) {
        m_operationPackages = pkgNames;
        m_operationState = AppActionState::PreparingPlan;
        notifyState();
        emit removeRequested(pkgNames);
        emit removePackageRefsRequested(targetRefs);
        emit appStateChanged(appKey);
    }
}

bool ApplicationStore::launchApp(const QString &appKey, const QString &source)
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

    QString expectedSource = QStringLiteral("native");
    if (source == QLatin1String("flatpak")) {
        expectedSource = QStringLiteral("flatpak");
    } else if (source.isEmpty()) {
        for (const auto &ref : inst.installedPackages) {
            if (ref.backend == QLatin1String("flatpak")) {
                expectedSource = QStringLiteral("flatpak");
                break;
            }
        }
    }

    QString errorMsg;
    bool ok = m_launcher.launchDesktopId(desktopId, &errorMsg, expectedSource);
    if (ok) {
        emit appLaunched(appKey);
    } else {
        emit appLaunchFailed(appKey, errorMsg);
    }
    return ok;
}

} // namespace lut
