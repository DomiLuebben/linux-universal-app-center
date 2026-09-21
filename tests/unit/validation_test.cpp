#include <QTest>
#include "liblut/backend/Validation.h"

class ValidationTest : public QObject {
    Q_OBJECT

private slots:
    void testValidPackageNames();
    void testInvalidPackageNames();
    void testPackageListValidation();
};

void ValidationTest::testValidPackageNames() {
    const QStringList valid = {
        QStringLiteral("glibc"),
        QStringLiteral("kernel-core"),
        QStringLiteral("libx11_6"),
        QStringLiteral("gcc-c++"),
        QStringLiteral("qt6-base"),
        QStringLiteral("python3.11"),
        QStringLiteral("alsa-plugins-pulseaudio"),
        QStringLiteral("xorg-x11-server-Xwayland")
    };

    for (const auto &pkg : valid) {
        QVERIFY2(lut::Validation::isValidPackageName(pkg),
                 qPrintable(QString("Package name '%1' must be valid").arg(pkg)));
    }
}

void ValidationTest::testInvalidPackageNames() {
    const QStringList invalid = {
        QString(),                                  // Leer
        QStringLiteral("-rf"),                      // Startet mit Minus
        QStringLiteral("--nodeps"),                 // Startet mit Minus
        QStringLiteral("../../etc/passwd"),         // Pfadtrennzeichen & Traversal
        QStringLiteral("/bin/sh"),                  // Absoluter Pfad
        QStringLiteral("pkg;rm -rf /"),             // Shell-Semikolon
        QStringLiteral("pkg$(reboot)"),             // Command-Substitution
        QStringLiteral("pkg`reboot`"),              // Backticks
        QStringLiteral("pkg|grep foo"),             // Pipe
        QStringLiteral("pkg&"),                     // Background
        QStringLiteral("pkg with spaces"),          // Leerzeichen
        QStringLiteral("pkg\nname"),                // Newline
        QString(129, QLatin1Char('a'))              // > 128 Zeichen
    };

    for (const auto &pkg : invalid) {
        QVERIFY2(!lut::Validation::isValidPackageName(pkg),
                 qPrintable(QString("Package name '%1' must be REJECTED").arg(pkg)));
    }
}

void ValidationTest::testPackageListValidation() {
    QVERIFY(lut::Validation::areValidPackageNames({QStringLiteral("curl"), QStringLiteral("wget")}));
    QVERIFY(!lut::Validation::areValidPackageNames({}));
    QVERIFY(!lut::Validation::areValidPackageNames({QStringLiteral("curl"), QStringLiteral("-rf")}));
}

QTEST_MAIN(ValidationTest)
#include "validation_test.moc"
