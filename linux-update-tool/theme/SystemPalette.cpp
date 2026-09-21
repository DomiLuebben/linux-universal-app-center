#include "SystemPalette.h"
#include <QSettings>
#include <QDir>
#include <QGuiApplication>
#include <QPalette>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDebug>

namespace lut {

SystemPalette *SystemPalette::instance() {
    static SystemPalette s_instance;
    return &s_instance;
}

SystemPalette::SystemPalette(QObject *parent)
    : QObject(parent) {
    m_kdeGlobalsPath = QDir::homePath() + QStringLiteral("/.config/kdeglobals");
    reloadTheme();

    if (QFile::exists(m_kdeGlobalsPath)) {
        m_watcher.addPath(m_kdeGlobalsPath);
        connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &SystemPalette::onKdeGlobalsChanged);
    }
}

void SystemPalette::onKdeGlobalsChanged(const QString &path) {
    // KDE schreibt kdeglobals atomar per Rename -> neu registrieren
    if (!m_watcher.files().contains(path) && QFile::exists(path)) {
        m_watcher.addPath(path);
    }
    reloadTheme();
}

QColor SystemPalette::parseRgb(const QString &str, const QColor &fallback) {
    if (str.isEmpty()) return fallback;
    QStringList parts = str.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.size() >= 3) {
        bool okR = false, okG = false, okB = false;
        int r = parts[0].trimmed().toInt(&okR);
        int g = parts[1].trimmed().toInt(&okG);
        int b = parts[2].trimmed().toInt(&okB);
        if (okR && okG && okB) {
            return QColor(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
        }
    }
    return fallback;
}

void SystemPalette::reloadTheme() {
    if (QFile::exists(m_kdeGlobalsPath)) {
        loadFromKdeGlobals();
    } else {
        loadFromPortal();
    }

    m_isDark = relativeLuminance(m_bg) < 0.5;

    // Tiefenstaffelung aus m_bg
    m_surface = elevate(m_bg, 1);
    m_surfaceRaised = elevate(m_bg, 2);

    // Textfarben mit Kontrastabsicherung
    m_text = ensureContrast(m_text, m_surface, 4.5);
    m_textMuted = ensureContrast(m_textMuted, m_surface, 3.0);
    m_textOnSunken = ensureContrast(m_textOnSunken, m_surfaceSunken, 4.5);

    // onAccent: bewusst per pickOn() ermittelt (schwarz oder weiß je nach Luminanz)
    m_onAccent = pickOn(m_accent);

    // Statusfarben mit Kontrastabsicherung gegen surface
    m_positive = ensureContrast(m_positive, m_surface, 3.5);
    m_negative = ensureContrast(m_negative, m_surface, 3.5);
    m_neutral = ensureContrast(m_neutral, m_surface, 3.5);

    // Separator als sanfte Mischung
    m_separator = mixLinearSrgb(m_text, m_bg, 0.88);

    emit themeChanged();
}

void SystemPalette::loadFromKdeGlobals() {
    QSettings s(m_kdeGlobalsPath, QSettings::IniFormat);

    // Standard-Fallbackwerte (Breeze Dark)
    const QColor defBg(42, 46, 50);
    const QColor defView(27, 30, 32);
    const QColor defText(252, 252, 252);
    const QColor defTextMuted(161, 169, 177);
    const QColor defAccent(61, 174, 233);
    const QColor defPositive(39, 174, 96);
    const QColor defNegative(218, 68, 83);
    const QColor defNeutral(246, 116, 0);

    // [Colors:Window]
    s.beginGroup(QStringLiteral("Colors:Window"));
    m_bg = parseRgb(s.value(QStringLiteral("BackgroundNormal")).toString(), defBg);
    m_text = parseRgb(s.value(QStringLiteral("ForegroundNormal")).toString(), defText);
    m_textMuted = parseRgb(s.value(QStringLiteral("ForegroundInactive")).toString(), defTextMuted);
    s.endGroup();

    // [Colors:View]
    s.beginGroup(QStringLiteral("Colors:View"));
    m_surfaceSunken = parseRgb(s.value(QStringLiteral("BackgroundNormal")).toString(), defView);
    m_surfaceAlt = parseRgb(s.value(QStringLiteral("BackgroundAlternate")).toString(), elevate(m_surfaceSunken, 1));
    m_textOnSunken = parseRgb(s.value(QStringLiteral("ForegroundNormal")).toString(), defText);
    m_positive = parseRgb(s.value(QStringLiteral("ForegroundPositive")).toString(), defPositive);
    m_negative = parseRgb(s.value(QStringLiteral("ForegroundNegative")).toString(), defNegative);
    m_neutral = parseRgb(s.value(QStringLiteral("ForegroundNeutral")).toString(), defNeutral);
    s.endGroup();

    // [General] AccentColor oder [Colors:Selection]
    s.beginGroup(QStringLiteral("General"));
    QString accentStr = s.value(QStringLiteral("AccentColor")).toString();
    s.endGroup();

    if (!accentStr.isEmpty()) {
        m_accent = parseRgb(accentStr, defAccent);
    } else {
        s.beginGroup(QStringLiteral("Colors:Selection"));
        m_accent = parseRgb(s.value(QStringLiteral("BackgroundNormal")).toString(), defAccent);
        s.endGroup();
    }

    // [KDE] AnimationDurationFactor
    s.beginGroup(QStringLiteral("KDE"));
    m_motionScale = s.value(QStringLiteral("AnimationDurationFactor"), 1.0).toDouble();
    if (m_motionScale < 0.0) m_motionScale = 0.0;
    s.endGroup();
}

void SystemPalette::loadFromPortal() {
    // Falls kein kdeglobals da ist: Platform Fallback
    loadFromPlatformFallback();
}

void SystemPalette::loadFromPlatformFallback() {
    QPalette pal = QGuiApplication::palette();
    m_bg = pal.color(QPalette::Window);
    m_surfaceSunken = pal.color(QPalette::Base);
    m_surfaceAlt = pal.color(QPalette::AlternateBase);
    m_text = pal.color(QPalette::WindowText);
    m_textMuted = pal.color(QPalette::PlaceholderText);
    m_textOnSunken = pal.color(QPalette::Text);
    m_accent = pal.color(QPalette::Highlight);
    m_positive = QColor(39, 174, 96);
    m_negative = QColor(218, 68, 83);
    m_neutral = QColor(246, 116, 0);
    m_motionScale = 1.0;
}

} // namespace lut
