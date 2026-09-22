#include "linux-update-tool/catalog/CatalogService.h"

#include <AppStreamQt/pool.h>
#include <AppStreamQt/component.h>
#include <AppStreamQt/icon.h>
#include <AppStreamQt/screenshot.h>
#include <AppStreamQt/image.h>
#include <AppStreamQt/launchable.h>
#include <AppStreamQt/developer.h>

#include <QRegularExpression>
#include <QLocale>
#include <QTextDocument>
#include <QDir>
#include <unistd.h>

namespace lut {

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

CatalogService::~CatalogService() = default;

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

    // Strictly exclude Flatpak metadata (FlagLoadFlatpak is omitted)
    AppStream::Pool::Flags flags;
    if (m_loadStdLocations) {
        flags.setFlag(AppStream::Pool::FlagLoadOsCatalog);
        flags.setFlag(AppStream::Pool::FlagLoadOsMetainfo);
        flags.setFlag(AppStream::Pool::FlagLoadOsDesktopFiles);
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
    m_loading = true;
    emit loadingChanged(true);

    m_pool->clear();
    m_pool->setLoadStdDataLocations(m_loadStdLocations);

    // Strictly exclude Flatpak metadata
    AppStream::Pool::Flags flags;
    if (m_loadStdLocations) {
        flags.setFlag(AppStream::Pool::FlagLoadOsCatalog);
        flags.setFlag(AppStream::Pool::FlagLoadOsMetainfo);
        flags.setFlag(AppStream::Pool::FlagLoadOsDesktopFiles);
    } else {
        QString testCache = QDir::tempPath() + QStringLiteral("/lut-appstream-cache-") + QString::number(getpid());
        QDir().mkpath(testCache);
        m_pool->overrideCacheLocations(testCache, testCache);
    }
    m_pool->setFlags(flags);

    for (const QString &loc : m_extraLocations) {
        m_pool->addExtraDataLocation(loc, AppStream::Metadata::FormatStyleCatalog);
    }

    m_pool->loadAsync();
}

void CatalogService::reload()
{
    m_generation++;
    load();
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
    if (u.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0 ||
        u.scheme().compare(QLatin1String("http"), Qt::CaseInsensitive) == 0) {
        return !u.host().isEmpty() && !url.contains(QLatin1Char('\n')) && !url.contains(QLatin1Char('\r'));
    }
    if (u.scheme().compare(QLatin1String("file"), Qt::CaseInsensitive) == 0) {
        const QString path = u.toLocalFile();
        return path.startsWith(QLatin1String("/usr/share/")) ||
               path.startsWith(QLatin1String("/var/cache/")) ||
               path.startsWith(QLatin1String("/tmp/"));
    }
    if (url.startsWith(QLatin1Char('/'))) {
        return url.startsWith(QLatin1String("/usr/share/")) ||
               url.startsWith(QLatin1String("/var/cache/")) ||
               url.startsWith(QLatin1String("/tmp/"));
    }
    return false;
}

AppRecord CatalogService::componentToAppRecord(const AppStream::Component &comp, const QString &preferredLocale)
{
    // Strict isolation: discard Flatpak origin or Flatpak bundles
    if (comp.origin().compare(QLatin1String("flatpak"), Qt::CaseInsensitive) == 0 ||
        comp.origin().contains(QLatin1String("flathub"), Qt::CaseInsensitive)) {
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

    // Icons: prioritize stock, then cached, then url/filename
    const auto icons = comp.icons();
    for (const auto &ic : icons) {
        if (ic.kind() == AppStream::Icon::KindStock) {
            rec.iconSource = ic.name();
            break;
        } else if (rec.iconSource.isEmpty() && ic.url().isValid()) {
            const QString candidate = ic.url().isLocalFile() ? ic.url().toLocalFile() : ic.url().toString();
            if (isSafeMediaUrl(candidate)) {
                rec.iconSource = candidate;
            }
        } else if (rec.iconSource.isEmpty() && !ic.name().isEmpty()) {
            rec.iconSource = ic.name();
        }
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
    m_appsByPackage.clear();

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

        m_apps.append(rec);
        m_appsByKey.insert(rec.appKey, rec);
        if (!rec.defaultPackageName.isEmpty()) {
            m_appsByPackage.insert(rec.defaultPackageName, rec);
        }
    }
}

QList<AppRecord> CatalogService::allApps() const
{
    return m_apps;
}

std::optional<AppRecord> CatalogService::appByKey(const QString &appKey) const
{
    auto it = m_appsByKey.constFind(appKey);
    if (it != m_appsByKey.constEnd()) {
        return *it;
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

} // namespace lut
