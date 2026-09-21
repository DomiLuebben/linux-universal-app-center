#pragma once

#include <QTimer>
#include <QFile>
#include "liblut/backend/Backend.h"

namespace lut {

struct ReplayItem {
    qint64 delayMs = 0;
    Event event;
};

class ReplayBackend : public Backend {
    Q_OBJECT

public:
    explicit ReplayBackend(QObject *parent = nullptr);
    explicit ReplayBackend(const QString &fixturePath, double speed = 1.0, QObject *parent = nullptr);
    ~ReplayBackend() override;

    bool loadFixture(const QString &fixturePath);
    void setSpeed(double speed);
    double speed() const { return m_speed; }

    void start();
    void pause();
    void resume();
    void stop();
    bool isRunning() const { return m_running; }

    // Synchroner Durchlauf ohne Event-Loop (für Headless-Tests)
    void runSynchronously(const std::function<void(const Event&)> &callback = nullptr);

    // Backend-Interface-Implementierung
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

signals:
    void replayFinished();
    void questionEncountered(const QString &id);

private slots:
    void processNextItem();

private:
    QString m_fixturePath;
    double m_speed = 1.0;
    QList<ReplayItem> m_items;
    int m_currentIndex = 0;
    bool m_running = false;
    bool m_waitingForAnswer = false;
    QString m_pendingQuestionId;
    QTimer *m_timer = nullptr;
    QList<PackageOp> m_cachedUpdates;
};

} // namespace lut
