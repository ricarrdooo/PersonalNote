#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QStringList>
#include <functional>
#include <QtSql/QSqlDatabase>

struct NoteNode {
    QString id, parentId, title, html, plainText, createdAt, updatedAt;
    bool folder = false;
    bool trashed = false;
    bool favorite = false;
};

struct Attachment {
    QString id, noteId, relativePath, originalName, mime;
    qint64 size = 0;
    bool isValid() const { return !id.isEmpty(); }
};

struct NoteRevision {
    QString id, noteId, title, html, plainText, savedAt;
};

class NoteStore : public QObject {
    Q_OBJECT
public:
    explicit NoteStore(QObject *parent = nullptr);
    ~NoteStore() override;
    bool open(const QString &dataDirectory, QString *error = nullptr);
    QString dataDirectory() const;
    QString errorString() const;
    QList<NoteNode> nodes(bool trashed = false) const;
    NoteNode node(const QString &id) const;
    QString createFolder(const QString &title, const QString &parentId = QString());
    QString createNote(const QString &title, const QString &parentId = QString(),
                       const QString &html = QString(), const QString &plainText = QString());
    bool updateNote(const QString &id, const QString &title, const QString &html, const QString &plainText);
    QList<NoteRevision> revisions(const QString &noteId, int limit = 200, int offset = 0) const;
    NoteRevision revision(const QString &id) const;
    bool restoreRevision(const QString &noteId, const QString &revisionId);
    bool renameNode(const QString &id, const QString &title);
    bool moveNode(const QString &id, const QString &parentId);
    bool setTrashed(const QString &id, bool trashed);
    bool setFavorite(const QString &id, bool favorite);
    QList<NoteNode> search(const QString &query, bool trashed = false) const;
    Attachment importAttachment(const QString &noteId, const QString &sourcePath, QString *error = nullptr);
    QList<Attachment> attachments(const QString &noteId) const;
    Attachment attachment(const QString &id) const;
    QString importNoteBundle(const QString &title, const QString &parentId, const QStringList &sourcePaths,
        const std::function<QPair<QString, QString>(const QList<Attachment> &)> &render, QString *error = nullptr);
    bool backupTo(const QString &targetDirectory, QString *error = nullptr, bool automatic = false);
    bool automaticBackup(const QString &root, QString *createdPath = nullptr, QString *error = nullptr);
    bool checkIntegrity(QString *error = nullptr) const;
    bool restoreFrom(const QString &sourceDirectory, QString *error = nullptr);
    bool seedIfEmpty();

signals:
    void changed();

private:
    bool connectDatabase(QString *error = nullptr);
    bool fail(const QString &message, QString *error = nullptr) const;
    bool validParent(const QString &parentId) const;
    QString createNode(const QString &title, const QString &parentId, bool folder,
                       const QString &html, const QString &plainText);
    bool validateBackup(const QString &directory, QString *error, bool includeAttachments = true, bool verifyManifest = true) const;
    bool recoverInterruptedRestore(QString *error);
    QString m_directory;
    QString m_connectionName;
    QSqlDatabase m_db;
    mutable QString m_error;
};
