#pragma once

#include <functional>

#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>
#include <QDBusConnection>
#include "liblut/repository/RepoTypes.h"
#include "liblut/detect/DistroDetect.h"

namespace lut {

class DaemonClient;

class RepositoriesModel : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QString distroName READ distroName CONSTANT)
    Q_PROPERTY(QString distroFamily READ distroFamily CONSTANT)
    Q_PROPERTY(QString backendName READ backendName CONSTANT)
    Q_PROPERTY(bool pacstallInstalled READ pacstallInstalled NOTIFY pacstallInstalledChanged)
    // Debian 13 enthält spdx-licenses nicht; pacstall lehnt dann jede Lizenzangabe ab.
    Q_PROPERTY(bool pacstallLicensesMissing READ pacstallLicensesMissing NOTIFY pacstallInstalledChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY isBusyChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        UrlRole,
        EnabledRole,
        IsSystemRole,
        BackendRole,
        DescriptionRole,
        FilePathRole
    };

    explicit RepositoriesModel(DaemonClient *client = nullptr, QObject *parent = nullptr);
    ~RepositoriesModel() override = default;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QVariantList presets() const { return m_presets; }
    QString distroName() const;
    QString distroFamily() const;
    QString backendName() const;
    bool pacstallInstalled() const;
    bool pacstallLicensesMissing() const;
    bool isBusy() const { return m_busy; }
    QString errorMessage() const { return m_errorMessage; }

public slots:
    void refresh();
    void addPreset(const QString &presetId);
    void addCustom(const QString &id, const QString &name, const QString &url);
    void removeRepo(const QString &repoId);
    void toggleRepo(const QString &repoId, bool enable);
    /// Nach einer Paketoperation: ist pacstall inzwischen installiert?
    void refreshPacstallState();

signals:
    void countChanged();
    void presetsChanged();
    void isBusyChanged();
    void errorChanged();
    void repoOperationFinished(const QString &action, bool success, const QString &error);
    void presetAdded(const QString &presetId);
    void pacstallInstalledChanged();

private:
    void callDaemon(const QString &method, const QVariantList &args, const QString &action,
                    const std::function<bool(QString *)> &fallback);
    void setBusy(bool busy);
    void setError(const QString &err);
    void loadFromDaemon();
    void loadDirectly();

    DaemonClient *m_client = nullptr;
    QList<RepoEntry> m_repos;
    QVariantList m_presets;
    bool m_busy = false;
    bool m_pacstallInstalled = false;
    QString m_pendingPresetId;
    QString m_errorMessage;
};

} // namespace lut
