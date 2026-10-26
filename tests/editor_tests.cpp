#include "noteeditor.h"
#include "mediaplayerdialog.h"

#include <QBuffer>
#include <QAbstractTextDocumentLayout>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMediaContent>
#include <QMediaPlayer>
#include <QLibraryInfo>
#include <QMimeData>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QVideoWidget>
#include <QAbstractVideoSurface>
#include <QVideoFrame>
#include <atomic>

class AccessibleEditor : public NoteEditor
{
public:
    using NoteEditor::insertFromMimeData;
    using NoteEditor::dropEvent;
};

namespace {
class ProbeSurface : public QAbstractVideoSurface
{
public:
    QList<QVideoFrame::PixelFormat> supportedPixelFormats(QAbstractVideoBuffer::HandleType type) const override
    {
        if (type != QAbstractVideoBuffer::NoHandle)
            return {};
        return {QVideoFrame::Format_ARGB32, QVideoFrame::Format_RGB32, QVideoFrame::Format_RGB24,
                QVideoFrame::Format_BGR24, QVideoFrame::Format_YUV420P, QVideoFrame::Format_YV12,
                QVideoFrame::Format_NV12, QVideoFrame::Format_NV21, QVideoFrame::Format_YUYV,
                QVideoFrame::Format_UYVY};
    }
    bool present(const QVideoFrame &frame) override
    {
        if (frame.isValid())
            ++frameCount;
        return true;
    }
    std::atomic<int> frameCount{0};
};

QByteArray fileBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

bool resourceHasPixels(QTextDocument *document, const QUrl &url)
{
    const QVariant resource = document->resource(QTextDocument::ImageResource, url);
    const QImage image = resource.value<QImage>();
    return !image.isNull() && image.size() != QSize(1, 1);
}

QByteArray littleEndian(const QList<quint32> &values)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    for (quint32 value : values)
        stream << value;
    return bytes;
}

QByteArray chunk(const char *tag, const QByteArray &content)
{
    QByteArray result(tag, 4);
    result += littleEndian({quint32(content.size())});
    result += content;
    if (content.size() % 2)
        result.append('\0');
    return result;
}

QByteArray list(const char *type, const QByteArray &content)
{
    return chunk("LIST", QByteArray(type, 4) + content);
}

// Small, genuine Motion-JPEG AVI with visible motion, generated locally rather
// than downloaded. Every compressed frame is an ordinary, decodable JPEG.
bool createMotionFixture(const QString &path)
{
    const int width = 320;
    const int height = 180;
    const int frameCount = 24;
    const int fps = 12;
    QList<QByteArray> frames;
    quint32 largestFrame = 0;
    for (int i = 0; i < frameCount; ++i) {
        QImage frame(width, height, QImage::Format_RGB32);
        frame.fill(QColor(QStringLiteral("#edf0e4")));
        QPainter painter(&frame);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#7771b5")));
        painter.drawEllipse(16 + i * 10, 55, 48, 48);
        painter.setPen(QColor(QStringLiteral("#52604a")));
        painter.setFont(QFont(QStringLiteral("Arial"), 14));
        painter.drawText(QRect(15, 125, 300, 30), Qt::AlignCenter, QStringLiteral("Shiye video test  %1 / %2").arg(i + 1).arg(frameCount));
        painter.end();
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        if (!frame.save(&buffer, "JPEG", 86))
            return false;
        if (QImage::fromData(jpeg, "JPEG").size() != QSize(width, height))
            return false;
        largestFrame = qMax(largestFrame, quint32(jpeg.size()));
        frames.append(jpeg);
    }
    const QByteArray aviHeader = littleEndian({quint32(1000000 / fps), largestFrame * fps, 0, 0x10,
                                               frameCount, 0, 1, largestFrame, width, height, 0, 0, 0, 0});
    QByteArray streamHeader = QByteArray("vidsMJPG", 8);
    streamHeader += littleEndian({0});
    QByteArray priorityAndLanguage;
    QDataStream priority(&priorityAndLanguage, QIODevice::WriteOnly);
    priority.setByteOrder(QDataStream::LittleEndian);
    priority << quint16(0) << quint16(0);
    streamHeader += priorityAndLanguage;
    streamHeader += littleEndian({0, 1, fps, 0, frameCount, largestFrame, 0xffffffff, 0});
    QByteArray rectangle;
    QDataStream rect(&rectangle, QIODevice::WriteOnly);
    rect.setByteOrder(QDataStream::LittleEndian);
    rect << qint16(0) << qint16(0) << qint16(width) << qint16(height);
    streamHeader += rectangle;
    QByteArray bitmap = littleEndian({40, width, height});
    QByteArray pixelFormat;
    QDataStream format(&pixelFormat, QIODevice::WriteOnly);
    format.setByteOrder(QDataStream::LittleEndian);
    format << quint16(1) << quint16(24);
    bitmap += pixelFormat;
    bitmap += QByteArray("MJPG", 4);
    bitmap += littleEndian({width * height * 3, 0, 0, 0, 0});
    const QByteArray headerList = list("hdrl", chunk("avih", aviHeader)
                                      + list("strl", chunk("strh", streamHeader) + chunk("strf", bitmap)));
    QByteArray movie;
    QByteArray index;
    quint32 offset = 4;
    for (const QByteArray &frame : frames) {
        index += QByteArray("00dc", 4) + littleEndian({0x10, offset, quint32(frame.size())});
        const QByteArray frameChunk = chunk("00dc", frame);
        movie += frameChunk;
        offset += quint32(frameChunk.size());
    }
    const QByteArray content = QByteArray("AVI ", 4) + headerList + list("movi", movie) + chunk("idx1", index);
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(chunk("RIFF", content)) > 0;
}
}

class EditorTests : public QObject
{
    Q_OBJECT
private slots:
    void relativeImageSurvivesHtmlReload();
    void imageResourceCannotEscapeLibrary();
    void headingsAndTodoUndo();
    void readOnlyContentCannotBeChangedByCommands();
    void slashMenuPreservesBlock();
    void fileDropAndImagePasteUseImportSignals();
    void pastedHtmlCannotLoadExternalImages();
    void videoLinkIsActivatableAndDoesNotLeakFormatting();
    void actualVideoDurationAndPlayback();
    void imageParagraphHasCompactSpacingAndNormalFollowingText();
    void legacyImageSpacingIsRepairedOnLoad();
    void imageSelectionResizePreviewAndRemoval();
    void pastedWebTextMatchesThemeAndCodeStaysPlain();
};

void EditorTests::relativeImageSurvivesHtmlReload()
{
    QTemporaryDir library;
    QVERIFY(library.isValid());
    QVERIFY(QDir(library.path()).mkdir(QStringLiteral("attachments")));
    const QString imagePath = library.filePath(QStringLiteral("attachments/sample.png"));
    QImage image(80, 60, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(imagePath));
    const QByteArray before = QCryptographicHash::hash(fileBytes(imagePath), QCryptographicHash::Sha256);
    AccessibleEditor editor;
    editor.setLibraryDirectory(library.path());
    editor.insertImage(QStringLiteral("attachments/sample.png"), QStringLiteral("测试图片.png"));
    const QString html = editor.noteHtml();
    QVERIFY2(html.contains(QStringLiteral("src=\"attachments/sample.png\"")), qPrintable(html));
    QVERIFY(!html.contains(library.path()));
    QVERIFY(resourceHasPixels(editor.document(), QUrl(QStringLiteral("attachments/sample.png"))));
    AccessibleEditor reopened;
    reopened.setLibraryDirectory(library.path());
    reopened.setNoteHtml(html);
    QVERIFY(resourceHasPixels(reopened.document(), QUrl(QStringLiteral("attachments/sample.png"))));
    QCOMPARE(reopened.document()->resource(QTextDocument::ImageResource, QUrl(QStringLiteral("attachments/sample.png"))).value<QImage>().pixelColor(2, 2), QColor(Qt::red));
    QCOMPARE(QCryptographicHash::hash(fileBytes(imagePath), QCryptographicHash::Sha256), before);
}

void EditorTests::imageResourceCannotEscapeLibrary()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(QDir(root.path()).mkpath(QStringLiteral("library/attachments")));
    const QString outside = root.filePath(QStringLiteral("private.png"));
    QImage image(30, 30, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(outside));
    AccessibleEditor editor;
    editor.setLibraryDirectory(root.filePath(QStringLiteral("library")));
    editor.setNoteHtml(QStringLiteral("<p>external image</p><img src=\"%1\">").arg(QUrl::fromLocalFile(outside).toString()));
    QVERIFY(!resourceHasPixels(editor.document(), QUrl::fromLocalFile(outside)));
    QVERIFY(!resourceHasPixels(editor.document(), QUrl(QStringLiteral("../private.png"))));
    QVERIFY(!resourceHasPixels(editor.document(), QUrl(QStringLiteral("https://example.invalid/image.png"))));
    QVERIFY(QFileInfo::exists(outside));
}

void EditorTests::headingsAndTodoUndo()
{
    AccessibleEditor editor;
    editor.setPlainText(QStringLiteral("新的理解"));
    editor.applyBlock(QStringLiteral("h2"));
    QCOMPARE(editor.textCursor().blockFormat().headingLevel(), 2);
    editor.applyBlock(QStringLiteral("paragraph"));
    QCOMPARE(editor.textCursor().blockFormat().headingLevel(), 0);
    editor.applyBlock(QStringLiteral("todo"));
    QVERIFY(editor.toPlainText().startsWith(QChar(0x2610)));
    QTest::keyClick(&editor, Qt::Key_Return, Qt::ControlModifier);
    QVERIFY(editor.toPlainText().startsWith(QChar(0x2611)));
    editor.undo();
    QVERIFY(editor.toPlainText().startsWith(QChar(0x2610)));
    AccessibleEditor reopened;
    reopened.setNoteHtml(editor.noteHtml());
    QVERIFY(reopened.toPlainText().startsWith(QChar(0x2610)));
}

void EditorTests::slashMenuPreservesBlock()
{
    AccessibleEditor editor;
    editor.resize(600, 400);
    editor.show();
    editor.setFocus();
    QSignalSpy menu(&editor, &NoteEditor::slashRequested);
    QTest::keyClicks(&editor, "/");
    QTRY_COMPARE(menu.count(), 1);
    editor.applyBlock(QStringLiteral("h2"));
    QVERIFY(!editor.toPlainText().contains(QLatin1Char('/')));
    QCOMPARE(editor.textCursor().blockFormat().headingLevel(), 2);
}

void EditorTests::readOnlyContentCannotBeChangedByCommands()
{
    AccessibleEditor editor;
    editor.setPlainText(QString(QChar(0x2610)) + QStringLiteral(" 已删除笔记"));
    editor.setReadOnly(true);
    const QString original = editor.noteHtml();
    QTest::keyClick(&editor, Qt::Key_Return, Qt::ControlModifier);
    editor.applyBlock(QStringLiteral("h2"));
    editor.insertVideo(QStringLiteral("readonly-video"), QStringLiteral("video.avi"));
    editor.insertAttachment(QStringLiteral("readonly-file"), QStringLiteral("file.txt"));
    editor.insertImage(QStringLiteral("attachments/nonexistent.png"));
    QMimeData clipboard;
    clipboard.setText(QStringLiteral("应当无法粘贴"));
    editor.insertFromMimeData(&clipboard);
    QCOMPARE(editor.noteHtml(), original);
}

void EditorTests::fileDropAndImagePasteUseImportSignals()
{
    QTemporaryDir source;
    const QString path = source.filePath(QStringLiteral("source.png"));
    QImage image(40, 20, QImage::Format_RGB32);
    image.fill(Qt::green);
    QVERIFY(image.save(path));
    AccessibleEditor editor;
    QSignalSpy importedFiles(&editor, &NoteEditor::filesDropped);
    QMimeData data;
    data.setUrls({QUrl::fromLocalFile(path)});
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    editor.dropEvent(&drop);
    QCOMPARE(importedFiles.count(), 1);
    QCOMPARE(importedFiles.at(0).at(0).toStringList(), QStringList({path}));
    QVERIFY(drop.isAccepted());
    QVERIFY(editor.toPlainText().isEmpty());
    QSignalSpy pastedImage(&editor, &NoteEditor::imagePasted);
    QMimeData clipboard;
    clipboard.setImageData(image);
    editor.insertFromMimeData(&clipboard);
    QCOMPARE(pastedImage.count(), 1);
    QCOMPARE(pastedImage.at(0).at(0).value<QImage>().size(), image.size());
    QVERIFY(editor.toPlainText().isEmpty());
    QVERIFY(QFileInfo::exists(path));
}

void EditorTests::pastedHtmlCannotLoadExternalImages()
{
    AccessibleEditor editor;
    QMimeData clipboard;
    clipboard.setHtml(QStringLiteral("<p><strong>保留内容</strong><img src='file:///C:/private.png'><script>alert(1)</script></p>"));
    editor.insertFromMimeData(&clipboard);
    QVERIFY(editor.toPlainText().contains(QStringLiteral("保留内容")));
    QVERIFY(!editor.noteHtml().contains(QStringLiteral("private.png")));
    QVERIFY(!editor.toPlainText().contains(QStringLiteral("alert")));
}

void EditorTests::videoLinkIsActivatableAndDoesNotLeakFormatting()
{
    AccessibleEditor editor;
    editor.resize(700, 350);
    editor.show();
    editor.insertVideo(QStringLiteral("video-test-001"), QStringLiteral("本地视频.avi"));
    QVERIFY(editor.noteHtml().contains(QStringLiteral("attachment:video-test-001")));
    QVERIFY(!editor.textCursor().charFormat().isAnchor());
    const QTextCursor match = editor.document()->find(QStringLiteral("本地视频.avi"));
    QVERIFY(!match.isNull());
    QTextCursor clicked = match;
    clicked.setPosition(match.selectionStart() + 1);
    editor.setTextCursor(clicked);
    const QPoint position = editor.cursorRect(clicked).center();
    QSignalSpy activated(&editor, &NoteEditor::attachmentActivated);
    QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::NoModifier, position);
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.at(0).at(0).toString(), QStringLiteral("video-test-001"));
}

void EditorTests::imageParagraphHasCompactSpacingAndNormalFollowingText()
{
    QTemporaryDir library;
    QVERIFY(QDir(library.path()).mkdir(QStringLiteral("attachments")));
    QImage image(400, 220, QImage::Format_RGB32); image.fill(Qt::red);
    QVERIFY(image.save(library.filePath(QStringLiteral("attachments/photo.png"))));
    AccessibleEditor editor;
    editor.resize(780, 600); editor.show();
    editor.setLibraryDirectory(library.path());
    editor.insertPlainText(QStringLiteral("Code before image"));
    editor.applyBlock(QStringLiteral("code")); editor.moveCursor(QTextCursor::End);
    editor.insertImage(QStringLiteral("attachments/photo.png"));
    QCOMPARE(editor.currentBlockType(), QStringLiteral("paragraph"));
    QVERIFY(!editor.currentCharFormat().fontFixedPitch());
    editor.insertPlainText(QStringLiteral("Normal text after image"));
    QApplication::processEvents();
    QTextBlock picture = editor.document()->findBlockByNumber(1);
    const QRectF bounds = editor.document()->documentLayout()->blockBoundingRect(picture);
    QVERIFY2(bounds.height() <= image.height() + 20, qPrintable(QString::number(bounds.height())));
    QVERIFY(!NoteEditor::isCodeBlock(picture));
    QCOMPARE(editor.document()->blockCount(), 3);
    const QString content = editor.toPlainText();
    editor.setDarkMode(true); editor.setDarkMode(false);
    QCOMPARE(editor.toPlainText(), content);
    QCOMPARE(editor.palette().color(QPalette::Base), editor.palette().color(QPalette::Window));
    AccessibleEditor reopened;
    reopened.setLibraryDirectory(library.path()); reopened.setNoteHtml(editor.noteHtml());
    QCOMPARE(reopened.toPlainText(), content);
    QVERIFY(reopened.document()->documentLayout()->blockBoundingRect(reopened.document()->findBlockByNumber(1)).height() <= image.height() + 20);
}

void EditorTests::legacyImageSpacingIsRepairedOnLoad()
{
    AccessibleEditor editor;
    editor.setNoteHtml(QStringLiteral("<p>Before</p><p style='line-height:165%;margin-bottom:10px'><img src='attachments/missing.png' width='400' height='220'></p><p>After</p>"));
    const QTextBlock image = editor.document()->findBlockByNumber(1);
    const QRectF bounds = editor.document()->documentLayout()->blockBoundingRect(image);
    QVERIFY2(bounds.height() <= 240, qPrintable(QString::number(bounds.height())));
    QVERIFY(editor.toPlainText().startsWith(QStringLiteral("Before")));
    QVERIFY(editor.toPlainText().endsWith(QStringLiteral("After")));
    const QString metadata = QString::fromLatin1(QByteArray("[{\"n\":0,\"id\":\"old-code\",\"language\":\"cpp\"},{\"n\":2,\"id\":\"old-code\",\"language\":\"cpp\"}]").toBase64());
    editor.setNoteHtml(QStringLiteral("<p style='margin-left:14px'><span style='font-family:Consolas'>int value = 1;</span></p><p><img src='attachments/missing.png' width='400' height='220'></p><p><br></p><!--shiye-code-v1:") + metadata + QStringLiteral("-->"));
    QVERIFY(NoteEditor::isCodeBlock(editor.document()->firstBlock()));
    QVERIFY(!NoteEditor::isCodeBlock(editor.document()->lastBlock()));
    QVERIFY(editor.document()->lastBlock().text().trimmed().isEmpty());
}

void EditorTests::imageSelectionResizePreviewAndRemoval()
{
    QTemporaryDir library;
    QVERIFY(QDir(library.path()).mkdir(QStringLiteral("attachments")));
    const QColor marker(QStringLiteral("#e845a8"));
    QImage image(300, 160, QImage::Format_RGB32); image.fill(marker);
    const QString file = library.filePath(QStringLiteral("attachments/photo.png"));
    QVERIFY(image.save(file));
    AccessibleEditor editor;
    editor.resize(780, 600); editor.show();
    editor.setLibraryDirectory(library.path());
    editor.insertImage(QStringLiteral("attachments/photo.png"));
    editor.insertPlainText(QStringLiteral("Keep following paragraph"));
    QApplication::processEvents();
    const QImage rendered = editor.viewport()->grab().toImage();
    QRect pixels;
    for (int y = 0; y < rendered.height(); ++y)
        for (int x = 0; x < rendered.width(); ++x)
            if (rendered.pixelColor(x, y) == marker) pixels = pixels.united(QRect(x, y, 1, 1));
    QVERIFY(!pixels.isEmpty());
    QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::NoModifier, pixels.center());
    QCOMPARE(editor.selectedImageSource(), QStringLiteral("attachments/photo.png"));
    QCOMPARE(editor.viewport()->grab().toImage().pixelColor(pixels.center()), marker);
    editor.setDarkMode(true);
    QCOMPARE(editor.viewport()->grab().toImage().pixelColor(pixels.center()), marker);
    QSignalSpy preview(&editor, &NoteEditor::imageActivated);
    QTest::mouseDClick(editor.viewport(), Qt::LeftButton, Qt::NoModifier, pixels.center());
    QCOMPARE(preview.count(), 1);
    editor.resizeSelectedImage(240);
    QCOMPARE(editor.selectedImageSize(), QSizeF(240, 128));
    editor.undo();
    QTextCursor selected(editor.document()); selected.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor); editor.setTextCursor(selected);
    QCOMPARE(editor.selectedImageSize(), QSizeF(300, 160));
    editor.resizeSelectedImage(240); editor.alignSelectedImage(Qt::AlignLeft);
    AccessibleEditor reopened;
    reopened.setLibraryDirectory(library.path()); reopened.setNoteHtml(editor.noteHtml());
    QTextCursor saved(reopened.document()); saved.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor); reopened.setTextCursor(saved);
    QCOMPARE(reopened.selectedImageSize(), QSizeF(240, 128));
    QVERIFY(reopened.textCursor().blockFormat().alignment().testFlag(Qt::AlignLeft));
    reopened.setReadOnly(true);
    const QString html = reopened.noteHtml();
    reopened.resizeSelectedImage(480); reopened.alignSelectedImage(Qt::AlignHCenter); reopened.removeSelectedImage();
    QCOMPARE(reopened.noteHtml(), html);
    editor.removeSelectedImage();
    QCOMPARE(editor.toPlainText(), QStringLiteral("Keep following paragraph"));
    QVERIFY(QFileInfo::exists(file));
    editor.undo();
    QVERIFY(editor.noteHtml().contains(QStringLiteral("attachments/photo.png")));
}

void EditorTests::pastedWebTextMatchesThemeAndCodeStaysPlain()
{
    AccessibleEditor editor;
    QMimeData pasted;
    pasted.setHtml(QStringLiteral("<p style='background-color:#111111;color:#ffffff;line-height:400%;margin-top:80px'><strong>Important</strong> text</p>"));
    pasted.setText(QStringLiteral("Important text"));
    editor.insertFromMimeData(&pasted);
    QCOMPARE(editor.toPlainText(), QStringLiteral("Important text"));
    QVERIFY(!editor.noteHtml().contains(QStringLiteral("#111111")));
    QVERIFY(!editor.noteHtml().contains(QStringLiteral("#ffffff")));
    QVERIFY(editor.document()->firstBlock().begin().fragment().charFormat().fontWeight() >= QFont::Bold);
    editor.setPlainText(QString());
    QMimeData literal;
    literal.setHtml(QStringLiteral("<p>color: blue; font-size: 12px;</p>"));
    editor.insertFromMimeData(&literal);
    QCOMPARE(editor.toPlainText(), QStringLiteral("color: blue; font-size: 12px;"));
    editor.setPlainText(QString()); editor.applyBlock(QStringLiteral("code"));
    editor.insertFromMimeData(&pasted);
    QCOMPARE(editor.currentBlockType(), QStringLiteral("code"));
    QCOMPARE(editor.toPlainText(), QStringLiteral("Important text"));
    QTest::keyClick(&editor, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(editor.currentBlockType(), QStringLiteral("paragraph"));
    QVERIFY(NoteEditor::isCodeBlock(editor.document()->firstBlock()));
}

void EditorTests::actualVideoDurationAndPlayback()
{
    const QString artifactDirectory = QDir::current().filePath(QStringLiteral("test-artifacts"));
    QVERIFY(QDir().mkpath(artifactDirectory));
    const QString generatedPath = QDir(artifactDirectory).filePath(QStringLiteral("generated-motion.avi"));
    QVERIFY(createMotionFixture(generatedPath));
    QVERIFY(fileBytes(generatedPath).startsWith("RIFF"));
    QString videoPath = QString::fromLocal8Bit(qgetenv("SHIYE_PLAYBACK_FIXTURE"));
    if (videoPath.isEmpty()) {
        const QString officialFixture = QDir(QLibraryInfo::location(QLibraryInfo::PrefixPath)).filePath(
            QStringLiteral("../Src/qtmultimedia/tests/auto/integration/qmediaplayerbackend/testdata/colors.mp4"));
        videoPath = QFileInfo::exists(officialFixture) ? QFileInfo(officialFixture).canonicalFilePath() : generatedPath;
    }
    QVERIFY(QFileInfo::exists(videoPath));
    ProbeSurface output;
    QMediaPlayer player;
    player.setVideoOutput(&output);
    player.setMedia(QMediaContent(QUrl::fromLocalFile(videoPath)));
    player.play();
    QElapsedTimer timeout;
    timeout.start();
    while (timeout.elapsed() < 9000 && player.error() == QMediaPlayer::NoError
           && (player.duration() <= 0 || player.position() < 250))
        QTest::qWait(100);
    const QString details = QStringLiteral("fixture=%1 duration=%2ms position=%3ms frames=%4 error=%5 %6")
                                .arg(videoPath).arg(player.duration()).arg(player.position()).arg(output.frameCount.load()).arg(int(player.error())).arg(player.errorString());
    qInfo().noquote() << details;
    const bool playbackSucceeded = player.duration() > 0 && player.position() >= 250;
    player.stop();
    if (!playbackSucceeded)
        QSKIP(qPrintable(QStringLiteral("Local multimedia backend could not play the genuine video fixture; the application retains its system-player action. ") + details));
    QVERIFY(player.duration() >= 1000);
}

QTEST_MAIN(EditorTests)
#include "editor_tests.moc"
