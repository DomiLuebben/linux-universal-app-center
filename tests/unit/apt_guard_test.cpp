#include <QtTest>
#include <QJsonObject>
#include "liblut/backend/apt/PlanGuard.h"
using namespace lut;
class AptGuardTest : public QObject {
    Q_OBJECT
private slots:
    void bindsActualNativeOperations() {
        const QString hash(64, QLatin1Char('a'));
        const QJsonObject install{{"name", "demo"}, {"arch", "amd64"}, {"oldArch", "amd64"},
            {"oldVersion", "1"}, {"newVersion", "2"}, {"remove", false}, {"sha256", hash}};
        const QJsonObject remove{{"name", "old"}, {"arch", "i386"}, {"oldArch", "i386"},
            {"oldVersion", "3"}, {"newVersion", ""}, {"remove", true}};
        const QByteArray protocol = "VERSION 3\nAPT::Architecture=amd64\n\ndemo 1 amd64 none < 2 amd64 none /cache/demo.deb\ndemo 1 amd64 none < 2 amd64 none **CONFIGURE**\nold 3 i386 none > - - none **REMOVE**\n";
        const auto digest = [hash](const QString &path) { return path == QLatin1String("/cache/demo.deb") ? hash : QString(); };
        QString error;
        QVERIFY2(verifyAptHook(protocol, {install, remove}, digest, &error), qPrintable(error));
        for (const auto &changed : {QByteArray(protocol).replace("demo 1", "demo 0"),
                QByteArray(protocol).replace("2 amd64", "2 i386"),
                QByteArray(protocol).replace("/cache/demo.deb", "/cache/replaced.deb"),
                protocol + "extra - - none < 1 amd64 none /cache/extra.deb\n",
                QByteArray(protocol).replace("VERSION 3", "VERSION 2"),
                QByteArray(protocol).replace("old 3 i386 none > - - none **REMOVE**\n", "")}) {
            QVERIFY(!verifyAptHook(changed, {install, remove}, digest, &error));
            QVERIFY(!error.isEmpty());
        }
        QVERIFY(verifyAptHook("VERSION 3\n\nold 3 i386 none > - - none **REMOVE**\n", {remove}, digest, &error));
        QVERIFY(!verifyAptHook("VERSION 3\n\ndemo 1 amd64 none < 2 amd64 none **CONFIGURE**\n", {install}, digest, &error));
    }
};
QTEST_GUILESS_MAIN(AptGuardTest)
#include "apt_guard_test.moc"
