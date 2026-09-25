#include "Dnf5PackageCatalog.h"
#include "liblut/backend/Validation.h"
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
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
    m_inventory.reset();
    m_fileOwners.reset();
    m_offersCache.clear();
    m_installedCache.clear();
    m_repoScoreCache.clear();
    m_repoScoresLoaded = false;
}

void Dnf5PackageCatalog::prepareSnapshot(const QStringList &packageNames) {
    QStringList names;
    for (const auto &name : packageNames) if (Validation::isValidPackageName(name)) names.append(name);
    names.removeDuplicates();
    const auto scores = repoScores();
    for (qsizetype offset = 0; offset < names.size(); offset += 512) {
        const auto batch = names.mid(offset, 512);
        const auto output = runQuery(QStringList{QStringLiteral("--cacheonly"), QStringLiteral("repoquery"),
            QStringLiteral("-q"), QStringLiteral("--available"), QStringLiteral("--queryformat"), availableQueryFormat} + batch);
        QHash<QString, QByteArray> groups;
        for (const auto &record : output.split(char(0x1e))) {
            const auto name = QString::fromUtf8(record.split(char(0x1f)).value(0)).trimmed();
            groups[name] += record + char(0x1e);
        }
        QMutexLocker lock(&m_mutex);
        for (const auto &name : batch) m_offersCache.insert(name, parseAvailableOffers(groups.value(name), scores));
    }
    const auto inventory = allInstalledPackages();
    // Desktop-IDs aus demselben RPM-Dateiindex wie findPackagesProvidingFile();
    // vorher las jeder Ladevorgang die Dateiliste der RPM-Datenbank zweimal.
    QHash<QString, QStringList> desktops;
    const QString applications = QStringLiteral("/usr/share/applications/");
    const auto owners = fileOwners();
    for (auto it = owners.cbegin(); it != owners.cend(); ++it) {
        const QString &path = it.key();
        if (!path.startsWith(applications) || !path.endsWith(QLatin1String(".desktop"))) continue;
        const QString desktopId = path.mid(applications.size()).replace(QLatin1Char('/'), QLatin1Char('-'));
        for (const auto &ref : it.value()) desktops[ref.name].append(desktopId);
    }
    QMutexLocker lock(&m_mutex);
    for (const auto &name : names) m_installedCache.insert(name, {});
    for (const auto &pkg : inventory) {
        auto &state = m_installedCache[pkg.name];
        state.installedPackages.append(PackageRef{QStringLiteral("dnf5"), {}, pkg.name, pkg.arch, pkg.version});
        state.isFullyInstalled = true;
        state.origin = QStringLiteral("dnf5");
        state.inventoryRevision = m_generation;
        state.launchableDesktopIds = desktops.value(pkg.name);
    }
}

QMap<QString, int> Dnf5PackageCatalog::parseRepoScores(const QByteArray &json) {
    QMap<QString, int> scores;
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        return scores;
    }
    for (const auto &value : doc.array()) {
        const QJsonObject repo = value.toObject();
        const QString id = repo.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) continue;
        const int priority = repo.value(QStringLiteral("priority")).toInt(kDefaultRepoPriority);
        const int cost = repo.value(QStringLiteral("cost")).toInt(kDefaultRepoCost);
        scores.insert(id, repoScore(priority, cost));
    }
    return scores;
}

QMap<QString, int> Dnf5PackageCatalog::repoScores() const {
    {
        QMutexLocker locker(&m_mutex);
        if (m_repoScoresLoaded) {
            return m_repoScoreCache;
        }
    }

    // `dnf5 repo info --json` liefert die tatsächlich konfigurierte priority und
    // cost je Repository. Ohne diese Angaben gilt die DNF5-Vorgabe für alle
    // Repositories gleichermaßen; dann entscheidet allein die EVR-Version.
    const QByteArray out = runQuery({QStringLiteral("repo"), QStringLiteral("info"), QStringLiteral("--json")});
    const QMap<QString, int> scores = parseRepoScores(out);

    QMutexLocker locker(&m_mutex);
    m_repoScoreCache = scores;
    m_repoScoresLoaded = true;
    return m_repoScoreCache;
}

QByteArray Dnf5PackageCatalog::runQuery(const QStringList &args) const {
    // Der Katalog läuft als Benutzer: nur den System-Cache lesen, den lutd
    // aktuell hält. Sonst lädt DNF5 alle Metadaten in ~/.cache nach und
    // überschreitet bei abgelaufenem Cache die Wartezeit.
    QStringList fullArgs = args;
    if (!fullArgs.contains(QStringLiteral("--cacheonly"))) fullArgs.prepend(QStringLiteral("--cacheonly"));
    QProcess process;
    process.setProcessEnvironment(dnfEnvironment());
    process.start(m_program, fullArgs);
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

int compareEvr(const QString &v1, const QString &v2) {
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

} // namespace

QList<PackageOffer> Dnf5PackageCatalog::parseAvailableOffers(const QByteArray &data) {
    return parseAvailableOffers(data, {});
}

QList<PackageOffer> Dnf5PackageCatalog::parseAvailableOffers(const QByteArray &data, const QMap<QString, int> &repoScores) {
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
        // Rang aus der tatsächlichen Repository-Konfiguration; ohne Angabe gilt
        // die DNF5-Vorgabe, dann entscheidet allein die EVR-Version.
        offer.priority = repoScores.value(repoId, repoScore(kDefaultRepoPriority, kDefaultRepoCost));

        offers.append(offer);
    }

    if (offers.size() > 1) {
        // Sortiere primär nach nativer RPM-EVR-Version absteigend, sekundär nach Priorität
        std::sort(offers.begin(), offers.end(), [](const PackageOffer &a, const PackageOffer &b) {
            const QString verA = a.packages.isEmpty() ? QString() : a.packages.first().version;
            const QString verB = b.packages.isEmpty() ? QString() : b.packages.first().version;
            int cmp = compareEvr(verA, verB);
            if (cmp != 0) {
                return cmp > 0;
            }
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
    QList<PackageOffer> offers = parseAvailableOffers(out, repoScores());

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
        QStringLiteral("--cacheonly"),
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
            QStringLiteral("--cacheonly"),
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
    { QMutexLocker lock(&m_mutex); if (m_inventory) return *m_inventory; }
    // Nur die RPM-Datenbank abfragen: ohne --cacheonly lüde DNF5 als Benutzer
    // abgelaufene Metadaten nach und liefe in die Zeitüberschreitung.
    QStringList args = {
        QStringLiteral("--cacheonly"),
        QStringLiteral("repoquery"),
        QStringLiteral("-q"),
        QStringLiteral("--installed"),
        QStringLiteral("--queryformat"),
        allInstalledFormat
    };
    QByteArray data = runQuery(args);
    if (data.trimmed().isEmpty()) {
        QMutexLocker lock(&m_mutex);
        return m_lastGoodInventory;
    }

    QStringList orphanArgs = {
        QStringLiteral("--cacheonly"),
        QStringLiteral("repoquery"),
        QStringLiteral("-q"),
        QStringLiteral("--unneeded"),
        QStringLiteral("--queryformat"),
        QStringLiteral("%{name}.%{arch}\n")
    };
    QByteArray orphanData = runQuery(orphanArgs);

    const auto packages = parseInstalledPackages(data, orphanData);
    QMutexLocker lock(&m_mutex);
    m_inventory = packages;
    m_lastGoodInventory = packages;
    return packages;
}

QHash<QString, QList<PackageRef>> Dnf5PackageCatalog::loadFileOwners() {
    // Eine einzige RPM-Abfrage für alle Desktop- und Metainfo-Dateien. Vorher
    // lief je Datei ein eigener dnf5-Prozess (~3 s), bei einigen hundert
    // Anwendungen blieb der Katalog dadurch minutenlang leer.
    QHash<QString, QList<PackageRef>> owners;
    QProcess rpm;
    rpm.setProcessEnvironment(dnfEnvironment());
    rpm.start(QStringLiteral("/usr/bin/rpm"), {QStringLiteral("-qa"), QStringLiteral("--qf"),
        QStringLiteral("[%{=NAME}\t%{=EPOCHNUM}:%{=VERSION}-%{=RELEASE}\t%{=ARCH}\t%{FILENAMES}\n]")});
    rpm.closeWriteChannel();
    if (!rpm.waitForStarted(5000) || !rpm.waitForFinished(60000) || rpm.exitCode() != 0) {
        rpm.kill(); rpm.waitForFinished();
        return owners;
    }
    static const QStringList prefixes = {QStringLiteral("/usr/share/applications/"),
        QStringLiteral("/usr/share/metainfo/"), QStringLiteral("/usr/share/appdata/")};
    for (const auto &line : QString::fromUtf8(rpm.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const auto fields = line.split(QLatin1Char('\t'));
        if (fields.size() != 4) continue;
        const QString &path = fields[3];
        if (!std::any_of(prefixes.begin(), prefixes.end(), [&](const QString &p) { return path.startsWith(p); })) continue;
        if (!Validation::isValidPackageName(fields[0])) continue;
        QString evr = fields[1];
        if (evr.startsWith(QLatin1String("0:"))) evr.remove(0, 2);
        owners[path].append(PackageRef{QStringLiteral("dnf5"), QStringLiteral("@System"), fields[0], fields[2], evr});
    }
    return owners;
}

QHash<QString, QList<PackageRef>> Dnf5PackageCatalog::fileOwners() {
    {
        QMutexLocker lock(&m_mutex);
        if (m_fileOwners) return *m_fileOwners;
    }
    auto owners = loadFileOwners();
    QMutexLocker lock(&m_mutex);
    if (!m_fileOwners) m_fileOwners = std::move(owners);
    return *m_fileOwners;
}

QList<PackageRef> Dnf5PackageCatalog::findPackagesProvidingFile(const QString &filePath) {
    if (filePath.isEmpty()) return {};

    {
        const auto owners = fileOwners();
        auto it = owners.constFind(filePath);
        if (it != owners.cend()) return it.value();
        if (filePath.startsWith(QLatin1String("/usr/share/"))) return {};
    }

    // Dateien außerhalb der vorab gelesenen Verzeichnisse: einzeln bei RPM
    // nachfragen, das ist lokal und schnell.
    QProcess rpm;
    rpm.setProcessEnvironment(dnfEnvironment());
    rpm.start(QStringLiteral("/usr/bin/rpm"), {QStringLiteral("-qf"), QStringLiteral("--qf"),
        QStringLiteral("%{NAME}\t%{EPOCHNUM}:%{VERSION}-%{RELEASE}\t%{ARCH}\n"), filePath});
    rpm.closeWriteChannel();
    QList<PackageRef> result;
    if (!rpm.waitForStarted(5000) || !rpm.waitForFinished(10000) || rpm.exitCode() != 0) {
        rpm.kill(); rpm.waitForFinished();
        return result;
    }
    for (const auto &line : QString::fromUtf8(rpm.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const auto fields = line.split(QLatin1Char('\t'));
        if (fields.size() != 3 || !Validation::isValidPackageName(fields[0])) continue;
        QString evr = fields[1];
        if (evr.startsWith(QLatin1String("0:"))) evr.remove(0, 2);
        result.append(PackageRef{QStringLiteral("dnf5"), QStringLiteral("@System"), fields[0], fields[2], evr});
    }
    return result;
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
    return parseAvailableOffers(out, repoScores());
}

int Dnf5PackageCatalog::compareVersions(const QString &v1, const QString &v2) const {
    return compareEvr(v1, v2);
}

} // namespace lut
