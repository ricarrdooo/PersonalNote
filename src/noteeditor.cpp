#include "noteeditor.h"
#include "codehighlighter.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QDropEvent>
#include <QFileInfo>
#include <QImageReader>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextImageFormat>
#include <QTextList>
#include <QTextListFormat>
#include <QTextLayout>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace {

constexpr int BlockKindProperty = QTextFormat::UserProperty + 701;
constexpr int CodeLanguageProperty = QTextFormat::UserProperty + 702;
constexpr int CodeBlockIdProperty = QTextFormat::UserProperty + 703;

QString normalizedCodeLanguage(QString language)
{
    language = language.trimmed().toLower();
    if (language.isEmpty() || language == QStringLiteral("plain") || language == QStringLiteral("text") || language == QStringLiteral("plain text")) return QStringLiteral("plaintext");
    const QHash<QString, QString> aliases = {
        {QStringLiteral("c++"), QStringLiteral("cpp")}, {QStringLiteral("cxx"), QStringLiteral("cpp")}, {QStringLiteral("cc"), QStringLiteral("cpp")},
        {QStringLiteral("py"), QStringLiteral("python")}, {QStringLiteral("js"), QStringLiteral("javascript")}, {QStringLiteral("ts"), QStringLiteral("typescript")},
        {QStringLiteral("sh"), QStringLiteral("bash")}, {QStringLiteral("shell"), QStringLiteral("bash")}, {QStringLiteral("c#"), QStringLiteral("csharp")},
        {QStringLiteral("cs"), QStringLiteral("csharp")}, {QStringLiteral("yml"), QStringLiteral("yaml")}, {QStringLiteral("md"), QStringLiteral("markdown")}
    };
    if (aliases.contains(language)) return aliases.value(language);
    return QRegularExpression(QStringLiteral("^[a-z0-9_+#.-]{1,32}$")).match(language).hasMatch() ? language : QStringLiteral("plaintext");
}

QString newCodeBlockId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QTextBlockFormat paragraphBlock()
{
    QTextBlockFormat format;
    format.setLineHeight(165, QTextBlockFormat::ProportionalHeight);
    format.setBottomMargin(12);
    return format;
}

QTextBlockFormat imageBlock(Qt::Alignment alignment = Qt::AlignHCenter)
{
    QTextBlockFormat block;
    // Proportional text leading multiplies the image height and produces a
    // large empty band above it. Image rows use their natural object height.
    block.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
    block.setTopMargin(8);
    block.setBottomMargin(8);
    block.setAlignment(alignment);
    return block;
}

QTextCharFormat paragraphCharacter(const QTextDocument *document)
{
    QTextCharFormat format;
    QFont font = document->defaultFont();
    // Use a single size unit. Qt gives FontPixelSize precedence over
    // FontPointSize, so retaining the former would flatten all heading sizes.
    if (font.pixelSize() > 0) font.setPointSizeF(font.pixelSize() * 72.0 / 96.0);
    format.setFont(font);
    format.setFontWeight(QFont::Normal);
    format.setFontItalic(false);
    format.setFontUnderline(false);
    format.setFontStrikeOut(false);
    format.setFontFixedPitch(false);
    format.setAnchor(false);
    return format;
}

void mergeBlockCharacterFormat(QTextCursor selection, const QTextCharFormat &character, bool clearColors = false)
{
    struct Span { int start; int end; QTextCharFormat format; };
    QList<Span> spans;
    const int start = selection.selectionStart(), end = selection.selectionEnd();
    for (QTextBlock block = selection.document()->findBlock(start); block.isValid() && block.position() < end; block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) continue;
            const int left = qMax(start, fragment.position());
            const int right = qMin(end, fragment.position() + fragment.length());
            if (left >= right) continue;
            QTextCharFormat merged = fragment.charFormat();
            if (clearColors) { merged.clearForeground(); merged.clearBackground(); }
            const bool anchor = merged.isAnchor();
            const QString href = merged.anchorHref();
            merged.merge(character);
            if (character.hasProperty(QTextFormat::FontPointSize)) merged.clearProperty(QTextFormat::FontPixelSize);
            if (anchor) { merged.setAnchor(true); merged.setAnchorHref(href); }
            spans.append({left, right, merged});
        }
    }
    for (const Span &span : spans) {
        QTextCursor cursor(selection.document());
        cursor.setPosition(span.start); cursor.setPosition(span.end, QTextCursor::KeepAnchor);
        cursor.setCharFormat(span.format);
    }
}

QVariant blockedImageResource()
{
    // Qt's built-in image handler attempts a direct file load if a resource is
    // null. A valid transparent placeholder prevents that fallback completely.
    QImage placeholder(1, 1, QImage::Format_ARGB32);
    placeholder.fill(Qt::transparent);
    return placeholder;
}

class LibraryDocument : public QTextDocument
{
public:
    explicit LibraryDocument(QObject *parent) : QTextDocument(parent) {}

    void setLibraryDirectory(const QString &directory)
    {
        m_libraryDirectory = QDir(directory).absolutePath();
        setBaseUrl(QUrl::fromLocalFile(m_libraryDirectory + QLatin1Char('/')));
    }

protected:
    QVariant loadResource(int type, const QUrl &name) override
    {
        if (type != QTextDocument::ImageResource)
            return QVariant();
        if (m_libraryDirectory.isEmpty())
            return blockedImageResource();
        const QUrl resolved = baseUrl().resolved(name);
        if (!resolved.isLocalFile())
            return blockedImageResource();
        const QString imagePath = QDir::fromNativeSeparators(QFileInfo(resolved.toLocalFile()).canonicalFilePath());
        const QString attachmentRoot = QDir::fromNativeSeparators(QFileInfo(QDir(m_libraryDirectory).filePath(QStringLiteral("attachments"))).canonicalFilePath());
        if (imagePath.isEmpty() || attachmentRoot.isEmpty()
            || !imagePath.startsWith(attachmentRoot + QLatin1Char('/'), Qt::CaseInsensitive))
            return blockedImageResource();
        QImageReader reader(imagePath);
        reader.setAutoTransform(true);
        const QSize size = reader.size();
        if (size.width() > 2000 || size.height() > 2000)
            reader.setScaledSize(size.scaled(2000, 2000, Qt::KeepAspectRatio));
        const QImage image = reader.read();
        return image.isNull() ? blockedImageResource() : QVariant(image);
    }

private:
    QString m_libraryDirectory;
};

QString safePastedHtml(QString html)
{
    const auto insensitive = QRegularExpression::CaseInsensitiveOption;
    const auto dotAll = QRegularExpression::DotMatchesEverythingOption;
    html.remove(QRegularExpression(QStringLiteral("<(script|style|iframe|object|embed|svg|math|form)\\b[^>]*>.*?</\\1\\s*>"), insensitive | dotAll));
    html.remove(QRegularExpression(QStringLiteral("<(img|link|meta|base|input|source|video|audio|iframe|object|embed)\\b[^>]*>"), insensitive));
    html.remove(QRegularExpression(QStringLiteral("\\s+on[a-z]+\\s*=\\s*(?:\"[^\"]*\"|'[^']*'|[^\\s>]+)"), insensitive));
    html.remove(QRegularExpression(QStringLiteral("\\s+(src|background|srcdoc)\\s*=\\s*(?:\"[^\"]*\"|'[^']*'|[^\\s>]+)"), insensitive));
    // Pasted links remain readable text. Only links generated by this application
    // can activate attachments; pasted URLs never access local resources.
    html.remove(QRegularExpression(QStringLiteral("</?a\\b[^>]*>"), insensitive));
    html.remove(QRegularExpression(QStringLiteral("url\\s*\\([^)]*\\)"), insensitive));
    // Keep semantic formatting while matching pasted web text to the note.
    const QRegularExpression styles(QStringLiteral("\\s+style\\s*=\\s*([\"'])(.*?)\\1"), insensitive | dotAll);
    auto foundStyles = styles.globalMatch(html);
    QList<QRegularExpressionMatch> attributes;
    while (foundStyles.hasNext()) attributes.append(foundStyles.next());
    for (int index = attributes.size() - 1; index >= 0; --index) {
        const auto match = attributes.at(index);
        QString style = match.captured(2);
        style.remove(QRegularExpression(QStringLiteral("(?:^|;)\\s*(?:background(?:-color)?|color|font-family|font-size|line-height|margin(?:-[a-z]+)?)\\s*:[^;]*(?=;|$)"), insensitive));
        html.replace(match.capturedStart(2), match.capturedLength(2), style);
    }
    auto foundTags = QRegularExpression(QStringLiteral("<[A-Za-z][^>]*>")).globalMatch(html);
    QList<QRegularExpressionMatch> tags;
    while (foundTags.hasNext()) tags.append(foundTags.next());
    for (int index = tags.size() - 1; index >= 0; --index) {
        const auto match = tags.at(index);
        QString tag = match.captured();
        tag.remove(QRegularExpression(QStringLiteral("\\s+(?:bgcolor|color|face|size)\\s*=\\s*(?:\"[^\"]*\"|'[^']*'|[^\\s>]+)"), insensitive));
        html.replace(match.capturedStart(), match.capturedLength(), tag);
    }
    return html;
}

void startStandaloneBlock(QTextCursor &cursor)
{
    if (cursor.hasSelection())
        cursor.removeSelectedText();
    if (!cursor.block().text().isEmpty() || cursor.positionInBlock() != 0)
        cursor.insertBlock();
    QTextBlockFormat block;
    block.setLineHeight(165, QTextBlockFormat::ProportionalHeight);
    block.setBottomMargin(10);
    cursor.setBlockFormat(block);
    QTextCharFormat character;
    cursor.setCharFormat(character);
}

QStringList localFilePaths(const QMimeData *source)
{
    QStringList paths;
    for (const QUrl &url : source->urls()) {
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile())
            paths.append(url.toLocalFile());
    }
    return paths;
}

} // namespace

NoteEditor::NoteEditor(QWidget *parent) : QTextEdit(parent)
{
    setObjectName(QStringLiteral("noteEditor"));
    auto *safeDocument = new LibraryDocument(this);
    setDocument(safeDocument);
    safeDocument->setDocumentMargin(1);
    QFont bodyFont(QStringLiteral("Microsoft YaHei"));
    bodyFont.setPixelSize(16);
    safeDocument->setDefaultFont(bodyFont);
    safeDocument->setDefaultStyleSheet(QStringLiteral(
        "p { margin-top: 0px; margin-bottom: 12px; line-height: 165%; }"
        "h1 { font-size: 30px; margin-top: 26px; margin-bottom: 14px; }"
        "h2 { font-size: 24px; margin-top: 22px; margin-bottom: 12px; }"
        "h3 { font-size: 20px; margin-top: 18px; margin-bottom: 10px; }"
        "blockquote { margin-left: 18px; margin-right: 8px; }"
        "pre { font-family: Consolas; font-size: 14px; white-space: pre-wrap; }"));
    setAcceptRichText(true);
    setAcceptDrops(true);
    setFrameShape(QFrame::NoFrame);
    setTabChangesFocus(false);
    setCursorWidth(2);
    setPlaceholderText(QStringLiteral("写下你的第一个想法，或输入 / 插入内容块…"));
    setLineWrapMode(QTextEdit::WidgetWidth);
    m_codeHighlighter = new CodeHighlighter(document());
    setDarkMode(false);
    connect(this, &QTextEdit::cursorPositionChanged, this, &NoteEditor::notifyBlockType);
    connect(this, &QTextEdit::currentCharFormatChanged, this, [this]() { notifyBlockType(); });
    connect(this, &QTextEdit::textChanged, this, &NoteEditor::notifyBlockType);
    connect(this, &QTextEdit::textChanged, this, &NoteEditor::scheduleCodePresentation);
    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this, [this]() { scheduleCodePresentation(); });
    connect(this, &QTextEdit::textChanged, this, &NoteEditor::refreshSearchMatches);
    connect(this, &QTextEdit::cursorPositionChanged, this, &NoteEditor::searchResultsChanged);
}

void NoteEditor::setLibraryDirectory(const QString &path)
{
    m_libraryDirectory = QDir(path).absolutePath();
    static_cast<LibraryDocument *>(document())->setLibraryDirectory(m_libraryDirectory);
}

void NoteEditor::setNoteHtml(const QString &html)
{
    {
        const QSignalBlocker blocker(this);
        m_pendingSlash = -1;
        m_preediting = false;
        QString content = html;
        const QRegularExpression metadataExpression(QStringLiteral("<!--shiye-code-v1:([A-Za-z0-9+/=]+)-->") );
        const auto metadata = metadataExpression.match(content);
        const QString encoded = metadata.hasMatch() ? metadata.captured(1) : QString();
        content.remove(metadataExpression);
        const bool undoEnabled = isUndoRedoEnabled();
        setUndoRedoEnabled(false);
        setHtml(content);
        normalizeLoadedCodeBlocks(encoded);
        normalizeImageBlocks();
        document()->setBaseUrl(QUrl::fromLocalFile(m_libraryDirectory + QLatin1Char('/')));
        document()->setModified(false);
        QTextCursor cursor(document());
        cursor.movePosition(QTextCursor::Start);
        setTextCursor(cursor);
        verticalScrollBar()->setValue(0);
        setUndoRedoEnabled(undoEnabled);
    }
    m_lastBlockType.clear();
    m_lastCodeLanguage.clear();
    refreshSearchMatches();
    refreshCodePresentation();
    if (m_codeHighlighter) m_codeHighlighter->rehighlight();
    notifyBlockType();
}

QString NoteEditor::noteHtml() const
{
    // Qt HTML drops custom block properties. Keep semantic code metadata
    // alongside the HTML; theme brushes and syntax colors stay in the view.
    QJsonArray metadata;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        if (!isCodeBlock(block)) continue;
        metadata.append(QJsonObject{{QStringLiteral("n"), block.blockNumber()}, {QStringLiteral("id"), codeBlockId(block)}, {QStringLiteral("language"), codeLanguageForBlock(block)}});
    }
    QString html = toHtml();
    if (!metadata.isEmpty()) {
        const QByteArray encoded = QJsonDocument(metadata).toJson(QJsonDocument::Compact).toBase64();
        const QString comment = QStringLiteral("<!--shiye-code-v1:") + QString::fromLatin1(encoded) + QStringLiteral("-->");
        const int bodyEnd = html.lastIndexOf(QStringLiteral("</body>"));
        html.insert(bodyEnd >= 0 ? bodyEnd : html.size(), comment);
    }
    return html;
}

void NoteEditor::insertImage(const QString &relativePath, const QString &originalName)
{
    if (isReadOnly())
        return;
    const QString absolutePath = QDir(m_libraryDirectory).filePath(relativePath);
    QImageReader reader(absolutePath);
    reader.setAutoTransform(true);
    QSize size = reader.size();
    if (reader.transformation().testFlag(QImageIOHandler::TransformationRotate90))
        size.transpose();
    if (!size.isValid()) {
        insertPlainText(QStringLiteral("[图片无法读取：%1]").arg(originalName));
        return;
    }
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    if (cursor.hasSelection()) cursor.removeSelectedText();
    if (cursor.positionInBlock() > 0) cursor.insertBlock();
    if (QTextList *list = cursor.currentList()) list->remove(cursor.block());
    cursor.setBlockFormat(imageBlock());
    cursor.setCharFormat(paragraphCharacter(document()));
    QTextImageFormat format;
    format.setName(QDir::fromNativeSeparators(relativePath));
    format.setToolTip(originalName);
    const int availableWidth = qMax(160, viewport()->width() - 32);
    const QSize displaySize = size.scaled(qMin(640, availableWidth), 640, Qt::KeepAspectRatio);
    const bool shrink = size.width() > displaySize.width() || size.height() > displaySize.height();
    format.setWidth(shrink ? displaySize.width() : size.width());
    format.setHeight(shrink ? displaySize.height() : size.height());
    cursor.insertImage(format);
    cursor.insertBlock(paragraphBlock(), paragraphCharacter(document()));
    cursor.endEditBlock();
    setTextCursor(cursor);
    ensureCursorVisible();
    setFocus();
}

void NoteEditor::normalizeImageBlocks()
{
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        QString text = block.text();
        text.remove(QChar::ObjectReplacementCharacter);
        if (!text.trimmed().isEmpty()) continue;
        bool hasImage = false;
        for (auto it = block.begin(); !it.atEnd(); ++it)
            if (it.fragment().charFormat().isImageFormat()) hasImage = true;
        if (hasImage) {
            const QString precedingCode = codeBlockId(block.previous());
            QTextCursor cursor(block);
            cursor.setBlockFormat(imageBlock(block.blockFormat().alignment()));
            cursor.setBlockCharFormat(paragraphCharacter(document()));
            // Older image insertions could carry the preceding code group's
            // identity across the picture onto empty trailing paragraphs.
            for (QTextBlock following = block.next(); !precedingCode.isEmpty() && following.isValid()
                 && following.text().trimmed().isEmpty() && codeBlockId(following) == precedingCode; following = following.next()) {
                QTextCursor trailing(following);
                trailing.setBlockFormat(paragraphBlock());
                trailing.setBlockCharFormat(paragraphCharacter(document()));
            }
        }
    }
}

QString NoteEditor::selectedImageSource() const
{
    const QTextCursor cursor = textCursor();
    if (cursor.selectionEnd() - cursor.selectionStart() != 1) return QString();
    QTextCursor object(document());
    object.setPosition(cursor.selectionStart());
    object.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    return object.charFormat().isImageFormat() ? object.charFormat().toImageFormat().name() : QString();
}

QSizeF NoteEditor::selectedImageSize() const
{
    if (selectedImageSource().isEmpty()) return QSizeF();
    QTextCursor cursor(document());
    cursor.setPosition(textCursor().selectionStart());
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    const auto image = cursor.charFormat().toImageFormat();
    return QSizeF(image.width(), image.height());
}

void NoteEditor::resizeSelectedImage(int width)
{
    if (isReadOnly() || selectedImageSource().isEmpty()) return;
    QTextCursor cursor(document());
    cursor.setPosition(textCursor().selectionStart());
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    QTextImageFormat image = cursor.charFormat().toImageFormat();
    if (image.width() <= 0 || image.height() <= 0) return;
    const qreal target = qBound(24, width <= 0 ? viewport()->width() - 32 : width, 2000);
    image.setHeight(image.height() * target / image.width());
    image.setWidth(target);
    cursor.beginEditBlock(); cursor.setCharFormat(image); cursor.endEditBlock();
    setTextCursor(cursor);
    ensureCursorVisible();
    viewport()->update();
}

void NoteEditor::alignSelectedImage(Qt::Alignment alignment)
{
    if (isReadOnly() || selectedImageSource().isEmpty()) return;
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    cursor.setBlockFormat(imageBlock(alignment));
    cursor.endEditBlock();
    setTextCursor(cursor);
}

void NoteEditor::removeSelectedImage()
{
    if (isReadOnly() || selectedImageSource().isEmpty()) return;
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    cursor.removeSelectedText();
    if (cursor.block().text().isEmpty()) {
        const QTextBlock next = cursor.block().next();
        if (next.isValid()) {
            const auto blockFormat = next.blockFormat();
            const auto character = next.charFormat();
            cursor.deleteChar();
            cursor.setBlockFormat(blockFormat);
            cursor.setBlockCharFormat(character);
        } else {
            cursor.setBlockFormat(paragraphBlock());
            cursor.setCharFormat(paragraphCharacter(document()));
        }
    }
    cursor.endEditBlock();
    setTextCursor(cursor);
    setFocus();
}

QRect NoteEditor::imageRect(const QTextCursor &selection) const
{
    QTextCursor start = selection;
    start.setPosition(selection.selectionStart());
    QTextCursor end = start;
    end.movePosition(QTextCursor::NextCharacter);
    return cursorRect(start).united(cursorRect(end)).adjusted(0, 0, -cursorWidth(), 0);
}

QTextCursor NoteEditor::imageCursorAt(const QPoint &point) const
{
    const int hit = cursorForPosition(point).position();
    for (const int position : {hit, hit - 1}) {
        if (position < 0 || position >= document()->characterCount() - 1) continue;
        QTextCursor cursor(document());
        cursor.setPosition(position);
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        if (cursor.charFormat().isImageFormat() && imageRect(cursor).contains(point)) return cursor;
    }
    return QTextCursor();
}

void NoteEditor::insertVideo(const QString &attachmentId, const QString &fileName)
{
    insertMediaLink(attachmentId, fileName, true);
}

void NoteEditor::insertAttachment(const QString &attachmentId, const QString &fileName)
{
    insertMediaLink(attachmentId, fileName, false);
}

void NoteEditor::insertMediaLink(const QString &attachmentId, const QString &fileName, bool video)
{
    if (isReadOnly())
        return;
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    startStandaloneBlock(cursor);
    QTextBlockFormat block = cursor.blockFormat();
    block.setBackground(m_darkMode ? QColor(QStringLiteral("#30382b")) : QColor(QStringLiteral("#f1f3eb")));
    block.setLeftMargin(14);
    block.setRightMargin(14);
    block.setTopMargin(9);
    block.setBottomMargin(9);
    cursor.setBlockFormat(block);
    QTextCharFormat link;
    link.setAnchor(true);
    link.setAnchorHref(QStringLiteral("attachment:") + attachmentId);
    link.setFontUnderline(false);
    link.setForeground(m_darkMode ? QColor(QStringLiteral("#b4b0d7")) : QColor(QStringLiteral("#7771b5")));
    link.setFontPointSize(11);
    cursor.insertText((video ? QStringLiteral("▶  视频  ·  ") : QStringLiteral("↗  附件  ·  ")) + fileName, link);
    cursor.insertBlock();
    QTextBlockFormat paragraph;
    paragraph.setLineHeight(165, QTextBlockFormat::ProportionalHeight);
    paragraph.setBottomMargin(10);
    cursor.setBlockFormat(paragraph);
    cursor.setCharFormat(QTextCharFormat());
    cursor.endEditBlock();
    setTextCursor(cursor);
    ensureCursorVisible();
    setFocus();
}

void NoteEditor::removePendingSlash(QTextCursor &cursor)
{
    if (m_pendingSlash < 0)
        return;
    QTextCursor slash(document());
    if (cursor.position() == m_pendingSlash + 1 && m_pendingSlash < document()->characterCount() - 1) {
        slash.setPosition(m_pendingSlash);
        slash.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
        if (slash.selectedText() == QStringLiteral("/")) {
            slash.removeSelectedText();
            cursor = slash;
        }
    }
    m_pendingSlash = -1;
}

void NoteEditor::applyBlock(const QString &type)
{
    if (isReadOnly())
        return;
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    removePendingSlash(cursor);
    formatBlock(cursor, type);
    cursor.endEditBlock();
    setTextCursor(cursor);
    ensureCursorVisible();
    setFocus();
    notifyBlockType();
}

void NoteEditor::formatBlock(QTextCursor &cursor, const QString &type)
{
    const QString previousText = cursor.block().text();
    if (type != QStringLiteral("todo") && (previousText.startsWith(QChar(0x2610)) || previousText.startsWith(QChar(0x2611)))) {
        const int position = cursor.position();
        QTextCursor prefix = cursor;
        prefix.movePosition(QTextCursor::StartOfBlock);
        prefix.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, previousText.length() > 1 && previousText.at(1) == QLatin1Char(' ') ? 2 : 1);
        const int length = prefix.selectedText().length();
        prefix.removeSelectedText();
        cursor.setPosition(qMax(prefix.block().position(), position - length));
    }
    if (type == QStringLiteral("divider")) {
        startStandaloneBlock(cursor);
        cursor.insertHtml(QStringLiteral("<hr /><p></p>"));
        cursor.insertBlock();
        cursor.setBlockFormat(QTextBlockFormat());
        cursor.setCharFormat(QTextCharFormat());
    } else if (type == QStringLiteral("bullet") || type == QStringLiteral("number")) {
        if (QTextList *oldList = cursor.currentList()) oldList->remove(cursor.block());
        cursor.setBlockFormat(paragraphBlock());
        const QTextCharFormat character = paragraphCharacter(document());
        QTextCursor format = cursor;
        if (!format.hasSelection()) format.select(QTextCursor::BlockUnderCursor);
        mergeBlockCharacterFormat(format, character);
        cursor.setPosition(cursor.position());
        cursor.setCharFormat(character);
        QTextListFormat list;
        list.setStyle(type == QStringLiteral("bullet") ? QTextListFormat::ListDisc : QTextListFormat::ListDecimal);
        list.setIndent(1);
        cursor.createList(list);
    } else {
        if (QTextList *list = cursor.currentList())
            list->remove(cursor.block());
        QTextBlockFormat block = paragraphBlock();
        QTextCharFormat character = paragraphCharacter(document());
        block.setProperty(BlockKindProperty, type);
        if (QRegularExpression(QStringLiteral("^h[1-6]$")).match(type).hasMatch()) {
            const int level = type.right(1).toInt();
            block.setHeadingLevel(level);
            block.setTopMargin(level == 1 ? 26 : level == 2 ? 22 : 18);
            character.setFontWeight(QFont::DemiBold);
            character.setFontPointSize(level == 1 ? 22.5 : level == 2 ? 18 : level == 3 ? 15 : level == 4 ? 13.5 : level == 5 ? 12.75 : 12);
        } else if (type == QStringLiteral("quote")) {
            block.setLeftMargin(18);
            block.setRightMargin(10);
            block.setTopMargin(9);
            block.setBottomMargin(16);
            block.setBackground(m_darkMode ? QColor(QStringLiteral("#30382b")) : QColor(QStringLiteral("#f1f3eb")));
        } else if (type == QStringLiteral("code")) {
            const bool existingCode = isCodeBlock(cursor.block());
            block.setProperty(CodeBlockIdProperty, existingCode ? codeBlockId(cursor.block()) : newCodeBlockId());
            block.setProperty(CodeLanguageProperty, existingCode ? codeLanguageForBlock(cursor.block()) : QStringLiteral("plaintext"));
            block.setTopMargin(0);
            block.setBottomMargin(0);
            block.setLeftMargin(14);
            block.setRightMargin(14);
            block.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
            block.setNonBreakableLines(false);
            character.setFontFamily(QStringLiteral("Consolas"));
            character.setFontFixedPitch(true);
            character.setFontPointSize(10.5);
        }
        cursor.setBlockFormat(block);
        const int originalPosition = cursor.position();
        QTextCursor formatCursor = cursor;
        if (!formatCursor.hasSelection())
            formatCursor.select(QTextCursor::BlockUnderCursor);
        mergeBlockCharacterFormat(formatCursor, character, type == QStringLiteral("code"));
        cursor.setPosition(qMin(originalPosition, document()->characterCount() - 1));
        cursor.setCharFormat(character);
        if (type == QStringLiteral("todo")) {
            cursor.movePosition(QTextCursor::StartOfBlock);
            const QString text = cursor.block().text();
            if (!text.startsWith(QChar(0x2610)) && !text.startsWith(QChar(0x2611)))
                cursor.insertText(QString(QChar(0x2610)) + QLatin1Char(' '));
            cursor.movePosition(QTextCursor::EndOfBlock);
        }
    }
}

QString NoteEditor::currentBlockType() const
{
    const QTextBlock block = textCursor().block();
    const QTextBlockFormat format = block.blockFormat();
    if (isCodeBlock(block)) return QStringLiteral("code");
    if (format.headingLevel() > 0) return QStringLiteral("h%1").arg(format.headingLevel());
    if (block.text().startsWith(QChar(0x2610)) || block.text().startsWith(QChar(0x2611))) return QStringLiteral("todo");
    if (block.textList()) return block.textList()->format().style() == QTextListFormat::ListDecimal ? QStringLiteral("number") : QStringLiteral("bullet");
    const QString tagged = format.property(BlockKindProperty).toString();
    if (tagged == QStringLiteral("quote") || format.leftMargin() >= 18) return QStringLiteral("quote");
    return QStringLiteral("paragraph");
}

bool NoteEditor::isCodeBlock(const QTextBlock &block)
{
    if (!block.isValid()) return false;
    const QTextBlockFormat format = block.blockFormat();
    const QString kind = format.property(BlockKindProperty).toString();
    if (kind == QStringLiteral("code") || !format.property(CodeBlockIdProperty).toString().isEmpty()) return true;
    if (kind == QStringLiteral("quote") || format.headingLevel() > 0 || block.textList()) return false;
    auto monospace = [](const QTextCharFormat &character) {
        const QString family = character.fontFamily().toLower();
        return character.fontFixedPitch() || family == QStringLiteral("consolas") || family == QStringLiteral("courier new") || family == QStringLiteral("monospace") || family == QStringLiteral("menlo") || family == QStringLiteral("cascadia code");
    };
    bool fixed = monospace(block.charFormat());
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const auto fragment = it.fragment();
        if (fragment.isValid() && !fragment.text().trimmed().isEmpty()) { fixed = monospace(fragment.charFormat()); break; }
    }
    return fixed && ((format.leftMargin() >= 12 && format.leftMargin() < 18) || format.nonBreakableLines());
}

QString NoteEditor::codeLanguageForBlock(const QTextBlock &block)
{
    return isCodeBlock(block) ? normalizedCodeLanguage(block.blockFormat().property(CodeLanguageProperty).toString()) : QString();
}

QString NoteEditor::codeBlockId(const QTextBlock &block)
{
    if (!isCodeBlock(block)) return QString();
    const QString stored = block.blockFormat().property(CodeBlockIdProperty).toString();
    if (!stored.isEmpty()) return stored;
    QTextBlock first = block;
    while (isCodeBlock(first.previous()) && first.previous().blockFormat().property(CodeBlockIdProperty).toString().isEmpty()) first = first.previous();
    return QStringLiteral("legacy-%1").arg(first.blockNumber());
}

QString NoteEditor::currentCodeLanguage() const { return codeLanguageForBlock(textCursor().block()); }

void NoteEditor::setCurrentCodeLanguage(const QString &language)
{
    if (isReadOnly() || !isCodeBlock(textCursor().block())) return;
    const QString normalized = normalizedCodeLanguage(language);
    const QString group = codeBlockId(textCursor().block());
    QTextBlock first = textCursor().block();
    while (isCodeBlock(first.previous()) && codeBlockId(first.previous()) == group) first = first.previous();
    bool changed = false;
    QTextCursor edit(document()); edit.beginEditBlock();
    for (QTextBlock block = first; isCodeBlock(block) && codeBlockId(block) == group; block = block.next()) {
        if (codeLanguageForBlock(block) == normalized && block.blockFormat().hasProperty(CodeLanguageProperty)) continue;
        QTextCursor cursor(block); QTextBlockFormat format = block.blockFormat();
        format.setProperty(CodeLanguageProperty, normalized); format.setProperty(CodeBlockIdProperty, group); format.setProperty(BlockKindProperty, QStringLiteral("code"));
        cursor.setBlockFormat(format); changed = true;
    }
    edit.endEditBlock();
    if (changed) {
        refreshCodePresentation(); if (m_codeHighlighter) m_codeHighlighter->rehighlight(); notifyBlockType(); emit contentMetadataChanged();
    }
}

void NoteEditor::normalizeLoadedCodeBlocks(const QString &encoded)
{
    QHash<int, QJsonObject> metadata;
    const QJsonDocument parsed = QJsonDocument::fromJson(QByteArray::fromBase64(encoded.toLatin1()));
    if (parsed.isArray()) for (const auto entry : parsed.array()) {
        if (!entry.isObject()) continue;
        const QJsonObject object = entry.toObject(); const int number = object.value(QStringLiteral("n")).toInt(-1);
        if (number >= 0 && number < document()->blockCount()) metadata.insert(number, object);
    }
    QString previousGroup;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        const bool known = metadata.contains(block.blockNumber());
        if (!known && !isCodeBlock(block)) { previousGroup.clear(); continue; }
        const auto info = metadata.value(block.blockNumber());
        QString group = info.value(QStringLiteral("id")).toString();
        if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{1,80}$")).match(group).hasMatch()) group = previousGroup.isEmpty() ? newCodeBlockId() : previousGroup;
        previousGroup = group;
        QTextBlockFormat format = block.blockFormat();
        format.setProperty(BlockKindProperty, QStringLiteral("code")); format.setProperty(CodeBlockIdProperty, group);
        format.setProperty(CodeLanguageProperty, normalizedCodeLanguage(info.value(QStringLiteral("language")).toString()));
        format.clearBackground(); format.setTopMargin(0); format.setBottomMargin(0); format.setLineHeight(100, QTextBlockFormat::ProportionalHeight); format.setLeftMargin(14); format.setRightMargin(14);
        QTextCursor cursor(block); cursor.setBlockFormat(format);
        QTextCharFormat blockCharacter = block.charFormat(); blockCharacter.clearForeground(); blockCharacter.clearBackground(); cursor.setBlockCharFormat(blockCharacter);
        struct Span { int start; int length; QTextCharFormat format; };
        QList<Span> spans;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment(); if (!fragment.isValid()) continue;
            QTextCharFormat clean = fragment.charFormat(); clean.clearForeground(); clean.clearBackground();
            spans.append({fragment.position(), fragment.length(), clean});
        }
        for (const auto &span : spans) { QTextCursor text(document()); text.setPosition(span.start); text.setPosition(span.start + span.length, QTextCursor::KeepAnchor); text.setCharFormat(span.format); }
    }
}

void NoteEditor::scheduleCodePresentation()
{
    // A layout signal can arrive while QTextEdit updates its own selections.
    // Wait for the edit to finish before replacing our presentation selections.
    if (m_codePresentationPending) return;
    m_codePresentationPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_codePresentationPending = false;
        refreshCodePresentation();
    });
}

void NoteEditor::refreshCodePresentation()
{
    if (m_refreshingCodePresentation) return;
    m_refreshingCodePresentation = true;
    document()->documentLayout()->documentSize();
    QList<QTextEdit::ExtraSelection> selections;
    const QColor background(m_darkMode ? QStringLiteral("#263244") : QStringLiteral("#f1f4f8"));
    const QColor foreground(m_darkMode ? QStringLiteral("#dce6f5") : QStringLiteral("#243041"));
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        if (isCodeBlock(block)) {
            const QTextLayout *layout = block.layout();
            const int lines = layout ? qMax(1, layout->lineCount()) : 1;
            for (int line = 0; line < lines; ++line) {
                QTextEdit::ExtraSelection selection;
                selection.cursor = QTextCursor(block);
                // FullWidthSelection requires an empty cursor selection. Add
                // one for every visual line so wrapped code is filled as well.
                if (layout && layout->lineCount() > line) selection.cursor.setPosition(block.position() + layout->lineAt(line).textStart());
                selection.format.setProperty(QTextFormat::FullWidthSelection, true);
                selection.format.setBackground(background);
                selections.append(selection);
            }
        } else {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const auto fragment = it.fragment(); if (!fragment.isValid()) continue;
                const auto format = fragment.charFormat();
                if (format.isImageFormat() || format.isAnchor() || (!format.fontFixedPitch() && format.fontFamily() != QStringLiteral("Consolas"))) continue;
                QTextEdit::ExtraSelection selection; selection.cursor = QTextCursor(document()); selection.cursor.setPosition(fragment.position()); selection.cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
                selection.format.setBackground(background); selection.format.setForeground(foreground); selections.append(selection);
            }
        }
    }
    // Search uses the same presentation list as code backgrounds so neither hides the other.
    for (int i = 0; i < qMin(1000, m_searchMatches.size()); ++i) {
        QTextEdit::ExtraSelection match; match.cursor = m_searchMatches.at(i);
        match.format.setBackground(m_darkMode ? QColor(QStringLiteral("#695625")) : QColor(QStringLiteral("#fff0ad")));
        match.format.setForeground(m_darkMode ? QColor(QStringLiteral("#fff5cc")) : QColor(QStringLiteral("#493a13")));
        selections.append(match);
    }
    setExtraSelections(selections); viewport()->update(); m_refreshingCodePresentation = false;
}

void NoteEditor::setSearch(const QString &text, bool caseSensitive, bool wholeWords)
{
    m_searchText = text; m_searchFlags = {};
    if (caseSensitive) m_searchFlags |= QTextDocument::FindCaseSensitively;
    if (wholeWords) m_searchFlags |= QTextDocument::FindWholeWords;
    refreshSearchMatches();
}

void NoteEditor::refreshSearchMatches()
{
    m_searchMatches.clear();
    m_searchLimitReached = false;
    if (!m_searchText.isEmpty()) {
        QTextCursor cursor(document());
        while (!(cursor = document()->find(m_searchText, cursor, m_searchFlags)).isNull()) {
            if (m_searchMatches.size() == 10000) { m_searchLimitReached = true; break; }
            m_searchMatches.append(cursor);
        }
    }
    scheduleCodePresentation(); emit searchResultsChanged();
}

int NoteEditor::searchMatchCount() const { return m_searchMatches.size(); }
bool NoteEditor::searchLimitReached() const { return m_searchLimitReached; }

int NoteEditor::currentSearchMatch() const
{
    const QTextCursor current = textCursor();
    for (int i = 0; i < m_searchMatches.size(); ++i) {
        const auto &match = m_searchMatches.at(i);
        if (current.selectionStart() == match.selectionStart() && current.selectionEnd() == match.selectionEnd()) return i + 1;
    }
    return 0;
}

bool NoteEditor::findSearchMatch(bool backwards)
{
    if (m_searchMatches.isEmpty()) return false;
    const QTextCursor current = textCursor();
    if (backwards) {
        for (int i = m_searchMatches.size() - 1; i >= 0; --i) {
            if (m_searchMatches.at(i).selectionEnd() <= current.selectionStart()) {
                setTextCursor(m_searchMatches.at(i)); ensureCursorVisible(); return true;
            }
        }
        setTextCursor(m_searchMatches.last());
    } else {
        for (const auto &match : m_searchMatches) {
            if (match.selectionStart() >= current.selectionEnd()) {
                setTextCursor(match); ensureCursorVisible(); return true;
            }
        }
        setTextCursor(m_searchMatches.first());
    }
    ensureCursorVisible(); return true;
}

void NoteEditor::replaceMatch(QTextCursor cursor, const QString &replacement)
{
    QTextCursor first(document()); first.setPosition(cursor.selectionStart()); first.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    cursor.insertText(replacement, first.charFormat());
}

bool NoteEditor::replaceSearchMatch(const QString &replacement)
{
    if (isReadOnly()) return false;
    const int index = currentSearchMatch();
    if (!index) { findSearchMatch(); return false; }
    replaceMatch(m_searchMatches.at(index - 1), replacement);
    refreshSearchMatches(); findSearchMatch(); return true;
}

int NoteEditor::replaceAllSearchMatches(const QString &replacement)
{
    if (m_searchLimitReached) return -1;
    if (isReadOnly() || m_searchMatches.isEmpty()) return 0;
    const auto matches = m_searchMatches;
    QTextCursor edit(document()); edit.beginEditBlock();
    {
        const QSignalBlocker blocker(this);
        for (int i = matches.size() - 1; i >= 0; --i) replaceMatch(matches.at(i), replacement);
        edit.endEditBlock();
    }
    refreshSearchMatches(); emit textChanged(); return matches.size();
}

void NoteEditor::notifyBlockType()
{
    const QString type = currentBlockType();
    if (type != m_lastBlockType) { m_lastBlockType = type; emit blockTypeChanged(type); }
    const QString language = currentCodeLanguage();
    if (language != m_lastCodeLanguage) { m_lastCodeLanguage = language; emit codeLanguageChanged(language); }
}

bool NoteEditor::convertMarkdownBlock(QKeyEvent *event)
{
    const bool space = event->text() == QStringLiteral(" ") && event->key() != Qt::Key_Return;
    const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    QTextCursor cursor = textCursor();
    if ((!space && !enter) || cursor.hasSelection() || !cursor.atBlockEnd()
        || event->modifiers().testFlag(Qt::ControlModifier) || event->modifiers().testFlag(Qt::AltModifier)
        || event->modifiers().testFlag(Qt::MetaModifier) || (enter && event->modifiers().testFlag(Qt::ShiftModifier))
        || currentBlockType() == QStringLiteral("code")) return false;
    const QString prefix = cursor.block().text();
    QString type;
    QString language;
    const auto fence = QRegularExpression(QStringLiteral("^```([A-Za-z0-9_+#.\\-]{0,32})$")).match(prefix);
    bool checked = false;
    if (space && QRegularExpression(QStringLiteral("^#{1,6}$")).match(prefix).hasMatch()) type = QStringLiteral("h%1").arg(prefix.length());
    else if (space && prefix == QStringLiteral(">")) type = QStringLiteral("quote");
    else if (space && (prefix == QStringLiteral("-") || prefix == QStringLiteral("*") || prefix == QStringLiteral("+"))) type = QStringLiteral("bullet");
    else if (space && QRegularExpression(QStringLiteral("^[0-9]+[.)]$")).match(prefix).hasMatch()) type = QStringLiteral("number");
    else if (space && (prefix == QStringLiteral("[]") || prefix == QStringLiteral("[ ]") || prefix.compare(QStringLiteral("[x]"), Qt::CaseInsensitive) == 0)) { type = QStringLiteral("todo"); checked = prefix.compare(QStringLiteral("[x]"), Qt::CaseInsensitive) == 0; }
    else if (fence.hasMatch()) { type = QStringLiteral("code"); language = normalizedCodeLanguage(fence.captured(1)); }
    else if (prefix == QStringLiteral("---") || prefix == QStringLiteral("***")) type = QStringLiteral("divider");
    if (type.isEmpty()) return false;
    // The trigger insertion is separate from the formatting command: one undo
    // restores exactly the Markdown the user typed, without deleting its text.
    if (space) QTextEdit::keyPressEvent(event);
    cursor = textCursor();
    cursor.beginEditBlock();
    cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    formatBlock(cursor, type);
    if (type == QStringLiteral("code")) {
        QTextBlockFormat block = cursor.blockFormat();
        block.setProperty(CodeLanguageProperty, language);
        cursor.setBlockFormat(block);
    }
    if (checked) {
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
        cursor.insertText(QString(QChar(0x2611)));
        cursor.movePosition(QTextCursor::EndOfBlock);
    }
    cursor.endEditBlock();
    setTextCursor(cursor);
    m_pendingSlash = -1;
    notifyBlockType();
    event->accept();
    return true;
}

bool NoteEditor::handleBlockReturn(QKeyEvent *event)
{
    if ((event->key() != Qt::Key_Return && event->key() != Qt::Key_Enter)
        || event->modifiers().testFlag(Qt::ShiftModifier) || event->modifiers().testFlag(Qt::ControlModifier)
        || event->modifiers().testFlag(Qt::AltModifier) || event->modifiers().testFlag(Qt::MetaModifier)) return false;
    const QString type = currentBlockType();
    QTextCursor cursor = textCursor();
    const QString text = cursor.block().text();
    const bool todo = type == QStringLiteral("todo");
    const bool empty = todo ? text.mid(1).trimmed().isEmpty() : text.trimmed().isEmpty();
    if (type == QStringLiteral("paragraph")) return false;
    if (type == QStringLiteral("code")) {
        if (text.trimmed() != QStringLiteral("```")) return false;
        cursor.beginEditBlock();
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        formatBlock(cursor, QStringLiteral("paragraph"));
        cursor.endEditBlock();
        setTextCursor(cursor);
        event->accept();
        return true;
    }
    if (type.startsWith(QLatin1Char('h'))) {
        cursor.beginEditBlock(); cursor.insertBlock(paragraphBlock(), paragraphCharacter(document())); cursor.endEditBlock(); setTextCursor(cursor); event->accept(); return true;
    }
    if ((todo || type == QStringLiteral("bullet") || type == QStringLiteral("number") || type == QStringLiteral("quote")) && empty) {
        cursor.beginEditBlock(); formatBlock(cursor, QStringLiteral("paragraph")); cursor.endEditBlock(); setTextCursor(cursor); event->accept(); return true;
    }
    if (todo) {
        cursor.beginEditBlock(); cursor.insertBlock(paragraphBlock(), paragraphCharacter(document())); formatBlock(cursor, QStringLiteral("todo")); cursor.endEditBlock(); setTextCursor(cursor); event->accept(); return true;
    }
    return false;
}

void NoteEditor::convertInlineMarkdown()
{
    if (currentBlockType() == QStringLiteral("code")) return;
    QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) return;
    const QString beforeCursor = cursor.block().text().left(cursor.positionInBlock());
    struct InlineRule { QString pattern; int kind; int markerLength; };
    const QList<InlineRule> rules = {
        {QStringLiteral("(?<![\\\\*])\\*\\*([^*\\n]+)\\*\\*$"), 0, 2},
        {QStringLiteral("(?<![\\\\~])~~([^~\\n]+)~~$"), 1, 2},
        {QStringLiteral("(?<![\\\\`])`([^`\\n]+)`$"), 2, 1},
        {QStringLiteral("(?<![\\\\*])\\*([^*\\n]+)\\*$"), 3, 1}
    };
    for (const auto &rule : rules) {
        const auto match = QRegularExpression(rule.pattern).match(beforeCursor);
        if (!match.hasMatch()) continue;
        const QTextCharFormat after = cursor.charFormat();
        QTextCursor content(document()); content.setPosition(cursor.block().position() + match.capturedStart(1));
        QTextCharFormat format = content.charFormat();
        if (format.fontFixedPitch() || format.fontFamily() == QStringLiteral("Consolas")) return;
        if (rule.kind == 0) format.setFontWeight(QFont::Bold);
        else if (rule.kind == 1) format.setFontStrikeOut(true);
        else if (rule.kind == 2) { format.setFontFamily(QStringLiteral("Consolas")); format.setFontFixedPitch(true); format.setBackground(QColor(m_darkMode ? "#30382b" : "#f1f3eb")); }
        else format.setFontItalic(true);
        cursor.beginEditBlock();
        cursor.setPosition(cursor.block().position() + match.capturedStart(), QTextCursor::KeepAnchor);
        cursor.insertText(match.captured(1), format);
        cursor.setCharFormat(after);
        cursor.endEditBlock();
        setTextCursor(cursor);
        return;
    }
}

void NoteEditor::setDarkMode(bool enabled)
{
    m_darkMode = enabled;
    QPalette colors = palette();
    colors.setColor(QPalette::Base, enabled ? QColor(QStringLiteral("#22271f")) : QColor(Qt::white));
    colors.setColor(QPalette::Window, colors.color(QPalette::Base));
    colors.setColor(QPalette::Text, enabled ? QColor(QStringLiteral("#d8decf")) : QColor(QStringLiteral("#30392d")));
    colors.setColor(QPalette::PlaceholderText, enabled ? QColor(QStringLiteral("#78846d")) : QColor(QStringLiteral("#a3ae97")));
    colors.setColor(QPalette::Highlight, enabled ? QColor(QStringLiteral("#5d5574")) : QColor(QStringLiteral("#dfdaee")));
    colors.setColor(QPalette::HighlightedText, enabled ? QColor(Qt::white) : QColor(QStringLiteral("#40384f")));
    setPalette(colors);
    refreshCodePresentation();
    if (m_codeHighlighter) m_codeHighlighter->setDarkMode(enabled);
}

bool NoteEditor::canInsertFromMimeData(const QMimeData *source) const
{
    return !isReadOnly() && source && (source->hasImage() || !localFilePaths(source).isEmpty() || QTextEdit::canInsertFromMimeData(source));
}

void NoteEditor::insertFromMimeData(const QMimeData *source)
{
    if (isReadOnly() || !source)
        return;
    const QStringList files = localFilePaths(source);
    if (!files.isEmpty()) {
        emit filesDropped(files);
        return;
    }
    if (source->hasImage()) {
        const QImage image = qvariant_cast<QImage>(source->imageData());
        if (!image.isNull()) {
            emit imagePasted(image);
            return;
        }
    }
    if (source->hasHtml()) {
        if (currentBlockType() == QStringLiteral("code")) textCursor().insertText(source->text());
        else textCursor().insertHtml(safePastedHtml(source->html()));
        return;
    }
    QTextEdit::insertFromMimeData(source);
}

void NoteEditor::dropEvent(QDropEvent *event)
{
    if (isReadOnly()) {
        event->ignore();
        return;
    }
    const QStringList files = localFilePaths(event->mimeData());
    if (!files.isEmpty()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        setTextCursor(cursorForPosition(event->position().toPoint()));
#else
        setTextCursor(cursorForPosition(event->pos()));
#endif
        emit filesDropped(files);
        event->acceptProposedAction();
        return;
    }
    QTextEdit::dropEvent(event);
}

void NoteEditor::toggleTodoAtCursor()
{
    if (isReadOnly())
        return;
    QTextCursor cursor = textCursor();
    const int oldPosition = cursor.position();
    const QString text = cursor.block().text();
    if (!text.startsWith(QChar(0x2610)) && !text.startsWith(QChar(0x2611)))
        return;
    cursor.beginEditBlock();
    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
    cursor.insertText(QString(text.at(0) == QChar(0x2610) ? QChar(0x2611) : QChar(0x2610)));
    cursor.setPosition(qMin(oldPosition, document()->characterCount() - 1));
    cursor.endEditBlock();
    setTextCursor(cursor);
}

void NoteEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Save)) {
        emit saveRequested();
        event->accept();
        return;
    }
    if (isReadOnly()) {
        QTextEdit::keyPressEvent(event);
        return;
    }
    if (m_preediting) {
        QTextEdit::keyPressEvent(event);
        return;
    }
    if (event->key() == Qt::Key_V && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
        insertPlainText(QApplication::clipboard()->text());
        event->accept();
        return;
    }
    if (!selectedImageSource().isEmpty() && (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)) {
        removeSelectedImage();
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && event->modifiers().testFlag(Qt::ControlModifier)) {
        if (currentBlockType() == QStringLiteral("code")) {
            QTextCursor cursor = textCursor();
            const QString group = codeBlockId(cursor.block());
            while (isCodeBlock(cursor.block().next()) && codeBlockId(cursor.block().next()) == group)
                cursor.movePosition(QTextCursor::NextBlock);
            cursor.movePosition(QTextCursor::EndOfBlock);
            if (cursor.block().next().isValid()) cursor.movePosition(QTextCursor::NextBlock);
            else cursor.insertBlock(paragraphBlock(), paragraphCharacter(document()));
            setTextCursor(cursor); ensureCursorVisible(); event->accept(); return;
        }
        toggleTodoAtCursor();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && currentBlockType() == QStringLiteral("code") && textCursor().block().text().isEmpty()) {
        applyBlock(QStringLiteral("paragraph"));
        event->accept();
        return;
    }
    if (convertMarkdownBlock(event) || handleBlockReturn(event)) return;
    if (event->key() == Qt::Key_Backspace && !textCursor().hasSelection()
        && textCursor().positionInBlock() == 0 && currentBlockType() != QStringLiteral("paragraph")) {
        applyBlock(QStringLiteral("paragraph")); event->accept(); return;
    }
    const bool slash = event->text() == QStringLiteral("/") && !event->modifiers().testFlag(Qt::ControlModifier)
        && currentBlockType() != QStringLiteral("code")
        && textCursor().block().text().trimmed().isEmpty();
    const int position = textCursor().position();
    QTextEdit::keyPressEvent(event);
    if (!event->modifiers().testFlag(Qt::ControlModifier) && !event->modifiers().testFlag(Qt::AltModifier)
        && !event->modifiers().testFlag(Qt::MetaModifier)
        && (event->text() == QStringLiteral("*") || event->text() == QStringLiteral("~") || event->text() == QStringLiteral("`")))
        convertInlineMarkdown();
    if (slash) {
        m_pendingSlash = position;
        QTimer::singleShot(0, this, [this]() { emit slashRequested(viewport()->mapToGlobal(cursorRect().bottomLeft())); });
    } else if (event->key() != Qt::Key_Escape) {
        m_pendingSlash = -1;
    }
}

bool NoteEditor::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride && !isReadOnly() && !m_preediting) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape && currentBlockType() == QStringLiteral("code") && textCursor().block().text().isEmpty()) {
            event->accept();
            return true;
        }
    }
    return QTextEdit::event(event);
}

void NoteEditor::inputMethodEvent(QInputMethodEvent *event)
{
    m_preediting = !event->preeditString().isEmpty();
    QTextEdit::inputMethodEvent(event);
}

void NoteEditor::mouseReleaseEvent(QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPoint point = event->position().toPoint();
#else
    const QPoint point = event->pos();
#endif
    const QTextCursor image = imageCursorAt(point);
    if (event->button() == Qt::LeftButton && !image.isNull()
        && (point - m_mousePressPoint).manhattanLength() < QApplication::startDragDistance()) {
        setTextCursor(image);
        viewport()->update();
        event->accept();
        return;
    }
    const QString anchor = anchorAt(point);
    QTextEdit::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton && !textCursor().hasSelection()) {
        if (anchor.startsWith(QStringLiteral("attachment:"))) {
            const QString id = anchor.mid(11);
            if (QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{1,80}$")).match(id).hasMatch())
                emit attachmentActivated(id);
            return;
        }
        const QUrl url(anchor);
        if (event->modifiers().testFlag(Qt::ControlModifier) &&
            (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("mailto"))) {
            emit externalLinkActivated(url); return;
        }
        const QTextCursor clicked = cursorForPosition(point);
        const QString blockText = clicked.block().text();
        if (clicked.positionInBlock() <= 1 && (blockText.startsWith(QChar(0x2610)) || blockText.startsWith(QChar(0x2611)))) {
            setTextCursor(clicked);
            toggleTodoAtCursor();
        }
    }
}

void NoteEditor::mousePressEvent(QMouseEvent *event)
{
    m_mousePressPoint = event->pos();
    QTextEdit::mousePressEvent(event);
}

void NoteEditor::mouseDoubleClickEvent(QMouseEvent *event)
{
    QTextCursor image = imageCursorAt(event->pos());
    // Showing the image toolbar can move a small image after the first click.
    if (image.isNull() && !selectedImageSource().isEmpty()
        && (event->pos() - m_mousePressPoint).manhattanLength() < QApplication::startDragDistance())
        image = textCursor();
    if (event->button() == Qt::LeftButton && !image.isNull()) {
        setTextCursor(image);
        emit imageActivated(selectedImageSource());
        event->accept();
        return;
    }
    QTextEdit::mouseDoubleClickEvent(event);
}

void NoteEditor::contextMenuEvent(QContextMenuEvent *event)
{
    const QTextCursor image = imageCursorAt(event->pos());
    if (!image.isNull()) setTextCursor(image);
    QMenu *menu = createStandardContextMenu(event->pos());
    const QUrl url(anchorAt(event->pos()));
    if (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("mailto")) {
        menu->addSeparator(); menu->addAction(QStringLiteral("打开链接（Ctrl+单击）"), this, [this, url]() { emit externalLinkActivated(url); });
    }
    if (!selectedImageSource().isEmpty()) {
        menu->addSeparator();
        menu->addAction(QStringLiteral("预览图片"), this, [this]() { emit imageActivated(selectedImageSource()); });
        if (!isReadOnly()) {
            menu->addAction(QStringLiteral("图片适应正文宽度"), this, [this]() { resizeSelectedImage(0); });
            menu->addAction(QStringLiteral("图片居中"), this, [this]() { alignSelectedImage(Qt::AlignHCenter); });
            menu->addAction(QStringLiteral("从正文移除图片"), this, &NoteEditor::removeSelectedImage);
        }
    } else if (!isReadOnly()) {
        menu->addSeparator();
        menu->addAction(QStringLiteral("粘贴为纯文本\tCtrl+Shift+V"), this, [this]() { insertPlainText(QApplication::clipboard()->text()); });
    }
    menu->exec(event->globalPos());
    delete menu;
}

void NoteEditor::paintEvent(QPaintEvent *event)
{
    QTextEdit::paintEvent(event);
    if (selectedImageSource().isEmpty()) return;
    QPainter painter(viewport());
    const QRect bounds = imageRect(textCursor());
    const QSizeF size = selectedImageSize();
    const QRectF picture(bounds.topLeft(), size);
    const QImage pixels = document()->resource(QTextDocument::ImageResource, QUrl(selectedImageSource())).value<QImage>();
    if (!pixels.isNull()) {
        // QTextEdit tints selected image objects like selected text. Keep the
        // picture's real colors and use an outline to indicate selection.
        painter.fillRect(bounds, palette().brush(QPalette::Base));
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(picture, pixels);
    }
    painter.setPen(QPen(m_darkMode ? QColor(QStringLiteral("#b4a6e8")) : QColor(QStringLiteral("#7771b5")), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(picture.adjusted(-2, -2, 2, 2));
}
