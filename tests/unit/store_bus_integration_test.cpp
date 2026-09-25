#include <QtTest>
#include <QProcess>
#include <QDBusReply>
#include <QDBusConnectionInterface>
#include <QJsonDocument>
#include <QThread>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include <atomic>
#include "linux-app-store/catalog/ApplicationStore.h"
#include "linux-app-store/theme/SystemPalette.h"
#include "linux-app-store/media/MediaCache.h"
#include "lutd/TransactionManager.h"
#include "linux-app-store/DaemonClient.h"
using namespace lut;

// A real bus and separate daemon process; only package mutation and PolicyKit
// are replaced here. Native package operations are covered by container tests.
class BusBackend : public Backend {
public:
    Capabilities capabilities() const override {
        Capabilities c; c.install = true; c.remove = true; c.typedPackageTargets = true;
        c.transactionReattach = true; c.protocolVersion = 2; return c;
    }
    void refreshMetadata() override {}
    void planUpgradeAll(const UpgradeOptions &) override {
        QList<PackageOp> ops;
        for (const char *name : {"mesa", "linux-firmware"}) {
            PackageOp op; op.name = QString::fromLatin1(name); op.arch = QStringLiteral("x86_64");
            op.kind = PackageOp::Kind::Upgrade; op.version = QStringLiteral("1"); op.newVersion = QStringLiteral("2");
            ops.append(op);
        }
        PlanReady plan; plan.ops = ops; plan.planRevision = QStringLiteral("upgrade-revision");
        emit eventEmitted(plan);
        emit eventEmitted(PhaseChanged{Phase::Idle, QStringLiteral("Ready"), false});
    }
    void planInstall(const QStringList &) override {}
    void planRemove(const QStringList &) override {}
    void planPackageTransaction(const TransactionIntent &intent) override {
        emit eventEmitted(PhaseChanged{Phase::Resolve, QStringLiteral("Resolving"), false});
        QThread::msleep(700); // A slow native resolver must not block the bus.
        PackageOp op; op.name = intent.targets.first().name; op.arch = QStringLiteral("x86_64");
        op.kind = intent.type == TransactionIntent::Type::Remove ? PackageOp::Kind::Remove : PackageOp::Kind::Install;
        op.newVersion = QStringLiteral("1"); op.downloadSize = 123;
        PlanReady plan; plan.ops = {op}; plan.planRevision = QStringLiteral("native-revision-which-is-not-a-gui-fingerprint");
        emit eventEmitted(plan);
        emit eventEmitted(PhaseChanged{Phase::Idle, QStringLiteral("Ready"), false});
    }
    void commit() override {
        emit eventEmitted(PhaseChanged{Phase::Commit, QStringLiteral("Committing"), false});
        QTimer::singleShot(500, this, [this] {
            emit eventEmitted(TransactionDone{Result::Success, QStringLiteral("Committed once"), false, {}, 0});
        });
    }
    void cancel() override { emit eventEmitted(TransactionDone{Result::Cancelled, QStringLiteral("Discarded"), false, {}, 0}); }
    void answerQuestion(const QString &, const QJsonObject &) override {}
    QList<PackageOp> availableUpdates() override { return {}; }
    QList<InstalledPackage> installedPackages(const QString &) override { return {}; }
    QList<ChangelogEntry> changelog(const QString &) override { return {}; }
    QList<HistoryEntry> history(int) override { return {}; }
};
class TestManager : public TransactionManager {
protected:
    bool authorize(const QString &, const QString &) override { return true; }
};
class GuiCatalog : public PackageCatalog {
public:
    std::atomic<bool> installed{false};
    quint64 catalogGeneration() const override { return 1; }
    QList<PackageOffer> offersForPackage(const QString &name) override {
        if (name != QLatin1String("kwrite")) return {};
        PackageOffer offer; offer.available = true; offer.isCandidate = true;
        offer.packages = {PackageRef{QStringLiteral("alpm"), QStringLiteral("extra"), name, QStringLiteral("x86_64"), QStringLiteral("1")}};
        return {offer};
    }
    std::optional<PackageOffer> candidateOffer(const QString &name) override {
        auto offers = offersForPackage(name); return offers.isEmpty() ? std::nullopt : std::optional<PackageOffer>(offers.first());
    }
    InstalledState installedStateForPackage(const QString &name) override {
        InstalledState state;
        if (installed && name == QLatin1String("kwrite")) {
            state.isFullyInstalled = true;
            state.installedPackages = {PackageRef{QStringLiteral("alpm"), {}, name, QStringLiteral("x86_64"), QStringLiteral("1")}};
        }
        return state;
    }
    QList<InstalledPackage> allInstalledPackages() override {
        if (!installed) return {};
        InstalledPackage pkg;
        pkg.name = QStringLiteral("kwrite");
        return {pkg};
    }
    QList<PackageRef> findPackagesProvidingFile(const QString &) override { return {}; }
};
class StoreBusIntegrationTest : public QObject {
    Q_OBJECT
    QProcess daemon;
    bool startDaemon() {
        daemon.setProcessChannelMode(QProcess::MergedChannels);
        daemon.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--test-daemon")});
        return daemon.waitForStarted() && daemon.waitForReadyRead() && daemon.readAll().contains("READY");
    }
    void stopDaemon() {
        daemon.terminate();
        if (!daemon.waitForFinished(3000)) { daemon.kill(); daemon.waitForFinished(); }
    }
private slots:
    void initTestCase() {
        QVERIFY(QDBusConnection::sessionBus().isConnected());
        QVERIFY2(startDaemon(), "Private test daemon did not start");
    }
    void previewRejectCommitAndReattach() {
        auto bus = QDBusConnection::sessionBus();
        auto client = std::make_unique<DaemonClient>(bus);
        QVERIFY(client->init());
        QVERIFY(client->installSupported());
        client->planStoreInstall(QStringLiteral("example"));
        QVERIFY(client->isBusy());
        QTest::qWait(50);
        QDBusInterface control(QStringLiteral("org.linuxupdatetool.Daemon1"), QStringLiteral("/org/linuxupdatetool/Daemon1"),
            QStringLiteral("org.linuxupdatetool.Daemon1"), bus);
        QElapsedTimer timer; timer.start();
        QDBusReply<QString> caps = control.call(QStringLiteral("GetCapabilities"));
        QVERIFY(caps.isValid()); QVERIFY2(timer.elapsed() < 300, "Resolver blocked the D-Bus event loop");
        QDBusReply<QDBusObjectPath> second = control.call(QStringLiteral("PlanInstall"), QStringList{QStringLiteral("second")});
        QVERIFY(!second.isValid());
        QTRY_VERIFY(client->hasPlan());
        QCOMPARE(client->planModel()->planRevision(), QStringLiteral("native-revision-which-is-not-a-gui-fingerprint"));
        QCOMPARE(client->planModel()->rowCount(), 1);
        client->commitStorePlan(QStringLiteral("wrong-revision"));
        QTRY_VERIFY(client->hasError()); // must not fall back to unbound legacy Commit (Antwort kommt asynchron)
        client.reset();
        client = std::make_unique<DaemonClient>(bus); QVERIFY(client->init());
        QVERIFY(client->hasPlan()); QVERIFY(client->isStorePlan());
        QCOMPARE(client->planModel()->planRevision(), QStringLiteral("native-revision-which-is-not-a-gui-fingerprint"));
        client->commitStorePlan();
        QVERIFY(client->isBusy());
        client.reset(); // close GUI during commit, daemon continues
        client = std::make_unique<DaemonClient>(bus); QVERIFY(client->init());
        QVERIFY(client->isBusy());
        QSignalSpy done(client.get(), &DaemonClient::transactionFinished);
        QTRY_COMPARE(done.count(), 1);
        QCOMPARE(qvariant_cast<Result>(done.first().first()), Result::Success);
        QVERIFY(!client->hasError());
        QDBusReply<QList<QDBusObjectPath>> active = control.call(QStringLiteral("GetActiveTransactions"));
        QVERIFY(active.isValid()); QVERIFY(active.value().isEmpty());
        client->planStoreRemove(QStringLiteral("example"));
        QTRY_VERIFY(client->hasPlan());
        QCOMPARE(client->planModel()->removeCount(), 1);
        client->discardStorePlan();
        QVERIFY(!client->hasPlan());
    }
    // Die automatische Prüfung hinterlässt einen unbestätigten Systemplan. Ein neu
    // gestartetes Fenster muss ihn samt Paketliste übernehmen – als wartenden
    // Plan, nicht als laufende Transaktion und nicht als Store-Plan.
    void pendingUpgradePlanSurvivesGuiRestart() {
        auto bus = QDBusConnection::sessionBus();
        auto client = std::make_unique<DaemonClient>(bus);
        QVERIFY(client->init());
        client->refreshUpdates();
        QTRY_VERIFY(client->hasPlan());
        QVERIFY(!client->isStorePlan());
        QCOMPARE(client->updatesModel()->totalCount(), 2);
        client.reset();

        client = std::make_unique<DaemonClient>(bus);
        QVERIFY(client->init());
        QVERIFY(client->hasPlan());
        QVERIFY(!client->isBusy());
        QVERIFY(!client->isStorePlan());
        QVERIFY(client->isUpgradeTransaction());
        QCOMPARE(client->updatesModel()->totalCount(), 2);

        QDBusInterface control(QStringLiteral("org.linuxupdatetool.Daemon1"), QStringLiteral("/org/linuxupdatetool/Daemon1"),
            QStringLiteral("org.linuxupdatetool.Daemon1"), bus);
        QDBusReply<QList<QDBusObjectPath>> active = control.call(QStringLiteral("GetActiveTransactions"));
        QVERIFY(active.isValid()); QCOMPARE(active.value().size(), 1);
        control.call(QStringLiteral("DiscardPlan"), QVariant::fromValue(active.value().first()));
        active = control.call(QStringLiteral("GetActiveTransactions"));
        QVERIFY(active.isValid()); QVERIFY(active.value().isEmpty());
    }
    void actualQmlInstallPreviewAndRemove() {
        // Das Mehrquellen-Auswahlfeld setzt ein vorhandenes Flatpak voraus: ohne
        // Flatpak werden Flatpak-Angebote bewusst verborgen (1.7.0). Die Container
        // für Debian und Fedora haben kein Flatpak, der Entwicklungsrechner schon.
        CatalogService::setFlatpakAvailableOverride(true);
        struct ResetOverride { ~ResetOverride() { CatalogService::setFlatpakAvailableOverride(std::nullopt); } } resetOverride;
        DaemonClient client(QDBusConnection::sessionBus());
        QVERIFY(client.init());
        CatalogService catalog;
        catalog.setLoadStdDataLocations(false);
        catalog.addExtraDataLocation(QStringLiteral(PROJECT_DIR "/tests/fixtures/store"));
        QVERIFY(catalog.load());
        GuiCatalog packages;
        ApplicationStore store(&catalog, &packages);
        QTRY_VERIFY(store.isLoaded());
        connect(&store, &ApplicationStore::installRequested, &client,
            qOverload<const QStringList &, const QString &>(&DaemonClient::planStoreInstall));
        connect(&store, &ApplicationStore::removeRequested, &client,
            qOverload<const QStringList &>(&DaemonClient::planStoreRemove));
        connect(&store, &ApplicationStore::installPackageRefsRequested, &client,
            qOverload<const QList<PackageRef> &>(&DaemonClient::planStoreInstall));
        connect(&store, &ApplicationStore::removePackageRefsRequested, &client,
            qOverload<const QList<PackageRef> &>(&DaemonClient::planStoreRemove));
        connect(&client, &DaemonClient::statusChanged, &store, [&] {
            store.updateTransactionStatus(client.installSupported(), client.removeSupported(), client.isBusy(), client.hasPlan(), client.hasError());
        });
        connect(&client, &DaemonClient::transactionStarted, &store, &ApplicationStore::transactionStarted);
        connect(&client, &DaemonClient::transactionFinished, &store, [&](Result result) {
            if (result == Result::Success) packages.installed = client.planModel()->removeCount() == 0;
            store.transactionFinished(result);
        });
        SystemPalette palette;
        QTemporaryDir mediaDirectory;
        MediaCache media(mediaDirectory.path(), MediaCache::Limits{}, nullptr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("Theme"), &palette);
        engine.rootContext()->setContextProperty(QStringLiteral("appStore"), &store);
        engine.rootContext()->setContextProperty(QStringLiteral("daemonClient"), &client);
        engine.rootContext()->setContextProperty(QStringLiteral("mediaCache"), &media);
        QQmlComponent detailsComponent(&engine, QUrl::fromLocalFile(QStringLiteral(PROJECT_DIR "/linux-app-store/qml/pages/AppDetails.qml")));
        QScopedPointer<QObject> details(detailsComponent.createWithInitialProperties({{QStringLiteral("appKey"), QStringLiteral("org.kde.kwrite")}}));
        QVERIFY2(details, qPrintable(detailsComponent.errorString()));
        QQuickWindow window; window.resize(1100, 760);
        auto *detailsItem = qobject_cast<QQuickItem *>(details.data()); QVERIFY(detailsItem);
        detailsItem->setParentItem(window.contentItem()); detailsItem->setSize(QSizeF(1100, 760));
        window.show();

        // Multi-source ComboBox verification
        auto *sourceCombo = details->findChild<QObject *>(QStringLiteral("sourceComboBox"));
        QVERIFY(sourceCombo);
        QVERIFY(sourceCombo->property("visible").toBool());
        QCOMPARE(details->property("currentSource").toString(), QStringLiteral("alpm"));

        // Switch to Flatpak offer (index 1)
        details->setProperty("selectedOfferIndex", 1);
        QCOMPARE(details->property("currentSource").toString(), QStringLiteral("flatpak"));

        // Switch back to Native (index 0)
        details->setProperty("selectedOfferIndex", 0);
        QCOMPARE(details->property("currentSource").toString(), QStringLiteral("alpm"));

        auto *install = details->findChild<QObject *>(QStringLiteral("storeInstallButton")); QVERIFY(install);
        QTRY_VERIFY(install->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(install, "clicked"));
        QTRY_VERIFY(client.hasPlan());
        QCOMPARE(details->property("actionState").toString(), QStringLiteral("AwaitingConfirmation"));
        QVERIFY(!install->property("enabled").toBool());
        QQmlComponent previewComponent(&engine, QUrl::fromLocalFile(QStringLiteral(PROJECT_DIR "/linux-app-store/qml/pages/PlanPreview.qml")));
        QScopedPointer<QObject> preview(previewComponent.create());
        QVERIFY2(preview, qPrintable(previewComponent.errorString()));
        auto *previewItem = qobject_cast<QQuickItem *>(preview.data()); QVERIFY(previewItem);
        detailsItem->setVisible(false);
        previewItem->setParentItem(window.contentItem()); previewItem->setSize(QSizeF(1100, 760));
        QVERIFY(connect(preview.data(), SIGNAL(confirmed()), &client, SLOT(commitStorePlan())));
        auto *confirm = preview->findChild<QObject *>(QStringLiteral("storeConfirmButton")); QVERIFY(confirm);
        QTRY_VERIFY(confirm->property("enabled").toBool());
        QTest::qWait(50);
        const auto screenshot = window.grabWindow();
        QVERIFY(!screenshot.isNull());
        QVERIFY(screenshot.save(QCoreApplication::applicationDirPath() + QStringLiteral("/store-plan-preview.png")));
        QVERIFY(QMetaObject::invokeMethod(confirm, "clicked"));
        QTRY_VERIFY(!store.isLoading() && store.installedState(QStringLiteral("org.kde.kwrite")).isFullyInstalled);
        previewItem->setVisible(false); detailsItem->setVisible(true);
        QCOMPARE(details->property("actionState").toString(), QStringLiteral("InstalledNoLaunch"));

        // When switching to Flatpak while Native is installed: state must be InstalledOtherSource
        details->setProperty("selectedOfferIndex", 1);
        QCOMPARE(details->property("actionState").toString(), QStringLiteral("InstalledOtherSource"));
        auto *parallelBtn = details->findChild<QObject *>(QStringLiteral("storeParallelInstallButton"));
        QVERIFY(parallelBtn);
        QVERIFY(parallelBtn->property("visible").toBool());
        QCOMPARE(install->property("text").toString(), QStringLiteral("Öffnen"));

        // Switch back to Native for removal
        details->setProperty("selectedOfferIndex", 0);
        QCOMPARE(details->property("actionState").toString(), QStringLiteral("InstalledNoLaunch"));

        auto *remove = details->findChild<QObject *>(QStringLiteral("storeRemoveButton")); QVERIFY(remove);
        QVERIFY(remove->property("enabled").toBool()); QVERIFY(remove->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(remove, "clicked"));
        QTRY_VERIFY(client.hasPlan()); QCOMPARE(client.planModel()->removeCount(), 1);
        previewItem->setVisible(true); detailsItem->setVisible(false);
        QTRY_VERIFY(confirm->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(confirm, "clicked"));
        QTRY_VERIFY(!client.isBusy() && !store.isLoading() && !store.installedState(QStringLiteral("org.kde.kwrite")).isFullyInstalled);
        QCOMPARE(details->property("actionState").toString(), QStringLiteral("Available"));
    }
    // lutd beendet sich nach 120 s Leerlauf und wird per D-Bus-Aktivierung neu
    // gestartet. Das darf die Oberfläche nicht als Verbindungsverlust werten:
    // sonst stand bei jeder App "Aktion nicht unterstützt" (1.8.0, Dominik).
    void idleExitOfActivatableDaemonKeepsInstallAvailable() {
        auto bus = QDBusConnection::sessionBus();
        {
            DaemonClient client(bus);
            client.setDaemonActivatableForTest(true);
            QVERIFY(client.init());
            QVERIFY(client.isConnected());
            QVERIFY(client.installSupported());
            stopDaemon(); // wie der Leerlauf-Exit
            QTest::qWait(300);
            QVERIFY(client.isConnected());
            QVERIFY(client.installSupported());
            QVERIFY2(startDaemon(), "Test daemon did not restart");
            // Nach dem Neustart muss ein Plan samt Ereignissen wieder ankommen.
            // Die Anmeldung des aktivierten lutd trifft erst während der Planung
            // ein. Sie darf die eigene Planung nicht als laufenden Commit
            // wiederanbinden: das Fortschrittsfenster verdeckte sonst die
            // Vorschau, bestätigen war unmöglich (1.8.1, Dominik).
            QSignalSpy started(&client, &DaemonClient::transactionStarted);
            client.planStoreInstall(QStringLiteral("example"));
            QVERIFY(QMetaObject::invokeMethod(&client, "onDbusNameOwnerChanged", Q_ARG(QString, QStringLiteral("org.linuxupdatetool.Daemon1")),
                                              Q_ARG(QString, QString()), Q_ARG(QString, QStringLiteral(":1.999"))));
            QTRY_VERIFY2(client.hasPlan(), qPrintable(QStringLiteral("busy=%1 error=%2 status=%3").arg(client.isBusy()).arg(client.hasError()).arg(client.statusMessage())));
            QVERIFY(!client.isBusy());
            QVERIFY(client.isStorePlan());
            QVERIFY(!client.planModel()->planRevision().isEmpty());
            QCOMPARE(started.count(), 0);
            client.discardStorePlan();
        }
        // Gegenprobe: nicht aktivierbar -> echter Verlust, wie bisher
        DaemonClient client(bus);
        client.setDaemonActivatableForTest(false);
        QVERIFY(client.init());
        QVERIFY(client.isConnected());
        stopDaemon();
        QTRY_VERIFY(!client.isConnected());
        QVERIFY2(startDaemon(), "Test daemon did not restart");
    }
    void cleanupTestCase() { daemon.terminate(); if (!daemon.waitForFinished(3000)) { daemon.kill(); daemon.waitForFinished(); } }
};
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--test-daemon"))) {
        TestManager manager;
        manager.setBackendForTest(std::make_unique<BusBackend>(), true);
        auto bus = QDBusConnection::sessionBus();
        if (!bus.registerService(QStringLiteral("org.linuxupdatetool.Daemon1")) || !bus.registerObject(
                QStringLiteral("/org/linuxupdatetool/Daemon1"), &manager, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) return 2;
        fprintf(stdout, "READY\n"); fflush(stdout);
        return app.exec();
    }
    StoreBusIntegrationTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "store_bus_integration_test.moc"
