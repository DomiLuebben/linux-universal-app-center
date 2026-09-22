#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

namespace lut {

class AppLauncher : public QObject {
    Q_OBJECT

public:
    struct LaunchableInfo {
        QString desktopId;
        QString name;
        QString exec;
        QString tryExec;
        QString icon;
        QString filePath;
        bool isValid = false;
        bool isHidden = false;
        bool noDisplay = false;
        bool isFlatpak = false;
        QString errorMessage;
    };

    explicit AppLauncher(QObject *parent = nullptr);
    ~AppLauncher() override = default;

    // Standard native application search paths
    static QStringList nativeSearchPaths();

    // Resolves a desktop ID (e.g. "org.kde.kate" or "org.kde.kate.desktop") to a native system path
    static QString resolveDesktopFilePath(const QString &desktopId, const QStringList &customPaths = {});

    // Inspects and validates a desktop file
    static LaunchableInfo inspectDesktopFile(const QString &filePath);

    // Checks whether a desktop file is a native system application (not a Flatpak / user override collision)
    static bool isNativeDesktopFile(const QString &filePath);

    // Launches an application given its desktop ID (as asynchronous KIO::ApplicationLauncherJob)
    bool launchDesktopId(const QString &desktopId, QString *errorMsg = nullptr);

    // Launches an application given its direct file path
    bool launchDesktopPath(const QString &desktopPath, QString *errorMsg = nullptr);

signals:
    void launchStarted(const QString &desktopId);
    void launchSucceeded(const QString &desktopId);
    void launchFailed(const QString &desktopId, const QString &errorMessage);
};

} // namespace lut
