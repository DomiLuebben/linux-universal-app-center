#pragma once

#include "liblut/backend/Backend.h"
#include <QProcess>
#include <functional>
#include <memory>

namespace lut {

class SnapBackend : public Backend {
    Q_OBJECT

public:
    using ProcessRunner = std::function<int(const QString &program, const QStringList &args, QString &stdoutOut, QString &stderrOut)>;

    explicit SnapBackend(ProcessRunner runner = nullptr, bool forceAvailable = false, QObject *parent = nullptr);
    ~SnapBackend() override;

    Capabilities capabilities() const override;

    void refreshMetadata() override;
    void planUpgradeAll(const UpgradeOptions &options = {}) override;
    void planInstall(const QStringList &names) override;
    void planRemove(const QStringList &names) override;
    void planPackageTransaction(const TransactionIntent &intent) override;
    void commit() override;
    void commitPlan(const QString &planRevision) override;
    void discardPlan() override;
    void cancel() override;
    void answerQuestion(const QString &id, const QJsonObject &answer) override;

    QList<PackageOp> availableUpdates() override;
    QList<InstalledPackage> installedPackages(const QString &query = QString()) override;
    QList<ChangelogEntry> changelog(const QString &pkgId) override;
    QList<HistoryEntry> history(int limit = 20) override;

    static bool isSnapAvailable();

    struct SnapDetails {
        QString name;
        QString version;
        QString revision;
        QString channel;
        QString confinement; // "strict", "classic", "devmode"
        qint64 downloadSize = 0;
    };

    static SnapDetails parseSnapInfo(const QString &infoText, const QString &channel = QStringLiteral("latest/stable"));
    static SnapDetails parseSnapFindJson(const QString &jsonText, const QString &snapName);
    static QList<PackageOp> parseRefreshList(const QString &output);
    static QList<HistoryEntry> parseChanges(const QString &output, int limit = 20);

private:
    int runCommand(const QStringList &args, QString &stdoutOut, QString &stderrOut);
    bool isArchLinux() const;

    ProcessRunner m_runner;
    bool m_forceAvailable = false;
    std::unique_ptr<QProcess> m_activeProcess;
    TransactionPlan m_currentPlan;
    TransactionIntent m_currentIntent;
    QString m_boundRevision;
    QString m_targetSnap;
    QString m_targetChannel;
    QString m_confinement;
    bool m_cancelled = false;
};

} // namespace lut
