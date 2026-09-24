#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>

namespace lut {

class DaemonClient;
class FlatpakUpdates;
class SnapUpdates;
class AurUpdates;
class PacstallUpdates;

/// Fasst den Fortschritt aller Paketquellen für das gemeinsame Fortschrittsfenster
/// zusammen. System- und Store-Transaktionen kommen über den Daemon, Flatpak,
/// Snap, AUR und Pacstall laufen im GUI-Prozess bzw. über eigene Backends. Das Fenster liest nur diese Klasse und
/// muss nicht wissen, woher ein Vorgang stammt.
class OperationMonitor : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString source READ source NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString detail READ detail NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(bool indeterminate READ indeterminate NOTIFY changed)
    Q_PROPERTY(int stepIndex READ stepIndex NOTIFY changed)
    Q_PROPERTY(int stepCount READ stepCount NOTIFY changed)
    Q_PROPERTY(QString etaString READ etaString NOTIFY changed)
    Q_PROPERTY(bool cancellable READ cancellable NOTIFY changed)
    Q_PROPERTY(int result READ result NOTIFY changed)
    Q_PROPERTY(QString resultMessage READ resultMessage NOTIFY changed)
    Q_PROPERTY(bool awaitingReview READ awaitingReview NOTIFY changed)
    Q_PROPERTY(QVariantList reviewItems READ reviewItems NOTIFY changed)

public:
    /// Gleiche Werte wie OperationResult.
    enum Outcome { Running = 0, Succeeded = 1, Failed = 2, Cancelled = 3 };
    Q_ENUM(Outcome)

    explicit OperationMonitor(DaemonClient *client, FlatpakUpdates *flatpak, SnapUpdates *snap,
                              AurUpdates *aur, PacstallUpdates *pacstall = nullptr, QObject *parent = nullptr);

    bool active() const { return m_active; }
    bool running() const { return m_active && m_result == Running; }
    QString source() const { return m_source; }
    QString title() const { return m_title; }
    QString detail() const { return m_detail; }
    double progress() const { return m_progress; }
    bool indeterminate() const { return m_indeterminate; }
    int stepIndex() const { return m_stepIndex; }
    int stepCount() const { return m_stepCount; }
    QString etaString() const { return m_eta; }
    bool cancellable() const;
    int result() const { return m_result; }
    QString resultMessage() const { return m_resultMessage; }
    bool awaitingReview() const { return m_awaitingReview; }
    QVariantList reviewItems() const { return m_reviewItems; }

public slots:
    void cancel();
    /// Nach Abschluss: Fenster darf geschlossen werden, Zustand wird zurückgesetzt.
    void acknowledge();
    void confirmReview();
    void rejectReview();

signals:
    void changed();
    /// Ein neuer Vorgang hat begonnen: Fenster öffnen.
    void started();
    void finished(int result);

private:
    void begin(const QString &source, const QString &title);
    void update(double fraction, bool indeterminate, int step, int total, const QString &text);
    void finish(int result, const QString &message);
    void syncNativeProgress();
    void appendLog(const QString &source, const QString &line);

    QPointer<DaemonClient> m_client;
    QPointer<FlatpakUpdates> m_flatpak;
    QPointer<SnapUpdates> m_snap;
    QPointer<AurUpdates> m_aur;
    QPointer<PacstallUpdates> m_pacstall;

    bool m_active = false;
    QString m_source;
    QString m_title;
    QString m_detail;
    double m_progress = 0.0;
    bool m_indeterminate = true;
    int m_stepIndex = 0;
    int m_stepCount = 0;
    QString m_eta;
    int m_result = Running;
    QString m_resultMessage;
    bool m_awaitingReview = false;
    QVariantList m_reviewItems;
};

} // namespace lut
