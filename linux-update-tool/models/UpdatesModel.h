#pragma once

#include <QAbstractListModel>
#include "liblut/protocol/events.h"

namespace lut {

class UpdatesModel : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(qint64 totalDownloadBytes READ totalDownloadBytes NOTIFY dataChangedTotal)
    Q_PROPERTY(QString totalDownloadFormatted READ totalDownloadFormatted NOTIFY dataChangedTotal)
    Q_PROPERTY(qint64 totalInstalledDeltaBytes READ totalInstalledDeltaBytes NOTIFY dataChangedTotal)
    Q_PROPERTY(QString totalInstalledDeltaFormatted READ totalInstalledDeltaFormatted NOTIFY dataChangedTotal)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        VersionRole,
        NewVersionRole,
        VersionTransitionRole,
        ArchRole,
        RepoRole,
        SummaryRole,
        DownloadSizeRole,
        DownloadSizeFormattedRole,
        InstalledSizeRole,
        IsSecurityRole,
        IsKernelRole,
        UserRequestedRole,
        SelectedRole,
        CategoryRole
    };

    explicit UpdatesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QHash<int, QByteArray> roleNames() const override;

    void setPackages(const QList<PackageOp> &packages);
    void clear();

    int totalCount() const { return m_packages.size(); }
    int selectedCount() const;
    qint64 totalDownloadBytes() const;
    QString totalDownloadFormatted() const;
    qint64 totalInstalledDeltaBytes() const;
    QString totalInstalledDeltaFormatted() const;

    QList<PackageOp> selectedPackages() const;

    static QString categorizePackage(const PackageOp &op);
    static QString formatBytes(qint64 bytes);

public slots:
    void toggleSelection(int row);
    void selectAll(bool select);

signals:
    void countChanged();
    void selectionChanged();
    void dataChangedTotal();

private:
    struct Item {
        PackageOp op;
        bool selected = true;
        QString category;
    };

    QList<Item> m_packages;
};

} // namespace lut
