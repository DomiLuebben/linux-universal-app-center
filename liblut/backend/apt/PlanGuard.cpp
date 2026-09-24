#include "PlanGuard.h"
#include <QJsonObject>
#include <QSet>
#include <QRegularExpression>
namespace lut {
bool verifyAptHook(const QByteArray &protocol, const QJsonArray &expected,
                   const std::function<QString(const QString &)> &sha256, QString *error) {
    const auto reject = [error](const QString &message) { if (error) *error = message; return false; };
    if (!protocol.startsWith("VERSION 3\n")) return reject(QStringLiteral("APT-Hook-Protokoll 3 fehlt."));
    const auto separator = protocol.indexOf("\n\n");
    if (separator < 0 || expected.isEmpty()) return reject(QStringLiteral("Leerer oder ungültiger Paketplan."));
    QHash<QString, QJsonObject> planned;
    for (const auto &value : expected) {
        const auto op = value.toObject();
        const QString key = op.value("name").toString() + QLatin1Char(':') + op.value("arch").toString();
        if (planned.contains(key)) return reject(QStringLiteral("Doppeltes Paketziel."));
        planned.insert(key, op);
    }
    QSet<QString> changed, configured;
    for (const auto &raw : protocol.mid(separator + 2).split('\n')) {
        if (raw.isEmpty()) continue;
        const auto fields = QString::fromUtf8(raw).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.size() != 9) return reject(QStringLiteral("Unbekannte APT-Paketoperation."));
        const QString action = fields[8];
        const bool remove = action == QLatin1String("**REMOVE**");
        const bool configure = action == QLatin1String("**CONFIGURE**");
        const QString key = fields[0] + QLatin1Char(':') + (remove ? fields[2] : fields[6]);
        if (!planned.contains(key)) return reject(QStringLiteral("Zusätzliche Operation: %1").arg(key));
        const auto op = planned.value(key);
        const QString oldVersion = op.value("oldVersion").toString();
        const QString newVersion = op.value("newVersion").toString();
        if (fields[1] != (oldVersion.isEmpty() ? QStringLiteral("-") : oldVersion)
            || fields[2] != (oldVersion.isEmpty() ? QStringLiteral("-") : op.value("oldArch").toString())
            || fields[5] != (newVersion.isEmpty() ? QStringLiteral("-") : newVersion)
            || remove != op.value("remove").toBool())
            return reject(QStringLiteral("Paketplan geändert: %1").arg(key));
        if (configure) { configured.insert(key); continue; }
        if (changed.contains(key)) return reject(QStringLiteral("Doppelte Paketoperation: %1").arg(key));
        changed.insert(key);
        if (!remove) {
            const QString digest = op.value("sha256").toString();
            if (!action.startsWith(QLatin1Char('/')) || digest.size() != 64 || sha256(action) != digest)
                return reject(QStringLiteral("Paketinhalt stimmt nicht mit der Vorschau überein: %1").arg(key));
        }
    }
    if (changed.size() != planned.size()) return reject(QStringLiteral("Operationen des bestätigten Plans fehlen."));
    for (const auto &key : configured) if (!changed.contains(key)) return reject(QStringLiteral("Ungeplante Konfiguration."));
    return true;
}
}
