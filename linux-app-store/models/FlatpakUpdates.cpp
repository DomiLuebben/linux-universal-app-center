#include "FlatpakUpdates.h"
#include "linux-app-store/OperationResult.h"
#include "linux-app-store/DaemonClient.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QLocale>

namespace lut {

FlatpakUpdates::FlatpakUpdates(QObject *parent)
    : QAbstractListModel(parent)
{
}

FlatpakUpdates::~FlatpakUpdates()
{
    if (m_activeProcess && m_activeProcess->state() != QProcess::NotRunning) {
        m_activeProcess->terminate();
        if (!m_activeProcess->waitForFinished(3000)) {
            m_activeProcess->kill();
        }
    }
}

bool FlatpakUpdates::available() const
{
    if (m_forceAvailable.has_value()) return *m_forceAvailable;
    return QFile::exists(QStringLiteral("/usr/bin/flatpak"));
}

void FlatpakUpdates::setDaemonClient(DaemonClient *client)
{
    m_daemonClient = client;
}

void FlatpakUpdates::setProcessRunner(ProcessRunner runner)
{
    m_runner = std::move(runner);
}

void FlatpakUpdates::setForceAvailable(bool force)
{
    m_forceAvailable = force;
}

int FlatpakUpdates::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_items.size();
}

QVariant FlatpakUpdates::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const auto &item = m_items.at(index.row());
    switch (role) {
    case IdRole: return item.id;
    case NameRole: return item.name;
    case OriginRole: return item.origin;
    case CurrentVersionRole: return item.currentVersion;
    case NewVersionRole: return item.newVersion;
    case VersionTransitionRole: return item.versionTransition;
    case RefRole: return item.ref;
    case DownloadSizeRole: return item.downloadSize;
    case DownloadSizeFormattedRole: return item.downloadSizeFormatted;
    default: return {};
    }
}

QHash<int, QByteArray> FlatpakUpdates::roleNames() const
{
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {OriginRole, "origin"},
        {CurrentVersionRole, "currentVersion"},
        {NewVersionRole, "newVersion"},
        {VersionTransitionRole, "versionTransition"},
        {RefRole, "ref"},
        {DownloadSizeRole, "downloadSize"},
        {DownloadSizeFormattedRole, "downloadSizeFormatted"}
    };
}

QString FlatpakUpdates::formatBytes(qint64 bytes)
{
    if (bytes <= 0) return QStringLiteral("0 B");
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    double size = bytes;
    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }
    return QStringLiteral("%1 %2").arg(QString::number(size, 'f', unitIndex > 1 ? 1 : 0), QString::fromLatin1(units[unitIndex]));
}

QString FlatpakUpdates::totalDownloadFormatted() const
{
    return formatBytes(m_totalDownloadBytes);
}

int FlatpakUpdates::runCommand(const QStringList &args, QString &stdoutOut, QString &stderrOut)
{
    if (m_runner) {
        return m_runner(args, stdoutOut, stderrOut);
    }

    if (!available()) {
        stderrOut = QStringLiteral("flatpak executable not found");
        return -1;
    }

    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    proc.setProcessEnvironment(env);
    proc.start(QStringLiteral("/usr/bin/flatpak"), args);

    if (!proc.waitForStarted(5000)) {
        stderrOut = QStringLiteral("Failed to start flatpak process");
        return -1;
    }

    if (!proc.waitForFinished(60000)) {
        proc.kill();
        stderrOut = QStringLiteral("flatpak process timed out");
        return -1;
    }

    stdoutOut = QString::fromUtf8(proc.readAllStandardOutput());
    stderrOut = QString::fromUtf8(proc.readAllStandardError());
    return proc.exitCode();
}

void FlatpakUpdates::setBusy(bool b)
{
    if (m_busy != b) {
        m_busy = b;
        emit busyChanged();
    }
}

void FlatpakUpdates::setStatus(const QString &msg)
{
    if (m_statusMessage != msg) {
        m_statusMessage = msg;
        emit statusChanged();
    }
}

QList<FlatpakUpdates::Entry> FlatpakUpdates::parseUpdatesOutput(const QString &remoteLsOutput, const QString &listOutput)
{
    QHash<QString, QString> installedVersions;
    if (!listOutput.isEmpty()) {
        const QStringList instLines = listOutput.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &l : instLines) {
            const QString trimmed = l.trimmed();
            if (trimmed.isEmpty()) continue;
            const QStringList parts = trimmed.split(QLatin1Char('\t'));
            if (parts.size() >= 2) {
                installedVersions.insert(parts.at(0).trimmed(), parts.at(1).trimmed());
            }
        }
    }

    QList<Entry> result;
    const QStringList lines = remoteLsOutput.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        const QStringList parts = trimmed.split(QLatin1Char('\t'));
        if (parts.size() < 4) continue;

        Entry entry;
        entry.id = parts.at(0).trimmed();
        entry.name = entry.id;
        entry.origin = parts.at(1).trimmed();
        entry.newVersion = parts.at(2).trimmed();
        entry.ref = parts.at(3).trimmed();
        if (parts.size() >= 5) {
            entry.downloadSize = parts.at(4).trimmed().toLongLong();
        }
        entry.downloadSizeFormatted = formatBytes(entry.downloadSize);

        if (installedVersions.contains(entry.id)) {
            entry.currentVersion = installedVersions.value(entry.id);
        } else {
            entry.currentVersion = QStringLiteral("-");
        }

        if (entry.currentVersion != QStringLiteral("-") && !entry.currentVersion.isEmpty()) {
            entry.versionTransition = QStringLiteral("%1 \u2192 %2").arg(entry.currentVersion, entry.newVersion);
        } else {
            entry.versionTransition = entry.newVersion;
        }

        result.append(entry);
    }
    return result;
}

void FlatpakUpdates::check()
{
    if (!available()) return;

    QString remoteLsOut, listOut, stderrOut;
    int rc = runCommand({QStringLiteral("remote-ls"), QStringLiteral("--system"), QStringLiteral("--app"), QStringLiteral("--updates"), QStringLiteral("--columns=application:f,origin:f,version:f,ref:f,download-size:f")}, remoteLsOut, stderrOut);
    if (rc == 0) {
        runCommand({QStringLiteral("list"), QStringLiteral("--system"), QStringLiteral("--app"), QStringLiteral("--columns=application:f,version:f")}, listOut, stderrOut);
    }

    auto entries = (rc == 0) ? parseUpdatesOutput(remoteLsOut, listOut) : QList<Entry>{};

    beginResetModel();
    m_items = entries;
    m_totalDownloadBytes = 0;
    for (const auto &item : m_items) {
        m_totalDownloadBytes += item.downloadSize;
    }
    m_hasChecked = true;
    endResetModel();

    emit countChanged();
}

bool FlatpakUpdates::parseProgressLine(const QString &line, int *step, int *total, QString *item)
{
    if (step) *step = 0;
    if (total) *total = 0;
    if (item) item->clear();

    const QString text = line.trimmed();
    if (text.isEmpty()) return false;

    // "Updating 2/5…" bzw. "Installing 1/3…"
    static const QRegularExpression counted(
        QStringLiteral("^(?:Installing|Updating|Uninstalling)\\s+(\\d+)\\s*/\\s*(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);
    auto match = counted.match(text);
    if (match.hasMatch()) {
        if (step) *step = match.captured(1).toInt();
        if (total) *total = match.captured(2).toInt();
        return true;
    }

    // Tabellenzeile "1. [✓] org.gnome.Calculator  stable  u  flathub  1.0 MB"
    static const QRegularExpression tableRow(
        QStringLiteral("^(\\d+)\\.\\s+\\[[^\\]]*\\]\\s+(\\S+)"));
    match = tableRow.match(text);
    if (match.hasMatch()) {
        if (step) *step = match.captured(1).toInt();
        if (item) *item = match.captured(2);
        return true;
    }

    // "Updating app/org.gnome.Calculator/x86_64/stable"
    static const QRegularExpression named(
        QStringLiteral("^(?:Installing|Updating|Uninstalling)\\s+(?:app/|runtime/)?([A-Za-z0-9_.-]+)"),
        QRegularExpression::CaseInsensitiveOption);
    match = named.match(text);
    if (match.hasMatch()) {
        if (item) *item = match.captured(1);
        return true;
    }
    return false;
}

void FlatpakUpdates::beginOperation(const QString &title, int expectedSteps)
{
    m_opStep = 0;
    m_opTotal = expectedSteps;
    m_lastError.clear();
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_cancelRequested = false;
    setBusy(true);
    setStatus(title);
    emit operationStarted(title);
    emit operationProgress(0.0, true, 0, m_opTotal, tr("Suche nach Aktualisierungen …"));
}

void FlatpakUpdates::endOperation(int result, const QString &message)
{
    setBusy(false);
    setStatus(message);
    emit operationFinished(result, message);
}

void FlatpakUpdates::handleOutput(const QByteArray &chunk, QByteArray &buffer, bool isError)
{
    buffer.append(chunk);
    // Flatpak schreibt Fortschritt teils mit Wagenrücklauf statt Zeilenumbruch.
    qsizetype cut = -1;
    while ((cut = buffer.indexOf('\n')) >= 0 || (cut = buffer.indexOf('\r')) >= 0) {
        const QString line = QString::fromUtf8(buffer.left(cut)).trimmed();
        buffer.remove(0, cut + 1);
        if (line.isEmpty()) continue;
        if (isError) m_lastError = line;
        handleOutputLine(line);
    }
}

void FlatpakUpdates::handleOutputLine(const QString &line)
{
    emit logLine(line);

    int step = 0;
    int total = 0;
    QString item;
    if (!parseProgressLine(line, &step, &total, &item)) return;

    if (step > 0) m_opStep = step; else ++m_opStep;
    if (total > 0) m_opTotal = total;

    const bool known = m_opTotal > 0;
    const double fraction = known ? qBound(0.0, double(m_opStep - 1) / m_opTotal, 1.0) : 0.0;
    const QString text = item.isEmpty() ? line : tr("Aktualisiere %1").arg(item);
    emit operationProgress(fraction, !known, m_opStep, m_opTotal, text);
}

void FlatpakUpdates::executeUpdateProcess(const QStringList &args, const QString &targetId)
{
    const QString successText = targetId.isEmpty() ? QStringLiteral("Alle Flatpaks erfolgreich aktualisiert.")
                                                    : QStringLiteral("Aktualisierung erfolgreich.");
    if (m_runner) {
        QString stdoutOut, stderrOut;
        int rc = m_runner(args, stdoutOut, stderrOut);
        const auto lines = stdoutOut.split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::SkipEmptyParts);
        for (const QString &line : lines) handleOutputLine(line.trimmed());
        if (rc == 0) {
            endOperation(OperationResult::Succeeded, successText);
            emit finished(targetId);
            check();
        } else {
            const QString err = stderrOut.trimmed();
            endOperation(OperationResult::Failed, QStringLiteral("Aktualisierung fehlgeschlagen: %1").arg(err));
            emit failed(err);
        }
        return;
    }

    if (!available()) {
        endOperation(OperationResult::Failed, QStringLiteral("flatpak nicht gefunden."));
        emit failed(QStringLiteral("flatpak nicht gefunden."));
        return;
    }

    if (m_activeProcess && m_activeProcess->state() != QProcess::NotRunning) {
        m_activeProcess->kill();
        m_activeProcess->waitForFinished(1000);
    }

    m_activeProcess = std::make_unique<QProcess>();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    m_activeProcess->setProcessEnvironment(env);

    auto *proc = m_activeProcess.get();
    // Ausgabe laufend weiterreichen: sonst sieht man bis zum Ende nichts.
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        if (proc != m_activeProcess.get()) return;
        handleOutput(proc->readAllStandardOutput(), m_stdoutBuffer, false);
    });
    connect(proc, &QProcess::readyReadStandardError, this, [this, proc]() {
        if (proc != m_activeProcess.get()) return;
        handleOutput(proc->readAllStandardError(), m_stderrBuffer, true);
    });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, targetId, successText](int exitCode, QProcess::ExitStatus exitStatus) {
        if (proc != m_activeProcess.get()) return;
        handleOutput(proc->readAllStandardOutput() + "\n", m_stdoutBuffer, false);
        handleOutput(proc->readAllStandardError() + "\n", m_stderrBuffer, true);

        if (m_cancelRequested) {
            m_cancelRequested = false;
            endOperation(OperationResult::Cancelled, QStringLiteral("Aktion abgebrochen."));
            check();
            return;
        }
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            endOperation(OperationResult::Succeeded, successText);
            emit finished(targetId);
            check();
        } else {
            QString err = m_lastError;
            if (err.isEmpty()) err = QStringLiteral("Prozess mit Fehlercode %1 beendet.").arg(exitCode);
            endOperation(OperationResult::Failed, QStringLiteral("Aktualisierung fehlgeschlagen: %1").arg(err));
            emit failed(err);
        }
    });

    proc->start(QStringLiteral("/usr/bin/flatpak"), args);
}

void FlatpakUpdates::updateApp(const QString &refOrId)
{
    // Globale Mutationssperre: Während eine Paketoperation läuft, wird keine zweite gestartet
    if (m_busy) return;
    if (m_daemonClient && m_daemonClient->isBusy()) {
        setStatus(QStringLiteral("Eine andere Paketaktion läuft bereits."));
        emit failed(QStringLiteral("Eine andere Paketaktion läuft bereits."));
        return;
    }

    beginOperation(QStringLiteral("Flatpak wird aktualisiert …"), 1);
    executeUpdateProcess({QStringLiteral("update"), QStringLiteral("--system"), QStringLiteral("-y"), QStringLiteral("--noninteractive"), refOrId}, refOrId);
}

void FlatpakUpdates::updateAll()
{
    // Globale Mutationssperre
    if (m_busy) return;
    if (m_daemonClient && m_daemonClient->isBusy()) {
        setStatus(QStringLiteral("Eine andere Paketaktion läuft bereits."));
        emit failed(QStringLiteral("Eine andere Paketaktion läuft bereits."));
        return;
    }

    beginOperation(QStringLiteral("Alle Flatpaks werden aktualisiert …"), static_cast<int>(m_items.size()));
    executeUpdateProcess({QStringLiteral("update"), QStringLiteral("--system"), QStringLiteral("-y"), QStringLiteral("--noninteractive")}, QString());
}

void FlatpakUpdates::cancel()
{
    if (m_activeProcess && m_activeProcess->state() != QProcess::NotRunning) {
        // Das Ende meldet der finished-Handler, und zwar als Abbruch statt als Fehler.
        m_cancelRequested = true;
        m_activeProcess->kill();
        return;
    }
    setBusy(false);
    setStatus(QStringLiteral("Aktion abgebrochen."));
}

} // namespace lut
