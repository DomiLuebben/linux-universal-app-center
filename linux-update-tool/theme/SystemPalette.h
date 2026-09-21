#pragma once

#include <QObject>
#include <QColor>
#include <QFileSystemWatcher>
#include "Contrast.h"

namespace lut {

class SystemPalette : public QObject {
    Q_OBJECT

    // Farb-Token (alle zur Laufzeit aus dem System abgeleitet)
    Q_PROPERTY(QColor bg READ bg NOTIFY themeChanged)
    Q_PROPERTY(QColor surface READ surface NOTIFY themeChanged)
    Q_PROPERTY(QColor surfaceRaised READ surfaceRaised NOTIFY themeChanged)
    Q_PROPERTY(QColor surfaceSunken READ surfaceSunken NOTIFY themeChanged)
    Q_PROPERTY(QColor surfaceAlt READ surfaceAlt NOTIFY themeChanged)
    Q_PROPERTY(QColor text READ text NOTIFY themeChanged)
    Q_PROPERTY(QColor textMuted READ textMuted NOTIFY themeChanged)
    Q_PROPERTY(QColor textOnSunken READ textOnSunken NOTIFY themeChanged)
    Q_PROPERTY(QColor accent READ accent NOTIFY themeChanged)
    Q_PROPERTY(QColor onAccent READ onAccent NOTIFY themeChanged)
    Q_PROPERTY(QColor positive READ positive NOTIFY themeChanged)
    Q_PROPERTY(QColor negative READ negative NOTIFY themeChanged)
    Q_PROPERTY(QColor neutral READ neutral NOTIFY themeChanged)
    Q_PROPERTY(QColor separator READ separator NOTIFY themeChanged)
    Q_PROPERTY(bool isDark READ isDark NOTIFY themeChanged)
    Q_PROPERTY(double motionScale READ motionScale NOTIFY themeChanged)

    // Abstände
    Q_PROPERTY(int s1 READ s1 CONSTANT)
    Q_PROPERTY(int s2 READ s2 CONSTANT)
    Q_PROPERTY(int s3 READ s3 CONSTANT)
    Q_PROPERTY(int s4 READ s4 CONSTANT)
    Q_PROPERTY(int s5 READ s5 CONSTANT)
    Q_PROPERTY(int s6 READ s6 CONSTANT)
    Q_PROPERTY(int s7 READ s7 CONSTANT)

    // Radien
    Q_PROPERTY(int radiusCard READ radiusCard CONSTANT)
    Q_PROPERTY(int radiusControl READ radiusControl CONSTANT)
    Q_PROPERTY(int radiusChip READ radiusChip CONSTANT)

    // Animationsdauer (skaliert mit motionScale)
    Q_PROPERTY(int durationFast READ durationFast NOTIFY themeChanged)
    Q_PROPERTY(int durationBase READ durationBase NOTIFY themeChanged)
    Q_PROPERTY(int durationSlow READ durationSlow NOTIFY themeChanged)

public:
    static SystemPalette *instance();
    explicit SystemPalette(QObject *parent = nullptr);

    QColor bg() const { return m_bg; }
    QColor surface() const { return m_surface; }
    QColor surfaceRaised() const { return m_surfaceRaised; }
    QColor surfaceSunken() const { return m_surfaceSunken; }
    QColor surfaceAlt() const { return m_surfaceAlt; }
    QColor text() const { return m_text; }
    QColor textMuted() const { return m_textMuted; }
    QColor textOnSunken() const { return m_textOnSunken; }
    QColor accent() const { return m_accent; }
    QColor onAccent() const { return m_onAccent; }
    QColor positive() const { return m_positive; }
    QColor negative() const { return m_negative; }
    QColor neutral() const { return m_neutral; }
    QColor separator() const { return m_separator; }
    bool isDark() const { return m_isDark; }
    double motionScale() const { return m_motionScale; }

    int s1() const { return 4; }
    int s2() const { return 8; }
    int s3() const { return 12; }
    int s4() const { return 16; }
    int s5() const { return 24; }
    int s6() const { return 32; }
    int s7() const { return 48; }

    int radiusCard() const { return 14; }
    int radiusControl() const { return 10; }
    int radiusChip() const { return 8; }

    int durationFast() const { return static_cast<int>(150 * m_motionScale); }
    int durationBase() const { return static_cast<int>(250 * m_motionScale); }
    int durationSlow() const { return static_cast<int>(400 * m_motionScale); }

public slots:
    void reloadTheme();

signals:
    void themeChanged();

private slots:
    void onKdeGlobalsChanged(const QString &path);

private:
    void loadFromKdeGlobals();
    void loadFromPortal();
    void loadFromPlatformFallback();
    static QColor parseRgb(const QString &str, const QColor &fallback);

    QFileSystemWatcher m_watcher;
    QString m_kdeGlobalsPath;

    QColor m_bg;
    QColor m_surface;
    QColor m_surfaceRaised;
    QColor m_surfaceSunken;
    QColor m_surfaceAlt;
    QColor m_text;
    QColor m_textMuted;
    QColor m_textOnSunken;
    QColor m_accent;
    QColor m_onAccent;
    QColor m_positive;
    QColor m_negative;
    QColor m_neutral;
    QColor m_separator;
    bool m_isDark = true;
    double m_motionScale = 1.0;
};

} // namespace lut
