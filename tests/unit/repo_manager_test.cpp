#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include "liblut/repository/RepoManager.h"

// Hilfsfunktion: RepoManager für Fedora mit Wegwerf-Verzeichnis und
// aufgezeichneten Befehlen statt echtem dnf5.
struct FedoraFixture {
    QTemporaryDir dir;
    lut::RepoManager mgr;
    QStringList commands;
    QHash<QString, QString> owners;   // Datei -> besitzendes Paket
    int exitCode = 0;

    explicit FedoraFixture(const QByteArray &osRelease = "NAME=Fedora Linux\nVERSION_ID=44\n") {
        QFile os(dir.filePath(QStringLiteral("os-release")));
        if (os.open(QIODevice::WriteOnly)) os.write(osRelease);
        os.close();
        QDir().mkpath(dir.filePath(QStringLiteral("yum.repos.d")));
        mgr.setForcedDistroFamily(lut::DistroFamily::Fedora);
        mgr.setYumReposDir(dir.filePath(QStringLiteral("yum.repos.d")));
        mgr.setOsReleasePath(dir.filePath(QStringLiteral("os-release")));
        mgr.setCommandRunner([this](const QString &program, const QStringList &args, QString *output) {
            if (program == QLatin1String("rpm")) {
                const QString owner = owners.value(args.last());
                if (output) *output = owner;
                return owner.isEmpty() ? 1 : 0;
            }
            commands.append(program + QLatin1Char(' ') + args.join(QLatin1Char(' ')));
            return exitCode;
        });
    }
    QString repoFile(const QString &name) const { return dir.filePath(QStringLiteral("yum.repos.d/") + name); }
    void writeRepo(const QString &name, const QByteArray &content) const {
        QFile f(repoFile(name));
        if (f.open(QIODevice::WriteOnly)) f.write(content);
    }
};

// --- Pacstall-PPR -------------------------------------------------------
// Fixture: tests/fixtures/ppr-public-key.asc, am 24.09.2026 geladen von
// https://ppr.pacstall.dev/ppr-public-key.asc (sha256 a68d257e…465613),
// gpg --show-keys: 9230DDFB1ABA144D4AB7FDDFB2490BBE624005C2 "PPR <pacstall@pm.me>".
QByteArray pprKey() {
    QFile f(QStringLiteral(PROJECT_DIR "/tests/fixtures/ppr-public-key.asc"));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

struct DebianPpr {
    QTemporaryDir dir;
    lut::RepoManager mgr;
    QStringList commands;
    QByteArray served;
    int aptExit = 0;
    explicit DebianPpr(const QByteArray &key) : served(key) {
        mgr.setForcedDistroFamily(lut::DistroFamily::Debian);
        mgr.setAptSourcesDir(dir.filePath(QStringLiteral("sources.list.d")));
        mgr.setAptSourcesList(dir.filePath(QStringLiteral("sources.list")));
        mgr.setAptKeyringDir(dir.filePath(QStringLiteral("keyrings")));
        mgr.setDownloader([this](const QUrl &url, QString *) {
            commands.append(QStringLiteral("GET ") + url.toString());
            return served;
        });
        mgr.setCommandRunner([this](const QString &program, const QStringList &args, QString *output) {
            commands.append(program + QLatin1Char(' ') + args.join(QLatin1Char(' ')));
            if (program == QLatin1String("dpkg")) { if (output) *output = QStringLiteral("amd64\n"); return 0; }
            if (output) *output = aptExit ? QStringLiteral("E: The repository is not signed.") : QString();
            return aptExit;
        });
    }
    QString list() const { return dir.filePath(QStringLiteral("sources.list.d/pacstall.list")); }
    QString keyring() const { return dir.filePath(QStringLiteral("keyrings/ppr-keyring.gpg")); }
};

class RepoManagerTest : public QObject {
    Q_OBJECT

private slots:
    void testValidRepoId() {
        QVERIFY(lut::RepoManager::isValidRepoId(QStringLiteral("multilib")));
        QVERIFY(lut::RepoManager::isValidRepoId(QStringLiteral("chaotic-aur")));
        QVERIFY(lut::RepoManager::isValidRepoId(QStringLiteral("rpmfusion-free")));
        QVERIFY(lut::RepoManager::isValidRepoId(QStringLiteral("repo_1.2-3")));

        QVERIFY(!lut::RepoManager::isValidRepoId(QStringLiteral("")));
        QVERIFY(!lut::RepoManager::isValidRepoId(QStringLiteral("   ")));
        QVERIFY(!lut::RepoManager::isValidRepoId(QStringLiteral("../escape")));
        QVERIFY(!lut::RepoManager::isValidRepoId(QStringLiteral("path/traversal")));
        QVERIFY(!lut::RepoManager::isValidRepoId(QStringLiteral("name with spaces")));
        QVERIFY(!lut::RepoManager::isValidRepoId(QStringLiteral("semi;colon")));
    }

    void testPacmanRepos() {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());
        QString confPath = tmpDir.filePath(QStringLiteral("pacman.conf"));

        QString initialConf = QStringLiteral(
            "[options]\n"
            "Architecture = auto\n"
            "SigLevel = Required DatabaseOptional\n\n"
            "[core]\n"
            "Include = /etc/pacman.d/mirrorlist\n\n"
            "[extra]\n"
            "Include = /etc/pacman.d/mirrorlist\n\n"
            "#[multilib]\n"
            "#Include = /etc/pacman.d/mirrorlist\n\n"
            "[custom-app]\n"
            "Server = https://example.com/repo/$arch\n"
        );

        {
            QFile f(confPath);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream out(&f);
            out << initialConf;
        }

        lut::RepoManager mgr;
        mgr.setForcedDistroFamily(lut::DistroFamily::Arch);
        mgr.setPacmanConfPath(confPath);

        auto repos = mgr.getRepositories();
        QCOMPARE(repos.size(), 4);

        auto findRepo = [&](const QString &id) -> const lut::RepoEntry* {
            for (const auto &r : repos) {
                if (r.id == id) return &r;
            }
            return nullptr;
        };

        const auto *core = findRepo(QStringLiteral("core"));
        QVERIFY(core != nullptr);
        QVERIFY(core->isSystem);
        QVERIFY(core->enabled);

        const auto *multilib = findRepo(QStringLiteral("multilib"));
        QVERIFY(multilib != nullptr);
        QVERIFY(!multilib->enabled);

        const auto *custom = findRepo(QStringLiteral("custom-app"));
        QVERIFY(custom != nullptr);
        QVERIFY(!custom->isSystem);
        QVERIFY(custom->enabled);

        // System-Repo darf nicht gelöscht werden
        QString err;
        QVERIFY(!mgr.removeRepository(QStringLiteral("core"), &err));
        QVERIFY(!err.isEmpty());

        // Multilib per Preset hinzufügen (einkommentieren)
        QVERIFY(mgr.addPreset(QStringLiteral("multilib"), &err));
        repos = mgr.getRepositories();
        multilib = findRepo(QStringLiteral("multilib"));
        QVERIFY(multilib != nullptr);
        QVERIFY(multilib->enabled);

        // Neues Custom-Repo hinzufügen
        lut::RepoEntry newRepo;
        newRepo.id = QStringLiteral("cidercollective");
        newRepo.name = QStringLiteral("Cider");
        newRepo.url = QStringLiteral("https://repo.cider.sh/arch");
        QVERIFY(mgr.addRepository(newRepo, &err));

        repos = mgr.getRepositories();
        const auto *cider = findRepo(QStringLiteral("cidercollective"));
        QVERIFY(cider != nullptr);
        QVERIFY(cider->enabled);

        // Custom-Repo löschen
        QVERIFY(mgr.removeRepository(QStringLiteral("custom-app"), &err));
        repos = mgr.getRepositories();
        QVERIFY(findRepo(QStringLiteral("custom-app")) == nullptr);
    }

    void testDnfRepos() {
        FedoraFixture fx;
        fx.writeRepo(QStringLiteral("custom.repo"), "[custom]\nname=Custom\nbaseurl=https://example.org/\nenabled=1\ngpgcheck=1\n");

        auto repos = fx.mgr.getRepositories();
        QCOMPARE(repos.size(), 1);
        QVERIFY(repos[0].enabled);

        QString err;
        QVERIFY2(fx.mgr.toggleRepository(QStringLiteral("custom"), false, &err), qPrintable(err));
        QVERIFY(!fx.mgr.getRepositories()[0].enabled);

        // Eigene Datei ohne Paketbesitzer wird gelöscht
        QVERIFY2(fx.mgr.removeRepository(QStringLiteral("custom"), &err), qPrintable(err));
        QCOMPARE(fx.mgr.getRepositories().size(), 0);
        QVERIFY(fx.commands.isEmpty());
    }

    // RPM Fusion schrieb früher eine .repo-Datei ohne gpgkey. Im Fedora-44-Container
    // nachgewiesen: jede Installation daraus scheiterte mit "The repository does not
    // have any OpenPGP keys configured". Jetzt: offizielles Einrichtungspaket.
    void testRpmFusionUsesReleasePackages() {
        FedoraFixture fx;
        const auto presets = fx.mgr.getPresets();
        auto find = [&](const QString &id) {
            for (const auto &p : presets) if (p.id == id) return p;
            return lut::RepoPreset{};
        };
        const QString freeUrl = QStringLiteral("https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-44.noarch.rpm");
        const QString nonfreeUrl = QStringLiteral("https://mirrors.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-44.noarch.rpm");
        QCOMPARE(find(QStringLiteral("rpmfusion-free")).setupPackages, QStringList{freeUrl});
        QCOMPARE(find(QStringLiteral("rpmfusion-nonfree")).setupPackages, (QStringList{freeUrl, nonfreeUrl}));

        QString err;
        QVERIFY2(fx.mgr.addPreset(QStringLiteral("rpmfusion-nonfree"), &err), qPrintable(err));
        QCOMPARE(fx.commands, QStringList{QStringLiteral("dnf5 install -y ") + freeUrl + QLatin1Char(' ') + nonfreeUrl});
        // Keine selbst geschriebene, schlüssellose Quelldatei mehr
        QVERIFY(!QFile::exists(fx.repoFile(QStringLiteral("rpmfusion-nonfree.repo"))));
        QVERIFY(!QFile::exists(fx.repoFile(QStringLiteral("rpmfusion-free.repo"))));

        // Fehlgeschlagene Einrichtung wird gemeldet, nicht verschluckt
        fx.exitCode = 1;
        fx.commands.clear();
        QVERIFY(!fx.mgr.addPreset(QStringLiteral("rpmfusion-free"), &err));
        QVERIFY(err.contains(QStringLiteral("RPM Fusion")));
    }

    void testTerraAndSubrepos() {
        FedoraFixture fx;
        auto ids = [&]() {
            QStringList list;
            for (const auto &p : fx.mgr.getPresets()) list.append(p.id);
            return list;
        };
        QVERIFY(ids().contains(QStringLiteral("terra")));
        // Unterquellen erst, wenn Terra eingerichtet ist
        QVERIFY(!ids().contains(QStringLiteral("terra-extras")));

        QString err;
        QVERIFY2(fx.mgr.addPreset(QStringLiteral("terra"), &err), qPrintable(err));
        QCOMPARE(fx.commands.size(), 1);
        // Genau wie in der Terra-Anleitung: --nogpgcheck nur für den ersten Bezug,
        // danach prüft terra-gpg-keys jedes Paket.
        QCOMPARE(fx.commands.first(), QStringLiteral("dnf5 install -y --nogpgcheck --repofrompath=terra,https://repos.fyralabs.com/terra$releasever terra-release terra-gpg-keys"));

        fx.writeRepo(QStringLiteral("terra.repo"), "[terra]\nname=Terra\nmetalink=https://tetsudou.fyralabs.com/metalink?repo=terra$releasever\nenabled=1\n");
        QVERIFY(ids().contains(QStringLiteral("terra-extras")));
        QVERIFY(ids().contains(QStringLiteral("terra-nvidia")));
        QVERIFY(ids().contains(QStringLiteral("terra-mesa")));

        fx.commands.clear();
        QVERIFY2(fx.mgr.addPreset(QStringLiteral("terra-nvidia"), &err), qPrintable(err));
        // Unterquellen brauchen kein --nogpgcheck: der Schlüssel ist dann schon da
        QCOMPARE(fx.commands, QStringList{QStringLiteral("dnf5 install -y terra-release-nvidia")});
    }

    void testRemovalOnlyTakesSetupPackages() {
        QVERIFY(lut::RepoManager::isRemovableSetupPackage(QStringLiteral("rpmfusion-free-release")));
        QVERIFY(lut::RepoManager::isRemovableSetupPackage(QStringLiteral("rpmfusion-nonfree-tainted-release")));
        QVERIFY(lut::RepoManager::isRemovableSetupPackage(QStringLiteral("terra-release")));
        QVERIFY(lut::RepoManager::isRemovableSetupPackage(QStringLiteral("terra-release-mesa")));
        QVERIFY(!lut::RepoManager::isRemovableSetupPackage(QStringLiteral("fedora-repos")));
        QVERIFY(!lut::RepoManager::isRemovableSetupPackage(QStringLiteral("fedora-release")));
        QVERIFY(!lut::RepoManager::isRemovableSetupPackage(QStringLiteral("dnf5")));

        FedoraFixture fx;
        fx.writeRepo(QStringLiteral("rpmfusion-free.repo"), "[rpmfusion-free]\nname=RPM Fusion\nenabled=1\n");
        fx.owners.insert(fx.repoFile(QStringLiteral("rpmfusion-free.repo")), QStringLiteral("rpmfusion-free-release"));
        QString err;
        QVERIFY2(fx.mgr.removeRepository(QStringLiteral("rpmfusion-free"), &err), qPrintable(err));
        QCOMPARE(fx.commands, QStringList{QStringLiteral("dnf5 remove -y rpmfusion-free-release")});

        // Gegenprobe: fedora-cisco-openh264.repo gehört fedora-repos. Das Paket
        // zu entfernen nähme alle Fedora-Quellen mit – es darf nur abgeschaltet werden.
        fx.commands.clear();
        const QByteArray openh264 = "[fedora-cisco-openh264]\nname=OpenH264\nmetalink=https://mirrors.fedoraproject.org/metalink?repo=fedora-cisco-openh264-$releasever\nenabled=1\ngpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-fedora-$releasever-$basearch\n";
        fx.writeRepo(QStringLiteral("fedora-cisco-openh264.repo"), openh264);
        fx.owners.insert(fx.repoFile(QStringLiteral("fedora-cisco-openh264.repo")), QStringLiteral("fedora-repos"));
        QVERIFY2(fx.mgr.removeRepository(QStringLiteral("fedora-cisco-openh264"), &err), qPrintable(err));
        QVERIFY2(fx.commands.isEmpty(), qPrintable(fx.commands.join(QLatin1Char('|'))));
        QVERIFY(QFile::exists(fx.repoFile(QStringLiteral("fedora-cisco-openh264.repo"))));
        bool found = false;
        for (const auto &r : fx.mgr.getRepositories()) {
            if (r.id == QLatin1String("fedora-cisco-openh264")) { found = true; QVERIFY(!r.enabled); }
        }
        QVERIFY(found);
    }

    // Eine vorhandene, nur abgeschaltete Quelle wird eingeschaltet statt durch
    // eine gleichnamige eigene Datei ersetzt – samt ihrem gpgkey.
    void testExistingSectionIsEnabledNotOverwritten() {
        FedoraFixture fx;
        fx.writeRepo(QStringLiteral("fedora-cisco-openh264.repo"),
                     "[fedora-cisco-openh264]\nname=OpenH264\nenabled=0\ngpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-fedora\n");
        QString err;
        QVERIFY2(fx.mgr.addPreset(QStringLiteral("fedora-cisco-openh264"), &err), qPrintable(err));
        QFile f(fx.repoFile(QStringLiteral("fedora-cisco-openh264.repo")));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(f.readAll());
        QVERIFY2(content.contains(QStringLiteral("enabled=1")), qPrintable(content));
        QVERIFY2(content.contains(QStringLiteral("gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-fedora")), qPrintable(content));
        QVERIFY(fx.commands.isEmpty());
    }

    void testUnknownFedoraVersionIsReported() {
        FedoraFixture fx("NAME=Fedora Linux\n");
        QString err;
        QVERIFY(!fx.mgr.addPreset(QStringLiteral("rpmfusion-free"), &err));
        QVERIFY2(err.contains(QStringLiteral("Fedora-Version")), qPrintable(err));
        QVERIFY(fx.commands.isEmpty());
    }

    void testAptRepos() {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());
        QString sourcesDir = tmpDir.filePath(QStringLiteral("sources.list.d"));

        lut::RepoManager mgr;
        mgr.setForcedDistroFamily(lut::DistroFamily::Debian);
        mgr.setAptSourcesDir(sourcesDir);
        mgr.setAptSourcesList(tmpDir.filePath(QStringLiteral("sources.list")));

        QString err;
        lut::RepoEntry entry;
        entry.id = QStringLiteral("my-ppa");
        entry.name = QStringLiteral("My PPA");
        entry.url = QStringLiteral("http://ppa.launchpad.net/my/ppa/ubuntu noble main");
        QVERIFY(mgr.addRepository(entry, &err));

        auto repos = mgr.getRepositories();
        QCOMPARE(repos.size(), 1);
        QCOMPARE(repos[0].id, QStringLiteral("my-ppa"));

        // Entfernen
        QVERIFY(mgr.removeRepository(QStringLiteral("my-ppa"), &err));
        repos = mgr.getRepositories();
        QCOMPARE(repos.size(), 0);
    }
    // Die Adresse einer Paketquelle wird als root in eine Konfigurationsdatei
    // geschrieben. Ein Zeilenumbruch darin würde beliebige weitere Direktiven
    // einschleusen - etwa eine abgeschaltete Signaturprüfung.
    void testConfigValuesRejectControlCharacters() {
        QVERIFY(lut::RepoManager::isSafeConfigValue(QStringLiteral("https://cdn-mirror.chaotic.cx/$repo/$arch")));
        QVERIFY(lut::RepoManager::isSafeConfigValue(QStringLiteral("deb http://deb.debian.org/debian trixie main")));

        QVERIFY(!lut::RepoManager::isSafeConfigValue(QStringLiteral("https://example.org\nSigLevel = Never")));
        QVERIFY(!lut::RepoManager::isSafeConfigValue(QStringLiteral("https://example.org\r\n[evil]")));
        QVERIFY(!lut::RepoManager::isSafeConfigValue(QStringLiteral("https://example.org") + QChar(QChar::Null)));
        QVERIFY(!lut::RepoManager::isSafeConfigValue(QString(3000, QLatin1Char('a'))));
    }

    void testInjectedServerLineIsRejected() {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());
        const QString confPath = tmpDir.filePath(QStringLiteral("pacman.conf"));
        const QString initial = QStringLiteral("[options]\nSigLevel = Required DatabaseOptional\n\n[core]\nInclude = /etc/pacman.d/mirrorlist\n");
        QFile file(confPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(initial.toUtf8());
        file.close();

        lut::RepoManager mgr;
        mgr.setForcedDistroFamily(lut::DistroFamily::Arch);
        mgr.setPacmanConfPath(confPath);

        lut::RepoEntry evil;
        evil.id = QStringLiteral("boeswillig");
        evil.backend = QStringLiteral("pacman");
        evil.url = QStringLiteral("https://example.org/$repo\nSigLevel = Never\n[untergeschoben]\nServer = http://angreifer.example/");

        QString err;
        QVERIFY2(!mgr.addRepository(evil, &err), "Eingeschleuste Direktiven wurden angenommen");
        QVERIFY(!err.isEmpty());

        // Die Datei darf unverändert geblieben sein.
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString after = QString::fromUtf8(file.readAll());
        file.close();
        QCOMPARE(after, initial);
        QVERIFY(!after.contains(QStringLiteral("SigLevel = Never")));
        QVERIFY(!after.contains(QStringLiteral("untergeschoben")));

        // Gegenprobe: eine unverdächtige Adresse muss weiterhin angenommen werden.
        lut::RepoEntry good;
        good.id = QStringLiteral("chaotic-aur");
        good.backend = QStringLiteral("pacman");
        good.url = QStringLiteral("https://cdn-mirror.chaotic.cx/$repo/$arch");
        QVERIFY2(mgr.addRepository(good, &err), qPrintable(err));
    }

    void testIncludeOnlyFromPacmanDirectory() {
        QVERIFY(lut::RepoManager::isAllowedPacmanInclude(QStringLiteral("/etc/pacman.d/mirrorlist")));
        QVERIFY(lut::RepoManager::isAllowedPacmanInclude(QStringLiteral("/etc/pacman.d/chaotic-mirrorlist")));

        // Früher genügte es, dass der Pfad irgendwo "mirrorlist" enthielt.
        QVERIFY(!lut::RepoManager::isAllowedPacmanInclude(QStringLiteral("/home/domi/mirrorlist")));
        QVERIFY(!lut::RepoManager::isAllowedPacmanInclude(QStringLiteral("/etc/pacman.d/../../tmp/mirrorlist")));
        QVERIFY(!lut::RepoManager::isAllowedPacmanInclude(QStringLiteral("/etc/pacman.d/unter/verzeichnis")));
        QVERIFY(!lut::RepoManager::isAllowedPacmanInclude(QStringLiteral("/etc/shadow")));
    }

    void testAptSourceRejectsTrustedOption() {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());
        lut::RepoManager mgr;
        mgr.setForcedDistroFamily(lut::DistroFamily::Debian);
        mgr.setAptSourcesList(tmpDir.filePath(QStringLiteral("sources.list")));
        mgr.setAptSourcesDir(tmpDir.filePath(QStringLiteral("sources.list.d")));

        lut::RepoEntry evil;
        evil.id = QStringLiteral("ohne-signatur");
        evil.backend = QStringLiteral("apt");
        evil.url = QStringLiteral("[trusted=yes] http://angreifer.example/ ./");

        QString err;
        QVERIFY2(!mgr.addRepository(evil, &err), "trusted=yes wurde in eine Paketquelle geschrieben");
        QVERIFY(!QFile::exists(tmpDir.filePath(QStringLiteral("sources.list.d/ohne-signatur.list"))));

        lut::RepoEntry good;
        good.id = QStringLiteral("backports");
        good.backend = QStringLiteral("apt");
        good.url = QStringLiteral("http://deb.debian.org/debian trixie-backports main");
        QVERIFY2(mgr.addRepository(good, &err), qPrintable(err));
    }

    // Eine bestehende Konfigurationsdatei darf zu keinem Zeitpunkt fehlen.
    void testExistingConfigSurvivesReplacement() {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());
        const QString confPath = tmpDir.filePath(QStringLiteral("pacman.conf"));
        QFile file(confPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("[options]\n\n[core]\nInclude = /etc/pacman.d/mirrorlist\n");
        file.close();
        const auto before = QFileInfo(confPath).permissions();

        lut::RepoManager mgr;
        mgr.setForcedDistroFamily(lut::DistroFamily::Arch);
        mgr.setPacmanConfPath(confPath);

        lut::RepoEntry entry;
        entry.id = QStringLiteral("multilib");
        entry.backend = QStringLiteral("pacman");
        entry.url = QStringLiteral("/etc/pacman.d/mirrorlist");

        QString err;
        QVERIFY2(mgr.addRepository(entry, &err), qPrintable(err));
        QVERIFY(QFile::exists(confPath));
        QCOMPARE(QFileInfo(confPath).permissions(), before);

        // Keine Reste des Schreibvorgangs im Verzeichnis.
        const auto leftovers = QDir(tmpDir.path()).entryList({QStringLiteral("*.tmp.*")}, QDir::Files);
        QVERIFY2(leftovers.isEmpty(), qPrintable(leftovers.join(QLatin1Char(','))));
    }


    void testPprFingerprintOfRealKey() {
        const QByteArray key = pprKey();
        QVERIFY(!key.isEmpty());
        QByteArray binary;
        QCOMPARE(lut::RepoManager::openPgpFingerprint(key, &binary), lut::RepoManager::PprFingerprint);
        QVERIFY(binary.size() > 500);
        // Binärform liefert denselben Fingerabdruck
        QCOMPARE(lut::RepoManager::openPgpFingerprint(binary), lut::RepoManager::PprFingerprint);
        // Zwei Primärschlüssel in einer Datei: apt vertraute beiden -> ablehnen
        QVERIFY(lut::RepoManager::openPgpFingerprint(binary + binary).isEmpty());
        QVERIFY(lut::RepoManager::openPgpFingerprint(QByteArray("kein Schlüssel")).isEmpty());
    }

    void testPprSetupWritesSignedSource() {
        DebianPpr fx(pprKey());
        QString err;
        QVERIFY2(fx.mgr.addPreset(QStringLiteral("pacstall"), &err), qPrintable(err));

        QFile list(fx.list());
        QVERIFY(list.open(QIODevice::ReadOnly));
        const QString line = QString::fromUtf8(list.readAll()).trimmed();
        QCOMPARE(line, QStringLiteral("deb [signed-by=%1 arch=amd64] https://ppr.pacstall.dev/pacstall/ pacstall main").arg(fx.keyring()));

        QByteArray binary;
        lut::RepoManager::openPgpFingerprint(pprKey(), &binary);
        QFile keyring(fx.keyring());
        QVERIFY(keyring.open(QIODevice::ReadOnly));
        QCOMPARE(keyring.readAll(), binary);

        // Nur die neue Quelle wird abgerufen, und zwar nach dem Schreiben
        const QString update = fx.commands.last();
        QVERIFY2(update.startsWith(QStringLiteral("apt-get update --error-on=any")), qPrintable(update));
        QVERIFY(update.contains(QStringLiteral("Dir::Etc::sourcelist=") + fx.list()));
        QVERIFY(update.contains(QStringLiteral("Dir::Etc::sourceparts=-")));

        // Anzeige ohne die Optionen in eckigen Klammern
        bool found = false;
        for (const auto &r : fx.mgr.getRepositories()) {
            if (r.id == QLatin1String("pacstall")) {
                found = true;
                QCOMPARE(r.url, QStringLiteral("https://ppr.pacstall.dev/pacstall/ pacstall main"));
            }
        }
        QVERIFY(found);

        // Entfernen nimmt den Schlüsselbund mit
        QVERIFY2(fx.mgr.removeRepository(QStringLiteral("pacstall"), &err), qPrintable(err));
        QVERIFY(!QFile::exists(fx.list()));
        QVERIFY(!QFile::exists(fx.keyring()));
    }

    void testPprSetupRejectsForeignKey() {
        QByteArray tampered = pprKey();
        // Ein Zeichen im Base64-Körper ändern: anderer Schlüssel, anderer Fingerabdruck
        const int pos = tampered.indexOf("\n\n") + 40;
        tampered[pos] = tampered.at(pos) == 'A' ? 'B' : 'A';
        DebianPpr fx(tampered);
        QString err;
        QVERIFY(!fx.mgr.addPreset(QStringLiteral("pacstall"), &err));
        QVERIFY2(err.contains(QStringLiteral("Fingerabdruck")), qPrintable(err));
        QVERIFY(!QFile::exists(fx.list()));
        QVERIFY(!QFile::exists(fx.keyring()));
        for (const auto &c : fx.commands) QVERIFY2(!c.startsWith(QStringLiteral("apt-get")), qPrintable(c));
    }

    void testPprSetupRollsBackOnAptFailure() {
        DebianPpr fx(pprKey());
        fx.aptExit = 100;
        QString err;
        QVERIFY(!fx.mgr.addPreset(QStringLiteral("pacstall"), &err));
        QVERIFY2(err.contains(QStringLiteral("not signed")), qPrintable(err));
        QVERIFY(!QFile::exists(fx.list()));
        QVERIFY(!QFile::exists(fx.keyring()));
    }

    void testThirdPartyPresetsFlagged() {
        // Arch
        {
            lut::RepoManager archMgr;
            archMgr.setForcedDistroFamily(lut::DistroFamily::Arch);
            const auto presets = archMgr.getPresets();
            auto findPreset = [&](const QString &id) {
                for (const auto &p : presets) if (p.id == id) return p;
                return lut::RepoPreset{};
            };
            const auto multi = findPreset(QStringLiteral("multilib"));
            QVERIFY(!multi.thirdParty);
            QVERIFY(multi.riskNotice.isEmpty());

            const auto chaotic = findPreset(QStringLiteral("chaotic-aur"));
            QVERIFY(chaotic.thirdParty);
            QVERIFY(!chaotic.riskNotice.isEmpty());
            QVERIFY(chaotic.riskNotice.contains(QStringLiteral("Chaotic-AUR")));

            // Json round-trip
            const auto json = chaotic.toJson();
            QCOMPARE(json.value(QStringLiteral("thirdParty")).toBool(), true);
            const auto restored = lut::RepoPreset::fromJson(json);
            QCOMPARE(restored.thirdParty, true);
            QCOMPARE(restored.riskNotice, chaotic.riskNotice);
        }

        // Fedora
        {
            FedoraFixture fx;
            const auto presets = fx.mgr.getPresets();
            auto findPreset = [&](const QString &id) {
                for (const auto &p : presets) if (p.id == id) return p;
                return lut::RepoPreset{};
            };
            const auto free = findPreset(QStringLiteral("rpmfusion-free"));
            QVERIFY(free.thirdParty);
            QVERIFY(!free.riskNotice.isEmpty());

            const auto nonfree = findPreset(QStringLiteral("rpmfusion-nonfree"));
            QVERIFY(nonfree.thirdParty);
            QVERIFY(!nonfree.riskNotice.isEmpty());

            const auto terra = findPreset(QStringLiteral("terra"));
            QVERIFY(terra.thirdParty);
            QVERIFY(!terra.riskNotice.isEmpty());
            QVERIFY(terra.riskNotice.contains(QStringLiteral("Terra kann mit RPM Fusion kollidieren")));

            const auto openh264 = findPreset(QStringLiteral("fedora-cisco-openh264"));
            QVERIFY(!openh264.thirdParty);
            QVERIFY(openh264.riskNotice.isEmpty());
        }

        // Debian
        {
            lut::RepoManager debMgr;
            debMgr.setForcedDistroFamily(lut::DistroFamily::Debian);
            const auto presets = debMgr.getPresets();
            auto findPreset = [&](const QString &id) {
                for (const auto &p : presets) if (p.id == id) return p;
                return lut::RepoPreset{};
            };
            const auto contrib = findPreset(QStringLiteral("contrib"));
            QVERIFY(!contrib.thirdParty);
            QVERIFY(contrib.riskNotice.isEmpty());

            const auto pacstall = findPreset(QStringLiteral("pacstall"));
            QVERIFY(pacstall.thirdParty);
            QVERIFY(!pacstall.riskNotice.isEmpty());
            QVERIFY(pacstall.riskNotice.contains(QStringLiteral("Pacstall")));
        }
    }
};

QTEST_MAIN(RepoManagerTest)
#include "repo_manager_test.moc"
