#pragma once

#include <QProcess>
#include "liblut/backend/Backend.h"
#include "StatusFdParser.h"

namespace lut {

class AptBackend : public Backend {
    Q_OBJECT

public:
    explicit AptBackend(QObject *parent = nullptr);
    ~AptBackend() override;

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

private:
    void handleStatusFdLine(const QString &line);

    QProcess *m_process = nullptr;
    QList<PackageOp> m_plannedOps;
};

} // namespace lut
