#include <QTest>
#include "linux-update-tool/theme/Contrast.h"

class ThemeContrastTest : public QObject {
    Q_OBJECT

private slots:
    void testContrastRatioBasics();
    void testPickOn();
    void testEnsureContrast();
    void testElevate();
    void testBreezeDarkAndLightProfiles();
};

void ThemeContrastTest::testContrastRatioBasics() {
    QCOMPARE(lut::contrastRatio(Qt::black, Qt::white), 21.0);
    QCOMPARE(lut::contrastRatio(Qt::black, Qt::black), 1.0);
    QCOMPARE(lut::contrastRatio(Qt::white, Qt::white), 1.0);
}

void ThemeContrastTest::testPickOn() {
    // Breeze Akzent-Blau #3daee9
    QColor breezeBlue(QStringLiteral("#3daee9"));
    QColor onBlue = lut::pickOn(breezeBlue);
    // Auf #3daee9 liefert Schwarz > 8.5:1, Weiß nur ~2.3:1
    QCOMPARE(onBlue, QColor(Qt::black));
    QVERIFY(lut::contrastRatio(onBlue, breezeBlue) >= 4.5);

    // Dunkler Hintergrund #1b1e20
    QColor darkBg(QStringLiteral("#1b1e20"));
    QColor onDark = lut::pickOn(darkBg);
    QCOMPARE(onDark, QColor(Qt::white));
    QVERIFY(lut::contrastRatio(onDark, darkBg) >= 4.5);
}

void ThemeContrastTest::testEnsureContrast() {
    QColor lowContrastGrey(QStringLiteral("#888888"));
    QColor darkBg(QStringLiteral("#1b1e20"));

    QColor ensured = lut::ensureContrast(lowContrastGrey, darkBg, 4.5);
    QVERIFY(lut::contrastRatio(ensured, darkBg) >= 4.5);
}

void ThemeContrastTest::testElevate() {
    QColor darkBg(QStringLiteral("#2a2e32"));
    QColor elevatedDark = lut::elevate(darkBg, 1);
    // Dunkle Basis wird aufgehellt
    QVERIFY(lut::relativeLuminance(elevatedDark) > lut::relativeLuminance(darkBg));

    QColor lightBg(QStringLiteral("#eff0f1"));
    QColor elevatedLight = lut::elevate(lightBg, 1);
    // Helle Basis wird abgedunkelt
    QVERIFY(lut::relativeLuminance(elevatedLight) < lut::relativeLuminance(lightBg));
}

void ThemeContrastTest::testBreezeDarkAndLightProfiles() {
    // Breeze Dark
    QColor breezeDarkBg(QStringLiteral("#2a2e32"));
    QColor breezeDarkText(QStringLiteral("#fcfcfc"));
    QVERIFY(lut::contrastRatio(breezeDarkText, breezeDarkBg) >= 4.5);

    // Breeze Light
    QColor breezeLightBg(QStringLiteral("#eff0f1"));
    QColor breezeLightText(QStringLiteral("#232629"));
    QVERIFY(lut::contrastRatio(breezeLightText, breezeLightBg) >= 4.5);
}

QTEST_MAIN(ThemeContrastTest)
#include "theme_contrast_test.moc"
