#include "InstalledModel.h"
#include <QThreadPool>
#include <QProcess>
#include <QLocale>
#include "liblut/backend/alpm/AlpmBackend.h"
#include "liblut/detect/DistroDetect.h"
#include <QDebug>

namespace lut {

InstalledModel::InstalledModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_alive(std::make_shared<std::atomic<bool>>(true)) {
    m_threadPool.setMaxThreadCount(1);
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(150); // 150 ms Debounce gemäß Spezifikation
    connect(&m_debounceTimer, &QTimer::timeout, this, &InstalledModel::executeSearch);

    // Initiales Laden
    refresh();
}

InstalledModel::~InstalledModel() {
    m_alive->store(false);
    m_debounceTimer.stop();
    m_threadPool.clear();
    m_threadPool.waitForDone();
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
    m_debounceTimer.stop();
    m_isSearching = true;
    emit isSearchingChanged();
    executeSearch();
}

void InstalledModel::executeSearch() {
    quint64 currentGen = ++m_queryGeneration;
    const QString query = m_pendingQuery;
    auto alive = m_alive;

    m_threadPool.start([this, alive, currentGen, query]() {
        if (!alive->load()) return;

        QString error;
        auto backend = Backend::createForHost(&error);
        auto results = (backend && alive->load()) ? backend->installedPackages(query) : QList<InstalledPackage>{};

        QStringList orphanNames;
        for (const auto &pkg : results) {
            if (pkg.isOrphan) orphanNames.append(pkg.name);
        }
        qint64 cleanableBytes = -1;
        if (alive->load() && DistroDetect::detectFamily() == DistroFamily::Arch) {
            // Unabhängig vom Suchbegriff: "Waisen entfernen" gilt immer allen.
            AlpmBackend alpmBackend;
            orphanNames.clear();
            for (const auto &pkg : alpmBackend.queryOrphans()) orphanNames.append(pkg.name);
            cleanableBytes = alpmBackend.queryCleanableCacheBytes();
        }
        const int orphanCount = orphanNames.size();

        if (!alive->load()) return;

        QMetaObject::invokeMethod(this, [this, alive, currentGen, results, orphanCount, orphanNames, cleanableBytes, error]() {
            if (!alive->load() || currentGen != m_queryGeneration) {
                return; // Veraltete Antwort oder Modell zerstört (Abschnitt 10.1)
            }

            if (!error.isEmpty()) {
                qWarning() << error;
            }

            beginResetModel();
            m_items = results;
            endResetModel();

            m_orphanCount = orphanCount;
            m_orphanNames = orphanNames;
            if (cleanableBytes >= 0) {
                m_cleanableCacheFormatted = formatSize(cleanableBytes);
            } else {
                m_cleanableCacheFormatted = QStringLiteral("unbekannt");
            }

            m_isSearching = false;
            emit isSearchingChanged();
            emit countChanged();
            emit cacheChanged();
        });
    });
}

void InstalledModel::updateHygieneStats() {
    // Hygiene-Statistiken werden bereits asynchron in executeSearch() ermittelt
}

void InstalledModel::cleanOrphans() {
    emit cleanupRequested(QStringLiteral("autoremove"));
}

void InstalledModel::cleanCache() {
    emit cleanupRequested(QStringLiteral("clean"));
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
