#include <QTest>
#include "liblut/backend/apt/StatusFdParser.h"

class StatusFdTest : public QObject {
    Q_OBJECT

private slots:
    void testParseDlStatus();
    void testParsePmStatus();
    void testParsePmStatusTriggers();
    void testParsePmConffile();
    void testParsePmError();
    void testInvalidLines();
};

void StatusFdTest::testParseDlStatus() {
    QString line = QStringLiteral("dlstatus:curl:45.5:Downloading curl 8.10.0-1");
    auto item = lut::StatusFdParser::parseLine(line);

    QVERIFY(item.has_value());
    QCOMPARE(item->type, lut::StatusFdItem::Type::DlStatus);
    QCOMPARE(item->target, QStringLiteral("curl"));
    QCOMPARE(item->percent, 45.5);
    QCOMPARE(item->message, QStringLiteral("Downloading curl 8.10.0-1"));

    auto ev = lut::StatusFdParser::toEvent(*item);
    QVERIFY(ev.has_value());
    QVERIFY(std::holds_alternative<lut::DownloadThroughput>(*ev));
}

void StatusFdTest::testParsePmStatus() {
    QString line = QStringLiteral("pmstatus:curl:75.0:Unpacking curl");
    auto item = lut::StatusFdParser::parseLine(line);

    QVERIFY(item.has_value());
    QCOMPARE(item->type, lut::StatusFdItem::Type::PmStatus);
    QCOMPARE(item->target, QStringLiteral("curl"));
    QCOMPARE(item->percent, 75.0);
    QVERIFY(!item->isTrigger);

    auto ev = lut::StatusFdParser::toEvent(*item);
    QVERIFY(ev.has_value());
    QVERIFY(std::holds_alternative<lut::ItemProgress>(*ev));
    QCOMPARE(std::get<lut::ItemProgress>(*ev).done, 75);
}

void StatusFdTest::testParsePmStatusTriggers() {
    QString line = QStringLiteral("pmstatus:systemd:90.0:Processing triggers for initramfs-tools");
    auto item = lut::StatusFdParser::parseLine(line);

    QVERIFY(item.has_value());
    QCOMPARE(item->type, lut::StatusFdItem::Type::PmStatus);
    QVERIFY(item->isTrigger);

    auto ev = lut::StatusFdParser::toEvent(*item);
    QVERIFY(ev.has_value());
    QVERIFY(std::holds_alternative<lut::ScriptletStarted>(*ev));
    QCOMPARE(std::get<lut::ScriptletStarted>(*ev).scriptletName, QStringLiteral("Processing triggers for initramfs-tools"));
}

void StatusFdTest::testParsePmConffile() {
    QString line = QStringLiteral("pmconffile:/etc/ssh/sshd_config:'/etc/ssh/sshd_config' '/etc/ssh/sshd_config.dpkg-new'");
    auto item = lut::StatusFdParser::parseLine(line);

    QVERIFY(item.has_value());
    QCOMPARE(item->type, lut::StatusFdItem::Type::PmConffile);
    QCOMPARE(item->target, QStringLiteral("/etc/ssh/sshd_config"));
    QCOMPARE(item->oldPath, QStringLiteral("/etc/ssh/sshd_config"));
    QCOMPARE(item->newPath, QStringLiteral("/etc/ssh/sshd_config.dpkg-new"));

    auto ev = lut::StatusFdParser::toEvent(*item);
    QVERIFY(ev.has_value());
    QVERIFY(std::holds_alternative<lut::Question>(*ev));
    auto q = std::get<lut::Question>(*ev);
    QCOMPARE(q.kind, lut::QuestionKind::ConffilePrompt);
    QCOMPARE(q.payload.value("file").toString(), QStringLiteral("/etc/ssh/sshd_config"));
}

void StatusFdTest::testParsePmError() {
    QString line = QStringLiteral("pmerror:dpkg:10.0:Sub-process /usr/bin/dpkg returned an error code (1)");
    auto item = lut::StatusFdParser::parseLine(line);

    QVERIFY(item.has_value());
    QCOMPARE(item->type, lut::StatusFdItem::Type::PmError);
    QCOMPARE(item->target, QStringLiteral("dpkg"));

    auto ev = lut::StatusFdParser::toEvent(*item);
    QVERIFY(ev.has_value());
    QVERIFY(std::holds_alternative<lut::LogLine>(*ev));
    QCOMPARE(std::get<lut::LogLine>(*ev).level, lut::LogLevel::Error);
}

void StatusFdTest::testInvalidLines() {
    QVERIFY(!lut::StatusFdParser::parseLine(QString()).has_value());
    QVERIFY(!lut::StatusFdParser::parseLine(QStringLiteral("invalid:short")).has_value());
    QVERIFY(!lut::StatusFdParser::parseLine(QStringLiteral("random console log line")).has_value());
}

QTEST_MAIN(StatusFdTest)
#include "statusfd_test.moc"
