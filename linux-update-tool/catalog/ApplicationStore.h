#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <memory>
#include <optional>
#include "liblut/catalog/PackageCatalog.h"
#include "liblut/transaction/TransactionTypes.h"
#include "linux-update-tool/catalog/CatalogService.h"
#include "linux-update-tool/AppLauncher.h"

namespace lut {

class ApplicationStore : public QObject {
    Q_OBJECT
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
    AppActionState actionState(const QString &appKey) const;

    QList<AppRecord> searchApps(const QString &query) const;
    QList<AppRecord> appsByCategory(const QString &category) const;
    QList<PackageOffer> searchPackagesOnly(const QString &query) const;

    void refresh();
    static int compareNativeVersions(const QString &backend, const QString &v1, const QString &v2, const PackageCatalog *catalog = nullptr);

    void loadCuratedCollections(const QString &filePath = QString());
    QList<CuratedCollection> rawCuratedCollections() const { return m_curatedCollections; }

    // QML-invokable API
    Q_INVOKABLE QVariantMap getApp(const QString &appKey) const;
    Q_INVOKABLE QString getActionState(const QString &appKey) const;
    Q_INVOKABLE QVariantMap getCandidateOffer(const QString &appKey) const;
    Q_INVOKABLE QVariantMap getInstalledState(const QString &appKey) const;
    Q_INVOKABLE QVariantList getCuratedCollection(const QString &collectionKey) const;
    Q_INVOKABLE QVariantList curatedCollections() const;
    Q_INVOKABLE QStringList curatedCollectionKeys() const;
    Q_INVOKABLE void requestInstall(const QString &appKey);
    Q_INVOKABLE void requestRemove(const QString &appKey);
    Q_INVOKABLE bool launchApp(const QString &appKey);

    AppLauncher* appLauncher() { return &m_launcher; }

signals:
    void loadingChanged(bool loading);
    void loadedChanged(bool loaded);
    void catalogLoaded();
    void appStateChanged(const QString &appKey);
    void appLaunched(const QString &appKey);
    void appLaunchFailed(const QString &appKey, const QString &errorMessage);
    void installRequested(const QString &packageName, const QString &repoId);
    void removeRequested(const QString &packageName);

private slots:
    void onCatalogServiceLoaded(bool success);

private:
    void logDiagnostics();

    CatalogService *m_catalogService = nullptr;
    PackageCatalog *m_packageCatalog = nullptr;
    QList<CuratedCollection> m_curatedCollections;
    AppLauncher m_launcher;
};

} // namespace lut
