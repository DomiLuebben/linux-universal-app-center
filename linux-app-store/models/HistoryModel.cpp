#include "HistoryModel.h"
#include "liblut/backend/Backend.h"
#include "liblut/backend/flatpak/FlatpakBackend.h"
#include "liblut/backend/snap/SnapBackend.h"
#include "liblut/backend/snap/SnapAvailability.h"
#include <QDebug>
#include <QSet>
#include <algorithm>

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
        case SourceRole: {
            if (item.command.startsWith(QLatin1String("[Flatpak]"))) return QStringLiteral("Flatpak");
            if (item.command.startsWith(QLatin1String("[Snap]"))) return QStringLiteral("Snap");
            return QStringLiteral("Nativ");
        }
        case TargetRole: {
            QString cmd = item.command;
            if (cmd.startsWith(QLatin1Char('['))) {
                int closing = cmd.indexOf(QLatin1Char(']'));
                if (closing != -1) cmd = cmd.mid(closing + 1).trimmed();
            }
            return cmd;
        }
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
        {CanUndoRole, "canUndo"},
        {SourceRole, "source"},
        {TargetRole, "target"}
    };
}

QList<HistoryEntry> HistoryModel::mergeHistory(const QList<HistoryEntry> &nativeEntries,
                                               const QList<HistoryEntry> &flatpakEntries,
                                               const QList<HistoryEntry> &snapEntries,
                                               int limit)
{
    QList<HistoryEntry> all;
    all.reserve(nativeEntries.size() + flatpakEntries.size() + snapEntries.size());

    for (auto entry : nativeEntries) {
        if (!entry.command.startsWith(QLatin1Char('['))) {
            entry.command = QStringLiteral("[Nativ] %1").arg(entry.command);
        }
        all.append(entry);
    }
    all.append(flatpakEntries);
    all.append(snapEntries);

    std::stable_sort(all.begin(), all.end(), [](const HistoryEntry &a, const HistoryEntry &b) {
        if (a.timestamp.isValid() && b.timestamp.isValid()) {
            return a.timestamp > b.timestamp;
        }
        return a.timestamp.isValid() > b.timestamp.isValid();
    });

    QList<HistoryEntry> deduplicated;
    deduplicated.reserve(all.size());
    QSet<QString> seenSignatures;

    for (const auto &entry : all) {
        qint64 tSec = entry.timestamp.isValid() ? (entry.timestamp.toSecsSinceEpoch() / 2) : 0;
        QString sig = QStringLiteral("%1:%2:%3").arg(QString::number(tSec), entry.command, entry.result);
        if (seenSignatures.contains(sig)) {
            continue;
        }
        seenSignatures.insert(sig);
        deduplicated.append(entry);
        if (deduplicated.size() >= limit) break;
    }

    return deduplicated;
}

void HistoryModel::refresh() {
    QString error;
    auto backend = Backend::createForHost(&error);
    auto nativeEntries = backend ? backend->history(50) : QList<HistoryEntry>{};
    if (!error.isEmpty()) qWarning() << error;

    QList<HistoryEntry> flatpakEntries;
    if (FlatpakBackend::isFlatpakAvailable()) {
        FlatpakBackend flatpakBackend;
        flatpakEntries = flatpakBackend.history(50);
    }

    QList<HistoryEntry> snapEntries;
    if (SnapAvailability::isSnapAvailable()) {
        SnapBackend snapBackend;
        snapEntries = snapBackend.history(50);
    }

    beginResetModel();
    m_items = mergeHistory(nativeEntries, flatpakEntries, snapEntries, 50);
    endResetModel();

    emit countChanged();
}

void HistoryModel::undo(int id) {
    emit undoRequested(id);
}

} // namespace lut
