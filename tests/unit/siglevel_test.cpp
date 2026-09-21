#include <QTest>
#include "liblut/backend/alpm/SigLevelParser.h"

using namespace lut;

class SigLevelTest : public QObject {
    Q_OBJECT

private slots:
    void testArchDefaultConfig();
    void testNeverDisablesChecks();
    void testTrustAllAddsMarginalBits();
    void testUnparsableInputReportsFailure();
    void testFallbackIsNeverPermissive();
};

#ifdef HAVE_ALPM

// So gibt `pacman-conf SigLevel` auf Arch/CachyOS aus.
void SigLevelTest::testArchDefaultConfig() {
    const QStringList tokens = {
        QStringLiteral("PackageRequired"),
        QStringLiteral("PackageTrustedOnly"),
        QStringLiteral("DatabaseOptional"),
        QStringLiteral("DatabaseTrustedOnly")
    };
    int level = parseSigLevel(tokens, 0);

    QVERIFY(level > 0);
    // Pakete müssen signiert sein ...
    QVERIFY(level & ALPM_SIG_PACKAGE);
    QVERIFY(!(level & ALPM_SIG_PACKAGE_OPTIONAL));
    // ... Datenbanken dürfen es sein.
    QVERIFY(level & ALPM_SIG_DATABASE);
    QVERIFY(level & ALPM_SIG_DATABASE_OPTIONAL);
    // TrustedOnly: keine marginalen/unbekannten Schlüssel akzeptieren.
    QVERIFY(!(level & ALPM_SIG_PACKAGE_MARGINAL_OK));
    QVERIFY(!(level & ALPM_SIG_PACKAGE_UNKNOWN_OK));
    QVERIFY(!(level & ALPM_SIG_DATABASE_MARGINAL_OK));
    QVERIFY(!(level & ALPM_SIG_DATABASE_UNKNOWN_OK));
}

void SigLevelTest::testNeverDisablesChecks() {
    int level = parseSigLevel({QStringLiteral("Never")}, kSecureSigLevelFallback);
    QCOMPARE(level & ALPM_SIG_PACKAGE, 0);
    QCOMPARE(level & ALPM_SIG_DATABASE, 0);
}

void SigLevelTest::testTrustAllAddsMarginalBits() {
    int level = parseSigLevel({QStringLiteral("Required"), QStringLiteral("TrustAll")}, 0);
    QVERIFY(level & ALPM_SIG_PACKAGE);
    QVERIFY(level & ALPM_SIG_PACKAGE_MARGINAL_OK);
    QVERIFY(level & ALPM_SIG_PACKAGE_UNKNOWN_OK);
}

void SigLevelTest::testUnparsableInputReportsFailure() {
    // -1 signalisiert dem Aufrufer "nichts Verwertbares" und löst die
    // strenge Vorgabe aus. Ein stilles 0 wäre "keine Signaturprüfung".
    QCOMPARE(parseSigLevel({}, 0), -1);
    QCOMPARE(parseSigLevel({QStringLiteral("QuatschWort")}, 0), -1);
    QCOMPARE(parseSigLevel({QStringLiteral("")}, 0), -1);
}

// Regressionswächter: der ursprüngliche Fehler war, dass gar kein Siglevel
// gesetzt wurde und alpm damit auf 0 stand – also ohne jede Signaturprüfung.
void SigLevelTest::testFallbackIsNeverPermissive() {
    QVERIFY(kSecureSigLevelFallback != 0);
    QVERIFY(kSecureSigLevelFallback & ALPM_SIG_PACKAGE);
    QVERIFY(!(kSecureSigLevelFallback & ALPM_SIG_PACKAGE_OPTIONAL));
    QVERIFY(!(kSecureSigLevelFallback & ALPM_SIG_PACKAGE_MARGINAL_OK));
    QVERIFY(!(kSecureSigLevelFallback & ALPM_SIG_PACKAGE_UNKNOWN_OK));
}

#else // !HAVE_ALPM

void SigLevelTest::testArchDefaultConfig() { QSKIP("ohne libalpm gebaut"); }
void SigLevelTest::testNeverDisablesChecks() { QSKIP("ohne libalpm gebaut"); }
void SigLevelTest::testTrustAllAddsMarginalBits() { QSKIP("ohne libalpm gebaut"); }
void SigLevelTest::testUnparsableInputReportsFailure() { QSKIP("ohne libalpm gebaut"); }
void SigLevelTest::testFallbackIsNeverPermissive() { QSKIP("ohne libalpm gebaut"); }

#endif

QTEST_MAIN(SigLevelTest)
#include "siglevel_test.moc"
