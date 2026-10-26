#include "noteeditor.h"

#include <QApplication>
#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QImage>
#include <QInputMethodEvent>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

namespace {
void focusEditor(NoteEditor &editor)
{
    editor.resize(800, 540);
    editor.show();
    editor.setFocus();
    QApplication::processEvents();
}

void settle(NoteEditor &editor)
{
    editor.document()->documentLayout()->documentSize();
    QApplication::processEvents();
}

void enter(NoteEditor &editor)
{
    QTest::keyClick(&editor, Qt::Key_Return);
}

void commitText(NoteEditor &editor, const QString &text)
{
    QInputMethodEvent event;
    event.setCommitString(text);
    QApplication::sendEvent(&editor, &event);
}

void fence(NoteEditor &editor, const QString &language)
{
    QTest::keyClicks(&editor, QStringLiteral("```") + language);
    enter(editor);
}

void closeCode(NoteEditor &editor)
{
    if (!editor.textCursor().block().text().isEmpty()) enter(editor);
    QTest::keyClicks(&editor, "```");
    enter(editor);
}

void selectBlock(NoteEditor &editor, const QTextBlock &block)
{
    editor.setTextCursor(QTextCursor(block));
    QApplication::processEvents();
}

QColor codeBackground(NoteEditor &editor, const QTextBlock &block)
{
    for (const auto &selection : editor.extraSelections()) {
        if (!selection.format.property(QTextFormat::FullWidthSelection).toBool()) continue;
        const int first = selection.cursor.selectionStart();
        const int last = selection.cursor.selectionEnd();
        const bool sameBlock = selection.cursor.blockNumber() == block.blockNumber();
        const bool coversBlock = first <= block.position() && last >= block.position();
        if ((sameBlock || coversBlock) && selection.format.background().style() != Qt::NoBrush)
            return selection.format.background().color();
    }
    return QColor();
}

QSet<QString> syntaxColors(const QTextBlock &block)
{
    QSet<QString> result;
    if (!block.layout()) return result;
    for (const auto &range : block.layout()->formats())
        if (range.length > 0 && range.format.foreground().style() != Qt::NoBrush)
            result.insert(range.format.foreground().color().name());
    return result;
}

QColor syntaxColorAt(const QTextBlock &block, int position)
{
    if (!block.layout()) return QColor();
    for (const auto &range : block.layout()->formats())
        if (position >= range.start && position < range.start + range.length
            && range.format.foreground().style() != Qt::NoBrush)
            return range.format.foreground().color();
    return QColor();
}

QList<QTextBlock> codeBlocks(const NoteEditor &editor)
{
    QList<QTextBlock> blocks;
    for (QTextBlock block = editor.document()->begin(); block.isValid(); block = block.next())
        if (NoteEditor::isCodeBlock(block)) blocks << block;
    return blocks;
}
}

class CodeTests : public QObject
{
    Q_OBJECT
private slots:
    void namedFencesSetLanguage_data();
    void namedFencesSetLanguage();
    void languageChangesStayInsideOneCodeGroup();
    void languageAndBlankLinesSurviveHtml();
    void themeSwitchChangesViewWithoutChangingContentOrUndo();
    void legacyDarkCodeFollowsLightTheme();
    void highlighterUsesLanguageAndCurrentTheme();
    void codeStateDoesNotLeakIntoBodyOrAnotherGroup();
    void readonlyCodeKeepsLanguageButAcceptsTheme();
    void codeRoundTripPreservesMediaAroundIt();
    void codeBackgroundFillsLinesAndWrapping();
};

void CodeTests::namedFencesSetLanguage_data()
{
    QTest::addColumn<QString>("typed");
    QTest::addColumn<QString>("expected");
    QTest::newRow("python") << QStringLiteral("python") << QStringLiteral("python");
    QTest::newRow("cpp") << QStringLiteral("cpp") << QStringLiteral("cpp");
    QTest::newRow("cpp-alias") << QStringLiteral("c++") << QStringLiteral("cpp");
    QTest::newRow("javascript-alias") << QStringLiteral("js") << QStringLiteral("javascript");
    QTest::newRow("json") << QStringLiteral("json") << QStringLiteral("json");
    QTest::newRow("plain") << QStringLiteral("") << QStringLiteral("plaintext");
}

void CodeTests::namedFencesSetLanguage()
{
    QFETCH(QString, typed);
    QFETCH(QString, expected);
    NoteEditor editor;
    focusEditor(editor);
    fence(editor, typed);
    QCOMPARE(editor.currentBlockType(), QStringLiteral("code"));
    QCOMPARE(editor.currentCodeLanguage(), expected);
    QCOMPARE(NoteEditor::codeLanguageForBlock(editor.textCursor().block()), expected);
    QVERIFY(!NoteEditor::codeBlockId(editor.textCursor().block()).isEmpty());
    QTest::keyClicks(&editor, "# Heading **literal**");
    QCOMPARE(editor.toPlainText(), QStringLiteral("# Heading **literal**"));
    QCOMPARE(editor.currentBlockType(), QStringLiteral("code"));
    enter(editor);
    QCOMPARE(editor.currentCodeLanguage(), expected);
}

void CodeTests::languageChangesStayInsideOneCodeGroup()
{
    NoteEditor editor;
    focusEditor(editor);
    fence(editor, QStringLiteral("cpp"));
    QTest::keyClicks(&editor, "int first = 1;"); enter(editor);
    QTest::keyClicks(&editor, "return first;");
    const QTextBlock first = editor.document()->firstBlock();
    const QTextBlock second = first.next();
    const QString firstGroup = NoteEditor::codeBlockId(first);
    QCOMPARE(NoteEditor::codeBlockId(second), firstGroup);
    closeCode(editor);
    QTest::keyClicks(&editor, "Between code blocks"); enter(editor);
    fence(editor, QStringLiteral("python"));
    QTest::keyClicks(&editor, "print('second')");
    const QTextBlock other = editor.textCursor().block();
    QVERIFY(NoteEditor::codeBlockId(other) != firstGroup);
    QSignalSpy changes(&editor, &NoteEditor::contentMetadataChanged);
    selectBlock(editor, second);
    editor.setCurrentCodeLanguage(QStringLiteral("json"));
    QCOMPARE(NoteEditor::codeLanguageForBlock(first), QStringLiteral("json"));
    QCOMPARE(NoteEditor::codeLanguageForBlock(second), QStringLiteral("json"));
    QCOMPARE(NoteEditor::codeLanguageForBlock(other), QStringLiteral("python"));
    QVERIFY(changes.count() >= 1);
}

void CodeTests::languageAndBlankLinesSurviveHtml()
{
    NoteEditor editor;
    focusEditor(editor);
    QTest::keyClicks(&editor, "Before"); enter(editor);
    fence(editor, QStringLiteral("python"));
    QTest::keyClicks(&editor, "def greet():"); enter(editor); enter(editor);
    QTest::keyClicks(&editor, "    return '<hello> & world'"); enter(editor);
    QTest::keyClicks(&editor, "# "); commitText(editor, QStringLiteral("中文注释"));
    closeCode(editor);
    QTest::keyClicks(&editor, "After");
    const QString plain = editor.toPlainText();
    const auto before = codeBlocks(editor);
    QVERIFY(before.size() >= 4);
    NoteEditor reopened;
    reopened.setNoteHtml(editor.noteHtml());
    QCOMPARE(reopened.toPlainText(), plain);
    const auto after = codeBlocks(reopened);
    QCOMPARE(after.size(), before.size());
    for (int index = 0; index < before.size(); ++index) {
        QCOMPARE(after[index].text(), before[index].text());
        QCOMPARE(NoteEditor::codeLanguageForBlock(after[index]), QStringLiteral("python"));
        QCOMPARE(NoteEditor::codeBlockId(after[index]), NoteEditor::codeBlockId(before[index]));
    }
    QVERIFY(!NoteEditor::isCodeBlock(reopened.document()->firstBlock()));
    QVERIFY(!NoteEditor::isCodeBlock(reopened.document()->lastBlock()));
}

void CodeTests::themeSwitchChangesViewWithoutChangingContentOrUndo()
{
    NoteEditor editor;
    focusEditor(editor);
    fence(editor, QStringLiteral("cpp"));
    QTest::keyClicks(&editor, "int value = 42;");
    const QString beforeTyping = editor.toPlainText();
    QTextCursor cursor = editor.textCursor();
    cursor.beginEditBlock(); cursor.insertText(QStringLiteral(" // edit")); cursor.endEditBlock();
    editor.setTextCursor(cursor);
    const QString content = editor.toPlainText();
    const QString html = editor.noteHtml();
    QVERIFY(editor.document()->isUndoAvailable());
    editor.setDarkMode(false); settle(editor);
    const QColor light = codeBackground(editor, editor.document()->firstBlock());
    QVERIFY(light.isValid());
    QVERIFY(light != editor.palette().color(QPalette::Base));
    editor.setDarkMode(true); settle(editor);
    const QColor dark = codeBackground(editor, editor.document()->firstBlock());
    QVERIFY(dark.isValid());
    QVERIFY(dark != light);
    QVERIFY(dark != editor.palette().color(QPalette::Base));
    editor.setDarkMode(false); settle(editor);
    QCOMPARE(codeBackground(editor, editor.document()->firstBlock()), light);
    QCOMPARE(editor.toPlainText(), content);
    QCOMPARE(editor.noteHtml(), html);
    QVERIFY(editor.document()->isUndoAvailable());
    editor.undo();
    QCOMPARE(editor.toPlainText(), beforeTyping);
}

void CodeTests::legacyDarkCodeFollowsLightTheme()
{
    NoteEditor editor;
    focusEditor(editor);
    editor.setNoteHtml(QStringLiteral("<p style='margin-left:14px;margin-right:14px;background-color:#2b3028'>"
                                     "<span style='font-family:Consolas;font-size:10.5pt;color:#eeeeee;background-color:#2b3028'>int legacy = 3;</span></p><p>Body</p>"));
    const QTextBlock code = editor.document()->firstBlock();
    QVERIFY(NoteEditor::isCodeBlock(code));
    const QString plain = editor.toPlainText();
    editor.setDarkMode(true); settle(editor);
    const QColor dark = codeBackground(editor, code);
    editor.setDarkMode(false); settle(editor);
    const QColor light = codeBackground(editor, code);
    QVERIFY(light.isValid());
    QVERIFY(dark.isValid());
    QVERIFY(light != dark);
    QVERIFY(light.lightness() > dark.lightness());
    QCOMPARE(editor.toPlainText(), plain);
    QCOMPARE(code.blockFormat().background().style(), Qt::NoBrush);
    const QString canonical = editor.noteHtml();
    editor.setDarkMode(true); settle(editor);
    QCOMPARE(editor.noteHtml(), canonical);
    NoteEditor reopened;
    reopened.setNoteHtml(canonical);
    reopened.setDarkMode(false); settle(reopened);
    QCOMPARE(codeBackground(reopened, reopened.document()->firstBlock()), light);
}

void CodeTests::highlighterUsesLanguageAndCurrentTheme()
{
    NoteEditor editor;
    focusEditor(editor);
    fence(editor, QStringLiteral("cpp"));
    QTest::keyClicks(&editor, "int value = 42; // comment");
    settle(editor);
    const QTextBlock code = editor.document()->firstBlock();
    const auto lightColors = syntaxColors(code);
    QVERIFY2(lightColors.size() >= 3, qPrintable(QStringLiteral("Only %1 distinct colors").arg(lightColors.size())));
    const QColor lightKeyword = syntaxColorAt(code, 1);
    QVERIFY(lightKeyword.isValid());
    const QString html = editor.noteHtml();
    editor.setDarkMode(true); settle(editor);
    const QColor darkKeyword = syntaxColorAt(code, 1);
    QVERIFY(darkKeyword.isValid());
    QVERIFY(darkKeyword != lightKeyword);
    QCOMPARE(editor.noteHtml(), html);
    editor.setCurrentCodeLanguage(QStringLiteral("plaintext")); settle(editor);
    QVERIFY(syntaxColors(code).size() <= 1);
}

void CodeTests::codeStateDoesNotLeakIntoBodyOrAnotherGroup()
{
    NoteEditor editor;
    focusEditor(editor);
    fence(editor, QStringLiteral("cpp"));
    QTest::keyClicks(&editor, "/* unclosed comment");
    closeCode(editor);
    QTest::keyClicks(&editor, "Ordinary paragraph");
    const QTextBlock body = editor.textCursor().block();
    enter(editor);
    fence(editor, QStringLiteral("cpp"));
    QTest::keyClicks(&editor, "int fresh = 42;");
    settle(editor);
    QVERIFY(!NoteEditor::isCodeBlock(body));
    QVERIFY(syntaxColors(body).isEmpty());
    QVERIFY(syntaxColors(editor.textCursor().block()).size() >= 2);
    QVERIFY(!codeBackground(editor, body).isValid());
}

void CodeTests::readonlyCodeKeepsLanguageButAcceptsTheme()
{
    NoteEditor editor;
    focusEditor(editor);
    fence(editor, QStringLiteral("python"));
    QTest::keyClicks(&editor, "print('hello')");
    editor.setReadOnly(true);
    const QString html = editor.noteHtml();
    editor.setCurrentCodeLanguage(QStringLiteral("cpp"));
    QCOMPARE(editor.currentCodeLanguage(), QStringLiteral("python"));
    QCOMPARE(editor.noteHtml(), html);
    editor.setDarkMode(true); settle(editor);
    const QColor dark = codeBackground(editor, editor.document()->firstBlock());
    editor.setDarkMode(false); settle(editor);
    QVERIFY(dark != codeBackground(editor, editor.document()->firstBlock()));
    QCOMPARE(editor.noteHtml(), html);
}

void CodeTests::codeRoundTripPreservesMediaAroundIt()
{
    QTemporaryDir library;
    QVERIFY(library.isValid());
    QVERIFY(QDir(library.path()).mkdir(QStringLiteral("attachments")));
    QImage image(64, 40, QImage::Format_RGB32); image.fill(Qt::green);
    QVERIFY(image.save(library.filePath(QStringLiteral("attachments/sample.png"))));
    NoteEditor editor;
    focusEditor(editor);
    editor.setLibraryDirectory(library.path());
    editor.insertImage(QStringLiteral("attachments/sample.png"), QStringLiteral("sample.png"));
    fence(editor, QStringLiteral("html"));
    QTest::keyClicks(&editor, "<section>&amp;\"quoted\"</section>");
    closeCode(editor);
    editor.insertVideo(QStringLiteral("saved-video"), QStringLiteral("video.mp4"));
    const QString html = editor.noteHtml();
    const QString plain = editor.toPlainText();
    NoteEditor reopened;
    reopened.setLibraryDirectory(library.path());
    reopened.setNoteHtml(html);
    QCOMPARE(reopened.toPlainText(), plain);
    QVERIFY(reopened.noteHtml().contains(QStringLiteral("attachments/sample.png")));
    QVERIFY(reopened.noteHtml().contains(QStringLiteral("attachment:saved-video")));
    const auto blocks = codeBlocks(reopened);
    QVERIFY(!blocks.isEmpty());
    for (const auto &block : blocks) QCOMPARE(NoteEditor::codeLanguageForBlock(block), QStringLiteral("html"));
    const QImage restored = reopened.document()->resource(QTextDocument::ImageResource, QUrl(QStringLiteral("attachments/sample.png"))).value<QImage>();
    QCOMPARE(restored.size(), image.size());
}

void CodeTests::codeBackgroundFillsLinesAndWrapping()
{
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) QSKIP("Requires native font rendering");
    NoteEditor editor;
    focusEditor(editor);
    editor.resize(430, 500);
    fence(editor, QStringLiteral("python"));
    QTest::keyClicks(&editor, "# Comment"); enter(editor);
    QTest::keyClicks(&editor, "print(\"A long line that wraps in the editor and stays inside the code background\")"); enter(editor); enter(editor);
    QTest::keyClicks(&editor, "return 42");
    settle(editor);
    QVERIFY(editor.document()->findBlockByNumber(1).layout()->lineCount() > 1);
    for (const bool dark : {false, true}) {
        editor.setDarkMode(dark); settle(editor);
        const QImage rendered = editor.viewport()->grab().toImage();
        const QColor background = codeBackground(editor, editor.document()->firstBlock());
        QTextCursor first(editor.document()->firstBlock());
        const int top = editor.cursorRect(first).top();
        QTextCursor last = editor.textCursor();
        const int bottom = editor.cursorRect(last).bottom();
        const int x = editor.viewport()->width() - 24;
        for (int y = top; y < bottom; ++y) {
            const QColor pixel = rendered.pixelColor(x, y);
            QVERIFY2(pixel == background, qPrintable(QStringLiteral("Background gap at y=%1: %2, expected %3").arg(y).arg(pixel.name(), background.name())));
        }
    }
}

QTEST_MAIN(CodeTests)
#include "code_tests.moc"
