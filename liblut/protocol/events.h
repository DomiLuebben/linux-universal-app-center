#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <variant>
#include <optional>

namespace lut {

enum class Phase {
    Idle,
    RefreshMetadata,   // Repo-Metadaten holen
    Resolve,           // Abhängigkeitsauflösung
    Download,          // Pakete holen
    Verify,            // GPG-/Checksummen-Prüfung
    TestTransaction,   // Test-Transaktion (Dry-Run)
    Commit,            // Eigentliche Installation, ab hier NICHT abbrechbar
    PostTransaction,   // Scriptlets, dpkg-Trigger, ALPM-Hooks, dracut, grub
    Cleanup,           // Temporäre Dateien aufräumen
    Finished,          // Erfolgreich beendet
    Failed,            // Fehlgeschlagen
    Cancelled          // Durch Benutzer abgebrochen
};

QString phaseToString(Phase phase);
Phase phaseFromString(const QString &str);
bool isPhaseCancellable(Phase phase);

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

QString logLevelToString(LogLevel level);
LogLevel logLevelFromString(const QString &str);

enum class QuestionKind {
    GpgKeyImport,
    ConffilePrompt,
    MediaChange,
    UntrustedPackage,
    FileConflict
};

QString questionKindToString(QuestionKind kind);
QuestionKind questionKindFromString(const QString &str);

enum class Result {
    Success,
    SuccessWithWarnings,
    Failed,
    Cancelled
};

QString resultToString(Result result);
Result resultFromString(const QString &str);

struct PackageOp {
    enum class Kind {
        Install,
        Upgrade,
        Downgrade,
        Remove,
        Reinstall,
        Obsolete
    };

    QString id;            // z.B. "kernel-core-6.17.4-200.fc44.x86_64"
    QString name;          // Paketname
    QString version;       // Installierte oder bisherige Version
    QString newVersion;    // Zielversion (leer bei Remove)
    QString arch;          // Architektur
    QString repo;          // Quell-Repository
    QString summary;       // Kurzbeschreibung
    Kind kind = Kind::Upgrade;
    qint64 downloadSize = 0;   // 0 wenn im Cache
    qint64 installedSize = 0;  // Zielgröße bzw. freizugebende Größe
    std::optional<qint64> installedSizeDelta;
    bool isSecurity = false;
    bool isKernel = false;     // Heuristik: matcht kernel*, linux*, linux-image*
    bool userRequested = false;

    bool operator==(const PackageOp &other) const = default;
    static QString kindToString(Kind kind);
    static Kind kindFromString(const QString &str);
    static bool detectIsKernel(const QString &name);
};

// Events
struct PhaseChanged {
    Phase phase = Phase::Idle;
    QString label;
    bool cancellable = false;
    bool indeterminate = false;

    bool operator==(const PhaseChanged &other) const = default;
};

struct PlanReady {
    QList<PackageOp> ops;
    qint64 downloadBytes = 0;
    qint64 installedSizeDelta = 0;
    QStringList warnings;

    bool operator==(const PlanReady &other) const = default;
};

struct ItemStarted {
    QString pkgId;
    PackageOp::Kind kind = PackageOp::Kind::Upgrade;
    qint64 weight = 0;

    bool operator==(const ItemStarted &other) const = default;
};

struct ItemProgress {
    QString pkgId;
    qint64 done = 0;
    qint64 total = 0;

    bool operator==(const ItemProgress &other) const = default;
};

struct ItemFinished {
    QString pkgId;
    bool ok = true;
    QString error;

    bool operator==(const ItemFinished &other) const = default;
};

struct DownloadThroughput {
    qint64 bytesPerSecond = 0;
    qint64 totalDone = 0;
    qint64 totalTotal = 0;

    bool operator==(const DownloadThroughput &other) const = default;
};

struct ScriptletStarted {
    QString pkgId;
    QString scriptletName;  // %post, dracut, dpkg-trigger, ALPM-Hook

    bool operator==(const ScriptletStarted &other) const = default;
};

struct ScriptletFinished {
    QString pkgId;
    QString scriptletName;
    int exitCode = 0;

    bool operator==(const ScriptletFinished &other) const = default;
};

struct LogLine {
    LogLevel level = LogLevel::Info;
    QString source;
    QString text;

    bool operator==(const LogLine &other) const = default;
};

struct Question {
    QString id;
    QuestionKind kind = QuestionKind::GpgKeyImport;
    QJsonObject payload;

    bool operator==(const Question &other) const = default;
};

struct TransactionDone {
    Result result = Result::Success;
    QString summary;
    bool rebootRequired = false;
    QStringList servicesNeedingRestart;
    qint64 historyId = 0;

    bool operator==(const TransactionDone &other) const = default;
};

using Event = std::variant<
    PhaseChanged,
    PlanReady,
    ItemStarted,
    ItemProgress,
    ItemFinished,
    DownloadThroughput,
    ScriptletStarted,
    ScriptletFinished,
    LogLine,
    Question,
    TransactionDone
>;

// JSON-Serialisierung (Protokoll Version 1)
QJsonObject serializePackageOp(const PackageOp &op);
PackageOp deserializePackageOp(const QJsonObject &obj);

QJsonObject serializeEvent(const Event &event);
std::optional<Event> deserializeEvent(const QJsonObject &obj);

} // namespace lut
