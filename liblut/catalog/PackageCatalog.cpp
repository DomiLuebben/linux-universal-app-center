#include "PackageCatalog.h"
#include <QVersionNumber>

namespace lut {

QJsonObject AppRecord::toJson() const {
    QJsonObject obj;
    obj[QStringLiteral("appKey")] = appKey;
    obj[QStringLiteral("componentId")] = componentId;
    obj[QStringLiteral("name")] = name;
    obj[QStringLiteral("summary")] = summary;
    obj[QStringLiteral("description")] = description;
    obj[QStringLiteral("developer")] = developer;
    obj[QStringLiteral("license")] = license;
    obj[QStringLiteral("urlHomepage")] = urlHomepage;
    obj[QStringLiteral("urlBugtracker")] = urlBugtracker;
    obj[QStringLiteral("iconSource")] = iconSource;
    obj[QStringLiteral("screenshots")] = QJsonArray::fromStringList(screenshots);
    obj[QStringLiteral("categories")] = QJsonArray::fromStringList(categories);
    obj[QStringLiteral("keywords")] = QJsonArray::fromStringList(keywords);
    obj[QStringLiteral("launchableDesktopIds")] = QJsonArray::fromStringList(launchableDesktopIds);
    obj[QStringLiteral("origin")] = origin;
    obj[QStringLiteral("defaultPackageName")] = defaultPackageName;
    obj[QStringLiteral("packageNames")] = QJsonArray::fromStringList(packageNames);
    return obj;
}

AppRecord AppRecord::fromJson(const QJsonObject &obj) {
    AppRecord rec;
    rec.appKey = obj.value(QStringLiteral("appKey")).toString();
    rec.componentId = obj.value(QStringLiteral("componentId")).toString();
    rec.name = obj.value(QStringLiteral("name")).toString();
    rec.summary = obj.value(QStringLiteral("summary")).toString();
    rec.description = obj.value(QStringLiteral("description")).toString();
    rec.developer = obj.value(QStringLiteral("developer")).toString();
    rec.license = obj.value(QStringLiteral("license")).toString();
    rec.urlHomepage = obj.value(QStringLiteral("urlHomepage")).toString();
    rec.urlBugtracker = obj.value(QStringLiteral("urlBugtracker")).toString();
    rec.iconSource = obj.value(QStringLiteral("iconSource")).toString();
    const QJsonArray shots = obj.value(QStringLiteral("screenshots")).toArray();
    for (const auto &s : shots) rec.screenshots.append(s.toString());
    const QJsonArray cats = obj.value(QStringLiteral("categories")).toArray();
    for (const auto &c : cats) rec.categories.append(c.toString());
    const QJsonArray keys = obj.value(QStringLiteral("keywords")).toArray();
    for (const auto &k : keys) rec.keywords.append(k.toString());
    const QJsonArray launches = obj.value(QStringLiteral("launchableDesktopIds")).toArray();
    for (const auto &l : launches) rec.launchableDesktopIds.append(l.toString());
    rec.origin = obj.value(QStringLiteral("origin")).toString();
    rec.defaultPackageName = obj.value(QStringLiteral("defaultPackageName")).toString();
    const QJsonArray pkgs = obj.value(QStringLiteral("packageNames")).toArray();
    for (const auto &p : pkgs) rec.packageNames.append(p.toString());
    if (rec.packageNames.isEmpty() && !rec.defaultPackageName.isEmpty()) {
        rec.packageNames.append(rec.defaultPackageName);
    }
    return rec;
}

QString CatalogQueryResult::statusToString(Status s) {
    switch (s) {
        case Status::Success: return QStringLiteral("Success");
        case Status::NotFound: return QStringLiteral("NotFound");
        case Status::BackendError: return QStringLiteral("BackendError");
        case Status::CatalogMissing: return QStringLiteral("CatalogMissing");
        case Status::Loading: return QStringLiteral("Loading");
    }
    return QStringLiteral("BackendError");
}

CatalogQueryResult::Status CatalogQueryResult::statusFromString(const QString &str) {
    if (str == QLatin1String("Success")) return Status::Success;
    if (str == QLatin1String("NotFound")) return Status::NotFound;
    if (str == QLatin1String("CatalogMissing")) return Status::CatalogMissing;
    if (str == QLatin1String("Loading")) return Status::Loading;
    return Status::BackendError;
}

int PackageCatalog::compareVersions(const QString &v1, const QString &v2) const {
    const QVersionNumber n1 = QVersionNumber::fromString(v1);
    const QVersionNumber n2 = QVersionNumber::fromString(v2);
    if (!n1.isNull() && !n2.isNull()) {
        return QVersionNumber::compare(n1, n2);
    }
    return QString::compare(v1, v2);
}

} // namespace lut
