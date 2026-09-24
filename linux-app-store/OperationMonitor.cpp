#include "OperationMonitor.h"
#include "AurUpdates.h"
#include "DaemonClient.h"
#include "models/FlatpakUpdates.h"
#include "models/SnapUpdates.h"
#include "models/PacstallUpdates.h"
#include "liblut/progress/ProgressModel.h"

#include <type_traits>

namespace lut {

namespace {

bool isNativeSource(const QString &source) {
    return source == QLatin1String("system") || source == QLatin1String("store");
}

LogLevel levelFor(const QString &line) {
    const QString lower = line.toLower();
    if (lower.startsWith(QLatin1String("error")) || lower.startsWith(QLatin1String("==> error"))
        || lower.startsWith(QLatin1String("fehler"))) {
        return LogLevel::Error;
    }
    if (lower.startsWith(QLatin1String("warning")) || lower.startsWith(QLatin1String("==> warning"))
        || lower.startsWith(QLatin1String("warnung"))) {
        return LogLevel::Warning;
    }
    return LogLevel::Info;
}

} // namespace

OperationMonitor::OperationMonitor(DaemonClient *client, FlatpakUpdates *flatpak, SnapUpdates *snap,
                                   AurUpdates *aur, PacstallUpdates *pacstall, QObject *parent)
    : QObject(parent), m_client(client), m_flatpak(flatpak), m_snap(snap), m_aur(aur), m_pacstall(pacstall)
{
    if (m_client) {
        connect(m_client, &DaemonClient::transactionStarted, this, [this]() {
            const bool upgrade = m_client->isUpgradeTransaction();
            QString title = tr("Systemaktualisierung");
            if (!upgrade) {
                const QString planTitle = m_client->planModel() ? m_client->planModel()->actionTitle() : QString();
                title = planTitle.isEmpty() ? tr("Paketoperation") : planTitle;
            }
            begin(upgrade ? QStringLiteral("system") : QStringLiteral("store"), title);
            syncNativeProgress();
        });
        connect(m_client, &DaemonClient::transactionFinished, this, [this](lut::Result result) {
            if (!running() || !isNativeSource(m_source)) return;
            int mapped = Failed;
            if (result == lut::Result::Success || result == lut::Result::SuccessWithWarnings) mapped = Succeeded;
            else if (result == lut::Result::Cancelled) mapped = Cancelled;
            finish(mapped, m_client->statusMessage());
        });

        ProgressModel *model = m_client->progressModel();
        const auto sync = [this]() { syncNativeProgress(); };
        connect(model, &ProgressModel::phaseChanged, this, sync);
        connect(model, &ProgressModel::progressChanged, this, sync);
        connect(model, &ProgressModel::activeItemChanged, this, sync);
        connect(model, &ProgressModel::throughputChanged, this, sync);
    }

    // Flatpak, Snap, AUR und Pacstall melden sich alle mit denselben Signalen.
    const auto wire = [this](auto *model, const QString &source) {
        if (!model) return;
        using Model = std::remove_pointer_t<decltype(model)>;
        connect(model, &Model::operationStarted, this, [this, source](const QString &title) {
            begin(source, title);
        });
        connect(model, &Model::operationProgress, this,
                [this, source](double fraction, bool indeterminate, int step, int total, const QString &text) {
            if (running() && m_source == source) update(fraction, indeterminate, step, total, text);
        });
        connect(model, &Model::operationFinished, this, [this, source](int result, const QString &message) {
            if (running() && m_source == source) finish(result, message);
        });
        connect(model, &Model::logLine, this, [this, source](const QString &line) {
            appendLog(source, line);
        });
    };
    wire(m_flatpak.data(), QStringLiteral("flatpak"));
    wire(m_snap.data(), QStringLiteral("snap"));
    wire(m_aur.data(), QStringLiteral("aur"));
    wire(m_pacstall.data(), QStringLiteral("pacstall"));

    if (m_aur) {
        connect(m_aur, &AurUpdates::reviewReady, this, [this](const QVariantList &items) {
            if (!running() || m_source != QLatin1String("aur")) return;
            m_awaitingReview = true;
            m_reviewItems = items;
            emit changed();
        });
    }

    if (m_pacstall) {
        connect(m_pacstall, &PacstallUpdates::reviewReady, this, [this](const QVariantList &items) {
            if (!running() || m_source != QLatin1String("pacstall")) return;
            m_awaitingReview = true;
            m_reviewItems = items;
            emit changed();
        });
    }
}

bool OperationMonitor::cancellable() const
{
    if (!running()) return false;
    if (m_awaitingReview) return true;
    if (isNativeSource(m_source)) return m_client && m_client->progressModel()->isCancellable();
    if (m_source == QLatin1String("flatpak")) return true;
    // "snap refresh" beenden hält snapd nicht an; ein Abbruch würde nur täuschen.
    if (m_source == QLatin1String("snap")) return false;
    if (m_source == QLatin1String("aur")) return m_aur && m_aur->cancellable();
    // Pacstall nur in der Prüfansicht (oben): mitten in "pacstall -I" abzubrechen
    // hieße dpkg während der Installation zu beenden.
    if (m_source == QLatin1String("pacstall")) return false;
    return false;
}

void OperationMonitor::begin(const QString &source, const QString &title)
{
    m_active = true;
    m_source = source;
    m_title = title;
    m_detail.clear();
    m_progress = 0.0;
    m_indeterminate = true;
    m_stepIndex = 0;
    m_stepCount = 0;
    m_eta.clear();
    m_result = Running;
    m_resultMessage.clear();
    m_awaitingReview = false;
    m_reviewItems.clear();
    emit changed();
    emit started();
}

void OperationMonitor::update(double fraction, bool indeterminate, int step, int total, const QString &text)
{
    m_progress = qBound(0.0, fraction, 1.0);
    m_indeterminate = indeterminate;
    m_stepIndex = step;
    m_stepCount = total;
    if (!text.isEmpty()) m_detail = text;
    // Nach der Prüfansicht geht es mit dem Bauen weiter.
    if (m_awaitingReview && m_source == QLatin1String("aur") && m_aur
        && m_aur->stage() != AurUpdates::Stage::Review) {
        m_awaitingReview = false;
        m_reviewItems.clear();
    }
    emit changed();
}

void OperationMonitor::finish(int result, const QString &message)
{
    m_result = result;
    m_resultMessage = message;
    m_awaitingReview = false;
    m_reviewItems.clear();
    if (result == Succeeded) {
        m_progress = 1.0;
        m_indeterminate = false;
    }
    emit changed();
    emit finished(result);
}

void OperationMonitor::syncNativeProgress()
{
    if (!running() || !isNativeSource(m_source) || !m_client) return;
    ProgressModel *model = m_client->progressModel();
    m_progress = qBound(0.0, model->totalProgress(), 1.0);
    m_indeterminate = model->isIndeterminate();
    m_detail = model->currentItemName().isEmpty() ? model->phaseLabel() : model->currentItemName();
    m_eta = m_indeterminate ? QString() : model->etaString();
    emit changed();
}

void OperationMonitor::appendLog(const QString &source, const QString &line)
{
    if (!m_client || line.isEmpty()) return;
    m_client->logModel()->appendLog(LogLine{levelFor(line), source, line});
}

void OperationMonitor::cancel()
{
    if (!cancellable()) return;
    if (m_awaitingReview) {
        rejectReview();
        return;
    }
    if (isNativeSource(m_source) && m_client) m_client->cancelTransaction();
    else if (m_source == QLatin1String("flatpak") && m_flatpak) m_flatpak->cancel();
    else if (m_source == QLatin1String("aur") && m_aur) m_aur->cancel();
    else if (m_source == QLatin1String("pacstall") && m_pacstall) m_pacstall->cancel();
}

void OperationMonitor::acknowledge()
{
    if (running()) return;
    m_active = false;
    emit changed();
}

void OperationMonitor::confirmReview()
{
    if (!m_awaitingReview) return;
    if (m_source == QLatin1String("aur") && m_aur) {
        m_awaitingReview = false;
        m_reviewItems.clear();
        emit changed();
        m_aur->confirmReview();
    } else if (m_source == QLatin1String("pacstall") && m_pacstall) {
        m_awaitingReview = false;
        m_reviewItems.clear();
        emit changed();
        m_pacstall->confirmReview();
    }
}

void OperationMonitor::rejectReview()
{
    if (!m_awaitingReview) return;
    if (m_source == QLatin1String("aur") && m_aur) {
        m_aur->cancel();
    } else if (m_source == QLatin1String("pacstall") && m_pacstall) {
        m_pacstall->cancel();
    }
}

} // namespace lut
