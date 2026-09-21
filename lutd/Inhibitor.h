#pragma once

#include <QObject>
#include <QDBusUnixFileDescriptor>

namespace lut {

class Inhibitor : public QObject {
    Q_OBJECT

public:
    explicit Inhibitor(QObject *parent = nullptr);
    ~Inhibitor() override;

    bool takeLock(const QString &why = QStringLiteral("Paketaktualisierung läuft"));
    void releaseLock();
    bool isHeld() const { return m_held; }

private:
    bool m_held = false;
    QDBusUnixFileDescriptor m_fd;
};

} // namespace lut
