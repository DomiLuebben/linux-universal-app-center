#pragma once
#include "liblut/backend/Backend.h"
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <functional>

namespace lut {
class Dnf5Backend : public Backend {
    Q_OBJECT
public:
    explicit Dnf5Backend(QObject *parent = nullptr, const QString &program = QStringLiteral("/usr/bin/dnf5"));
    ~Dnf5Backend() override;
    Capabilities capabilities() const override;
    void refreshMetadata() override;
    void cleanCache();
    void planUpgradeAll(const UpgradeOptions &options = {}) override;
    void planInstall(const QStringList &names) override;
    void planRemove(const QStringList &names) override;
    void commit() override;
    void cancel() override;
    void answerQuestion(const QString &, const QJsonObject &) override {}
    QList<PackageOp> availableUpdates() override;
    QList<InstalledPackage> installedPackages(const QString &query = {}) override;
    QList<ChangelogEntry> changelog(const QString &pkgId) override;
    QList<HistoryEntry> history(int limit = 20) override;

    // Commands share the same resolver, stored plan and authorized commit path.
    static QStringList transactionCommands();
    void planCommand(const QString &command, const QStringList &arguments, const UpgradeOptions &options = {});
    static QList<HistoryEntry> parseHistory(const QByteArray &json, int limit, QString *error = nullptr);

private:
    void start(const QStringList &args, bool cancellable, std::function<void()> success);
    QByteArray query(const QStringList &args, bool *ok = nullptr);
    void readOutput();
    void fail(const QString &message);
    void loadStoredPlan();
    QString m_program;
    QProcess m_process;
    QTimer m_timeout;
    std::unique_ptr<QTemporaryDir> m_plan;
    QList<PackageOp> m_plannedOps;
    QString m_output;
    std::function<void()> m_success;
    bool m_ready = false;
    bool m_cancellable = false;
    bool m_cancelled = false;
    bool m_timedOut = false;
    bool m_busy = false;
};
}
