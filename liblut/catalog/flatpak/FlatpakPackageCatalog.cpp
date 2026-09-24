#include "FlatpakPackageCatalog.h"
#include "liblut/backend/Validation.h"
#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace lut {

FlatpakPackageCatalog::FlatpakPackageCatalog(CommandRunner runner)
    : m_runner(std::move(runner))
{
}

bool FlatpakPackageCatalog::isAvailable() const
{
    if (m_runner) return true;
    return QFile::exists(QStringLiteral("/usr/bin/flatpak"));
}

bool FlatpakPackageCatalog::executeCommand(const QStringList &args, QString &stdoutOut, QString &stderrOut) const
{
    if (m_runner) {
        return m_runner(args, stdoutOut, stderrOut);
    }

    if (!isAvailable()) {
        stderrOut = QStringLiteral("flatpak executable not found");
        return false;
    }

    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    proc.setProcessEnvironment(env);
    proc.start(QStringLiteral("/usr/bin/flatpak"), args);
    if (!proc.waitForStarted(5000)) {
        stderrOut = QStringLiteral("Failed to start flatpak process");
        return false;
    }

    if (!proc.waitForFinished(15000)) {
        proc.kill();
        stderrOut = QStringLiteral("flatpak process timed out");
        return false;
    }

    stdoutOut = QString::fromUtf8(proc.readAllStandardOutput());
    stderrOut = QString::fromUtf8(proc.readAllStandardError());
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

void FlatpakPackageCatalog::ensureSnapshotLoaded() const
{
    if (m_snapshotLoaded) {
        return;
    }

    m_installedPackages.clear();
    m_installedByAppId.clear();

    QString out, err;
    const QStringList args = {
        QStringLiteral("list"),
        QStringLiteral("--app"),
        QStringLiteral("--columns=application:f,origin:f,installation:f,ref:f,active:f,version:f,runtime:f")
    };

    if (executeCommand(args, out, err)) {
        const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList parts = line.split(QLatin1Char('\t'));
            if (parts.size() < 3) {
                continue;
            }

            const QString appId = parts[0].trimmed();
            const QString origin = parts[1].trimmed();
            const QString scope = parts[2].trimmed(); // "system" or "user"
            const QString ref = parts.size() > 3 ? parts[3].trimmed() : QString();
            const QString commit = parts.size() > 4 ? parts[4].trimmed() : QString();
            QString version = parts.size() > 5 ? parts[5].trimmed() : QString();

            if (appId.isEmpty() || !Validation::isValidFlatpakAppId(appId)) {
                continue;
            }

            if (version.isEmpty() && !commit.isEmpty()) {
                version = commit.left(12);
            }
            if (version.isEmpty()) {
                version = QStringLiteral("stable");
            }

            InstalledPackage ipkg;
            ipkg.id = ref.isEmpty() ? appId : ref;
            ipkg.name = appId;
            ipkg.version = version;
            ipkg.repo = origin;
            ipkg.arch = QStringLiteral("x86_64");
            ipkg.summary = scope; // "system" or "user"
            m_installedPackages.append(ipkg);

            InstalledState state;
            state.isFullyInstalled = true;
            state.origin = scope;
            state.launchableDesktopIds = {appId + QStringLiteral(".desktop")};

            PackageRef pref;
            pref.backend = QStringLiteral("flatpak");
            pref.repoId = origin;
            pref.name = appId;
            pref.arch = QStringLiteral("x86_64");
            pref.version = version;
            state.installedPackages = {pref};

            m_installedByAppId.insert(appId, state);
            if (!appId.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) {
                m_installedByAppId.insert(appId + QStringLiteral(".desktop"), state);
            } else {
                m_installedByAppId.insert(appId.left(appId.size() - 8), state);
            }
        }
    }

    m_snapshotLoaded = true;
}

QList<PackageOffer> FlatpakPackageCatalog::offersForPackage(const QString &packageName)
{
    ensureSnapshotLoaded();
    auto cand = candidateOffer(packageName);
    if (cand.has_value()) {
        return {*cand};
    }
    return {};
}

void FlatpakPackageCatalog::ensureRemoteOffersLoaded() const
{
    if (m_remoteOffersLoaded) {
        return;
    }
    m_remoteOffersLoaded = true;
    m_remoteOffersByAppId.clear();

    // Ein Aufruf je Remote, nicht einer je Anwendung: Ein Katalogdurchlauf darf
    // keine Unterprozesse pro Eintrag starten (Abschnitt 5.4 des Plans).
    QString remotesOut, remotesErr;
    if (!executeCommand({QStringLiteral("remotes"), QStringLiteral("--columns=name:f")}, remotesOut, remotesErr)) {
        return;
    }

    const QStringList remotes = remotesOut.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &rawRemote : remotes) {
        const QString remote = rawRemote.trimmed();
        if (remote.isEmpty() || !Validation::isValidFlatpakRemote(remote)) {
            continue;
        }

        QString out, err;
        const QStringList args = {
            QStringLiteral("remote-ls"),
            remote,
            QStringLiteral("--app"),
            QStringLiteral("--columns=application:f,version:f,branch:f,download-size:f,installed-size:f")
        };
        if (!executeCommand(args, out, err)) {
            continue;
        }

        const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList parts = line.split(QLatin1Char('\t'));
            if (parts.isEmpty()) {
                continue;
            }
            const QString appId = parts[0].trimmed();
            if (appId.isEmpty() || !Validation::isValidFlatpakAppId(appId)) {
                continue;
            }
            // Der erste Remote in der Reihenfolge von `flatpak remotes` gewinnt.
            if (m_remoteOffersByAppId.contains(appId)) {
                continue;
            }

            QString version = parts.size() > 1 ? parts[1].trimmed() : QString();
            const QString branch = parts.size() > 2 ? parts[2].trimmed() : QString();
            if (version.isEmpty()) {
                version = branch.isEmpty() ? QStringLiteral("stable") : branch;
            }

            PackageRef ref;
            ref.backend = QStringLiteral("flatpak");
            ref.repoId = remote;
            ref.name = appId;
            ref.arch = QStringLiteral("x86_64");
            ref.version = version;

            PackageOffer offer;
            offer.packages = {ref};
            offer.available = true;
            offer.isCandidate = true;
            // Größen erst, wenn flatpak sie tatsächlich liefert. Keine geratenen Werte.
            const auto parseSize = [](const QString &text) -> std::optional<qint64> {
                bool ok = false;
                const qint64 value = text.trimmed().toLongLong(&ok);
                if (ok && value > 0) return value;
                return std::nullopt;
            };
            if (parts.size() > 3) offer.downloadSize = parseSize(parts[3]);
            if (parts.size() > 4) offer.installedSize = parseSize(parts[4]);

            m_remoteOffersByAppId.insert(appId, offer);
            if (!appId.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) {
                m_remoteOffersByAppId.insert(appId + QStringLiteral(".desktop"), offer);
            } else {
                m_remoteOffersByAppId.insert(appId.left(appId.size() - 8), offer);
            }
        }
    }
}

std::optional<PackageOffer> FlatpakPackageCatalog::candidateOffer(const QString &packageName)
{
    ensureSnapshotLoaded();

    // Bereits installiert: diese Fassung ist der Kandidat (Abschnitt 3.2).
    const auto it = m_installedByAppId.constFind(packageName);
    if (it != m_installedByAppId.constEnd() && !it->installedPackages.isEmpty()) {
        PackageOffer offer;
        offer.available = true;
        offer.isCandidate = true;
        offer.packages = it->installedPackages;
        return offer;
    }

    // Sonst das Angebot aus den konfigurierten Remotes.
    ensureRemoteOffersLoaded();
    const auto remoteIt = m_remoteOffersByAppId.constFind(packageName);
    if (remoteIt != m_remoteOffersByAppId.constEnd()) {
        return *remoteIt;
    }
    return std::nullopt;
}

InstalledState FlatpakPackageCatalog::installedStateForPackage(const QString &packageName)
{
    ensureSnapshotLoaded();
    return m_installedByAppId.value(packageName);
}

QList<InstalledPackage> FlatpakPackageCatalog::allInstalledPackages()
{
    ensureSnapshotLoaded();
    return m_installedPackages;
}

QList<PackageRef> FlatpakPackageCatalog::findPackagesProvidingFile(const QString &filePath)
{
    Q_UNUSED(filePath);
    return {};
}

QList<PackageOffer> FlatpakPackageCatalog::searchPackages(const QString &query)
{
    const QString needle = query.trimmed();
    if (needle.isEmpty()) {
        return {};
    }

    ensureSnapshotLoaded();
    ensureRemoteOffersLoaded();

    QList<PackageOffer> results;
    QSet<QString> seen;
    for (auto it = m_remoteOffersByAppId.constBegin(); it != m_remoteOffersByAppId.constEnd(); ++it) {
        if (it->packages.isEmpty()) {
            continue;
        }
        const QString appId = it->packages.first().name;
        if (seen.contains(appId) || !appId.contains(needle, Qt::CaseInsensitive)) {
            continue;
        }
        seen.insert(appId);
        results.append(*it);
    }
    return results;
}

void FlatpakPackageCatalog::prepareSnapshot(const QStringList &names)
{
    Q_UNUSED(names);
    ensureSnapshotLoaded();
}

void FlatpakPackageCatalog::reload()
{
    m_snapshotLoaded = false;
    m_remoteOffersLoaded = false;
    ++m_generation;
    ensureSnapshotLoaded();
}

} // namespace lut
