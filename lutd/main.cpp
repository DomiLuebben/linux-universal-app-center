#include <QCoreApplication>
#include <QDBusConnection>
#include <QDebug>
#include "TransactionManager.h"
#include "liblut/liblut.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("lutd"));
    app.setApplicationVersion(lut::versionString());

    qInfo() << "lutd (Linux Update Tool Daemon) v" << app.applicationVersion() << "starting...";

    lut::TransactionManager manager;
    if (!manager.init()) {
        qCritical() << "lutd: Failed to initialize TransactionManager!";
        return 1;
    }

    auto bus = QDBusConnection::systemBus();
    bool registered = false;

    if (bus.isConnected()) {
        if (bus.registerService(QStringLiteral("org.linuxupdatetool.Daemon1"))) {
            if (bus.registerObject(QStringLiteral("/org/linuxupdatetool/Daemon1"), &manager,
                                    QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
                registered = true;
                qInfo() << "lutd: Successfully registered on system bus";
            }
        } else {
            qInfo() << "lutd: System bus registration not permitted (unprivileged dev mode), falling back to session bus";
        }
    }

    if (!registered) {
        bus = QDBusConnection::sessionBus();
        if (bus.isConnected()) {
            if (bus.registerService(QStringLiteral("org.linuxupdatetool.Daemon1")) &&
                bus.registerObject(QStringLiteral("/org/linuxupdatetool/Daemon1"), &manager,
                                   QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
                registered = true;
                qInfo() << "lutd: Successfully registered on session bus";
            } else {
                qCritical() << "lutd: Failed to register on session bus:" << bus.lastError().message();
                return 1;
            }
        }
    }

    return app.exec();
}
