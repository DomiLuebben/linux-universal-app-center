#pragma once

#include <QtGlobal>
#include "liblut/protocol/events.h"

namespace lut {

struct PhaseWeights {
    double refresh = 0.02;
    double resolve = 0.03;
    double download = 0.40;
    double verify = 0.02;
    double commit = 0.3975;
    double post = 0.1325;

    double total() const {
        return refresh + resolve + download + verify + commit + post;
    }
};

PhaseWeights calculateWeights(qint64 downloadBytes, qint64 installBytes, double kFactor = 0.35);
double phaseBaseProgress(Phase phase, const PhaseWeights &weights);
double phaseWeight(Phase phase, const PhaseWeights &weights);

} // namespace lut
