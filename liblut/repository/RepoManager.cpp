#include "RepoManager.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <fcntl.h>
#include <QProcess>
#include <QProcessEnvironment>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QThread>
#include <unistd.h>

namespace lut {

namespace {

bool safeWriteFile(const QString &filePath, const QString &content, QString *error) {
    QFileInfo fi(filePath);
    QDir dir = fi.dir();
    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral("."))) {
            if (error) *error = QStringLiteral("Verzeichnis konnte nicht erstellt werden: %1").arg(dir.path());
            return false;
        }
    }

    // Sicherungskopie anlegen, falls Datei bereits existiert
    if (QFile::exists(filePath)) {
        QString backupPath = filePath + QStringLiteral(".lut-backup");
        QFile::remove(backupPath);
        QFile::copy(filePath, backupPath);
    }

    QString tempPath = filePath + QStringLiteral(".tmp.%1").arg(QCoreApplication::applicationPid());
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("Temporäre Datei konnte nicht geschrieben werden: %1").arg(tempFile.errorString());
        return false;
    }

    {
        QTextStream out(&tempFile);
        out << content;
    }
    // Erst die Daten auf den Datenträger bringen, dann umbenennen. Sonst kann ein
    // Stromausfall eine leere Konfigurationsdatei hinterlassen.
    if (!tempFile.flush() || !tempFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                      | QFileDevice::ReadGroup | QFileDevice::ReadOther)) {
        if (error) *error = QStringLiteral("Temporäre Datei konnte nicht abgeschlossen werden: %1").arg(tempFile.errorString());
        tempFile.remove();
        return false;
    }
    ::fsync(tempFile.handle());
    tempFile.close();

    // Ersetzen ohne vorheriges Löschen: rename() innerhalb desselben Dateisystems
    // ist atomar. Die frühere Reihenfolge (erst entfernen, dann umbenennen) konnte
    // das System ohne /etc/pacman.conf zurücklassen, wenn dazwischen etwas schiefging.
    if (::rename(QFile::encodeName(tempPath).constData(), QFile::encodeName(filePath).constData()) != 0) {
        if (error) *error = QStringLiteral("Datei konnte nicht atomar ersetzt werden: %1").arg(filePath);
        tempFile.remove();
        return false;
    }

    // Auch das Verzeichnis dauerhaft machen, damit der neue Name einen Absturz überlebt.
    const int dirFd = ::open(QFile::encodeName(dir.path()).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dirFd >= 0) {
        ::fsync(dirFd);
        ::close(dirFd);
    }

    return true;
}

bool isSystemPacmanRepo(const QString &id) {
    QString lower = id.toLower();
    if (lower == QStringLiteral("core") || lower == QStringLiteral("extra") ||
        lower == QStringLiteral("testing") || lower == QStringLiteral("core-testing") ||
        lower == QStringLiteral("extra-testing") || lower == QStringLiteral("multilib-testing")) {
        return true;
    }
    if (lower.startsWith(QStringLiteral("cachyos"))) {
        return true;
    }
    return false;
}

bool isSystemDnfRepo(const QString &id) {
    QString lower = id.toLower();
    return lower == QStringLiteral("fedora") || lower == QStringLiteral("fedora-updates") ||
           lower == QStringLiteral("updates") || lower == QStringLiteral("fedora-modular") ||
           lower == QStringLiteral("updates-modular");
}

} // namespace

RepoManager &RepoManager::instance() {
    static RepoManager mgr;
    return mgr;
}

RepoManager::RepoManager(QObject *parent)
    : QObject(parent) {}

DistroFamily RepoManager::currentFamily() const {
    if (m_forcedFamily.has_value()) {
        return *m_forcedFamily;
    }
    if (qEnvironmentVariableIsSet("LUT_FORCE_DISTRO_FAMILY")) {
        const QString forced = qEnvironmentVariable("LUT_FORCE_DISTRO_FAMILY").toLower();
        if (forced == QLatin1String("arch")) return DistroFamily::Arch;
        if (forced == QLatin1String("fedora")) return DistroFamily::Fedora;
        if (forced == QLatin1String("debian")) return DistroFamily::Debian;
    }
    return DistroDetect::detectFamily();
}

QString RepoManager::pacmanConfPath() const {
    return m_pacmanConfPath.isEmpty() ? QStringLiteral("/etc/pacman.conf") : m_pacmanConfPath;
}

QString RepoManager::yumReposDir() const {
    return m_yumReposDir.isEmpty() ? QStringLiteral("/etc/yum.repos.d") : m_yumReposDir;
}

QString RepoManager::aptSourcesList() const {
    return m_aptSourcesList.isEmpty() ? QStringLiteral("/etc/apt/sources.list") : m_aptSourcesList;
}

QString RepoManager::aptSourcesDir() const {
    return m_aptSourcesDir.isEmpty() ? QStringLiteral("/etc/apt/sources.list.d") : m_aptSourcesDir;
}

bool RepoManager::isValidRepoId(const QString &id) {
    if (id.trimmed().isEmpty()) return false;
    static const QRegularExpression regex(QStringLiteral("^[a-zA-Z0-9_.-]+$"));
    if (!regex.match(id).hasMatch()) return false;
    if (id.contains(QStringLiteral("..")) || id.contains(QLatin1Char('/')) || id.contains(QLatin1Char('\\'))) {
        return false;
    }
    return true;
}

QList<RepoEntry> RepoManager::getRepositories() {
    switch (currentFamily()) {
    case DistroFamily::Arch:
        return getPacmanRepos();
    case DistroFamily::Fedora:
        return getDnfRepos();
    case DistroFamily::Debian:
        return getAptRepos();
    default:
        return {};
    }
}

QString RepoManager::getRepositoriesJson() {
    QJsonArray arr;
    for (const auto &repo : getRepositories()) {
        arr.append(repo.toJson());
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QList<RepoPreset> RepoManager::getPresets() {
    QList<RepoPreset> presets;
    QList<RepoEntry> currentRepos = getRepositories();
    auto isRepoPresent = [&](const QString &id) {
        for (const auto &r : currentRepos) {
            if (r.id.compare(id, Qt::CaseInsensitive) == 0 && r.enabled) return true;
        }
        return false;
    };

    switch (currentFamily()) {
    case DistroFamily::Arch: {
        RepoPreset multilib;
        multilib.id = QStringLiteral("multilib");
        multilib.name = QStringLiteral("Multilib");
        multilib.description = QStringLiteral("32-Bit Bibliotheken für Steam, Wine und 32-Bit-Spiele");
        multilib.backend = QStringLiteral("pacman");
        multilib.defaultUrl = QStringLiteral("/etc/pacman.d/mirrorlist");
        multilib.isAdded = isRepoPresent(multilib.id);
        presets.append(multilib);

        RepoPreset chaotic;
        chaotic.id = QStringLiteral("chaotic-aur");
        chaotic.name = QStringLiteral("Chaotic-AUR");
        // Der Store importiert bewusst keine Signaturschlüssel. Ohne chaotic-keyring
        // scheitert jede Installation aus dieser Quelle an der Signaturprüfung -
        // das muss vor dem Hinzufügen dranstehen, nicht erst im Fehlerfall.
        chaotic.description = QStringLiteral("Automatisch vorkompilierte Binärpakete aus dem Arch User Repository (AUR). "
                                             "Benötigt zusätzlich die Pakete chaotic-keyring und chaotic-mirrorlist; "
                                             "Signaturschlüssel werden nicht automatisch importiert.");
        chaotic.backend = QStringLiteral("pacman");
        chaotic.defaultUrl = QStringLiteral("https://cdn-mirror.chaotic.cx/$repo/$arch");
        chaotic.thirdParty = true;
        chaotic.riskNotice = QStringLiteral("Chaotic-AUR baut AUR-Pakete automatisch und verteilt sie signiert. "
                                            "Die Pakete werden von Arch Linux nicht geprüft; du vertraust den Betreibern von Chaotic-AUR.");
        chaotic.isAdded = isRepoPresent(chaotic.id);
        presets.append(chaotic);
        break;
    }
    case DistroFamily::Fedora: {
        // RPM Fusion und Terra werden über ihre offiziellen Einrichtungspakete
        // aktiviert. Eine selbst geschriebene .repo-Datei hatte keinen
        // Signaturschlüssel: jede Installation daraus scheiterte mit
        // "The repository does not have any OpenPGP keys configured".
        const QString release = fedoraVersion();
        const QString fusionBase = QStringLiteral("https://mirrors.rpmfusion.org/%1/fedora/rpmfusion-%1-release-%2.noarch.rpm");

        RepoPreset free;
        free.id = QStringLiteral("rpmfusion-free");
        free.name = QStringLiteral("RPM Fusion (Free)");
        free.description = QStringLiteral("Freie Multimedia-Codecs, Player (VLC) und zusätzliche Open-Source-Software. "
                                          "Wird über das offizielle Paket rpmfusion-free-release samt Signaturschlüssel eingerichtet.");
        free.backend = QStringLiteral("dnf5");
        free.thirdParty = true;
        free.riskNotice = QStringLiteral("Diese Paketquelle wird nicht vom Fedora-Projekt betrieben. "
                                         "Ihre Pakete werden mit ihrem eigenen Schlüssel signiert und können Fedora-Pakete ersetzen. "
                                         "Du vertraust den Betreibern der Quelle.");
        if (!release.isEmpty()) free.setupPackages = {fusionBase.arg(QStringLiteral("free"), release)};
        free.isAdded = isRepoPresent(free.id);
        presets.append(free);

        RepoPreset nonfree;
        nonfree.id = QStringLiteral("rpmfusion-nonfree");
        nonfree.name = QStringLiteral("RPM Fusion (Nonfree)");
        nonfree.description = QStringLiteral("Proprietäre Treiber (NVIDIA), Video-Codecs und unfreie Software. "
                                             "Setzt RPM Fusion (Free) voraus; beide werden gemeinsam eingerichtet.");
        nonfree.backend = QStringLiteral("dnf5");
        nonfree.thirdParty = true;
        nonfree.riskNotice = QStringLiteral("Diese Paketquelle wird nicht vom Fedora-Projekt betrieben. "
                                            "Ihre Pakete werden mit ihrem eigenen Schlüssel signiert und können Fedora-Pakete ersetzen. "
                                            "Du vertraust den Betreibern der Quelle.");
        if (!release.isEmpty()) {
            // rpmfusion-nonfree-release hängt von rpmfusion-free-release ab
            nonfree.setupPackages = {fusionBase.arg(QStringLiteral("free"), release),
                                     fusionBase.arg(QStringLiteral("nonfree"), release)};
        }
        nonfree.isAdded = isRepoPresent(nonfree.id);
        presets.append(nonfree);

        RepoPreset terra;
        terra.id = QStringLiteral("terra");
        terra.name = QStringLiteral("Terra");
        terra.description = QStringLiteral("Paketquelle von Fyra Labs mit Anwendungen, die Fedora nicht anbietet. "
                                           "Einrichtung wie vom Anbieter beschrieben über terra-release und terra-gpg-keys; "
                                           "danach wird jedes Terra-Paket gegen deren Schlüssel geprüft.");
        terra.backend = QStringLiteral("dnf5");
        terra.thirdParty = true;
        terra.riskNotice = QStringLiteral("Diese Paketquelle wird nicht vom Fedora-Projekt betrieben. "
                                          "Ihre Pakete werden mit ihrem eigenen Schlüssel signiert und können Fedora-Pakete ersetzen. "
                                          "Du vertraust den Betreibern der Quelle. Terra kann mit RPM Fusion kollidieren.");
        terra.setupRepoFromPath = QStringLiteral("terra,https://repos.fyralabs.com/terra$releasever");
        terra.setupPackages = {QStringLiteral("terra-release"), QStringLiteral("terra-gpg-keys")};
        terra.isAdded = isRepoPresent(terra.id);
        presets.append(terra);

        // Unterquellen erst anbieten, wenn Terra eingerichtet ist.
        if (terra.isAdded) {
            const struct { const char *id; const char *name; const char *description; } subrepos[] = {
                {"terra-extras", "Terra Extras", "Pakete, die mit Fedora kollidieren, etwa ein gepatchtes WINE."},
                {"terra-nvidia", "Terra NVIDIA", "NVIDIA-Treiber auf Grundlage der Negativo17-Pakete und CUDA."},
                {"terra-mesa", "Terra Mesa", "Mesa mit zusätzlichen Patches. Vorher Mesa-freeworld-Pakete von RPM Fusion entfernen."},
            };
            for (const auto &sub : subrepos) {
                RepoPreset preset;
                preset.id = QString::fromLatin1(sub.id);
                preset.name = QString::fromLatin1(sub.name);
                preset.description = QString::fromUtf8(sub.description)
                    + QStringLiteral(" Kann mit RPM Fusion oder Fedora kollidieren.");
                preset.backend = QStringLiteral("dnf5");
                preset.thirdParty = true;
                preset.riskNotice = QStringLiteral("Diese Paketquelle wird nicht vom Fedora-Projekt betrieben. "
                                                   "Ihre Pakete werden mit ihrem eigenen Schlüssel signiert und können Fedora-Pakete ersetzen. "
                                                   "Du vertraust den Betreibern der Quelle. Terra kann mit RPM Fusion kollidieren.");
                preset.setupPackages = {QStringLiteral("terra-release-") + preset.id.mid(6)};
                preset.requiresPreset = QStringLiteral("terra");
                preset.isAdded = isRepoPresent(preset.id);
                presets.append(preset);
            }
        }

        RepoPreset openh264;
        openh264.id = QStringLiteral("fedora-cisco-openh264");
        openh264.name = QStringLiteral("Fedora Cisco OpenH264");
        openh264.description = QStringLiteral("OpenH264 Video-Codec für Firefox und WebRTC Streaming");
        openh264.backend = QStringLiteral("dnf5");
        openh264.defaultUrl = QStringLiteral("https://codecs.fedoraproject.org/openh264/$releasever/$basearch/");
        openh264.isAdded = isRepoPresent(openh264.id);
        presets.append(openh264);
        break;
    }
    case DistroFamily::Debian: {
        RepoPreset contrib;
        contrib.id = QStringLiteral("contrib");
        contrib.name = QStringLiteral("Contrib");
        contrib.description = QStringLiteral("Freie Software mit Abhängigkeiten zu proprietärer Software");
        contrib.backend = QStringLiteral("apt");
        contrib.defaultUrl = QStringLiteral("contrib");
        contrib.isAdded = isRepoPresent(contrib.id);
        presets.append(contrib);

        RepoPreset nonfree;
        nonfree.id = QStringLiteral("non-free");
        nonfree.name = QStringLiteral("Non-Free");
        nonfree.description = QStringLiteral("Proprietäre Treiber und Software ohne freie Lizenz");
        nonfree.backend = QStringLiteral("apt");
        nonfree.defaultUrl = QStringLiteral("non-free");
        nonfree.isAdded = isRepoPresent(nonfree.id);
        presets.append(nonfree);

        RepoPreset firmware;
        firmware.id = QStringLiteral("non-free-firmware");
        firmware.name = QStringLiteral("Non-Free Firmware");
        firmware.description = QStringLiteral("Offizielle Hardware-Firmware für WLAN-, Grafik- und Netzwerk-Chips");
        firmware.backend = QStringLiteral("apt");
        firmware.defaultUrl = QStringLiteral("non-free-firmware");
        firmware.isAdded = isRepoPresent(firmware.id);
        presets.append(firmware);

        RepoPreset backports;
        backports.id = QStringLiteral("backports");
        backports.name = QStringLiteral("Backports");
        backports.description = QStringLiteral("Neuere Softwareversionen aus Testing angepasst für Stable");
        backports.backend = QStringLiteral("apt");
        backports.defaultUrl = QStringLiteral("backports");
        backports.isAdded = isRepoPresent(backports.id);
        presets.append(backports);

        RepoPreset pacstall;
        pacstall.id = QStringLiteral("pacstall");
        pacstall.name = QStringLiteral("Pacstall (PPR)");
        pacstall.description = QStringLiteral("Offizielles Pacstall Program Repository (PPR) zur Installation und Aktualisierung von Pacstall. "
                                              "Wird mit dem Signaturschlüssel der PPR eingerichtet (Fingerabdruck geprüft).");
        pacstall.backend = QStringLiteral("apt");
        // Keine defaultUrl: eingerichtet wird mit Schlüssel über setupPacstallRepo().
        // Eine nackte "deb …"-Zeile ohne signed-by ließe jedes "apt update" scheitern.
        pacstall.thirdParty = true;
        pacstall.riskNotice = QStringLiteral("Pacstall installiert Programme nach Bauanleitungen (Pacscripts) aus einem von Nutzern gepflegten Verzeichnis, nicht aus Debian oder Ubuntu. "
                                             "Pacscripts werden mit Administratorrechten ausgeführt. "
                                             "Der Store zeigt dir jedes Pacscript vor der Ausführung, beurteilen musst du es selbst.");
        pacstall.isAdded = isRepoPresent(pacstall.id);
        presets.append(pacstall);
        break;
    }
    default:
        break;
    }

    return presets;
}

QString RepoManager::getPresetsJson() {
    QJsonArray arr;
    for (const auto &preset : getPresets()) {
        arr.append(preset.toJson());
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

bool RepoManager::isSafeConfigValue(const QString &value) {
    if (value.size() > 2048) return false;
    for (const QChar c : value) {
        // Steuerzeichen einschließlich Zeilenumbruch, Wagenrücklauf und Null-Byte.
        if (c.unicode() < 0x20 || c.unicode() == 0x7f) return false;
    }
    return true;
}

bool RepoManager::isAllowedPacmanInclude(const QString &path) {
    if (!isSafeConfigValue(path)) return false;
    if (!path.startsWith(QStringLiteral("/etc/pacman.d/"))) return false;
    if (path.contains(QStringLiteral(".."))) return false;
    // Kein zweiter Pfadbestandteil und keine Sonderzeichen im Dateinamen.
    const QString name = path.mid(QStringLiteral("/etc/pacman.d/").size());
    static const QRegularExpression fileName(QStringLiteral("^[A-Za-z0-9_.-]+$"));
    return fileName.match(name).hasMatch();
}

bool RepoManager::addRepository(const RepoEntry &entry, QString *error) {
    if (!isValidRepoId(entry.id)) {
        if (error) *error = QStringLiteral("Ungültige Repository-ID: %1").arg(entry.id);
        return false;
    }
    // Die Adresse und der Anzeigename landen unverändert in einer Konfigurationsdatei,
    // die als root geschrieben wird. Steuerzeichen würden dort zu eigenen Direktiven.
    if (!isSafeConfigValue(entry.url)) {
        if (error) *error = QStringLiteral("Unzulässige Zeichen in der Repository-Adresse.");
        return false;
    }
    if (!isSafeConfigValue(entry.name)) {
        if (error) *error = QStringLiteral("Unzulässige Zeichen im Repository-Namen.");
        return false;
    }
    if (entry.url.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("Die Repository-Adresse fehlt.");
        return false;
    }

    bool ok = false;
    switch (currentFamily()) {
    case DistroFamily::Arch:
        ok = addPacmanRepo(entry, error);
        break;
    case DistroFamily::Fedora:
        ok = addDnfRepo(entry, error);
        break;
    case DistroFamily::Debian:
        ok = addAptRepo(entry, error);
        break;
    default:
        if (error) *error = QStringLiteral("Nicht unterstützte Distribution");
        return false;
    }

    if (ok) emit repositoriesChanged();
    return ok;
}

bool RepoManager::addRepositoryFromJson(const QString &jsonStr, QString *error) {
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("Ungültiges JSON-Format: %1").arg(parseErr.errorString());
        return false;
    }
    RepoEntry entry = RepoEntry::fromJson(doc.object());
    return addRepository(entry, error);
}

QString RepoManager::fedoraVersion() const {
    QFile file(m_osReleasePath.isEmpty() ? QStringLiteral("/etc/os-release") : m_osReleasePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    static const QRegularExpression pattern(QStringLiteral("^VERSION_ID=\"?([0-9]+)\"?\\s*$"));
    for (const QString &line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
        const auto match = pattern.match(line.trimmed());
        if (match.hasMatch()) return match.captured(1);
    }
    return {};
}

// Offizielle Einrichtung laut https://pacstall.dev/q/ppr (Schritte 3 und 4),
// Stand 24.09.2026. Der Fingerabdruck ist fest verdrahtet: Ein über HTTPS
// ausgetauschter Schlüssel wird so abgelehnt statt still vertraut.
const QString RepoManager::PprKeyUrl = QStringLiteral("https://ppr.pacstall.dev/ppr-public-key.asc");
const QString RepoManager::PprFingerprint = QStringLiteral("9230DDFB1ABA144D4AB7FDDFB2490BBE624005C2");
const QString RepoManager::PprUri = QStringLiteral("https://ppr.pacstall.dev/pacstall/");

QString RepoManager::aptKeyringDir() const {
    return m_aptKeyringDir.isEmpty() ? QStringLiteral("/etc/apt/keyrings") : m_aptKeyringDir;
}

QString RepoManager::openPgpFingerprint(const QByteArray &key, QByteArray *binaryOut) {
    QByteArray binary = key;
    if (key.trimmed().startsWith("-----BEGIN PGP PUBLIC KEY BLOCK-----")) {
        // Kopfzeilen bis zur Leerzeile überspringen, dann Base64 bis zur
        // Prüfsummenzeile ("=XXXX") oder zum Ende des Blocks.
        const QList<QByteArray> lines = key.split('\n');
        QByteArray base64;
        bool inBody = false;
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            if (line.startsWith("-----END")) break;
            if (!inBody) {
                if (line.isEmpty()) inBody = true;
                continue;
            }
            if (line.startsWith('=')) break;
            base64 += line;
        }
        binary = QByteArray::fromBase64(base64);
    }

    QString fingerprint;
    int primaryKeys = 0;
    int pos = 0;
    while (pos < binary.size()) {
        const auto header = static_cast<quint8>(binary.at(pos));
        if (!(header & 0x80)) return {};
        int tag = 0;
        qint64 length = 0;
        int headerLength = 1;
        const auto byteAt = [&](int offset) -> int {
            return pos + offset < binary.size() ? static_cast<quint8>(binary.at(pos + offset)) : -1;
        };
        if (header & 0x40) {
            tag = header & 0x3f;
            const int first = byteAt(1);
            if (first < 0) return {};
            if (first < 192) { length = first; headerLength = 2; }
            else if (first < 224) {
                const int second = byteAt(2);
                if (second < 0) return {};
                length = ((first - 192) << 8) + second + 192; headerLength = 3;
            } else if (first == 255) {
                length = 0;
                for (int i = 2; i < 6; ++i) { const int b = byteAt(i); if (b < 0) return {}; length = (length << 8) | b; }
                headerLength = 6;
            } else {
                return {}; // Teillängen kommen in Schlüsseldateien nicht vor
            }
        } else {
            tag = (header >> 2) & 0x0f;
            const int lengthType = header & 0x03;
            const int bytes = lengthType == 0 ? 1 : lengthType == 1 ? 2 : lengthType == 2 ? 4 : 0;
            if (bytes == 0) return {};
            for (int i = 1; i <= bytes; ++i) { const int b = byteAt(i); if (b < 0) return {}; length = (length << 8) | b; }
            headerLength = 1 + bytes;
        }
        if (length < 0 || pos + headerLength + length > binary.size()) return {};
        const QByteArray body = binary.mid(pos + headerLength, static_cast<int>(length));
        if (tag == 6) {
            ++primaryKeys;
            if (body.isEmpty() || static_cast<quint8>(body.at(0)) != 4 || length > 0xffff) return {};
            QByteArray material;
            material.append(static_cast<char>(0x99));
            material.append(static_cast<char>((length >> 8) & 0xff));
            material.append(static_cast<char>(length & 0xff));
            material.append(body);
            fingerprint = QString::fromLatin1(QCryptographicHash::hash(material, QCryptographicHash::Sha1).toHex()).toUpper();
        }
        pos += headerLength + static_cast<int>(length);
    }
    // Ein zweiter Primärschlüssel in derselben Datei würde von apt ebenfalls
    // als vertrauenswürdig behandelt – solche Dateien werden abgelehnt.
    if (primaryKeys != 1) return {};
    if (binaryOut) *binaryOut = binary;
    return fingerprint;
}

QByteArray RepoManager::download(const QUrl &url, QString *error) const {
    if (url.scheme() != QLatin1String("https")) {
        if (error) *error = QStringLiteral("Nur HTTPS ist zulässig: %1").arg(url.toString());
        return {};
    }
    if (m_downloader) return m_downloader(url, error);

    // Eigener Thread mit eigener Ereignisschleife: RepoManager läuft im Daemon
    // innerhalb eines D-Bus-Aufrufs, eine verschachtelte Schleife im Hauptthread
    // würde dort weitere Aufrufe mitten hinein ausführen.
    QByteArray data;
    QString failure;
    QThread *worker = QThread::create([&url, &data, &failure] {
        constexpr qint64 limit = 64 * 1024;
        QNetworkAccessManager nam;
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setTransferTimeout(30000);
        QNetworkReply *reply = nam.get(request);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
            if (received > limit) reply->abort();
        });
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        if (reply->error() != QNetworkReply::NoError) failure = reply->errorString();
        else data = reply->readAll();
        if (data.size() > limit) { data.clear(); failure = QStringLiteral("Datei zu groß"); }
        delete reply;
    });
    worker->start();
    worker->wait();
    delete worker;
    if (!failure.isEmpty() && error) *error = failure;
    return failure.isEmpty() ? data : QByteArray();
}

bool RepoManager::setupPacstallRepo(QString *error) {
    QString output;
    if (runCommand(QStringLiteral("dpkg"), {QStringLiteral("--print-architecture")}, &output) != 0) {
        if (error) *error = QStringLiteral("Die Systemarchitektur konnte nicht ermittelt werden.");
        return false;
    }
    const QString arch = output.trimmed();
    if (arch != QLatin1String("amd64") && arch != QLatin1String("arm64")) {
        if (error) *error = QStringLiteral("Die Pacstall-PPR gibt es nur für amd64 und arm64, nicht für %1.").arg(arch);
        return false;
    }

    QString downloadError;
    const QByteArray armored = download(QUrl(PprKeyUrl), &downloadError);
    if (armored.isEmpty()) {
        if (error) *error = QStringLiteral("Der Schlüssel der Pacstall-PPR konnte nicht geladen werden: %1").arg(downloadError);
        return false;
    }
    QByteArray keyring;
    const QString fingerprint = openPgpFingerprint(armored, &keyring);
    if (fingerprint != PprFingerprint) {
        if (error) *error = QStringLiteral("Der geladene PPR-Schlüssel hat nicht den erwarteten Fingerabdruck (%1). Einrichtung abgebrochen.")
                               .arg(fingerprint.isEmpty() ? QStringLiteral("unlesbar") : fingerprint);
        return false;
    }

    const QString keyringPath = QDir(aptKeyringDir()).filePath(QStringLiteral("ppr-keyring.gpg"));
    const QString listPath = QDir(aptSourcesDir()).filePath(QStringLiteral("pacstall.list"));
    QDir().mkpath(aptKeyringDir());
    QSaveFile keyFile(keyringPath);
    if (!keyFile.open(QIODevice::WriteOnly) || keyFile.write(keyring) != keyring.size() || !keyFile.commit()) {
        if (error) *error = QStringLiteral("Schlüsselbund konnte nicht geschrieben werden: %1").arg(keyringPath);
        return false;
    }
    QFile::setPermissions(keyringPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);

    // Nur die Komponente "main": dort liegt pacstall selbst. Die vorgebauten
    // Pakete der Distributions-Komponenten würden sonst als Kandidaten für
    // beliebige Pakete neben Debian/Ubuntu treten.
    const QString line = QStringLiteral("deb [signed-by=%1 arch=%2] %3 pacstall main\n").arg(keyringPath, arch, PprUri);
    const auto rollback = [&] { QFile::remove(listPath); QFile::remove(keyringPath); };
    if (!safeWriteFile(listPath, line, error)) {
        rollback();
        return false;
    }

    // Nur die neue Quelle abrufen: prüft Signatur und Erreichbarkeit sofort,
    // statt beim nächsten Systemupdate alle Quellen scheitern zu lassen.
    const int code = runCommand(QStringLiteral("apt-get"),
        {QStringLiteral("update"), QStringLiteral("--error-on=any"),
         QStringLiteral("-o"), QStringLiteral("Dir::Etc::sourcelist=%1").arg(listPath),
         QStringLiteral("-o"), QStringLiteral("Dir::Etc::sourceparts=-"),
         QStringLiteral("-o"), QStringLiteral("APT::Get::List-Cleanup=0")}, &output);
    if (code != 0) {
        rollback();
        const QStringList lines = output.trimmed().split(QLatin1Char('\n'));
        if (error) *error = QStringLiteral("Die Pacstall-PPR ließ sich nicht abrufen: %1").arg(lines.isEmpty() ? QString() : lines.last().trimmed());
        return false;
    }
    emit repositoriesChanged();
    return true;
}

int RepoManager::runCommand(const QString &program, const QStringList &args, QString *output) const {
    if (m_runner) return m_runner(program, args, output);
    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process.setProcessEnvironment(env);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, args);
    process.closeWriteChannel();
    // Einrichtungspakete laden Metadaten nach; das darf dauern.
    if (!process.waitForFinished(15 * 60 * 1000)) {
        process.kill();
        process.waitForFinished();
        if (output) *output = QStringLiteral("Zeitüberschreitung");
        return -1;
    }
    if (output) *output = QString::fromUtf8(process.readAll());
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
}

bool RepoManager::installSetupPackages(const RepoPreset &preset, QString *error) {
    if (preset.setupPackages.isEmpty()) {
        if (error) *error = QStringLiteral("Die Fedora-Version konnte nicht ermittelt werden.");
        return false;
    }
    QStringList args{QStringLiteral("install"), QStringLiteral("-y")};
    if (!preset.setupRepoFromPath.isEmpty()) {
        // Vom Anbieter so vorgesehen: der erste Bezug kann noch nicht geprüft
        // werden, weil der Schlüssel erst mit diesem Paket kommt. HTTPS sichert
        // den Transport; alle späteren Pakete werden gegen den Schlüssel geprüft.
        args << QStringLiteral("--nogpgcheck")
             << QStringLiteral("--repofrompath=%1").arg(preset.setupRepoFromPath);
    }
    args << preset.setupPackages;

    QString output;
    const int code = runCommand(QStringLiteral("dnf5"), args, &output);
    if (code != 0) {
        const QStringList lines = output.trimmed().split(QLatin1Char('\n'));
        if (error) *error = QStringLiteral("Einrichtung von %1 fehlgeschlagen: %2")
                               .arg(preset.name, lines.isEmpty() ? QString() : lines.last().trimmed());
        return false;
    }
    emit repositoriesChanged();
    return true;
}

bool RepoManager::addPreset(const QString &presetId, QString *error) {
    for (const auto &preset : getPresets()) {
        if (preset.id == presetId) {
            // Gibt es die Quelle schon (nur abgeschaltet), wird sie eingeschaltet.
            // Sonst würde eine gleichnamige Datei die Fassung der Distribution
            // überschreiben – etwa fedora-cisco-openh264.repo samt Schlüssel.
            for (const auto &existing : getRepositories()) {
                if (existing.id == preset.id) {
                    return existing.enabled ? true : toggleRepository(preset.id, true, error);
                }
            }
            if (currentFamily() == DistroFamily::Debian && preset.id == QLatin1String("pacstall")) {
                return setupPacstallRepo(error);
            }
            if (!preset.setupPackages.isEmpty() || (currentFamily() == DistroFamily::Fedora && preset.defaultUrl.isEmpty())) {
                return installSetupPackages(preset, error);
            }
            RepoEntry entry;
            entry.id = preset.id;
            entry.name = preset.name;
            entry.description = preset.description;
            entry.url = preset.defaultUrl;
            entry.enabled = true;
            entry.backend = preset.backend;
            return addRepository(entry, error);
        }
    }
    if (error) *error = QStringLiteral("Preset nicht gefunden: %1").arg(presetId);
    return false;
}

bool RepoManager::removeRepository(const QString &repoId, QString *error) {
    if (!isValidRepoId(repoId)) {
        if (error) *error = QStringLiteral("Ungültige Repository-ID: %1").arg(repoId);
        return false;
    }

    bool ok = false;
    switch (currentFamily()) {
    case DistroFamily::Arch:
        ok = removePacmanRepo(repoId, error);
        break;
    case DistroFamily::Fedora:
        ok = removeDnfRepo(repoId, error);
        break;
    case DistroFamily::Debian:
        ok = removeAptRepo(repoId, error);
        break;
    default:
        if (error) *error = QStringLiteral("Nicht unterstützte Distribution");
        return false;
    }

    if (ok) emit repositoriesChanged();
    return ok;
}

bool RepoManager::toggleRepository(const QString &repoId, bool enable, QString *error) {
    if (!isValidRepoId(repoId)) {
        if (error) *error = QStringLiteral("Ungültige Repository-ID: %1").arg(repoId);
        return false;
    }

    bool ok = false;
    switch (currentFamily()) {
    case DistroFamily::Arch:
        ok = togglePacmanRepo(repoId, enable, error);
        break;
    case DistroFamily::Fedora:
        ok = toggleDnfRepo(repoId, enable, error);
        break;
    case DistroFamily::Debian:
        ok = toggleAptRepo(repoId, enable, error);
        break;
    default:
        if (error) *error = QStringLiteral("Nicht unterstützte Distribution");
        return false;
    }

    if (ok) emit repositoriesChanged();
    return ok;
}

// =========================================================================
// Arch Linux (Pacman)
// =========================================================================

QList<RepoEntry> RepoManager::getPacmanRepos() {
    QList<RepoEntry> list;
    QFile file(pacmanConfPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return list;
    }

    QTextStream in(&file);
    static const QRegularExpression activeSectionRegex(QStringLiteral(R"(^\s*\[([a-zA-Z0-9_.-]+)\]\s*$)"));
    static const QRegularExpression commentedSectionRegex(QStringLiteral(R"(^\s*#\s*\[([a-zA-Z0-9_.-]+)\]\s*$)"));

    RepoEntry currentEntry;
    bool inSection = false;

    auto finishEntry = [&]() {
        if (inSection && !currentEntry.id.isEmpty() && currentEntry.id != QStringLiteral("options")
            && currentEntry.id != QStringLiteral("repo-name") && currentEntry.url != QStringLiteral("ServerName")) {
            currentEntry.isSystem = isSystemPacmanRepo(currentEntry.id);
            currentEntry.backend = QStringLiteral("pacman");
            currentEntry.filePath = pacmanConfPath();
            list.append(currentEntry);
        }
        currentEntry = RepoEntry();
        inSection = false;
    };

    while (!in.atEnd()) {
        QString line = in.readLine();
        QString trimmed = line.trimmed();

        auto matchActive = activeSectionRegex.match(trimmed);
        auto matchCommented = commentedSectionRegex.match(trimmed);

        if (matchActive.hasMatch()) {
            finishEntry();
            QString name = matchActive.captured(1);
            if (name != QStringLiteral("options")) {
                currentEntry.id = name;
                currentEntry.name = name;
                currentEntry.enabled = true;
                inSection = true;
            }
            continue;
        }

        if (matchCommented.hasMatch()) {
            finishEntry();
            QString name = matchCommented.captured(1);
            if (name != QStringLiteral("options")) {
                currentEntry.id = name;
                currentEntry.name = name;
                currentEntry.enabled = false;
                inSection = true;
            }
            continue;
        }

        if (inSection) {
            // Include, Server oder SigLevel parsen
            QString cleanLine = trimmed;
            if (cleanLine.startsWith(QLatin1Char('#'))) cleanLine = cleanLine.mid(1).trimmed();

            if (cleanLine.startsWith(QStringLiteral("Server"), Qt::CaseInsensitive)) {
                int eq = cleanLine.indexOf(QLatin1Char('='));
                if (eq != -1 && currentEntry.url.isEmpty()) {
                    currentEntry.url = cleanLine.mid(eq + 1).trimmed();
                }
            } else if (cleanLine.startsWith(QStringLiteral("Include"), Qt::CaseInsensitive)) {
                int eq = cleanLine.indexOf(QLatin1Char('='));
                if (eq != -1 && currentEntry.url.isEmpty()) {
                    currentEntry.url = cleanLine.mid(eq + 1).trimmed();
                }
            }
        }
    }
    finishEntry();

    return list;
}

bool RepoManager::addPacmanRepo(const RepoEntry &entry, QString *error) {
    QString path = pacmanConfPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Konnte %1 nicht lesen: %2").arg(path, file.errorString());
        return false;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    // 1. Prüfen, ob Sektion bereits auskommentiert existiert (z. B. #[multilib])
    QRegularExpression commentedRegex(QStringLiteral(R"raw((^|\n)\s*#\s*\[%1\]([\s\S]*?)(?=(\n\s*\[|\n\s*#\s*\[|$)))raw").arg(QRegularExpression::escape(entry.id)));
    auto match = commentedRegex.match(content);

    if (match.hasMatch()) {
        // Uncomment existing section
        QString sectionBlock = match.captured(0);
        QStringList lines = sectionBlock.split(QLatin1Char('\n'));
        QStringList newLines;
        for (QString line : lines) {
            QString trimmed = line.trimmed();
            if (trimmed.startsWith(QLatin1Char('#'))) {
                QString uncommented = line;
                int hashIdx = uncommented.indexOf(QLatin1Char('#'));
                if (hashIdx != -1) {
                    uncommented.remove(hashIdx, 1);
                    if (uncommented.startsWith(QLatin1Char(' '))) uncommented.remove(0, 1);
                }
                newLines.append(uncommented);
            } else {
                newLines.append(line);
            }
        }
        content.replace(match.capturedStart(0), match.capturedLength(0), newLines.join(QLatin1Char('\n')));
    } else {
        // Prüfen, ob die Sektion bereits aktiv existiert
        QRegularExpression activeRegex(QStringLiteral(R"((^|\n)\s*\[%1\])").arg(QRegularExpression::escape(entry.id)));
        if (activeRegex.match(content).hasMatch()) {
            // Bereits aktiv
            return true;
        }

        // An das Ende anfügen
        if (!content.endsWith(QLatin1Char('\n'))) content += QLatin1Char('\n');
        content += QStringLiteral("\n[%1]\n").arg(entry.id);
        content += QStringLiteral("# Added by Linux Universal App Center\n");
        content += QStringLiteral("SigLevel = Required DatabaseOptional\n");
        // Include nur für echte Pfade unterhalb von /etc/pacman.d/. Früher genügte
        // es, dass die Adresse irgendwo "mirrorlist" enthielt - damit ließ sich eine
        // beliebige Datei in die pacman.conf einbinden.
        if (entry.url.startsWith(QLatin1Char('/'))) {
            if (!isAllowedPacmanInclude(entry.url)) {
                if (error) *error = QStringLiteral("Nur Include-Dateien unterhalb von /etc/pacman.d/ sind zulässig.");
                return false;
            }
            content += QStringLiteral("Include = %1\n").arg(entry.url);
        } else {
            content += QStringLiteral("Server = %1\n").arg(entry.url);
        }
    }

    return safeWriteFile(path, content, error);
}

bool RepoManager::removePacmanRepo(const QString &repoId, QString *error) {
    if (isSystemPacmanRepo(repoId)) {
        if (error) *error = QStringLiteral("System-Repository '%1' ist geschützt und kann nicht entfernt werden.").arg(repoId);
        return false;
    }

    QString path = pacmanConfPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Konnte %1 nicht lesen: %2").arg(path, file.errorString());
        return false;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    if (repoId.compare(QStringLiteral("multilib"), Qt::CaseInsensitive) == 0) {
        // Bei multilib auskommentieren statt löschen
        return togglePacmanRepo(repoId, false, error);
    }

    // Für custom repos: Sektion aktiv oder auskommentiert komplett entfernen
    QRegularExpression secRegex(QStringLiteral(R"raw((^|\n)\s*#?\s*\[%1\][\s\S]*?(?=(\n\s*\[|\n\s*#\s*\[|$)))raw").arg(QRegularExpression::escape(repoId)));
    if (!secRegex.match(content).hasMatch()) {
        return true; // Bereits nicht vorhanden
    }

    content.replace(secRegex, QStringLiteral(""));
    return safeWriteFile(path, content, error);
}

bool RepoManager::togglePacmanRepo(const QString &repoId, bool enable, QString *error) {
    QString path = pacmanConfPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Konnte %1 nicht lesen: %2").arg(path, file.errorString());
        return false;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    if (enable) {
        // Falls auskommentiert, einkommentieren
        QRegularExpression commRegex(QStringLiteral(R"raw((^|\n)\s*#\s*\[%1\]([\s\S]*?)(?=(\n\s*\[|\n\s*#\s*\[|$)))raw").arg(QRegularExpression::escape(repoId)));
        auto match = commRegex.match(content);
        if (match.hasMatch()) {
            QString sectionBlock = match.captured(0);
            QStringList lines = sectionBlock.split(QLatin1Char('\n'));
            QStringList newLines;
            for (QString line : lines) {
                int hashIdx = line.indexOf(QLatin1Char('#'));
                if (hashIdx != -1) {
                    line.remove(hashIdx, 1);
                    if (line.startsWith(QLatin1Char(' '))) line.remove(0, 1);
                }
                newLines.append(line);
            }
            content.replace(match.capturedStart(0), match.capturedLength(0), newLines.join(QLatin1Char('\n')));
            return safeWriteFile(path, content, error);
        }
        return true;
    } else {
        if (isSystemPacmanRepo(repoId)) {
            if (error) *error = QStringLiteral("System-Repository '%1' kann nicht deaktiviert werden.").arg(repoId);
            return false;
        }

        // Aktivierte Sektion auskommentieren
        QRegularExpression actRegex(QStringLiteral(R"raw((^|\n)\s*\[%1\]([\s\S]*?)(?=(\n\s*\[|\n\s*#\s*\[|$)))raw").arg(QRegularExpression::escape(repoId)));
        auto match = actRegex.match(content);
        if (match.hasMatch()) {
            QString sectionBlock = match.captured(0);
            QStringList lines = sectionBlock.split(QLatin1Char('\n'));
            QStringList newLines;
            for (QString line : lines) {
                if (!line.trimmed().isEmpty()) {
                    newLines.append(QStringLiteral("#") + line);
                } else {
                    newLines.append(line);
                }
            }
            content.replace(match.capturedStart(0), match.capturedLength(0), newLines.join(QLatin1Char('\n')));
            return safeWriteFile(path, content, error);
        }
        return true;
    }
}

// =========================================================================
// Fedora (DNF5)
// =========================================================================

QList<RepoEntry> RepoManager::getDnfRepos() {
    QList<RepoEntry> list;
    QDir dir(yumReposDir());
    if (!dir.exists()) return list;

    QStringList files = dir.entryList(QStringList() << QStringLiteral("*.repo"), QDir::Files);
    for (const QString &fileName : files) {
        QString fullPath = dir.filePath(fileName);
        QFile file(fullPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

        QTextStream in(&file);
        RepoEntry current;
        bool inSec = false;

        auto finish = [&]() {
            if (inSec && !current.id.isEmpty()) {
                current.isSystem = isSystemDnfRepo(current.id);
                current.backend = QStringLiteral("dnf5");
                current.filePath = fullPath;
                list.append(current);
            }
            current = RepoEntry();
            inSec = false;
        };

        static const QRegularExpression secRegex(QStringLiteral(R"(^\s*\[([a-zA-Z0-9_.-]+)\]\s*$)"));

        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            auto match = secRegex.match(line);
            if (match.hasMatch()) {
                finish();
                current.id = match.captured(1);
                current.name = current.id;
                current.enabled = true;
                inSec = true;
                continue;
            }

            if (inSec) {
                if (line.startsWith(QStringLiteral("name="), Qt::CaseInsensitive)) {
                    current.name = line.mid(5).trimmed();
                } else if (line.startsWith(QStringLiteral("baseurl="), Qt::CaseInsensitive)) {
                    if (current.url.isEmpty()) current.url = line.mid(8).trimmed();
                } else if (line.startsWith(QStringLiteral("metalink="), Qt::CaseInsensitive)) {
                    current.url = line.mid(9).trimmed();
                } else if (line.startsWith(QStringLiteral("enabled="), Qt::CaseInsensitive)) {
                    current.enabled = (line.mid(8).trimmed() == QStringLiteral("1"));
                }
            }
        }
        finish();
    }

    return list;
}

bool RepoManager::addDnfRepo(const RepoEntry &entry, QString *error) {
    QDir dir(yumReposDir());
    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral("."))) {
            if (error) *error = QStringLiteral("Verzeichnis %1 konnte nicht erstellt werden").arg(dir.path());
            return false;
        }
    }

    QString repoFile = dir.filePath(QStringLiteral("%1.repo").arg(entry.id));
    QString content;
    content += QStringLiteral("[%1]\n").arg(entry.id);
    content += QStringLiteral("name=%1\n").arg(entry.name.isEmpty() ? entry.id : entry.name);
    if (entry.url.contains(QStringLiteral("metalink"))) {
        content += QStringLiteral("metalink=%1\n").arg(entry.url);
    } else {
        content += QStringLiteral("baseurl=%1\n").arg(entry.url);
    }
    content += QStringLiteral("enabled=%1\n").arg(entry.enabled ? 1 : 0);
    content += QStringLiteral("gpgcheck=1\n");

    return safeWriteFile(repoFile, content, error);
}

bool RepoManager::isRemovableSetupPackage(const QString &name) {
    // Nur Einrichtungspakete von Drittanbietern. Andere Quelldateien gehören
    // Systempaketen – fedora-cisco-openh264.repo etwa gehört fedora-repos;
    // dieses Paket zu entfernen würde alle Fedora-Paketquellen mitnehmen.
    static const QRegularExpression pattern(QStringLiteral(
        "^(rpmfusion-(free|nonfree)(-tainted)?-release|terra-release(-[a-z0-9]+)?)$"));
    return pattern.match(name).hasMatch();
}

bool RepoManager::removeOwnedRepoFile(const QString &filePath, const QString &repoId, bool *handled, QString *error) {
    *handled = false;
    QString owner;
    if (runCommand(QStringLiteral("rpm"), {QStringLiteral("-qf"), QStringLiteral("--qf"), QStringLiteral("%{NAME}"), filePath}, &owner) != 0) {
        return false; // gehört keinem Paket: eigene Datei, die gelöscht werden darf
    }
    owner = owner.trimmed();
    *handled = true;

    if (!isRemovableSetupPackage(owner)) {
        // Datei eines Systempakets: nicht löschen, nur abschalten.
        return toggleDnfRepo(repoId, false, error);
    }

    QString output;
    if (runCommand(QStringLiteral("dnf5"), {QStringLiteral("remove"), QStringLiteral("-y"), owner}, &output) != 0) {
        const QStringList lines = output.trimmed().split(QLatin1Char('\n'));
        if (error) *error = QStringLiteral("%1 konnte nicht entfernt werden: %2").arg(owner, lines.isEmpty() ? QString() : lines.last().trimmed());
        return false;
    }
    return true;
}

bool RepoManager::removeDnfRepo(const QString &repoId, QString *error) {
    if (isSystemDnfRepo(repoId)) {
        if (error) *error = QStringLiteral("System-Repository '%1' ist geschützt und kann nicht entfernt werden.").arg(repoId);
        return false;
    }

    // Stammt die Quelle aus einem Einrichtungspaket (RPM Fusion, Terra), wird
    // das Paket entfernt. Nur die Datei zu löschen hinterließe ein Paket, das
    // sie beim nächsten Update wieder anlegt.
    for (const auto &existing : getDnfRepos()) {
        if (existing.id != repoId || existing.filePath.isEmpty()) continue;
        bool handled = false;
        const bool ok = removeOwnedRepoFile(existing.filePath, repoId, &handled, error);
        if (handled) return ok;
        break;
    }

    QDir dir(yumReposDir());
    QString repoFile = dir.filePath(QStringLiteral("%1.repo").arg(repoId));
    if (QFile::exists(repoFile)) {
        if (!QFile::remove(repoFile)) {
            if (error) *error = QStringLiteral("Konnte Datei nicht entfernen: %1").arg(repoFile);
            return false;
        }
        return true;
    }

    // Wenn nicht als eigene Datei vorhanden, durchsuche alle .repo-Dateien und deaktiviere die Sektion
    return toggleDnfRepo(repoId, false, error);
}

bool RepoManager::toggleDnfRepo(const QString &repoId, bool enable, QString *error) {
    if (!enable && isSystemDnfRepo(repoId)) {
        if (error) *error = QStringLiteral("System-Repository '%1' kann nicht deaktiviert werden.").arg(repoId);
        return false;
    }

    QDir dir(yumReposDir());
    QStringList files = dir.entryList(QStringList() << QStringLiteral("*.repo"), QDir::Files);

    for (const QString &fileName : files) {
        QString fullPath = dir.filePath(fileName);
        QFile file(fullPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QString content = QString::fromUtf8(file.readAll());
        file.close();

        QRegularExpression secRegex(QStringLiteral(R"raw((^|\n)\s*\[%1\][\s\S]*?(?=(\n\s*\[|$)))raw").arg(QRegularExpression::escape(repoId)));
        auto match = secRegex.match(content);
        if (match.hasMatch()) {
            QString secBlock = match.captured(0);
            if (secBlock.contains(QStringLiteral("enabled="))) {
                secBlock.replace(QRegularExpression(QStringLiteral(R"(enabled=\s*[01])")), QStringLiteral("enabled=%1").arg(enable ? 1 : 0));
            } else {
                secBlock += QStringLiteral("\nenabled=%1").arg(enable ? 1 : 0);
            }
            content.replace(match.capturedStart(0), match.capturedLength(0), secBlock);
            return safeWriteFile(fullPath, content, error);
        }
    }

    return true;
}

// =========================================================================
// Debian / Ubuntu (APT)
// =========================================================================

QList<RepoEntry> RepoManager::getAptRepos() {
    QList<RepoEntry> list;

    auto parseListFile = [&](const QString &filePath) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
            if (!line.startsWith(QStringLiteral("deb "))) continue;

            QStringList tokens = line.split(QRegularExpression(QStringLiteral(R"(\s+)")), Qt::SkipEmptyParts);
            // Optionen wie "[signed-by=… arch=amd64]" gehören nicht zur Adresse.
            if (tokens.size() > 1 && tokens.at(1).startsWith(QLatin1Char('['))) {
                int end = 1;
                while (end < tokens.size() && !tokens.at(end).endsWith(QLatin1Char(']'))) ++end;
                tokens.erase(tokens.begin() + 1, tokens.begin() + std::min<int>(end + 1, tokens.size()));
            }
            if (tokens.size() >= 3) {
                RepoEntry entry;
                QString uri = tokens[1];
                QString suite = tokens[2];
                QString components;
                for (int i = 3; i < tokens.size(); ++i) {
                    if (!components.isEmpty()) components += QLatin1Char(' ');
                    components += tokens[i];
                }

                QFileInfo fi(filePath);
                entry.id = fi.baseName();
                if (entry.id == QStringLiteral("sources")) {
                    entry.id = suite;
                }
                entry.name = QStringLiteral("%1 (%2)").arg(entry.id, suite);
                entry.url = QStringLiteral("%1 %2 %3").arg(uri, suite, components).trimmed();
                entry.enabled = true;
                entry.isSystem = components.contains(QStringLiteral("main")) &&
                                 (uri.contains(QStringLiteral("debian.org")) || uri.contains(QStringLiteral("ubuntu.com")));
                entry.backend = QStringLiteral("apt");
                entry.filePath = filePath;
                list.append(entry);
            }
        }
    };

    if (QFile::exists(aptSourcesList())) {
        parseListFile(aptSourcesList());
    }

    QDir dir(aptSourcesDir());
    if (dir.exists()) {
        QStringList files = dir.entryList(QStringList() << QStringLiteral("*.list"), QDir::Files);
        for (const QString &f : files) {
            parseListFile(dir.filePath(f));
        }
    }

    return list;
}

bool RepoManager::addAptRepo(const RepoEntry &entry, QString *error) {
    QDir dir(aptSourcesDir());
    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral("."))) {
            if (error) *error = QStringLiteral("Verzeichnis %1 konnte nicht erstellt werden").arg(dir.path());
            return false;
        }
    }

    QString filePath = dir.filePath(QStringLiteral("%1.list").arg(entry.id));
    QString line = entry.url.trimmed();
    // Optionen in eckigen Klammern nicht durchreichen: "[trusted=yes]" würde die
    // Signaturprüfung für diese Quelle abschalten.
    if (line.contains(QLatin1Char('[')) || line.contains(QLatin1Char(']'))) {
        if (error) *error = QStringLiteral("Optionen in eckigen Klammern sind in Paketquellen nicht zulässig.");
        return false;
    }
    if (!line.startsWith(QStringLiteral("deb "))) {
        line = QStringLiteral("deb %1").arg(line);
    }
    line += QLatin1Char('\n');

    return safeWriteFile(filePath, line, error);
}

bool RepoManager::removeAptRepo(const QString &repoId, QString *error) {
    QDir dir(aptSourcesDir());
    QString filePath = dir.filePath(QStringLiteral("%1.list").arg(repoId));
    if (repoId == QLatin1String("pacstall")) {
        QFile::remove(QDir(aptKeyringDir()).filePath(QStringLiteral("ppr-keyring.gpg")));
    }
    if (QFile::exists(filePath)) {
        if (!QFile::remove(filePath)) {
            if (error) *error = QStringLiteral("Konnte Datei nicht entfernen: %1").arg(filePath);
            return false;
        }
        return true;
    }
    return true;
}

bool RepoManager::toggleAptRepo(const QString &repoId, bool enable, QString *error) {
    QDir dir(aptSourcesDir());
    QString filePath = dir.filePath(QStringLiteral("%1.list").arg(repoId));
    if (!QFile::exists(filePath)) return true;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QString content = QString::fromUtf8(file.readAll());
    file.close();

    QStringList lines = content.split(QLatin1Char('\n'));
    QStringList newLines;
    for (QString l : lines) {
        QString trimmed = l.trimmed();
        if (enable) {
            if (trimmed.startsWith(QLatin1Char('#'))) {
                l = trimmed.mid(1).trimmed();
            }
        } else {
            if (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1Char('#'))) {
                l = QStringLiteral("# ") + l;
            }
        }
        newLines.append(l);
    }

    return safeWriteFile(filePath, newLines.join(QLatin1Char('\n')), error);
}

} // namespace lut
