#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>
#include <QMultiHash>
#include <QThreadPool>
#include <memory>
#include <optional>
#include "liblut/catalog/PackageCatalog.h"

namespace AppStream {
class Pool;
class Component;
}

namespace lut {

class CatalogService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(bool isLoaded READ isLoaded NOTIFY loadedChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)

public:
    explicit CatalogService(QObject *parent = nullptr);
    ~CatalogService() override;

    bool isLoaded() const { return m_loaded; }
    bool isLoading() const { return m_loading; }
    QString lastError() const { return m_lastError; }
    quint64 generation() const { return m_generation; }

    void setLoadStdDataLocations(bool loadStd);
    void addExtraDataLocation(const QString &directory);
    void setLocale(const QString &locale);
    QString locale() const;

    bool load();
    void loadAsync();
    void reload();

    QList<AppRecord> allApps() const;
    std::optional<AppRecord> appByKey(const QString &appKey) const;
    // Kennung ohne ".desktop"-Zusatz, damit Sammlungen distributionsübergreifend treffen.
    static QString normalizedAppKey(const QString &appKey);
    std::optional<AppRecord> appByPackageName(const QString &packageName) const;
    QList<AppRecord> appsByPackageName(const QString &packageName) const;
    QList<AppRecord> appsByCategory(const QString &category) const;
    QList<AppRecord> search(const QString &query) const;
    void searchAsync(const QString &query, quint64 requestId);
    void setPackageNamesForApp(const QString &appKey, const QStringList &packageNames);

    QList<PackageOffer> flatpakOffersForApp(const QString &appKey) const;
    QHash<QString, QList<PackageOffer>> allFlatpakOffers() const;
    static bool isFlatpakAvailable();
    static void setFlatpakAvailableOverride(std::optional<bool> override);

    static QString sanitizeDescription(const QString &raw);
    static bool isSafeMediaUrl(const QString &url);
    static AppRecord componentToAppRecord(const AppStream::Component &comp, const QString &preferredLocale = QString());

signals:
    void loadingChanged(bool loading);
    void loadedChanged(bool loaded);
    void loaded(bool success);
    void errorOccurred(const QString &error);
    void searchCompleted(quint64 requestId, const QList<lut::AppRecord> &results);

private:
    void processLoadedComponents();

    QThreadPool m_loader;
    std::unique_ptr<AppStream::Pool> m_pool;
    bool m_loading = false;
    bool m_loaded = false;
    QString m_lastError;
    quint64 m_generation = 1;
    bool m_loadStdLocations = true;
    QStringList m_extraLocations;
    QString m_locale;

    QList<AppRecord> m_apps;
    QHash<QString, AppRecord> m_appsByKey;
    QHash<QString, AppRecord> m_appsByNormalizedKey;
    QMultiHash<QString, AppRecord> m_appsByPackage;
    QHash<QString, QList<PackageOffer>> m_flatpakOffersByNormalizedKey;
};

} // namespace lut
