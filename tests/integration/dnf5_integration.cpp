#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>
#include <QDebug>
#include "liblut/backend/dnf5/Dnf5Backend.h"
using namespace lut;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (!QFileInfo::exists(QStringLiteral("/.dockerenv")) || qEnvironmentVariable("LUT_INTEGRATION_ALLOW_MUTATIONS") != QLatin1String("1")) {
        qCritical() << "This integration test only runs in an explicitly enabled disposable Docker container."; return 77;
    }
    Dnf5Backend backend;
    QList<Event> events;
    QEventLoop loop;
    QTimer timeout; timeout.setSingleShot(true); timeout.setInterval(300000);
    QObject::connect(&timeout, &QTimer::timeout, &app, [] { qFatal("Integration test timeout"); });
    bool settled = false;
    QObject::connect(&backend, &Backend::eventEmitted, [&](const Event &event) {
        events.append(event);
        if (const auto *line = std::get_if<LogLine>(&event)) qInfo().noquote() << line->text;
        if (std::holds_alternative<PlanReady>(event) || std::holds_alternative<TransactionDone>(event)) {
            settled = true; loop.quit();
        }
    });
    // Bei einem leeren Plan meldet commit() noch innerhalb des Aufrufs fertig.
    // Ohne diese Prüfung liefe die Schleife in die Zeitüberschreitung, weil das
    // Ereignis bereits vor loop.exec() eingetroffen ist.
    auto begin = [&] { events.clear(); settled = false; };
    auto wait = [&] { if (!settled) { timeout.start(); loop.exec(); timeout.stop(); } };

    // Plant einen Befehl und liefert den Plan zurück. Ein leerer Plan ist gültig
    // (DNF5 meldet "Nothing to do."), ein fehlgeschlagener Plan bricht ab.
    auto plan = [&](const QString &command, const QStringList &args) {
        begin(); UpgradeOptions options; options.refreshFirst = false;
        backend.planCommand(command, args, options); wait();
        for (const auto &event : events)
            if (const auto *done = std::get_if<TransactionDone>(&event))
                qFatal("Planning %s failed: %s", qPrintable(command), qPrintable(done->summary));
        PlanReady result;
        bool seen = false;
        for (const auto &event : events) if (const auto *item = std::get_if<PlanReady>(&event)) { result = *item; seen = true; }
        if (!seen) qFatal("No plan for %s", qPrintable(command));
        return result;
    };
    auto commit = [&](const QString &what) {
        begin(); backend.commit(); wait();
        if (events.isEmpty() || !std::holds_alternative<TransactionDone>(events.last()) ||
            std::get<TransactionDone>(events.last()).result != Result::Success)
            qFatal("Commit failed: %s", qPrintable(what));
    };
    auto expectOp = [&](const PlanReady &p, const QString &name, PackageOp::Kind kind) {
        for (const auto &op : p.ops)
            if (op.name == name && op.kind == kind) {
                if (op.installedSize <= 0) qFatal("Missing real size for %s", qPrintable(name));
                return op;
            }
        qFatal("Expected %s action for %s missing", qPrintable(PackageOp::kindToString(kind)), qPrintable(name));
    };
    // installedPackages(filter) sucht absichtlich auch in den Zusammenfassungen.
    // Für Zusicherungen muss deshalb exakt auf den Paketnamen geprüft werden,
    // sonst trifft ein beliebiges Paket, dessen Beschreibung das Wort enthält.
    auto installedVersion = [&](const QString &name) {
        for (const auto &pkg : backend.installedPackages(name))
            if (pkg.name == name) return pkg.version;
        return QString();
    };
    auto installed = [&](const QString &name) { return !installedVersion(name).isEmpty(); };

    // 1. install / reinstall / remove mit echten Größen, Verlauf und Changelog
    if (installed(QStringLiteral("tree"))) qFatal("Expected a fresh container without tree");
    expectOp(plan(QStringLiteral("install"), {QStringLiteral("tree")}), QStringLiteral("tree"), PackageOp::Kind::Install);
    commit(QStringLiteral("install tree"));
    if (!installed(QStringLiteral("tree"))) qFatal("Installed query missing tree");
    if (backend.history().isEmpty()) qFatal("History is empty");
    if (backend.changelog(QStringLiteral("tree")).isEmpty()) qFatal("Changelog is empty");
    expectOp(plan(QStringLiteral("reinstall"), {QStringLiteral("tree")}), QStringLiteral("tree"), PackageOp::Kind::Reinstall);
    commit(QStringLiteral("reinstall tree"));
    expectOp(plan(QStringLiteral("remove"), {QStringLiteral("tree")}), QStringLiteral("tree"), PackageOp::Kind::Remove);
    commit(QStringLiteral("remove tree"));
    if (installed(QStringLiteral("tree"))) qFatal("remove did not take effect");

    // 2. history undo auf die Entfernung: tree muss danach wieder da sein.
    //    Bewusst die Entfernung und nicht die Neuinstallation zurücknehmen –
    //    nur so hat die Rücknahme eine nachweisbare Wirkung.
    const auto lastTransaction = backend.history(1);
    if (lastTransaction.isEmpty()) qFatal("History is empty before undo");
    plan(QStringLiteral("history undo"), {QString::number(lastTransaction.first().id)});
    commit(QStringLiteral("history undo"));
    if (!installed(QStringLiteral("tree"))) qFatal("history undo did not restore tree");

    // 3. swap: entfernt tree und installiert which in einer Transaktion.
    const auto swapped = plan(QStringLiteral("swap"), {QStringLiteral("tree"), QStringLiteral("which")});
    expectOp(swapped, QStringLiteral("tree"), PackageOp::Kind::Remove);
    expectOp(swapped, QStringLiteral("which"), PackageOp::Kind::Install);
    commit(QStringLiteral("swap tree which"));
    if (installed(QStringLiteral("tree")) || !installed(QStringLiteral("which"))) qFatal("swap did not take effect");

    // 4. downgrade: prüft, dass der Plan die exakt installierte Version trifft.
    const auto newer = installedVersion(QStringLiteral("which"));
    if (newer.isEmpty()) qFatal("which not found after swap");
    expectOp(plan(QStringLiteral("downgrade"), {QStringLiteral("which")}), QStringLiteral("which"), PackageOp::Kind::Downgrade);
    commit(QStringLiteral("downgrade which"));
    const auto older = installedVersion(QStringLiteral("which"));
    if (older.isEmpty() || older == newer)
        qFatal("downgrade did not change the installed version: %s -> %s", qPrintable(newer), qPrintable(older));

    // 5. distro-sync und autoremove ohne Argumente (Pfad für argumentlose Befehle).
    //    distro-sync hebt das Downgrade wieder auf und ist damit kein Leerlauf.
    plan(QStringLiteral("distro-sync"), {});
    commit(QStringLiteral("distro-sync"));
    plan(QStringLiteral("autoremove"), {});
    commit(QStringLiteral("autoremove"));

    // Aufräumen, damit der Container keinen Rest hinterlässt.
    expectOp(plan(QStringLiteral("remove"), {QStringLiteral("which")}), QStringLiteral("which"), PackageOp::Kind::Remove);
    commit(QStringLiteral("remove which"));
    if (installed(QStringLiteral("which"))) qFatal("which remains installed");

    qInfo() << "DNF5 install/reinstall/remove/history-undo/swap/downgrade/distro-sync/autoremove passed"
            << "- mit echten Größen, installierten Paketen, Changelog und Verlauf.";
}
