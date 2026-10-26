#include "noteeditor.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QInputMethodEvent>
#include <QSignalSpy>
#include <QTest>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextListFormat>

namespace {
void focusEditor(NoteEditor &editor)
{
    editor.resize(760, 520);
    editor.show();
    editor.setFocus();
    QApplication::processEvents();
}

void commitText(NoteEditor &editor, const QString &text)
{
    QInputMethodEvent event;
    event.setCommitString(text);
    QApplication::sendEvent(&editor, &event);
}

QTextCharFormat firstCharacterFormat(const QTextBlock &block)
{
    QTextCursor cursor(block);
    cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
    return cursor.charFormat();
}

QTextCharFormat characterFormatAt(QTextDocument *document, int position)
{
    QTextCursor cursor(document);
    cursor.setPosition(position);
    cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
    return cursor.charFormat();
}
}

class MarkdownTests : public QObject
{
    Q_OBJECT
private slots:
    void headingsConvertWhileTyping_data();
    void headingsConvertWhileTyping();
    void headingFontHierarchyAndHtmlRoundTrip();
    void resolvedHeadingFontsHaveVisibleHierarchy();
    void headingEnterStartsBodyAndSoftBreakKeepsHeading();
    void inputMethodCommitPreservesHeading();
    void plainHashesDoNotConvert_data();
    void plainHashesDoNotConvert();
    void headingConversionUndoRedo();
    void listPrefixesConvertAndEmptyItemsExit_data();
    void listPrefixesConvertAndEmptyItemsExit();
    void todoPrefixesConvertAndContinueUnchecked_data();
    void todoPrefixesConvertAndContinueUnchecked();
    void quotePrefixAndEmptyQuoteExit();
    void codeFencePreservesMarkdownLiterals_data();
    void codeFencePreservesMarkdownLiterals();
    void codeSlashDoesNotOpenBlockMenu();
    void dividerPrefixes_data();
    void dividerPrefixes();
    void inlineMarkersBecomeFormatting_data();
    void inlineMarkersBecomeFormatting();
    void inlineFormattingDoesNotLeakToFollowingText_data();
    void inlineFormattingDoesNotLeakToFollowingText();
    void boldClosingMarkerDoesNotPrematurelyBecomeItalic();
    void readonlyCannotTriggerMarkdown();
    void cursorMovementReportsCurrentBlockType();
};

void MarkdownTests::headingsConvertWhileTyping_data()
{
    QTest::addColumn<int>("level");
    for (int level = 1; level <= 6; ++level)
        QTest::newRow(qPrintable(QStringLiteral("h%1").arg(level))) << level;
}

void MarkdownTests::headingsConvertWhileTyping()
{
    QFETCH(int, level);
    NoteEditor editor;
    focusEditor(editor);
    QTest::keyClicks(&editor, QString(level, QLatin1Char('#')) + QStringLiteral(" Heading"));
    QCOMPARE(editor.toPlainText(), QStringLiteral("Heading"));
    QCOMPARE(editor.textCursor().blockFormat().headingLevel(), level);
    QCOMPARE(editor.currentBlockType(), QStringLiteral("h%1").arg(level));
    QCOMPARE(editor.textCursor().positionInBlock(), 7);
    QVERIFY(firstCharacterFormat(editor.document()->firstBlock()).fontWeight() >= QFont::DemiBold);
}

void MarkdownTests::headingFontHierarchyAndHtmlRoundTrip()
{
    NoteEditor editor;
    focusEditor(editor);
    QTest::keyClicks(&editor, "# First");
    QTest::keyClick(&editor, Qt::Key_Return);
    QTest::keyClicks(&editor, "## Second");
    QTest::keyClick(&editor, Qt::Key_Return);
    QTest::keyClicks(&editor, "### Third");
    QTextBlock first = editor.document()->firstBlock();
    QTextBlock second = first.next();
    QTextBlock third = second.next();
    QCOMPARE(first.blockFormat().headingLevel(), 1);
    QCOMPARE(second.blockFormat().headingLevel(), 2);
    QCOMPARE(third.blockFormat().headingLevel(), 3);
    const qreal firstSize = firstCharacterFormat(first).fontPointSize();
    const qreal secondSize = firstCharacterFormat(second).fontPointSize();
    const qreal thirdSize = firstCharacterFormat(third).fontPointSize();
    QVERIFY2(firstSize > secondSize && secondSize > thirdSize,
             qPrintable(QStringLiteral("Heading sizes: %1 / %2 / %3").arg(firstSize).arg(secondSize).arg(thirdSize)));
    NoteEditor reopened;
    reopened.setNoteHtml(editor.noteHtml());
    QCOMPARE(reopened.toPlainText(), editor.toPlainText());
    for (int level = 1; level <= 3; ++level) {
        const QTextBlock block = reopened.document()->findBlockByNumber(level - 1);
        QCOMPARE(block.blockFormat().headingLevel(), level);
        QVERIFY(firstCharacterFormat(block).fontWeight() >= QFont::DemiBold);
    }
    QVERIFY(firstCharacterFormat(reopened.document()->firstBlock()).fontPointSize()
            > firstCharacterFormat(reopened.document()->findBlockByNumber(1)).fontPointSize());
}

void MarkdownTests::headingEnterStartsBodyAndSoftBreakKeepsHeading()
{
    NoteEditor editor;
    focusEditor(editor);
    QTest::keyClicks(&editor, "## Heading");
    const qreal headingSize = editor.currentCharFormat().fontPointSize();
    QTest::keyClick(&editor, Qt::Key_Return, Qt::ShiftModifier);
    QCOMPARE(editor.document()->blockCount(), 1);
    QCOMPARE(editor.currentBlockType(), QStringLiteral("h2"));
    QTest::keyClicks(&editor, "Continued");
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.document()->blockCount(), 2);
    QCOMPARE(editor.textCursor().blockFormat().headingLevel(), 0);
    QCOMPARE(editor.currentBlockType(), QStringLiteral("paragraph"));
    QTest::keyClicks(&editor, "Body");
    QCOMPARE(editor.textCursor().block().text(), QStringLiteral("Body"));
    QVERIFY(editor.currentCharFormat().fontPointSize() < headingSize);
}

void MarkdownTests::resolvedHeadingFontsHaveVisibleHierarchy()
{
    NoteEditor editor;
    focusEditor(editor);
    QTest::keyClicks(&editor, "# First");
    QTest::keyClick(&editor, Qt::Key_Return);
    QTest::keyClicks(&editor, "## Second");
    QTest::keyClick(&editor, Qt::Key_Return);
    QTest::keyClicks(&editor, "### Third");
    QTest::keyClick(&editor, Qt::Key_Return);
    QTest::keyClicks(&editor, "Body");
    QList<qreal> sizes;
    QList<qreal> heights;
    QStringList details;
    for (int index = 0; index < 4; ++index) {
        const QFont font = firstCharacterFormat(editor.document()->findBlockByNumber(index)).font();
        const qreal size = font.pixelSize() > 0 ? font.pixelSize() : font.pointSizeF() * 96.0 / 72.0;
        const qreal height = QFontMetricsF(font).height();
        sizes << size;
        heights << height;
        details << QStringLiteral("%1:px=%2,pt=%3,height=%4").arg(index).arg(font.pixelSize()).arg(font.pointSizeF()).arg(height);
    }
    qInfo().noquote() << details.join(QStringLiteral("; "));
    for (int index = 0; index < 3; ++index)
        QVERIFY2(sizes[index] > sizes[index + 1], qPrintable(details.join(QStringLiteral("; "))));
    if (QGuiApplication::platformName() != QStringLiteral("offscreen")) {
        for (int index = 0; index < 3; ++index)
            QVERIFY2(heights[index] > heights[index + 1], qPrintable(details.join(QStringLiteral("; "))));
    }
}

void MarkdownTests::inputMethodCommitPreservesHeading()
{
    NoteEditor editor;
    focusEditor(editor);
    QTest::keyClicks(&editor, "## ");
    QInputMethodEvent preedit(QStringLiteral("zhongwen"), {});
    QApplication::sendEvent(&editor, &preedit);
    QCOMPARE(editor.toPlainText(), QString());
    QCOMPARE(editor.textCursor().blockFormat().headingLevel(), 2);
    commitText(editor, QStringLiteral("中文标题"));
    QCOMPARE(editor.toPlainText(), QStringLiteral("中文标题"));
    QCOMPARE(editor.currentBlockType(), QStringLiteral("h2"));
    QTest::keyClick(&editor, Qt::Key_Return);
    commitText(editor, QStringLiteral("这是正文。"));
    QCOMPARE(editor.textCursor().block().text(), QStringLiteral("这是正文。"));
    QCOMPARE(editor.currentBlockType(), QStringLiteral("paragraph"));
}

void MarkdownTests::plainHashesDoNotConvert_data()
{
    QTest::addColumn<QString>("text");
    QTest::newRow("middle-of-text") << QStringLiteral("Some # heading");
    QTest::newRow("csharp") << QStringLiteral("C# programming");
    QTest::newRow("no-separating-space") << QStringLiteral("##Heading");
    QTest::newRow("hashtag") << QStringLiteral("#tag");
    QTest::newRow("too-many-hashes") << QStringLiteral("####### text");
}

void MarkdownTests::plainHashesDoNotConvert()
{
    QFETCH(QString, text);
    NoteEditor editor;
    focusEditor(editor);
    QTest::keyClicks(&editor, text);
    QCOMPARE(editor.toPlainText(), text);
    QCOMPARE(editor.textCursor().blockFormat().headingLevel(), 0);
}

void MarkdownTests::headingConversionUndoRedo()
{
    NoteEditor editor;
    focusEditor(editor);
    QTest::keyClicks(&editor, "## ");
    QCOMPARE(editor.currentBlockType(), QStringLiteral("h2"));
    editor.undo();
    QVERIFY2(editor.toPlainText().startsWith(QStringLiteral("##")), qPrintable(editor.toPlainText()));
    QCOMPARE(editor.textCursor().blockFormat().headingLevel(), 0);
    editor.redo();
    QCOMPARE(editor.toPlainText(), QString());
    QCOMPARE(editor.textCursor().blockFormat().headingLevel(), 2);
    QTest::keyClicks(&editor, "Keep this heading");
    editor.undo();
    QCOMPARE(editor.textCursor().blockFormat().headingLevel(), 2);
    editor.redo();
    QCOMPARE(editor.toPlainText(), QStringLiteral("Keep this heading"));
}

void MarkdownTests::listPrefixesConvertAndEmptyItemsExit_data()
{
    QTest::addColumn<QString>("prefix");
    QTest::addColumn<QString>("type");
    QTest::newRow("dash") << QStringLiteral("- ") << QStringLiteral("bullet");
    QTest::newRow("asterisk") << QStringLiteral("* ") << QStringLiteral("bullet");
    QTest::newRow("plus") << QStringLiteral("+ ") << QStringLiteral("bullet");
    QTest::newRow("ordered") << QStringLiteral("1. ") << QStringLiteral("number");
}

void MarkdownTests::listPrefixesConvertAndEmptyItemsExit()
{
    QFETCH(QString, prefix);
    QFETCH(QString, type);
    NoteEditor editor;
    focusEditor(editor);
    QTest::keyClicks(&editor, prefix + QStringLiteral("Item"));
    QCOMPARE(editor.toPlainText(), QStringLiteral("Item"));
    QCOMPARE(editor.currentBlockType(), type);
    QVERIFY(editor.textCursor().currentList());
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.currentBlockType(), type);
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.currentBlockType(), QStringLiteral("paragraph"));
    QVERIFY(!editor.textCursor().currentList());
    QVERIFY(editor.textCursor().block().text().isEmpty());
}

void MarkdownTests::todoPrefixesConvertAndContinueUnchecked_data()
{
    QTest::addColumn<QString>("prefix");
    QTest::addColumn<bool>("checked");
    QTest::newRow("brackets-short") << QStringLiteral("[] ") << false;
    QTest::newRow("brackets") << QStringLiteral("[ ] ") << false;
    QTest::newRow("checked") << QStringLiteral("[x] ") << true;
    QTest::newRow("markdown-list-todo") << QStringLiteral("- [ ] ") << false;
}

void MarkdownTests::todoPrefixesConvertAndContinueUnchecked()
{
    QFETCH(QString, prefix);
    QFETCH(bool, checked);
    NoteEditor editor;
    focusEditor(editor);
    QTest::keyClicks(&editor, prefix + QStringLiteral("Task"));
    QCOMPARE(editor.currentBlockType(), QStringLiteral("todo"));
    QVERIFY(!editor.textCursor().currentList());
    QVERIFY(editor.toPlainText().startsWith(checked ? QChar(0x2611) : QChar(0x2610)));
    QVERIFY(editor.toPlainText().endsWith(QStringLiteral("Task")));
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.currentBlockType(), QStringLiteral("todo"));
    QVERIFY(editor.textCursor().block().text().startsWith(QChar(0x2610)));
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.currentBlockType(), QStringLiteral("paragraph"));
    QVERIFY(editor.textCursor().block().text().isEmpty());
}

void MarkdownTests::quotePrefixAndEmptyQuoteExit()
{
    NoteEditor editor;
    focusEditor(editor);
    QTest::keyClicks(&editor, "> A quotation");
    QCOMPARE(editor.toPlainText(), QStringLiteral("A quotation"));
    QCOMPARE(editor.currentBlockType(), QStringLiteral("quote"));
    QVERIFY(editor.textCursor().blockFormat().leftMargin() > 0);
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.currentBlockType(), QStringLiteral("quote"));
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.currentBlockType(), QStringLiteral("paragraph"));
}

void MarkdownTests::codeFencePreservesMarkdownLiterals_data()
{
    QTest::addColumn<bool>("triggerEnter");
    QTest::newRow("space-trigger") << false;
    QTest::newRow("enter-trigger") << true;
}

void MarkdownTests::codeFencePreservesMarkdownLiterals()
{
    QFETCH(bool, triggerEnter);
    NoteEditor editor;
    focusEditor(editor);
    QTest::keyClicks(&editor, "```");
    QTest::keyClick(&editor, triggerEnter ? Qt::Key_Return : Qt::Key_Space);
    QCOMPARE(editor.currentBlockType(), QStringLiteral("code"));
    QVERIFY(!editor.toPlainText().contains(QStringLiteral("```")));
    QTest::keyClicks(&editor, "# Not a heading **literal**");
    QCOMPARE(editor.toPlainText(), QStringLiteral("# Not a heading **literal**"));
    QCOMPARE(editor.textCursor().blockFormat().headingLevel(), 0);
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.currentBlockType(), QStringLiteral("code"));
    QTest::keyClick(&editor, Qt::Key_Escape);
    QCOMPARE(editor.currentBlockType(), QStringLiteral("paragraph"));
}

void MarkdownTests::dividerPrefixes_data()
{
    QTest::addColumn<QString>("prefix");
    QTest::addColumn<bool>("triggerEnter");
    QTest::newRow("dashes-enter") << QStringLiteral("---") << true;
    QTest::newRow("asterisks-enter") << QStringLiteral("***") << true;
    QTest::newRow("dashes-space") << QStringLiteral("---") << false;
}

void MarkdownTests::codeSlashDoesNotOpenBlockMenu()
{
    NoteEditor editor;
    focusEditor(editor);
    QTest::keyClicks(&editor, "```");
    QTest::keyClick(&editor, Qt::Key_Return);
    QSignalSpy menu(&editor, &NoteEditor::slashRequested);
    QTest::keyClicks(&editor, "/home");
    QApplication::processEvents();
    QCOMPARE(editor.toPlainText(), QStringLiteral("/home"));
    QCOMPARE(editor.currentBlockType(), QStringLiteral("code"));
    QCOMPARE(menu.count(), 0);
}

void MarkdownTests::dividerPrefixes()
{
    QFETCH(QString, prefix);
    QFETCH(bool, triggerEnter);
    NoteEditor editor;
    focusEditor(editor);
    QTest::keyClicks(&editor, prefix);
    QTest::keyClick(&editor, triggerEnter ? Qt::Key_Return : Qt::Key_Space);
    QVERIFY2(editor.noteHtml().contains(QStringLiteral("<hr")), qPrintable(editor.noteHtml()));
    QCOMPARE(editor.currentBlockType(), QStringLiteral("paragraph"));
    QVERIFY(!editor.toPlainText().contains(prefix));
}

void MarkdownTests::inlineMarkersBecomeFormatting_data()
{
    QTest::addColumn<QString>("typed");
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("format");
    QTest::newRow("bold") << QStringLiteral("**bold**") << QStringLiteral("bold") << QStringLiteral("bold");
    QTest::newRow("italic") << QStringLiteral("*italic*") << QStringLiteral("italic") << QStringLiteral("italic");
    QTest::newRow("code") << QStringLiteral("`code`") << QStringLiteral("code") << QStringLiteral("code");
    QTest::newRow("strike") << QStringLiteral("~~strike~~") << QStringLiteral("strike") << QStringLiteral("strike");
}

void MarkdownTests::inlineMarkersBecomeFormatting()
{
    QFETCH(QString, typed);
    QFETCH(QString, text);
    QFETCH(QString, format);
    NoteEditor editor;
    focusEditor(editor);
    QTest::keyClicks(&editor, typed);
    QCOMPARE(editor.toPlainText(), text);
    const QTextCharFormat actual = firstCharacterFormat(editor.document()->firstBlock());
    if (format == QStringLiteral("bold")) QVERIFY(actual.fontWeight() >= QFont::Bold);
    else if (format == QStringLiteral("italic")) QVERIFY(actual.fontItalic());
    else if (format == QStringLiteral("strike")) QVERIFY(actual.fontStrikeOut());
    else QVERIFY(actual.fontFixedPitch() || actual.fontFamily() == QStringLiteral("Consolas"));
    editor.undo();
    QVERIFY2(editor.toPlainText().contains(text), qPrintable(editor.toPlainText()));
    QVERIFY2(editor.toPlainText() != text, qPrintable(editor.toPlainText()));
    editor.redo();
    QCOMPARE(editor.toPlainText(), text);
}

void MarkdownTests::readonlyCannotTriggerMarkdown()
{
    NoteEditor editor;
    focusEditor(editor);
    editor.setPlainText(QStringLiteral("##"));
    QTextCursor cursor = editor.textCursor(); cursor.movePosition(QTextCursor::End); editor.setTextCursor(cursor);
    editor.setReadOnly(true);
    QTest::keyClick(&editor, Qt::Key_Space);
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.toPlainText(), QStringLiteral("##"));
    QCOMPARE(editor.textCursor().blockFormat().headingLevel(), 0);
}

void MarkdownTests::inlineFormattingDoesNotLeakToFollowingText_data()
{
    QTest::addColumn<QString>("typed");
    QTest::newRow("bold") << QStringLiteral("**bold** normal");
    QTest::newRow("italic") << QStringLiteral("*italic* normal");
    QTest::newRow("code") << QStringLiteral("`code` normal");
    QTest::newRow("strike") << QStringLiteral("~~strike~~ normal");
}

void MarkdownTests::inlineFormattingDoesNotLeakToFollowingText()
{
    QFETCH(QString, typed);
    NoteEditor editor;
    focusEditor(editor);
    QTest::keyClicks(&editor, typed);
    const int normal = editor.toPlainText().indexOf(QStringLiteral("normal"));
    QVERIFY(normal >= 0);
    const QTextCharFormat format = characterFormatAt(editor.document(), normal + 1);
    QVERIFY(!format.fontItalic());
    QVERIFY(!format.fontStrikeOut());
    QVERIFY(format.fontWeight() < QFont::Bold);
    QVERIFY(!format.fontFixedPitch());
    QVERIFY(format.fontFamily() != QStringLiteral("Consolas"));
    QCOMPARE(editor.currentBlockType(), QStringLiteral("paragraph"));
}

void MarkdownTests::boldClosingMarkerDoesNotPrematurelyBecomeItalic()
{
    NoteEditor editor;
    focusEditor(editor);
    QTest::keyClicks(&editor, "**bold*");
    QCOMPARE(editor.toPlainText(), QStringLiteral("**bold*"));
    QVERIFY(!firstCharacterFormat(editor.document()->firstBlock()).fontItalic());
    QTest::keyClicks(&editor, "*");
    QCOMPARE(editor.toPlainText(), QStringLiteral("bold"));
    QVERIFY(firstCharacterFormat(editor.document()->firstBlock()).fontWeight() >= QFont::Bold);
}

void MarkdownTests::cursorMovementReportsCurrentBlockType()
{
    NoteEditor editor;
    focusEditor(editor);
    QSignalSpy signal(&editor, &NoteEditor::blockTypeChanged);
    QTest::keyClicks(&editor, "# Heading");
    QTest::keyClick(&editor, Qt::Key_Return);
    QTest::keyClicks(&editor, "Body");
    QCOMPARE(editor.currentBlockType(), QStringLiteral("paragraph"));
    QTest::keyClick(&editor, Qt::Key_Home, Qt::ControlModifier);
    QCOMPARE(editor.currentBlockType(), QStringLiteral("h1"));
    QVERIFY(!signal.isEmpty());
    QCOMPARE(signal.last().first().toString(), QStringLiteral("h1"));
}

QTEST_MAIN(MarkdownTests)
#include "markdown_tests.moc"
