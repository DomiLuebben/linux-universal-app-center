#pragma once

#include <QAbstractListModel>
#include <QProcess>
#include <QStringList>

namespace lut {

/// AUR-Aktualisierungen für Arch und Derivate.
///
/// Bewusst ohne eigene AUR-Logik: Erkennung, Schnappschuss und Bau liegen im
/// Helfer des linux-package-installer (/usr/share/linux-package-installer/
/// aurbuild.py). Diese Klasse ruft ihn nur auf und zeigt sein Ergebnis an.
///
/// Läuft im GUI-Prozess als normaler Benutzer, NICHT über lutd: makepkg
/// verweigert Root, und nur das abschliessende "pacman -U" braucht Rechte –
/// das holt sich der Helfer selbst über pkexec.
class AurUpdates : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    /// Wurde in dieser Sitzung schon einmal erfolgreich geprüft?
    Q_PROPERTY(bool hasChecked READ hasChecked NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        InstalledVersionRole,
        AvailableVersionRole,
    };

    struct Entry {
        QString name;
        QString installed;
        QString available;
    };

    explicit AurUpdates(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool available() const { return !m_helperPath.isEmpty(); }
    bool busy() const { return m_busy; }
    QString statusMessage() const { return m_statusMessage; }
    bool hasChecked() const { return m_hasChecked; }

    /// Pfad zum Helfer, oder leer, wenn er nicht installiert ist.
    static QString findHelper();
    /// Wandelt die JSON-Ausgabe von "aurbuild.py check" in Einträge um.
    /// Wirft nicht; meldet stattdessen über \p error, damit ein Netzfehler
    /// nicht als "alles aktuell" durchgeht.
    static QList<Entry> parseCheckOutput(const QByteArray &json, QString *error);

public slots:
    /// Fragt veraltete AUR-Pakete ab.
    void check();
    /// Lädt den Schnappschuss und meldet den PKGBUILD zur Ansicht.
    void prepare(const QString &name);
    /// Baut den zuvor vorbereiteten Ordner und installiert das Ergebnis.
    void build(const QString &pkgDir);
    void cancel();

signals:
    void busyChanged();
    void countChanged();
    void statusChanged();
    /// Ergebnis von prepare(): der PKGBUILD gehört vor dem Bauen angesehen.
    void prepared(const QString &name, const QString &pkgDir, const QString &pkgbuild,
                  const QStringList &missingRepoDeps);
    void logLine(const QString &line);
    void failed(const QString &message);
    void finished(const QString &name);

private:
    void setBusy(bool busy);
    void setStatus(const QString &message);
    QProcess *startHelper(const QStringList &arguments);

    QString m_helperPath;
    QString m_statusMessage;
    bool m_busy = false;
    bool m_hasChecked = false;
    QString m_pendingName;
    QList<Entry> m_items;
};

} // namespace lut
