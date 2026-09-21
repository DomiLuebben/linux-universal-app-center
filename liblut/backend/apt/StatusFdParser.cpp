#include "StatusFdParser.h"
#include <QRegularExpression>

namespace lut {

std::optional<StatusFdItem> StatusFdParser::parseLine(const QString &rawLine) {
    QString line = rawLine.trimmed();
    if (line.isEmpty()) {
        return std::nullopt;
    }

    QStringList parts = line.split(QLatin1Char(':'));
    if (parts.size() < 3) {
        return std::nullopt;
    }

    const QString prefix = parts[0];
    StatusFdItem item;

    if (prefix == QLatin1String("dlstatus") && parts.size() >= 4) {
        item.type = StatusFdItem::Type::DlStatus;
        item.target = parts[1];
        item.percent = parts[2].toDouble();
        item.message = parts.mid(3).join(QLatin1Char(':'));
        return item;
    }

    if (prefix == QLatin1String("pmstatus") && parts.size() >= 4) {
        item.type = StatusFdItem::Type::PmStatus;
        item.target = parts[1];
        item.percent = parts[2].toDouble();
        item.message = parts.mid(3).join(QLatin1Char(':'));
        item.isTrigger = item.message.contains(QLatin1String("Processing triggers"), Qt::CaseInsensitive) ||
                         item.message.contains(QLatin1String("Trigger"), Qt::CaseInsensitive);
        return item;
    }

    if (prefix == QLatin1String("pmconffile")) {
        item.type = StatusFdItem::Type::PmConffile;
        item.target = parts[1];
        // Format: pmconffile:<file>:'<old>' '<new>'
        static const QRegularExpression re(QStringLiteral("'([^']*)'\\s+'([^']*)'"));
        QString rest = parts.mid(2).join(QLatin1Char(':'));
        auto match = re.match(rest);
        if (match.hasMatch()) {
            item.oldPath = match.captured(1);
            item.newPath = match.captured(2);
        }
        return item;
    }

    if (prefix == QLatin1String("pmerror") && parts.size() >= 4) {
        item.type = StatusFdItem::Type::PmError;
        item.target = parts[1];
        item.percent = parts[2].toDouble();
        item.message = parts.mid(3).join(QLatin1Char(':'));
        return item;
    }

    if (prefix == QLatin1String("media-change")) {
        item.type = StatusFdItem::Type::MediaChange;
        item.target = parts[1];
        item.message = parts.mid(2).join(QLatin1Char(':'));
        return item;
    }

    return std::nullopt;
}

std::optional<Event> StatusFdParser::toEvent(const StatusFdItem &item) {
    switch (item.type) {
        case StatusFdItem::Type::DlStatus: {
            DownloadThroughput ev;
            ev.totalDone = static_cast<qint64>(item.percent * 1000);
            ev.totalTotal = 100000;
            return ev;
        }
        case StatusFdItem::Type::PmStatus: {
            if (item.isTrigger) {
                ScriptletStarted sc;
                sc.pkgId = item.target;
                sc.scriptletName = item.message;
                return sc;
            }
            ItemProgress pr;
            pr.pkgId = item.target;
            pr.done = static_cast<qint64>(item.percent);
            pr.total = 100;
            return pr;
        }
        case StatusFdItem::Type::PmConffile: {
            Question q;
            q.id = QStringLiteral("conffile-%1").arg(item.target);
            q.kind = QuestionKind::ConffilePrompt;
            QJsonObject payload;
            payload[QStringLiteral("file")] = item.target;
            payload[QStringLiteral("oldFile")] = item.oldPath;
            payload[QStringLiteral("newFile")] = item.newPath;
            q.payload = payload;
            return q;
        }
        case StatusFdItem::Type::PmError: {
            LogLine log;
            log.level = LogLevel::Error;
            log.source = QStringLiteral("apt");
            log.text = QStringLiteral("%1: %2").arg(item.target, item.message);
            return log;
        }
        case StatusFdItem::Type::MediaChange:
        case StatusFdItem::Type::Unknown:
            break;
    }
    return std::nullopt;
}

} // namespace lut
