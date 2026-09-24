#include <QTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QFile>
#include <QColor>
#include "linux-app-store/theme/SystemPalette.h"
#include "linux-app-store/theme/Contrast.h"

// Die Oberfläche erschien grau statt im Plasma-Schema, weil QSettings im
// INI-Format unquotierte Kommawerte ("5,14,21") als QStringList liefert.
// toString() ergibt darauf einen LEEREN String, und jede Farbe fiel
// stillschweigend auf den einkompilierten Breeze-Vorgabewert zurück.
class KdeGlobalsTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_path;

private slots:
    void initTestCase();
    void testCommaValueIsNotEmpty();
    void testPlainValueStillWorks();
    void testAccentColorLivesAtRoot();
    void testMissingKeyStaysEmpty();
    void testElevateKeepsHueOnDarkScheme();
};

void KdeGlobalsTest::initTestCase() {
    QVERIFY(m_dir.isValid());
    m_path = m_dir.path() + QStringLiteral("/kdeglobals");
    QFile f(m_path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write(
        "[Colors:Window]\n"
        "BackgroundNormal=5,14,21\n"
        "ForegroundNormal=215,241,248\n"
        "\n"
        "[General]\n"
        "AccentColor=61,174,233\n"
        "ColorSchemeHash=215c34bda75ba8108f2263e97ab41675939a17bc\n"
        "\n"
        "[KDE]\n"
        "AnimationDurationFactor=1\n");
    f.close();
}

void KdeGlobalsTest::testCommaValueIsNotEmpty() {
    QSettings s(m_path, QSettings::IniFormat);
    QCOMPARE(s.status(), QSettings::NoError);

    // Gegenprobe: genau hier lag der Fehler.
    QVERIFY2(s.value(QStringLiteral("Colors:Window/BackgroundNormal")).toString().isEmpty(),
             "Erwartung: toString() ist bei Kommawerten leer - sonst ist die Annahme veraltet.");

    const QString raw = lut::SystemPalette::rawValue(s, QStringLiteral("Colors:Window/BackgroundNormal"));
    QCOMPARE(raw, QStringLiteral("5,14,21"));
}

void KdeGlobalsTest::testPlainValueStillWorks() {
    QSettings s(m_path, QSettings::IniFormat);
    QCOMPARE(lut::SystemPalette::rawValue(s, QStringLiteral("ColorSchemeHash")),
             QStringLiteral("215c34bda75ba8108f2263e97ab41675939a17bc"));
}

// Zweite Falle derselben Datei: QSettings bildet den INI-Abschnitt [General]
// auf die Wurzelebene ab. Wer unter "General/..." sucht, findet nie etwas -
// die in Plasma eingestellte Akzentfarbe wurde deshalb ignoriert.
void KdeGlobalsTest::testAccentColorLivesAtRoot() {
    QSettings s(m_path, QSettings::IniFormat);
    QVERIFY2(!s.value(QStringLiteral("General/AccentColor")).isValid(),
             "Erwartung: unter General/ liegt nichts - sonst ist die Annahme veraltet.");
    QCOMPARE(lut::SystemPalette::rawValue(s, QStringLiteral("AccentColor")),
             QStringLiteral("61,174,233"));
}

void KdeGlobalsTest::testMissingKeyStaysEmpty() {
    QSettings s(m_path, QSettings::IniFormat);
    QVERIFY(lut::SystemPalette::rawValue(s, QStringLiteral("Colors:Window/GibtEsNicht")).isEmpty());
}

// Zweiter Teil des Graus: elevate() mischte gegen reines Weiss und entsättigte
// sehr dunkle Schemata vollständig (#050e15 wurde zum grauen #393b3d).
void KdeGlobalsTest::testElevateKeepsHueOnDarkScheme() {
    const QColor base(5, 14, 21);
    const QColor raised = lut::elevate(base, 1);

    QVERIFY(lut::relativeLuminance(raised) > lut::relativeLuminance(base));
    // Farbton bleibt erhalten ...
    QVERIFY(qAbs(raised.hslHue() - base.hslHue()) <= 2);
    // ... und die Sättigung bricht nicht weg. Der alte Mischweg landete bei
    // etwa 3 von 255; alles unter der Hälfte der Ausgangssättigung ist grau.
    QVERIFY2(raised.hslSaturation() >= base.hslSaturation() / 2,
             qPrintable(QStringLiteral("Sättigung %1 gegenüber %2")
                            .arg(raised.hslSaturation()).arg(base.hslSaturation())));
}

QTEST_MAIN(KdeGlobalsTest)
#include "kdeglobals_test.moc"
