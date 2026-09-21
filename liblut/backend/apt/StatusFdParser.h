#pragma once

#include <QString>
#include <optional>
#include "liblut/protocol/events.h"

namespace lut {

struct StatusFdItem {
    enum class Type {
        DlStatus,
        PmStatus,
        PmConffile,
        PmError,
        MediaChange,
        Unknown
    };

    Type type = Type::Unknown;
    QString target;
    double percent = 0.0;
    QString message;
    QString oldPath;
    QString newPath;
    bool isTrigger = false;
};

class StatusFdParser {
public:
    static std::optional<StatusFdItem> parseLine(const QString &line);
    static std::optional<Event> toEvent(const StatusFdItem &item);
};

} // namespace lut
