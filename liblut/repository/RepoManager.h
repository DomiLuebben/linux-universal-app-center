#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <optional>
#include <functional>
#include <QStringList>
#include <QUrl>
#include "RepoTypes.h"
#include "liblut/detect/DistroDetect.h"

namespace lut {

class RepoManager : public QObject {
    Q_OBJECT

public:
    static RepoManager &instance();

    explicit RepoManager(QObject *parent = nullptr);
    ~RepoManager() override = default;

    // Distro detection override for tests
    void setForcedDistroFamily(std::optional<DistroFamily> family) { m_forcedFamily = family; }
    DistroFamily currentFamily() const;

    /// Führt ein Programm aus und liefert den Exit-Code; Ausgabe in \p output.
    /// Austauschbar, damit Tests die erzeugten Befehle prüfen können, ohne dnf auszuführen.
    using CommandRunner = std::function<int(const QString &program, const QStringList &args, QString *output)>;
    void setCommandRunner(CommandRunner runner) { m_runner = std::move(runner); }
    void setOsReleasePath(const QString &path) { m_osReleasePath = path; }

    /// Lädt eine Datei über HTTPS (hier: den Schlüssel der Pacstall-PPR).
    /// Austauschbar, damit Tests ohne Netz auskommen.
    using Downloader = std::function<QByteArray(const QUrl &url, QString *error)>;
    void setDownloader(Downloader downloader) { m_downloader = std::move(downloader); }
    void setAptKeyringDir(const QString &path) { m_aptKeyringDir = path; }
    QString aptKeyringDir() const;

    /// Fingerabdruck (OpenPGP v4, Großbuchstaben-Hex) des einzigen Primärschlüssels
    /// in \p key (ASCII-armored oder binär). Leer, wenn die Daten keinen oder mehr
    /// als einen Primärschlüssel enthalten. \p binaryOut erhält die Binärform.
    static QString openPgpFingerprint(const QByteArray &key, QByteArray *binaryOut = nullptr);

    /// Offizielle Adresse, Schlüssel und Fingerabdruck der Pacstall-PPR.
    static const QString PprKeyUrl;
    static const QString PprFingerprint;
    static const QString PprUri;

    /// Fedora-Hauptversion aus os-release (VERSION_ID), leer wenn unbekannt.
    QString fedoraVersion() const;

    // Path overrides for unit tests
    void setPacmanConfPath(const QString &path) { m_pacmanConfPath = path; }
    void setYumReposDir(const QString &path) { m_yumReposDir = path; }
    void setAptSourcesList(const QString &path) { m_aptSourcesList = path; }
    void setAptSourcesDir(const QString &path) { m_aptSourcesDir = path; }

    QString pacmanConfPath() const;
    QString yumReposDir() const;
    QString aptSourcesList() const;
    QString aptSourcesDir() const;

    // Repositories listing and operations
    QList<RepoEntry> getRepositories();
    QString getRepositoriesJson();

    QList<RepoPreset> getPresets();
    QString getPresetsJson();

    bool addRepository(const RepoEntry &entry, QString *error = nullptr);
    bool addRepositoryFromJson(const QString &jsonStr, QString *error = nullptr);
    bool addPreset(const QString &presetId, QString *error = nullptr);
    bool removeRepository(const QString &repoId, QString *error = nullptr);
    bool toggleRepository(const QString &repoId, bool enable, QString *error = nullptr);

    static bool isValidRepoId(const QString &id);
    /// Einrichtungspaket eines Drittanbieters, das beim Entfernen der Quelle mitgehen darf.
    static bool isRemovableSetupPackage(const QString &name);
    // Werte, die in eine Paketverwaltungs-Konfiguration geschrieben werden, dürfen
    // keine Steuerzeichen enthalten. Ein Zeilenumbruch in der Server-Adresse würde
    // sonst beliebige weitere Direktiven einschleusen - etwa "SigLevel = Never" -
    // und zwar als root in /etc.
    static bool isSafeConfigValue(const QString &value);
    // Nur echte Include-Pfade unterhalb von /etc/pacman.d/ sind zulässig.
    static bool isAllowedPacmanInclude(const QString &path);

signals:
    void repositoriesChanged();

private:
    // Distro-specific implementations
    QList<RepoEntry> getPacmanRepos();
    bool addPacmanRepo(const RepoEntry &entry, QString *error);
    bool removePacmanRepo(const QString &repoId, QString *error);
    bool togglePacmanRepo(const QString &repoId, bool enable, QString *error);

    QList<RepoEntry> getDnfRepos();
    bool addDnfRepo(const RepoEntry &entry, QString *error);
    bool removeDnfRepo(const QString &repoId, QString *error);
    bool toggleDnfRepo(const QString &repoId, bool enable, QString *error);

    QList<RepoEntry> getAptRepos();
    bool addAptRepo(const RepoEntry &entry, QString *error);
    bool removeAptRepo(const QString &repoId, QString *error);
    bool toggleAptRepo(const QString &repoId, bool enable, QString *error);

    int runCommand(const QString &program, const QStringList &args, QString *output) const;
    bool installSetupPackages(const RepoPreset &preset, QString *error);
    bool setupPacstallRepo(QString *error);
    QByteArray download(const QUrl &url, QString *error) const;
    bool removeOwnedRepoFile(const QString &filePath, const QString &repoId, bool *handled, QString *error);

    CommandRunner m_runner;
    Downloader m_downloader;
    QString m_aptKeyringDir;
    QString m_osReleasePath;
    std::optional<DistroFamily> m_forcedFamily;
    QString m_pacmanConfPath;
    QString m_yumReposDir;
    QString m_aptSourcesList;
    QString m_aptSourcesDir;
};

} // namespace lut
