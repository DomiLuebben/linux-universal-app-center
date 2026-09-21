#pragma once

#include <QAbstractListModel>
#include <QTimer>
#include <QFutureWatcher>
#include "liblut/backend/Backend.h"

namespace lut {

class InstalledModel : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
    Q_PROPERTY(int orphanCount READ orphanCount NOTIFY countChanged)
    Q_PROPERTY(QString cleanableCacheFormatted READ cleanableCacheFormatted NOTIFY cacheChanged)
    Q_PROPERTY(bool isSearching READ isSearching NOTIFY isSearchingChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        VersionRole,
        ArchRole,
        InstalledSizeRole,
        InstalledSizeFormattedRole,
        DescriptionRole,
        IsOrphanRole,
        IsOldKernelRole
    };

    explicit InstalledModel(QObject *parent = nullptr);
    ~InstalledModel() override = default;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int totalCount() const { return m_items.size(); }
    int orphanCount() const { return m_orphanCount; }
    QString cleanableCacheFormatted() const { return m_cleanableCacheFormatted; }
    bool isSearching() const { return m_isSearching; }

public slots:
    void search(const QString &query);
    void refresh();
    void cleanOrphans();
    void cleanCache();

signals:
    void countChanged();
    void cacheChanged();
    void isSearchingChanged();
    void cleanupRequested(const QString &command);
    void cleanupStarted(const QString &action);
    void cleanupFinished(const QString &action, bool success);

private slots:
    void executeSearch();

private:
    void updateHygieneStats();
    static QString formatSize(qint64 bytes);

    QList<InstalledPackage> m_items;
    QTimer m_debounceTimer;
    QString m_pendingQuery;
    int m_orphanCount = 0;
    QString m_cleanableCacheFormatted;
    bool m_isSearching = false;
};

} // namespace lut
