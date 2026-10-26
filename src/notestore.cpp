#include "notestore.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSignalBlocker>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextDocument>
#include <QUuid>
#include <QVariant>

namespace {
const QString columns = QStringLiteral("id,parent_id,title,html,plain_text,created_at,updated_at,kind,trashed,favorite");
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
QVariant parentValue(const QString &id) { return id.isEmpty() ? QVariant() : QVariant(id); }
QString titleValue(const QString &title, bool folder) {
    const QString trimmed = title.trimmed();
    return trimmed.isEmpty() ? (folder ? QStringLiteral("新建文件夹") : QStringLiteral("无标题笔记")) : trimmed;
}
NoteNode readNode(const QSqlQuery &query) {
    NoteNode n;
    n.id = query.value(0).toString(); n.parentId = query.value(1).toString();
    n.title = query.value(2).toString(); n.html = query.value(3).toString();
    n.plainText = query.value(4).toString(); n.createdAt = query.value(5).toString();
    n.updatedAt = query.value(6).toString(); n.folder = query.value(7).toInt() == 1;
    n.trashed = query.value(8).toBool(); n.favorite = query.value(9).toBool();
    return n;
}
Attachment readAttachment(const QSqlQuery &q) {
    Attachment a;
    a.id = q.value(0).toString(); a.noteId = q.value(1).toString();
    a.relativePath = q.value(2).toString(); a.originalName = q.value(3).toString();
    a.mime = q.value(4).toString(); a.size = q.value(5).toLongLong();
    return a;
}
QString cleanAbsolute(const QString &path) {
    QFileInfo info(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    QStringList suffix;
    while (!info.exists() && info.absoluteFilePath() != info.absolutePath()) {
        suffix.prepend(info.fileName()); info.setFile(info.absolutePath());
    }
    QString base = info.canonicalFilePath();
    if (base.isEmpty()) base = info.absoluteFilePath();
    return QDir::cleanPath(base + (suffix.isEmpty() ? QString() : QLatin1Char('/') + suffix.join(QLatin1Char('/'))));
}
bool within(const QString &child, const QString &parent) {
    const QString a = cleanAbsolute(child), b = cleanAbsolute(parent);
    return a.startsWith(b + QLatin1Char('/'), Qt::CaseInsensitive);
}
bool removeOwnedTree(const QString &path, const QString &parent) {
    return within(path, parent) && !QFileInfo(path).isSymLink() && QDir(path).removeRecursively();
}
bool copyTree(const QString &source, const QString &target, QString *error) {
    const QDir sourceDir(source);
    if (!sourceDir.exists() || !QDir().mkpath(target)) {
        if (error) *error = QStringLiteral("无法创建备份附件目录。");
        return false;
    }
    const auto entries = sourceDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
    for (const QFileInfo &info : entries) {
        if (info.isSymLink()) {
            if (error) *error = QStringLiteral("资料库包含不支持的符号链接：") + info.fileName();
            return false;
        }
        const QString destination = QDir(target).filePath(info.fileName());
        if (info.isDir()) {
            if (!copyTree(info.absoluteFilePath(), destination, error)) return false;
        } else if (!QFile::copy(info.absoluteFilePath(), destination)) {
            if (error) *error = QStringLiteral("无法复制附件：") + info.fileName();
            return false;
        }
    }
    return true;
}
QString fileHash(const QString &path) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) return {};
    return QString::fromLatin1(hash.result().toHex());
}
bool writeJson(const QString &path, const QJsonObject &object) {
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(object).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
// Never follow a link (including a directory junction) while reading or removing a backup.
bool safeTree(const QString &directory) {
    const QFileInfo root(directory);
    if (!root.isDir() || root.isSymLink()) return false;
    QDirIterator it(directory, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const auto info = it.fileInfo();
        if (info.isSymLink() || !within(info.canonicalFilePath(), root.canonicalFilePath())) return false;
    }
    return true;
}
QString escapedLike(QString value) {
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    value.replace(QLatin1Char('_'), QStringLiteral("\\_"));
    return QLatin1Char('%') + value + QLatin1Char('%');
}
}

NoteStore::NoteStore(QObject *parent) : QObject(parent), m_connectionName(QStringLiteral("shiye-") + uuid()) {
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
}

NoteStore::~NoteStore() {
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool NoteStore::fail(const QString &message, QString *error) const {
    m_error = message;
    if (error) *error = message;
    return false;
}

QString NoteStore::dataDirectory() const { return m_directory; }
QString NoteStore::errorString() const { return m_error; }

bool NoteStore::connectDatabase(QString *error) {
    m_db.setDatabaseName(QDir(m_directory).filePath(QStringLiteral("notes.sqlite")));
    m_db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (!m_db.open()) return fail(QStringLiteral("无法打开笔记资料库：") + m_db.lastError().text(), error);
    QSqlQuery version(m_db);
    if (!version.exec(QStringLiteral("PRAGMA user_version")) || !version.next() || version.value(0).toInt() > 2)
        return fail(QStringLiteral("资料库版本比当前程序更新，请使用新版拾页打开。"), error);
    version.finish();
    QSqlQuery integrity(m_db);
    if (!integrity.exec(QStringLiteral("PRAGMA quick_check")) || !integrity.next() || integrity.value(0).toString() != QStringLiteral("ok"))
        return fail(QStringLiteral("资料库检查失败，请保留原文件并从完整备份恢复。"), error);
    integrity.finish();
    const QStringList statements = {
        QStringLiteral("PRAGMA foreign_keys=ON"),
        QStringLiteral("PRAGMA journal_mode=DELETE"),
        QStringLiteral("PRAGMA synchronous=FULL"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS nodes("
                       "id TEXT PRIMARY KEY,parent_id TEXT REFERENCES nodes(id),title TEXT NOT NULL,"
                       "html TEXT NOT NULL DEFAULT '',plain_text TEXT NOT NULL DEFAULT '',"
                       "created_at TEXT NOT NULL,updated_at TEXT NOT NULL,kind INTEGER NOT NULL CHECK(kind IN(0,1)),"
                       "trashed INTEGER NOT NULL DEFAULT 0,trash_batch TEXT NOT NULL DEFAULT '',favorite INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS nodes_parent ON nodes(parent_id,trashed)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS attachments("
                       "id TEXT PRIMARY KEY,note_id TEXT NOT NULL REFERENCES nodes(id),relative_path TEXT NOT NULL UNIQUE,"
                       "original_name TEXT NOT NULL,mime TEXT NOT NULL,size INTEGER NOT NULL CHECK(size>=0))"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS attachments_note ON attachments(note_id)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS revisions("
                       "id TEXT PRIMARY KEY,note_id TEXT NOT NULL REFERENCES nodes(id),title TEXT NOT NULL,"
                       "html TEXT NOT NULL,plain_text TEXT NOT NULL,saved_at TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS revisions_note ON revisions(note_id,saved_at)"),
        QStringLiteral("PRAGMA user_version=2")
    };
    for (const QString &statement : statements) {
        QSqlQuery q(m_db);
        if (!q.exec(statement)) return fail(QStringLiteral("初始化资料库失败：") + q.lastError().text(), error);
    }
    m_error.clear();
    return true;
}

bool NoteStore::open(const QString &dataDirectory, QString *error) {
    if (dataDirectory.trimmed().isEmpty()) return fail(QStringLiteral("资料库目录不能为空。"), error);
    m_db.close();
    m_directory = cleanAbsolute(dataDirectory);
    if (!recoverInterruptedRestore(error)) return false;
    const QFileInfo database(QDir(m_directory).filePath(QStringLiteral("notes.sqlite")));
    if (!database.exists()) {
        const QDir assets(QDir(m_directory).filePath(QStringLiteral("attachments")));
        if (QFileInfo::exists(QDir(m_directory).filePath(QStringLiteral("preferences.ini"))) ||
            !assets.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty() ||
            QDir(QDir(m_directory).filePath(QStringLiteral("backups"))).exists())
            return fail(QStringLiteral("原资料库的 notes.sqlite 缺失，已停止创建空库。请保留当前目录，从完整备份目录打开或恢复。"), error);
    } else if (!database.isFile() || database.size() == 0 || database.isSymLink()) {
        return fail(QStringLiteral("资料库文件为空或无效，已停止写入。请保留原文件并使用备份。"), error);
    }
    if (database.exists() && !validateBackup(m_directory, error, false, false)) return false;
    if (!QDir().mkpath(QDir(m_directory).filePath(QStringLiteral("attachments"))))
        return fail(QStringLiteral("无法创建资料库目录。"), error);
    return connectDatabase(error);
}

QList<NoteNode> NoteStore::nodes(bool trashed) const {
    QList<NoteNode> result;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT ") + columns + QStringLiteral(" FROM nodes WHERE trashed=? ORDER BY kind DESC,title COLLATE NOCASE"));
    q.addBindValue(trashed ? 1 : 0);
    if (!q.exec()) { fail(q.lastError().text()); return result; }
    while (q.next()) result.append(readNode(q));
    return result;
}

NoteNode NoteStore::node(const QString &id) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT ") + columns + QStringLiteral(" FROM nodes WHERE id=?"));
    q.addBindValue(id);
    if (!q.exec()) { fail(q.lastError().text()); return {}; }
    return q.next() ? readNode(q) : NoteNode();
}

bool NoteStore::validParent(const QString &parentId) const {
    if (parentId.isEmpty()) return true;
    const NoteNode parent = node(parentId);
    return !parent.id.isEmpty() && parent.folder && !parent.trashed;
}

QString NoteStore::createNode(const QString &title, const QString &parentId, bool folder, const QString &html, const QString &plainText) {
    if (!validParent(parentId)) { fail(QStringLiteral("目标文件夹不存在或已移入回收站。")); return {}; }
    const QString id = uuid(), stamp = now();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO nodes(id,parent_id,title,html,plain_text,created_at,updated_at,kind) VALUES(?,?,?,?,?,?,?,?)"));
    q.addBindValue(id); q.addBindValue(parentValue(parentId)); q.addBindValue(titleValue(title, folder));
    q.addBindValue(html.isNull() ? QStringLiteral("") : html);
    q.addBindValue(plainText.isNull() ? QStringLiteral("") : plainText);
    q.addBindValue(stamp); q.addBindValue(stamp); q.addBindValue(folder ? 1 : 0);
    if (!q.exec()) { fail(q.lastError().text()); return {}; }
    emit changed();
    return id;
}

QString NoteStore::createFolder(const QString &title, const QString &parentId) { return createNode(title, parentId, true, {}, {}); }
QString NoteStore::createNote(const QString &title, const QString &parentId, const QString &html, const QString &plainText) {
    QString text = plainText;
    if (text.isEmpty() && !html.isEmpty()) { QTextDocument document; document.setHtml(html); text = document.toPlainText(); }
    return createNode(title, parentId, false, html, text);
}

bool NoteStore::updateNote(const QString &id, const QString &title, const QString &html, const QString &plainText) {
    const NoteNode existing = node(id);
    if (existing.id.isEmpty() || existing.folder || existing.trashed) return fail(QStringLiteral("这篇笔记不存在或已删除。"));
    if (existing.title == titleValue(title, false) && existing.html == html && existing.plainText == plainText) return true;
    // A failed history write must also prevent overwriting the current note.
    if (!m_db.transaction()) return fail(m_db.lastError().text());
    QSqlQuery history(m_db);
    history.prepare(QStringLiteral("INSERT INTO revisions(id,note_id,title,html,plain_text,saved_at) VALUES(?,?,?,?,?,?)"));
    history.addBindValue(uuid()); history.addBindValue(id); history.addBindValue(existing.title);
    history.addBindValue(existing.html); history.addBindValue(existing.plainText); history.addBindValue(existing.updatedAt);
    if (!history.exec()) { m_db.rollback(); return fail(history.lastError().text()); }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE nodes SET title=?,html=?,plain_text=?,updated_at=? WHERE id=? AND kind=0 AND trashed=0"));
    q.addBindValue(titleValue(title, false)); q.addBindValue(html.isNull() ? QStringLiteral("") : html);
    q.addBindValue(plainText.isNull() ? QStringLiteral("") : plainText); q.addBindValue(now()); q.addBindValue(id);
    if (!q.exec()) { m_db.rollback(); return fail(q.lastError().text()); }
    if (!m_db.commit()) { m_db.rollback(); return fail(m_db.lastError().text()); }
    emit changed(); return true;
}

QList<NoteRevision> NoteStore::revisions(const QString &noteId, int limit, int offset) const {
    QList<NoteRevision> result;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id,note_id,title,saved_at FROM revisions WHERE note_id=? ORDER BY rowid DESC LIMIT ? OFFSET ?"));
    q.addBindValue(noteId); q.addBindValue(qBound(1, limit, 10000)); q.addBindValue(qMax(0, offset));
    if (!q.exec()) { fail(q.lastError().text()); return result; }
    while (q.next()) {
        NoteRevision r; r.id = q.value(0).toString(); r.noteId = q.value(1).toString();
        r.title = q.value(2).toString(); r.savedAt = q.value(3).toString(); result.append(r);
    }
    return result;
}

NoteRevision NoteStore::revision(const QString &id) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id,note_id,title,html,plain_text,saved_at FROM revisions WHERE id=?")); q.addBindValue(id);
    if (!q.exec()) { fail(q.lastError().text()); return {}; }
    if (!q.next()) return {};
    NoteRevision r; r.id = q.value(0).toString(); r.noteId = q.value(1).toString(); r.title = q.value(2).toString();
    r.html = q.value(3).toString(); r.plainText = q.value(4).toString(); r.savedAt = q.value(5).toString(); return r;
}

bool NoteStore::restoreRevision(const QString &noteId, const QString &revisionId) {
    const NoteRevision r = revision(revisionId);
    if (r.id.isEmpty() || r.noteId != noteId) return fail(QStringLiteral("所选历史版本不属于当前笔记。"));
    return updateNote(noteId, r.title, r.html, r.plainText);
}

bool NoteStore::renameNode(const QString &id, const QString &title) {
    const NoteNode existing = node(id);
    if (existing.id.isEmpty() || existing.trashed) return fail(QStringLiteral("无法重命名已删除的项目。"));
    if (!existing.folder) return updateNote(id, title, existing.html, existing.plainText);
    QSqlQuery q(m_db); q.prepare(QStringLiteral("UPDATE nodes SET title=?,updated_at=? WHERE id=?"));
    q.addBindValue(titleValue(title, existing.folder)); q.addBindValue(now()); q.addBindValue(id);
    if (!q.exec()) return fail(q.lastError().text());
    emit changed(); return true;
}

bool NoteStore::moveNode(const QString &id, const QString &parentId) {
    const NoteNode moving = node(id);
    if (moving.id.isEmpty() || moving.trashed || !validParent(parentId)) return fail(QStringLiteral("无法移动到这个目录。"));
    QSet<QString> visited;
    QString ancestor = parentId;
    while (!ancestor.isEmpty()) {
        if (ancestor == id || visited.contains(ancestor)) return fail(QStringLiteral("文件夹不能移动到自身或自己的子目录中。"));
        visited.insert(ancestor); ancestor = node(ancestor).parentId;
    }
    QSqlQuery q(m_db); q.prepare(QStringLiteral("UPDATE nodes SET parent_id=?,updated_at=? WHERE id=?"));
    q.addBindValue(parentValue(parentId)); q.addBindValue(now()); q.addBindValue(id);
    if (!q.exec()) return fail(q.lastError().text());
    emit changed(); return true;
}

bool NoteStore::setTrashed(const QString &id, bool trashed) {
    const NoteNode selected = node(id);
    if (selected.id.isEmpty()) return fail(QStringLiteral("项目不存在。"));
    if (selected.trashed == trashed) return true;
    QString batch = uuid();
    if (!trashed) {
        QSqlQuery query(m_db); query.prepare(QStringLiteral("SELECT trash_batch FROM nodes WHERE id=?")); query.addBindValue(id);
        if (!query.exec() || !query.next()) return fail(query.lastError().text());
        batch = query.value(0).toString();
    }
    QStringList descendants;
    {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral("WITH RECURSIVE branch(id) AS (SELECT id FROM nodes WHERE id=? UNION SELECT n.id FROM nodes n JOIN branch b ON n.parent_id=b.id) SELECT id FROM branch"));
        query.addBindValue(id);
        if (!query.exec()) return fail(query.lastError().text());
        while (query.next()) descendants.append(query.value(0).toString());
    }
    if (!m_db.transaction()) return fail(m_db.lastError().text());
    for (const QString &childId : descendants) {
        QSqlQuery q(m_db);
        if (trashed) {
            q.prepare(QStringLiteral("UPDATE nodes SET trashed=1,trash_batch=?,updated_at=? WHERE id=? AND trashed=0"));
            q.addBindValue(batch); q.addBindValue(now()); q.addBindValue(childId);
        } else {
            q.prepare(QStringLiteral("UPDATE nodes SET trashed=0,trash_batch='',updated_at=? WHERE id=? AND trashed=1 AND trash_batch=?"));
            q.addBindValue(now()); q.addBindValue(childId); q.addBindValue(batch);
        }
        if (!q.exec()) { m_db.rollback(); return fail(q.lastError().text()); }
    }
    if (!trashed && !selected.parentId.isEmpty() && !validParent(selected.parentId)) {
        QSqlQuery q(m_db); q.prepare(QStringLiteral("UPDATE nodes SET parent_id=NULL WHERE id=?")); q.addBindValue(id);
        if (!q.exec()) { m_db.rollback(); return fail(q.lastError().text()); }
    }
    if (!m_db.commit()) { m_db.rollback(); return fail(m_db.lastError().text()); }
    emit changed(); return true;
}

bool NoteStore::setFavorite(const QString &id, bool favorite) {
    const NoteNode existing = node(id);
    if (existing.id.isEmpty() || existing.folder || existing.trashed) return fail(QStringLiteral("无法收藏这个项目。"));
    QSqlQuery q(m_db); q.prepare(QStringLiteral("UPDATE nodes SET favorite=? WHERE id=?"));
    q.addBindValue(favorite ? 1 : 0); q.addBindValue(id);
    if (!q.exec()) return fail(q.lastError().text());
    emit changed(); return true;
}

QList<NoteNode> NoteStore::search(const QString &query, bool trashed) const {
    QList<NoteNode> result;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT ") + columns + QStringLiteral(" FROM nodes WHERE kind=0 AND trashed=? AND (title LIKE ? ESCAPE '\\' OR plain_text LIKE ? ESCAPE '\\') ORDER BY favorite DESC,updated_at DESC"));
    q.addBindValue(trashed ? 1 : 0); q.addBindValue(escapedLike(query.trimmed())); q.addBindValue(escapedLike(query.trimmed()));
    if (!q.exec()) { fail(q.lastError().text()); return result; }
    while (q.next()) result.append(readNode(q));
    return result;
}

Attachment NoteStore::importAttachment(const QString &noteId, const QString &sourcePath, QString *error) {
    const NoteNode noteRecord = node(noteId);
    const QFileInfo source(sourcePath);
    if (noteRecord.id.isEmpty() || noteRecord.folder || noteRecord.trashed) { fail(QStringLiteral("请先选择一篇可编辑的笔记。"), error); return {}; }
    if (!source.isFile() || !source.isReadable()) { fail(QStringLiteral("附件文件不存在或无法读取。"), error); return {}; }
    Attachment a; a.id = uuid(); a.noteId = noteId; a.originalName = source.fileName(); a.size = source.size();
    a.mime = QMimeDatabase().mimeTypeForFile(source).name();
    QString extension = source.suffix().toLower();
    if (extension.size() > 16 || extension.contains(QRegularExpression(QStringLiteral("[^a-z0-9]")))) extension.clear();
    a.relativePath = QStringLiteral("attachments/") + a.id + (extension.isEmpty() ? QString() : QLatin1Char('.') + extension);
    const QString destination = QDir(m_directory).filePath(a.relativePath);
    if (!QFile::copy(source.absoluteFilePath(), destination)) { fail(QStringLiteral("无法复制附件到资料库。"), error); return {}; }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO attachments(id,note_id,relative_path,original_name,mime,size) VALUES(?,?,?,?,?,?)"));
    q.addBindValue(a.id); q.addBindValue(a.noteId); q.addBindValue(a.relativePath);
    q.addBindValue(a.originalName); q.addBindValue(a.mime); q.addBindValue(a.size);
    if (!q.exec()) { QFile::remove(destination); fail(q.lastError().text(), error); return {}; }
    emit changed(); return a;
}

QList<Attachment> NoteStore::attachments(const QString &noteId) const {
    QList<Attachment> result; QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id,note_id,relative_path,original_name,mime,size FROM attachments WHERE note_id=? ORDER BY rowid")); q.addBindValue(noteId);
    if (!q.exec()) { fail(q.lastError().text()); return result; }
    while (q.next()) result.append(readAttachment(q));
    return result;
}

Attachment NoteStore::attachment(const QString &id) const {
    QSqlQuery q(m_db); q.prepare(QStringLiteral("SELECT id,note_id,relative_path,original_name,mime,size FROM attachments WHERE id=?")); q.addBindValue(id);
    if (!q.exec()) { fail(q.lastError().text()); return {}; }
    return q.next() ? readAttachment(q) : Attachment();
}

QString NoteStore::importNoteBundle(const QString &title, const QString &parentId, const QStringList &sourcePaths,
    const std::function<QPair<QString, QString>(const QList<Attachment> &)> &render, QString *error) {
    if (!validParent(parentId)) { fail(QStringLiteral("目标目录不可用。"), error); return {}; }
    if (!m_db.transaction()) { fail(m_db.lastError().text(), error); return {}; }
    QString id, reason;
    QList<Attachment> imported;
    bool success = false;
    {
        const QSignalBlocker blocker(this);
        id = createNote(title, parentId);
        if (id.isEmpty()) reason = errorString();
        for (const QString &path : sourcePaths) {
            if (!reason.isEmpty()) break;
            const auto attachment = importAttachment(id, path, &reason);
            if (attachment.isValid()) imported.append(attachment);
        }
        if (reason.isEmpty()) {
            const auto content = render(imported);
            if (content.first.isEmpty()) reason = QStringLiteral("Markdown 转换失败，未导入笔记。");
            else {
                QSqlQuery q(m_db); q.prepare(QStringLiteral("UPDATE nodes SET html=?,plain_text=? WHERE id=?"));
                q.addBindValue(content.first); q.addBindValue(content.second.isNull() ? QStringLiteral("") : content.second); q.addBindValue(id);
                if (!q.exec()) reason = q.lastError().text();
                else if (!m_db.commit()) reason = m_db.lastError().text();
                else success = true;
            }
        }
        if (!success) {
            m_db.rollback();
            for (const auto &attachment : imported) QFile::remove(QDir(m_directory).filePath(attachment.relativePath));
        }
    }
    if (!success) { fail(reason, error); return {}; }
    emit changed(); return id;
}

bool NoteStore::backupTo(const QString &targetDirectory, QString *error, bool automatic) {
    const QString target = cleanAbsolute(targetDirectory);
    const QString assets = QDir(m_directory).filePath(QStringLiteral("attachments"));
    if (target.compare(m_directory, Qt::CaseInsensitive) == 0 || target.compare(assets, Qt::CaseInsensitive) == 0 || within(target, assets))
        return fail(QStringLiteral("备份位置不能位于附件目录中。"), error);
    if (!safeTree(assets)) return fail(QStringLiteral("附件目录含有链接或无效路径，无法备份。"), error);
    if (QFileInfo::exists(target) && (!QFileInfo(target).isDir() || QFileInfo(target).isSymLink() ||
        !QDir(target).entryList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System).isEmpty()))
        return fail(QStringLiteral("请选择一个新的或空的备份文件夹。"), error);
    const QString parent = QFileInfo(target).absolutePath();
    const QString stage = target + QStringLiteral(".partial-") + uuid();
    if (!QDir().mkpath(stage)) return fail(QStringLiteral("无法创建备份目录。"), error);
    m_db.close();
    QString copyError;
    bool copied = QFile::copy(QDir(m_directory).filePath(QStringLiteral("notes.sqlite")), QDir(stage).filePath(QStringLiteral("notes.sqlite")))
        && copyTree(QDir(m_directory).filePath(QStringLiteral("attachments")), QDir(stage).filePath(QStringLiteral("attachments")), &copyError);
    const bool reopened = connectDatabase();
    if (copied) {
        QJsonObject hashes;
        QDirIterator files(stage, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QString path = files.next(), hash = fileHash(path);
            if (hash.isEmpty()) { copied = false; break; }
            hashes.insert(QDir(stage).relativeFilePath(path), hash);
        }
        if (copied) copied = writeJson(QDir(stage).filePath(QStringLiteral("backup.json")),
            {{QStringLiteral("format"), QStringLiteral("shiye-backup")}, {QStringLiteral("version"), 2},
             {QStringLiteral("createdAt"), now()}, {QStringLiteral("automatic"), automatic}, {QStringLiteral("sha256"), hashes}});
        if (copied) copied = validateBackup(stage, &copyError);
    }
    if (!copied || !reopened) {
        removeOwnedTree(stage, parent);
        return fail(!reopened ? QStringLiteral("备份后无法重新打开资料库：") + m_error : (copyError.isEmpty() ? QStringLiteral("备份文件写入失败。") : copyError), error);
    }
    if (QFileInfo::exists(target) && !QDir().rmdir(target)) {
        removeOwnedTree(stage, parent); return fail(QStringLiteral("无法使用所选备份目录。"), error);
    }
    if (!QDir().rename(stage, target)) { removeOwnedTree(stage, parent); return fail(QStringLiteral("无法完成备份目录写入。"), error); }
    return true;
}

bool NoteStore::automaticBackup(const QString &root, QString *createdPath, QString *error) {
    if (root.trimmed().isEmpty()) return fail(QStringLiteral("请设置自动备份目录。"), error);
    const QString target = QDir(root).filePath(QStringLiteral("auto-") + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz-")) + uuid());
    if (!backupTo(target, error, true)) return false;
    if (createdPath) *createdPath = target;
    // Only prune our own completed automatic backups, and only after a new verified copy exists.
    const auto entries = QDir(root).entryInfoList({QStringLiteral("auto-*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    int kept = 0;
    for (const auto &entry : entries) {
        if (!QRegularExpression(QStringLiteral("^auto-[0-9]{8}-[0-9]{6}-[0-9]{3}-[0-9a-f-]{36}$")).match(entry.fileName()).hasMatch()) continue;
        QFile manifest(QDir(entry.absoluteFilePath()).filePath(QStringLiteral("backup.json")));
        if (!manifest.open(QIODevice::ReadOnly)) continue;
        const auto object = QJsonDocument::fromJson(manifest.readAll()).object(); manifest.close();
        if (object.value(QStringLiteral("format")).toString() != QStringLiteral("shiye-backup") || !object.value(QStringLiteral("automatic")).toBool()) continue;
        if (++kept > 10 && entry.absoluteFilePath() != target && safeTree(entry.absoluteFilePath())) removeOwnedTree(entry.absoluteFilePath(), root);
    }
    return true;
}

bool NoteStore::checkIntegrity(QString *error) const {
    return validateBackup(m_directory, error, true, false);
}

bool NoteStore::validateBackup(const QString &directory, QString *error, bool includeAttachments, bool verifyManifest) const {
    const QString database = QDir(directory).filePath(QStringLiteral("notes.sqlite"));
    if (!QFileInfo(database).isFile() || QFileInfo(database).isSymLink() || (includeAttachments && !QDir(QDir(directory).filePath(QStringLiteral("attachments"))).exists()))
        return fail(QStringLiteral("这不是完整的拾页备份：缺少数据库或附件目录。"), error);
    if (includeAttachments && !safeTree(QDir(directory).filePath(QStringLiteral("attachments")))) return fail(QStringLiteral("附件包含不支持的链接或路径。"), error);
    const QString manifestPath = QDir(directory).filePath(QStringLiteral("backup.json"));
    if (verifyManifest && QFileInfo::exists(manifestPath)) {
        QFile manifest(manifestPath);
        if (!manifest.open(QIODevice::ReadOnly)) return fail(QStringLiteral("无法读取备份校验文件。"), error);
        const auto object = QJsonDocument::fromJson(manifest.readAll()).object();
        const int version = object.value(QStringLiteral("version")).toInt();
        if (object.value(QStringLiteral("format")).toString() != QStringLiteral("shiye-backup") || (version != 1 && version != 2))
            return fail(QStringLiteral("备份格式无效或需要新版拾页。"), error);
        if (version == 2) {
            const auto hashes = object.value(QStringLiteral("sha256")).toObject();
            QStringList files{QStringLiteral("notes.sqlite")};
            QDirIterator it(QDir(directory).filePath(QStringLiteral("attachments")), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
            while (it.hasNext()) files.append(QDir(directory).relativeFilePath(it.next()));
            if (hashes.size() != files.size()) return fail(QStringLiteral("备份文件数量与校验记录不符。"), error);
            for (const QString &file : files) {
                const QString expected = hashes.value(file).toString();
                if (expected.size() != 64 || fileHash(QDir(directory).filePath(file)) != expected)
                    return fail(QStringLiteral("备份校验失败，文件可能损坏：") + file, error);
            }
        }
    }
    const QString connection = QStringLiteral("shiye-validation-") + uuid();
    bool valid = true; QString reason;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(database); db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!db.open()) { valid = false; reason = QStringLiteral("无法读取备份数据库。"); }
        if (valid) {
            QSqlQuery q(db);
            if (!q.exec(QStringLiteral("PRAGMA user_version")) || !q.next() || q.value(0).toInt() > 2) {
                valid = false; reason = QStringLiteral("备份资料库版本不兼容。");
            }
            const int schemaVersion = q.value(0).toInt();
            if (valid && schemaVersion >= 2 && !db.tables().contains(QStringLiteral("revisions"))) {
                valid = false; reason = QStringLiteral("历史版本表缺失，已停止写入资料库。");
            }
            if (!q.exec(QStringLiteral("PRAGMA integrity_check")) || !q.next() || q.value(0).toString() != QStringLiteral("ok")) {
                valid = false; reason = QStringLiteral("备份数据库完整性检查失败。");
            }
            if (valid && !q.exec(QStringLiteral("SELECT ") + columns + QStringLiteral(",trash_batch FROM nodes LIMIT 1"))) {
                valid = false; reason = QStringLiteral("备份数据库格式不兼容。");
            }
            if (valid && (!q.exec(QStringLiteral("PRAGMA foreign_key_check")) || q.next())) {
                valid = false; reason = QStringLiteral("备份存在损坏的目录或附件关系。");
            }
            if (valid && db.tables().contains(QStringLiteral("revisions")) &&
                !q.exec(QStringLiteral("SELECT id,note_id,title,html,plain_text,saved_at FROM revisions LIMIT 1"))) {
                valid = false; reason = QStringLiteral("历史版本数据格式异常。");
            }
            if (valid && !q.exec(QStringLiteral("SELECT relative_path,size FROM attachments"))) {
                valid = false; reason = QStringLiteral("备份附件索引缺失。");
            }
            while (valid && includeAttachments && q.next()) {
                const QString relative = q.value(0).toString();
                const QString full = QDir(directory).filePath(relative);
                const QFileInfo info(full);
                if (!relative.startsWith(QStringLiteral("attachments/")) || !within(full, QDir(directory).filePath(QStringLiteral("attachments"))) || !info.isFile() || info.isSymLink() || info.size() != q.value(1).toLongLong()) {
                    valid = false; reason = QStringLiteral("备份附件缺失、大小异常或路径无效。");
                }
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return valid || fail(reason, error);
}

bool NoteStore::restoreFrom(const QString &sourceDirectory, QString *error) {
    const QString source = cleanAbsolute(sourceDirectory);
    if (source.compare(m_directory, Qt::CaseInsensitive) == 0 || within(m_directory, source)) return fail(QStringLiteral("请从独立的备份目录恢复。"), error);
    if (!QFileInfo::exists(QDir(source).filePath(QStringLiteral("backup.json"))))
        return fail(QStringLiteral("备份校验文件 backup.json 缺失，已停止恢复。"), error);
    if (!validateBackup(source, error)) return false;
    const QString token = uuid();
    const QString stage = QDir(m_directory).filePath(QStringLiteral(".restore-stage-") + token);
    if (!QDir().mkpath(stage)) return fail(QStringLiteral("无法创建恢复暂存目录。"), error);
    QString copyError;
    if (!QFile::copy(QDir(source).filePath(QStringLiteral("notes.sqlite")), QDir(stage).filePath(QStringLiteral("notes.sqlite"))) ||
        !copyTree(QDir(source).filePath(QStringLiteral("attachments")), QDir(stage).filePath(QStringLiteral("attachments")), &copyError) ||
        (QFileInfo::exists(QDir(source).filePath(QStringLiteral("backup.json"))) &&
         !QFile::copy(QDir(source).filePath(QStringLiteral("backup.json")), QDir(stage).filePath(QStringLiteral("backup.json")))) ||
        !validateBackup(stage, error)) {
        removeOwnedTree(stage, m_directory);
        return fail(copyError.isEmpty() ? QStringLiteral("无法准备恢复文件，当前笔记未改变。") : copyError, error);
    }
    const QString safety = QDir(m_directory).filePath(QStringLiteral("backups/pre-restore-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QLatin1Char('-') + token.left(8));
    if (!backupTo(safety, error)) { removeOwnedTree(stage, m_directory); return false; }
    const QString database = QDir(m_directory).filePath(QStringLiteral("notes.sqlite"));
    const QString assets = QDir(m_directory).filePath(QStringLiteral("attachments"));
    const QString oldDatabase = QDir(m_directory).filePath(QStringLiteral(".restore-old-") + token + QStringLiteral(".sqlite"));
    const QString oldAssets = QDir(m_directory).filePath(QStringLiteral(".restore-old-") + token + QStringLiteral("-attachments"));
    const QString marker = QDir(m_directory).filePath(QStringLiteral("restore-pending.json"));
    if (!writeJson(marker, {{QStringLiteral("token"), token}})) {
        removeOwnedTree(stage, m_directory);
        return fail(QStringLiteral("无法写入恢复保护记录，当前资料未改变。"), error);
    }
    m_db.close();
    const bool movedDatabase = QFile::rename(database, oldDatabase);
    const bool movedAssets = movedDatabase && QDir().rename(assets, oldAssets);
    const bool installedDatabase = movedAssets && QFile::rename(QDir(stage).filePath(QStringLiteral("notes.sqlite")), database);
    const bool installedAssets = installedDatabase && QDir().rename(QDir(stage).filePath(QStringLiteral("attachments")), assets);
    const bool reopened = installedAssets && connectDatabase();
    if (!reopened) {
        m_db.close();
        const bool recovered = recoverInterruptedRestore(error) && connectDatabase();
        return fail(recovered ? QStringLiteral("恢复未完成，已保留原资料库。") : QStringLiteral("恢复失败，请从 backups 中的恢复前备份重新打开资料库。"), error);
    }
    // Removing this marker is the commit point. Before it, startup rolls back to the old library.
    if (!QFile::remove(marker)) {
        m_db.close(); recoverInterruptedRestore(nullptr); connectDatabase();
        return fail(QStringLiteral("无法完成恢复保护记录，已回到恢复前的资料。请检查目录权限并重启。"), error);
    }
    QFile::remove(oldDatabase);
    removeOwnedTree(oldAssets, m_directory);
    removeOwnedTree(stage, m_directory);
    emit changed(); return true;
}

bool NoteStore::recoverInterruptedRestore(QString *error) {
    const QString marker = QDir(m_directory).filePath(QStringLiteral("restore-pending.json"));
    if (!QFileInfo::exists(marker)) return true;
    QFile file(marker);
    if (!file.open(QIODevice::ReadOnly)) return fail(QStringLiteral("无法读取恢复保护记录，已停止打开资料库。"), error);
    const QString token = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("token")).toString(); file.close();
    if (!QRegularExpression(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")).match(token).hasMatch())
        return fail(QStringLiteral("恢复保护记录异常，请保留目录并使用恢复前备份。"), error);
    const QString prefix = QDir(m_directory).filePath(QStringLiteral(".restore-old-") + token);
    const QList<QPair<QString, QString>> pairs = {
        {prefix + QStringLiteral(".sqlite"), QDir(m_directory).filePath(QStringLiteral("notes.sqlite"))},
        {prefix + QStringLiteral("-attachments"), QDir(m_directory).filePath(QStringLiteral("attachments"))}
    };
    for (const auto &pair : pairs) {
        if (!QFileInfo::exists(pair.first)) continue;
        if (QFileInfo(pair.first).isSymLink() || QFileInfo(pair.second).isSymLink())
            return fail(QStringLiteral("恢复路径异常，已保留全部文件并停止操作。"), error);
        // Keep the interrupted replacement too. No user data is deleted during rollback.
        if (QFileInfo::exists(pair.second) && !QDir().rename(pair.second, pair.second + QStringLiteral(".interrupted-") + uuid()))
            return fail(QStringLiteral("无法保留中断的恢复文件，请关闭占用该目录的程序后重试。"), error);
        if (!QDir().rename(pair.first, pair.second)) return fail(QStringLiteral("无法还原恢复前文件，请保留当前目录并重试。"), error);
    }
    if (!validateBackup(m_directory, error)) return false;
    if (!QFile::remove(marker)) return fail(QStringLiteral("无法完成恢复保护记录清理，请检查目录写入权限。"), error);
    return true;
}

bool NoteStore::seedIfEmpty() {
    QSqlQuery count(m_db);
    if (!count.exec(QStringLiteral("SELECT COUNT(*) FROM nodes")) || !count.next()) return fail(count.lastError().text());
    if (count.value(0).toInt() > 0) return true;
    count.finish();
    if (!m_db.transaction()) return fail(m_db.lastError().text());
    const QString learning = createFolder(QStringLiteral("学习与成长"));
    const QString work = createFolder(QStringLiteral("工作与项目"));
    const QString life = createFolder(QStringLiteral("生活与灵感"));
    const QString qt = createFolder(QStringLiteral("Qt 学习"), learning);
    const QString reading = createFolder(QStringLiteral("阅读笔记"), learning);
    const QString garden = createNote(QStringLiteral("建立自己的知识花园"), learning,
        QStringLiteral("<p>笔记不只是信息的仓库，也可以是一座慢慢生长的花园。把零散的想法种下来，在回顾和连接中，让它们成为自己的知识。</p>"
        "<h2>从记录，到连接</h2><p>不急着整理出完美的体系，先留下那些让我停下来思考的片刻。</p>"
        "<blockquote><b>让记录变得轻一点</b><p>每一条笔记，只需要回答一个问题：它为什么对我有用？</p></blockquote>"
        "<h2>给知识一点生长的空间</h2><ul><li>建立一个随手记录的目录。</li><li>每周留 15 分钟，回顾最近的笔记。</li><li>用自己的话，写下一个新的理解。</li></ul>"
        "<p>你也可以把图片和视频拖到编辑区，和想法一起保存。</p>"));
    const QString qtNote = createNote(QStringLiteral("Qt 学习：让界面有呼吸感"), qt,
        QStringLiteral("<h2>今天的三个发现</h2><ul><li>用统一的间距，让界面有自然的节奏。</li><li>让正文、说明和操作拥有清晰的视觉层级。</li><li>及时反馈每一个保存和导入操作。</li></ul><h2>下一步</h2><p>给常用控件建立统一的主题变量。</p>"));
    const QString bookNote = createNote(QStringLiteral("让阅读真正发生"), reading,
        QStringLiteral("<p>读完一本书以后，留下的未必是原句，而是看问题的新角度。</p><h2>我想记住的</h2><p>遇到有触动的地方，停下来写一句自己的理解。</p><h2>把书里的想法带进生活</h2><p>选择一个观点，试着用一周时间实践。</p>"));
    const QString projectNote = createNote(QStringLiteral("拾页 · 产品设计备忘"), work,
        QStringLiteral("<h2>做一款愿意每天打开的笔记工具</h2><p>目录简单，记录顺手，内容可以放心保存。</p><ul><li>按自己的习惯创建分级文件夹。</li><li>图片可以直接插入正文。</li><li>视频作为附件保存，随时打开播放。</li></ul>"));
    const QString lifeNote = createNote(QStringLiteral("周末，去公园走走"), life,
        QStringLiteral("<p>秋天的阳光刚刚好。</p><h2>带上这些就出发</h2><ul><li>一本读到一半的书。</li><li>一杯热咖啡。</li><li>一个不用赶时间的下午。</li></ul><p>拍下树叶的颜色，也给自己一点发呆的时间。</p>"));
    if (learning.isEmpty() || work.isEmpty() || life.isEmpty() || qt.isEmpty() || reading.isEmpty() || garden.isEmpty() || qtNote.isEmpty() || bookNote.isEmpty() || projectNote.isEmpty() || lifeNote.isEmpty() || !setFavorite(garden, true)) {
        m_db.rollback(); return false;
    }
    if (!m_db.commit()) { m_db.rollback(); return fail(m_db.lastError().text()); }
    emit changed(); return true;
}
