#pragma once

#include <QString>

namespace lut {

struct Capabilities {
    bool partialUpgrade = true; // Auf Arch/Pacman verboten (immer false)
    bool downgrade = false;
    bool historyUndo = false;
    bool changelogs = false;
    bool securityFlag = false;
    bool offlineUpdate = false;
    bool autoremove = true;
    bool parallelDownloads = true;
    bool degraded = false;

    // Store & Transaktion Version 2
    bool catalogQuery = false;
    bool install = false;
    bool remove = false;
    bool installRequiresFullUpgrade = false; // Auf Arch/CachyOS true
    bool typedPackageTargets = true;
    bool transactionReattach = true;
    int protocolVersion = 2;
};

} // namespace lut
