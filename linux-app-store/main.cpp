#include "media/MediaCache.h"
#include <QApplication>
#include <QQuickStyle>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QImage>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QIcon>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QLibraryInfo>
#include "DaemonClient.h"
#include "AurUpdates.h"
#include "AppSettings.h"
#include "models/FlatpakUpdates.h"
#include "models/SnapUpdates.h"
#include "models/PacstallUpdates.h"
#include "theme/SystemPalette.h"
#include "catalog/CatalogService.h"
#include "catalog/ApplicationStore.h"
#include "models/StoreModel.h"
#include "ExternalChangeWatcher.h"
#include "TrayManager.h"
#include "OperationMonitor.h"
#include "liblut/liblut.h"
#include "liblut/repository/RepoManager.h"

#ifdef HAVE_ALPM
#include "liblut/catalog/alpm/AlpmPackageCatalog.h"
#endif
#include "liblut/catalog/dnf5/Dnf5PackageCatalog.h"
#include "liblut/catalog/apt/AptPackageCatalog.h"
#include "liblut/catalog/flatpak/FlatpakPackageCatalog.h"
#include "liblut/catalog/snap/SnapPackageCatalog.h"
#include "liblut/backend/snap/SnapAvailability.h"
#include "liblut/detect/DistroDetect.h"

namespace {

// --check-qml sammelt QML-Warnungen (Bindungsschleifen, unbekannte Eigenschaften),
// damit der Prüfstand nicht nur "geladen" sondern "fehlerfrei" bestätigt.
bool g_collectQmlWarnings = false;
QStringList g_qmlWarnings;
QtMessageHandler g_previousHandler = nullptr;

void qmlWarningCollector(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    if (g_collectQmlWarnings && (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)) {
        if (!context.category || qstrcmp(context.category, "kf.kirigami.platform") != 0) {
            g_qmlWarnings.append(msg);
        }
    }
    if (g_previousHandler) {
        g_previousHandler(type, context, msg);
    } else {
        // qInstallMessageHandler liefert nullptr, wenn vorher der Standard-Handler aktiv war.
        // Ohne diesen Zweig würden alle Meldungen stillschweigend verschwinden.
        fprintf(stderr, "%s\n", qPrintable(msg));
        fflush(stderr);
    }
}

// Alle Seiten liegen hinter StackView-Components und werden beim Laden von Main.qml
// NICHT instanziiert. Der Smoketest muss sie deshalb selbst erzeugen.
const QStringList &checkablePages() {
    static const QStringList pages = {
        QStringLiteral("qrc:/LinuxAppStore/qml/pages/Discover.qml"),
        QStringLiteral("qrc:/LinuxAppStore/qml/pages/Search.qml"),
        QStringLiteral("qrc:/LinuxAppStore/qml/pages/AppDetails.qml"),
        QStringLiteral("qrc:/LinuxAppStore/qml/pages/Updates.qml"),
        QStringLiteral("qrc:/LinuxAppStore/qml/pages/Manage.qml"),
        QStringLiteral("qrc:/LinuxAppStore/qml/components/AppTile.qml"),
        QStringLiteral("qrc:/LinuxAppStore/qml/pages/PlanPreview.qml"),
        QStringLiteral("qrc:/LinuxAppStore/qml/components/OperationProgressView.qml"),
        QStringLiteral("qrc:/LinuxAppStore/qml/pages/Report.qml"),
        QStringLiteral("qrc:/LinuxAppStore/qml/pages/Installed.qml"),
        QStringLiteral("qrc:/LinuxAppStore/qml/pages/History.qml"),
        QStringLiteral("qrc:/LinuxAppStore/qml/pages/Logs.qml"),
        QStringLiteral("qrc:/LinuxAppStore/qml/pages/Settings.qml"),
    };
    return pages;
}

// Bis 1.7.0 hieß die App "Linux App Store": Einstellungen (AUR-/Pacstall-Freigabe)
// und Caches (AUR-Klone samt Baumarkierung, Bildercache) lagen unter
// LinuxAppStore/linux-app-store. Einmalig umziehen, statt sie zu verlieren.
void migrateUserDataFromLinuxAppStore() {
    for (const auto location : {QStandardPaths::AppConfigLocation, QStandardPaths::CacheLocation}) {
        const QString current = QStandardPaths::writableLocation(location);
        QString previous = current;
        previous.replace(QStringLiteral("LinuxUniversalAppCenter/linux-universal-app-center"),
                         QStringLiteral("LinuxAppStore/linux-app-store"));
        if (previous == current || !QFileInfo(previous).isDir()) continue;
        // Eintragsweise: das neue Verzeichnis kann schon existieren (leer angelegt
        // von AppSettings oder mit qmlcache aus einem Prüflauf). Was dort fehlt,
        // zieht um; was schon da ist, bleibt – nichts wird überschrieben.
        QDir().mkpath(current);
        const QDir from(previous);
        for (const QString &entry : from.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)) {
            const QString target = QDir(current).filePath(entry);
            if (QFileInfo::exists(target)) {
                // Leere Ordner legt schon ein Prüflauf an (etwa store-media):
                // die zählen nicht als vorhanden, sonst bliebe der alte Inhalt liegen.
                if (!QFileInfo(target).isDir() || !QDir(target).isEmpty() || !QDir().rmdir(target)) continue;
            }
            if (!QDir().rename(from.filePath(entry), target)) {
                qWarning().noquote() << "Umzug der Benutzerdaten fehlgeschlagen:" << from.filePath(entry) << "->" << target;
            }
        }
        // Nur leere Verzeichnisse entfernen; Reste (etwa ein alter qmlcache) bleiben.
        if (QDir().rmdir(previous)) QDir().rmdir(QFileInfo(previous).path());
    }
}

int instantiateAllPages(QQmlApplicationEngine &engine) {
    int failures = 0;
    // Eingebettete Dateien, ohne die Entdecken leer bleibt bzw. das Symbol fehlt.
    // Geprüft wird der Ressourcenpfad selbst, nicht irgendein Fallback im Dateisystem.
    for (const QString &resource : {QStringLiteral(":/LinuxAppStore/store/curated.json"),
                                    QStringLiteral(":/LinuxAppStore/icons/org.linuxuniversalappcenter.svg")}) {
        if (!QFile::exists(resource)) {
            qCritical().noquote() << "Eingebettete Ressource fehlt:" << resource;
            ++failures;
        }
    }
    for (const QString &page : checkablePages()) {
        QQmlComponent component(&engine, QUrl(page));
        if (component.isError()) {
            qCritical().noquote() << "QML-Fehler in" << page << ":" << component.errorString().trimmed();
            ++failures;
            continue;
        }
        QScopedPointer<QObject> obj(component.create(engine.rootContext()));
        if (!obj) {
            qCritical().noquote() << "QML-Instanziierung fehlgeschlagen:" << page
                                  << component.errorString().trimmed();
            ++failures;
        }
    }
    return failures;
}

} // namespace

int main(int argc, char *argv[]) {
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        const QString kdeStyle = QLibraryInfo::path(QLibraryInfo::QmlImportsPath) + QStringLiteral("/org/kde/desktop/qmldir");
        QQuickStyle::setStyle(QFileInfo::exists(kdeStyle) ? QStringLiteral("org.kde.desktop") : QStringLiteral("Fusion"));
    }

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("linux-universal-app-center"));
    app.setApplicationDisplayName(QStringLiteral("Linux Universal App Center"));
    app.setApplicationVersion(lut::versionString());
    app.setOrganizationName(QStringLiteral("LinuxUniversalAppCenter"));
    app.setOrganizationDomain(QStringLiteral("linuxuniversalappcenter.org"));
    // Wayland ordnet Fenster und Symbol über die Desktop-ID zu.
    QGuiApplication::setDesktopFileName(QStringLiteral("org.linuxuniversalappcenter"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("org.linuxuniversalappcenter")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Linux Universal App Center"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption replayOption(
        QStringList() << QStringLiteral("r") << QStringLiteral("replay"),
        QStringLiteral("Run in replay mode using a .jsonl fixture file."),
        QStringLiteral("fixture")
    );
    parser.addOption(replayOption);

    QCommandLineOption speedOption(
        QStringList() << QStringLiteral("s") << QStringLiteral("speed"),
        QStringLiteral("Replay speed factor (default 1.0)."),
        QStringLiteral("speed"),
        QStringLiteral("1.0")
    );
    parser.addOption(speedOption);

    QCommandLineOption checkQmlOption(
        QStringList() << QStringLiteral("check-qml"),
        QStringLiteral("Test loading QML components and exit immediately (0 on success, -1 on failure).")
    );
    parser.addOption(checkQmlOption);

    // Rendert das Hauptfenster in eine PNG-Datei und beendet sich. Gedacht für
    // die Abnahme von Layout und Farbschema, die kein Testlauf sichtbar macht.
    QCommandLineOption screenshotOption(
        QStringList() << QStringLiteral("screenshot"),
        QStringLiteral("Render the main window to a PNG file and exit."),
        QStringLiteral("path")
    );
    parser.addOption(screenshotOption);

    QCommandLineOption trayOption(
        QStringList() << QStringLiteral("t") << QStringLiteral("tray"),
        QStringLiteral("Start minimized to the system tray.")
    );
    parser.addOption(trayOption);

    parser.process(app);

    bool checkQml = parser.isSet(checkQmlOption);
    bool startInTray = parser.isSet(trayOption);
    if (checkQml) {
        g_collectQmlWarnings = true;
        g_previousHandler = qInstallMessageHandler(qmlWarningCollector);
    }
    const QString screenshotPath = parser.value(screenshotOption);
    QString replayFixture = parser.value(replayOption);
    double replaySpeed = parser.value(speedOption).toDouble();
    if (replaySpeed <= 0.0) replaySpeed = 1.0;

    // Nur beim echten Start: Prüfläufe (--check-qml in verify-all.sh und
    // makepkg check(), Replay, Bildschirmfotos) dürfen die Benutzerdaten der
    // installierten Fassung nicht wegziehen.
    if (!checkQml && screenshotPath.isEmpty() && replayFixture.isEmpty()) {
        migrateUserDataFromLinuxAppStore();
    }

    // Im Hintergrund weiterlaufen, wenn ein System-Tray verfügbar ist
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        app.setQuitOnLastWindowClosed(false);
    }

    if (qEnvironmentVariableIsSet("LUT_FORCE_DISTRO_FAMILY")) {
        const QString forced = qEnvironmentVariable("LUT_FORCE_DISTRO_FAMILY").toLower();
        if (forced == QLatin1String("arch")) {
            lut::RepoManager::instance().setForcedDistroFamily(lut::DistroFamily::Arch);
        } else if (forced == QLatin1String("fedora")) {
            lut::RepoManager::instance().setForcedDistroFamily(lut::DistroFamily::Fedora);
            const QString osRel = qEnvironmentVariable("LUT_OS_RELEASE");
            if (!osRel.isEmpty() && QFile::exists(osRel)) {
                lut::RepoManager::instance().setOsReleasePath(osRel);
            }
        } else if (forced == QLatin1String("debian")) {
            lut::RepoManager::instance().setForcedDistroFamily(lut::DistroFamily::Debian);
            const QString osRel = qEnvironmentVariable("LUT_OS_RELEASE");
            if (!osRel.isEmpty() && QFile::exists(osRel)) {
                lut::RepoManager::instance().setOsReleasePath(osRel);
            }
        }
    }

    auto *palette = lut::SystemPalette::instance();
    auto *appSettings = new lut::AppSettings(QString(), &app);
    auto *client = new lut::DaemonClient(&app);
    auto *aur = new lut::AurUpdates(&app);
    aur->setAppSettings(appSettings);
    auto *pacstallUpdates = new lut::PacstallUpdates(&app);
    pacstallUpdates->setAppSettings(appSettings);
    auto *flatpakUpdates = new lut::FlatpakUpdates(&app);
    auto *snapUpdates = new lut::SnapUpdates(&app);

    aur->setDaemonClient(client);
    flatpakUpdates->setDaemonClient(client);
    snapUpdates->setDaemonClient(client);

    const auto syncExternalBusy = [client, aur, pacstallUpdates, flatpakUpdates, snapUpdates]() {
        bool extBusy = (aur && aur->busy()) ||
                       (pacstallUpdates && pacstallUpdates->busy()) ||
                       (flatpakUpdates && flatpakUpdates->busy()) ||
                       (snapUpdates && snapUpdates->busy());
        client->setExternalBusy(extBusy);
    };
    QObject::connect(aur, &lut::AurUpdates::busyChanged, &app, syncExternalBusy);
    QObject::connect(pacstallUpdates, &lut::PacstallUpdates::busyChanged, &app, syncExternalBusy);
    QObject::connect(flatpakUpdates, &lut::FlatpakUpdates::busyChanged, &app, syncExternalBusy);
    QObject::connect(snapUpdates, &lut::SnapUpdates::busyChanged, &app, syncExternalBusy);

    client->init(replayFixture, replaySpeed);

    auto *catalogService = new lut::CatalogService(&app);
    if (checkQml || !replayFixture.isEmpty()) catalogService->setLoadStdDataLocations(false);

    std::unique_ptr<lut::PackageCatalog> packageCatalog;
    switch (lut::DistroDetect::detectFamily()) {
    case lut::DistroFamily::Arch:
#ifdef HAVE_ALPM
        packageCatalog = std::make_unique<lut::AlpmPackageCatalog>();
#endif
        break;
    case lut::DistroFamily::Fedora:
        packageCatalog = std::make_unique<lut::Dnf5PackageCatalog>();
        break;
    case lut::DistroFamily::Debian:
        packageCatalog = std::make_unique<lut::AptPackageCatalog>();
        break;
    default:
        break;
    }

    std::unique_ptr<lut::FlatpakPackageCatalog> flatpakCatalog;
    if (QFile::exists(QStringLiteral("/usr/bin/flatpak"))) {
        flatpakCatalog = std::make_unique<lut::FlatpakPackageCatalog>();
    }

    std::unique_ptr<lut::SnapPackageCatalog> snapCatalog;
    if (lut::SnapAvailability::isSnapAvailable()) {
        snapCatalog = std::make_unique<lut::SnapPackageCatalog>();
    }

    auto appStoreOwner = std::make_unique<lut::ApplicationStore>(catalogService, packageCatalog.get());
    auto *appStore = appStoreOwner.get();
    if (flatpakCatalog) {
        appStore->addPackageCatalog(flatpakCatalog.get());
    }
    if (snapCatalog) {
        appStore->addPackageCatalog(snapCatalog.get());
    }
    catalogService->loadAsync();
    auto *storeModel = new lut::StoreModel(appStore, &app);
    auto *installedStoreModel = new lut::StoreModel(appStore, &app);
    installedStoreModel->setInstalledOnly(true);

    auto *changeWatcher = new lut::ExternalChangeWatcher(&app);

    QObject::connect(appStore, &lut::ApplicationStore::installPackageRefsRequested, client,
                     qOverload<const QList<lut::PackageRef> &>(&lut::DaemonClient::planStoreInstall));
    QObject::connect(appStore, &lut::ApplicationStore::removePackageRefsRequested, client,
                     qOverload<const QList<lut::PackageRef> &>(&lut::DaemonClient::planStoreRemove));
    const auto updateStoreStatus = [appStore, client, aur, pacstallUpdates, flatpakUpdates, snapUpdates] {
        bool anyBusy = client->isBusy() ||
                       (aur && aur->busy()) ||
                       (pacstallUpdates && pacstallUpdates->busy()) ||
                       (flatpakUpdates && flatpakUpdates->busy()) ||
                       (snapUpdates && snapUpdates->busy());
        appStore->updateTransactionStatus(client->isConnected() && client->installSupported(),
            client->isConnected() && client->removeSupported(), anyBusy, client->hasPlan(), client->hasError());
    };
    QObject::connect(client, &lut::DaemonClient::statusChanged, appStore, updateStoreStatus);
    QObject::connect(client, &lut::DaemonClient::capabilitiesChanged, appStore, updateStoreStatus);
    QObject::connect(client, &lut::DaemonClient::connectionChanged, appStore, updateStoreStatus);
    QObject::connect(client, &lut::DaemonClient::transactionStarted, appStore, &lut::ApplicationStore::transactionStarted);
    QObject::connect(client, &lut::DaemonClient::transactionFinished, appStore, &lut::ApplicationStore::transactionFinished);
    QObject::connect(aur, &lut::AurUpdates::busyChanged, appStore, updateStoreStatus);
    QObject::connect(pacstallUpdates, &lut::PacstallUpdates::busyChanged, appStore, updateStoreStatus);
    QObject::connect(flatpakUpdates, &lut::FlatpakUpdates::busyChanged, appStore, updateStoreStatus);
    QObject::connect(snapUpdates, &lut::SnapUpdates::busyChanged, appStore, updateStoreStatus);
    updateStoreStatus();

    // Abschnitt 8.7: Die Vorschau muss nennen, welche Anwendungen von den zu
    // entfernenden Paketen betroffen sind - auch die, die der Benutzer nicht
    // angeklickt hat, weil mehrere Apps sich ein Paket teilen können.
    QObject::connect(client, &lut::DaemonClient::planReady, appStore, [appStore, client]() {
        QStringList removedPackages;
        for (const lut::PackageOp &op : client->planModel()->ops()) {
            if (op.kind == lut::PackageOp::Kind::Remove && !op.name.isEmpty()) {
                removedPackages.append(op.name);
            }
        }
        client->planModel()->setAffectedApps(
            removedPackages.isEmpty() ? QStringList() : appStore->appsProvidedByPackages(removedPackages));
    });

    QObject::connect(changeWatcher, &lut::ExternalChangeWatcher::databaseChanged, appStore, [appStore, client]() {
        appStore->refresh();
        client->installedModel()->refresh();
    });

    qmlRegisterType<lut::StoreModel>("LinuxAppStore", 1, 0, "StoreModel");
    qmlRegisterType<lut::StoreModel>("LinuxUpdateTool", 1, 0, "StoreModel");

    lut::MediaCache mediaCache;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("mediaCache"), &mediaCache);

    // Theme & Models im QML Context bereitstellen
    engine.rootContext()->setContextProperty(QStringLiteral("Theme"), palette);
    engine.rootContext()->setContextProperty(QStringLiteral("daemonClient"), client);
    engine.rootContext()->setContextProperty(QStringLiteral("updatesModel"), client->updatesModel());
    engine.rootContext()->setContextProperty(QStringLiteral("progressModel"), client->progressModel());
    engine.rootContext()->setContextProperty(QStringLiteral("logModel"), client->logModel());
    engine.rootContext()->setContextProperty(QStringLiteral("installedModel"), client->installedModel());
    engine.rootContext()->setContextProperty(QStringLiteral("historyModel"), client->historyModel());
    engine.rootContext()->setContextProperty(QStringLiteral("aurUpdates"), aur);
    engine.rootContext()->setContextProperty(QStringLiteral("pacstallUpdates"), pacstallUpdates);
    engine.rootContext()->setContextProperty(QStringLiteral("flatpakUpdates"), flatpakUpdates);
    engine.rootContext()->setContextProperty(QStringLiteral("snapUpdates"), snapUpdates);
    // Ein gemeinsames Fortschrittsfenster für System, Flatpak, Snap, AUR und Pacstall.
    auto *operationMonitor = new lut::OperationMonitor(client, flatpakUpdates, snapUpdates, aur, pacstallUpdates, &app);
    engine.rootContext()->setContextProperty(QStringLiteral("operationMonitor"), operationMonitor);
    engine.rootContext()->setContextProperty(QStringLiteral("appStore"), appStore);
    engine.rootContext()->setContextProperty(QStringLiteral("storeModel"), storeModel);
    engine.rootContext()->setContextProperty(QStringLiteral("installedStoreModel"), installedStoreModel);
    engine.rootContext()->setContextProperty(QStringLiteral("repositoriesModel"), client->repositoriesModel());
    // Pacstall einrichten = PPR mit Schlüssel hinzufügen, dann pacstall über den
    // normalen Installationsweg (Plan, Vorschau, Bestätigung, APT-Wächter).
    QObject::connect(client->repositoriesModel(), &lut::RepositoriesModel::presetAdded, client, [client](const QString &id) {
        if (id == QLatin1String("pacstall") && !QFile::exists(QStringLiteral("/usr/bin/pacstall"))) {
            client->planStoreInstall(QStringLiteral("pacstall"));
        }
    });
    QObject::connect(client, &lut::DaemonClient::transactionFinished, client->repositoriesModel(), &lut::RepositoriesModel::refreshPacstallState);
    QObject::connect(client, &lut::DaemonClient::transactionFinished, pacstallUpdates, &lut::PacstallUpdates::refreshSupport);
    int initialNavIndex = 0;
    bool okNav = false;
    int envNav = qEnvironmentVariableIntValue("LUT_NAV_INDEX", &okNav);
    if (okNav) initialNavIndex = envNav;
    engine.rootContext()->setContextProperty(QStringLiteral("initialNavIndex"), initialNavIndex);

    // Direkt eine Detailseite öffnen (für Bildschirmfotos und Verweise)
    engine.rootContext()->setContextProperty(QStringLiteral("initialAppKey"), qEnvironmentVariable("LUT_APP_KEY"));

    // Reiter in „Verwalten": 0 Aktualisierungen, 1 Apps, 2 Alle Pakete & Pflege
    int initialManageTab = qEnvironmentVariableIntValue("LUT_MANAGE_TAB");
    engine.rootContext()->setContextProperty(QStringLiteral("initialManageTab"), initialManageTab);

    int initialInstalledTab = qEnvironmentVariableIntValue("LUT_INSTALLED_TAB");
    engine.rootContext()->setContextProperty(QStringLiteral("initialInstalledTab"), initialInstalledTab);

    int initialSettingsTab = qEnvironmentVariableIntValue("LUT_SETTINGS_TAB");
    engine.rootContext()->setContextProperty(QStringLiteral("initialSettingsTab"), initialSettingsTab);
    engine.rootContext()->setContextProperty(QStringLiteral("appSettings"), appSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("screenshotRiskSource"), qEnvironmentVariable("LUT_SCREENSHOT_RISK_SOURCE"));

    std::unique_ptr<lut::TrayManager> trayManager;
    if (!checkQml && screenshotPath.isEmpty()) {
        trayManager = std::make_unique<lut::TrayManager>(nullptr, client, flatpakUpdates, snapUpdates, aur, pacstallUpdates);
        trayManager->setOperationMonitor(operationMonitor);
        engine.rootContext()->setContextProperty(QStringLiteral("trayManager"), trayManager.get());
    } else {
        engine.rootContext()->setContextProperty(QStringLiteral("trayManager"), nullptr);
    }
    engine.rootContext()->setContextProperty(QStringLiteral("startInTray"), startInTray);

    const QUrl url(QStringLiteral("qrc:/LinuxAppStore/qml/Main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url, checkQml, screenshotPath, startInTray, &app, &engine, appStore, &trayManager, client, replayFixture](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl) {
                QCoreApplication::exit(-1);
            } else if (obj && !screenshotPath.isEmpty()) {
                auto *window = qobject_cast<QQuickWindow *>(obj);
                if (!window) {
                    fprintf(stderr, "Wurzelobjekt ist kein Fenster – kein Screenshot möglich.\n");
                    app.exit(1);
                    return;
                }
                auto takeShot = [&app, window, screenshotPath]() {
                    const QImage image = window->grabWindow();
                    if (image.isNull() || !image.save(screenshotPath)) {
                        fprintf(stderr, "Screenshot konnte nicht gespeichert werden: %s\n",
                                qPrintable(screenshotPath));
                        app.exit(1);
                        return;
                    }
                    printf("Screenshot gespeichert: %s (%dx%d)\n",
                           qPrintable(screenshotPath), image.width(), image.height());
                    app.exit(0);
                };
                // Nur für Bildschirmfotos im Replay: eine Aktualisierung anstoßen,
                // damit das Fortschrittsfenster auf dem Bild zu sehen ist.
                if (!replayFixture.isEmpty() && qEnvironmentVariableIsSet("LUT_SCREENSHOT_UPGRADE")) {
                    QTimer::singleShot(100, client, &lut::DaemonClient::startUpgrade);
                }
                // Nur planen, nicht starten: so steht nach der automatischen Prüfung
                // ein unbestätigter Systemplan bereit (Statusleiste).
                if (!replayFixture.isEmpty() && qEnvironmentVariableIsSet("LUT_SCREENSHOT_PLAN")) {
                    QTimer::singleShot(100, client, &lut::DaemonClient::refreshUpdates);
                }
                if (appStore->isLoaded()) {
                    QTimer::singleShot(800, &app, takeShot);
                } else {
                    QObject::connect(appStore, &lut::ApplicationStore::loadedChanged, &app, [takeShot](bool loaded) {
                        if (loaded) {
                            QTimer::singleShot(1200, takeShot);
                        }
                    });
                }
            } else if (obj && checkQml) {
                QTimer::singleShot(50, &app, [&app, &engine]() {
                    int failures = instantiateAllPages(engine);
                    // Sammeln abschalten und den Standard-Handler wiederherstellen,
                    // bevor berichtet wird: sonst landen die Meldungen wieder im Sammler.
                    g_collectQmlWarnings = false;
                    qInstallMessageHandler(g_previousHandler);
                    if (!g_qmlWarnings.isEmpty()) {
                        fprintf(stderr, "%lld QML-Warnung(en) im Smoketest:\n",
                                static_cast<long long>(g_qmlWarnings.size()));
                        for (const QString &w : std::as_const(g_qmlWarnings)) {
                            fprintf(stderr, "  - %s\n", qPrintable(w));
                        }
                        fflush(stderr);
                        failures += static_cast<int>(g_qmlWarnings.size());
                    }
                    app.exit(failures == 0 ? 0 : 1);
                });
            } else if (obj) {
                auto *window = qobject_cast<QQuickWindow *>(obj);
                if (window) {
                    if (trayManager) {
                        trayManager->setMainWindow(window);
                    }
                    if (startInTray && QSystemTrayIcon::isSystemTrayAvailable()) {
                        window->hide();
                    }
                }
            }
        },
        Qt::QueuedConnection
    );
    engine.load(url);

    // Automatisch beim App-Start Metadaten prüfen und Updates laden
    if (!checkQml) {

        // AUR nur prüfen wenn available; ist es aus aber unterstützt, Fremdpakete für Hinweis ermitteln
        if (replayFixture.isEmpty()) {
            if (aur->available()) {
                QTimer::singleShot(400, aur, &lut::AurUpdates::check);
            } else if (aur->supported()) {
                QTimer::singleShot(400, aur, &lut::AurUpdates::checkForeignPackages);
            }
            if (pacstallUpdates->available()) {
                QTimer::singleShot(400, pacstallUpdates, &lut::PacstallUpdates::check);
            }
        }
    }

    return app.exec();
}
