#include "AppSettings.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace lut {

namespace {

QString resolveDefaultPath() {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    return configDir + QStringLiteral("/settings.conf");
}

} // namespace

AppSettings::AppSettings(const QString &customFilePath, QObject *parent)
    : QObject(parent)
    , m_filePath(customFilePath.isEmpty() ? resolveDefaultPath() : customFilePath)
    , m_settings(m_filePath, QSettings::IniFormat) {
    if (!m_filePath.isEmpty()) {
        QFileInfo fi(m_filePath);
        if (!fi.dir().exists()) {
            fi.dir().mkpath(QStringLiteral("."));
        }
    }
}

bool AppSettings::aurEnabled() const {
    return m_settings.value(QStringLiteral("thirdParty/aurEnabled"), false).toBool();
}

void AppSettings::setAurEnabled(bool enabled) {
    if (aurEnabled() == enabled) return;
    m_settings.setValue(QStringLiteral("thirdParty/aurEnabled"), enabled);
    m_settings.sync();
    emit aurEnabledChanged(enabled);
}

bool AppSettings::pacstallEnabled() const {
    return m_settings.value(QStringLiteral("thirdParty/pacstallEnabled"), false).toBool();
}

void AppSettings::setPacstallEnabled(bool enabled) {
    if (pacstallEnabled() == enabled) return;
    m_settings.setValue(QStringLiteral("thirdParty/pacstallEnabled"), enabled);
    m_settings.sync();
    emit pacstallEnabledChanged(enabled);
}

QString AppSettings::filePath() const {
    return m_filePath;
}

} // namespace lut
