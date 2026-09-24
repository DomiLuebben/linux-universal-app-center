#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <memory>
#include <optional>
#include "liblut/backend/Backend.h"
#include "liblut/transaction/TransactionTypes.h"

namespace lut {

struct AppRecord {
    QString appKey;                   // stabiler Schlüssel, z.B. "org.kde.kwrite"
    QString componentId;              // AppStream-ID
    QString name;                     // lokalisierter Name
    QString summary;                  // Kurztext
    QString description;              // bereinigte Beschreibung
    QString developer;                // Entwickler
    QString license;                  // SPDX / Lizenz
    QString urlHomepage;              // Website
    QString urlBugtracker;            // Bugtracker
    QString iconSource;               // Icon-Name oder Pfad / URL
    QStringList screenshots;          // URLs zu Screenshots
    QStringList categories;           // Standardisierte Kategorien
    QStringList keywords;             // Suchstichwörter
    QStringList launchableDesktopIds; // .desktop-Dateinamen
    QString origin;                   // Metadatenquelle, z.B. "archlinux", "fedora", "ubuntu"
    QString defaultPackageName;       // primärer Paketname
    QStringList packageNames;         // alle zugehörigen Pakete der Anwendung

    bool operator==(const AppRecord &other) const = default;
    QJsonObject toJson() const;
    static AppRecord fromJson(const QJsonObject &obj);
};

// Katalogabfrage-Ergebnisvertrag einschließlich Erfolg/Fehler/Generation
struct CatalogQueryResult {
    enum class Status {
        Success,
        NotFound,
        BackendError,
        CatalogMissing,
        Loading
    };

    Status status = Status::Success;
    quint64 generation = 0;
    QString errorMessage;
    QList<AppRecord> apps;
    QList<PackageOffer> offers;
    QList<InstalledPackage> rawPackages;

    bool isSuccess() const { return status == Status::Success; }
    static QString statusToString(Status s);
    static Status statusFromString(const QString &str);
};

// Abstrakter Vertrag für lesende Paketdaten der Backends (ohne GUI-Bezug)
class PackageCatalog {
public:
    virtual ~PackageCatalog() = default;

    virtual quint64 catalogGeneration() const = 0;
    virtual QList<PackageOffer> offersForPackage(const QString &packageName) = 0;
    virtual std::optional<PackageOffer> candidateOffer(const QString &packageName) = 0;
    virtual InstalledState installedStateForPackage(const QString &packageName) = 0;
    virtual QList<InstalledPackage> allInstalledPackages() = 0;
    virtual QList<PackageRef> findPackagesProvidingFile(const QString &filePath) = 0;
    virtual QList<PackageOffer> searchPackages(const QString &query) { Q_UNUSED(query); return {}; }
    virtual int compareVersions(const QString &v1, const QString &v2) const;
    // Prime command-based backends in batches on the store worker.
    virtual void prepareSnapshot(const QStringList &names) { Q_UNUSED(names); }
    virtual void reload() {}
};

} // namespace lut
