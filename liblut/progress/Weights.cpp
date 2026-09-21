#include "Weights.h"
#include <algorithm>

namespace lut {

PhaseWeights calculateWeights(qint64 downloadBytes, qint64 installBytes, double kFactor) {
    PhaseWeights w;
    w.refresh = 0.02;
    w.resolve = 0.03;
    w.verify = 0.02;

    double dBytes = static_cast<double>(std::max<qint64>(0, downloadBytes));
    double iBytes = static_cast<double>(std::max<qint64>(0, installBytes));

    double denom = dBytes + (iBytes * kFactor);
    if (denom <= 0.0) {
        w.download = 0.05;
    } else {
        double ratio = dBytes / denom;
        w.download = std::clamp(ratio, 0.05, 0.75);
    }

    double fixedSum = w.refresh + w.resolve + w.download + w.verify;
    double rest = std::max(0.0, 1.0 - fixedSum);

    w.commit = rest * 0.75;
    w.post = rest * 0.25;

    // Rundungsdifferenzen ausgleichen, sodass Summe exakt 1.0 ergibt
    double sum = w.refresh + w.resolve + w.download + w.verify + w.commit + w.post;
    double diff = 1.0 - sum;
    w.commit += diff;

    return w;
}

double phaseWeight(Phase phase, const PhaseWeights &weights) {
    switch (phase) {
        case Phase::RefreshMetadata: return weights.refresh;
        case Phase::Resolve: return weights.resolve;
        case Phase::Download: return weights.download;
        case Phase::Verify: return weights.verify;
        case Phase::TestTransaction: return 0.01; // Kleiner Vorab-Test
        case Phase::Commit: return weights.commit;
        case Phase::PostTransaction: return weights.post;
        case Phase::Idle:
        case Phase::Cleanup:
        case Phase::Finished:
        case Phase::Failed:
        case Phase::Cancelled:
            return 0.0;
    }
    return 0.0;
}

double phaseBaseProgress(Phase phase, const PhaseWeights &weights) {
    switch (phase) {
        case Phase::Idle:
        case Phase::RefreshMetadata:
            return 0.0;
        case Phase::Resolve:
            return weights.refresh;
        case Phase::Download:
            return weights.refresh + weights.resolve;
        case Phase::Verify:
            return weights.refresh + weights.resolve + weights.download;
        case Phase::TestTransaction:
        case Phase::Commit:
            return weights.refresh + weights.resolve + weights.download + weights.verify;
        case Phase::PostTransaction:
            return weights.refresh + weights.resolve + weights.download + weights.verify + weights.commit;
        case Phase::Cleanup:
        case Phase::Finished:
            return 1.0;
        case Phase::Failed:
        case Phase::Cancelled:
            return 0.0; // Wird vom Model auf aktuellem Stand gehalten
    }
    return 0.0;
}

} // namespace lut
