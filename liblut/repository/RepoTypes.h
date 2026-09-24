#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include "liblut/detect/DistroDetect.h"

namespace lut {

struct RepoEntry {
    QString id;             // Unique identifier, e.g. "multilib", "chaotic-aur", "rpmfusion-free", "contrib"
    QString name;           // Display name
    QString url;            // Server, mirrorlist, metalink, or suite/components
    bool enabled = true;
    bool isSystem = false;  // Protected base system repo (e.g. core, extra, fedora, main)
    QString backend;        // "pacman", "dnf5", "apt"
    QString description;    // Friendly summary
    QString filePath;       // Source config file path

    QJsonObject toJson() const {
        QJsonObject obj;
        obj[QStringLiteral("id")] = id;
        obj[QStringLiteral("name")] = name.isEmpty() ? id : name;
        obj[QStringLiteral("url")] = url;
        obj[QStringLiteral("enabled")] = enabled;
        obj[QStringLiteral("isSystem")] = isSystem;
        obj[QStringLiteral("backend")] = backend;
        obj[QStringLiteral("description")] = description;
        obj[QStringLiteral("filePath")] = filePath;
        return obj;
    }

    static RepoEntry fromJson(const QJsonObject &obj) {
        RepoEntry entry;
        entry.id = obj.value(QStringLiteral("id")).toString();
        entry.name = obj.value(QStringLiteral("name")).toString(entry.id);
        entry.url = obj.value(QStringLiteral("url")).toString();
        entry.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
        entry.isSystem = obj.value(QStringLiteral("isSystem")).toBool(false);
        entry.backend = obj.value(QStringLiteral("backend")).toString();
        entry.description = obj.value(QStringLiteral("description")).toString();
        entry.filePath = obj.value(QStringLiteral("filePath")).toString();
        return entry;
    }
};

struct RepoPreset {
    QString id;
    QString name;
    QString description;
    QString backend;        // "pacman", "dnf5", "apt"
    QString defaultUrl;
    bool isAdded = false;   // Set dynamically based on existing repos
    bool thirdParty = false; // gehört in „Drittanbieter-Quellen“, Einschalten nur mit Risikodialog
    QString riskNotice;      // Text aus 2.2, leer bei offiziellen Quellen

    // Einrichtung über das offizielle Paket des Anbieters statt über eine
    // selbst geschriebene Quelldatei: nur so kommen die Signaturschlüssel mit.
    // Ohne Schlüssel scheitert jede Installation an der Signaturprüfung.
    QStringList setupPackages;     // Paketnamen oder HTTPS-URLs für "dnf5 install"
    QString setupRepoFromPath;     // "id,url" für den ersten Bezug (nur Terra)
    QString requiresPreset;        // Unterquelle, die eine andere voraussetzt

    QJsonObject toJson() const {
        QJsonObject obj;
        obj[QStringLiteral("id")] = id;
        obj[QStringLiteral("name")] = name;
        obj[QStringLiteral("description")] = description;
        obj[QStringLiteral("backend")] = backend;
        obj[QStringLiteral("defaultUrl")] = defaultUrl;
        obj[QStringLiteral("isAdded")] = isAdded;
        obj[QStringLiteral("thirdParty")] = thirdParty;
        obj[QStringLiteral("riskNotice")] = riskNotice;
        obj[QStringLiteral("installsPackages")] = !setupPackages.isEmpty();
        obj[QStringLiteral("requiresPreset")] = requiresPreset;
        return obj;
    }

    static RepoPreset fromJson(const QJsonObject &obj) {
        RepoPreset preset;
        preset.id = obj.value(QStringLiteral("id")).toString();
        preset.name = obj.value(QStringLiteral("name")).toString();
        preset.description = obj.value(QStringLiteral("description")).toString();
        preset.backend = obj.value(QStringLiteral("backend")).toString();
        preset.defaultUrl = obj.value(QStringLiteral("defaultUrl")).toString();
        preset.isAdded = obj.value(QStringLiteral("isAdded")).toBool(false);
        preset.thirdParty = obj.value(QStringLiteral("thirdParty")).toBool(false);
        preset.riskNotice = obj.value(QStringLiteral("riskNotice")).toString();
        preset.requiresPreset = obj.value(QStringLiteral("requiresPreset")).toString();
        return preset;
    }
};

} // namespace lut
