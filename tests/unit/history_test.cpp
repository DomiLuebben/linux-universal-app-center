#include <QTest>
#include <QTemporaryDir>
#include "liblut/history/HistoryDb.h"

class HistoryTest : public QObject {
    Q_OBJECT

private slots:
    void testOpenRecordAndQuery();
    void testAverageCommitRate();
};

void HistoryTest::testOpenRecordAndQuery() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.path() + QStringLiteral("/test_history.db");

    lut::HistoryDb db(dbPath);
    QVERIFY(db.open());

    qint64 id = db.recordTransaction(5000, 10 * 1024 * 1024, 20 * 1024 * 1024,
                                     QStringLiteral("Success"), QStringLiteral("Test Run"));
    QVERIFY(id > 0);

    auto list = db.recentTransactions(10);
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.first().id, id);
    QCOMPARE(list.first().durationMs, 5000);
    QCOMPARE(list.first().downloadBytes, 10 * 1024 * 1024);
    QCOMPARE(list.first().installBytes, 20 * 1024 * 1024);
    QCOMPARE(list.first().result, QStringLiteral("Success"));
    QCOMPARE(list.first().summary, QStringLiteral("Test Run"));
}

void HistoryTest::testAverageCommitRate() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.path() + QStringLiteral("/test_history.db");

    lut::HistoryDb db(dbPath);
    QVERIFY(db.open());

    QVERIFY(!db.averageCommitBytesPerSecond().has_value());

    // 10 MB in 2000 ms = 5 MB/s
    db.recordTransaction(2000, 0, 10 * 1024 * 1024, QStringLiteral("Success"), QString());
    // 20 MB in 2000 ms = 10 MB/s
    db.recordTransaction(2000, 0, 20 * 1024 * 1024, QStringLiteral("Success"), QString());

    auto avg = db.averageCommitBytesPerSecond(5);
    QVERIFY(avg.has_value());
    // Durchschnitt: (5 + 10) / 2 = 7.5 MB/s
    double expected = 7.5 * 1024 * 1024;
    QVERIFY(std::abs(*avg - expected) < 1000.0);
}

QTEST_MAIN(HistoryTest)
#include "history_test.moc"
