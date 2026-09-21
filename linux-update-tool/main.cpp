#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QIcon>
#include <QDebug>
#include "DaemonClient.h"
#include "theme/SystemPalette.h"
#include "liblut/liblut.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("linux-update-tool"));
    app.setApplicationDisplayName(QStringLiteral("Linux Update Tool"));
    app.setApplicationVersion(lut::versionString());
    app.setOrganizationName(QStringLiteral("LinuxUpdateTool"));
    app.setOrganizationDomain(QStringLiteral("linuxupdatetool.org"));

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

    parser.process(app);

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

    const QUrl url(QStringLiteral("qrc:/LinuxUpdateTool/qml/Main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl) {
                QCoreApplication::exit(-1);
            }
        },
        Qt::QueuedConnection
    );
    engine.load(url);

    // Falls Replay-Modus aktiv ist: nach 500ms automatisch Metadaten laden/abspielen
    if (client->isReplayMode()) {
        QTimer::singleShot(500, client, &lut::DaemonClient::refreshUpdates);
    }

    return app.exec();
}
