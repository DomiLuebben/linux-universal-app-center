#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QQueue>
#include <QSet>
#include <QHash>
#include <QThreadPool>
#include <QElapsedTimer>
#include <memory>

namespace lut {
// Only decoded raster images reach QML. All remote I/O passes through these limits.
class MediaCache : public QObject {
    Q_OBJECT
    Q_PROPERTY(quint64 revision READ revision NOTIFY changed)
public:
    struct Limits {
        qint64 bytes = 10 * 1024 * 1024;
        qint64 pixels = 16 * 1024 * 1024;
        qint64 diskBytes = 512 * 1024 * 1024;
        int redirects = 3;
        int timeoutMs = 15000;
        int concurrent = 8;
        int queued = 2048;
    };
    explicit MediaCache(QObject *parent = nullptr);
    MediaCache(const QString &directory, Limits limits, QNetworkAccessManager *network, QObject *parent = nullptr);
    ~MediaCache() override;
    quint64 revision() const { return m_revision; }
    Q_INVOKABLE QString source(const QString &url);
    static bool safeRemoteUrl(const QUrl &url);
    static QByteArray decodeRaster(const QByteArray &bytes, const Limits &limits);
    void trim();
signals:
    void changed();
private:
    struct Job {
        QString key;
        QUrl url;
        QByteArray data;
        qint64 received = 0;
        int redirects = 0;
        QElapsedTimer elapsed;
    };
    void pump();
    void fetch(const std::shared_ptr<Job> &job);
    void finish(const std::shared_ptr<Job> &job, const QByteArray &image = {});
    QString pathFor(const QString &key) const;
    QString m_directory;
    Limits m_limits;
    QNetworkAccessManager *m_network;
    QThreadPool m_decoder;
    QQueue<std::shared_ptr<Job>> m_queue;
    QSet<QString> m_pending;
    QHash<QString, qint64> m_failures;
    QSet<QNetworkReply *> m_replies;
    int m_active = 0;
    quint64 m_revision = 0;
};
}
