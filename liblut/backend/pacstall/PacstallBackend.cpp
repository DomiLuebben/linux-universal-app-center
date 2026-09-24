#include "PacstallBackend.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QThread>
#include <pwd.h>
#include <unistd.h>

namespace lut {

QJsonObject PacstallUpdate::toJson() const {
    QJsonObject obj;
    obj[QStringLiteral("name")] = name;
    obj[QStringLiteral("repo")] = repo;
    obj[QStringLiteral("installed")] = installed;
    obj[QStringLiteral("available")] = available;
    return obj;
}

PacstallUpdate PacstallUpdate::fromJson(const QJsonObject &obj) {
    PacstallUpdate u;
    u.name = obj.value(QStringLiteral("name")).toString();
    u.repo = obj.value(QStringLiteral("repo")).toString();
    u.installed = obj.value(QStringLiteral("installed")).toString();
    u.available = obj.value(QStringLiteral("available")).toString();
    return u;
}

PacstallBackend::PacstallBackend(ProcessRunner runner, bool forceAvailable, QObject *parent)
    : Backend(parent), m_runner(std::move(runner)), m_forceAvailable(forceAvailable) {
}

PacstallBackend::~PacstallBackend() {
    cleanStoredPacscripts();
}

Capabilities PacstallBackend::capabilities() const {
    Capabilities caps;
    caps.install = true;
    caps.remove = false;
    caps.partialUpgrade = true;
    return caps;
}

bool PacstallBackend::isPacstallAvailable() {
    return QFileInfo::exists(QStringLiteral("/usr/bin/pacstall")) &&
           QFileInfo(QStringLiteral("/usr/bin/pacstall")).isExecutable();
}

QString PacstallBackend::defaultRepoFile() {
    return QStringLiteral("/usr/share/pacstall/repo/pacstallrepo");
}

QString PacstallBackend::defaultRepoUrl() {
    return QStringLiteral("https://raw.githubusercontent.com/pacstall/pacstall-programs/master");
}

QString PacstallBackend::pacstallRepoUrl() const {
    QString repoFile = m_repoFilePath.isEmpty() ? defaultRepoFile() : m_repoFilePath;
    QFile file(repoFile);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = QString::fromUtf8(file.readAll()).trimmed();
        if (!content.isEmpty()) {
            return content;
        }
    }
    return defaultRepoUrl();
}

QString PacstallBackend::pacscriptsDir() const {
    if (!m_pacscriptsDir.isEmpty()) {
        QDir dir(m_pacscriptsDir);
        if (!dir.exists()) {
            dir.mkpath(QStringLiteral("."));
        }
        return m_pacscriptsDir;
    }
    QString defaultDir = QStringLiteral("/var/lib/linux-update-tool/pacstall/plans");
    QDir dir(defaultDir);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
        QFile::setPermissions(defaultDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    return defaultDir;
}

QList<PacstallUpdate> PacstallBackend::parseUpdates(const QString &noColorOutput) {
    QList<PacstallUpdate> result;
    // Carriage Returns und ANSI-Escape-Sequenzen bereinigen (z. B. aus script/PTY)
    QString clean = noColorOutput;
    clean.remove(QLatin1Char('\r'));
    static const QRegularExpression ansiRegex(QStringLiteral(R"(\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~]))"));
    clean.remove(ansiRegex);

    // Format aus pacstall -Lu: \tname @ repo ( alt -> neu )
    // Kopf- und Fußzeilen (z. B. "Upgradable: N" oder Statusmeldungen) werden ignoriert.
    static const QRegularExpression regex(
        QStringLiteral(R"(^\s*([^\s@]+)\s*@\s*([^\s(]+)\s*\(\s*(\S+)\s*->\s*(\S+)\s*\))"),
        QRegularExpression::MultilineOption);

    auto matches = regex.globalMatch(clean);
    while (matches.hasNext()) {
        auto match = matches.next();
        PacstallUpdate u;
        u.name = match.captured(1).trimmed();
        u.repo = match.captured(2).trimmed();
        u.installed = match.captured(3).trimmed();
        u.available = match.captured(4).trimmed();
        if (!u.name.isEmpty()) {
            result.append(u);
        }
    }
    return result;
}

QString PacstallBackend::updatesToJson(const QList<PacstallUpdate> &updates) {
    QJsonArray arr;
    for (const auto &u : updates) {
        arr.append(u.toJson());
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QList<PacstallUpdate> PacstallBackend::jsonToUpdates(const QString &json) {
    QList<PacstallUpdate> result;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (doc.isArray()) {
        for (const auto &val : doc.array()) {
            if (val.isObject()) {
                result.append(PacstallUpdate::fromJson(val.toObject()));
            }
        }
    }
    return result;
}

QString PacstallBackend::computePlanRevision(const QMap<QString, QByteArray> &pacscripts) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (auto it = pacscripts.constBegin(); it != pacscripts.constEnd(); ++it) {
        hash.addData(it.key().toUtf8());
        hash.addData(":");
        hash.addData(it.value());
        hash.addData("\n");
    }
    return QString::fromUtf8(hash.result().toHex());
}

QByteArray PacstallBackend::downloadPacscript(const QUrl &url, QString *error) {
    if (url.scheme().toLower() != QLatin1String("https")) {
        if (error) *error = QStringLiteral("Nur HTTPS-Quellen sind zulässig: %1").arg(url.toString());
        return {};
    }

    if (m_downloader) {
        return m_downloader(url, error);
    }

    // HTTPS-Download mit 1 MiB Größenlimit
    constexpr qint64 maxSizeBytes = 1024 * 1024;
    QNetworkAccessManager nam;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    std::unique_ptr<QNetworkReply> reply(nam.get(request));
    QEventLoop loop;
    QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        if (error) *error = QStringLiteral("Netzwerkfehler beim Laden von %1: %2").arg(url.toString(), reply->errorString());
        return {};
    }

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (statusCode != 200) {
        if (error) *error = QStringLiteral("HTTP %1 beim Laden von %2").arg(statusCode).arg(url.toString());
        return {};
    }

    QByteArray data = reply->readAll();
    if (data.size() > maxSizeBytes) {
        if (error) *error = QStringLiteral("Pacscript überschreitet Größenlimit von 1 MiB (%1 Bytes)").arg(data.size());
        return {};
    }

    return data;
}

int PacstallBackend::runCommand(const QString &program, const QStringList &args, const QProcessEnvironment &env,
                               QString &stdoutOut, QString &stderrOut) {
    // Sicherheitsprüfung: niemals -Ns oder -Nc
    for (const auto &arg : args) {
        if (arg == QLatin1String("-Ns") || arg == QLatin1String("-Nc") ||
            arg.contains(QLatin1String("-Ns")) || arg.contains(QLatin1String("-Nc"))) {
            stderrOut = QStringLiteral("Sicherheitsrichtlinie verletzt: Flags -Ns und -Nc sind nicht zulässig.");
            return -1;
        }
    }

    if (m_runner) {
        int rc = m_runner(program, args, env, stdoutOut, stderrOut);
        if (stdoutOut.contains(QLatin1String("already running another instance")) ||
            stderrOut.contains(QLatin1String("already running another instance"))) {
            stderrOut = QStringLiteral("Pacstall läuft bereits in einem anderen Fenster oder Terminal.");
            return -2;
        }
        return rc;
    }

    QProcess process;
    process.setProcessEnvironment(env);
    process.setStandardInputFile(QStringLiteral("/dev/null"));

    QElapsedTimer timer;
    timer.start();

    process.start(program, args);
    if (!process.waitForStarted(5000)) {
        stderrOut = QStringLiteral("Konnte %1 nicht starten: %2").arg(program, process.errorString());
        return -1;
    }

    while (!process.waitForFinished(500)) {
        if (m_cancelled) {
            process.terminate();
            if (!process.waitForFinished(1000)) process.kill();
            stderrOut = QStringLiteral("Abgebrochen");
            return -3;
        }
        QString currentOutput = QString::fromUtf8(process.readAllStandardOutput());
        QString currentError = QString::fromUtf8(process.readAllStandardError());
        stdoutOut.append(currentOutput);
        stderrOut.append(currentError);
        // Laufend ins Live-Protokoll: ein Bau dauert, und sonst stünde dort minutenlang nichts.
        for (const QString &chunk : {currentOutput, currentError}) {
            QString text = chunk;
            text.remove(QLatin1Char('\r'));
            static const QRegularExpression ansi(QStringLiteral(R"(\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~]))"));
            text.remove(ansi);
            for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                if (!line.trimmed().isEmpty()) emit eventEmitted(LogLine{LogLevel::Info, QStringLiteral("pacstall"), line.trimmed()});
            }
        }

        if ((stdoutOut.contains(QLatin1String("already running another instance")) ||
             stderrOut.contains(QLatin1String("already running another instance"))) &&
            timer.elapsed() >= m_lockTimeoutMs) {
            process.terminate();
            if (!process.waitForFinished(1000)) process.kill();
            stderrOut = QStringLiteral("Pacstall läuft bereits in einem anderen Fenster oder Terminal.");
            return -2;
        }
    }

    stdoutOut.append(QString::fromUtf8(process.readAllStandardOutput()));
    stderrOut.append(QString::fromUtf8(process.readAllStandardError()));

    if ((stdoutOut.contains(QLatin1String("already running another instance")) ||
         stderrOut.contains(QLatin1String("already running another instance"))) &&
        timer.elapsed() >= m_lockTimeoutMs) {
        stderrOut = QStringLiteral("Pacstall läuft bereits in einem anderen Fenster oder Terminal.");
        return -2;
    }

    return process.exitCode();
}

QList<PacstallUpdate> PacstallBackend::checkUpdates(QString *error) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString sudoUser = m_callerUsername;
    if (sudoUser.isEmpty()) sudoUser = qEnvironmentVariable("USER");
    if (!sudoUser.isEmpty()) env.insert(QStringLiteral("SUDO_USER"), sudoUser);
    env.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    env.insert(QStringLiteral("DISABLE_PROMPTS"), QStringLiteral("yes"));
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    env.insert(QStringLiteral("TERM"), QStringLiteral("xterm"));

    QString stdoutOut;
    QString stderrOut;
    int rc = -1;
    // pacstall -Lu nutzt intern stty -g / tput civis; in nicht-interaktiven
    // Daemon- und Testumgebungen stellt /usr/bin/script ein Pseudo-Terminal bereit.
    if (!m_runner && QFile::exists(QStringLiteral("/usr/bin/script"))) {
        rc = runCommand(QStringLiteral("/usr/bin/script"),
                        {QStringLiteral("-q"), QStringLiteral("-e"), QStringLiteral("-c"),
                         QStringLiteral("TERM=xterm /usr/bin/pacstall -Lu"), QStringLiteral("/dev/null")},
                        env, stdoutOut, stderrOut);
    } else {
        rc = runCommand(QStringLiteral("/usr/bin/pacstall"), {QStringLiteral("-Lu")}, env, stdoutOut, stderrOut);
    }
    QList<PacstallUpdate> updates = parseUpdates(stdoutOut);

    // Eine gescheiterte Abfrage darf nicht als "alles aktuell" enden. Regulär
    // endet -Lu mit "Nothing to upgrade" oder "Upgradable: N" (upgrade.sh);
    // ohne Netz bricht es mit Fehler ab.
    QString clean = stdoutOut + QLatin1Char('\n') + stderrOut;
    clean.remove(QLatin1Char('\r'));
    static const QRegularExpression ansi(QStringLiteral(R"(\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~]))"));
    clean.remove(ansi);
    static const QRegularExpression upgradable(QStringLiteral(R"(Upgradable:\s*(\d+))"));
    const auto counted = upgradable.match(clean);
    QString failure;
    if (rc != 0) {
        const QStringList lines = clean.trimmed().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        failure = QStringLiteral("pacstall -Lu ist fehlgeschlagen: %1").arg(lines.isEmpty() ? QString::number(rc) : lines.last().trimmed());
    } else if (!counted.hasMatch() && !clean.contains(QLatin1String("Nothing to upgrade"))) {
        failure = QStringLiteral("Unerwartete Ausgabe von pacstall -Lu.");
    } else if (counted.hasMatch() && counted.captured(1).toInt() != updates.size()) {
        failure = QStringLiteral("pacstall meldet %1 Aktualisierungen, erkannt wurden %2.")
                      .arg(counted.captured(1)).arg(updates.size());
    }
    if (!failure.isEmpty()) {
        emit eventEmitted(LogLine{LogLevel::Warning, QStringLiteral("pacstall"), failure});
        if (error) *error = failure;
    }
    m_knownUpdates.clear();
    for (const auto &u : updates) {
        m_knownUpdates[u.name] = u;
    }
    return updates;
}

void PacstallBackend::planPacstallUpgrade(const QStringList &names, quint32 callerUid) {
    m_cancelled = false;
    m_savedPacscripts.clear();
    m_plannedPackages.clear();
    m_currentPlanRevision.clear();

    // Caller-Benutzer auflösen
    QString callerUser = m_callerUsername;
    if (callerUser.isEmpty() && callerUid > 0 && callerUid != std::numeric_limits<quint32>::max()) {
        struct passwd *pw = ::getpwuid(callerUid);
        if (pw && pw->pw_name) {
            callerUser = QString::fromUtf8(pw->pw_name);
        }
    }
    if (callerUser.isEmpty()) {
        callerUser = qEnvironmentVariable("USER");
    }
    m_activeCallerUser = callerUser;

    emit eventEmitted(PhaseChanged{Phase::Resolve, tr("Pacscripts werden heruntergeladen …"), false, true});

    QString repoUrl = pacstallRepoUrl();
    if (repoUrl.endsWith(QLatin1Char('/'))) {
        repoUrl.chop(1);
    }

    QString dirPath = pacscriptsDir();
    QDir targetDir(dirPath);

    for (const auto &name : names) {
        if (m_cancelled) {
            emit eventEmitted(PhaseChanged{Phase::Cancelled, tr("Abgebrochen"), false, false});
            emit eventEmitted(TransactionDone{Result::Cancelled, tr("Vorgang abgebrochen."), false, {}, 0});
            return;
        }

        QString urlStr = QStringLiteral("%1/packages/%2/%2.pacscript").arg(repoUrl, name);
        QString downloadError;
        QByteArray content = downloadPacscript(QUrl(urlStr), &downloadError);
        if (content.isEmpty() || !downloadError.isEmpty()) {
            emit eventEmitted(PhaseChanged{Phase::Failed, tr("Download fehlgeschlagen"), false, false});
            emit eventEmitted(TransactionDone{Result::Failed,
                QStringLiteral("Pacscript für %1 konnte nicht geladen werden: %2").arg(name, downloadError), false, {}, 0});
            cleanStoredPacscripts();
            return;
        }

        // Root-eigene Datei mit 0600 ablegen
        QString filePath = targetDir.filePath(QStringLiteral("%1.pacscript").arg(name));
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit eventEmitted(PhaseChanged{Phase::Failed, tr("Speichern fehlgeschlagen"), false, false});
            emit eventEmitted(TransactionDone{Result::Failed,
                QStringLiteral("Konnte Pacscript-Datei nicht anlegen: %1").arg(filePath), false, {}, 0});
            cleanStoredPacscripts();
            return;
        }
        file.write(content);
        file.close();
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);

        m_savedPacscripts[name] = content;
        m_plannedPackages.append(name);
    }

    m_currentPlanRevision = computePlanRevision(m_savedPacscripts);

    QList<PackageOp> ops;
    for (const auto &name : m_plannedPackages) {
        PackageOp op;
        op.id = name;
        op.name = name;
        op.kind = PackageOp::Kind::Upgrade;
        if (m_knownUpdates.contains(name)) {
            op.version = m_knownUpdates[name].installed;
            op.newVersion = m_knownUpdates[name].available;
            op.repo = m_knownUpdates[name].repo;
        }
        // Pacscript-Inhalt für die Prüfung in der GUI
        op.summary = QString::fromUtf8(m_savedPacscripts.value(name));
        ops.append(op);
    }

    emit eventEmitted(PlanReady{ops, 0, 0, {}, m_currentPlanRevision});
    emit eventEmitted(PhaseChanged{Phase::Idle, tr("Bereit zur Prüfung"), true, false});
}

void PacstallBackend::commitPlan(const QString &planRevision) {
    if (m_plannedPackages.isEmpty() || m_savedPacscripts.isEmpty()) {
        emit eventEmitted(PhaseChanged{Phase::Failed, tr("Fehlgeschlagen"), false, false});
        emit eventEmitted(TransactionDone{Result::Failed, tr("Kein aktiver Plan vorhanden."), false, {}, 0});
        return;
    }

    // Revisionsprüfung: Geänderter Inhalt -> Commit abgelehnt
    QDir targetDir(pacscriptsDir());
    QMap<QString, QByteArray> currentOnDisk;
    for (const auto &name : m_plannedPackages) {
        QString filePath = targetDir.filePath(QStringLiteral("%1.pacscript").arg(name));
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            emit eventEmitted(PhaseChanged{Phase::Failed, tr("Plan ungültig"), false, false});
            emit eventEmitted(TransactionDone{Result::Failed,
                QStringLiteral("Pacscript-Datei fehlt: %1").arg(filePath), false, {}, 0});
            cleanStoredPacscripts();
            return;
        }
        currentOnDisk[name] = file.readAll();
    }

    QString diskRevision = computePlanRevision(currentOnDisk);
    if (diskRevision != planRevision || diskRevision != m_currentPlanRevision) {
        emit eventEmitted(PhaseChanged{Phase::Failed, tr("Planrevision stimmt nicht überein"), false, false});
        emit eventEmitted(TransactionDone{Result::Failed,
            QStringLiteral("Planrevision stimmt nicht überein (Plan geändert). Bitte neu bestätigen."), false, {}, 0});
        cleanStoredPacscripts();
        return;
    }

    emit eventEmitted(PhaseChanged{Phase::Commit, tr("Pacstall-Aktualisierung wird ausgeführt"), false, true});

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!m_activeCallerUser.isEmpty()) {
        env.insert(QStringLiteral("SUDO_USER"), m_activeCallerUser);
    }
    env.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    env.insert(QStringLiteral("DISABLE_PROMPTS"), QStringLiteral("yes"));
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    env.insert(QStringLiteral("TERM"), QStringLiteral("xterm"));

    int total = m_plannedPackages.size();
    for (int i = 0; i < total; ++i) {
        if (m_cancelled) {
            emit eventEmitted(PhaseChanged{Phase::Cancelled, tr("Abgebrochen"), false, false});
            emit eventEmitted(TransactionDone{Result::Cancelled, tr("Vorgang abgebrochen."), false, {}, 0});
            cleanStoredPacscripts();
            return;
        }

        const QString &name = m_plannedPackages.at(i);
        QString filePath = targetDir.filePath(QStringLiteral("%1.pacscript").arg(name));

        // Fortschritt: je Paket ein Schritt („Paket 2 von 5“)
        QString stepLabel = QStringLiteral("Paket %1 von %2").arg(i + 1).arg(total);
        emit eventEmitted(PhaseChanged{Phase::Commit, stepLabel, false, true});
        emit eventEmitted(ItemStarted{name, PackageOp::Kind::Upgrade, 0});

        QString stdoutOut;
        QString stderrOut;
        // Ausgeführt wird genau die geprüfte Datei: pacstall -P -I <root-eigene Kopie>.pacscript
        int rc = runCommand(QStringLiteral("/usr/bin/pacstall"),
                            {QStringLiteral("-P"), QStringLiteral("-I"), filePath},
                            env, stdoutOut, stderrOut);

        if (rc != 0) {
            emit eventEmitted(ItemFinished{name, false, stderrOut});
            emit eventEmitted(PhaseChanged{Phase::Failed, tr("Fehlgeschlagen"), false, false});
            // Pacstall meldet Fehler auf stdout ("[!] ERROR: 'MIT' is not a valid license").
            QString combined = stdoutOut + QLatin1Char('\n') + stderrOut;
            static const QRegularExpression ansi(QStringLiteral(R"(\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~]))"));
            combined.remove(ansi);
            QString reason;
            for (const QString &line : combined.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                if (line.contains(QLatin1String("ERROR"), Qt::CaseInsensitive) || line.contains(QLatin1String("Fehler"))) reason = line.trimmed();
            }
            if (reason.isEmpty()) {
                const QStringList lines = combined.trimmed().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                if (!lines.isEmpty()) reason = lines.last().trimmed();
            }
            QString msg = reason.isEmpty()
                ? QStringLiteral("Installation von %1 fehlgeschlagen.").arg(name)
                : QStringLiteral("Installation von %1 fehlgeschlagen: %2").arg(name, reason);
            emit eventEmitted(TransactionDone{Result::Failed, msg, false, {}, 0});
            cleanStoredPacscripts();
            return;
        }

        emit eventEmitted(ItemFinished{name, true, QString()});
    }

    cleanStoredPacscripts();
    emit eventEmitted(PhaseChanged{Phase::Finished, tr("Abgeschlossen"), false, false});
    emit eventEmitted(TransactionDone{Result::Success, tr("Pacstall-Aktualisierung erfolgreich abgeschlossen."), false, {}, 0});
}

void PacstallBackend::cleanStoredPacscripts() {
    if (!m_pacscriptsDir.isEmpty() || QFile::exists(pacscriptsDir())) {
        QDir dir(pacscriptsDir());
        for (const auto &name : m_plannedPackages) {
            QString filePath = dir.filePath(QStringLiteral("%1.pacscript").arg(name));
            QFile::remove(filePath);
        }
    }
    m_savedPacscripts.clear();
    m_plannedPackages.clear();
    m_currentPlanRevision.clear();
}

void PacstallBackend::refreshMetadata() {
    checkUpdates();
}

void PacstallBackend::planUpgradeAll(const UpgradeOptions &options) {
    Q_UNUSED(options);
    auto updates = checkUpdates();
    QStringList names;
    for (const auto &u : updates) {
        names.append(u.name);
    }
    planPacstallUpgrade(names, 0);
}

void PacstallBackend::planInstall(const QStringList &names) {
    planPacstallUpgrade(names, 0);
}

void PacstallBackend::planRemove(const QStringList &names) {
    Q_UNUSED(names);
    emit eventEmitted(TransactionDone{Result::Failed, tr("Entfernen von Pacstall-Paketen wird im Store nicht unterstützt."), false, {}, 0});
}

void PacstallBackend::commit() {
    commitPlan(m_currentPlanRevision);
}

void PacstallBackend::discardPlan() {
    cleanStoredPacscripts();
    emit eventEmitted(PhaseChanged{Phase::Idle, tr("Plan verworfen"), false, false});
    emit eventEmitted(TransactionDone{Result::Cancelled, tr("Plan verworfen."), false, {}, 0});
}

void PacstallBackend::cancel() {
    m_cancelled = true;
}

void PacstallBackend::answerQuestion(const QString &id, const QJsonObject &answer) {
    Q_UNUSED(id);
    Q_UNUSED(answer);
}

QList<PackageOp> PacstallBackend::availableUpdates() {
    auto updates = checkUpdates();
    QList<PackageOp> ops;
    for (const auto &u : updates) {
        PackageOp op;
        op.id = u.name;
        op.name = u.name;
        op.version = u.installed;
        op.newVersion = u.available;
        op.repo = u.repo;
        op.kind = PackageOp::Kind::Upgrade;
        ops.append(op);
    }
    return ops;
}

QList<InstalledPackage> PacstallBackend::installedPackages(const QString &query) {
    Q_UNUSED(query);
    return {};
}

QList<ChangelogEntry> PacstallBackend::changelog(const QString &pkgId) {
    Q_UNUSED(pkgId);
    return {};
}

QList<HistoryEntry> PacstallBackend::history(int limit) {
    Q_UNUSED(limit);
    return {};
}

} // namespace lut
