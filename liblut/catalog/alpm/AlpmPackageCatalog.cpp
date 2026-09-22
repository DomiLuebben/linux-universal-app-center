#include "liblut/catalog/alpm/AlpmPackageCatalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QDateTime>
#include <sys/utsname.h>

#ifdef HAVE_ALPM
#include <alpm.h>
#endif

namespace lut {

AlpmPackageCatalog::AlpmPackageCatalog(const QString &rootPath,
                                       const QString &dbPath,
                                       const QString &configPath)
    : m_rootPath(rootPath)
    , m_dbPath(dbPath)
    , m_configPath(configPath)
{
}

AlpmPackageCatalog::~AlpmPackageCatalog()
{
    QMutexLocker locker(&m_mutex);
    releaseHandleLocked();
}

quint64 AlpmPackageCatalog::catalogGeneration() const
{
    QMutexLocker locker(&m_mutex);
    return m_generation;
}

void AlpmPackageCatalog::reload()
{
    QMutexLocker locker(&m_mutex);
    releaseHandleLocked();
    m_generation++;
}

QStringList AlpmPackageCatalog::configuredRepositories() const
{
    QMutexLocker locker(&m_mutex);
    ensureInitializedLocked();
    return m_repositories;
}

QStringList AlpmPackageCatalog::configuredArchitectures() const
{
    QMutexLocker locker(&m_mutex);
    ensureInitializedLocked();
    return m_architectures;
}

void AlpmPackageCatalog::parseConfigurationLocked() const
{
    m_repositories.clear();
    m_architectures.clear();

    QString hostArch = QStringLiteral("x86_64");
    struct utsname uts;
    if (uname(&uts) == 0) {
        hostArch = QString::fromUtf8(uts.machine);
    }

    // 1. Try pacman-conf CLI if config file exists
    if (QFile::exists(m_configPath)) {
        QProcess proc;
        proc.start(QStringLiteral("pacman-conf"), {QStringLiteral("--config"), m_configPath, QStringLiteral("--repo-list")});
        if (proc.waitForFinished(2000) && proc.exitCode() == 0) {
            const QString out = QString::fromUtf8(proc.readAllStandardOutput());
            for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                QString repo = line.trimmed();
                if (!repo.isEmpty() && !m_repositories.contains(repo)) {
                    m_repositories.append(repo);
                }
            }
        }

        QProcess archProc;
        archProc.start(QStringLiteral("pacman-conf"), {QStringLiteral("--config"), m_configPath, QStringLiteral("Architecture")});
        if (archProc.waitForFinished(2000) && archProc.exitCode() == 0) {
            const QString out = QString::fromUtf8(archProc.readAllStandardOutput());
            for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                QString arch = line.trimmed();
                if (arch == QLatin1String("auto")) arch = hostArch;
                if (!arch.isEmpty() && !m_architectures.contains(arch)) {
                    m_architectures.append(arch);
                }
            }
        }
    }

    // 2. Direct parsing of pacman.conf if pacman-conf failed or returned empty
    if (m_repositories.isEmpty() && QFile::exists(m_configPath)) {
        QFile file(m_configPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            static const QRegularExpression sectionRegex(QStringLiteral(R"(^\s*\[([a-zA-Z0-9_-]+)\])"));
            static const QRegularExpression archRegex(QStringLiteral(R"(^\s*Architecture\s*=\s*(.+)$)"), QRegularExpression::CaseInsensitiveOption);

            while (!file.atEnd()) {
                QString line = QString::fromUtf8(file.readLine()).trimmed();
                if (line.startsWith(QLatin1Char('#')) || line.isEmpty()) continue;

                auto archMatch = archRegex.match(line);
                if (archMatch.hasMatch()) {
                    for (const QString &arch : archMatch.captured(1).split(QRegularExpression(QStringLiteral(R"(\s+)")))) {
                        QString clean = arch.trimmed();
                        if (clean == QLatin1String("auto")) clean = hostArch;
                        if (!clean.isEmpty() && !m_architectures.contains(clean)) {
                            m_architectures.append(clean);
                        }
                    }
                    continue;
                }

                auto secMatch = sectionRegex.match(line);
                if (secMatch.hasMatch()) {
                    QString section = secMatch.captured(1);
                    if (section != QLatin1String("options") && !m_repositories.contains(section)) {
                        m_repositories.append(section);
                    }
                }
            }
        }
    }

    // 3. Fallback: discover from sync db directory
    if (m_repositories.isEmpty()) {
        QDir syncDir(m_dbPath + QStringLiteral("/sync"));
        if (syncDir.exists()) {
            const QStringList dbFiles = syncDir.entryList({QStringLiteral("*.db")}, QDir::Files);
            for (const QString &dbFile : dbFiles) {
                QString repo = dbFile.chopped(3); // strip .db
                if (!repo.isEmpty() && !m_repositories.contains(repo)) {
                    m_repositories.append(repo);
                }
            }
        }
    }

    if (m_architectures.isEmpty()) {
        m_architectures = {hostArch, QStringLiteral("any")};
    }
}

void AlpmPackageCatalog::ensureInitializedLocked() const
{
    if (m_handle) {
        return;
    }

    parseConfigurationLocked();

#ifdef HAVE_ALPM
    alpm_errno_t err = ALPM_ERR_OK;
    m_handle = alpm_initialize(m_rootPath.toUtf8().constData(),
                               m_dbPath.toUtf8().constData(),
                               &err);
    if (!m_handle) {
        return;
    }

    for (const QString &repo : m_repositories) {
        alpm_register_syncdb(m_handle, repo.toUtf8().constData(), ALPM_SIG_USE_DEFAULT);
    }
#endif
}

void AlpmPackageCatalog::releaseHandleLocked() const
{
#ifdef HAVE_ALPM
    if (m_handle) {
        alpm_release(m_handle);
        m_handle = nullptr;
    }
#endif
}

QList<PackageOffer> AlpmPackageCatalog::offersForPackage(const QString &packageName)
{
    QMutexLocker locker(&m_mutex);
    ensureInitializedLocked();

    QList<PackageOffer> offers;
#ifdef HAVE_ALPM
    if (!m_handle) return offers;

    int repoIndex = 0;
    bool candidateChosen = false;

    for (const QString &repoName : m_repositories) {
        alpm_db_t *matchedDb = nullptr;
        for (const alpm_list_t *d = alpm_get_syncdbs(m_handle); d; d = alpm_list_next(d)) {
            auto *sdb = static_cast<alpm_db_t *>(d->data);
            if (QString::fromUtf8(alpm_db_get_name(sdb)) == repoName) {
                matchedDb = sdb;
                break;
            }
        }

        if (!matchedDb) {
            repoIndex++;
            continue;
        }

        alpm_pkg_t *pkg = alpm_db_get_pkg(matchedDb, packageName.toUtf8().constData());
        if (pkg) {
            QString name = QString::fromUtf8(alpm_pkg_get_name(pkg));
            QString ver = QString::fromUtf8(alpm_pkg_get_version(pkg));
            QString arch = QString::fromUtf8(alpm_pkg_get_arch(pkg) ? alpm_pkg_get_arch(pkg) : "any");

            PackageRef ref;
            ref.backend = QStringLiteral("alpm");
            ref.repoId = repoName;
            ref.name = name;
            ref.arch = arch;
            ref.version = ver;

            PackageOffer offer;
            offer.packages = {ref};
            offer.downloadSize = alpm_pkg_get_size(pkg);
            offer.installedSize = alpm_pkg_get_isize(pkg);
            // Higher priority for earlier repos (e.g. CachyOS before extra/core)
            offer.priority = static_cast<int>(m_repositories.size() - repoIndex) * 10;

            bool archOk = (arch == QLatin1String("any") || m_architectures.contains(arch));

            if (!archOk) {
                offer.available = false;
                offer.isCandidate = false;
                offer.unavailabilityReason = QStringLiteral("Architektur '%1' wird vom System nicht unterstützt.").arg(arch);
            } else {
                offer.available = true;
                if (!candidateChosen) {
                    offer.isCandidate = true;
                    candidateChosen = true;
                } else {
                    offer.isCandidate = false;
                }
            }

            offers.append(offer);
        }
        repoIndex++;
    }
#else
    Q_UNUSED(packageName);
#endif
    return offers;
}

std::optional<PackageOffer> AlpmPackageCatalog::candidateOffer(const QString &packageName)
{
    const QList<PackageOffer> allOffers = offersForPackage(packageName);
    for (const PackageOffer &offer : allOffers) {
        if (offer.isCandidate && offer.available) {
            return offer;
        }
    }
    if (!allOffers.isEmpty() && allOffers.first().available) {
        return allOffers.first();
    }
    return std::nullopt;
}

InstalledState AlpmPackageCatalog::installedStateForPackage(const QString &packageName)
{
    QMutexLocker locker(&m_mutex);
    ensureInitializedLocked();

    InstalledState state;
    state.inventoryRevision = m_generation;

#ifdef HAVE_ALPM
    if (!m_handle) return state;

    alpm_db_t *localdb = alpm_get_localdb(m_handle);
    if (!localdb) return state;

    alpm_pkg_t *pkg = alpm_db_get_pkg(localdb, packageName.toUtf8().constData());
    if (pkg) {
        state.isFullyInstalled = true;
        state.origin = QStringLiteral("alpm");
        QString name = QString::fromUtf8(alpm_pkg_get_name(pkg));
        QString ver = QString::fromUtf8(alpm_pkg_get_version(pkg));
        QString arch = QString::fromUtf8(alpm_pkg_get_arch(pkg) ? alpm_pkg_get_arch(pkg) : "any");

        state.installedPackages = {PackageRef{
            QStringLiteral("alpm"),
            QStringLiteral("local"),
            name,
            arch,
            ver
        }};

        // Resolve installed desktop IDs from filelist
        alpm_filelist_t *files = alpm_pkg_get_files(pkg);
        if (files) {
            for (size_t i = 0; i < files->count; ++i) {
                const char *fn = files->files[i].name;
                if (!fn) continue;
                QString path = QString::fromUtf8(fn);
                if (path.endsWith(QLatin1String(".desktop")) &&
                    (path.contains(QLatin1String("applications/")) || path.startsWith(QLatin1String("usr/share/applications/")))) {
                    QString desktopId = QFileInfo(path).fileName();
                    if (!state.launchableDesktopIds.contains(desktopId)) {
                        state.launchableDesktopIds.append(desktopId);
                    }
                }
            }
        }
    }
#else
    Q_UNUSED(packageName);
#endif

    return state;
}

QList<InstalledPackage> AlpmPackageCatalog::allInstalledPackages()
{
    QMutexLocker locker(&m_mutex);
    ensureInitializedLocked();

    QList<InstalledPackage> result;
#ifdef HAVE_ALPM
    if (!m_handle) return result;

    alpm_db_t *localdb = alpm_get_localdb(m_handle);
    if (!localdb) return result;

    for (const alpm_list_t *p = alpm_db_get_pkgcache(localdb); p; p = alpm_list_next(p)) {
        auto *pkg = static_cast<alpm_pkg_t *>(p->data);
        QString name = QString::fromUtf8(alpm_pkg_get_name(pkg));
        QString ver = QString::fromUtf8(alpm_pkg_get_version(pkg));

        InstalledPackage ip;
        ip.name = name;
        ip.version = ver;
        ip.id = QStringLiteral("%1-%2").arg(name, ver);
        ip.description = QString::fromUtf8(alpm_pkg_get_desc(pkg) ? alpm_pkg_get_desc(pkg) : "");
        ip.installedSize = alpm_pkg_get_isize(pkg);
        ip.arch = QString::fromUtf8(alpm_pkg_get_arch(pkg) ? alpm_pkg_get_arch(pkg) : "any");
        result.append(ip);
    }
#endif
    return result;
}

QList<PackageRef> AlpmPackageCatalog::findPackagesProvidingFile(const QString &filePath)
{
    QMutexLocker locker(&m_mutex);
    ensureInitializedLocked();

    QList<PackageRef> result;
#ifdef HAVE_ALPM
    if (!m_handle) return result;

    QString cleanPath = filePath;
    if (cleanPath.startsWith(QLatin1Char('/'))) {
        cleanPath = cleanPath.mid(1);
    }
    const QByteArray searchBytes = cleanPath.toUtf8();

    alpm_db_t *localdb = alpm_get_localdb(m_handle);
    if (!localdb) return result;

    for (const alpm_list_t *p = alpm_db_get_pkgcache(localdb); p; p = alpm_list_next(p)) {
        auto *pkg = static_cast<alpm_pkg_t *>(p->data);
        alpm_filelist_t *fl = alpm_pkg_get_files(pkg);
        if (fl && alpm_filelist_contains(fl, searchBytes.constData())) {
            PackageRef ref;
            ref.backend = QStringLiteral("alpm");
            ref.repoId = QStringLiteral("local");
            ref.name = QString::fromUtf8(alpm_pkg_get_name(pkg));
            ref.arch = QString::fromUtf8(alpm_pkg_get_arch(pkg) ? alpm_pkg_get_arch(pkg) : "any");
            ref.version = QString::fromUtf8(alpm_pkg_get_version(pkg));
            result.append(ref);
        }
    }
#else
    Q_UNUSED(filePath);
#endif
    return result;
}

QList<PackageOffer> AlpmPackageCatalog::searchPackages(const QString &query)
{
    QMutexLocker locker(&m_mutex);
    ensureInitializedLocked();

    QList<PackageOffer> results;
#ifdef HAVE_ALPM
    if (!m_handle || query.trimmed().isEmpty()) return results;

    const QString cleanQuery = query.trimmed();
    int repoIndex = 0;

    for (const QString &repoName : m_repositories) {
        alpm_db_t *matchedDb = nullptr;
        for (const alpm_list_t *d = alpm_get_syncdbs(m_handle); d; d = alpm_list_next(d)) {
            auto *sdb = static_cast<alpm_db_t *>(d->data);
            if (QString::fromUtf8(alpm_db_get_name(sdb)) == repoName) {
                matchedDb = sdb;
                break;
            }
        }

        if (!matchedDb) {
            repoIndex++;
            continue;
        }

        for (const alpm_list_t *p = alpm_db_get_pkgcache(matchedDb); p; p = alpm_list_next(p)) {
            auto *pkg = static_cast<alpm_pkg_t *>(p->data);
            QString name = QString::fromUtf8(alpm_pkg_get_name(pkg));
            QString desc = QString::fromUtf8(alpm_pkg_get_desc(pkg) ? alpm_pkg_get_desc(pkg) : "");

            if (name.contains(cleanQuery, Qt::CaseInsensitive) || desc.contains(cleanQuery, Qt::CaseInsensitive)) {
                PackageRef ref;
                ref.backend = QStringLiteral("alpm");
                ref.repoId = repoName;
                ref.name = name;
                ref.arch = QString::fromUtf8(alpm_pkg_get_arch(pkg) ? alpm_pkg_get_arch(pkg) : "any");
                ref.version = QString::fromUtf8(alpm_pkg_get_version(pkg));

                PackageOffer offer;
                offer.packages = {ref};
                offer.downloadSize = alpm_pkg_get_size(pkg);
                offer.installedSize = alpm_pkg_get_isize(pkg);
                offer.priority = static_cast<int>(m_repositories.size() - repoIndex) * 10;
                offer.available = true;

                results.append(offer);
            }
        }
        repoIndex++;
    }
#else
    Q_UNUSED(query);
#endif
    return results;
}

int AlpmPackageCatalog::compareVersions(const QString &v1, const QString &v2) const
{
#ifdef HAVE_ALPM
    return alpm_pkg_vercmp(v1.toUtf8().constData(), v2.toUtf8().constData());
#else
    return PackageCatalog::compareVersions(v1, v2);
#endif
}

} // namespace lut
