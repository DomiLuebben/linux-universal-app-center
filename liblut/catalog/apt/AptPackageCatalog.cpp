#include "AptPackageCatalog.h"
#include "liblut/backend/Validation.h"
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <limits>
#include <algorithm>

namespace lut {

namespace {

QProcessEnvironment aptEnvironment() {
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    env.insert(QStringLiteral("DEBIAN_FRONTEND"), QStringLiteral("noninteractive"));
    env.insert(QStringLiteral("PAGER"), QStringLiteral("cat"));
    return env;
}

} // namespace

AptPackageCatalog::AptPackageCatalog(const QString &aptCacheProgram,
                                     const QString &dpkgQueryProgram)
    : m_aptCacheProgram(aptCacheProgram)
    , m_dpkgQueryProgram(dpkgQueryProgram)
{
}

quint64 AptPackageCatalog::catalogGeneration() const {
    QMutexLocker locker(&m_mutex);
    return m_generation;
}

void AptPackageCatalog::reload() {
    QMutexLocker locker(&m_mutex);
    m_offersCache.clear();
    m_installedCache.clear();
    m_generation++;
}

QByteArray AptPackageCatalog::runCommand(const QString &program, const QStringList &args) const {
    QProcess process;
    process.setProcessEnvironment(aptEnvironment());
    process.start(program, args);
    process.closeWriteChannel();

    if (!process.waitForStarted(5000) || !process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished();
        return {};
    }

    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        return process.readAllStandardOutput();
    }
    return {};
}

QString AptPackageCatalog::parsePolicyCandidate(const QString &policyOutput) {
    static const QRegularExpression regex(QStringLiteral(R"(Candidate:\s*(\S+))"));
    auto match = regex.match(policyOutput);
    if (match.hasMatch()) {
        const QString ver = match.captured(1).trimmed();
        if (ver != QLatin1String("(none)")) {
            return ver;
        }
    }
    return {};
}

QString AptPackageCatalog::parsePolicyInstalled(const QString &policyOutput) {
    static const QRegularExpression regex(QStringLiteral(R"(Installed:\s*(\S+))"));
    auto match = regex.match(policyOutput);
    if (match.hasMatch()) {
        const QString ver = match.captured(1).trimmed();
        if (ver != QLatin1String("(none)")) {
            return ver;
        }
    }
    return {};
}

QList<PackageOffer> AptPackageCatalog::parseAvailableOffers(const QString &policyOutput,
                                                           const QString &showOutput,
                                                           const QString &packageName) {
    Q_UNUSED(packageName);
    QList<PackageOffer> offers;
    const QString candidateVersion = parsePolicyCandidate(policyOutput);

    // Zerlege apt-cache show in einzelne Strophen
    QMap<QString, QString> fields;
    const auto flushRecord = [&]() {
        if (!fields.contains(QStringLiteral("Package"))) {
            fields.clear();
            return;
        }

        const QString name = fields.value(QStringLiteral("Package"));
        const QString ver = fields.value(QStringLiteral("Version"));
        const QString arch = fields.value(QStringLiteral("Architecture"));
        const QString section = fields.value(QStringLiteral("Section"));

        bool ok = false;
        qint64 downloadSize = fields.value(QStringLiteral("Size")).toLongLong(&ok);
        if (!ok || downloadSize < 0) {
            downloadSize = 0;
        }

        qint64 installedSizeKb = fields.value(QStringLiteral("Installed-Size")).toLongLong(&ok);
        qint64 installedSize = 0;
        if (ok && installedSizeKb >= 0 && installedSizeKb <= std::numeric_limits<qint64>::max() / 1024) {
            installedSize = installedSizeKb * 1024;
        }

        PackageRef ref;
        ref.backend = QStringLiteral("apt");
        ref.name = name;
        ref.arch = arch;
        ref.version = ver;
        ref.repoId = section;

        PackageOffer offer;
        offer.packages = {ref};
        offer.downloadSize = downloadSize;
        offer.installedSize = installedSize;
        offer.available = true;

        if (!candidateVersion.isEmpty() && ver == candidateVersion) {
            offer.isCandidate = true;
            offer.priority = 100;
        } else {
            offer.isCandidate = false;
            offer.priority = 50;
        }

        // Vermeide Duplikate derselben Version & Architektur
        bool duplicate = false;
        for (const auto &existing : offers) {
            if (!existing.packages.isEmpty() &&
                existing.packages.first().version == ref.version &&
                existing.packages.first().arch == ref.arch) {
                duplicate = true;
                break;
            }
        }

        if (!duplicate) {
            offers.append(offer);
        }
        fields.clear();
    };

    for (const auto &line : showOutput.split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty()) {
            flushRecord();
            continue;
        }
        if (line.startsWith(QLatin1Char(' '))) {
            continue; // Fortsetzungszeile der Description ignorieren
        }
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon > 0) {
            fields.insert(line.left(colon).trimmed(), line.mid(colon + 1).trimmed());
        }
    }
    flushRecord();

    // Sortiere Kandidat nach oben
    std::stable_sort(offers.begin(), offers.end(), [](const PackageOffer &a, const PackageOffer &b) {
        if (a.isCandidate != b.isCandidate) {
            return a.isCandidate > b.isCandidate;
        }
        return a.priority > b.priority;
    });

    return offers;
}

InstalledState AptPackageCatalog::parseInstalledState(const QString &dpkgQueryOutput,
                                                     const QString &packageName,
                                                     const QStringList &desktopFiles) {
    Q_UNUSED(packageName);
    InstalledState state;
    state.isFullyInstalled = false;
    state.isPartiallyInstalled = false;
    state.origin = QStringLiteral("apt");

    for (const auto &line : dpkgQueryOutput.split(QLatin1Char('\n'))) {
        const auto fields = line.split(QLatin1Char('\t'));
        if (fields.size() < 4) {
            continue;
        }

        const QString status = fields[0].trimmed();
        const QString name = fields[1].trimmed();
        const QString version = fields[2].trimmed();
        const QString arch = fields[3].trimmed();

        if (!packageName.isEmpty() && name != packageName) {
            continue;
        }

        // Strikte Abnahme nach APT-03: Nur der explizite dpkg-Status "installed" gilt als installiert.
        // Pakete im Status "config-files" (rc) oder "not-installed" gelten als deinstalliert!
        if (status == QLatin1String("installed")) {
            PackageRef ref;
            ref.backend = QStringLiteral("apt");
            ref.name = name;
            ref.version = version;
            ref.arch = arch;
            ref.repoId = QString();

            state.installedPackages.append(ref);
            state.isFullyInstalled = true;
        }
    }

    if (state.isFullyInstalled) {
        state.launchableDesktopIds = desktopFiles;
    }

    return state;
}

QList<InstalledPackage> AptPackageCatalog::parseInstalledPackages(const QString &dpkgQueryOutput) {
    QList<InstalledPackage> result;

    for (const auto &line : dpkgQueryOutput.split(QLatin1Char('\n'))) {
        const auto fields = line.split(QLatin1Char('\t'));
        if (fields.size() < 5) {
            continue;
        }

        const QString status = fields[0].trimmed();
        if (status != QLatin1String("installed")) {
            continue;
        }

        InstalledPackage pkg;
        pkg.name = fields[1].trimmed();
        pkg.version = fields[2].trimmed();
        pkg.arch = fields[3].trimmed();

        bool ok = false;
        qint64 sizeKb = fields[4].trimmed().toLongLong(&ok);
        pkg.installedSize = (ok && sizeKb > 0) ? (sizeKb * 1024) : 0;

        if (fields.size() > 5) {
            pkg.summary = fields[5].trimmed();
            pkg.description = pkg.summary;
        }
        pkg.id = pkg.name + QLatin1Char(':') + pkg.arch;

        result.append(pkg);
    }

    return result;
}

QList<PackageRef> AptPackageCatalog::parseFileProviders(const QString &dpkgSearchOutput) {
    QList<PackageRef> result;

    for (const auto &line : dpkgSearchOutput.split(QLatin1Char('\n'))) {
        const int colon = line.indexOf(QLatin1String(": "));
        if (colon <= 0) {
            continue;
        }

        QString rawPkg = line.left(colon).trimmed();
        QString arch;
        const int archSep = rawPkg.indexOf(QLatin1Char(':'));
        if (archSep > 0) {
            arch = rawPkg.mid(archSep + 1).trimmed();
            rawPkg = rawPkg.left(archSep).trimmed();
        }

        if (Validation::isValidPackageName(rawPkg)) {
            PackageRef ref;
            ref.backend = QStringLiteral("apt");
            ref.name = rawPkg;
            ref.arch = arch;
            result.append(ref);
        }
    }

    return result;
}

QList<PackageOffer> AptPackageCatalog::offersForPackage(const QString &packageName) {
    if (!Validation::isValidPackageName(packageName)) {
        return {};
    }

    {
        QMutexLocker locker(&m_mutex);
        auto it = m_offersCache.find(packageName);
        if (it != m_offersCache.end()) {
            return it.value();
        }
    }

    const QByteArray policyData = runCommand(m_aptCacheProgram, {QStringLiteral("policy"), packageName});
    const QByteArray showData = runCommand(m_aptCacheProgram, {QStringLiteral("show"), packageName});

    const auto offers = parseAvailableOffers(QString::fromUtf8(policyData),
                                             QString::fromUtf8(showData),
                                             packageName);

    {
        QMutexLocker locker(&m_mutex);
        m_offersCache.insert(packageName, offers);
    }

    return offers;
}

std::optional<PackageOffer> AptPackageCatalog::candidateOffer(const QString &packageName) {
    const auto offers = offersForPackage(packageName);
    for (const auto &offer : offers) {
        if (offer.isCandidate) {
            return offer;
        }
    }
    if (!offers.isEmpty()) {
        return offers.first();
    }
    return std::nullopt;
}

InstalledState AptPackageCatalog::installedStateForPackage(const QString &packageName) {
    if (!Validation::isValidPackageName(packageName)) {
        return {};
    }

    {
        QMutexLocker locker(&m_mutex);
        auto it = m_installedCache.find(packageName);
        if (it != m_installedCache.end()) {
            return it.value();
        }
    }

    const QString format = QStringLiteral("${db:Status-Status}\t${Package}\t${Version}\t${Architecture}\t${Installed-Size}\t${binary:Summary}\\n");
    const QByteArray queryData = runCommand(m_dpkgQueryProgram, {QStringLiteral("-W"), QStringLiteral("-f=") + format, packageName});

    QStringList desktopFiles;
    // Prüfe, ob Desktop-Dateien installiert sind
    const QByteArray fileListData = runCommand(m_dpkgQueryProgram, {QStringLiteral("-L"), packageName});
    for (const auto &line : QString::fromUtf8(fileListData).split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("/usr/share/applications/")) && trimmed.endsWith(QLatin1String(".desktop"))) {
            desktopFiles.append(trimmed.section(QLatin1Char('/'), -1));
        }
    }

    const auto state = parseInstalledState(QString::fromUtf8(queryData), packageName, desktopFiles);

    {
        QMutexLocker locker(&m_mutex);
        m_installedCache.insert(packageName, state);
    }

    return state;
}

QList<InstalledPackage> AptPackageCatalog::allInstalledPackages() {
    const QString format = QStringLiteral("${db:Status-Status}\t${Package}\t${Version}\t${Architecture}\t${Installed-Size}\t${binary:Summary}\\n");
    const QByteArray output = runCommand(m_dpkgQueryProgram, {QStringLiteral("-W"), QStringLiteral("-f=") + format});
    return parseInstalledPackages(QString::fromUtf8(output));
}

QList<PackageRef> AptPackageCatalog::findPackagesProvidingFile(const QString &filePath) {
    const QByteArray output = runCommand(m_dpkgQueryProgram, {QStringLiteral("-S"), filePath});
    return parseFileProviders(QString::fromUtf8(output));
}

QList<PackageOffer> AptPackageCatalog::searchPackages(const QString &query) {
    if (query.trimmed().isEmpty()) {
        return {};
    }

    const QByteArray output = runCommand(m_aptCacheProgram, {QStringLiteral("search"), query});
    QList<PackageOffer> results;

    int count = 0;
    for (const auto &line : QString::fromUtf8(output).split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;

        const int space = trimmed.indexOf(QLatin1Char(' '));
        if (space <= 0) continue;

        const QString pkgName = trimmed.left(space);

        if (!Validation::isValidPackageName(pkgName)) continue;

        PackageRef ref;
        ref.backend = QStringLiteral("apt");
        ref.name = pkgName;

        PackageOffer offer;
        offer.packages = {ref};
        offer.available = true;

        results.append(offer);
        if (++count >= 50) break;
    }

    return results;
}

namespace {

int dpkgOrder(QChar c) {
    if (c.isNull()) return 0;
    if (c == QLatin1Char('~')) return -1;
    if (c.isLetter()) return c.unicode();
    if (!c.isDigit()) return c.unicode() + 256;
    return 0;
}

int dpkgvercmpPart(const QString &s1, const QString &s2) {
    int i1 = 0, i2 = 0;
    const int n1 = s1.length(), n2 = s2.length();
    while (i1 < n1 || i2 < n2) {
        int firstDiff = 0;
        while ((i1 < n1 && !s1[i1].isDigit()) || (i2 < n2 && !s2[i2].isDigit())) {
            const int ac = (i1 < n1) ? dpkgOrder(s1[i1]) : 0;
            const int bc = (i2 < n2) ? dpkgOrder(s2[i2]) : 0;
            if (ac != bc) return ac < bc ? -1 : 1;
            if (i1 < n1) ++i1;
            if (i2 < n2) ++i2;
        }
        while (i1 < n1 && s1[i1] == QLatin1Char('0')) ++i1;
        while (i2 < n2 && s2[i2] == QLatin1Char('0')) ++i2;
        int d1 = 0, d2 = 0;
        while (i1 + d1 < n1 && s1[i1 + d1].isDigit()) ++d1;
        while (i2 + d2 < n2 && s2[i2 + d2].isDigit()) ++d2;
        if (d1 != d2) return d1 < d2 ? -1 : 1;
        while (d1 > 0) {
            if (firstDiff == 0 && s1[i1] != s2[i2]) {
                firstDiff = (s1[i1] < s2[i2]) ? -1 : 1;
            }
            ++i1; ++i2; --d1;
        }
        if (firstDiff != 0) return firstDiff;
    }
    return 0;
}

void splitDpkgEvr(const QString &evr, qint64 &epoch, QString &ver, QString &rev) {
    epoch = 0;
    QString rem = evr.trimmed();
    int colon = rem.indexOf(QLatin1Char(':'));
    if (colon >= 0) {
        epoch = rem.left(colon).toLongLong();
        rem = rem.mid(colon + 1);
    }
    int hyphen = rem.lastIndexOf(QLatin1Char('-'));
    if (hyphen >= 0) {
        ver = rem.left(hyphen);
        rev = rem.mid(hyphen + 1);
    } else {
        ver = rem;
        rev.clear();
    }
}

} // namespace

int AptPackageCatalog::compareVersions(const QString &v1, const QString &v2) const {
    qint64 e1 = 0, e2 = 0;
    QString ver1, ver2, rev1, rev2;
    splitDpkgEvr(v1, e1, ver1, rev1);
    splitDpkgEvr(v2, e2, ver2, rev2);

    if (e1 != e2) {
        return e1 > e2 ? 1 : -1;
    }
    int vcmp = dpkgvercmpPart(ver1, ver2);
    if (vcmp != 0) {
        return vcmp;
    }
    return dpkgvercmpPart(rev1, rev2);
}

} // namespace lut
