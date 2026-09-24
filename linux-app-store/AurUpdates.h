#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QMap>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <QSet>
#include <QVariantList>

class QNetworkAccessManager;

namespace lut {

class AppSettings;
class DaemonClient;

/// AUR-Aktualisierungen für Arch und Derivate — vollständig im Werkzeug selbst.
///
/// Läuft im GUI-Prozess als normaler Benutzer, NICHT über lutd: makepkg
/// verweigert Root, und nur das abschliessende "pacman -U" braucht Rechte.
/// Die Quellen kommen per git von aur.archlinux.org; das ist der offizielle
/// Weg und erspart eine eigene, gehärtete Tar-Entpackung.
class AurUpdates : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(bool hasChecked READ hasChecked NOTIFY countChanged)
    Q_PROPERTY(int foreignPackageCount READ foreignPackageCount NOTIFY foreignPackageCountChanged)
    Q_PROPERTY(bool hasForeignPackages READ hasForeignPackages NOTIFY foreignPackageCountChanged)

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

    /// Ein Eintrag der Stapelaktualisierung: ein pkgbase wird einmal geholt und
    /// gebaut, auch wenn mehrere installierte Pakete daraus stammen.
    struct BatchItem {
        QString base;
        QStringList names;
        QString fromVersion;
        QString toVersion;
        QString dir;
        QString review;          // Diff seit dem letzten Bau oder vollständiger PKGBUILD
        bool firstBuild = false; // noch nie mit diesem Werkzeug gebaut
        QStringList files;       // gebaute Pakete, die installiert werden
        QString error;
    };

    /// Abschnitte der Stapelaktualisierung, in dieser Reihenfolge.
    enum class Stage { Idle, Fetch, Review, Build, Install };

    explicit AurUpdates(QObject *parent = nullptr);
    ~AurUpdates() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool supported() const { return m_supported; }
    bool enabled() const { return m_enabled; }
    bool available() const { return m_supported && m_enabled; }
    bool busy() const { return m_busy; }
    int count() const { return rowCount(); }
    QString statusMessage() const { return m_statusMessage; }
    bool hasChecked() const { return m_hasChecked; }
    int foreignPackageCount() const { return m_foreignPackageCount; }
    bool hasForeignPackages() const { return m_foreignPackageCount > 0; }

    void setAppSettings(AppSettings *settings);
    void setDaemonClient(DaemonClient *client) { m_daemonClient = client; }

    using ProcessRunner = std::function<QByteArray(const QString &program, const QStringList &arguments, int *exitCode)>;
    void setProcessRunner(ProcessRunner runner) { m_processRunner = std::move(runner); }
    void setNetworkAccessManager(QNetworkAccessManager *nam) { m_network = nam; }
    void setForceSupported(bool supported);

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

    /// Fasst die gewünschten Pakete nach pkgbase zusammen, in der gegebenen
    /// Reihenfolge. Pakete ohne verfügbare Aktualisierung fallen heraus.
    static QList<BatchItem> groupByBase(const QStringList &names, const QList<Entry> &outdated,
                                        const QHash<QString, AurPackage> &remote);

    /// Paketname aus einem Dateinamen wie "foo-bar-1.2-3-x86_64.pkg.tar.zst".
    static QString packageNameFromFile(const QString &path);

    /// Nur die gebauten Pakete, die bereits installiert sind. Ein Split-pkgbase
    /// baut oft mehr (etwa "-docs" oder "-debug"); das darf nicht nebenbei
    /// auf dem System landen.
    static QStringList filterInstallable(const QStringList &files, const QSet<QString> &installedNames);

    /// Gesamtfortschritt 0..1: Holen 10 %, Bauen 80 %, Installieren 10 %.
    static double batchFraction(Stage stage, int position, int count);

    Stage stage() const { return m_stage; }
    bool cancellable() const;

public slots:
    void check();
    void setEnabled(bool enabled);
    void checkForeignPackages();
    /// Alle verfügbaren AUR-Aktualisierungen: einmal holen, einmal prüfen,
    /// nacheinander bauen, zusammen mit einer einzigen Passwortabfrage installieren.
    void upgradeAll();
    void upgradePackages(const QStringList &names);
    /// Einzelnes Paket; derselbe Weg wie upgradeAll().
    void prepare(const QString &name);
    /// Nach der Prüfansicht: bauen und installieren.
    void confirmReview();
    void cancel();

signals:
    void supportedChanged();
    void enabledChanged(bool enabled);
    void availableChanged();
    void foreignPackageCountChanged();
    void busyChanged();
    void countChanged();
    void statusChanged();
    void logLine(const QString &line);
    void failed(const QString &message);
    void finished(const QString &name);

    // Für das gemeinsame Fortschrittsfenster
    void operationStarted(const QString &title);
    void operationProgress(double fraction, bool indeterminate, int step, int total, const QString &text);
    void operationFinished(int result, const QString &message);
    /// Liste von {base, names, fromVersion, toVersion, review, firstBuild, error}
    void reviewReady(const QVariantList &items);

private:
    void setBusy(bool busy);
    void setStatus(const QString &message);
    void fail(const QString &message);
    void requestAurInfo(const QStringList &names);
    void finishCheck();
    QProcess *makeProcess();
    QProcess *makeStreamingProcess();
    void fetchNext();
    void enterReview();
    void buildNext();
    void startInstall();
    void reportProgress(const QString &text);
    void finishOperation(int result, const QString &message);
    QString buildReview(BatchItem &item) const;
    QByteArray runProcess(const QString &program, const QStringList &arguments, int *exitCode);

    bool m_supported = false;
    bool m_enabled = false;
    bool m_busy = false;
    bool m_hasChecked = false;
    int m_foreignPackageCount = 0;
    QString m_statusMessage;
    QList<Entry> m_items;

    AppSettings *m_settings = nullptr;
    ProcessRunner m_processRunner;
    QNetworkAccessManager *m_network = nullptr;
    QMap<QString, QString> m_foreign;
    QHash<QString, AurPackage> m_remote;
    int m_pendingReplies = 0;
    QString m_checkError;
    DaemonClient *m_daemonClient = nullptr;

    Stage m_stage = Stage::Idle;
    QList<BatchItem> m_batch;
    int m_batchPos = 0;
    bool m_cancelRequested = false;
    QString m_operationTitle;
};

} // namespace lut
