#include <QTest>
#include <QSignalSpy>
#include <QDBusObjectPath>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "lutd/TransactionManager.h"
#include "linux-update-tool/DaemonClient.h"
#include "liblut/protocol/events.h"
#include "liblut/backend/Backend.h"
#include "liblut/backend/replay/ReplayBackend.h"

using namespace lut;

// Dummy Test-Backend zur präzisen Steuerung von Events
class MockDbusBackend : public Backend {
    Q_OBJECT
public:
    Capabilities caps;
    bool cancelCalled = false;
    bool commitCalled = false;

    Capabilities capabilities() const override {
        Capabilities c = caps;
        c.install = true;
        c.remove = true;
        c.transactionReattach = true;
        c.typedPackageTargets = true;
        return c;
    }

    void refreshMetadata() override {}
    void planUpgradeAll(const UpgradeOptions &) override {}
    void planInstall(const QStringList &) override {}
    void planRemove(const QStringList &) override {}
    void planPackageTransaction(const TransactionIntent &) override {}
    void commitPlan(const QString &) override { commitCalled = true; }
    void discardPlan() override {}
    void commit() override { commitCalled = true; }
    void cancel() override { cancelCalled = true; }
    void answerQuestion(const QString &, const QJsonObject &) override {}

    QList<PackageOp> availableUpdates() override { return {}; }
    QList<InstalledPackage> installedPackages(const QString & = QString()) override { return {}; }
    QList<ChangelogEntry> changelog(const QString &) override { return {}; }
    QList<HistoryEntry> history(int = 20) override { return {}; }

    void emitMockEvent(const Event &event) {
        emit eventEmitted(event);
    }
};

class StoreDbusTest : public QObject {
    Q_OBJECT

private:
    static QString eventJson(const Event &event, quint64 seq = 0, const QString &path = QStringLiteral("/org/linuxupdatetool/Transaction/1")) {
        QJsonObject obj = serializeEvent(event, seq, path);
        return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }

    static void deliver(DaemonClient &client, const QDBusObjectPath &path, const Event &event, quint64 seq = 0) {
        QMetaObject::invokeMethod(
            &client, "onDbusTransactionEvent", Qt::DirectConnection,
            Q_ARG(QDBusObjectPath, path),
            Q_ARG(QString, eventJson(event, seq, path.path()))
        );
    }

private slots:
    void testReattachActiveTransaction();
    void testSecondClientRejected();
    void testOwnershipSameUidAllowed();
    void testOwnershipDifferentUidRejected();
    void testEventBeforeMethodReply();
    void testMonotoneEventSequence();
    void testDuplicateTerminalEvents();
    void testInventoryRefreshOnAllTerminalStates();
    void testCancellationBoundToPhase();
    void testRingBufferOverflow();
    void testConnectionLossDoesNotReportInstallationFailed();
};

void StoreDbusTest::testReattachActiveTransaction() {
    TransactionManager tm;
    auto mockBackend = std::make_unique<MockDbusBackend>();
    auto *backendPtr = mockBackend.get();
    tm.setBackendForTest(std::move(mockBackend));

    QVariantList targets;
    QVariantMap target;
    target[QStringLiteral("name")] = QStringLiteral("inkscape");
    targets.append(target);

    // Transaktion 1 starten
    QDBusObjectPath path = tm.PlanPackageTransaction(QStringLiteral("Install"), targets, {});
    QCOMPARE(path.path(), QStringLiteral("/org/linuxupdatetool/Transaction/1"));

    // Backend liefert Plan
    PlanReady plan;
    PackageOp op;
    op.name = QStringLiteral("inkscape");
    op.kind = PackageOp::Kind::Install;
    op.downloadSize = 35000000;
    plan.ops.append(op);
    plan.planRevision = QStringLiteral("rev-12345");
    backendPtr->emitMockEvent(plan);

    // Commit ausführen und in Download-Phase wechseln
    tm.CommitPlan(path, QStringLiteral("rev-12345"));
    backendPtr->emitMockEvent(PhaseChanged{Phase::Download, QStringLiteral("Pakete werden geladen …"), true});
    backendPtr->emitMockEvent(DownloadThroughput{1024000, 5000000, 35000000});

    // Prüfen: Transaktion ist aktiv
    QList<QDBusObjectPath> active = tm.GetActiveTransactions();
    QCOMPARE(active.size(), 1);
    QCOMPARE(active.first().path(), QStringLiteral("/org/linuxupdatetool/Transaction/1"));

    // Snapshot prüfen
    QString snapJson = tm.AttachTransaction(path, 0);
    QJsonDocument doc = QJsonDocument::fromJson(snapJson.toUtf8());
    QVERIFY(doc.isObject());
    TransactionSnapshot snap = TransactionSnapshot::fromJson(doc.object());
    QCOMPARE(snap.transactionPath, QStringLiteral("/org/linuxupdatetool/Transaction/1"));
    QCOMPARE(snap.phase, Phase::Download);
    QCOMPARE(snap.plan.ops.size(), 1);
    QCOMPARE(snap.plan.ops.first().name, QStringLiteral("inkscape"));
    QVERIFY(snap.active);
    QVERIFY(snap.canCancel);
}

void StoreDbusTest::testSecondClientRejected() {
    TransactionManager tm;
    auto mockBackend = std::make_unique<MockDbusBackend>();
    tm.setBackendForTest(std::move(mockBackend));

    QVariantList targets;
    QVariantMap target;
    target[QStringLiteral("name")] = QStringLiteral("gimp");
    targets.append(target);

    // Erste Transaktion starten
    QDBusObjectPath path1 = tm.PlanPackageTransaction(QStringLiteral("Install"), targets, {});
    QCOMPARE(path1.path(), QStringLiteral("/org/linuxupdatetool/Transaction/1"));

    // Zweite Transaktion anfordern während erste aktiv ist
    QVariantMap opts;
    QDBusObjectPath path2 = tm.PlanUpgrade(opts);
    // Sollte abgewiesen werden (leerer Pfad /)
    QCOMPARE(path2.path(), QStringLiteral("/"));

    // Auch ein zweiter Store-Aufruf wird abgewiesen
    QDBusObjectPath path3 = tm.PlanPackageTransaction(QStringLiteral("Install"), targets, {});
    QCOMPARE(path3.path(), QStringLiteral("/"));
}

void StoreDbusTest::testOwnershipSameUidAllowed() {
    TransactionManager tm;
    auto mockBackend = std::make_unique<MockDbusBackend>();
    auto *backendPtr = mockBackend.get();
    tm.setBackendForTest(std::move(mockBackend));

    // Aufrufer-UID auf 1000 setzen
    tm.setCallerUidForTest(1000);

    QVariantList targets;
    QVariantMap target;
    target[QStringLiteral("name")] = QStringLiteral("blender");
    targets.append(target);

    QDBusObjectPath path = tm.PlanPackageTransaction(QStringLiteral("Install"), targets, {});
    QCOMPARE(tm.callerUidForTest(), static_cast<quint32>(1000));

    // PlanReady emittieren
    PlanReady plan;
    plan.planRevision = QStringLiteral("rev-blender");
    backendPtr->emitMockEvent(plan);

    // Neuer Aufruf mit selbiger UID (z.B. neugestarteter GUI-Prozess)
    tm.setCallerUidForTest(1000);

    // Plan verwerfen ist zulässig für selbige UID
    tm.DiscardPlan(path);
    QVERIFY(tm.GetActiveTransactions().isEmpty());
}

void StoreDbusTest::testOwnershipDifferentUidRejected() {
    TransactionManager tm;
    auto mockBackend = std::make_unique<MockDbusBackend>();
    auto *backendPtr = mockBackend.get();
    tm.setBackendForTest(std::move(mockBackend));

    tm.setCallerUidForTest(1000);

    QVariantList targets;
    QVariantMap target;
    target[QStringLiteral("name")] = QStringLiteral("vlc");
    targets.append(target);

    QDBusObjectPath path = tm.PlanPackageTransaction(QStringLiteral("Install"), targets, {});
    PlanReady plan;
    plan.planRevision = QStringLiteral("rev-vlc");
    backendPtr->emitMockEvent(plan);

    // Fremder Benutzer (UID 1001) versucht Commit/Discard
    tm.setCallerUidForTest(1001);

    // DiscardPlan darf nicht ausgeführt werden
    tm.DiscardPlan(path);

    // Transaktion muss weiterhin aktiv / vorhanden sein
    QCOMPARE(tm.GetActiveTransactions().size(), 1);
}

void StoreDbusTest::testEventBeforeMethodReply() {
    DaemonClient client;
    Capabilities caps;
    caps.install = true;
    client.setCapabilitiesForTest(caps);

    // Simuliere: Client hat Anfrage abgesendet (pending)
    // Event trifft ein, bevor der RPC-Rückgabewert den Pfad setzt
    QDBusObjectPath txPath(QStringLiteral("/org/linuxupdatetool/Transaction/42"));

    // Event ohne pending wird nicht direkt übernommen
    deliver(client, txPath, PhaseChanged{Phase::Download, QStringLiteral("Downloading ..."), true}, 1);
    QCOMPARE(client.isBusy(), false);

    // Jetzt mit pending-Transaktion (Anfrage abgesendet, Antwort noch ausstehend):
    client.setPendingTransactionForTest(true);
    deliver(client, txPath, PhaseChanged{Phase::Download, QStringLiteral("Downloading ..."), true}, 2);
    // Jetzt wurde txPath übernommen!
    QCOMPARE(client.isBusy(), true);

    // Event einer alten Transaktion 41 wird ignoriert
    QDBusObjectPath oldTxPath(QStringLiteral("/org/linuxupdatetool/Transaction/41"));
    deliver(client, oldTxPath, PhaseChanged{Phase::Failed, QStringLiteral("Old error"), false}, 3);
    QVERIFY(!client.hasError()); // Altes Event wurde ignoriert!
}

void StoreDbusTest::testMonotoneEventSequence() {
    DaemonClient client;
    client.setPendingTransactionForTest(true);
    QDBusObjectPath txPath(QStringLiteral("/org/linuxupdatetool/Transaction/1"));
    deliver(client, txPath, ItemStarted{QStringLiteral("kate"), PackageOp::Kind::Install, 100}, 1);
    QCOMPARE(client.logModel()->rowCount(), 1);

    deliver(client, txPath, ItemStarted{QStringLiteral("kwrite"), PackageOp::Kind::Install, 100}, 2);
    QCOMPARE(client.logModel()->rowCount(), 2);

    // Duplikat mit Sequenz 2 erneut liefern:
    deliver(client, txPath, ItemStarted{QStringLiteral("kwrite"), PackageOp::Kind::Install, 100}, 2);
    // Darf NICHT erneut ins Log oder verarbeitet werden:
    QCOMPARE(client.logModel()->rowCount(), 2);

    // Veraltete Sequenz 1 erneut liefern:
    deliver(client, txPath, ItemStarted{QStringLiteral("kate"), PackageOp::Kind::Install, 100}, 1);
    QCOMPARE(client.logModel()->rowCount(), 2);
}

void StoreDbusTest::testDuplicateTerminalEvents() {
    DaemonClient client;
    QDBusObjectPath txPath(QStringLiteral("/org/linuxupdatetool/Transaction/1"));

    QSignalSpy spyFinished(&client, &DaemonClient::transactionFinished);
    client.planStoreInstall(QStringLiteral("kcalc"));

    // Erstes Terminal Event
    deliver(client, txPath, TransactionDone{Result::Success, QStringLiteral("Erfolgreich installiert"), false, {}, 1}, 10);
    QCOMPARE(spyFinished.count(), 1);
    QCOMPARE(client.statusMessage(), QStringLiteral("Erfolgreich installiert"));

    // Zweites identisches Terminal Event (z.B. durch Signal-Prellen oder mehrfachen Worker-Exit)
    deliver(client, txPath, TransactionDone{Result::Success, QStringLiteral("Erfolgreich installiert"), false, {}, 11}, 11);
    // Darf nicht doppelt gefeuert werden!
    QCOMPARE(spyFinished.count(), 1);
}

void StoreDbusTest::testInventoryRefreshOnAllTerminalStates() {
    // 1. Success
    {
        DaemonClient client;
        QSignalSpy spy(&client, &DaemonClient::transactionFinished);
        client.planStoreInstall(QStringLiteral("app1"));
        deliver(client, QDBusObjectPath(QStringLiteral("/org/linuxupdatetool/Transaction/1")),
                TransactionDone{Result::Success, QStringLiteral("Success"), false, {}, 1}, 1);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().value<Result>(), Result::Success);
    }
    // 2. SuccessWithWarnings
    {
        DaemonClient client;
        QSignalSpy spy(&client, &DaemonClient::transactionFinished);
        client.planStoreInstall(QStringLiteral("app2"));
        deliver(client, QDBusObjectPath(QStringLiteral("/org/linuxupdatetool/Transaction/2")),
                TransactionDone{Result::SuccessWithWarnings, QStringLiteral("Warnings"), false, {}, 2}, 1);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().value<Result>(), Result::SuccessWithWarnings);
    }
    // 3. Failed
    {
        DaemonClient client;
        QSignalSpy spy(&client, &DaemonClient::transactionFinished);
        client.planStoreInstall(QStringLiteral("app3"));
        deliver(client, QDBusObjectPath(QStringLiteral("/org/linuxupdatetool/Transaction/3")),
                TransactionDone{Result::Failed, QStringLiteral("Failed"), false, {}, 3}, 1);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().value<Result>(), Result::Failed);
    }
    // 4. Cancelled
    {
        DaemonClient client;
        QSignalSpy spy(&client, &DaemonClient::transactionFinished);
        client.planStoreInstall(QStringLiteral("app4"));
        deliver(client, QDBusObjectPath(QStringLiteral("/org/linuxupdatetool/Transaction/4")),
                TransactionDone{Result::Cancelled, QStringLiteral("Cancelled"), false, {}, 4}, 1);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().value<Result>(), Result::Cancelled);
    }
}

void StoreDbusTest::testCancellationBoundToPhase() {
    TransactionManager tm;
    auto mockBackend = std::make_unique<MockDbusBackend>();
    auto *backendPtr = mockBackend.get();
    tm.setBackendForTest(std::move(mockBackend));

    QVariantList targets;
    QVariantMap target;
    target[QStringLiteral("name")] = QStringLiteral("nano");
    targets.append(target);

    QDBusObjectPath path = tm.PlanPackageTransaction(QStringLiteral("Install"), targets, {});
    PlanReady plan;
    plan.planRevision = QStringLiteral("rev-nano");
    backendPtr->emitMockEvent(plan);

    // In Download-Phase wechseln -> Abbrechen erlaubt!
    backendPtr->emitMockEvent(PhaseChanged{Phase::Download, QStringLiteral("Download"), true});
    tm.Cancel(path);
    QCoreApplication::processEvents();
    QVERIFY(backendPtr->cancelCalled);

    // In Commit-Phase wechseln -> Abbrechen verboten!
    backendPtr->cancelCalled = false;
    backendPtr->emitMockEvent(PhaseChanged{Phase::Commit, QStringLiteral("Commit"), false});
    tm.Cancel(path);
    QCoreApplication::processEvents();
    // cancel() am Backend darf NICHT aufgerufen worden sein!
    QVERIFY(!backendPtr->cancelCalled);
}

void StoreDbusTest::testRingBufferOverflow() {
    TransactionManager tm;
    auto mockBackend = std::make_unique<MockDbusBackend>();
    auto *backendPtr = mockBackend.get();
    tm.setBackendForTest(std::move(mockBackend));

    QVariantList targets;
    QVariantMap target;
    target[QStringLiteral("name")] = QStringLiteral("vim");
    targets.append(target);

    QDBusObjectPath path = tm.PlanPackageTransaction(QStringLiteral("Install"), targets, {});

    // 5200 Events emittieren (Ringpuffer-Limit ist 5000)
    for (int i = 1; i <= 5200; ++i) {
        backendPtr->emitMockEvent(ItemProgress{QStringLiteral("vim"), i, 5200});
    }

    // Historie darf 5000 nicht überschreiten
    QStringList history = tm.GetEventHistory(path);
    QCOMPARE(history.size(), 5000);
    QCOMPARE(tm.sequenceCounter(), static_cast<quint64>(5200));

    // Snapshot bleibt valide
    QString snapJson = tm.AttachTransaction(path, 0);
    QJsonDocument doc = QJsonDocument::fromJson(snapJson.toUtf8());
    QVERIFY(doc.isObject());
    TransactionSnapshot snap = TransactionSnapshot::fromJson(doc.object());
    QCOMPARE(snap.sequenceNumber, static_cast<quint64>(5200));
}

void StoreDbusTest::testConnectionLossDoesNotReportInstallationFailed() {
    DaemonClient client;
    QSignalSpy connSpy(&client, &DaemonClient::connectionChanged);
    QSignalSpy statusSpy(&client, &DaemonClient::statusChanged);

    // D-Bus Verbindungsverlust simulieren
    QMetaObject::invokeMethod(
        &client, "onDbusNameOwnerChanged", Qt::DirectConnection,
        Q_ARG(QString, QStringLiteral("org.linuxupdatetool.Daemon1")),
        Q_ARG(QString, QStringLiteral(":1.50")),
        Q_ARG(QString, QString()) // newOwner leer -> Dienst weg
    );

    QVERIFY(!client.isConnected());
    QVERIFY(!client.hasError()); // Kein falsches "Installation fehlgeschlagen"
    QVERIFY(client.statusMessage().contains(QStringLiteral("Verbindung zum Systemdienst verloren")));
    QVERIFY(connSpy.count() >= 1);
    QVERIFY(statusSpy.count() >= 1);
}

QTEST_MAIN(StoreDbusTest)
#include "store_dbus_test.moc"
