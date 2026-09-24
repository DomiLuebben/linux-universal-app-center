#include "UpdatesModel.h"
#include <QLocale>

namespace lut {

UpdatesModel::UpdatesModel(QObject *parent)
    : QAbstractListModel(parent) {}

int UpdatesModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return m_packages.size();
}

QHash<int, QByteArray> UpdatesModel::roleNames() const {
    return {
        {IdRole, "pkgId"},
        {NameRole, "name"},
        {VersionRole, "version"},
        {NewVersionRole, "newVersion"},
        {VersionTransitionRole, "versionTransition"},
        {ArchRole, "arch"},
        {RepoRole, "repo"},
        {SummaryRole, "summary"},
        {DownloadSizeRole, "downloadSize"},
        {DownloadSizeFormattedRole, "downloadSizeFormatted"},
        {InstalledSizeRole, "installedSize"},
        {IsSecurityRole, "isSecurity"},
        {IsKernelRole, "isKernel"},
        {UserRequestedRole, "userRequested"},
        {SelectedRole, "selected"},
        {CategoryRole, "category"}
    };
}

QString UpdatesModel::categorizePackage(const PackageOp &op) {
    if (op.isSecurity) {
        return QStringLiteral("Sicherheit");
    }
    if (op.isKernel || op.name.contains(QLatin1String("grub")) || op.name.contains(QLatin1String("systemd")) ||
        op.name.contains(QLatin1String("dracut")) || op.name.contains(QLatin1String("mkinitcpio"))) {
        return QStringLiteral("Kernel & Boot");
    }
    if (op.name.startsWith(QLatin1String("lib")) || op.name.startsWith(QLatin1String("qt6-")) ||
        op.name.contains(QLatin1String("glibc")) || op.name.startsWith(QLatin1String("kf6-"))) {
        return QStringLiteral("Systembibliotheken");
    }
    if (!op.summary.isEmpty() && !op.summary.startsWith(QLatin1String("Library"))) {
        return QStringLiteral("Anwendungen");
    }
    return QStringLiteral("Sonstiges");
}

QString UpdatesModel::formatBytes(qint64 bytes) {
    if (bytes < 0) return QStringLiteral("−") + formatBytes(-bytes);
    if (bytes == 0) return QStringLiteral("0 B");
    const double kb = 1024.0;
    const double mb = kb * 1024.0;
    const double gb = mb * 1024.0;

    QLocale loc;
    if (bytes >= gb) {
        return loc.toString(bytes / gb, 'f', 1) + QStringLiteral(" GB");
    }
    if (bytes >= mb) {
        return loc.toString(bytes / mb, 'f', 1) + QStringLiteral(" MB");
    }
    if (bytes >= kb) {
        return loc.toString(bytes / kb, 'f', 0) + QStringLiteral(" KB");
    }
    return QString::number(bytes) + QStringLiteral(" B");
}

QVariant UpdatesModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_packages.size()) {
        return QVariant();
    }

    const auto &item = m_packages.at(index.row());
    const auto &op = item.op;

    switch (role) {
        case IdRole: return op.id;
        case NameRole: return op.name;
        case VersionRole: return op.version;
        case NewVersionRole: return op.newVersion;
        case VersionTransitionRole:
            if (op.kind == PackageOp::Kind::Remove) return QStringLiteral("Entfernen: %1").arg(op.version);
            if (op.version.isEmpty()) return QStringLiteral("Installieren: %1").arg(op.newVersion);
            return QStringLiteral("%1: %2 → %3").arg(PackageOp::kindToString(op.kind), op.version, op.newVersion);
        case ArchRole: return op.arch;
        case RepoRole: return op.repo.isEmpty() ? QStringLiteral("system") : op.repo;
        case SummaryRole: return op.summary;
        case DownloadSizeRole: return op.downloadSize;
        case DownloadSizeFormattedRole: return formatBytes(op.downloadSize);
        case InstalledSizeRole: return op.installedSize;
        case IsSecurityRole: return op.isSecurity;
        case IsKernelRole: return op.isKernel;
        case UserRequestedRole: return op.userRequested;
        case SelectedRole: return item.selected;
        case CategoryRole: return item.category;
    }

    return QVariant();
}

bool UpdatesModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_packages.size()) {
        return false;
    }

    if (role == SelectedRole) {
        bool sel = value.toBool();
        if (m_packages[index.row()].selected != sel) {
            m_packages[index.row()].selected = sel;
            emit dataChanged(index, index, {SelectedRole});
            emit selectionChanged();
            emit dataChangedTotal();
            return true;
        }
    }

    return false;
}

void UpdatesModel::setPackages(const QList<PackageOp> &packages) {
    beginResetModel();
    m_packages.clear();
    for (const auto &op : packages) {
        Item it;
        it.op = op;
        it.selected = true;
        it.category = categorizePackage(op);
        m_packages.append(it);
    }
    endResetModel();

    emit countChanged();
    emit selectionChanged();
    emit dataChangedTotal();
}

void UpdatesModel::clear() {
    beginResetModel();
    m_packages.clear();
    endResetModel();

    emit countChanged();
    emit selectionChanged();
    emit dataChangedTotal();
}

void UpdatesModel::removeApplied(bool allPackages) {
    if (allPackages) {
        clear();
        return;
    }
    beginResetModel();
    QList<Item> remaining;
    for (const auto &item : std::as_const(m_packages)) {
        if (!item.selected) remaining.append(item);
    }
    m_packages = remaining;
    endResetModel();

    emit countChanged();
    emit selectionChanged();
    emit dataChangedTotal();
}

int UpdatesModel::selectedCount() const {
    int count = 0;
    for (const auto &it : m_packages) {
        if (it.selected) count++;
    }
    return count;
}

qint64 UpdatesModel::totalDownloadBytes() const {
    qint64 total = 0;
    for (const auto &it : m_packages) {
        if (it.selected) total += it.op.downloadSize;
    }
    return total;
}

QString UpdatesModel::totalDownloadFormatted() const {
    return formatBytes(totalDownloadBytes());
}

qint64 UpdatesModel::totalInstalledDeltaBytes() const {
    qint64 total = 0;
    for (const auto &it : m_packages) {
        if (it.selected) total += it.op.installedSizeDelta.value_or(it.op.installedSize);
    }
    return total;
}

QString UpdatesModel::totalInstalledDeltaFormatted() const {
    return formatBytes(totalInstalledDeltaBytes());
}

QList<PackageOp> UpdatesModel::selectedPackages() const {
    QList<PackageOp> list;
    for (const auto &it : m_packages) {
        if (it.selected) list.append(it.op);
    }
    return list;
}

void UpdatesModel::toggleSelection(int row) {
    if (row >= 0 && row < m_packages.size()) {
        m_packages[row].selected = !m_packages[row].selected;
        QModelIndex idx = index(row, 0);
        emit dataChanged(idx, idx, {SelectedRole});
        emit selectionChanged();
        emit dataChangedTotal();
    }
}

void UpdatesModel::selectAll(bool select) {
    bool changed = false;
    for (int i = 0; i < m_packages.size(); ++i) {
        if (m_packages[i].selected != select) {
            m_packages[i].selected = select;
            changed = true;
        }
    }
    if (changed) {
        emit dataChanged(index(0, 0), index(m_packages.size() - 1, 0), {SelectedRole});
        emit selectionChanged();
        emit dataChangedTotal();
    }
}

} // namespace lut
