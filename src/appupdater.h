#pragma once
#include <QObject>
#include <QJsonObject>
#include <QUrl>
#include <memory>
#include <atomic>

struct UpdateRelease {
    QString version, notes;
    QUrl page, manifestUrl, archiveUrl;
    QString manifestDigest;
};

namespace UpdateProtocol {
QString repository(const QString &input);
int compareVersions(const QString &left, const QString &right, bool *valid = nullptr);
bool parseRelease(const QByteArray &json, const QString &repository, UpdateRelease *release, QString *error);
bool validateManifest(const QJsonObject &manifest, const QString &version, QString *error);
bool allowedPath(const QString &path);
bool allowedUrl(const QUrl &url);
bool writeCacheFile(const QString &path, const QByteArray &bytes, QString *error = nullptr);
}

class AppUpdater : public QObject {
    Q_OBJECT
public:
    explicit AppUpdater(QObject *parent = nullptr);
    ~AppUpdater() override;
    bool busy() const;
    UpdateRelease release() const;
    void check(const QString &repository);
    void download();
    void cancel();
signals:
    void checkingFinished(bool available, const QString &message);
    void failed(const QString &message);
    void downloadProgress(qint64 received, qint64 total);
    void packageReady(const QString &directory, const QString &version);
private:
    bool m_busy = false;
    UpdateRelease m_release;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
