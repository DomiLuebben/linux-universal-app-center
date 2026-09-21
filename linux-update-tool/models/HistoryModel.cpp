#include "HistoryModel.h"
#include "liblut/backend/alpm/AlpmBackend.h"

namespace lut {

HistoryModel::HistoryModel(QObject *parent)
    : QAbstractListModel(parent) {
    refresh();
}

int HistoryModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return m_items.size();
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const auto &item = m_items.at(index.row());
    switch (role) {
        case IdRole: return item.id;
        case TimestampFormattedRole:
            return item.timestamp.isValid() ? item.timestamp.toString(QStringLiteral("dd.MM.yyyy HH:mm")) : QStringLiteral("-");
        case CommandRole: return item.command;
        case ResultRole: return item.result;
        case PackagesAlteredRole: return item.packagesAltered;
        case CanUndoRole: return item.canUndo;
        default: return {};
    }
}

QHash<int, QByteArray> HistoryModel::roleNames() const {
    return {
        {IdRole, "historyId"},
        {TimestampFormattedRole, "timestampFormatted"},
        {CommandRole, "command"},
        {ResultRole, "result"},
        {PackagesAlteredRole, "packagesAltered"},
        {CanUndoRole, "canUndo"}
    };
}

void HistoryModel::refresh() {
    AlpmBackend backend;
    auto entries = backend.history(50);

    beginResetModel();
    m_items = entries;
    endResetModel();

    emit countChanged();
}

void HistoryModel::undo(int id) {
    emit undoRequested(id);
}

} // namespace lut
