#include <QTest>
#include "linux-app-store/AurUpdates.h"

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
    void testGroupsSplitPackagesByBase();
    void testPackageNameFromFile();
    void testOnlyInstalledSplitPackagesAreInstalled();
    void testBatchFractionOrder();
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

void AurUpdatesTest::testGroupsSplitPackagesByBase() {
    // Zwei installierte Pakete aus demselben pkgbase werden EINMAL gebaut.
    const QList<AurUpdates::Entry> outdated = {
        {QStringLiteral("spotify"), QStringLiteral("1.2-1"), QStringLiteral("1.3-1")},
        {QStringLiteral("foo-cli"), QStringLiteral("2-1"), QStringLiteral("3-1")},
        {QStringLiteral("foo-gui"), QStringLiteral("2-1"), QStringLiteral("3-1")},
    };
    QHash<QString, AurUpdates::AurPackage> remote;
    remote.insert(QStringLiteral("spotify"), {QStringLiteral("1.3-1"), QStringLiteral("spotify")});
    remote.insert(QStringLiteral("foo-cli"), {QStringLiteral("3-1"), QStringLiteral("foo")});
    remote.insert(QStringLiteral("foo-gui"), {QStringLiteral("3-1"), QStringLiteral("foo")});

    const auto batch = AurUpdates::groupByBase(
        {QStringLiteral("spotify"), QStringLiteral("foo-cli"), QStringLiteral("foo-gui"), QStringLiteral("aktuell")},
        outdated, remote);
    QCOMPARE(batch.size(), 2);
    QCOMPARE(batch[0].base, QStringLiteral("spotify"));
    QCOMPARE(batch[1].base, QStringLiteral("foo"));
    QCOMPARE(batch[1].names, (QStringList{QStringLiteral("foo-cli"), QStringLiteral("foo-gui")}));
    QCOMPARE(batch[0].fromVersion, QStringLiteral("1.2-1"));
    QCOMPARE(batch[0].toVersion, QStringLiteral("1.3-1"));
}

void AurUpdatesTest::testPackageNameFromFile() {
    QCOMPARE(AurUpdates::packageNameFromFile(QStringLiteral("/tmp/aur/foo/foo-bar-baz-1.2.3-4-x86_64.pkg.tar.zst")),
             QStringLiteral("foo-bar-baz"));
    QCOMPARE(AurUpdates::packageNameFromFile(QStringLiteral("spotify-1:1.2.3-1-x86_64.pkg.tar.zst")),
             QStringLiteral("spotify"));
    QCOMPARE(AurUpdates::packageNameFromFile(QStringLiteral("foo-debug-1-1-x86_64.pkg.tar.zst")),
             QStringLiteral("foo-debug"));
    QVERIFY(AurUpdates::packageNameFromFile(QStringLiteral("kein-paket.txt")).isEmpty());
}

void AurUpdatesTest::testOnlyInstalledSplitPackagesAreInstalled() {
    const QStringList built = {
        QStringLiteral("/c/foo/foo-cli-3-1-x86_64.pkg.tar.zst"),
        QStringLiteral("/c/foo/foo-docs-3-1-any.pkg.tar.zst"),
        QStringLiteral("/c/foo/foo-debug-3-1-x86_64.pkg.tar.zst"),
    };
    // Nur was schon installiert ist, wird aktualisiert – nicht nebenbei "-docs" oder "-debug".
    const QStringList result = AurUpdates::filterInstallable(built, {QStringLiteral("foo-cli")});
    QCOMPARE(result, QStringList{QStringLiteral("/c/foo/foo-cli-3-1-x86_64.pkg.tar.zst")});
}

void AurUpdatesTest::testBatchFractionOrder() {
    using S = AurUpdates::Stage;
    QCOMPARE(AurUpdates::batchFraction(S::Fetch, 0, 4), 0.0);
    QVERIFY(AurUpdates::batchFraction(S::Fetch, 4, 4) <= AurUpdates::batchFraction(S::Review, 0, 4));
    QVERIFY(AurUpdates::batchFraction(S::Build, 0, 4) < AurUpdates::batchFraction(S::Build, 2, 4));
    QVERIFY(AurUpdates::batchFraction(S::Build, 4, 4) <= AurUpdates::batchFraction(S::Install, 0, 4));
    QVERIFY(AurUpdates::batchFraction(S::Install, 0, 4) < 1.0);
    // Kein Teilen durch null bei leerem Stapel
    QCOMPARE(AurUpdates::batchFraction(S::Build, 0, 0), 0.1);
}

QTEST_MAIN(AurUpdatesTest)
#include "aur_updates_test.moc"
