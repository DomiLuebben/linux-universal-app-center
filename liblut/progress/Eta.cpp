#include "Eta.h"
#include <cmath>
#include <numeric>

namespace lut {

EtaCalculator::EtaCalculator(double alpha, int requiredSamples, double maxRelativeStdDev)
    : m_alpha(alpha),
      m_requiredSamples(requiredSamples),
      m_maxRelativeStdDev(maxRelativeStdDev) {}

void EtaCalculator::reset() {
    m_ewma = 0.0;
    m_hasEwma = false;
    m_samples.clear();
}

void EtaCalculator::addThroughputSample(qint64 bytesPerSecond) {
    if (bytesPerSecond < 0) return;
    double current = static_cast<double>(bytesPerSecond);

    if (!m_hasEwma) {
        m_ewma = current;
        m_hasEwma = true;
    } else {
        m_ewma = (m_alpha * current) + ((1.0 - m_alpha) * m_ewma);
    }

    m_samples.append(current);
    while (m_samples.size() > m_requiredSamples) {
        m_samples.removeFirst();
    }
}

double EtaCalculator::smoothedThroughput() const {
    return m_ewma;
}

double EtaCalculator::relativeStandardDeviation() const {
    if (m_samples.size() < m_requiredSamples) {
        return 1.0; // Noch nicht genügend Samples
    }

    double sum = std::accumulate(m_samples.begin(), m_samples.end(), 0.0);
    double mean = sum / m_samples.size();
    if (mean <= 0.0) {
        return 1.0;
    }

    double sqDiffSum = 0.0;
    for (double x : m_samples) {
        double diff = x - mean;
        sqDiffSum += diff * diff;
    }

    double variance = sqDiffSum / m_samples.size();
    double stdDev = std::sqrt(variance);
    return stdDev / mean;
}

bool EtaCalculator::hasValidEta() const {
    if (m_samples.size() < m_requiredSamples) {
        return false;
    }
    if (m_ewma <= 1024.0) { // Unter 1 KB/s ist Schätzung instabil
        return false;
    }
    return relativeStandardDeviation() < m_maxRelativeStdDev;
}

std::optional<qint64> EtaCalculator::estimateRemainingSeconds(qint64 remainingBytes) const {
    if (!hasValidEta() || remainingBytes <= 0) {
        return std::nullopt;
    }
    double sec = static_cast<double>(remainingBytes) / m_ewma;
    return static_cast<qint64>(std::round(sec));
}

QString EtaCalculator::formatRemainingTime(qint64 seconds) const {
    if (seconds <= 0) {
        return QString();
    }
    if (seconds < 15) {
        return QStringLiteral("noch wenige Sekunden");
    }
    if (seconds < 60) {
        // Auf nächste 15s runden
        qint64 rounded = ((seconds + 7) / 15) * 15;
        if (rounded >= 60) {
            return QStringLiteral("noch etwa 1 Minute");
        }
        return QStringLiteral("noch etwa %1 Sekunden").arg(rounded);
    }
    if (seconds < 3600) {
        // Auf Minuten runden
        qint64 minutes = (seconds + 30) / 60;
        if (minutes <= 1) {
            return QStringLiteral("noch etwa 1 Minute");
        }
        return QStringLiteral("noch etwa %1 Minuten").arg(minutes);
    }

    qint64 hours = (seconds + 1800) / 3600;
    if (hours <= 1) {
        return QStringLiteral("noch etwa 1 Stunde");
    }
    return QStringLiteral("noch etwa %1 Stunden").arg(hours);
}

} // namespace lut
