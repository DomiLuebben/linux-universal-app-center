#include <QtTest>
#include <QTemporaryDir>
#include <QNetworkReply>
#include <QTimer>
#include <QImage>
#include <QBuffer>
#include "linux-app-store/media/MediaCache.h"
using namespace lut;
class Reply : public QNetworkReply {
public:
    QByteArray bytes;
    qsizetype offset = 0;
    bool stopped = false;
    Reply(const QNetworkRequest &request, QByteArray data, const QUrl &redirect, bool hang, QObject *parent)
        : QNetworkReply(parent), bytes(data) {
        setRequest(request); setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, redirect.isEmpty() ? 200 : 302);
        if (!redirect.isEmpty()) setAttribute(QNetworkRequest::RedirectionTargetAttribute, redirect);
        open(QIODevice::ReadOnly);
        if (!hang) QTimer::singleShot(0, this, [this] {
            if (stopped) return;
            emit readyRead();
            if (!stopped) { setFinished(true); emit finished(); }
        });
    }
    void abort() override {
        if (stopped) return;
        stopped = true; setError(OperationCanceledError, QStringLiteral("aborted"));
        setFinished(true); emit finished();
    }
    qint64 bytesAvailable() const override { return bytes.size() - offset + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 length) override {
        const auto count = qMin(length, qint64(bytes.size() - offset));
        if (count == 0) return -1;
        memcpy(data, bytes.constData() + offset, count); offset += count; return count;
    }
};
class Network : public QNetworkAccessManager {
public:
    QByteArray data;
    QUrl redirect;
    bool hang = false;
    int requests = 0;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        ++requests;
        return new Reply(request, data, redirect, hang, this);
    }
};
class StoreMediaTest : public QObject {
    Q_OBJECT
private slots:
    void limitsAndCache() {
        QTemporaryDir directory;
        Network network;
        QImage picture(16, 16, QImage::Format_RGB32); picture.fill(Qt::blue);
        QBuffer buffer(&network.data); buffer.open(QIODevice::WriteOnly); QVERIFY(picture.save(&buffer, "PNG"));
        MediaCache::Limits limits; limits.timeoutMs = 40; limits.bytes = 1024; limits.pixels = 256;
        MediaCache cache(directory.path(), limits, &network);
        QVERIFY(cache.source(QStringLiteral("https://example.test/image")).isEmpty());
        QTRY_VERIFY(cache.revision() > 0);
        const auto cached = cache.source(QStringLiteral("https://example.test/image"));
        QVERIFY(cached.startsWith(QLatin1String("file:")));
        QCOMPARE(network.requests, 1);
        QVERIFY(!MediaCache::decodeRaster(network.data, limits).isEmpty());
        limits.pixels = 255;
        QVERIFY(MediaCache::decodeRaster(network.data, limits).isEmpty());
        QVERIFY(MediaCache::decodeRaster("<svg/>", limits).isEmpty());
        QVERIFY(cache.source(QStringLiteral("http://example.test/image")).isEmpty());
        QVERIFY(cache.source(QStringLiteral("https://user:secret@example.test/image")).isEmpty());
        QCOMPARE(network.requests, 1);
        auto revision = cache.revision();
        network.data = QByteArray(1025, 'x');
        cache.source(QStringLiteral("https://example.test/large"));
        QTRY_VERIFY(cache.revision() > revision);
        QVERIFY(cache.source(QStringLiteral("https://example.test/large")).isEmpty());
        revision = cache.revision(); network.data.clear(); network.redirect = QUrl(QStringLiteral("http://example.test/unsafe"));
        cache.source(QStringLiteral("https://example.test/redirect"));
        QTRY_VERIFY(cache.revision() > revision);
        QCOMPARE(network.requests, 3); // the HTTP destination was never requested
        revision = cache.revision(); network.redirect = QUrl(QStringLiteral("https://example.test/loop"));
        cache.source(QStringLiteral("https://example.test/loop"));
        QTRY_VERIFY(cache.revision() > revision);
        QCOMPARE(network.requests, 7); // original plus three allowed redirects
        revision = cache.revision(); network.redirect = QUrl(); network.hang = true;
        cache.source(QStringLiteral("https://example.test/hang"));
        QTRY_VERIFY(cache.revision() > revision);
        QVERIFY(cache.source(QStringLiteral("https://example.test/hang")).isEmpty());
    }
    void evictsLeastRecentlyUsed() {
        QTemporaryDir directory;
        const auto now = QDateTime::currentDateTimeUtc();
        for (int i = 0; i < 3; ++i) {
            QFile file(directory.filePath(QString::number(i) + QStringLiteral(".png")));
            QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QByteArray(100, 'x')); file.flush();
            file.setFileTime(now.addSecs(i), QFileDevice::FileModificationTime);
        }
        MediaCache::Limits limits; limits.diskBytes = 200;
        MediaCache cache(directory.path(), limits, nullptr);
        QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("0.png"))));
        QVERIFY(QFile::exists(directory.filePath(QStringLiteral("1.png"))));
        QVERIFY(QFile::exists(directory.filePath(QStringLiteral("2.png"))));
    }
};
QTEST_GUILESS_MAIN(StoreMediaTest)
#include "store_media_test.moc"
