#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QTemporaryDir>
#include "appupdater.h"

class UpdateTests : public QObject {
    Q_OBJECT
private slots:
    void repositoryNames();
    void versionsAreNumericAndStable();
    void releasesRequireExpectedAssets();
    void manifestsCannotTargetNotesOrEscape();
    void redirectsStayOnGithubHttps();
    void invalidConfigurationDoesNotStartWorker();
    void cancelledCheckReleasesBusyState();
    void cacheWritesAreExclusiveAndComplete();
    void publicRepositoryNetworkProbe();
};

static QJsonObject manifest() {
    const QString digest(64, QLatin1Char('a'));
    return {{QStringLiteral("schema"), 1}, {QStringLiteral("version"), QStringLiteral("v1.2.3")},
        {QStringLiteral("archive"), QStringLiteral("Shiye-windows-x64.zip")}, {QStringLiteral("sha256"), digest},
        {QStringLiteral("files"), QJsonObject{{QStringLiteral("Shiye.exe"), digest}, {QStringLiteral("update-installer.ps1"), digest}, {QStringLiteral("platforms/qwindows.dll"), digest}}}};
}
void UpdateTests::repositoryNames() {
    QCOMPARE(UpdateProtocol::repository(QStringLiteral("https://github.com/ricarrdooo/PersonalNote.git/")), QStringLiteral("ricarrdooo/PersonalNote"));
    QCOMPARE(UpdateProtocol::repository(QStringLiteral(" ricarrdooo/PersonalNote ")), QStringLiteral("ricarrdooo/PersonalNote"));
    for (const QString &bad : {QStringLiteral("https://evil.test/a/b"), QStringLiteral("https://github.com/a/b?x=1"), QStringLiteral("a/../b"), QStringLiteral("a/b/releases"), QStringLiteral("a/.."), QStringLiteral("a/b\ncommand")}) QVERIFY(UpdateProtocol::repository(bad).isEmpty());
}
void UpdateTests::versionsAreNumericAndStable() {
    bool valid = false;
    QVERIFY(UpdateProtocol::compareVersions(QStringLiteral("v0.10.0"), QStringLiteral("0.9.9"), &valid) > 0); QVERIFY(valid);
    QCOMPARE(UpdateProtocol::compareVersions(QStringLiteral("1.2.0"), QStringLiteral("v1.2"), &valid), 0); QVERIFY(valid);
    QVERIFY(UpdateProtocol::compareVersions(QStringLiteral("0.3.0"), QStringLiteral("0.4.0")) < 0);
    UpdateProtocol::compareVersions(QStringLiteral("1.2.3-beta"), QStringLiteral("1.2.3"), &valid); QVERIFY(!valid);
    UpdateProtocol::compareVersions(QStringLiteral("1.2.bad"), QStringLiteral("1.2.3"), &valid); QVERIFY(!valid);
}
void UpdateTests::releasesRequireExpectedAssets() {
    const QString base = QStringLiteral("https://github.com/ricarrdooo/PersonalNote/releases/download/v1.2.3/");
    QJsonArray assets;
    for (const QString &name : {QStringLiteral("Shiye-update.json"), QStringLiteral("Shiye-windows-x64.zip")}) assets.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("state"), QStringLiteral("uploaded")}, {QStringLiteral("browser_download_url"), base + name}});
    QJsonObject object{{QStringLiteral("tag_name"), QStringLiteral("v1.2.3")}, {QStringLiteral("body"), QStringLiteral("修复保存问题")}, {QStringLiteral("assets"), assets}};
    UpdateRelease release; QString error;
    QVERIFY(UpdateProtocol::parseRelease(QJsonDocument(object).toJson(), QStringLiteral("ricarrdooo/PersonalNote"), &release, &error)); QCOMPARE(release.version, QStringLiteral("v1.2.3"));
    object.insert(QStringLiteral("prerelease"), true); QVERIFY(!UpdateProtocol::parseRelease(QJsonDocument(object).toJson(), QStringLiteral("ricarrdooo/PersonalNote"), &release, &error));
    object.remove(QStringLiteral("prerelease")); auto asset = assets.first().toObject(); asset.insert(QStringLiteral("browser_download_url"), QStringLiteral("https://evil.test/Shiye-update.json")); assets.replace(0, asset); object.insert(QStringLiteral("assets"), assets);
    QVERIFY(!UpdateProtocol::parseRelease(QJsonDocument(object).toJson(), QStringLiteral("ricarrdooo/PersonalNote"), &release, &error));
}
void UpdateTests::manifestsCannotTargetNotesOrEscape() {
    QVERIFY(UpdateProtocol::allowedPath(QStringLiteral("libstdc++-6.dll")));
    QString error; QVERIFY(UpdateProtocol::validateManifest(manifest(), QStringLiteral("1.2.3"), &error));
    for (const QString &bad : {QStringLiteral("../Shiye.exe"), QStringLiteral("C:/Shiye.exe"), QStringLiteral("data/notes.sqlite"), QStringLiteral("Data/image.png"), QStringLiteral("assets/../data"), QStringLiteral("x/CON.txt"), QStringLiteral("x\\file.dll"), QStringLiteral("folder./file.dll")}) {
        QVERIFY(!UpdateProtocol::allowedPath(bad)); auto object = manifest(); auto files = object.value(QStringLiteral("files")).toObject(); files.insert(bad, QString(64, QLatin1Char('b'))); object.insert(QStringLiteral("files"), files); QVERIFY(!UpdateProtocol::validateManifest(object, QStringLiteral("v1.2.3"), &error));
    }
    auto object = manifest(); QVERIFY(!UpdateProtocol::validateManifest(object, QStringLiteral("v1.2.4"), &error));
    auto files = object.value(QStringLiteral("files")).toObject(); files.insert(QStringLiteral("shiye.exe"), QString(64, QLatin1Char('a'))); object.insert(QStringLiteral("files"), files); QVERIFY(!UpdateProtocol::validateManifest(object, QStringLiteral("v1.2.3"), &error));
    object = manifest(); object.insert(QStringLiteral("sha256"), QStringLiteral("invalid")); QVERIFY(!UpdateProtocol::validateManifest(object, QStringLiteral("v1.2.3"), &error));
}
void UpdateTests::redirectsStayOnGithubHttps() {
    QVERIFY(UpdateProtocol::allowedUrl(QUrl(QStringLiteral("https://api.github.com/repos/a/b/releases/latest"))));
    QVERIFY(UpdateProtocol::allowedUrl(QUrl(QStringLiteral("https://release-assets.githubusercontent.com/file?token=temporary"))));
    for (const QString &bad : {QStringLiteral("http://github.com/a/b"), QStringLiteral("https://github.com.evil.test/file"), QStringLiteral("file:///tmp/file"), QStringLiteral("https://user:password@github.com/a/b"), QStringLiteral("https://github.com:444/file")}) QVERIFY(!UpdateProtocol::allowedUrl(QUrl(bad)));
}
void UpdateTests::invalidConfigurationDoesNotStartWorker() {
    AppUpdater updater; QSignalSpy failed(&updater, &AppUpdater::failed); updater.check(QStringLiteral("not-a-repo")); QCOMPARE(failed.count(), 1); QVERIFY(!updater.busy());
}
void UpdateTests::cancelledCheckReleasesBusyState() {
    AppUpdater updater; QSignalSpy failed(&updater, &AppUpdater::failed);
    updater.check(QStringLiteral("ricarrdooo/PersonalNote")); updater.cancel();
    QTRY_VERIFY_WITH_TIMEOUT(failed.count() > 0, 30000); QVERIFY(!updater.busy());
}
void UpdateTests::cacheWritesAreExclusiveAndComplete() {
    QTemporaryDir temporary; QVERIFY(temporary.isValid()); const QString path = temporary.filePath(QStringLiteral("manifest.json"));
    const QByteArray bytes("manifest bytes\0end", 18); QString error;
    QVERIFY2(UpdateProtocol::writeCacheFile(path, bytes, &error), qPrintable(error));
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes); file.close();
    QVERIFY(!UpdateProtocol::writeCacheFile(path, QByteArray("overwrite"), &error));
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes);
}
void UpdateTests::publicRepositoryNetworkProbe() {
    if (qEnvironmentVariable("SHIYE_LIVE_UPDATE_TEST") != QStringLiteral("1")) QSKIP("Set SHIYE_LIVE_UPDATE_TEST=1 to verify the Windows HTTPS connection.");
    AppUpdater updater; QSignalSpy done(&updater, &AppUpdater::checkingFinished), failed(&updater, &AppUpdater::failed);
    updater.check(QStringLiteral("ricarrdooo/PersonalNote"));
    QTRY_VERIFY_WITH_TIMEOUT(done.count() + failed.count() > 0, 45000);
    if (failed.count()) {
        const QString message = failed.first().first().toString();
        QVERIFY2(message.contains(QStringLiteral("未找到正式 Release")) || message.contains(QStringLiteral("尚未上传")), qPrintable(message));
        qInfo().noquote() << message;
    }
    QVERIFY(!updater.busy());
}

QTEST_GUILESS_MAIN(UpdateTests)
#include "update_tests.moc"
