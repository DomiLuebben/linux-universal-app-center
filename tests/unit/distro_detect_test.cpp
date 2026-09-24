#include <QTest>
#include <QTemporaryFile>
#include "liblut/detect/DistroDetect.h"

class DistroDetectTest : public QObject {
    Q_OBJECT

private slots:
    void testDetectFedora44();
    void testDetectCachyOS();
    void testDetectManjaro();
    void testDetectDebian13();
    void testDetectUbuntu2404();
    void testDetectCurrentHost();
};

// Herkunft: Container-Abbild fedora:44 (Image-ID 43b29f65a41e, 23.09.2026)
// Befehl: docker run --net=none --rm fedora:44 cat /etc/os-release
void DistroDetectTest::testDetectFedora44() {
    QTemporaryFile file;
    QVERIFY(file.open());
    const QByteArray osRelease =
        "NAME=\"Fedora Linux\"\n"
        "VERSION=\"44 (Container Image)\"\n"
        "RELEASE_TYPE=stable\n"
        "ID=fedora\n"
        "VERSION_ID=44\n"
        "VERSION_CODENAME=\"\"\n"
        "PRETTY_NAME=\"Fedora Linux 44 (Container Image)\"\n"
        "ANSI_COLOR=\"0;38;2;60;110;180\"\n"
        "LOGO=fedora-logo-icon\n"
        "CPE_NAME=\"cpe:/o:fedoraproject:fedora:44\"\n"
        "DEFAULT_HOSTNAME=\"fedora\"\n"
        "HOME_URL=\"https://fedoraproject.org/\"\n"
        "DOCUMENTATION_URL=\"https://docs.fedoraproject.org/en-US/fedora/f44/\"\n"
        "SUPPORT_URL=\"https://ask.fedoraproject.org/\"\n"
        "BUG_REPORT_URL=\"https://bugzilla.redhat.com/\"\n"
        "REDHAT_BUGZILLA_PRODUCT=\"Fedora\"\n"
        "REDHAT_BUGZILLA_PRODUCT_VERSION=44\n"
        "REDHAT_SUPPORT_PRODUCT=\"Fedora\"\n"
        "REDHAT_SUPPORT_PRODUCT_VERSION=44\n"
        "SUPPORT_END=2027-05-19\n"
        "VARIANT=\"Container Image\"\n"
        "VARIANT_ID=container\n";
    file.write(osRelease);
    file.close();

    lut::DistroFamily f = lut::DistroDetect::detectFamily(file.fileName());
    QCOMPARE(f, lut::DistroFamily::Fedora);
    QCOMPARE(lut::DistroDetect::prettyName(file.fileName()), QStringLiteral("Fedora Linux 44 (Container Image)"));
}

// Herkunft: Entwicklungsrechner Host-System /etc/os-release (23.09.2026)
void DistroDetectTest::testDetectCachyOS() {
    QTemporaryFile file;
    QVERIFY(file.open());
    const QByteArray osRelease =
        "NAME=\"CachyOS Linux\"\n"
        "PRETTY_NAME=\"CachyOS\"\n"
        "ID=cachyos\n"
        "ID_LIKE=arch\n"
        "BUILD_ID=rolling\n"
        "ANSI_COLOR=\"38;2;23;147;209\"\n"
        "HOME_URL=\"https://cachyos.org/\"\n"
        "DOCUMENTATION_URL=\"https://wiki.cachyos.org/\"\n"
        "SUPPORT_URL=\"https://discuss.cachyos.org/\"\n"
        "BUG_REPORT_URL=\"https://github.com/cachyos\"\n"
        "PRIVACY_POLICY_URL=\"https://terms.archlinux.org/docs/privacy-policy/\"\n"
        "LOGO=cachyos\n";
    file.write(osRelease);
    file.close();

    lut::DistroFamily f = lut::DistroDetect::detectFamily(file.fileName());
    QCOMPARE(f, lut::DistroFamily::Arch);
    QCOMPARE(lut::DistroDetect::prettyName(file.fileName()), QStringLiteral("CachyOS"));
}

// Herkunft: Container-Abbild manjarolinux/base:latest (sha256:bbf1f1d746f28e138eea610e140d2f28cbb5b7c5da2fbff034b883527aa604e9, 23.09.2026)
// Befehl: docker run --net=host --rm manjarolinux/base cat /etc/os-release
void DistroDetectTest::testDetectManjaro() {
    QTemporaryFile file;
    QVERIFY(file.open());
    const QByteArray osRelease =
        "NAME=\"Manjaro Linux\"\n"
        "PRETTY_NAME=\"Manjaro Linux\"\n"
        "ID=manjaro\n"
        "ID_LIKE=arch\n"
        "BUILD_ID=rolling\n"
        "ANSI_COLOR=\"32;1;24;144;200\"\n"
        "HOME_URL=\"https://manjaro.org/\"\n"
        "DOCUMENTATION_URL=\"https://wiki.manjaro.org/\"\n"
        "SUPPORT_URL=\"https://forum.manjaro.org/\"\n"
        "BUG_REPORT_URL=\"https://manjaro.org/help/\"\n"
        "PRIVACY_POLICY_URL=\"https://manjaro.org/privacy-policy/\"\n"
        "LOGO=manjarolinux\n";
    file.write(osRelease);
    file.close();

    lut::DistroFamily f = lut::DistroDetect::detectFamily(file.fileName());
    QCOMPARE(f, lut::DistroFamily::Arch);
    QCOMPARE(lut::DistroDetect::prettyName(file.fileName()), QStringLiteral("Manjaro Linux"));
}

// Herkunft: Container-Abbild debian:trixie-slim (Image-ID a99cfc517144, 23.09.2026)
// Befehl: docker run --net=none --rm debian:trixie-slim cat /etc/os-release
void DistroDetectTest::testDetectDebian13() {
    QTemporaryFile file;
    QVERIFY(file.open());
    const QByteArray osRelease =
        "PRETTY_NAME=\"Debian GNU/Linux 13 (trixie)\"\n"
        "NAME=\"Debian GNU/Linux\"\n"
        "VERSION_ID=\"13\"\n"
        "VERSION=\"13 (trixie)\"\n"
        "VERSION_CODENAME=trixie\n"
        "DEBIAN_VERSION_FULL=13.7\n"
        "ID=debian\n"
        "HOME_URL=\"https://www.debian.org/\"\n"
        "SUPPORT_URL=\"https://www.debian.org/support\"\n"
        "BUG_REPORT_URL=\"https://bugs.debian.org/\"\n";
    file.write(osRelease);
    file.close();

    lut::DistroFamily f = lut::DistroDetect::detectFamily(file.fileName());
    QCOMPARE(f, lut::DistroFamily::Debian);
    QCOMPARE(lut::DistroDetect::prettyName(file.fileName()), QStringLiteral("Debian GNU/Linux 13 (trixie)"));
}

// Herkunft: Container-Abbild ubuntu:24.04 (Image-ID 008173c23f95, 23.09.2026)
// Befehl: docker run --net=none --rm ubuntu:24.04 cat /etc/os-release
void DistroDetectTest::testDetectUbuntu2404() {
    QTemporaryFile file;
    QVERIFY(file.open());
    const QByteArray osRelease =
        "PRETTY_NAME=\"Ubuntu 24.04.5 LTS\"\n"
        "NAME=\"Ubuntu\"\n"
        "VERSION_ID=\"24.04\"\n"
        "VERSION=\"24.04.5 LTS (Noble Numbat)\"\n"
        "VERSION_CODENAME=noble\n"
        "ID=ubuntu\n"
        "ID_LIKE=debian\n"
        "HOME_URL=\"https://www.ubuntu.com/\"\n"
        "SUPPORT_URL=\"https://help.ubuntu.com/\"\n"
        "BUG_REPORT_URL=\"https://bugs.launchpad.net/ubuntu/\"\n"
        "PRIVACY_POLICY_URL=\"https://www.ubuntu.com/legal/terms-and-policies/privacy-policy\"\n"
        "UBUNTU_CODENAME=noble\n"
        "LOGO=ubuntu-logo\n";
    file.write(osRelease);
    file.close();

    lut::DistroFamily f = lut::DistroDetect::detectFamily(file.fileName());
    QCOMPARE(f, lut::DistroFamily::Debian);
    QCOMPARE(lut::DistroDetect::prettyName(file.fileName()), QStringLiteral("Ubuntu 24.04.5 LTS"));
}

void DistroDetectTest::testDetectCurrentHost() {
    lut::DistroFamily f = lut::DistroDetect::detectFamily();
    QVERIFY(f == lut::DistroFamily::Arch || f == lut::DistroFamily::Fedora ||
            f == lut::DistroFamily::Debian || f == lut::DistroFamily::Unknown);
}

QTEST_MAIN(DistroDetectTest)
#include "distro_detect_test.moc"
