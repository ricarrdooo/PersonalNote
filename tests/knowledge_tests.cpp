#include <QtTest>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextTable>
#include "noteeditor.h"
#include "notestore.h"
#include "markdownimporter.h"
#include "markdownexporter.h"

class KnowledgeTests : public QObject {
    Q_OBJECT
private slots:
    void searchNavigatesAndWraps();
    void replacementPreservesFormattingAndUndo();
    void searchDoesNotChangeCodeOrTheme();
    void replacementRequiresMatchingSelectionAndWritableNote();
    void excessiveMatchesCannotBePartiallyReplaced();
    void markdownImportsStructureAndOwnedMedia();
    void invalidEncodingAndMissingFilesLeaveNoPartialNotes();
    void importTransactionRollsBackFilesAndRows();
    void missingAndExternalImagesRemainVisible();
    void markdownExportRoundTrip();
    void externalLinksRequireExplicitActivation();
};

static bool writeBytes(const QString &path, const QByteArray &bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
static QByteArray readBytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
static QTextTable *firstTable(QTextDocument *document) {
    for (auto block = document->begin(); block.isValid(); block = block.next()) if (auto *table = QTextCursor(block).currentTable()) return table;
    return nullptr;
}

void KnowledgeTests::searchNavigatesAndWraps() {
    NoteEditor editor; editor.setPlainText(QStringLiteral("知识库 知识库\ncat CAT scatter")); editor.moveCursor(QTextCursor::Start);
    editor.setSearch(QStringLiteral("知识库")); QCOMPARE(editor.searchMatchCount(), 2);
    QVERIFY(editor.findSearchMatch()); QCOMPARE(editor.currentSearchMatch(), 1);
    QVERIFY(editor.findSearchMatch()); QCOMPARE(editor.currentSearchMatch(), 2);
    QVERIFY(editor.findSearchMatch()); QCOMPARE(editor.currentSearchMatch(), 1);
    QVERIFY(editor.findSearchMatch(true)); QCOMPARE(editor.currentSearchMatch(), 2);
    editor.setSearch(QStringLiteral("cat")); QCOMPARE(editor.searchMatchCount(), 3);
    editor.setSearch(QStringLiteral("cat"), true, true); QCOMPARE(editor.searchMatchCount(), 1);
    editor.setSearch(QStringLiteral("cat"), false, true); QCOMPARE(editor.searchMatchCount(), 2);
    editor.setSearch(QString()); QCOMPARE(editor.searchMatchCount(), 0); QVERIFY(!editor.findSearchMatch());
}

void KnowledgeTests::replacementPreservesFormattingAndUndo() {
    NoteEditor editor; editor.setNoteHtml(QStringLiteral("<h1>知识库</h1><p><b>知识库</b>和图片</p>"));
    const QString before = editor.toPlainText(); editor.setSearch(QStringLiteral("知识库"));
    QCOMPARE(editor.replaceAllSearchMatches(QStringLiteral("知识库笔记")), 2);
    QCOMPARE(editor.searchMatchCount(), 2); QCOMPARE(editor.document()->begin().blockFormat().headingLevel(), 1);
    const auto second = editor.document()->begin().next(); QVERIFY(second.begin().fragment().charFormat().fontWeight() >= QFont::Bold);
    const QString after = editor.toPlainText(); editor.undo(); QCOMPARE(editor.toPlainText(), before); editor.redo(); QCOMPARE(editor.toPlainText(), after);
    editor.setSearch(QStringLiteral("笔记")); QCOMPARE(editor.replaceAllSearchMatches(QString()), 2); QCOMPARE(editor.toPlainText(), before);
}

void KnowledgeTests::searchDoesNotChangeCodeOrTheme() {
    NoteEditor editor; editor.applyBlock(QStringLiteral("code")); editor.insertPlainText(QStringLiteral("value = 1\nprint(value)")); editor.setCurrentCodeLanguage(QStringLiteral("python"));
    const QString html = editor.noteHtml(); editor.setSearch(QStringLiteral("value")); QCoreApplication::processEvents();
    QCOMPARE(editor.searchMatchCount(), 2); QVERIFY(editor.extraSelections().size() >= 4);
    editor.setDarkMode(true); QCOMPARE(editor.noteHtml(), html);
    editor.setSearch(QString()); QCoreApplication::processEvents(); QVERIFY(editor.extraSelections().size() >= 2);
    editor.setSearch(QStringLiteral("value")); QCOMPARE(editor.replaceAllSearchMatches(QStringLiteral("count")), 2);
    QCOMPARE(editor.currentCodeLanguage(), QStringLiteral("python")); editor.undo(); QCOMPARE(editor.noteHtml(), html);
}

void KnowledgeTests::replacementRequiresMatchingSelectionAndWritableNote() {
    NoteEditor editor; editor.setPlainText(QStringLiteral("one two one")); editor.moveCursor(QTextCursor::Start);
    editor.setSearch(QStringLiteral("one")); QVERIFY(!editor.replaceSearchMatch(QStringLiteral("three"))); QCOMPARE(editor.toPlainText(), QStringLiteral("one two one"));
    QVERIFY(editor.replaceSearchMatch(QStringLiteral("three"))); QCOMPARE(editor.toPlainText(), QStringLiteral("three two one"));
    editor.setReadOnly(true); QVERIFY(editor.findSearchMatch()); QVERIFY(!editor.replaceSearchMatch(QStringLiteral("none"))); QCOMPARE(editor.replaceAllSearchMatches(QStringLiteral("none")), 0);
    QCOMPARE(editor.toPlainText(), QStringLiteral("three two one"));
}

void KnowledgeTests::excessiveMatchesCannotBePartiallyReplaced() {
    NoteEditor editor; const QString text(10001, QLatin1Char('a')); editor.setPlainText(text); editor.setSearch(QStringLiteral("a"));
    QVERIFY(editor.searchLimitReached()); QCOMPARE(editor.replaceAllSearchMatches(QStringLiteral("b")), -1); QCOMPARE(editor.toPlainText(), text);
}

void KnowledgeTests::markdownImportsStructureAndOwnedMedia() {
    QTemporaryDir temp; const QString sourceDir = QDir(temp.path()).filePath(QStringLiteral("source")); QVERIFY(QDir().mkpath(sourceDir));
    QImage image(80, 40, QImage::Format_RGB32); image.fill(Qt::green); QVERIFY(image.save(QDir(sourceDir).filePath(QStringLiteral("图片.png"))));
    QVERIFY(writeBytes(QDir(sourceDir).filePath(QStringLiteral("资料.txt")), "attachment bytes"));
    const QByteArray markdown = QStringLiteral("# 知识标题\n\n**重要**与*斜体*和~~旧文~~。\n\n- 项目\n- [x] 已完成\n\n> 引用内容\n\n```python\nvalue = 1\n\nprint(value)\n```\n\n```cpp\nint main() {}\n```\n\n![示例](图片.png)\n\n[资料](资料.txt)\n\n| 名称 | 值 |\n| --- | --- |\n| 中文 | 42 |\n").toUtf8();
    const QString path = QDir(sourceDir).filePath(QStringLiteral("导入笔记.md")); QVERIFY(writeBytes(path, markdown));
    NoteStore store; QVERIFY(store.open(QDir(temp.path()).filePath(QStringLiteral("library"))));
    const auto result = MarkdownImporter::importFile(store, path); QVERIFY2(!result.noteId.isEmpty(), qPrintable(result.error)); QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join(QLatin1Char('\n'))));
    QCOMPARE(store.attachments(result.noteId).size(), 3);
    QCOMPARE(readBytes(QDir(store.dataDirectory()).filePath(store.attachments(result.noteId).first().relativePath)), markdown);
    NoteEditor editor; editor.setLibraryDirectory(store.dataDirectory()); editor.setNoteHtml(store.node(result.noteId).html);
    QCOMPARE(editor.document()->begin().blockFormat().headingLevel(), 1); QVERIFY(editor.toPlainText().contains(QStringLiteral("☑ 已完成")));
    QVERIFY(firstTable(editor.document())); QCOMPARE(firstTable(editor.document())->rows(), 2);
    QStringList languages, codeText;
    for (auto block = editor.document()->begin(); block.isValid(); block = block.next()) if (NoteEditor::isCodeBlock(block)) { languages << NoteEditor::codeLanguageForBlock(block); codeText << block.text(); }
    QCOMPARE(languages, QStringList({QStringLiteral("python"), QStringLiteral("python"), QStringLiteral("python"), QStringLiteral("cpp")}));
    QCOMPARE(codeText.at(1), QString());
    QVERIFY(QFile::remove(QDir(sourceDir).filePath(QStringLiteral("图片.png")))); QVERIFY(QFile::remove(path));
    for (const auto &attachment : store.attachments(result.noteId)) QVERIFY(QFileInfo::exists(QDir(store.dataDirectory()).filePath(attachment.relativePath)));
    const QString original = store.node(result.noteId).html;
    QVERIFY(store.updateNote(result.noteId, QStringLiteral("误改"), QStringLiteral("<p>修改</p>"), QStringLiteral("修改")));
    QVERIFY(store.restoreRevision(result.noteId, store.revisions(result.noteId).first().id)); QCOMPARE(store.node(result.noteId).html, original);
    const QString backup = QDir(temp.path()).filePath(QStringLiteral("backup")); QVERIFY(store.backupTo(backup)); QVERIFY(store.restoreFrom(backup)); QVERIFY(store.checkIntegrity());
}

void KnowledgeTests::invalidEncodingAndMissingFilesLeaveNoPartialNotes() {
    QTemporaryDir temp; NoteStore store; QVERIFY(store.open(QDir(temp.path()).filePath(QStringLiteral("library"))));
    const QString source = QDir(temp.path()).filePath(QStringLiteral("bad.md")); QVERIFY(writeBytes(source, QByteArray::fromHex("fffe4142")));
    QVERIFY(MarkdownImporter::importFile(store, source).noteId.isEmpty()); QVERIFY(MarkdownImporter::importFile(store, source + QStringLiteral("missing")).noteId.isEmpty()); QCOMPARE(store.nodes().size(), 0);
    QVERIFY(writeBytes(source, QByteArray("\xEF\xBB\xBF") + "# BOM\n")); QVERIFY(!MarkdownImporter::importFile(store, source).noteId.isEmpty());
}

void KnowledgeTests::importTransactionRollsBackFilesAndRows() {
    QTemporaryDir temp; NoteStore store; QVERIFY(store.open(QDir(temp.path()).filePath(QStringLiteral("library"))));
    const QString source = QDir(temp.path()).filePath(QStringLiteral("original.md")); QVERIFY(writeBytes(source, "content"));
    auto render = [](const QList<Attachment> &) { return qMakePair(QStringLiteral("<p>body</p>"), QStringLiteral("body")); };
    QVERIFY(store.importNoteBundle(QStringLiteral("失败"), {}, {source, source + QStringLiteral("missing")}, render).isEmpty());
    QCOMPARE(store.nodes().size(), 0); QCOMPARE(QDir(QDir(store.dataDirectory()).filePath(QStringLiteral("attachments"))).entryList(QDir::Files).size(), 0);
    QVERIFY(store.importNoteBundle(QStringLiteral("转换失败"), {}, {source}, [](const QList<Attachment> &) { return QPair<QString, QString>(); }).isEmpty());
    QCOMPARE(store.nodes().size(), 0); QCOMPARE(QDir(QDir(store.dataDirectory()).filePath(QStringLiteral("attachments"))).entryList(QDir::Files).size(), 0); QVERIFY(store.checkIntegrity());
}

void KnowledgeTests::missingAndExternalImagesRemainVisible() {
    QTemporaryDir temp; NoteStore store; QVERIFY(store.open(QDir(temp.path()).filePath(QStringLiteral("library"))));
    const QString source = QDir(temp.path()).filePath(QStringLiteral("external.md"));
    QVERIFY(writeBytes(source, "![missing](missing.png)\n\n![remote](https://example.com/img.png)\n\n<img src=\"file:///private.png\">\n\n[bad](javascript:alert)"));
    const auto result = MarkdownImporter::importFile(store, source); QVERIFY(!result.noteId.isEmpty()); QVERIFY(result.warnings.size() >= 2);
    QCOMPARE(store.attachments(result.noteId).size(), 1); const auto note = store.node(result.noteId);
    QVERIFY(note.plainText.contains(QStringLiteral("missing.png"))); QVERIFY(note.plainText.contains(QStringLiteral("https://example.com/img.png"))); QVERIFY(note.plainText.contains(QStringLiteral("<img")));
    QVERIFY(!note.html.contains(QStringLiteral("href=\"javascript:"))); QVERIFY(!note.html.contains(QStringLiteral("<img src=\"file:")));
}

void KnowledgeTests::markdownExportRoundTrip() {
    QTemporaryDir temp; NoteStore store; QVERIFY(store.open(QDir(temp.path()).filePath(QStringLiteral("library"))));
    const QString path = QDir(temp.path()).filePath(QStringLiteral("roundtrip.md"));
    QVERIFY(writeBytes(path, "# Heading\n\nLiteral \\*stars\\* and [link](https://example.com/path?q=1&x=2).\n\n````markdown\n```python\nexample\n```\n\n````\n\n| Name | Value |\n| --- | --- |\n| a\\|b | **bold** |\n| code | `x\\|y` |\n\n- [x] done\n"));
    const auto first = MarkdownImporter::importFile(store, path); QVERIFY2(!first.noteId.isEmpty(), qPrintable(first.error));
    NoteEditor editor; editor.setNoteHtml(store.node(first.noteId).html);
    const QString text = editor.toPlainText(); QVERIFY(text.contains(QStringLiteral("x|y"))); const QString exported = MarkdownExporter::fromDocument(editor.document(), QString());
    QVERIFY(exported.contains(QStringLiteral("````markdown"))); QVERIFY(exported.contains(QStringLiteral("| --- | --- |"))); QVERIFY(exported.contains(QStringLiteral("- [x] done")));
    QVERIFY(writeBytes(path, exported.toUtf8())); const auto second = MarkdownImporter::importFile(store, path); QVERIFY2(!second.noteId.isEmpty(), qPrintable(second.error));
    NoteEditor reloaded; reloaded.setNoteHtml(store.node(second.noteId).html);
    QCOMPARE(reloaded.toPlainText(), text); QVERIFY(firstTable(reloaded.document())); QCOMPARE(firstTable(reloaded.document())->columns(), 2);
}

void KnowledgeTests::externalLinksRequireExplicitActivation() {
    NoteEditor editor; editor.resize(500, 240); editor.setNoteHtml(QStringLiteral("<p><a href=\"https://example.com/article\">Read article</a></p>")); editor.show(); QCoreApplication::processEvents();
    QSignalSpy spy(&editor, &NoteEditor::externalLinkActivated);
    QTextCursor cursor(editor.document()); cursor.setPosition(2); const QPoint point = editor.cursorRect(cursor).center();
    QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::NoModifier, point); QCOMPARE(spy.count(), 0);
    QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, point); QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toUrl(), QUrl(QStringLiteral("https://example.com/article")));
    editor.setNoteHtml(QStringLiteral("<p><a href=\"javascript:alert(1)\">Read article</a></p>")); QCoreApplication::processEvents();
    QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, point); QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(KnowledgeTests)
#include "knowledge_tests.moc"
