#pragma once

#include <QDBusConnection>
#include <QDBusInterface>
#include <QProcess>
#include "liblut/backend/Backend.h"

namespace lut {

class Dnf5Backend : public Backend {
    Q_OBJECT

public:
    explicit Dnf5Backend(QObject *parent = nullptr);
    ~Dnf5Backend() override;

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

private slots:
    void onDnfDownloadProgress(const QString &msg);
    void onDnfTransactionProgress(const QString &msg);

private:
    bool connectToDnf5Daemon();
    void runCliFallback(const QStringList &args);

    bool m_usingDaemon = false;
    QString m_sessionPath;
    QProcess *m_cliProcess = nullptr;
    QList<PackageOp> m_plannedOps;
};

} // namespace lut
