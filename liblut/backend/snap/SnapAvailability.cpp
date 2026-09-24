#include "SnapAvailability.h"
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>

namespace lut {

static bool defaultSocketProbe(const QString &socketPath, QString &errOut, QString &responseOut) {
    QLocalSocket socket;
    socket.connectToServer(socketPath);
    if (!socket.waitForConnected(500)) {
        errOut = QStringLiteral("Verbindung zu %1 fehlgeschlagen: %2")
                     .arg(socketPath, socket.errorString());
        return false;
    }
    const QByteArray request = "GET /v2/system-info HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    if (socket.write(request) == -1 || !socket.waitForBytesWritten(500)) {
        errOut = QStringLiteral("Konnte Anfrage nicht an snapd-Socket senden: %1").arg(socket.errorString());
        return false;
    }
    QByteArray responseData;
    while (socket.waitForReadyRead(500)) {
        responseData.append(socket.readAll());
    }
    if (responseData.isEmpty()) {
        errOut = QStringLiteral("Keine Antwort von snapd am Socket empfangen");
        return false;
    }
    responseOut = QString::fromUtf8(responseData);
    return true;
}

SnapAvailability::CheckResult SnapAvailability::check(const QString &binaryPath,
                                                     const QString &socketPath,
                                                     SocketProber prober) {
    // 1. Fall: Fehlende oder nicht ausführbare Binärdatei
    if (binaryPath.isEmpty() || !QFile::exists(binaryPath)) {
        return {false, QStringLiteral("Snap-Binärdatei nicht gefunden: %1").arg(binaryPath), {}};
    }
    QFileInfo binInfo(binaryPath);
    if (!binInfo.isExecutable()) {
        return {false, QStringLiteral("Snap-Binärdatei ist nicht ausführbar: %1").arg(binaryPath), {}};
    }

    // 2. Fall: Binärdatei vorhanden, aber Socket-Datei existiert nicht
    if (socketPath.isEmpty() || !QFile::exists(socketPath)) {
        return {false, QStringLiteral("Snapd-Socket existiert nicht: %1").arg(socketPath), {}};
    }

    // 3. Fall: Nicht erreichbarer Socket
    QString errOut;
    QString responseOut;
    bool ok = false;
    if (prober) {
        ok = prober(socketPath, errOut, responseOut);
    } else {
        ok = defaultSocketProbe(socketPath, errOut, responseOut);
    }
    if (!ok) {
        return {false, errOut, {}};
    }

    // 4. Fall: Socket erreichbar, aber unerwartete Antwort
    QString statusLine;
    QString bodyStr;
    const int headerEnd = responseOut.indexOf(QStringLiteral("\r\n\r\n"));
    if (headerEnd != -1) {
        const int firstLineEnd = responseOut.indexOf(QStringLiteral("\r\n"));
        statusLine = responseOut.left(firstLineEnd);
        bodyStr = responseOut.mid(headerEnd + 4);
    } else {
        bodyStr = responseOut;
    }

    if (!statusLine.isEmpty() && !statusLine.contains(QLatin1String("200"))) {
        return {false, QStringLiteral("snapd-Dienst meldete Fehlerstatus: %1").arg(statusLine), {}};
    }

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(bodyStr.toUtf8(), &parseErr);
    if (doc.isNull() || !doc.isObject()) {
        return {false, QStringLiteral("Antwort von snapd ist kein gültiges JSON"), {}};
    }
    QJsonObject root = doc.object();
    if (root.value(QStringLiteral("type")).toString() != QLatin1String("sync")) {
        return {false, QStringLiteral("Antwort von snapd hat unerwarteten Typ (erwartet: sync)"), {}};
    }
    if (root.value(QStringLiteral("status-code")).toInt() != 200) {
        return {false, QStringLiteral("snapd meldet Statuscode %1").arg(root.value(QStringLiteral("status-code")).toInt()), {}};
    }
    QJsonObject res = root.value(QStringLiteral("result")).toObject();
    QString daemonVersion = res.value(QStringLiteral("version")).toString();
    return {true, QStringLiteral("snapd ist verfügbar (Version %1)").arg(daemonVersion), daemonVersion};
}

bool SnapAvailability::isSnapAvailable() {
    static const bool available = check().available;
    return available;
}

} // namespace lut
