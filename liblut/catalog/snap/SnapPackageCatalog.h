#pragma once

#include "liblut/catalog/PackageCatalog.h"
#include <QHash>
#include <QSet>
#include <functional>

namespace lut {

class SnapPackageCatalog : public PackageCatalog {
public:
    using CommandRunner = std::function<bool(const QStringList &args, QString &stdoutOut, QString &stderrOut)>;

    explicit SnapPackageCatalog(CommandRunner runner = nullptr, bool forceAvailable = false);
    ~SnapPackageCatalog() override = default;

    quint64 catalogGeneration() const override { return m_generation; }
    QList<PackageOffer> offersForPackage(const QString &packageName) override;
    std::optional<PackageOffer> candidateOffer(const QString &packageName) override;
    InstalledState installedStateForPackage(const QString &packageName) override;
    QList<InstalledPackage> allInstalledPackages() override;
    QList<PackageRef> findPackagesProvidingFile(const QString &filePath) override;
    QList<PackageOffer> searchPackages(const QString &query) override;
    void prepareSnapshot(const QStringList &names) override;
    void reload() override;

    bool isAvailable() const;

private:
    void ensureSnapshotLoaded() const;
    bool executeCommand(const QStringList &args, QString &stdoutOut, QString &stderrOut) const;

    CommandRunner m_runner;
    bool m_forceAvailable = false;
    mutable bool m_snapshotLoaded = false;
    mutable quint64 m_generation = 1;
    mutable QList<InstalledPackage> m_installedPackages;
    mutable QHash<QString, InstalledState> m_installedBySnapName;
};

} // namespace lut
