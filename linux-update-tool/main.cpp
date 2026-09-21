#include <QApplication>
#include <QQuickStyle>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QIcon>
#include <QTimer>
#include <QDebug>
#include "DaemonClient.h"
#include "theme/SystemPalette.h"
#include "liblut/liblut.h"

int main(int argc, char *argv[]) {
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
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

    parser.process(app);

    bool checkQml = parser.isSet(checkQmlOption);
    QString replayFixture = parser.value(replayOption);
    double replaySpeed = parser.value(speedOption).toDouble();
    if (replaySpeed <= 0.0) replaySpeed = 1.0;

    auto *palette = lut::SystemPalette::instance();
    auto *client = new lut::DaemonClient(&app);
    client->init(replayFixture, replaySpeed);

    QQmlApplicationEngine engine;

    // Theme & Models im QML Context bereitstellen
    engine.rootContext()->setContextProperty(QStringLiteral("Theme"), palette);
    engine.rootContext()->setContextProperty(QStringLiteral("daemonClient"), client);
    engine.rootContext()->setContextProperty(QStringLiteral("updatesModel"), client->updatesModel());
    engine.rootContext()->setContextProperty(QStringLiteral("progressModel"), client->progressModel());
    engine.rootContext()->setContextProperty(QStringLiteral("logModel"), client->logModel());
    engine.rootContext()->setContextProperty(QStringLiteral("installedModel"), client->installedModel());
    engine.rootContext()->setContextProperty(QStringLiteral("historyModel"), client->historyModel());

    const QUrl url(QStringLiteral("qrc:/LinuxUpdateTool/qml/Main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url, checkQml, &app](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl) {
                QCoreApplication::exit(-1);
            } else if (obj && checkQml) {
                QTimer::singleShot(50, &app, [&app]() {
                    app.exit(0);
                });
            }
        },
        Qt::QueuedConnection
    );
    engine.load(url);

    // Automatisch beim App-Start Metadaten prüfen und Updates laden
    if (!checkQml) {
        QTimer::singleShot(300, client, &lut::DaemonClient::refreshUpdates);
    }

    return app.exec();
}
