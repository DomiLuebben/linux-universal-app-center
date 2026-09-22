#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QSignalSpy>

#include "liblut/catalog/alpm/AlpmPackageCatalog.h"
#include "liblut/catalog/dnf5/Dnf5PackageCatalog.h"
#include "liblut/catalog/apt/AptPackageCatalog.h"
#include "linux-update-tool/catalog/CatalogService.h"
#include "linux-update-tool/catalog/ApplicationStore.h"
#include "linux-update-tool/models/StoreModel.h"
#include <QElapsedTimer>

using namespace lut;

class StoreUiTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testSearchRankingExactMatch();
    void testCategoryFiltering();
    void testCuratedCollections();
    void testActionStateAndInstalledFilter();
    void testPackagesOnlyMode();
    void testDebouncedSearch();
    void test10kBenchmarkPerformance();
    void testDescriptionSanitization();
    void testSafeMediaUrlSchemes();
    void testVersionComparisonMatrix();

private:
    QString m_fixturesDir;

    bool writeTextFile(const QString &path, const QByteArray &content) {
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(content) == content.size();
    }

    struct MockPkg {
        QString name;
        QString ver;
        QString arch = QStringLiteral("x86_64");
        QString desc = QStringLiteral("Test package");
    };

    void createMockSyncDbMulti(const QString &destDbPath, const QList<MockPkg> &pkgs) {
        QTemporaryDir tempArchive;
        QStringList tarArgs = {QStringLiteral("-czf"), destDbPath, QStringLiteral("-C"), tempArchive.path()};
        for (const auto &p : pkgs) {
            const QString pkgDir = tempArchive.path() + QStringLiteral("/%1-%2").arg(p.name, p.ver);
            QDir().mkpath(pkgDir);
            QByteArray descContent = QStringLiteral(
                "%NAME%\n%1\n\n%VERSION%\n%2\n\n%ARCH%\n%3\n\n%DESC%\n%4\n\n%CSIZE%\n1024\n\n%ISIZE%\n4096\n\n"
            ).arg(p.name, p.ver, p.arch, p.desc).toUtf8();
            writeTextFile(pkgDir + QStringLiteral("/desc"), descContent);
            tarArgs.append(QStringLiteral("%1-%2").arg(p.name, p.ver));
        }
        QProcess tar;
        tar.start(QStringLiteral("tar"), tarArgs);
        tar.waitForFinished();
    }

    void createMockInstalledPkg(const QString &localDbDir, const QString &pkgName, const QString &pkgVer,
                                const QStringList &desktopFiles = {}, const QString &arch = QStringLiteral("x86_64")) {
        const QString pkgDir = localDbDir + QStringLiteral("/%1-%2").arg(pkgName, pkgVer);
        QDir().mkpath(pkgDir);

        QByteArray descContent = QStringLiteral(
            "%NAME%\n%1\n\n%VERSION%\n%2\n\n%ARCH%\n%3\n\n%DESC%\nInstalled %1\n\n%ISIZE%\n4096\n\n"
        ).arg(pkgName, pkgVer, arch).toUtf8();
        writeTextFile(pkgDir + QStringLiteral("/desc"), descContent);

        QByteArray filesContent = "%FILES%\n";
        for (const QString &df : desktopFiles) {
            filesContent += "usr/share/applications/" + df.toUtf8() + "\n";
        }
        writeTextFile(pkgDir + QStringLiteral("/files"), filesContent);
    }
};

void StoreUiTest::initTestCase()
{
    m_fixturesDir = QStringLiteral(STORE_FIXTURES_DIR);
    QVERIFY(QDir(m_fixturesDir).exists());
}

void StoreUiTest::testSearchRankingExactMatch()
{
    QTemporaryDir env;
    const QString dbPath = env.path() + QStringLiteral("/pacman");
    const QString syncDb = dbPath + QStringLiteral("/sync");
    QDir().mkpath(syncDb);

    createMockSyncDbMulti(syncDb + QStringLiteral("/extra.db"), {
        { QStringLiteral("kwrite"), QStringLiteral("24.08.0-1") },
        { QStringLiteral("gimp"), QStringLiteral("2.10.38-1") }
    });

    const QString configPath = env.path() + QStringLiteral("/pacman.conf");
    writeTextFile(configPath, "[options]\nArchitecture = x86_64\n[extra]\nServer = file:///dev/null\n");

    AlpmPackageCatalog alpmCatalog(env.path(), dbPath, configPath);
    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(m_fixturesDir);
    QVERIFY(catService.load());

    ApplicationStore store(&catService, &alpmCatalog);
    StoreModel model(&store);

    // Suche nach "kwrite" -> exakter Treffer kwrite muss Rang 1 haben
    model.setSearchQuery(QStringLiteral("kwrite"));
    QVERIFY(model.rowCount() >= 1);
    QCOMPARE(model.data(model.index(0, 0), StoreModel::AppKeyRole).toString(), QStringLiteral("org.kde.kwrite"));

    // Suche nach "Text" -> muss KWrite finden (Beschreibung/Summary)
    model.setSearchQuery(QStringLiteral("Text"));
    QVERIFY(model.rowCount() >= 1);
    QCOMPARE(model.data(model.index(0, 0), StoreModel::AppKeyRole).toString(), QStringLiteral("org.kde.kwrite"));
}

void StoreUiTest::testCategoryFiltering()
{
    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(m_fixturesDir);
    QVERIFY(catService.load());

    ApplicationStore store(&catService, nullptr);
    StoreModel model(&store);

    // KWrite gehört zu Utility / TextEditor -> "Werkzeuge"
    model.setCategory(QStringLiteral("Werkzeuge"));
    QVERIFY(model.rowCount() >= 1);

    // GIMP gehört zu Graphics -> "Grafik"
    model.setCategory(QStringLiteral("Grafik"));
    QVERIFY(model.rowCount() >= 1);
    QCOMPARE(model.data(model.index(0, 0), StoreModel::AppKeyRole).toString(), QStringLiteral("org.gimp.GIMP"));

    // Nicht in Spiele
    model.setCategory(QStringLiteral("Spiele"));
    QCOMPARE(model.rowCount(), 0);

    // Zurück zu Alle
    model.setCategory(QStringLiteral("Alle"));
    QVERIFY(model.rowCount() >= 2);
}

void StoreUiTest::testCuratedCollections()
{
    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(m_fixturesDir);
    QVERIFY(catService.load());

    ApplicationStore store(&catService, nullptr);
    store.loadCuratedCollections(QStringLiteral(PROJECT_DIR) + QStringLiteral("/data/store/curated.json"));

    QVERIFY(!store.rawCuratedCollections().isEmpty());

    StoreModel model(&store);
    model.setCollection(QStringLiteral("kde"));
    // Fixture enthält org.kde.kwrite
    QVERIFY(model.rowCount() >= 1);
    QCOMPARE(model.data(model.index(0, 0), StoreModel::AppKeyRole).toString(), QStringLiteral("org.kde.kwrite"));

    model.setCollection(QStringLiteral("creative"));
    // Fixture enthält org.gimp.GIMP
    QVERIFY(model.rowCount() >= 1);
    QCOMPARE(model.data(model.index(0, 0), StoreModel::AppKeyRole).toString(), QStringLiteral("org.gimp.GIMP"));
}

void StoreUiTest::testActionStateAndInstalledFilter()
{
    QTemporaryDir env;
    const QString dbPath = env.path() + QStringLiteral("/pacman");
    const QString localDb = dbPath + QStringLiteral("/local");
    const QString syncDb = dbPath + QStringLiteral("/sync");
    QDir().mkpath(localDb);
    QDir().mkpath(syncDb);
    writeTextFile(localDb + QStringLiteral("/ALPM_DB_VERSION"), "9\n");

    // Installiere kwrite lokal mit desktop-Datei
    createMockInstalledPkg(localDb, QStringLiteral("kwrite"), QStringLiteral("24.08.0-1"), {QStringLiteral("org.kde.kwrite.desktop")});

    // Beide Pakete in extra.db
    createMockSyncDbMulti(syncDb + QStringLiteral("/extra.db"), {
        { QStringLiteral("kwrite"), QStringLiteral("24.08.0-1") },
        { QStringLiteral("gimp"), QStringLiteral("2.10.38-1") }
    });

    const QString configPath = env.path() + QStringLiteral("/pacman.conf");
    writeTextFile(configPath, "[options]\nArchitecture = x86_64\n[extra]\nServer = file:///dev/null\n");

    AlpmPackageCatalog alpmCatalog(env.path(), dbPath, configPath);
    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(m_fixturesDir);
    QVERIFY(catService.load());

    ApplicationStore store(&catService, &alpmCatalog);
    StoreModel model(&store);

    // Prüfe ActionState
    QCOMPARE(store.getActionState(QStringLiteral("org.gimp.GIMP")), QStringLiteral("Available"));
    QCOMPARE(store.getActionState(QStringLiteral("org.kde.kwrite")), QStringLiteral("Installed"));

    // Filter auf installiert
    model.setInstalledOnly(true);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), StoreModel::AppKeyRole).toString(), QStringLiteral("org.kde.kwrite"));
    QCOMPARE(model.data(model.index(0, 0), StoreModel::IsInstalledRole).toBool(), true);
}

void StoreUiTest::testPackagesOnlyMode()
{
    QTemporaryDir env;
    const QString dbPath = env.path() + QStringLiteral("/pacman");
    const QString syncDb = dbPath + QStringLiteral("/sync");
    QDir().mkpath(syncDb);

    // libsomething hat keinen AppStream AppRecord
    createMockSyncDbMulti(syncDb + QStringLiteral("/extra.db"), {
        { QStringLiteral("libsomething"), QStringLiteral("1.0.0-1"), QStringLiteral("x86_64"), QStringLiteral("Library without appstream") }
    });

    const QString configPath = env.path() + QStringLiteral("/pacman.conf");
    writeTextFile(configPath, "[options]\nArchitecture = x86_64\n[extra]\nServer = file:///dev/null\n");

    AlpmPackageCatalog alpmCatalog(env.path(), dbPath, configPath);
    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(m_fixturesDir);
    QVERIFY(catService.load());

    ApplicationStore store(&catService, &alpmCatalog);
    StoreModel model(&store);

    model.setPackagesOnly(true);
    model.setSearchQuery(QStringLiteral("libsomething"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), StoreModel::NameRole).toString(), QStringLiteral("libsomething"));
    QVERIFY(model.data(model.index(0, 0), StoreModel::SummaryRole).toString().contains(QStringLiteral("Repository-Paket")));
}

void StoreUiTest::testDebouncedSearch()
{
    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(m_fixturesDir);
    QVERIFY(catService.load());

    ApplicationStore store(&catService, nullptr);
    StoreModel model(&store);

    QSignalSpy filterSpy(&model, &StoreModel::filterChanged);
    model.search(QStringLiteral("kwrite"));

    // Vor Ablauf des Timers noch nicht angewendet
    QCOMPARE(filterSpy.count(), 0);

    // Nach Timer (150ms) angewendet
    QVERIFY(filterSpy.wait(300));
    QCOMPARE(model.searchQuery(), QStringLiteral("kwrite"));
}

void StoreUiTest::test10kBenchmarkPerformance()
{
    // Generiere 10.000 synthetische AppRecords (UI-13 Skalierungsprüfung)
    const QStringList sampleCategories = {
        QStringLiteral("AudioVideo"), QStringLiteral("Development"), QStringLiteral("Graphics"),
        QStringLiteral("Network"), QStringLiteral("Office"), QStringLiteral("Science"),
        QStringLiteral("Game"), QStringLiteral("Utility")
    };

    QList<AppRecord> dataset;
    dataset.reserve(10000);
    for (int i = 0; i < 10000; ++i) {
        AppRecord r;
        r.appKey = QStringLiteral("org.example.app%1").arg(i);
        r.componentId = r.appKey;
        r.name = QStringLiteral("Application %1").arg(i);
        r.defaultPackageName = QStringLiteral("app-%1").arg(i);
        r.summary = QStringLiteral("A versatile software tool for daily use number %1").arg(i);
        r.description = QStringLiteral("Full description of application %1 with rich features and tools.").arg(i);
        r.categories = { sampleCategories.at(i % sampleCategories.size()) };
        r.keywords = { QStringLiteral("tool%1").arg(i), QStringLiteral("feature") };
        dataset.append(r);
    }
    QCOMPARE(dataset.size(), 10000);

    // 1. Kategoriefilter-Messung (10.000 Elemente)
    QElapsedTimer catTimer;
    catTimer.start();
    int devCount = 0;
    for (const auto &app : dataset) {
        if (StoreModel::matchesCategory(app.categories, QStringLiteral("Entwicklung"))) {
            devCount++;
        }
    }
    qint64 catTimeMs = catTimer.elapsed();
    qInfo() << "10.000 Elemente Kategoriefilterung:" << catTimeMs << "ms, Treffer:" << devCount;
    QVERIFY(devCount > 1000);
    QVERIFY2(catTimeMs < 50, "Kategoriefilterung über 10.000 Datensätze muss in < 50ms ausgeführt sein");

    // 2. Such-Scoring und Sortierung (10.000 Elemente)
    QElapsedTimer searchTimer;
    searchTimer.start();
    struct Scored {
        const AppRecord *app;
        int score;
    };
    QList<Scored> matches;
    matches.reserve(1000);

    const QString query = QStringLiteral("Application 99");
    for (const auto &app : dataset) {
        int score = StoreModel::calculateSearchScore(app, query);
        if (score >= 0) {
            matches.append({ &app, score });
        }
    }
    std::stable_sort(matches.begin(), matches.end(), [](const Scored &a, const Scored &b) {
        return a.score > b.score;
    });
    qint64 searchTimeMs = searchTimer.elapsed();
    qInfo() << "10.000 Elemente Such-Scoring & Sortierung:" << searchTimeMs << "ms, Treffer:" << matches.size();
    QVERIFY(!matches.isEmpty());
    // Exakter Treffer "Application 99" muss auf Rang 1 mit hohem Score stehen
    QCOMPARE(matches.first().app->name, QStringLiteral("Application 99"));
    QVERIFY(matches.first().score >= 800);
    QVERIFY2(searchTimeMs < 50, "Such-Scoring und Sortierung über 10.000 Datensätze muss in < 50ms ausgeführt sein");
}

void StoreUiTest::testDescriptionSanitization()
{
    // Script-Tags und Inhalt strikt entfernen (UI-07)
    QString xss = QStringLiteral("<p>Safe text</p><script>alert('XSS')</script><b>Bold</b>");
    QString clean = CatalogService::sanitizeDescription(xss);
    QVERIFY(!clean.contains(QStringLiteral("script")));
    QVERIFY(!clean.contains(QStringLiteral("alert")));
    QVERIFY(clean.contains(QStringLiteral("Safe text")));
    QVERIFY(clean.contains(QStringLiteral("Bold")));

    // Style-Tags und Inhalt strikt entfernen
    QString styleHtml = QStringLiteral("<style>body { display: none; }</style><p>Content</p>");
    clean = CatalogService::sanitizeDescription(styleHtml);
    QVERIFY(!clean.contains(QStringLiteral("style")));
    QVERIFY(!clean.contains(QStringLiteral("display")));
    QVERIFY(clean.contains(QStringLiteral("Content")));

    // Listen in Aufzählungspunkte und Absätze in Umbrüche konvertieren
    QString listHtml = QStringLiteral("<p>Intro</p><ul><li>First</li><li>Second</li></ul>");
    clean = CatalogService::sanitizeDescription(listHtml);
    QVERIFY(clean.contains(QStringLiteral("• First")));
    QVERIFY(clean.contains(QStringLiteral("• Second")));

    // HTML-Entities sauber dekodieren
    QString entitiesHtml = QStringLiteral("Rock &amp; Roll &lt;rocks&gt;");
    clean = CatalogService::sanitizeDescription(entitiesHtml);
    QCOMPARE(clean, QStringLiteral("Rock & Roll <rocks>"));
}

void StoreUiTest::testSafeMediaUrlSchemes()
{
    // Erlaubte sichere Schemata (UI-06)
    QVERIFY(CatalogService::isSafeMediaUrl(QStringLiteral("https://flathub.org/repo/app.png")));
    QVERIFY(CatalogService::isSafeMediaUrl(QStringLiteral("http://example.com/screenshot.jpg")));
    QVERIFY(CatalogService::isSafeMediaUrl(QStringLiteral("/usr/share/icons/hicolor/48x48/apps/app.png")));
    QVERIFY(CatalogService::isSafeMediaUrl(QStringLiteral("file:///usr/share/pixmaps/app.png")));
    QVERIFY(CatalogService::isSafeMediaUrl(QStringLiteral("/var/cache/appstream/icons/app.png")));

    // Unsichere / manipulierte Schemata abweisen
    QVERIFY(!CatalogService::isSafeMediaUrl(QStringLiteral("javascript:alert(1)")));
    QVERIFY(!CatalogService::isSafeMediaUrl(QStringLiteral("data:image/png;base64,iVBORw0KGgoAAAANSUhEUg==")));
    QVERIFY(!CatalogService::isSafeMediaUrl(QStringLiteral("file:///etc/shadow")));
    QVERIFY(!CatalogService::isSafeMediaUrl(QStringLiteral("file:///home/user/.bashrc")));
    QVERIFY(!CatalogService::isSafeMediaUrl(QStringLiteral("smb://nas/share/image.png")));
    QVERIFY(!CatalogService::isSafeMediaUrl(QStringLiteral("ftp://files/image.png")));
    QVERIFY(!CatalogService::isSafeMediaUrl(QStringLiteral("")));
    QVERIFY(!CatalogService::isSafeMediaUrl(QStringLiteral("https://bad\nhost/img.png")));
}

void StoreUiTest::testVersionComparisonMatrix()
{
    // 1. DNF5 / RPM Versionsvergleich (CAT-13)
    Dnf5PackageCatalog dnfCatalog;
    // Epoch hat Vorrang vor Version
    QVERIFY(dnfCatalog.compareVersions(QStringLiteral("1:2.0-1.fc44"), QStringLiteral("2.0-1.fc44")) > 0);
    QVERIFY(dnfCatalog.compareVersions(QStringLiteral("2.0-1.fc44"), QStringLiteral("1:2.0-1.fc44")) < 0);
    // Release-Unterschied
    QVERIFY(dnfCatalog.compareVersions(QStringLiteral("2.2.1-4.fc44"), QStringLiteral("2.2.1-3.fc44")) > 0);
    // Mehrstellige Zahl: 1.10.0 > 1.2.0 (kein fehlerhafter lexikographischer Vergleich)
    QVERIFY(dnfCatalog.compareVersions(QStringLiteral("1.10.0-1.fc44"), QStringLiteral("1.2.0-1.fc44")) > 0);
    // Tilde-Präfix ordnet vor regulärer Version ein (2.0~rc1 < 2.0)
    QVERIFY(dnfCatalog.compareVersions(QStringLiteral("2.0~rc1-1.fc44"), QStringLiteral("2.0-1.fc44")) < 0);
    // Identische Versionen
    QCOMPARE(dnfCatalog.compareVersions(QStringLiteral("2.2.1-4.fc44"), QStringLiteral("2.2.1-4.fc44")), 0);

    // 2. APT / Dpkg Versionsvergleich (CAT-13)
    AptPackageCatalog aptCatalog;
    // Epoch hat Vorrang
    QVERIFY(aptCatalog.compareVersions(QStringLiteral("1:2.0-1"), QStringLiteral("2.0-1")) > 0);
    // Debian-Revision
    QVERIFY(aptCatalog.compareVersions(QStringLiteral("2.0-2"), QStringLiteral("2.0-1")) > 0);
    // Mehrstellige Zahl: 1.10.0 > 1.2.0
    QVERIFY(aptCatalog.compareVersions(QStringLiteral("1.10.0-1"), QStringLiteral("1.2.0-1")) > 0);
    // Tilde ordnet davor ein
    QVERIFY(aptCatalog.compareVersions(QStringLiteral("2.0~rc1-1"), QStringLiteral("2.0-1")) < 0);
    // Identisch
    QCOMPARE(aptCatalog.compareVersions(QStringLiteral("2.0-1"), QStringLiteral("2.0-1")), 0);

    // 3. Fallback-Vergleich über ApplicationStore
    QVERIFY(ApplicationStore::compareNativeVersions(QStringLiteral("generic"), QStringLiteral("1.10.0"), QStringLiteral("1.2.0")) > 0);
    QVERIFY(ApplicationStore::compareNativeVersions(QStringLiteral("dnf5"), QStringLiteral("1:2.0-1"), QStringLiteral("2.0-1"), &dnfCatalog) > 0);
}

QTEST_MAIN(StoreUiTest)
#include "store_ui_test.moc"
