#include "Backend.h"
#include "alpm/AlpmBackend.h"
#include "apt/AptBackend.h"
#include "dnf5/Dnf5Backend.h"
#include "liblut/detect/DistroDetect.h"
#include <QFileInfo>

namespace lut {
std::unique_ptr<Backend> Backend::createForHost(QString *error) {
    if (QFileInfo::exists(QStringLiteral("/run/ostree-booted")) ||
        QFileInfo::exists(QStringLiteral("/usr/lib/bootc"))) {
        if (error) *error = QStringLiteral("Imagebasierte Systeme benötigen ihren eigenen Updatepfad (rpm-ostree/bootc).");
        return {};
    }
    switch (DistroDetect::detectFamily()) {
    case DistroFamily::Fedora:
        if (QFileInfo::exists(QStringLiteral("/usr/bin/dnf5"))) return std::make_unique<Dnf5Backend>();
        if (error) *error = QStringLiteral("DNF5 ist nicht installiert. DNF4 wird nicht als Ersatz verwendet.");
        return {};
    case DistroFamily::Debian: return std::make_unique<AptBackend>();
    case DistroFamily::Arch:
#ifdef HAVE_ALPM
        return std::make_unique<AlpmBackend>();
#else
        if (error) *error = QStringLiteral("Dieses Programm wurde ohne libalpm gebaut.");
        return {};
#endif
    case DistroFamily::Unknown:
        if (error) *error = QStringLiteral("Unbekannte Distribution – kein passendes Backend.");
        return {};
    }
    return {};
}

void Backend::planPackageTransaction(const TransactionIntent &intent) {
    QStringList names;
    for (const auto &target : intent.targets) {
        if (!target.name.isEmpty()) {
            names.append(target.name);
        }
    }
    if (intent.type == TransactionIntent::Type::Install) {
        planInstall(names);
    } else if (intent.type == TransactionIntent::Type::Remove) {
        planRemove(names);
    } else {
        UpgradeOptions opt;
        planUpgradeAll(opt);
    }
}
}

