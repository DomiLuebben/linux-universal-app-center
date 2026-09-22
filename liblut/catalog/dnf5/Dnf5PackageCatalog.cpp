#include "Dnf5PackageCatalog.h"
#include "liblut/backend/Validation.h"
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace lut {

namespace {

QProcessEnvironment dnfEnvironment() {
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    env.insert(QStringLiteral("TERM"), QStringLiteral("dumb"));
    return env;
}

const QString availableQueryFormat =
    QStringLiteral("%{name}\x1f%{epoch}\x1f%{version}\x1f%{release}\x1f%{evr}\x1f%{arch}\x1f%{downloadsize}\x1f%{installsize}\x1f%{repoid}\x1f%{summary}\x1e");

const QString installedQueryFormat =
    QStringLiteral("%{name}\x1f%{epoch}\x1f%{version}\x1f%{release}\x1f%{evr}\x1f%{arch}\x1f%{installsize}\x1f%{from_repo}\x1e");

const QString fileQueryFormat =
    QStringLiteral("%{name}\x1f%{evr}\x1f%{arch}\x1f%{repoid}\x1e");

const QString allInstalledFormat =
    QStringLiteral("%{name}\x1f%{evr}\x1f%{arch}\x1f%{installsize}\x1f%{from_repo}\x1f%{installtime}\x1f%{summary}\x1e");

} // namespace

Dnf5PackageCatalog::Dnf5PackageCatalog(const QString &program)
    : m_program(program) {
}

quint64 Dnf5PackageCatalog::catalogGeneration() const {
    QMutexLocker locker(&m_mutex);
    return m_generation;
}

void Dnf5PackageCatalog::reload() {
    QMutexLocker locker(&m_mutex);
    m_generation++;
    m_offersCache.clear();
    m_installedCache.clear();
}

QByteArray Dnf5PackageCatalog::runQuery(const QStringList &args) const {
    QProcess process;
    process.setProcessEnvironment(dnfEnvironment());
    process.start(m_program, args);
    process.closeWriteChannel();
    if (!process.waitForStarted(5000)) {
        return {};
    }
    if (!process.waitForFinished(60000)) {
        process.kill();
        process.waitForFinished();
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return {};
    }
    return process.readAllStandardOutput();
}

QList<PackageOffer> Dnf5PackageCatalog::parseAvailableOffers(const QByteArray &data) {
    QList<PackageOffer> offers;
    const QString text = QString::fromUtf8(data);
    const QStringList records = text.split(QChar(0x1e), Qt::SkipEmptyParts);

    for (const auto &record : records) {
        const QString trimmed = record.trimmed();
        if (trimmed.isEmpty()) continue;
        const QStringList fields = trimmed.split(QChar(0x1f));
        if (fields.size() < 10) continue;

        const QString name = fields[0];
        if (!Validation::isValidPackageName(name)) continue;

        const QString evr = fields[4];
        const QString arch = fields[5];
        const qint64 downloadSize = fields[6].toLongLong();
        const qint64 installSize = fields[7].toLongLong();
        const QString repoId = fields[8];

        PackageOffer offer;
        offer.packages = {PackageRef{
            QStringLiteral("dnf5"),
            repoId,
            name,
            arch,
            evr
        }};
        offer.downloadSize = downloadSize;
        offer.installedSize = installSize;
        offer.available = true;
        offer.isCandidate = true;
        // Priorität: Updates-Repo hat Vorrang vor Base-Fedora
        offer.priority = repoId.contains(QLatin1String("updates")) ? 110 : 100;

        offers.append(offer);
    }

    if (offers.size() > 1) {
        // Sortiere nach Priorität absteigend
        std::sort(offers.begin(), offers.end(), [](const PackageOffer &a, const PackageOffer &b) {
            return a.priority > b.priority;
        });
        // Nur das oberste Angebot ist Standardkandidat
        for (int i = 1; i < offers.size(); ++i) {
            offers[i].isCandidate = false;
        }
    }

    return offers;
}

InstalledState Dnf5PackageCatalog::parseInstalledState(const QByteArray &data, const QString &packageName) {
    InstalledState state;
    state.isFullyInstalled = false;
    state.isPartiallyInstalled = false;

    const QString text = QString::fromUtf8(data);
    const QStringList records = text.split(QChar(0x1e), Qt::SkipEmptyParts);

    for (const auto &record : records) {
        const QString trimmed = record.trimmed();
        if (trimmed.isEmpty()) continue;
        const QStringList fields = trimmed.split(QChar(0x1f));
        if (fields.size() < 8) continue;

        if (fields[0] == packageName) {
            state.isFullyInstalled = true;
            state.origin = QStringLiteral("dnf5");
            state.installedPackages = {PackageRef{
                QStringLiteral("dnf5"),
                fields[7],
                fields[0],
                fields[5],
                fields[4]
            }};
            break;
        }
    }

    return state;
}

QList<PackageRef> Dnf5PackageCatalog::parseFileProviders(const QByteArray &data) {
    QList<PackageRef> result;
    const QString text = QString::fromUtf8(data);
    const QStringList records = text.split(QChar(0x1e), Qt::SkipEmptyParts);

    for (const auto &record : records) {
        const QString trimmed = record.trimmed();
        if (trimmed.isEmpty()) continue;
        const QStringList fields = trimmed.split(QChar(0x1f));
        if (fields.size() < 4) continue;

        const QString name = fields[0];
        if (!Validation::isValidPackageName(name)) continue;

        PackageRef ref;
        ref.backend = QStringLiteral("dnf5");
        ref.name = name;
        ref.version = fields[1];
        ref.arch = fields[2];
        ref.repoId = fields[3];
        result.append(ref);
    }

    return result;
}

QList<InstalledPackage> Dnf5PackageCatalog::parseInstalledPackages(const QByteArray &data, const QByteArray &unneededData) {
    QList<InstalledPackage> result;
    const auto orphanList = QString::fromUtf8(unneededData).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const QSet<QString> orphans(orphanList.begin(), orphanList.end());

    const QString text = QString::fromUtf8(data);
    const QStringList records = text.split(QChar(0x1e), Qt::SkipEmptyParts);

    for (const auto &record : records) {
        const QString trimmed = record.trimmed();
        if (trimmed.isEmpty()) continue;
        const QStringList fields = trimmed.split(QChar(0x1f));
        if (fields.size() < 7) continue;

        InstalledPackage pkg;
        pkg.name = fields[0];
        pkg.version = fields[1];
        pkg.arch = fields[2];
        pkg.installedSize = fields[3].toLongLong();
        pkg.repo = fields[4];
        pkg.installDate = QDateTime::fromSecsSinceEpoch(fields[5].toLongLong()).toString(Qt::ISODate);
        pkg.summary = fields[6];
        pkg.description = pkg.summary;
        pkg.id = pkg.name + QLatin1Char('-') + pkg.version + QLatin1Char('.') + pkg.arch;
        pkg.isOrphan = orphans.contains(pkg.name + QLatin1Char('.') + pkg.arch);
        result.append(pkg);
    }

    return result;
}

QList<PackageOffer> Dnf5PackageCatalog::offersForPackage(const QString &packageName) {
    if (!Validation::isValidPackageName(packageName)) return {};

    QMutexLocker locker(&m_mutex);
    auto it = m_offersCache.find(packageName);
    if (it != m_offersCache.end()) {
        return it.value();
    }
    locker.unlock();

    QStringList args = {
        QStringLiteral("repoquery"),
        QStringLiteral("-q"),
        QStringLiteral("--available"),
        packageName,
        QStringLiteral("--queryformat"),
        availableQueryFormat
    };

    QByteArray out = runQuery(args);
    QList<PackageOffer> offers = parseAvailableOffers(out);

    locker.relock();
    m_offersCache.insert(packageName, offers);
    return offers;
}

std::optional<PackageOffer> Dnf5PackageCatalog::candidateOffer(const QString &packageName) {
    const auto offers = offersForPackage(packageName);
    for (const auto &offer : offers) {
        if (offer.isCandidate) return offer;
    }
    if (!offers.isEmpty()) return offers.first();
    return std::nullopt;
}

InstalledState Dnf5PackageCatalog::installedStateForPackage(const QString &packageName) {
    if (!Validation::isValidPackageName(packageName)) return {};

    QMutexLocker locker(&m_mutex);
    auto it = m_installedCache.find(packageName);
    if (it != m_installedCache.end()) {
        return it.value();
    }
    locker.unlock();

    QStringList args = {
        QStringLiteral("repoquery"),
        QStringLiteral("-q"),
        QStringLiteral("--installed"),
        packageName,
        QStringLiteral("--queryformat"),
        installedQueryFormat
    };

    QByteArray out = runQuery(args);
    InstalledState state = parseInstalledState(out, packageName);
    state.inventoryRevision = catalogGeneration();

    if (state.isFullyInstalled) {
        // Startbare Desktop-IDs ermitteln
        QStringList fileArgs = {
            QStringLiteral("repoquery"),
            QStringLiteral("-q"),
            QStringLiteral("--installed"),
            QStringLiteral("-l"),
            packageName
        };
        const QString filesStr = QString::fromUtf8(runQuery(fileArgs));
        for (const auto &line : filesStr.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QString trimmed = line.trimmed();
            if (trimmed.endsWith(QLatin1String(".desktop")) &&
                (trimmed.contains(QLatin1String("applications/")) || trimmed.startsWith(QLatin1String("/usr/share/applications/")))) {
                QString desktopId = QFileInfo(trimmed).fileName();
                if (!state.launchableDesktopIds.contains(desktopId)) {
                    state.launchableDesktopIds.append(desktopId);
                }
            }
        }
    }

    locker.relock();
    m_installedCache.insert(packageName, state);
    return state;
}

QList<InstalledPackage> Dnf5PackageCatalog::allInstalledPackages() {
    QStringList args = {
        QStringLiteral("repoquery"),
        QStringLiteral("-q"),
        QStringLiteral("--installed"),
        QStringLiteral("--queryformat"),
        allInstalledFormat
    };
    QByteArray data = runQuery(args);

    QStringList orphanArgs = {
        QStringLiteral("repoquery"),
        QStringLiteral("-q"),
        QStringLiteral("--unneeded"),
        QStringLiteral("--queryformat"),
        QStringLiteral("%{name}.%{arch}\n")
    };
    QByteArray orphanData = runQuery(orphanArgs);

    return parseInstalledPackages(data, orphanData);
}

QList<PackageRef> Dnf5PackageCatalog::findPackagesProvidingFile(const QString &filePath) {
    if (filePath.isEmpty()) return {};

    QStringList args = {
        QStringLiteral("repoquery"),
        QStringLiteral("-q"),
        QStringLiteral("-f"),
        filePath,
        QStringLiteral("--queryformat"),
        fileQueryFormat
    };

    QByteArray out = runQuery(args);
    return parseFileProviders(out);
}

QList<PackageOffer> Dnf5PackageCatalog::searchPackages(const QString &query) {
    if (query.trimmed().isEmpty()) return {};

    QString pattern = QStringLiteral("*") + query.trimmed() + QStringLiteral("*");
    QStringList args = {
        QStringLiteral("repoquery"),
        QStringLiteral("-q"),
        QStringLiteral("--available"),
        QStringLiteral("--latest-limit=1"),
        pattern,
        QStringLiteral("--queryformat"),
        availableQueryFormat
    };

    QByteArray out = runQuery(args);
    return parseAvailableOffers(out);
}

namespace {

int rpmvercmpPart(const QString &s1, const QString &s2) {
    int i1 = 0, i2 = 0;
    const int n1 = s1.length(), n2 = s2.length();
    while (i1 < n1 || i2 < n2) {
        while (i1 < n1 && !s1[i1].isLetterOrNumber() && s1[i1] != QLatin1Char('~') && s1[i1] != QLatin1Char('^')) ++i1;
        while (i2 < n2 && !s2[i2].isLetterOrNumber() && s2[i2] != QLatin1Char('~') && s2[i2] != QLatin1Char('^')) ++i2;

        if ((i1 < n1 && s1[i1] == QLatin1Char('~')) || (i2 < n2 && s2[i2] == QLatin1Char('~'))) {
            if (i1 < n1 && s1[i1] == QLatin1Char('~') && (i2 >= n2 || s2[i2] != QLatin1Char('~'))) return -1;
            if (i2 < n2 && s2[i2] == QLatin1Char('~') && (i1 >= n1 || s1[i1] != QLatin1Char('~'))) return 1;
            ++i1; ++i2;
            continue;
        }

        if ((i1 < n1 && s1[i1] == QLatin1Char('^')) || (i2 < n2 && s2[i2] == QLatin1Char('^'))) {
            if (i1 < n1 && s1[i1] == QLatin1Char('^') && (i2 >= n2 || s2[i2] != QLatin1Char('^'))) return (i2 >= n2) ? 1 : -1;
            if (i2 < n2 && s2[i2] == QLatin1Char('^') && (i1 >= n1 || s1[i1] != QLatin1Char('^'))) return (i1 >= n1) ? -1 : 1;
            ++i1; ++i2;
            continue;
        }

        if (i1 >= n1 || i2 >= n2) break;

        const bool isNum = s1[i1].isDigit();
        if (isNum != s2[i2].isDigit()) {
            return isNum ? 1 : -1;
        }

        int start1 = i1;
        while (i1 < n1 && (isNum ? s1[i1].isDigit() : s1[i1].isLetter())) ++i1;
        QString seg1 = s1.mid(start1, i1 - start1);

        int start2 = i2;
        while (i2 < n2 && (isNum ? s2[i2].isDigit() : s2[i2].isLetter())) ++i2;
        QString seg2 = s2.mid(start2, i2 - start2);

        if (isNum) {
            int z1 = 0; while (z1 < seg1.length() && seg1[z1] == QLatin1Char('0')) ++z1;
            int z2 = 0; while (z2 < seg2.length() && seg2[z2] == QLatin1Char('0')) ++z2;
            QString t1 = seg1.mid(z1);
            QString t2 = seg2.mid(z2);
            if (t1.length() != t2.length()) {
                return t1.length() > t2.length() ? 1 : -1;
            }
            int cmp = QString::compare(t1, t2);
            if (cmp != 0) return cmp > 0 ? 1 : -1;
        } else {
            int cmp = QString::compare(seg1, seg2);
            if (cmp != 0) return cmp > 0 ? 1 : -1;
        }
    }
    if (i1 >= n1 && i2 >= n2) return 0;
    return (i1 < n1) ? 1 : -1;
}

void splitRpmEvr(const QString &evr, qint64 &epoch, QString &ver, QString &rel) {
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
        rel = rem.mid(hyphen + 1);
    } else {
        ver = rem;
        rel.clear();
    }
}

} // namespace

int Dnf5PackageCatalog::compareVersions(const QString &v1, const QString &v2) const {
    qint64 e1 = 0, e2 = 0;
    QString ver1, ver2, rel1, rel2;
    splitRpmEvr(v1, e1, ver1, rel1);
    splitRpmEvr(v2, e2, ver2, rel2);

    if (e1 != e2) {
        return e1 > e2 ? 1 : -1;
    }
    int vcmp = rpmvercmpPart(ver1, ver2);
    if (vcmp != 0) {
        return vcmp;
    }
    return rpmvercmpPart(rel1, rel2);
}

} // namespace lut
