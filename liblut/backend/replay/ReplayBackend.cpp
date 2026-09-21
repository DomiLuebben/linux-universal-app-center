#include "ReplayBackend.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <cmath>

namespace lut {

ReplayBackend::ReplayBackend(QObject *parent)
    : Backend(parent),
      m_timer(new QTimer(this)) {
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &ReplayBackend::processNextItem);
}

ReplayBackend::ReplayBackend(const QString &fixturePath, double speed, QObject *parent)
    : ReplayBackend(parent) {
    m_speed = speed;
    loadFixture(fixturePath);
}

ReplayBackend::~ReplayBackend() {
    stop();
}

bool ReplayBackend::loadFixture(const QString &fixturePath) {
    stop();
    m_items.clear();
    m_cachedUpdates.clear();
    m_fixturePath = fixturePath;

    QFile file(fixturePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "ReplayBackend: Could not open fixture:" << fixturePath;
        return false;
    }

    qint64 lastTimeMs = 0;

    while (!file.atEnd()) {
        QByteArray line = file.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }

        QJsonObject root = doc.object();
        QJsonObject eventObj;
        qint64 delayMs = 25; // Standard-Verzögerung falls nicht angegeben

        if (root.contains(QStringLiteral("event"))) {
            eventObj = root.value(QStringLiteral("event")).toObject();
            if (root.contains(QStringLiteral("delay_ms"))) {
                delayMs = root.value(QStringLiteral("delay_ms")).toInteger();
            } else if (root.contains(QStringLiteral("time_ms"))) {
                qint64 t = root.value(QStringLiteral("time_ms")).toInteger();
                delayMs = std::max<qint64>(0, t - lastTimeMs);
                lastTimeMs = t;
            }
        } else {
            // Direktes Event-JSON
            eventObj = root;
        }

        auto ev = deserializeEvent(eventObj);
        if (ev.has_value()) {
            if (std::holds_alternative<PlanReady>(*ev)) {
                m_cachedUpdates = std::get<PlanReady>(*ev).ops;
            }
            m_items.append(ReplayItem{delayMs, *ev});
        }
    }

    return !m_items.isEmpty();
}

void ReplayBackend::setSpeed(double speed) {
    m_speed = speed;
}

void ReplayBackend::start() {
    if (m_items.isEmpty()) {
        emit replayFinished();
        return;
    }
    m_currentIndex = 0;
    m_running = true;
    m_waitingForAnswer = false;
    processNextItem();
}

void ReplayBackend::pause() {
    m_running = false;
    m_timer->stop();
}

void ReplayBackend::resume() {
    if (!m_running && !m_waitingForAnswer && m_currentIndex < m_items.size()) {
        m_running = true;
        processNextItem();
    }
}

void ReplayBackend::stop() {
    m_running = false;
    m_waitingForAnswer = false;
    m_timer->stop();
    m_currentIndex = 0;
}

void ReplayBackend::processNextItem() {
    if (!m_running || m_waitingForAnswer) {
        return;
    }

    if (m_currentIndex >= m_items.size()) {
        m_running = false;
        emit replayFinished();
        return;
    }

    const ReplayItem &item = m_items.at(m_currentIndex);
    m_currentIndex++;

    // Event emittieren
    emit eventEmitted(item.event);

    // Falls es eine Frage war, pausieren bis answerQuestion() gerufen wird
    if (std::holds_alternative<Question>(item.event)) {
        m_waitingForAnswer = true;
        m_pendingQuestionId = std::get<Question>(item.event).id;
        emit questionEncountered(m_pendingQuestionId);
        return;
    }

    if (m_currentIndex < m_items.size()) {
        qint64 nextDelay = m_items.at(m_currentIndex).delayMs;
        if (m_speed > 0.0) {
            nextDelay = static_cast<qint64>(std::round(nextDelay / m_speed));
        } else {
            nextDelay = 0;
        }
        m_timer->start(static_cast<int>(std::max<qint64>(1, nextDelay)));
    } else {
        m_running = false;
        emit replayFinished();
    }
}

void ReplayBackend::runSynchronously(const std::function<void(const Event&)> &callback) {
    for (const auto &item : m_items) {
        emit eventEmitted(item.event);
        if (callback) {
            callback(item.event);
        }
    }
}

Capabilities ReplayBackend::capabilities() const {
    Capabilities cap;
    cap.partialUpgrade = true;
    cap.historyUndo = true;
    cap.changelogs = true;
    cap.securityFlag = true;
    cap.autoremove = true;
    return cap;
}

void ReplayBackend::refreshMetadata() {
    start();
}

void ReplayBackend::planUpgradeAll(const UpgradeOptions &) {
    if (!m_running) {
        start();
    }
}

void ReplayBackend::planInstall(const QStringList &) {}
void ReplayBackend::planRemove(const QStringList &) {}
void ReplayBackend::commit() {
    if (!m_running && m_currentIndex > 0) {
        resume();
    }
}

void ReplayBackend::cancel() {
    lut::PhaseChanged ev{lut::Phase::Cancelled, QStringLiteral("Durch Benutzer abgebrochen"), false};
    emit eventEmitted(ev);
    lut::TransactionDone done{lut::Result::Cancelled, QStringLiteral("Transaktion abgebrochen"), false, {}, 0};
    emit eventEmitted(done);
    stop();
}

void ReplayBackend::answerQuestion(const QString &id, const QJsonObject &) {
    if (m_waitingForAnswer && m_pendingQuestionId == id) {
        m_waitingForAnswer = false;
        m_pendingQuestionId.clear();
        if (m_running) {
            processNextItem();
        }
    }
}

QList<PackageOp> ReplayBackend::availableUpdates() {
    return m_cachedUpdates;
}

QList<InstalledPackage> ReplayBackend::installedPackages(const QString &) {
    return {};
}

QList<ChangelogEntry> ReplayBackend::changelog(const QString &) {
    return {};
}

QList<HistoryEntry> ReplayBackend::history(int) {
    return {};
}

} // namespace lut
