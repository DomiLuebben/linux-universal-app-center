#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include "liblut/backend/pacstall/PacstallBackend.h"
#include "liblut/protocol/events.h"
#include "linux-app-store/models/PacstallUpdates.h"
#include "linux-app-store/AppSettings.h"

using namespace lut;

class PacstallBackendTest : public QObject {
    Q_OBJECT

private slots:
    void testParseUpdatesFromGenuineD6Output();
    void testUpdatesJsonRoundtrip();
    void testFailedCheckIsNotUpToDate();
    void testCommandArgumentsAndEnvironment();
    void testRejectForbiddenFlags();
    void testLockTimeout();
    void testRevisionMatchingAndProducerByteExact();
    void testPacstallUpdatesModelSettingsAndAvailability();
};

void PacstallBackendTest::testParseUpdatesFromGenuineD6Output() {
    // Echte Ausgabe aus Phase D6 (Container Ubuntu 24.04 / Debian, Pacstall 6.4.2 Nectarine, 2026-09-24):
    // Aufruf: NO_COLOR=1 DISABLE_PROMPTS=yes LC_ALL=C.UTF-8 pacstall -Lu
    const QString genuineOutput = QStringLiteral(
        "[+] INFO: Checking for updates\n"
        "\t[>] Building dependency tree\n"
        "\t[>] Checking versions\n"
        "\n"
        "[+] INFO: Packages can be upgraded\n"
        "Upgradable: 1\n"
        "\ttree-sitter-cli-bin @ github:pacstall/pacstall-programs ( 0.26.10-pacstall1 -> 0.26.11-pacstall1 )\n"
    );

    QList<PacstallUpdate> updates = PacstallBackend::parseUpdates(genuineOutput);
    QCOMPARE(updates.size(), 1);
    QCOMPARE(updates.first().name, QStringLiteral("tree-sitter-cli-bin"));
    QCOMPARE(updates.first().repo, QStringLiteral("github:pacstall/pacstall-programs"));
    QCOMPARE(updates.first().installed, QStringLiteral("0.26.10-pacstall1"));
    QCOMPARE(updates.first().available, QStringLiteral("0.26.11-pacstall1"));

    // Gegenprobe: Wenn "Nothing to upgrade" gemeldet wird (echte Ausgabe ohne Upgrades)
    const QString noUpdatesOutput = QStringLiteral(
        "[*] WARNING: Reading input from pipe\n"
        "[+] INFO: Checking for updates\n"
        "[*] WARNING: Reading input from pipe\n"
        "[+] INFO: Nothing to upgrade\n"
    );
    QList<PacstallUpdate> emptyUpdates = PacstallBackend::parseUpdates(noUpdatesOutput);
    QCOMPARE(emptyUpdates.size(), 0);
}

void PacstallBackendTest::testUpdatesJsonRoundtrip() {
    PacstallUpdate u1;
    u1.name = QStringLiteral("neofetch");
    u1.repo = QStringLiteral("pacstall");
    u1.installed = QStringLiteral("7.1.0-1");
    u1.available = QStringLiteral("7.1.0-2");

    PacstallUpdate u2;
    u2.name = QStringLiteral("ripgrep-bin");
    u2.repo = QStringLiteral("github");
    u2.installed = QStringLiteral("14.0.0");
    u2.available = QStringLiteral("14.1.0");

    QList<PacstallUpdate> list = {u1, u2};
    QString json = PacstallBackend::updatesToJson(list);
    QVERIFY(!json.isEmpty());

    QList<PacstallUpdate> parsed = PacstallBackend::jsonToUpdates(json);
    QCOMPARE(parsed.size(), 2);
    QCOMPARE(parsed.at(0).name, u1.name);
    QCOMPARE(parsed.at(0).repo, u1.repo);
    QCOMPARE(parsed.at(0).installed, u1.installed);
    QCOMPARE(parsed.at(0).available, u1.available);
    QCOMPARE(parsed.at(1).name, u2.name);
    QCOMPARE(parsed.at(1).repo, u2.repo);
    QCOMPARE(parsed.at(1).installed, u2.installed);
    QCOMPARE(parsed.at(1).available, u2.available);
}

// Eine gescheiterte Abfrage darf nicht als "alles aktuell" enden – weder bei
// Exit-Code != 0 noch bei Ausgabe ohne die Schlussmeldung von upgrade.sh noch,
// wenn weniger Zeilen erkannt werden, als pacstall selbst zählt.
void PacstallBackendTest::testFailedCheckIsNotUpToDate() {
    struct Case { int rc; QString out; bool fails; int count; };
    const QList<Case> cases = {
        {1, QStringLiteral("[!] ERROR: Could not connect to the internet\n"), true, 0},
        {0, QString(), true, 0},
        {0, QStringLiteral("Upgradable: 2\n\tfoo @ github:pacstall/pacstall-programs ( 1 -> 2 )\n"), true, 1},
        {0, QStringLiteral("[+] INFO: Nothing to upgrade\n"), false, 0},
        {0, QStringLiteral("Upgradable: 1\n\tfoo @ github:pacstall/pacstall-programs ( 1 -> 2 )\n"), false, 1},
    };
    for (const auto &c : cases) {
        PacstallBackend backend(nullptr, true);
        backend.setRunner([&c](const QString &, const QStringList &, const QProcessEnvironment &,
                               QString &stdoutOut, QString &) {
            stdoutOut = c.out;
            return c.rc;
        });
        QString error;
        const auto updates = backend.checkUpdates(&error);
        QVERIFY2(error.isEmpty() != c.fails, qPrintable(c.out + QStringLiteral(" -> ") + error));
        QCOMPARE(updates.size(), c.count);
    }
}

void PacstallBackendTest::testCommandArgumentsAndEnvironment() {
    PacstallBackend backend(nullptr, true);
    backend.setCallerUsername(QStringLiteral("testuser"));

    QString capturedProg;
    QStringList capturedArgs;
    QProcessEnvironment capturedEnv;

    backend.setRunner([&](const QString &prog, const QStringList &args, const QProcessEnvironment &env,
                          QString &stdoutOut, QString &) {
        capturedProg = prog;
        capturedArgs = args;
        capturedEnv = env;
        stdoutOut = QString();
        return 0;
    });

    backend.checkUpdates();

    QCOMPARE(capturedProg, QStringLiteral("/usr/bin/pacstall"));
    QCOMPARE(capturedArgs, QStringList({QStringLiteral("-Lu")}));
    QCOMPARE(capturedEnv.value(QStringLiteral("SUDO_USER")), QStringLiteral("testuser"));
    QCOMPARE(capturedEnv.value(QStringLiteral("NO_COLOR")), QStringLiteral("1"));
    QCOMPARE(capturedEnv.value(QStringLiteral("DISABLE_PROMPTS")), QStringLiteral("yes"));
    QCOMPARE(capturedEnv.value(QStringLiteral("LC_ALL")), QStringLiteral("C.UTF-8"));
}

void PacstallBackendTest::testRejectForbiddenFlags() {
    PacstallBackend backend;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString out, err;

    // Test rejection of -Ns
    int rc1 = backend.runCommandForTest(QStringLiteral("/usr/bin/pacstall"), {QStringLiteral("-Ns")}, env, out, err);
    QCOMPARE(rc1, -1);
    QVERIFY(err.contains(QStringLiteral("Sicherheitsrichtlinie verletzt")));
    QVERIFY(err.contains(QStringLiteral("-Ns")));

    // Test rejection of -Nc
    err.clear();
    int rc2 = backend.runCommandForTest(QStringLiteral("/usr/bin/pacstall"), {QStringLiteral("-Nc")}, env, out, err);
    QCOMPARE(rc2, -1);
    QVERIFY(err.contains(QStringLiteral("Sicherheitsrichtlinie verletzt")));
    QVERIFY(err.contains(QStringLiteral("-Nc")));

    // Test rejection when flag is embedded/combined
    err.clear();
    int rc3 = backend.runCommandForTest(QStringLiteral("/usr/bin/pacstall"), {QStringLiteral("-I"), QStringLiteral("foo"), QStringLiteral("-Ns")}, env, out, err);
    QCOMPARE(rc3, -1);
    QVERIFY(err.contains(QStringLiteral("Sicherheitsrichtlinie verletzt")));
}

void PacstallBackendTest::testLockTimeout() {
    PacstallBackend backend;
    backend.setRunner([&](const QString &, const QStringList &, const QProcessEnvironment &,
                          QString &stdoutOut, QString &stderrOut) {
        stdoutOut = QStringLiteral("pacstall is already running another instance");
        stderrOut = QString();
        return 1;
    });

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString out, err;
    int rc = backend.runCommandForTest(QStringLiteral("/usr/bin/pacstall"), {QStringLiteral("-Lu")}, env, out, err);
    QCOMPARE(rc, -2);
    QCOMPARE(err, QStringLiteral("Pacstall läuft bereits in einem anderen Fenster oder Terminal."));
}

void PacstallBackendTest::testRevisionMatchingAndProducerByteExact() {
    PacstallBackend backend(nullptr, true);
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    backend.setPacscriptsDir(tempDir.path());
    backend.setCallerUsername(QStringLiteral("testuser"));

    const QByteArray pacscriptContent =
        "#!/usr/bin/env bash\n"
        "pkgname='testpkg'\n"
        "pkgver='1.0'\n"
        "give_source() { echo 'dummy'; }\n";

    backend.setDownloader([&](const QUrl &, QString *err) -> QByteArray {
        if (err) *err = QString();
        return pacscriptContent;
    });

    QList<Event> emittedEvents;
    QObject::connect(&backend, &Backend::eventEmitted, [&](const Event &e) {
        emittedEvents.append(e);
    });

    // 1. Plan erzeugen
    backend.planPacstallUpgrade({QStringLiteral("testpkg")}, 1000);

    // Prüfen, ob PlanReady gesendet wurde
    QString planRevision;
    bool foundPlanReady = false;
    for (const auto &ev : emittedEvents) {
        if (auto pr = std::get_if<PlanReady>(&ev)) {
            foundPlanReady = true;
            planRevision = pr->planRevision;
            QCOMPARE(pr->ops.size(), 1);
            QCOMPARE(pr->ops.first().name, QStringLiteral("testpkg"));
            QCOMPARE(pr->ops.first().summary, QString::fromUtf8(pacscriptContent));
            break;
        }
    }
    QVERIFY(foundPlanReady);
    QVERIFY(!planRevision.isEmpty());

    // Datei auf Platte prüfen
    QString scriptPath = tempDir.path() + QStringLiteral("/testpkg.pacscript");
    QVERIFY(QFile::exists(scriptPath));

    // 2. Commit ausführen und bitgenaue Übergabe an pacstall -P -I prüfen
    QString executedProgram;
    QStringList executedArgs;
    QByteArray executedFileContent;
    QProcessEnvironment executedEnv;

    backend.setRunner([&](const QString &prog, const QStringList &args, const QProcessEnvironment &env,
                          QString &stdoutOut, QString &) {
        executedProgram = prog;
        executedArgs = args;
        executedEnv = env;
        if (args.size() >= 3) {
            QFile f(args.at(2));
            if (f.open(QIODevice::ReadOnly)) {
                executedFileContent = f.readAll();
            }
        }
        stdoutOut = QStringLiteral("Installation successful");
        return 0;
    });

    emittedEvents.clear();
    backend.commitPlan(planRevision);

    // Aufruf und Bitgenauigkeit verifizieren
    QCOMPARE(executedProgram, QStringLiteral("/usr/bin/pacstall"));
    QCOMPARE(executedArgs.size(), 3);
    QCOMPARE(executedArgs.at(0), QStringLiteral("-P"));
    QCOMPARE(executedArgs.at(1), QStringLiteral("-I"));
    QCOMPARE(executedArgs.at(2), scriptPath);
    QCOMPARE(executedFileContent, pacscriptContent); // Producer-Seite: bitgenau identisch!
    QCOMPARE(executedEnv.value(QStringLiteral("SUDO_USER")), QStringLiteral("testuser"));

    // Erfolgs-Event verifizieren
    bool successFound = false;
    for (const auto &ev : emittedEvents) {
        if (auto done = std::get_if<TransactionDone>(&ev)) {
            if (done->result == Result::Success) {
                successFound = true;
            }
        }
    }
    QVERIFY(successFound);

    // Nach erfolgreichem Commit muss die temporäre Pacscript-Datei aufgeräumt sein
    QVERIFY(!QFile::exists(scriptPath));

    // 3. Negativtest: Manipulation der Datei auf der Platte vor Commit -> Ablehnung wegen Revisionsabweichung
    emittedEvents.clear();
    backend.planPacstallUpgrade({QStringLiteral("testpkg")}, 1000);
    QString newRevision;
    for (const auto &ev : emittedEvents) {
        if (auto pr = std::get_if<PlanReady>(&ev)) {
            newRevision = pr->planRevision;
            break;
        }
    }
    QVERIFY(!newRevision.isEmpty());
    QVERIFY(QFile::exists(scriptPath));

    // Datei auf Platte böswillig manipulieren
    {
        QFile file(scriptPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("MALICIOUS CONTENT INJECTED");
        file.close();
    }

    executedProgram.clear();
    executedArgs.clear();
    emittedEvents.clear();

    backend.commitPlan(newRevision);

    // Runner darf bei Revisionskonflikt NICHT ausgeführt worden sein
    QVERIFY(executedProgram.isEmpty());
    QVERIFY(executedArgs.isEmpty());

    // Fehlermeldung verifizieren
    bool failureFound = false;
    for (const auto &ev : emittedEvents) {
        if (auto done = std::get_if<TransactionDone>(&ev)) {
            if (done->result == Result::Failed) {
                failureFound = true;
                QVERIFY(done->summary.contains(QStringLiteral("Planrevision stimmt nicht überein")));
            }
        }
    }
    QVERIFY(failureFound);
}

void PacstallBackendTest::testPacstallUpdatesModelSettingsAndAvailability() {
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings settings(settingsDir.path() + QStringLiteral("/settings.conf"));

    PacstallUpdates pacstall;
    pacstall.setAppSettings(&settings);

    // 1. Nicht unterstützt
    pacstall.setForceSupported(false);
    QCOMPARE(pacstall.supported(), false);
    QCOMPARE(pacstall.available(), false);

    // 2. Unterstützt aber nicht aktiviert
    pacstall.setForceSupported(true);
    settings.setPacstallEnabled(false);
    pacstall.setEnabled(false);
    QCOMPARE(pacstall.supported(), true);
    QCOMPARE(pacstall.enabled(), false);
    QCOMPARE(pacstall.available(), false);

    // 3. Unterstützt und aktiviert
    settings.setPacstallEnabled(true);
    pacstall.setEnabled(true);
    QCOMPARE(pacstall.supported(), true);
    QCOMPARE(pacstall.enabled(), true);
    QCOMPARE(pacstall.available(), true);

    // 4. Test-Einträge setzen
    PacstallUpdates::Entry e1;
    e1.name = QStringLiteral("hello-world");
    e1.repo = QStringLiteral("pacstall");
    e1.installed = QStringLiteral("1.0");
    e1.available = QStringLiteral("2.0");

    pacstall.setEntriesForTest({e1});
    QCOMPARE(pacstall.rowCount(), 1);
    QCOMPARE(pacstall.count(), 1);

    QModelIndex idx = pacstall.index(0, 0);
    QCOMPARE(pacstall.data(idx, PacstallUpdates::NameRole).toString(), QStringLiteral("hello-world"));
    QCOMPARE(pacstall.data(idx, PacstallUpdates::RepoRole).toString(), QStringLiteral("pacstall"));
    QCOMPARE(pacstall.data(idx, PacstallUpdates::InstalledVersionRole).toString(), QStringLiteral("1.0"));
    QCOMPARE(pacstall.data(idx, PacstallUpdates::AvailableVersionRole).toString(), QStringLiteral("2.0"));
}

QTEST_MAIN(PacstallBackendTest)
#include "pacstall_backend_test.moc"
