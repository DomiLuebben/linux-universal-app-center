#include <QTest>
#include "linux-update-tool/AurUpdates.h"

using namespace lut;

// Der AUR-Helfer meldet Fehler als {"error": "..."} auf demselben Kanal wie
// das Ergebnis. Wird das nicht unterschieden, liest sich ein Netzausfall als
// "keine Aktualisierungen verfügbar" – die gefährlichere der beiden Lügen.
class AurUpdatesTest : public QObject {
    Q_OBJECT

private slots:
    void testParsesEntries();
    void testEmptyListIsNotAnError();
    void testHelperErrorIsReported();
    void testBrokenJsonIsReported();
    void testEntriesWithoutNameAreDropped();
};

void AurUpdatesTest::testParsesEntries() {
    const QByteArray json = R"([
        {"name": "nordvpn-gui", "installed": "5.3.0-1", "available": "5.4.0-2"},
        {"name": "antigravity", "installed": "2.15.0-1", "available": "2.16.0-1"}
    ])";
    QString error;
    const auto entries = AurUpdates::parseCheckOutput(json, &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).name, QStringLiteral("nordvpn-gui"));
    QCOMPARE(entries.at(0).installed, QStringLiteral("5.3.0-1"));
    QCOMPARE(entries.at(1).available, QStringLiteral("2.16.0-1"));
}

void AurUpdatesTest::testEmptyListIsNotAnError() {
    QString error;
    const auto entries = AurUpdates::parseCheckOutput("[]", &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(entries.isEmpty());
}

void AurUpdatesTest::testHelperErrorIsReported() {
    QString error;
    const auto entries = AurUpdates::parseCheckOutput(
        R"({"error": "AUR-Abfrage fehlgeschlagen: kein Netz"})", &error);
    QVERIFY(entries.isEmpty());
    QVERIFY2(!error.isEmpty(), "Ein Helferfehler darf nicht als leere Liste durchgehen.");
    QVERIFY(error.contains(QStringLiteral("kein Netz")));
}

void AurUpdatesTest::testBrokenJsonIsReported() {
    QString error;
    const auto entries = AurUpdates::parseCheckOutput("das ist kein JSON", &error);
    QVERIFY(entries.isEmpty());
    QVERIFY2(!error.isEmpty(), "Unlesbare Ausgabe darf nicht als leere Liste durchgehen.");
}

void AurUpdatesTest::testEntriesWithoutNameAreDropped() {
    QString error;
    const auto entries = AurUpdates::parseCheckOutput(
        R"([{"installed": "1-1", "available": "2-1"}, {"name": "gut", "installed": "1-1", "available": "2-1"}])",
        &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.at(0).name, QStringLiteral("gut"));
}

QTEST_MAIN(AurUpdatesTest)
#include "aur_updates_test.moc"
