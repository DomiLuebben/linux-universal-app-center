#pragma once

#include <QtGlobal>
#include <QString>
#include <QList>
#include <optional>

namespace lut {

class EtaCalculator {
public:
    explicit EtaCalculator(double alpha = 0.2, int requiredSamples = 10, double maxRelativeStdDev = 0.25);

    void reset();
    void addThroughputSample(qint64 bytesPerSecond);

    bool hasValidEta() const;
    double smoothedThroughput() const;
    double relativeStandardDeviation() const;

    std::optional<qint64> estimateRemainingSeconds(qint64 remainingBytes) const;
    QString formatRemainingTime(qint64 seconds) const;

private:
    double m_alpha;
    int m_requiredSamples;
    double m_maxRelativeStdDev;

    double m_ewma = 0.0;
    bool m_hasEwma = false;
    QList<double> m_samples;
};

} // namespace lut
