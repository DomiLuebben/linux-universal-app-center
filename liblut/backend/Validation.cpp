#include "Validation.h"
#include <QRegularExpression>

namespace lut {

static const QRegularExpression s_pkgNameRegex(
    QStringLiteral("^[a-zA-Z0-9][a-zA-Z0-9._+-]{0,127}$")
);

bool Validation::isValidPackageName(const QString &name) {
    if (name.isEmpty() || name.length() > 128) {
        return false;
    }
    // Keine führenden Striche (Optionen wie -rf)
    if (name.startsWith(QLatin1Char('-'))) {
        return false;
    }
    // Keine Pfad-Elemente
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        return false;
    }

    return s_pkgNameRegex.match(name).hasMatch();
}

bool Validation::areValidPackageNames(const QStringList &names) {
    if (names.isEmpty()) {
        return false;
    }
    for (const auto &name : names) {
        if (!isValidPackageName(name)) {
            return false;
        }
    }
    return true;
}

} // namespace lut
