#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>
#include <QDebug>
#include "liblut/backend/flatpak/FlatpakBackend.h"
#include "liblut/catalog/flatpak/FlatpakPackageCatalog.h"
#include "liblut/transaction/TransactionTypes.h"

using namespace lut;

// Dieser Test schließt die Lücke zwischen den beiden vorhandenen Prüfungen:
// store_flatpak_test treibt FlatpakBackend gegen einen eingeschleusten, gefälschten
// Prozessaufruf; scripts/verify-flatpak-container.sh prüft die echte flatpak-CLI,
// aber ohne unser Backend. Keine von beiden belegt, dass unser Backend eine echte
// Flatpak-Installation tatsächlich auslöst. Genau das prüft dieser Test.
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    // Strikter Schutz vor versehentlicher Ausführung auf dem Entwicklungsrechner
    if (!QFileInfo::exists(QStringLiteral("/.dockerenv")) || qEnvironmentVariable("LUT_INTEGRATION_ALLOW_MUTATIONS") != QLatin1String("1")) {
        qCritical() << "This integration test only runs in an explicitly enabled disposable Docker container.";
        return 77;
    }

    const QString appId = qEnvironmentVariable("LUT_FLATPAK_TEST_APP", QStringLiteral("org.gnome.Calculator"));

    FlatpakBackend backend;           // kein eingeschleuster Runner: echte flatpak-CLI
    FlatpakPackageCatalog storeCatalog;

    if (!FlatpakBackend::isFlatpakAvailable()) {
        qFatal("flatpak is not available inside the container");
    }

    QList<Event> events;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(900000); // Flatpak lädt Laufzeitumgebungen; großzügig bemessen
    QObject::connect(&timeout, &QTimer::timeout, &app, [] {
        qFatal("Flatpak integration test timed out");
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
    auto commitAndExpectSuccess = [&](const QString &revision, const QString &what) {
        begin();
        backend.commitPlan(revision);
        wait();
        if (events.isEmpty() || !std::holds_alternative<TransactionDone>(events.last())
            || std::get<TransactionDone>(events.last()).result != Result::Success) {
            qFatal("Commit failed for %s: %s", qPrintable(what), qPrintable(lastFailure()));
        }
    };

    // 1. Katalogabfrage über unseren Katalog, nicht über die rohe CLI
    const auto candidate = storeCatalog.candidateOffer(appId);
    if (!candidate.has_value() || candidate->packages.isEmpty()) {
        qFatal("FlatpakPackageCatalog found no candidate for %s", qPrintable(appId));
    }
    if (candidate->packages.first().backend != QLatin1String("flatpak")) {
        qFatal("Candidate backend is not 'flatpak'");
    }
    qInfo().noquote() << "Kandidat:" << candidate->packages.first().name
                      << "Remote:" << candidate->packages.first().repoId
                      << "Version:" << candidate->packages.first().version;

    // 2. Installation über planPackageTransaction, an die Planrevision gebunden
    TransactionIntent installIntent;
    installIntent.type = TransactionIntent::Type::Install;
    installIntent.targets = candidate->packages;

    begin();
    backend.planPackageTransaction(installIntent);
    wait();

    const QString installRevision = planRevisionFromEvents();
    if (installRevision.isEmpty()) {
        qFatal("PlanReady missing or without planRevision for install: %s", qPrintable(lastFailure()));
    }

    // Planen darf nichts installieren.
    storeCatalog.reload();
    if (storeCatalog.installedStateForPackage(appId).isFullyInstalled) {
        qFatal("Planning already installed %s", qPrintable(appId));
    }

    // 2a. Abweichende Revision muss abgewiesen werden (Abschnitt 6.4).
    begin();
    backend.commitPlan(QStringLiteral("commit-der-nicht-stimmt"));
    QString mismatch = lastFailure();
    if (mismatch.isEmpty()) {
        qFatal("Commit with a wrong plan revision was not rejected");
    }
    qInfo().noquote() << "Abweichende Revision abgewiesen:" << mismatch;

    // 2b. Korrekte Revision muss durchgehen.
    begin();
    backend.planPackageTransaction(installIntent);
    wait();
    const QString freshRevision = planRevisionFromEvents();
    if (freshRevision.isEmpty()) {
        qFatal("Re-planning before commit produced no revision");
    }
    commitAndExpectSuccess(freshRevision, QStringLiteral("Flatpak install"));

    // 3. Bestand über unseren Katalog prüfen
    storeCatalog.reload();
    const auto installed = storeCatalog.installedStateForPackage(appId);
    if (!installed.isFullyInstalled) {
        qFatal("FlatpakPackageCatalog does not report %s as installed after commit", qPrintable(appId));
    }
    if (installed.installedPackages.isEmpty()
        || installed.installedPackages.first().backend != QLatin1String("flatpak")) {
        qFatal("Installed package reference is not a flatpak reference");
    }
    if (installed.launchableDesktopIds.isEmpty()) {
        qFatal("No launchable desktop id resolved for the installed flatpak");
    }
    qInfo().noquote() << "Installiert, startbar über:" << installed.launchableDesktopIds.join(QLatin1Char(','));

    // 4. Die exportierte Desktop-Datei muss tatsächlich existieren
    const QString exported = QStringLiteral("/var/lib/flatpak/exports/share/applications/%1.desktop").arg(appId);
    if (!QFileInfo::exists(exported)) {
        qFatal("Exported desktop file missing: %s", qPrintable(exported));
    }

    // 5. Entfernung über denselben typisierten Weg
    TransactionIntent removeIntent;
    removeIntent.type = TransactionIntent::Type::Remove;
    removeIntent.targets = installed.installedPackages;

    begin();
    backend.planPackageTransaction(removeIntent);
    wait();
    const QString removeRevision = planRevisionFromEvents();
    if (removeRevision.isEmpty()) {
        qFatal("PlanReady missing for remove: %s", qPrintable(lastFailure()));
    }
    commitAndExpectSuccess(removeRevision, QStringLiteral("Flatpak remove"));

    // 6. Erneut prüfen
    storeCatalog.reload();
    if (storeCatalog.installedStateForPackage(appId).isFullyInstalled) {
        qFatal("%s is still reported as installed after remove", qPrintable(appId));
    }
    if (QFileInfo::exists(exported)) {
        qFatal("Exported desktop file still present after remove: %s", qPrintable(exported));
    }

    qInfo().noquote() << "Flatpak: Kandidat, Planbindung, echte Installation, Bestand, Startbarkeit "
                         "und Entfernung über FlatpakBackend und FlatpakPackageCatalog bestanden.";
    return 0;
}
