#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include "liblut/backend/dnf5/Dnf5Backend.h"
#include "liblut/progress/ProgressModel.h"
using namespace lut;
class Dnf5BackendTest : public QObject {
    Q_OBJECT
private slots:
    void historySchema() {
        QString error;
        auto entries = Dnf5Backend::parseHistory(R"([{"id":2,"start_time":1700000000,"command_line":"dnf5 upgrade","status":"Ok","altered_count":3},{"id":9,"start_time":1700000100,"command_line":"dnf5 remove tree","status":"Error","altered_count":1}])", 1, &error);
        QVERIFY(error.isEmpty()); QCOMPARE(entries.size(), 1); QCOMPARE(entries[0].id, 9);
        QVERIFY(!entries[0].canUndo);
        QVERIFY(Dnf5Backend::parseHistory("{}", 20, &error).isEmpty()); QVERIFY(!error.isEmpty());
    }
    void errorsCompleteOnce() {
        Dnf5Backend backend(nullptr, QStringLiteral("/nonexistent/lut-dnf5"));
        int done = 0; Result result = Result::Success;
        connect(&backend, &Backend::eventEmitted, [&](const Event &event) {
            if (const auto *value = std::get_if<TransactionDone>(&event)) { ++done; result = value->result; }
        });
        backend.planInstall({QStringLiteral("tree")});
        QTRY_COMPARE(done, 1); QCOMPARE(result, Result::Failed);
        QTest::qWait(20); QCOMPARE(done, 1);
    }
    void invalidArgumentsCannotRun() {
        Dnf5Backend backend(nullptr, QStringLiteral("/nonexistent/lut-dnf5"));
        int done = 0;
        connect(&backend, &Backend::eventEmitted, [&](const Event &event) { if (std::holds_alternative<TransactionDone>(event)) ++done; });
        backend.planInstall({QStringLiteral("--installroot=/")}); QCOMPARE(done, 1);
        backend.planCommand(QStringLiteral("history undo"), {QStringLiteral("--help")}); QCOMPARE(done, 2);
        backend.planCommand(QStringLiteral("swap"), {QStringLiteral("one")}); QCOMPARE(done, 3);
        backend.commit(); QCOMPARE(done, 4);
    }
    void storedPlanPreservesFiltersAndCommitsOnlyOnce() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile script(dir.filePath(QStringLiteral("dnf5"))); QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(R"SH(#!/bin/sh
printf '%s\n' "$*" >> "$(dirname "$0")/calls"
case "$*" in
  *--store=*)
    for arg in "$@"; do case "$arg" in --store=*) dest=${arg#--store=};; esac; done
    mkdir -p "$dest/packages"
    printf 'test rpm' > "$dest/packages/tree.rpm"
    printf '%s' '{"version":"1.0","rpms":[{"nevra":"tree-2.2.1-4.fc44.x86_64","action":"Install","reason":"User","repo_id":"@stored_transaction(fedora)","package_path":"./packages/tree.rpm"}]}' > "$dest/transaction.json"
    ;;
  *repoquery*--installed*) ;;
  *repoquery*) printf 'tree\0372.2.1-4.fc44\037x86_64\037122910\037\0370\037Tree viewer\036' ;;
  *replay*) printf 'Transaction complete\n' ;;
esac
)SH");
        script.close(); QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        Dnf5Backend backend(nullptr, script.fileName());
        int plans = 0, done = 0; PlanReady plan; ProgressModel progress;
        connect(&backend, &Backend::eventEmitted, [&](const Event &event) {
            progress.processEvent(event);
            if (const auto *value = std::get_if<PlanReady>(&event)) { plan = *value; ++plans; }
            if (std::holds_alternative<TransactionDone>(event)) ++done;
        });
        UpgradeOptions options; options.includeSecurityOnly = true; options.excludeKernel = true; options.allowDowngrade = true;
        backend.planUpgradeAll(options);
        QTRY_COMPARE(plans, 1); QCOMPARE(done, 0); QCOMPARE(plan.ops.size(), 1);
        QCOMPARE(plan.ops[0].installedSize, 122910); QCOMPARE(plan.installedSizeDelta, 122910);
        QCOMPARE(plan.downloadBytes, 0); QCOMPARE(plan.ops[0].repo, QStringLiteral("fedora"));
        backend.commit(); QVERIFY(progress.isIndeterminate()); QVERIFY(!progress.isCancellable());
        backend.cancel(); // Must not terminate RPM after commit.
        QTRY_COMPARE(done, 1);
        backend.commit(); QCOMPARE(done, 2); // expired plan is a failure, not another execution
        QFile calls(dir.filePath(QStringLiteral("calls"))); QVERIFY(calls.open(QIODevice::ReadOnly));
        const auto log = calls.readAll();
        QVERIFY(log.contains("--security")); QVERIFY(log.contains("--exclude=kernel*")); QVERIFY(log.contains("--allow-downgrade"));
        QCOMPARE(log.count("replay"), 1);
        QVERIFY(log.contains("--setopt=localpkg_gpgcheck=1"));
        QVERIFY(log.contains("--setopt=*.pkg_gpgcheck=1"));
    }
    void indeterminateProtocolRoundTrip() {
        PhaseChanged phase{Phase::Commit, QStringLiteral("DNF5"), false, true};
        auto event = deserializeEvent(serializeEvent(phase)); QVERIFY(event);
        QVERIFY(std::get<PhaseChanged>(*event).indeterminate);
        PackageOp op; op.installedSizeDelta = -4096;
        QCOMPARE(deserializePackageOp(serializePackageOp(op)).installedSizeDelta.value(), -4096);
    }
};
QTEST_GUILESS_MAIN(Dnf5BackendTest)
#include "dnf5_backend_test.moc"
