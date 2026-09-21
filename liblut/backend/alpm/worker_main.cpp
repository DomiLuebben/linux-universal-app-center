#include <iostream>
#include <unistd.h>
#include <sys/utsname.h>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QProcess>
#include <QDateTime>
#include <QRegularExpression>

#ifdef HAVE_ALPM
#include <alpm.h>
#endif

#include "liblut/protocol/events.h"
#include "liblut/backend/alpm/SigLevelParser.h"

namespace {

void emitEvent(const lut::Event &event) {
    QJsonObject obj = lut::serializeEvent(event);
    obj[QStringLiteral("v")] = 1;
    QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    std::cout << line.constData() << "\n";
    std::cout.flush();
}

QStringList detectPacnewFiles() {
    QStringList result;
    QDir etcDir(QStringLiteral("/etc"));
    QStringList filters = {QStringLiteral("*.pacnew"), QStringLiteral("*.pacsave")};
    QDirIterator it(QStringLiteral("/etc"), filters, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        result.append(it.next());
    }
    return result;
}

bool detectKernelRebootNeeded() {
    struct utsname uts;
    if (uname(&uts) != 0) {
        return false;
    }
    QString runningKernel = QString::fromUtf8(uts.release);

    QDir modulesDir(QStringLiteral("/usr/lib/modules"));
    QStringList installedModules = modulesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &mod : installedModules) {
        if (mod != runningKernel && (mod.contains(QLatin1String("cachyos")) || mod.contains(QLatin1String("arch")))) {
            return true;
        }
    }
    return false;
}

QStringList detectServicesNeedingRestart() {
    QStringList services;
    QProcess proc;
    proc.start(QStringLiteral("systemctl"), {QStringLiteral("list-units"), QStringLiteral("--type=service"), QStringLiteral("--state=running"), QStringLiteral("--plain"), QStringLiteral("--no-legend")});
    if (proc.waitForFinished(2000)) {
        // Services querying heuristic
    }
    return services;
}

#ifdef HAVE_ALPM
void setupAlpmCallbacks(alpm_handle_t *handle, QStringList &createdPacnewFiles) {
    alpm_option_set_logcb(handle, [](void *, alpm_loglevel_t level, const char *fmt, va_list args) {
        // Debug- UND Function-Trace unterdrücken: beide sind hochfrequent und haben
        // zusammen die D-Bus-Verbindung geflutet. ALPM_LOG_FUNCTION allein zu
        // vergessen hätte die Flut nur halb abgestellt.
        if (level & (ALPM_LOG_DEBUG | ALPM_LOG_FUNCTION)) {
            return;
        }

        char buf[2048];
        vsnprintf(buf, sizeof(buf), fmt, args);
        QString text = QString::fromUtf8(buf).trimmed();
        if (text.isEmpty()) return;

        lut::LogLevel l = lut::LogLevel::Info;
        if (level & ALPM_LOG_ERROR) l = lut::LogLevel::Error;
        else if (level & ALPM_LOG_WARNING) l = lut::LogLevel::Warning;

        emitEvent(lut::LogLine{l, QStringLiteral("alpm"), text});
    }, nullptr);

    alpm_option_set_eventcb(handle, [](void *ctx, alpm_event_t *ev) {
        if (!ev) return;
        auto *pacnewFiles = static_cast<QStringList *>(ctx);

        switch (ev->type) {
            case ALPM_EVENT_CHECKDEPS_START:
                emitEvent(lut::PhaseChanged{lut::Phase::Resolve, QStringLiteral("Abhängigkeiten werden geprüft"), true});
                break;
            case ALPM_EVENT_FILECONFLICTS_START:
                emitEvent(lut::PhaseChanged{lut::Phase::Resolve, QStringLiteral("Dateikonflikte werden geprüft"), true});
                break;
            case ALPM_EVENT_RESOLVEDEPS_START:
                emitEvent(lut::PhaseChanged{lut::Phase::Resolve, QStringLiteral("Abhängigkeiten auflösen"), true});
                break;
            case ALPM_EVENT_INTEGRITY_START:
                emitEvent(lut::PhaseChanged{lut::Phase::Verify, QStringLiteral("Paketintegrität prüfen"), true});
                break;
            case ALPM_EVENT_KEYRING_START:
                emitEvent(lut::PhaseChanged{lut::Phase::Verify, QStringLiteral("Schlüsselbund prüfen"), true});
                break;
            case ALPM_EVENT_PKG_RETRIEVE_START:
                emitEvent(lut::PhaseChanged{lut::Phase::Download, QStringLiteral("Pakete herunterladen"), true});
                break;
            case ALPM_EVENT_DISKSPACE_START:
                emitEvent(lut::PhaseChanged{lut::Phase::TestTransaction, QStringLiteral("Speicherplatz prüfen"), true});
                break;
            case ALPM_EVENT_TRANSACTION_START:
                emitEvent(lut::PhaseChanged{lut::Phase::Commit, QStringLiteral("Pakete installieren"), false});
                break;
            case ALPM_EVENT_PACKAGE_OPERATION_START: {
                auto op = ev->package_operation;
                alpm_pkg_t *pkg = (op.operation == ALPM_PACKAGE_INSTALL || op.operation == ALPM_PACKAGE_UPGRADE) ? op.newpkg : op.oldpkg;
                QString name = QString::fromUtf8(alpm_pkg_get_name(pkg));
                QString ver = QString::fromUtf8(alpm_pkg_get_version(pkg));
                lut::PackageOp::Kind kind = lut::PackageOp::Kind::Upgrade;
                if (op.operation == ALPM_PACKAGE_INSTALL) kind = lut::PackageOp::Kind::Install;
                else if (op.operation == ALPM_PACKAGE_REMOVE) kind = lut::PackageOp::Kind::Remove;
                else if (op.operation == ALPM_PACKAGE_DOWNGRADE) kind = lut::PackageOp::Kind::Downgrade;
                qint64 size = alpm_pkg_get_isize(pkg);
                emitEvent(lut::ItemStarted{QStringLiteral("%1-%2").arg(name, ver), kind, size});
                break;
            }
            case ALPM_EVENT_PACKAGE_OPERATION_DONE: {
                auto op = ev->package_operation;
                alpm_pkg_t *pkg = (op.operation == ALPM_PACKAGE_INSTALL || op.operation == ALPM_PACKAGE_UPGRADE) ? op.newpkg : op.oldpkg;
                QString name = QString::fromUtf8(alpm_pkg_get_name(pkg));
                QString ver = QString::fromUtf8(alpm_pkg_get_version(pkg));
                emitEvent(lut::ItemFinished{QStringLiteral("%1-%2").arg(name, ver), true, QString()});
                break;
            }
            case ALPM_EVENT_HOOK_START:
                emitEvent(lut::PhaseChanged{lut::Phase::PostTransaction, QStringLiteral("ALPM-Hooks ausführen"), false});
                break;
            case ALPM_EVENT_HOOK_RUN_START: {
                QString hookName = QString::fromUtf8(ev->hook_run.name ? ev->hook_run.name : "hook");
                QString hookDesc = QString::fromUtf8(ev->hook_run.desc ? ev->hook_run.desc : "");
                QString displayName = hookDesc.isEmpty() ? hookName : QStringLiteral("%1 (%2)").arg(hookName, hookDesc);
                emitEvent(lut::ScriptletStarted{QStringLiteral("alpm-hook"), displayName});
                emitEvent(lut::LogLine{lut::LogLevel::Info, QStringLiteral("alpm-hook"), QStringLiteral("Starte Hook: %1").arg(displayName)});
                break;
            }
            case ALPM_EVENT_HOOK_RUN_DONE: {
                QString hookName = QString::fromUtf8(ev->hook_run.name ? ev->hook_run.name : "hook");
                emitEvent(lut::ScriptletFinished{QStringLiteral("alpm-hook"), hookName, 0});
                break;
            }
            case ALPM_EVENT_PACNEW_CREATED: {
                QString file = QString::fromUtf8(ev->pacnew_created.file);
                if (pacnewFiles) pacnewFiles->append(file + QStringLiteral(".pacnew"));
                emitEvent(lut::LogLine{lut::LogLevel::Warning, QStringLiteral("alpm"), QStringLiteral("Neue Konfigurationsdatei: %1.pacnew").arg(file)});
                break;
            }
            case ALPM_EVENT_PACSAVE_CREATED: {
                QString file = QString::fromUtf8(ev->pacsave_created.file);
                if (pacnewFiles) pacnewFiles->append(file + QStringLiteral(".pacsave"));
                emitEvent(lut::LogLine{lut::LogLevel::Warning, QStringLiteral("alpm"), QStringLiteral("Konfigurationssicherung: %1.pacsave").arg(file)});
                break;
            }
            case ALPM_EVENT_SCRIPTLET_INFO: {
                QString line = QString::fromUtf8(ev->scriptlet_info.line).trimmed();
                emitEvent(lut::LogLine{lut::LogLevel::Info, QStringLiteral("scriptlet"), line});
                break;
            }
            default:
                break;
        }
    }, &createdPacnewFiles);

    alpm_option_set_progresscb(handle, [](void *, alpm_progress_t, const char *pkg,
                                          int, size_t howmany, size_t current) {
        QString pkgName = QString::fromUtf8(pkg ? pkg : "");
        emitEvent(lut::ItemProgress{pkgName, static_cast<qint64>(current), static_cast<qint64>(howmany)});
    }, nullptr);

    alpm_option_set_dlcb(handle, [](void *, const char *,
                                    alpm_download_event_type_t type, void *data) {
        if (type == ALPM_DOWNLOAD_PROGRESS && data) {
            auto *p = static_cast<alpm_download_event_progress_t *>(data);
            emitEvent(lut::DownloadThroughput{0, p->downloaded, p->total});
        }
    }, nullptr);

    alpm_option_set_questioncb(handle, [](void *, alpm_question_t *q) {
        if (!q) return;
        switch (q->type) {
            case ALPM_QUESTION_IMPORT_KEY:
                q->import_key.import = 1;
                break;
            case ALPM_QUESTION_CONFLICT_PKG:
                q->conflict.remove = 1;
                break;
            case ALPM_QUESTION_CORRUPTED_PKG:
                q->corrupted.remove = 1;
                break;
            case ALPM_QUESTION_REPLACE_PKG:
                q->replace.replace = 1;
                break;
            case ALPM_QUESTION_SELECT_PROVIDER:
                q->select_provider.use_index = 0;
                break;
            default:
                break;
        }
    }, nullptr);
}

QStringList pacmanConfValues(const QStringList &args) {
    QProcess proc;
    proc.start(QStringLiteral("pacman-conf"), args);
    if (!proc.waitForFinished(3000) || proc.exitCode() != 0) {
        return {};
    }
    return QString::fromUtf8(proc.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

void configureRepositories(alpm_handle_t *handle) {
    // WICHTIG: alpm_initialize() startet mit Siglevel 0 – das bedeutet "keine
    // Signaturprüfung". ALPM_SIG_USE_DEFAULT an alpm_register_syncdb() löst nur
    // auf diesen Standard auf und ist ohne den folgenden Block wirkungslos.
    // Fail-secure-Ausgangswert entspricht "Required TrustedOnly DatabaseOptional".
    int defaultSig = lut::parseSigLevel(pacmanConfValues({QStringLiteral("SigLevel")}), 0);
    if (defaultSig < 0) {
        defaultSig = lut::kSecureSigLevelFallback;
        emitEvent(lut::LogLine{lut::LogLevel::Warning, QStringLiteral("alpm-worker"),
            QStringLiteral("SigLevel aus pacman.conf nicht lesbar – verwende strenge Vorgabe.")});
    }
    alpm_option_set_default_siglevel(handle, defaultSig);

    int localSig = lut::parseSigLevel(pacmanConfValues({QStringLiteral("LocalFileSigLevel")}), defaultSig);
    alpm_option_set_local_file_siglevel(handle, localSig < 0 ? defaultSig : localSig);

    int remoteSig = lut::parseSigLevel(pacmanConfValues({QStringLiteral("RemoteFileSigLevel")}), defaultSig);
    alpm_option_set_remote_file_siglevel(handle, remoteSig < 0 ? defaultSig : remoteSig);

    emitEvent(lut::LogLine{lut::LogLevel::Info, QStringLiteral("alpm-worker"),
        QStringLiteral("Signaturprüfung aktiv (SigLevel=%1).").arg(defaultSig)});

    // Pfade VOR dem Registrieren der Datenbanken setzen. libalpm initialisiert
    // GPGME beim ersten signaturrelevanten Zugriff und merkt sich das Ergebnis;
    // ein bis dahin ungesetztes gpgdir führt zu "Public keyring not found".
    // GPGDir aus pacman.conf lesen statt hart zu verdrahten.
    const QStringList gpgDirs = pacmanConfValues({QStringLiteral("GPGDir")});
    const QByteArray gpgDir = (gpgDirs.isEmpty() ? QStringLiteral("/etc/pacman.d/gnupg/")
                                                 : gpgDirs.first().trimmed()).toUtf8();
    alpm_option_set_gpgdir(handle, gpgDir.constData());

    const QStringList cacheDirs = pacmanConfValues({QStringLiteral("CacheDir")});
    if (cacheDirs.isEmpty()) {
        alpm_option_add_cachedir(handle, "/var/cache/pacman/pkg");
    } else {
        for (const QString &dir : cacheDirs) alpm_option_add_cachedir(handle, dir.trimmed().toUtf8().constData());
    }

    // /usr/share/libalpm/hooks/ setzt alpm_initialize() bereits selbst – hier
    // darf nur der Verwalter-Hookordner dazukommen, sonst steht er doppelt drin.
    const QStringList hookDirs = pacmanConfValues({QStringLiteral("HookDir")});
    if (hookDirs.isEmpty()) {
        alpm_option_add_hookdir(handle, "/etc/pacman.d/hooks/");
    } else {
        for (const QString &dir : hookDirs) alpm_option_add_hookdir(handle, dir.trimmed().toUtf8().constData());
    }

    QProcess proc;
    proc.start(QStringLiteral("pacman-conf"), {QStringLiteral("--repo-list")});
    if (proc.waitForFinished(3000)) {
        QStringList repos = QString::fromUtf8(proc.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &repo : repos) {
            QString cleanRepo = repo.trimmed();
            if (cleanRepo.isEmpty()) continue;

            // pacman-conf löst den effektiven SigLevel des Repos bereits auf.
            // Fällt das aus, bleibt ALPM_SIG_USE_DEFAULT – der oben gesetzte Standard.
            int repoSig = lut::parseSigLevel(pacmanConfValues(
                {QStringLiteral("--repo"), cleanRepo, QStringLiteral("SigLevel")}), 0);
            alpm_db_t *db = alpm_register_syncdb(handle, cleanRepo.toUtf8().constData(),
                                                 repoSig < 0 ? ALPM_SIG_USE_DEFAULT : repoSig);
            if (db) {
                QProcess srvProc;
                srvProc.start(QStringLiteral("pacman-conf"), {QStringLiteral("--repo"), cleanRepo, QStringLiteral("Server")});
                if (srvProc.waitForFinished(2000)) {
                    QStringList servers = QString::fromUtf8(srvProc.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                    for (const QString &server : servers) {
                        QString cleanServer = server.trimmed();
                        if (!cleanServer.isEmpty()) {
                            alpm_db_add_server(db, cleanServer.toUtf8().constData());
                        }
                    }
                }
            }
        }
    }

}
#endif

void runTestMode() {
    emitEvent(lut::LogLine{lut::LogLevel::Info, QStringLiteral("alpm-worker"), QStringLiteral("Starte ALPM-Worker im Testmodus...")});
    emitEvent(lut::PhaseChanged{lut::Phase::RefreshMetadata, QStringLiteral("Synchronisiere Paketdatenbanken"), true});
    emitEvent(lut::PhaseChanged{lut::Phase::Resolve, QStringLiteral("Prüfe Systemaktualisierungen"), true});
    emitEvent(lut::PhaseChanged{lut::Phase::Verify, QStringLiteral("Signaturen prüfen"), true});
    emitEvent(lut::PhaseChanged{lut::Phase::Download, QStringLiteral("Pakete herunterladen"), true});
    emitEvent(lut::DownloadThroughput{5242880, 10485760, 20971520});
    emitEvent(lut::PhaseChanged{lut::Phase::Commit, QStringLiteral("Pakete installieren"), false});
    emitEvent(lut::ItemStarted{QStringLiteral("linux-cachyos-6.13.5-1"), lut::PackageOp::Kind::Upgrade, 145000000});
    emitEvent(lut::ItemProgress{QStringLiteral("linux-cachyos"), 50, 100});
    emitEvent(lut::ItemFinished{QStringLiteral("linux-cachyos-6.13.5-1"), true, QString()});
    emitEvent(lut::PhaseChanged{lut::Phase::PostTransaction, QStringLiteral("ALPM-Hooks ausführen"), false});
    emitEvent(lut::ScriptletStarted{QStringLiteral("alpm-hook"), QStringLiteral("90-mkinitcpio-install.hook (Initramfs generieren)")});
    emitEvent(lut::ScriptletFinished{QStringLiteral("alpm-hook"), QStringLiteral("90-mkinitcpio-install.hook"), 0});
    emitEvent(lut::PhaseChanged{lut::Phase::Cleanup, QStringLiteral("Aufräumen"), false});
    emitEvent(lut::TransactionDone{lut::Result::Success, QStringLiteral("Testmodus-Transaktion erfolgreich abgeschlossen"), true, {}, 42});
    emitEvent(lut::PhaseChanged{lut::Phase::Finished, QStringLiteral("Fertig"), false});
}

} // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("lut-alpm-worker"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Linux Update Tool - ALPM Transaction Worker"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption sysupgradeOption(QStringLiteral("sysupgrade"), QStringLiteral("Führt eine vollständige Systemaktualisierung aus"));
    QCommandLineOption testModeOption(QStringLiteral("test-mode"), QStringLiteral("Führt den Worker im Testmodus aus (keine Root-Rechte erforderlich)"));
    QCommandLineOption dryRunOption(QStringLiteral("dry-run"), QStringLiteral("Simuliert die Transaktion ohne Änderungen am Dateisystem"));
    QCommandLineOption dbPathOption(QStringLiteral("dbpath"), QStringLiteral("Pfad zur Pacman-Datenbank"), QStringLiteral("path"), QStringLiteral("/var/lib/pacman"));
    QCommandLineOption rootOption(QStringLiteral("root"), QStringLiteral("Root-Verzeichnis"), QStringLiteral("path"), QStringLiteral("/"));

    parser.addOption(sysupgradeOption);
    parser.addOption(testModeOption);
    parser.addOption(dryRunOption);
    parser.addOption(dbPathOption);
    parser.addOption(rootOption);
    parser.process(app);

    if (parser.isSet(testModeOption)) {
        runTestMode();
        return 0;
    }

    QString dbPath = parser.value(dbPathOption);
    QString rootPath = parser.value(rootOption);
    QString lockFile = dbPath + QStringLiteral("/db.lck");

    if (QFile::exists(lockFile)) {
        emitEvent(lut::LogLine{lut::LogLevel::Error, QStringLiteral("alpm-worker"),
                               QStringLiteral("Kollision: Die Datenbank ist durch einen anderen Paketmanager gesperrt (%1 existiert).").arg(lockFile)});
        emitEvent(lut::TransactionDone{lut::Result::Failed,
                                       QStringLiteral("Ein anderer Paketmanager läuft bereits (db.lck gesperrt)."),
                                       false, {}, 0});
        return 1;
    }

#ifndef HAVE_ALPM
    emitEvent(lut::LogLine{lut::LogLevel::Error, QStringLiteral("alpm-worker"), QStringLiteral("libalpm Unterstützung nicht einkompiliert.")});
    emitEvent(lut::TransactionDone{lut::Result::Failed, QStringLiteral("libalpm nicht verfügbar"), false, {}, 0});
    return 1;
#else
    bool isDryRun = parser.isSet(dryRunOption);
    if (geteuid() != 0 && !isDryRun) {
        emitEvent(lut::LogLine{lut::LogLevel::Error, QStringLiteral("alpm-worker"), QStringLiteral("Root-Rechte erforderlich für ALPM-Transaktionen.")});
        emitEvent(lut::TransactionDone{lut::Result::Failed, QStringLiteral("Root-Rechte erforderlich"), false, {}, 0});
        return 1;
    }

    emitEvent(lut::LogLine{lut::LogLevel::Info, QStringLiteral("alpm-worker"), QStringLiteral("Initialisiere ALPM (root=%1, dbpath=%2)...").arg(rootPath, dbPath)});

    alpm_errno_t err;
    alpm_handle_t *handle = alpm_initialize(rootPath.toUtf8().constData(), dbPath.toUtf8().constData(), &err);
    if (!handle) {
        emitEvent(lut::LogLine{lut::LogLevel::Error, QStringLiteral("alpm-worker"), QStringLiteral("alpm_initialize fehlgeschlagen: %1").arg(alpm_strerror(err))});
        emitEvent(lut::TransactionDone{lut::Result::Failed, QStringLiteral("alpm_initialize fehlgeschlagen"), false, {}, 0});
        return 1;
    }

    QStringList createdPacnewFiles;
    setupAlpmCallbacks(handle, createdPacnewFiles);
    configureRepositories(handle);

    emitEvent(lut::PhaseChanged{lut::Phase::Resolve, QStringLiteral("Bereite Systemaktualisierung vor"), true});

    int transFlags = isDryRun ? ALPM_TRANS_FLAG_NOLOCK : 0;
    if (alpm_trans_init(handle, transFlags) != 0) {
        emitEvent(lut::LogLine{lut::LogLevel::Error, QStringLiteral("alpm-worker"), QStringLiteral("alpm_trans_init fehlgeschlagen: %1").arg(alpm_strerror(alpm_errno(handle)))});
        alpm_release(handle);
        emitEvent(lut::TransactionDone{lut::Result::Failed, QStringLiteral("Transaktionsinitialisierung fehlgeschlagen"), false, {}, 0});
        return 1;
    }

    if (alpm_sync_sysupgrade(handle, 0) != 0) {
        alpm_errno_t err = alpm_errno(handle);
        QString errStr = QString::fromUtf8(alpm_strerror(err));
        emitEvent(lut::LogLine{lut::LogLevel::Error, QStringLiteral("alpm-worker"), QStringLiteral("alpm_sync_sysupgrade fehlgeschlagen: %1").arg(errStr)});
        alpm_trans_release(handle);
        alpm_release(handle);
        emitEvent(lut::PhaseChanged{lut::Phase::Failed, QStringLiteral("Abhängigkeitsprüfung fehlgeschlagen: %1").arg(errStr), false});
        emitEvent(lut::TransactionDone{lut::Result::Failed, QStringLiteral("Abhängigkeitsauflösung fehlgeschlagen: %1").arg(errStr), false, {}, 0});
        return 1;
    }

    alpm_list_t *data = nullptr;
    if (alpm_trans_prepare(handle, &data) != 0) {
        alpm_errno_t err = alpm_errno(handle);
        QString errStr = QString::fromUtf8(alpm_strerror(err));
        emitEvent(lut::LogLine{lut::LogLevel::Error, QStringLiteral("alpm-worker"), QStringLiteral("alpm_trans_prepare fehlgeschlagen: %1").arg(errStr)});
        if (data) {
            for (alpm_list_t *i = data; i; i = alpm_list_next(i)) {
                if (err == ALPM_ERR_UNSATISFIED_DEPS) {
                    auto *dep = static_cast<alpm_depmissing_t *>(i->data);
                    if (dep) {
                        char *depstr = alpm_dep_compute_string(dep->depend);
                        emitEvent(lut::LogLine{lut::LogLevel::Error, QStringLiteral("alpm"),
                            QStringLiteral("Fehlende Abhängigkeit für '%1': %2").arg(QString::fromUtf8(dep->target), QString::fromUtf8(depstr ? depstr : ""))});
                        free(depstr);
                    }
                } else if (err == ALPM_ERR_CONFLICTING_DEPS) {
                    auto *conflict = static_cast<alpm_conflict_t *>(i->data);
                    if (conflict && conflict->package1 && conflict->package2) {
                        emitEvent(lut::LogLine{lut::LogLevel::Error, QStringLiteral("alpm"),
                            QStringLiteral("Paketkonflikt: '%1' kollidiert mit '%2'").arg(
                                QString::fromUtf8(alpm_pkg_get_name(conflict->package1)),
                                QString::fromUtf8(alpm_pkg_get_name(conflict->package2)))});
                    }
                }
            }
        }
        alpm_trans_release(handle);
        alpm_release(handle);
        emitEvent(lut::PhaseChanged{lut::Phase::Failed, QStringLiteral("Vorbereitung fehlgeschlagen: %1").arg(errStr), false});
        emitEvent(lut::TransactionDone{lut::Result::Failed, QStringLiteral("Transaktionsvorbereitung fehlgeschlagen: %1").arg(errStr), false, {}, 0});
        return 1;
    }

    // Leere Transaktion ist ein Erfolg, kein Fehler. libalpm setzt den Zustand
    // PREPARED nur, wenn es tatsächlich Ziele gibt: alpm_trans_prepare() liefert
    // dann zwar 0, alpm_trans_commit() scheitert aber mit
    // ALPM_ERR_TRANS_NOT_PREPARED ("Vorgang nicht vorbereitet"). Genau das
    // passiert, wenn alle Kandidaten lokal neuer sind als im Repository.
    if (alpm_trans_get_add(handle) == nullptr && alpm_trans_get_remove(handle) == nullptr) {
        emitEvent(lut::LogLine{lut::LogLevel::Info, QStringLiteral("alpm-worker"),
            QStringLiteral("Keine Pakete zu aktualisieren – das System ist aktuell.")});
        alpm_trans_release(handle);
        alpm_release(handle);
        emitEvent(lut::TransactionDone{lut::Result::Success,
            QStringLiteral("Keine Aktualisierungen verfügbar – das System ist aktuell."), false, {}, 0});
        emitEvent(lut::PhaseChanged{lut::Phase::Finished, QStringLiteral("Fertig"), false});
        return 0;
    }

    if (!isDryRun) {
        if (alpm_trans_commit(handle, &data) != 0) {
            alpm_errno_t err = alpm_errno(handle);
            QString errStr = QString::fromUtf8(alpm_strerror(err));
            emitEvent(lut::LogLine{lut::LogLevel::Error, QStringLiteral("alpm-worker"), QStringLiteral("alpm_trans_commit fehlgeschlagen: %1").arg(errStr)});
            if (data) {
                for (alpm_list_t *i = data; i; i = alpm_list_next(i)) {
                    if (err == ALPM_ERR_FILE_CONFLICTS) {
                        auto *fc = static_cast<alpm_fileconflict_t *>(i->data);
                        if (fc) {
                            QString conflictTarget = fc->ctarget ? QString::fromUtf8(fc->ctarget) : QStringLiteral("Dateisystem");
                            emitEvent(lut::LogLine{lut::LogLevel::Error, QStringLiteral("alpm"),
                                QStringLiteral("Dateikonflikt: Paket '%1' kollidiert bei '%2' mit '%3'")
                                    .arg(QString::fromUtf8(fc->target), QString::fromUtf8(fc->file), conflictTarget)});
                        }
                    } else if (err == ALPM_ERR_PKG_INVALID || err == ALPM_ERR_PKG_INVALID_SIG) {
                        auto *pkgName = static_cast<const char *>(i->data);
                        if (pkgName) {
                            emitEvent(lut::LogLine{lut::LogLevel::Error, QStringLiteral("alpm"),
                                QStringLiteral("Ungültiges Paket oder fehlerhafte Signatur: %1").arg(QString::fromUtf8(pkgName))});
                        }
                    }
                }
            }
            alpm_trans_release(handle);
            alpm_release(handle);
            emitEvent(lut::PhaseChanged{lut::Phase::Failed, QStringLiteral("Installation fehlgeschlagen: %1").arg(errStr), false});
            emitEvent(lut::TransactionDone{lut::Result::Failed, QStringLiteral("Transaktionsausführung fehlgeschlagen: %1").arg(errStr), false, {}, 0});
            return 1;
        }
    }

    alpm_trans_release(handle);
    alpm_release(handle);

    emitEvent(lut::PhaseChanged{lut::Phase::Cleanup, QStringLiteral("Aufräumen & Systemzustand prüfen"), false});

    QStringList allPacnew = detectPacnewFiles();
    bool rebootNeeded = detectKernelRebootNeeded();
    QStringList restartServices = detectServicesNeedingRestart();

    emitEvent(lut::TransactionDone{
        lut::Result::Success,
        QStringLiteral("System erfolgreich aktualisiert"),
        rebootNeeded,
        restartServices,
        0
    });
    emitEvent(lut::PhaseChanged{lut::Phase::Finished, QStringLiteral("Fertig"), false});

    return 0;
#endif
}
