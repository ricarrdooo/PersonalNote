#ifndef NOTEEDITOR_H
#define NOTEEDITOR_H

#include <QTextEdit>
#include <QImage>
#include <QStringList>
#include <QTextBlock>
#include <QUrl>

class CodeHighlighter;

class NoteEditor : public QTextEdit
{
    Q_OBJECT
public:
    explicit NoteEditor(QWidget *parent = nullptr);
    void setLibraryDirectory(const QString &path);
    void setNoteHtml(const QString &html);
    QString noteHtml() const;
    void insertImage(const QString &relativePath, const QString &originalName = QString());
    void insertVideo(const QString &attachmentId, const QString &fileName);
    void insertAttachment(const QString &attachmentId, const QString &fileName);
    void applyBlock(const QString &type);
    QString currentBlockType() const;
    QString currentCodeLanguage() const;
    void setCurrentCodeLanguage(const QString &language);
    static bool isCodeBlock(const QTextBlock &block);
    static QString codeLanguageForBlock(const QTextBlock &block);
    static QString codeBlockId(const QTextBlock &block);
    void setDarkMode(bool enabled);
    QString selectedImageSource() const;
    QSizeF selectedImageSize() const;
    void resizeSelectedImage(int width);
    void alignSelectedImage(Qt::Alignment alignment);
    void removeSelectedImage();
    void setSearch(const QString &text, bool caseSensitive = false, bool wholeWords = false);
    int searchMatchCount() const;
    bool searchLimitReached() const;
    int currentSearchMatch() const;
    bool findSearchMatch(bool backwards = false);
    bool replaceSearchMatch(const QString &replacement);
    int replaceAllSearchMatches(const QString &replacement);

signals:
    void filesDropped(const QStringList &paths);
    void imagePasted(const QImage &image);
    void attachmentActivated(const QString &id);
    void saveRequested();
    void slashRequested(const QPoint &globalPosition);
    void blockTypeChanged(const QString &type);
    void codeLanguageChanged(const QString &language);
    void contentMetadataChanged();
    void imageActivated(const QString &relativePath);
    void searchResultsChanged();
    void externalLinkActivated(const QUrl &url);

protected:
    bool event(QEvent *event) override;
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void insertMediaLink(const QString &attachmentId, const QString &fileName, bool video);
    void toggleTodoAtCursor();
    void removePendingSlash(QTextCursor &cursor);
    void formatBlock(QTextCursor &cursor, const QString &type);
    void notifyBlockType();
    bool convertMarkdownBlock(QKeyEvent *event);
    bool handleBlockReturn(QKeyEvent *event);
    void convertInlineMarkdown();
    void refreshCodePresentation();
    void scheduleCodePresentation();
    void normalizeLoadedCodeBlocks(const QString &metadata);
    void normalizeImageBlocks();
    void refreshSearchMatches();
    void replaceMatch(QTextCursor cursor, const QString &replacement);
    QTextCursor imageCursorAt(const QPoint &point) const;
    QRect imageRect(const QTextCursor &selection) const;
    QPoint m_mousePressPoint;
    QString m_libraryDirectory;
    bool m_darkMode = false;
    int m_pendingSlash = -1;
    QString m_lastBlockType;
    bool m_preediting = false;
    QString m_lastCodeLanguage;
    CodeHighlighter *m_codeHighlighter = nullptr;
    bool m_refreshingCodePresentation = false;
    bool m_codePresentationPending = false;
    QString m_searchText;
    QTextDocument::FindFlags m_searchFlags;
    QList<QTextCursor> m_searchMatches;
    bool m_searchLimitReached = false;
};

#endif
