#pragma once
#include <QMap>
#include <limits>
#include <QString>
#include <QStringList>

namespace lut {
struct AptPackageMetadata {
    QString name, version, arch, summary;
    qint64 downloadSize = -1;
    qint64 installedSize = -1;
};
inline QList<AptPackageMetadata> parseAptMetadata(const QString &text) {
    QList<AptPackageMetadata> result;
    QMap<QString, QString> fields;
    const auto flush = [&] {
        if (!fields.contains(QStringLiteral("Package"))) { fields.clear(); return; }
        AptPackageMetadata item;
        item.name = fields.value(QStringLiteral("Package"));
        item.version = fields.value(QStringLiteral("Version"));
        item.arch = fields.value(QStringLiteral("Architecture"));
        item.summary = fields.value(QStringLiteral("Description"));
        bool ok = false;
        auto size = fields.value(QStringLiteral("Size")).toLongLong(&ok);
        if (ok && size >= 0) item.downloadSize = size;
        size = fields.value(QStringLiteral("Installed-Size")).toLongLong(&ok);
        if (ok && size >= 0 && size <= std::numeric_limits<qint64>::max() / 1024) item.installedSize = size * 1024;
        result.append(item); fields.clear();
    };
    for (const auto &line : text.split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty()) { flush(); continue; }
        if (line.startsWith(QLatin1Char(' '))) continue;
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon > 0) fields.insert(line.left(colon), line.mid(colon + 1).trimmed());
    }
    flush();
    return result;
}
}
