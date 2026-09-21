#include "AurUpdates.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace lut {
namespace {

// Der Helfer wird als Modul installiert (Modus 644), deshalb über python3
// aufrufen statt ihn direkt auszuführen.
const QStringList &helperCandidates() {
    static const QStringList paths = {
        QStringLiteral("/usr/share/linux-package-installer/aurbuild.py"),
        QStringLiteral("/usr/local/share/linux-package-installer/aurbuild.py"),
    };
    return paths;
}

} // namespace

AurUpdates::AurUpdates(QObject *parent)
    : QAbstractListModel(parent), m_helperPath(findHelper()) {
    if (m_helperPath.isEmpty()) {
        m_statusMessage = tr("Für AUR-Aktualisierungen wird linux-package-installer benötigt.");
    }
}

QString AurUpdates::findHelper() {
    // Übersteuerung für Abnahme und Entwicklung: erlaubt den Projektstand des
    // Helfers, bevor ein neues linux-package-installer-Paket installiert ist.
    const QString override = qEnvironmentVariable("LUT_AUR_HELPER");
    if (!override.isEmpty() && QFileInfo::exists(override)) return override;

    for (const QString &path : helperCandidates()) {
        if (QFileInfo::exists(path)) return path;
    }
    return {};
}

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

QList<AurUpdates::Entry> AurUpdates::parseCheckOutput(const QByteArray &json, QString *error) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) *error = parseError.errorString();
        return {};
    }
    // Der Helfer meldet Fehler als {"error": "..."} – das darf NICHT als
    // "keine Aktualisierungen" gelesen werden.
    if (doc.isObject()) {
        if (error) *error = doc.object().value(QStringLiteral("error")).toString(
            QStringLiteral("Unerwartete Antwort des AUR-Helfers."));
        return {};
    }
    if (!doc.isArray()) {
        if (error) *error = QStringLiteral("Unerwartete Antwort des AUR-Helfers.");
        return {};
    }

    QList<Entry> result;
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject obj = value.toObject();
        Entry entry;
        entry.name = obj.value(QStringLiteral("name")).toString();
        entry.installed = obj.value(QStringLiteral("installed")).toString();
        entry.available = obj.value(QStringLiteral("available")).toString();
        if (!entry.name.isEmpty()) result.append(entry);
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

QProcess *AurUpdates::startHelper(const QStringList &arguments) {
    auto *process = new QProcess(this);
    process->setProgram(QStringLiteral("python3"));
    process->setArguments(QStringList{m_helperPath} << arguments);
    connect(process, &QProcess::finished, process, &QObject::deleteLater);
    return process;
}

void AurUpdates::check() {
    if (m_busy || m_helperPath.isEmpty()) return;
    setBusy(true);
    setStatus(tr("Prüfe AUR-Pakete …"));

    QProcess *process = startHelper({QStringLiteral("check")});
    // check gibt JSON auf die Standardausgabe; Diagnose bleibt getrennt,
    // sonst landet sie mitten im JSON.
    process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
        setBusy(false);
        const QByteArray out = process->readAllStandardOutput();

        QString error;
        QList<Entry> entries = parseCheckOutput(out, &error);
        if (!error.isEmpty() || status != QProcess::NormalExit || (exitCode != 0 && entries.isEmpty())) {
            const QString reason = error.isEmpty()
                ? QString::fromUtf8(process->readAllStandardError()).trimmed()
                : error;
            setStatus(tr("AUR-Prüfung fehlgeschlagen: %1").arg(reason));
            emit failed(reason);
            return;
        }

        beginResetModel();
        m_items = entries;
        endResetModel();
        m_hasChecked = true;
        emit countChanged();
        setStatus(m_items.isEmpty() ? tr("Keine AUR-Aktualisierungen verfügbar.")
                                    : tr("%n AUR-Aktualisierung(en) verfügbar.", "", m_items.size()));
    });
    process->start();
}

void AurUpdates::prepare(const QString &name) {
    if (m_busy || m_helperPath.isEmpty() || name.isEmpty()) return;
    setBusy(true);
    m_pendingName = name;
    setStatus(tr("Lade Schnappschuss für %1 …").arg(name));

    QProcess *process = startHelper({QStringLiteral("prepare"), name});
    process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process, name](int, QProcess::ExitStatus) {
        setBusy(false);
        const QJsonDocument doc = QJsonDocument::fromJson(process->readAllStandardOutput());
        const QJsonObject obj = doc.object();
        const QString error = obj.value(QStringLiteral("error")).toString();
        if (!doc.isObject() || !error.isEmpty()) {
            const QString reason = error.isEmpty() ? tr("Unerwartete Antwort des AUR-Helfers.") : error;
            setStatus(tr("Vorbereitung fehlgeschlagen: %1").arg(reason));
            emit failed(reason);
            return;
        }

        QStringList missing;
        for (const QJsonValue &value : obj.value(QStringLiteral("deps_repo")).toArray()) {
            missing.append(value.toString());
        }
        setStatus(tr("%1 ist vorbereitet. Bitte PKGBUILD prüfen.").arg(name));
        emit prepared(name,
                      obj.value(QStringLiteral("pkg_dir")).toString(),
                      obj.value(QStringLiteral("pkgbuild")).toString(),
                      missing);
    });
    process->start();
}

void AurUpdates::build(const QString &pkgDir) {
    if (m_busy || m_helperPath.isEmpty() || pkgDir.isEmpty()) return;
    setBusy(true);
    setStatus(tr("Baue %1 …").arg(m_pendingName));

    QProcess *process = startHelper({QStringLiteral("build"), pkgDir});
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
        while (process->canReadLine()) {
            const QString line = QString::fromUtf8(process->readLine()).trimmed();
            if (!line.isEmpty()) emit logLine(line);
        }
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
        const QByteArray rest = process->readAllStandardOutput();
        if (!rest.isEmpty()) emit logLine(QString::fromUtf8(rest).trimmed());
        setBusy(false);

        if (status != QProcess::NormalExit || exitCode != 0) {
            const QString reason = tr("Bau fehlgeschlagen (Exit-Code %1)").arg(exitCode);
            setStatus(reason);
            emit failed(reason);
            return;
        }
        setStatus(tr("%1 wurde aktualisiert.").arg(m_pendingName));
        emit finished(m_pendingName);
        check(); // Liste auffrischen, damit das Paket verschwindet
    });
    process->start();
}

void AurUpdates::cancel() {
    // makepkg und pacman laufen als eigene Prozesse weiter; hier wird nur der
    // Helfer beendet. Ein Abbruch mitten im "pacman -U" wäre gefährlich,
    // deshalb bleibt die Installationsphase bewusst unangetastet.
    for (QProcess *process : findChildren<QProcess *>()) {
        if (process->state() != QProcess::NotRunning
            && !process->arguments().contains(QStringLiteral("build"))) {
            process->terminate();
        }
    }
    setBusy(false);
    setStatus(tr("Abgebrochen."));
}

} // namespace lut
