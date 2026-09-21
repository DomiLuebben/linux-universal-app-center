#include "InstalledModel.h"
#include <QThreadPool>
#include <QProcess>
#include <QLocale>
#include "liblut/backend/alpm/AlpmBackend.h"

namespace lut {

InstalledModel::InstalledModel(QObject *parent)
    : QAbstractListModel(parent) {
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(150); // 150 ms Debounce gemäß Spezifikation
    connect(&m_debounceTimer, &QTimer::timeout, this, &InstalledModel::executeSearch);

    // Initiales Laden
    refresh();
}

int InstalledModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return m_items.size();
}

QVariant InstalledModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const auto &item = m_items.at(index.row());
    switch (role) {
        case NameRole: return item.name;
        case VersionRole: return item.version;
        case ArchRole: return item.arch;
        case InstalledSizeRole: return item.installedSize;
        case InstalledSizeFormattedRole: return formatSize(item.installedSize);
        case DescriptionRole: return item.description;
        case IsOrphanRole: return item.isOrphan;
        case IsOldKernelRole: return item.isOldKernel;
        default: return {};
    }
}

QHash<int, QByteArray> InstalledModel::roleNames() const {
    return {
        {NameRole, "name"},
        {VersionRole, "version"},
        {ArchRole, "arch"},
        {InstalledSizeRole, "installedSize"},
        {InstalledSizeFormattedRole, "sizeFormatted"},
        {DescriptionRole, "description"},
        {IsOrphanRole, "isOrphan"},
        {IsOldKernelRole, "isOldKernel"}
    };
}

void InstalledModel::search(const QString &query) {
    m_pendingQuery = query;
    m_isSearching = true;
    emit isSearchingChanged();
    m_debounceTimer.start();
}

void InstalledModel::refresh() {
    executeSearch();
    updateHygieneStats();
}

void InstalledModel::executeSearch() {
    AlpmBackend backend;
    auto results = backend.installedPackages(m_pendingQuery);

    beginResetModel();
    m_items = results;
    endResetModel();

    m_isSearching = false;
    emit isSearchingChanged();
    emit countChanged();
}

void InstalledModel::updateHygieneStats() {
    AlpmBackend backend;
    m_orphanCount = backend.queryOrphans().size();
    qint64 cacheBytes = backend.queryCleanableCacheBytes();
    m_cleanableCacheFormatted = formatSize(cacheBytes);

    emit countChanged();
    emit cacheChanged();
}

void InstalledModel::cleanOrphans() {
    emit cleanupStarted(QStringLiteral("Waisenpakete bereinigen"));
    QProcess proc;
    proc.start(QStringLiteral("pacman"), {QStringLiteral("-Qdtq")});
    if (proc.waitForFinished(3000)) {
        QStringList orphans = QString::fromUtf8(proc.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (!orphans.isEmpty()) {
            emit cleanupFinished(QStringLiteral("Waisenpakete gefunden"), true);
        } else {
            emit cleanupFinished(QStringLiteral("Keine Waisenpakete vorhanden"), true);
        }
    }
    refresh();
}

void InstalledModel::cleanCache() {
    emit cleanupStarted(QStringLiteral("Paketcache bereinigen"));
    QProcess proc;
    proc.start(QStringLiteral("paccache"), {QStringLiteral("-rk1")});
    if (!proc.waitForStarted(1000)) {
        proc.start(QStringLiteral("pacman"), {QStringLiteral("-Sc"), QStringLiteral("--noconfirm")});
    }
    proc.waitForFinished(10000);
    emit cleanupFinished(QStringLiteral("Paketcache bereinigt"), true);
    refresh();
}

QString InstalledModel::formatSize(qint64 bytes) {
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    double kb = bytes / 1024.0;
    if (kb < 1024.0) return QStringLiteral("%1 KB").arg(kb, 0, 'f', 1);
    double mb = kb / 1024.0;
    if (mb < 1024.0) return QStringLiteral("%1 MB").arg(mb, 0, 'f', 1);
    double gb = mb / 1024.0;
    return QStringLiteral("%1 GB").arg(gb, 0, 'f', 2);
}

} // namespace lut
