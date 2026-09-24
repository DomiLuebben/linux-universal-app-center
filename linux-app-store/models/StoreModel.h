#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QTimer>
#include "liblut/catalog/PackageCatalog.h"
#include "linux-app-store/catalog/ApplicationStore.h"

namespace lut {

class StoreModel : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(ApplicationStore* appStore READ appStore WRITE setAppStore NOTIFY appStoreChanged)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY filterChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY filterChanged)
    Q_PROPERTY(QString collection READ collection WRITE setCollection NOTIFY filterChanged)
    Q_PROPERTY(bool installedOnly READ installedOnly WRITE setInstalledOnly NOTIFY filterChanged)
    Q_PROPERTY(bool packagesOnly READ packagesOnly WRITE setPackagesOnly NOTIFY filterChanged)
    Q_PROPERTY(QString sourceFilter READ sourceFilter WRITE setSourceFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        AppKeyRole = Qt::UserRole + 1,
        NameRole,
        SummaryRole,
        DeveloperRole,
        IconSourceRole,
        ActionStateRole,
        IsInstalledRole,
        CategoriesRole,
        OriginRole,
        DefaultPackageNameRole,
        RecordRole
    };
    Q_ENUM(Roles)

    explicit StoreModel(QObject *parent = nullptr);
    explicit StoreModel(ApplicationStore *store, QObject *parent = nullptr);
    ~StoreModel() override = default;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    ApplicationStore* appStore() const;
    void setAppStore(ApplicationStore *store);

    QString category() const;
    void setCategory(const QString &category);

    QString searchQuery() const;
    void setSearchQuery(const QString &query);

    QString collection() const;
    void setCollection(const QString &collection);

    bool installedOnly() const;
    void setInstalledOnly(bool installedOnly);

    bool packagesOnly() const;
    void setPackagesOnly(bool packagesOnly);

    QString sourceFilter() const;
    void setSourceFilter(const QString &source);

    int count() const;

    Q_INVOKABLE QVariantMap get(int index) const;

    static bool matchesCategory(const QStringList &appCats, const QString &selectedCat);
    static int calculateSearchScore(const AppRecord &app, const QString &query);

public slots:
    void refresh();
    void search(const QString &query);

signals:
    void appStoreChanged();
    void filterChanged();
    void countChanged();

private slots:
    void onDebounceTimeout();
    void onAppStateChanged(const QString &appKey);
    void onCatalogLoaded();

private:
    struct Item {
        QString appKey;
        QString name;
        QString summary;
        QString developer;
        QString iconSource;
        QString actionState;
        bool isInstalled = false;
        QStringList categories;
        QString origin;
        QString defaultPackageName;
        QVariantMap record;
        int searchScore = 0;
    };

    void rebuildItems();

    ApplicationStore *m_appStore = nullptr;
    QString m_category;
    QString m_searchQuery;
    QString m_collection;
    QString m_sourceFilter;
    bool m_installedOnly = false;
    bool m_packagesOnly = false;

    QList<Item> m_items;
    QTimer m_debounceTimer;
    QString m_pendingQuery;
};

} // namespace lut
