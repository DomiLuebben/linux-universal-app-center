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
#include <QTemporaryDir>
#include <QProcessEnvironment>
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
    cap.catalogQuery = true;
    cap.install = true;
    cap.remove = true;
    cap.installRequiresFullUpgrade = true;
    cap.typedPackageTargets = true;
    cap.transactionReattach = true;
    cap.protocolVersion = 2;
    return cap;
}

void AlpmBackend::refreshMetadata() {
    planUpgradeAll({});
}

void AlpmBackend::planUpgradeAll(const UpgradeOptions &) {
    m_plannedAction = PlannedAction::Upgrade;
    m_plannedTargets.clear();
    m_expectedRevision.clear();
    m_planRevision.clear();
    m_plannedOps.clear();

    auto fail = [this](const QString &reason) {
        emit eventEmitted(PhaseChanged{Phase::Failed, reason, false});
        emit eventEmitted(TransactionDone{Result::Failed, reason, false, {}, 0});
    };
    emit eventEmitted(PhaseChanged{Phase::RefreshMetadata, QStringLiteral("Prüfe aktuelle Paketlisten"), false, true});
    QTemporaryDir database(QDir::tempPath() + QStringLiteral("/lut-checkupdates-XXXXXX"));
    if (!database.isValid()) { fail(QStringLiteral("Temporäre Paketdatenbank konnte nicht erstellt werden.")); return; }
    QProcess proc;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    env.insert(QStringLiteral("CHECKUPDATES_DB"), database.path());
    proc.setProcessEnvironment(env);
    proc.start(QStringLiteral("checkupdates"), {QStringLiteral("--nocolor")});
    if (!proc.waitForStarted(5000)) {
        fail(QStringLiteral("checkupdates konnte nicht gestartet werden. Bitte pacman-contrib installieren.")); return;
    }
    if (!proc.waitForFinished(180000)) {
        proc.kill(); proc.waitForFinished();
        fail(QStringLiteral("Zeitüberschreitung beim Laden der Paketlisten.")); return;
    }
    // checkupdates: 0 = Updates, 2 = keine Updates, 1 = Fehler.
    // Niemals auf die möglicherweise veraltete Systemdatenbank zurückfallen.
    if (proc.exitStatus() != QProcess::NormalExit || (proc.exitCode() != 0 && proc.exitCode() != 2)) {
        fail(QStringLiteral("Updateprüfung fehlgeschlagen: %1").arg(QString::fromUtf8(proc.readAllStandardError()).trimmed())); return;
    }
    const QString worker = findWorkerExecutable();
    if (worker.isEmpty()) { fail(QStringLiteral("ALPM-Worker fehlt. Bitte Linux Update Tool neu installieren.")); return; }
    proc.start(worker, {QStringLiteral("--plan"), QStringLiteral("--dbpath"), database.path()});
    if (!proc.waitForStarted(5000) || !proc.waitForFinished(60000)) {
        proc.kill(); proc.waitForFinished(); fail(QStringLiteral("Paketplan konnte nicht erstellt werden.")); return;
    }
    std::optional<PlanReady> plan;
    QString failure;
    for (const QByteArray &line : proc.readAllStandardOutput().split('\n')) {
        auto event = deserializeEvent(QJsonDocument::fromJson(line).object());
        if (!event) continue;
        if (auto *ready = std::get_if<PlanReady>(&*event)) plan = *ready;
        else if (auto *done = std::get_if<TransactionDone>(&*event)) failure = done->summary;
        else emit eventEmitted(*event);
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0 || !plan) {
        fail(failure.isEmpty() ? QStringLiteral("ALPM lieferte keinen gültigen Paketplan.") : failure); return;
    }
    m_plannedOps = plan->ops;
    m_planRevision = plan->planRevision;
    emit eventEmitted(*plan);
    emit eventEmitted(PhaseChanged{Phase::Idle, QStringLiteral("Aktualisierungsplan bereit"), false});
}

void AlpmBackend::planInstall(const QStringList &names) {
    m_plannedAction = PlannedAction::Install;
    m_plannedTargets = names;
    m_expectedRevision.clear();
    m_planRevision.clear();
    m_plannedOps.clear();

    auto fail = [this](const QString &reason) {
        emit eventEmitted(PhaseChanged{Phase::Failed, reason, false});
        emit eventEmitted(TransactionDone{Result::Failed, reason, false, {}, 0});
    };

    if (names.isEmpty()) {
        fail(QStringLiteral("Keine Pakete zur Installation angegeben."));
        return;
    }

    emit eventEmitted(PhaseChanged{Phase::RefreshMetadata, QStringLiteral("Bereite Installation vor"), false, true});
    const QString worker = findWorkerExecutable();
    if (worker.isEmpty()) {
        fail(QStringLiteral("ALPM-Worker fehlt. Bitte Linux Update Tool neu installieren."));
        return;
    }

    if (!m_process) {
        m_process = new QProcess(this);
    } else if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(500);
    }
    m_process->disconnect();
    m_workerDoneEmitted = false;

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        while (m_process && m_process->canReadLine()) {
            parseWorkerOutputLine(QString::fromUtf8(m_process->readLine()).trimmed());
        }
    });

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, fail](int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_process) {
            QByteArray remaining = m_process->readAllStandardOutput();
            if (!remaining.isEmpty()) {
                for (const QString &line : QString::fromUtf8(remaining).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                    parseWorkerOutputLine(line.trimmed());
                }
            }
        }
        if (m_workerDoneEmitted) return;
        if (exitStatus != QProcess::NormalExit || exitCode != 0 || m_plannedOps.isEmpty()) {
            fail(QStringLiteral("ALPM lieferte keinen gültigen Paketplan oder Vorgang abgebrochen."));
        } else {
            emit eventEmitted(PhaseChanged{Phase::Idle, QStringLiteral("Installationsplan bereit"), false});
        }
    });

    QStringList args = {QStringLiteral("--plan"), QStringLiteral("--action"), QStringLiteral("install"), QStringLiteral("--install"), names.join(QLatin1Char(','))};
    m_process->start(worker, args);
}

void AlpmBackend::planRemove(const QStringList &names) {
    m_plannedAction = PlannedAction::Remove;
    m_plannedTargets = names;
    m_expectedRevision.clear();
    m_planRevision.clear();
    m_plannedOps.clear();

    auto fail = [this](const QString &reason) {
        emit eventEmitted(PhaseChanged{Phase::Failed, reason, false});
        emit eventEmitted(TransactionDone{Result::Failed, reason, false, {}, 0});
    };

    if (names.isEmpty()) {
        fail(QStringLiteral("Keine Pakete zur Entfernung angegeben."));
        return;
    }

    emit eventEmitted(PhaseChanged{Phase::Resolve, QStringLiteral("Bereite Entfernung vor"), false, true});
    const QString worker = findWorkerExecutable();
    if (worker.isEmpty()) {
        fail(QStringLiteral("ALPM-Worker fehlt. Bitte Linux Update Tool neu installieren."));
        return;
    }

    if (!m_process) {
        m_process = new QProcess(this);
    } else if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(500);
    }
    m_process->disconnect();
    m_workerDoneEmitted = false;

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        while (m_process && m_process->canReadLine()) {
            parseWorkerOutputLine(QString::fromUtf8(m_process->readLine()).trimmed());
        }
    });

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, fail](int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_process) {
            QByteArray remaining = m_process->readAllStandardOutput();
            if (!remaining.isEmpty()) {
                for (const QString &line : QString::fromUtf8(remaining).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                    parseWorkerOutputLine(line.trimmed());
                }
            }
        }
        if (m_workerDoneEmitted) return;
        if (exitStatus != QProcess::NormalExit || exitCode != 0 || m_plannedOps.isEmpty()) {
            fail(QStringLiteral("ALPM lieferte keinen gültigen Paketplan oder Vorgang abgebrochen."));
        } else {
            emit eventEmitted(PhaseChanged{Phase::Idle, QStringLiteral("Entfernungsplan bereit"), false});
        }
    });

    QStringList args = {QStringLiteral("--plan"), QStringLiteral("--action"), QStringLiteral("remove"), QStringLiteral("--remove"), names.join(QLatin1Char(','))};
    m_process->start(worker, args);
}

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
    if (workerExe.isEmpty()) {
        emit eventEmitted(PhaseChanged{Phase::Failed, QStringLiteral("ALPM-Worker fehlt"), false});
        emit eventEmitted(TransactionDone{Result::Failed, QStringLiteral("ALPM-Worker nicht gefunden. Bitte Installation prüfen."), false, {}, 0});
        return;
    }

    const QString expected = !m_expectedRevision.isEmpty() ? m_expectedRevision : m_planRevision;
    if (m_plannedAction == PlannedAction::Install || m_plannedAction == PlannedAction::Remove) {
        if (expected.isEmpty()) {
            emit eventEmitted(PhaseChanged{Phase::Failed, QStringLiteral("Kein gültiger Planungsfingerprint vorhanden"), false});
            emit eventEmitted(TransactionDone{Result::Failed, QStringLiteral("Kein gültiger Planungsfingerprint vorhanden. Bitte Transaktion neu planen."), false, {}, 0});
            return;
        }
    }

    if (!m_process) {
        m_process = new QProcess(this);
    } else if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(500);
    }

    m_process->disconnect();

    m_workerDoneEmitted = false;
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_workerDoneEmitted) return;
        m_workerDoneEmitted = true;
        const QString reason = QStringLiteral("Paketprozess konnte nicht gestartet werden: %1").arg(m_process->errorString());
        emit eventEmitted(PhaseChanged{Phase::Failed, reason, false});
        emit eventEmitted(TransactionDone{Result::Failed, reason, false, {}, 0});
    });

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        while (m_process->canReadLine()) {
            parseWorkerOutputLine(QString::fromUtf8(m_process->readLine()).trimmed());
        }
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        QByteArray remaining = m_process->readAllStandardOutput();
        if (!remaining.isEmpty()) {
            QStringList lines = QString::fromUtf8(remaining).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                parseWorkerOutputLine(line.trimmed());
            }
        }
        if (m_workerDoneEmitted) {
            return; // Worker (oder cancel()) hat das Ergebnis bereits gemeldet
        }

        QString reason;
        if (exitStatus == QProcess::CrashExit) {
            reason = QStringLiteral("Worker-Prozess abgestürzt");
        } else if (exitCode != 0) {
            reason = QStringLiteral("Worker beendet mit Code %1").arg(exitCode);
        } else {
            reason = QStringLiteral("Worker endete ohne Abschlussmeldung");
        }
        m_workerDoneEmitted = true;
        emit eventEmitted(PhaseChanged{Phase::Failed, reason, false});
        emit eventEmitted(TransactionDone{Result::Failed, reason, false, {}, 0});
    });

    QStringList args;
    args << QStringLiteral("--commit");
    if (m_plannedAction == PlannedAction::Install) {
        args << QStringLiteral("--action") << QStringLiteral("install");
        if (!m_plannedTargets.isEmpty()) {
            args << QStringLiteral("--install") << m_plannedTargets.join(QLatin1Char(','));
        }
    } else if (m_plannedAction == PlannedAction::Remove) {
        args << QStringLiteral("--action") << QStringLiteral("remove");
        if (!m_plannedTargets.isEmpty()) {
            args << QStringLiteral("--remove") << m_plannedTargets.join(QLatin1Char(','));
        }
    } else {
        args << QStringLiteral("--action") << QStringLiteral("upgrade");
    }

    if (!expected.isEmpty()) {
        args << QStringLiteral("--expected-fingerprint") << expected;
    }

    m_process->start(workerExe, args);
}

void AlpmBackend::commitPlan(const QString &planRevision) {
    m_expectedRevision = planRevision;
    commit();
}

void AlpmBackend::discardPlan() {
    m_plannedAction = PlannedAction::Upgrade;
    m_plannedTargets.clear();
    m_expectedRevision.clear();
    m_planRevision.clear();
    m_plannedOps.clear();
    cancel();
}

TransactionSnapshot AlpmBackend::currentSnapshot() const {
    TransactionSnapshot snapshot;
    if (m_plannedAction == PlannedAction::Install) {
        snapshot.intent.type = TransactionIntent::Type::Install;
        for (const auto &t : m_plannedTargets) {
            snapshot.intent.targets.append(PackageRef{QStringLiteral("alpm"), QString(), t, QString(), QString()});
        }
    } else if (m_plannedAction == PlannedAction::Remove) {
        snapshot.intent.type = TransactionIntent::Type::Remove;
        for (const auto &t : m_plannedTargets) {
            snapshot.intent.targets.append(PackageRef{QStringLiteral("alpm"), QString(), t, QString(), QString()});
        }
    } else {
        snapshot.intent.type = TransactionIntent::Type::UpgradeAll;
    }
    snapshot.plan.ops = m_plannedOps;
    snapshot.plan.planRevision = !m_expectedRevision.isEmpty() ? m_expectedRevision : m_planRevision;
    return snapshot;
}

void AlpmBackend::cancel() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
    }
    // Vor dem terminate()-Nachlauf setzen: sonst überschreibt der finished-Handler
    // das "Abgebrochen" gleich wieder mit "Fehlgeschlagen".
    m_workerDoneEmitted = true;
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
            if (auto *ready = std::get_if<PlanReady>(&*ev)) {
                m_plannedOps = ready->ops;
                m_planRevision = ready->planRevision;
            } else if (std::holds_alternative<TransactionDone>(*ev)) {
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
    bool hasInstalls = false;
    bool hasRemoves = false;
    bool hasUpgrades = false;

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
                hasInstalls = false;
                hasRemoves = false;
                hasUpgrades = false;
            } else if (action == QLatin1String("started") && currentEntry.id > 0) {
                if (hasInstalls && !hasUpgrades && !hasRemoves) {
                    currentEntry.command = QStringLiteral("Paketinstallation");
                } else if (hasRemoves && !hasUpgrades && !hasInstalls) {
                    currentEntry.command = QStringLiteral("Paketentfernung");
                } else if (hasUpgrades) {
                    currentEntry.command = QStringLiteral("Systemaktualisierung");
                } else {
                    currentEntry.command = QStringLiteral("Pakettransaktion");
                }
                result.append(currentEntry);
                currentEntry = HistoryEntry();
            }
        } else if (currentEntry.id > 0) {
            if (line.contains(QLatin1String("[ALPM] upgraded"))) {
                currentEntry.packagesAltered++;
                hasUpgrades = true;
            } else if (line.contains(QLatin1String("[ALPM] installed"))) {
                currentEntry.packagesAltered++;
                hasInstalls = true;
            } else if (line.contains(QLatin1String("[ALPM] removed"))) {
                currentEntry.packagesAltered++;
                hasRemoves = true;
            } else if (line.contains(QLatin1String("[ALPM] reinstalled"))) {
                currentEntry.packagesAltered++;
            }
        }
    }

    return result;
}

} // namespace lut
