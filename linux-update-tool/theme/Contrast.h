#pragma once

#include <QColor>

namespace lut {

double relativeLuminance(const QColor &color);
double contrastRatio(const QColor &c1, const QColor &c2);
QColor ensureContrast(const QColor &fg, const QColor &bg, double minRatio = 4.5);
QColor pickOn(const QColor &bg);
QColor mixLinearSrgb(const QColor &c1, const QColor &c2, double weightC2);
QColor elevate(const QColor &base, int steps);

} // namespace lut
