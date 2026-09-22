#include "TransactionPlanModel.h"
#include <QLocale>

namespace lut {

TransactionPlanModel::TransactionPlanModel(QObject *parent)
    : QAbstractListModel(parent) {}

int TransactionPlanModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return m_ops.size();
}

QVariant TransactionPlanModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_ops.size())
        return {};

    const auto &op = m_ops.at(index.row());
    switch (role) {
    case IdRole: return op.id;
    case NameRole: return op.name;
    case VersionRole: return op.version;
    case NewVersionRole: return op.newVersion;
    case ArchRole: return op.arch;
    case RepoRole: return op.repo;
    case SummaryRole: return op.summary;
    case KindRole: return PackageOp::kindToString(op.kind);
    case KindEnumRole: return static_cast<int>(op.kind);
    case DownloadSizeRole: return op.downloadSize;
    case DownloadSizeFormattedRole: return formatBytes(op.downloadSize);
    case InstalledSizeRole: return op.installedSize;
    case InstalledSizeFormattedRole: return formatBytes(op.installedSize);
    case IsSecurityRole: return op.isSecurity;
    case IsKernelRole: return op.isKernel;
    case UserRequestedRole: return op.userRequested;
    default: return {};
    }
}

QHash<int, QByteArray> TransactionPlanModel::roleNames() const {
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {VersionRole, "version"},
        {NewVersionRole, "newVersion"},
        {ArchRole, "arch"},
        {RepoRole, "repo"},
        {SummaryRole, "summary"},
        {KindRole, "kind"},
        {KindEnumRole, "kindEnum"},
        {DownloadSizeRole, "downloadSize"},
        {DownloadSizeFormattedRole, "downloadSizeFormatted"},
        {InstalledSizeRole, "installedSize"},
        {InstalledSizeFormattedRole, "installedSizeFormatted"},
        {IsSecurityRole, "isSecurity"},
        {IsKernelRole, "isKernel"},
        {UserRequestedRole, "userRequested"}
    };
}

void TransactionPlanModel::setPlan(const TransactionPlan &plan, const QString &actionTitle) {
    beginResetModel();
    m_ops = plan.ops;
    m_downloadBytes = plan.downloadBytes;
    m_installedDelta = plan.installedSizeDelta;
    m_warnings = plan.warnings;
    m_planRevision = plan.planRevision.isEmpty() ? plan.calculateFingerprint() : plan.planRevision;
    m_hasProtectedConflict = plan.hasProtectedPackageConflict;
    m_affectedApps = plan.affectedApps;
    m_actionTitle = actionTitle;
    endResetModel();
    emit countChanged();
    emit planChanged();
}

void TransactionPlanModel::setOps(const QList<PackageOp> &ops, qint64 downloadBytes, qint64 installedDelta,
                                  const QStringList &warnings, const QString &actionTitle) {
    beginResetModel();
    m_ops = ops;
    m_downloadBytes = downloadBytes;
    m_installedDelta = installedDelta;
    m_warnings = warnings;
    TransactionPlan p;
    p.ops = ops;
    p.downloadBytes = downloadBytes;
    p.installedSizeDelta = installedDelta;
    m_planRevision = p.calculateFingerprint();
    m_hasProtectedConflict = false;
    m_affectedApps.clear();
    m_actionTitle = actionTitle;
    endResetModel();
    emit countChanged();
    emit planChanged();
}

void TransactionPlanModel::clear() {
    beginResetModel();
    m_ops.clear();
    m_downloadBytes = 0;
    m_installedDelta = 0;
    m_warnings.clear();
    m_planRevision.clear();
    m_hasProtectedConflict = false;
    m_affectedApps.clear();
    m_actionTitle.clear();
    endResetModel();
    emit countChanged();
    emit planChanged();
}

int TransactionPlanModel::installCount() const {
    int count = 0;
    for (const auto &op : m_ops) {
        if (op.kind == PackageOp::Kind::Install) count++;
    }
    return count;
}

int TransactionPlanModel::upgradeCount() const {
    int count = 0;
    for (const auto &op : m_ops) {
        if (op.kind == PackageOp::Kind::Upgrade || op.kind == PackageOp::Kind::Downgrade) count++;
    }
    return count;
}

int TransactionPlanModel::removeCount() const {
    int count = 0;
    for (const auto &op : m_ops) {
        if (op.kind == PackageOp::Kind::Remove || op.kind == PackageOp::Kind::Obsolete) count++;
    }
    return count;
}

QString TransactionPlanModel::totalDownloadFormatted() const {
    return formatBytes(m_downloadBytes);
}

QString TransactionPlanModel::totalInstalledDeltaFormatted() const {
    if (m_installedDelta == 0) return QStringLiteral("±0 B");
    QString sign = m_installedDelta > 0 ? QStringLiteral("+") : QStringLiteral("-");
    return sign + formatBytes(std::abs(m_installedDelta));
}

QString TransactionPlanModel::formatBytes(qint64 bytes) {
    if (bytes <= 0) return QStringLiteral("0 B");
    const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    int unitIndex = 0;
    double size = bytes;
    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }
    QLocale locale;
    return QStringLiteral("%1 %2").arg(locale.toString(size, 'f', unitIndex > 0 ? 1 : 0),
                                      QString::fromLatin1(units[unitIndex]));
}

} // namespace lut
