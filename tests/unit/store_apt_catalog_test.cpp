#include <QTest>
#include <QSignalSpy>
#include "liblut/catalog/apt/AptPackageCatalog.h"
#include "liblut/backend/apt/AptBackend.h"

class StoreAptCatalogTest : public QObject {
    Q_OBJECT

private slots:
    void testAvailableOffersAndCandidatePolicy();
    void testMultiarchOffers();
    void testDpkgStatusInstalledVsConfigFiles();
    void testHoldAndPinnedPolicy();
    void testProtectedPackagesBlocked();
    void testCommitFingerprintMismatch();
    void testCapabilities();
    void testFileProviders();
    void testReloadInvalidation();
};

void StoreAptCatalogTest::testAvailableOffersAndCandidatePolicy() {
    const QString policyOutput = QStringLiteral(
        "tree:\n"
        "  Installed: (none)\n"
        "  Candidate: 2.2.1-1\n"
        "  Version table:\n"
        "     2.2.1-1 500\n"
        "        500 http://deb.debian.org/debian trixie/main amd64 Packages\n"
        "     2.1.0-1 500\n"
        "        500 http://deb.debian.org/debian trixie/main amd64 Packages\n"
    );

    const QString showOutput = QStringLiteral(
        "Package: tree\n"
        "Version: 2.2.1-1\n"
        "Installed-Size: 129\n"
        "Architecture: amd64\n"
        "Description: displays an indented directory tree, in color\n"
        "Section: utils\n"
        "Size: 59392\n\n"
        "Package: tree\n"
        "Version: 2.1.0-1\n"
        "Installed-Size: 120\n"
        "Architecture: amd64\n"
        "Description: displays an indented directory tree, in color\n"
        "Section: utils\n"
        "Size: 54000\n"
    );

    QCOMPARE(lut::AptPackageCatalog::parsePolicyCandidate(policyOutput), QStringLiteral("2.2.1-1"));
    QCOMPARE(lut::AptPackageCatalog::parsePolicyInstalled(policyOutput), QString());

    const auto offers = lut::AptPackageCatalog::parseAvailableOffers(policyOutput, showOutput, QStringLiteral("tree"));
    QCOMPARE(offers.size(), 2);

    // Der Kandidat muss an erster Stelle stehen
    QVERIFY(offers.first().isCandidate);
    QCOMPARE(offers.first().packages.first().version, QStringLiteral("2.2.1-1"));
    QCOMPARE(offers.first().packages.first().arch, QStringLiteral("amd64"));
    QCOMPARE(offers.first().packages.first().repoId, QStringLiteral("utils"));
    QCOMPARE(offers.first().downloadSize.value_or(0), 59392);
    QCOMPARE(offers.first().installedSize.value_or(0), 129 * 1024);
    QCOMPARE(offers.first().packages.first().backend, QStringLiteral("apt"));
    QCOMPARE(offers.first().packages.first().name, QStringLiteral("tree"));

    // Zweites Angebot ist kein Kandidat
    QVERIFY(!offers.at(1).isCandidate);
    QCOMPARE(offers.at(1).packages.first().version, QStringLiteral("2.1.0-1"));
    QCOMPARE(offers.at(1).downloadSize.value_or(0), 54000);
    QCOMPARE(offers.at(1).installedSize.value_or(0), 120 * 1024);
}

void StoreAptCatalogTest::testMultiarchOffers() {
    const QString policyOutput = QStringLiteral(
        "wine:\n"
        "  Installed: (none)\n"
        "  Candidate: 9.0-1\n"
        "  Version table:\n"
        "     9.0-1 500\n"
        "        500 http://deb.debian.org/debian trixie/main amd64 Packages\n"
    );

    const QString showOutput = QStringLiteral(
        "Package: wine\n"
        "Version: 9.0-1\n"
        "Architecture: amd64\n"
        "Installed-Size: 500\n"
        "Size: 100000\n"
        "Section: otherosfs\n"
        "Description: Windows API implementation - standard suite\n\n"
        "Package: wine\n"
        "Version: 9.0-1\n"
        "Architecture: i386\n"
        "Installed-Size: 520\n"
        "Size: 105000\n"
        "Section: otherosfs\n"
        "Description: Windows API implementation - 32-bit suite\n"
    );

    const auto offers = lut::AptPackageCatalog::parseAvailableOffers(policyOutput, showOutput, QStringLiteral("wine"));
    QCOMPARE(offers.size(), 2);

    // Beide Architekturen getrennt modelliert
    bool hasAmd64 = false;
    bool hasI386 = false;
    for (const auto &offer : offers) {
        if (offer.packages.first().arch == QLatin1String("amd64")) {
            hasAmd64 = true;
        } else if (offer.packages.first().arch == QLatin1String("i386")) {
            hasI386 = true;
        }
    }
    QVERIFY(hasAmd64);
    QVERIFY(hasI386);
}

void StoreAptCatalogTest::testDpkgStatusInstalledVsConfigFiles() {
    // APT-03: Strikte Unterscheidung: "installed" gilt als installiert,
    // "config-files" (rc) nach Deinstallation ohne Purge gilt als NICHT installiert!
    const QString dpkgOutput = QStringLiteral(
        "installed\tgedit\t46.1-1\tamd64\t3400\tpopular text editor\n"
        "config-files\tlogrotate\t3.22.0-1\tamd64\t150\tLog rotation utility\n"
        "not-installed\tvlc\t3.0.20-1\tamd64\t40000\tmultimedia player\n"
    );

    // 1. gedit ist installiert
    const auto geditState = lut::AptPackageCatalog::parseInstalledState(dpkgOutput, QStringLiteral("gedit"), {QStringLiteral("org.gnome.gedit.desktop")});
    QVERIFY(geditState.isFullyInstalled);
    QCOMPARE(geditState.installedPackages.size(), 1);
    QCOMPARE(geditState.installedPackages.first().name, QStringLiteral("gedit"));
    QCOMPARE(geditState.installedPackages.first().version, QStringLiteral("46.1-1"));
    QCOMPARE(geditState.launchableDesktopIds, QStringList{QStringLiteral("org.gnome.gedit.desktop")});

    // 2. logrotate ist config-files -> darf keinesfalls als installiert gewertet werden!
    const auto logrotateState = lut::AptPackageCatalog::parseInstalledState(dpkgOutput, QStringLiteral("logrotate"), {});
    QVERIFY(!logrotateState.isFullyInstalled);
    QVERIFY(logrotateState.installedPackages.isEmpty());

    // 3. vlc ist not-installed
    const auto vlcState = lut::AptPackageCatalog::parseInstalledState(dpkgOutput, QStringLiteral("vlc"), {});
    QVERIFY(!vlcState.isFullyInstalled);
    QVERIFY(vlcState.installedPackages.isEmpty());

    // 4. allInstalledPackages filtert config-files und not-installed heraus
    const auto allInstalled = lut::AptPackageCatalog::parseInstalledPackages(dpkgOutput);
    QCOMPARE(allInstalled.size(), 1);
    QCOMPARE(allInstalled.first().name, QStringLiteral("gedit"));
}

void StoreAptCatalogTest::testHoldAndPinnedPolicy() {
    // APT-02: Pinned / Held Kandidat weicht von neuester Repo-Version ab
    const QString pinnedPolicy = QStringLiteral(
        "mypkg:\n"
        "  Installed: 1.0.0-1\n"
        "  Candidate: 1.0.0-1\n"
        "  Version table:\n"
        "     2.0.0-1 50\n"
        "        50 http://deb.debian.org/debian experimental/main amd64 Packages\n"
        " *** 1.0.0-1 1001\n"
        "        500 http://deb.debian.org/debian trixie/main amd64 Packages\n"
        "        100 /var/lib/dpkg/status\n"
    );

    const QString showOutput = QStringLiteral(
        "Package: mypkg\n"
        "Version: 2.0.0-1\n"
        "Architecture: amd64\n"
        "Installed-Size: 200\n"
        "Size: 50000\n"
        "Section: utils\n"
        "Description: experimental version\n\n"
        "Package: mypkg\n"
        "Version: 1.0.0-1\n"
        "Architecture: amd64\n"
        "Installed-Size: 180\n"
        "Size: 45000\n"
        "Section: utils\n"
        "Description: stable pinned version\n"
    );

    const auto offers = lut::AptPackageCatalog::parseAvailableOffers(pinnedPolicy, showOutput, QStringLiteral("mypkg"));
    QCOMPARE(offers.size(), 2);

    // 1.0.0-1 muss der Kandidat sein, obwohl 2.0.0-1 existiert
    QVERIFY(offers.first().isCandidate);
    QCOMPARE(offers.first().packages.first().version, QStringLiteral("1.0.0-1"));
    QVERIFY(!offers.at(1).isCandidate);
    QCOMPARE(offers.at(1).packages.first().version, QStringLiteral("2.0.0-1"));
}

void StoreAptCatalogTest::testProtectedPackagesBlocked() {
    // APT-04: Essentielle / geschützte Pakete dürfen niemals deinstalliert werden
    QVERIFY(lut::AptBackend::isProtectedPackage(QStringLiteral("dpkg")));
    QVERIFY(lut::AptBackend::isProtectedPackage(QStringLiteral("apt")));
    QVERIFY(lut::AptBackend::isProtectedPackage(QStringLiteral("libc6")));
    QVERIFY(lut::AptBackend::isProtectedPackage(QStringLiteral("systemd")));
    QVERIFY(lut::AptBackend::isProtectedPackage(QStringLiteral("coreutils")));
    QVERIFY(lut::AptBackend::isProtectedPackage(QStringLiteral("bash")));

    QVERIFY(!lut::AptBackend::isProtectedPackage(QStringLiteral("tree")));
    QVERIFY(!lut::AptBackend::isProtectedPackage(QStringLiteral("htop")));

    // Backend-Entfernungsversuch muss fehlschlagen
    lut::AptBackend backend;
    bool hasFailedDone = false;
    QObject::connect(&backend, &lut::Backend::eventEmitted, [&](const lut::Event &ev) {
        if (std::holds_alternative<lut::TransactionDone>(ev)) {
            const auto &done = std::get<lut::TransactionDone>(ev);
            if (done.result == lut::Result::Failed && done.summary.contains(QStringLiteral("geschützten"))) {
                hasFailedDone = true;
            }
        }
    });

    lut::TransactionIntent removeAptIntent;
    removeAptIntent.type = lut::TransactionIntent::Type::Remove;
    removeAptIntent.targets = {lut::PackageRef{QStringLiteral("apt"), QString(), QStringLiteral("apt"), QString(), QString()}};

    backend.planPackageTransaction(removeAptIntent);
    QVERIFY(hasFailedDone);
}

void StoreAptCatalogTest::testCommitFingerprintMismatch() {
    // TX-09 / APT-05: Commit mit abweichendem Fingerprint wird abgewiesen
    lut::AptBackend backend;
    bool hasMismatchFailed = false;
    QObject::connect(&backend, &lut::Backend::eventEmitted, [&](const lut::Event &ev) {
        if (std::holds_alternative<lut::TransactionDone>(ev)) {
            const auto &done = std::get<lut::TransactionDone>(ev);
            if (done.result == lut::Result::Failed) {
                hasMismatchFailed = true;
            }
        }
    });

    backend.commitPlan(QStringLiteral("sha256:invalidfingerprint1234567890"));
    QVERIFY(hasMismatchFailed);
}

void StoreAptCatalogTest::testCapabilities() {
    lut::AptBackend backend;
    const auto cap = backend.capabilities();

    QVERIFY(cap.catalogQuery);
    QVERIFY(cap.install);
    QVERIFY(cap.remove);
    QVERIFY(!cap.installRequiresFullUpgrade); // Debian verlangt kein Teilupdate-Verbot bei apt install
    QVERIFY(cap.typedPackageTargets);
    QVERIFY(cap.transactionReattach);
    QVERIFY(!cap.degraded);
    QCOMPARE(cap.protocolVersion, 2);
}

void StoreAptCatalogTest::testFileProviders() {
    const QString dpkgSearchOutput = QStringLiteral(
        "libc6:amd64: /lib/x86_64-linux-gnu/libc.so.6\n"
        "coreutils: /bin/ls\n"
    );

    const auto providers = lut::AptPackageCatalog::parseFileProviders(dpkgSearchOutput);
    QCOMPARE(providers.size(), 2);
    QCOMPARE(providers.first().name, QStringLiteral("libc6"));
    QCOMPARE(providers.first().arch, QStringLiteral("amd64"));
    QCOMPARE(providers.at(1).name, QStringLiteral("coreutils"));
    QVERIFY(providers.at(1).arch.isEmpty());
}

void StoreAptCatalogTest::testReloadInvalidation() {
    lut::AptPackageCatalog catalog;
    const quint64 gen1 = catalog.catalogGeneration();
    catalog.reload();
    const quint64 gen2 = catalog.catalogGeneration();
    QVERIFY(gen2 > gen1);
}

QTEST_GUILESS_MAIN(StoreAptCatalogTest)
#include "store_apt_catalog_test.moc"
