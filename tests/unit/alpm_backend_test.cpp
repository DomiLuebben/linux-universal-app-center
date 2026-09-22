#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QDateTime>
#include <QScopeGuard>
#include <QCryptographicHash>
#ifdef HAVE_ALPM
#include <alpm.h>
#endif
#include "liblut/backend/alpm/AlpmBackend.h"
#include "liblut/protocol/events.h"

using namespace lut;

class AlpmBackendTest : public QObject {
    Q_OBJECT

private slots:
    void testFreshRepositoryPlan();
    void testRefreshFailure();
    void testCheckupdatesFailure();
    void testCapabilities();
    void testInstalledPackages();
    void testQueryOrphansAndCache();
    void testDetectPacnew();
    void testWorkerLockCollision();
    void testEmptyTransactionIsSuccess();
    void testLibalpmEmptyTransactionContract();
    void testWorkerTestMode();
    void testInstallWithUpgradePlan(); // ALPM-01
    void testRemoveDoesNotSysupgrade(); // ALPM-02
    void testRemoveProtectedPackageBlocked(); // ALPM-05 / TX-04
    void testCommitFingerprintMismatch(); // TX-09 / TX-10
};

namespace {
bool writeFile(const QString &path, const QByteArray &data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
QString workerBinary() {
    return QStringLiteral(ALPM_WORKER_PATH);
}

struct MockAlpmEnvironment {
    QTemporaryDir temp;
    QString db;
    QString repo;
    QString archive;
    QString config;

    bool init() {
        if (!temp.isValid()) return false;
        db = temp.path() + QStringLiteral("/db");
        repo = temp.path() + QStringLiteral("/repo");
        archive = temp.path() + QStringLiteral("/archive");
        if (!QDir().mkpath(db + QStringLiteral("/local")) ||
            !QDir().mkpath(db + QStringLiteral("/sync")) ||
            !QDir().mkpath(repo) ||
            !QDir().mkpath(temp.path() + QStringLiteral("/cache"))) {
            return false;
        }
        if (!writeFile(db + QStringLiteral("/local/ALPM_DB_VERSION"), "9\n")) return false;
        config = temp.path() + QStringLiteral("/pacman.conf");
        QByteArray confContent = "[options]\nArchitecture = auto\nSigLevel = Never\nCacheDir = " +
                                 temp.path().toUtf8() + "/cache\n[demo]\nServer = file://" +
                                 repo.toUtf8() + "\n";
        return writeFile(config, confContent);
    }

    bool addLocalPackage(const QString &name, const QString &version, const QString &desc = QStringLiteral("Local test pkg")) {
        QString dir = db + QStringLiteral("/local/") + name + QLatin1Char('-') + version;
        if (!QDir().mkpath(dir)) return false;
        QByteArray content;
        content += "%NAME%\n" + name.toUtf8() + "\n\n";
        content += "%VERSION%\n" + version.toUtf8() + "\n\n";
        content += "%ARCH%\nany\n\n";
        content += "%SIZE%\n1024\n\n";
        content += "%DESC%\n" + desc.toUtf8() + "\n\n";
        return writeFile(dir + QStringLiteral("/desc"), content) &&
               writeFile(dir + QStringLiteral("/files"), "%FILES%\n\n");
    }

    bool addSyncPackage(const QString &name, const QString &version, const QString &desc = QStringLiteral("Sync test pkg")) {
        QString dir = archive + QStringLiteral("/") + name + QLatin1Char('-') + version;
        if (!QDir().mkpath(dir)) return false;
        QByteArray content;
        content += "%NAME%\n" + name.toUtf8() + "\n\n";
        content += "%VERSION%\n" + version.toUtf8() + "\n\n";
        content += "%FILENAME%\n" + name.toUtf8() + "-" + version.toUtf8() + "-any.pkg.tar.gz\n\n";
        content += "%ARCH%\nany\n\n";
        content += "%CSIZE%\n512\n\n";
        content += "%ISIZE%\n2048\n\n";
        content += "%SHA256SUM%\n0000000000000000000000000000000000000000000000000000000000000000\n\n";
        content += "%DESC%\n" + desc.toUtf8() + "\n\n";
        return writeFile(dir + QStringLiteral("/desc"), content);
    }

    bool buildSyncDb() {
        QDir archDir(archive);
        QStringList entries = archDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (entries.isEmpty()) return false;
        QProcess tar;
        QStringList args = {QStringLiteral("-czf"), db + QStringLiteral("/sync/demo.db"), QStringLiteral("-C"), archive};
        args.append(entries);
        tar.start(QStringLiteral("tar"), args);
        return tar.waitForFinished(5000) && tar.exitCode() == 0;
    }
};

}

void AlpmBackendTest::testFreshRepositoryPlan() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString db = temp.path() + "/db";
    const QString repo = temp.path() + "/repo";
    const QString archive = temp.path() + "/archive";
    QVERIFY(QDir().mkpath(db + "/local/example-1-1"));
    QVERIFY(QDir().mkpath(db + "/sync"));
    QVERIFY(QDir().mkpath(repo));
    QVERIFY(QDir().mkpath(temp.path() + "/cache"));
    QVERIFY(QDir().mkpath(temp.path() + "/payload/usr/share/lut-test"));
    QVERIFY(writeFile(temp.path() + "/payload/.PKGINFO", "pkgname = example\npkgbase = example\npkgver = 2-1\npkgdesc = Test update\nsize = 4096\narch = any\n"));
    QVERIFY(writeFile(temp.path() + "/payload/usr/share/lut-test/version", "2-1\n"));
    QVERIFY(QDir().mkpath(archive + "/example-1-1"));
    QVERIFY(writeFile(db + "/local/ALPM_DB_VERSION", "9\n"));
    const QByteArray installed = "%NAME%\nexample\n\n%VERSION%\n1-1\n\n%ARCH%\nany\n\n%SIZE%\n1024\n\n%DESC%\nTest package\n\n";
    QVERIFY(writeFile(db + "/local/example-1-1/desc", installed));
    QVERIFY(writeFile(db + "/local/example-1-1/files", "%FILES%\n\n"));
    QVERIFY(writeFile(archive + "/example-1-1/desc", installed));
    QProcess tar;
    tar.start("tar", {"-czf", db + "/sync/demo.db", "-C", archive, "example-1-1"});
    QVERIFY(tar.waitForFinished()); QCOMPARE(tar.exitCode(), 0);
    QFile stale(db + "/sync/demo.db");
    QVERIFY(stale.open(QIODevice::ReadWrite));
    QVERIFY(stale.setFileTime(QDateTime::fromSecsSinceEpoch(1000000000), QFileDevice::FileModificationTime));
    stale.close();
    tar.start("tar", {"-czf", repo + "/example-2-1-any.pkg.tar.gz", "-C", temp.path() + "/payload", ".PKGINFO", "usr"});
    QVERIFY(tar.waitForFinished()); QCOMPARE(tar.exitCode(), 0);
    QFile package(repo + "/example-2-1-any.pkg.tar.gz");
    QVERIFY(package.open(QIODevice::ReadOnly));
    const auto packageBytes = package.readAll();
    const auto digest = QCryptographicHash::hash(packageBytes, QCryptographicHash::Sha256).toHex();
    QVERIFY(QDir().mkpath(archive + "/example-2-1"));
    QVERIFY(writeFile(archive + "/example-2-1/desc", QByteArray("%NAME%\nexample\n\n%VERSION%\n2-1\n\n%FILENAME%\nexample-2-1-any.pkg.tar.gz\n\n%ARCH%\nany\n\n%CSIZE%\n") + QByteArray::number(packageBytes.size()) + "\n\n%ISIZE%\n4096\n\n%SHA256SUM%\n" + digest + "\n\n%DESC%\nTest update\n\n"));
    tar.start("tar", {"-czf", repo + "/demo.db", "-C", archive, "example-2-1"});
    QVERIFY(tar.waitForFinished()); QCOMPARE(tar.exitCode(), 0);
    const QString config = temp.path() + "/pacman.conf";
    QVERIFY(writeFile(config, ("[options]\nArchitecture = auto\nSigLevel = Never\nCacheDir = " + temp.path() + "/cache\n[demo]\nServer = file://" + repo + "\n").toUtf8()));
    const QStringList args = {"--plan", "--root", temp.path(), "--dbpath", db, "--config", config};
    QProcess worker;
    worker.start(workerBinary(), args);
    QVERIFY(worker.waitForFinished(10000));
    auto output = worker.readAllStandardOutput();
    QVERIFY2(worker.exitCode() == 0, output.constData());
    QVERIFY2(output.contains("\"ops\":[]"), output.constData()); // reproduces stale system database
    worker.start(workerBinary(), args + QStringList{"--refresh"});
    QVERIFY(worker.waitForFinished(10000));
    output = worker.readAllStandardOutput();
    QVERIFY2(worker.exitCode() == 0, output.constData());
    std::optional<PlanReady> plan;
    for (const auto &line : output.split('\n')) {
        const auto event = deserializeEvent(QJsonDocument::fromJson(line).object());
        if (event && std::holds_alternative<PlanReady>(*event)) plan = std::get<PlanReady>(*event);
    }
    QVERIFY2(plan.has_value(), output.constData());
    QCOMPARE(plan->ops.size(), 1);
    QCOMPARE(plan->ops[0].name, "example");
    QCOMPARE(plan->ops[0].newVersion, "2-1");
    QCOMPARE(plan->downloadBytes, packageBytes.size());
    QCOMPARE(plan->installedSizeDelta, 3072);
    QFile unchanged(db + "/local/example-1-1/desc");
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), installed); // planning never installs
    unchanged.close();

    // Restore stale metadata, then exercise the actual commit branch inside a
    // disposable root. fakeroot provides only simulated privileges; all paths
    // used for packages, cache, and databases belong to this temporary directory.
    tar.start("tar", {"-czf", db + "/sync/demo.db", "-C", archive, "example-1-1"});
    QVERIFY(tar.waitForFinished()); QCOMPARE(tar.exitCode(), 0);
    QVERIFY(stale.open(QIODevice::ReadWrite));
    QVERIFY(stale.setFileTime(QDateTime::fromSecsSinceEpoch(1000000000), QFileDevice::FileModificationTime));
    stale.close();
    worker.start("fakeroot", {workerBinary(), "--sysupgrade", "--root", temp.path(), "--dbpath", db, "--config", config});
    QVERIFY(worker.waitForFinished(20000));
    output = worker.readAllStandardOutput();
    QVERIFY2(worker.exitCode() == 0, output.constData());
    QVERIFY2(output.contains("\"result\":\"Success\""), output.constData());
    QFile version(temp.path() + "/usr/share/lut-test/version");
    QVERIFY2(version.open(QIODevice::ReadOnly), output.constData());
    QCOMPARE(version.readAll(), "2-1\n");
    QVERIFY(QFile::exists(db + "/local/example-2-1/desc"));
    QVERIFY(!QFile::exists(db + "/local/example-1-1/desc"));
}

void AlpmBackendTest::testRefreshFailure() {
    QTemporaryDir temp;
    QVERIFY(QDir().mkpath(temp.path() + "/db/local"));
    const QString config = temp.path() + "/pacman.conf";
    QVERIFY(writeFile(config, ("[options]\nSigLevel = Never\n[missing]\nServer = file://" + temp.path() + "/not-present\n").toUtf8()));
    QProcess worker;
    worker.start(workerBinary(), {"--plan", "--refresh", "--root", temp.path(), "--dbpath", temp.path() + "/db", "--config", config});
    QVERIFY(worker.waitForFinished(10000));
    const auto output = worker.readAllStandardOutput();
    QCOMPARE(worker.exitCode(), 1);
    QVERIFY(output.contains("\"result\":\"Failed\""));
    QVERIFY(!output.contains("PlanReady"));
    QVERIFY(!output.contains("System ist aktuell"));
}

void AlpmBackendTest::testCheckupdatesFailure() {
    QTemporaryDir temp;
    const QString script = temp.path() + "/checkupdates";
    QVERIFY(writeFile(script, "#!/bin/sh\necho 'Mirror unavailable' >&2\nexit 1\n"));
    QVERIFY(QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const QByteArray oldPath = qgetenv("PATH");
    const auto restore = qScopeGuard([&] { qputenv("PATH", oldPath); });
    qputenv("PATH", temp.path().toUtf8() + ':' + oldPath);
    AlpmBackend backend;
    bool sawPlan = false, sawFailure = false;
    connect(&backend, &Backend::eventEmitted, this, [&](const Event &event) {
        if (std::holds_alternative<PlanReady>(event)) sawPlan = true;
        if (auto *done = std::get_if<TransactionDone>(&event))
            sawFailure = done->result == Result::Failed && done->summary.contains("Mirror unavailable");
    });
    backend.planUpgradeAll();
    QVERIFY(sawFailure);
    QVERIFY(!sawPlan);
}

void AlpmBackendTest::testCapabilities() {
    AlpmBackend backend;
    auto cap = backend.capabilities();
    // Strikte Arch Linux / CachyOS Regel: partialUpgrade muss false sein!
    QVERIFY(!cap.partialUpgrade);
    QVERIFY(!cap.historyUndo);
    QVERIFY(cap.autoremove);
    QVERIFY(cap.parallelDownloads);
    QVERIFY(cap.catalogQuery);
    QVERIFY(cap.install);
    QVERIFY(cap.remove);
    QVERIFY(cap.installRequiresFullUpgrade);
    QVERIFY(cap.typedPackageTargets);
}

void AlpmBackendTest::testInstalledPackages() {
    AlpmBackend backend;
    auto pkgs = backend.installedPackages();
    // Auf CachyOS müssen installierte Pakete gefunden werden
    QVERIFY(pkgs.size() > 0);

    bool foundPacmanOrCore = false;
    for (const auto &pkg : pkgs) {
        if (pkg.name == QLatin1String("pacman") || pkg.name == QLatin1String("glibc")) {
            foundPacmanOrCore = true;
            QVERIFY(!pkg.version.isEmpty());
            QVERIFY(pkg.installedSize > 0);
            QVERIFY(!pkg.arch.isEmpty());
        }
    }
    QVERIFY(foundPacmanOrCore);

    // Suche mit Filter
    auto filtered = backend.installedPackages(QStringLiteral("pacman"));
    QVERIFY(filtered.size() >= 1);
    bool nameMatched = false;
    for (const auto &pkg : filtered) {
        if (pkg.name.contains(QLatin1String("pacman"), Qt::CaseInsensitive)) {
            nameMatched = true;
        }
    }
    QVERIFY(nameMatched);
}

void AlpmBackendTest::testQueryOrphansAndCache() {
    AlpmBackend backend;
    auto orphans = backend.queryOrphans();
    // queryOrphans darf nicht abstürzen
    Q_UNUSED(orphans);

    qint64 cacheBytes = backend.queryCleanableCacheBytes();
    QVERIFY(cacheBytes >= 0);
}

void AlpmBackendTest::testDetectPacnew() {
    AlpmBackend backend;
    QStringList pacnews = backend.detectPacnewFiles();
    // Auf diesem CachyOS-System existieren .pacnew-Dateien in /etc
    for (const QString &f : pacnews) {
        QVERIFY(f.endsWith(QLatin1String(".pacnew")) || f.endsWith(QLatin1String(".pacsave")));
    }
}

void AlpmBackendTest::testWorkerLockCollision() {
    // Temporäres Verzeichnis mit gefakter db.lck anlegen
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString fakeDbDir = tempDir.path() + QStringLiteral("/pacman");
    QDir().mkpath(fakeDbDir);
    QString lockPath = fakeDbDir + QStringLiteral("/db.lck");
    QFile lockFile(lockPath);
    QVERIFY(lockFile.open(QIODevice::WriteOnly));
    lockFile.write("1234\n");
    lockFile.close();

    // Finde lut-alpm-worker binary
    QString workerPath = QStringLiteral(ALPM_WORKER_PATH);
    if (!QFile::exists(workerPath)) {
        workerPath = QCoreApplication::applicationDirPath() + QStringLiteral("/lut-alpm-worker");
    }
    QVERIFY(QFile::exists(workerPath));

    QProcess proc;
    proc.start(workerPath, {QStringLiteral("--dbpath"), fakeDbDir});
    QVERIFY(proc.waitForFinished(5000));

    // Kollision muss Exit-Code 1 liefern
    QCOMPARE(proc.exitCode(), 1);

    QString out = QString::fromUtf8(proc.readAllStandardOutput());
    QVERIFY(out.contains(QLatin1String("db.lck")));
    QVERIFY(out.contains(QLatin1String("TransactionDone")));
    QVERIFY(out.contains(QLatin1String("Failed")));
}

// Pinnt die libalpm-Zusicherung, auf der die Leerlauf-Behandlung im Worker
// beruht: bei leerer Transaktion liefert prepare 0, commit scheitert aber mit
// ALPM_ERR_TRANS_NOT_PREPARED. Ändert libalpm das, muss der Worker nachziehen.
void AlpmBackendTest::testLibalpmEmptyTransactionContract() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString root = tempDir.path() + QStringLiteral("/root/");
    const QString db = tempDir.path() + QStringLiteral("/db/");
    QVERIFY(QDir().mkpath(root) && QDir().mkpath(db + QStringLiteral("local")));

    alpm_errno_t err;
    alpm_handle_t *handle = alpm_initialize(root.toUtf8().constData(), db.toUtf8().constData(), &err);
    QVERIFY2(handle, alpm_strerror(err));
    QCOMPARE(alpm_trans_init(handle, 0), 0);

    alpm_list_t *data = nullptr;
    QCOMPARE(alpm_trans_prepare(handle, &data), 0);          // meldet Erfolg ...
    QCOMPARE(alpm_trans_get_add(handle), nullptr);
    QCOMPARE(alpm_trans_get_remove(handle), nullptr);
    QCOMPARE(alpm_trans_commit(handle, &data), -1);          // ... commit aber nicht
    QCOMPARE(alpm_errno(handle), ALPM_ERR_TRANS_NOT_PREPARED);

    alpm_trans_release(handle);
    alpm_release(handle);
}

// Ein System ohne verfügbare Aktualisierungen muss als Erfolg enden, nicht als
// "Transaktionsausführung fehlgeschlagen: Vorgang nicht vorbereitet".
void AlpmBackendTest::testEmptyTransactionIsSuccess() {
    QString workerPath = QStringLiteral(ALPM_WORKER_PATH);
    if (!QFile::exists(workerPath)) {
        workerPath = QCoreApplication::applicationDirPath() + QStringLiteral("/lut-alpm-worker");
    }
    QVERIFY(QFile::exists(workerPath));

    // Leere Datenbank in einem Wegwerfverzeichnis: es gibt nichts zu tun.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString db = tempDir.path() + QStringLiteral("/db");
    QVERIFY(QDir().mkpath(db + QStringLiteral("/local")));

    QProcess proc;
    proc.start(workerPath, {QStringLiteral("--dry-run"),
                            QStringLiteral("--root"), tempDir.path(),
                            QStringLiteral("--dbpath"), db});
    QVERIFY(proc.waitForFinished(60000));

    const QString out = QString::fromUtf8(proc.readAllStandardOutput());
    QVERIFY2(proc.exitCode() == 0, qPrintable(out));
    QVERIFY2(out.contains(QLatin1String("\"result\":\"Success\"")), qPrintable(out));
    QVERIFY2(!out.contains(QLatin1String("nicht vorbereitet")), qPrintable(out));
    // Auf die unterscheidende Meldung prüfen, nicht nur auf "Success": ohne die
    // Leerlauf-Behandlung fällt der Worker durch und meldet fälschlich
    // "System erfolgreich aktualisiert", obwohl nichts geschehen ist.
    // QStringLiteral, nicht QLatin1String: die Quelldatei ist UTF-8, und
    // QLatin1String würde das "ü" byteweise als zwei Zeichen lesen.
    QVERIFY2(out.contains(QStringLiteral("Keine Aktualisierungen verfügbar")), qPrintable(out));
    QVERIFY2(!out.contains(QLatin1String("System erfolgreich aktualisiert")), qPrintable(out));
}

void AlpmBackendTest::testWorkerTestMode() {
    QString workerPath = QStringLiteral(ALPM_WORKER_PATH);
    if (!QFile::exists(workerPath)) {
        workerPath = QCoreApplication::applicationDirPath() + QStringLiteral("/lut-alpm-worker");
    }
    QVERIFY(QFile::exists(workerPath));

    QProcess proc;
    proc.start(workerPath, {QStringLiteral("--test-mode")});
    QVERIFY(proc.waitForFinished(5000));
    QCOMPARE(proc.exitCode(), 0);

    QList<Event> receivedEvents;
    bool sawRefresh = false;
    bool sawResolve = false;
    bool sawVerify = false;
    bool sawDownload = false;
    bool sawCommit = false;
    bool sawPostTransaction = false;
    bool sawCleanup = false;
    bool sawFinished = false;
    bool sawHook = false;
    bool sawDoneSuccess = false;

    while (proc.canReadLine()) {
        QString line = QString::fromUtf8(proc.readLine()).trimmed();
        if (line.isEmpty()) continue;

        QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
        if (!doc.isObject()) continue;

        auto ev = deserializeEvent(doc.object());
        if (!ev.has_value()) continue;

        receivedEvents.append(*ev);

        if (std::holds_alternative<PhaseChanged>(*ev)) {
            auto pc = std::get<PhaseChanged>(*ev);
            if (pc.phase == Phase::RefreshMetadata) sawRefresh = true;
            else if (pc.phase == Phase::Resolve) sawResolve = true;
            else if (pc.phase == Phase::Verify) sawVerify = true;
            else if (pc.phase == Phase::Download) sawDownload = true;
            else if (pc.phase == Phase::Commit) {
                sawCommit = true;
                // Commit darf nicht abbrechbar sein
                QVERIFY(!pc.cancellable);
            } else if (pc.phase == Phase::PostTransaction) {
                sawPostTransaction = true;
                QVERIFY(!pc.cancellable);
            } else if (pc.phase == Phase::Cleanup) sawCleanup = true;
            else if (pc.phase == Phase::Finished) sawFinished = true;
        } else if (std::holds_alternative<ScriptletStarted>(*ev)) {
            auto sc = std::get<ScriptletStarted>(*ev);
            if (sc.pkgId == QLatin1String("alpm-hook")) {
                sawHook = true;
            }
        } else if (std::holds_alternative<TransactionDone>(*ev)) {
            auto td = std::get<TransactionDone>(*ev);
            if (td.result == Result::Success) {
                sawDoneSuccess = true;
                QVERIFY(td.rebootRequired);
            }
        }
    }

    QVERIFY(sawRefresh);
    QVERIFY(sawResolve);
    QVERIFY(sawVerify);
    QVERIFY(sawDownload);
    QVERIFY(sawCommit);
    QVERIFY(sawPostTransaction);
    QVERIFY(sawHook);
    QVERIFY(sawCleanup);
    QVERIFY(sawDoneSuccess);
    QVERIFY(sawFinished);
}

void AlpmBackendTest::testInstallWithUpgradePlan() {
    MockAlpmEnvironment env;
    QVERIFY(env.init());
    QVERIFY(env.addLocalPackage(QStringLiteral("existing-app"), QStringLiteral("1-1")));
    QVERIFY(env.addSyncPackage(QStringLiteral("existing-app"), QStringLiteral("2-1")));
    QVERIFY(env.addSyncPackage(QStringLiteral("new-app"), QStringLiteral("1-0")));
    QVERIFY(env.buildSyncDb());

    QProcess worker;
    const QStringList args = {
        QStringLiteral("--plan"),
        QStringLiteral("--action"), QStringLiteral("install"),
        QStringLiteral("--install"), QStringLiteral("new-app"),
        QStringLiteral("--root"), env.temp.path(),
        QStringLiteral("--dbpath"), env.db,
        QStringLiteral("--config"), env.config
    };
    worker.start(workerBinary(), args);
    QVERIFY(worker.waitForFinished(10000));
    const auto output = worker.readAllStandardOutput();
    QVERIFY2(worker.exitCode() == 0, output.constData());

    std::optional<PlanReady> plan;
    for (const auto &line : output.split('\n')) {
        const auto event = deserializeEvent(QJsonDocument::fromJson(line).object());
        if (event && std::holds_alternative<PlanReady>(*event)) {
            plan = std::get<PlanReady>(*event);
        }
    }
    QVERIFY2(plan.has_value(), output.constData());
    // ALPM-01: No Partial Upgrade: install of new-app also co-plans existing-app upgrade!
    QCOMPARE(plan->ops.size(), 2);
    bool foundInstall = false;
    bool foundUpgrade = false;
    for (const auto &op : plan->ops) {
        if (op.name == QLatin1String("new-app") && op.kind == PackageOp::Kind::Install) {
            foundInstall = true;
        }
        if (op.name == QLatin1String("existing-app") && op.kind == PackageOp::Kind::Upgrade) {
            foundUpgrade = true;
        }
    }
    QVERIFY(foundInstall);
    QVERIFY(foundUpgrade);
    QVERIFY(!plan->planRevision.isEmpty());
}

void AlpmBackendTest::testRemoveDoesNotSysupgrade() {
    MockAlpmEnvironment env;
    QVERIFY(env.init());
    QVERIFY(env.addLocalPackage(QStringLiteral("existing-app"), QStringLiteral("1-1")));
    QVERIFY(env.addLocalPackage(QStringLiteral("app-to-remove"), QStringLiteral("1-0")));
    QVERIFY(env.addSyncPackage(QStringLiteral("existing-app"), QStringLiteral("2-1")));
    QVERIFY(env.buildSyncDb());

    QProcess worker;
    const QStringList args = {
        QStringLiteral("--plan"),
        QStringLiteral("--action"), QStringLiteral("remove"),
        QStringLiteral("--remove"), QStringLiteral("app-to-remove"),
        QStringLiteral("--root"), env.temp.path(),
        QStringLiteral("--dbpath"), env.db,
        QStringLiteral("--config"), env.config
    };
    worker.start(workerBinary(), args);
    QVERIFY(worker.waitForFinished(10000));
    const auto output = worker.readAllStandardOutput();
    QVERIFY2(worker.exitCode() == 0, output.constData());

    std::optional<PlanReady> plan;
    for (const auto &line : output.split('\n')) {
        const auto event = deserializeEvent(QJsonDocument::fromJson(line).object());
        if (event && std::holds_alternative<PlanReady>(*event)) {
            plan = std::get<PlanReady>(*event);
        }
    }
    QVERIFY2(plan.has_value(), output.constData());
    // ALPM-02: Remove must NEVER call alpm_sync_sysupgrade!
    QCOMPARE(plan->ops.size(), 1);
    QCOMPARE(plan->ops.first().name, QStringLiteral("app-to-remove"));
    QCOMPARE(plan->ops.first().kind, PackageOp::Kind::Remove);
    QVERIFY(!plan->planRevision.isEmpty());
}

void AlpmBackendTest::testRemoveProtectedPackageBlocked() {
    MockAlpmEnvironment env;
    QVERIFY(env.init());
    QVERIFY(env.addLocalPackage(QStringLiteral("pacman"), QStringLiteral("7.1.0-1")));
    QVERIFY(env.addSyncPackage(QStringLiteral("dummy"), QStringLiteral("1-0")));
    QVERIFY(env.buildSyncDb());

    QProcess worker;
    const QStringList args = {
        QStringLiteral("--plan"),
        QStringLiteral("--action"), QStringLiteral("remove"),
        QStringLiteral("--remove"), QStringLiteral("pacman"),
        QStringLiteral("--root"), env.temp.path(),
        QStringLiteral("--dbpath"), env.db,
        QStringLiteral("--config"), env.config
    };
    worker.start(workerBinary(), args);
    QVERIFY(worker.waitForFinished(10000));
    const auto output = worker.readAllStandardOutput();
    // ALPM-05 / TX-04: Removal of protected package 'pacman' must be blocked!
    QVERIFY(worker.exitCode() != 0);
    QVERIFY(output.contains("\"result\":\"Failed\""));
    QVERIFY(output.contains("pacman"));
}

void AlpmBackendTest::testCommitFingerprintMismatch() {
    MockAlpmEnvironment env;
    QVERIFY(env.init());
    QVERIFY(env.addSyncPackage(QStringLiteral("new-app"), QStringLiteral("1-0")));
    QVERIFY(env.buildSyncDb());

    // 1. Plan first to get the legitimate fingerprint
    QProcess worker;
    const QStringList planArgs = {
        QStringLiteral("--plan"),
        QStringLiteral("--action"), QStringLiteral("install"),
        QStringLiteral("--install"), QStringLiteral("new-app"),
        QStringLiteral("--root"), env.temp.path(),
        QStringLiteral("--dbpath"), env.db,
        QStringLiteral("--config"), env.config
    };
    worker.start(workerBinary(), planArgs);
    QVERIFY(worker.waitForFinished(10000));
    auto output = worker.readAllStandardOutput();
    QVERIFY2(worker.exitCode() == 0, output.constData());

    std::optional<PlanReady> plan;
    for (const auto &line : output.split('\n')) {
        const auto event = deserializeEvent(QJsonDocument::fromJson(line).object());
        if (event && std::holds_alternative<PlanReady>(*event)) {
            plan = std::get<PlanReady>(*event);
        }
    }
    QVERIFY(plan.has_value());
    const QString legitimateFingerprint = plan->planRevision;
    QVERIFY(!legitimateFingerprint.isEmpty());

    // 2. Commit with MISMATCHED expected fingerprint (TX-09, TX-10 negative control)
    const QStringList mismatchArgs = {
        QStringLiteral("--dry-run"),
        QStringLiteral("--commit"),
        QStringLiteral("--action"), QStringLiteral("install"),
        QStringLiteral("--install"), QStringLiteral("new-app"),
        QStringLiteral("--expected-fingerprint"), QStringLiteral("0000000000000000000000000000000000000000000000000000000000000000"),
        QStringLiteral("--root"), env.temp.path(),
        QStringLiteral("--dbpath"), env.db,
        QStringLiteral("--config"), env.config
    };
    worker.start(workerBinary(), mismatchArgs);
    QVERIFY(worker.waitForFinished(10000));
    output = worker.readAllStandardOutput();
    QVERIFY2(worker.exitCode() != 0, output.constData());
    QVERIFY2(output.contains("Planabweichung"), output.constData());

    // 3. Commit with MATCHING expected fingerprint (TX-09 positive control)
    const QStringList matchArgs = {
        QStringLiteral("--dry-run"),
        QStringLiteral("--commit"),
        QStringLiteral("--action"), QStringLiteral("install"),
        QStringLiteral("--install"), QStringLiteral("new-app"),
        QStringLiteral("--expected-fingerprint"), legitimateFingerprint,
        QStringLiteral("--root"), env.temp.path(),
        QStringLiteral("--dbpath"), env.db,
        QStringLiteral("--config"), env.config
    };
    worker.start(workerBinary(), matchArgs);
    QVERIFY(worker.waitForFinished(10000));
    output = worker.readAllStandardOutput();
    QVERIFY2(worker.exitCode() == 0, output.constData());
    QVERIFY2(output.contains("\"result\":\"Success\""), output.constData());
}

QTEST_MAIN(AlpmBackendTest)
#include "alpm_backend_test.moc"
