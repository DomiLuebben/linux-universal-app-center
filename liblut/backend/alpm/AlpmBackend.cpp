#include "AlpmBackend.h"
#include <QRegularExpression>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <sys/utsname.h>

#ifdef HAVE_ALPM
#include <alpm.h>
#endif

namespace lut {

AlpmBackend::AlpmBackend(QObject *parent)
    : Backend(parent) {}

AlpmBackend::~AlpmBackend() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

Capabilities AlpmBackend::capabilities() const {
    Capabilities cap;
    cap.partialUpgrade = false; // Auf Arch/Pacman AUSDRÜCKLICH verboten!
    cap.downgrade = false;
    cap.historyUndo = false;
    cap.changelogs = false;
    cap.securityFlag = false;
    cap.offlineUpdate = false;
    cap.autoremove = true;
    cap.parallelDownloads = true;
    return cap;
}

void AlpmBackend::refreshMetadata() {
    emit eventEmitted(PhaseChanged{Phase::RefreshMetadata, QStringLiteral("Synchronisiere Paketdatenbanken"), true});

    // Checkupdates-Prinzip: Temporäre Datenbank synchronisieren ohne Root
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/alpm-db");
    QDir().mkpath(cacheDir);

    QProcess proc;
    proc.start(QStringLiteral("checkupdates"), {QStringLiteral("-d")});
    if (!proc.waitForStarted(2000)) {
        // Fallback pacman -Sy mit temp dbpath falls checkupdates nicht installiert
        proc.start(QStringLiteral("pacman"), {QStringLiteral("-Sy"), QStringLiteral("--dbpath"), cacheDir});
    }
    proc.waitForFinished(60000);

    emit eventEmitted(PhaseChanged{Phase::Idle, QStringLiteral("Bereit"), false});
}

void AlpmBackend::planUpgradeAll(const UpgradeOptions &) {
    emit eventEmitted(PhaseChanged{Phase::Resolve, QStringLiteral("Prüfe Systemaktualisierungen"), true});

    QProcess proc;
    // checkupdates oder pacman -Qu
    proc.start(QStringLiteral("checkupdates"), {QStringLiteral("--nocolor")});
    if (!proc.waitForStarted(2000)) {
        proc.start(QStringLiteral("pacman"), {QStringLiteral("-Qu")});
    }
    proc.waitForFinished(30000);

    QList<PackageOp> ops;
    qint64 totalDownload = 0;
    qint64 totalInstalled = 0;

    // Format von pacman -Qu / checkupdates:
    // name alt -> neu [repo]
    // z.B.: linux-cachyos 6.13.4-arch1 -> 6.13.5-arch1 [cachyos]
    QRegularExpression re(QStringLiteral(R"(^([a-zA-Z0-9._+-]+)\s+([^\s]+)\s+->\s+([^\s]+)(?:\s+\[([a-zA-Z0-9._+-]+)\])?)"));

    while (proc.canReadLine()) {
        QString line = QString::fromUtf8(proc.readLine()).trimmed();
        auto match = re.match(line);
        if (match.hasMatch()) {
            PackageOp op;
            op.name = match.captured(1);
            op.version = match.captured(2);
            op.newVersion = match.captured(3);
            if (match.lastCapturedIndex() >= 4) {
                op.repo = match.captured(4);
            }
            op.id = QStringLiteral("%1-%2").arg(op.name, op.newVersion);
            op.kind = PackageOp::Kind::Upgrade;
            op.isKernel = PackageOp::detectIsKernel(op.name);

            // Typische Schätzgrößen falls keine Metadaten vorhanden
            op.downloadSize = op.isKernel ? (75 * 1024 * 1024) : (5 * 1024 * 1024);
            op.installedSize = op.isKernel ? (150 * 1024 * 1024) : (15 * 1024 * 1024);

            totalDownload += op.downloadSize;
            totalInstalled += op.installedSize;
            ops.append(op);
        }
    }

    m_plannedOps = ops;

    PlanReady plan;
    plan.ops = ops;
    plan.downloadBytes = totalDownload;
    plan.installedSizeDelta = totalInstalled;
    if (!capabilities().partialUpgrade) {
        plan.warnings.append(QStringLiteral("Arch Linux / CachyOS: Es werden immer alle Pakete gemeinsam aktualisiert (Rolling Release)."));
    }

    emit eventEmitted(plan);
    emit eventEmitted(PhaseChanged{Phase::Idle, QStringLiteral("Aktualisierungsplan bereit"), true});
}

void AlpmBackend::planInstall(const QStringList &) {}
void AlpmBackend::planRemove(const QStringList &) {}

QString AlpmBackend::findWorkerExecutable() const {
    QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        appDir + QStringLiteral("/lut-alpm-worker"),
        appDir + QStringLiteral("/../liblut/lut-alpm-worker"),
        QStringLiteral("/usr/libexec/linux-update-tool/lut-alpm-worker"),
        QStringLiteral("/usr/lib/linux-update-tool/lut-alpm-worker")
    };

    for (const QString &c : candidates) {
        if (QFile::exists(c)) {
            return c;
        }
    }

    return QStandardPaths::findExecutable(QStringLiteral("lut-alpm-worker"));
}

void AlpmBackend::commit() {
    emit eventEmitted(PhaseChanged{Phase::Download, QStringLiteral("Pakete holen"), true});

    QString workerExe = findWorkerExecutable();
    bool useWorker = !workerExe.isEmpty();

    if (!m_process) {
        m_process = new QProcess(this);
    } else if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(500);
    }

    m_process->disconnect();

    m_workerDoneEmitted = false;
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    if (useWorker) {
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
            while (m_process->canReadLine()) {
                parseWorkerOutputLine(QString::fromUtf8(m_process->readLine()).trimmed());
            }
        });
        connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int exitCode, QProcess::ExitStatus) {
            QByteArray remaining = m_process->readAllStandardOutput();
            if (!remaining.isEmpty()) {
                QStringList lines = QString::fromUtf8(remaining).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                for (const QString &line : lines) {
                    parseWorkerOutputLine(line.trimmed());
                }
            }
            if (exitCode != 0 && !m_workerDoneEmitted) {
                emit eventEmitted(PhaseChanged{Phase::Failed, QStringLiteral("Fehlgeschlagen (Exit Code %1)").arg(exitCode), false});
                emit eventEmitted(TransactionDone{Result::Failed, QStringLiteral("Worker beendet mit Code %1").arg(exitCode), false, {}, 0});
            }
        });

        m_process->start(workerExe, {QStringLiteral("--sysupgrade")});
    } else {
        // Fallback pacman
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
            while (m_process->canReadLine()) {
                parsePacmanOutput(QString::fromUtf8(m_process->readLine()).trimmed());
            }
        });
        connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int exitCode, QProcess::ExitStatus) {
            if (exitCode == 0) {
                emit eventEmitted(PhaseChanged{Phase::Cleanup, QStringLiteral("Aufräumen"), false});
                emit eventEmitted(TransactionDone{Result::Success, QStringLiteral("System erfolgreich aktualisiert"), false, {}, 0});
            } else {
                emit eventEmitted(PhaseChanged{Phase::Failed, QStringLiteral("Fehlgeschlagen"), false});
                emit eventEmitted(TransactionDone{Result::Failed, QStringLiteral("Pacman beendet mit Code %1").arg(exitCode), false, {}, 0});
            }
        });

        m_process->start(QStringLiteral("pacman"), {QStringLiteral("-Syu"), QStringLiteral("--noconfirm")});
    }
}

void AlpmBackend::cancel() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
    }
    emit eventEmitted(PhaseChanged{Phase::Cancelled, QStringLiteral("Abgebrochen"), false});
    emit eventEmitted(TransactionDone{Result::Cancelled, QStringLiteral("Vom Benutzer abgebrochen"), false, {}, 0});
}

void AlpmBackend::answerQuestion(const QString &, const QJsonObject &) {}

void AlpmBackend::parseWorkerOutputLine(const QString &line) {
    if (line.isEmpty()) return;

    QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
    if (doc.isObject()) {
        auto ev = deserializeEvent(doc.object());
        if (ev.has_value()) {
            if (std::holds_alternative<TransactionDone>(*ev)) {
                m_workerDoneEmitted = true;
            }
            emit eventEmitted(*ev);
            return;
        }
    }

    // Fallback falls plain log line
    emit eventEmitted(LogLine{LogLevel::Info, QStringLiteral("worker"), line});
}

void AlpmBackend::parsePacmanOutput(const QString &line) {
    emit eventEmitted(LogLine{LogLevel::Info, QStringLiteral("pacman"), line});

    if (line.contains(QLatin1String("downloading")) || line.contains(QLatin1String("Lade Pakete"))) {
        emit eventEmitted(PhaseChanged{Phase::Download, QStringLiteral("Pakete herunterladen"), true});
    } else if (line.contains(QLatin1String("checking keyring")) || line.contains(QLatin1String("checking package integrity"))) {
        emit eventEmitted(PhaseChanged{Phase::Verify, QStringLiteral("Signaturen prüfen"), true});
    } else if (line.contains(QLatin1String("upgrading")) || line.contains(QLatin1String("installing"))) {
        emit eventEmitted(PhaseChanged{Phase::Commit, QStringLiteral("Pakete installieren"), false});
    } else if (line.contains(QLatin1String("Running post-transaction hooks")) || line.contains(QLatin1String("Arming ConditionNeedsUpdate"))) {
        emit eventEmitted(PhaseChanged{Phase::PostTransaction, QStringLiteral("ALPM-Hooks ausführen"), false});
    }
}

QList<PackageOp> AlpmBackend::availableUpdates() {
    return m_plannedOps;
}

QList<InstalledPackage> AlpmBackend::installedPackages(const QString &query) {
    QList<InstalledPackage> result;

#ifdef HAVE_ALPM
    alpm_errno_t err;
    alpm_handle_t *handle = alpm_initialize("/", "/var/lib/pacman", &err);
    if (!handle) {
        return result;
    }

    struct utsname uts;
    QString runningKernel;
    if (uname(&uts) == 0) {
        runningKernel = QString::fromUtf8(uts.release);
    }

    alpm_db_t *localdb = alpm_get_localdb(handle);
    const alpm_list_t *pkgs = alpm_db_get_pkgcache(localdb);
    QString qLower = query.trimmed().toLower();

    for (const alpm_list_t *i = pkgs; i; i = alpm_list_next(i)) {
        alpm_pkg_t *pkg = static_cast<alpm_pkg_t *>(i->data);
        QString name = QString::fromUtf8(alpm_pkg_get_name(pkg));
        QString ver = QString::fromUtf8(alpm_pkg_get_version(pkg));
        QString desc = QString::fromUtf8(alpm_pkg_get_desc(pkg) ? alpm_pkg_get_desc(pkg) : "");

        if (!qLower.isEmpty()) {
            if (!name.toLower().contains(qLower) && !desc.toLower().contains(qLower)) {
                continue;
            }
        }

        InstalledPackage ip;
        ip.name = name;
        ip.version = ver;
        ip.id = QStringLiteral("%1-%2").arg(name, ver);
        ip.description = desc;
        ip.installedSize = alpm_pkg_get_isize(pkg);
        ip.arch = QString::fromUtf8(alpm_pkg_get_arch(pkg) ? alpm_pkg_get_arch(pkg) : "x86_64");

        // Verwaiste Pakete erkennen
        if (alpm_pkg_get_reason(pkg) == ALPM_PKG_REASON_DEPEND) {
            alpm_list_t *reqBy = alpm_pkg_compute_requiredby(pkg);
            alpm_list_t *optFor = alpm_pkg_compute_optionalfor(pkg);
            if (!reqBy && !optFor) {
                ip.isOrphan = true;
            }
            if (reqBy) FREELIST(reqBy);
            if (optFor) FREELIST(optFor);
        }

        // Alter Kernel Erkennung
        if (PackageOp::detectIsKernel(name) && !ver.contains(runningKernel)) {
            ip.isOldKernel = true;
        }

        result.append(ip);
    }

    alpm_release(handle);
#endif

    return result;
}

QList<InstalledPackage> AlpmBackend::queryOrphans() {
    QList<InstalledPackage> orphans;
    QList<InstalledPackage> all = installedPackages();
    for (const auto &pkg : all) {
        if (pkg.isOrphan) {
            orphans.append(pkg);
        }
    }
    return orphans;
}

qint64 AlpmBackend::queryCleanableCacheBytes() const {
    qint64 total = 0;
    QDir cacheDir(QStringLiteral("/var/cache/pacman/pkg"));
    QFileInfoList entries = cacheDir.entryInfoList(QDir::Files);
    for (const auto &fi : entries) {
        total += fi.size();
    }
    return total;
}

QStringList AlpmBackend::detectPacnewFiles() const {
    QStringList result;
    QDir etcDir(QStringLiteral("/etc"));
    QStringList filters = {QStringLiteral("*.pacnew"), QStringLiteral("*.pacsave")};
    QDirIterator it(QStringLiteral("/etc"), filters, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        result.append(it.next());
    }
    return result;
}

QList<ChangelogEntry> AlpmBackend::changelog(const QString &) {
    return {};
}

QList<HistoryEntry> AlpmBackend::history(int limit) {
    QList<HistoryEntry> result;
    QFile file(QStringLiteral("/var/log/pacman.log"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return result;
    }

    // Liest Transaktionen aus pacman.log rückwärts
    QByteArray content = file.readAll();
    file.close();

    QList<QByteArray> lines = content.split('\n');
    QRegularExpression transStartRe(QStringLiteral(R"(\[(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{4})\]\s+\[(ALPM|PACMAN)\]\s+transaction (started|completed))"));

    HistoryEntry currentEntry;
    int entryCounter = 1;

    for (int i = lines.size() - 1; i >= 0 && result.size() < limit; --i) {
        QString line = QString::fromUtf8(lines.at(i)).trimmed();
        if (line.isEmpty()) continue;

        auto match = transStartRe.match(line);
        if (match.hasMatch()) {
            QString timestampStr = match.captured(1);
            QString action = match.captured(3);

            if (action == QLatin1String("completed")) {
                currentEntry = HistoryEntry();
                currentEntry.id = entryCounter++;
                currentEntry.timestamp = QDateTime::fromString(timestampStr, Qt::ISODate);
                currentEntry.result = QStringLiteral("Success");
                currentEntry.canUndo = false;
            } else if (action == QLatin1String("started") && currentEntry.id > 0) {
                currentEntry.command = QStringLiteral("Systemaktualisierung");
                result.append(currentEntry);
                currentEntry = HistoryEntry();
            }
        } else if (currentEntry.id > 0 && line.contains(QLatin1String("[ALPM] upgraded"))) {
            currentEntry.packagesAltered++;
        }
    }

    return result;
}

} // namespace lut
