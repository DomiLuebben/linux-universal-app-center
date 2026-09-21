#include "AurUpdates.h"

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

AurUpdates::AurUpdates(QObject *parent)
    : QAbstractListModel(parent) {
    const bool arch = DistroDetect::detectFamily() == DistroFamily::Arch;
    m_available = arch && toolExists(QStringLiteral("git"))
                       && toolExists(QStringLiteral("makepkg"))
                       && toolExists(QStringLiteral("pacman"));
    if (!m_available) {
        m_statusMessage = arch
            ? tr("Für AUR-Aktualisierungen werden git, makepkg und pacman benötigt.")
            : tr("AUR-Aktualisierungen gibt es nur auf Arch und Derivaten.");
    }
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
    if (m_busy || !m_available) return;
    setBusy(true);
    setStatus(tr("Prüfe AUR-Pakete …"));

    int code = 0;
    const QByteArray out = runSync(QStringLiteral("pacman"), {QStringLiteral("-Qm")}, &code);
    if (code != 0) {
        fail(tr("Installierte AUR-Pakete konnten nicht ermittelt werden."));
        return;
    }

    m_foreign = parseForeignPackages(out);
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
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("linux-update-tool"));
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

void AurUpdates::prepare(const QString &name) {
    if (m_busy || !m_available || name.isEmpty()) return;

    const auto found = m_remote.constFind(name);
    const QString base = found != m_remote.constEnd() ? found->packageBase : name;
    m_pendingName = name;
    m_pendingBase = base;

    const QString dir = cacheDir() + QLatin1Char('/') + base;
    QDir().mkpath(cacheDir());

    setBusy(true);
    setStatus(tr("Hole Quellen für %1 …").arg(name));

    // Vorhandene Arbeitskopie auffrischen, sonst frisch klonen.
    const bool vorhanden = QFileInfo::exists(dir + QStringLiteral("/.git"));
    const QStringList arguments = vorhanden
        ? QStringList{QStringLiteral("-C"), dir, QStringLiteral("pull"), QStringLiteral("--ff-only")}
        : QStringList{QStringLiteral("clone"), QStringLiteral("--depth"), QStringLiteral("1"),
                      kAurBase + QLatin1Char('/') + base + QStringLiteral(".git"), dir};

    QProcess *process = makeProcess();
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
        while (process->canReadLine()) {
            const QString line = QString::fromUtf8(process->readLine()).trimmed();
            if (!line.isEmpty()) emit logLine(line);
        }
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, dir, name](int exitCode, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || exitCode != 0) {
            fail(tr("Quellen für %1 konnten nicht geholt werden.").arg(name));
            return;
        }

        QFile pkgbuild(dir + QStringLiteral("/PKGBUILD"));
        if (!pkgbuild.open(QIODevice::ReadOnly | QIODevice::Text)) {
            fail(tr("Im geholten Stand von %1 fehlt die PKGBUILD-Datei.").arg(name));
            return;
        }
        const QString text = QString::fromUtf8(pkgbuild.readAll());
        pkgbuild.close();

        setBusy(false);
        setStatus(tr("%1 ist bereit. Bitte PKGBUILD prüfen.").arg(name));
        // Fehlende Abhängigkeiten löst makepkg -s selbst auf.
        emit prepared(name, dir, text, {});
    });
    process->start(QStringLiteral("git"), arguments);
}

void AurUpdates::build(const QString &pkgDir) {
    if (m_busy || !m_available || pkgDir.isEmpty()) return;
    setBusy(true);
    setStatus(tr("Baue %1 …").arg(m_pendingName));

    QProcess *process = makeProcess();
    process->setWorkingDirectory(pkgDir);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
        while (process->canReadLine()) {
            const QString line = QString::fromUtf8(process->readLine()).trimmed();
            if (!line.isEmpty()) emit logLine(line);
        }
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, pkgDir](int exitCode, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || exitCode != 0) {
            fail(tr("makepkg fehlgeschlagen (Exit-Code %1)").arg(exitCode));
            return;
        }

        QProcess list;
        list.setProcessEnvironment(cEnvironment());
        list.setWorkingDirectory(pkgDir);
        list.start(QStringLiteral("makepkg"), {QStringLiteral("--packagelist")});
        list.waitForFinished(15000);

        QStringList files;
        const QStringList lines = QString::fromUtf8(list.readAllStandardOutput())
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QString path = line.trimmed();
            if (!path.isEmpty() && QFileInfo::exists(path)) files.append(path);
        }
        if (list.exitCode() != 0 || files.isEmpty()) {
            fail(tr("Es wurden keine gebauten Pakete gefunden."));
            return;
        }

        emit logLine(tr("==> Installiere: %1").arg(files.join(QLatin1Char(' '))));
        QProcess *install = makeProcess();
        install->setProcessChannelMode(QProcess::MergedChannels);
        connect(install, &QProcess::readyReadStandardOutput, this, [this, install]() {
            while (install->canReadLine()) {
                const QString line = QString::fromUtf8(install->readLine()).trimmed();
                if (!line.isEmpty()) emit logLine(line);
            }
        });
        connect(install, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this](int installCode, QProcess::ExitStatus installStatus) {
            if (installStatus != QProcess::NormalExit || installCode != 0) {
                fail(tr("Installation fehlgeschlagen (Exit-Code %1)").arg(installCode));
                return;
            }
            setBusy(false);
            setStatus(tr("%1 wurde aktualisiert.").arg(m_pendingName));
            emit finished(m_pendingName);
            check();
        });
        install->start(QStringLiteral("pkexec"),
                       QStringList{QStringLiteral("pacman"), QStringLiteral("-U"),
                                   QStringLiteral("--noconfirm")} + files);
    });

    // -s zieht fehlende Abhängigkeiten nach, --needed vermeidet unnötige
    // Neuinstallationen. makepkg verweigert Root und fragt selbst nach Rechten.
    process->start(QStringLiteral("makepkg"),
                   {QStringLiteral("-s"), QStringLiteral("--noconfirm"), QStringLiteral("--needed")});
}

void AurUpdates::cancel() {
    // Nur Holen und Bauen werden abgebrochen. Ein Abbruch mitten im
    // "pacman -U" wäre gefährlich und bleibt deshalb aus.
    const auto processes = findChildren<QProcess *>();
    for (QProcess *process : processes) {
        if (process->state() != QProcess::NotRunning
            && process->program() != QStringLiteral("pkexec")) {
            process->terminate();
        }
    }
    setBusy(false);
    setStatus(tr("Abgebrochen."));
}

} // namespace lut
