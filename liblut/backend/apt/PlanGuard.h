#pragma once
#include <QJsonArray>
#include <QString>
#include <functional>
namespace lut {
// APT's version 3 Pre-Install-Pkgs protocol is generated from its actual dpkg
// operation list under the frontend lock, including remove-only transactions.
bool verifyAptHook(const QByteArray &protocol, const QJsonArray &expected,
                   const std::function<QString(const QString &)> &sha256, QString *error);
}
