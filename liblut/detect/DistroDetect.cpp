#include "DistroDetect.h"
#include <QFile>
#include <QTextStream>

namespace lut {

DistroFamily DistroDetect::detectFamily(const QString &osReleasePath) {
    QFile file(osReleasePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return DistroFamily::Unknown;
    }

    QString id;
    QStringList idLike;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1String("ID="))) {
            id = line.mid(3).remove(QLatin1Char('"')).remove(QLatin1Char('\'')).toLower();
        } else if (line.startsWith(QLatin1String("ID_LIKE="))) {
            QString likes = line.mid(8).remove(QLatin1Char('"')).remove(QLatin1Char('\'')).toLower();
            idLike = likes.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        }
    }

    auto matches = [&](const QString &token) {
        return id == token || idLike.contains(token);
    };

    if (matches(QLatin1String("fedora")) || matches(QLatin1String("rhel")) ||
        matches(QLatin1String("centos")) || matches(QLatin1String("nobara")) ||
        matches(QLatin1String("bazzite")) || matches(QLatin1String("almalinux")) ||
        matches(QLatin1String("rocky"))) {
        return DistroFamily::Fedora;
    }

    if (matches(QLatin1String("arch")) || matches(QLatin1String("cachyos")) ||
        matches(QLatin1String("manjaro")) || matches(QLatin1String("endeavouros")) ||
        matches(QLatin1String("steamos")) || matches(QLatin1String("archarm"))) {
        return DistroFamily::Arch;
    }

    if (matches(QLatin1String("debian")) || matches(QLatin1String("ubuntu")) ||
        matches(QLatin1String("linuxmint")) || matches(QLatin1String("pop")) ||
        matches(QLatin1String("tuxedo")) || matches(QLatin1String("zorin"))) {
        return DistroFamily::Debian;
    }

    return DistroFamily::Unknown;
}

QString DistroDetect::familyToString(DistroFamily family) {
    switch (family) {
        case DistroFamily::Fedora: return QStringLiteral("Fedora");
        case DistroFamily::Debian: return QStringLiteral("Debian");
        case DistroFamily::Arch: return QStringLiteral("Arch");
        case DistroFamily::Unknown: return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

QString DistroDetect::prettyName(const QString &osReleasePath) {
    QFile file(osReleasePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral("Linux");
    }

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1String("PRETTY_NAME="))) {
            return line.mid(12).remove(QLatin1Char('"')).remove(QLatin1Char('\''));
        }
    }
    return QStringLiteral("Linux");
}

} // namespace lut
