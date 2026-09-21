#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QDateTime>
#include <QJsonObject>
#include "Capabilities.h"
#include "liblut/protocol/events.h"

namespace lut {

struct UpgradeOptions {
    bool includeSecurityOnly = false;
    bool excludeKernel = false;
    bool allowDowngrade = false;
    bool refreshFirst = true;
};

struct InstalledPackage {
    QString id;
    QString name;
    QString version;
    QString arch;
    qint64 installedSize = 0;
    QString summary;
    QString description;
    QString repo;
    QString installDate;
    bool isOrphan = false;
    bool isOldKernel = false;
};

struct ChangelogEntry {
    QString version;
    QString date;
    QString author;
    QString text;
};

struct HistoryEntry {
    qint64 id = 0;
    QDateTime timestamp;
    QString command;
    QString result;
    int packagesAltered = 0;
    bool canUndo = false;
};

class Backend : public QObject {
    Q_OBJECT

public:
    explicit Backend(QObject *parent = nullptr) : QObject(parent) {}
    ~Backend() override = default;

    virtual Capabilities capabilities() const = 0;

    // Transaktionssteuerung
    virtual void refreshMetadata() = 0;
    virtual void planUpgradeAll(const UpgradeOptions &options = {}) = 0;
    virtual void planInstall(const QStringList &names) = 0; // [Phase 2]
    virtual void planRemove(const QStringList &names) = 0;
    virtual void commit() = 0;
    virtual void cancel() = 0;
    virtual void answerQuestion(const QString &id, const QJsonObject &answer) = 0;

    // Lesende Abfragen ohne root
    virtual QList<PackageOp> availableUpdates() = 0;
    virtual QList<InstalledPackage> installedPackages(const QString &query = QString()) = 0;
    virtual QList<ChangelogEntry> changelog(const QString &pkgId) = 0;
    virtual QList<HistoryEntry> history(int limit = 20) = 0;

signals:
    void eventEmitted(const lut::Event &event);
};

} // namespace lut
