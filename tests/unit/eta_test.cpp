#include <QTest>
#include "liblut/progress/Eta.h"

class EtaTest : public QObject {
    Q_OBJECT

private slots:
    void testInsufficientSamples();
    void testStableThroughputProducesEta();
    void testHighVarianceSuppressesEta();
    void testFormatRemainingTime();
};

void EtaTest::testInsufficientSamples() {
    lut::EtaCalculator calc(0.2, 10, 0.25);
    QVERIFY(!calc.hasValidEta());

    // 9 samples (< 10)
    for (int i = 0; i < 9; ++i) {
        calc.addThroughputSample(1024 * 1024);
        QVERIFY(!calc.hasValidEta());
    }
}

void EtaTest::testStableThroughputProducesEta() {
    lut::EtaCalculator calc(0.2, 10, 0.25);

    // 10 identical samples
    for (int i = 0; i < 10; ++i) {
        calc.addThroughputSample(10 * 1024 * 1024); // 10 MB/s
    }

    QVERIFY(calc.hasValidEta());
    QVERIFY(calc.relativeStandardDeviation() < 0.01);

    auto est = calc.estimateRemainingSeconds(100 * 1024 * 1024);
    QVERIFY(est.has_value());
    QCOMPARE(*est, 10); // 100 MB / 10 MB/s = 10s
}

void EtaTest::testHighVarianceSuppressesEta() {
    lut::EtaCalculator calc(0.2, 10, 0.25);

    // Fluctuating wildly between 10 KB/s and 50 MB/s
    for (int i = 0; i < 5; ++i) {
        calc.addThroughputSample(10 * 1024);
        calc.addThroughputSample(50 * 1024 * 1024);
    }

    QVERIFY(calc.relativeStandardDeviation() > 0.25);
    QVERIFY(!calc.hasValidEta());
    QVERIFY(!calc.estimateRemainingSeconds(100 * 1024 * 1024).has_value());
}

void EtaTest::testFormatRemainingTime() {
    lut::EtaCalculator calc;
    QCOMPARE(calc.formatRemainingTime(0), QString());
    QCOMPARE(calc.formatRemainingTime(8), QStringLiteral("noch wenige Sekunden"));
    QCOMPARE(calc.formatRemainingTime(28), QStringLiteral("noch etwa 30 Sekunden"));
    QCOMPARE(calc.formatRemainingTime(43), QStringLiteral("noch etwa 45 Sekunden"));
    QCOMPARE(calc.formatRemainingTime(120), QStringLiteral("noch etwa 2 Minuten"));
    QCOMPARE(calc.formatRemainingTime(3600), QStringLiteral("noch etwa 1 Stunde"));
    QCOMPARE(calc.formatRemainingTime(7200), QStringLiteral("noch etwa 2 Stunden"));
}

QTEST_MAIN(EtaTest)
#include "eta_test.moc"
