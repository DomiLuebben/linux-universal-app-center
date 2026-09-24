#include "AurUpdates.h"
#include "AppSettings.h"
#include "DaemonClient.h"
#include "OperationResult.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QRegularExpression>
#include <algorithm>

#include "liblut/detect/DistroDetect.h"

#ifdef HAVE_ALPM
#include <alpm.h>
#endif

namespace lut {
namespace {

const QString kAurBase = QStringLiteral("https://aur.archlinux.org");
// Die AUR-Schnittstelle begrenzt die Zahl der Argumente je Anfrage.
constexpr int kBatchSize = 100;

QString cacheDir() {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
           + QStringLiteral("/aur");
}

bool toolExists(const QString &name) {
    return !QStandardPaths::findExecutable(name).isEmpty();
}

QProcessEnvironment cEnvironment() {
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    return env;
}

/// Führt einen Befehl bis zum Ende aus und liefert die Standardausgabe.
QByteArray runSync(const QString &program, const QStringList &arguments, int *exitCode) {
    QProcess process;
    process.setProcessEnvironment(cEnvironment());
    process.start(program, arguments);
    process.closeWriteChannel();
    if (!process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished();
        if (exitCode) *exitCode = -1;
        return {};
    }
    if (exitCode) *exitCode = process.exitCode();
    return process.readAllStandardOutput();
}

} // namespace

QByteArray AurUpdates::runProcess(const QString &program, const QStringList &arguments, int *exitCode) {
    if (m_processRunner) return m_processRunner(program, arguments, exitCode);
    return runSync(program, arguments, exitCode);
}

AurUpdates::AurUpdates(QObject *parent)
    : QAbstractListModel(parent) {
    const bool arch = DistroDetect::detectFamily() == DistroFamily::Arch;
    m_supported = arch && toolExists(QStringLiteral("git"))
                       && toolExists(QStringLiteral("makepkg"))
                       && toolExists(QStringLiteral("pacman"));
    m_enabled = false;
    if (arch && !m_supported) {
        m_statusMessage = tr("Für AUR-Aktualisierungen werden git, makepkg und pacman benötigt.");
    }
}

void AurUpdates::setAppSettings(AppSettings *settings) {
    m_settings = settings;
    if (m_settings) {
        setEnabled(m_settings->aurEnabled());
        connect(m_settings, &AppSettings::aurEnabledChanged, this, &AurUpdates::setEnabled);
    }
}

void AurUpdates::setEnabled(bool enabled) {
    if (m_busy && enabled != m_enabled) return;
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (m_settings && m_settings->aurEnabled() != enabled) {
        m_settings->setAurEnabled(enabled);
    }
    emit enabledChanged(m_enabled);
    emit availableChanged();

    if (!m_enabled) {
        beginResetModel();
        m_items.clear();
        endResetModel();
        m_hasChecked = false;
        emit countChanged();
        checkForeignPackages();
    } else {
        if (m_supported) {
            check();
        }
    }
}

void AurUpdates::checkForeignPackages() {
    if (!m_supported) {
        m_foreignPackageCount = 0;
        emit foreignPackageCountChanged();
        return;
    }
    int code = 0;
    const QByteArray out = runProcess(QStringLiteral("pacman"), {QStringLiteral("-Qm")}, &code);
    if (code == 0) {
        m_foreign = parseForeignPackages(out);
        m_foreignPackageCount = m_foreign.size();
    } else {
        m_foreignPackageCount = 0;
    }
    emit foreignPackageCountChanged();
}

void AurUpdates::setForceSupported(bool supported) {
    if (m_supported == supported) return;
    m_supported = supported;
    emit supportedChanged();
    emit availableChanged();
}

AurUpdates::~AurUpdates() = default;

int AurUpdates::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant AurUpdates::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) return {};
    const Entry &entry = m_items.at(index.row());
    switch (role) {
        case NameRole: return entry.name;
        case InstalledVersionRole: return entry.installed;
        case AvailableVersionRole: return entry.available;
        default: return {};
    }
}

QHash<int, QByteArray> AurUpdates::roleNames() const {
    return {
        {NameRole, "name"},
        {InstalledVersionRole, "installedVersion"},
        {AvailableVersionRole, "availableVersion"},
    };
}

QMap<QString, QString> AurUpdates::parseForeignPackages(const QByteArray &output) {
    QMap<QString, QString> result;
    const QStringList lines = QString::fromUtf8(output).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList parts = line.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 2) result.insert(parts.at(0), parts.at(1));
    }
    return result;
}

QHash<QString, AurUpdates::AurPackage> AurUpdates::parseAurInfo(const QByteArray &json, QString *error) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) *error = parseError.errorString();
        return {};
    }
    if (!doc.isObject()) {
        if (error) *error = QStringLiteral("Unerwartete Antwort der AUR-Schnittstelle.");
        return {};
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("type")).toString() == QLatin1String("error")) {
        if (error) {
            *error = root.value(QStringLiteral("error")).toString(
                QStringLiteral("Die AUR-Schnittstelle meldet einen Fehler."));
        }
        return {};
    }

    QHash<QString, AurPackage> result;
    const QJsonArray results = root.value(QStringLiteral("results")).toArray();
    for (const QJsonValue &value : results) {
        const QJsonObject obj = value.toObject();
        const QString name = obj.value(QStringLiteral("Name")).toString();
        if (name.isEmpty()) continue;
        AurPackage pkg;
        pkg.version = obj.value(QStringLiteral("Version")).toString();
        pkg.packageBase = obj.value(QStringLiteral("PackageBase")).toString();
        if (pkg.packageBase.isEmpty()) pkg.packageBase = name;
        result.insert(name, pkg);
    }
    return result;
}

int AurUpdates::compareVersions(const QString &left, const QString &right) {
#ifdef HAVE_ALPM
    // alpm_pkg_vercmp ist eine reine Funktion und braucht keinen Handle.
    const QByteArray l = left.toUtf8();
    const QByteArray r = right.toUtf8();
    return alpm_pkg_vercmp(l.constData(), r.constData());
#else
    int code = 0;
    const QByteArray out = runSync(QStringLiteral("vercmp"), {left, right}, &code);
    return code == 0 ? QString::fromUtf8(out).trimmed().toInt() : 0;
#endif
}

QList<AurUpdates::Entry> AurUpdates::selectOutdated(const QMap<QString, QString> &installed,
                                                    const QHash<QString, AurPackage> &remote) {
    QList<Entry> result;
    for (auto it = installed.constBegin(); it != installed.constEnd(); ++it) {
        const auto found = remote.constFind(it.key());
        if (found == remote.constEnd() || found->version.isEmpty()) continue;
        if (compareVersions(it.value(), found->version) < 0) {
            result.append(Entry{it.key(), it.value(), found->version});
        }
    }
    return result;
}

void AurUpdates::setBusy(bool busy) {
    if (m_busy == busy) return;
    m_busy = busy;
    emit busyChanged();
}

void AurUpdates::setStatus(const QString &message) {
    if (m_statusMessage == message) return;
    m_statusMessage = message;
    emit statusChanged();
}

void AurUpdates::fail(const QString &message) {
    setBusy(false);
    setStatus(message);
    emit failed(message);
}

QProcess *AurUpdates::makeProcess() {
    auto *process = new QProcess(this);
    process->setProcessEnvironment(cEnvironment());
    connect(process, &QProcess::finished, process, &QObject::deleteLater);
    return process;
}

void AurUpdates::check() {
    if (m_busy || !available()) return;
    setBusy(true);
    setStatus(tr("Prüfe AUR-Pakete …"));

    int code = 0;
    const QByteArray out = runProcess(QStringLiteral("pacman"), {QStringLiteral("-Qm")}, &code);
    if (code != 0) {
        fail(tr("Installierte AUR-Pakete konnten nicht ermittelt werden."));
        return;
    }

    m_foreign = parseForeignPackages(out);
    m_foreignPackageCount = m_foreign.size();
    emit foreignPackageCountChanged();
    m_remote.clear();
    m_checkError.clear();
    if (m_foreign.isEmpty()) {
        finishCheck();
        return;
    }

    if (!m_network) m_network = new QNetworkAccessManager(this);

    const QStringList names = m_foreign.keys();
    m_pendingReplies = 0;
    for (int start = 0; start < names.size(); start += kBatchSize) {
        requestAurInfo(names.mid(start, kBatchSize));
    }
}

void AurUpdates::requestAurInfo(const QStringList &names) {
    QUrlQuery query;
    for (const QString &name : names) query.addQueryItem(QStringLiteral("arg[]"), name);

    QUrl url(kAurBase + QStringLiteral("/rpc/v5/info"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("linux-universal-app-center"));
    request.setTransferTimeout(15000);

    ++m_pendingReplies;
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (m_checkError.isEmpty()) m_checkError = reply->errorString();
        } else {
            QString error;
            const auto batch = parseAurInfo(reply->readAll(), &error);
            if (!error.isEmpty()) {
                if (m_checkError.isEmpty()) m_checkError = error;
            } else {
                for (auto it = batch.constBegin(); it != batch.constEnd(); ++it) {
                    m_remote.insert(it.key(), it.value());
                }
            }
        }
        if (--m_pendingReplies == 0) finishCheck();
    });
}

void AurUpdates::finishCheck() {
    setBusy(false);

    // Eine gescheiterte Abfrage darf NICHT als "keine Aktualisierungen" enden.
    if (!m_checkError.isEmpty()) {
        fail(tr("AUR-Prüfung fehlgeschlagen: %1").arg(m_checkError));
        return;
    }

    beginResetModel();
    m_items = selectOutdated(m_foreign, m_remote);
    endResetModel();
    m_hasChecked = true;
    emit countChanged();
    setStatus(m_items.isEmpty()
        ? tr("Keine AUR-Aktualisierungen verfügbar.")
        : tr("%n AUR-Aktualisierung(en) verfügbar.", "", static_cast<int>(m_items.size())));
}

// --- Stapelaktualisierung -------------------------------------------------

QList<AurUpdates::BatchItem> AurUpdates::groupByBase(const QStringList &names, const QList<Entry> &outdated,
                                                     const QHash<QString, AurPackage> &remote) {
    QList<BatchItem> batch;
    QHash<QString, int> indexByBase;
    for (const QString &name : names) {
        const auto entry = std::find_if(outdated.cbegin(), outdated.cend(),
                                        [&name](const Entry &e) { return e.name == name; });
        if (entry == outdated.cend()) continue;

        const auto found = remote.constFind(name);
        const QString base = (found != remote.constEnd() && !found->packageBase.isEmpty())
                                 ? found->packageBase : name;
        const auto known = indexByBase.constFind(base);
        if (known != indexByBase.constEnd()) {
            if (!batch[*known].names.contains(name)) batch[*known].names.append(name);
            continue;
        }
        BatchItem item;
        item.base = base;
        item.names = {name};
        item.fromVersion = entry->installed;
        item.toVersion = entry->available;
        indexByBase.insert(base, static_cast<int>(batch.size()));
        batch.append(item);
    }
    return batch;
}

QString AurUpdates::packageNameFromFile(const QString &path) {
    // <pkgname>-<pkgver>-<pkgrel>-<arch>.pkg.tar.<ext>; pkgname darf selbst Bindestriche enthalten.
    QString file = QFileInfo(path).fileName();
    const qsizetype marker = file.indexOf(QStringLiteral(".pkg.tar"));
    if (marker <= 0) return {};
    file.truncate(marker);
    QStringList parts = file.split(QLatin1Char('-'));
    if (parts.size() < 4) return {};
    parts.removeLast(); // arch
    parts.removeLast(); // pkgrel
    parts.removeLast(); // pkgver
    return parts.join(QLatin1Char('-'));
}

QStringList AurUpdates::filterInstallable(const QStringList &files, const QSet<QString> &installedNames) {
    QStringList result;
    for (const QString &file : files) {
        const QString name = packageNameFromFile(file);
        if (!name.isEmpty() && installedNames.contains(name)) result.append(file);
    }
    return result;
}

double AurUpdates::batchFraction(Stage stage, int position, int count) {
    const double share = count > 0 ? qBound(0.0, double(position) / count, 1.0) : 0.0;
    switch (stage) {
    case Stage::Idle:    return 0.0;
    case Stage::Fetch:   return 0.1 * share;
    case Stage::Review:  return 0.1;
    case Stage::Build:   return 0.1 + 0.8 * share;
    case Stage::Install: return 0.9;
    }
    return 0.0;
}

bool AurUpdates::cancellable() const {
    // Ein Abbruch mitten in "pacman -U" wäre gefährlich.
    return m_stage == Stage::Fetch || m_stage == Stage::Review || m_stage == Stage::Build;
}

QProcess *AurUpdates::makeStreamingProcess() {
    QProcess *process = makeProcess();
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
        while (process->canReadLine()) {
            const QString line = QString::fromUtf8(process->readLine()).trimmed();
            if (!line.isEmpty()) emit logLine(line);
        }
    });
    return process;
}

void AurUpdates::reportProgress(const QString &text) {
    const int count = static_cast<int>(m_batch.size());
    const int step = m_stage == Stage::Install ? count : qMin(m_batchPos + 1, count);
    setStatus(text);
    emit operationProgress(batchFraction(m_stage, m_batchPos, count), false, step, count, text);
}

void AurUpdates::finishOperation(int result, const QString &message) {
    m_stage = Stage::Idle;
    m_cancelRequested = false;
    setBusy(false);
    setStatus(message);
    emit operationFinished(result, message);
    if (result == OperationResult::Failed) emit failed(message);
}

void AurUpdates::upgradeAll() {
    QStringList names;
    for (const Entry &entry : std::as_const(m_items)) names.append(entry.name);
    upgradePackages(names);
}

void AurUpdates::prepare(const QString &name) {
    upgradePackages({name});
}

void AurUpdates::upgradePackages(const QStringList &names) {
    if (m_busy || !available() || names.isEmpty()) return;
    if (m_daemonClient && m_daemonClient->isBusy()) {
        fail(tr("Eine andere Paketaktion läuft bereits."));
        return;
    }

    m_batch = groupByBase(names, m_items, m_remote);
    if (m_batch.isEmpty()) {
        fail(tr("Für die gewählten Pakete liegt keine AUR-Aktualisierung vor."));
        return;
    }
    QDir().mkpath(cacheDir());
    for (BatchItem &item : m_batch) item.dir = cacheDir() + QLatin1Char('/') + item.base;

    int packageCount = 0;
    for (const BatchItem &item : std::as_const(m_batch)) packageCount += item.names.size();
    m_operationTitle = packageCount == 1
        ? tr("AUR: %1 aktualisieren").arg(m_batch.first().names.first())
        : tr("AUR: %1 Pakete aktualisieren").arg(packageCount);

    m_stage = Stage::Fetch;
    m_batchPos = 0;
    m_cancelRequested = false;
    setBusy(true);
    emit operationStarted(m_operationTitle);
    fetchNext();
}

QString AurUpdates::buildReview(BatchItem &item) const {
    // Seit dem letzten eigenen Bau geänderte Dateien zeigen. Das ist, was man
    // wirklich prüfen muss; beim ersten Bau gibt es nur den ganzen PKGBUILD.
    QFile marker(item.dir + QStringLiteral("/.git/lut-built"));
    QString builtCommit;
    if (marker.open(QIODevice::ReadOnly)) builtCommit = QString::fromUtf8(marker.readAll()).trimmed();

    static const QRegularExpression commitPattern(QStringLiteral("^[0-9a-f]{7,64}$"));
    if (commitPattern.match(builtCommit).hasMatch()) {
        int code = 0;
        runSync(QStringLiteral("git"), {QStringLiteral("-C"), item.dir, QStringLiteral("cat-file"),
                                        QStringLiteral("-e"), builtCommit + QStringLiteral("^{commit}")}, &code);
        if (code == 0) {
            const QByteArray diff = runSync(QStringLiteral("git"),
                {QStringLiteral("-C"), item.dir, QStringLiteral("diff"), builtCommit, QStringLiteral("HEAD")}, &code);
            if (code == 0) {
                item.firstBuild = false;
                return diff.trimmed().isEmpty()
                    ? tr("Keine Änderungen seit dem letzten Bau mit dem Linux Universal App Center.")
                    : QString::fromUtf8(diff);
            }
        }
    }

    item.firstBuild = true;
    QFile pkgbuild(item.dir + QStringLiteral("/PKGBUILD"));
    if (!pkgbuild.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(pkgbuild.readAll());
}

void AurUpdates::fetchNext() {
    if (m_cancelRequested) {
        finishOperation(OperationResult::Cancelled, tr("Abgebrochen."));
        return;
    }
    if (m_batchPos >= m_batch.size()) {
        enterReview();
        return;
    }

    BatchItem &item = m_batch[m_batchPos];
    reportProgress(tr("Hole Quellen für %1 (%2 von %3)")
                       .arg(item.base).arg(m_batchPos + 1).arg(m_batch.size()));

    const bool vorhanden = QFileInfo::exists(item.dir + QStringLiteral("/.git"));
    if (vorhanden) {
        // makepkg schreibt bei VCS-Paketen pkgver in den PKGBUILD zurück; diese
        // lokale Änderung ließe "pull --ff-only" scheitern. Der Ordner gehört
        // allein diesem Werkzeug.
        int code = 0;
        runSync(QStringLiteral("git"), {QStringLiteral("-C"), item.dir, QStringLiteral("checkout"),
                                        QStringLiteral("--"), QStringLiteral(".")}, &code);
    }
    const QStringList arguments = vorhanden
        ? QStringList{QStringLiteral("-C"), item.dir, QStringLiteral("pull"), QStringLiteral("--ff-only")}
        : QStringList{QStringLiteral("clone"), QStringLiteral("--depth"), QStringLiteral("1"),
                      kAurBase + QLatin1Char('/') + item.base + QStringLiteral(".git"), item.dir};

    QProcess *process = makeStreamingProcess();
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
        if (m_cancelRequested) {
            finishOperation(OperationResult::Cancelled, tr("Abgebrochen."));
            return;
        }
        BatchItem &current = m_batch[m_batchPos];
        if (status != QProcess::NormalExit || exitCode != 0) {
            current.error = tr("Quellen konnten nicht geholt werden.");
        } else {
            current.review = buildReview(current);
            if (current.review.isEmpty()) current.error = tr("Im geholten Stand fehlt die PKGBUILD-Datei.");
        }
        if (!current.error.isEmpty()) emit logLine(tr("==> %1: %2").arg(current.base, current.error));
        ++m_batchPos;
        fetchNext();
    });
    process->start(QStringLiteral("git"), arguments);
}

void AurUpdates::enterReview() {
    const bool anyUsable = std::any_of(m_batch.cbegin(), m_batch.cend(),
                                       [](const BatchItem &item) { return item.error.isEmpty(); });
    if (!anyUsable) {
        finishOperation(OperationResult::Failed, tr("Für keines der Pakete konnten die Quellen geholt werden."));
        return;
    }

    m_stage = Stage::Review;
    m_batchPos = 0;
    reportProgress(tr("Änderungen prüfen"));

    QVariantList items;
    for (const BatchItem &item : std::as_const(m_batch)) {
        items.append(QVariantMap{
            {QStringLiteral("base"), item.base},
            {QStringLiteral("names"), item.names},
            {QStringLiteral("fromVersion"), item.fromVersion},
            {QStringLiteral("toVersion"), item.toVersion},
            {QStringLiteral("review"), item.review},
            {QStringLiteral("firstBuild"), item.firstBuild},
            {QStringLiteral("error"), item.error},
        });
    }
    emit reviewReady(items);
}

void AurUpdates::confirmReview() {
    if (m_stage != Stage::Review) return;
    m_stage = Stage::Build;
    m_batchPos = 0;
    buildNext();
}

void AurUpdates::buildNext() {
    if (m_cancelRequested) {
        finishOperation(OperationResult::Cancelled, tr("Abgebrochen."));
        return;
    }
    while (m_batchPos < m_batch.size() && !m_batch[m_batchPos].error.isEmpty()) ++m_batchPos;
    if (m_batchPos >= m_batch.size()) {
        startInstall();
        return;
    }

    const BatchItem &item = m_batch[m_batchPos];
    reportProgress(tr("Baue %1 (%2 von %3)").arg(item.base).arg(m_batchPos + 1).arg(m_batch.size()));
    emit logLine(tr("==> Baue %1").arg(item.base));

    QProcess *process = makeStreamingProcess();
    process->setWorkingDirectory(item.dir);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
        if (m_cancelRequested) {
            finishOperation(OperationResult::Cancelled, tr("Abgebrochen."));
            return;
        }
        BatchItem &current = m_batch[m_batchPos];
        // 13 = "Paket wurde bereits gebaut": der Bau liegt schon vor, etwa nach
        // einer abgebrochenen Installation. Die Dateien sind trotzdem da.
        constexpr int kAlreadyBuilt = 13;
        if (status != QProcess::NormalExit || (exitCode != 0 && exitCode != kAlreadyBuilt)) {
            current.error = tr("makepkg fehlgeschlagen (Exit-Code %1)").arg(exitCode);
        } else {
            int listCode = 0;
            QProcess list;
            list.setProcessEnvironment(cEnvironment());
            list.setWorkingDirectory(current.dir);
            list.start(QStringLiteral("makepkg"), {QStringLiteral("--packagelist")});
            list.waitForFinished(15000);
            listCode = list.exitCode();

            QStringList files;
            const QStringList lines = QString::fromUtf8(list.readAllStandardOutput())
                                          .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                const QString path = line.trimmed();
                if (!path.isEmpty() && QFileInfo::exists(path)) files.append(path);
            }
            QSet<QString> installed(m_foreign.keyBegin(), m_foreign.keyEnd());
            for (const QString &name : std::as_const(current.names)) installed.insert(name);
            current.files = filterInstallable(files, installed);
            if (listCode != 0 || current.files.isEmpty()) {
                current.error = tr("Es wurden keine installierbaren Pakete gebaut.");
            }
        }
        if (!current.error.isEmpty()) emit logLine(tr("==> %1: %2").arg(current.base, current.error));
        ++m_batchPos;
        buildNext();
    });

    // -s zieht fehlende Abhängigkeiten nach, --needed vermeidet unnötige
    // Neuinstallationen. makepkg verweigert Root und fragt selbst nach Rechten.
    process->start(QStringLiteral("makepkg"),
                   {QStringLiteral("-s"), QStringLiteral("--noconfirm"), QStringLiteral("--needed")});
}

void AurUpdates::startInstall() {
    QStringList files;
    QStringList failedBases;
    for (const BatchItem &item : std::as_const(m_batch)) {
        if (item.error.isEmpty()) files.append(item.files);
        else failedBases.append(item.base);
    }
    if (files.isEmpty()) {
        finishOperation(OperationResult::Failed,
                        tr("Kein Paket konnte gebaut werden: %1").arg(failedBases.join(QStringLiteral(", "))));
        return;
    }

    m_stage = Stage::Install;
    reportProgress(tr("Installiere %1 Paket(e) …").arg(files.size()));
    emit logLine(tr("==> Installiere: %1").arg(files.join(QLatin1Char(' '))));

    // Alle gebauten Pakete in EINEM Aufruf: eine Passwortabfrage statt einer je Paket.
    QProcess *install = makeStreamingProcess();
    connect(install, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, failedBases](int code, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || code != 0) {
            // pkexec: 126 = Anmeldung abgelehnt oder abgebrochen, 127 = nicht autorisiert
            const QString message = (code == 126 || code == 127)
                ? tr("Installation nicht autorisiert.")
                : tr("Installation fehlgeschlagen (Exit-Code %1)").arg(code);
            finishOperation(OperationResult::Failed, message);
            check();
            return;
        }

        QStringList done;
        for (const BatchItem &item : std::as_const(m_batch)) {
            if (!item.error.isEmpty()) continue;
            // Stand merken: beim nächsten Mal nur zeigen, was sich seitdem geändert hat.
            int code = 0;
            const QByteArray head = runSync(QStringLiteral("git"),
                {QStringLiteral("-C"), item.dir, QStringLiteral("rev-parse"), QStringLiteral("HEAD")}, &code).trimmed();
            QFile marker(item.dir + QStringLiteral("/.git/lut-built"));
            if (code == 0 && !head.isEmpty() && marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                marker.write(head + '\n');
            }
            done.append(item.names);
        }

        if (failedBases.isEmpty()) {
            finishOperation(OperationResult::Succeeded,
                            tr("%1 AUR-Paket(e) aktualisiert.").arg(done.size()));
        } else {
            finishOperation(OperationResult::Failed,
                            tr("%1 aktualisiert, fehlgeschlagen: %2")
                                .arg(done.join(QStringLiteral(", ")), failedBases.join(QStringLiteral(", "))));
        }
        for (const QString &name : std::as_const(done)) emit finished(name);
        check();
    });
    install->start(QStringLiteral("pkexec"),
                   QStringList{QStringLiteral("pacman"), QStringLiteral("-U"), QStringLiteral("--noconfirm")} + files);
}

void AurUpdates::cancel() {
    if (m_stage == Stage::Install) return; // nicht abbrechbar
    if (m_stage == Stage::Review) {
        finishOperation(OperationResult::Cancelled, tr("Abgebrochen."));
        return;
    }

    bool running = false;
    const auto processes = findChildren<QProcess *>();
    for (QProcess *process : processes) {
        if (process->state() != QProcess::NotRunning
            && process->program() != QStringLiteral("pkexec")) {
            running = true;
            process->terminate();
        }
    }
    if (m_stage != Stage::Idle && running) {
        // Den Rest erledigt der finished-Handler des beendeten Prozesses.
        m_cancelRequested = true;
        return;
    }
    if (m_stage != Stage::Idle) {
        finishOperation(OperationResult::Cancelled, tr("Abgebrochen."));
        return;
    }
    setBusy(false);
    setStatus(tr("Abgebrochen."));
}

} // namespace lut
