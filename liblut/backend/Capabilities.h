#pragma once

#include <QString>
#include <QList>
#include <QJsonObject>

namespace lut {

// Fähigkeiten je Paketquelle (Abschnitt 4.2)
struct SourceCapabilities {
    QString source;                         // "alpm", "dnf5", "apt", "flatpak", "snap"
    bool available = false;                  // Werkzeug vorhanden und benutzbar
    bool install = false;                    // Installation unterstützt
    bool remove = false;                     // Entfernen unterstützt
    bool systemScope = true;                 // Systemweite Installation
    bool userScope = false;                  // Benutzerinstallation
    bool boundRevision = false;              // Ausführung an geprüfte Fassung (Commit/Revision) gebunden
    bool installRequiresFullUpgrade = false; // Nur ALPM auf Arch/CachyOS true; Flatpak/Snap strikt false!

    bool operator==(const SourceCapabilities &other) const = default;
    QJsonObject toJson() const;
    static SourceCapabilities fromJson(const QJsonObject &obj);
};

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

    // Mehrquellenfähigkeiten (Abschnitt 4.2)
    QList<SourceCapabilities> sources;

    SourceCapabilities sourceCapabilities(const QString &sourceName) const {
        for (const auto &s : sources) {
            if (s.source == sourceName) return s;
        }
        return {};
    }

    void setSourceCapabilities(const SourceCapabilities &sc) {
        for (int i = 0; i < sources.size(); ++i) {
            if (sources[i].source == sc.source) {
                sources[i] = sc;
                return;
            }
        }
        sources.append(sc);
    }
};

} // namespace lut
