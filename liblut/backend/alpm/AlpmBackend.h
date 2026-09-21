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
    void cancel() override;
    void answerQuestion(const QString &id, const QJsonObject &answer) override;

    QList<PackageOp> availableUpdates() override;
    QList<InstalledPackage> installedPackages(const QString &query = QString()) override;
    QList<ChangelogEntry> changelog(const QString &pkgId) override;
    QList<HistoryEntry> history(int limit = 20) override;

    QList<InstalledPackage> queryOrphans();
    qint64 queryCleanableCacheBytes() const;
    QStringList detectPacnewFiles() const;

private:
    QString findWorkerExecutable() const;
    void parseWorkerOutputLine(const QString &line);
    void parsePacmanOutput(const QString &line);

    QProcess *m_process = nullptr;
    QList<PackageOp> m_plannedOps;
};

} // namespace lut
