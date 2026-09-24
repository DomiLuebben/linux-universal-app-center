#include <QTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include "linux-app-store/models/FlatpakUpdates.h"
#include "linux-app-store/models/SnapUpdates.h"
#include "linux-app-store/models/HistoryModel.h"
#include "linux-app-store/DaemonClient.h"
#include "linux-app-store/AurUpdates.h"
#include "linux-app-store/TrayManager.h"
#include "liblut/backend/flatpak/FlatpakBackend.h"
#include "liblut/backend/snap/SnapBackend.h"
#include "liblut/backend/snap/SnapAvailability.h"
#include "liblut/history/HistoryDb.h"

using namespace lut;

class StoreUpdatesHistoryTest : public QObject {
    Q_OBJECT

private slots:
    void testFlatpakUpdatesParsing();
    void testSnapUpdatesParsing();
    void testSnapUpdatesHostUnavailable();
    void testCombinedUpdateCountAndBreakdown();
    void testTrayManagerBreakdown();
    void testCrossSourceMutationLock();
    void testFlatpakBackendHistoryAndUpdates();
    void testSnapBackendHistoryAndUpdates();
    void testHistoryModelMergingAndDeduplication();
    void testHistoryDbDeduplicationAndSourceTarget();
};

void StoreUpdatesHistoryTest::testFlatpakUpdatesParsing() {
    QString sampleRemoteLs = QStringLiteral(
        "io.github.kolunmi.Bazaar\tflathub\t0.4.0\tapp/io.github.kolunmi.Bazaar/x86_64/stable\t64903328\n"
        "org.videolan.VLC\tflathub\t3.0.21\tapp/org.videolan.VLC/x86_64/stable\t41943040\n"
    );
    QString sampleList = QStringLiteral(
        "io.github.kolunmi.Bazaar\t0.3.2\n"
        "org.videolan.VLC\t3.0.20\n"
    );

    auto entries = FlatpakUpdates::parseUpdatesOutput(sampleRemoteLs, sampleList);
    QCOMPARE(entries.size(), 2);

    QCOMPARE(entries.at(0).id, QStringLiteral("io.github.kolunmi.Bazaar"));
    QCOMPARE(entries.at(0).origin, QStringLiteral("flathub"));
    QCOMPARE(entries.at(0).newVersion, QStringLiteral("0.4.0"));
    QCOMPARE(entries.at(0).currentVersion, QStringLiteral("0.3.2"));
    QCOMPARE(entries.at(0).versionTransition, QStringLiteral("0.3.2 \u2192 0.4.0"));
    QCOMPARE(entries.at(0).ref, QStringLiteral("app/io.github.kolunmi.Bazaar/x86_64/stable"));
    QCOMPARE(entries.at(0).downloadSize, 64903328);

    QCOMPARE(entries.at(1).id, QStringLiteral("org.videolan.VLC"));
    QCOMPARE(entries.at(1).newVersion, QStringLiteral("3.0.21"));
    QCOMPARE(entries.at(1).currentVersion, QStringLiteral("3.0.20"));
    QCOMPARE(entries.at(1).versionTransition, QStringLiteral("3.0.20 \u2192 3.0.21"));

    FlatpakUpdates model;
    model.setForceAvailable(true);
    model.setProcessRunner([sampleRemoteLs, sampleList](const QStringList &args, QString &stdoutOut, QString &stderrOut) {
        Q_UNUSED(stderrOut);
        if (args.contains(QStringLiteral("remote-ls"))) {
            stdoutOut = sampleRemoteLs;
            return 0;
        }
        if (args.contains(QStringLiteral("list"))) {
            stdoutOut = sampleList;
            return 0;
        }
        return -1;
    });

    model.check();
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.totalDownloadBytes(), 64903328 + 41943040);
    QVERIFY(!model.totalDownloadFormatted().isEmpty());
}

void StoreUpdatesHistoryTest::testSnapUpdatesParsing() {
    QString sampleOutput = QStringLiteral(
        "Name      Version    Rev   Publisher   Notes\n"
        "vlc       3.0.20     3722  videolan✓   -\n"
        "core22    20240111   1122  canonical✓  base\n"
    );

    auto entries = SnapUpdates::parseRefreshOutput(sampleOutput);
    QCOMPARE(entries.size(), 2);

    QCOMPARE(entries.at(0).name, QStringLiteral("vlc"));
    QCOMPARE(entries.at(0).newVersion, QStringLiteral("3.0.20"));
    QCOMPARE(entries.at(0).revision, QStringLiteral("3722"));
    QCOMPARE(entries.at(0).publisher, QStringLiteral("videolan✓"));

    QCOMPARE(entries.at(1).name, QStringLiteral("core22"));
    QCOMPARE(entries.at(1).newVersion, QStringLiteral("20240111"));
    QCOMPARE(entries.at(1).notes, QStringLiteral("base"));

    // Empty or up-to-date output
    QString upToDate = QStringLiteral("All snaps up to date.\n");
    QVERIFY(SnapUpdates::parseRefreshOutput(upToDate).isEmpty());
}

void StoreUpdatesHistoryTest::testSnapUpdatesHostUnavailable() {
    SnapUpdates model;
    // On host without snapd, available() must be false
    if (!SnapAvailability::isSnapAvailable()) {
        QVERIFY(!model.available());
        model.check();
        QCOMPARE(model.count(), 0);
    }
}

void StoreUpdatesHistoryTest::testCombinedUpdateCountAndBreakdown() {
    // Simulate count and breakdown calculation matching Updates.qml & NavRail.qml
    int nativeCount = 5;
    int flatpakCount = 2;
    int snapCount = 1;
    int aurCount = 3;

    int total = nativeCount + flatpakCount + snapCount + aurCount;
    QCOMPARE(total, 11);

    QStringList parts;
    if (nativeCount > 0) parts.append(QStringLiteral("%1 System").arg(nativeCount));
    if (flatpakCount > 0) parts.append(QStringLiteral("%1 Flatpak").arg(flatpakCount));
    if (snapCount > 0) parts.append(QStringLiteral("%1 Snap").arg(snapCount));
    if (aurCount > 0) parts.append(QStringLiteral("%1 AUR").arg(aurCount));

    QString breakdown = parts.join(QStringLiteral(" · "));
    QCOMPARE(breakdown, QStringLiteral("5 System · 2 Flatpak · 1 Snap · 3 AUR"));
}

void StoreUpdatesHistoryTest::testTrayManagerBreakdown() {
    // Formatierungsanforderung: "XY Nativ, XY Flatpak, XY Snap"
    QCOMPARE(TrayManager::formatBreakdown(0, 0, 0), QStringLiteral("0 Nativ, 0 Flatpak, 0 Snap"));
    QCOMPARE(TrayManager::formatBreakdown(2, 3, 0), QStringLiteral("2 Nativ, 3 Flatpak, 0 Snap"));
    QCOMPARE(TrayManager::formatBreakdown(15, 4, 1), QStringLiteral("15 Nativ, 4 Flatpak, 1 Snap"));
    QCOMPARE(TrayManager::formatBreakdown(3, 1, 0, 2), QStringLiteral("3 Nativ, 1 Flatpak, 0 Snap, 2 AUR"));

    // Wenn Quellen nicht verfügbar sind, dürfen sie nicht in der Aufschlüsselung erscheinen (Paket D)
    QCOMPARE(TrayManager::formatBreakdown(5, 0, 0, 0, false, false, false), QStringLiteral("5 Nativ"));
    QCOMPARE(TrayManager::formatBreakdown(5, 2, 0, 0, true, false, false), QStringLiteral("5 Nativ, 2 Flatpak"));
    QCOMPARE(TrayManager::formatBreakdown(5, 0, 1, 0, false, true, false), QStringLiteral("5 Nativ, 1 Snap"));
    QCOMPARE(TrayManager::formatBreakdown(5, 0, 0, 3, false, false, true), QStringLiteral("5 Nativ, 3 AUR"));
    QCOMPARE(TrayManager::formatBreakdown(5, 0, 0, 0, false, false, true), QStringLiteral("5 Nativ"));
}

void StoreUpdatesHistoryTest::testCrossSourceMutationLock() {
    DaemonClient client;
    FlatpakUpdates flatpak;
    SnapUpdates snap;
    snap.setForceAvailable(true);
    AurUpdates aur;

    flatpak.setDaemonClient(&client);
    snap.setDaemonClient(&client);
    aur.setDaemonClient(&client);

    // 1. When DaemonClient is busy, Flatpak, Snap, and AUR mutations must be blocked
    client.setExternalBusy(true);
    QVERIFY(client.isBusy());

    QSignalSpy flatpakFailed(&flatpak, &FlatpakUpdates::failed);
    flatpak.updateApp(QStringLiteral("app/org.videolan.VLC/x86_64/stable"));
    QCOMPARE(flatpakFailed.count(), 1);
    QCOMPARE(flatpakFailed.first().at(0).toString(), QStringLiteral("Eine andere Paketaktion läuft bereits."));

    QSignalSpy snapFailed(&snap, &SnapUpdates::failed);
    snap.updateApp(QStringLiteral("vlc"));
    QCOMPARE(snapFailed.count(), 1);
    QCOMPARE(snapFailed.first().at(0).toString(), QStringLiteral("Eine andere Paketaktion läuft bereits."));

    if (aur.available()) {
        QSignalSpy aurFailed(&aur, &AurUpdates::failed);
        aur.prepare(QStringLiteral("some-aur-pkg"));
        QCOMPARE(aurFailed.count(), 1);
        QCOMPARE(aurFailed.first().at(0).toString(), QStringLiteral("Eine andere Paketaktion läuft bereits."));
    }

    client.setExternalBusy(false);
    QVERIFY(!client.isBusy());

    // 2. When Flatpak is busy, DaemonClient is locked via setExternalBusy
    client.setExternalBusy(true);
    QVERIFY(client.isBusy());
    // Starting an upgrade or store action on DaemonClient must be blocked when isBusy() is true
    client.refreshUpdates();
    // hasPlan must be false because it was blocked
    QVERIFY(!client.hasPlan());
}

void StoreUpdatesHistoryTest::testFlatpakBackendHistoryAndUpdates() {
    QString remoteLs = QStringLiteral("org.kde.kwrite\tflathub\t24.02.0\tapp/org.kde.kwrite/x86_64/stable\t25000000\n");
    auto ops = FlatpakBackend::parseUpdates(remoteLs);
    QCOMPARE(ops.size(), 1);
    QCOMPARE(ops.first().name, QStringLiteral("org.kde.kwrite"));
    QCOMPARE(ops.first().newVersion, QStringLiteral("24.02.0"));
    QCOMPARE(ops.first().repo, QStringLiteral("flathub"));

    QByteArray historyJson = R"([
        {"time": "2026-03-20 14:00:00", "change": "install", "application": "org.kde.kwrite"},
        {"time": "2026-03-21 09:30:00", "change": "update", "application": "org.videolan.VLC"}
    ])";
    auto history = FlatpakBackend::parseHistoryJson(historyJson, 10);
    QCOMPARE(history.size(), 2);
    QCOMPARE(history.at(0).command, QStringLiteral("[Flatpak] install org.kde.kwrite"));
    QCOMPARE(history.at(0).packagesAltered, 1);
    QCOMPARE(history.at(0).result, QStringLiteral("Success"));
    QCOMPARE(history.at(1).command, QStringLiteral("[Flatpak] update org.videolan.VLC"));
}

void StoreUpdatesHistoryTest::testSnapBackendHistoryAndUpdates() {
    QString refreshOut = QStringLiteral(
        "Name   Version   Rev   Publisher   Notes\n"
        "firefox 124.0     4000  mozilla✓    -\n"
    );
    auto ops = SnapBackend::parseRefreshList(refreshOut);
    QCOMPARE(ops.size(), 1);
    QCOMPARE(ops.first().name, QStringLiteral("firefox"));
    QCOMPARE(ops.first().newVersion, QStringLiteral("124.0"));
    QCOMPARE(ops.first().repo, QStringLiteral("snap"));

    QString changesOut = QStringLiteral(
        "ID   Status  Spawn               Ready               Summary\n"
        "10   Done    2026-03-20T10:00:00Z 2026-03-20T10:01:00Z Install \"firefox\" snap\n"
        "11   Error   2026-03-21T11:00:00Z 2026-03-21T11:01:00Z Refresh \"vlc\" snap\n"
    );
    auto history = SnapBackend::parseChanges(changesOut, 10);
    QCOMPARE(history.size(), 2);
    QCOMPARE(history.at(0).id, 10);
    QCOMPARE(history.at(0).result, QStringLiteral("Success"));
    QCOMPARE(history.at(0).command, QStringLiteral("[Snap] Install \"firefox\" snap"));
    QCOMPARE(history.at(1).id, 11);
    QCOMPARE(history.at(1).result, QStringLiteral("Failed"));
    QCOMPARE(history.at(1).command, QStringLiteral("[Snap] Refresh \"vlc\" snap"));
}

void StoreUpdatesHistoryTest::testHistoryModelMergingAndDeduplication() {
    QDateTime t1 = QDateTime::fromString(QStringLiteral("2026-03-20 10:00:00"), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    QDateTime t2 = QDateTime::fromString(QStringLiteral("2026-03-20 12:00:00"), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    QDateTime t3 = QDateTime::fromString(QStringLiteral("2026-03-20 14:00:00"), QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    HistoryEntry hNative;
    hNative.id = 1;
    hNative.timestamp = t1;
    hNative.command = QStringLiteral("pacman -S neovim");
    hNative.result = QStringLiteral("Success");
    hNative.packagesAltered = 1;

    HistoryEntry hFlatpak;
    hFlatpak.id = 2;
    hFlatpak.timestamp = t3; // newest
    hFlatpak.command = QStringLiteral("[Flatpak] install org.videolan.VLC");
    hFlatpak.result = QStringLiteral("Success");
    hFlatpak.packagesAltered = 1;

    HistoryEntry hSnap;
    hSnap.id = 3;
    hSnap.timestamp = t2;
    hSnap.command = QStringLiteral("[Snap] Install \"chromium\" snap");
    hSnap.result = QStringLiteral("Success");
    hSnap.packagesAltered = 1;

    // Duplicate entry of Flatpak operation
    HistoryEntry hFlatpakDup = hFlatpak;

    auto merged = HistoryModel::mergeHistory({hNative}, {hFlatpak, hFlatpakDup}, {hSnap}, 50);

    // Must have exactly 3 entries (the duplicate Flatpak entry suppressed)
    QCOMPARE(merged.size(), 3);

    // Sorted descending by timestamp: t3 (Flatpak) > t2 (Snap) > t1 (Native)
    QCOMPARE(merged.at(0).command, QStringLiteral("[Flatpak] install org.videolan.VLC"));
    QCOMPARE(merged.at(1).command, QStringLiteral("[Snap] Install \"chromium\" snap"));
    QCOMPARE(merged.at(2).command, QStringLiteral("[Nativ] pacman -S neovim"));
}

void StoreUpdatesHistoryTest::testHistoryDbDeduplicationAndSourceTarget() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.path() + QStringLiteral("/test_history_dedup.db");

    lut::HistoryDb db(dbPath);
    QVERIFY(db.open());

    // Record initial transaction
    qint64 id1 = db.recordTransaction(1500, 1024, 2048, QStringLiteral("Success"),
                                      QStringLiteral("Install VLC"), QStringLiteral("flatpak"), QStringLiteral("org.videolan.VLC"));
    QVERIFY(id1 > 0);

    // Record immediate duplicate transaction (same result, summary, source within 5s)
    qint64 id2 = db.recordTransaction(1500, 1024, 2048, QStringLiteral("Success"),
                                      QStringLiteral("Install VLC"), QStringLiteral("flatpak"), QStringLiteral("org.videolan.VLC"));
    // Must return existing ID without creating a duplicate record
    QCOMPARE(id1, id2);

    auto list = db.recentTransactions(10);
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.first().source, QStringLiteral("flatpak"));
    QCOMPARE(list.first().target, QStringLiteral("org.videolan.VLC"));
    QCOMPARE(list.first().summary, QStringLiteral("Install VLC"));
}

QTEST_MAIN(StoreUpdatesHistoryTest)
#include "store_updates_history_test.moc"
