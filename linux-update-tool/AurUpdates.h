#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QMap>
#include <QPointer>
#include <QProcess>
#include <QStringList>

class QNetworkAccessManager;

namespace lut {

/// AUR-Aktualisierungen für Arch und Derivate — vollständig im Werkzeug selbst.
///
/// Läuft im GUI-Prozess als normaler Benutzer, NICHT über lutd: makepkg
/// verweigert Root, und nur das abschliessende "pacman -U" braucht Rechte.
/// Die Quellen kommen per git von aur.archlinux.org; das ist der offizielle
/// Weg und erspart eine eigene, gehärtete Tar-Entpackung.
class AurUpdates : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
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

    /// Was die AUR-Schnittstelle je Paket liefert, soweit hier gebraucht.
    struct AurPackage {
        QString version;
        QString packageBase;
    };

    explicit AurUpdates(QObject *parent = nullptr);
    ~AurUpdates() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool available() const { return m_available; }
    bool busy() const { return m_busy; }
    QString statusMessage() const { return m_statusMessage; }
    bool hasChecked() const { return m_hasChecked; }

    // --- Reine Funktionen, damit sie ohne Netz und ohne pacman prüfbar sind ---

    /// Zerlegt die Ausgabe von "pacman -Qm" in {Name: Version}.
    static QMap<QString, QString> parseForeignPackages(const QByteArray &output);

    /// Liest eine Antwort der AUR-Schnittstelle (rpc/v5/info).
    /// Meldet über \p error, statt eine leere Liste zurückzugeben: ein
    /// Netz- oder Dienstfehler darf sich nicht als "alles aktuell" lesen.
    static QHash<QString, AurPackage> parseAurInfo(const QByteArray &json, QString *error);

    /// Wählt die Pakete, deren AUR-Version echt neuer ist. Ein lokal neuerer
    /// Rebuild taucht bewusst nicht auf.
    static QList<Entry> selectOutdated(const QMap<QString, QString> &installed,
                                       const QHash<QString, AurPackage> &remote);

    /// Vergleicht zwei Paketversionen: negativ, 0 oder positiv.
    static int compareVersions(const QString &left, const QString &right);

public slots:
    void check();
    void prepare(const QString &name);
    void build(const QString &pkgDir);
    void cancel();

signals:
    void busyChanged();
    void countChanged();
    void statusChanged();
    void prepared(const QString &name, const QString &pkgDir, const QString &pkgbuild,
                  const QStringList &missingRepoDeps);
    void logLine(const QString &line);
    void failed(const QString &message);
    void finished(const QString &name);

private:
    void setBusy(bool busy);
    void setStatus(const QString &message);
    void fail(const QString &message);
    void requestAurInfo(const QStringList &names);
    void finishCheck();
    QProcess *makeProcess();

    bool m_available = false;
    bool m_busy = false;
    bool m_hasChecked = false;
    QString m_statusMessage;
    QString m_pendingName;
    QString m_pendingBase;
    QList<Entry> m_items;

    QNetworkAccessManager *m_network = nullptr;
    QMap<QString, QString> m_foreign;
    QHash<QString, AurPackage> m_remote;
    int m_pendingReplies = 0;
    QString m_checkError;
};

} // namespace lut
