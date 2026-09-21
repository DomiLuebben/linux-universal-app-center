#include <QTest>
#include "linux-update-tool/AurUpdates.h"

using namespace lut;

// Die AUR-Schnittstelle meldet Fehler im selben JSON-Objekt wie das Ergebnis.
// Wird das nicht unterschieden, liest sich ein Netzausfall als "keine
// Aktualisierungen verfügbar" – die gefährlichere der beiden Lügen.
class AurUpdatesTest : public QObject {
    Q_OBJECT

private slots:
    void testParsesForeignPackages();
    void testIgnoresMalformedForeignLines();
    void testParsesAurInfo();
    void testAurErrorIsReported();
    void testBrokenJsonIsReported();
    void testEmptyResultIsNotAnError();
    void testCompareVersions();
    void testSelectsOnlyRealUpgrades();
};

void AurUpdatesTest::testParsesForeignPackages() {
    const auto packages = AurUpdates::parseForeignPackages(
        "nordvpn-gui 5.3.0-1\nclaude-desktop-bin 1.24012.9-2\n");
    QCOMPARE(packages.size(), 2);
    QCOMPARE(packages.value(QStringLiteral("nordvpn-gui")), QStringLiteral("5.3.0-1"));
    QCOMPARE(packages.value(QStringLiteral("claude-desktop-bin")), QStringLiteral("1.24012.9-2"));
}

void AurUpdatesTest::testIgnoresMalformedForeignLines() {
    const auto packages = AurUpdates::parseForeignPackages("nur-ein-wort\n\n  \ngut 1.0-1\n");
    QCOMPARE(packages.size(), 1);
    QCOMPARE(packages.value(QStringLiteral("gut")), QStringLiteral("1.0-1"));
}

void AurUpdatesTest::testParsesAurInfo() {
    const QByteArray json = R"({
        "resultcount": 2, "type": "multiinfo",
        "results": [
            {"Name": "nordvpn-gui", "Version": "5.4.0-2", "PackageBase": "nordvpn-gui"},
            {"Name": "teil-paket", "Version": "1.0-1", "PackageBase": "sammel-basis"}
        ]
    })";
    QString error;
    const auto info = AurUpdates::parseAurInfo(json, &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(info.size(), 2);
    QCOMPARE(info.value(QStringLiteral("nordvpn-gui")).version, QStringLiteral("5.4.0-2"));
    // Split-Pakete werden unter ihrer Basis geklont, nicht unter dem Paketnamen.
    QCOMPARE(info.value(QStringLiteral("teil-paket")).packageBase, QStringLiteral("sammel-basis"));
}

void AurUpdatesTest::testAurErrorIsReported() {
    QString error;
    const auto info = AurUpdates::parseAurInfo(
        R"({"type": "error", "error": "Too many package results."})", &error);
    QVERIFY(info.isEmpty());
    QVERIFY2(!error.isEmpty(), "Ein Dienstfehler darf nicht als leeres Ergebnis durchgehen.");
    QVERIFY(error.contains(QStringLiteral("Too many")));
}

void AurUpdatesTest::testBrokenJsonIsReported() {
    QString error;
    const auto info = AurUpdates::parseAurInfo("das ist kein JSON", &error);
    QVERIFY(info.isEmpty());
    QVERIFY2(!error.isEmpty(), "Unlesbare Antwort darf nicht als leeres Ergebnis durchgehen.");
}

void AurUpdatesTest::testEmptyResultIsNotAnError() {
    QString error;
    const auto info = AurUpdates::parseAurInfo(R"({"resultcount": 0, "results": []})", &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(info.isEmpty());
}

void AurUpdatesTest::testCompareVersions() {
    QVERIFY(AurUpdates::compareVersions(QStringLiteral("5.3.0-1"), QStringLiteral("5.4.0-2")) < 0);
    QVERIFY(AurUpdates::compareVersions(QStringLiteral("5.4.0-2"), QStringLiteral("5.3.0-1")) > 0);
    QCOMPARE(AurUpdates::compareVersions(QStringLiteral("1.0-1"), QStringLiteral("1.0-1")), 0);
    // pkgrel zählt mit, sonst bliebe ein Rebuild unbemerkt.
    QVERIFY(AurUpdates::compareVersions(QStringLiteral("1.0-1"), QStringLiteral("1.0-2")) < 0);
}

void AurUpdatesTest::testSelectsOnlyRealUpgrades() {
    const QMap<QString, QString> installed = {
        {QStringLiteral("veraltet"), QStringLiteral("1.0-1")},
        {QStringLiteral("aktuell"), QStringLiteral("2.0-1")},
        {QStringLiteral("lokal-neuer"), QStringLiteral("9.0-1")},
        {QStringLiteral("nicht-im-aur"), QStringLiteral("1.0-1")},
    };
    QHash<QString, AurUpdates::AurPackage> remote;
    remote.insert(QStringLiteral("veraltet"), {QStringLiteral("1.1-1"), QStringLiteral("veraltet")});
    remote.insert(QStringLiteral("aktuell"), {QStringLiteral("2.0-1"), QStringLiteral("aktuell")});
    remote.insert(QStringLiteral("lokal-neuer"), {QStringLiteral("8.0-1"), QStringLiteral("lokal-neuer")});

    const auto outdated = AurUpdates::selectOutdated(installed, remote);

    QCOMPARE(outdated.size(), 1);
    QCOMPARE(outdated.first().name, QStringLiteral("veraltet"));
    QCOMPARE(outdated.first().installed, QStringLiteral("1.0-1"));
    QCOMPARE(outdated.first().available, QStringLiteral("1.1-1"));
}

QTEST_MAIN(AurUpdatesTest)
#include "aur_updates_test.moc"
