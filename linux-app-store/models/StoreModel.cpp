#include "linux-app-store/models/StoreModel.h"
#include <QRegularExpression>
#include <algorithm>

namespace lut {

StoreModel::StoreModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_debounceTimer.setSingleShot(true);
    connect(&m_debounceTimer, &QTimer::timeout, this, &StoreModel::onDebounceTimeout);
}

StoreModel::StoreModel(ApplicationStore *store, QObject *parent)
    : StoreModel(parent)
{
    setAppStore(store);
}

int StoreModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_items.size();
}

QVariant StoreModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const Item &item = m_items.at(index.row());

    switch (role) {
    case AppKeyRole:
        return item.appKey;
    case NameRole:
        return item.name;
    case SummaryRole:
        return item.summary;
    case DeveloperRole:
        return item.developer;
    case IconSourceRole:
        return item.iconSource;
    case ActionStateRole:
        return item.actionState;
    case IsInstalledRole:
        return item.isInstalled;
    case CategoriesRole:
        return item.categories;
    case OriginRole:
        return item.origin;
    case DefaultPackageNameRole:
        return item.defaultPackageName;
    case RecordRole:
        return item.record;
    case Qt::DisplayRole:
        return item.name;
    default:
        break;
    }

    return {};
}

QHash<int, QByteArray> StoreModel::roleNames() const
{
    static const QHash<int, QByteArray> roles = {
        { AppKeyRole, "appKey" },
        { NameRole, "name" },
        { SummaryRole, "summary" },
        { DeveloperRole, "developer" },
        { IconSourceRole, "iconSource" },
        { ActionStateRole, "actionState" },
        { IsInstalledRole, "isInstalled" },
        { CategoriesRole, "categories" },
        { OriginRole, "origin" },
        { DefaultPackageNameRole, "defaultPackageName" },
        { RecordRole, "record" }
    };
    return roles;
}

ApplicationStore* StoreModel::appStore() const
{
    return m_appStore;
}

void StoreModel::setAppStore(ApplicationStore *store)
{
    if (m_appStore == store) {
        return;
    }

    if (m_appStore) {
        disconnect(m_appStore, nullptr, this, nullptr);
    }

    m_appStore = store;

    if (m_appStore) {
        connect(m_appStore, &ApplicationStore::catalogLoaded, this, &StoreModel::onCatalogLoaded);
        connect(m_appStore, &ApplicationStore::appStateChanged, this, &StoreModel::onAppStateChanged);
    }

    emit appStoreChanged();
    rebuildItems();
}

QString StoreModel::category() const
{
    return m_category;
}

void StoreModel::setCategory(const QString &category)
{
    if (m_category == category) {
        return;
    }
    m_category = category;
    emit filterChanged();
    rebuildItems();
}

QString StoreModel::searchQuery() const
{
    return m_searchQuery;
}

void StoreModel::setSearchQuery(const QString &query)
{
    if (m_searchQuery == query) {
        return;
    }
    m_searchQuery = query;
    m_pendingQuery = query;
    m_debounceTimer.stop();
    emit filterChanged();
    rebuildItems();
}

void StoreModel::search(const QString &query)
{
    if (m_pendingQuery == query) {
        return;
    }
    m_pendingQuery = query;
    m_debounceTimer.start(150); // 150ms Entprellung laut Spezifikation 3.5
}

void StoreModel::onDebounceTimeout()
{
    if (m_searchQuery != m_pendingQuery) {
        m_searchQuery = m_pendingQuery;
        emit filterChanged();
        rebuildItems();
    }
}

QString StoreModel::collection() const
{
    return m_collection;
}

void StoreModel::setCollection(const QString &collection)
{
    if (m_collection == collection) {
        return;
    }
    m_collection = collection;
    emit filterChanged();
    rebuildItems();
}

bool StoreModel::installedOnly() const
{
    return m_installedOnly;
}

void StoreModel::setInstalledOnly(bool installedOnly)
{
    if (m_installedOnly == installedOnly) {
        return;
    }
    m_installedOnly = installedOnly;
    emit filterChanged();
    rebuildItems();
}

bool StoreModel::packagesOnly() const
{
    return m_packagesOnly;
}

void StoreModel::setPackagesOnly(bool packagesOnly)
{
    if (m_packagesOnly == packagesOnly) {
        return;
    }
    m_packagesOnly = packagesOnly;
    emit filterChanged();
    rebuildItems();
}

QString StoreModel::sourceFilter() const
{
    return m_sourceFilter;
}

void StoreModel::setSourceFilter(const QString &source)
{
    if (m_sourceFilter == source) {
        return;
    }
    m_sourceFilter = source;
    emit filterChanged();
    rebuildItems();
}

int StoreModel::count() const
{
    return m_items.size();
}

QVariantMap StoreModel::get(int index) const
{
    if (index < 0 || index >= m_items.size()) {
        return {};
    }
    const Item &item = m_items.at(index);
    QVariantMap map = item.record;
    map[QStringLiteral("appKey")] = item.appKey;
    map[QStringLiteral("name")] = item.name;
    map[QStringLiteral("summary")] = item.summary;
    map[QStringLiteral("developer")] = item.developer;
    map[QStringLiteral("iconSource")] = item.iconSource;
    map[QStringLiteral("actionState")] = item.actionState;
    map[QStringLiteral("isInstalled")] = item.isInstalled;
    map[QStringLiteral("categories")] = item.categories;
    map[QStringLiteral("origin")] = item.origin;
    map[QStringLiteral("defaultPackageName")] = item.defaultPackageName;
    return map;
}

void StoreModel::refresh()
{
    if (m_appStore) {
        m_appStore->refresh();
    }
    rebuildItems();
}

void StoreModel::onCatalogLoaded()
{
    rebuildItems();
}

void StoreModel::onAppStateChanged(const QString &appKey)
{
    if (!m_appStore) return;

    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].appKey == appKey) {
            m_items[i].actionState = m_appStore->getActionState(appKey);
            InstalledState inst = m_appStore->installedState(appKey);
            m_items[i].isInstalled = inst.isFullyInstalled;

            if (inst.isFullyInstalled || inst.isPartiallyInstalled) {
                QStringList installedSources;
                bool hasNative = false;
                bool hasFlatpak = false;
                bool hasSnap = false;
                for (const auto &ref : inst.installedPackages) {
                    if (ref.backend == QLatin1String("flatpak")) hasFlatpak = true;
                    else if (ref.backend == QLatin1String("snap")) hasSnap = true;
                    else hasNative = true;
                }
                if (hasNative) installedSources.append(QStringLiteral("Nativ"));
                if (hasFlatpak) installedSources.append(QStringLiteral("Flatpak"));
                if (hasSnap) installedSources.append(QStringLiteral("Snap"));
                m_items[i].origin = installedSources.join(QStringLiteral(", "));
            }

            emit dataChanged(index(i), index(i), { ActionStateRole, IsInstalledRole, OriginRole });
            break;
        }
    }
}

bool StoreModel::matchesCategory(const QStringList &appCats, const QString &selectedCat)
{
    if (selectedCat.isEmpty() || selectedCat.compare(QLatin1String("all"), Qt::CaseInsensitive) == 0
        || selectedCat.compare(QLatin1String("alle"), Qt::CaseInsensitive) == 0) {
        return true;
    }

    // Direct match (case-insensitive)
    for (const QString &c : appCats) {
        if (c.compare(selectedCat, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    // Standardized mapping: German UI labels & generic terms to XDG AppStream categories
    static const QHash<QString, QStringList> categoryMap = {
        { QStringLiteral("internet"), { QStringLiteral("Network"), QStringLiteral("WebBrowser"), QStringLiteral("Email"), QStringLiteral("Chat"), QStringLiteral("FileTransfer"), QStringLiteral("News"), QStringLiteral("P2P"), QStringLiteral("RemoteAccess"), QStringLiteral("Telephony") } },
        { QStringLiteral("network"), { QStringLiteral("Network"), QStringLiteral("WebBrowser"), QStringLiteral("Email"), QStringLiteral("Chat"), QStringLiteral("FileTransfer"), QStringLiteral("News"), QStringLiteral("P2P"), QStringLiteral("RemoteAccess"), QStringLiteral("Telephony") } },
        { QStringLiteral("büro"), { QStringLiteral("Office"), QStringLiteral("WordProcessor"), QStringLiteral("Spreadsheet"), QStringLiteral("Presentation"), QStringLiteral("Publishing"), QStringLiteral("Viewer"), QStringLiteral("Finance") } },
        { QStringLiteral("office"), { QStringLiteral("Office"), QStringLiteral("WordProcessor"), QStringLiteral("Spreadsheet"), QStringLiteral("Presentation"), QStringLiteral("Publishing"), QStringLiteral("Viewer"), QStringLiteral("Finance") } },
        { QStringLiteral("grafik"), { QStringLiteral("Graphics"), QStringLiteral("2DGraphics"), QStringLiteral("VectorGraphics"), QStringLiteral("RasterGraphics"), QStringLiteral("Photography"), QStringLiteral("Scanning"), QStringLiteral("OCR") } },
        { QStringLiteral("grafik/fotografie"), { QStringLiteral("Graphics"), QStringLiteral("2DGraphics"), QStringLiteral("VectorGraphics"), QStringLiteral("RasterGraphics"), QStringLiteral("Photography"), QStringLiteral("Scanning"), QStringLiteral("OCR") } },
        { QStringLiteral("graphics"), { QStringLiteral("Graphics"), QStringLiteral("2DGraphics"), QStringLiteral("VectorGraphics"), QStringLiteral("RasterGraphics"), QStringLiteral("Photography"), QStringLiteral("Scanning"), QStringLiteral("OCR") } },
        { QStringLiteral("audio & video"), { QStringLiteral("AudioVideo"), QStringLiteral("Audio"), QStringLiteral("Video"), QStringLiteral("Player"), QStringLiteral("Recorder"), QStringLiteral("Music"), QStringLiteral("Midi"), QStringLiteral("Mixer"), QStringLiteral("Sequencer"), QStringLiteral("Tuner"), QStringLiteral("TV") } },
        { QStringLiteral("audio/video"), { QStringLiteral("AudioVideo"), QStringLiteral("Audio"), QStringLiteral("Video"), QStringLiteral("Player"), QStringLiteral("Recorder"), QStringLiteral("Music"), QStringLiteral("Midi"), QStringLiteral("Mixer"), QStringLiteral("Sequencer"), QStringLiteral("Tuner"), QStringLiteral("TV") } },
        { QStringLiteral("audiovideo"), { QStringLiteral("AudioVideo"), QStringLiteral("Audio"), QStringLiteral("Video"), QStringLiteral("Player"), QStringLiteral("Recorder"), QStringLiteral("Music"), QStringLiteral("Midi"), QStringLiteral("Mixer"), QStringLiteral("Sequencer"), QStringLiteral("Tuner"), QStringLiteral("TV") } },
        { QStringLiteral("spiele"), { QStringLiteral("Game"), QStringLiteral("ActionGame"), QStringLiteral("AdventureGame"), QStringLiteral("ArcadeGame"), QStringLiteral("BoardGame"), QStringLiteral("BlocksGame"), QStringLiteral("CardGame"), QStringLiteral("KidsGame"), QStringLiteral("LogicGame"), QStringLiteral("RolePlaying"), QStringLiteral("Shooter"), QStringLiteral("Simulation"), QStringLiteral("SportsGame"), QStringLiteral("StrategyGame") } },
        { QStringLiteral("games"), { QStringLiteral("Game"), QStringLiteral("ActionGame"), QStringLiteral("AdventureGame"), QStringLiteral("ArcadeGame"), QStringLiteral("BoardGame"), QStringLiteral("BlocksGame"), QStringLiteral("CardGame"), QStringLiteral("KidsGame"), QStringLiteral("LogicGame"), QStringLiteral("RolePlaying"), QStringLiteral("Shooter"), QStringLiteral("Simulation"), QStringLiteral("SportsGame"), QStringLiteral("StrategyGame") } },
        { QStringLiteral("game"), { QStringLiteral("Game"), QStringLiteral("ActionGame"), QStringLiteral("AdventureGame"), QStringLiteral("ArcadeGame"), QStringLiteral("BoardGame"), QStringLiteral("BlocksGame"), QStringLiteral("CardGame"), QStringLiteral("KidsGame"), QStringLiteral("LogicGame"), QStringLiteral("RolePlaying"), QStringLiteral("Shooter"), QStringLiteral("Simulation"), QStringLiteral("SportsGame"), QStringLiteral("StrategyGame") } },
        { QStringLiteral("entwicklung"), { QStringLiteral("Development"), QStringLiteral("IDE"), QStringLiteral("Debugger"), QStringLiteral("Building"), QStringLiteral("GUIDesigner"), QStringLiteral("Profiling"), QStringLiteral("RevisionControl"), QStringLiteral("Translation"), QStringLiteral("WebDevelopment") } },
        { QStringLiteral("development"), { QStringLiteral("Development"), QStringLiteral("IDE"), QStringLiteral("Debugger"), QStringLiteral("Building"), QStringLiteral("GUIDesigner"), QStringLiteral("Profiling"), QStringLiteral("RevisionControl"), QStringLiteral("Translation"), QStringLiteral("WebDevelopment") } },
        { QStringLiteral("bildung & wissenschaft"), { QStringLiteral("Education"), QStringLiteral("Science"), QStringLiteral("Astronomy"), QStringLiteral("Biology"), QStringLiteral("Chemistry"), QStringLiteral("ComputerScience"), QStringLiteral("DataVisualization"), QStringLiteral("Economy"), QStringLiteral("Electricity"), QStringLiteral("Geography"), QStringLiteral("Geology"), QStringLiteral("Geoscience"), QStringLiteral("History"), QStringLiteral("Humanities"), QStringLiteral("Math"), QStringLiteral("MedicalSoftware"), QStringLiteral("Physics"), QStringLiteral("Robotics") } },
        { QStringLiteral("bildung/wissenschaft"), { QStringLiteral("Education"), QStringLiteral("Science"), QStringLiteral("Astronomy"), QStringLiteral("Biology"), QStringLiteral("Chemistry"), QStringLiteral("ComputerScience"), QStringLiteral("DataVisualization"), QStringLiteral("Economy"), QStringLiteral("Electricity"), QStringLiteral("Geography"), QStringLiteral("Geology"), QStringLiteral("Geoscience"), QStringLiteral("History"), QStringLiteral("Humanities"), QStringLiteral("Math"), QStringLiteral("MedicalSoftware"), QStringLiteral("Physics"), QStringLiteral("Robotics") } },
        { QStringLiteral("education"), { QStringLiteral("Education"), QStringLiteral("Science"), QStringLiteral("Astronomy"), QStringLiteral("Biology"), QStringLiteral("Chemistry"), QStringLiteral("ComputerScience"), QStringLiteral("DataVisualization"), QStringLiteral("Economy"), QStringLiteral("Electricity"), QStringLiteral("Geography"), QStringLiteral("Geology"), QStringLiteral("Geoscience"), QStringLiteral("History"), QStringLiteral("Humanities"), QStringLiteral("Math"), QStringLiteral("MedicalSoftware"), QStringLiteral("Physics"), QStringLiteral("Robotics") } },
        { QStringLiteral("science"), { QStringLiteral("Education"), QStringLiteral("Science"), QStringLiteral("Astronomy"), QStringLiteral("Biology"), QStringLiteral("Chemistry"), QStringLiteral("ComputerScience"), QStringLiteral("DataVisualization"), QStringLiteral("Economy"), QStringLiteral("Electricity"), QStringLiteral("Geography"), QStringLiteral("Geology"), QStringLiteral("Geoscience"), QStringLiteral("History"), QStringLiteral("Humanities"), QStringLiteral("Math"), QStringLiteral("MedicalSoftware"), QStringLiteral("Physics"), QStringLiteral("Robotics") } },
        { QStringLiteral("werkzeuge"), { QStringLiteral("Utility"), QStringLiteral("System"), QStringLiteral("Archiving"), QStringLiteral("Compression"), QStringLiteral("FileManager"), QStringLiteral("TerminalEmulator"), QStringLiteral("Calculator"), QStringLiteral("Clock"), QStringLiteral("TextEditor") } },
        { QStringLiteral("utility"), { QStringLiteral("Utility"), QStringLiteral("System"), QStringLiteral("Archiving"), QStringLiteral("Compression"), QStringLiteral("FileManager"), QStringLiteral("TerminalEmulator"), QStringLiteral("Calculator"), QStringLiteral("Clock"), QStringLiteral("TextEditor") } },
        { QStringLiteral("utilities"), { QStringLiteral("Utility"), QStringLiteral("System"), QStringLiteral("Archiving"), QStringLiteral("Compression"), QStringLiteral("FileManager"), QStringLiteral("TerminalEmulator"), QStringLiteral("Calculator"), QStringLiteral("Clock"), QStringLiteral("TextEditor") } },
        { QStringLiteral("system"), { QStringLiteral("Utility"), QStringLiteral("System"), QStringLiteral("Archiving"), QStringLiteral("Compression"), QStringLiteral("FileManager"), QStringLiteral("TerminalEmulator"), QStringLiteral("Calculator"), QStringLiteral("Clock"), QStringLiteral("TextEditor") } }
    };

    const QString lowerKey = selectedCat.toLower().trimmed();
    if (categoryMap.contains(lowerKey)) {
        const QStringList &targets = categoryMap.value(lowerKey);
        for (const QString &ac : appCats) {
            for (const QString &t : targets) {
                if (ac.compare(t, Qt::CaseInsensitive) == 0) {
                    return true;
                }
            }
        }
    }

    return false;
}

int StoreModel::calculateSearchScore(const AppRecord &app, const QString &query)
{
    const QString q = query.trimmed().toLower();
    if (q.isEmpty()) {
        return 0;
    }

    const QString appKeyLower = app.appKey.toLower();
    const QString pkgLower = app.defaultPackageName.toLower();
    const QString nameLower = app.name.toLower();

    // 1. Exakter App-Key oder Paketname
    if (appKeyLower == q || pkgLower == q) {
        return 1000;
    }

    // 2. Exakter App-Name
    if (nameLower == q) {
        return 900;
    }

    // 3. Name fängt mit Suchbegriff an
    if (nameLower.startsWith(q)) {
        return 800;
    }

    // 4. Wort im Namen fängt mit Suchbegriff an
    int wordIdx = nameLower.indexOf(q);
    while (wordIdx > 0) {
        const QChar prev = nameLower[wordIdx - 1];
        if (prev.isSpace() || prev == QLatin1Char('-') || prev == QLatin1Char('_') || prev == QLatin1Char('.')) {
            return 700;
        }
        wordIdx = nameLower.indexOf(q, wordIdx + 1);
    }

    // 5. Paketname fängt mit Suchbegriff an
    if (pkgLower.startsWith(q)) {
        return 650;
    }

    // 6. Keywords
    for (const QString &kw : app.keywords) {
        if (kw.compare(q, Qt::CaseInsensitive) == 0) {
            return 600;
        }
        if (kw.startsWith(q, Qt::CaseInsensitive)) {
            return 500;
        }
    }

    // 7. Name enthält Suchbegriff
    if (nameLower.contains(q)) {
        return 400;
    }

    // 8. Summary enthält Suchbegriff
    if (app.summary.contains(q, Qt::CaseInsensitive)) {
        return 300;
    }

    // 9. Description enthält Suchbegriff
    if (app.description.contains(q, Qt::CaseInsensitive)) {
        return 100;
    }

    return -1; // Kein Treffer
}

void StoreModel::rebuildItems()
{
    beginResetModel();
    m_items.clear();

    if (!m_appStore) {
        endResetModel();
        emit countChanged();
        return;
    }

    if (m_packagesOnly) {
        // Modus: "Alle Pakete" (Abschnitt 3.5 & CAT-17)
        QList<PackageOffer> offers = m_appStore->searchPackagesOnly(m_searchQuery);
        for (const auto &offer : offers) {
            if (offer.packages.isEmpty()) continue;
            const auto &pkg = offer.packages.first();

            Item item;
            item.appKey = QStringLiteral("pkg:%1").arg(pkg.name);
            item.name = pkg.name;
            item.summary = QStringLiteral("Repository-Paket: %1 (%2)").arg(pkg.repoId, pkg.version);
            item.developer = pkg.repoId;
            item.iconSource = QStringLiteral("package-x-generic");
            item.defaultPackageName = pkg.name;
            item.origin = pkg.backend;

            InstalledState inst = m_appStore->installedState(item.appKey);
            item.isInstalled = inst.isFullyInstalled;
            item.actionState = m_appStore->getActionState(item.appKey);

            QVariantMap record;
            record[QStringLiteral("name")] = pkg.name;
            record[QStringLiteral("version")] = pkg.version;
            record[QStringLiteral("repoId")] = pkg.repoId;
            record[QStringLiteral("backend")] = pkg.backend;
            record[QStringLiteral("isPackageOnly")] = true;
            item.record = record;

            if (m_installedOnly && !item.isInstalled) {
                continue;
            }

            m_items.append(item);
        }
        endResetModel();
        emit countChanged();
        return;
    }

    QList<AppRecord> candidateApps;

    if (!m_collection.isEmpty()) {
        // Sammlungsmodus
        const auto rawCols = m_appStore->rawCuratedCollections();
        for (const auto &col : rawCols) {
            if (col.id == m_collection) {
                for (const QString &appId : col.appIds) {
                    auto app = m_appStore->appRecord(appId);
                    if (app.has_value()) {
                        candidateApps.append(*app);
                    }
                }
                break;
            }
        }
    } else {
        candidateApps = m_appStore->allApps();
    }

    // Filter anwenden: Kategorie & Installiert & Quelle (Abschnitt 8.3)
    QList<AppRecord> filteredApps;
    for (const auto &app : candidateApps) {
        if (!m_category.isEmpty() && !matchesCategory(app.categories, m_category)) {
            continue;
        }

        InstalledState inst = m_appStore->installedState(app.appKey);
        if (m_installedOnly && !inst.isFullyInstalled && !inst.isPartiallyInstalled) {
            continue;
        }

        if (!m_sourceFilter.isEmpty() && m_sourceFilter != QLatin1String("all") && m_sourceFilter != QLatin1String("alle")) {
            const QString sf = m_sourceFilter.toLower();
            const auto offers = m_appStore->allOffers(app.appKey);
            bool matches = false;
            for (const auto &o : offers) {
                const QString s = o.source().toLower();
                if (sf == QLatin1String("flatpak") && s == QLatin1String("flatpak")) {
                    matches = true; break;
                } else if (sf == QLatin1String("snap") && s == QLatin1String("snap")) {
                    matches = true; break;
                } else if (sf == QLatin1String("native") && (s == QLatin1String("alpm") || s == QLatin1String("dnf5") || s == QLatin1String("apt") || s.isEmpty())) {
                    matches = true; break;
                }
            }
            if (!matches && offers.isEmpty()) {
                if (sf == QLatin1String("flatpak") && app.origin == QLatin1String("flatpak")) matches = true;
                else if (sf == QLatin1String("native") && app.origin != QLatin1String("flatpak") && app.origin != QLatin1String("snap")) matches = true;
            }
            if (!matches) {
                continue;
            }
        }

        filteredApps.append(app);
    }

    // Suche & Ranking (Abschnitt 3.5)
    struct ScoredApp {
        AppRecord app;
        int score = 0;
    };

    QList<ScoredApp> scoredList;
    const bool hasSearch = !m_searchQuery.trimmed().isEmpty();

    for (const auto &app : filteredApps) {
        int score = 0;
        if (hasSearch) {
            score = calculateSearchScore(app, m_searchQuery);
            if (score < 0) {
                continue; // Kein Treffer
            }
        }
        scoredList.append({ app, score });
    }

    if (hasSearch) {
        // Sortieren nach Score absteigend, bei Gleichstand alphabetisch nach Name
        std::stable_sort(scoredList.begin(), scoredList.end(), [](const ScoredApp &a, const ScoredApp &b) {
            if (a.score != b.score) {
                return a.score > b.score;
            }
            return QString::compare(a.app.name, b.app.name, Qt::CaseInsensitive) < 0;
        });
    }

    // Item-Liste aufbauen
    for (const auto &entry : scoredList) {
        const auto &app = entry.app;
        Item item;
        item.appKey = app.appKey;
        item.name = app.name;
        item.summary = app.summary;
        item.developer = app.developer;
        item.iconSource = app.iconSource;
        item.categories = app.categories;
        item.defaultPackageName = app.defaultPackageName;
        item.actionState = m_appStore->getActionState(app.appKey);

        InstalledState inst = m_appStore->installedState(app.appKey);
        item.isInstalled = inst.isFullyInstalled;

        if (inst.isFullyInstalled || inst.isPartiallyInstalled) {
            QStringList installedSources;
            bool hasNative = false;
            bool hasFlatpak = false;
            bool hasSnap = false;
            for (const auto &ref : inst.installedPackages) {
                if (ref.backend == QLatin1String("flatpak")) hasFlatpak = true;
                else if (ref.backend == QLatin1String("snap")) hasSnap = true;
                else hasNative = true;
            }
            if (hasNative) installedSources.append(QStringLiteral("Nativ"));
            if (hasFlatpak) installedSources.append(QStringLiteral("Flatpak"));
            if (hasSnap) installedSources.append(QStringLiteral("Snap"));
            item.origin = installedSources.join(QStringLiteral(", "));
        } else if (app.origin == QLatin1String("flatpak")) {
            item.origin = QStringLiteral("Flatpak");
        } else if (app.origin == QLatin1String("snap")) {
            item.origin = QStringLiteral("Snap");
        } else {
            item.origin = QString();
        }

        item.record = app.toJson().toVariantMap();
        item.searchScore = entry.score;

        m_items.append(item);
    }

    endResetModel();
    emit countChanged();
}

} // namespace lut
