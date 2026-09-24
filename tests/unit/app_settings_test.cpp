#include <QTest>
#include <QTemporaryDir>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>

#include "linux-app-store/AppSettings.h"
#include "linux-app-store/AurUpdates.h"

using namespace lut;

namespace {

class CountingNetworkManager : public QNetworkAccessManager {
public:
    int requestCount = 0;
protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoingData) override {
        ++requestCount;
        return QNetworkAccessManager::createRequest(op, request, outgoingData);
    }
};

} // namespace

class AppSettingsTest : public QObject {
    Q_OBJECT

private slots:
    void testFreshFileDefaultsToDisabled();
    void testEnablePersistsAcrossInstances();
    void testNonArchCannotBecomeAvailable();
    void testDisabledProducesNoNetworkReplyAndCallsQmOnlyForHint();
    void testDisableClearsUpdatesList();
};

void AppSettingsTest::testFreshFileDefaultsToDisabled() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString conf = dir.filePath(QStringLiteral("settings.conf"));

    AppSettings settings(conf);
    QCOMPARE(settings.aurEnabled(), false);
    QCOMPARE(settings.pacstallEnabled(), false);

    AurUpdates aur;
    aur.setForceSupported(true);
    aur.setAppSettings(&settings);

    QCOMPARE(aur.supported(), true);
    QCOMPARE(aur.enabled(), false);
    QCOMPARE(aur.available(), false);
}

void AppSettingsTest::testEnablePersistsAcrossInstances() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString conf = dir.filePath(QStringLiteral("settings.conf"));

    {
        AppSettings settings(conf);
        settings.setAurEnabled(true);
        settings.setPacstallEnabled(true);
    }

    {
        AppSettings settings2(conf);
        QCOMPARE(settings2.aurEnabled(), true);
        QCOMPARE(settings2.pacstallEnabled(), true);

        AurUpdates aur;
        aur.setForceSupported(true);
        aur.setAppSettings(&settings2);

        QCOMPARE(aur.supported(), true);
        QCOMPARE(aur.enabled(), true);
        QCOMPARE(aur.available(), true);
    }
}

void AppSettingsTest::testNonArchCannotBecomeAvailable() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString conf = dir.filePath(QStringLiteral("settings.conf"));

    AppSettings settings(conf);
    AurUpdates aur;
    aur.setForceSupported(false);
    aur.setAppSettings(&settings);

    QCOMPARE(aur.supported(), false);
    aur.setEnabled(true);
    QCOMPARE(aur.enabled(), true);
    // Nicht-Arch: available muss zwingend false bleiben
    QCOMPARE(aur.available(), false);
}

void AppSettingsTest::testDisabledProducesNoNetworkReplyAndCallsQmOnlyForHint() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString conf = dir.filePath(QStringLiteral("settings.conf"));

    AppSettings settings(conf);
    settings.setAurEnabled(false);

    AurUpdates aur;
    aur.setForceSupported(true);
    aur.setAppSettings(&settings);

    int qmCalls = 0;
    int otherProcCalls = 0;
    aur.setProcessRunner([&](const QString &prog, const QStringList &args, int *code) -> QByteArray {
        if (prog == QStringLiteral("pacman") && args.contains(QStringLiteral("-Qm"))) {
            ++qmCalls;
            if (code) *code = 0;
            return "fdroidserver 2.3.4-1\n";
        }
        ++otherProcCalls;
        if (code) *code = 0;
        return {};
    });

    CountingNetworkManager nam;
    aur.setNetworkAccessManager(&nam);

    // check() bei ausgeschaltetem AUR darf gar nichts tun (weder pacman noch Netz)
    aur.check();
    QCOMPARE(qmCalls, 0);
    QCOMPARE(otherProcCalls, 0);
    QCOMPARE(nam.requestCount, 0);

    // checkForeignPackages() führt nur pacman -Qm lokal aus, absolut kein Netz
    aur.checkForeignPackages();
    QCOMPARE(qmCalls, 1);
    QCOMPARE(otherProcCalls, 0);
    QCOMPARE(nam.requestCount, 0);
    QCOMPARE(aur.foreignPackageCount(), 1);
    QCOMPARE(aur.hasForeignPackages(), true);
}

void AppSettingsTest::testDisableClearsUpdatesList() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString conf = dir.filePath(QStringLiteral("settings.conf"));

    AppSettings settings(conf);
    settings.setAurEnabled(true);

    AurUpdates aur;
    aur.setProcessRunner([](const QString &, const QStringList &, int *code) -> QByteArray {
        if (code) *code = 0;
        return {};
    });
    aur.setForceSupported(true);
    aur.setAppSettings(&settings);
    QVERIFY(aur.available());

    // setEnabled(false) muss die Liste zurücksetzen und available auf false setzen
    aur.setEnabled(false);
    QCOMPARE(aur.available(), false);
    QCOMPARE(aur.count(), 0);
}

QTEST_MAIN(AppSettingsTest)
#include "app_settings_test.moc"
