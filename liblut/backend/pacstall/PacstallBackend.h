#pragma once

#include "liblut/backend/Backend.h"
#include <QProcess>
#include <QProcessEnvironment>
#include <QUrl>
#include <functional>
#include <memory>
#include <optional>
#include <QMap>

namespace lut {

struct PacstallUpdate {
    QString name;
    QString repo;
    QString installed;
    QString available;

    bool operator==(const PacstallUpdate &other) const = default;
    QJsonObject toJson() const;
    static PacstallUpdate fromJson(const QJsonObject &obj);
};

class PacstallBackend : public Backend {
    Q_OBJECT

public:
    using ProcessRunner = std::function<int(const QString &program,
                                            const QStringList &args,
                                            const QProcessEnvironment &env,
                                            QString &stdoutOut,
                                            QString &stderrOut)>;

    using Downloader = std::function<QByteArray(const QUrl &url, QString *error)>;

    explicit PacstallBackend(ProcessRunner runner = nullptr, bool forceAvailable = false, QObject *parent = nullptr);
    ~PacstallBackend() override;

    Capabilities capabilities() const override;

    void refreshMetadata() override;
    void planUpgradeAll(const UpgradeOptions &options = {}) override;
    void planInstall(const QStringList &names) override;
    void planRemove(const QStringList &names) override;
    void commit() override;
    void commitPlan(const QString &planRevision) override;
    void discardPlan() override;
    void cancel() override;
    void answerQuestion(const QString &id, const QJsonObject &answer) override;

    QList<PackageOp> availableUpdates() override;
    QList<InstalledPackage> installedPackages(const QString &query = QString()) override;
    QList<ChangelogEntry> changelog(const QString &pkgId) override;
    QList<HistoryEntry> history(int limit = 20) override;

    static bool isPacstallAvailable();

    // Reine Parser & Konverter
    static QList<PacstallUpdate> parseUpdates(const QString &noColorOutput);
    static QString updatesToJson(const QList<PacstallUpdate> &updates);
    static QList<PacstallUpdate> jsonToUpdates(const QString &json);
    static QString computePlanRevision(const QMap<QString, QByteArray> &pacscripts);

    // Repository- & Pfad-Konfiguration
    static QString defaultRepoFile();
    static QString defaultRepoUrl();

    QString pacstallRepoUrl() const;
    QString pacscriptsDir() const;

    // Spezifische Pacstall-Aufrufe
    /// pacstall -Lu. Scheitert die Abfrage oder ist die Ausgabe nicht wie erwartet,
    /// ist \p error gesetzt – dann heißt eine leere Liste NICHT "alles aktuell".
    QList<PacstallUpdate> checkUpdates(QString *error = nullptr);
    void planPacstallUpgrade(const QStringList &names, quint32 callerUid);

    // Test-Injektion
    void setRunner(ProcessRunner runner) { m_runner = std::move(runner); }
    void setDownloader(Downloader downloader) { m_downloader = std::move(downloader); }
    void setPacscriptsDir(const QString &dir) { m_pacscriptsDir = dir; }
    void setRepoFilePath(const QString &path) { m_repoFilePath = path; }
    void setCallerUsername(const QString &user) { m_callerUsername = user; }
    void setLockTimeoutMs(int ms) { m_lockTimeoutMs = ms; }

    const QMap<QString, QByteArray> &savedPacscripts() const { return m_savedPacscripts; }
    QString activeCallerUser() const { return m_activeCallerUser; }

    int runCommandForTest(const QString &program, const QStringList &args, const QProcessEnvironment &env,
                          QString &stdoutOut, QString &stderrOut) {
        return runCommand(program, args, env, stdoutOut, stderrOut);
    }

private:
    int runCommand(const QString &program, const QStringList &args, const QProcessEnvironment &env,
                   QString &stdoutOut, QString &stderrOut);
    QByteArray downloadPacscript(const QUrl &url, QString *error);
    void cleanStoredPacscripts();

    ProcessRunner m_runner;
    Downloader m_downloader;
    bool m_forceAvailable = false;
    QString m_pacscriptsDir;
    QString m_repoFilePath;
    QString m_callerUsername;
    QString m_activeCallerUser;
    int m_lockTimeoutMs = 60000; // 60 Sekunden Timeout bei Sperre

    QMap<QString, PacstallUpdate> m_knownUpdates;
    QMap<QString, QByteArray> m_savedPacscripts;
    QString m_currentPlanRevision;
    QStringList m_plannedPackages;
    bool m_cancelled = false;
};

} // namespace lut
