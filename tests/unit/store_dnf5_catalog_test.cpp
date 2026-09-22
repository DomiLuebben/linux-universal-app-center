#include <QTest>
#include <QSignalSpy>
#include "liblut/catalog/dnf5/Dnf5PackageCatalog.h"
#include "liblut/backend/dnf5/Dnf5Backend.h"

using namespace lut;

class StoreDnf5CatalogTest : public QObject {
    Q_OBJECT

private slots:
    void testParseAvailableOffers();
    void testMultiOfferCandidateSelection();
    void testParseInstalledState();
    void testParseFileProviders();
    void testParseInstalledPackagesWithOrphans();
    void testDnf5BackendCapabilities();
    void testCatalogGenerationAndReload();
};

void StoreDnf5CatalogTest::testParseAvailableOffers() {
    // Format: %{name}\x1f%{epoch}\x1f%{version}\x1f%{release}\x1f%{evr}\x1f%{arch}\x1f%{downloadsize}\x1f%{installsize}\x1f%{repoid}\x1f%{summary}\x1e
    QByteArray sampleData =
        "tree\x1f" "0\x1f" "2.2.1\x1f" "4.fc44\x1f" "2.2.1-4.fc44\x1f" "x86_64\x1f"
        "64157\x1f" "122910\x1f" "fedora\x1f" "File system tree viewer\x1e";

    auto offers = Dnf5PackageCatalog::parseAvailableOffers(sampleData);
    QCOMPARE(offers.size(), 1);

    const auto &offer = offers.first();
    QCOMPARE(offer.packages.size(), 1);
    QCOMPARE(offer.packages.first().backend, QStringLiteral("dnf5"));
    QCOMPARE(offer.packages.first().name, QStringLiteral("tree"));
    QCOMPARE(offer.packages.first().version, QStringLiteral("2.2.1-4.fc44"));
    QCOMPARE(offer.packages.first().arch, QStringLiteral("x86_64"));
    QCOMPARE(offer.packages.first().repoId, QStringLiteral("fedora"));
    QCOMPARE(offer.downloadSize.value_or(0), 64157);
    QCOMPARE(offer.installedSize.value_or(0), 122910);
    QVERIFY(offer.available);
    QVERIFY(offer.isCandidate);
    QCOMPARE(offer.priority, 100);
}

void StoreDnf5CatalogTest::testMultiOfferCandidateSelection() {
    // Zwei Versionen: fedora (Base) und updates
    QByteArray sampleData =
        "inkscape\x1f" "0\x1f" "1.4\x1f" "1.fc44\x1f" "1.4-1.fc44\x1f" "x86_64\x1f"
        "45000000\x1f" "110000000\x1f" "fedora\x1f" "Vector graphics editor\x1e"
        "inkscape\x1f" "0\x1f" "1.4.1\x1f" "1.fc44\x1f" "1.4.1-1.fc44\x1f" "x86_64\x1f"
        "46000000\x1f" "112000000\x1f" "updates\x1f" "Vector graphics editor\x1e";

    auto offers = Dnf5PackageCatalog::parseAvailableOffers(sampleData);
    QCOMPARE(offers.size(), 2);

    // Updates-Repo (Priorität 110) muss an erster Stelle stehen und Kandidat sein
    QCOMPARE(offers[0].packages.first().repoId, QStringLiteral("updates"));
    QCOMPARE(offers[0].packages.first().version, QStringLiteral("1.4.1-1.fc44"));
    QCOMPARE(offers[0].priority, 110);
    QVERIFY(offers[0].isCandidate);

    // Fedora-Repo (Priorität 100) darf kein Kandidat sein
    QCOMPARE(offers[1].packages.first().repoId, QStringLiteral("fedora"));
    QCOMPARE(offers[1].priority, 100);
    QVERIFY(!offers[1].isCandidate);
}

void StoreDnf5CatalogTest::testParseInstalledState() {
    // Format: %{name}\x1f%{epoch}\x1f%{version}\x1f%{release}\x1f%{evr}\x1f%{arch}\x1f%{installsize}\x1f%{from_repo}\x1e
    QByteArray installedData =
        "ripgrep\x1f" "0\x1f" "14.1.0\x1f" "2.fc44\x1f" "14.1.0-2.fc44\x1f" "x86_64\x1f"
        "4500000\x1f" "fedora\x1e";

    auto state = Dnf5PackageCatalog::parseInstalledState(installedData, QStringLiteral("ripgrep"));
    QVERIFY(state.isFullyInstalled);
    QCOMPARE(state.origin, QStringLiteral("dnf5"));
    QCOMPARE(state.installedPackages.size(), 1);
    QCOMPARE(state.installedPackages.first().name, QStringLiteral("ripgrep"));
    QCOMPARE(state.installedPackages.first().version, QStringLiteral("14.1.0-2.fc44"));
    QCOMPARE(state.installedPackages.first().arch, QStringLiteral("x86_64"));

    // Nicht installiertes Paket
    auto missingState = Dnf5PackageCatalog::parseInstalledState(installedData, QStringLiteral("neovim"));
    QVERIFY(!missingState.isFullyInstalled);
    QVERIFY(missingState.installedPackages.isEmpty());
}

void StoreDnf5CatalogTest::testParseFileProviders() {
    QByteArray fileData =
        "coreutils\x1f" "9.5-2.fc44\x1f" "x86_64\x1f" "fedora\x1e";

    auto providers = Dnf5PackageCatalog::parseFileProviders(fileData);
    QCOMPARE(providers.size(), 1);
    QCOMPARE(providers.first().backend, QStringLiteral("dnf5"));
    QCOMPARE(providers.first().name, QStringLiteral("coreutils"));
    QCOMPARE(providers.first().version, QStringLiteral("9.5-2.fc44"));
    QCOMPARE(providers.first().arch, QStringLiteral("x86_64"));
    QCOMPARE(providers.first().repoId, QStringLiteral("fedora"));
}

void StoreDnf5CatalogTest::testParseInstalledPackagesWithOrphans() {
    QByteArray data =
        "gcc\x1f" "14.2.1-1.fc44\x1f" "x86_64\x1f" "95000000\x1f" "fedora\x1f" "1720000000\x1f" "GNU C Compiler\x1e"
        "libmpc\x1f" "1.3.1-4.fc44\x1f" "x86_64\x1f" "350000\x1f" "fedora\x1f" "1720000000\x1f" "C library for complex numbers\x1e";

    QByteArray unneeded = "libmpc.x86_64\n";

    auto pkgs = Dnf5PackageCatalog::parseInstalledPackages(data, unneeded);
    QCOMPARE(pkgs.size(), 2);

    QCOMPARE(pkgs[0].name, QStringLiteral("gcc"));
    QVERIFY(!pkgs[0].isOrphan);

    QCOMPARE(pkgs[1].name, QStringLiteral("libmpc"));
    QVERIFY(pkgs[1].isOrphan);
}

void StoreDnf5CatalogTest::testDnf5BackendCapabilities() {
    Dnf5Backend backend;
    Capabilities cap = backend.capabilities();

    QVERIFY(cap.catalogQuery);
    QVERIFY(cap.install);
    QVERIFY(cap.remove);
    QVERIFY(!cap.installRequiresFullUpgrade); // Fedora erlaubt Einzelinstallation ohne Systemupdate
    QVERIFY(cap.typedPackageTargets);
    QVERIFY(cap.transactionReattach);
    QCOMPARE(cap.protocolVersion, 2);
}

void StoreDnf5CatalogTest::testCatalogGenerationAndReload() {
    Dnf5PackageCatalog catalog;
    quint64 gen1 = catalog.catalogGeneration();
    QCOMPARE(gen1, static_cast<quint64>(1));

    catalog.reload();
    quint64 gen2 = catalog.catalogGeneration();
    QCOMPARE(gen2, static_cast<quint64>(2));
}

QTEST_MAIN(StoreDnf5CatalogTest)
#include "store_dnf5_catalog_test.moc"
