#include <QTest>
#include "liblut/protocol/events.h"

class EventsTest : public QObject {
    Q_OBJECT

private slots:
    void testPhaseEnums();
    void testCancellableMatrix();
    void testPackageOpSerialization();
    void testKernelDetection();
    void testEventRoundtrips();
    void testInvalidJsonReturnsNullopt();
    void testResultEnums();
    void testLogLevelEnums();
    void testQuestionKindEnums();
    void testPackageOpKindEnums();
};

void EventsTest::testPhaseEnums() {
    const QList<lut::Phase> phases = {
        lut::Phase::Idle,
        lut::Phase::RefreshMetadata,
        lut::Phase::Resolve,
        lut::Phase::Download,
        lut::Phase::Verify,
        lut::Phase::TestTransaction,
        lut::Phase::Commit,
        lut::Phase::PostTransaction,
        lut::Phase::Cleanup,
        lut::Phase::Finished,
        lut::Phase::Failed,
        lut::Phase::Cancelled
    };

    for (lut::Phase p : phases) {
        QString s = lut::phaseToString(p);
        lut::Phase parsed = lut::phaseFromString(s);
        QCOMPARE(parsed, p);
    }
}

void EventsTest::testCancellableMatrix() {
    // Abbrechbar:
    QVERIFY(lut::isPhaseCancellable(lut::Phase::RefreshMetadata));
    QVERIFY(lut::isPhaseCancellable(lut::Phase::Resolve));
    QVERIFY(lut::isPhaseCancellable(lut::Phase::Download));
    QVERIFY(lut::isPhaseCancellable(lut::Phase::Verify));
    QVERIFY(lut::isPhaseCancellable(lut::Phase::TestTransaction));

    // Ausdrücklich NICHT abbrechbar:
    QVERIFY(!lut::isPhaseCancellable(lut::Phase::Idle));
    QVERIFY(!lut::isPhaseCancellable(lut::Phase::Commit));
    QVERIFY(!lut::isPhaseCancellable(lut::Phase::PostTransaction));
    QVERIFY(!lut::isPhaseCancellable(lut::Phase::Cleanup));
    QVERIFY(!lut::isPhaseCancellable(lut::Phase::Finished));
    QVERIFY(!lut::isPhaseCancellable(lut::Phase::Failed));
    QVERIFY(!lut::isPhaseCancellable(lut::Phase::Cancelled));
}

void EventsTest::testPackageOpSerialization() {
    lut::PackageOp op;
    op.id = QStringLiteral("kernel-core-6.17.4-200.fc44.x86_64");
    op.name = QStringLiteral("kernel-core");
    op.version = QStringLiteral("6.17.3-200.fc44");
    op.newVersion = QStringLiteral("6.17.4-200.fc44");
    op.arch = QStringLiteral("x86_64");
    op.repo = QStringLiteral("updates");
    op.summary = QStringLiteral("The Linux kernel core");
    op.kind = lut::PackageOp::Kind::Upgrade;
    op.downloadSize = 35 * 1024 * 1024;
    op.installedSize = 85 * 1024 * 1024;
    op.isSecurity = true;
    op.isKernel = true;
    op.userRequested = false;

    QJsonObject json = lut::serializePackageOp(op);
    lut::PackageOp deserialized = lut::deserializePackageOp(json);

    QCOMPARE(deserialized, op);
}

void EventsTest::testKernelDetection() {
    QVERIFY(lut::PackageOp::detectIsKernel(QStringLiteral("kernel-core")));
    QVERIFY(lut::PackageOp::detectIsKernel(QStringLiteral("linux-zen")));
    QVERIFY(lut::PackageOp::detectIsKernel(QStringLiteral("linux-image-6.1.0-generic")));
    QVERIFY(lut::PackageOp::detectIsKernel(QStringLiteral("kmod-nvidia")));
    QVERIFY(!lut::PackageOp::detectIsKernel(QStringLiteral("firefox")));
    QVERIFY(!lut::PackageOp::detectIsKernel(QStringLiteral("libc6")));
}

void EventsTest::testEventRoundtrips() {
    // 1. PhaseChanged
    {
        lut::PhaseChanged original{lut::Phase::Download, QStringLiteral("Pakete werden heruntergeladen"), true};
        lut::Event ev = original;
        QJsonObject json = lut::serializeEvent(ev);
        auto res = lut::deserializeEvent(json);
        QVERIFY(res.has_value());
        QVERIFY(std::holds_alternative<lut::PhaseChanged>(*res));
        QCOMPARE(std::get<lut::PhaseChanged>(*res), original);
    }

    // 2. PlanReady
    {
        lut::PlanReady original;
        lut::PackageOp op;
        op.id = QStringLiteral("bash-5.2");
        op.name = QStringLiteral("bash");
        original.ops.append(op);
        original.downloadBytes = 2048;
        original.installedSizeDelta = 1024;
        original.warnings = {QStringLiteral("Neustart empfohlen")};

        lut::Event ev = original;
        QJsonObject json = lut::serializeEvent(ev);
        auto res = lut::deserializeEvent(json);
        QVERIFY(res.has_value());
        QVERIFY(std::holds_alternative<lut::PlanReady>(*res));
        QCOMPARE(std::get<lut::PlanReady>(*res), original);
    }

    // 3. ItemStarted
    {
        lut::ItemStarted original{QStringLiteral("gcc"), lut::PackageOp::Kind::Upgrade, 50000000};
        lut::Event ev = original;
        auto res = lut::deserializeEvent(lut::serializeEvent(ev));
        QVERIFY(res.has_value() && std::holds_alternative<lut::ItemStarted>(*res));
        QCOMPARE(std::get<lut::ItemStarted>(*res), original);
    }

    // 4. ItemProgress
    {
        lut::ItemProgress original{QStringLiteral("gcc"), 25000000, 50000000};
        lut::Event ev = original;
        auto res = lut::deserializeEvent(lut::serializeEvent(ev));
        QVERIFY(res.has_value() && std::holds_alternative<lut::ItemProgress>(*res));
        QCOMPARE(std::get<lut::ItemProgress>(*res), original);
    }

    // 5. ItemFinished
    {
        lut::ItemFinished original{QStringLiteral("gcc"), true, QString()};
        lut::Event ev = original;
        auto res = lut::deserializeEvent(lut::serializeEvent(ev));
        QVERIFY(res.has_value() && std::holds_alternative<lut::ItemFinished>(*res));
        QCOMPARE(std::get<lut::ItemFinished>(*res), original);
    }

    // 6. DownloadThroughput
    {
        lut::DownloadThroughput original{1024 * 1024, 5 * 1024 * 1024, 20 * 1024 * 1024};
        lut::Event ev = original;
        auto res = lut::deserializeEvent(lut::serializeEvent(ev));
        QVERIFY(res.has_value() && std::holds_alternative<lut::DownloadThroughput>(*res));
        QCOMPARE(std::get<lut::DownloadThroughput>(*res), original);
    }

    // 7. ScriptletStarted & Finished
    {
        lut::ScriptletStarted origStart{QStringLiteral("kernel-core"), QStringLiteral("dracut")};
        lut::Event ev1 = origStart;
        auto res1 = lut::deserializeEvent(lut::serializeEvent(ev1));
        QVERIFY(res1.has_value() && std::holds_alternative<lut::ScriptletStarted>(*res1));
        QCOMPARE(std::get<lut::ScriptletStarted>(*res1), origStart);

        lut::ScriptletFinished origFin{QStringLiteral("kernel-core"), QStringLiteral("dracut"), 0};
        lut::Event ev2 = origFin;
        auto res2 = lut::deserializeEvent(lut::serializeEvent(ev2));
        QVERIFY(res2.has_value() && std::holds_alternative<lut::ScriptletFinished>(*res2));
        QCOMPARE(std::get<lut::ScriptletFinished>(*res2), origFin);
    }

    // 8. LogLine
    {
        lut::LogLine original{lut::LogLevel::Warning, QStringLiteral("dpkg"), QStringLiteral("Conffile altered")};
        lut::Event ev = original;
        auto res = lut::deserializeEvent(lut::serializeEvent(ev));
        QVERIFY(res.has_value() && std::holds_alternative<lut::LogLine>(*res));
        QCOMPARE(std::get<lut::LogLine>(*res), original);
    }

    // 9. Question
    {
        QJsonObject payload;
        payload[QStringLiteral("keyId")] = QStringLiteral("4F368D5D");
        lut::Question original{QStringLiteral("q1"), lut::QuestionKind::GpgKeyImport, payload};
        lut::Event ev = original;
        auto res = lut::deserializeEvent(lut::serializeEvent(ev));
        QVERIFY(res.has_value() && std::holds_alternative<lut::Question>(*res));
        QCOMPARE(std::get<lut::Question>(*res), original);
    }

    // 10. TransactionDone
    {
        lut::TransactionDone original{
            lut::Result::Success,
            QStringLiteral("3 Pakete aktualisiert"),
            true,
            {QStringLiteral("sshd.service")},
            42
        };
        lut::Event ev = original;
        auto res = lut::deserializeEvent(lut::serializeEvent(ev));
        QVERIFY(res.has_value() && std::holds_alternative<lut::TransactionDone>(*res));
        QCOMPARE(std::get<lut::TransactionDone>(*res), original);
    }
}

void EventsTest::testInvalidJsonReturnsNullopt() {
    QJsonObject empty;
    QVERIFY(!lut::deserializeEvent(empty).has_value());

    QJsonObject wrongVersion;
    wrongVersion[QStringLiteral("v")] = 2;
    wrongVersion[QStringLiteral("type")] = QStringLiteral("PhaseChanged");
    QVERIFY(!lut::deserializeEvent(wrongVersion).has_value());
}

QTEST_MAIN(EventsTest)
#include "events_test.moc"

void EventsTest::testResultEnums() {
    const QList<lut::Result> list = {
        lut::Result::Success,
        lut::Result::SuccessWithWarnings,
        lut::Result::Failed,
        lut::Result::Cancelled
    };
    for (auto r : list) {
        QCOMPARE(lut::resultFromString(lut::resultToString(r)), r);
    }
}

void EventsTest::testLogLevelEnums() {
    const QList<lut::LogLevel> list = {
        lut::LogLevel::Debug,
        lut::LogLevel::Info,
        lut::LogLevel::Warning,
        lut::LogLevel::Error
    };
    for (auto l : list) {
        QCOMPARE(lut::logLevelFromString(lut::logLevelToString(l)), l);
    }
}

void EventsTest::testQuestionKindEnums() {
    const QList<lut::QuestionKind> list = {
        lut::QuestionKind::GpgKeyImport,
        lut::QuestionKind::ConffilePrompt,
        lut::QuestionKind::MediaChange,
        lut::QuestionKind::UntrustedPackage,
        lut::QuestionKind::FileConflict
    };
    for (auto q : list) {
        QCOMPARE(lut::questionKindFromString(lut::questionKindToString(q)), q);
    }
}

void EventsTest::testPackageOpKindEnums() {
    const QList<lut::PackageOp::Kind> list = {
        lut::PackageOp::Kind::Install,
        lut::PackageOp::Kind::Upgrade,
        lut::PackageOp::Kind::Downgrade,
        lut::PackageOp::Kind::Remove,
        lut::PackageOp::Kind::Reinstall,
        lut::PackageOp::Kind::Obsolete
    };
    for (auto k : list) {
        QCOMPARE(lut::PackageOp::kindFromString(lut::PackageOp::kindToString(k)), k);
    }
}
