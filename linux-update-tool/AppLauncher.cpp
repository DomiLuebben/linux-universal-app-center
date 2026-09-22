#include "AppLauncher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QDebug>

#include <KService>
#include <kio/applicationlauncherjob.h>

namespace lut {

AppLauncher::AppLauncher(QObject *parent)
    : QObject(parent)
{
}

QStringList AppLauncher::nativeSearchPaths()
{
    return {
        QStringLiteral("/usr/share/applications/"),
        QStringLiteral("/usr/local/share/applications/")
    };
}

bool AppLauncher::isNativeDesktopFile(const QString &filePath)
{
    const QString cleanPath = QDir::cleanPath(filePath);

    // Flatpak-Exporte und Flatpak-Pfade strikt ausschließen (Abschnitt 3.7 & 9)
    if (cleanPath.contains(QLatin1String("/flatpak/"), Qt::CaseInsensitive)) {
        return false;
    }
    if (cleanPath.contains(QLatin1String("/var/lib/flatpak/"))) {
        return false;
    }
    if (cleanPath.contains(QLatin1String("/.local/share/flatpak/"))) {
        return false;
    }

    QFile f(cleanPath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!f.atEnd()) {
            QByteArray line = f.readLine();
            if (line.startsWith("X-Flatpak") || line.contains("flatpak run")) {
                return false;
            }
        }
    }

    return true;
}

QString AppLauncher::resolveDesktopFilePath(const QString &desktopId, const QStringList &customPaths)
{
    if (desktopId.isEmpty()) {
        return {};
    }

    QString id = desktopId.trimmed();
    if (!id.endsWith(QStringLiteral(".desktop"), Qt::CaseInsensitive)) {
        id += QStringLiteral(".desktop");
    }

    // Falls absoluter Pfad übergeben wurde
    if (QFileInfo(id).isAbsolute()) {
        if (QFileInfo::exists(id) && isNativeDesktopFile(id)) {
            return id;
        }
        return {};
    }

    // 1. Zuerst spezifische customPaths prüfen (z.B. für Tests oder Fixtures)
    for (const QString &dirPath : customPaths) {
        QString fullPath = QDir(dirPath).filePath(id);
        if (QFileInfo::exists(fullPath) && isNativeDesktopFile(fullPath)) {
            return fullPath;
        }
    }

    // 2. Standard-Systempfade für native Pakete prüfen
    for (const QString &dirPath : nativeSearchPaths()) {
        QString fullPath = QDir(dirPath).filePath(id);
        if (QFileInfo::exists(fullPath) && isNativeDesktopFile(fullPath)) {
            return fullPath;
        }
    }

    return {};
}

AppLauncher::LaunchableInfo AppLauncher::inspectDesktopFile(const QString &filePath)
{
    LaunchableInfo info;
    info.filePath = filePath;
    info.desktopId = QFileInfo(filePath).fileName();

    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) {
        info.isValid = false;
        info.errorMessage = QStringLiteral("Desktop-Datei existiert nicht: %1").arg(filePath);
        return info;
    }

    if (!isNativeDesktopFile(filePath)) {
        info.isValid = false;
        info.isFlatpak = true;
        info.errorMessage = QStringLiteral("Flatpak-Desktop-Eintrag zurückgewiesen; nur native Pakete erlaubt: %1").arg(filePath);
        return info;
    }

    QSettings settings(filePath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Desktop Entry"));

    const QString type = settings.value(QStringLiteral("Type")).toString();
    if (!type.isEmpty() && type.compare(QLatin1String("Application"), Qt::CaseInsensitive) != 0) {
        info.isValid = false;
        info.errorMessage = QStringLiteral("Desktop-Eintrag ist keine Anwendung (Type=%1)").arg(type);
        return info;
    }

    info.name = settings.value(QStringLiteral("Name")).toString();
    info.exec = settings.value(QStringLiteral("Exec")).toString();
    info.tryExec = settings.value(QStringLiteral("TryExec")).toString();
    info.icon = settings.value(QStringLiteral("Icon")).toString();
    info.noDisplay = settings.value(QStringLiteral("NoDisplay"), false).toBool();
    info.isHidden = settings.value(QStringLiteral("Hidden"), false).toBool();

    if (info.exec.isEmpty()) {
        info.isValid = false;
        info.errorMessage = QStringLiteral("Desktop-Eintrag enthält keinen 'Exec='-Befehl.");
        return info;
    }

    // Validierung von TryExec (Abschnitt 9)
    if (!info.tryExec.trimmed().isEmpty()) {
        const QString tryExecBinary = info.tryExec.trimmed();
        bool found = false;
        if (QFileInfo(tryExecBinary).isAbsolute()) {
            found = QFileInfo::exists(tryExecBinary) && QFileInfo(tryExecBinary).isExecutable();
        } else {
            found = !QStandardPaths::findExecutable(tryExecBinary).isEmpty();
        }

        if (!found) {
            info.isValid = false;
            info.errorMessage = QStringLiteral("Ausführbare Datei '%1' (TryExec) nicht im Systempfad gefunden.").arg(tryExecBinary);
            return info;
        }
    }

    info.isValid = true;
    return info;
}

bool AppLauncher::launchDesktopId(const QString &desktopId, QString *errorMsg)
{
    QString filePath = resolveDesktopFilePath(desktopId);
    if (filePath.isEmpty()) {
        const QString msg = QStringLiteral("Nativer Desktop-Eintrag für '%1' nicht gefunden.").arg(desktopId);
        if (errorMsg) *errorMsg = msg;
        emit launchFailed(desktopId, msg);
        return false;
    }

    return launchDesktopPath(filePath, errorMsg);
}

bool AppLauncher::launchDesktopPath(const QString &desktopPath, QString *errorMsg)
{
    LaunchableInfo info = inspectDesktopFile(desktopPath);
    if (!info.isValid) {
        if (errorMsg) *errorMsg = info.errorMessage;
        emit launchFailed(info.desktopId, info.errorMessage);
        return false;
    }

    if (info.isHidden) {
        const QString msg = QStringLiteral("Desktop-Eintrag '%1' ist als versteckt ('Hidden=true') markiert.").arg(info.desktopId);
        if (errorMsg) *errorMsg = msg;
        emit launchFailed(info.desktopId, msg);
        return false;
    }

    // Start über KDE-Frameworks (KService + KIO::ApplicationLauncherJob)
    KService::Ptr service(new KService(desktopPath));
    if (!service || !service->isValid()) {
        service = KService::serviceByDesktopPath(desktopPath);
    }

    if (!service || !service->isValid()) {
        const QString msg = QStringLiteral("KService konnte Desktop-Eintrag '%1' nicht initialisieren.").arg(desktopPath);
        if (errorMsg) *errorMsg = msg;
        emit launchFailed(info.desktopId, msg);
        return false;
    }

    auto *job = new KIO::ApplicationLauncherJob(service, this);
    const QString dId = info.desktopId;

    connect(job, &KJob::result, this, [this, dId](KJob *finishedJob) {
        if (finishedJob->error() != 0) {
            emit launchFailed(dId, finishedJob->errorString());
        } else {
            emit launchSucceeded(dId);
        }
    });

    job->start();
    emit launchStarted(dId);
    return true;
}

} // namespace lut
