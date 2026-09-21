#include "Contrast.h"
#include <cmath>
#include <algorithm>

namespace lut {

static double linearize(double channel) {
    if (channel <= 0.04045) {
        return channel / 12.92;
    }
    return std::pow((channel + 0.055) / 1.055, 2.4);
}

static double delinearize(double channel) {
    if (channel <= 0.0031308) {
        return channel * 12.92;
    }
    return 1.055 * std::pow(channel, 1.0 / 2.4) - 0.055;
}

double relativeLuminance(const QColor &color) {
    double r = linearize(color.redF());
    double g = linearize(color.greenF());
    double b = linearize(color.blueF());
    return (0.2126 * r) + (0.7152 * g) + (0.0722 * b);
}

double contrastRatio(const QColor &c1, const QColor &c2) {
    double l1 = relativeLuminance(c1);
    double l2 = relativeLuminance(c2);
    if (l1 < l2) {
        std::swap(l1, l2);
    }
    return (l1 + 0.05) / (l2 + 0.05);
}

QColor pickOn(const QColor &bg) {
    double contrastBlack = contrastRatio(Qt::black, bg);
    double contrastWhite = contrastRatio(Qt::white, bg);
    return (contrastBlack > contrastWhite) ? QColor(Qt::black) : QColor(Qt::white);
}

QColor mixLinearSrgb(const QColor &c1, const QColor &c2, double weightC2) {
    double w2 = std::clamp(weightC2, 0.0, 1.0);
    double w1 = 1.0 - w2;

    double r = std::clamp(w1 * linearize(c1.redF()) + w2 * linearize(c2.redF()), 0.0, 1.0);
    double g = std::clamp(w1 * linearize(c1.greenF()) + w2 * linearize(c2.greenF()), 0.0, 1.0);
    double b = std::clamp(w1 * linearize(c1.blueF()) + w2 * linearize(c2.blueF()), 0.0, 1.0);

    return QColor::fromRgbF(
        static_cast<float>(std::clamp(delinearize(r), 0.0, 1.0)),
        static_cast<float>(std::clamp(delinearize(g), 0.0, 1.0)),
        static_cast<float>(std::clamp(delinearize(b), 0.0, 1.0)),
        static_cast<float>(w1 * c1.alphaF() + w2 * c2.alphaF())
    );
}

QColor elevate(const QColor &base, int steps) {
    bool dark = relativeLuminance(base) < 0.5;
    const QColor target = dark ? Qt::white : Qt::black;
    double factor = 0.04 * std::abs(steps);
    return mixLinearSrgb(base, target, factor);
}

QColor ensureContrast(const QColor &fg, const QColor &bg, double minRatio) {
    if (contrastRatio(fg, bg) >= minRatio) {
        return fg;
    }

    bool bgIsDark = relativeLuminance(bg) < 0.5;
    QColor target = bgIsDark ? Qt::white : Qt::black;

    // In 20 Schritten Richtung Ziel-Luminanz schieben
    QColor adjusted = fg;
    for (int i = 1; i <= 20; ++i) {
        adjusted = mixLinearSrgb(fg, target, i * 0.05);
        if (contrastRatio(adjusted, bg) >= minRatio) {
            return adjusted;
        }
    }

    return target;
}

} // namespace lut
