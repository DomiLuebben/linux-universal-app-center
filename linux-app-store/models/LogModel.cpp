#include "LogModel.h"
#include <QGuiApplication>
#include <QClipboard>

namespace lut {

LogModel::LogModel(QObject *parent)
    : QAbstractListModel(parent) {}

int LogModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return m_visibleLogs.size();
}

QHash<int, QByteArray> LogModel::roleNames() const {
    return {
        {LevelRole, "level"},
        {LevelStringRole, "levelString"},
        {SourceRole, "source"},
        {TextRole, "text"},
        {ColorRole, "colorType"} // "positive", "neutral", "negative", "text"
    };
}

QVariant LogModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleLogs.size()) {
        return QVariant();
    }

    const auto &item = m_visibleLogs.at(index.row());

    switch (role) {
        case LevelRole: return static_cast<int>(item.level);
        case LevelStringRole: return logLevelToString(item.level);
        case SourceRole: return item.source;
        case TextRole: return item.text;
        case ColorRole:
            switch (item.level) {
                case LogLevel::Error: return QStringLiteral("negative");
                case LogLevel::Warning: return QStringLiteral("neutral");
                case LogLevel::Info: return QStringLiteral("text");
                case LogLevel::Debug: return QStringLiteral("textMuted");
            }
            return QStringLiteral("text");
    }

    return QVariant();
}

void LogModel::setFilterMode(int mode) {
    if (m_filterMode != mode) {
        m_filterMode = mode;
        refilter();
        emit filterModeChanged();
    }
}

void LogModel::appendLog(const LogLine &line) {
    m_allLogs.append(line);
    if (m_allLogs.size() > 5000) {
        m_allLogs.removeFirst();
    }

    bool shouldShow = true;
    if (m_filterMode == 1 && line.level != LogLevel::Warning && line.level != LogLevel::Error) {
        shouldShow = false;
    } else if (m_filterMode == 2 && line.level != LogLevel::Error) {
        shouldShow = false;
    }

    if (shouldShow) {
        beginInsertRows(QModelIndex(), m_visibleLogs.size(), m_visibleLogs.size());
        m_visibleLogs.append(line);
        endInsertRows();
        emit countChanged();
    }
}

void LogModel::clear() {
    beginResetModel();
    m_allLogs.clear();
    m_visibleLogs.clear();
    endResetModel();
    emit countChanged();
}

void LogModel::refilter() {
    beginResetModel();
    m_visibleLogs.clear();
    for (const auto &l : m_allLogs) {
        if (m_filterMode == 1 && l.level != LogLevel::Warning && l.level != LogLevel::Error) {
            continue;
        }
        if (m_filterMode == 2 && l.level != LogLevel::Error) {
            continue;
        }
        m_visibleLogs.append(l);
    }
    endResetModel();
    emit countChanged();
}

QString LogModel::copyAll() const {
    QString res;
    for (const auto &l : m_visibleLogs) {
        res += QStringLiteral("[%1] (%2) %3\n").arg(logLevelToString(l.level), l.source, l.text);
    }
    return res;
}

} // namespace lut
