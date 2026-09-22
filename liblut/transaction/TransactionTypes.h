#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QVariantMap>
#include <optional>
#include "liblut/protocol/events.h"

namespace lut {

// PackageRef: Backend, Repository-ID, Paketname, Architektur, native Version; separate Felder
struct PackageRef {
    QString backend;    // "alpm", "dnf5", "apt"
    QString repoId;     // z.B. "extra", "core", "fedora", "main"
    QString name;       // Paketname
    QString arch;       // Architektur, z.B. "x86_64", "any", "amd64"
    QString version;    // native Version

    bool operator==(const PackageRef &other) const = default;
    bool isValid() const;
    QJsonObject toJson() const;
    static PackageRef fromJson(const QJsonObject &obj);
};

// PackageOffer: Paketreferenzen eines Angebots, native Priorität, Kandidatenstatus, Größen optional, Verfügbarkeit
struct PackageOffer {
    QList<PackageRef> packages;
    int priority = 0;
    bool isCandidate = true;
    std::optional<qint64> downloadSize;
    std::optional<qint64> installedSize;
    bool available = true;
    QString unavailabilityReason;

    bool operator==(const PackageOffer &other) const = default;
    QJsonObject toJson() const;
    static PackageOffer fromJson(const QJsonObject &obj);
};

// InstalledState: Tatsächlich installierte Paketreferenzen, vollständig/teilweise installiert, Herkunft, startbare Desktop-IDs, Bestandsrevision
struct InstalledState {
    QList<PackageRef> installedPackages;
    bool isFullyInstalled = false;
    bool isPartiallyInstalled = false;
    QString origin;
    QStringList launchableDesktopIds;
    quint64 inventoryRevision = 0;

    bool operator==(const InstalledState &other) const = default;
    QJsonObject toJson() const;
    static InstalledState fromJson(const QJsonObject &obj);
};

// AppActionState: Aus Bestand, Fähigkeiten und laufender Transaktion abgeleitet
enum class AppActionState {
    Available,                  // Verfügbar, nicht installiert -> "Installieren"
    Installed,                  // Installiert und startbar -> "Öffnen" / "Entfernen"
    InstalledNoLaunch,          // Installiert ohne startbaren Desktop-Eintrag -> "Installiert" (deaktiviert) / "Entfernen"
    UpdateAvailable,            // Update verfügbar -> "Öffnen" / "Aktualisieren"
    PartiallyInstalled,         // Teilweise installiert -> "Prüfen / Vervollständigen"
    MissingSource,              // Installiert, aber Quelle fehlt -> "Öffnen"
    Unavailable,                // Nicht verfügbar -> "Nicht verfügbar"
    ActionUnsupported,          // Backend unterstützt Aktion nicht
    PreparingPlan,              // Plan wird berechnet -> "Wird vorbereitet …"
    AwaitingConfirmation,       // Vorschau wartet -> "Vorschau anzeigen"
    Progressing,                // Wird installiert/entfernt -> Fortschritt
    OtherTransactionRunning,    // Andere Pakettransaktion läuft -> gesperrt
    Reconciling,                // Ergebnis da, Abgleich läuft -> "Status wird geprüft …"
    ErrorOrCancelled            // Fehler oder Abbruch -> "Erneut versuchen"
};

QString appActionStateToString(AppActionState state);
AppActionState appActionStateFromString(const QString &str);

// TransactionIntent: Typ Install/Remove/Upgrade, ausgewählte präzise Ziele, zugehörige App-Kennung nur für Anzeige
struct TransactionIntent {
    enum class Type {
        UpgradeAll,
        Install,
        Remove,
        CustomCommand
    };

    Type type = Type::UpgradeAll;
    QList<PackageRef> targets;
    QString appKey; // optional, rein zur UI-Zuordnung/Anzeige
    QVariantMap options;

    bool operator==(const TransactionIntent &other) const = default;
    static QString typeToString(Type type);
    static Type typeFromString(const QString &str);
    QJsonObject toJson() const;
    static TransactionIntent fromJson(const QJsonObject &obj);
};

// TransactionPlan: ID, Revision/Fingerprint, native Backend-/Bestands-/Quellenrevision, vollständige PackageOp-Liste, Summen, Warnungen
struct TransactionPlan {
    QString id;
    QString planRevision;
    QString backendRevision;
    QList<PackageOp> ops;
    qint64 downloadBytes = 0;
    qint64 installedSizeDelta = 0;
    QStringList warnings;
    bool hasProtectedPackageConflict = false;
    QStringList affectedApps;

    bool operator==(const TransactionPlan &other) const = default;
    QJsonObject toJson() const;
    static TransactionPlan fromJson(const QJsonObject &obj);
    QString calculateFingerprint() const;
};

// TransactionSnapshot: Aktionsart, Zustand, Plan, Fortschritt, Ergebnis, letzte Ereignisnummer, Wiederanbindungsinformationen
struct TransactionSnapshot {
    QString transactionPath;
    TransactionIntent intent;
    Phase phase = Phase::Idle;
    TransactionPlan plan;
    bool canCancel = false;
    quint64 sequenceNumber = 0;
    Result lastResult = Result::Success;
    QString statusMessage;
    int progressPercent = 0;
    QDateTime timestamp;
    quint32 callerUid = 0;
    bool active = false;
    QStringList missedEvents;

    bool operator==(const TransactionSnapshot &other) const = default;
    QJsonObject toJson() const;
    static TransactionSnapshot fromJson(const QJsonObject &obj);
};

} // namespace lut
