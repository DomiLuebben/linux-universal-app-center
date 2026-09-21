#pragma once

#include <QString>
#include <QStringList>

namespace lut {

enum class DistroFamily {
    Fedora, // dnf5
    Debian, // apt
    Arch,   // pacman / alpm
    Unknown
};

class DistroDetect {
public:
    static DistroFamily detectFamily(const QString &osReleasePath = QStringLiteral("/etc/os-release"));
    static QString familyToString(DistroFamily family);
    static QString prettyName(const QString &osReleasePath = QStringLiteral("/etc/os-release"));
};

} // namespace lut
