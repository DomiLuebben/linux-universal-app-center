#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>

#include "linux-update-tool/AppLauncher.h"
#include "linux-update-tool/ExternalChangeWatcher.h"
#include "linux-update-tool/catalog/CatalogService.h"
#include "linux-update-tool/catalog/ApplicationStore.h"
#include "linux-update-tool/models/StoreModel.h"
#include "linux-update-tool/models/InstalledModel.h"

using namespace lut;

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
    auto info1 = AppLauncher::inspectDesktopFile(flatpakPath1);
    QVERIFY(!info1.isValid);
    QVERIFY(info1.isFlatpak);

    AppLauncher launcher;
    QSignalSpy spyFailed(&launcher, &AppLauncher::launchFailed);
    QString err;
    bool ok = launcher.launchDesktopPath(flatpakPath1, &err);
    QVERIFY(!ok);
    QCOMPARE(spyFailed.count(), 1);

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

QTEST_MAIN(StoreInstalledTest)
#include "store_installed_test.moc"
