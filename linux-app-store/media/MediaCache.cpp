#include "MediaCache.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkCookieJar>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QTimer>
#include <QImageReader>
#include <QBuffer>
#include <QDateTime>
#include <QRegularExpression>

namespace lut {
MediaCache::MediaCache(QObject *parent)
    : MediaCache(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/store-media"), Limits{}, nullptr, parent) {}

MediaCache::MediaCache(const QString &directory, Limits limits, QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), m_directory(directory), m_limits(limits), m_network(network ? network : new QNetworkAccessManager(this)) {
    m_decoder.setMaxThreadCount(4);
    QDir().mkpath(m_directory);
    QFile::setPermissions(m_directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    trim();
}

MediaCache::~MediaCache() {
    for (auto *reply : std::as_const(m_replies)) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    m_decoder.clear();
    m_decoder.waitForDone();
}

bool MediaCache::safeRemoteUrl(const QUrl &url) {
    return url.isValid() && url.scheme() == QLatin1String("https") && !url.host().isEmpty()
        && url.userInfo().isEmpty() && !url.toString().contains(QRegularExpression(QStringLiteral("[\\r\\n]")));
}

QString MediaCache::pathFor(const QString &key) const {
    return m_directory + QLatin1Char('/') + QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex()) + QStringLiteral(".png");
}

QString MediaCache::source(const QString &input) {
    // Theme icons and canonical distribution-owned files do not use the network.
    static const QRegularExpression icon(QStringLiteral("^[A-Za-z0-9_.-]+$"));
    if (icon.match(input).hasMatch()) return input;
    const QUrl url(input);
    if (url.isLocalFile() || input.startsWith(QLatin1Char('/'))) {
        const QString path = QFileInfo(url.isLocalFile() ? url.toLocalFile() : input).canonicalFilePath();
        const QString userData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        if (path.startsWith(QStringLiteral("/usr/share/")) ||
            path.startsWith(QStringLiteral("/var/cache/")) ||
            path.startsWith(QStringLiteral("/tmp/")) ||
            path.startsWith(QStringLiteral("/var/lib/flatpak/")) ||
            path.startsWith(QStringLiteral("/var/lib/app-info/")) ||
            path.startsWith(QStringLiteral("/var/lib/snapd/")) ||
            (!userData.isEmpty() && path.startsWith(userData))) {
            return QUrl::fromLocalFile(path).toString();
        }
        return {};
    }
    if (!safeRemoteUrl(url)) return {};
    const QString key = url.toString(QUrl::FullyEncoded);
    const QString path = pathFor(key);
    QFile file(path);
    if (file.exists() && file.size() > 0 && file.size() <= m_limits.bytes) {
        if (file.open(QIODevice::ReadOnly)) file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
        return QUrl::fromLocalFile(path).toString();
    }
    if (m_pending.contains(key) || m_failures.value(key) > QDateTime::currentMSecsSinceEpoch() - 300000) return {};
    if (m_queue.size() >= m_limits.queued) return {};
    auto job = std::make_shared<Job>();
    job->key = key;
    job->url = url;
    m_pending.insert(key);
    m_queue.enqueue(job);
    pump();
    return {};
}

void MediaCache::pump() {
    while (m_active < m_limits.concurrent && !m_queue.isEmpty()) {
        auto job = m_queue.dequeue();
        ++m_active;
        job->elapsed.start();
        fetch(job);
    }
}

void MediaCache::fetch(const std::shared_ptr<Job> &job) {
    const int remaining = m_limits.timeoutMs - int(job->elapsed.elapsed());
    if (remaining <= 0 || !safeRemoteUrl(job->url)) { finish(job); return; }
    QNetworkRequest request(job->url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LinuxAppStore/1.3 (compatible; FlathubClient)"));
    request.setRawHeader("Accept", "image/png, image/jpeg, image/webp, image/avif, image/*;q=0.8, */*;q=0.5");
    request.setTransferTimeout(remaining);
    auto *reply = m_network->get(request);
    m_replies.insert(reply);
    reply->setReadBufferSize(64 * 1024);
    auto *deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    deadline->start(remaining); // total wall time, including redirects and slow trickles
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply] {
        if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > m_limits.bytes) reply->abort();
    });
    connect(reply, &QIODevice::readyRead, this, [this, reply, job] {
        const auto chunk = reply->read(m_limits.bytes - job->received + 1);
        job->received += chunk.size();
        if (job->received > m_limits.bytes) { reply->abort(); return; }
        job->data += chunk;
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, job] {
        const auto error = reply->error();
        const auto redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        m_replies.remove(reply);
        reply->deleteLater();
        if (error != QNetworkReply::NoError || job->received > m_limits.bytes) { finish(job); return; }
        if (!redirect.isEmpty()) {
            job->url = job->url.resolved(redirect);
            job->data.clear();
            if (++job->redirects > m_limits.redirects || !safeRemoteUrl(job->url)) { finish(job); return; }
            fetch(job);
            return;
        }
        if (status != 200) { finish(job); return; }
        m_decoder.start([this, job, limits = m_limits] {
            const auto decoded = decodeRaster(job->data, limits);
            QMetaObject::invokeMethod(this, [this, job, decoded] { finish(job, decoded); }, Qt::QueuedConnection);
        });
    });
}

QByteArray MediaCache::decodeRaster(const QByteArray &bytes, const Limits &limits) {
    if (bytes.isEmpty() || bytes.size() > limits.bytes) return {};
    QBuffer input;
    input.setData(bytes);
    input.open(QIODevice::ReadOnly);
    QImageReader reader(&input);
    const auto format = reader.format();
    if (format != "png" && format != "jpeg" && format != "webp") return {};
    const QSize size = reader.size();
    if (size.width() <= 0 || size.height() <= 0 || qint64(size.width()) * size.height() > limits.pixels) return {};
    reader.setAutoTransform(true);
    if (size.width() > 1920 || size.height() > 1080)
        reader.setScaledSize(size.scaled(1920, 1080, Qt::KeepAspectRatio));
    const auto decoded = reader.read();
    if (decoded.isNull()) return {};
    QByteArray output;
    QBuffer sink(&output);
    sink.open(QIODevice::WriteOnly);
    if (!decoded.save(&sink, "PNG") || output.size() > limits.bytes) return {};
    return output;
}

void MediaCache::finish(const std::shared_ptr<Job> &job, const QByteArray &image) {
    bool saved = false;
    if (!image.isEmpty() && image.size() <= m_limits.diskBytes) {
        QSaveFile file(pathFor(job->key));
        if (file.open(QIODevice::WriteOnly) && file.write(image) == image.size()) saved = file.commit();
    }
    if (!saved) {
        if (m_failures.size() >= 1024) m_failures.clear();
        m_failures.insert(job->key, QDateTime::currentMSecsSinceEpoch());
    }
    m_pending.remove(job->key);
    --m_active;
    trim();
    ++m_revision;
    emit changed();
    pump();
}

void MediaCache::trim() {
    const auto entries = QDir(m_directory).entryInfoList({QStringLiteral("*.png")}, QDir::Files, QDir::Time);
    qint64 total = 0;
    for (const auto &entry : entries) {
        total += entry.size();
        if (total > m_limits.diskBytes) QFile::remove(entry.absoluteFilePath());
    }
}
}
