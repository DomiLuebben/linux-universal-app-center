#include "ProgressModel.h"
#include <algorithm>

namespace lut {

ProgressModel::ProgressModel(QObject *parent)
    : QObject(parent),
      m_weights(calculateWeights(0, 0)) {
}

void ProgressModel::reset() {
    m_phase = Phase::Idle;
    m_phaseLabel.clear();
    m_cancellable = false;

    m_totalProgress = 0.0;
    m_phaseProgress = 0.0;
    m_isIndeterminate = false;

    m_weights = calculateWeights(0, 0);
    m_etaCalc.reset();

    m_plannedOps.clear();
    m_totalDownloadBytes = 0;
    m_totalInstallBytes = 0;
    m_hasKernel = false;

    m_currentItemId.clear();
    m_currentItemName.clear();
    m_currentItemProgress = 0.0;
    m_itemDone = 0;
    m_itemTotal = 0;

    m_phaseBytesDone = 0;
    m_phaseBytesTotal = 0;
    m_itemsFinishedInPhase = 0;
    m_itemsTotalInPhase = 0;

    m_downloadSpeed = 0;
    m_downloadDone = 0;
    m_downloadTotal = 0;

    m_scriptletTasks.clear();
    m_logs.clear();

    emit phaseChanged(m_phase, m_phaseLabel, m_cancellable);
    emit progressChanged(m_totalProgress, m_phaseProgress);
    emit activeItemChanged(m_currentItemName, m_currentItemProgress);
    emit throughputChanged(0, 0, 0);
    emit planChanged();
}

void ProgressModel::setMonotonicTotalProgress(double p) {
    double clamped = std::clamp(p, 0.0, 1.0);
    if (clamped > m_totalProgress) {
        m_totalProgress = clamped;
        emit progressChanged(m_totalProgress, m_phaseProgress);
    }
}

void ProgressModel::recalculatePhaseProgress() {
    double base = phaseBaseProgress(m_phase, m_weights);
    double weight = phaseWeight(m_phase, m_weights);

    double newTotal = base + (weight * m_phaseProgress);
    setMonotonicTotalProgress(newTotal);
}

QString ProgressModel::mapScriptletToDescription(const QString &scriptletName) {
    const QString s = scriptletName.toLower();
    if (s.contains(QLatin1String("dracut"))) {
        return QStringLiteral("Initramfs erzeugen");
    }
    if (s.contains(QLatin1String("kernel-install"))) {
        return QStringLiteral("Bootloader-Eintrag anlegen");
    }
    if (s.contains(QLatin1String("mkinitcpio"))) {
        return QStringLiteral("Initramfs erzeugen (mkinitcpio)");
    }
    if (s.contains(QLatin1String("grub"))) {
        return QStringLiteral("GRUB-Bootloader-Konfiguration aktualisieren");
    }
    if (s.contains(QLatin1String("ldconfig"))) {
        return QStringLiteral("Bibliotheks-Cache neu aufbauen");
    }
    if (s.contains(QLatin1String("gtk-update-icon-cache")) || s.contains(QLatin1String("icon-cache"))) {
        return QStringLiteral("Icon-Zwischenspeicher erneuern");
    }
    if (s.contains(QLatin1String("glib-compile-schemas"))) {
        return QStringLiteral("GSettings-Schemata kompilieren");
    }
    if (s.contains(QLatin1String("systemd-sysusers")) || s.contains(QLatin1String("sysusers"))) {
        return QStringLiteral("Systembenutzer konfigurieren");
    }
    if (s.contains(QLatin1String("systemd-tmpfiles")) || s.contains(QLatin1String("tmpfiles"))) {
        return QStringLiteral("Temporäre Systempfade einrichten");
    }
    if (s.contains(QLatin1String("systemd-udev")) || s.contains(QLatin1String("udev"))) {
        return QStringLiteral("Hardware-Regeln neu laden");
    }
    if (s.contains(QLatin1String("akmods")) || s.contains(QLatin1String("dkms"))) {
        return QStringLiteral("Kernelmodule kompilieren");
    }
    if (s.contains(QLatin1String("%post"))) {
        return QStringLiteral("Nachbereitungsskript (%post)");
    }
    if (s.contains(QLatin1String("%pre"))) {
        return QStringLiteral("Vorbereitungsskript (%pre)");
    }
    if (s.contains(QLatin1String("trigger"))) {
        return QStringLiteral("System-Trigger ausführen");
    }

    return scriptletName;
}

QString ProgressModel::postTransactionNote() const {
    if (m_hasKernel) {
        return QStringLiteral("Initramfs und Bootloader werden erzeugt — das dauert bei Kernel-Updates typischerweise 1–3 Minuten.");
    }
    return QString();
}

QString ProgressModel::etaString() const {
    if (m_phase == Phase::Download) {
        qint64 remaining = std::max<qint64>(0, m_downloadTotal - m_downloadDone);
        auto est = m_etaCalc.estimateRemainingSeconds(remaining);
        if (est.has_value()) {
            return m_etaCalc.formatRemainingTime(*est);
        }
    }
    return QString();
}

void ProgressModel::processEvent(const Event &event) {
    std::visit([this](auto &&arg) {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, PhaseChanged>) {
            m_phase = arg.phase;
            m_phaseLabel = arg.label.isEmpty() ? phaseToString(arg.phase) : arg.label;
            m_cancellable = arg.cancellable;

            m_phaseProgress = 0.0;
            m_phaseBytesDone = 0;
            m_phaseBytesTotal = 0;
            m_itemsFinishedInPhase = 0;

            if (m_phase == Phase::Resolve || m_phase == Phase::Verify || m_phase == Phase::TestTransaction) {
                m_isIndeterminate = true;
            } else {
                m_isIndeterminate = false;
            }

            if (m_phase == Phase::Cleanup || m_phase == Phase::Finished) {
                m_phaseProgress = 1.0;
                setMonotonicTotalProgress(1.0);
            } else {
                recalculatePhaseProgress();
            }

            emit phaseChanged(m_phase, m_phaseLabel, m_cancellable);
            emit progressChanged(m_totalProgress, m_phaseProgress);
        } else if constexpr (std::is_same_v<T, PlanReady>) {
            m_plannedOps = arg.ops;
            m_totalDownloadBytes = arg.downloadBytes;
            m_totalInstallBytes = 0;
            m_hasKernel = false;

            for (const auto &op : m_plannedOps) {
                m_totalInstallBytes += op.installedSize;
                if (op.isKernel) {
                    m_hasKernel = true;
                }
            }

            m_weights = calculateWeights(m_totalDownloadBytes, m_totalInstallBytes);
            emit planChanged();
        } else if constexpr (std::is_same_v<T, ItemStarted>) {
            m_currentItemId = arg.pkgId;
            m_currentItemName = arg.pkgId;
            m_currentItemProgress = 0.0;
            m_itemDone = 0;
            m_itemTotal = arg.weight > 0 ? arg.weight : 100;

            emit activeItemChanged(m_currentItemName, m_currentItemProgress);
        } else if constexpr (std::is_same_v<T, ItemProgress>) {
            if (arg.total > 0) {
                m_currentItemProgress = std::clamp(static_cast<double>(arg.done) / static_cast<double>(arg.total), 0.0, 1.0);
            } else {
                m_currentItemProgress = 0.0;
            }

            if (m_phase == Phase::Commit) {
                if (m_totalInstallBytes > 0) {
                    double currentDone = static_cast<double>(m_phaseBytesDone) + (m_currentItemProgress * static_cast<double>(m_itemTotal));
                    m_phaseProgress = std::clamp(currentDone / static_cast<double>(m_totalInstallBytes), 0.0, 1.0);
                    recalculatePhaseProgress();
                } else if (!m_plannedOps.isEmpty()) {
                    double step = 1.0 / static_cast<double>(m_plannedOps.size());
                    m_phaseProgress = std::clamp((m_itemsFinishedInPhase * step) + (m_currentItemProgress * step), 0.0, 1.0);
                    recalculatePhaseProgress();
                }
            }

            emit activeItemChanged(m_currentItemName, m_currentItemProgress);
        } else if constexpr (std::is_same_v<T, ItemFinished>) {
            m_itemsFinishedInPhase++;
            m_phaseBytesDone += m_itemTotal;
            m_currentItemProgress = 1.0;

            if (m_phase == Phase::Commit && !m_plannedOps.isEmpty()) {
                m_phaseProgress = std::clamp(static_cast<double>(m_itemsFinishedInPhase) / static_cast<double>(m_plannedOps.size()), 0.0, 1.0);
                recalculatePhaseProgress();
            }

            emit activeItemChanged(m_currentItemName, 1.0);
        } else if constexpr (std::is_same_v<T, DownloadThroughput>) {
            m_downloadSpeed = arg.bytesPerSecond;
            m_downloadDone = arg.totalDone;
            m_downloadTotal = arg.totalTotal;

            m_etaCalc.addThroughputSample(arg.bytesPerSecond);

            if (m_phase == Phase::Download && m_downloadTotal > 0) {
                m_phaseProgress = std::clamp(static_cast<double>(m_downloadDone) / static_cast<double>(m_downloadTotal), 0.0, 1.0);
                recalculatePhaseProgress();
            }

            emit throughputChanged(m_downloadSpeed, m_downloadDone, m_downloadTotal);
        } else if constexpr (std::is_same_v<T, ScriptletStarted>) {
            ScriptletTask task;
            task.pkgId = arg.pkgId;
            task.name = arg.scriptletName;
            task.label = mapScriptletToDescription(arg.scriptletName);
            task.isRunning = true;
            task.isFinished = false;
            m_scriptletTasks.append(task);

            m_currentItemName = task.label;
            m_currentItemProgress = 0.0;
            m_isIndeterminate = true;

            emit scriptletTaskUpdated();
            emit activeItemChanged(m_currentItemName, m_currentItemProgress);
        } else if constexpr (std::is_same_v<T, ScriptletFinished>) {
            for (auto &t : m_scriptletTasks) {
                if (t.pkgId == arg.pkgId && t.name == arg.scriptletName && t.isRunning) {
                    t.isRunning = false;
                    t.isFinished = true;
                    t.exitCode = arg.exitCode;
                    break;
                }
            }

            // PostTransaction Fortschritt nach abgeschlossenen Scriptlets
            int finishedCount = 0;
            for (const auto &t : m_scriptletTasks) {
                if (t.isFinished) finishedCount++;
            }
            if (!m_scriptletTasks.isEmpty()) {
                m_phaseProgress = std::clamp(static_cast<double>(finishedCount) / static_cast<double>(m_scriptletTasks.size()), 0.0, 1.0);
                recalculatePhaseProgress();
            }

            emit scriptletTaskUpdated();
        } else if constexpr (std::is_same_v<T, LogLine>) {
            m_logs.append(arg);
            if (m_logs.size() > 1000) {
                m_logs.removeFirst();
            }
            emit logAdded(arg);
        } else if constexpr (std::is_same_v<T, Question>) {
            emit questionReceived(arg);
        } else if constexpr (std::is_same_v<T, TransactionDone>) {
            if (arg.result == Result::Success || arg.result == Result::SuccessWithWarnings) {
                m_phase = Phase::Finished;
                m_phaseProgress = 1.0;
                setMonotonicTotalProgress(1.0);
            } else if (arg.result == Result::Failed) {
                m_phase = Phase::Failed;
            } else if (arg.result == Result::Cancelled) {
                m_phase = Phase::Cancelled;
            }
            m_cancellable = false;
            emit phaseChanged(m_phase, phaseToString(m_phase), m_cancellable);
            emit transactionCompleted(arg);
        }
    }, event);
}

} // namespace lut
