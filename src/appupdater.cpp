#include "appupdater.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>
#include <QVersionNumber>
#include <functional>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#endif

class UpdateWorker : public QThread {
    Q_OBJECT
public:
    std::function<void(UpdateWorker *)> task;
    UpdateRelease release;
    QString error;
signals:
    void progress(qint64 received, qint64 total);
protected:
    void run() override { task(this); }
};

namespace {
const QString archiveName = QStringLiteral("Shiye-windows-x64.zip");
const QString manifestName = QStringLiteral("Shiye-update.json");
QVersionNumber version(QString value) {
    value = value.trimmed(); if (value.startsWith(QLatin1Char('v'))) value.remove(0, 1);
    if (!QRegularExpression(QStringLiteral("^[0-9]{1,8}(\\.[0-9]{1,8}){1,3}$")).match(value).hasMatch()) return {};
    return QVersionNumber::fromString(value).normalized();
}
QString hash(const QByteArray &bytes) { return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()); }
QString fileHash(const QString &path) {
    QFile file(path); QCryptographicHash digest(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !digest.addData(&file)) return {};
    return QString::fromLatin1(digest.result().toHex());
}
struct Response { QByteArray bytes; int status = 0; QString error; };
using Cancel = std::shared_ptr<std::atomic_bool>;
using Progress = std::function<void(qint64, qint64)>;

Response fetch(QUrl url, qint64 limit, const Cancel &cancel, const QString &filePath = QString(), Progress progress = {}) {
    Response response;
#ifdef Q_OS_WIN
    struct Handle { HINTERNET value = nullptr; ~Handle() { if (value) WinHttpCloseHandle(value); } };
    Handle session{WinHttpOpen(L"Shiye-Updater/0.4", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) { response.error = QStringLiteral("无法启动 Windows 网络连接。"); return response; }
    WinHttpSetTimeouts(session.value, 10000, 10000, 15000, 20000);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session.value, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    for (int redirect = 0; redirect < 6; ++redirect) {
        if (cancel->load()) { response.error = QStringLiteral("已取消更新操作。"); return response; }
        if (!UpdateProtocol::allowedUrl(url)) { response.error = QStringLiteral("更新地址无效或跳转到了非 GitHub 下载站点。"); return response; }
        const std::wstring host = url.host().toStdWString();
        Handle connection{WinHttpConnect(session.value, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)};
        const std::wstring path = QString::fromUtf8(url.toEncoded(QUrl::RemoveScheme | QUrl::RemoveAuthority | QUrl::RemoveFragment)).toStdWString();
        Handle request{connection.value ? WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr};
        if (!request.value) break;
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
        const wchar_t *headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2026-03-10\r\n";
        if (!WinHttpSendRequest(request.value, headers, DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.value, nullptr)) break;
        DWORD status = 0, length = sizeof(status);
        if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX)) break;
        response.status = int(status);
        if (status >= 300 && status < 400) {
            wchar_t location[8192]; length = sizeof(location);
            if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, location, &length, WINHTTP_NO_HEADER_INDEX)) break;
            url = url.resolved(QUrl(QString::fromWCharArray(location))); continue;
        }
        if (status != 200) {
            response.error = status == 404 ? QStringLiteral("未找到正式 Release。请检查仓库地址、是否公开，以及是否已发布版本。") : status == 403 || status == 429 ? QStringLiteral("GitHub 暂时限制请求，请稍后再试。") : QStringLiteral("GitHub 返回 HTTP %1。").arg(status);
            return response;
        }
        wchar_t contentLength[64]; length = sizeof(contentLength); qint64 total = 0;
        if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, contentLength, &length, WINHTTP_NO_HEADER_INDEX)) total = QString::fromWCharArray(contentLength).toLongLong();
        if (total > limit) { response.error = QStringLiteral("更新文件超过允许大小，已停止下载。"); return response; }
        QSaveFile output(filePath);
        if (!filePath.isEmpty() && !output.open(QIODevice::WriteOnly)) { response.error = QStringLiteral("无法写入更新下载目录。"); return response; }
        qint64 received = 0; char buffer[65536]; DWORD read = 0;
        while (!cancel->load()) {
            if (!WinHttpReadData(request.value, buffer, sizeof(buffer), &read)) { response.error = QStringLiteral("下载连接中断，请重试。"); return response; }
            if (!read) {
                if (total && total != received) { response.error = QStringLiteral("更新文件下载不完整，请重试。"); return response; }
                if (!filePath.isEmpty() && !output.commit()) { response.error = QStringLiteral("更新文件保存失败，请检查磁盘空间。"); return response; }
                return response;
            }
            received += read;
            if (received > limit) { response.error = QStringLiteral("更新文件超过允许大小。"); return response; }
            if (filePath.isEmpty()) response.bytes.append(buffer, int(read));
            else if (output.write(buffer, read) != read) { response.error = QStringLiteral("下载写入失败，请检查磁盘空间。"); return response; }
            if (progress) progress(received, total);
        }
        response.error = QStringLiteral("已取消更新操作。"); return response;
    }
    response.error = QStringLiteral("无法连接 GitHub，请检查网络或代理后重试。（Windows 错误 %1）").arg(GetLastError());
#else
    Q_UNUSED(url); Q_UNUSED(limit); Q_UNUSED(cancel); Q_UNUSED(filePath); Q_UNUSED(progress);
    response.error = QStringLiteral("当前自动更新安装仅支持 Windows 64 位版本。");
#endif
    return response;
}
}

QString UpdateProtocol::repository(const QString &input) {
    QString value = input.trimmed();
    if (value.startsWith(QStringLiteral("https://github.com/"), Qt::CaseInsensitive)) {
        const QUrl url(value); if (!url.query().isEmpty() || !url.fragment().isEmpty() || !url.userInfo().isEmpty()) return {};
        value = url.path().mid(1);
    }
    while (value.endsWith(QLatin1Char('/'))) value.chop(1);
    if (value.endsWith(QStringLiteral(".git"))) value.chop(4);
    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9-]{0,38}/[A-Za-z0-9_.-]{1,100}$")).match(value).hasMatch() || value.section(QLatin1Char('/'), 1) == QStringLiteral("..")) return {};
    return value;
}
int UpdateProtocol::compareVersions(const QString &left, const QString &right, bool *valid) {
    const auto a = version(left), b = version(right); if (valid) *valid = !a.isNull() && !b.isNull();
    return a.isNull() || b.isNull() ? 0 : QVersionNumber::compare(a, b);
}
bool UpdateProtocol::allowedUrl(const QUrl &url) {
    const QSet<QString> hosts{QStringLiteral("api.github.com"), QStringLiteral("github.com"), QStringLiteral("release-assets.githubusercontent.com"), QStringLiteral("objects.githubusercontent.com"), QStringLiteral("github-releases.githubusercontent.com")};
    return url.isValid() && url.scheme() == QStringLiteral("https") && hosts.contains(url.host().toLower()) && url.userInfo().isEmpty() && url.port(443) == 443;
}
bool UpdateProtocol::allowedPath(const QString &path) {
    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_.+-]+(/[A-Za-z0-9_.+-]+)*$")).match(path).hasMatch() || path.size() > 220) return false;
    const QSet<QString> blocked{QStringLiteral("data"), QStringLiteral("attachments"), QStringLiteral("backups"), QStringLiteral("notes.sqlite"), QStringLiteral("preferences.ini"), QStringLiteral("library.lock")};
    for (const auto &part : path.split(QLatin1Char('/'))) {
        const QString lower = part.toLower();
        if (part.startsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char('.')) || blocked.contains(lower) ||
            QRegularExpression(QStringLiteral("^(con|prn|aux|nul|com[1-9]|lpt[1-9])($|\\.)")).match(lower).hasMatch()) return false;
    }
    return true;
}
bool UpdateProtocol::parseRelease(const QByteArray &json, const QString &repo, UpdateRelease *release, QString *error) {
    const auto object = QJsonDocument::fromJson(json).object(); const QString tag = object.value(QStringLiteral("tag_name")).toString();
    auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (object.isEmpty() || version(tag).isNull() || object.value(QStringLiteral("draft")).toBool() || object.value(QStringLiteral("prerelease")).toBool()) return fail(QStringLiteral("未发现有效的正式版本（版本号需为 v1.2.3 形式）。"));
    UpdateRelease result; result.version = tag; result.notes = object.value(QStringLiteral("body")).toString().left(50000);
    result.page = QUrl(QStringLiteral("https://github.com/") + repo + QStringLiteral("/releases/tag/") + tag);
    const QString prefix = QStringLiteral("https://github.com/") + repo + QStringLiteral("/releases/download/") + tag + QLatin1Char('/');
    for (const auto value : object.value(QStringLiteral("assets")).toArray()) {
        const auto asset = value.toObject(); const QString name = asset.value(QStringLiteral("name")).toString();
        if (asset.value(QStringLiteral("state")).toString() != QStringLiteral("uploaded")) continue;
        const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
        if (!allowedUrl(url) || url.toString() != prefix + name) continue;
        if (name == manifestName) { result.manifestUrl = url; result.manifestDigest = asset.value(QStringLiteral("digest")).toString(); }
        else if (name == archiveName) result.archiveUrl = url;
    }
    if (result.manifestUrl.isEmpty() || result.archiveUrl.isEmpty()) return fail(QStringLiteral("此 Release 尚未上传 Shiye-update.json 和 Shiye-windows-x64.zip。"));
    if (release) *release = result;
    return true;
}
bool UpdateProtocol::validateManifest(const QJsonObject &manifest, const QString &expectedVersion, QString *error) {
    auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    bool valid = false; const int comparison = compareVersions(manifest.value(QStringLiteral("version")).toString(), expectedVersion, &valid);
    const QRegularExpression digest(QStringLiteral("^[0-9a-f]{64}$"));
    if (manifest.value(QStringLiteral("schema")).toInt() != 1 || !valid || comparison != 0 || manifest.value(QStringLiteral("archive")).toString() != archiveName ||
        !digest.match(manifest.value(QStringLiteral("sha256")).toString()).hasMatch()) return fail(QStringLiteral("更新清单的版本或校验信息无效。"));
    const auto files = manifest.value(QStringLiteral("files")).toObject(); QSet<QString> seen;
    if (!files.contains(QStringLiteral("Shiye.exe")) || !files.contains(QStringLiteral("update-installer.ps1")) || files.size() > 2000) return fail(QStringLiteral("更新清单缺少程序或安装组件。"));
    for (auto it = files.begin(); it != files.end(); ++it) {
        if (!allowedPath(it.key()) || seen.contains(it.key().toLower()) || !digest.match(it.value().toString()).hasMatch()) return fail(QStringLiteral("更新清单包含无效路径或校验值：") + it.key());
        seen.insert(it.key().toLower());
    }
    return true;
}

AppUpdater::AppUpdater(QObject *parent) : QObject(parent) {}
AppUpdater::~AppUpdater() { cancel(); }
bool AppUpdater::busy() const { return m_busy; }
UpdateRelease AppUpdater::release() const { return m_release; }
void AppUpdater::cancel() { if (m_cancel) m_cancel->store(true); }
void AppUpdater::check(const QString &input) {
    if (m_busy) return;
    const QString repo = UpdateProtocol::repository(input);
    if (repo.isEmpty()) { emit failed(QStringLiteral("请先填写 GitHub 仓库，例如 用户名/仓库名。")); return; }
    m_busy = true; m_release = {}; m_cancel = std::make_shared<std::atomic_bool>(false); const auto cancel = m_cancel;
    auto *thread = new UpdateWorker;
    thread->task = [repo, cancel](UpdateWorker *worker) {
        const auto response = fetch(QUrl(QStringLiteral("https://api.github.com/repos/") + repo + QStringLiteral("/releases/latest")), 2 * 1024 * 1024, cancel);
        worker->error = response.error;
        if (worker->error.isEmpty()) UpdateProtocol::parseRelease(response.bytes, repo, &worker->release, &worker->error);
    };
    connect(thread, &QThread::finished, this, [this, thread, cancel]() {
        m_busy = false;
        if (!thread->error.isEmpty()) { emit failed(thread->error); return; }
        if (cancel->load()) { emit failed(QStringLiteral("已取消更新检查。")); return; }
        m_release = thread->release;
        const bool available = UpdateProtocol::compareVersions(m_release.version, QCoreApplication::applicationVersion()) > 0;
        emit checkingFinished(available, available ? QStringLiteral("发现新版本 %1").arg(m_release.version) : QStringLiteral("当前已是最新版本。"));
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater); thread->start();
}
void AppUpdater::download() {
    if (m_busy || m_release.archiveUrl.isEmpty()) return;
    if (UpdateProtocol::compareVersions(m_release.version, QCoreApplication::applicationVersion()) <= 0) { emit failed(QStringLiteral("此版本不高于当前版本，无需安装。")); return; }
    m_busy = true; m_cancel = std::make_shared<std::atomic_bool>(false); const auto cancel = m_cancel;
    const auto release = m_release;
    const QString directory = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("updates/") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    auto *thread = new UpdateWorker;
    thread->task = [release, cancel, directory](UpdateWorker *worker) {
        QString error; const auto response = fetch(release.manifestUrl, 1024 * 1024, cancel); error = response.error;
        const auto manifest = QJsonDocument::fromJson(response.bytes).object();
        if (error.isEmpty() && release.manifestDigest.startsWith(QStringLiteral("sha256:")) && hash(response.bytes) != release.manifestDigest.mid(7)) error = QStringLiteral("GitHub 更新清单校验失败。");
        if (error.isEmpty()) UpdateProtocol::validateManifest(manifest, release.version, &error);
        if (error.isEmpty() && !QDir().mkpath(directory)) error = QStringLiteral("无法创建更新缓存目录。");
        if (error.isEmpty()) {
            QSaveFile file(QDir(directory).filePath(manifestName));
            if (!file.open(QIODevice::WriteOnly) || file.write(response.bytes) != response.bytes.size() || !file.commit()) error = QStringLiteral("无法保存更新校验清单。");
        }
        const QString archive = QDir(directory).filePath(archiveName);
        if (error.isEmpty()) {
            const auto download = fetch(release.archiveUrl, 512LL * 1024 * 1024, cancel, archive, [worker](qint64 received, qint64 total) { emit worker->progress(received, total); }); error = download.error;
        }
        if (error.isEmpty() && fileHash(archive) != manifest.value(QStringLiteral("sha256")).toString()) error = QStringLiteral("更新包校验失败，已停止安装。请重新下载。");
        if (!error.isEmpty()) QFile::remove(archive);
        worker->error = error;
    };
    connect(thread, &UpdateWorker::progress, this, &AppUpdater::downloadProgress, Qt::QueuedConnection);
    connect(thread, &QThread::finished, this, [this, thread, directory, release, cancel]() {
        m_busy = false;
        if (cancel->load()) { emit failed(QStringLiteral("已取消更新下载。")); return; }
        if (!thread->error.isEmpty()) emit failed(thread->error); else emit packageReady(directory, release.version);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater); thread->start();
}

#include "appupdater.moc"
