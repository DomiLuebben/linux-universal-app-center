#include "SnapUpdates.h"
#include "linux-app-store/DaemonClient.h"
#include "linux-app-store/OperationResult.h"
#include "liblut/backend/snap/SnapAvailability.h"
#include <QRegularExpression>

namespace lut {

SnapUpdates::SnapUpdates(QObject *parent)
    : QAbstractListModel(parent)
{
}

SnapUpdates::~SnapUpdates()
{
    if (m_activeProcess && m_activeProcess->state() != QProcess::NotRunning) {
        m_activeProcess->terminate();
        if (!m_activeProcess->waitForFinished(3000)) {
            m_activeProcess->kill();
        }
    }
}

bool SnapUpdates::available() const
{
    if (m_forceAvailable.has_value()) return *m_forceAvailable;
    return SnapAvailability::isSnapAvailable();
}

void SnapUpdates::setDaemonClient(DaemonClient *client)
{
    m_daemonClient = client;
}

void SnapUpdates::setProcessRunner(ProcessRunner runner)
{
    m_runner = std::move(runner);
}

void SnapUpdates::setForceAvailable(bool force)
{
    m_forceAvailable = force;
}

int SnapUpdates::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_items.size();
}

QVariant SnapUpdates::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const auto &item = m_items.at(index.row());
    switch (role) {
    case NameRole: return item.name;
    case CurrentVersionRole: return item.currentVersion;
    case NewVersionRole: return item.newVersion;
    case VersionTransitionRole: return item.versionTransition;
    case RevisionRole: return item.revision;
    case PublisherRole: return item.publisher;
    case NotesRole: return item.notes;
    default: return {};
    }
}

QHash<int, QByteArray> SnapUpdates::roleNames() const
{
    return {
        {NameRole, "name"},
        {CurrentVersionRole, "currentVersion"},
        {NewVersionRole, "newVersion"},
        {VersionTransitionRole, "versionTransition"},
        {RevisionRole, "revision"},
        {PublisherRole, "publisher"},
        {NotesRole, "notes"}
    };
}

int SnapUpdates::runCommand(const QStringList &args, QString &stdoutOut, QString &stderrOut)
{
    if (m_runner) {
        return m_runner(args, stdoutOut, stderrOut);
    }

    if (!available()) {
        stderrOut = QStringLiteral("snapd is not available");
        return -1;
    }

    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    proc.setProcessEnvironment(env);
    proc.start(QStringLiteral("/usr/bin/snap"), args);

    if (!proc.waitForStarted(5000)) {
        stderrOut = QStringLiteral("Failed to start snap process");
        return -1;
    }

    if (!proc.waitForFinished(60000)) {
        proc.kill();
        stderrOut = QStringLiteral("snap process timed out");
        return -1;
    }

    stdoutOut = QString::fromUtf8(proc.readAllStandardOutput());
    stderrOut = QString::fromUtf8(proc.readAllStandardError());
    return proc.exitCode();
}

void SnapUpdates::setBusy(bool b)
{
    if (m_busy != b) {
        m_busy = b;
        emit busyChanged();
    }
}

void SnapUpdates::setStatus(const QString &msg)
{
    if (m_statusMessage != msg) {
        m_statusMessage = msg;
        emit statusChanged();
    }
}

QList<SnapUpdates::Entry> SnapUpdates::parseRefreshOutput(const QString &refreshListOutput)
{
    QList<Entry> result;
    const QStringList lines = refreshListOutput.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1String("Name ")) || trimmed.startsWith(QLatin1String("All snaps up to date"))) {
            continue;
        }

        const QStringList parts = trimmed.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 4) continue;

        Entry entry;
        entry.name = parts.at(0);
        entry.newVersion = parts.at(1);
        entry.revision = parts.at(2);
        entry.publisher = parts.at(3);
        if (parts.size() >= 5) {
            entry.notes = parts.at(4);
        }
        entry.currentVersion = QStringLiteral("-");
        entry.versionTransition = entry.newVersion;

        result.append(entry);
    }
    return result;
}

void SnapUpdates::check()
{
    if (!available()) return;

    QString stdoutOut, stderrOut;
    int rc = runCommand({QStringLiteral("refresh"), QStringLiteral("--list"), QStringLiteral("--unicode=never"), QStringLiteral("--color=never")}, stdoutOut, stderrOut);

    auto entries = (rc == 0) ? parseRefreshOutput(stdoutOut) : QList<Entry>{};

    beginResetModel();
    m_items = entries;
    m_hasChecked = true;
    endResetModel();

    emit countChanged();
}

bool SnapUpdates::parseRefreshedLine(const QString &line, QString *name)
{
    if (name) name->clear();
    const QString text = line.trimmed();
    if (!text.contains(QStringLiteral("refreshed"), Qt::CaseInsensitive)) return false;
    // "<name> <version> from <publisher> refreshed"
    static const QRegularExpression pattern(QStringLiteral("^([a-z0-9][a-z0-9-]*)\\s+\\S+.*\\brefreshed\\b"),
                                            QRegularExpression::CaseInsensitiveOption);
    const auto match = pattern.match(text);
    if (!match.hasMatch()) return false;
    if (name) *name = match.captured(1);
    return true;
}

void SnapUpdates::handleOutput(const QByteArray &chunk, QByteArray &buffer, bool isError)
{
    buffer.append(chunk);
    qsizetype cut = -1;
    while ((cut = buffer.indexOf('\n')) >= 0 || (cut = buffer.indexOf('\r')) >= 0) {
        const QString line = QString::fromUtf8(buffer.left(cut)).trimmed();
        buffer.remove(0, cut + 1);
        if (line.isEmpty()) continue;
        if (isError) m_lastError = line;
        handleOutputLine(line);
    }
}

void SnapUpdates::handleOutputLine(const QString &line)
{
    emit logLine(line);
    QString name;
    if (!parseRefreshedLine(line, &name)) return;
    ++m_opDone;
    const bool known = m_opTotal > 0;
    const double fraction = known ? qBound(0.0, double(m_opDone) / m_opTotal, 1.0) : 0.0;
    emit operationProgress(fraction, !known, m_opDone, m_opTotal, tr("%1 aktualisiert").arg(name));
}

void SnapUpdates::endOperation(int result, const QString &message, const QString &target)
{
    setBusy(false);
    setStatus(message);
    emit operationFinished(result, message);
    if (result == OperationResult::Succeeded) {
        emit finished(target);
    } else if (result == OperationResult::Failed) {
        emit failed(m_lastError);
    }
    check();
}

void SnapUpdates::runUpdate(const QStringList &args, const QString &target, const QString &title, int expectedSteps)
{
    // Globale Mutationssperre
    if (m_busy) return;
    if (m_daemonClient && m_daemonClient->isBusy()) {
        setStatus(QStringLiteral("Eine andere Paketaktion läuft bereits."));
        emit failed(QStringLiteral("Eine andere Paketaktion läuft bereits."));
        return;
    }

    m_opDone = 0;
    m_opTotal = expectedSteps;
    m_lastError.clear();
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_cancelRequested = false;
    setBusy(true);
    setStatus(title);
    emit operationStarted(title);
    emit operationProgress(0.0, true, 0, m_opTotal, tr("Snaps werden aktualisiert …"));

    const QString successText = target.isEmpty() ? QStringLiteral("Alle Snaps erfolgreich aktualisiert.")
                                                 : QStringLiteral("Snap-Aktualisierung erfolgreich.");
    if (m_runner) {
        QString stdoutOut, stderrOut;
        const int rc = m_runner(args, stdoutOut, stderrOut);
        const auto lines = stdoutOut.split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::SkipEmptyParts);
        for (const QString &line : lines) handleOutputLine(line.trimmed());
        if (rc == 0) {
            endOperation(OperationResult::Succeeded, successText, target);
        } else {
            m_lastError = stderrOut.trimmed();
            endOperation(OperationResult::Failed, QStringLiteral("Aktualisierung fehlgeschlagen: %1").arg(m_lastError), target);
        }
        return;
    }

    if (!available()) {
        m_lastError = QStringLiteral("snapd is not available");
        endOperation(OperationResult::Failed, QStringLiteral("Aktualisierung fehlgeschlagen: snapd ist nicht verfügbar."), target);
        return;
    }

    // Asynchron: "snap refresh" dauert Minuten und darf die Oberfläche nicht einfrieren.
    m_activeProcess = std::make_unique<QProcess>();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    m_activeProcess->setProcessEnvironment(env);
    auto *proc = m_activeProcess.get();
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        if (proc != m_activeProcess.get()) return;
        handleOutput(proc->readAllStandardOutput(), m_stdoutBuffer, false);
    });
    connect(proc, &QProcess::readyReadStandardError, this, [this, proc]() {
        if (proc != m_activeProcess.get()) return;
        handleOutput(proc->readAllStandardError(), m_stderrBuffer, true);
    });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, target, successText](int exitCode, QProcess::ExitStatus exitStatus) {
        if (proc != m_activeProcess.get()) return;
        handleOutput(proc->readAllStandardOutput() + "\n", m_stdoutBuffer, false);
        handleOutput(proc->readAllStandardError() + "\n", m_stderrBuffer, true);
        if (m_cancelRequested) {
            m_cancelRequested = false;
            endOperation(OperationResult::Cancelled, QStringLiteral("Aktion abgebrochen."), target);
            return;
        }
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            endOperation(OperationResult::Succeeded, successText, target);
        } else {
            if (m_lastError.isEmpty()) m_lastError = QStringLiteral("Prozess mit Fehlercode %1 beendet.").arg(exitCode);
            endOperation(OperationResult::Failed, QStringLiteral("Aktualisierung fehlgeschlagen: %1").arg(m_lastError), target);
        }
    });
    proc->start(QStringLiteral("/usr/bin/snap"), args);
}

void SnapUpdates::updateApp(const QString &name)
{
    runUpdate({QStringLiteral("refresh"), name}, name, QStringLiteral("Snap-Paket wird aktualisiert …"), 1);
}

void SnapUpdates::updateAll()
{
    runUpdate({QStringLiteral("refresh")}, QString(), QStringLiteral("Alle Snaps werden aktualisiert …"),
              static_cast<int>(m_items.size()));
}

void SnapUpdates::cancel()
{
    if (m_activeProcess && m_activeProcess->state() != QProcess::NotRunning) {
        m_cancelRequested = true;
        m_activeProcess->kill();
        return;
    }
    setBusy(false);
    setStatus(QStringLiteral("Aktion abgebrochen."));
}

} // namespace lut
