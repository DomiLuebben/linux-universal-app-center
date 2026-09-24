#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

namespace lut {

/// Zentrale Verwaltung persistenter Benutzereinstellungen des Linux Universal App Center.
/// Speichert Einstellungen in settings.conf unter AppConfigLocation.
class AppSettings : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool aurEnabled READ aurEnabled WRITE setAurEnabled NOTIFY aurEnabledChanged)
    Q_PROPERTY(bool pacstallEnabled READ pacstallEnabled WRITE setPacstallEnabled NOTIFY pacstallEnabledChanged)

public:
    explicit AppSettings(const QString &customFilePath = QString(), QObject *parent = nullptr);
    ~AppSettings() override = default;

    bool aurEnabled() const;
    void setAurEnabled(bool enabled);

    bool pacstallEnabled() const;
    void setPacstallEnabled(bool enabled);

    QString filePath() const;

signals:
    void aurEnabledChanged(bool enabled);
    void pacstallEnabledChanged(bool enabled);

private:
    QString m_filePath;
    mutable QSettings m_settings;
};

} // namespace lut
