#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QMutex>
#include <QMap>
#include <optional>
#include "liblut/catalog/PackageCatalog.h"

namespace lut {

class Dnf5PackageCatalog : public PackageCatalog {
public:
    explicit Dnf5PackageCatalog(const QString &program = QStringLiteral("/usr/bin/dnf5"));
    ~Dnf5PackageCatalog() override = default;

    quint64 catalogGeneration() const override;
    QList<PackageOffer> offersForPackage(const QString &packageName) override;
    std::optional<PackageOffer> candidateOffer(const QString &packageName) override;
    InstalledState installedStateForPackage(const QString &packageName) override;
    QList<InstalledPackage> allInstalledPackages() override;
    QList<PackageRef> findPackagesProvidingFile(const QString &filePath) override;
    QList<PackageOffer> searchPackages(const QString &query) override;
    int compareVersions(const QString &v1, const QString &v2) const override;
    void reload() override;

    // Öffentliche Parser-Methoden für isolierte Unit-Tests
    static QList<PackageOffer> parseAvailableOffers(const QByteArray &data);
    static InstalledState parseInstalledState(const QByteArray &data, const QString &packageName);
    static QList<PackageRef> parseFileProviders(const QByteArray &data);
    static QList<InstalledPackage> parseInstalledPackages(const QByteArray &data, const QByteArray &unneededData);

private:
    QByteArray runQuery(const QStringList &args) const;

    QString m_program;
    mutable QMutex m_mutex;
    mutable quint64 m_generation = 1;
    mutable QMap<QString, QList<PackageOffer>> m_offersCache;
    mutable QMap<QString, InstalledState> m_installedCache;
};

} // namespace lut
