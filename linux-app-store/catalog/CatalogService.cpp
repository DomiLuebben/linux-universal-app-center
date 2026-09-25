#include "linux-app-store/catalog/CatalogService.h"

#include <AppStreamQt/pool.h>
#include <AppStreamQt/component.h>
#include <AppStreamQt/icon.h>
#include <AppStreamQt/version.h>
#include <AppStreamQt/screenshot.h>
#include <AppStreamQt/image.h>
#include <AppStreamQt/launchable.h>
#include <AppStreamQt/developer.h>

#include <AppStreamQt/bundle.h>
#include <AppStreamQt/release.h>

#include <QRegularExpression>
#include <QLocale>
#include <QTextDocument>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QStandardPaths>
#include <QDirIterator>
#include <QSet>
#include <QSettings>
#include <unistd.h>

// Icon::filename() gibt es erst ab AppStreamQt 1.2. Debian 13 hat 1.0.5,
// Fedora 44 hat 1.1.3: dort liefert url() für Cache-/lokale Symbole den Dateipfad.
// Ohne diese Weiche baute der Store auf beiden seit 1.3.1 nicht mehr.
static QString iconFilename(const AppStream::Icon &icon) {
#if ASQ_CHECK_VERSION(1, 2, 0)
    return icon.filename();
#else
    return icon.url().isLocalFile() ? icon.url().toLocalFile() : QString();
#endif
}

namespace lut {

static int appRecordSourceRank(const AppRecord &rec)
{
    if (rec.origin.compare(QLatin1String("flatpak"), Qt::CaseInsensitive) == 0 ||
        rec.origin.contains(QLatin1String("flathub"), Qt::CaseInsensitive)) {
        return 200;
    }
    if (rec.origin.compare(QLatin1String("snap"), Qt::CaseInsensitive) == 0) {
        return 100;
    }
    return 300;
}

CatalogService::CatalogService(QObject *parent)
    : QObject(parent)
    , m_pool(std::make_unique<AppStream::Pool>())
    , m_locale(QLocale::system().name())
{
    m_pool->setLocale(m_locale);
    connect(m_pool.get(), &AppStream::Pool::loadFinished, this, [this](bool success) {
        m_loading = false;
        m_loaded = success;
        emit loadingChanged(m_loading);
        emit loadedChanged(m_loaded);

        if (!success) {
            m_lastError = m_pool->lastError();
            emit errorOccurred(m_lastError);
        } else {
            processLoadedComponents();
        }
        emit loaded(success);
    });
}

CatalogService::~CatalogService() { m_loader.waitForDone(); }

void CatalogService::setLoadStdDataLocations(bool loadStd)
{
    m_loadStdLocations = loadStd;
}

void CatalogService::addExtraDataLocation(const QString &directory)
{
    if (!m_extraLocations.contains(directory)) {
        m_extraLocations.append(directory);
    }
}

void CatalogService::setLocale(const QString &locale)
{
    if (m_locale != locale) {
        m_locale = locale;
        m_pool->setLocale(locale);
        reload();
    }
}

QString CatalogService::locale() const
{
    return m_locale;
}

bool CatalogService::load()
{
    m_loading = true;
    emit loadingChanged(true);

    m_pool->clear();
    m_pool->setLoadStdDataLocations(m_loadStdLocations);

    // Load metadata flags including Flatpak when standard locations are requested and flatpak is available
    AppStream::Pool::Flags flags;
    if (m_loadStdLocations) {
        flags.setFlag(AppStream::Pool::FlagLoadOsCatalog);
        flags.setFlag(AppStream::Pool::FlagLoadOsMetainfo);
        flags.setFlag(AppStream::Pool::FlagLoadOsDesktopFiles);
        if (isFlatpakAvailable()) {
            flags.setFlag(AppStream::Pool::FlagLoadFlatpak);
        }
    } else {
        QString testCache = QDir::tempPath() + QStringLiteral("/lut-appstream-cache-") + QString::number(getpid());
        QDir().mkpath(testCache);
        m_pool->overrideCacheLocations(testCache, testCache);
    }
    m_pool->setFlags(flags);

    for (const QString &loc : m_extraLocations) {
        m_pool->addExtraDataLocation(loc, AppStream::Metadata::FormatStyleCatalog);
    }

    bool ok = m_pool->load();
    m_loading = false;
    m_loaded = ok;
    emit loadingChanged(false);
    emit loadedChanged(m_loaded);

    if (!ok) {
        m_lastError = m_pool->lastError();
        emit errorOccurred(m_lastError);
    } else {
        processLoadedComponents();
    }
    emit loaded(ok);
    return ok;
}

void CatalogService::loadAsync()
{
    if (m_loading) return;
    m_loading = true;
    m_lastError.clear();
    emit loadingChanged(true);
    const bool standard = m_loadStdLocations;
    const auto locations = m_extraLocations;
    const auto localeName = m_locale;
    m_loader.start([this, standard, locations, localeName] {
        // Keep the native pool and its lifetime in one worker. In particular, an
        // AppStream async callback must never outlive a destroyed Qt Pool wrapper.
        CatalogService reader;
        reader.setLoadStdDataLocations(standard);
        reader.setLocale(localeName);
        for (const auto &location : locations) reader.addExtraDataLocation(location);
        const bool ok = reader.load();
        QMetaObject::invokeMethod(this, [this, ok, apps = reader.m_apps,
                byKey = reader.m_appsByKey, normalized = reader.m_appsByNormalizedKey,
                byPackage = reader.m_appsByPackage, flatpakOffers = reader.m_flatpakOffersByNormalizedKey,
                error = reader.m_lastError] {
            m_loading = false;
            m_loaded = ok;
            m_lastError = error;
            if (ok) {
                m_apps = apps;
                m_appsByKey = byKey;
                m_appsByNormalizedKey = normalized;
                m_appsByPackage = byPackage;
                m_flatpakOffersByNormalizedKey = flatpakOffers;
            } else emit errorOccurred(error);
            emit loadingChanged(false);
            emit loadedChanged(ok);
            emit loaded(ok);
        }, Qt::QueuedConnection);
    });
}

void CatalogService::reload()
{
    m_generation++;
    loadAsync();
}

QString CatalogService::sanitizeDescription(const QString &raw)
{
    if (raw.isEmpty()) return QString();

    // 1. Remove script tags and contents
    static const QRegularExpression scriptRegex(QStringLiteral(R"(<script[^>]*>.*?</script>)"),
                                                QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QString clean = raw;
    clean.replace(scriptRegex, QString());

    // 2. Remove style tags and contents
    static const QRegularExpression styleRegex(QStringLiteral(R"(<style[^>]*>.*?</style>)"),
                                               QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    clean.replace(styleRegex, QString());

    // 3. Convert paragraphs / breaks to line feeds
    clean.replace(QRegularExpression(QStringLiteral(R"(<br\s*/?>)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("\n"));
    clean.replace(QRegularExpression(QStringLiteral(R"(</p>)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("\n\n"));
    clean.replace(QRegularExpression(QStringLiteral(R"(<li>)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral(" • "));
    clean.replace(QRegularExpression(QStringLiteral(R"(</li>)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("\n"));

    // 4. Strip remaining HTML tags
    static const QRegularExpression tagRegex(QStringLiteral(R"(<[^>]+>)"));
    clean.replace(tagRegex, QString());

    // 5. Decode HTML entities
    QTextDocument doc;
    doc.setHtml(clean);
    return doc.toPlainText().trimmed();
}

bool CatalogService::isSafeMediaUrl(const QString &url)
{
    if (url.trimmed().isEmpty()) return false;
    const QUrl u(url);
    if (u.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0) {
        return !u.host().isEmpty() && !url.contains(QLatin1Char('\n')) && !url.contains(QLatin1Char('\r'));
    }
    auto isAllowedLocalPath = [](const QString &path) {
        if (path.isEmpty()) return false;
        const QString userData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        return path.startsWith(QLatin1String("/usr/share/")) ||
               path.startsWith(QLatin1String("/var/cache/")) ||
               path.startsWith(QLatin1String("/tmp/")) ||
               path.startsWith(QLatin1String("/var/lib/flatpak/")) ||
               path.startsWith(QLatin1String("/var/lib/app-info/")) ||
               path.startsWith(QLatin1String("/var/lib/snapd/")) ||
               (!userData.isEmpty() && path.startsWith(userData));
    };
    if (u.scheme().compare(QLatin1String("file"), Qt::CaseInsensitive) == 0) {
        return isAllowedLocalPath(u.toLocalFile());
    }
    if (url.startsWith(QLatin1Char('/'))) {
        return isAllowedLocalPath(url);
    }
    return false;
}

namespace {

// QIcon::hasThemeIcon() durchsucht bei jedem Aufruf die Theme-Verzeichnisse
// (~2,4 ms). Beim Laden des Katalogs waren das über 3000 Aufrufe und rund
// 8 Sekunden. Stattdessen werden die vorhandenen Icon-Namen einmal je Prozess
// eingelesen: aktuelles Theme samt geerbter Themes, hicolor und pixmaps.
QSet<QString> buildThemeIconIndex()
{
    static const QStringList suffixes = {QStringLiteral("png"), QStringLiteral("svg"),
                                         QStringLiteral("svgz"), QStringLiteral("xpm")};
    const QStringList searchPaths = QIcon::themeSearchPaths();
    QStringList themes;
    QStringList queue{QIcon::themeName(), QIcon::fallbackThemeName(), QStringLiteral("hicolor")};
    while (!queue.isEmpty()) {
        const QString theme = queue.takeFirst();
        if (theme.isEmpty() || themes.contains(theme)) continue;
        themes.append(theme);
        for (const QString &base : searchPaths) {
            const QString indexFile = base + QLatin1Char('/') + theme + QStringLiteral("/index.theme");
            if (!QFile::exists(indexFile)) continue;
            QSettings index(indexFile, QSettings::IniFormat);
            const QVariant inherits = index.value(QStringLiteral("Icon Theme/Inherits"));
            queue.append(inherits.typeId() == QMetaType::QStringList ? inherits.toStringList()
                                                                     : inherits.toString().split(QLatin1Char(','), Qt::SkipEmptyParts));
            break;
        }
    }

    QSet<QString> names;
    const auto addFile = [&names](const QFileInfo &info) {
        if (suffixes.contains(info.suffix(), Qt::CaseInsensitive)) names.insert(info.completeBaseName());
    };
    for (const QString &theme : std::as_const(themes)) {
        for (const QString &base : searchPaths) {
            const QString dir = base + QLatin1Char('/') + theme;
            if (!QFileInfo(dir).isDir()) continue;
            QDirIterator it(dir, QDir::Files | QDir::System, QDirIterator::Subdirectories);
            while (it.hasNext()) { it.next(); addFile(it.fileInfo()); }
        }
    }
    for (const QString &dir : QIcon::fallbackSearchPaths()) {
        QDirIterator it(dir, QDir::Files | QDir::System);
        while (it.hasNext()) { it.next(); addFile(it.fileInfo()); }
    }
    return names;
}

bool themeIconExists(const QString &name)
{
    static const QSet<QString> index = buildThemeIconIndex();
    return index.contains(name);
}

} // namespace

AppRecord CatalogService::componentToAppRecord(const AppStream::Component &comp, const QString &preferredLocale)
{
    // AppImage remains strictly excluded
    if (comp.bundle(AppStream::Bundle::KindAppImage).kind() == AppStream::Bundle::KindAppImage ||
        comp.origin().contains(QLatin1String("appimage"), Qt::CaseInsensitive)) {
        return {};
    }

    AppRecord rec;
    rec.appKey = comp.id();
    rec.componentId = comp.id();

    // Name and fallback
    rec.name = comp.name();
    if (rec.name.isEmpty()) {
        rec.name = comp.id();
    }

    rec.summary = comp.summary();
    rec.description = sanitizeDescription(comp.description());

    rec.developer = comp.developer().name();
    rec.license = comp.projectLicense();
    rec.urlHomepage = comp.url(AppStream::Component::UrlKindHomepage).toString();
    rec.urlBugtracker = comp.url(AppStream::Component::UrlKindBugtracker).toString();
    rec.origin = comp.origin();
    if (rec.origin.isEmpty()) {
        if (comp.bundle(AppStream::Bundle::KindFlatpak).kind() == AppStream::Bundle::KindFlatpak) {
            rec.origin = QStringLiteral("flatpak");
        } else if (comp.bundle(AppStream::Bundle::KindSnap).kind() == AppStream::Bundle::KindSnap) {
            rec.origin = QStringLiteral("snap");
        }
    }

    // Icons: prioritize existing local cached file (128 > 64 > other),
    // then remote URL (MediaCache background fetch),
    // then Flathub CDN constructed URL (for Flathub/cached/network apps),
    // then existing desktop theme icon (QIcon::hasThemeIcon),
    // then fallback stock icon name.
    const auto icons = comp.icons();
    QString cachedFile128;
    QString cachedFile64;
    QString cachedFileAny;
    QString remoteUrl128;
    QString remoteUrlAny;
    QString cachedFilename128;
    QString cachedFilename64;
    QString cachedFilenameAny;
    QString themeStockIcon;
    QString fallbackStock;

    for (const auto &ic : icons) {
        // 1. Check for cached/local file on disk
        QString localPath;
        if (ic.url().isValid() && ic.url().isLocalFile()) {
            localPath = ic.url().toLocalFile();
        } else if (!iconFilename(ic).isEmpty() && (iconFilename(ic).startsWith(QLatin1Char('/')) || iconFilename(ic).startsWith(QLatin1String("file://")))) {
            localPath = iconFilename(ic).startsWith(QLatin1String("file://")) ? QUrl(iconFilename(ic)).toLocalFile() : iconFilename(ic);
        }
        if (!localPath.isEmpty() && isSafeMediaUrl(localPath) && QFile::exists(localPath)) {
            uint w = ic.width();
            uint h = ic.height();
            if (w == 128 || h == 128) {
                cachedFile128 = localPath;
            } else if (w == 64 || h == 64) {
                cachedFile64 = localPath;
            } else if (cachedFileAny.isEmpty()) {
                cachedFileAny = localPath;
            }
            continue;
        }

        // 2. Check for remote HTTPS url
        if (ic.kind() == AppStream::Icon::KindRemote || (ic.url().isValid() && ic.url().scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0)) {
            QString rUrl = ic.url().toString();
            if (isSafeMediaUrl(rUrl)) {
                uint w = ic.width();
                uint h = ic.height();
                if (w == 128 || h == 128) {
                    remoteUrl128 = rUrl;
                } else if (remoteUrlAny.isEmpty()) {
                    remoteUrlAny = rUrl;
                }
            }
            continue;
        }

        // 3. Keep track of cached icon filename (even if not yet downloaded to disk)
        QString fname;
        if (!iconFilename(ic).isEmpty()) {
            fname = QFileInfo(iconFilename(ic)).fileName();
        } else if (ic.kind() == AppStream::Icon::KindCached || !ic.name().isEmpty()) {
            if (ic.name().endsWith(QLatin1String(".png"), Qt::CaseInsensitive) ||
                ic.name().endsWith(QLatin1String(".svg"), Qt::CaseInsensitive) ||
                ic.name().endsWith(QLatin1String(".webp"), Qt::CaseInsensitive)) {
                fname = ic.name();
            }
        }
        if (!fname.isEmpty()) {
            uint w = ic.width();
            uint h = ic.height();
            if (w == 128 || h == 128) {
                cachedFilename128 = fname;
            } else if (w == 64 || h == 64) {
                cachedFilename64 = fname;
            } else if (cachedFilenameAny.isEmpty()) {
                cachedFilenameAny = fname;
            }
        }

        // 4. Check for theme stock icon
        if (!ic.name().isEmpty()) {
            if (themeIconExists(ic.name())) {
                if (themeStockIcon.isEmpty()) {
                    themeStockIcon = ic.name();
                }
            } else if (fallbackStock.isEmpty()) {
                fallbackStock = ic.name();
            }
        }
    }

    QString flathubConstructedUrl;
    QString targetFilename = !cachedFilename128.isEmpty() ? cachedFilename128
                           : (!cachedFilename64.isEmpty() ? cachedFilename64 : cachedFilenameAny);
    if (!targetFilename.isEmpty()) {
        flathubConstructedUrl = QStringLiteral("https://dl.flathub.org/repo/appstream/x86_64/icons/128x128/") + targetFilename;
    } else if (!icons.isEmpty()) {
        bool isFlatpak = (comp.bundle(AppStream::Bundle::KindFlatpak).kind() == AppStream::Bundle::KindFlatpak ||
                          rec.origin.contains(QLatin1String("flatpak"), Qt::CaseInsensitive) ||
                          rec.origin.contains(QLatin1String("flathub"), Qt::CaseInsensitive));
        QString cleanId = comp.id();
        if (cleanId.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) {
            cleanId.chop(8);
        }
        if (isFlatpak) {
            flathubConstructedUrl = QStringLiteral("https://dl.flathub.org/repo/appstream/x86_64/icons/128x128/") + cleanId + QStringLiteral(".png");
        } else if (cleanId.count(QLatin1Char('.')) >= 2 && themeStockIcon.isEmpty()) {
            flathubConstructedUrl = QStringLiteral("https://dl.flathub.org/repo/appstream/x86_64/icons/128x128/") + cleanId + QStringLiteral(".png");
        }
    }

    if (!cachedFile128.isEmpty()) {
        rec.iconSource = cachedFile128;
    } else if (!cachedFile64.isEmpty()) {
        rec.iconSource = cachedFile64;
    } else if (!cachedFileAny.isEmpty()) {
        rec.iconSource = cachedFileAny;
    } else if (!remoteUrl128.isEmpty()) {
        rec.iconSource = remoteUrl128;
    } else if (!remoteUrlAny.isEmpty()) {
        rec.iconSource = remoteUrlAny;
    } else if (!flathubConstructedUrl.isEmpty()) {
        rec.iconSource = flathubConstructedUrl;
    } else if (!themeStockIcon.isEmpty()) {
        rec.iconSource = themeStockIcon;
    } else if (!fallbackStock.isEmpty()) {
        rec.iconSource = fallbackStock;
    }

    // Screenshots (UI-06: strictly enforce allowed safe URL schemes)
    const auto screenshots = comp.screenshotsAll();
    for (const auto &scr : screenshots) {
        for (const auto &img : scr.images()) {
            if (img.url().isValid()) {
                const QString imgUrl = img.url().toString();
                if (isSafeMediaUrl(imgUrl)) {
                    rec.screenshots.append(imgUrl);
                }
            }
        }
    }

    rec.categories = comp.categories();
    rec.keywords = comp.searchTokens();

    // Launchable desktop IDs
    auto launchable = comp.launchable(AppStream::Launchable::KindDesktopId);
    rec.launchableDesktopIds = launchable.entries();

    // Package names
    const QStringList pkgNames = comp.packageNames();
    rec.packageNames = pkgNames;
    if (!pkgNames.isEmpty()) {
        rec.defaultPackageName = pkgNames.first();
    }

    Q_UNUSED(preferredLocale);
    return rec;
}

void CatalogService::processLoadedComponents()
{
    m_apps.clear();
    m_appsByKey.clear();
    m_appsByNormalizedKey.clear();
    m_appsByPackage.clear();
    m_flatpakOffersByNormalizedKey.clear();

    const auto components = m_pool->components();
    for (const auto &comp : components) {
        // Only accept relevant components (desktop apps, console apps, or generic with desktop/package info)
        auto kind = comp.kind();
        if (kind != AppStream::Component::KindDesktopApp &&
            kind != AppStream::Component::KindConsoleApp &&
            kind != AppStream::Component::KindGeneric) {
            continue;
        }

        AppRecord rec = componentToAppRecord(comp, m_locale);
        if (rec.appKey.isEmpty()) {
            continue;
        }

        const QString normKey = normalizedAppKey(rec.appKey);

        // Check if this component provides Flatpak bundle / origin
        bool isFlatpak = (rec.origin.compare(QLatin1String("flatpak"), Qt::CaseInsensitive) == 0 ||
                          rec.origin.contains(QLatin1String("flathub"), Qt::CaseInsensitive) ||
                          comp.bundle(AppStream::Bundle::KindFlatpak).kind() == AppStream::Bundle::KindFlatpak);

        if (isFlatpak) {
            PackageRef pref;
            pref.backend = QStringLiteral("flatpak");
            pref.name = comp.id();
            pref.repoId = rec.origin.isEmpty() ? QStringLiteral("flathub") : rec.origin;
            pref.arch = QStringLiteral("x86_64");

            QString ver;
            auto relEntries = comp.releasesPlain().entries();
            if (!relEntries.isEmpty()) {
                ver = relEntries.first().version();
            }
            if (ver.isEmpty()) {
                auto b = comp.bundle(AppStream::Bundle::KindFlatpak);
                if (!b.isEmpty()) {
                    QStringList parts = b.id().split(QLatin1Char('/'));
                    if (parts.size() >= 4) {
                        ver = parts.at(3);
                    }
                }
            }
            if (ver.isEmpty()) {
                ver = QStringLiteral("stable");
            }
            pref.version = ver;

            PackageOffer offer;
            offer.available = true;
            offer.isCandidate = true;
            offer.packages = {pref};

            bool exists = false;
            auto &existingOffers = m_flatpakOffersByNormalizedKey[normKey];
            for (const auto &eo : existingOffers) {
                if (eo.packages.first().name == pref.name && eo.packages.first().repoId == pref.repoId) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                existingOffers.append(offer);
            }
        }

        // Merge or insert AppRecord by normalizedAppKey() (Section 5.2)
        if (!m_appsByNormalizedKey.contains(normKey)) {
            m_apps.append(rec);
            m_appsByKey.insert(rec.appKey, rec);
            m_appsByNormalizedKey.insert(normKey, rec);
            for (const auto &pkg : rec.packageNames) {
                if (!pkg.isEmpty()) {
                    m_appsByPackage.insert(pkg, rec);
                }
            }
            if (!rec.defaultPackageName.isEmpty() && !rec.packageNames.contains(rec.defaultPackageName)) {
                m_appsByPackage.insert(rec.defaultPackageName, rec);
            }
        } else {
            // Already present: merge into single AppRecord!
            // Identity (name, icon, summary, description, developer, license, screenshots, urls)
            // is preferred from native source (rank 300), fallback to highest available.
            AppRecord existing = m_appsByNormalizedKey.value(normKey);
            int existingRank = appRecordSourceRank(existing);
            int newRank = appRecordSourceRank(rec);

            auto iconQuality = [](const QString &src) -> int {
                if (src.isEmpty()) return 0;
                if (src.startsWith(QLatin1Char('/')) || src.startsWith(QLatin1String("file://"))) {
                    QString p = src.startsWith(QLatin1String("file://")) ? QUrl(src).toLocalFile() : src;
                    if (QFile::exists(p)) return 4;
                }
                if (src.startsWith(QLatin1String("https://"))) return 3;
                if (themeIconExists(src)) return 2;
                return 1;
            };

            AppRecord merged;
            if (newRank > existingRank) {
                merged = rec;
                if (merged.packageNames.isEmpty() && !existing.packageNames.isEmpty()) {
                    merged.packageNames = existing.packageNames;
                    merged.defaultPackageName = existing.defaultPackageName;
                }
                if (iconQuality(existing.iconSource) > iconQuality(merged.iconSource)) {
                    merged.iconSource = existing.iconSource;
                }
                if (merged.screenshots.isEmpty() && !existing.screenshots.isEmpty()) {
                    merged.screenshots = existing.screenshots;
                }
            } else {
                merged = existing;
                if (merged.description.isEmpty() && !rec.description.isEmpty()) merged.description = rec.description;
                if (iconQuality(rec.iconSource) > iconQuality(merged.iconSource)) {
                    merged.iconSource = rec.iconSource;
                }
                if (merged.screenshots.isEmpty() && !rec.screenshots.isEmpty()) merged.screenshots = rec.screenshots;
                if (merged.developer.isEmpty() && !rec.developer.isEmpty()) merged.developer = rec.developer;
                if (merged.license.isEmpty() && !rec.license.isEmpty()) merged.license = rec.license;
                if (merged.urlHomepage.isEmpty() && !rec.urlHomepage.isEmpty()) merged.urlHomepage = rec.urlHomepage;
                if (merged.urlBugtracker.isEmpty() && !rec.urlBugtracker.isEmpty()) merged.urlBugtracker = rec.urlBugtracker;
                if (merged.packageNames.isEmpty() && !rec.packageNames.isEmpty()) {
                    merged.packageNames = rec.packageNames;
                    merged.defaultPackageName = rec.defaultPackageName;
                }
            }

            for (const auto &cat : rec.categories) {
                if (!merged.categories.contains(cat)) merged.categories.append(cat);
            }
            for (const auto &kw : rec.keywords) {
                if (!merged.keywords.contains(kw)) merged.keywords.append(kw);
            }
            for (const auto &l : rec.launchableDesktopIds) {
                if (!merged.launchableDesktopIds.contains(l)) merged.launchableDesktopIds.append(l);
            }

            for (int i = 0; i < m_apps.size(); ++i) {
                if (normalizedAppKey(m_apps[i].appKey) == normKey) {
                    m_apps[i] = merged;
                    break;
                }
            }
            m_appsByNormalizedKey.insert(normKey, merged);
            m_appsByKey.insert(rec.appKey, merged);
            m_appsByKey.insert(existing.appKey, merged);
            m_appsByKey.insert(merged.appKey, merged);

            for (const auto &pkg : merged.packageNames) {
                if (!pkg.isEmpty() && !m_appsByPackage.contains(pkg, merged)) {
                    m_appsByPackage.insert(pkg, merged);
                }
            }
            if (!merged.defaultPackageName.isEmpty() && !m_appsByPackage.contains(merged.defaultPackageName, merged)) {
                m_appsByPackage.insert(merged.defaultPackageName, merged);
            }
        }
    }
}

static std::optional<bool> s_flatpakAvailableOverride;

bool CatalogService::isFlatpakAvailable()
{
    if (s_flatpakAvailableOverride.has_value()) {
        return *s_flatpakAvailableOverride;
    }
    return QFile::exists(QStringLiteral("/usr/bin/flatpak"));
}

void CatalogService::setFlatpakAvailableOverride(std::optional<bool> override)
{
    s_flatpakAvailableOverride = override;
}

QList<PackageOffer> CatalogService::flatpakOffersForApp(const QString &appKey) const
{
    if (!isFlatpakAvailable()) {
        return {};
    }
    const QString norm = normalizedAppKey(appKey);
    return m_flatpakOffersByNormalizedKey.value(norm);
}

QHash<QString, QList<PackageOffer>> CatalogService::allFlatpakOffers() const
{
    if (!isFlatpakAvailable()) {
        return {};
    }
    return m_flatpakOffersByNormalizedKey;
}

QList<AppRecord> CatalogService::allApps() const
{
    return m_apps;
}

QString CatalogService::normalizedAppKey(const QString &appKey)
{
    // Distributionen führen dieselbe Anwendung mit und ohne ".desktop":
    // Arch liefert "org.kde.kate.desktop", andere Kataloge "org.kde.kate".
    // Für den Abgleich zählt die Kennung ohne diesen Zusatz.
    if (appKey.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) {
        return appKey.left(appKey.size() - 8);
    }
    return appKey;
}

std::optional<AppRecord> CatalogService::appByKey(const QString &appKey) const
{
    auto it = m_appsByKey.constFind(appKey);
    if (it != m_appsByKey.constEnd()) {
        return *it;
    }

    const auto normalized = m_appsByNormalizedKey.constFind(normalizedAppKey(appKey));
    if (normalized != m_appsByNormalizedKey.constEnd()) {
        return *normalized;
    }
    return std::nullopt;
}

std::optional<AppRecord> CatalogService::appByPackageName(const QString &packageName) const
{
    auto it = m_appsByPackage.constFind(packageName);
    if (it != m_appsByPackage.constEnd()) {
        return *it;
    }
    return std::nullopt;
}

QList<AppRecord> CatalogService::appsByPackageName(const QString &packageName) const
{
    return m_appsByPackage.values(packageName);
}

QList<AppRecord> CatalogService::appsByCategory(const QString &category) const
{
    QList<AppRecord> result;
    const QString cleanCat = category.trimmed();
    for (const auto &app : m_apps) {
        for (const auto &c : app.categories) {
            if (c.compare(cleanCat, Qt::CaseInsensitive) == 0) {
                result.append(app);
                break;
            }
        }
    }
    return result;
}

QList<AppRecord> CatalogService::search(const QString &query) const
{
    if (query.trimmed().isEmpty()) {
        return m_apps;
    }

    QList<AppRecord> result;
    const QString term = query.trimmed();

    for (const auto &app : m_apps) {
        if (app.name.contains(term, Qt::CaseInsensitive) ||
            app.summary.contains(term, Qt::CaseInsensitive) ||
            app.defaultPackageName.contains(term, Qt::CaseInsensitive) ||
            app.appKey.contains(term, Qt::CaseInsensitive) ||
            app.keywords.contains(term, Qt::CaseInsensitive) ||
            app.categories.contains(term, Qt::CaseInsensitive) ||
            app.description.contains(term, Qt::CaseInsensitive)) {
            result.append(app);
        }
    }
    return result;
}

void CatalogService::searchAsync(const QString &query, quint64 requestId)
{
    QList<AppRecord> results = search(query);
    emit searchCompleted(requestId, results);
}

void CatalogService::setPackageNamesForApp(const QString &appKey, const QStringList &packageNames)
{
    if (packageNames.isEmpty() || appKey.isEmpty()) return;

    auto updateRecord = [&](AppRecord &rec) {
        rec.packageNames = packageNames;
        if (rec.defaultPackageName.isEmpty()) {
            rec.defaultPackageName = packageNames.first();
        }
    };

    auto it = m_appsByKey.find(appKey);
    if (it != m_appsByKey.end()) {
        updateRecord(it.value());
        for (const auto &pkg : packageNames) {
            if (!pkg.isEmpty()) {
                m_appsByPackage.insert(pkg, it.value());
            }
        }
    }

    const QString normKey = normalizedAppKey(appKey);
    auto itNorm = m_appsByNormalizedKey.find(normKey);
    if (itNorm != m_appsByNormalizedKey.end()) {
        updateRecord(itNorm.value());
        for (const auto &pkg : packageNames) {
            if (!pkg.isEmpty()) {
                m_appsByPackage.insert(pkg, itNorm.value());
            }
        }
    }

    for (auto &app : m_apps) {
        if (app.appKey == appKey || normalizedAppKey(app.appKey) == normKey) {
            updateRecord(app);
            break;
        }
    }
}

} // namespace lut
