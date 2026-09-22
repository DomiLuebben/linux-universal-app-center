#include "TransactionTypes.h"
#include "liblut/backend/Validation.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <algorithm>

namespace lut {

bool PackageRef::isValid() const {
    if (name.isEmpty() || !Validation::isValidPackageName(name)) {
        return false;
    }
    if (!arch.isEmpty() && arch.length() > 32) {
        return false;
    }
    if (!repoId.isEmpty() && repoId.length() > 64) {
        return false;
    }
    if (!version.isEmpty() && version.length() > 128) {
        return false;
    }
    return true;
}

QJsonObject PackageRef::toJson() const {
    QJsonObject obj;
    obj[QStringLiteral("backend")] = backend;
    obj[QStringLiteral("repoId")] = repoId;
    obj[QStringLiteral("name")] = name;
    obj[QStringLiteral("arch")] = arch;
    obj[QStringLiteral("version")] = version;
    return obj;
}

PackageRef PackageRef::fromJson(const QJsonObject &obj) {
    PackageRef ref;
    ref.backend = obj.value(QStringLiteral("backend")).toString();
    ref.repoId = obj.value(QStringLiteral("repoId")).toString();
    ref.name = obj.value(QStringLiteral("name")).toString();
    ref.arch = obj.value(QStringLiteral("arch")).toString();
    ref.version = obj.value(QStringLiteral("version")).toString();
    return ref;
}

QJsonObject PackageOffer::toJson() const {
    QJsonObject obj;
    QJsonArray pkgsArr;
    for (const auto &p : packages) {
        pkgsArr.append(p.toJson());
    }
    obj[QStringLiteral("packages")] = pkgsArr;
    obj[QStringLiteral("priority")] = priority;
    obj[QStringLiteral("isCandidate")] = isCandidate;
    if (downloadSize.has_value()) {
        obj[QStringLiteral("downloadSize")] = *downloadSize;
    }
    if (installedSize.has_value()) {
        obj[QStringLiteral("installedSize")] = *installedSize;
    }
    obj[QStringLiteral("available")] = available;
    obj[QStringLiteral("unavailabilityReason")] = unavailabilityReason;
    return obj;
}

PackageOffer PackageOffer::fromJson(const QJsonObject &obj) {
    PackageOffer offer;
    const QJsonArray pkgsArr = obj.value(QStringLiteral("packages")).toArray();
    for (const auto &val : pkgsArr) {
        if (val.isObject()) {
            offer.packages.append(PackageRef::fromJson(val.toObject()));
        }
    }
    offer.priority = obj.value(QStringLiteral("priority")).toInt(0);
    offer.isCandidate = obj.value(QStringLiteral("isCandidate")).toBool(true);
    if (obj.contains(QStringLiteral("downloadSize"))) {
        offer.downloadSize = obj.value(QStringLiteral("downloadSize")).toInteger();
    }
    if (obj.contains(QStringLiteral("installedSize"))) {
        offer.installedSize = obj.value(QStringLiteral("installedSize")).toInteger();
    }
    offer.available = obj.value(QStringLiteral("available")).toBool(true);
    offer.unavailabilityReason = obj.value(QStringLiteral("unavailabilityReason")).toString();
    return offer;
}

QJsonObject InstalledState::toJson() const {
    QJsonObject obj;
    QJsonArray pkgsArr;
    for (const auto &p : installedPackages) {
        pkgsArr.append(p.toJson());
    }
    obj[QStringLiteral("installedPackages")] = pkgsArr;
    obj[QStringLiteral("isFullyInstalled")] = isFullyInstalled;
    obj[QStringLiteral("isPartiallyInstalled")] = isPartiallyInstalled;
    obj[QStringLiteral("origin")] = origin;
    obj[QStringLiteral("launchableDesktopIds")] = QJsonArray::fromStringList(launchableDesktopIds);
    obj[QStringLiteral("inventoryRevision")] = static_cast<qint64>(inventoryRevision);
    return obj;
}

InstalledState InstalledState::fromJson(const QJsonObject &obj) {
    InstalledState state;
    const QJsonArray pkgsArr = obj.value(QStringLiteral("installedPackages")).toArray();
    for (const auto &val : pkgsArr) {
        if (val.isObject()) {
            state.installedPackages.append(PackageRef::fromJson(val.toObject()));
        }
    }
    state.isFullyInstalled = obj.value(QStringLiteral("isFullyInstalled")).toBool(false);
    state.isPartiallyInstalled = obj.value(QStringLiteral("isPartiallyInstalled")).toBool(false);
    state.origin = obj.value(QStringLiteral("origin")).toString();
    const QJsonArray idsArr = obj.value(QStringLiteral("launchableDesktopIds")).toArray();
    for (const auto &val : idsArr) {
        state.launchableDesktopIds.append(val.toString());
    }
    state.inventoryRevision = static_cast<quint64>(obj.value(QStringLiteral("inventoryRevision")).toInteger());
    return state;
}

QString appActionStateToString(AppActionState state) {
    switch (state) {
        case AppActionState::Available: return QStringLiteral("Available");
        case AppActionState::Installed: return QStringLiteral("Installed");
        case AppActionState::InstalledNoLaunch: return QStringLiteral("InstalledNoLaunch");
        case AppActionState::UpdateAvailable: return QStringLiteral("UpdateAvailable");
        case AppActionState::PartiallyInstalled: return QStringLiteral("PartiallyInstalled");
        case AppActionState::MissingSource: return QStringLiteral("MissingSource");
        case AppActionState::Unavailable: return QStringLiteral("Unavailable");
        case AppActionState::ActionUnsupported: return QStringLiteral("ActionUnsupported");
        case AppActionState::PreparingPlan: return QStringLiteral("PreparingPlan");
        case AppActionState::AwaitingConfirmation: return QStringLiteral("AwaitingConfirmation");
        case AppActionState::Progressing: return QStringLiteral("Progressing");
        case AppActionState::OtherTransactionRunning: return QStringLiteral("OtherTransactionRunning");
        case AppActionState::Reconciling: return QStringLiteral("Reconciling");
        case AppActionState::ErrorOrCancelled: return QStringLiteral("ErrorOrCancelled");
    }
    return QStringLiteral("Unavailable");
}

AppActionState appActionStateFromString(const QString &str) {
    if (str == QLatin1String("Available")) return AppActionState::Available;
    if (str == QLatin1String("Installed")) return AppActionState::Installed;
    if (str == QLatin1String("InstalledNoLaunch")) return AppActionState::InstalledNoLaunch;
    if (str == QLatin1String("UpdateAvailable")) return AppActionState::UpdateAvailable;
    if (str == QLatin1String("PartiallyInstalled")) return AppActionState::PartiallyInstalled;
    if (str == QLatin1String("MissingSource")) return AppActionState::MissingSource;
    if (str == QLatin1String("PreparingPlan")) return AppActionState::PreparingPlan;
    if (str == QLatin1String("AwaitingConfirmation")) return AppActionState::AwaitingConfirmation;
    if (str == QLatin1String("Progressing")) return AppActionState::Progressing;
    if (str == QLatin1String("OtherTransactionRunning")) return AppActionState::OtherTransactionRunning;
    if (str == QLatin1String("Reconciling")) return AppActionState::Reconciling;
    if (str == QLatin1String("ErrorOrCancelled")) return AppActionState::ErrorOrCancelled;
    if (str == QLatin1String("ActionUnsupported")) return AppActionState::ActionUnsupported;
    return AppActionState::Unavailable;
}

QString TransactionIntent::typeToString(Type type) {
    switch (type) {
        case Type::UpgradeAll: return QStringLiteral("UpgradeAll");
        case Type::Install: return QStringLiteral("Install");
        case Type::Remove: return QStringLiteral("Remove");
        case Type::CustomCommand: return QStringLiteral("CustomCommand");
    }
    return QStringLiteral("UpgradeAll");
}

TransactionIntent::Type TransactionIntent::typeFromString(const QString &str) {
    if (str == QLatin1String("Install")) return Type::Install;
    if (str == QLatin1String("Remove")) return Type::Remove;
    if (str == QLatin1String("CustomCommand")) return Type::CustomCommand;
    return Type::UpgradeAll;
}

QJsonObject TransactionIntent::toJson() const {
    QJsonObject obj;
    obj[QStringLiteral("type")] = typeToString(type);
    QJsonArray targetsArr;
    for (const auto &t : targets) {
        targetsArr.append(t.toJson());
    }
    obj[QStringLiteral("targets")] = targetsArr;
    obj[QStringLiteral("appKey")] = appKey;
    obj[QStringLiteral("options")] = QJsonObject::fromVariantMap(options);
    return obj;
}

TransactionIntent TransactionIntent::fromJson(const QJsonObject &obj) {
    TransactionIntent intent;
    intent.type = typeFromString(obj.value(QStringLiteral("type")).toString());
    const QJsonArray targetsArr = obj.value(QStringLiteral("targets")).toArray();
    for (const auto &val : targetsArr) {
        if (val.isObject()) {
            intent.targets.append(PackageRef::fromJson(val.toObject()));
        }
    }
    intent.appKey = obj.value(QStringLiteral("appKey")).toString();
    intent.options = obj.value(QStringLiteral("options")).toObject().toVariantMap();
    return intent;
}

QString TransactionPlan::calculateFingerprint() const {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(backendRevision.toUtf8());
    hash.addData(QByteArray::number(downloadBytes));
    hash.addData(QByteArray::number(installedSizeDelta));
    hash.addData(hasProtectedPackageConflict ? "1" : "0");

    // Operations stabil nach Name sortieren für kanonischen Hash
    QList<PackageOp> sortedOps = ops;
    std::sort(sortedOps.begin(), sortedOps.end(), [](const PackageOp &a, const PackageOp &b) {
        if (a.name != b.name) return a.name < b.name;
        return a.arch < b.arch;
    });

    for (const auto &op : sortedOps) {
        hash.addData(PackageOp::kindToString(op.kind).toUtf8());
        hash.addData(":");
        hash.addData(op.name.toUtf8());
        hash.addData(":");
        hash.addData(op.version.toUtf8());
        hash.addData("->");
        hash.addData(op.newVersion.toUtf8());
        hash.addData("@");
        hash.addData(op.repo.toUtf8());
        hash.addData(";");
    }

    return QString::fromLatin1(hash.result().toHex());
}

QJsonObject TransactionPlan::toJson() const {
    QJsonObject obj;
    obj[QStringLiteral("id")] = id;
    obj[QStringLiteral("planRevision")] = planRevision;
    obj[QStringLiteral("backendRevision")] = backendRevision;
    QJsonArray opsArr;
    for (const auto &op : ops) {
        opsArr.append(serializePackageOp(op));
    }
    obj[QStringLiteral("ops")] = opsArr;
    obj[QStringLiteral("downloadBytes")] = downloadBytes;
    obj[QStringLiteral("installedSizeDelta")] = installedSizeDelta;
    obj[QStringLiteral("warnings")] = QJsonArray::fromStringList(warnings);
    obj[QStringLiteral("hasProtectedPackageConflict")] = hasProtectedPackageConflict;
    obj[QStringLiteral("affectedApps")] = QJsonArray::fromStringList(affectedApps);
    return obj;
}

TransactionPlan TransactionPlan::fromJson(const QJsonObject &obj) {
    TransactionPlan plan;
    plan.id = obj.value(QStringLiteral("id")).toString();
    plan.planRevision = obj.value(QStringLiteral("planRevision")).toString();
    plan.backendRevision = obj.value(QStringLiteral("backendRevision")).toString();
    const QJsonArray opsArr = obj.value(QStringLiteral("ops")).toArray();
    for (const auto &val : opsArr) {
        if (val.isObject()) {
            plan.ops.append(deserializePackageOp(val.toObject()));
        }
    }
    plan.downloadBytes = obj.value(QStringLiteral("downloadBytes")).toInteger();
    plan.installedSizeDelta = obj.value(QStringLiteral("installedSizeDelta")).toInteger();
    const QJsonArray warnArr = obj.value(QStringLiteral("warnings")).toArray();
    for (const auto &val : warnArr) plan.warnings.append(val.toString());
    plan.hasProtectedPackageConflict = obj.value(QStringLiteral("hasProtectedPackageConflict")).toBool(false);
    const QJsonArray appsArr = obj.value(QStringLiteral("affectedApps")).toArray();
    for (const auto &val : appsArr) plan.affectedApps.append(val.toString());
    return plan;
}

QJsonObject TransactionSnapshot::toJson() const {
    QJsonObject obj;
    obj[QStringLiteral("transactionPath")] = transactionPath;
    obj[QStringLiteral("intent")] = intent.toJson();
    obj[QStringLiteral("phase")] = phaseToString(phase);
    obj[QStringLiteral("plan")] = plan.toJson();
    obj[QStringLiteral("canCancel")] = canCancel;
    obj[QStringLiteral("sequenceNumber")] = static_cast<qint64>(sequenceNumber);
    obj[QStringLiteral("lastResult")] = resultToString(lastResult);
    obj[QStringLiteral("statusMessage")] = statusMessage;
    obj[QStringLiteral("progressPercent")] = progressPercent;
    obj[QStringLiteral("timestamp")] = timestamp.toString(Qt::ISODateWithMs);
    obj[QStringLiteral("callerUid")] = static_cast<qint64>(callerUid);
    obj[QStringLiteral("active")] = active;
    QJsonArray eventsArr;
    for (const auto &ev : missedEvents) {
        eventsArr.append(ev);
    }
    obj[QStringLiteral("missedEvents")] = eventsArr;
    return obj;
}

TransactionSnapshot TransactionSnapshot::fromJson(const QJsonObject &obj) {
    TransactionSnapshot snap;
    snap.transactionPath = obj.value(QStringLiteral("transactionPath")).toString();
    snap.intent = TransactionIntent::fromJson(obj.value(QStringLiteral("intent")).toObject());
    snap.phase = phaseFromString(obj.value(QStringLiteral("phase")).toString());
    snap.plan = TransactionPlan::fromJson(obj.value(QStringLiteral("plan")).toObject());
    snap.canCancel = obj.value(QStringLiteral("canCancel")).toBool(false);
    snap.sequenceNumber = static_cast<quint64>(obj.value(QStringLiteral("sequenceNumber")).toInteger());
    snap.lastResult = resultFromString(obj.value(QStringLiteral("lastResult")).toString());
    snap.statusMessage = obj.value(QStringLiteral("statusMessage")).toString();
    snap.progressPercent = obj.value(QStringLiteral("progressPercent")).toInt(0);
    snap.timestamp = QDateTime::fromString(obj.value(QStringLiteral("timestamp")).toString(), Qt::ISODateWithMs);
    snap.callerUid = static_cast<quint32>(obj.value(QStringLiteral("callerUid")).toInteger(0));
    snap.active = obj.value(QStringLiteral("active")).toBool(false);
    const QJsonArray eventsArr = obj.value(QStringLiteral("missedEvents")).toArray();
    for (const auto &val : eventsArr) {
        snap.missedEvents.append(val.toString());
    }
    return snap;
}

} // namespace lut
