#pragma once

#include "notestore.h"
#include <QColor>
#include <QMainWindow>
#include <QTimer>
#include <memory>

class NoteEditor;
class AppUpdater;
class QCloseEvent;
class QComboBox;
class QCheckBox;
class QFrame;
class QImage;
class QLabel;
class QLineEdit;
class QPushButton;
class QListWidget;
class QLockFile;
class QSettings;
class QSplitter;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &dataDirectory, QWidget *parent = nullptr);
    ~MainWindow() override;
    bool isReady() const;
    QString startupError() const;
    bool runSmokeTest();
signals:
    void updateStateChanged();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    enum class View { Library, Favorites, Trash };
    void buildUi();
    void applyAppearance();
    void refreshTree(const QString &selectId = QString());
    void treeSelectionChanged();
    void showNote(const QString &id);
    void showFolder(const QString &id);
    bool saveCurrent();
    void scheduleSave();
    QString currentFolder() const;
    QString folderPath(const QString &id) const;
    void updateNavigationContext();
    void createNote(const QString &templateName = QString());
    void createFolder();
    void createDailyNote();
    void renameSelected();
    void moveSelected();
    void trashOrRestoreSelected();
    void setView(View view);
    void showTreeMenu(const QPoint &position);
    void importMedia(const QString &kind = QString());
    void importFiles(const QStringList &paths);
    void importPastedImage(const QImage &image);
    void chooseMarkdownFiles();
    void importMarkdownFiles(const QStringList &paths);
    void showFindBar(bool replace = false);
    void closeFindBar();
    void updateFindStatus();
    void refreshAttachments();
    void openAttachment(const QString &id);
    void previewImage(const QString &relativePath);
    void exportNote(bool html);
    void createBackup();
    void restoreBackup();
    void showHistory();
    void showDataProtection();
    bool runAutomaticBackup(bool force = false);
    void updateBackupStatus(const QString &failure = QString());
    void showPreferences();
    void showUpdates();
    void checkUpdates();
    void installUpdate();
    void showTemplateMenu();
    void showBlockMenu(const QPoint &position);
    void updateOutline();
    void updateEditorControls();
    void showError(const QString &message);
    void status(const QString &message);
    QTreeWidgetItem *findItem(const QString &id) const;
    QString selectedTreeId() const;
    void selectTreeId(const QString &id);

    NoteStore m_store;
    std::unique_ptr<QLockFile> m_lock;
    std::unique_ptr<QSettings> m_settings;
    QString m_startupError;
    QString m_noteId;
    QString m_folderId;
    View m_view = View::Library;
    bool m_ready = false;
    bool m_loading = false;
    bool m_dirty = false;
    bool m_saving = false;
    bool m_dark = false;
    bool m_focus = false;
    int m_fontSize = 16;
    QColor m_accent = QColor(QStringLiteral("#7771b5"));
    QTimer m_saveTimer;
    QTimer m_saveDeadlineTimer;
    QTimer m_searchTimer;
    QTimer m_backupTimer;
    AppUpdater *m_updater = nullptr;
    QString m_updateMessage;
    QString m_updateDirectory;
    QString m_updateVersion;
    bool m_updateAvailable = false;
    QPushButton *m_updateButton = nullptr;
    bool m_needsBackup = true;
    bool m_backupBusy = false;

    QSplitter *m_splitter = nullptr;
    QFrame *m_sidebar = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_sectionLabel = nullptr;
    QLabel *m_breadcrumb = nullptr;
    QLabel *m_saveLabel = nullptr;
    QLabel *m_backupLabel = nullptr;
    QStackedWidget *m_pages = nullptr;
    QWidget *m_notePage = nullptr;
    QWidget *m_folderPage = nullptr;
    QLineEdit *m_title = nullptr;
    QLabel *m_meta = nullptr;
    NoteEditor *m_editor = nullptr;
    QFrame *m_findBar = nullptr;
    QWidget *m_replaceRow = nullptr;
    QLineEdit *m_findText = nullptr;
    QLineEdit *m_replaceText = nullptr;
    QLabel *m_findStatus = nullptr;
    QCheckBox *m_findCase = nullptr;
    QCheckBox *m_findWholeWords = nullptr;
    QPushButton *m_replaceOne = nullptr;
    QPushButton *m_replaceAll = nullptr;
    QListWidget *m_attachmentList = nullptr;
    QToolButton *m_attachmentToggle = nullptr;
    QToolButton *m_favoriteButton = nullptr;
    QToolButton *m_focusButton = nullptr;
    QLabel *m_folderTitle = nullptr;
    QLabel *m_folderHint = nullptr;
    QListWidget *m_folderList = nullptr;
    QLabel *m_footerHint = nullptr;
    QListWidget *m_outline = nullptr;
    QFrame *m_codeTools = nullptr;
    QComboBox *m_codeLanguage = nullptr;
    QFrame *m_imageTools = nullptr;
    QLabel *m_imageInfo = nullptr;
};
