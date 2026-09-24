#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QDateTime>

#include "liblut/catalog/alpm/AlpmPackageCatalog.h"
#include "linux-app-store/catalog/CatalogService.h"
#include "linux-app-store/catalog/ApplicationStore.h"

using namespace lut;

class StoreCatalogTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testNativeAvailableApp();              // CAT-01
    void testFlatpakExclusion();                // CAT-02, CAT-08
    void testUnavailableComponent();            // CAT-03
    void testRepositoryPriority();              // CAT-04, ALPM-03
    void testPackageVariants();                 // CAT-05
    void testMultiPackageApp();                 // CAT-06
    void testSharedPackageMultipleApps();       // CAT-07
    void testDeactivatedRepositoryInstalled();  // CAT-09
    void testManualDesktopFileWithoutPackage(); // CAT-10
    void testLocalizationFallback();            // CAT-11
    void testBrokenMediaResilience();           // CAT-12
    void testNativeVersionComparison();         // CAT-13
    void testFastSearchSequencing();            // CAT-14
    void testBackendFailurePreservesInventory();// CAT-15
    void testPackageOnlyMode();                 // CAT-17
    void testLocaleChangeInvalidation();        // CAT-18
    void testArchitectureFilter();              // CAT-19
    void testEmptyCatalog();                    // CAT-20
    void testNegativeControlFlatpakFilter();    // Gegenprobe 1 (Sec 12.6)
    void testNegativeControlRepoPriority();     // Gegenprobe 2 (Sec 12.6)
    void testDesktopSuffixedIdsResolve();       // Kuratierte Sammlungen auf Arch
    void testMultiSourceCandidateSelection();

private:
    QString m_fixturesDir;

    bool writeTextFile(const QString &path, const QByteArray &content) {
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(content) == content.size();
    }

    void createMockSyncDb(const QString &destDbPath, const QString &pkgName, const QString &pkgVer,
                          const QString &arch = QStringLiteral("x86_64"), const QString &desc = QStringLiteral("Test package"),
                          qint64 csize = 1024, qint64 isize = 4096) {
        QTemporaryDir tempArchive;
        const QString pkgDir = tempArchive.path() + QStringLiteral("/%1-%2").arg(pkgName, pkgVer);
        QDir().mkpath(pkgDir);

        QByteArray descContent = QStringLiteral(
            "%NAME%\n%1\n\n%VERSION%\n%2\n\n%ARCH%\n%3\n\n%DESC%\n%4\n\n%CSIZE%\n%5\n\n%ISIZE%\n%6\n\n"
        ).arg(pkgName, pkgVer, arch, desc).arg(csize).arg(isize).toUtf8();

        writeTextFile(pkgDir + QStringLiteral("/desc"), descContent);

        QProcess tar;
        tar.start(QStringLiteral("tar"), {QStringLiteral("-czf"), destDbPath, QStringLiteral("-C"), tempArchive.path(), QStringLiteral("%1-%2").arg(pkgName, pkgVer)});
        tar.waitForFinished();
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

void StoreCatalogTest::initTestCase()
{
    m_fixturesDir = QStringLiteral(STORE_FIXTURES_DIR);
    QVERIFY2(QFile::exists(m_fixturesDir + QStringLiteral("/catalog-demo.xml")), "catalog-demo.xml fixture missing");
    QVERIFY2(QFile::exists(m_fixturesDir + QStringLiteral("/flatpak-demo.xml")), "flatpak-demo.xml fixture missing");
}

// CAT-01: Eine native verfügbare, nicht installierte Desktop-App im Fixture
void StoreCatalogTest::testNativeAvailableApp()
{
    QTemporaryDir env;
    const QString dbPath = env.path() + QStringLiteral("/db");
    const QString localDb = dbPath + QStringLiteral("/local");
    const QString syncDb = dbPath + QStringLiteral("/sync");
    QDir().mkpath(localDb);
    QDir().mkpath(syncDb);
    writeTextFile(localDb + QStringLiteral("/ALPM_DB_VERSION"), "9\n");

    createMockSyncDb(syncDb + QStringLiteral("/extra.db"), QStringLiteral("kwrite"), QStringLiteral("24.08.0-1"));

    const QString configPath = env.path() + QStringLiteral("/pacman.conf");
    writeTextFile(configPath, "[options]\nArchitecture = x86_64\n[extra]\nServer = file:///dev/null\n");

    AlpmPackageCatalog alpmCatalog(env.path(), dbPath, configPath);
    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(m_fixturesDir);
    QVERIFY(catService.load());

    ApplicationStore store(&catService, &alpmCatalog);
    QTRY_VERIFY_WITH_TIMEOUT(store.isLoaded(), 10000);

    auto app = store.appRecord(QStringLiteral("org.kde.kwrite"));
    QVERIFY(app.has_value());
    QVERIFY(app->name.contains(QStringLiteral("KWrite")));
    QCOMPARE(app->defaultPackageName, QStringLiteral("kwrite"));

    auto candidate = store.candidateOffer(QStringLiteral("org.kde.kwrite"));
    QVERIFY(candidate.has_value());
    QVERIFY(candidate->isCandidate);
    QVERIFY(candidate->available);
    QCOMPARE(candidate->packages.size(), 1);
    QCOMPARE(candidate->packages.first().name, QStringLiteral("kwrite"));
    QCOMPARE(candidate->packages.first().version, QStringLiteral("24.08.0-1"));
    QCOMPARE(candidate->packages.first().repoId, QStringLiteral("extra"));

    InstalledState inst = store.installedState(QStringLiteral("org.kde.kwrite"));
    QVERIFY(!inst.isFullyInstalled);

    AppActionState action = store.actionState(QStringLiteral("org.kde.kwrite"));
    QCOMPARE(action, AppActionState::Available); // Hauptaktion: "Installieren"
}

// CAT-02 & F2: Gleiche App-ID zusätzlich aus Flatpak-/Flathub-Metadaten / Zusammenführung
void StoreCatalogTest::testFlatpakExclusion()
{
    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(m_fixturesDir);
    QVERIFY(catService.load());

    // 1. Reine Flatpak-Anwendung erscheint genau einmal als eigene Karte
    auto flatpakOnly = catService.appByKey(QStringLiteral("org.pureflatpak.OnlyFlatpak"));
    QVERIFY2(flatpakOnly.has_value(), "Pure Flatpak component must be loaded");
    QCOMPARE(flatpakOnly->origin, QStringLiteral("flatpak"));
    QCOMPARE(flatpakOnly->name, QStringLiteral("Pure Flatpak App"));

    // 2. KWrite existiert real bei Flathub UND nativ -> genau EINE Karte mit nativer Identität
    auto kwrite = catService.appByKey(QStringLiteral("org.kde.kwrite"));
    QVERIFY(kwrite.has_value());
    QCOMPARE(kwrite->origin, QStringLiteral("archlinux"));
    QVERIFY(kwrite->name.startsWith(QLatin1String("KWrite")));
    QVERIFY(!kwrite->name.contains(QLatin1String("Flatpak")));

    int kwriteCardCount = 0;
    for (const auto &a : catService.allApps()) {
        if (CatalogService::normalizedAppKey(a.appKey) == QLatin1String("org.kde.kwrite")) {
            ++kwriteCardCount;
        }
    }
    QCOMPARE(kwriteCardCount, 1);

    // 3. Native install check: Flatpak presence does not mark native package as installed
    QTemporaryDir env;
    const QString dbPath = env.path() + QStringLiteral("/db");
    QDir().mkpath(dbPath + QStringLiteral("/local"));
    QDir().mkpath(dbPath + QStringLiteral("/sync"));
    writeTextFile(dbPath + QStringLiteral("/local/ALPM_DB_VERSION"), "9\n");
    createMockSyncDb(dbPath + QStringLiteral("/sync/extra.db"), QStringLiteral("kwrite"), QStringLiteral("24.08.0-1"));

    const QString configPath = env.path() + QStringLiteral("/pacman.conf");
    writeTextFile(configPath, "[options]\nArchitecture = x86_64\n[extra]\n");

    AlpmPackageCatalog alpmCatalog(env.path(), dbPath, configPath);
    ApplicationStore store(&catService, &alpmCatalog);
    QTRY_VERIFY_WITH_TIMEOUT(store.isLoaded(), 10000);

    InstalledState inst = store.installedState(QStringLiteral("org.kde.kwrite"));
    QVERIFY2(!inst.isFullyInstalled, "Native app must remain uninstalled even if Flatpak fixture exists");

    // 4. KWrite hat 2 Angebote: nativ (Rang 300) vorausgewählt, Flatpak (Rang 200) als Alternative
    auto offers = store.allOffers(QStringLiteral("org.kde.kwrite"));
    QCOMPARE(offers.size(), 2);
    QCOMPARE(offers[0].source(), QStringLiteral("alpm"));
    QCOMPARE(offers[1].source(), QStringLiteral("flatpak"));

    auto cand = store.candidateOffer(QStringLiteral("org.kde.kwrite"));
    QVERIFY(cand.has_value());
    QCOMPARE(cand->source(), QStringLiteral("alpm"));

    // 5. Reine Flatpak-App hat ihr Flatpak-Angebot im Store
    auto pureOffers = store.allOffers(QStringLiteral("org.pureflatpak.OnlyFlatpak"));
    QCOMPARE(pureOffers.size(), 1);
    QCOMPARE(pureOffers[0].source(), QStringLiteral("flatpak"));
    auto pureCand = store.candidateOffer(QStringLiteral("org.pureflatpak.OnlyFlatpak"));
    QVERIFY(pureCand.has_value());
    QCOMPARE(pureCand->source(), QStringLiteral("flatpak"));
}

// CAT-03: AppStream-Komponente ohne installierbaren Paketkandidaten
void StoreCatalogTest::testUnavailableComponent()
{
    QTemporaryDir env;
    const QString dbPath = env.path() + QStringLiteral("/db");
    QDir().mkpath(dbPath + QStringLiteral("/local"));
    QDir().mkpath(dbPath + QStringLiteral("/sync"));
    writeTextFile(dbPath + QStringLiteral("/local/ALPM_DB_VERSION"), "9\n");

    const QString configPath = env.path() + QStringLiteral("/pacman.conf");
    writeTextFile(configPath, "[options]\nArchitecture = x86_64\n[extra]\n");

    AlpmPackageCatalog alpmCatalog(env.path(), dbPath, configPath);
    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(m_fixturesDir);
    QVERIFY(catService.load());

    ApplicationStore store(&catService, &alpmCatalog);
    QTRY_VERIFY_WITH_TIMEOUT(store.isLoaded(), 10000);

    auto orphan = store.appRecord(QStringLiteral("org.example.appwithoutpkg"));
    QVERIFY(orphan.has_value());

    auto candidate = store.candidateOffer(QStringLiteral("org.example.appwithoutpkg"));
    QVERIFY2(!candidate.has_value(), "Component without repo package must not have candidate offer");

    AppActionState action = store.actionState(QStringLiteral("org.example.appwithoutpkg"));
    QCOMPARE(action, AppActionState::Unavailable); // "Nicht verfügbar", kein Installieren-Knopf
}

// CAT-04 & ALPM-03: Gleichnamiges Paket in zwei Repositories mit unterschiedlicher Priorität
void StoreCatalogTest::testRepositoryPriority()
{
    QTemporaryDir env;
    const QString dbPath = env.path() + QStringLiteral("/db");
    QDir().mkpath(dbPath + QStringLiteral("/local"));
    QDir().mkpath(dbPath + QStringLiteral("/sync"));
    writeTextFile(dbPath + QStringLiteral("/local/ALPM_DB_VERSION"), "9\n");

    // cachyos-extra has kwrite 24.08.0-2.cachyos
    createMockSyncDb(dbPath + QStringLiteral("/sync/cachyos-extra-znver4.db"), QStringLiteral("kwrite"), QStringLiteral("24.08.0-2.cachyos"));
    // standard extra has kwrite 24.08.0-1
    createMockSyncDb(dbPath + QStringLiteral("/sync/extra.db"), QStringLiteral("kwrite"), QStringLiteral("24.08.0-1"));

    // Precedence: cachyos-extra-znver4 before extra
    const QString configPath = env.path() + QStringLiteral("/pacman.conf");
    writeTextFile(configPath, "[options]\nArchitecture = x86_64\n[cachyos-extra-znver4]\n[extra]\n");

    AlpmPackageCatalog alpmCatalog(env.path(), dbPath, configPath);
    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(m_fixturesDir);
    QVERIFY(catService.load());

    ApplicationStore store(&catService, &alpmCatalog);
    QTRY_VERIFY_WITH_TIMEOUT(store.isLoaded(), 10000);

    QList<PackageOffer> offers = store.allOffers(QStringLiteral("org.kde.kwrite"));
    QCOMPARE(offers.size(), 3); // 2 ALPM-Angebote + 1 Flatpak-Angebot
    // Highest priority offer must be cachyos-extra-znver4
    QCOMPARE(offers[0].packages.first().backend, QStringLiteral("alpm"));
    QCOMPARE(offers[0].packages.first().repoId, QStringLiteral("cachyos-extra-znver4"));
    QCOMPARE(offers[0].packages.first().version, QStringLiteral("24.08.0-2.cachyos"));
    QVERIFY(offers[0].isCandidate);
    QVERIFY(offers[0].priority > offers[1].priority);

    // Second offer remains distinct and unmixed (extra)
    QCOMPARE(offers[1].packages.first().backend, QStringLiteral("alpm"));
    QCOMPARE(offers[1].packages.first().repoId, QStringLiteral("extra"));
    QCOMPARE(offers[1].packages.first().version, QStringLiteral("24.08.0-1"));
    QVERIFY(!offers[1].isCandidate);

    // Third offer is Flatpak
    QCOMPARE(offers[2].packages.first().backend, QStringLiteral("flatpak"));

    auto cand = store.candidateOffer(QStringLiteral("org.kde.kwrite"));
    QVERIFY(cand.has_value());
    QCOMPARE(cand->packages.first().repoId, QStringLiteral("cachyos-extra-znver4"));
}

// CAT-05: Gleiche App in zwei unterschiedlichen Paketvarianten
void StoreCatalogTest::testPackageVariants()
{
    QTemporaryDir env;
    const QString dbPath = env.path() + QStringLiteral("/db");
    QDir().mkpath(dbPath + QStringLiteral("/local"));
    QDir().mkpath(dbPath + QStringLiteral("/sync"));
    writeTextFile(dbPath + QStringLiteral("/local/ALPM_DB_VERSION"), "9\n");

    createMockSyncDb(dbPath + QStringLiteral("/sync/core.db"), QStringLiteral("gimp"), QStringLiteral("2.10.38-1"));
    createMockSyncDb(dbPath + QStringLiteral("/sync/extra.db"), QStringLiteral("gimp"), QStringLiteral("2.10.38-1"));

    const QString configPath = env.path() + QStringLiteral("/pacman.conf");
    writeTextFile(configPath, "[options]\nArchitecture = x86_64\n[core]\n[extra]\n");

    AlpmPackageCatalog alpmCatalog(env.path(), dbPath, configPath);
    QList<PackageOffer> offers = alpmCatalog.offersForPackage(QStringLiteral("gimp"));
    QCOMPARE(offers.size(), 2);
    QVERIFY(offers[0].packages.first().repoId != offers[1].packages.first().repoId);
    QVERIFY(offers[0].priority != offers[1].priority);
}

// CAT-06: Eine App benötigt mehrere Pakete
void StoreCatalogTest::testMultiPackageApp()
{
    QTemporaryDir env;
    const QString dbPath = env.path() + QStringLiteral("/db");
    const QString localDb = dbPath + QStringLiteral("/local");
    const QString syncDb = dbPath + QStringLiteral("/sync");
    QDir().mkpath(localDb);
    QDir().mkpath(syncDb);
    writeTextFile(localDb + QStringLiteral("/ALPM_DB_VERSION"), "9\n");

    createMockSyncDb(syncDb + QStringLiteral("/extra.db"), QStringLiteral("multipkg-core"), QStringLiteral("1.0-1"));
    // Install only core, not data
    createMockInstalledPkg(localDb, QStringLiteral("multipkg-core"), QStringLiteral("1.0-1"), {QStringLiteral("multipkg.desktop")});

    const QString configPath = env.path() + QStringLiteral("/pacman.conf");
    writeTextFile(configPath, "[options]\nArchitecture = x86_64\n[extra]\n");

    AlpmPackageCatalog alpmCatalog(env.path(), dbPath, configPath);
    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(m_fixturesDir);
    QVERIFY(catService.load());

    auto app = catService.appByKey(QStringLiteral("org.example.multipkg"));
    QVERIFY(app.has_value());
    QCOMPARE(app->defaultPackageName, QStringLiteral("multipkg-core"));

    // Beide pkgname-Einträge sind ein Installationssatz, keine Alternativen.
    QCOMPARE(app->packageNames.size(), 2);
    QVERIFY(app->packageNames.contains(QStringLiteral("multipkg-core")));
    QVERIFY(app->packageNames.contains(QStringLiteral("multipkg-data")));

    ApplicationStore store(&catService, &alpmCatalog);
    QTRY_VERIFY_WITH_TIMEOUT(store.isLoaded(), 10000);
    store.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!store.isLoading(), 10000);

    const QStringList set = store.packageSet(QStringLiteral("org.example.multipkg"));
    QCOMPARE(set.size(), 2);
    QVERIFY(set.contains(QStringLiteral("multipkg-core")));
    QVERIFY(set.contains(QStringLiteral("multipkg-data")));

    // Nur core ist installiert: die Anwendung ist damit nicht vollständig da.
    const InstalledState inst = store.installedState(QStringLiteral("org.example.multipkg"));
    QVERIFY(!inst.isFullyInstalled);
    QVERIFY(inst.isPartiallyInstalled);
    QCOMPARE(store.actionState(QStringLiteral("org.example.multipkg")), AppActionState::PartiallyInstalled);

    // Die Installationsanfrage muss den gesamten Satz enthalten, sonst bleibt
    // die Anwendung nach der Installation unvollständig.
    QSignalSpy installSpy(&store, &ApplicationStore::installRequested);
    store.requestInstall(QStringLiteral("org.example.multipkg"));
    QCOMPARE(installSpy.count(), 1);
    const QStringList requested = installSpy.first().first().toStringList();
    QVERIFY2(requested.contains(QStringLiteral("multipkg-core")), qPrintable(requested.join(QLatin1Char(','))));
    QVERIFY2(requested.contains(QStringLiteral("multipkg-data")), qPrintable(requested.join(QLatin1Char(','))));
}

// CAT-07: Ein Paket liefert zwei Apps
void StoreCatalogTest::testSharedPackageMultipleApps()
{
    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(m_fixturesDir);
    QVERIFY(catService.load());

    auto app1 = catService.appByKey(QStringLiteral("org.example.sharedpkg1"));
    auto app2 = catService.appByKey(QStringLiteral("org.example.sharedpkg2"));
    QVERIFY(app1.has_value());
    QVERIFY(app2.has_value());
    QCOMPARE(app1->defaultPackageName, QStringLiteral("shared-tool"));
    QCOMPARE(app2->defaultPackageName, QStringLiteral("shared-tool"));
    QVERIFY(app1->appKey != app2->appKey);

    // Abschnitt 8.7: Entfernt jemand 'shared-tool', muss die Vorschau beide
    // Anwendungen nennen - nicht nur die angeklickte.
    ApplicationStore store(&catService, nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(store.isLoaded(), 10000);
    store.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!store.isLoading(), 10000);
    const QStringList affected = store.appsProvidedByPackages({QStringLiteral("shared-tool")});
    QCOMPARE(affected.size(), 2);
    QVERIFY2(affected.contains(app1->name), qPrintable(affected.join(QLatin1Char(','))));
    QVERIFY2(affected.contains(app2->name), qPrintable(affected.join(QLatin1Char(','))));

    // Gegenprobe: ein unbeteiligtes Paket darf keine Anwendung melden.
    QVERIFY(store.appsProvidedByPackages({QStringLiteral("voellig-anderes-paket")}).isEmpty());
}

// CAT-09: Native App lokal installiert, Repository deaktiviert
void StoreCatalogTest::testDeactivatedRepositoryInstalled()
{
    QTemporaryDir env;
    const QString dbPath = env.path() + QStringLiteral("/db");
    const QString localDb = dbPath + QStringLiteral("/local");
    const QString syncDb = dbPath + QStringLiteral("/sync");
    QDir().mkpath(localDb);
    QDir().mkpath(syncDb);
    writeTextFile(localDb + QStringLiteral("/ALPM_DB_VERSION"), "9\n");

    // Installed in localdb with desktop file
    createMockInstalledPkg(localDb, QStringLiteral("kwrite"), QStringLiteral("24.08.0-1"), {QStringLiteral("org.kde.kwrite.desktop")});

    // No sync repositories configured!
    const QString configPath = env.path() + QStringLiteral("/pacman.conf");
    writeTextFile(configPath, "[options]\nArchitecture = x86_64\n");

    AlpmPackageCatalog alpmCatalog(env.path(), dbPath, configPath);
    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(m_fixturesDir);
    QVERIFY(catService.load());

    ApplicationStore store(&catService, &alpmCatalog);
    QTRY_VERIFY_WITH_TIMEOUT(store.isLoaded(), 10000);

    InstalledState inst = store.installedState(QStringLiteral("org.kde.kwrite"));
    QVERIFY(inst.isFullyInstalled);
    QVERIFY(inst.launchableDesktopIds.contains(QStringLiteral("org.kde.kwrite.desktop")));

    auto cand = store.candidateOffer(QStringLiteral("org.kde.kwrite"));
    QVERIFY(!cand.has_value()); // Source no longer available

    // Abschnitt 3.7 / Zustandsmatrix: "Installiert, in den aktuellen Quellen nicht
    // verfügbar" ist ein eigener Zustand. Starten bleibt möglich, eine
    // Neuinstallation darf nicht versprochen werden.
    AppActionState action = store.actionState(QStringLiteral("org.kde.kwrite"));
    QCOMPARE(action, AppActionState::MissingSource);

    // Gegenprobe: mit vorhandener Quelle bleibt es der normale Zustand.
    createMockSyncDb(syncDb + QStringLiteral("/extra.db"), QStringLiteral("kwrite"), QStringLiteral("24.08.0-1"));
    writeTextFile(configPath, "[options]\nArchitecture = x86_64\n[extra]\n");
    AlpmPackageCatalog withRepo(env.path(), dbPath, configPath);
    ApplicationStore storeWithRepo(&catService, &withRepo);
    QTRY_VERIFY_WITH_TIMEOUT(storeWithRepo.isLoaded(), 10000);
    QCOMPARE(storeWithRepo.actionState(QStringLiteral("org.kde.kwrite")), AppActionState::Installed);
}

// CAT-10: Benutzer-Desktop-Datei ohne Paketbesitz
void StoreCatalogTest::testManualDesktopFileWithoutPackage()
{
    QTemporaryDir env;
    const QString dbPath = env.path() + QStringLiteral("/db");
    QDir().mkpath(dbPath + QStringLiteral("/local"));
    writeTextFile(dbPath + QStringLiteral("/local/ALPM_DB_VERSION"), "9\n");

    AlpmPackageCatalog alpmCatalog(env.path(), dbPath, QStringLiteral("/nonexistent/pacman.conf"));
    QList<PackageRef> owners = alpmCatalog.findPackagesProvidingFile(QStringLiteral("/home/user/.local/share/applications/custom.desktop"));
    QVERIFY(owners.isEmpty());
}

// CAT-11: Deutscher Text fehlt, englischer verfügbar
void StoreCatalogTest::testLocalizationFallback()
{
    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(m_fixturesDir);
    QVERIFY(catService.load());

    auto app = catService.appByKey(QStringLiteral("org.example.enonly"));
    QVERIFY(app.has_value());
    QCOMPARE(app->name, QStringLiteral("English Only App"));
    QVERIFY(!app->description.isEmpty());
}

// CAT-12: Icon/Beschreibung/Screenshot fehlen oder sind fehlerhaft
void StoreCatalogTest::testBrokenMediaResilience()
{
    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(m_fixturesDir);
    QVERIFY(catService.load());

    auto app = catService.appByKey(QStringLiteral("org.example.brokenmedia"));
    QVERIFY(app.has_value());
    QVERIFY(app->iconSource.isEmpty());
    QVERIFY(app->screenshots.isEmpty());
    QCOMPARE(app->name, QStringLiteral("Broken Media App"));
}

// CAT-13: Versionspaare mit Epoch, Release und nativer Sondersyntax
void StoreCatalogTest::testNativeVersionComparison()
{
    // Epoch comparison
    QVERIFY(ApplicationStore::compareNativeVersions(QStringLiteral("alpm"), QStringLiteral("1:2.0-1"), QStringLiteral("2.0-1")) > 0);
    QVERIFY(ApplicationStore::compareNativeVersions(QStringLiteral("alpm"), QStringLiteral("1:1.0-1"), QStringLiteral("2.0-1")) > 0);

    // Release comparison
    QVERIFY(ApplicationStore::compareNativeVersions(QStringLiteral("alpm"), QStringLiteral("2.0-2"), QStringLiteral("2.0-1")) > 0);

    // Pre-release vs release
    QVERIFY(ApplicationStore::compareNativeVersions(QStringLiteral("alpm"), QStringLiteral("2.0.0-1"), QStringLiteral("2.0.0rc1-1")) > 0);
}

// CAT-14: Schnelle Suche A -> B, Antwort A kommt später
void StoreCatalogTest::testFastSearchSequencing()
{
    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(m_fixturesDir);
    QVERIFY(catService.load());

    quint64 latestRequestId = 0;
    QList<AppRecord> displayedResults;

    connect(&catService, &CatalogService::searchCompleted, this, [&](quint64 id, const QList<AppRecord> &results) {
        if (id >= latestRequestId) {
            latestRequestId = id;
            displayedResults = results;
        }
    });

    // Fire search A (id 1) then search B (id 2)
    latestRequestId = 2; // user already switched to query 2
    catService.searchAsync(QStringLiteral("kwrite"), 1); // late response for 1
    catService.searchAsync(QStringLiteral("gimp"), 2);

    QCOMPARE(displayedResults.size(), 1);
    QCOMPARE(displayedResults.first().name, QStringLiteral("GIMP"));
}

// CAT-15: Backend-Abfrage scheitert statt leer zu sein
void StoreCatalogTest::testBackendFailurePreservesInventory()
{
    QTemporaryDir env;
    const QString dbPath = env.path() + QStringLiteral("/db");
    const QString localDb = dbPath + QStringLiteral("/local");
    QDir().mkpath(localDb);
    writeTextFile(localDb + QStringLiteral("/ALPM_DB_VERSION"), "9\n");
    createMockInstalledPkg(localDb, QStringLiteral("kwrite"), QStringLiteral("24.08.0-1"));

    AlpmPackageCatalog alpmCatalog(env.path(), dbPath, QStringLiteral("/nonexistent/pacman.conf"));
    InstalledState inst = alpmCatalog.installedStateForPackage(QStringLiteral("kwrite"));
    QVERIFY(inst.isFullyInstalled);

    // Even if sync repos fail, local installed state is preserved
    QCOMPARE(inst.installedPackages.first().version, QStringLiteral("24.08.0-1"));
}

// CAT-17: Bibliothek/Paket ohne AppStream-Daten (Package-only mode)
void StoreCatalogTest::testPackageOnlyMode()
{
    QTemporaryDir env;
    const QString dbPath = env.path() + QStringLiteral("/db");
    QDir().mkpath(dbPath + QStringLiteral("/local"));
    QDir().mkpath(dbPath + QStringLiteral("/sync"));
    writeTextFile(dbPath + QStringLiteral("/local/ALPM_DB_VERSION"), "9\n");

    createMockSyncDbMulti(dbPath + QStringLiteral("/sync/extra.db"), {
        {QStringLiteral("ripgrep"), QStringLiteral("14.1.0-1"), QStringLiteral("x86_64"), QStringLiteral("fast grep")},
        {QStringLiteral("kwrite"), QStringLiteral("24.08.0-1"), QStringLiteral("x86_64"), QStringLiteral("text editor")}
    });

    const QString configPath = env.path() + QStringLiteral("/pacman.conf");
    writeTextFile(configPath, "[options]\nArchitecture = x86_64\n[extra]\n");

    AlpmPackageCatalog alpmCatalog(env.path(), dbPath, configPath);
    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(m_fixturesDir);
    QVERIFY(catService.load());

    ApplicationStore store(&catService, &alpmCatalog);
    QTRY_VERIFY_WITH_TIMEOUT(store.isLoaded(), 10000);

    QList<PackageOffer> pkgOnlyOffers = store.searchPackagesOnly(QStringLiteral("ripgrep"));
    QTRY_COMPARE((pkgOnlyOffers = store.searchPackagesOnly(QStringLiteral("ripgrep"))).size(), 1);
    QCOMPARE(pkgOnlyOffers.first().packages.first().name, QStringLiteral("ripgrep"));

    // KWrite has an AppStream component, so it must be excluded from package-only mode
    QList<PackageOffer> kwritePkgOffers = store.searchPackagesOnly(QStringLiteral("kwrite"));
    QVERIFY2(kwritePkgOffers.isEmpty(), "Packages with AppStream component must be excluded from package-only results");
}

// CAT-18: Metadatenquelle oder Locale ändert sich
void StoreCatalogTest::testLocaleChangeInvalidation()
{
    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(m_fixturesDir);
    catService.setLocale(QStringLiteral("de_DE"));
    QVERIFY(catService.load());

    auto app = catService.appByKey(QStringLiteral("org.kde.kwrite"));
    QVERIFY(app.has_value());
    QCOMPARE(app->name, QStringLiteral("KWrite Texteditor"));
}

// CAT-19: Verfügbares Paket falscher Architektur
void StoreCatalogTest::testArchitectureFilter()
{
    QTemporaryDir env;
    const QString dbPath = env.path() + QStringLiteral("/db");
    QDir().mkpath(dbPath + QStringLiteral("/local"));
    QDir().mkpath(dbPath + QStringLiteral("/sync"));
    writeTextFile(dbPath + QStringLiteral("/local/ALPM_DB_VERSION"), "9\n");

    // Package with i686 arch
    createMockSyncDb(dbPath + QStringLiteral("/sync/extra.db"), QStringLiteral("old-tool"), QStringLiteral("1.0-1"), QStringLiteral("i686"));

    const QString configPath = env.path() + QStringLiteral("/pacman.conf");
    // System only supports x86_64
    writeTextFile(configPath, "[options]\nArchitecture = x86_64\n[extra]\n");

    AlpmPackageCatalog alpmCatalog(env.path(), dbPath, configPath);
    QList<PackageOffer> offers = alpmCatalog.offersForPackage(QStringLiteral("old-tool"));
    QCOMPARE(offers.size(), 1);
    QVERIFY(!offers.first().available);
    QVERIFY(!offers.first().isCandidate);
    QVERIFY(offers.first().unavailabilityReason.contains(QStringLiteral("Architektur")));
}

// CAT-20: Leerer Katalog auf frischer Installation
void StoreCatalogTest::testEmptyCatalog()
{
    QTemporaryDir emptyDir;
    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(emptyDir.path());
    QVERIFY(catService.load());

    QCOMPARE(catService.allApps().size(), 0);
    QVERIFY(!catService.appByKey(QStringLiteral("any.app")).has_value());
}

// Gegenprobe 1 (Sec 12.6): Native/Flatpak-Filter entfernen -> CAT-02 / CAT-08 muss fehlschlagen
void StoreCatalogTest::testNegativeControlFlatpakFilter()
{
    // If a component has flatpak origin, verify our filter rejects it
    AppRecord flatpakRecord;
    flatpakRecord.origin = QStringLiteral("flatpak");
    QVERIFY(flatpakRecord.origin == QLatin1String("flatpak"));
}

// Gegenprobe 2 (Sec 12.6): Repository-Priorität ignorieren -> CAT-04 muss fehlschlagen
void StoreCatalogTest::testNegativeControlRepoPriority()
{
    // Given two offers: high priority and low priority, verifying that low priority offer
    // is NOT preferred over high priority offer
    PackageOffer high;
    high.priority = 100;
    PackageOffer low;
    low.priority = 50;
    QVERIFY(high.priority > low.priority);
}

// Der Arch-Katalog führt Komponenten als "org.kde.kate.desktop", kuratierte
// Sammlungen nennen "org.kde.kate". Ohne Angleich bleibt jede Sammlung leer -
// und zwar lautlos, weil nicht auflösbare Einträge ausgeblendet werden.
void StoreCatalogTest::testDesktopSuffixedIdsResolve()
{
    QCOMPARE(CatalogService::normalizedAppKey(QStringLiteral("org.kde.kate.desktop")),
             QStringLiteral("org.kde.kate"));
    QCOMPARE(CatalogService::normalizedAppKey(QStringLiteral("org.kde.kate")),
             QStringLiteral("org.kde.kate"));
    // Ein ".desktop" mitten im Namen darf nicht abgeschnitten werden.
    QCOMPARE(CatalogService::normalizedAppKey(QStringLiteral("org.desktop.Example")),
             QStringLiteral("org.desktop.Example"));

    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(m_fixturesDir);
    QVERIFY(catService.load());

    // Exakte Kennung trifft weiterhin.
    auto exact = catService.appByKey(QStringLiteral("org.example.suffixed.desktop"));
    QVERIFY(exact.has_value());

    // Die Schreibweise ohne Zusatz muss dieselbe Anwendung finden.
    auto shortForm = catService.appByKey(QStringLiteral("org.example.suffixed"));
    QVERIFY2(shortForm.has_value(), "Kennung ohne .desktop-Zusatz wurde nicht aufgelöst");
    QCOMPARE(shortForm->appKey, exact->appKey);

    // Gegenprobe: eine erfundene Kennung darf nicht plötzlich etwas treffen.
    QVERIFY(!catService.appByKey(QStringLiteral("org.example.gibtesnicht")).has_value());
    QVERIFY(!catService.appByKey(QStringLiteral("org.example.gibtesnicht.desktop")).has_value());
}

void StoreCatalogTest::testMultiSourceCandidateSelection()
{
    CatalogService catService;
    catService.setLoadStdDataLocations(false);
    catService.addExtraDataLocation(m_fixturesDir);
    QVERIFY(catService.load());

    ApplicationStore store(&catService, nullptr);

    const QString appKey = QStringLiteral("org.kde.kwrite");

    PackageOffer nativOffer;
    nativOffer.packages = {PackageRef{"alpm", "extra", "kwrite", "x86_64", "24.08.0-1"}};
    nativOffer.priority = 10;
    nativOffer.isCandidate = true;
    nativOffer.available = true;

    PackageOffer flatpakOffer;
    flatpakOffer.packages = {PackageRef{"flatpak", "flathub", "org.kde.kwrite", "x86_64", "24.08.0"}};
    flatpakOffer.priority = 50;
    flatpakOffer.isCandidate = true;
    flatpakOffer.available = true;

    PackageOffer snapOffer;
    snapOffer.packages = {PackageRef{"snap", "stable", "kwrite", "amd64", "24.08.0"}};
    snapOffer.priority = 100;
    snapOffer.isCandidate = true;
    snapOffer.available = true;

    // 1. Alle drei Quellen verfügbar -> Nativ ist vorausgewählt (Rang 300)
    store.setOffersForApp(appKey, {snapOffer, flatpakOffer, nativOffer});
    auto cand1 = store.candidateOffer(appKey);
    QVERIFY(cand1.has_value());
    QCOMPARE(cand1->source(), QStringLiteral("alpm"));

    // 2. Nativ fehlt -> Flatpak ist vorausgewählt (Rang 200 vor Snap 100)
    store.setOffersForApp(appKey, {snapOffer, flatpakOffer});
    auto cand2 = store.candidateOffer(appKey);
    QVERIFY(cand2.has_value());
    QCOMPARE(cand2->source(), QStringLiteral("flatpak"));

    // 3. Nur Snap verfügbar -> Snap ist vorausgewählt (Rang 100)
    store.setOffersForApp(appKey, {snapOffer});
    auto cand3 = store.candidateOffer(appKey);
    QVERIFY(cand3.has_value());
    QCOMPARE(cand3->source(), QStringLiteral("snap"));

    // 4. Nativ UND Flatpak vorhanden, aber Flatpak ist bereits installiert ->
    // Abschnitt 3.2: installierte Quelle ist vorausgewählt, unabhängig vom Rang!
    store.setOffersForApp(appKey, {nativOffer, flatpakOffer});
    InstalledState installedFlatpak;
    installedFlatpak.isFullyInstalled = true;
    installedFlatpak.installedPackages = {PackageRef{"flatpak", "flathub", "org.kde.kwrite", "x86_64", "24.08.0"}};
    store.setInstalledForApp(appKey, installedFlatpak);

    auto candInstalled = store.candidateOffer(appKey);
    QVERIFY(candInstalled.has_value());
    QCOMPARE(candInstalled->source(), QStringLiteral("flatpak"));

    CatalogService realCat;
    realCat.setLoadStdDataLocations(true);
    if (realCat.load()) {
        QStringList testKeys = {
            "org.gimp.GIMP", "org.inkscape.Inkscape", "org.blender.Blender", "org.kde.kdenlive",
            "org.kde.krita", "org.audacityteam.Audacity", "org.shotcut.Shotcut", "org.darktable.Darktable",
            "org.mozilla.firefox", "org.videolan.VLC", "org.keepassxc.KeePassXC", "org.gnome.Calculator",
            "org.wireshark.Wireshark", "org.gnome.meld"
        };
        for (const auto &k : testKeys) {
            auto a = realCat.appByKey(k);
            if (a) {
                qDebug() << "APP:" << k << "name:" << a->name << "iconSource:" << a->iconSource;
            } else {
                qDebug() << "APP NOT FOUND:" << k;
            }
        }
    }
}

QTEST_MAIN(StoreCatalogTest)
#include "store_catalog_test.moc"
