#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <memory>
#include <optional>
#include <QSet>
#include <QThreadPool>
#include <QHash>
#include <atomic>
#include "liblut/catalog/PackageCatalog.h"
#include "liblut/transaction/TransactionTypes.h"
#include "linux-app-store/catalog/CatalogService.h"
#include "linux-app-store/AppLauncher.h"

namespace lut {

class ApplicationStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(quint64 revision READ revision NOTIFY catalogLoaded)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(bool isLoaded READ isLoaded NOTIFY loadedChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY catalogLoaded)
    Q_PROPERTY(int totalAppCount READ totalAppCount NOTIFY catalogLoaded)
    Q_PROPERTY(int installedAppCount READ installedAppCount NOTIFY catalogLoaded)

public:
    struct CuratedCollection {
        QString id;
        QString title;
        QString description;
        QStringList appIds;
    };

    explicit ApplicationStore(CatalogService *catalogService,
                              PackageCatalog *packageCatalog,
                              QObject *parent = nullptr);
    ~ApplicationStore() override;

    void setPackageCatalog(PackageCatalog *packageCatalog);
    void addPackageCatalog(PackageCatalog *packageCatalog);
    QList<PackageCatalog*> packageCatalogs() const;

    // Test- und Mock-Schnittstellen für Multi-Quellen-Abnahme
    void setOffersForApp(const QString &appKey, const QList<PackageOffer> &offers);
    void setInstalledForApp(const QString &appKey, const InstalledState &state);

    quint64 revision() const { return m_revision; }
    bool isLoading() const;
    bool isLoaded() const;
    QString lastError() const;
    int totalAppCount() const;
    int installedAppCount() const;

    QList<AppRecord> allApps() const;
    std::optional<AppRecord> appRecord(const QString &appKey) const;
    std::optional<PackageOffer> candidateOffer(const QString &appKey) const;
    QList<PackageOffer> allOffers(const QString &appKey) const;
    InstalledState installedState(const QString &appKey) const;
    AppActionState actionState(const QString &appKey, const QString &selectedSource = QString()) const;
    bool isSourceInstalled(const QString &appKey, const QString &source) const;

    // Alle Pakete, die zusammen die Anwendung ergeben. AppStream kann mehrere
    // pkgname-Einträge führen; die sind ein Installationssatz, keine Alternativen.
    QStringList packageSet(const QString &appKey) const;
    // Anwendungen, die von den genannten Paketen abhängen - für die Entfernungsvorschau.
    QStringList appsProvidedByPackages(const QStringList &packageNames) const;

    QList<AppRecord> searchApps(const QString &query) const;
    QList<AppRecord> appsByCategory(const QString &category) const;
    QList<PackageOffer> searchPackagesOnly(const QString &query) const;

    void refresh();
    // Nur Installationsstatus und Paketangebote neu ermitteln. Die AppStream-
    // Metadaten ändern sich durch eine Paketaktion nicht; sie neu einzulesen
    // kostete nach jeder Installation mehrere Sekunden.
    void refreshInstalledState();
    void updateTransactionStatus(bool installSupported, bool removeSupported, bool busy, bool hasPlan, bool error);
    void transactionStarted();
    void transactionFinished(Result result);

    static int compareNativeVersions(const QString &backend, const QString &v1, const QString &v2, const PackageCatalog *catalog = nullptr);

    void loadCuratedCollections(const QString &filePath = QString());
    QList<CuratedCollection> rawCuratedCollections() const { return m_curatedCollections; }

    // QML-invokable API
    Q_INVOKABLE QVariantMap getApp(const QString &appKey) const;
    Q_INVOKABLE QString getActionState(const QString &appKey, const QString &selectedSource = QString()) const;
    Q_INVOKABLE QVariantMap getCandidateOffer(const QString &appKey) const;
    Q_INVOKABLE QVariantList getAllOffers(const QString &appKey) const;
    Q_INVOKABLE QVariantMap getInstalledState(const QString &appKey) const;
    Q_INVOKABLE QVariantList getCuratedCollection(const QString &collectionKey) const;
    Q_INVOKABLE QVariantList curatedCollections() const;
    Q_INVOKABLE QStringList curatedCollectionKeys() const;
    Q_INVOKABLE void requestInstall(const QString &appKey, int offerIndex = -1);
    Q_INVOKABLE void requestRemove(const QString &appKey, const QString &source = QString());
    Q_INVOKABLE bool launchApp(const QString &appKey, const QString &source = QString());

    AppLauncher* appLauncher() { return &m_launcher; }

signals:
    void loadingChanged(bool loading);
    void loadedChanged(bool loaded);
    void catalogLoaded();
    void appStateChanged(const QString &appKey);
    void appLaunched(const QString &appKey);
    void appLaunchFailed(const QString &appKey, const QString &errorMessage);
    void installRequested(const QStringList &packageNames, const QString &repoId);
    void removeRequested(const QStringList &packageNames);
    void installPackageRefsRequested(const QList<lut::PackageRef> &targets);
    void removePackageRefsRequested(const QList<lut::PackageRef> &targets);

private slots:
    void onCatalogServiceLoaded(bool success);

private:
    void logDiagnostics();
    void buildSnapshot();
    void notifyState();
    bool m_installSupported = true;
    bool m_removeSupported = true;
    bool m_transactionBusy = false;
    bool m_transactionPlan = false;
    QStringList m_operationPackages;
    AppActionState m_operationState = AppActionState::Available;
    bool m_operationFailed = false;

    struct Snapshot {
        QHash<QString, QList<PackageOffer>> offers;       // Key: Paketname oder AppKey
        QHash<QString, QList<PackageOffer>> appOffers;    // Key: normalisierter AppKey
        QHash<QString, PackageOffer> candidates;
        QHash<QString, InstalledState> installed;         // Key: Paketname
        QHash<QString, InstalledState> appInstalled;      // Key: normalisierter AppKey
    };
    QThreadPool m_worker;
    Snapshot m_snapshot;
    bool m_snapshotLoading = false;
    bool m_snapshotLoaded = false;
    bool m_refreshPending = false;
    quint64 m_revision = 0;
    QHash<QString, AppRecord> m_packageRecords;
    mutable std::atomic<quint64> m_latestSearch{0};
    mutable QString m_searchQuery;
    mutable quint64 m_searchRequest = 0;
    mutable QList<PackageOffer> m_searchResults;
    // Namen aller installierten Pakete aus einer einzigen Backendabfrage.
    // Ohne diesen Vorfilter startet ein Katalogdurchlauf je Paket einen
    // eigenen Unterprozess - bei tausenden Katalogeinträgen dauert das Minuten.
    const QSet<QString> &installedPackageNames() const;

    CatalogService *m_catalogService = nullptr;
    PackageCatalog *m_packageCatalog = nullptr;
    QList<PackageCatalog*> m_packageCatalogs;
    QList<CuratedCollection> m_curatedCollections;
    AppLauncher m_launcher;
    mutable QSet<QString> m_installedNames;
    mutable quint64 m_installedNamesGeneration = 0;
    mutable bool m_installedNamesLoaded = false;
    QHash<QString, QStringList> m_resolvedPackages;
    void resolvePackageNamesForApps(const QList<AppRecord> &apps,
                                   const QSet<QString> &installedPackageNames,
                                   QHash<QString, QStringList> &resolvedOut);
};

} // namespace lut
