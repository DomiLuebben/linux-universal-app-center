#include "TrayManager.h"
#include "DaemonClient.h"
#include "models/FlatpakUpdates.h"
#include "models/SnapUpdates.h"
#include "models/PacstallUpdates.h"
#include "AurUpdates.h"
#include "OperationMonitor.h"

#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QWindow>
#include <QIcon>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QFont>
#include <QPalette>

namespace lut {

TrayManager::TrayManager(QWindow *mainWindow,
                         DaemonClient *client,
                         FlatpakUpdates *flatpakUpdates,
                         SnapUpdates *snapUpdates,
                         AurUpdates *aurUpdates,
                         PacstallUpdates *pacstallUpdates,
                         QObject *parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
    , m_client(client)
    , m_flatpakUpdates(flatpakUpdates)
    , m_snapUpdates(snapUpdates)
    , m_aurUpdates(aurUpdates)
    , m_pacstallUpdates(pacstallUpdates)
{
    // Signale verdrahten für Aktualisierungsänderungen
    if (m_client) {
        if (m_client->updatesModel()) {
            connect(m_client->updatesModel(), &UpdatesModel::countChanged, this, &TrayManager::onCountsChanged);
        }
        connect(m_client, &DaemonClient::statusChanged, this, [this]() {
            emit busyChanged();
        });
    }
    if (m_flatpakUpdates) {
        connect(m_flatpakUpdates, &FlatpakUpdates::countChanged, this, &TrayManager::onCountsChanged);
        connect(m_flatpakUpdates, &FlatpakUpdates::busyChanged, this, &TrayManager::busyChanged);
    }
    if (m_snapUpdates) {
        connect(m_snapUpdates, &SnapUpdates::countChanged, this, &TrayManager::onCountsChanged);
        connect(m_snapUpdates, &SnapUpdates::busyChanged, this, &TrayManager::busyChanged);
    }
    if (m_aurUpdates) {
        connect(m_aurUpdates, &AurUpdates::countChanged, this, &TrayManager::onCountsChanged);
        connect(m_aurUpdates, &AurUpdates::availableChanged, this, &TrayManager::onCountsChanged);
        connect(m_aurUpdates, &AurUpdates::busyChanged, this, &TrayManager::busyChanged);
    }
    if (m_pacstallUpdates) {
        connect(m_pacstallUpdates, &PacstallUpdates::countChanged, this, &TrayManager::onCountsChanged);
        connect(m_pacstallUpdates, &PacstallUpdates::availableChanged, this, &TrayManager::onCountsChanged);
        connect(m_pacstallUpdates, &PacstallUpdates::busyChanged, this, &TrayManager::busyChanged);
    }

    // System-Tray-Icon einrichten
    if (isTrayAvailable()) {
        m_trayIcon = new QSystemTrayIcon(this);
        QIcon appIcon = QIcon::fromTheme(
            QStringLiteral("org.linuxuniversalappcenter"),
            QIcon(QStringLiteral(":/LinuxAppStore/icons/org.linuxuniversalappcenter.svg")));
        if (appIcon.isNull()) {
            appIcon = QIcon::fromTheme(QStringLiteral("system-software-update"));
        }
        m_baseIcon = appIcon;
        m_trayIcon->setIcon(appIcon);

        m_menu = new QMenu();
        m_statusAction = m_menu->addAction(tr("System ist aktuell"), this, &TrayManager::showUpdates);
        m_statusAction->setEnabled(false);

        m_menu->addSeparator();

        m_openAction = m_menu->addAction(tr("Store öffnen"), this, &TrayManager::showWindow);
        m_checkAction = m_menu->addAction(tr("Nach Aktualisierungen suchen"), this, &TrayManager::checkForUpdates);

        m_menu->addSeparator();

        m_quitAction = m_menu->addAction(tr("Beenden"), this, &TrayManager::quitApplication);

        m_trayIcon->setContextMenu(m_menu);

        connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            onTrayActivated(static_cast<int>(reason));
        });

        connect(m_trayIcon, &QSystemTrayIcon::messageClicked, this, &TrayManager::showUpdates);

        m_trayIcon->show();
    }

    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        if (m_trayIcon) {
            m_trayIcon->hide();
        }
    });

    updateTrayUi();

    // Hintergrund-Timer für automatische Update-Prüfung
    // Erster Check 5 Sekunden nach Programmstart
    QTimer::singleShot(5000, this, &TrayManager::checkForUpdates);

    // Periodischer Check alle 2 Stunden (7.200.000 ms)
    m_periodicTimer = new QTimer(this);
    m_periodicTimer->setInterval(2 * 60 * 60 * 1000);
    connect(m_periodicTimer, &QTimer::timeout, this, &TrayManager::checkForUpdates);
    m_periodicTimer->start();
}

TrayManager::~TrayManager()
{
    if (m_periodicTimer) {
        m_periodicTimer->stop();
    }
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
    delete m_menu;
}

void TrayManager::setMainWindow(QWindow *mainWindow)
{
    m_mainWindow = mainWindow;
}

bool TrayManager::isTrayAvailable() const
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

int TrayManager::totalUpdates() const
{
    int nativeCount = (m_client && m_client->updatesModel()) ? m_client->updatesModel()->totalCount() : 0;
    int flatpakCount = (m_flatpakUpdates && m_flatpakUpdates->available()) ? m_flatpakUpdates->count() : 0;
    int snapCount = (m_snapUpdates && m_snapUpdates->available()) ? m_snapUpdates->count() : 0;
    int aurCount = (m_aurUpdates && m_aurUpdates->available()) ? m_aurUpdates->count() : 0;
    int pacstallCount = (m_pacstallUpdates && m_pacstallUpdates->available()) ? m_pacstallUpdates->count() : 0;
    return nativeCount + flatpakCount + snapCount + aurCount + pacstallCount;
}

QString TrayManager::breakdownText() const
{
    bool hasFlatpak = m_flatpakUpdates && m_flatpakUpdates->available();
    bool hasSnap = m_snapUpdates && m_snapUpdates->available();
    bool hasAur = m_aurUpdates && m_aurUpdates->available();
    bool hasPacstall = m_pacstallUpdates && m_pacstallUpdates->available();
    int nativeCount = (m_client && m_client->updatesModel()) ? m_client->updatesModel()->totalCount() : 0;
    int flatpakCount = hasFlatpak ? m_flatpakUpdates->count() : 0;
    int snapCount = hasSnap ? m_snapUpdates->count() : 0;
    int aurCount = hasAur ? m_aurUpdates->count() : 0;
    int pacstallCount = hasPacstall ? m_pacstallUpdates->count() : 0;
    return formatBreakdown(nativeCount, flatpakCount, snapCount, aurCount, hasFlatpak, hasSnap, hasAur, pacstallCount, hasPacstall);
}

bool TrayManager::isBusy() const
{
    bool clientBusy = m_client && m_client->isBusy();
    bool flatpakBusy = m_flatpakUpdates && m_flatpakUpdates->busy();
    bool snapBusy = m_snapUpdates && m_snapUpdates->busy();
    bool aurBusy = m_aurUpdates && m_aurUpdates->busy();
    bool pacstallBusy = m_pacstallUpdates && m_pacstallUpdates->busy();
    return clientBusy || flatpakBusy || snapBusy || aurBusy || pacstallBusy;
}

QString TrayManager::formatBreakdown(int nativeCount, int flatpakCount, int snapCount, int aurCount,
                                     bool hasFlatpak, bool hasSnap, bool hasAur,
                                     int pacstallCount, bool hasPacstall)
{
    QStringList parts;
    parts.append(QStringLiteral("%1 Nativ").arg(nativeCount));
    if (hasFlatpak) {
        parts.append(QStringLiteral("%1 Flatpak").arg(flatpakCount));
    }
    if (hasSnap) {
        parts.append(QStringLiteral("%1 Snap").arg(snapCount));
    }
    if (hasAur && aurCount > 0) {
        parts.append(QStringLiteral("%1 AUR").arg(aurCount));
    }
    if (hasPacstall && pacstallCount > 0) {
        parts.append(QStringLiteral("%1 Pacstall").arg(pacstallCount));
    }
    return parts.join(QStringLiteral(", "));
}

QString TrayManager::badgeLabel(int count)
{
    return count > 99 ? QStringLiteral("99+") : QString::number(count);
}

QIcon TrayManager::badgedIcon(const QIcon &base, int count, const QColor &background, const QColor &foreground,
                              const QColor &outline)
{
    if (count <= 0) return base;

    const QString label = badgeLabel(count);
    QIcon result;
    // Plasma fordert je nach Leiste und Skalierung unterschiedliche Größen an.
    for (int size : {16, 22, 24, 32, 44, 48, 64}) {
        QPixmap canvas(size, size);
        canvas.fill(Qt::transparent);

        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        base.paint(&painter, QRect(0, 0, size, size));

        // Die Plakette nimmt gut die Hälfte ein, sonst ist die Zahl bei 22 px nicht lesbar.
        const qreal height = size * 0.58;
        const qreal width = label.size() == 1 ? height : height * (0.55 + 0.38 * label.size());
        const QRectF badge(size - width, size - height, width, height);

        QFont font = QGuiApplication::font();
        font.setBold(true);
        font.setPixelSize(qMax(7, qRound(height * (label.size() > 2 ? 0.62 : 0.74))));

        painter.setPen(Qt::NoPen);
        // Schmaler Rand in der Fensterfarbe trennt die Plakette vom (ebenfalls
        // blauen) Symbol darunter; ohne ihn verschwamm sie bei 22 px.
        if (outline.isValid()) {
            const qreal ring = qMax<qreal>(1.0, size / 16.0);
            painter.setBrush(outline);
            painter.drawRoundedRect(badge.adjusted(-ring, -ring, ring, ring), height / 2.0 + ring, height / 2.0 + ring);
        }
        painter.setBrush(background);
        painter.drawRoundedRect(badge, height / 2.0, height / 2.0);
        painter.setPen(foreground);
        painter.setFont(font);
        painter.drawText(badge, Qt::AlignCenter, label);
        painter.end();

        result.addPixmap(canvas);
    }
    return result;
}

void TrayManager::setOperationMonitor(OperationMonitor *monitor)
{
    m_monitor = monitor;
    if (m_monitor) {
        connect(m_monitor, &OperationMonitor::changed, this, &TrayManager::updateTrayUi);
    }
    updateTrayUi();
}

void TrayManager::checkForUpdates()
{
    if (isBusy()) return;
    if (m_client) m_client->refreshUpdates();
    if (m_flatpakUpdates && m_flatpakUpdates->available()) m_flatpakUpdates->check();
    if (m_snapUpdates && m_snapUpdates->available()) m_snapUpdates->check();
    if (m_aurUpdates && m_aurUpdates->available()) m_aurUpdates->check();
    if (m_pacstallUpdates && m_pacstallUpdates->available()) m_pacstallUpdates->check();
}

void TrayManager::showWindow()
{
    if (!m_mainWindow) return;
    if (m_mainWindow->visibility() == QWindow::Minimized) {
        m_mainWindow->showNormal();
    } else {
        m_mainWindow->show();
    }
    m_mainWindow->raise();
    m_mainWindow->requestActivate();
}

void TrayManager::showUpdates()
{
    showWindow();
    emit openUpdatesRequested();
}

void TrayManager::hideWindow()
{
    if (!m_mainWindow) return;
    m_mainWindow->hide();
}

void TrayManager::toggleWindow()
{
    if (!m_mainWindow) return;
    if (m_mainWindow->isVisible() && m_mainWindow->isActive()) {
        hideWindow();
    } else {
        showWindow();
    }
}

void TrayManager::quitApplication()
{
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
    QCoreApplication::quit();
}

void TrayManager::onCountsChanged()
{
    updateTrayUi();
}

void TrayManager::onTrayActivated(int reason)
{
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        toggleWindow();
    }
}

void TrayManager::updateTrayUi()
{
    bool hasFlatpak = m_flatpakUpdates && m_flatpakUpdates->available();
    bool hasSnap = m_snapUpdates && m_snapUpdates->available();
    bool hasAur = m_aurUpdates && m_aurUpdates->available();
    int nativeCount = (m_client && m_client->updatesModel()) ? m_client->updatesModel()->totalCount() : 0;
    int flatpakCount = hasFlatpak ? m_flatpakUpdates->count() : 0;
    int snapCount = hasSnap ? m_snapUpdates->count() : 0;
    int aurCount = hasAur ? m_aurUpdates->count() : 0;

    int total = nativeCount + flatpakCount + snapCount + aurCount;
    QString breakdown = formatBreakdown(nativeCount, flatpakCount, snapCount, aurCount, hasFlatpak, hasSnap, hasAur);

    if (m_statusAction) {
        m_statusAction->setText(total > 0 ? breakdown : tr("System ist aktuell"));
        m_statusAction->setEnabled(total > 0);
    }

    if (m_trayIcon) {
        QString tip = total > 0 ? QStringLiteral("Linux Universal App Center\n%1").arg(breakdown)
                                : QStringLiteral("Linux Universal App Center\nSystem ist auf dem neuesten Stand");
        if (m_monitor && m_monitor->running()) {
            const QString percent = m_monitor->indeterminate()
                ? QString()
                : QStringLiteral(" – %1 %").arg(qRound(m_monitor->progress() * 100));
            tip += QStringLiteral("\n%1%2").arg(m_monitor->title(), percent);
        }
        m_trayIcon->setToolTip(tip);

        // Zahl ins Symbol zeichnen; nur neu erzeugen, wenn sie sich ändert.
        if (total != m_shownBadgeCount) {
            const QPalette palette = QGuiApplication::palette();
            m_trayIcon->setIcon(total > 0
                ? badgedIcon(m_baseIcon, total, palette.color(QPalette::Highlight), palette.color(QPalette::HighlightedText),
                             palette.color(QPalette::Window))
                : m_baseIcon);
            m_shownBadgeCount = total;
        }
    }

    // Wenn neue Updates im Hintergrund entdeckt wurden und das Fenster nicht aktiv ist:
    if (total > m_lastTotalUpdates && m_trayIcon && isTrayAvailable()) {
        if (!m_mainWindow || !m_mainWindow->isVisible() || !m_mainWindow->isActive()) {
            m_trayIcon->showMessage(tr("Aktualisierungen verfügbar"), breakdown, QSystemTrayIcon::Information, 8000);
        }
    }
    m_lastTotalUpdates = total;
    emit updatesChanged();
}

} // namespace lut
