#include "ExternalChangeWatcher.h"

#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QCoreApplication>
#include <QGuiApplication>
#include "liblut/detect/DistroDetect.h"

namespace lut {

ExternalChangeWatcher::ExternalChangeWatcher(QObject *parent)
    : QObject(parent)
{
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(m_debounceIntervalMs);
    connect(&m_debounceTimer, &QTimer::timeout, this, &ExternalChangeWatcher::onDebounceTimeout);

    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &ExternalChangeWatcher::onDirectoryChanged);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &ExternalChangeWatcher::onFileChanged);

    const auto paths = defaultDatabasePaths();
    for (const QString &p : paths) {
        addWatchPath(p);
    }

    recordPathTimestamps();

    // Fenster-Fokus-Prüfung (Abschnitt 3.7 & UI-14)
    if (qApp) {
        auto *guiApp = qobject_cast<QGuiApplication *>(qApp);
        if (guiApp) {
            connect(guiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
                if (state == Qt::ApplicationActive) {
                    checkChanges();
                }
            });
        }
    }
}

QStringList ExternalChangeWatcher::defaultDatabasePaths()
{
    QStringList paths;
    DistroFamily family = DistroDetect::detectFamily();

    switch (family) {
        case DistroFamily::Arch:
            if (QFileInfo::exists(QStringLiteral("/var/lib/pacman/local"))) {
                paths.append(QStringLiteral("/var/lib/pacman/local"));
            } else if (QFileInfo::exists(QStringLiteral("/var/lib/pacman"))) {
                paths.append(QStringLiteral("/var/lib/pacman"));
            }
            break;
        case DistroFamily::Fedora:
            if (QFileInfo::exists(QStringLiteral("/var/lib/rpm"))) {
                paths.append(QStringLiteral("/var/lib/rpm"));
            } else if (QFileInfo::exists(QStringLiteral("/usr/lib/sysimage/rpm"))) {
                paths.append(QStringLiteral("/usr/lib/sysimage/rpm"));
            }
            break;
        case DistroFamily::Debian:
            if (QFileInfo::exists(QStringLiteral("/var/lib/dpkg/status"))) {
                paths.append(QStringLiteral("/var/lib/dpkg/status"));
            } else if (QFileInfo::exists(QStringLiteral("/var/lib/dpkg"))) {
                paths.append(QStringLiteral("/var/lib/dpkg"));
            }
            break;
        default:
            break;
    }

    return paths;
}

bool ExternalChangeWatcher::addWatchPath(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    if (clean.isEmpty() || !QFileInfo::exists(clean)) {
        return false;
    }

    if (!m_configuredPaths.contains(clean)) {
        m_configuredPaths.append(clean);
    }

    m_watcher.addPath(clean);
    m_lastModified[clean] = QFileInfo(clean).lastModified();
    return true;
}

bool ExternalChangeWatcher::removeWatchPath(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    m_configuredPaths.removeAll(clean);
    m_lastModified.remove(clean);
    return m_watcher.removePath(clean);
}

QStringList ExternalChangeWatcher::watchedPaths() const
{
    return m_configuredPaths;
}

void ExternalChangeWatcher::setDebounceInterval(int ms)
{
    m_debounceIntervalMs = qMax(50, ms);
    m_debounceTimer.setInterval(m_debounceIntervalMs);
}

void ExternalChangeWatcher::recordPathTimestamps()
{
    for (const QString &p : m_configuredPaths) {
        QFileInfo fi(p);
        if (fi.exists()) {
            m_lastModified[p] = fi.lastModified();
        }
    }
}

bool ExternalChangeWatcher::hasAnyPathTimestampChanged()
{
    for (const QString &p : m_configuredPaths) {
        QFileInfo fi(p);
        if (fi.exists()) {
            QDateTime currentMTime = fi.lastModified();
            if (!m_lastModified.contains(p) || m_lastModified.value(p) != currentMTime) {
                return true;
            }
        }
    }
    return false;
}

void ExternalChangeWatcher::ensurePathsRegistered()
{
    // Berücksichtigt atomar ersetzte Verzeichnisse/Dateien (Abschnitt 3.7 & P5)
    QStringList currentlyWatched = m_watcher.directories() + m_watcher.files();
    for (const QString &p : m_configuredPaths) {
        if (QFileInfo::exists(p) && !currentlyWatched.contains(p)) {
            m_watcher.addPath(p);
        }
    }
}

void ExternalChangeWatcher::onDirectoryChanged(const QString &path)
{
    Q_UNUSED(path);
    ensurePathsRegistered();
    m_debounceTimer.start();
}

void ExternalChangeWatcher::onFileChanged(const QString &path)
{
    Q_UNUSED(path);
    ensurePathsRegistered();
    m_debounceTimer.start();
}

void ExternalChangeWatcher::onDebounceTimeout()
{
    recordPathTimestamps();
    emit databaseChanged();
}

void ExternalChangeWatcher::checkChanges()
{
    ensurePathsRegistered();
    if (hasAnyPathTimestampChanged()) {
        recordPathTimestamps();
        emit databaseChanged();
    }
}

} // namespace lut
