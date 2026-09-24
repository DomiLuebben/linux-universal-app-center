#pragma once

#include <QAbstractListModel>
#include <QProcess>
#include <functional>
#include <memory>
#include <optional>

namespace lut {

class DaemonClient;

class SnapUpdates : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(bool hasChecked READ hasChecked NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        CurrentVersionRole,
        NewVersionRole,
        VersionTransitionRole,
        RevisionRole,
        PublisherRole,
        NotesRole
    };

    struct Entry {
        QString name;
        QString currentVersion;
        QString newVersion;
        QString versionTransition;
        QString revision;
        QString publisher;
        QString notes;
    };

    using ProcessRunner = std::function<int(const QStringList &args, QString &stdoutOut, QString &stderrOut)>;

    explicit SnapUpdates(QObject *parent = nullptr);
    ~SnapUpdates() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool available() const;
    bool busy() const { return m_busy; }
    int count() const { return rowCount(); }
    bool hasChecked() const { return m_hasChecked; }
    QString statusMessage() const { return m_statusMessage; }

    void setDaemonClient(DaemonClient *client);
    void setProcessRunner(ProcessRunner runner);
    void setForceAvailable(bool force);

    static QList<Entry> parseRefreshOutput(const QString &refreshListOutput);

    /// Erkennt eine abgeschlossene Aktualisierung in der Ausgabe von "snap refresh",
    /// etwa "firefox 125.0 from Mozilla\u2713 refreshed". Liefert den Snap-Namen.
    static bool parseRefreshedLine(const QString &line, QString *name);

public slots:
    void check();
    void updateApp(const QString &name);
    void updateAll();
    void cancel();

signals:
    void busyChanged();
    void countChanged();
    void statusChanged();
    void finished(const QString &name);
    void failed(const QString &message);

    // Für das gemeinsame Fortschrittsfenster
    void operationStarted(const QString &title);
    void operationProgress(double fraction, bool indeterminate, int step, int total, const QString &text);
    void operationFinished(int result, const QString &message);
    void logLine(const QString &line);

private:
    int runCommand(const QStringList &args, QString &stdoutOut, QString &stderrOut);
    void setBusy(bool b);
    void setStatus(const QString &msg);
    void runUpdate(const QStringList &args, const QString &target, const QString &title, int expectedSteps);
    void handleOutput(const QByteArray &chunk, QByteArray &buffer, bool isError);
    void handleOutputLine(const QString &line);
    void endOperation(int result, const QString &message, const QString &target);

    bool m_busy = false;
    bool m_hasChecked = false;
    std::optional<bool> m_forceAvailable;
    QString m_statusMessage;
    QList<Entry> m_items;

    ProcessRunner m_runner;
    std::unique_ptr<QProcess> m_activeProcess;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    QString m_lastError;
    int m_opDone = 0;
    int m_opTotal = 0;
    bool m_cancelRequested = false;
    DaemonClient *m_daemonClient = nullptr;
};

} // namespace lut
