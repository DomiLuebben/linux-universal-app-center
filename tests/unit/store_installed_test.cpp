#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QThread>
#include <QElapsedTimer>
#include <atomic>

#include "linux-app-store/AppLauncher.h"
#include "linux-app-store/ExternalChangeWatcher.h"
#include "linux-app-store/catalog/CatalogService.h"
#include "linux-app-store/catalog/ApplicationStore.h"
#include "linux-app-store/models/StoreModel.h"
#include "linux-app-store/models/InstalledModel.h"
#ifdef HAVE_ALPM
#include "liblut/catalog/alpm/AlpmPackageCatalog.h"
#endif

using namespace lut;

class SlowCatalog : public PackageCatalog {
public:
    std::atomic<bool> installed{false};
    std::atomic<int> calls{0};
    std::atomic<bool> touchedGui{false};
    void check() { ++calls; if (QThread::currentThread() == QCoreApplication::instance()->thread()) touchedGui = true; }
    quint64 catalogGeneration() const override { return 1; }
    void prepareSnapshot(const QStringList &) override { check(); QThread::msleep(150); }
    QList<PackageOffer> offersForPackage(const QString &name) override {
        check(); if (name != QLatin1String("kwrite")) return {};
        PackageOffer offer; offer.available = true; offer.isCandidate = true;
        offer.packages = {PackageRef{QStringLiteral("apt"), QStringLiteral("stable"), name, QStringLiteral("amd64"), QStringLiteral("1")}};
        return {offer};
    }
    std::optional<PackageOffer> candidateOffer(const QString &name) override {
        auto offers = offersForPackage(name); return offers.isEmpty() ? std::nullopt : std::optional<PackageOffer>(offers.first());
    }
    InstalledState installedStateForPackage(const QString &name) override {
        check(); InstalledState state;
        if (installed && name == QLatin1String("kwrite")) {
            state.isFullyInstalled = true;
            state.installedPackages = {PackageRef{QStringLiteral("apt"), {}, name, QStringLiteral("amd64"), QStringLiteral("1")}};
        }
        return state;
    }
    QList<InstalledPackage> allInstalledPackages() override {
        check(); if (!installed) return {}; InstalledPackage pkg; pkg.name = QStringLiteral("kwrite"); return {pkg};
    }
    QList<PackageRef> findPackagesProvidingFile(const QString &) override { return {}; }
};

class MockBottlesCatalog : public PackageCatalog {
public:
    quint64 catalogGeneration() const override { return 1; }
    QList<PackageOffer> offersForPackage(const QString &name) override {
        if (name != QLatin1String("bottles")) return {};
        PackageOffer offer;
        offer.available = true;
        offer.isCandidate = true;
        offer.packages = {PackageRef{QStringLiteral("alpm"), QStringLiteral("chaotic-aur"), name, QStringLiteral("x86_64"), QStringLiteral("2:67.4-1")}};
        return {offer};
    }
    std::optional<PackageOffer> candidateOffer(const QString &name) override {
        auto offers = offersForPackage(name);
        return offers.isEmpty() ? std::nullopt : std::optional<PackageOffer>(offers.first());
    }
    InstalledState installedStateForPackage(const QString &name) override {
        InstalledState state;
        if (name == QLatin1String("bottles")) {
            state.isFullyInstalled = true;
            state.origin = QStringLiteral("alpm");
            state.installedPackages = {PackageRef{QStringLiteral("alpm"), QStringLiteral("local"), name, QStringLiteral("x86_64"), QStringLiteral("2:67.4-1")}};
            state.launchableDesktopIds = {QStringLiteral("com.usebottles.bottles.desktop")};
        }
        return state;
    }
    QList<InstalledPackage> allInstalledPackages() override {
        InstalledPackage pkg;
        pkg.name = QStringLiteral("bottles");
        pkg.version = QStringLiteral("2:67.4-1");
        return {pkg};
    }
    QList<PackageRef> findPackagesProvidingFile(const QString &path) override {
        if (path.contains(QLatin1String("com.usebottles.bottles.desktop"))) {
            return {PackageRef{QStringLiteral("alpm"), QStringLiteral("local"), QStringLiteral("bottles"), QStringLiteral("x86_64"), QStringLiteral("2:67.4-1")}};
        }
        return {};
    }
};

class StoreInstalledTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testAppLauncherValidDesktop();
    void testAppLauncherRejectFlatpak();
    void testAppLauncherTryExecMissing();
    void testAppLauncherHidden();
    void testInstalledModelAsyncGeneration();
    void testExternalWatcherDebounce();
    void testMultiViewReconciliation();
    void testAsyncSnapshotAndActionStates();
    void testReverseDnsInstalledResolution();
    void testReverseDnsNeverInventsInstallTarget();
    void testRealHostBottlesDetection();

private:
    QTemporaryDir m_tempDir;
};

void StoreInstalledTest::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
}

void StoreInstalledTest::testAppLauncherValidDesktop()
{
    // Erzeuge eine valide native Desktop-Datei mit D-Bus / Exec-Feldcodes (UI-03)
    const QString desktopPath = m_tempDir.filePath(QStringLiteral("test.valid.desktop"));
    QFile f(desktopPath);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&f);
    out << "[Desktop Entry]\n";
    out << "Type=Application\n";
    out << "Name=Test Valide App\n";
    out << "Exec=/bin/true %U\n";
    out << "TryExec=/bin/true\n";
    out << "Icon=utilities-terminal\n";
    out << "Terminal=false\n";
    f.close();
    QFile::setPermissions(desktopPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup);

    // Inspektion
    auto info = AppLauncher::inspectDesktopFile(desktopPath);
    QVERIFY2(info.isValid, qPrintable(info.errorMessage));
    QCOMPARE(info.name, QStringLiteral("Test Valide App"));
    QCOMPARE(info.tryExec, QStringLiteral("/bin/true"));
    QCOMPARE(info.isHidden, false);
    QCOMPARE(info.isFlatpak, false);

    // Auflösung über Pfade
    QString resolved = AppLauncher::resolveDesktopFilePath(QStringLiteral("test.valid.desktop"), { m_tempDir.path() });
    QCOMPARE(resolved, desktopPath);

    // Start über KDE-Frameworks
    AppLauncher launcher;
    QSignalSpy spyStarted(&launcher, &AppLauncher::launchStarted);
    QString errorMsg;
    bool ok = launcher.launchDesktopPath(desktopPath, &errorMsg);
    QVERIFY2(ok, qPrintable(errorMsg));
    QCOMPARE(spyStarted.count(), 1);
}

void StoreInstalledTest::testAppLauncherRejectFlatpak()
{
    // Flatpak-Datei mit X-Flatpak Schlüssel (UI-04)
    const QString flatpakPath1 = m_tempDir.filePath(QStringLiteral("org.test.flatpak1.desktop"));
    {
        QFile f(flatpakPath1);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << "[Desktop Entry]\n";
        out << "Type=Application\n";
        out << "Name=Flatpak App 1\n";
        out << "Exec=/usr/bin/flatpak run org.test.flatpak1\n";
        out << "X-Flatpak=org.test.flatpak1\n";
        f.close();
    }

    QVERIFY(!AppLauncher::isNativeDesktopFile(flatpakPath1));
    auto info1 = AppLauncher::inspectDesktopFile(flatpakPath1); // Default: expectedSource = native
    QVERIFY(!info1.isValid);
    QVERIFY(info1.isFlatpak);

    AppLauncher launcher;
    QSignalSpy spyFailed(&launcher, &AppLauncher::launchFailed);
    QString err;
    bool ok = launcher.launchDesktopPath(flatpakPath1, &err); // Default: expectedSource = native
    QVERIFY(!ok);
    QCOMPARE(spyFailed.count(), 1);

    // Aber für die Quelle "flatpak" ist der Flatpak-Starter gültig und zulässig (Abschnitt 1)
    auto infoFlatpak = AppLauncher::inspectDesktopFile(flatpakPath1, QStringLiteral("flatpak"));
    QVERIFY(infoFlatpak.isValid);
    QVERIFY(infoFlatpak.isFlatpak);

    // Eine native Desktop-Datei wiederum darf niemals als Flatpak akzeptiert werden (Abschnitt 1)
    const QString nativePath = m_tempDir.filePath(QStringLiteral("org.test.native.desktop"));
    {
        QFile f(nativePath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << "[Desktop Entry]\nType=Application\nName=Native App\nExec=/bin/echo hello\n";
    }
    auto infoNativeAsFlatpak = AppLauncher::inspectDesktopFile(nativePath, QStringLiteral("flatpak"));
    QVERIFY(!infoNativeAsFlatpak.isValid);
    QVERIFY(!infoNativeAsFlatpak.isFlatpak);

    // Pfad enthält "/flatpak/"
    const QString flatpakSubdir = m_tempDir.filePath(QStringLiteral("flatpak/exports"));
    QDir().mkpath(flatpakSubdir);
    const QString flatpakPath2 = flatpakSubdir + QStringLiteral("/org.test.flatpak2.desktop");
    {
        QFile f(flatpakPath2);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << "[Desktop Entry]\n";
        out << "Type=Application\n";
        out << "Name=Flatpak App 2\n";
        out << "Exec=/bin/echo hello\n";
        f.close();
    }
    QVERIFY(!AppLauncher::isNativeDesktopFile(flatpakPath2));
    QVERIFY(AppLauncher::isFlatpakDesktopFile(flatpakPath2));
}

void StoreInstalledTest::testAppLauncherTryExecMissing()
{
    // Desktop-Datei mit nicht-existentem TryExec-Programm
    const QString desktopPath = m_tempDir.filePath(QStringLiteral("test.missing_exec.desktop"));
    QFile f(desktopPath);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&f);
    out << "[Desktop Entry]\n";
    out << "Type=Application\n";
    out << "Name=Broken App\n";
    out << "Exec=/usr/bin/not_existing_lut_binary_12345 %f\n";
    out << "TryExec=not_existing_lut_binary_12345\n";
    f.close();

    auto info = AppLauncher::inspectDesktopFile(desktopPath);
    QVERIFY(!info.isValid);
    QVERIFY(info.errorMessage.contains(QLatin1String("TryExec")));

    AppLauncher launcher;
    QString err;
    bool ok = launcher.launchDesktopPath(desktopPath, &err);
    QVERIFY(!ok);
    QVERIFY(err.contains(QLatin1String("TryExec")));
}

void StoreInstalledTest::testAppLauncherHidden()
{
    const QString desktopPath = m_tempDir.filePath(QStringLiteral("test.hidden.desktop"));
    QFile f(desktopPath);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&f);
    out << "[Desktop Entry]\n";
    out << "Type=Application\n";
    out << "Name=Hidden App\n";
    out << "Exec=/bin/true\n";
    out << "Hidden=true\n";
    f.close();

    auto info = AppLauncher::inspectDesktopFile(desktopPath);
    QVERIFY(info.isHidden);

    AppLauncher launcher;
    QString err;
    bool ok = launcher.launchDesktopPath(desktopPath, &err);
    QVERIFY(!ok);
    QVERIFY(err.contains(QLatin1String("Hidden=true")));
}

void StoreInstalledTest::testInstalledModelAsyncGeneration()
{
    InstalledModel model;
    quint64 gen0 = model.queryGeneration();

    // Schnelle Suchen hintereinander
    model.search(QStringLiteral("pkg1"));
    model.search(QStringLiteral("pkg2"));
    model.search(QStringLiteral("pkg3"));

    // Generation muss bei jeder Suche/Refresh monoton steigen
    model.refresh();
    quint64 genAfter = model.queryGeneration();
    QVERIFY(genAfter > gen0);

    // Warte kurz bis der Hintergrund-Worker die Ergebnisse liefert
    QTRY_VERIFY_WITH_TIMEOUT(!model.isSearching(), 3000);
}

void StoreInstalledTest::testExternalWatcherDebounce()
{
    ExternalChangeWatcher watcher;
    watcher.setDebounceInterval(80); // Kurzes Intervall für Test

    const QString testWatchDir = m_tempDir.filePath(QStringLiteral("watch_dir"));
    QDir().mkpath(testWatchDir);
    QVERIFY(watcher.addWatchPath(testWatchDir));

    QSignalSpy spy(&watcher, &ExternalChangeWatcher::databaseChanged);

    // Schreibe 5 Dateien in rascher Folge (simuliert laufendes CLI-Update)
    for (int i = 0; i < 5; ++i) {
        QFile f(testWatchDir + QStringLiteral("/pkg_%1").arg(i));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("change");
        f.close();
    }

    // Direkt nach den Schreibvorgängen darf das Signal noch nicht gefeuert haben (Entprellung)
    QCOMPARE(spy.count(), 0);

    // Nach Ablauf der Entprellzeit muss genau 1 Signal eintreffen
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1500);
}

void StoreInstalledTest::testMultiViewReconciliation()
{
    CatalogService catalog;
    catalog.setLoadStdDataLocations(false);
    catalog.addExtraDataLocation(QStringLiteral(STORE_FIXTURES_DIR));
    QVERIFY(catalog.load());

    ApplicationStore store(&catalog, nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(store.isLoaded(), 10000);
    StoreModel storeModel(&store);
    storeModel.setInstalledOnly(true);

    QSignalSpy spyLoaded(&store, &ApplicationStore::catalogLoaded);

    // Nach Mutation: refresh() aktualisiert alle abhängigen Modelle
    store.refresh();

    QTRY_COMPARE_WITH_TIMEOUT(spyLoaded.count(), 1, 1500);
    QCOMPARE(storeModel.installedOnly(), true);

    // Öffnen einer App über ApplicationStore
    QSignalSpy spyFailed(&store, &ApplicationStore::appLaunchFailed);
    // Unbekannte App schlägt kontrolliert fehl ohne Crash
    bool ok = store.launchApp(QStringLiteral("nonexistent.app"));
    QVERIFY(!ok);
    QCOMPARE(spyFailed.count(), 1);
}

void StoreInstalledTest::testAsyncSnapshotAndActionStates() {
    CatalogService catalog;
    catalog.setLoadStdDataLocations(false);
    catalog.addExtraDataLocation(QStringLiteral(STORE_FIXTURES_DIR));
    QVERIFY(catalog.load());
    SlowCatalog packages;
    int ticks = 0;
    QTimer heartbeat;
    connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; });
    heartbeat.start(10);
    ApplicationStore store(&catalog, &packages);
    StoreModel discover(&store), installed(&store);
    installed.setInstalledOnly(true);
    QVERIFY(store.isLoading());
    QTRY_VERIFY_WITH_TIMEOUT(store.isLoaded(), 3000);
    QVERIFY(ticks >= 5);
    QVERIFY(!packages.touchedGui);
    const int calls = packages.calls;
    for (int i = 0; i < 1000; ++i) {
        store.getActionState(QStringLiteral("org.kde.kwrite"));
        store.getInstalledState(QStringLiteral("org.kde.kwrite"));
        store.getCandidateOffer(QStringLiteral("org.kde.kwrite"));
    }
    QCOMPARE(packages.calls.load(), calls); // rendering never calls a native backend
    const QString key = QStringLiteral("org.kde.kwrite");
    store.updateTransactionStatus(false, false, false, false, false);
    QCOMPARE(store.actionState(key), AppActionState::ActionUnsupported);
    store.updateTransactionStatus(true, true, false, false, false);
    QSignalSpy request(&store, &ApplicationStore::installRequested);
    store.requestInstall(key);
    QCOMPARE(request.count(), 1);
    QCOMPARE(store.actionState(key), AppActionState::PreparingPlan);
    store.updateTransactionStatus(true, true, false, true, false);
    QCOMPARE(store.actionState(key), AppActionState::AwaitingConfirmation);
    QCOMPARE(store.actionState(QStringLiteral("org.gimp.GIMP")), AppActionState::OtherTransactionRunning);
    store.requestInstall(key); QCOMPARE(request.count(), 1);
    store.updateTransactionStatus(true, true, true, false, false);
    store.transactionStarted();
    QCOMPARE(store.actionState(key), AppActionState::Progressing);
    packages.installed = true;
    store.transactionFinished(Result::Success);
    store.updateTransactionStatus(true, true, false, false, false);
    QCOMPARE(store.actionState(key), AppActionState::Reconciling);
    QTRY_VERIFY_WITH_TIMEOUT(!store.isLoading(), 3000);
    QCOMPARE(store.actionState(key), AppActionState::InstalledNoLaunch);
    QCOMPARE(installed.rowCount(), 1);
    store.requestRemove(key);
    store.transactionFinished(Result::Cancelled);
    QTRY_VERIFY_WITH_TIMEOUT(!store.isLoading(), 3000);
    QCOMPARE(store.actionState(key), AppActionState::ErrorOrCancelled);
    QVERIFY(!packages.touchedGui);
}

void StoreInstalledTest::testReverseDnsInstalledResolution()
{
    QTemporaryDir catalogDir;
    QVERIFY(catalogDir.isValid());
    const QString catalogXmlPath = catalogDir.filePath(QStringLiteral("bottles-catalog.xml"));
    QFile f(catalogXmlPath);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&f);
    out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        << "<components origin=\"os\" version=\"0.16\">\n"
        << "  <component type=\"desktop-application\">\n"
        << "    <id>com.usebottles.bottles</id>\n"
        << "    <name>Bottles</name>\n"
        << "    <summary>Run Windows software</summary>\n"
        << "    <launchable type=\"desktop-id\">com.usebottles.bottles.desktop</launchable>\n"
        << "  </component>\n"
        << "</components>\n";
    f.close();

    CatalogService catalog;
    catalog.setLoadStdDataLocations(false);
    catalog.addExtraDataLocation(catalogDir.path());
    QVERIFY(catalog.load());

    // Before snapshot resolution, verify packageNames and defaultPackageName are empty
    auto rawApp = catalog.appByKey(QStringLiteral("com.usebottles.bottles"));
    QVERIFY(rawApp.has_value());
    QVERIFY(rawApp->defaultPackageName.isEmpty());
    QVERIFY(rawApp->packageNames.isEmpty());

    MockBottlesCatalog bottlesCatalog;
    ApplicationStore store(&catalog, &bottlesCatalog);
    QTRY_VERIFY_WITH_TIMEOUT(store.isLoaded(), 5000);

    // After snapshot resolution, verify packageSet was resolved to "bottles"
    const QStringList pkgs = store.packageSet(QStringLiteral("com.usebottles.bottles"));
    QCOMPARE(pkgs, QStringList{QStringLiteral("bottles")});

    // Candidate offer should be found via resolved package name
    auto cand = store.candidateOffer(QStringLiteral("com.usebottles.bottles"));
    QVERIFY(cand.has_value());
    QCOMPARE(cand->packages.first().repoId, QStringLiteral("chaotic-aur"));

    // Installed state must be fully installed
    InstalledState inst = store.installedState(QStringLiteral("com.usebottles.bottles"));
    QVERIFY(inst.isFullyInstalled);
    QCOMPARE(inst.origin, QStringLiteral("alpm"));
    QCOMPARE(inst.installedPackages.first().name, QStringLiteral("bottles"));

    // Action state must be Installed (launchable)
    QCOMPARE(store.actionState(QStringLiteral("com.usebottles.bottles")), AppActionState::Installed);

    // StoreModel in installedOnly mode must contain Bottles
    StoreModel installedModel(&store);
    installedModel.setInstalledOnly(true);
    QCOMPARE(installedModel.count(), 1);
    QCOMPARE(installedModel.get(0).value(QStringLiteral("name")).toString(), QStringLiteral("Bottles"));
    QCOMPARE(installedModel.get(0).value(QStringLiteral("actionState")).toString(), QStringLiteral("Installed"));
    QCOMPARE(installedModel.get(0).value(QStringLiteral("isInstalled")).toBool(), true);

    // Reverse lookup in CatalogService by package name must now find Bottles
    auto resolvedByPkg = catalog.appByPackageName(QStringLiteral("bottles"));
    QVERIFY(resolvedByPkg.has_value());
    QCOMPARE(resolvedByPkg->appKey, QStringLiteral("com.usebottles.bottles"));
}

// Gegenprobe zur Namensableitung: Sie darf ausschließlich Installiertes erkennen.
// Für eine nicht installierte Komponente ohne <pkgname> darf kein gleichnamiges
// Repository-Paket zum Installationsziel erklärt werden - sonst böte der Store
// ein völlig unbeteiligtes Paket unter fremdem Namen und Symbol an.
void StoreInstalledTest::testReverseDnsNeverInventsInstallTarget()
{
    QTemporaryDir catalogDir;
    QVERIFY(catalogDir.isValid());
    QFile f(catalogDir.filePath(QStringLiteral("namensgleich.xml")));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&f);
    out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        << "<components origin=\"os\" version=\"0.16\">\n"
        << "  <component type=\"desktop-application\">\n"
        << "    <id>com.beispiel.bottles</id>\n"
        << "    <name>Bottles</name>\n"
        << "    <summary>Eine andere Anwendung, die zufällig so heißt</summary>\n"
        << "    <launchable type=\"desktop-id\">com.beispiel.bottles.desktop</launchable>\n"
        << "  </component>\n"
        << "</components>\n";
    f.close();

    CatalogService catalog;
    catalog.setLoadStdDataLocations(false);
    catalog.addExtraDataLocation(catalogDir.path());
    QVERIFY(catalog.load());

    // Der Katalog kennt ein verfügbares Paket "bottles", installiert ist es nicht.
    class AvailableOnlyCatalog : public MockBottlesCatalog {
    public:
        InstalledState installedStateForPackage(const QString &) override { return {}; }
        QList<InstalledPackage> allInstalledPackages() override { return {}; }
        QList<PackageRef> findPackagesProvidingFile(const QString &) override { return {}; }
    } packages;

    ApplicationStore store(&catalog, &packages);
    QTRY_VERIFY_WITH_TIMEOUT(store.isLoaded(), 5000);

    QVERIFY2(store.packageSet(QStringLiteral("com.beispiel.bottles")).isEmpty(),
             "Aus dem Anzeigenamen wurde ein Installationsziel erfunden");
    QVERIFY(!store.candidateOffer(QStringLiteral("com.beispiel.bottles")).has_value());
    QCOMPARE(store.actionState(QStringLiteral("com.beispiel.bottles")), AppActionState::Unavailable);
}

void StoreInstalledTest::testRealHostBottlesDetection()
{
#ifdef HAVE_ALPM
    if (!QFile::exists(QStringLiteral("/usr/share/applications/com.usebottles.bottles.desktop"))) {
        QSKIP("Bottles desktop file not found on host, skipping real host detection test.");
    }

    CatalogService catalog;
    catalog.setLoadStdDataLocations(true);
    if (!catalog.load()) {
        QSKIP("Host AppStream catalog could not be loaded.");
    }

    auto appOpt = catalog.appByKey(QStringLiteral("com.usebottles.bottles"));
    if (!appOpt.has_value()) {
        QSKIP("com.usebottles.bottles AppStream metadata not present on host.");
    }

    AlpmPackageCatalog alpmCatalog;
    ApplicationStore store(&catalog, &alpmCatalog);
    QTRY_VERIFY_WITH_TIMEOUT(store.isLoaded(), 15000);

    const QStringList pkgs = store.packageSet(QStringLiteral("com.usebottles.bottles"));
    QVERIFY2(!pkgs.isEmpty(), "packageSet for com.usebottles.bottles should have resolved package name");
    QCOMPARE(pkgs.first(), QStringLiteral("bottles"));

    InstalledState inst = store.installedState(QStringLiteral("com.usebottles.bottles"));
    QVERIFY2(inst.isFullyInstalled, "com.usebottles.bottles should be recognized as fully installed");
    QCOMPARE(inst.origin, QStringLiteral("alpm"));
    QVERIFY(!inst.installedPackages.isEmpty());
    QCOMPARE(inst.installedPackages.first().name, QStringLiteral("bottles"));

    QCOMPARE(store.actionState(QStringLiteral("com.usebottles.bottles")), AppActionState::Installed);

    StoreModel installedModel(&store);
    installedModel.setInstalledOnly(true);
    bool foundInModel = false;
    for (int i = 0; i < installedModel.count(); ++i) {
        if (installedModel.get(i).value(QStringLiteral("appKey")).toString() == QLatin1String("com.usebottles.bottles")) {
            foundInModel = true;
            QCOMPARE(installedModel.get(i).value(QStringLiteral("actionState")).toString(), QStringLiteral("Installed"));
            QCOMPARE(installedModel.get(i).value(QStringLiteral("isInstalled")).toBool(), true);
            break;
        }
    }
    QVERIFY2(foundInModel, "Bottles should appear in StoreModel installedOnly view");
#else
    QSKIP("ALPM support not compiled in.");
#endif
}

QTEST_MAIN(StoreInstalledTest)
#include "store_installed_test.moc"
