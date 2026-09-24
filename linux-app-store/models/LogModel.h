#pragma once

#include <QAbstractListModel>
#include "liblut/protocol/events.h"

namespace lut {

class LogModel : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(int filterMode READ filterMode WRITE setFilterMode NOTIFY filterModeChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        LevelRole = Qt::UserRole + 1,
        LevelStringRole,
        SourceRole,
        TextRole,
        ColorRole
    };

    explicit LogModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int filterMode() const { return m_filterMode; }
    void setFilterMode(int mode);

public slots:
    void appendLog(const lut::LogLine &line);
    void clear();
    QString copyAll() const;

signals:
    void filterModeChanged();
    void countChanged();

private:
    void refilter();

    int m_filterMode = 0; // 0 = Alle, 1 = Warnungen & Fehler, 2 = Nur Fehler
    QList<LogLine> m_allLogs;
    QList<LogLine> m_visibleLogs;
};

} // namespace lut
