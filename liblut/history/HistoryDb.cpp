#include "HistoryDb.h"
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QDebug>

namespace lut {

QString HistoryDb::defaultDbPath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/history.db");
}

HistoryDb::HistoryDb(const QString &dbPath)
    : m_path(dbPath.isEmpty() ? defaultDbPath() : dbPath) {}

HistoryDb::~HistoryDb() {
    close();
}

bool HistoryDb::open() {
    if (m_db) return true;

    int rc = sqlite3_open(m_path.toUtf8().constData(), &m_db);
    if (rc != SQLITE_OK) {
        qWarning() << "Failed to open HistoryDb at" << m_path << ":" << sqlite3_errmsg(m_db);
        close();
        return false;
    }

    const char *createSql =
        "CREATE TABLE IF NOT EXISTS history ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  timestamp INTEGER NOT NULL,"
        "  duration_ms INTEGER NOT NULL,"
        "  download_bytes INTEGER NOT NULL,"
        "  install_bytes INTEGER NOT NULL,"
        "  result TEXT NOT NULL,"
        "  summary TEXT,"
        "  source TEXT DEFAULT 'nativ',"
        "  target TEXT"
        ");";

    char *errMsg = nullptr;
    rc = sqlite3_exec(m_db, createSql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        qWarning() << "Failed to create history table:" << errMsg;
        sqlite3_free(errMsg);
        close();
        return false;
    }

    // Schema-Migration für vorhandene Datenbanken
    sqlite3_exec(m_db, "ALTER TABLE history ADD COLUMN source TEXT DEFAULT 'nativ';", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "ALTER TABLE history ADD COLUMN target TEXT;", nullptr, nullptr, nullptr);

    return true;
}

void HistoryDb::close() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

qint64 HistoryDb::recordTransaction(qint64 durationMs, qint64 downloadBytes, qint64 installBytes,
                                     const QString &result, const QString &summary,
                                     const QString &source, const QString &target) {
    if (!open()) return -1;

    qint64 now = QDateTime::currentSecsSinceEpoch();

    // Deduplizierung: Keine doppelten Verlaufseinträge (Abschnitt 9)
    if (!summary.isEmpty() || !target.isEmpty()) {
        const char *dedupSql = "SELECT id FROM history WHERE result = ? AND summary = ? AND source = ? AND target = ? AND abs(timestamp - ?) <= 5 LIMIT 1;";
        sqlite3_stmt *dedupStmt = nullptr;
        if (sqlite3_prepare_v2(m_db, dedupSql, -1, &dedupStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(dedupStmt, 1, result.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(dedupStmt, 2, summary.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(dedupStmt, 3, source.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(dedupStmt, 4, target.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(dedupStmt, 5, now);
            if (sqlite3_step(dedupStmt) == SQLITE_ROW) {
                qint64 existingId = sqlite3_column_int64(dedupStmt, 0);
                sqlite3_finalize(dedupStmt);
                return existingId;
            }
            sqlite3_finalize(dedupStmt);
        }
    } else {
        const char *dedupSql = "SELECT id FROM history WHERE result = ? AND summary = ? AND source = ? AND target = ? "
                               "AND duration_ms = ? AND download_bytes = ? AND install_bytes = ? AND abs(timestamp - ?) <= 5 LIMIT 1;";
        sqlite3_stmt *dedupStmt = nullptr;
        if (sqlite3_prepare_v2(m_db, dedupSql, -1, &dedupStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(dedupStmt, 1, result.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(dedupStmt, 2, summary.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(dedupStmt, 3, source.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(dedupStmt, 4, target.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(dedupStmt, 5, durationMs);
            sqlite3_bind_int64(dedupStmt, 6, downloadBytes);
            sqlite3_bind_int64(dedupStmt, 7, installBytes);
            sqlite3_bind_int64(dedupStmt, 8, now);
            if (sqlite3_step(dedupStmt) == SQLITE_ROW) {
                qint64 existingId = sqlite3_column_int64(dedupStmt, 0);
                sqlite3_finalize(dedupStmt);
                return existingId;
            }
            sqlite3_finalize(dedupStmt);
        }
    }

    const char *sql = "INSERT INTO history (timestamp, duration_ms, download_bytes, install_bytes, result, summary, source, target) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, now);
    sqlite3_bind_int64(stmt, 2, durationMs);
    sqlite3_bind_int64(stmt, 3, downloadBytes);
    sqlite3_bind_int64(stmt, 4, installBytes);
    sqlite3_bind_text(stmt, 5, result.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, summary.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, source.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, target.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    qint64 insertId = -1;
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        insertId = sqlite3_last_insert_rowid(m_db);
    }
    sqlite3_finalize(stmt);
    return insertId;
}

QList<HistoryRecord> HistoryDb::recentTransactions(int limit) const {
    QList<HistoryRecord> list;
    if (!const_cast<HistoryDb*>(this)->open()) return list;

    const char *sql = "SELECT id, timestamp, duration_ms, download_bytes, install_bytes, result, summary, source, target "
                      "FROM history ORDER BY id DESC LIMIT ?;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return list;
    }

    sqlite3_bind_int(stmt, 1, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        HistoryRecord r;
        r.id = sqlite3_column_int64(stmt, 0);
        r.timestamp = sqlite3_column_int64(stmt, 1);
        r.durationMs = sqlite3_column_int64(stmt, 2);
        r.downloadBytes = sqlite3_column_int64(stmt, 3);
        r.installBytes = sqlite3_column_int64(stmt, 4);
        r.result = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
        const char *sumTxt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        if (sumTxt) {
            r.summary = QString::fromUtf8(sumTxt);
        }
        const char *srcTxt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (srcTxt) {
            r.source = QString::fromUtf8(srcTxt);
        }
        const char *tgtTxt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        if (tgtTxt) {
            r.target = QString::fromUtf8(tgtTxt);
        }
        list.append(r);
    }

    sqlite3_finalize(stmt);
    return list;
}

std::optional<double> HistoryDb::averageCommitBytesPerSecond(int limit) const {
    if (!const_cast<HistoryDb*>(this)->open()) return std::nullopt;

    const char *sql = "SELECT install_bytes, duration_ms FROM history "
                      "WHERE result = 'Success' AND install_bytes > 0 AND duration_ms > 0 "
                      "ORDER BY id DESC LIMIT ?;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_int(stmt, 1, limit);

    double totalRate = 0.0;
    int count = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        qint64 bytes = sqlite3_column_int64(stmt, 0);
        qint64 ms = sqlite3_column_int64(stmt, 1);
        if (ms > 0 && bytes > 0) {
            double rate = (static_cast<double>(bytes) / static_cast<double>(ms)) * 1000.0;
            totalRate += rate;
            count++;
        }
    }

    sqlite3_finalize(stmt);

    if (count == 0) {
        return std::nullopt;
    }
    return totalRate / count;
}

} // namespace lut
