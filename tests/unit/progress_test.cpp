#include <QTest>
#include "liblut/progress/ProgressModel.h"

class ProgressModelTest : public QObject {
    Q_OBJECT

private slots:
    void testMonotonicity();
    void testKernelPlanAndNote();
    void testNonKernelPlanHasNoNote();
    void testScriptletLifecycleAndMapping();
    void testDownloadThroughputAndEta();
    void testTransactionDoneResults();
    void testResetClearsAllState();
    void testLogLineCapping();
    void testQuestionPropagation();
};

void ProgressModelTest::testMonotonicity() {
    lut::ProgressModel model;

    lut::PlanReady plan;
    lut::PackageOp op;
    op.id = QStringLiteral("pkg1");
    op.name = QStringLiteral("pkg1");
    op.downloadSize = 1000;
    op.installedSize = 2000;
    plan.ops.append(op);
    plan.downloadBytes = 1000;
    plan.installedSizeDelta = 2000;
    model.processEvent(plan);

    double lastProgress = 0.0;
    auto checkMonotonic = [&](const lut::Event &ev) {
        model.processEvent(ev);
        QVERIFY2(model.totalProgress() >= lastProgress,
                 qPrintable(QString("Progress decreased! Was %1, now %2")
                                .arg(lastProgress).arg(model.totalProgress())));
        lastProgress = model.totalProgress();
    };

    checkMonotonic(lut::PhaseChanged{lut::Phase::RefreshMetadata, QString(), true});
    checkMonotonic(lut::PhaseChanged{lut::Phase::Resolve, QString(), true});
    checkMonotonic(lut::PhaseChanged{lut::Phase::Download, QString(), true});

    // Download schiebt voran
    checkMonotonic(lut::DownloadThroughput{100, 500, 1000});
    // Simulierter "Rücksprung" im Durchsatz / DownloadDone darf Gesamtfortschritt nicht senken!
    checkMonotonic(lut::DownloadThroughput{100, 200, 1000});

    checkMonotonic(lut::PhaseChanged{lut::Phase::Verify, QString(), true});
    checkMonotonic(lut::PhaseChanged{lut::Phase::Commit, QString(), false});

    checkMonotonic(lut::ItemStarted{QStringLiteral("pkg1"), lut::PackageOp::Kind::Upgrade, 2000});
    checkMonotonic(lut::ItemProgress{QStringLiteral("pkg1"), 1500, 2000});
    // Rückwärtssprung bei ItemProgress
    checkMonotonic(lut::ItemProgress{QStringLiteral("pkg1"), 500, 2000});
    checkMonotonic(lut::ItemFinished{QStringLiteral("pkg1"), true, QString()});

    checkMonotonic(lut::PhaseChanged{lut::Phase::PostTransaction, QString(), false});
    checkMonotonic(lut::ScriptletStarted{QStringLiteral("pkg1"), QStringLiteral("dracut")});
    checkMonotonic(lut::ScriptletFinished{QStringLiteral("pkg1"), QStringLiteral("dracut"), 0});

    checkMonotonic(lut::PhaseChanged{lut::Phase::Cleanup, QString(), false});
    QCOMPARE(model.totalProgress(), 1.0);

    checkMonotonic(lut::TransactionDone{lut::Result::Success, QString(), false, {}, 1});
    QCOMPARE(model.totalProgress(), 1.0);
}

void ProgressModelTest::testKernelPlanAndNote() {
    lut::ProgressModel model;

    lut::PlanReady plan;
    lut::PackageOp op;
    op.id = QStringLiteral("kernel-core-6.17.4");
    op.name = QStringLiteral("kernel-core");
    op.isKernel = true;
    plan.ops.append(op);

    model.processEvent(plan);
    QVERIFY(model.hasKernelUpdate());
    QVERIFY(!model.postTransactionNote().isEmpty());
    QVERIFY(model.postTransactionNote().contains(QLatin1String("Initramfs und Bootloader")));
}

void ProgressModelTest::testNonKernelPlanHasNoNote() {
    lut::ProgressModel model;

    lut::PlanReady plan;
    lut::PackageOp op;
    op.id = QStringLiteral("curl-8.0");
    op.name = QStringLiteral("curl");
    op.isKernel = false;
    plan.ops.append(op);

    model.processEvent(plan);
    QVERIFY(!model.hasKernelUpdate());
    QVERIFY(model.postTransactionNote().isEmpty());
}

void ProgressModelTest::testScriptletLifecycleAndMapping() {
    lut::ProgressModel model;
    model.processEvent(lut::PhaseChanged{lut::Phase::PostTransaction, QString(), false});

    // Test Mapping
    QCOMPARE(lut::ProgressModel::mapScriptletToDescription(QStringLiteral("dracut")),
             QStringLiteral("Initramfs erzeugen"));
    QCOMPARE(lut::ProgressModel::mapScriptletToDescription(QStringLiteral("kernel-install")),
             QStringLiteral("Bootloader-Eintrag anlegen"));
    QCOMPARE(lut::ProgressModel::mapScriptletToDescription(QStringLiteral("gtk-update-icon-cache")),
             QStringLiteral("Icon-Zwischenspeicher erneuern"));

    // Lifecycle
    model.processEvent(lut::ScriptletStarted{QStringLiteral("kernel"), QStringLiteral("dracut")});
    QCOMPARE(model.scriptletTasks().size(), 1);
    QVERIFY(model.scriptletTasks().first().isRunning);
    QVERIFY(!model.scriptletTasks().first().isFinished);
    QCOMPARE(model.scriptletTasks().first().label, QStringLiteral("Initramfs erzeugen"));

    model.processEvent(lut::ScriptletFinished{QStringLiteral("kernel"), QStringLiteral("dracut"), 0});
    QVERIFY(!model.scriptletTasks().first().isRunning);
    QVERIFY(model.scriptletTasks().first().isFinished);
    QCOMPARE(model.scriptletTasks().first().exitCode, 0);
}

void ProgressModelTest::testDownloadThroughputAndEta() {
    lut::ProgressModel model;
    model.processEvent(lut::PhaseChanged{lut::Phase::Download, QString(), true});

    // 10 Samples füttern
    for (int i = 0; i < 10; ++i) {
        model.processEvent(lut::DownloadThroughput{10 * 1024 * 1024, i * 10 * 1024 * 1024, 200 * 1024 * 1024});
    }

    QCOMPARE(model.downloadSpeed(), 10 * 1024 * 1024);
    QVERIFY(!model.etaString().isEmpty());
}

void ProgressModelTest::testTransactionDoneResults() {
    // 1. Success
    {
        lut::ProgressModel model;
        model.processEvent(lut::TransactionDone{lut::Result::Success, QStringLiteral("Fertig"), false, {}, 1});
        QCOMPARE(model.currentPhase(), lut::Phase::Finished);
        QCOMPARE(model.totalProgress(), 1.0);
        QVERIFY(!model.isCancellable());
    }

    // 2. Failed
    {
        lut::ProgressModel model;
        model.processEvent(lut::TransactionDone{lut::Result::Failed, QStringLiteral("Fehler"), false, {}, 2});
        QCOMPARE(model.currentPhase(), lut::Phase::Failed);
        QVERIFY(!model.isCancellable());
    }

    // 3. Cancelled
    {
        lut::ProgressModel model;
        model.processEvent(lut::TransactionDone{lut::Result::Cancelled, QStringLiteral("Abgebrochen"), false, {}, 3});
        QCOMPARE(model.currentPhase(), lut::Phase::Cancelled);
        QVERIFY(!model.isCancellable());
    }
}

QTEST_MAIN(ProgressModelTest)
#include "progress_test.moc"

void ProgressModelTest::testResetClearsAllState() {
    lut::ProgressModel model;
    model.processEvent(lut::PhaseChanged{lut::Phase::Commit, QStringLiteral("Installation"), false});
    model.processEvent(lut::ItemStarted{QStringLiteral("pkg"), lut::PackageOp::Kind::Upgrade, 100});

    model.reset();
    QCOMPARE(model.currentPhase(), lut::Phase::Idle);
    QCOMPARE(model.totalProgress(), 0.0);
    QCOMPARE(model.phaseProgress(), 0.0);
    QVERIFY(model.currentItemName().isEmpty());
    QVERIFY(model.scriptletTasks().isEmpty());
}

void ProgressModelTest::testLogLineCapping() {
    lut::ProgressModel model;
    for (int i = 0; i < 1100; ++i) {
        model.processEvent(lut::LogLine{lut::LogLevel::Info, QStringLiteral("src"), QString::number(i)});
    }
    // Gedeckelt auf 1000 Zeilen
    QCOMPARE(model.recentLogs().size(), 1000);
    QCOMPARE(model.recentLogs().last().text, QStringLiteral("1099"));
}

void ProgressModelTest::testQuestionPropagation() {
    lut::ProgressModel model;
    bool questionReceived = false;
    lut::Question received;

    QObject::connect(&model, &lut::ProgressModel::questionReceived, [&](const lut::Question &q) {
        questionReceived = true;
        received = q;
    });

    lut::Question q{QStringLiteral("q42"), lut::QuestionKind::ConffilePrompt, {}};
    model.processEvent(q);

    QVERIFY(questionReceived);
    QCOMPARE(received.id, QStringLiteral("q42"));
}
