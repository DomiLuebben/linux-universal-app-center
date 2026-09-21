#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QIcon>
#include "liblut/liblut.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("linux-update-tool"));
    app.setApplicationDisplayName(QStringLiteral("Linux Update Tool"));
    app.setApplicationVersion(lut::versionString());
    app.setOrganizationName(QStringLiteral("LinuxUpdateTool"));
    app.setOrganizationDomain(QStringLiteral("linuxupdatetool.org"));

    QQmlApplicationEngine engine;
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

    return app.exec();
}
