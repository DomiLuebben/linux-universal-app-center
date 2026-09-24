#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>
#include <QDebug>
#include <QDir>
#include <QFile>
#include "liblut/backend/pacstall/PacstallBackend.h"
#include "liblut/repository/RepoManager.h"
#include "liblut/transaction/TransactionTypes.h"

using namespace lut;

// Treibt PacstallBackend gegen die echte pacstall-CLI und das echte PPR-Repository.
// Läuft nur im Wegwerf-Container, siehe scripts/verify-pacstall.sh.
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    // Strikter Schutz vor versehentlicher Ausführung auf dem Entwicklungsrechner
    if (!QFileInfo::exists(QStringLiteral("/.dockerenv")) || qEnvironmentVariable("LUT_INTEGRATION_ALLOW_MUTATIONS") != QLatin1String("1")) {
        qCritical() << "This integration test only runs in an explicitly enabled disposable Docker container.";
        return 77;
    }

    // Phase "setup": die PPR über denselben Code einrichten, den der Store im
    // Daemon nutzt (Schlüssel laden, Fingerabdruck prüfen, Quelle schreiben,
    // nur diese Quelle abrufen). Installiert wird pacstall danach vom Skript.
    if (argc > 1 && QString::fromLatin1(argv[1]) == QLatin1String("setup")) {
        RepoManager manager;
        manager.setForcedDistroFamily(DistroFamily::Debian);
        QString error;
        if (!manager.addPreset(QStringLiteral("pacstall"), &error)) {
            qFatal("PPR-Einrichtung fehlgeschlagen: %s", qPrintable(error));
        }
        QFile list(QStringLiteral("/etc/apt/sources.list.d/pacstall.list"));
        if (!list.open(QIODevice::ReadOnly)) qFatal("pacstall.list fehlt");
        const QByteArray line = list.readAll();
        qInfo().noquote() << "Quelle:" << QString::fromUtf8(line).trimmed();
        if (!line.contains("signed-by=/etc/apt/keyrings/ppr-keyring.gpg")) qFatal("Quelle ohne signed-by");
        if (!QFile::exists(QStringLiteral("/etc/apt/keyrings/ppr-keyring.gpg"))) qFatal("Schlüsselbund fehlt");
        bool listed = false;
        for (const auto &repo : manager.getRepositories()) listed = listed || repo.id == QLatin1String("pacstall");
        if (!listed) qFatal("PPR erscheint nicht in der Quellenliste");
        qInfo().noquote() << "PPR über RepoManager eingerichtet.";
        return 0;
    }

    if (!PacstallBackend::isPacstallAvailable()) {
        qFatal("Pacstall is not available inside the container");
    }

    PacstallBackend backend;

    QList<Event> events;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(300000); // 5 Minuten
    QObject::connect(&timeout, &QTimer::timeout, &app, [] {
        qFatal("Pacstall integration test timed out");
    });

    bool settled = false;
    QObject::connect(&backend, &Backend::eventEmitted, [&](const Event &event) {
        events.append(event);
        if (const auto *line = std::get_if<LogLine>(&event)) {
            qInfo().noquote() << line->text;
        }
        if (std::holds_alternative<PlanReady>(event) || std::holds_alternative<TransactionDone>(event)) {
            settled = true;
            loop.quit();
        }
    });

    auto begin = [&] { events.clear(); settled = false; };
    auto wait = [&] {
        if (!settled) { timeout.start(); loop.exec(); timeout.stop(); }
    };
    auto lastFailure = [&]() -> QString {
        QString summary;
        for (const auto &ev : events) {
            if (const auto *done = std::get_if<TransactionDone>(&ev)) summary = done->summary;
        }
        return summary;
    };
    auto planRevisionFromEvents = [&]() -> QString {
        QString revision;
        for (const auto &ev : events) {
            if (const auto *plan = std::get_if<PlanReady>(&ev)) revision = plan->planRevision;
        }
        return revision;
    };

    // 1. Prüfen, ob anstehende Updates erkannt werden
    QString checkError;
    auto updates = backend.checkUpdates(&checkError);
    if (!checkError.isEmpty()) qFatal("pacstall -Lu fehlgeschlagen: %s", qPrintable(checkError));
    qInfo() << "Gefundene Pacstall-Updates:" << updates.size();
    if (updates.isEmpty()) {
        qFatal("Expected at least 1 pacstall update (e.g. tree-sitter-cli-bin)");
    }
    const QString pkgName = updates.first().name;
    qInfo().noquote() << "Test-Paket:" << pkgName
                      << "Installiert:" << updates.first().installed
                      << "Verfügbar:" << updates.first().available;

    // 2. Plan erstellen
    begin();
    backend.planPacstallUpgrade({pkgName}, 0);
    wait();

    const QString initialRevision = planRevisionFromEvents();
    if (initialRevision.isEmpty()) {
        qFatal("PlanReady missing or without revision: %s", qPrintable(lastFailure()));
    }
    qInfo().noquote() << "Plan revision:" << initialRevision;

    // 3. Negativtest: Manipulation der Datei auf der Platte vor Commit -> Ablehnung
    QString pacscriptPath = QDir(backend.pacscriptsDir()).filePath(QStringLiteral("%1.pacscript").arg(pkgName));
    if (!QFile::exists(pacscriptPath)) {
        qFatal("Expected pacscript file at %s", qPrintable(pacscriptPath));
    }
    {
        QFile f(pacscriptPath);
        if (!f.open(QIODevice::Append)) {
            qFatal("Cannot append to %s for tamper test", qPrintable(pacscriptPath));
        }
        f.write("\n# TAMPERED\n");
        f.close();
    }

    begin();
    backend.commitPlan(initialRevision);
    wait();

    QString tamperFail = lastFailure();
    if (tamperFail.isEmpty() || !tamperFail.contains(QStringLiteral("Planrevision"))) {
        qFatal("Expected rejection due to tampered pacscript, but got: %s", qPrintable(tamperFail));
    }
    qInfo().noquote() << "Erfolgreich abgewiesen nach Manipulation:" << tamperFail;

    // 4. Negativtest: Falsche Revision übergeben -> Ablehnung
    begin();
    backend.planPacstallUpgrade({pkgName}, 0);
    wait();

    begin();
    backend.commitPlan(QStringLiteral("ungueltige-revision-12345"));
    wait();

    QString wrongRevFail = lastFailure();
    if (wrongRevFail.isEmpty() || !wrongRevFail.contains(QStringLiteral("Planrevision"))) {
        qFatal("Commit with wrong revision was not rejected: %s", qPrintable(wrongRevFail));
    }
    qInfo().noquote() << "Erfolgreich abgewiesen bei falscher Revision:" << wrongRevFail;

    // 5. Neu planen und erfolgreich commiten
    begin();
    backend.planPacstallUpgrade({pkgName}, 0);
    wait();

    const QString validRevision = planRevisionFromEvents();
    if (validRevision.isEmpty()) {
        qFatal("Re-planning before final commit failed: %s", qPrintable(lastFailure()));
    }

    begin();
    backend.commitPlan(validRevision);
    wait();

    if (events.isEmpty() || !std::holds_alternative<TransactionDone>(events.last())
        || std::get<TransactionDone>(events.last()).result != Result::Success) {
        qFatal("Commit failed: %s", qPrintable(lastFailure()));
    }
    qInfo().noquote() << "Commit erfolgreich abgeschlossen.";

    // 6. Nach Commit prüfen: Paket muss aktualisiert sein, checkUpdates() darf es nicht mehr melden
    auto postUpdates = backend.checkUpdates(&checkError);
    if (!checkError.isEmpty()) qFatal("pacstall -Lu nach dem Commit fehlgeschlagen: %s", qPrintable(checkError));
    for (const auto &u : postUpdates) {
        if (u.name == pkgName) {
            qFatal("Package %s is still listed in updates after successful commit!", qPrintable(pkgName));
        }
    }

    qInfo().noquote() << "Pacstall: Erkennung, Planrevision, Manipulationsabwehr, falsche Revision und echte Aktualisierung bestanden.";
    return 0;
}
