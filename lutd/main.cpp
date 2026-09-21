#include <QCoreApplication>
#include <QDebug>
#include "liblut/liblut.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("lutd"));
    app.setApplicationVersion(lut::versionString());

    qInfo() << "lutd (Linux Update Tool Daemon) v" << app.applicationVersion() << "starting...";
    return 0;
}
