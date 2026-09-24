#include <QtTest>
#include <QTemporaryDir>
#include <QLocalServer>
#include <QLocalSocket>
#include "liblut/backend/snap/SnapAvailability.h"
#include "liblut/backend/snap/SnapBackend.h"
#include "liblut/catalog/snap/SnapPackageCatalog.h"
#include "lutd/TransactionManager.h"
#include "liblut/transaction/TransactionTypes.h"
#include "liblut/detect/DistroDetect.h"

using namespace lut;

class StoreSnapTest : public QObject {
    Q_OBJECT

private slots:
    void testSnapAvailabilityFourCases() {
        // Fall 1: Fehlende Binärdatei
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());
        const QString dummySocket = tmpDir.filePath(QStringLiteral("snapd.socket"));
        QFile sockFile(dummySocket);
        QVERIFY(sockFile.open(QIODevice::WriteOnly));
        sockFile.close();

        auto res1 = SnapAvailability::check(QStringLiteral("/nonexistent/path/to/snap"), dummySocket);
        QVERIFY(!res1.available);
        QVERIFY(res1.reason.contains(QStringLiteral("Snap-Binärdatei nicht gefunden")));

        // Fall 2: Binärdatei vorhanden, aber Socket fehlt
        const QString realBinary = QStringLiteral("/bin/sh");
        auto res2 = SnapAvailability::check(realBinary, QStringLiteral("/nonexistent/path/to/snapd.socket"));
        QVERIFY(!res2.available);
        QVERIFY(res2.reason.contains(QStringLiteral("Snapd-Socket existiert nicht")));

        // Fall 3: Socket-Datei existiert, aber Verbindung verweigert / Dienst antwortet nicht
        // Test mit injiziertem Prober der Verbindungsabbruch simuliert
        auto res3 = SnapAvailability::check(realBinary, dummySocket, [](const QString &path, QString &err, QString &) {
            Q_UNUSED(path);
            err = QStringLiteral("Verbindung verweigert");
            return false;
        });
        QVERIFY(!res3.available);
        QVERIFY(res3.reason.contains(QStringLiteral("Verbindung verweigert")));

        // Fall 4: Socket erreichbar, aber unerwartete Antwort (z.B. HTTP 500 oder HTML oder ungültiges JSON)
        auto res4a = SnapAvailability::check(realBinary, dummySocket, [](const QString &, QString &, QString &resp) {
            resp = QStringLiteral("HTTP/1.1 500 Internal Server Error\r\n\r\nError");
            return true;
        });
        QVERIFY(!res4a.available);
        QVERIFY(res4a.reason.contains(QStringLiteral("Fehlerstatus")));

        auto res4b = SnapAvailability::check(realBinary, dummySocket, [](const QString &, QString &, QString &resp) {
            resp = QStringLiteral("HTTP/1.1 200 OK\r\n\r\n{\"type\":\"error\",\"status-code\":400}");
            return true;
        });
        QVERIFY(!res4b.available);
        QVERIFY(res4b.reason.contains(QStringLiteral("unerwarteten Typ")));

        // Fall 5: Gültige echte snapd-Antwort (Erfolgsfall)
        auto res5 = SnapAvailability::check(realBinary, dummySocket, [](const QString &, QString &, QString &resp) {
            resp = QStringLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
                                  "{\"type\":\"sync\",\"status-code\":200,\"status\":\"OK\",\"result\":{\"version\":\"2.58.2\",\"series\":\"16\"}}");
            return true;
        });
        QVERIFY(res5.available);
        QCOMPARE(res5.daemonVersion, QStringLiteral("2.58.2"));

        // Host-Sicherheit: Auf diesem Testhost (Arch/CachyOS ohne snapd) darf Snap NIEMALS verfügbar sein!
        QVERIFY(!SnapAvailability::isSnapAvailable());
    }

    void testSnapCatalogUnavailableBehavior() {
        SnapPackageCatalog catalog(nullptr, false);
        // Ohne forceAvailable und ohne runner auf host: nicht verfügbar
        QVERIFY(!catalog.isAvailable());
        QVERIFY(catalog.allInstalledPackages().isEmpty());
        QVERIFY(catalog.offersForPackage(QStringLiteral("vlc")).isEmpty());
        QVERIFY(!catalog.candidateOffer(QStringLiteral("vlc")).has_value());
        QVERIFY(!catalog.installedStateForPackage(QStringLiteral("vlc")).isFullyInstalled);
    }

    void testSnapCatalogParsingSingleCall() {
        int callCount = 0;
        // Echte aufgezeichnete Ausgabe von 'snap list --unicode=never --color=never'
        const QString recordedSnapList = QStringLiteral(
            "Name               Version          Rev    Tracking       Publisher   Notes\n"
            "bare               1.0              5      latest/stable  canonical*  base\n"
            "core20             20230801         2015   latest/stable  canonical*  base\n"
            "firefox            117.0-1          3131   latest/stable  mozilla*    -\n"
            "code               1.85.1           159    latest/stable  vscode*     classic\n"
            "vlc                3.0.19           3721   latest/stable  videolan*   -\n"
        );

        SnapPackageCatalog catalog([&](const QStringList &args, QString &stdoutOut, QString &stderrOut) -> bool {
            ++callCount;
            Q_UNUSED(stderrOut);
            if (args.isEmpty() || args.first() != QStringLiteral("list")) {
                return false;
            }
            stdoutOut = recordedSnapList;
            return true;
        }, true);

        QVERIFY(catalog.isAvailable());
        const auto installed = catalog.allInstalledPackages();
        // Genau EIN CLI-Aufruf für den gesamten Bestand!
        QCOMPARE(callCount, 1);
        QCOMPARE(installed.size(), 5);

        // Prüfe vlc
        const auto vlcState = catalog.installedStateForPackage(QStringLiteral("vlc"));
        QVERIFY(vlcState.isFullyInstalled);
        QCOMPARE(vlcState.origin, QStringLiteral("snap"));
        QVERIFY(!vlcState.installedPackages.isEmpty());
        const auto &vlcRef = vlcState.installedPackages.first();
        QCOMPARE(vlcRef.backend, QStringLiteral("snap"));
        QCOMPARE(vlcRef.name, QStringLiteral("vlc"));
        QCOMPARE(vlcRef.repoId, QStringLiteral("latest/stable"));
        QCOMPARE(vlcRef.version, QStringLiteral("3.0.19 (rev 3721)"));
        QCOMPARE(vlcState.launchableDesktopIds, QStringList{QStringLiteral("vlc.desktop")});

        // Prüfe Base-Snap (core20) hat keinen App-Starter
        const auto coreState = catalog.installedStateForPackage(QStringLiteral("core20"));
        QVERIFY(coreState.isFullyInstalled);
        QVERIFY(coreState.launchableDesktopIds.isEmpty());

        // Zweite Abfrage nutzt den Cache ohne erneuten Aufruf
        catalog.installedStateForPackage(QStringLiteral("code"));
        QCOMPARE(callCount, 1);
    }

    void testSnapBackendCapabilities() {
        SnapBackend backend(nullptr, false);
        const auto caps = backend.capabilities();
        QVERIFY(!caps.sources.isEmpty());
        const auto &src = caps.sources.first();
        QCOMPARE(src.source, QStringLiteral("snap"));
        QVERIFY(src.boundRevision);
        // Abschnitt 4.2 & 7: Strikt false! Snap darf niemals Systemupgrades verlangen
        QVERIFY(!src.installRequiresFullUpgrade);
        QVERIFY(src.systemScope);
        QVERIFY(!src.userScope);
        // Auf dem Testhost ist es nicht verfügbar
        QVERIFY(!src.available);
    }

    void testSnapInstallPlanAndRevisionBinding() {
        QString recordedSnapInfo = QStringLiteral(
            "name:      vlc\n"
            "summary:   The ultimate media player\n"
            "publisher: VideoLAN*\n"
            "license:   GPL-2.0+\n"
            "channels:\n"
            "  latest/stable:    3.0.19 2023-10-11 (3721) 338MB -\n"
            "  latest/candidate: 3.0.19 2023-10-11 (3721) 338MB -\n"
            "  latest/beta:      3.0.20 2024-01-10 (3777) 338MB -\n"
            "  latest/edge:      4.0.0-dev 2024-01-10 (3782) 340MB -\n"
        );

        QStringList lastExecutedArgs;
        SnapBackend backend([&](const QString &prog, const QStringList &args, QString &stdoutOut, QString &stderrOut) {
            Q_UNUSED(prog);
            Q_UNUSED(stderrOut);
            if (args.contains(QStringLiteral("info"))) {
                stdoutOut = recordedSnapInfo;
                return 0;
            }
            if (args.contains(QStringLiteral("install"))) {
                lastExecutedArgs = args;
                stdoutOut = QStringLiteral("vlc (latest/stable) 3.0.19 installed");
                return 0;
            }
            return -1;
        }, true);

        TransactionIntent intent;
        intent.type = TransactionIntent::Type::Install;
        PackageRef target;
        target.backend = QStringLiteral("snap");
        target.name = QStringLiteral("vlc");
        target.repoId = QStringLiteral("latest/stable");
        intent.targets = {target};

        QSignalSpy spy(&backend, &Backend::eventEmitted);
        backend.planPackageTransaction(intent);

        // Verifiziere PlanReady Event
        bool planReadyFound = false;
        QString boundRevision;
        for (const auto &item : spy) {
            const auto ev = qvariant_cast<lut::Event>(item.first());
            if (std::holds_alternative<PlanReady>(ev)) {
                planReadyFound = true;
                const auto &p = std::get<PlanReady>(ev);
                QCOMPARE(p.ops.size(), 1);
                QCOMPARE(p.ops.first().name, QStringLiteral("vlc"));
                QCOMPARE(p.ops.first().newVersion, QStringLiteral("3.0.19 (rev 3721)"));
                boundRevision = p.planRevision;
                QCOMPARE(boundRevision, QStringLiteral("3721")); // Revisionsbindung!
                QVERIFY(p.warnings.isEmpty()); // strict confinement -> keine Classic-Warnung
            }
        }
        QVERIFY(planReadyFound);

        // Commit mit bestätigter Revision ausführen
        spy.clear();
        backend.commitPlan(boundRevision);

        QCOMPARE(lastExecutedArgs, (QStringList{QStringLiteral("install"), QStringLiteral("vlc"), QStringLiteral("--revision=3721")}));

        bool successFound = false;
        for (const auto &item : spy) {
            const auto ev = qvariant_cast<lut::Event>(item.first());
            if (std::holds_alternative<TransactionDone>(ev)) {
                const auto &td = std::get<TransactionDone>(ev);
                if (td.result == Result::Success) successFound = true;
            }
        }
        QVERIFY(successFound);
    }

    void testSnapRevisionMismatchAbortion() {
        QString recordedSnapInfo = QStringLiteral(
            "name:      vlc\n"
            "channels:\n"
            "  latest/stable:    3.0.19 2023-10-11 (3721) 338MB -\n"
        );

        bool installExecuted = false;
        SnapBackend backend([&](const QString &, const QStringList &args, QString &stdoutOut, QString &) {
            if (args.contains(QStringLiteral("info"))) {
                stdoutOut = recordedSnapInfo;
                return 0;
            }
            if (args.contains(QStringLiteral("install"))) {
                installExecuted = true;
                return 0;
            }
            return -1;
        }, true);

        TransactionIntent intent;
        intent.type = TransactionIntent::Type::Install;
        PackageRef target;
        target.backend = QStringLiteral("snap");
        target.name = QStringLiteral("vlc");
        intent.targets = {target};

        backend.planPackageTransaction(intent);

        QSignalSpy spy(&backend, &Backend::eventEmitted);
        // Bestätige mit veralteter Revision 3720 statt 3721
        backend.commitPlan(QStringLiteral("3720"));

        QVERIFY(!installExecuted); // Keine Installation vor Revisionstreffer!
        bool mismatchFound = false;
        for (const auto &item : spy) {
            const auto ev = qvariant_cast<lut::Event>(item.first());
            if (std::holds_alternative<TransactionDone>(ev)) {
                const auto &td = std::get<TransactionDone>(ev);
                if (td.result == Result::Failed) mismatchFound = true;
            }
        }
        QVERIFY(mismatchFound);
    }

    void testSnapClassicConfinementAndArchSymlink() {
        QString recordedClassicInfo = QStringLiteral(
            "name:      code\n"
            "summary:   Code editing. Redefined.\n"
            "publisher: Visual Studio Code*\n"
            "license:   unset\n"
            "confinement: classic\n"
            "channels:\n"
            "  latest/stable:    1.85.1 2023-12-13 (159) 110MB classic\n"
        );

        QStringList lastArgs;
        SnapBackend backend([&](const QString &, const QStringList &args, QString &stdoutOut, QString &) {
            if (args.contains(QStringLiteral("info"))) {
                stdoutOut = recordedClassicInfo;
                return 0;
            }
            if (args.contains(QStringLiteral("install"))) {
                lastArgs = args;
                return 0;
            }
            return -1;
        }, true);

        TransactionIntent intent;
        intent.type = TransactionIntent::Type::Install;
        PackageRef target;
        target.backend = QStringLiteral("snap");
        target.name = QStringLiteral("code");
        intent.targets = {target};

        QSignalSpy spy(&backend, &Backend::eventEmitted);
        backend.planPackageTransaction(intent);

        bool classicWarnFound = false;
        bool archLinkWarnFound = false;
        for (const auto &item : spy) {
            const auto ev = qvariant_cast<lut::Event>(item.first());
            if (std::holds_alternative<PlanReady>(ev)) {
                const auto &p = std::get<PlanReady>(ev);
                for (const QString &w : p.warnings) {
                    if (w.contains(QStringLiteral("Classic-Confinement"))) classicWarnFound = true;
                    if (w.contains(QStringLiteral("/snap"))) archLinkWarnFound = true;
                }
            }
        }
        QVERIFY(classicWarnFound);
        // Auf Arch ohne /snap fehlt der Link, den Classic-Snaps brauchen: Hinweis muss vorhanden sein
        if (lut::DistroDetect::detectFamily() == lut::DistroFamily::Arch && !QFile::exists(QStringLiteral("/snap"))) {
            QVERIFY(archLinkWarnFound);
        }

        backend.commitPlan(QStringLiteral("159"));
        // Classic-Flag muss mitgegeben werden
        QVERIFY(lastArgs.contains(QStringLiteral("--classic")));
        QVERIFY(lastArgs.contains(QStringLiteral("--revision=159")));
    }

    void testSnapDevmodeRejected() {
        QString recordedDevmodeInfo = QStringLiteral(
            "name:      test-dev\n"
            "confinement: devmode\n"
            "channels:\n"
            "  latest/stable:    0.1 2023-01-01 (1) 10MB devmode\n"
        );

        SnapBackend backend([&](const QString &, const QStringList &args, QString &stdoutOut, QString &) {
            if (args.contains(QStringLiteral("info"))) {
                stdoutOut = recordedDevmodeInfo;
                return 0;
            }
            return -1;
        }, true);

        TransactionIntent intent;
        intent.type = TransactionIntent::Type::Install;
        PackageRef target;
        target.backend = QStringLiteral("snap");
        target.name = QStringLiteral("test-dev");
        intent.targets = {target};

        QSignalSpy spy(&backend, &Backend::eventEmitted);
        backend.planPackageTransaction(intent);

        bool devmodeRejected = false;
        for (const auto &item : spy) {
            const auto ev = qvariant_cast<lut::Event>(item.first());
            if (std::holds_alternative<TransactionDone>(ev)) {
                const auto &td = std::get<TransactionDone>(ev);
                if (td.result == Result::Failed && td.summary.contains(QStringLiteral("devmode"))) {
                    devmodeRejected = true;
                }
            }
        }
        QVERIFY(devmodeRejected);
    }

    void testSnapRemove() {
        QStringList lastArgs;
        SnapBackend backend([&](const QString &, const QStringList &args, QString &, QString &) {
            if (args.contains(QStringLiteral("remove"))) {
                lastArgs = args;
                return 0;
            }
            return -1;
        }, true);

        TransactionIntent intent;
        intent.type = TransactionIntent::Type::Remove;
        PackageRef target;
        target.backend = QStringLiteral("snap");
        target.name = QStringLiteral("vlc");
        intent.targets = {target};

        backend.planPackageTransaction(intent);
        backend.commit();

        QCOMPARE(lastArgs, (QStringList{QStringLiteral("remove"), QStringLiteral("vlc")}));
    }

    void testTransactionManagerSnapRouting() {
        class MockNativeBackend : public Backend {
        public:
            Capabilities capabilities() const override {
                Capabilities c; c.install = true; c.remove = true; c.protocolVersion = 2; return c;
            }
            void refreshMetadata() override {}
            void planUpgradeAll(const UpgradeOptions &) override {}
            void planInstall(const QStringList &) override {}
            void planRemove(const QStringList &) override {}
            void commit() override {}
            void cancel() override {}
            void answerQuestion(const QString &, const QJsonObject &) override {}
            QList<PackageOp> availableUpdates() override { return {}; }
            QList<InstalledPackage> installedPackages(const QString &) override { return {}; }
            QList<ChangelogEntry> changelog(const QString &) override { return {}; }
            QList<HistoryEntry> history(int) override { return {}; }
        };

        TransactionManager manager;
        manager.setBackendForTest(std::make_unique<MockNativeBackend>());

        bool snapCalled = false;
        auto snapBackend = std::make_unique<SnapBackend>([&](const QString &, const QStringList &args, QString &stdoutOut, QString &) {
            if (args.contains(QStringLiteral("info"))) {
                snapCalled = true;
                stdoutOut = QStringLiteral("name: vlc\nchannels:\n  latest/stable: 3.0.19 (3721) 338MB -\n");
                return 0;
            }
            return 0;
        }, true);

        manager.setSnapBackendForTest(std::move(snapBackend));

        const QString capsJson = manager.GetCapabilities();
        QVERIFY(capsJson.contains(QStringLiteral("\"source\":\"snap\"")));

        QVariantMap target;
        target[QStringLiteral("backend")] = QStringLiteral("snap");
        target[QStringLiteral("name")] = QStringLiteral("vlc");
        target[QStringLiteral("repoId")] = QStringLiteral("latest/stable");

        QDBusObjectPath path = manager.PlanPackageTransaction(QStringLiteral("Install"), {target}, {});
        QVERIFY(path.path() != QStringLiteral("/"));
        QTest::qWait(100);
        QVERIFY(snapCalled);
    }
};

QTEST_GUILESS_MAIN(StoreSnapTest)
#include "store_snap_test.moc"
