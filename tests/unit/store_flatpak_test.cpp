#include <QTest>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include "liblut/catalog/flatpak/FlatpakPackageCatalog.h"
#include "liblut/backend/flatpak/FlatpakBackend.h"
#include "lutd/TransactionManager.h"

using namespace lut;

class StoreFlatpakTest : public QObject {
    Q_OBJECT

private slots:
    void testCatalogParsingAndScope();
    void testCatalogOffersNotInstalledRemoteApps();
    void testBackendCapabilities();
    void testInstallPlanWithRuntimeAndCommitBinding();
    void testInstallCommitMismatchAbortion();
    void testRemoveSystemAppNoUnused();
    void testRemoveUserAppRejected();
    void testTransactionManagerFlatpakRouting();
};

void StoreFlatpakTest::testCatalogParsingAndScope() {
    // Tab-delimited mock output from `flatpak list --app --columns=...`
    // application, origin, installation, ref, active, version, runtime
    const QString mockListOutput =
        QStringLiteral("org.kde.kate\tflathub\tsystem\tapp/org.kde.kate/x86_64/stable\ta1b2c3d4e5f6\t24.02.0\torg.kde.Platform/x86_64/6.6\n") +
        QStringLiteral("com.spotify.Client\tflathub\tuser\tapp/com.spotify.Client/x86_64/stable\tfedcba654321\t1.2.31\torg.freedesktop.Platform/x86_64/23.08\n") +
        QStringLiteral("io.gitlab.NoVersion\ttalk-origin\tsystem\tapp/io.gitlab.NoVersion/x86_64/master\t998877665544\t\torg.gnome.Platform/x86_64/46\n");

    FlatpakPackageCatalog catalog([mockListOutput](const QStringList &args, QString &out, QString &err) {
        Q_UNUSED(err);
        if (args.contains(QStringLiteral("list"))) {
            out = mockListOutput;
            return true;
        }
        return false;
    });

    QCOMPARE(catalog.allInstalledPackages().size(), 3);

    // Test system-installed app
    InstalledState kateState = catalog.installedStateForPackage(QStringLiteral("org.kde.kate"));
    QVERIFY(kateState.isFullyInstalled);
    QCOMPARE(kateState.origin, QStringLiteral("system"));
    QCOMPARE(kateState.installedPackages.size(), 1);
    QCOMPARE(kateState.installedPackages.first().backend, QStringLiteral("flatpak"));
    QCOMPARE(kateState.installedPackages.first().repoId, QStringLiteral("flathub"));
    QCOMPARE(kateState.installedPackages.first().version, QStringLiteral("24.02.0"));

    // Also reachable via .desktop suffix
    InstalledState kateDesktopState = catalog.installedStateForPackage(QStringLiteral("org.kde.kate.desktop"));
    QVERIFY(kateDesktopState.isFullyInstalled);

    // Test user-installed app (Section 6.2)
    InstalledState spotifyState = catalog.installedStateForPackage(QStringLiteral("com.spotify.Client"));
    QVERIFY(spotifyState.isFullyInstalled);
    QCOMPARE(spotifyState.origin, QStringLiteral("user"));

    // Test app without version: falls back to commit prefix
    InstalledState noVerState = catalog.installedStateForPackage(QStringLiteral("io.gitlab.NoVersion"));
    QVERIFY(noVerState.isFullyInstalled);
    QCOMPARE(noVerState.installedPackages.first().version, QStringLiteral("998877665544"));

    // Candidate offer check
    auto candKate = catalog.candidateOffer(QStringLiteral("org.kde.kate"));
    QVERIFY(candKate.has_value());
    QCOMPARE(candKate->source(), QStringLiteral("flatpak"));
    QCOMPARE(candKate->sourceRank(), 200);
}

// Der Katalog muss Anwendungen anbieten, die noch NICHT installiert sind.
// Solange er nur `flatpak list` las, konnte der Store aus Flatpak nie etwas
// installieren - er kannte ausschließlich bereits Installiertes.
void StoreFlatpakTest::testCatalogOffersNotInstalledRemoteApps() {
    const QString mockList =
        QStringLiteral("org.kde.kate\tflathub\tsystem\tapp/org.kde.kate/x86_64/stable\ta1b2c3\t24.02.0\torg.kde.Platform/x86_64/6.6\n");
    const QString mockRemotes = QStringLiteral("flathub\ntalk-origin\n");
    // application, version, branch, download-size, installed-size
    const QString mockFlathub =
        QStringLiteral("org.gnome.Calculator\t46.1\tstable\t1048576\t4194304\n") +
        QStringLiteral("org.kde.kate\t24.02.0\tstable\t2097152\t8388608\n");
    const QString mockTalk =
        QStringLiteral("com.nextcloud.talk\t18.0\tstable\t3145728\t12582912\n");

    QStringList seenCommands;
    FlatpakPackageCatalog catalog([&](const QStringList &args, QString &out, QString &err) {
        Q_UNUSED(err);
        seenCommands.append(args.join(QLatin1Char(' ')));
        if (args.contains(QStringLiteral("list"))) { out = mockList; return true; }
        if (args.contains(QStringLiteral("remotes"))) { out = mockRemotes; return true; }
        if (args.contains(QStringLiteral("remote-ls"))) {
            if (args.contains(QStringLiteral("flathub"))) { out = mockFlathub; return true; }
            if (args.contains(QStringLiteral("talk-origin"))) { out = mockTalk; return true; }
        }
        return false;
    });

    // Nicht installiert, aber bei Flathub vorhanden: muss ein Angebot liefern.
    auto calculator = catalog.candidateOffer(QStringLiteral("org.gnome.Calculator"));
    QVERIFY2(calculator.has_value(), "Nicht installierte Remote-Anwendung lieferte kein Angebot");
    QVERIFY(calculator->available);
    QCOMPARE(calculator->packages.first().backend, QStringLiteral("flatpak"));
    QCOMPARE(calculator->packages.first().repoId, QStringLiteral("flathub"));
    QCOMPARE(calculator->packages.first().version, QStringLiteral("46.1"));
    QCOMPARE(calculator->downloadSize.value_or(0), 1048576);
    QCOMPARE(calculator->installedSize.value_or(0), 4194304);

    // Ein zweiter Remote wird ebenfalls berücksichtigt und behält seinen Namen.
    auto talk = catalog.candidateOffer(QStringLiteral("com.nextcloud.talk"));
    QVERIFY(talk.has_value());
    QCOMPARE(talk->packages.first().repoId, QStringLiteral("talk-origin"));

    // Installiertes bleibt Kandidat mit seiner installierten Fassung.
    auto kate = catalog.candidateOffer(QStringLiteral("org.kde.kate"));
    QVERIFY(kate.has_value());
    QCOMPARE(kate->packages.first().repoId, QStringLiteral("flathub"));

    // Schreibweise ohne bzw. mit .desktop muss beide Male treffen.
    QVERIFY(catalog.candidateOffer(QStringLiteral("org.gnome.Calculator.desktop")).has_value());

    // Gegenprobe: Erfundenes darf kein Angebot ergeben.
    QVERIFY(!catalog.candidateOffer(QStringLiteral("org.example.GibtEsNicht")).has_value());

    // Je Remote genau ein remote-ls, nicht einer je Anwendung.
    int remoteLsCalls = 0;
    for (const QString &c : seenCommands) {
        if (c.contains(QStringLiteral("remote-ls"))) ++remoteLsCalls;
    }
    QCOMPARE(remoteLsCalls, 2);

    // Suche greift ebenfalls auf die Remotes zu.
    const auto found = catalog.searchPackages(QStringLiteral("Calculator"));
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first().packages.first().name, QStringLiteral("org.gnome.Calculator"));
}

void StoreFlatpakTest::testBackendCapabilities() {
    FlatpakBackend backend;
    Capabilities caps = backend.capabilities();

    QVERIFY(caps.install);
    QVERIFY(caps.remove);
    // Flatpak must NEVER couple to ALPM full system upgrade (Section 4.2)
    QVERIFY(!caps.installRequiresFullUpgrade);

    QCOMPARE(caps.sources.size(), 1);
    const SourceCapabilities &src = caps.sources.first();
    QCOMPARE(src.source, QStringLiteral("flatpak"));
    QVERIFY(src.install);
    QVERIFY(src.remove);
    QVERIFY(src.systemScope);
    QVERIFY(!src.userScope); // Root daemon only handles system scope
    QVERIFY(src.boundRevision);
    QVERIFY(!src.installRequiresFullUpgrade);
}

void StoreFlatpakTest::testInstallPlanWithRuntimeAndCommitBinding() {
    const QString expectedCommit = QStringLiteral("64charhexcommit0123456789abcdef0123456789abcdef0123456789abcdef01");
    const QString expectedRuntime = QStringLiteral("org.gnome.Platform/x86_64/50");

    QStringList executedCommands;
    QList<Event> receivedEvents;

    FlatpakBackend backend([&executedCommands, expectedCommit, expectedRuntime](const QString &prog, const QStringList &args, QString &out, QString &err) -> int {
        Q_UNUSED(prog);
        Q_UNUSED(err);
        executedCommands.append(args.join(QLatin1Char(' ')));

        if (args.contains(QStringLiteral("-c"))) {
            // remote-info -c <repo> <ref>
            out = expectedCommit + QStringLiteral("\n");
            return 0;
        }
        if (args.contains(QStringLiteral("--show-runtime"))) {
            out = expectedRuntime + QStringLiteral("\n");
            return 0;
        }
        if (args.contains(QStringLiteral("--runtime")) && args.contains(QStringLiteral("list"))) {
            // Return empty list so runtime is detected as NOT yet installed
            out = QStringLiteral("org.freedesktop.Platform/x86_64/23.08\n");
            return 0;
        }
        if (args.contains(QStringLiteral("remote-info")) && !args.contains(QStringLiteral("-c"))) {
            out = QStringLiteral("Download Size: 15.2 MB\nInstalled Size: 32.4 MB\n");
            return 0;
        }
        if (args.contains(QStringLiteral("install"))) {
            return 0;
        }
        return 0;
    });

    connect(&backend, &Backend::eventEmitted, [&receivedEvents](const Event &ev) {
        receivedEvents.append(ev);
    });

    TransactionIntent intent;
    intent.type = TransactionIntent::Type::Install;
    PackageRef pref;
    pref.backend = QStringLiteral("flatpak");
    pref.repoId = QStringLiteral("flathub");
    pref.name = QStringLiteral("org.gnome.Calculator");
    intent.targets.append(pref);

    backend.planPackageTransaction(intent);

    // Verify PlanReady was emitted
    bool planReadyFound = false;
    PlanReady plan;
    for (const auto &ev : receivedEvents) {
        if (std::holds_alternative<PlanReady>(ev)) {
            planReadyFound = true;
            plan = std::get<PlanReady>(ev);
            break;
        }
    }
    QVERIFY(planReadyFound);

    // Verify commit binding (Section 6.4)
    QCOMPARE(plan.planRevision, expectedCommit);

    // Verify runtime was added as separate operation (Section 6.3)
    QCOMPARE(plan.ops.size(), 2);
    QCOMPARE(plan.ops[0].name, QStringLiteral("org.gnome.Calculator"));
    QCOMPARE(plan.ops[0].kind, PackageOp::Kind::Install);
    QCOMPARE(plan.ops[1].name, expectedRuntime);
    QCOMPARE(plan.ops[1].summary, QStringLiteral("Laufzeitumgebung"));

    // Now execute commit with valid revision
    receivedEvents.clear();
    backend.commitPlan(expectedCommit);

    bool finishedFound = false;
    for (const auto &ev : receivedEvents) {
        if (std::holds_alternative<TransactionDone>(ev)) {
            const auto &td = std::get<TransactionDone>(ev);
            if (td.result == Result::Success) {
                finishedFound = true;
            }
        }
    }
    QVERIFY(finishedFound);

    // Verify execution command
    bool installCommandFound = false;
    for (const QString &cmd : executedCommands) {
        if (cmd.contains(QStringLiteral("install --system -y --noninteractive flathub org.gnome.Calculator"))) {
            installCommandFound = true;
            break;
        }
    }
    QVERIFY(installCommandFound);
}

void StoreFlatpakTest::testInstallCommitMismatchAbortion() {
    const QString initialCommit = QStringLiteral("commit_initial_1234567890abcdef1234567890abcdef1234567890abcdef");
    const QString changedCommit = QStringLiteral("commit_changed_9876543210fedcba9876543210fedcba9876543210fedcba");

    int remoteInfoCount = 0;
    QList<Event> receivedEvents;

    FlatpakBackend backend([&remoteInfoCount, initialCommit, changedCommit](const QString &prog, const QStringList &args, QString &out, QString &err) -> int {
        Q_UNUSED(prog);
        Q_UNUSED(err);
        if (args.contains(QStringLiteral("-c"))) {
            remoteInfoCount++;
            if (remoteInfoCount == 1) {
                out = initialCommit + QStringLiteral("\n");
            } else {
                // Changed commit between plan and execution!
                out = changedCommit + QStringLiteral("\n");
            }
            return 0;
        }
        return 0;
    });

    connect(&backend, &Backend::eventEmitted, [&receivedEvents](const Event &ev) {
        receivedEvents.append(ev);
    });

    TransactionIntent intent;
    intent.type = TransactionIntent::Type::Install;
    PackageRef pref;
    pref.backend = QStringLiteral("flatpak");
    pref.repoId = QStringLiteral("flathub");
    pref.name = QStringLiteral("org.gnome.Calculator");
    intent.targets.append(pref);

    backend.planPackageTransaction(intent);

    // Try committing with the planned revision
    receivedEvents.clear();
    backend.commitPlan(initialCommit);

    // Must fail due to commit mismatch (Section 6.4)
    bool failureFound = false;
    QString failureSummary;
    for (const auto &ev : receivedEvents) {
        if (std::holds_alternative<TransactionDone>(ev)) {
            const auto &td = std::get<TransactionDone>(ev);
            if (td.result == Result::Failed) {
                failureFound = true;
                failureSummary = td.summary;
            }
        }
    }
    QVERIFY(failureFound);
    QVERIFY(failureSummary.contains(QStringLiteral("Commit-Hash hat sich zwischen Planung und Ausführung geändert")));
}

void StoreFlatpakTest::testRemoveSystemAppNoUnused() {
    QStringList executedCommands;
    QList<Event> receivedEvents;

    FlatpakBackend backend([&executedCommands](const QString &prog, const QStringList &args, QString &out, QString &err) -> int {
        Q_UNUSED(prog);
        Q_UNUSED(err);
        executedCommands.append(args.join(QLatin1Char(' ')));
        if (args.contains(QStringLiteral("list"))) {
            out = QStringLiteral("org.gnome.Calculator\tsystem\n");
            return 0;
        }
        if (args.contains(QStringLiteral("uninstall"))) {
            return 0;
        }
        return 0;
    });

    connect(&backend, &Backend::eventEmitted, [&receivedEvents](const Event &ev) {
        receivedEvents.append(ev);
    });

    TransactionIntent intent;
    intent.type = TransactionIntent::Type::Remove;
    PackageRef pref;
    pref.backend = QStringLiteral("flatpak");
    pref.name = QStringLiteral("org.gnome.Calculator");
    intent.targets.append(pref);

    backend.planPackageTransaction(intent);

    bool planReadyFound = false;
    PlanReady plan;
    for (const auto &ev : receivedEvents) {
        if (std::holds_alternative<PlanReady>(ev)) {
            planReadyFound = true;
            plan = std::get<PlanReady>(ev);
            break;
        }
    }
    QVERIFY(planReadyFound);
    QCOMPARE(plan.ops.size(), 1);
    QCOMPARE(plan.ops[0].kind, PackageOp::Kind::Remove);

    receivedEvents.clear();
    backend.commitPlan(plan.planRevision);

    bool finishedFound = false;
    for (const auto &ev : receivedEvents) {
        if (std::holds_alternative<TransactionDone>(ev)) {
            if (std::get<TransactionDone>(ev).result == Result::Success) {
                finishedFound = true;
            }
        }
    }
    QVERIFY(finishedFound);

    // Section 6.3: Must NOT include --unused
    bool uninstallFound = false;
    for (const QString &cmd : executedCommands) {
        if (cmd.contains(QStringLiteral("uninstall"))) {
            uninstallFound = true;
            QVERIFY(!cmd.contains(QStringLiteral("--unused")));
            QVERIFY(cmd.contains(QStringLiteral("--system")));
            QVERIFY(cmd.contains(QStringLiteral("org.gnome.Calculator")));
        }
    }
    QVERIFY(uninstallFound);
}

void StoreFlatpakTest::testRemoveUserAppRejected() {
    QList<Event> receivedEvents;

    FlatpakBackend backend([](const QString &prog, const QStringList &args, QString &out, QString &err) -> int {
        Q_UNUSED(prog);
        Q_UNUSED(err);
        if (args.contains(QStringLiteral("list"))) {
            out = QStringLiteral("com.spotify.Client\tuser\n");
            return 0;
        }
        return 0;
    });

    connect(&backend, &Backend::eventEmitted, [&receivedEvents](const Event &ev) {
        receivedEvents.append(ev);
    });

    TransactionIntent intent;
    intent.type = TransactionIntent::Type::Remove;
    PackageRef pref;
    pref.backend = QStringLiteral("flatpak");
    pref.name = QStringLiteral("com.spotify.Client");
    intent.targets.append(pref);

    backend.planPackageTransaction(intent);

    // Section 6.2: user installation must be rejected with exact error message
    bool failureFound = false;
    QString failureSummary;
    for (const auto &ev : receivedEvents) {
        if (std::holds_alternative<TransactionDone>(ev)) {
            const auto &td = std::get<TransactionDone>(ev);
            if (td.result == Result::Failed) {
                failureFound = true;
                failureSummary = td.summary;
            }
        }
    }
    QVERIFY(failureFound);
    QCOMPARE(failureSummary, QStringLiteral("Benutzerinstallationen (user) können nicht über den systemweiten Dienst entfernt werden."));
}

void StoreFlatpakTest::testTransactionManagerFlatpakRouting() {
    TransactionManager manager;
    manager.setCallerUidForTest(1000);

    bool flatpakPlanned = false;
    auto flatpakBackend = std::make_unique<FlatpakBackend>([&flatpakPlanned](const QString &prog, const QStringList &args, QString &out, QString &err) -> int {
        Q_UNUSED(prog);
        Q_UNUSED(err);
        if (args.contains(QStringLiteral("-c"))) {
            flatpakPlanned = true;
            out = QStringLiteral("f1a79a1000000000000000000000000000000000000000000000000000000000\n");
            return 0;
        }
        return 0;
    });

    manager.setFlatpakBackendForTest(std::move(flatpakBackend));

    // Check capabilities via D-Bus JSON method
    QString capJson = manager.GetCapabilities();
    QJsonDocument doc = QJsonDocument::fromJson(capJson.toUtf8());
    QVERIFY(doc.isObject());
    QJsonArray sources = doc.object().value(QStringLiteral("sources")).toArray();
    bool foundFlatpakSource = false;
    for (const auto &srcVal : sources) {
        if (srcVal.toObject().value(QStringLiteral("source")).toString() == QLatin1String("flatpak")) {
            foundFlatpakSource = true;
            QCOMPARE(srcVal.toObject().value(QStringLiteral("installRequiresFullUpgrade")).toBool(), false);
        }
    }
    QVERIFY(foundFlatpakSource);

    // Plan transaction with flatpak target
    QVariantMap target;
    target[QStringLiteral("backend")] = QStringLiteral("flatpak");
    target[QStringLiteral("repoId")] = QStringLiteral("flathub");
    target[QStringLiteral("name")] = QStringLiteral("org.gnome.Calculator");
    QVariantList targets = {target};

    QDBusObjectPath path = manager.PlanPackageTransaction(QStringLiteral("Install"), targets, {});
    QVERIFY(path.path() != QStringLiteral("/"));
    QCoreApplication::processEvents();
    QVERIFY(flatpakPlanned);
}

QTEST_MAIN(StoreFlatpakTest)
#include "store_flatpak_test.moc"
