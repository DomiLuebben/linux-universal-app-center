#include <QTest>
#include <QElapsedTimer>
#include "linux-update-tool/models/UpdatesModel.h"

class ListPerfTest : public QObject {
    Q_OBJECT

private slots:
    void test2500PackagesPerformance();
    void testSelectionPerformance();
};

void ListPerfTest::test2500PackagesPerformance() {
    lut::UpdatesModel model;

    QList<lut::PackageOp> ops;
    ops.reserve(2500);

    for (int i = 0; i < 2500; ++i) {
        lut::PackageOp op;
        op.id = QStringLiteral("pkg-%1").arg(i);
        op.name = QStringLiteral("package-name-%1").arg(i);
        op.version = QStringLiteral("1.0.%1").arg(i);
        op.newVersion = QStringLiteral("1.1.%1").arg(i);
        op.arch = QStringLiteral("x86_64");
        op.repo = QStringLiteral("repo-main");
        op.summary = QStringLiteral("Summary for package %1").arg(i);
        op.downloadSize = 1024 * 1024;
        op.installedSize = 2 * 1024 * 1024;
        op.kind = lut::PackageOp::Kind::Upgrade;
        ops.append(op);
    }

    QElapsedTimer timer;
    timer.start();
    model.setPackages(ops);
    qint64 elapsedMs = timer.elapsed();

    QCOMPARE(model.totalCount(), 2500);
    // 2500 Items laden muss unter 25 ms liegen
    QVERIFY2(elapsedMs < 25, qPrintable(QString("Setting 2500 packages took %1 ms (must be < 25ms)").arg(elapsedMs)));
}

void ListPerfTest::testSelectionPerformance() {
    lut::UpdatesModel model;
    QList<lut::PackageOp> ops;
    for (int i = 0; i < 2500; ++i) {
        lut::PackageOp op;
        op.id = QStringLiteral("pkg-%1").arg(i);
        op.downloadSize = 1000;
        op.installedSize = 2000;
        ops.append(op);
    }
    model.setPackages(ops);

    QElapsedTimer timer;
    timer.start();
    model.selectAll(false);
    qint64 unselectMs = timer.elapsed();
    QCOMPARE(model.selectedCount(), 0);
    QVERIFY2(unselectMs < 10, qPrintable(QString("Unselecting 2500 items took %1 ms").arg(unselectMs)));

    timer.restart();
    model.selectAll(true);
    qint64 selectMs = timer.elapsed();
    QCOMPARE(model.selectedCount(), 2500);
    QVERIFY2(selectMs < 10, qPrintable(QString("Selecting 2500 items took %1 ms").arg(selectMs)));
}

QTEST_MAIN(ListPerfTest)
#include "list_perf_test.moc"
