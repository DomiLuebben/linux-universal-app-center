#include "PlanGuard.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QTextStream>
#include <QProcess>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static bool aptHoldsLock() {
    const int fd = ::open("/var/lib/dpkg/lock-frontend", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    struct flock lock{};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    const bool held = ::fcntl(fd, F_GETLK, &lock) == 0 && lock.l_type != F_UNLCK;
    ::close(fd);
    if (!held) return false;
    // The lock must belong to the invoking APT process, possibly through /bin/sh.
    pid_t ancestor = ::getppid();
    for (int depth = 0; depth < 8 && ancestor > 1; ++depth) {
        if (ancestor == lock.l_pid) return true;
        QFile status(QStringLiteral("/proc/%1/status").arg(ancestor));
        if (!status.open(QIODevice::ReadOnly)) return false;
        ancestor = 0;
        for (const auto &line : status.readAll().split('\n'))
            if (line.startsWith("PPid:")) ancestor = line.mid(5).trimmed().toInt();
    }
    return false;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto reject = [](const QString &error) {
        QTextStream(stderr) << "Linux Update Tool: " << error << ". Bitte neu planen.\n";
        return 42;
    };
    if (::geteuid() != 0 || !aptHoldsLock()) return reject(QStringLiteral("Native APT-Sperre fehlt"));
    const QString path = qEnvironmentVariable("LUT_APT_PLAN_FILE");
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    struct stat info{};
    if (fd < 0) return reject(QStringLiteral("Bestätigter Plan fehlt"));
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != 0 || (info.st_mode & 0077) || info.st_size > 8 * 1024 * 1024) {
        ::close(fd); return reject(QStringLiteral("Unsicherer Planpfad"));
    }
    QFile plan;
    plan.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle);
    const auto document = QJsonDocument::fromJson(plan.readAll());
    if (!document.isArray()) return reject(QStringLiteral("Ungültiger Paketplan"));
    QFile input;
    input.open(STDIN_FILENO, QIODevice::ReadOnly);
    QByteArray protocol;
    while (protocol.size() <= 16 * 1024 * 1024) {
        const auto chunk = input.read(65536);
        if (chunk.isEmpty()) break;
        protocol += chunk;
    }
    if (protocol.size() > 16 * 1024 * 1024) return reject(QStringLiteral("Paketprotokoll zu groß"));
    QString error;
    if (!lut::verifyAptHook(protocol, document.array(), [](const QString &archive) {
        QFile file(archive);
        if (!file.open(QIODevice::ReadOnly)) return QString();
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&file)) return QString();
        return QString::fromLatin1(hash.result().toHex());
    }, &error)) return reject(error);
    // Check dpkg again under the lock: no pre-existing half configured state may be repaired implicitly.
    QProcess audit;
    audit.start(QStringLiteral("/usr/bin/dpkg"), {QStringLiteral("--audit")});
    if (!audit.waitForFinished(30000) || audit.exitCode() != 0 || !audit.readAllStandardOutput().trimmed().isEmpty())
        return reject(QStringLiteral("Unvollständiger dpkg-Zustand"));
    QFile verified(path + QStringLiteral(".verified"));
    if (!verified.open(QIODevice::WriteOnly) || verified.write("verified\n") != 9) return reject(QStringLiteral("Prüfbestätigung konnte nicht gespeichert werden"));
    return 0;
}
