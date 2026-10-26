#include "mainwindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Shiye"));
    QApplication::setApplicationDisplayName(QStringLiteral("拾页"));
    QApplication::setOrganizationName(QStringLiteral("Shiye"));
    QApplication::setApplicationVersion(QStringLiteral("0.4.1"));
    QApplication::setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("拾页 · 本地优先的个人电子笔记"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("data-dir"), QStringLiteral("资料库目录"), QStringLiteral("directory")});
    parser.addOption({QStringLiteral("smoke-test"), QStringLiteral("执行桌面保存与媒体导入验证后退出")});
    parser.addOption({QStringLiteral("screenshot"), QStringLiteral("启动后保存窗口截图"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("update-result"), QStringLiteral("显示本次更新结果"), QStringLiteral("path")});
    parser.process(app);
    const QString appDirectory = QCoreApplication::applicationDirPath();
    if (QFileInfo::exists(QDir(appDirectory).filePath(QStringLiteral(".shiye-update-pending.json")))) {
        const QString powershell = QDir(qEnvironmentVariable("WINDIR", "C:/Windows")).filePath(QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"));
        const bool started = QProcess::startDetached(powershell, {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-File"), QDir(appDirectory).filePath(QStringLiteral("update-installer.ps1")), QStringLiteral("-RecoverInstall"), appDirectory, QStringLiteral("-WaitForProcessId"), QString::number(QCoreApplication::applicationPid())});
        if (!started) QMessageBox::critical(nullptr, QStringLiteral("更新恢复"), QStringLiteral("检测到上次更新中断。请使用“打开拾页.cmd”恢复旧程序，资料库尚未打开。"));
        return started ? 0 : 1;
    }
    const QString dataDir = parser.isSet(QStringLiteral("data-dir"))
        ? QDir(parser.value(QStringLiteral("data-dir"))).absolutePath()
        : QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("data"));
    MainWindow window(dataDir);
    if (!window.isReady()) {
        QMessageBox::critical(nullptr, QStringLiteral("无法打开资料库"), window.startupError());
        return 1;
    }
    window.show();
    if (parser.isSet(QStringLiteral("update-result"))) {
        const QString path = parser.value(QStringLiteral("update-result"));
        QTimer::singleShot(300, &window, [&window, path]() {
            QFile file(path); if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return;
            const auto result = QJsonDocument::fromJson(file.readAll()).object();
            QMessageBox::information(&window, QStringLiteral("软件更新"), result.value(QStringLiteral("ok")).toBool() ? QStringLiteral("更新安装完成，已重新打开原资料库。") : QStringLiteral("更新未完成，当前资料已保留。\n") + result.value(QStringLiteral("message")).toString());
        });
    }
    if (parser.isSet(QStringLiteral("smoke-test"))) {
        QTimer::singleShot(500, &window, [&window, &app]() { app.exit(window.runSmokeTest() ? 0 : 2); });
    } else if (parser.isSet(QStringLiteral("screenshot"))) {
        const QString path = parser.value(QStringLiteral("screenshot"));
        QTimer::singleShot(900, &window, [&window, path]() { window.grab().save(path); });
    }
    return app.exec();
}

