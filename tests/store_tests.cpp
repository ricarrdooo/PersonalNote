#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QUuid>
#include "notestore.h"

class StoreTests : public QObject {
    Q_OBJECT
private slots:
    void directoriesAndRestart();
    void chineseAndLiteralSearch();
    void recycleBinPreservesIndependentDeletes();
    void attachmentCopySurvivesOriginalRemoval();
    void backupRestoreAndInvalidBackup();
    void initialLibraryIsSeededOnce();
    void historySurvivesRestartAndRestoration();
    void failedHistoryWriteDoesNotOverwriteNote();
    void backupDetectsSameSizeCorruption();
    void automaticBackupsRetainOnlyOwnedCompletedCopies();
    void interruptedRestoreRecovers_data();
    void interruptedRestoreRecovers();
    void missingDatabaseDoesNotCreateEmptyLibrary();
    void oldLibraryMigratesWithoutLosingContent();
    void missingAttachmentKeepsNotesReadable();
    void rejectsIncompleteAndFutureLibraries();
    void legacyBackupsRemainRestorable();
};

static bool writeFile(const QString &path, const QByteArray &content) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

static bool executeSql(const QString &directory, const QStringList &statements) {
    const QString connection = QUuid::createUuid().toString();
    bool ok = true;
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(QDir(directory).filePath(QStringLiteral("notes.sqlite")));
        ok = db.open();
        if (ok) for (const auto &statement : statements) { QSqlQuery q(db); if (!q.exec(statement)) { ok = false; break; } }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection); return ok;
}

void StoreTests::historySurvivesRestartAndRestoration() {
    QTemporaryDir temp; QString note, firstRevision;
    {
        NoteStore store; QVERIFY(store.open(temp.path()));
        note = store.createNote(QStringLiteral("第一版"), {}, QStringLiteral("<h1>中文知识</h1><p>原文</p>"));
        QVERIFY(store.updateNote(note, QStringLiteral("第二版"), QStringLiteral("<p>新文</p>"), QStringLiteral("新文")));
        QCOMPARE(store.revisions(note).size(), 1); firstRevision = store.revisions(note).first().id;
        QVERIFY(store.updateNote(note, QStringLiteral("第二版"), QStringLiteral("<p>新文</p>"), QStringLiteral("新文")));
        QCOMPARE(store.revisions(note).size(), 1);
        QVERIFY(store.renameNode(note, QStringLiteral("重命名")));
        QCOMPARE(store.revisions(note).size(), 2);
        QCOMPARE(store.revisions(note, 1, 1).first().id, firstRevision);
    }
    NoteStore store; QVERIFY(store.open(temp.path()));
    QVERIFY(store.restoreRevision(note, firstRevision));
    QCOMPARE(store.node(note).title, QStringLiteral("第一版"));
    QVERIFY(store.node(note).html.contains(QStringLiteral("中文知识")));
    QCOMPARE(store.revisions(note).size(), 3);
    QCOMPARE(store.revision(store.revisions(note).first().id).title, QStringLiteral("重命名"));
    const QString other = store.createNote(QStringLiteral("其他笔记"));
    QVERIFY(!store.restoreRevision(other, firstRevision));
    QVERIFY(store.setTrashed(note, true)); QVERIFY(!store.restoreRevision(note, firstRevision));
    QVERIFY(store.setTrashed(note, false));
    const QString backup = QDir(temp.path()).filePath(QStringLiteral("history-backup"));
    QVERIFY2(store.backupTo(backup), qPrintable(store.errorString()));
    QVERIFY(store.updateNote(note, QStringLiteral("备份后"), QStringLiteral("changed"), QStringLiteral("changed")));
    QVERIFY2(store.restoreFrom(backup), qPrintable(store.errorString()));
    QCOMPARE(store.revisions(note).size(), 3); QCOMPARE(store.node(note).title, QStringLiteral("第一版"));
}

void StoreTests::failedHistoryWriteDoesNotOverwriteNote() {
    QTemporaryDir temp; NoteStore store; QVERIFY(store.open(temp.path()));
    const QString note = store.createNote(QStringLiteral("原标题"), {}, QStringLiteral("<p>不可丢失</p>"));
    QVERIFY(executeSql(temp.path(), {QStringLiteral("CREATE TRIGGER fail_revision BEFORE INSERT ON revisions BEGIN SELECT RAISE(ABORT, 'simulated disk failure'); END")}));
    QVERIFY(!store.updateNote(note, QStringLiteral("新标题"), QStringLiteral("<p>覆盖</p>"), QStringLiteral("覆盖")));
    QCOMPARE(store.node(note).title, QStringLiteral("原标题")); QCOMPARE(store.revisions(note).size(), 0);
    QVERIFY(executeSql(temp.path(), {QStringLiteral("DROP TRIGGER fail_revision")}));
    QVERIFY(executeSql(temp.path(), {QStringLiteral("CREATE TRIGGER fail_note BEFORE UPDATE ON nodes BEGIN SELECT RAISE(ABORT, 'simulated write failure'); END")}));
    QVERIFY(!store.updateNote(note, QStringLiteral("新标题"), QStringLiteral("body"), QStringLiteral("body")));
    QCOMPARE(store.revisions(note).size(), 0); QCOMPARE(store.node(note).title, QStringLiteral("原标题"));
    QVERIFY(executeSql(temp.path(), {QStringLiteral("DROP TRIGGER fail_note")}));
    QVERIFY(store.updateNote(note, QStringLiteral("恢复写入"), QStringLiteral("<p>内容</p>"), QStringLiteral("内容")));
    QCOMPARE(store.revisions(note).size(), 1);
}

void StoreTests::backupDetectsSameSizeCorruption() {
    QTemporaryDir temp; NoteStore store; QVERIFY(store.open(QDir(temp.path()).filePath(QStringLiteral("library"))));
    const QString note = store.createNote(QStringLiteral("正文"));
    const QString source = QDir(temp.path()).filePath(QStringLiteral("media.bin")); QVERIFY(writeFile(source, "original"));
    const auto attachment = store.importAttachment(note, source); QVERIFY(attachment.isValid());
    const QString backup = QDir(temp.path()).filePath(QStringLiteral("backup")); QVERIFY(store.backupTo(backup));
    QVERIFY(writeFile(QDir(backup).filePath(attachment.relativePath), "tampered"));
    QString error; QVERIFY(!store.restoreFrom(backup, &error)); QVERIFY(error.contains(QStringLiteral("校验失败")));
    QCOMPARE(store.node(note).title, QStringLiteral("正文"));
    QVERIFY(store.checkIntegrity());
    QVERIFY(QFile::remove(QDir(store.dataDirectory()).filePath(attachment.relativePath)));
    QVERIFY(!store.checkIntegrity());
    QVERIFY(!store.backupTo(QDir(temp.path()).filePath(QStringLiteral("incomplete"))));
}

void StoreTests::automaticBackupsRetainOnlyOwnedCompletedCopies() {
    QTemporaryDir temp; NoteStore store; QVERIFY(store.open(QDir(temp.path()).filePath(QStringLiteral("library"))));
    const QString note = store.createNote(QStringLiteral("知识库"));
    const QString root = QDir(temp.path()).filePath(QStringLiteral("automatic"));
    QVERIFY(store.backupTo(QDir(root).filePath(QStringLiteral("manual"))));
    QVERIFY(QDir().mkpath(QDir(root).filePath(QStringLiteral("auto-unrelated"))));
    QString latest;
    for (int i = 0; i < 12; ++i) {
        QVERIFY(store.updateNote(note, QString::number(i), QStringLiteral("text"), QStringLiteral("text")));
        QVERIFY2(store.automaticBackup(root, &latest), qPrintable(store.errorString()));
    }
    QCOMPARE(QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 12);
    QVERIFY(QDir(QDir(root).filePath(QStringLiteral("manual"))).exists());
    QVERIFY(store.restoreFrom(latest)); QCOMPARE(store.node(note).title, QStringLiteral("11"));
    QVERIFY(!store.automaticBackup(QDir(store.dataDirectory()).filePath(QStringLiteral("attachments"))));
    QCOMPARE(QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 12);
}

void StoreTests::interruptedRestoreRecovers_data() {
    QTest::addColumn<int>("phase");
    for (int i = 0; i <= 4; ++i) QTest::newRow(qPrintable(QStringLiteral("phase-%1").arg(i))) << i;
}

void StoreTests::interruptedRestoreRecovers() {
    QFETCH(int, phase); QTemporaryDir temp;
    const QString library = QDir(temp.path()).filePath(QStringLiteral("library"));
    const QString replacement = QDir(temp.path()).filePath(QStringLiteral("replacement"));
    QString original;
    {
        NoteStore store; QVERIFY(store.open(library)); original = store.createNote(QStringLiteral("原资料必须保留"));
        const QString media = QDir(temp.path()).filePath(QStringLiteral("media")); QVERIFY(writeFile(media, "bytes"));
        QVERIFY(store.importAttachment(original, media).isValid());
        NoteStore other; QVERIFY(other.open(replacement)); QVERIFY(!other.createNote(QStringLiteral("替换内容")).isEmpty());
    }
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVERIFY(writeFile(QDir(library).filePath(QStringLiteral("restore-pending.json")), QJsonDocument(QJsonObject{{QStringLiteral("token"), token}}).toJson()));
    const QString prefix = QDir(library).filePath(QStringLiteral(".restore-old-") + token);
    if (phase >= 1) QVERIFY(QFile::rename(QDir(library).filePath(QStringLiteral("notes.sqlite")), prefix + QStringLiteral(".sqlite")));
    if (phase >= 2) QVERIFY(QDir().rename(QDir(library).filePath(QStringLiteral("attachments")), prefix + QStringLiteral("-attachments")));
    if (phase >= 3) QVERIFY(QFile::copy(QDir(replacement).filePath(QStringLiteral("notes.sqlite")), QDir(library).filePath(QStringLiteral("notes.sqlite"))));
    if (phase >= 4) QVERIFY(QDir().mkpath(QDir(library).filePath(QStringLiteral("attachments"))));
    NoteStore reopened; QVERIFY2(reopened.open(library), qPrintable(reopened.errorString()));
    QCOMPARE(reopened.node(original).title, QStringLiteral("原资料必须保留")); QVERIFY(reopened.checkIntegrity());
    QVERIFY(!QFileInfo::exists(QDir(library).filePath(QStringLiteral("restore-pending.json"))));
}

void StoreTests::missingDatabaseDoesNotCreateEmptyLibrary() {
    QTemporaryDir temp;
    QVERIFY(writeFile(QDir(temp.path()).filePath(QStringLiteral("preferences.ini")), "existing-library"));
    NoteStore store; QVERIFY(!store.open(temp.path()));
    QVERIFY(!QFileInfo::exists(QDir(temp.path()).filePath(QStringLiteral("notes.sqlite"))));
}

void StoreTests::oldLibraryMigratesWithoutLosingContent() {
    QTemporaryDir temp; QString id;
    { NoteStore store; QVERIFY(store.open(temp.path())); id = store.createNote(QStringLiteral("旧笔记"), {}, QStringLiteral("<p>保留正文</p>")); }
    QVERIFY(executeSql(temp.path(), {QStringLiteral("DROP TABLE revisions"), QStringLiteral("PRAGMA user_version=0")}));
    NoteStore store; QVERIFY2(store.open(temp.path()), qPrintable(store.errorString()));
    QCOMPARE(store.node(id).title, QStringLiteral("旧笔记"));
    QVERIFY(store.updateNote(id, QStringLiteral("修改"), QStringLiteral("<p>新正文</p>"), QStringLiteral("新正文")));
    QCOMPARE(store.revisions(id).size(), 1); QVERIFY(store.checkIntegrity());
}

void StoreTests::missingAttachmentKeepsNotesReadable() {
    QTemporaryDir temp; QString note;
    {
        NoteStore store; QVERIFY(store.open(temp.path())); note = store.createNote(QStringLiteral("仍可阅读的正文"));
        const QString source = QDir(temp.path()).filePath(QStringLiteral("source")); QVERIFY(writeFile(source, "bytes"));
        const auto media = store.importAttachment(note, source); QVERIFY(media.isValid());
        QVERIFY(QFile::remove(QDir(temp.path()).filePath(media.relativePath)));
    }
    NoteStore store; QVERIFY2(store.open(temp.path()), qPrintable(store.errorString()));
    QCOMPARE(store.node(note).title, QStringLiteral("仍可阅读的正文")); QVERIFY(!store.checkIntegrity());
    QVERIFY(!store.backupTo(QDir(temp.path()).filePath(QStringLiteral("bad-backup"))));
}

void StoreTests::rejectsIncompleteAndFutureLibraries() {
    QTemporaryDir temp;
    { NoteStore store; QVERIFY(store.open(temp.path())); QVERIFY(!store.createNote(QStringLiteral("保留")).isEmpty()); }
    QVERIFY(executeSql(temp.path(), {QStringLiteral("PRAGMA user_version=999")}));
    { NoteStore store; QVERIFY(!store.open(temp.path())); }
    QVERIFY(executeSql(temp.path(), {QStringLiteral("PRAGMA user_version=2"), QStringLiteral("DROP TABLE attachments")}));
    NoteStore store; QVERIFY(!store.open(temp.path()));
}

void StoreTests::legacyBackupsRemainRestorable() {
    QTemporaryDir temp; NoteStore store; QVERIFY(store.open(QDir(temp.path()).filePath(QStringLiteral("library"))));
    const QString note = store.createNote(QStringLiteral("旧备份"));
    const QString backup = QDir(temp.path()).filePath(QStringLiteral("backup")); QVERIFY(store.backupTo(backup));
    const QString manifest = QDir(backup).filePath(QStringLiteral("backup.json"));
    QVERIFY(writeFile(manifest, QJsonDocument(QJsonObject{{QStringLiteral("format"), QStringLiteral("shiye-backup")}, {QStringLiteral("version"), 1}}).toJson()));
    QVERIFY(store.renameNode(note, QStringLiteral("更改"))); QVERIFY(store.restoreFrom(backup));
    QCOMPARE(store.node(note).title, QStringLiteral("旧备份"));
    QVERIFY(QFile::remove(manifest)); QVERIFY(!store.restoreFrom(backup));
}

void StoreTests::directoriesAndRestart() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QString root, child, note;
    {
        NoteStore store;
        QVERIFY2(store.open(temp.path()), qPrintable(store.errorString()));
        root = store.createFolder(QStringLiteral("学习"));
        child = store.createFolder(QStringLiteral("Qt"), root);
        note = store.createNote(QStringLiteral("输入法笔记"), child, QStringLiteral("<p>中文输入与保存</p>"));
        QVERIFY(!root.isEmpty() && !child.isEmpty() && !note.isEmpty());
        QVERIFY(!store.moveNode(root, child));
        QVERIFY(!store.moveNode(root, root));
        QVERIFY(!store.moveNode(child, note));
        QVERIFY(!store.moveNode(note, QStringLiteral("missing-parent")));
        QVERIFY(store.createNote(QStringLiteral("无效父项"), note).isEmpty());
        QVERIFY(store.moveNode(note, root));
        QCOMPARE(store.node(note).parentId, root);
        QVERIFY(store.updateNote(note, QStringLiteral("新的标题"), QStringLiteral("<h2>段落</h2><p>保存成功</p>"), QStringLiteral("段落\n保存成功")));
        QVERIFY(store.setFavorite(note, true));
    }
    NoteStore reopened;
    QVERIFY(reopened.open(temp.path()));
    QCOMPARE(reopened.node(note).title, QStringLiteral("新的标题"));
    QVERIFY(reopened.node(note).html.contains(QStringLiteral("保存成功")));
    QCOMPARE(reopened.node(note).plainText, QStringLiteral("段落\n保存成功"));
    QCOMPARE(reopened.node(note).parentId, root);
    QVERIFY(reopened.node(note).favorite);
    QCOMPARE(reopened.nodes().size(), 3);
}

void StoreTests::chineseAndLiteralSearch() {
    QTemporaryDir temp;
    NoteStore store;
    QVERIFY(store.open(temp.path()));
    const QString id = store.createNote(QStringLiteral("中文检索"), {}, QStringLiteral("<p>正文包含知识花园和100%完成，a_b。</p>"));
    QVERIFY(!id.isEmpty());
    QCOMPARE(store.search(QStringLiteral("中文")).size(), 1);
    QCOMPARE(store.search(QStringLiteral("知识")).size(), 1);
    QCOMPARE(store.search(QStringLiteral("花")).size(), 1);
    QCOMPARE(store.search(QStringLiteral("%")).size(), 1);
    QCOMPARE(store.search(QStringLiteral("_")).size(), 1);
    QVERIFY(!store.createNote(QStringLiteral("其他"), {}, QStringLiteral("<p>普通文本</p>")).isEmpty());
    QCOMPARE(store.search(QStringLiteral("%")).size(), 1);
    QCOMPARE(store.search(QStringLiteral("_")).size(), 1);
    QCOMPARE(store.search(QStringLiteral("不存在")).size(), 0);
    QVERIFY(store.setTrashed(id, true));
    QCOMPARE(store.search(QStringLiteral("中文")).size(), 0);
    QCOMPARE(store.search(QStringLiteral("中文"), true).size(), 1);
}

void StoreTests::recycleBinPreservesIndependentDeletes() {
    QTemporaryDir temp;
    NoteStore store;
    QVERIFY(store.open(temp.path()));
    const QString parent = store.createFolder(QStringLiteral("项目"));
    const QString folder = store.createFolder(QStringLiteral("资料"), parent);
    const QString oldDelete = store.createNote(QStringLiteral("以前删除的"), folder);
    const QString active = store.createNote(QStringLiteral("本次删除的"), folder);
    QVERIFY(store.setTrashed(oldDelete, true));
    QVERIFY(store.setTrashed(parent, true));
    QVERIFY(store.node(parent).trashed && store.node(folder).trashed && store.node(active).trashed);
    QVERIFY(store.createNote(QStringLiteral("不可创建"), folder).isEmpty());
    QVERIFY(!store.updateNote(active, QStringLiteral("不能编辑"), {}, {}));
    QVERIFY(!store.moveNode(active, {}));
    QVERIFY(store.setTrashed(parent, false));
    QVERIFY(!store.node(parent).trashed && !store.node(folder).trashed && !store.node(active).trashed);
    QVERIFY(store.node(oldDelete).trashed);
    QVERIFY(store.setTrashed(parent, true));
    QVERIFY(store.setTrashed(active, false));
    QVERIFY(!store.node(active).trashed);
    QVERIFY(store.node(active).parentId.isEmpty());
    QVERIFY(store.node(parent).trashed);
}

void StoreTests::attachmentCopySurvivesOriginalRemoval() {
    QTemporaryDir temp;
    const QString original = QDir(temp.path()).filePath(QStringLiteral("原始视频.mp4"));
    const QByteArray bytes("test-video-content\0more-bytes", 29);
    QVERIFY(writeFile(original, bytes));
    NoteStore store;
    QVERIFY(store.open(QDir(temp.path()).filePath(QStringLiteral("library"))));
    const QString id = store.createNote(QStringLiteral("媒体笔记"));
    const Attachment media = store.importAttachment(id, original);
    QVERIFY2(media.isValid(), qPrintable(store.errorString()));
    QCOMPARE(media.noteId, id);
    QCOMPARE(media.originalName, QStringLiteral("原始视频.mp4"));
    QCOMPARE(media.size, qint64(bytes.size()));
    QVERIFY(media.relativePath.startsWith(QStringLiteral("attachments/")));
    QVERIFY(QFile::remove(original));
    QFile saved(QDir(store.dataDirectory()).filePath(media.relativePath));
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), bytes);
    QCOMPARE(store.attachments(id).size(), 1);
    QCOMPARE(store.attachment(media.id).relativePath, media.relativePath);
    QVERIFY(!store.importAttachment(id, temp.path()).isValid());
    QVERIFY(store.setTrashed(id, true));
    QVERIFY(!store.importAttachment(id, saved.fileName()).isValid());
}

void StoreTests::backupRestoreAndInvalidBackup() {
    QTemporaryDir temp;
    NoteStore store;
    QVERIFY(store.open(QDir(temp.path()).filePath(QStringLiteral("library"))));
    const QString folder = store.createFolder(QStringLiteral("带附件的文件夹"));
    const QString note = store.createNote(QStringLiteral("备份前"), folder, QStringLiteral("<p>原来的内容</p>"));
    const QString original = QDir(temp.path()).filePath(QStringLiteral("图片.png"));
    QVERIFY(writeFile(original, QByteArray("image-bytes")));
    const Attachment image = store.importAttachment(note, original);
    QVERIFY(image.isValid());
    const QString backup = QDir(temp.path()).filePath(QStringLiteral("backup"));
    QString error;
    QVERIFY2(store.backupTo(backup, &error), qPrintable(error));
    QVERIFY(QFileInfo(QDir(backup).filePath(QStringLiteral("notes.sqlite"))).isFile());
    QVERIFY(QFileInfo(QDir(backup).filePath(image.relativePath)).isFile());
    QVERIFY(!store.backupTo(backup, &error));
    QVERIFY(store.updateNote(note, QStringLiteral("备份后修改"), QStringLiteral("<p>新的内容</p>"), QStringLiteral("新的内容")));
    const QString extra = store.createNote(QStringLiteral("备份后的新笔记"));
    QVERIFY(!extra.isEmpty());
    QVERIFY2(store.restoreFrom(backup, &error), qPrintable(error));
    QCOMPARE(store.node(note).title, QStringLiteral("备份前"));
    QVERIFY(store.node(note).html.contains(QStringLiteral("原来的内容")));
    QVERIFY(store.node(extra).id.isEmpty());
    QVERIFY(QFileInfo(QDir(store.dataDirectory()).filePath(image.relativePath)).isFile());
    const auto snapshots = QDir(QDir(store.dataDirectory()).filePath(QStringLiteral("backups"))).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QVERIFY(!snapshots.isEmpty());
    QVERIFY(QFile::remove(QDir(backup).filePath(image.relativePath)));
    QVERIFY(!store.restoreFrom(backup, &error));
    QCOMPARE(store.node(note).title, QStringLiteral("备份前"));
    QVERIFY(!store.restoreFrom(store.dataDirectory(), &error));
}

void StoreTests::initialLibraryIsSeededOnce() {
    QTemporaryDir temp;
    NoteStore store;
    QVERIFY(store.open(temp.path()));
    QVERIFY2(store.seedIfEmpty(), qPrintable(store.errorString()));
    const int count = store.nodes().size();
    QVERIFY(count >= 8);
    QCOMPARE(store.search(QStringLiteral("建立自己的知识花园")).size(), 1);
    QVERIFY(store.seedIfEmpty());
    QCOMPARE(store.nodes().size(), count);
}

QTEST_MAIN(StoreTests)
#include "store_tests.moc"
