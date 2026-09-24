#include "SnapPackageCatalog.h"
#include "liblut/backend/Validation.h"
#include "liblut/backend/snap/SnapAvailability.h"
#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace lut {

SnapPackageCatalog::SnapPackageCatalog(CommandRunner runner, bool forceAvailable)
    : m_runner(std::move(runner))
    , m_forceAvailable(forceAvailable)
{
}

bool SnapPackageCatalog::isAvailable() const
{
    if (m_forceAvailable || m_runner) return true;
    return SnapAvailability::isSnapAvailable();
}

bool SnapPackageCatalog::executeCommand(const QStringList &args, QString &stdoutOut, QString &stderrOut) const
{
    if (m_runner) {
        return m_runner(args, stdoutOut, stderrOut);
    }

    if (!isAvailable()) {
        stderrOut = QStringLiteral("snapd is not available or snap executable not found");
        return false;
    }

    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    proc.setProcessEnvironment(env);
    proc.start(QStringLiteral("/usr/bin/snap"), args);
    if (!proc.waitForStarted(5000)) {
        stderrOut = QStringLiteral("Failed to start snap process");
        return false;
    }

    if (!proc.waitForFinished(15000)) {
        proc.kill();
        stderrOut = QStringLiteral("snap process timed out");
        return false;
    }

    stdoutOut = QString::fromUtf8(proc.readAllStandardOutput());
    stderrOut = QString::fromUtf8(proc.readAllStandardError());
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

void SnapPackageCatalog::ensureSnapshotLoaded() const
{
    if (m_snapshotLoaded) {
        return;
    }

    m_installedPackages.clear();
    m_installedBySnapName.clear();

    if (!isAvailable()) {
        m_snapshotLoaded = true;
        return;
    }

    QString out, err;
    const QStringList args = {
        QStringLiteral("list"),
        QStringLiteral("--unicode=never"),
        QStringLiteral("--color=never")
    };

    if (executeCommand(args, out, err)) {
        const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        bool inHeader = true;
        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith(QLatin1String("No snaps are installed"))) {
                continue;
            }
            if (inHeader && (trimmed.startsWith(QLatin1String("Name")) || trimmed.contains(QLatin1String("Version")))) {
                inHeader = false;
                continue;
            }

            // Columns: Name Version Rev Tracking Publisher Notes
            const QStringList parts = trimmed.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            if (parts.size() < 3) {
                continue;
            }

            const QString snapName = parts[0];
            const QString version = parts[1];
            const QString rev = parts[2];
            const QString tracking = parts.size() > 3 ? parts[3] : QStringLiteral("latest/stable");
            const QString publisher = parts.size() > 4 ? parts[4] : QString();
            const QString notes = parts.size() > 5 ? parts[5] : QString();

            if (snapName.isEmpty() || !Validation::isValidSnapName(snapName)) {
                continue;
            }

            const QString displayVersion = rev.isEmpty() ? version : QStringLiteral("%1 (rev %2)").arg(version, rev);

            InstalledPackage ipkg;
            ipkg.id = QStringLiteral("snap:") + snapName;
            ipkg.name = snapName;
            ipkg.version = displayVersion;
            ipkg.repo = tracking;
            ipkg.arch = QStringLiteral("x86_64");

            m_installedPackages.append(ipkg);

            InstalledState state;
            state.isFullyInstalled = true;
            PackageRef ref;
            ref.backend = QStringLiteral("snap");
            ref.repoId = tracking;
            ref.name = snapName;
            ref.arch = QStringLiteral("x86_64");
            ref.version = displayVersion;

            state.installedPackages = {ref};
            state.origin = QStringLiteral("snap");

            // Snaps with "base" notes are runtimes/base systems, not desktop apps
            if (!notes.contains(QLatin1String("base"))) {
                state.launchableDesktopIds = {QStringLiteral("%1.desktop").arg(snapName)};
            }

            m_installedBySnapName.insert(snapName, state);
            m_installedBySnapName.insert(QStringLiteral("%1.desktop").arg(snapName), state);
        }
    }

    m_snapshotLoaded = true;
}

QList<PackageOffer> SnapPackageCatalog::offersForPackage(const QString &packageName)
{
    if (!isAvailable()) return {};
    ensureSnapshotLoaded();
    QString queryName = packageName;
    if (queryName.endsWith(QLatin1String(".desktop"))) {
        queryName.chop(8);
    }

    if (m_installedBySnapName.contains(queryName)) {
        const auto state = m_installedBySnapName.value(queryName);
        if (!state.installedPackages.isEmpty()) {
            PackageOffer offer;
            offer.packages = state.installedPackages;
            offer.priority = 0;
            offer.isCandidate = true;
            offer.available = true;
            return {offer};
        }
    }
    return {};
}

std::optional<PackageOffer> SnapPackageCatalog::candidateOffer(const QString &packageName)
{
    auto offers = offersForPackage(packageName);
    if (!offers.isEmpty()) {
        return offers.first();
    }
    return std::nullopt;
}

InstalledState SnapPackageCatalog::installedStateForPackage(const QString &packageName)
{
    if (!isAvailable()) return {};
    ensureSnapshotLoaded();
    QString queryName = packageName;
    if (m_installedBySnapName.contains(queryName)) {
        return m_installedBySnapName.value(queryName);
    }
    if (queryName.endsWith(QLatin1String(".desktop"))) {
        queryName.chop(8);
        if (m_installedBySnapName.contains(queryName)) {
            return m_installedBySnapName.value(queryName);
        }
    }
    return {};
}

QList<InstalledPackage> SnapPackageCatalog::allInstalledPackages()
{
    if (!isAvailable()) return {};
    ensureSnapshotLoaded();
    return m_installedPackages;
}

QList<PackageRef> SnapPackageCatalog::findPackagesProvidingFile(const QString &filePath)
{
    Q_UNUSED(filePath);
    return {};
}

QList<PackageOffer> SnapPackageCatalog::searchPackages(const QString &query)
{
    Q_UNUSED(query);
    return {};
}

void SnapPackageCatalog::prepareSnapshot(const QStringList &names)
{
    Q_UNUSED(names);
    ensureSnapshotLoaded();
}

void SnapPackageCatalog::reload()
{
    m_snapshotLoaded = false;
    ++m_generation;
    ensureSnapshotLoaded();
}

} // namespace lut
