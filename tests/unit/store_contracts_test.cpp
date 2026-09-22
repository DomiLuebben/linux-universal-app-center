#include <QTest>
#include <QFile>
#include <QJsonDocument>
#include "liblut/transaction/TransactionTypes.h"
#include "liblut/catalog/PackageCatalog.h"
#include "linux-update-tool/models/TransactionPlanModel.h"
#include "linux-update-tool/models/UpdatesModel.h"
#include "linux-update-tool/DaemonClient.h"

using namespace lut;

class StoreContractsTest : public QObject {
    Q_OBJECT

private slots:
    void testPackageRefValidation();
    void testPackageRefRoundtrip();
    void testPackageOfferRoundtrip();
    void testInstalledStateRoundtrip();
    void testTransactionIntentRoundtrip();
    void testTransactionPlanAndFingerprint();
    void testTransactionSnapshotRoundtrip();
    void testAppRecordRoundtrip();
    void testCatalogQueryResult();
    void testTransactionPlanModelCounting();
    void testPlanReadyDoesNotOverwriteUpdatesModel(); // TX-27
    void testReplayFixtureCompatibility();
};

void StoreContractsTest::testPackageRefValidation() {
    PackageRef validRef{"alpm", "extra", "firefox", "x86_64", "130.0-1"};
    QVERIFY(validRef.isValid());

    PackageRef invalidEmptyName{"alpm", "extra", "", "x86_64", "130.0-1"};
    QVERIFY(!invalidEmptyName.isValid());

    PackageRef invalidDashName{"alpm", "extra", "-rf", "x86_64", "1.0"};
    QVERIFY(!invalidDashName.isValid());

    PackageRef invalidPathName{"alpm", "extra", "foo/bar", "x86_64", "1.0"};
    QVERIFY(!invalidPathName.isValid());

    PackageRef invalidLongArch{"alpm", "extra", "firefox", QString(40, 'a'), "1.0"};
    QVERIFY(!invalidLongArch.isValid());
}

void StoreContractsTest::testPackageRefRoundtrip() {
    PackageRef original{"alpm", "extra", "kwrite", "x86_64", "24.08.0-1"};
    QJsonObject json = original.toJson();
    PackageRef parsed = PackageRef::fromJson(json);
    QCOMPARE(parsed, original);
}

void StoreContractsTest::testPackageOfferRoundtrip() {
    PackageOffer original;
    original.packages = {
        PackageRef{"alpm", "extra", "kwrite", "x86_64", "24.08.0-1"},
        PackageRef{"alpm", "extra", "kwrite-data", "any", "24.08.0-1"}
    };
    original.priority = 10;
    original.isCandidate = true;
    original.downloadSize = 5120000;
    original.installedSize = 18400000;
    original.available = true;
    original.unavailabilityReason = QString();

    QJsonObject json = original.toJson();
    PackageOffer parsed = PackageOffer::fromJson(json);
    QCOMPARE(parsed, original);
}

void StoreContractsTest::testInstalledStateRoundtrip() {
    InstalledState original;
    original.installedPackages = {
        PackageRef{"alpm", "extra", "kwrite", "x86_64", "24.08.0-1"}
    };
    original.isFullyInstalled = true;
    original.isPartiallyInstalled = false;
    original.origin = QStringLiteral("archlinux");
    original.launchableDesktopIds = {QStringLiteral("org.kde.kwrite.desktop")};
    original.inventoryRevision = 42;

    QJsonObject json = original.toJson();
    InstalledState parsed = InstalledState::fromJson(json);
    QCOMPARE(parsed, original);
}

void StoreContractsTest::testTransactionIntentRoundtrip() {
    TransactionIntent original;
    original.type = TransactionIntent::Type::Install;
    original.targets = {PackageRef{"alpm", "extra", "vlc", "x86_64", "3.0.21-1"}};
    original.appKey = QStringLiteral("org.videolan.vlc");
    original.options.insert(QStringLiteral("refreshFirst"), true);

    QJsonObject json = original.toJson();
    TransactionIntent parsed = TransactionIntent::fromJson(json);
    QCOMPARE(parsed, original);

    // Fallback auf UpgradeAll bei ungültigem String
    QCOMPARE(TransactionIntent::typeFromString(QStringLiteral("Invalid")), TransactionIntent::Type::UpgradeAll);
}

void StoreContractsTest::testTransactionPlanAndFingerprint() {
    TransactionPlan plan1;
    plan1.id = QStringLiteral("tx-1");
    plan1.backendRevision = QStringLiteral("rev-100");
    plan1.downloadBytes = 1048576;
    plan1.installedSizeDelta = 2097152;
    plan1.warnings = {QStringLiteral("Warnung 1")};
    plan1.hasProtectedPackageConflict = false;
    plan1.affectedApps = {QStringLiteral("org.kde.kwrite")};

    PackageOp op1;
    op1.id = QStringLiteral("kwrite-1");
    op1.name = QStringLiteral("kwrite");
    op1.version = QStringLiteral("24.05.0-1");
    op1.newVersion = QStringLiteral("24.08.0-1");
    op1.arch = QStringLiteral("x86_64");
    op1.repo = QStringLiteral("extra");
    op1.kind = PackageOp::Kind::Upgrade;
    plan1.ops.append(op1);

    plan1.planRevision = plan1.calculateFingerprint();
    QVERIFY(!plan1.planRevision.isEmpty());

    // Roundtrip
    QJsonObject json = plan1.toJson();
    TransactionPlan parsed = TransactionPlan::fromJson(json);
    QCOMPARE(parsed.id, plan1.id);
    QCOMPARE(parsed.planRevision, plan1.planRevision);
    QCOMPARE(parsed.ops.size(), 1);
    QCOMPARE(parsed.ops[0].name, QStringLiteral("kwrite"));
    QCOMPARE(parsed.downloadBytes, 1048576);

    // Fingerprint ändert sich bei anderer Version
    TransactionPlan plan2 = plan1;
    plan2.ops[0].newVersion = QStringLiteral("24.08.1-1");
    QString fp2 = plan2.calculateFingerprint();
    QVERIFY(plan1.planRevision != fp2);
}

void StoreContractsTest::testTransactionSnapshotRoundtrip() {
    TransactionSnapshot original;
    original.transactionPath = QStringLiteral("/org/linuxupdatetool/Transaction/1");
    original.intent.type = TransactionIntent::Type::Install;
    original.phase = Phase::Download;
    original.canCancel = true;
    original.sequenceNumber = 12;
    original.lastResult = Result::Success;
    original.statusMessage = QStringLiteral("Pakete werden geladen");
    original.progressPercent = 45;
    original.timestamp = QDateTime::currentDateTime();

    QJsonObject json = original.toJson();
    TransactionSnapshot parsed = TransactionSnapshot::fromJson(json);
    QCOMPARE(parsed.transactionPath, original.transactionPath);
    QCOMPARE(parsed.intent.type, original.intent.type);
    QCOMPARE(parsed.phase, original.phase);
    QCOMPARE(parsed.canCancel, original.canCancel);
    QCOMPARE(parsed.sequenceNumber, original.sequenceNumber);
    QCOMPARE(parsed.progressPercent, original.progressPercent);
}

void StoreContractsTest::testAppRecordRoundtrip() {
    AppRecord original;
    original.appKey = QStringLiteral("org.kde.kwrite");
    original.componentId = QStringLiteral("org.kde.kwrite.desktop");
    original.name = QStringLiteral("KWrite");
    original.summary = QStringLiteral("Texteditor");
    original.description = QStringLiteral("Ein mächtiger Texteditor von KDE.");
    original.developer = QStringLiteral("KDE");
    original.license = QStringLiteral("LGPL-2.0-or-later");
    original.urlHomepage = QStringLiteral("https://apps.kde.org/kwrite/");
    original.iconSource = QStringLiteral("kwrite");
    original.screenshots = {QStringLiteral("https://cdn.kde.org/screenshots/kwrite.png")};
    original.categories = {QStringLiteral("Werkzeuge"), QStringLiteral("Entwicklung")};
    original.keywords = {QStringLiteral("text"), QStringLiteral("editor")};
    original.launchableDesktopIds = {QStringLiteral("org.kde.kwrite.desktop")};
    original.origin = QStringLiteral("archlinux");
    original.defaultPackageName = QStringLiteral("kwrite");

    QJsonObject json = original.toJson();
    AppRecord parsed = AppRecord::fromJson(json);
    QCOMPARE(parsed, original);
}

void StoreContractsTest::testCatalogQueryResult() {
    CatalogQueryResult res;
    res.status = CatalogQueryResult::Status::Success;
    res.generation = 3;
    QVERIFY(res.isSuccess());
    QCOMPARE(CatalogQueryResult::statusToString(CatalogQueryResult::Status::CatalogMissing),
             QStringLiteral("CatalogMissing"));
    QCOMPARE(CatalogQueryResult::statusFromString(QStringLiteral("CatalogMissing")),
             CatalogQueryResult::Status::CatalogMissing);
}

void StoreContractsTest::testTransactionPlanModelCounting() {
    TransactionPlanModel model;
    QCOMPARE(model.totalCount(), 0);
    QVERIFY(model.isEmpty());

    QList<PackageOp> ops;
    PackageOp opInst;
    opInst.name = QStringLiteral("vlc");
    opInst.kind = PackageOp::Kind::Install;
    opInst.downloadSize = 1000000;
    ops.append(opInst);

    PackageOp opDep;
    opDep.name = QStringLiteral("libvlc");
    opDep.kind = PackageOp::Kind::Install;
    opDep.downloadSize = 2000000;
    ops.append(opDep);

    PackageOp opRem;
    opRem.name = QStringLiteral("old-codec");
    opRem.kind = PackageOp::Kind::Remove;
    ops.append(opRem);

    model.setOps(ops, 3000000, 5000000, {QStringLiteral("Achtung")}, QStringLiteral("VLC installieren"));
    QCOMPARE(model.totalCount(), 3);
    QCOMPARE(model.installCount(), 2);
    QCOMPARE(model.removeCount(), 1);
    QCOMPARE(model.upgradeCount(), 0);
    QCOMPARE(model.actionTitle(), QStringLiteral("VLC installieren"));
    QVERIFY(!model.planRevision().isEmpty());
    QCOMPARE(model.warnings().size(), 1);

    model.clear();
    QCOMPARE(model.totalCount(), 0);
    QVERIFY(model.isEmpty());
}

// TX-27: Store-Installationsplan trifft ein, Updatebestand ist bereits geladen
// Updatebestand bleibt erhalten; Vorschau nutzt eigenes Modell
void StoreContractsTest::testPlanReadyDoesNotOverwriteUpdatesModel() {
    DaemonClient client;

    client.init(QStringLiteral(PROJECT_DIR "/tests/fixtures/small-update.jsonl"), 0.0);

    // 1. Systemupdates simulieren (m_isUpgradePlan = true)
    PackageOp up1;
    up1.name = QStringLiteral("glibc");
    up1.version = QStringLiteral("2.39");
    up1.newVersion = QStringLiteral("2.40");
    up1.kind = PackageOp::Kind::Upgrade;

    PackageOp up2;
    up2.name = QStringLiteral("systemd");
    up2.version = QStringLiteral("256");
    up2.newVersion = QStringLiteral("257");
    up2.kind = PackageOp::Kind::Upgrade;

    client.refreshUpdates(); // setzt m_isUpgradePlan = true
    PlanReady upgradePlan;
    upgradePlan.ops = {up1, up2};

    // Event manuell via private/replayed Simulation einspeisen
    // Da handleEvent private ist, prüfen wir den Effekt über QMetaObject::invokeMethod
    QVERIFY(QMetaObject::invokeMethod(&client, "handleEvent",
                                      Q_ARG(lut::Event, lut::Event(upgradePlan))));

    QCOMPARE(client.updatesModel()->totalCount(), 2);
    QCOMPARE(client.planModel()->totalCount(), 2);

    // 2. Store-Installation anfordern -> m_isUpgradePlan wird false
    client.planStoreInstall(QStringLiteral("kwrite"), QStringLiteral("extra"));
    QVERIFY(!client.hasPlan());
    QCOMPARE(client.updatesModel()->totalCount(), 2); // Updates bleiben erhalten

    // 3. Store PlanReady trifft ein mit neuen/anderen Paketen
    PackageOp storeOp;
    storeOp.name = QStringLiteral("kwrite");
    storeOp.newVersion = QStringLiteral("24.08.0");
    storeOp.kind = PackageOp::Kind::Install;

    PlanReady storePlan;
    storePlan.ops = {storeOp};

    QVERIFY(QMetaObject::invokeMethod(&client, "handleEvent",
                                      Q_ARG(lut::Event, lut::Event(storePlan))));

    // VERIFIKATION TX-27:
    // UpdatesModel hat weiterhin die 2 Systemupdates (glibc, systemd)!
    QCOMPARE(client.updatesModel()->totalCount(), 2);
    QCOMPARE(client.updatesModel()->selectedPackages().size(), 2);

    // TransactionPlanModel hat genau das Store-Paket (kwrite)!
    QCOMPARE(client.planModel()->totalCount(), 1);
    QCOMPARE(client.planModel()->ops().at(0).name, QStringLiteral("kwrite"));
}

void StoreContractsTest::testReplayFixtureCompatibility() {
    QFile file(QStringLiteral(PROJECT_DIR "/tests/fixtures/small-update.jsonl"));
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), "Fixture-Datei konnte nicht geöffnet werden");

    int eventCount = 0;
    while (!file.atEnd()) {
        QByteArray line = file.readLine().trimmed();
        if (line.isEmpty()) continue;
        QJsonDocument doc = QJsonDocument::fromJson(line);
        QVERIFY(doc.isObject());
        QJsonObject root = doc.object();
        QJsonObject eventObj = root.contains(QStringLiteral("event"))
                                   ? root.value(QStringLiteral("event")).toObject()
                                   : root;
        auto ev = deserializeEvent(eventObj);
        QVERIFY(ev.has_value());
        eventCount++;
    }
    QVERIFY(eventCount > 0);
}

QTEST_MAIN(StoreContractsTest)
#include "store_contracts_test.moc"
