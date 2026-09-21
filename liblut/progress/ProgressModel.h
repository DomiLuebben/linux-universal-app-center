#pragma once

#include <QObject>
#include <QString>
#include <QElapsedTimer>
#include <QMap>
#include <QList>
#include "liblut/protocol/events.h"
#include "liblut/progress/Weights.h"
#include "liblut/progress/Eta.h"

namespace lut {

struct ScriptletTask {
    QString pkgId;
    QString name;
    QString label;
    bool isRunning = false;
    bool isFinished = false;
    int exitCode = 0;
    qint64 elapsedMs = 0;
};

class ProgressModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(lut::Phase currentPhase READ currentPhase NOTIFY phaseChanged)
    Q_PROPERTY(QString phaseLabel READ phaseLabel NOTIFY phaseChanged)
    Q_PROPERTY(bool isCancellable READ isCancellable NOTIFY phaseChanged)
    Q_PROPERTY(double totalProgress READ totalProgress NOTIFY progressChanged)
    Q_PROPERTY(double phaseProgress READ phaseProgress NOTIFY progressChanged)
    Q_PROPERTY(bool isIndeterminate READ isIndeterminate NOTIFY progressChanged)
    Q_PROPERTY(QString currentItemName READ currentItemName NOTIFY activeItemChanged)
    Q_PROPERTY(double currentItemProgress READ currentItemProgress NOTIFY activeItemChanged)
    Q_PROPERTY(qint64 downloadSpeed READ downloadSpeed NOTIFY throughputChanged)
    Q_PROPERTY(QString etaString READ etaString NOTIFY throughputChanged)
    Q_PROPERTY(bool hasKernelUpdate READ hasKernelUpdate NOTIFY planChanged)
    Q_PROPERTY(QString postTransactionNote READ postTransactionNote NOTIFY planChanged)

public:
    explicit ProgressModel(QObject *parent = nullptr);

    void reset();
    void processEvent(const Event &event);

    Phase currentPhase() const { return m_phase; }
    QString phaseLabel() const { return m_phaseLabel; }
    bool isCancellable() const { return m_cancellable; }
    double totalProgress() const { return m_totalProgress; }
    double phaseProgress() const { return m_phaseProgress; }
    bool isIndeterminate() const { return m_isIndeterminate; }

    QString currentItemName() const { return m_currentItemName; }
    double currentItemProgress() const { return m_currentItemProgress; }

    qint64 downloadSpeed() const { return m_downloadSpeed; }
    QString etaString() const;

    bool hasKernelUpdate() const { return m_hasKernel; }
    QString postTransactionNote() const;

    const PhaseWeights &weights() const { return m_weights; }
    const QList<ScriptletTask> &scriptletTasks() const { return m_scriptletTasks; }
    const QList<PackageOp> &plannedOps() const { return m_plannedOps; }
    const QList<LogLine> &recentLogs() const { return m_logs; }

    static QString mapScriptletToDescription(const QString &scriptletName);

signals:
    void phaseChanged(lut::Phase phase, const QString &label, bool cancellable);
    void progressChanged(double total, double phase);
    void activeItemChanged(const QString &name, double progress);
    void throughputChanged(qint64 bps, qint64 done, qint64 total);
    void planChanged();
    void scriptletTaskUpdated();
    void logAdded(const lut::LogLine &line);
    void questionReceived(const lut::Question &question);
    void transactionCompleted(const lut::TransactionDone &done);

private:
    void setMonotonicTotalProgress(double p);
    void recalculatePhaseProgress();

    Phase m_phase = Phase::Idle;
    QString m_phaseLabel;
    bool m_cancellable = false;

    double m_totalProgress = 0.0;
    double m_phaseProgress = 0.0;
    bool m_isIndeterminate = false;

    PhaseWeights m_weights;
    EtaCalculator m_etaCalc;

    // Plan & Größen
    QList<PackageOp> m_plannedOps;
    qint64 m_totalDownloadBytes = 0;
    qint64 m_totalInstallBytes = 0;
    bool m_hasKernel = false;

    // Aktiver Item
    QString m_currentItemId;
    QString m_currentItemName;
    double m_currentItemProgress = 0.0;
    qint64 m_itemDone = 0;
    qint64 m_itemTotal = 0;

    // Phase Item-Fortschrittssummen
    qint64 m_phaseBytesDone = 0;
    qint64 m_phaseBytesTotal = 0;
    int m_itemsFinishedInPhase = 0;
    int m_itemsTotalInPhase = 0;

    // Download Durchsatz
    qint64 m_downloadSpeed = 0;
    qint64 m_downloadDone = 0;
    qint64 m_downloadTotal = 0;

    // Scriptlets
    QList<ScriptletTask> m_scriptletTasks;
    QElapsedTimer m_scriptletTimer;

    // Logs
    QList<LogLine> m_logs;
};

} // namespace lut
