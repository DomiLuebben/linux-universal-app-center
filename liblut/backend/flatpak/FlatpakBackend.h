#pragma once

#include "liblut/backend/Backend.h"
#include <QProcess>
#include <functional>
#include <memory>

namespace lut {

class FlatpakBackend : public Backend {
    Q_OBJECT

public:
    using ProcessRunner = std::function<int(const QString &program, const QStringList &args, QString &stdoutOut, QString &stderrOut)>;

    explicit FlatpakBackend(ProcessRunner runner = nullptr, QObject *parent = nullptr);
    ~FlatpakBackend() override;

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

    static bool isFlatpakAvailable();
    static QList<PackageOp> parseUpdates(const QString &remoteLsOutput);
    static QList<HistoryEntry> parseHistoryJson(const QByteArray &json, int limit = 20);

private:
    int runCommand(const QStringList &args, QString &stdoutOut, QString &stderrOut);

    ProcessRunner m_runner;
    std::unique_ptr<QProcess> m_activeProcess;
    TransactionPlan m_currentPlan;
    TransactionIntent m_currentIntent;
    QString m_boundCommit;
    QString m_targetRef;
    QString m_targetRepo;
    bool m_cancelled = false;
};

} // namespace lut
