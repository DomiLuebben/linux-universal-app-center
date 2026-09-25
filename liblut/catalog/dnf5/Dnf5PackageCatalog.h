#pragma once
#include <QHash>

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
    void prepareSnapshot(const QStringList &names) override;

    // Öffentliche Parser-Methoden für isolierte Unit-Tests
    static QList<PackageOffer> parseAvailableOffers(const QByteArray &data);
    static QList<PackageOffer> parseAvailableOffers(const QByteArray &data, const QMap<QString, int> &repoScores);
    // Liest `dnf5 repo info --json` und bildet id -> Rangwert ab.
    static QMap<QString, int> parseRepoScores(const QByteArray &json);
    // DNF bevorzugt die niedrigere repo-priority, bei Gleichstand die niedrigere cost.
    // Unser Modell sortiert absteigend, deshalb das Vorzeichen.
    static int repoScore(int priority, int cost) { return -(priority * 100000 + cost); }
    // Vorgaben von DNF5, wenn zu einem Repository keine Angaben vorliegen.
    static constexpr int kDefaultRepoPriority = 99;
    static constexpr int kDefaultRepoCost = 1000;
    static InstalledState parseInstalledState(const QByteArray &data, const QString &packageName);
    static QList<PackageRef> parseFileProviders(const QByteArray &data);
    static QList<InstalledPackage> parseInstalledPackages(const QByteArray &data, const QByteArray &unneededData);

private:
    QByteArray runQuery(const QStringList &args) const;
    static QHash<QString, QList<PackageRef>> loadFileOwners();
    QHash<QString, QList<PackageRef>> fileOwners();
    QMap<QString, int> repoScores() const;

    QString m_program;
    mutable QMutex m_mutex;
    std::optional<QList<InstalledPackage>> m_inventory;
    // Letzter erfolgreicher Bestand: ein fehlgeschlagener Abruf (z. B. während
    // einer laufenden RPM-Transaktion) darf nicht "nichts installiert" melden.
    QList<InstalledPackage> m_lastGoodInventory;
    std::optional<QHash<QString, QList<PackageRef>>> m_fileOwners;
    mutable quint64 m_generation = 1;
    mutable QMap<QString, QList<PackageOffer>> m_offersCache;
    mutable QMap<QString, InstalledState> m_installedCache;
    mutable QMap<QString, int> m_repoScoreCache;
    mutable bool m_repoScoresLoaded = false;
};

} // namespace lut
