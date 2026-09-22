#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QMutex>
#include <optional>
#include "liblut/catalog/PackageCatalog.h"

#ifdef HAVE_ALPM
#include <alpm.h>
#else
struct _alpm_handle_t;
typedef struct _alpm_handle_t alpm_handle_t;
#endif

namespace lut {

class AlpmPackageCatalog : public PackageCatalog {
public:
    explicit AlpmPackageCatalog(const QString &rootPath = QStringLiteral("/"),
                                const QString &dbPath = QStringLiteral("/var/lib/pacman"),
                                const QString &configPath = QStringLiteral("/etc/pacman.conf"));
    ~AlpmPackageCatalog() override;

    quint64 catalogGeneration() const override;
    QList<PackageOffer> offersForPackage(const QString &packageName) override;
    std::optional<PackageOffer> candidateOffer(const QString &packageName) override;
    InstalledState installedStateForPackage(const QString &packageName) override;
    QList<InstalledPackage> allInstalledPackages() override;
    QList<PackageRef> findPackagesProvidingFile(const QString &filePath) override;
    QList<PackageOffer> searchPackages(const QString &query) override;
    int compareVersions(const QString &v1, const QString &v2) const override;

    QStringList configuredRepositories() const;
    QStringList configuredArchitectures() const;
    void reload();

private:
    void ensureInitializedLocked() const;
    void releaseHandleLocked() const;
    void parseConfigurationLocked() const;

    QString m_rootPath;
    QString m_dbPath;
    QString m_configPath;

    mutable QMutex m_mutex;
    mutable alpm_handle_t *m_handle = nullptr;
    mutable QStringList m_repositories;
    mutable QStringList m_architectures;
    mutable quint64 m_generation = 1;
};

} // namespace lut
