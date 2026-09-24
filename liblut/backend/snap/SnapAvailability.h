#pragma once

#include <QString>
#include <functional>

namespace lut {

class SnapAvailability {
public:
    struct CheckResult {
        bool available = false;
        QString reason;
        QString daemonVersion;
    };

    using SocketProber = std::function<bool(const QString &socketPath, QString &errOut, QString &responseOut)>;

    static CheckResult check(const QString &binaryPath = QStringLiteral("/usr/bin/snap"),
                             const QString &socketPath = QStringLiteral("/run/snapd.socket"),
                             SocketProber prober = nullptr);

    static bool isSnapAvailable();
};

} // namespace lut
