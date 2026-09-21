#include <QTest>
#include <QSignalSpy>
#include "liblut/backend/replay/ReplayBackend.h"
#include "liblut/progress/ProgressModel.h"

class ReplayTest : public QObject {
    Q_OBJECT

private slots:
    void testReplayKernelUpdateHeadless();
    void testReplaySmallUpdateHeadless();
    void testReplayDownloadErrorHeadless();
    void testReplayGpgQuestionHeadless();
    void testReplayConffileQuestionHeadless();
    void testReplayCancelledHeadless();
};

void ReplayTest::testReplayKernelUpdateHeadless() {
    lut::ReplayBackend backend(QStringLiteral(PROJECT_DIR "/tests/fixtures/kernel-update.jsonl"));
    lut::ProgressModel model;

    QObject::connect(&backend, &lut::Backend::eventEmitted, &model, &lut::ProgressModel::processEvent);

    bool finished = false;
    lut::TransactionDone doneResult;

    QObject::connect(&model, &lut::ProgressModel::transactionCompleted, [&](const lut::TransactionDone &d) {
        finished = true;
        doneResult = d;
    });

    backend.runSynchronously();

    QVERIFY(finished);
    QCOMPARE(doneResult.result, lut::Result::Success);
    QVERIFY(doneResult.rebootRequired);
    QVERIFY(model.hasKernelUpdate());
    QCOMPARE(model.totalProgress(), 1.0);
    QCOMPARE(model.currentPhase(), lut::Phase::Finished);
}

void ReplayTest::testReplaySmallUpdateHeadless() {
    lut::ReplayBackend backend(QStringLiteral(PROJECT_DIR "/tests/fixtures/small-update.jsonl"));
    lut::ProgressModel model;

    QObject::connect(&backend, &lut::Backend::eventEmitted, &model, &lut::ProgressModel::processEvent);
    backend.runSynchronously();

    QCOMPARE(model.totalProgress(), 1.0);
    QCOMPARE(model.currentPhase(), lut::Phase::Finished);
    QVERIFY(!model.hasKernelUpdate());
}

void ReplayTest::testReplayDownloadErrorHeadless() {
    lut::ReplayBackend backend(QStringLiteral(PROJECT_DIR "/tests/fixtures/download-error.jsonl"));
    lut::ProgressModel model;

    QObject::connect(&backend, &lut::Backend::eventEmitted, &model, &lut::ProgressModel::processEvent);
    backend.runSynchronously();

    QCOMPARE(model.currentPhase(), lut::Phase::Failed);
}

void ReplayTest::testReplayGpgQuestionHeadless() {
    lut::ReplayBackend backend(QStringLiteral(PROJECT_DIR "/tests/fixtures/gpg-question.jsonl"));
    lut::ProgressModel model;

    bool sawQuestion = false;
    QObject::connect(&backend, &lut::Backend::eventEmitted, &model, &lut::ProgressModel::processEvent);
    QObject::connect(&model, &lut::ProgressModel::questionReceived, [&](const lut::Question &q) {
        sawQuestion = true;
        QCOMPARE(q.kind, lut::QuestionKind::GpgKeyImport);
        QVERIFY(q.payload.contains(QStringLiteral("fingerprint")));
    });

    backend.runSynchronously();
    QVERIFY(sawQuestion);
    QCOMPARE(model.totalProgress(), 1.0);
}

void ReplayTest::testReplayConffileQuestionHeadless() {
    lut::ReplayBackend backend(QStringLiteral(PROJECT_DIR "/tests/fixtures/conffile-question.jsonl"));
    lut::ProgressModel model;

    bool sawConffile = false;
    QObject::connect(&backend, &lut::Backend::eventEmitted, &model, &lut::ProgressModel::processEvent);
    QObject::connect(&model, &lut::ProgressModel::questionReceived, [&](const lut::Question &q) {
        sawConffile = true;
        QCOMPARE(q.kind, lut::QuestionKind::ConffilePrompt);
        QVERIFY(q.payload.contains(QStringLiteral("diff")));
    });

    backend.runSynchronously();
    QVERIFY(sawConffile);
    QCOMPARE(model.totalProgress(), 1.0);
}

void ReplayTest::testReplayCancelledHeadless() {
    lut::ReplayBackend backend(QStringLiteral(PROJECT_DIR "/tests/fixtures/cancelled-transaction.jsonl"));
    lut::ProgressModel model;

    QObject::connect(&backend, &lut::Backend::eventEmitted, &model, &lut::ProgressModel::processEvent);
    backend.runSynchronously();

    QCOMPARE(model.currentPhase(), lut::Phase::Cancelled);
}

QTEST_MAIN(ReplayTest)
#include "replay_test.moc"
