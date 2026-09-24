#include <QTest>
#include <QDBusObjectPath>
#include <QJsonDocument>
#include "linux-app-store/DaemonClient.h"
#include "liblut/protocol/events.h"

using namespace lut;

// Wächter gegen eine doppelte Protokollzeile: LogLine darf genau EINEN Weg ins
// LogModel haben (ProgressModel::logAdded). Hängt handleEvent() LogLine
// zusätzlich direkt an, steht jede Worker-Zeile zweimal im Protokoll – im
// Produktivbetrieb aufgefallen, von keinem anderen Test erfasst.
class DaemonClientLogTest : public QObject {
    Q_OBJECT

private:
    static QString eventJson(const Event &event) {
        QJsonObject obj = serializeEvent(event);
        obj[QStringLiteral("v")] = 1;
        return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }

    static void deliver(DaemonClient &client, const Event &event) {
        // Privater Slot, per Metaobjekt aufgerufen: so braucht der Test keinen
        // laufenden Daemon und keine D-Bus-Verbindung.
        const bool ok = QMetaObject::invokeMethod(
            &client, "onDbusTransactionEvent", Qt::DirectConnection,
            Q_ARG(QDBusObjectPath, QDBusObjectPath(QStringLiteral("/org/linuxupdatetool/Transaction/1"))),
            Q_ARG(QString, eventJson(event)));
        QVERIFY(ok);
    }

private slots:
    void testFailedCheckIsNotAnEmptyPlan();
    void testLogLineIsRecordedOnce();
    void testPhaseChangeIsRecordedOnce();
};

void DaemonClientLogTest::testFailedCheckIsNotAnEmptyPlan() {
    DaemonClient client;
    client.setPendingTransactionForTest(true, false);
    QVERIFY(!client.hasPlan());
    QVERIFY(!client.hasError());
    deliver(client, PlanReady{});
    QVERIFY(client.hasPlan());
    QVERIFY(!client.hasError());
    deliver(client, TransactionDone{Result::Failed, QStringLiteral("Mirror unavailable"), false, {}, 0});
    QVERIFY(!client.hasPlan());
    QVERIFY(client.hasError());
    QCOMPARE(client.statusMessage(), QStringLiteral("Mirror unavailable"));
    deliver(client, PlanReady{});
    QVERIFY(client.hasPlan());
    QVERIFY(!client.hasError());
}

void DaemonClientLogTest::testLogLineIsRecordedOnce() {
    DaemonClient client;
    client.setPendingTransactionForTest(true, false);
    QCOMPARE(client.logModel()->rowCount(), 0);

    deliver(client, LogLine{LogLevel::Info, QStringLiteral("alpm-worker"),
                            QStringLiteral("Signaturprüfung aktiv (SigLevel=3073).")});
    QCOMPARE(client.logModel()->rowCount(), 1);

    deliver(client, LogLine{LogLevel::Warning, QStringLiteral("alpm"),
                            QStringLiteral("Public keyring not found")});
    QCOMPARE(client.logModel()->rowCount(), 2);
}

void DaemonClientLogTest::testPhaseChangeIsRecordedOnce() {
    DaemonClient client;
    client.setPendingTransactionForTest(true, false);
    deliver(client, PhaseChanged{Phase::Download, QStringLiteral("Pakete holen"), true});
    QCOMPARE(client.logModel()->rowCount(), 1);
}

QTEST_MAIN(DaemonClientLogTest)
#include "daemonclient_log_test.moc"
