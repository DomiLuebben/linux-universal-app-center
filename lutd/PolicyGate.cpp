#include "PolicyGate.h"
#include <PolkitQt1/Authority>
#include <PolkitQt1/Subject>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDebug>

namespace lut {

const QString PolicyGate::ActionRefresh = QStringLiteral("org.linuxupdatetool.refresh");
const QString PolicyGate::ActionUpgrade = QStringLiteral("org.linuxupdatetool.upgrade");
const QString PolicyGate::ActionInstall = QStringLiteral("org.linuxupdatetool.install");
const QString PolicyGate::ActionRemove = QStringLiteral("org.linuxupdatetool.remove");
const QString PolicyGate::ActionManageOthers = QStringLiteral("org.linuxupdatetool.manage-others");
const QString PolicyGate::ActionManageRepositories = QStringLiteral("org.linuxupdatetool.manage-repositories");
const QString PolicyGate::ActionPacstall = QStringLiteral("org.linuxupdatetool.pacstall");

PolicyGate::PolicyGate(QObject *parent)
    : QObject(parent) {}

bool PolicyGate::checkAuthorization(const QString &actionId, const QString &callerService, bool allowInteraction) {
    if (callerService.isEmpty()) {
        qWarning() << "PolicyGate: Caller service is empty";
        return false;
    }

    // Falls Caller root ist (z.B. interner systemd-Dienst oder CLI als root), direkt erlauben
    auto *iface = QDBusConnection::systemBus().interface();
    if (iface) {
        auto reply = iface->serviceUid(callerService);
        if (reply.isValid() && reply.value() == 0) {
            return true;
        }
    }

    PolkitQt1::SystemBusNameSubject subject(callerService);
    auto flags = allowInteraction ? PolkitQt1::Authority::AllowUserInteraction
                                  : PolkitQt1::Authority::None;

    auto *authority = PolkitQt1::Authority::instance();
    auto result = authority->checkAuthorizationSync(actionId, subject, flags);

    if (result == PolkitQt1::Authority::Yes) {
        qInfo() << "PolicyGate: Authorized" << actionId << "for" << callerService;
        return true;
    }

    qWarning() << "PolicyGate: Authorization failed for action" << actionId
               << "caller" << callerService << "(result:" << result << ")";
    return false;
}

} // namespace lut
