#include <QApplication>
#include <QQuickStyle>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QImage>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QIcon>
#include <QTimer>
#include <QDebug>
#include <QFileInfo>
#include <QLibraryInfo>
#include "DaemonClient.h"
#include "AurUpdates.h"
#include "theme/SystemPalette.h"
#include "catalog/CatalogService.h"
#include "catalog/ApplicationStore.h"
#include "models/StoreModel.h"
#include "ExternalChangeWatcher.h"
#include "liblut/liblut.h"

#ifdef HAVE_ALPM
#include "liblut/catalog/alpm/AlpmPackageCatalog.h"
#endif
#include "liblut/catalog/dnf5/Dnf5PackageCatalog.h"
#include "liblut/catalog/apt/AptPackageCatalog.h"
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
        QStringLiteral("qrc:/LinuxUpdateTool/qml/pages/Discover.qml"),
        QStringLiteral("qrc:/LinuxUpdateTool/qml/pages/Search.qml"),
        QStringLiteral("qrc:/LinuxUpdateTool/qml/pages/AppDetails.qml"),
        QStringLiteral("qrc:/LinuxUpdateTool/qml/pages/Updates.qml"),
        QStringLiteral("qrc:/LinuxUpdateTool/qml/pages/Transaction.qml"),
        QStringLiteral("qrc:/LinuxUpdateTool/qml/pages/Report.qml"),
        QStringLiteral("qrc:/LinuxUpdateTool/qml/pages/Installed.qml"),
        QStringLiteral("qrc:/LinuxUpdateTool/qml/pages/History.qml"),
        QStringLiteral("qrc:/LinuxUpdateTool/qml/pages/Logs.qml"),
        QStringLiteral("qrc:/LinuxUpdateTool/qml/pages/Settings.qml"),
    };
    return pages;
}

int instantiateAllPages(QQmlApplicationEngine &engine) {
    int failures = 0;
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
    app.setApplicationName(QStringLiteral("linux-update-tool"));
    app.setApplicationDisplayName(QStringLiteral("Linux Update Tool"));
    app.setApplicationVersion(lut::versionString());
    app.setOrganizationName(QStringLiteral("LinuxUpdateTool"));
    app.setOrganizationDomain(QStringLiteral("linuxupdatetool.org"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("org.linuxupdatetool")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Linux Update Tool GUI"));
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

    parser.process(app);

    bool checkQml = parser.isSet(checkQmlOption);
    if (checkQml) {
        g_collectQmlWarnings = true;
        g_previousHandler = qInstallMessageHandler(qmlWarningCollector);
    }
    const QString screenshotPath = parser.value(screenshotOption);
    QString replayFixture = parser.value(replayOption);
    double replaySpeed = parser.value(speedOption).toDouble();
    if (replaySpeed <= 0.0) replaySpeed = 1.0;

    auto *palette = lut::SystemPalette::instance();
    auto *client = new lut::DaemonClient(&app);
    auto *aur = new lut::AurUpdates(&app);
    client->init(replayFixture, replaySpeed);

    auto *catalogService = new lut::CatalogService(&app);
    catalogService->load();

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

    auto *appStore = new lut::ApplicationStore(catalogService, packageCatalog.get(), &app);
    auto *storeModel = new lut::StoreModel(appStore, &app);
    auto *installedStoreModel = new lut::StoreModel(appStore, &app);
    installedStoreModel->setInstalledOnly(true);

    auto *changeWatcher = new lut::ExternalChangeWatcher(&app);

    QObject::connect(appStore, &lut::ApplicationStore::installRequested, client, &lut::DaemonClient::planStoreInstall);
    QObject::connect(appStore, &lut::ApplicationStore::removeRequested, client, &lut::DaemonClient::planStoreRemove);
    QObject::connect(client, &lut::DaemonClient::statusChanged, appStore, [appStore, installedStoreModel, client]() {
        if (!client->isBusy() && !client->hasPlan()) {
            appStore->refresh();
            installedStoreModel->refresh();
        }
    });
    QObject::connect(client, &lut::DaemonClient::transactionFinished, appStore, [appStore, installedStoreModel](lut::Result) {
        appStore->refresh();
        installedStoreModel->refresh();
    });
    QObject::connect(client, &lut::DaemonClient::capabilitiesChanged, appStore, &lut::ApplicationStore::catalogLoaded);

    QObject::connect(changeWatcher, &lut::ExternalChangeWatcher::databaseChanged, appStore, [appStore, client, installedStoreModel]() {
        appStore->refresh();
        client->installedModel()->refresh();
        installedStoreModel->refresh();
    });

    qmlRegisterType<lut::StoreModel>("LinuxUpdateTool", 1, 0, "StoreModel");

    QQmlApplicationEngine engine;

    // Theme & Models im QML Context bereitstellen
    engine.rootContext()->setContextProperty(QStringLiteral("Theme"), palette);
    engine.rootContext()->setContextProperty(QStringLiteral("daemonClient"), client);
    engine.rootContext()->setContextProperty(QStringLiteral("updatesModel"), client->updatesModel());
    engine.rootContext()->setContextProperty(QStringLiteral("progressModel"), client->progressModel());
    engine.rootContext()->setContextProperty(QStringLiteral("logModel"), client->logModel());
    engine.rootContext()->setContextProperty(QStringLiteral("installedModel"), client->installedModel());
    engine.rootContext()->setContextProperty(QStringLiteral("historyModel"), client->historyModel());
    engine.rootContext()->setContextProperty(QStringLiteral("aurUpdates"), aur);
    engine.rootContext()->setContextProperty(QStringLiteral("appStore"), appStore);
    engine.rootContext()->setContextProperty(QStringLiteral("storeModel"), storeModel);
    engine.rootContext()->setContextProperty(QStringLiteral("installedStoreModel"), installedStoreModel);
    int initialNavIndex = 0;
    bool okNav = false;
    int envNav = qEnvironmentVariableIntValue("LUT_NAV_INDEX", &okNav);
    if (okNav) initialNavIndex = envNav;
    engine.rootContext()->setContextProperty(QStringLiteral("initialNavIndex"), initialNavIndex);

    int initialInstalledTab = qEnvironmentVariableIntValue("LUT_INSTALLED_TAB");
    engine.rootContext()->setContextProperty(QStringLiteral("initialInstalledTab"), initialInstalledTab);

    const QUrl url(QStringLiteral("qrc:/LinuxUpdateTool/qml/Main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url, checkQml, screenshotPath, &app, &engine](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl) {
                QCoreApplication::exit(-1);
            } else if (obj && !screenshotPath.isEmpty()) {
                auto *window = qobject_cast<QQuickWindow *>(obj);
                if (!window) {
                    fprintf(stderr, "Wurzelobjekt ist kein Fenster – kein Screenshot möglich.\n");
                    app.exit(1);
                    return;
                }
                // Zwei Ereignisdurchläufe abwarten, damit Layout und erster
                // Renderdurchgang abgeschlossen sind.
                QTimer::singleShot(600, &app, [&app, window, screenshotPath]() {
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
                });
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
            }
        },
        Qt::QueuedConnection
    );
    engine.load(url);

    // Automatisch beim App-Start Metadaten prüfen und Updates laden
    if (!checkQml) {
        QTimer::singleShot(300, client, &lut::DaemonClient::refreshUpdates);
        // AUR gleich mitprüfen: die Liste steht auf derselben Seite, also
        // soll sie auch ohne zusätzlichen Klick gefüllt sein.
        if (replayFixture.isEmpty()) QTimer::singleShot(400, aur, &lut::AurUpdates::check);
    }

    return app.exec();
}
