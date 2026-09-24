#include "SnapBackend.h"
#include "liblut/backend/Validation.h"
#include "liblut/backend/snap/SnapAvailability.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>

namespace lut {

SnapBackend::SnapBackend(ProcessRunner runner, bool forceAvailable, QObject *parent)
    : Backend(parent)
    , m_runner(std::move(runner))
    , m_forceAvailable(forceAvailable)
{
}

SnapBackend::~SnapBackend()
{
    m_cancelled = true;
    if (m_activeProcess && m_activeProcess->state() != QProcess::NotRunning) {
        m_activeProcess->terminate();
        if (!m_activeProcess->waitForFinished(3000)) {
            m_activeProcess->kill();
        }
    }
}

bool SnapBackend::isSnapAvailable()
{
    return SnapAvailability::isSnapAvailable();
}

Capabilities SnapBackend::capabilities() const
{
    Capabilities c;
    c.protocolVersion = 2;
    c.typedPackageTargets = true;
    c.transactionReattach = false;

    const bool avail = m_forceAvailable || (m_runner ? true : isSnapAvailable());

    SourceCapabilities snapCap;
    snapCap.source = QStringLiteral("snap");
    snapCap.available = avail;
    snapCap.install = avail;
    snapCap.remove = avail;
    snapCap.systemScope = true;
    snapCap.userScope = false;
    snapCap.boundRevision = true;
    snapCap.installRequiresFullUpgrade = false; // Abschnitt 4.2: Strikt false für Snap!
    c.sources = {snapCap};

    c.install = avail;
    c.remove = avail;
    return c;
}

int SnapBackend::runCommand(const QStringList &args, QString &stdoutOut, QString &stderrOut)
{
    if (m_runner) {
        return m_runner(QStringLiteral("snap"), args, stdoutOut, stderrOut);
    }

    if (!isSnapAvailable()) {
        stderrOut = QStringLiteral("snapd ist auf diesem System nicht verfügbar.");
        return -1;
    }

    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    proc.setProcessEnvironment(env);
    proc.start(QStringLiteral("/usr/bin/snap"), args);

    if (!proc.waitForStarted(5000)) {
        stderrOut = QStringLiteral("Fehler beim Starten von /usr/bin/snap");
        return -1;
    }

    while (proc.state() == QProcess::Running) {
        if (m_cancelled) {
            proc.kill();
            proc.waitForFinished(1000);
            return -2;
        }
        proc.waitForReadyRead(100);
    }

    stdoutOut = QString::fromUtf8(proc.readAllStandardOutput());
    stderrOut = QString::fromUtf8(proc.readAllStandardError());
    return proc.exitCode();
}

bool SnapBackend::isArchLinux() const
{
    if (QFile::exists(QStringLiteral("/etc/arch-release"))) {
        return true;
    }
    QFile osRelease(QStringLiteral("/etc/os-release"));
    if (osRelease.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString content = QString::fromUtf8(osRelease.readAll());
        if (content.contains(QLatin1String("ID=arch")) || content.contains(QLatin1String("ID_LIKE=arch"))) {
            return true;
        }
    }
    return false;
}

SnapBackend::SnapDetails SnapBackend::parseSnapInfo(const QString &infoText, const QString &channel)
{
    SnapDetails details;
    details.channel = channel.isEmpty() ? QStringLiteral("latest/stable") : channel;

    const QStringList lines = infoText.split(QLatin1Char('\n'));
    bool inChannels = false;

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("name:"))) {
            details.name = trimmed.mid(5).trimmed();
        } else if (trimmed.startsWith(QLatin1String("confinement:"))) {
            details.confinement = trimmed.mid(12).trimmed();
        } else if (trimmed.startsWith(QLatin1String("channels:"))) {
            inChannels = true;
            continue;
        }

        if (inChannels) {
            if (!line.startsWith(QLatin1String("  "))) {
                inChannels = false;
                continue;
            }

            // e.g. "  latest/stable:    3.0.19 2023-10-11 (3721) 338MB -"
            // or   "  latest/stable:    1.85.1 2023-12-13 (159) 110MB classic"
            const int colonIdx = line.indexOf(QLatin1Char(':'));
            if (colonIdx != -1) {
                const QString chanName = line.left(colonIdx).trimmed();
                if (chanName == details.channel || (details.revision.isEmpty() && chanName == QLatin1String("latest/stable"))) {
                    const QString rest = line.mid(colonIdx + 1).trimmed();
                    // Extract revision inside parentheses
                    static const QRegularExpression revRegex(QStringLiteral("\\((\\d+)\\)"));
                    const auto revMatch = revRegex.match(rest);
                    if (revMatch.hasMatch()) {
                        details.revision = revMatch.captured(1);
                    }

                    // Extract tokens
                    const QStringList tokens = rest.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
                    if (!tokens.isEmpty()) {
                        details.version = tokens.first();
                    }

                    for (const QString &tok : tokens) {
                        if (tok.endsWith(QLatin1String("MB"), Qt::CaseInsensitive)) {
                            bool ok = false;
                            double mb = tok.left(tok.length() - 2).toDouble(&ok);
                            if (ok) details.downloadSize = static_cast<qint64>(mb * 1024 * 1024);
                        } else if (tok.endsWith(QLatin1String("kB"), Qt::CaseInsensitive) || tok.endsWith(QLatin1String("KB"), Qt::CaseInsensitive)) {
                            bool ok = false;
                            double kb = tok.left(tok.length() - 2).toDouble(&ok);
                            if (ok) details.downloadSize = static_cast<qint64>(kb * 1024);
                        } else if (tok == QLatin1String("classic") || tok == QLatin1String("devmode")) {
                            details.confinement = tok;
                        }
                    }
                }
            }
        }
    }

    if (details.confinement.isEmpty()) {
        details.confinement = QStringLiteral("strict");
    }
    return details;
}

SnapBackend::SnapDetails SnapBackend::parseSnapFindJson(const QString &jsonText, const QString &snapName)
{
    SnapDetails details;
    details.channel = QStringLiteral("latest/stable");
    details.confinement = QStringLiteral("strict");

    QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8());
    if (doc.isNull()) return details;

    QJsonObject targetObj;
    if (doc.isObject()) {
        QJsonObject root = doc.object();
        if (root.contains(QLatin1String("result"))) {
            QJsonValue resVal = root.value(QLatin1String("result"));
            if (resVal.isArray()) {
                QJsonArray arr = resVal.toArray();
                for (const auto &item : arr) {
                    QJsonObject obj = item.toObject();
                    if (obj.value(QLatin1String("name")).toString() == snapName || snapName.isEmpty()) {
                        targetObj = obj;
                        break;
                    }
                }
            } else if (resVal.isObject()) {
                targetObj = resVal.toObject();
            }
        } else {
            targetObj = root;
        }
    }

    if (!targetObj.isEmpty()) {
        details.name = targetObj.value(QLatin1String("name")).toString();
        details.version = targetObj.value(QLatin1String("version")).toString();
        details.revision = targetObj.value(QLatin1String("revision")).toString();
        if (targetObj.contains(QLatin1String("channel"))) {
            details.channel = targetObj.value(QLatin1String("channel")).toString();
        }
        if (targetObj.contains(QLatin1String("confinement"))) {
            details.confinement = targetObj.value(QLatin1String("confinement")).toString();
        }
        if (targetObj.contains(QLatin1String("download-size"))) {
            details.downloadSize = targetObj.value(QLatin1String("download-size")).toInteger();
        }
    }

    return details;
}

void SnapBackend::planPackageTransaction(const TransactionIntent &intent)
{
    m_cancelled = false;
    m_currentIntent = intent;
    m_currentPlan = TransactionPlan();

    if (intent.targets.isEmpty()) {
        emit eventEmitted(TransactionDone{Result::Failed, QStringLiteral("Keine Snap-Ziele angegeben"), false, {}, 0});
        return;
    }

    const auto &target = intent.targets.first();
    const QString snapName = target.name;
    m_targetSnap = snapName;
    m_targetChannel = target.repoId.isEmpty() ? QStringLiteral("latest/stable") : target.repoId;

    emit eventEmitted(PhaseChanged{Phase::Resolve, QStringLiteral("Löse Snap-Metadaten auf..."), false});

    if (intent.type == TransactionIntent::Type::Remove) {
        PackageOp op;
        op.name = snapName;
        op.kind = PackageOp::Kind::Remove;
        op.arch = QStringLiteral("x86_64");

        m_currentPlan.ops = {op};
        m_currentPlan.planRevision = QStringLiteral("snap-remove-") + snapName;
        PlanReady planReady;
        planReady.ops = m_currentPlan.ops;
        planReady.downloadBytes = m_currentPlan.downloadBytes;
        planReady.installedSizeDelta = m_currentPlan.installedSizeDelta;
        planReady.warnings = m_currentPlan.warnings;
        planReady.planRevision = m_currentPlan.planRevision;
        emit eventEmitted(planReady);
        emit eventEmitted(PhaseChanged{Phase::Idle, QStringLiteral("Bereit zur Deinstallation"), false});
        return;
    }

    // Für Install: snap info abfragen
    QString stdoutOut, stderrOut;
    const int rc = runCommand({QStringLiteral("info"), snapName, QStringLiteral("--unicode=never")}, stdoutOut, stderrOut);

    SnapDetails details;
    if (rc == 0 && !stdoutOut.isEmpty()) {
        details = parseSnapInfo(stdoutOut, m_targetChannel);
    } else {
        // Fallback: versuche JSON falls info nicht CLI-Text war oder runner JSON injizierte
        details = parseSnapFindJson(stdoutOut, snapName);
    }

    if (details.revision.isEmpty()) {
        // Falls keine Revisionsnummer ermittelt werden konnte, aber Version da ist
        if (!target.version.isEmpty()) {
            details.version = target.version;
            details.revision = QStringLiteral("current");
        } else {
            emit eventEmitted(TransactionDone{Result::Failed,
                QStringLiteral("Konnte Revision für Snap '%1' nicht ermitteln: %2").arg(snapName, stderrOut),
                false, {}, 0});
            return;
        }
    }

    m_boundRevision = details.revision;
    m_confinement = details.confinement;

    // Confinement-Prüfung (Abschnitt 7.3)
    if (details.confinement == QLatin1String("devmode")) {
        emit eventEmitted(TransactionDone{Result::Failed,
            QStringLiteral("Snaps mit devmode-Confinement werden in Version 1 nicht angeboten."),
            false, {}, 0});
        return;
    }

    if (details.confinement == QLatin1String("classic")) {
        m_currentPlan.warnings.append(QStringLiteral(
            "Warnung: Dieser Snap erfordert Classic-Confinement. Die Sicherheits-Sandbox wird weitgehend aufgehoben."));

        // Arch-Linux-Prüfung für Classic-Snaps (Abschnitt 7.3 Satz 3)
        if (isArchLinux()) {
            QFileInfo snapDir(QStringLiteral("/snap"));
            if (!snapDir.exists()) {
                m_currentPlan.warnings.append(QStringLiteral(
                    "Hinweis für Arch Linux: Für Classic-Snaps ist der symbolische Link von /snap nach /var/lib/snapd/snap erforderlich ('sudo ln -s /var/lib/snapd/snap /snap'). Fehlt dieser, schlägt die Installation fehl."));
            }
        }
    }

    PackageOp op;
    op.name = snapName;
    op.kind = PackageOp::Kind::Install;
    op.newVersion = QStringLiteral("%1 (rev %2)").arg(details.version, details.revision);
    op.downloadSize = details.downloadSize;
    op.arch = QStringLiteral("x86_64");

    m_currentPlan.ops = {op};
    m_currentPlan.downloadBytes = details.downloadSize;
    m_currentPlan.planRevision = details.revision; // Revisionsbindung (Abschnitt 7.2)

    PlanReady planReady;
    planReady.ops = m_currentPlan.ops;
    planReady.downloadBytes = m_currentPlan.downloadBytes;
    planReady.installedSizeDelta = m_currentPlan.installedSizeDelta;
    planReady.warnings = m_currentPlan.warnings;
    planReady.planRevision = m_currentPlan.planRevision;
    emit eventEmitted(planReady);
    emit eventEmitted(PhaseChanged{Phase::Idle, QStringLiteral("Bereit zur Installation"), false});
}

void SnapBackend::commit()
{
    commitPlan(m_boundRevision);
}

void SnapBackend::commitPlan(const QString &planRevision)
{
    if (m_currentIntent.type == TransactionIntent::Type::Install) {
        if (!planRevision.isEmpty() && !m_boundRevision.isEmpty() && planRevision != m_boundRevision) {
            emit eventEmitted(TransactionDone{Result::Failed,
                QStringLiteral("Snap-Revision hat sich geändert (erwartet: %1, übergeben: %2)").arg(m_boundRevision, planRevision),
                false, {}, 0});
            return;
        }

        emit eventEmitted(PhaseChanged{Phase::Commit, QStringLiteral("Installiere Snap..."), false});

        QStringList args = {
            QStringLiteral("install"),
            m_targetSnap
        };

        if (!m_boundRevision.isEmpty() && m_boundRevision != QLatin1String("current")) {
            args << QStringLiteral("--revision=%1").arg(m_boundRevision);
        }

        if (m_confinement == QLatin1String("classic")) {
            args << QStringLiteral("--classic");
        }

        QString stdoutOut, stderrOut;
        const int rc = runCommand(args, stdoutOut, stderrOut);

        if (m_cancelled) {
            emit eventEmitted(TransactionDone{Result::Cancelled, QStringLiteral("Installation abgebrochen"), false, {}, 0});
            return;
        }

        if (rc != 0) {
            QString errMsg = stderrOut.trimmed();
            if (errMsg.isEmpty()) errMsg = stdoutOut.trimmed();
            if (errMsg.isEmpty()) errMsg = QStringLiteral("Snap-Installation fehlgeschlagen (Code %1)").arg(rc);

            if (m_confinement == QLatin1String("classic") && isArchLinux() && !QFile::exists(QStringLiteral("/snap"))) {
                errMsg += QStringLiteral(" [Ursache: Fehlender /snap Symlink unter Arch Linux]");
            }

            emit eventEmitted(TransactionDone{Result::Failed, errMsg, false, {}, 0});
            return;
        }

        emit eventEmitted(TransactionDone{Result::Success,
            QStringLiteral("Snap '%1' erfolgreich installiert").arg(m_targetSnap),
            false, {}, 0});

    } else if (m_currentIntent.type == TransactionIntent::Type::Remove) {
        emit eventEmitted(PhaseChanged{Phase::Commit, QStringLiteral("Entferne Snap..."), false});

        const QStringList args = {
            QStringLiteral("remove"),
            m_targetSnap
        };

        QString stdoutOut, stderrOut;
        const int rc = runCommand(args, stdoutOut, stderrOut);

        if (m_cancelled) {
            emit eventEmitted(TransactionDone{Result::Cancelled, QStringLiteral("Entfernung abgebrochen"), false, {}, 0});
            return;
        }

        if (rc != 0) {
            QString errMsg = stderrOut.trimmed();
            if (errMsg.isEmpty()) errMsg = stdoutOut.trimmed();
            if (errMsg.isEmpty()) errMsg = QStringLiteral("Snap-Entfernung fehlgeschlagen (Code %1)").arg(rc);
            emit eventEmitted(TransactionDone{Result::Failed, errMsg, false, {}, 0});
            return;
        }

        emit eventEmitted(TransactionDone{Result::Success,
            QStringLiteral("Snap '%1' erfolgreich entfernt").arg(m_targetSnap),
            false, {}, 0});
    }
}

void SnapBackend::discardPlan()
{
    m_currentPlan = TransactionPlan();
    m_boundRevision.clear();
    m_targetSnap.clear();
    emit eventEmitted(PhaseChanged{Phase::Idle, QStringLiteral("Plan verworfen"), false});
}

void SnapBackend::cancel()
{
    m_cancelled = true;
    emit eventEmitted(TransactionDone{Result::Cancelled, QStringLiteral("Aktion abgebrochen"), false, {}, 0});
}

void SnapBackend::refreshMetadata() {}
void SnapBackend::planUpgradeAll(const UpgradeOptions &) {}
void SnapBackend::planInstall(const QStringList &) {}
void SnapBackend::planRemove(const QStringList &) {}
void SnapBackend::answerQuestion(const QString &, const QJsonObject &) {}
QList<PackageOp> SnapBackend::availableUpdates()
{
    if (!m_forceAvailable && !isSnapAvailable()) return {};
    QString stdoutOut, stderrOut;
    int rc = runCommand({QStringLiteral("refresh"), QStringLiteral("--list"), QStringLiteral("--unicode=never"), QStringLiteral("--color=never")}, stdoutOut, stderrOut);
    if (rc != 0) return {};
    return parseRefreshList(stdoutOut);
}

QList<PackageOp> SnapBackend::parseRefreshList(const QString &output)
{
    QList<PackageOp> ops;
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.isEmpty()) return ops;

    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1String("Name ")) || line.startsWith(QLatin1String("All snaps up to date"))) {
            continue;
        }

        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 4) continue;

        PackageOp op;
        op.name = parts.at(0);
        op.newVersion = parts.at(1);
        op.repo = QStringLiteral("snap");
        op.summary = QStringLiteral("Rev %1").arg(parts.at(2));
        op.kind = PackageOp::Kind::Upgrade;
        ops.append(op);
    }
    return ops;
}

QList<InstalledPackage> SnapBackend::installedPackages(const QString &) { return {}; }
QList<ChangelogEntry> SnapBackend::changelog(const QString &) { return {}; }

QList<HistoryEntry> SnapBackend::history(int limit)
{
    if (!m_forceAvailable && !isSnapAvailable()) return {};
    QString stdoutOut, stderrOut;
    int rc = runCommand({QStringLiteral("changes")}, stdoutOut, stderrOut);
    if (rc != 0) return {};
    return parseChanges(stdoutOut, limit);
}

QList<HistoryEntry> SnapBackend::parseChanges(const QString &output, int limit)
{
    QList<HistoryEntry> list;
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = 0; i < lines.size() && list.size() < limit; ++i) {
        const QString line = lines.at(i).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1String("ID "))) continue;

        static const QRegularExpression changeRe(QStringLiteral(R"(^(\d+)\s+(\S+)\s+(.*?at\s+\S+\s+\S+|\S+)\s+(.*?at\s+\S+\s+\S+|\S+)\s+(.*)$)"));
        auto match = changeRe.match(line);
        if (!match.hasMatch()) continue;

        HistoryEntry entry;
        entry.id = match.captured(1).toLongLong();
        const QString status = match.captured(2);
        entry.result = (status == QLatin1String("Done")) ? QStringLiteral("Success") : QStringLiteral("Failed");
        entry.command = QStringLiteral("[Snap] %1").arg(match.captured(5).trimmed());
        entry.packagesAltered = 1;
        entry.canUndo = false;
        list.append(entry);
    }
    return list;
}

} // namespace lut
