#include <QTest>
#include "liblut/backend/apt/PackageMetadata.h"
class AptMetadataTest : public QObject {
    Q_OBJECT
private slots:
    void realUnitsAndVersions() {
        auto items = lut::parseAptMetadata(QStringLiteral("Package: example\nVersion: 1:2.0-1\nArchitecture: amd64\nSize: 12345\nInstalled-Size: 20\nDescription: Example\n continuation\n\nPackage: example\nVersion: 1:1.0-1\nArchitecture: arm64\nSize: 4000\nInstalled-Size: 8\n"));
        QCOMPARE(items.size(), 2); QCOMPARE(items[0].downloadSize, 12345); QCOMPARE(items[0].installedSize, 20480);
        QCOMPARE(items[0].version, QStringLiteral("1:2.0-1")); QCOMPARE(items[1].arch, QStringLiteral("arm64"));
        QCOMPARE(items[0].summary, QStringLiteral("Example"));
    }
    void unknownIsNotZeroOrAnInventedEstimate() {
        auto items = lut::parseAptMetadata(QStringLiteral("Package: a\nSize: nonsense\nInstalled-Size: 9223372036854775807\n\nPackage: b\nSize: 0\nInstalled-Size: 0\n"));
        QCOMPARE(items.size(), 2); QCOMPARE(items[0].downloadSize, -1); QCOMPARE(items[0].installedSize, -1);
        QCOMPARE(items[1].downloadSize, 0); QCOMPARE(items[1].installedSize, 0);
    }
};
QTEST_GUILESS_MAIN(AptMetadataTest)
#include "apt_metadata_test.moc"
