#include "Inhibitor.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDebug>
#include <unistd.h>

namespace lut {

Inhibitor::Inhibitor(QObject *parent)
    : QObject(parent) {}

Inhibitor::~Inhibitor() {
    releaseLock();
}

bool Inhibitor::takeLock(const QString &why) {
    if (m_held) return true;

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.login1"),
        QStringLiteral("/org/freedesktop/login1"),
        QStringLiteral("org.freedesktop.login1.Manager"),
        QStringLiteral("Inhibit")
    );

    msg << QStringLiteral("sleep:shutdown:idle")
        << QStringLiteral("Linux Update Tool")
        << why
        << QStringLiteral("block");

    QDBusReply<QDBusUnixFileDescriptor> reply = QDBusConnection::systemBus().call(msg);
    if (!reply.isValid()) {
        qWarning() << "Inhibitor: Failed to acquire login1 inhibitor lock:" << reply.error().message();
        return false;
    }

    m_fd = reply.value();
    if (m_fd.isValid() && m_fd.fileDescriptor() >= 0) {
        m_held = true;
        qInfo() << "Inhibitor: Successfully acquired systemd sleep/shutdown lock (FD" << m_fd.fileDescriptor() << ")";
        return true;
    }

    return false;
}

void Inhibitor::releaseLock() {
    if (m_held) {
        if (m_fd.isValid() && m_fd.fileDescriptor() >= 0) {
            ::close(m_fd.fileDescriptor());
        }
        m_held = false;
        qInfo() << "Inhibitor: Released systemd sleep/shutdown lock";
    }
}

} // namespace lut
