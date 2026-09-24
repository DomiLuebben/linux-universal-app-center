#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QMutex>
#include <QMap>
#include <optional>
#include "liblut/catalog/PackageCatalog.h"

namespace lut {

class AptPackageCatalog : public PackageCatalog {
public:
    explicit AptPackageCatalog(const QString &aptCacheProgram = QStringLiteral("/usr/bin/apt-cache"),
                              const QString &dpkgQueryProgram = QStringLiteral("/usr/bin/dpkg-query"));
    ~AptPackageCatalog() override = default;

    quint64 catalogGeneration() const override;
    QList<PackageOffer> offersForPackage(const QString &packageName) override;
    std::optional<PackageOffer> candidateOffer(const QString &packageName) override;
    InstalledState installedStateForPackage(const QString &packageName) override;
    QList<InstalledPackage> allInstalledPackages() override;
    QList<PackageRef> findPackagesProvidingFile(const QString &filePath) override;
    QList<PackageOffer> searchPackages(const QString &query) override;
    int compareVersions(const QString &v1, const QString &v2) const override;
    void reload() override;
    void prepareSnapshot(const QStringList &names) override;

    // Öffentliche statische Parser-Methoden für isolierte Unit-Tests
    static QString parsePolicyCandidate(const QString &policyOutput);
    static QString parsePolicyInstalled(const QString &policyOutput);
    static QList<PackageOffer> parseAvailableOffers(const QString &policyOutput, const QString &showOutput, const QString &packageName);
    static InstalledState parseInstalledState(const QString &dpkgQueryOutput, const QString &packageName, const QStringList &desktopFiles = {});
    static QList<InstalledPackage> parseInstalledPackages(const QString &dpkgQueryOutput);
    static QList<PackageRef> parseFileProviders(const QString &dpkgSearchOutput);

private:
    QByteArray runCommand(const QString &program, const QStringList &args) const;

    QString m_aptCacheProgram;
    QString m_dpkgQueryProgram;
    mutable QMutex m_mutex;
    std::optional<QList<InstalledPackage>> m_inventory;
    mutable quint64 m_generation = 1;
    mutable QMap<QString, QList<PackageOffer>> m_offersCache;
    mutable QMap<QString, InstalledState> m_installedCache;
};

} // namespace lut
