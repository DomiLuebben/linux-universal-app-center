#include <QTest>
#include <cmath>
#include "liblut/progress/Weights.h"

class WeightsTest : public QObject {
    Q_OBJECT

private slots:
    void testWeightsSumEqualsOne();
    void testWeightsClamping();
    void testBaseProgressMonotonicity();
};

void WeightsTest::testWeightsSumEqualsOne() {
    struct Case { qint64 d; qint64 i; };
    const QList<Case> cases = {
        {0, 0},
        {1024, 1024},
        {100 * 1024 * 1024, 500 * 1024 * 1024},
        {1, 10000000000LL},
        {10000000000LL, 1},
        {50 * 1024 * 1024, 0}
    };

    for (const auto &c : cases) {
        lut::PhaseWeights w = lut::calculateWeights(c.d, c.i);
        QVERIFY2(std::abs(w.total() - 1.0) < 1e-9,
                 qPrintable(QString("Sum of weights must be 1.0 for d=%1, i=%2, got %3")
                                .arg(c.d).arg(c.i).arg(w.total())));
        QVERIFY(w.refresh > 0.0);
        QVERIFY(w.resolve > 0.0);
        QVERIFY(w.download >= 0.05);
        QVERIFY(w.download <= 0.75);
        QVERIFY(w.verify > 0.0);
        QVERIFY(w.commit > 0.0);
        QVERIFY(w.post > 0.0);
    }
}

void WeightsTest::testWeightsClamping() {
    // Extreme download vs install
    lut::PhaseWeights wMin = lut::calculateWeights(1, 10000000000LL);
    QCOMPARE(wMin.download, 0.05);

    lut::PhaseWeights wMax = lut::calculateWeights(10000000000LL, 1);
    QCOMPARE(wMax.download, 0.75);
}

void WeightsTest::testBaseProgressMonotonicity() {
    lut::PhaseWeights w = lut::calculateWeights(100 * 1024 * 1024, 200 * 1024 * 1024);

    const QList<lut::Phase> sequence = {
        lut::Phase::RefreshMetadata,
        lut::Phase::Resolve,
        lut::Phase::Download,
        lut::Phase::Verify,
        lut::Phase::Commit,
        lut::Phase::PostTransaction,
        lut::Phase::Cleanup,
        lut::Phase::Finished
    };

    double prevBase = -1.0;
    for (lut::Phase p : sequence) {
        double base = lut::phaseBaseProgress(p, w);
        QVERIFY2(base >= prevBase,
                 qPrintable(QString("Base progress for phase %1 (%2) must be >= prev (%3)")
                                .arg(lut::phaseToString(p)).arg(base).arg(prevBase)));
        prevBase = base;
    }
    QCOMPARE(lut::phaseBaseProgress(lut::Phase::Finished, w), 1.0);
}

QTEST_MAIN(WeightsTest)
#include "weights_test.moc"
