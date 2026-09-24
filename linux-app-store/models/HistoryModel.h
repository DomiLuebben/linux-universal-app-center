#pragma once

#include <QAbstractListModel>
#include "liblut/backend/Backend.h"

namespace lut {

class HistoryModel : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TimestampFormattedRole,
        CommandRole,
        ResultRole,
        PackagesAlteredRole,
        CanUndoRole,
        SourceRole,
        TargetRole
    };

    static QList<HistoryEntry> mergeHistory(const QList<HistoryEntry> &nativeEntries,
                                            const QList<HistoryEntry> &flatpakEntries,
                                            const QList<HistoryEntry> &snapEntries,
                                            int limit = 50);

    explicit HistoryModel(QObject *parent = nullptr);
    ~HistoryModel() override = default;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int totalCount() const { return m_items.size(); }

public slots:
    void refresh();
    void undo(int id);

signals:
    void countChanged();
    void undoRequested(int id);

private:
    QList<HistoryEntry> m_items;
};

} // namespace lut
