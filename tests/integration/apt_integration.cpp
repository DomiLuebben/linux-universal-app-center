#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>
#include <QDebug>
#include "liblut/backend/apt/AptBackend.h"
#include "liblut/catalog/apt/AptPackageCatalog.h"
#include "liblut/transaction/TransactionTypes.h"

using namespace lut;

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    // Strikter Schutz vor versehentlicher Ausführung auf dem Entwicklungsrechner
    if (!QFileInfo::exists(QStringLiteral("/.dockerenv")) || qEnvironmentVariable("LUT_INTEGRATION_ALLOW_MUTATIONS") != QLatin1String("1")) {
        qCritical() << "This integration test only runs in an explicitly enabled disposable Docker container.";
        return 77;
    }

    AptBackend backend(nullptr, QCoreApplication::applicationDirPath() + QStringLiteral("/../liblut/lut-apt-guard"));
    AptPackageCatalog storeCatalog;

    QList<Event> events;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(300000); // 5 Minuten Timeout für Netzwerk-/Paketdownloads
    QObject::connect(&timeout, &QTimer::timeout, &app, [] {
        qFatal("APT integration test timed out");
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

    auto begin = [&] {
        events.clear();
        settled = false;
    };

    auto wait = [&] {
        if (!settled) {
            timeout.start();
            loop.exec();
            timeout.stop();
        }
    };

    auto commit = [&](const QString &what) {
        begin();
        backend.commit();
        wait();
        if (events.isEmpty() || !std::holds_alternative<TransactionDone>(events.last()) ||
            std::get<TransactionDone>(events.last()).result != Result::Success) {
            QString errorMsg;
            for (const auto &ev : events) {
                if (const auto *done = std::get_if<TransactionDone>(&ev)) {
                    errorMsg = done->summary;
                }
            }
            qFatal("Commit failed for %s: %s", qPrintable(what), qPrintable(errorMsg));
        }
    };

    // 1. Katalogabfrage: Angebote und Kandidat für 'tree'
    const auto offers = storeCatalog.offersForPackage(QStringLiteral("tree"));
    if (offers.isEmpty()) {
        qFatal("StoreCatalog offers for 'tree' empty");
    }
    const auto candidate = storeCatalog.candidateOffer(QStringLiteral("tree"));
    if (!candidate.has_value() || !candidate->isCandidate) {
        qFatal("StoreCatalog candidate for 'tree' invalid");
    }
    if (candidate->packages.first().backend != QLatin1String("apt")) {
        qFatal("StoreCatalog backend not 'apt'");
    }
    if (candidate->downloadSize.value_or(0) <= 0 || candidate->installedSize.value_or(0) <= 0) {
        qFatal("StoreCatalog package sizes invalid for 'tree'");
    }

    // 2. Typisierte Store-Installation über planPackageTransaction
    TransactionIntent installIntent;
    installIntent.type = TransactionIntent::Type::Install;
    installIntent.targets = candidate->packages;

    begin();
    backend.planPackageTransaction(installIntent);
    wait();

    bool planSeen = false;
    QString planRevision;
    for (const auto &ev : events) {
        if (const auto *plan = std::get_if<PlanReady>(&ev)) {
            planSeen = true;
            planRevision = plan->planRevision;
            if (plan->downloadBytes <= 0) {
                qFatal("Planned download bytes missing");
            }
        }
    }
    if (!planSeen || planRevision.isEmpty()) {
        qFatal("PlanReady missing for Store install");
    }

    commit(QStringLiteral("Store install tree"));

    // 3. Bestandsabgleich nach Installation
    storeCatalog.reload();
    const auto installedState = storeCatalog.installedStateForPackage(QStringLiteral("tree"));
    if (!installedState.isFullyInstalled) {
        qFatal("StoreCatalog 'tree' not reported as fully installed after commit");
    }
    if (installedState.installedPackages.isEmpty() || installedState.installedPackages.first().name != QLatin1String("tree")) {
        qFatal("StoreCatalog installed package ref incorrect");
    }

    // 4. Typisierte Store-Entfernung über planPackageTransaction
    TransactionIntent removeIntent;
    removeIntent.type = TransactionIntent::Type::Remove;
    removeIntent.targets = {PackageRef{QStringLiteral("apt"), QString(), QStringLiteral("tree"), QString(), QString()}};

    begin();
    backend.planPackageTransaction(removeIntent);
    wait();

    commit(QStringLiteral("Store remove tree"));

    // 5. Bestandsabgleich nach Deinstallation (APT-03: selbst wenn config-files bleibt)
    storeCatalog.reload();
    const auto afterRemoveState = storeCatalog.installedStateForPackage(QStringLiteral("tree"));
    if (afterRemoveState.isFullyInstalled) {
        qFatal("StoreCatalog 'tree' still reported as installed after remove");
    }

    // 6. Schutz essentieller Pakete prüfen (APT-04)
    TransactionIntent removeAptIntent;
    removeAptIntent.type = TransactionIntent::Type::Remove;
    removeAptIntent.targets = {PackageRef{QStringLiteral("apt"), QString(), QStringLiteral("apt"), QString(), QString()}};

    begin();
    backend.planPackageTransaction(removeAptIntent);
    bool protectedBlocked = false;
    for (const auto &ev : events) {
        if (const auto *done = std::get_if<TransactionDone>(&ev)) {
            if (done->result == Result::Failed && done->summary.contains(QStringLiteral("geschützten"))) {
                protectedBlocked = true;
            }
        }
    }
    if (!protectedBlocked) {
        qFatal("Removal of essential package 'apt' was not blocked!");
    }

    // 7. Revisionsintegrität / Commit-Race-Schutz prüfen (APT-05, TX-09).
    //    Dafür braucht es einen frisch aufgelösten, gültigen Plan - sonst greift
    //    schon der vorgelagerte Wächter "Kein gültiger APT-Plan vorhanden" und
    //    die Fingerprintprüfung würde nie ausgeführt.
    TransactionIntent revisionIntent;
    revisionIntent.type = TransactionIntent::Type::Install;
    revisionIntent.targets = candidate->packages;

    begin();
    backend.planPackageTransaction(revisionIntent);
    wait();

    QString freshRevision;
    for (const auto &ev : events) {
        if (const auto *plan = std::get_if<PlanReady>(&ev)) {
            freshRevision = plan->planRevision;
        }
    }
    if (freshRevision.isEmpty()) {
        qFatal("PlanReady missing for revision integrity check");
    }

    // 7a. Abweichende Revision muss genau an der Fingerprintprüfung scheitern.
    begin();
    backend.commitPlan(QStringLiteral("sha256:divergentfingerprint0000000000000000"));
    QString mismatchSummary;
    for (const auto &ev : events) {
        if (const auto *done = std::get_if<TransactionDone>(&ev)) {
            if (done->result == Result::Failed && mismatchSummary.isEmpty()) {
                mismatchSummary = done->summary;
            }
        }
    }
    if (!mismatchSummary.contains(QStringLiteral("Plan-Fingerprint"))) {
        qFatal("Commit with mismatched planRevision was not rejected by the fingerprint guard: %s",
               qPrintable(mismatchSummary));
    }

    // 7b. Gegenprobe: die korrekte Revision muss den Wächter passieren.
    //     Sonst würde ein immer ablehnender Commit als "Schutz" durchgehen.
    begin();
    backend.planPackageTransaction(revisionIntent);
    wait();
    QString secondRevision;
    for (const auto &ev : events) {
        if (const auto *plan = std::get_if<PlanReady>(&ev)) {
            secondRevision = plan->planRevision;
        }
    }
    if (secondRevision != freshRevision) {
        qFatal("planRevision is not deterministic for an unchanged system");
    }

    begin();
    backend.commitPlan(secondRevision);
    wait();
    if (events.isEmpty() || !std::holds_alternative<TransactionDone>(events.last()) ||
        std::get<TransactionDone>(events.last()).result != Result::Success) {
        QString errorMsg;
        for (const auto &ev : events) {
            if (const auto *done = std::get_if<TransactionDone>(&ev)) {
                errorMsg = done->summary;
            }
        }
        qFatal("Commit with matching planRevision was rejected: %s", qPrintable(errorMsg));
    }

    storeCatalog.reload();
    if (!storeCatalog.installedStateForPackage(QStringLiteral("tree")).isFullyInstalled) {
        qFatal("Commit with matching planRevision did not install 'tree'");
    }

    qInfo() << "APT install/remove, candidate resolution, config-files state, protected package guard and planRevision integrity passed successfully.";
    return 0;
}
