#pragma once

#include <QString>
#include <QStringList>

namespace lut {

class Validation {
public:
    // Whitelist: ^[a-zA-Z0-9][a-zA-Z0-9._+-]{0,127}$
    static bool isValidPackageName(const QString &name);
    static bool areValidPackageNames(const QStringList &names);

    // Flatpak Application-ID (Reverse-DNS, min. 2 Segmente, z.B. org.kde.kate)
    static bool isValidFlatpakAppId(const QString &id);

    // Flatpak-Remote-Name (z.B. flathub, talk-origin). Geht als Argument in einen
    // Prozessaufruf und wird deshalb vorher geprüft.
    static bool isValidFlatpakRemote(const QString &remote);

    // Snap-Name (Kleinbuchstaben, Ziffern, Bindestriche, 1-64 Zeichen, z.B. vlc)
    static bool isValidSnapName(const QString &name);
};

} // namespace lut
