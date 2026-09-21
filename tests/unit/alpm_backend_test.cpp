#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#ifdef HAVE_ALPM
#include <alpm.h>
#endif
#include "liblut/backend/alpm/AlpmBackend.h"
#include "liblut/protocol/events.h"

using namespace lut;

class AlpmBackendTest : public QObject {
    Q_OBJECT

private slots:
    void testCapabilities();
    void testInstalledPackages();
    void testQueryOrphansAndCache();
    void testDetectPacnew();
    void testWorkerLockCollision();
    void testEmptyTransactionIsSuccess();
    void testLibalpmEmptyTransactionContract();
    void testWorkerTestMode();
};

void AlpmBackendTest::testCapabilities() {
    AlpmBackend backend;
    auto cap = backend.capabilities();
    // Strikte Arch Linux / CachyOS Regel: partialUpgrade muss false sein!
    QVERIFY(!cap.partialUpgrade);
    QVERIFY(!cap.historyUndo);
    QVERIFY(cap.autoremove);
    QVERIFY(cap.parallelDownloads);
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
    QString workerPath = QStringLiteral(PROJECT_DIR) + QStringLiteral("/build/liblut/lut-alpm-worker");
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
    QString workerPath = QStringLiteral(PROJECT_DIR) + QStringLiteral("/build/liblut/lut-alpm-worker");
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
    QString workerPath = QStringLiteral(PROJECT_DIR) + QStringLiteral("/build/liblut/lut-alpm-worker");
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

QTEST_MAIN(AlpmBackendTest)
#include "alpm_backend_test.moc"
