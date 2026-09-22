#pragma once

#include <QObject>
#include <QStringList>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QDateTime>
#include <QHash>

namespace lut {

class ExternalChangeWatcher : public QObject {
    Q_OBJECT

public:
    explicit ExternalChangeWatcher(QObject *parent = nullptr);
    ~ExternalChangeWatcher() override = default;

    // Standard package database directories based on distro family
    static QStringList defaultDatabasePaths();

    // Adds a custom path to monitor (used also for testing)
    bool addWatchPath(const QString &path);

    // Removes a path from monitoring
    bool removeWatchPath(const QString &path);

    // Returns list of currently watched paths
    QStringList watchedPaths() const;

    // Configures debounce interval in milliseconds (default: 500ms)
    void setDebounceInterval(int ms);
    int debounceInterval() const { return m_debounceIntervalMs; }

    // Manually trigger a database check (e.g. when application window receives focus)
    void checkChanges();

signals:
    void databaseChanged();

private slots:
    void onDirectoryChanged(const QString &path);
    void onFileChanged(const QString &path);
    void onDebounceTimeout();

private:
    void recordPathTimestamps();
    bool hasAnyPathTimestampChanged();
    void ensurePathsRegistered();

    QFileSystemWatcher m_watcher;
    QTimer m_debounceTimer;
    int m_debounceIntervalMs = 500;
    QStringList m_configuredPaths;
    QHash<QString, QDateTime> m_lastModified;
};

} // namespace lut
