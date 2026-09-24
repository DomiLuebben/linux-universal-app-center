#pragma once

#include <QObject>
#include <QString>
#include <QIcon>
#include <QColor>
#include <memory>

class QSystemTrayIcon;
class QMenu;
class QAction;
class QTimer;
class QWindow;

namespace lut {

class DaemonClient;
class FlatpakUpdates;
class SnapUpdates;
class AurUpdates;
class PacstallUpdates;

class TrayManager : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool isTrayAvailable READ isTrayAvailable CONSTANT)
    Q_PROPERTY(int totalUpdates READ totalUpdates NOTIFY updatesChanged)
    Q_PROPERTY(QString breakdownText READ breakdownText NOTIFY updatesChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY busyChanged)

public:
    explicit TrayManager(QWindow *mainWindow,
                         DaemonClient *client,
                         FlatpakUpdates *flatpakUpdates,
                         SnapUpdates *snapUpdates,
                         AurUpdates *aurUpdates,
                         PacstallUpdates *pacstallUpdates = nullptr,
                         QObject *parent = nullptr);
    ~TrayManager() override;

    void setMainWindow(QWindow *mainWindow);
    bool isTrayAvailable() const;
    int totalUpdates() const;
    QString breakdownText() const;
    bool isBusy() const;

    /// Formatiert die Aufschlüsselung gemäß Anforderung: nur verfügbare Quellen aufzählen
    static QString formatBreakdown(int nativeCount, int flatpakCount, int snapCount, int aurCount = 0,
                                   bool hasFlatpak = true, bool hasSnap = true, bool hasAur = true,
                                   int pacstallCount = 0, bool hasPacstall = false);

    /// Zeichnet die Anzahl als Plakette in die untere rechte Ecke des Symbols.
    /// QSystemTrayIcon kennt keine Zahlenplakette; sie muss ins Bild selbst.
    static QIcon badgedIcon(const QIcon &base, int count, const QColor &background, const QColor &foreground,
                            const QColor &outline = QColor());

    /// Beschriftung der Plakette: bis 99 die Zahl, darüber "99+".
    static QString badgeLabel(int count);

    /// Hängt den laufenden Vorgang an den Tooltip an, solange das Fortschrittsfenster verborgen ist.
    void setOperationMonitor(class OperationMonitor *monitor);

public slots:
    void checkForUpdates();
    void showWindow();
    void showUpdates();
    void hideWindow();
    void toggleWindow();
    void quitApplication();

signals:
    void updatesChanged();
    void busyChanged();
    void openUpdatesRequested();

private slots:
    void onCountsChanged();
    void onTrayActivated(int reason);

private:
    void updateTrayUi();

    QWindow *m_mainWindow = nullptr;
    DaemonClient *m_client = nullptr;
    FlatpakUpdates *m_flatpakUpdates = nullptr;
    SnapUpdates *m_snapUpdates = nullptr;
    AurUpdates *m_aurUpdates = nullptr;
    PacstallUpdates *m_pacstallUpdates = nullptr;

    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_statusAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_checkAction = nullptr;
    QAction *m_quitAction = nullptr;

    QTimer *m_periodicTimer = nullptr;
    int m_lastTotalUpdates = 0;
    QIcon m_baseIcon;
    int m_shownBadgeCount = -1;
    class OperationMonitor *m_monitor = nullptr;
};

} // namespace lut
