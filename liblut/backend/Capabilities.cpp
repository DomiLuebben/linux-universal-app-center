#include "Capabilities.h"

namespace lut {

QJsonObject SourceCapabilities::toJson() const {
    QJsonObject obj;
    obj[QStringLiteral("source")] = source;
    obj[QStringLiteral("available")] = available;
    obj[QStringLiteral("install")] = install;
    obj[QStringLiteral("remove")] = remove;
    obj[QStringLiteral("systemScope")] = systemScope;
    obj[QStringLiteral("userScope")] = userScope;
    obj[QStringLiteral("boundRevision")] = boundRevision;
    obj[QStringLiteral("installRequiresFullUpgrade")] = installRequiresFullUpgrade;
    return obj;
}

SourceCapabilities SourceCapabilities::fromJson(const QJsonObject &obj) {
    SourceCapabilities sc;
    sc.source = obj.value(QStringLiteral("source")).toString();
    sc.available = obj.value(QStringLiteral("available")).toBool(false);
    sc.install = obj.value(QStringLiteral("install")).toBool(false);
    sc.remove = obj.value(QStringLiteral("remove")).toBool(false);
    sc.systemScope = obj.value(QStringLiteral("systemScope")).toBool(true);
    sc.userScope = obj.value(QStringLiteral("userScope")).toBool(false);
    sc.boundRevision = obj.value(QStringLiteral("boundRevision")).toBool(false);
    sc.installRequiresFullUpgrade = obj.value(QStringLiteral("installRequiresFullUpgrade")).toBool(false);
    return sc;
}

} // namespace lut
