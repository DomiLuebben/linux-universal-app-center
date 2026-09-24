#pragma once

#include <QAbstractListModel>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QVariantList>
#include <optional>

namespace lut {

class AppSettings;

class PacstallUpdates : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(bool hasChecked READ hasChecked NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        InstalledVersionRole,
        AvailableVersionRole,
        RepoRole,
    };

    struct Entry {
        QString name;
        QString repo;
        QString installed;
        QString available;
    };

    explicit PacstallUpdates(QObject *parent = nullptr);
    ~PacstallUpdates() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool supported() const;
    bool enabled() const { return m_enabled; }
    bool available() const { return supported() && m_enabled; }
    bool busy() const { return m_busy; }
    int count() const { return rowCount(); }
    QString statusMessage() const { return m_statusMessage; }
    bool hasChecked() const { return m_hasChecked; }

    void setAppSettings(AppSettings *settings);
    void setForceSupported(std::optional<bool> supported);
    void setDbusConnection(const QDBusConnection &connection);

    // Hilfsfunktionen für reine Tests
    void setEntriesForTest(const QList<Entry> &entries);

public slots:
    void check();
    /// Nach einer Paketoperation neu bewerten, ob pacstall jetzt installiert ist.
    void refreshSupport();
    void setEnabled(bool enabled);
    void upgradeAll();
    void upgradePackages(const QStringList &names);
    void confirmReview();
    void cancel();

signals:
    void supportedChanged();
    void enabledChanged(bool enabled);
    void availableChanged();
    void busyChanged();
    void countChanged();
    void statusChanged();
    void logLine(const QString &line);
    void failed(const QString &message);
    void finished(const QString &name);

    void operationStarted(const QString &title);
    void operationProgress(double fraction, bool indeterminate, int step, int total, const QString &text);
    void operationFinished(int result, const QString &message);
    void reviewReady(const QVariantList &items);

private slots:
    void onDbusTransactionEvent(const QDBusObjectPath &path, const QString &eventJson);

private:
    void setBusy(bool busy);
    void setStatus(const QString &message);

    std::optional<bool> m_forceSupported;
    bool m_enabled = false;
    bool m_busy = false;
    bool m_hasChecked = false;
    QString m_statusMessage;
    QList<Entry> m_items;

    AppSettings *m_settings = nullptr;
    QDBusConnection m_bus;
    QDBusObjectPath m_activeTransactionPath;
    QString m_planRevision;
    bool m_lastSupported = false;
    bool m_committed = false;
};

} // namespace lut
