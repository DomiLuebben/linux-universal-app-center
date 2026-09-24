#pragma once

#include <QAbstractListModel>
#include <QProcess>
#include <functional>
#include <memory>
#include <optional>

namespace lut {

class DaemonClient;

class FlatpakUpdates : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(bool hasChecked READ hasChecked NOTIFY countChanged)
    Q_PROPERTY(qint64 totalDownloadBytes READ totalDownloadBytes NOTIFY countChanged)
    Q_PROPERTY(QString totalDownloadFormatted READ totalDownloadFormatted NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        OriginRole,
        CurrentVersionRole,
        NewVersionRole,
        VersionTransitionRole,
        RefRole,
        DownloadSizeRole,
        DownloadSizeFormattedRole
    };

    struct Entry {
        QString id;
        QString name;
        QString origin;
        QString currentVersion;
        QString newVersion;
        QString versionTransition;
        QString ref;
        qint64 downloadSize = 0;
        QString downloadSizeFormatted;
    };

    using ProcessRunner = std::function<int(const QStringList &args, QString &stdoutOut, QString &stderrOut)>;

    explicit FlatpakUpdates(QObject *parent = nullptr);
    ~FlatpakUpdates() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool available() const;
    bool busy() const { return m_busy; }
    int count() const { return rowCount(); }
    bool hasChecked() const { return m_hasChecked; }
    QString statusMessage() const { return m_statusMessage; }
    qint64 totalDownloadBytes() const { return m_totalDownloadBytes; }
    QString totalDownloadFormatted() const;

    void setDaemonClient(DaemonClient *client);
    void setProcessRunner(ProcessRunner runner);
    void setForceAvailable(bool force);

    static QList<Entry> parseUpdatesOutput(const QString &remoteLsOutput, const QString &listOutput = QString());
    static QString formatBytes(qint64 bytes);

    /// Erkennt in einer Zeile der Flatpak-Ausgabe (LC_ALL=C) den Beginn eines
    /// Vorgangs. Liefert \p step/\p total, wenn Flatpak "Updating 2/5" meldet,
    /// sonst 0; \p item ist die betroffene Anwendung, soweit erkennbar.
    static bool parseProgressLine(const QString &line, int *step, int *total, QString *item);

public slots:
    void check();
    void updateApp(const QString &refOrId);
    void updateAll();
    void cancel();

signals:
    void busyChanged();
    void countChanged();
    void statusChanged();
    void finished(const QString &id);
    void failed(const QString &message);

    // Für das gemeinsame Fortschrittsfenster
    void operationStarted(const QString &title);
    void operationProgress(double fraction, bool indeterminate, int step, int total, const QString &text);
    void operationFinished(int result, const QString &message);
    void logLine(const QString &line);

private:
    int runCommand(const QStringList &args, QString &stdoutOut, QString &stderrOut);
    void executeUpdateProcess(const QStringList &args, const QString &targetId);
    void setBusy(bool b);
    void setStatus(const QString &msg);
    void handleOutput(const QByteArray &chunk, QByteArray &buffer, bool isError);
    void handleOutputLine(const QString &line);
    void beginOperation(const QString &title, int expectedSteps);
    void endOperation(int result, const QString &message);

    bool m_busy = false;
    bool m_hasChecked = false;
    std::optional<bool> m_forceAvailable;
    qint64 m_totalDownloadBytes = 0;
    QString m_statusMessage;
    QList<Entry> m_items;

    ProcessRunner m_runner;
    std::unique_ptr<QProcess> m_activeProcess;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    QString m_lastError;
    int m_opStep = 0;
    int m_opTotal = 0;
    bool m_cancelRequested = false;
    DaemonClient *m_daemonClient = nullptr;
};

} // namespace lut
