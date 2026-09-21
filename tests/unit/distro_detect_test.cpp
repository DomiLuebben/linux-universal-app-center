#include <QTest>
#include <QTemporaryFile>
#include "liblut/detect/DistroDetect.h"

class DistroDetectTest : public QObject {
    Q_OBJECT

private slots:
    void testDetectFedora();
    void testDetectArch();
    void testDetectDebian();
    void testDetectCurrentHost();
};

void DistroDetectTest::testDetectFedora() {
    QTemporaryFile file;
    QVERIFY(file.open());
    file.write("NAME=\"Fedora Linux\"\nVERSION=\"44 (Workstation Edition)\"\nID=fedora\nVERSION_ID=44\nPRETTY_NAME=\"Fedora Linux 44 (Workstation Edition)\"\n");
    file.close();

    lut::DistroFamily f = lut::DistroDetect::detectFamily(file.fileName());
    QCOMPARE(f, lut::DistroFamily::Fedora);
    QCOMPARE(lut::DistroDetect::prettyName(file.fileName()), QStringLiteral("Fedora Linux 44 (Workstation Edition)"));
}

void DistroDetectTest::testDetectArch() {
    QTemporaryFile file;
    QVERIFY(file.open());
    file.write("NAME=\"CachyOS Linux\"\nID=cachyos\nID_LIKE=arch\nPRETTY_NAME=\"CachyOS\"\n");
    file.close();

    lut::DistroFamily f = lut::DistroDetect::detectFamily(file.fileName());
    QCOMPARE(f, lut::DistroFamily::Arch);
}

void DistroDetectTest::testDetectDebian() {
    QTemporaryFile file;
    QVERIFY(file.open());
    file.write("PRETTY_NAME=\"Ubuntu 24.04 LTS\"\nNAME=\"Ubuntu\"\nVERSION_ID=\"24.04\"\nVERSION=\"24.04 LTS (Noble Numbat)\"\nID=ubuntu\nID_LIKE=debian\n");
    file.close();

    lut::DistroFamily f = lut::DistroDetect::detectFamily(file.fileName());
    QCOMPARE(f, lut::DistroFamily::Debian);
    QCOMPARE(lut::DistroDetect::prettyName(file.fileName()), QStringLiteral("Ubuntu 24.04 LTS"));
}

void DistroDetectTest::testDetectCurrentHost() {
    lut::DistroFamily f = lut::DistroDetect::detectFamily();
    // On this host (CachyOS), it must be Arch
    QCOMPARE(f, lut::DistroFamily::Arch);
}

QTEST_MAIN(DistroDetectTest)
#include "distro_detect_test.moc"
