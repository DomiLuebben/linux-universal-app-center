#include <QtTest>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>

#include "liblut/repository/RepoManager.h"
#include "linux-app-store/AppSettings.h"
#include "linux-app-store/DaemonClient.h"
#include "linux-app-store/AurUpdates.h"
#include "linux-app-store/models/FlatpakUpdates.h"
#include "linux-app-store/models/SnapUpdates.h"
#include "linux-app-store/models/PacstallUpdates.h"
#include "linux-app-store/OperationMonitor.h"
#include "linux-app-store/catalog/CatalogService.h"
#include "linux-app-store/catalog/ApplicationStore.h"
#include "linux-app-store/models/StoreModel.h"
#include "linux-app-store/models/RepositoriesModel.h"
#include "linux-app-store/theme/SystemPalette.h"
#include "linux-app-store/media/MediaCache.h"

using namespace lut;

class PhantomSourcesTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testNoPhantomSourcesWhenUnavailable();
    void testGegenprobeFlatpakDetectedWhenAvailable();

private:
    void collectVisibleTexts(QObject *obj, QStringList &out, QSet<QObject *> &visited);
};

void PhantomSourcesTest::initTestCase() {
    qmlRegisterType<lut::StoreModel>("LinuxAppStore", 1, 0, "StoreModel");
    qmlRegisterType<lut::StoreModel>("LinuxUpdateTool", 1, 0, "StoreModel");
}

void PhantomSourcesTest::collectVisibleTexts(QObject *obj, QStringList &out, QSet<QObject *> &visited) {
    if (!obj || visited.contains(obj)) return;
    visited.insert(obj);

    if (auto *item = qobject_cast<QQuickItem *>(obj)) {
        if (!item->isVisible()) {
            return;
        }
    }

    // 1. Text-Eigenschaft
    QVariant textProp = obj->property("text");
    if (textProp.isValid() && textProp.userType() == QMetaType::QString) {
        QString s = textProp.toString().trimmed();
        if (!s.isEmpty()) {
            out.append(s);
        }
    }

    // 2. Title-Eigenschaft
    QVariant titleProp = obj->property("title");
    if (titleProp.isValid() && titleProp.userType() == QMetaType::QString) {
        QString s = titleProp.toString().trimmed();
        if (!s.isEmpty()) {
            out.append(s);
        }
    }

    // 3. ComboBox: aktueller Text und alle Modellwerte einsammeln
    if (obj->inherits("QQuickComboBox") || obj->metaObject()->className() == QByteArray("QQuickComboBox")) {
        QVariant curText = obj->property("currentText");
        if (curText.isValid() && !curText.toString().trimmed().isEmpty()) {
            out.append(curText.toString().trimmed());
        }
        QVariant modelProp = obj->property("model");
        if (modelProp.canConvert<QVariantList>()) {
            const auto list = modelProp.toList();
            for (const auto &entry : list) {
                if (entry.userType() == QMetaType::QString) {
                    out.append(entry.toString().trimmed());
                } else if (entry.canConvert<QVariantMap>()) {
                    const auto map = entry.toMap();
                    if (map.contains(QStringLiteral("label"))) {
                        out.append(map.value(QStringLiteral("label")).toString().trimmed());
                    }
                    if (map.contains(QStringLiteral("text"))) {
                        out.append(map.value(QStringLiteral("text")).toString().trimmed());
                    }
                }
            }
        }
    }

    // Kindelemente rekursiv durchsuchen
    for (QObject *child : obj->children()) {
        collectVisibleTexts(child, out, visited);
    }
    if (auto *item = qobject_cast<QQuickItem *>(obj)) {
        for (QQuickItem *childItem : item->childItems()) {
            collectVisibleTexts(childItem, out, visited);
        }
    }
}

void PhantomSourcesTest::testNoPhantomSourcesWhenUnavailable() {
    // 6.3 Pflichttest „kein Geisterquellen-Text“:
    // Aufbau mit Flatpak, Snap, AUR und Pacstall nicht vorhanden
    // (setForceAvailable(false) bzw. Stubs; Distro Arch ohne AUR-Freigabe).
    RepoManager::instance().setForcedDistroFamily(DistroFamily::Arch);

    QTemporaryDir settingsDir;
    AppSettings appSettings(settingsDir.path() + QStringLiteral("/settings.conf"));
    appSettings.setAurEnabled(false);
    appSettings.setPacstallEnabled(false);

    DaemonClient client;
    AurUpdates aur;
    aur.setAppSettings(&appSettings);
    aur.setDaemonClient(&client);
    aur.setProcessRunner([](const QString &, const QStringList &, int *exitCode) -> QByteArray {
        if (exitCode) *exitCode = 0;
        return QByteArray();
    });
    aur.checkForeignPackages();

    QCOMPARE(aur.enabled(), false);
    QCOMPARE(aur.available(), false);
    QCOMPARE(aur.hasForeignPackages(), false);

    FlatpakUpdates flatpak;
    flatpak.setForceAvailable(false);
    flatpak.setDaemonClient(&client);
    QCOMPARE(flatpak.available(), false);

    SnapUpdates snap;
    snap.setForceAvailable(false);
    snap.setDaemonClient(&client);
    QCOMPARE(snap.available(), false);

    PacstallUpdates pacstall;
    pacstall.setAppSettings(&appSettings);
    pacstall.setForceSupported(false);
    QCOMPARE(pacstall.available(), false);

    CatalogService::setFlatpakAvailableOverride(false);
    CatalogService catalogService;
    catalogService.setLoadStdDataLocations(false);
    catalogService.load();

    ApplicationStore appStore(&catalogService, nullptr);
    StoreModel storeModel(&appStore);
    StoreModel installedStoreModel(&appStore);
    installedStoreModel.setInstalledOnly(true);

    OperationMonitor operationMonitor(&client, &flatpak, &snap, &aur, &pacstall);
    SystemPalette *palette = SystemPalette::instance();
    MediaCache mediaCache;

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(PROJECT_DIR "/linux-app-store"));
    engine.addImportPath(QStringLiteral(PROJECT_DIR "/linux-app-store/qml"));

    engine.rootContext()->setContextProperty(QStringLiteral("Theme"), palette);
    engine.rootContext()->setContextProperty(QStringLiteral("daemonClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("updatesModel"), client.updatesModel());
    engine.rootContext()->setContextProperty(QStringLiteral("progressModel"), client.progressModel());
    engine.rootContext()->setContextProperty(QStringLiteral("logModel"), client.logModel());
    engine.rootContext()->setContextProperty(QStringLiteral("installedModel"), client.installedModel());
    engine.rootContext()->setContextProperty(QStringLiteral("historyModel"), client.historyModel());
    engine.rootContext()->setContextProperty(QStringLiteral("aurUpdates"), &aur);
    engine.rootContext()->setContextProperty(QStringLiteral("flatpakUpdates"), &flatpak);
    engine.rootContext()->setContextProperty(QStringLiteral("snapUpdates"), &snap);
    engine.rootContext()->setContextProperty(QStringLiteral("pacstallUpdates"), &pacstall);
    engine.rootContext()->setContextProperty(QStringLiteral("operationMonitor"), &operationMonitor);
    engine.rootContext()->setContextProperty(QStringLiteral("appStore"), &appStore);
    engine.rootContext()->setContextProperty(QStringLiteral("storeModel"), &storeModel);
    engine.rootContext()->setContextProperty(QStringLiteral("installedStoreModel"), &installedStoreModel);
    engine.rootContext()->setContextProperty(QStringLiteral("repositoriesModel"), client.repositoriesModel());
    engine.rootContext()->setContextProperty(QStringLiteral("appSettings"), &appSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("mediaCache"), &mediaCache);
    engine.rootContext()->setContextProperty(QStringLiteral("initialManageTab"), 0);
    engine.rootContext()->setContextProperty(QStringLiteral("initialInstalledTab"), 0);
    engine.rootContext()->setContextProperty(QStringLiteral("initialSettingsTab"), 0);
    engine.rootContext()->setContextProperty(QStringLiteral("initialNavIndex"), 0);
    engine.rootContext()->setContextProperty(QStringLiteral("initialAppKey"), QString());
    engine.rootContext()->setContextProperty(QStringLiteral("screenshotRiskSource"), QString());
    engine.rootContext()->setContextProperty(QStringLiteral("trayManager"), nullptr);
    engine.rootContext()->setContextProperty(QStringLiteral("startInTray"), false);

    const QStringList pages = {
        QStringLiteral("/linux-app-store/qml/pages/Discover.qml"),
        QStringLiteral("/linux-app-store/qml/pages/Search.qml"),
        QStringLiteral("/linux-app-store/qml/pages/AppDetails.qml"),
        QStringLiteral("/linux-app-store/qml/pages/Updates.qml"),
        QStringLiteral("/linux-app-store/qml/pages/Manage.qml"),
        QStringLiteral("/linux-app-store/qml/components/AppTile.qml"),
        QStringLiteral("/linux-app-store/qml/pages/PlanPreview.qml"),
        QStringLiteral("/linux-app-store/qml/components/OperationProgressView.qml"),
        QStringLiteral("/linux-app-store/qml/pages/Report.qml"),
        QStringLiteral("/linux-app-store/qml/pages/Installed.qml"),
        QStringLiteral("/linux-app-store/qml/pages/History.qml"),
        QStringLiteral("/linux-app-store/qml/pages/Logs.qml"),
        QStringLiteral("/linux-app-store/qml/pages/Settings.qml"),
    };

    static const QRegularExpression phantomRegex(
        QStringLiteral("flatpak|snap|\\baur\\b|pacstall"),
        QRegularExpression::CaseInsensitiveOption);

    static const QRegularExpression settingsPhantomRegex(
        QStringLiteral("flatpak|snap|pacstall"),
        QRegularExpression::CaseInsensitiveOption);

    for (const QString &relPath : pages) {
        const QString fullPath = QStringLiteral(PROJECT_DIR) + relPath;
        QQmlComponent component(&engine, QUrl::fromLocalFile(fullPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        if (auto *quickItem = qobject_cast<QQuickItem *>(root.data())) {
            quickItem->setSize(QSizeF(1024, 768));
        }
        QCoreApplication::processEvents();

        const bool isSettings = relPath.endsWith(QLatin1String("Settings.qml"));
        const bool isManage = relPath.endsWith(QLatin1String("Manage.qml"));

        if (isManage) {
            // Bei Manage.qml jeden Reiter prüfen (0: Updates, 1: Apps, 2: Alle Pakete & Pflege)
            for (int t = 0; t <= 2; ++t) {
                root->setProperty("tab", t);
                QCoreApplication::processEvents();

                QStringList texts;
                QSet<QObject *> visited;
                collectVisibleTexts(root.data(), texts, visited);

                for (const QString &text : texts) {
                    QRegularExpressionMatch match = phantomRegex.match(text);
                    if (match.hasMatch()) {
                        qWarning() << "Geisterquellen-Text in Manage tab" << t << ":" << text << "matched:" << match.captured();
                    }
                    QVERIFY2(!match.hasMatch(), qPrintable(QStringLiteral("Phantom text in Manage (tab %1): %2").arg(t).arg(text)));
                }
            }
        } else if (isSettings) {
            // In Einstellungen auf Arch ist AUR/Chaotic-AUR unter Drittanbieter-Quellen
            // die autorisierte Quelle (Paket A/B); Flatpak, Snap und Pacstall dürfen keinesfalls vorkommen.
            QStringList texts;
            QSet<QObject *> visited;
            collectVisibleTexts(root.data(), texts, visited);

            for (const QString &text : texts) {
                QRegularExpressionMatch match = settingsPhantomRegex.match(text);
                if (match.hasMatch()) {
                    qWarning() << "Geisterquellen-Text in Settings:" << text << "matched:" << match.captured();
                }
                QVERIFY2(!match.hasMatch(), qPrintable(QStringLiteral("Phantom text in Settings: %1").arg(text)));
            }
        } else {
            // Alle übrigen Seiten dürfen keinen Treffer auf flatpak|snap|\baur\b|pacstall haben
            QStringList texts;
            QSet<QObject *> visited;
            collectVisibleTexts(root.data(), texts, visited);

            for (const QString &text : texts) {
                QRegularExpressionMatch match = phantomRegex.match(text);
                if (match.hasMatch()) {
                    qWarning() << "Geisterquellen-Text in" << relPath << ":" << text << "matched:" << match.captured();
                }
                QVERIFY2(!match.hasMatch(), qPrintable(QStringLiteral("Phantom text in %1: %2").arg(relPath, text)));
            }
        }
    }
}

void PhantomSourcesTest::testGegenprobeFlatpakDetectedWhenAvailable() {
    // Gegenprobe gemäß Abschnitt 6.3:
    // Denselben Test mit Flatpak vorhanden laufen lassen, dann muss „Flatpak"
    // gefunden werden. So ist belegt, dass der Sammler überhaupt etwas sieht.
    RepoManager::instance().setForcedDistroFamily(DistroFamily::Arch);

    QTemporaryDir settingsDir;
    AppSettings appSettings(settingsDir.path() + QStringLiteral("/settings.conf"));

    DaemonClient client;
    AurUpdates aur;
    aur.setAppSettings(&appSettings);
    aur.setDaemonClient(&client);

    FlatpakUpdates flatpak;
    flatpak.setForceAvailable(true); // Gegenprobe: Flatpak vorhanden!
    flatpak.setDaemonClient(&client);
    QCOMPARE(flatpak.available(), true);

    SnapUpdates snap;
    snap.setForceAvailable(false);
    snap.setDaemonClient(&client);

    PacstallUpdates pacstall;
    pacstall.setAppSettings(&appSettings);
    pacstall.setForceSupported(false);

    CatalogService::setFlatpakAvailableOverride(true);
    CatalogService catalogService;
    catalogService.setLoadStdDataLocations(false);
    catalogService.load();

    ApplicationStore appStore(&catalogService, nullptr);
    StoreModel storeModel(&appStore);
    StoreModel installedStoreModel(&appStore);
    installedStoreModel.setInstalledOnly(true);

    OperationMonitor operationMonitor(&client, &flatpak, &snap, &aur, &pacstall);
    SystemPalette *palette = SystemPalette::instance();
    MediaCache mediaCache;

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(PROJECT_DIR "/linux-app-store"));
    engine.addImportPath(QStringLiteral(PROJECT_DIR "/linux-app-store/qml"));

    engine.rootContext()->setContextProperty(QStringLiteral("Theme"), palette);
    engine.rootContext()->setContextProperty(QStringLiteral("daemonClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("updatesModel"), client.updatesModel());
    engine.rootContext()->setContextProperty(QStringLiteral("progressModel"), client.progressModel());
    engine.rootContext()->setContextProperty(QStringLiteral("logModel"), client.logModel());
    engine.rootContext()->setContextProperty(QStringLiteral("installedModel"), client.installedModel());
    engine.rootContext()->setContextProperty(QStringLiteral("historyModel"), client.historyModel());
    engine.rootContext()->setContextProperty(QStringLiteral("aurUpdates"), &aur);
    engine.rootContext()->setContextProperty(QStringLiteral("flatpakUpdates"), &flatpak);
    engine.rootContext()->setContextProperty(QStringLiteral("snapUpdates"), &snap);
    engine.rootContext()->setContextProperty(QStringLiteral("pacstallUpdates"), &pacstall);
    engine.rootContext()->setContextProperty(QStringLiteral("operationMonitor"), &operationMonitor);
    engine.rootContext()->setContextProperty(QStringLiteral("appStore"), &appStore);
    engine.rootContext()->setContextProperty(QStringLiteral("storeModel"), &storeModel);
    engine.rootContext()->setContextProperty(QStringLiteral("installedStoreModel"), &installedStoreModel);
    engine.rootContext()->setContextProperty(QStringLiteral("repositoriesModel"), client.repositoriesModel());
    engine.rootContext()->setContextProperty(QStringLiteral("appSettings"), &appSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("mediaCache"), &mediaCache);
    engine.rootContext()->setContextProperty(QStringLiteral("initialManageTab"), 0);
    engine.rootContext()->setContextProperty(QStringLiteral("initialInstalledTab"), 0);
    engine.rootContext()->setContextProperty(QStringLiteral("initialSettingsTab"), 0);
    engine.rootContext()->setContextProperty(QStringLiteral("initialNavIndex"), 0);
    engine.rootContext()->setContextProperty(QStringLiteral("initialAppKey"), QString());
    engine.rootContext()->setContextProperty(QStringLiteral("screenshotRiskSource"), QString());
    engine.rootContext()->setContextProperty(QStringLiteral("trayManager"), nullptr);
    engine.rootContext()->setContextProperty(QStringLiteral("startInTray"), false);

    // In Search.qml muss der Quellenfilter nun Flatpak anbieten
    const QString searchPath = QStringLiteral(PROJECT_DIR "/linux-app-store/qml/pages/Search.qml");
    QQmlComponent searchComp(&engine, QUrl::fromLocalFile(searchPath));
    QVERIFY2(searchComp.isReady(), qPrintable(searchComp.errorString()));

    QScopedPointer<QObject> searchRoot(searchComp.create());
    QVERIFY2(searchRoot, qPrintable(searchComp.errorString()));

    if (auto *quickItem = qobject_cast<QQuickItem *>(searchRoot.data())) {
        quickItem->setSize(QSizeF(1024, 768));
    }
    QCoreApplication::processEvents();

    QStringList searchTexts;
    QSet<QObject *> visited;
    collectVisibleTexts(searchRoot.data(), searchTexts, visited);

    bool flatpakFoundInSearch = false;
    for (const QString &t : searchTexts) {
        if (t.contains(QStringLiteral("Flatpak"), Qt::CaseInsensitive)) {
            flatpakFoundInSearch = true;
            break;
        }
    }
    QVERIFY2(flatpakFoundInSearch, "Gegenprobe fehlgeschlagen: 'Flatpak' wurde in Search.qml nicht gefunden, obwohl Flatpak aktiv ist!");

    // In Updates.qml muss der Flatpak-Abschnitt sichtbar sein
    const QString updatesPath = QStringLiteral(PROJECT_DIR "/linux-app-store/qml/pages/Updates.qml");
    QQmlComponent updatesComp(&engine, QUrl::fromLocalFile(updatesPath));
    QVERIFY2(updatesComp.isReady(), qPrintable(updatesComp.errorString()));

    QScopedPointer<QObject> updatesRoot(updatesComp.create());
    QVERIFY2(updatesRoot, qPrintable(updatesComp.errorString()));

    if (auto *quickItem = qobject_cast<QQuickItem *>(updatesRoot.data())) {
        quickItem->setSize(QSizeF(1024, 768));
    }
    QCoreApplication::processEvents();

    QStringList updatesTexts;
    visited.clear();
    collectVisibleTexts(updatesRoot.data(), updatesTexts, visited);

    bool flatpakFoundInUpdates = false;
    for (const QString &t : updatesTexts) {
        if (t.contains(QStringLiteral("Flatpak"), Qt::CaseInsensitive)) {
            flatpakFoundInUpdates = true;
            break;
        }
    }
    QVERIFY2(flatpakFoundInUpdates, "Gegenprobe fehlgeschlagen: 'Flatpak' wurde in Updates.qml nicht gefunden, obwohl Flatpak aktiv ist!");
}

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    PhantomSourcesTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "phantom_sources_test.moc"
