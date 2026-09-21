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
};

} // namespace lut
