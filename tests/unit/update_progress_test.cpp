#include <QTest>
#include <QSignalSpy>
#include <QDBusObjectPath>
#include <QJsonDocument>
#include <QImage>
#include <QPainter>
#include "linux-app-store/DaemonClient.h"
#include "linux-app-store/OperationMonitor.h"
#include "linux-app-store/AurUpdates.h"
#include "linux-app-store/TrayManager.h"
#include "linux-app-store/models/FlatpakUpdates.h"
#include "linux-app-store/models/SnapUpdates.h"
#include "liblut/protocol/events.h"

using namespace lut;

// Prüft den Aktualisierungsablauf, wie Dominik ihn bemängelt hat:
// erledigte Pakete blieben in der Liste, die Zahl fehlte im Tray, und
// Flatpak/Snap/AUR hatten keinen gemeinsamen Fortschritt.
class UpdateProgressTest : public QObject {
    Q_OBJECT

private:
    static QString eventJson(const Event &event) {
        QJsonObject obj = serializeEvent(event);
        obj[QStringLiteral("v")] = 1;
        return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }

    static void deliver(DaemonClient &client, const Event &event) {
        const bool ok = QMetaObject::invokeMethod(
            &client, "onDbusTransactionEvent", Qt::DirectConnection,
            Q_ARG(QDBusObjectPath, QDBusObjectPath(QStringLiteral("/org/linuxupdatetool/Transaction/1"))),
            Q_ARG(QString, eventJson(event)));
        QVERIFY(ok);
    }

    static PlanReady twoPackagePlan() {
        PlanReady plan;
        PackageOp a; a.name = QStringLiteral("firefox"); a.kind = PackageOp::Kind::Upgrade;
        a.version = QStringLiteral("1"); a.newVersion = QStringLiteral("2");
        PackageOp b; b.name = QStringLiteral("mesa"); b.kind = PackageOp::Kind::Upgrade;
        b.version = QStringLiteral("24.1"); b.newVersion = QStringLiteral("24.2");
        plan.ops = {a, b};
        plan.planRevision = QStringLiteral("rev");
        return plan;
    }

    // Systemplan mit zwei Paketen im Client, wie nach "Prüfen".
    static void prepareUpgradePlan(DaemonClient &client) {
        client.refreshUpdates();                    // setzt den Systemplan-Modus; ohne Daemon endet es sofort
        client.setPendingTransactionForTest(true, false);
        deliver(client, twoPackagePlan());
    }

private slots:
    void testSuccessfulUpgradeRemovesPackagesFromList();
    void testFailedUpgradeKeepsPackages();
    void testMonitorFollowsFlatpakUpdate();
    void testMonitorIgnoresPlainChecks();
    void testFlatpakProgressLines();
    void testSnapRefreshedLines();
    void testTrayBadgeIsDrawnIntoIcon();
};

void UpdateProgressTest::testSuccessfulUpgradeRemovesPackagesFromList() {
    DaemonClient client;
    prepareUpgradePlan(client);
    QCOMPARE(client.updatesModel()->totalCount(), 2);

    deliver(client, TransactionDone{Result::Success, QStringLiteral("System erfolgreich aktualisiert"), false, {}, 0});

    // Vorher blieben beide Pakete nach "Fertig" stehen – samt Zähler im Tray.
    QCOMPARE(client.updatesModel()->totalCount(), 0);
    QVERIFY(!client.lastCheckedString().isEmpty());
}

void UpdateProgressTest::testFailedUpgradeKeepsPackages() {
    DaemonClient client;
    prepareUpgradePlan(client);
    deliver(client, TransactionDone{Result::Failed, QStringLiteral("Dateikonflikt"), false, {}, 0});
    // Gegenprobe: was nicht angewendet wurde, bleibt sichtbar.
    QCOMPARE(client.updatesModel()->totalCount(), 2);
}

void UpdateProgressTest::testMonitorFollowsFlatpakUpdate() {
    DaemonClient client;
    FlatpakUpdates flatpak;
    flatpak.setForceAvailable(true);
    flatpak.setDaemonClient(&client);
    flatpak.setProcessRunner([](const QStringList &args, QString &out, QString &) {
        if (args.contains(QStringLiteral("update"))) {
            out = QStringLiteral("Looking for updates…\nUpdating 1/2…\nUpdating 2/2…\nUpdates complete.\n");
        }
        return 0;
    });

    OperationMonitor monitor(&client, &flatpak, nullptr, nullptr);
    QSignalSpy started(&monitor, &OperationMonitor::started);
    QSignalSpy finished(&monitor, &OperationMonitor::finished);

    flatpak.updateAll();

    QCOMPARE(started.count(), 1);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(monitor.source(), QStringLiteral("flatpak"));
    QCOMPARE(monitor.result(), int(OperationMonitor::Succeeded));
    QCOMPARE(monitor.progress(), 1.0);
    QVERIFY(monitor.active());
    QVERIFY(!monitor.running());

    // Die Ausgabe landet im gemeinsamen Protokoll statt im Nichts.
    const QString log = client.logModel()->copyAll();
    QVERIFY2(log.contains(QStringLiteral("Updating 2/2")), qPrintable(log));

    monitor.acknowledge();
    QVERIFY(!monitor.active());
}

void UpdateProgressTest::testMonitorIgnoresPlainChecks() {
    DaemonClient client;
    FlatpakUpdates flatpak;
    flatpak.setForceAvailable(true);
    flatpak.setProcessRunner([](const QStringList &, QString &, QString &) { return 0; });
    OperationMonitor monitor(&client, &flatpak, nullptr, nullptr);
    QSignalSpy started(&monitor, &OperationMonitor::started);

    flatpak.check();
    // Eine Prüfung ist kein Vorgang: kein Fortschrittsfenster.
    QCOMPARE(started.count(), 0);
    QVERIFY(!monitor.active());
}

void UpdateProgressTest::testFlatpakProgressLines() {
    int step = -1, total = -1;
    QString item;
    QVERIFY(FlatpakUpdates::parseProgressLine(QStringLiteral("Updating 2/5…"), &step, &total, &item));
    QCOMPARE(step, 2);
    QCOMPARE(total, 5);

    QVERIFY(FlatpakUpdates::parseProgressLine(QStringLiteral(" 3. [✓] org.gnome.Calculator  stable  u  flathub  1.0 MB"), &step, &total, &item));
    QCOMPARE(step, 3);
    QCOMPARE(item, QStringLiteral("org.gnome.Calculator"));

    QVERIFY(FlatpakUpdates::parseProgressLine(QStringLiteral("Updating app/org.kde.kate/x86_64/stable"), &step, &total, &item));
    QCOMPARE(item, QStringLiteral("org.kde.kate"));

    QVERIFY(!FlatpakUpdates::parseProgressLine(QStringLiteral("Looking for updates…"), &step, &total, &item));
    QVERIFY(!FlatpakUpdates::parseProgressLine(QString(), &step, &total, &item));
}

void UpdateProgressTest::testSnapRefreshedLines() {
    QString name;
    QVERIFY(SnapUpdates::parseRefreshedLine(QStringLiteral("firefox 125.0-2 from Mozilla✓ refreshed"), &name));
    QCOMPARE(name, QStringLiteral("firefox"));
    QVERIFY(!SnapUpdates::parseRefreshedLine(QStringLiteral("All snaps up to date."), &name));
}

void UpdateProgressTest::testTrayBadgeIsDrawnIntoIcon() {
    QCOMPARE(TrayManager::badgeLabel(7), QStringLiteral("7"));
    QCOMPARE(TrayManager::badgeLabel(99), QStringLiteral("99"));
    QCOMPARE(TrayManager::badgeLabel(150), QStringLiteral("99+"));

    QPixmap plain(48, 48);
    plain.fill(Qt::transparent);
    const QIcon base(plain);
    const QColor background(10, 120, 220);

    const QImage badged = TrayManager::badgedIcon(base, 7, background, Qt::white).pixmap(48, 48).toImage();
    // Linke obere Ecke bleibt das Programmsymbol, rechts unten sitzt die Plakette.
    QCOMPARE(badged.pixelColor(2, 2).alpha(), 0);
    int badgePixels = 0;
    for (int y = 30; y < 48; ++y)
        for (int x = 30; x < 48; ++x)
            if (badged.pixelColor(x, y) == background) ++badgePixels;
    QVERIFY2(badgePixels > 20, "Plakette nicht gezeichnet");

    // Ohne Aktualisierungen bleibt das Symbol unverändert.
    const QImage unchanged = TrayManager::badgedIcon(base, 0, background, Qt::white).pixmap(48, 48).toImage();
    for (int y = 30; y < 48; ++y)
        for (int x = 30; x < 48; ++x)
            QCOMPARE(unchanged.pixelColor(x, y).alpha(), 0);
}

QTEST_MAIN(UpdateProgressTest)
#include "update_progress_test.moc"
