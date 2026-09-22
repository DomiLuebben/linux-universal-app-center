#pragma once

#include <QProcess>
#include "liblut/backend/Backend.h"

namespace lut {

class AlpmBackend : public Backend {
    Q_OBJECT

public:
    explicit AlpmBackend(QObject *parent = nullptr);
    ~AlpmBackend() override;

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
    TransactionSnapshot currentSnapshot() const override;

    QList<PackageOp> availableUpdates() override;
    QList<InstalledPackage> installedPackages(const QString &query = QString()) override;
    QList<ChangelogEntry> changelog(const QString &pkgId) override;
    QList<HistoryEntry> history(int limit = 20) override;

    QList<InstalledPackage> queryOrphans();
    qint64 queryCleanableCacheBytes() const;
    QStringList detectPacnewFiles() const;

private:
    enum class PlannedAction { Upgrade, Install, Remove };

    QString findWorkerExecutable() const;
    void parseWorkerOutputLine(const QString &line);
    void parsePacmanOutput(const QString &line);

    QProcess *m_process = nullptr;
    QList<PackageOp> m_plannedOps;
    PlannedAction m_plannedAction = PlannedAction::Upgrade;
    QStringList m_plannedTargets;
    QString m_planRevision;
    QString m_expectedRevision;
    bool m_workerDoneEmitted = false;
};

} // namespace lut
