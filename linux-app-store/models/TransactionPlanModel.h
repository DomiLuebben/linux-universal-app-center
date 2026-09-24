#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include "liblut/protocol/events.h"
#include "liblut/transaction/TransactionTypes.h"

namespace lut {

class TransactionPlanModel : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
    Q_PROPERTY(bool isEmpty READ isEmpty NOTIFY countChanged)
    Q_PROPERTY(qint64 totalDownloadBytes READ totalDownloadBytes NOTIFY planChanged)
    Q_PROPERTY(QString totalDownloadFormatted READ totalDownloadFormatted NOTIFY planChanged)
    Q_PROPERTY(qint64 totalInstalledDeltaBytes READ totalInstalledDeltaBytes NOTIFY planChanged)
    Q_PROPERTY(QString totalInstalledDeltaFormatted READ totalInstalledDeltaFormatted NOTIFY planChanged)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY planChanged)
    Q_PROPERTY(QString planRevision READ planRevision NOTIFY planChanged)
    Q_PROPERTY(bool hasProtectedConflict READ hasProtectedConflict NOTIFY planChanged)
    Q_PROPERTY(QStringList affectedApps READ affectedApps NOTIFY planChanged)
    Q_PROPERTY(QString actionTitle READ actionTitle NOTIFY planChanged)
    Q_PROPERTY(int installCount READ installCount NOTIFY planChanged)
    Q_PROPERTY(int upgradeCount READ upgradeCount NOTIFY planChanged)
    Q_PROPERTY(int removeCount READ removeCount NOTIFY planChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        VersionRole,
        NewVersionRole,
        ArchRole,
        RepoRole,
        SummaryRole,
        KindRole,
        KindEnumRole,
        DownloadSizeRole,
        DownloadSizeFormattedRole,
        InstalledSizeRole,
        InstalledSizeFormattedRole,
        IsSecurityRole,
        IsKernelRole,
        UserRequestedRole
    };
    Q_ENUM(Roles)

    explicit TransactionPlanModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setPlan(const TransactionPlan &plan, const QString &actionTitle = QString());
    void setOps(const QList<PackageOp> &ops, qint64 downloadBytes, qint64 installedDelta,
                const QStringList &warnings = {}, const QString &actionTitle = QString());
    void setAffectedApps(const QStringList &apps);
    void clear();

    int totalCount() const { return m_ops.size(); }
    bool isEmpty() const { return m_ops.isEmpty(); }
    qint64 totalDownloadBytes() const { return m_downloadBytes; }
    QString totalDownloadFormatted() const;
    qint64 totalInstalledDeltaBytes() const { return m_installedDelta; }
    QString totalInstalledDeltaFormatted() const;
    QStringList warnings() const { return m_warnings; }
    QString planRevision() const { return m_planRevision; }
    bool hasProtectedConflict() const { return m_hasProtectedConflict; }
    QStringList affectedApps() const { return m_affectedApps; }
    QString actionTitle() const { return m_actionTitle; }

    int installCount() const;
    int upgradeCount() const;
    int removeCount() const;

    const QList<PackageOp> &ops() const { return m_ops; }

    static QString formatBytes(qint64 bytes);

signals:
    void countChanged();
    void planChanged();

private:
    QList<PackageOp> m_ops;
    qint64 m_downloadBytes = 0;
    qint64 m_installedDelta = 0;
    QStringList m_warnings;
    QString m_planRevision;
    bool m_hasProtectedConflict = false;
    QStringList m_affectedApps;
    QString m_actionTitle;
};

} // namespace lut
