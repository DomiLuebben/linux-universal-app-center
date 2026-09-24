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

static const QRegularExpression s_flatpakIdRegex(
    QStringLiteral("^[a-zA-Z_][a-zA-Z0-9_-]*(\\.[a-zA-Z_][a-zA-Z0-9_-]*)+$")
);

static const QRegularExpression s_snapNameRegex(
    QStringLiteral("^[a-z0-9](-?[a-z0-9]+)*$")
);

bool Validation::isValidFlatpakRemote(const QString &remote) {
    if (remote.isEmpty() || remote.length() > 128) {
        return false;
    }
    if (remote.startsWith(QLatin1Char('-'))) {
        return false;
    }
    static const QRegularExpression regex(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
    return regex.match(remote).hasMatch();
}

bool Validation::isValidFlatpakAppId(const QString &id) {
    if (id.isEmpty() || id.length() > 255) {
        return false;
    }
    if (id.startsWith(QLatin1Char('-')) || id.endsWith(QLatin1Char('.'))) {
        return false;
    }
    if (id.contains(QLatin1Char('/')) || id.contains(QLatin1Char('\\'))) {
        return false;
    }
    return s_flatpakIdRegex.match(id).hasMatch();
}

bool Validation::isValidSnapName(const QString &name) {
    if (name.isEmpty() || name.length() > 64) {
        return false;
    }
    if (name.startsWith(QLatin1Char('-')) || name.endsWith(QLatin1Char('-'))) {
        return false;
    }
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        return false;
    }
    return s_snapNameRegex.match(name).hasMatch();
}

} // namespace lut
