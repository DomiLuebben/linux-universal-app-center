#include "events.h"
#include <QRegularExpression>

namespace lut {

QString phaseToString(Phase phase) {
    switch (phase) {
        case Phase::Idle: return QStringLiteral("Idle");
        case Phase::RefreshMetadata: return QStringLiteral("RefreshMetadata");
        case Phase::Resolve: return QStringLiteral("Resolve");
        case Phase::Download: return QStringLiteral("Download");
        case Phase::Verify: return QStringLiteral("Verify");
        case Phase::TestTransaction: return QStringLiteral("TestTransaction");
        case Phase::Commit: return QStringLiteral("Commit");
        case Phase::PostTransaction: return QStringLiteral("PostTransaction");
        case Phase::Cleanup: return QStringLiteral("Cleanup");
        case Phase::Finished: return QStringLiteral("Finished");
        case Phase::Failed: return QStringLiteral("Failed");
        case Phase::Cancelled: return QStringLiteral("Cancelled");
    }
    return QStringLiteral("Unknown");
}

Phase phaseFromString(const QString &str) {
    if (str == QLatin1String("Idle")) return Phase::Idle;
    if (str == QLatin1String("RefreshMetadata")) return Phase::RefreshMetadata;
    if (str == QLatin1String("Resolve")) return Phase::Resolve;
    if (str == QLatin1String("Download")) return Phase::Download;
    if (str == QLatin1String("Verify")) return Phase::Verify;
    if (str == QLatin1String("TestTransaction")) return Phase::TestTransaction;
    if (str == QLatin1String("Commit")) return Phase::Commit;
    if (str == QLatin1String("PostTransaction")) return Phase::PostTransaction;
    if (str == QLatin1String("Cleanup")) return Phase::Cleanup;
    if (str == QLatin1String("Finished")) return Phase::Finished;
    if (str == QLatin1String("Failed")) return Phase::Failed;
    if (str == QLatin1String("Cancelled")) return Phase::Cancelled;
    return Phase::Idle;
}

bool isPhaseCancellable(Phase phase) {
    switch (phase) {
        case Phase::RefreshMetadata:
        case Phase::Resolve:
        case Phase::Download:
        case Phase::Verify:
        case Phase::TestTransaction:
            return true;
        case Phase::Idle:
        case Phase::Commit:
        case Phase::PostTransaction:
        case Phase::Cleanup:
        case Phase::Finished:
        case Phase::Failed:
        case Phase::Cancelled:
            return false;
    }
    return false;
}

QString logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return QStringLiteral("Debug");
        case LogLevel::Info: return QStringLiteral("Info");
        case LogLevel::Warning: return QStringLiteral("Warning");
        case LogLevel::Error: return QStringLiteral("Error");
    }
    return QStringLiteral("Info");
}

LogLevel logLevelFromString(const QString &str) {
    if (str == QLatin1String("Debug")) return LogLevel::Debug;
    if (str == QLatin1String("Info")) return LogLevel::Info;
    if (str == QLatin1String("Warning")) return LogLevel::Warning;
    if (str == QLatin1String("Error")) return LogLevel::Error;
    return LogLevel::Info;
}

QString questionKindToString(QuestionKind kind) {
    switch (kind) {
        case QuestionKind::GpgKeyImport: return QStringLiteral("GpgKeyImport");
        case QuestionKind::ConffilePrompt: return QStringLiteral("ConffilePrompt");
        case QuestionKind::MediaChange: return QStringLiteral("MediaChange");
        case QuestionKind::UntrustedPackage: return QStringLiteral("UntrustedPackage");
        case QuestionKind::FileConflict: return QStringLiteral("FileConflict");
    }
    return QStringLiteral("GpgKeyImport");
}

QuestionKind questionKindFromString(const QString &str) {
    if (str == QLatin1String("GpgKeyImport")) return QuestionKind::GpgKeyImport;
    if (str == QLatin1String("ConffilePrompt")) return QuestionKind::ConffilePrompt;
    if (str == QLatin1String("MediaChange")) return QuestionKind::MediaChange;
    if (str == QLatin1String("UntrustedPackage")) return QuestionKind::UntrustedPackage;
    if (str == QLatin1String("FileConflict")) return QuestionKind::FileConflict;
    return QuestionKind::GpgKeyImport;
}

QString resultToString(Result result) {
    switch (result) {
        case Result::Success: return QStringLiteral("Success");
        case Result::SuccessWithWarnings: return QStringLiteral("SuccessWithWarnings");
        case Result::Failed: return QStringLiteral("Failed");
        case Result::Cancelled: return QStringLiteral("Cancelled");
    }
    return QStringLiteral("Success");
}

Result resultFromString(const QString &str) {
    if (str == QLatin1String("Success")) return Result::Success;
    if (str == QLatin1String("SuccessWithWarnings")) return Result::SuccessWithWarnings;
    if (str == QLatin1String("Failed")) return Result::Failed;
    if (str == QLatin1String("Cancelled")) return Result::Cancelled;
    return Result::Success;
}

QString PackageOp::kindToString(Kind kind) {
    switch (kind) {
        case Kind::Install: return QStringLiteral("Install");
        case Kind::Upgrade: return QStringLiteral("Upgrade");
        case Kind::Downgrade: return QStringLiteral("Downgrade");
        case Kind::Remove: return QStringLiteral("Remove");
        case Kind::Reinstall: return QStringLiteral("Reinstall");
        case Kind::Obsolete: return QStringLiteral("Obsolete");
    }
    return QStringLiteral("Upgrade");
}

PackageOp::Kind PackageOp::kindFromString(const QString &str) {
    if (str == QLatin1String("Install")) return Kind::Install;
    if (str == QLatin1String("Upgrade")) return Kind::Upgrade;
    if (str == QLatin1String("Downgrade")) return Kind::Downgrade;
    if (str == QLatin1String("Remove")) return Kind::Remove;
    if (str == QLatin1String("Reinstall")) return Kind::Reinstall;
    if (str == QLatin1String("Obsolete")) return Kind::Obsolete;
    return Kind::Upgrade;
}

bool PackageOp::detectIsKernel(const QString &name) {
    const QString lower = name.toLower();
    return lower.startsWith(QLatin1String("kernel")) ||
           lower.startsWith(QLatin1String("linux")) ||
           lower.contains(QLatin1String("linux-image")) ||
           lower.startsWith(QLatin1String("kmod-"));
}

QJsonObject serializePackageOp(const PackageOp &op) {
    QJsonObject obj;
    obj[QStringLiteral("id")] = op.id;
    obj[QStringLiteral("name")] = op.name;
    obj[QStringLiteral("version")] = op.version;
    obj[QStringLiteral("newVersion")] = op.newVersion;
    obj[QStringLiteral("arch")] = op.arch;
    obj[QStringLiteral("repo")] = op.repo;
    obj[QStringLiteral("summary")] = op.summary;
    obj[QStringLiteral("kind")] = PackageOp::kindToString(op.kind);
    obj[QStringLiteral("downloadSize")] = op.downloadSize;
    obj[QStringLiteral("installedSize")] = op.installedSize;
    if (op.installedSizeDelta) obj[QStringLiteral("installedSizeDelta")] = *op.installedSizeDelta;
    obj[QStringLiteral("isSecurity")] = op.isSecurity;
    obj[QStringLiteral("isKernel")] = op.isKernel;
    obj[QStringLiteral("userRequested")] = op.userRequested;
    return obj;
}

PackageOp deserializePackageOp(const QJsonObject &obj) {
    PackageOp op;
    op.id = obj.value(QStringLiteral("id")).toString();
    op.name = obj.value(QStringLiteral("name")).toString();
    op.version = obj.value(QStringLiteral("version")).toString();
    op.newVersion = obj.value(QStringLiteral("newVersion")).toString();
    op.arch = obj.value(QStringLiteral("arch")).toString();
    op.repo = obj.value(QStringLiteral("repo")).toString();
    op.summary = obj.value(QStringLiteral("summary")).toString();
    op.kind = PackageOp::kindFromString(obj.value(QStringLiteral("kind")).toString());
    op.downloadSize = obj.value(QStringLiteral("downloadSize")).toInteger();
    op.installedSize = obj.value(QStringLiteral("installedSize")).toInteger();
    if (obj.contains(QStringLiteral("installedSizeDelta"))) op.installedSizeDelta = obj.value(QStringLiteral("installedSizeDelta")).toInteger();
    op.isSecurity = obj.value(QStringLiteral("isSecurity")).toBool();
    op.isKernel = obj.contains(QStringLiteral("isKernel"))
                      ? obj.value(QStringLiteral("isKernel")).toBool()
                      : PackageOp::detectIsKernel(op.name);
    op.userRequested = obj.value(QStringLiteral("userRequested")).toBool();
    return op;
}

QJsonObject serializeEvent(const Event &event, quint64 seq, const QString &transactionPath) {
    QJsonObject root;
    root[QStringLiteral("v")] = 1;
    if (seq > 0) root[QStringLiteral("seq")] = static_cast<qint64>(seq);
    if (!transactionPath.isEmpty()) root[QStringLiteral("transactionPath")] = transactionPath;

    std::visit([&root](auto &&arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, PhaseChanged>) {
            root[QStringLiteral("type")] = QStringLiteral("PhaseChanged");
            QJsonObject data;
            data[QStringLiteral("phase")] = phaseToString(arg.phase);
            data[QStringLiteral("label")] = arg.label;
            data[QStringLiteral("cancellable")] = arg.cancellable;
            data[QStringLiteral("indeterminate")] = arg.indeterminate;
            root[QStringLiteral("data")] = data;
        } else if constexpr (std::is_same_v<T, PlanReady>) {
            root[QStringLiteral("type")] = QStringLiteral("PlanReady");
            QJsonObject data;
            QJsonArray opsArr;
            for (const auto &op : arg.ops) {
                opsArr.append(serializePackageOp(op));
            }
            data[QStringLiteral("ops")] = opsArr;
            data[QStringLiteral("downloadBytes")] = arg.downloadBytes;
            data[QStringLiteral("installedSizeDelta")] = arg.installedSizeDelta;
            QJsonArray warningsArr;
            for (const auto &w : arg.warnings) {
                warningsArr.append(w);
            }
            data[QStringLiteral("warnings")] = warningsArr;
            data[QStringLiteral("planRevision")] = arg.planRevision;
            root[QStringLiteral("data")] = data;
        } else if constexpr (std::is_same_v<T, ItemStarted>) {
            root[QStringLiteral("type")] = QStringLiteral("ItemStarted");
            QJsonObject data;
            data[QStringLiteral("pkgId")] = arg.pkgId;
            data[QStringLiteral("kind")] = PackageOp::kindToString(arg.kind);
            data[QStringLiteral("weight")] = arg.weight;
            root[QStringLiteral("data")] = data;
        } else if constexpr (std::is_same_v<T, ItemProgress>) {
            root[QStringLiteral("type")] = QStringLiteral("ItemProgress");
            QJsonObject data;
            data[QStringLiteral("pkgId")] = arg.pkgId;
            data[QStringLiteral("done")] = arg.done;
            data[QStringLiteral("total")] = arg.total;
            root[QStringLiteral("data")] = data;
        } else if constexpr (std::is_same_v<T, ItemFinished>) {
            root[QStringLiteral("type")] = QStringLiteral("ItemFinished");
            QJsonObject data;
            data[QStringLiteral("pkgId")] = arg.pkgId;
            data[QStringLiteral("ok")] = arg.ok;
            data[QStringLiteral("error")] = arg.error;
            root[QStringLiteral("data")] = data;
        } else if constexpr (std::is_same_v<T, DownloadThroughput>) {
            root[QStringLiteral("type")] = QStringLiteral("DownloadThroughput");
            QJsonObject data;
            data[QStringLiteral("bytesPerSecond")] = arg.bytesPerSecond;
            data[QStringLiteral("totalDone")] = arg.totalDone;
            data[QStringLiteral("totalTotal")] = arg.totalTotal;
            root[QStringLiteral("data")] = data;
        } else if constexpr (std::is_same_v<T, ScriptletStarted>) {
            root[QStringLiteral("type")] = QStringLiteral("ScriptletStarted");
            QJsonObject data;
            data[QStringLiteral("pkgId")] = arg.pkgId;
            data[QStringLiteral("scriptletName")] = arg.scriptletName;
            root[QStringLiteral("data")] = data;
        } else if constexpr (std::is_same_v<T, ScriptletFinished>) {
            root[QStringLiteral("type")] = QStringLiteral("ScriptletFinished");
            QJsonObject data;
            data[QStringLiteral("pkgId")] = arg.pkgId;
            data[QStringLiteral("scriptletName")] = arg.scriptletName;
            data[QStringLiteral("exitCode")] = arg.exitCode;
            root[QStringLiteral("data")] = data;
        } else if constexpr (std::is_same_v<T, LogLine>) {
            root[QStringLiteral("type")] = QStringLiteral("LogLine");
            QJsonObject data;
            data[QStringLiteral("level")] = logLevelToString(arg.level);
            data[QStringLiteral("source")] = arg.source;
            data[QStringLiteral("text")] = arg.text;
            root[QStringLiteral("data")] = data;
        } else if constexpr (std::is_same_v<T, Question>) {
            root[QStringLiteral("type")] = QStringLiteral("Question");
            QJsonObject data;
            data[QStringLiteral("id")] = arg.id;
            data[QStringLiteral("kind")] = questionKindToString(arg.kind);
            data[QStringLiteral("payload")] = arg.payload;
            root[QStringLiteral("data")] = data;
        } else if constexpr (std::is_same_v<T, TransactionDone>) {
            root[QStringLiteral("type")] = QStringLiteral("TransactionDone");
            QJsonObject data;
            data[QStringLiteral("result")] = resultToString(arg.result);
            data[QStringLiteral("summary")] = arg.summary;
            data[QStringLiteral("rebootRequired")] = arg.rebootRequired;
            QJsonArray servicesArr;
            for (const auto &s : arg.servicesNeedingRestart) {
                servicesArr.append(s);
            }
            data[QStringLiteral("servicesNeedingRestart")] = servicesArr;
            data[QStringLiteral("historyId")] = arg.historyId;
            root[QStringLiteral("data")] = data;
        }
    }, event);

    return root;
}

std::optional<Event> deserializeEvent(const QJsonObject &obj, quint64 *outSeq, QString *outTransactionPath) {
    if (obj.value(QStringLiteral("v")).toInt() != 1) {
        return std::nullopt;
    }

    if (outSeq) {
        *outSeq = static_cast<quint64>(obj.value(QStringLiteral("seq")).toInteger(0));
    }
    if (outTransactionPath) {
        *outTransactionPath = obj.value(QStringLiteral("transactionPath")).toString();
    }

    const QString type = obj.value(QStringLiteral("type")).toString();
    const QJsonObject data = obj.value(QStringLiteral("data")).toObject();

    if (type == QLatin1String("PhaseChanged")) {
        PhaseChanged e;
        e.phase = phaseFromString(data.value(QStringLiteral("phase")).toString());
        e.label = data.value(QStringLiteral("label")).toString();
        e.cancellable = data.value(QStringLiteral("cancellable")).toBool();
        e.indeterminate = data.value(QStringLiteral("indeterminate")).toBool();
        return e;
    }
    if (type == QLatin1String("PlanReady")) {
        PlanReady e;
        const QJsonArray opsArr = data.value(QStringLiteral("ops")).toArray();
        for (const auto &v : opsArr) {
            e.ops.append(deserializePackageOp(v.toObject()));
        }
        e.downloadBytes = data.value(QStringLiteral("downloadBytes")).toInteger();
        e.installedSizeDelta = data.value(QStringLiteral("installedSizeDelta")).toInteger();
        const QJsonArray warningsArr = data.value(QStringLiteral("warnings")).toArray();
        for (const auto &w : warningsArr) {
            e.warnings.append(w.toString());
        }
        e.planRevision = data.value(QStringLiteral("planRevision")).toString();
        return e;
    }
    if (type == QLatin1String("ItemStarted")) {
        ItemStarted e;
        e.pkgId = data.value(QStringLiteral("pkgId")).toString();
        e.kind = PackageOp::kindFromString(data.value(QStringLiteral("kind")).toString());
        e.weight = data.value(QStringLiteral("weight")).toInteger();
        return e;
    }
    if (type == QLatin1String("ItemProgress")) {
        ItemProgress e;
        e.pkgId = data.value(QStringLiteral("pkgId")).toString();
        e.done = data.value(QStringLiteral("done")).toInteger();
        e.total = data.value(QStringLiteral("total")).toInteger();
        return e;
    }
    if (type == QLatin1String("ItemFinished")) {
        ItemFinished e;
        e.pkgId = data.value(QStringLiteral("pkgId")).toString();
        e.ok = data.value(QStringLiteral("ok")).toBool();
        e.error = data.value(QStringLiteral("error")).toString();
        return e;
    }
    if (type == QLatin1String("DownloadThroughput")) {
        DownloadThroughput e;
        e.bytesPerSecond = data.value(QStringLiteral("bytesPerSecond")).toInteger();
        e.totalDone = data.value(QStringLiteral("totalDone")).toInteger();
        e.totalTotal = data.value(QStringLiteral("totalTotal")).toInteger();
        return e;
    }
    if (type == QLatin1String("ScriptletStarted")) {
        ScriptletStarted e;
        e.pkgId = data.value(QStringLiteral("pkgId")).toString();
        e.scriptletName = data.value(QStringLiteral("scriptletName")).toString();
        return e;
    }
    if (type == QLatin1String("ScriptletFinished")) {
        ScriptletFinished e;
        e.pkgId = data.value(QStringLiteral("pkgId")).toString();
        e.scriptletName = data.value(QStringLiteral("scriptletName")).toString();
        e.exitCode = data.value(QStringLiteral("exitCode")).toInt();
        return e;
    }
    if (type == QLatin1String("LogLine")) {
        LogLine e;
        e.level = logLevelFromString(data.value(QStringLiteral("level")).toString());
        e.source = data.value(QStringLiteral("source")).toString();
        e.text = data.value(QStringLiteral("text")).toString();
        return e;
    }
    if (type == QLatin1String("Question")) {
        Question e;
        e.id = data.value(QStringLiteral("id")).toString();
        e.kind = questionKindFromString(data.value(QStringLiteral("kind")).toString());
        e.payload = data.value(QStringLiteral("payload")).toObject();
        return e;
    }
    if (type == QLatin1String("TransactionDone")) {
        TransactionDone e;
        e.result = resultFromString(data.value(QStringLiteral("result")).toString());
        e.summary = data.value(QStringLiteral("summary")).toString();
        e.rebootRequired = data.value(QStringLiteral("rebootRequired")).toBool();
        const QJsonArray servicesArr = data.value(QStringLiteral("servicesNeedingRestart")).toArray();
        for (const auto &s : servicesArr) {
            e.servicesNeedingRestart.append(s.toString());
        }
        e.historyId = data.value(QStringLiteral("historyId")).toInteger();
        return e;
    }

    return std::nullopt;
}

} // namespace lut
