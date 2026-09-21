#pragma once

#include <QString>
#include <QList>
#include <sqlite3.h>
#include <optional>

namespace lut {

struct HistoryRecord {
    qint64 id = 0;
    qint64 timestamp = 0;
    qint64 durationMs = 0;
    qint64 downloadBytes = 0;
    qint64 installBytes = 0;
    QString result;
    QString summary;
};

class HistoryDb {
public:
    explicit HistoryDb(const QString &dbPath = QString());
    ~HistoryDb();

    bool open();
    void close();
    bool isOpen() const { return m_db != nullptr; }

    qint64 recordTransaction(qint64 durationMs, qint64 downloadBytes, qint64 installBytes,
                              const QString &result, const QString &summary);

    QList<HistoryRecord> recentTransactions(int limit = 20) const;
    std::optional<double> averageCommitBytesPerSecond(int limit = 5) const;

    static QString defaultDbPath();

private:
    QString m_path;
    sqlite3 *m_db = nullptr;
};

} // namespace lut
