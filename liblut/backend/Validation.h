#pragma once

#include <QString>
#include <QStringList>

namespace lut {

class Validation {
public:
    // Whitelist: ^[a-zA-Z0-9][a-zA-Z0-9._+-]{0,127}$
    static bool isValidPackageName(const QString &name);
    static bool areValidPackageNames(const QStringList &names);
};

} // namespace lut
