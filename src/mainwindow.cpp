#include "mainwindow.h"
#include "apptheme.h"
#include "mediaplayerdialog.h"
#include "noteeditor.h"
#include "markdownimporter.h"
#include "markdownexporter.h"
#include "appupdater.h"

#include <QAction>
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QCloseEvent>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDropEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLockFile>
#include <QMenu>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QProgressDialog>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QProcess>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QScrollArea>
#include <QScopedValueRollback>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTemporaryFile>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextList>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QUuid>
#include <algorithm>
#include <functional>

namespace {
constexpr int IdRole = Qt::UserRole;
constexpr int FolderRole = Qt::UserRole + 1;

class ImagePreviewDialog : public QDialog {
public:
    ImagePreviewDialog(const QString &path, const QString &name, QWidget *parent) : QDialog(parent) {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(name);
        setObjectName(QStringLiteral("imagePreviewDialog"));
        resize(1000, 720);
        QImageReader reader(path); reader.setAutoTransform(true);
        m_pixmap = QPixmap::fromImage(reader.read());
        auto *layout = new QVBoxLayout(this);
        auto *actions = new QHBoxLayout;
        auto *minus = new QPushButton(QStringLiteral("缩小 −"), this);
        auto *plus = new QPushButton(QStringLiteral("放大 +"), this);
        auto *fit = new QPushButton(QStringLiteral("适应窗口"), this);
        auto *actual = new QPushButton(QStringLiteral("100%"), this);
        m_zoom = new QLabel(this);
        actions->addWidget(minus); actions->addWidget(m_zoom); actions->addWidget(plus);
        actions->addWidget(fit); actions->addWidget(actual); actions->addStretch();
        auto *open = new QPushButton(QStringLiteral("用系统看图工具打开"), this);
        actions->addWidget(open);
        layout->addLayout(actions);
        m_area = new QScrollArea(this); m_area->setFrameShape(QFrame::NoFrame);
        m_area->setAlignment(Qt::AlignCenter);
        m_image = new QLabel; m_image->setAlignment(Qt::AlignCenter);
        m_area->setWidget(m_image); layout->addWidget(m_area, 1);
        connect(minus, &QPushButton::clicked, this, [this]() { m_fit = false; zoom(m_scale / 1.25); });
        connect(plus, &QPushButton::clicked, this, [this]() { m_fit = false; zoom(m_scale * 1.25); });
        connect(actual, &QPushButton::clicked, this, [this]() { m_fit = false; zoom(1); });
        connect(fit, &QPushButton::clicked, this, [this]() { m_fit = true; fitImage(); });
        connect(open, &QPushButton::clicked, this, [path]() { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
        QTimer::singleShot(0, this, [this]() { fitImage(); });
    }
protected:
    void resizeEvent(QResizeEvent *event) override {
        QDialog::resizeEvent(event);
        if (m_fit) fitImage();
    }
private:
    void fitImage() {
        if (!m_area || m_pixmap.isNull()) return;
        zoom(qMin(1.0, qMin(qreal(m_area->viewport()->width() - 24) / m_pixmap.width(),
                            qreal(m_area->viewport()->height() - 24) / m_pixmap.height())));
    }
    void zoom(qreal scale) {
        if (m_pixmap.isNull()) { m_image->setText(QStringLiteral("图片无法读取")); return; }
        m_scale = qBound(0.05, scale, 3.0);
        const QSize size = m_pixmap.size() * m_scale;
        m_image->setPixmap(m_pixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_image->resize(size);
        m_zoom->setText(QStringLiteral("%1%").arg(qRound(m_scale * 100)));
    }
    QPixmap m_pixmap;
    QScrollArea *m_area = nullptr;
    QLabel *m_image = nullptr;
    QLabel *m_zoom = nullptr;
    qreal m_scale = 1;
    bool m_fit = true;
};

class DirectoryTree : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
    std::function<void(const QString &, const QString &)> onMove;
protected:
    void drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const override {
        if (!model()->hasChildren(index)) return;
        const QPointF center(rect.right() - 8, rect.center().y());
        QPolygonF triangle;
        if (isExpanded(index)) triangle << center + QPointF(-3, -2) << center + QPointF(3, -2) << center + QPointF(0, 2);
        else triangle << center + QPointF(-2, -3) << center + QPointF(-2, 3) << center + QPointF(2, 0);
        painter->save(); painter->setRenderHint(QPainter::Antialiasing); painter->setPen(Qt::NoPen); painter->setBrush(QColor(QStringLiteral("#a0aa94"))); painter->drawPolygon(triangle); painter->restore();
    }
    void dropEvent(QDropEvent *event) override {
        if (!currentItem() || !onMove || dragDropMode() != InternalMove) { event->ignore(); return; }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QTreeWidgetItem *target = itemAt(event->position().toPoint());
#else
        QTreeWidgetItem *target = itemAt(event->pos());
#endif
        QString parent;
        if (target) {
            if (target->data(0, FolderRole).toBool() && dropIndicatorPosition() == OnItem) parent = target->data(0, IdRole).toString();
            else if (target->parent()) parent = target->parent()->data(0, IdRole).toString();
        }
        const QString id = currentItem()->data(0, IdRole).toString();
        onMove(id, parent);
        // The model has already been updated by the database callback. Returning
        // MoveAction would make QAbstractItemView remove the new selection again.
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }
};

class GardenCover : public QWidget {
public:
    explicit GardenCover(QWidget *parent = nullptr) : QWidget(parent) { setFixedHeight(105); }
    bool dark = false;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(dark ? "#2c3527" : "#f0f2e9"));
        const int right = width();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(dark ? "#37422f" : "#e2e8da"));
        painter.drawEllipse(QPointF(right - 130, 60), 74, 74);
        painter.setBrush(QColor(dark ? "#414735" : "#e8e8db"));
        painter.drawEllipse(QPointF(right - 35, 115), 83, 83);
        QPen pen(QColor(dark ? "#637657" : "#a2b194"), 1.2);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        for (int offset : {130, 270}) {
            const qreal x = right - offset;
            QPainterPath stem;
            stem.moveTo(x - 25, 125); stem.cubicTo(x - 15, 70, x + 18, 35, x + 38, 10);
            painter.drawPath(stem);
            QPainterPath leaf;
            leaf.moveTo(x, 75); leaf.cubicTo(x - 38, 65, x - 48, 43, x - 42, 32); leaf.cubicTo(x - 15, 34, x - 1, 51, x, 75);
            painter.drawPath(leaf);
            QPainterPath second;
            second.moveTo(x + 15, 47); second.cubicTo(x + 5, 18, x + 29, 4, x + 40, 8); second.cubicTo(x + 48, 29, x + 30, 44, x + 15, 47);
            painter.drawPath(second);
        }
        painter.setPen(QColor(dark ? "#9eaf90" : "#9aa88e"));
        QFont font(QStringLiteral("Microsoft YaHei UI")); font.setPixelSize(12); painter.setFont(font);
        painter.drawText(47, 55, QStringLiteral("让每一个想法，慢慢生长。"));
    }
};

QToolButton *tool(QWidget *parent, const QString &icon, const QString &tooltip) {
    auto *button = new QToolButton(parent);
    button->setIcon(AppTheme::icon(icon));
    button->setToolTip(tooltip);
    button->setAccessibleName(tooltip);
    button->setCursor(Qt::PointingHandCursor);
    button->setIconSize(QSize(18, 18));
    return button;
}

QString safeFileName(QString title) {
    for (const QChar c : QStringLiteral("\\/:*?\"<>|")) title.replace(c, QLatin1Char('_'));
    title = title.trimmed();
    return title.isEmpty() ? QStringLiteral("无标题笔记") : title.left(120);
}

bool isVideo(const Attachment &attachment) {
    return attachment.mime.startsWith(QStringLiteral("video/")) || QStringList{QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("mkv"), QStringLiteral("avi"), QStringLiteral("webm"), QStringLiteral("wmv"), QStringLiteral("m4v")}.contains(QFileInfo(attachment.originalName).suffix().toLower());
}

QString humanSize(qint64 bytes) {
    if (bytes < 1024) return QString::number(bytes) + QStringLiteral(" B");
    if (bytes < 1024 * 1024) return QString::number(bytes / 1024.0, 'f', 1) + QStringLiteral(" KB");
    return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
}
}

MainWindow::MainWindow(const QString &dataDirectory, QWidget *parent) : QMainWindow(parent), m_store(this)
{
    QDir().mkpath(dataDirectory);
    m_lock.reset(new QLockFile(QDir(dataDirectory).filePath(QStringLiteral("library.lock"))));
    if (!m_lock->tryLock(0)) { m_startupError = QStringLiteral("这个资料库已在另一个拾页窗口中打开。请先关闭那个窗口。"); return; }
    if (!m_store.open(dataDirectory, &m_startupError)) return;
    if (!m_store.seedIfEmpty()) { m_startupError = m_store.errorString(); return; }
    m_settings.reset(new QSettings(QDir(dataDirectory).filePath(QStringLiteral("preferences.ini")), QSettings::IniFormat));
    m_updater = new AppUpdater(this);
    m_dark = m_settings->value(QStringLiteral("appearance/dark"), false).toBool();
    m_fontSize = qBound(14, m_settings->value(QStringLiteral("appearance/fontSize"), 16).toInt(), 24);
    m_accent = QColor(m_settings->value(QStringLiteral("appearance/accent"), QStringLiteral("#7771b5")).toString());
    if (!m_accent.isValid()) m_accent = QColor(QStringLiteral("#7771b5"));
    setObjectName(QStringLiteral("MainWindow"));
    setWindowTitle(QStringLiteral("拾页 · 个人笔记"));
    setWindowIcon(AppTheme::icon(QStringLiteral("leaf"), QColor(QStringLiteral("#7d9167"))));
    resize(1440, 960);
    setMinimumSize(960, 680);
    buildUi();
    applyAppearance();
    m_saveTimer.setSingleShot(true); m_saveTimer.setInterval(650);
    connect(&m_saveTimer, &QTimer::timeout, this, [this]() { saveCurrent(); });
    m_saveDeadlineTimer.setSingleShot(true); m_saveDeadlineTimer.setInterval(5000);
    connect(&m_saveDeadlineTimer, &QTimer::timeout, this, [this]() { saveCurrent(); });
    m_searchTimer.setSingleShot(true); m_searchTimer.setInterval(180);
    connect(&m_searchTimer, &QTimer::timeout, this, [this]() { if (saveCurrent()) refreshTree(m_noteId); });
    connect(m_search, &QLineEdit::textChanged, this, [this]() { m_searchTimer.start(); });
    const QString previous = m_settings->value(QStringLiteral("session/selectedNote")).toString();
    QString first = previous;
    if (m_store.node(first).id.isEmpty() || m_store.node(first).trashed || m_store.node(first).folder) {
        first.clear();
        for (const auto &node : m_store.nodes()) {
            if (!node.folder && (first.isEmpty() || node.title == QStringLiteral("建立自己的知识花园"))) first = node.id;
        }
    }
    refreshTree(first);
    if (!first.isEmpty()) showNote(first); else showFolder(QString());
    const QByteArray geometry = m_settings->value(QStringLiteral("window/geometry")).toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    const QByteArray splitter = m_settings->value(QStringLiteral("window/splitter")).toByteArray();
    if (!splitter.isEmpty()) m_splitter->restoreState(splitter);
    m_ready = true;
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationInactive && m_dirty && !m_backupBusy) saveCurrent();
    });
    connect(&m_store, &NoteStore::changed, this, [this]() { m_needsBackup = true; });
    m_backupTimer.setInterval(30 * 60 * 1000);
    connect(&m_backupTimer, &QTimer::timeout, this, [this]() { runAutomaticBackup(); });
    m_backupTimer.start();
    updateBackupStatus();
    QTimer::singleShot(5000, this, [this]() { runAutomaticBackup(); });
    connect(m_updater, &AppUpdater::checkingFinished, this, [this](bool available, const QString &message) {
        m_updateAvailable = available; m_updateMessage = message;
        m_updateButton->setText(available ? QStringLiteral("可用更新 %1").arg(m_updater->release().version) : QStringLiteral("检查更新"));
        m_updateButton->setToolTip(message); m_updateButton->setEnabled(true); emit updateStateChanged();
    });
    connect(m_updater, &AppUpdater::failed, this, [this](const QString &message) {
        m_updateMessage = message; m_updateButton->setText(m_updateAvailable ? QStringLiteral("可用更新 %1").arg(m_updater->release().version) : QStringLiteral("检查更新")); m_updateButton->setEnabled(true); m_updateButton->setToolTip(message); emit updateStateChanged();
    });
    connect(m_updater, &AppUpdater::packageReady, this, [this](const QString &directory, const QString &version) {
        m_updateDirectory = directory; m_updateVersion = version; m_updateMessage = QStringLiteral("更新包下载和校验完成，可以安装。"); emit updateStateChanged();
    });
    if (!QCoreApplication::arguments().contains(QStringLiteral("--smoke-test"))) QTimer::singleShot(12000, this, [this]() {
        const auto last = QDateTime::fromString(m_settings->value(QStringLiteral("updates/lastCheck")).toString(), Qt::ISODateWithMs);
        if (m_settings->value(QStringLiteral("updates/automatic"), true).toBool() && (!last.isValid() || last.secsTo(QDateTime::currentDateTimeUtc()) > 4 * 60 * 60)) checkUpdates();
    });
}

MainWindow::~MainWindow() = default;
bool MainWindow::isReady() const { return m_ready; }
QString MainWindow::startupError() const { return m_startupError; }

void MainWindow::buildUi()
{
    auto *app = new QWidget(this); app->setObjectName(QStringLiteral("app")); setCentralWidget(app);
    auto *outer = new QVBoxLayout(app); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);
    auto *titlebar = new QFrame(app); titlebar->setObjectName(QStringLiteral("titlebar")); titlebar->setFixedHeight(52);
    auto *titleLayout = new QHBoxLayout(titlebar); titleLayout->setContentsMargins(23, 0, 23, 0); titleLayout->setSpacing(12);
    auto *mark = new QLabel(titlebar); mark->setPixmap(AppTheme::icon(QStringLiteral("leaf"), QColor("#7e906a")).pixmap(28, 28));
    auto *brand = new QLabel(QStringLiteral("拾页"), titlebar); brand->setObjectName(QStringLiteral("brandLabel"));
    auto *tagline = new QLabel(QStringLiteral("给想法一个家"), titlebar); tagline->setObjectName(QStringLiteral("brandTagline"));
    titleLayout->addWidget(mark); titleLayout->addWidget(brand); titleLayout->addWidget(tagline); titleLayout->addStretch();
    auto *local = new QLabel(QStringLiteral("●  本地资料库"), titlebar); local->setObjectName(QStringLiteral("footerHint")); titleLayout->addWidget(local);
    outer->addWidget(titlebar);
    m_splitter = new QSplitter(Qt::Horizontal, app); m_splitter->setHandleWidth(1); outer->addWidget(m_splitter, 1);

    m_sidebar = new QFrame(m_splitter); m_sidebar->setObjectName(QStringLiteral("sidebar")); m_sidebar->setMinimumWidth(215); m_sidebar->setMaximumWidth(390);
    auto *side = new QVBoxLayout(m_sidebar); side->setContentsMargins(16, 23, 16, 16); side->setSpacing(12);
    auto *space = new QLabel(QStringLiteral("我的笔记"), m_sidebar); space->setObjectName(QStringLiteral("spaceLabel")); side->addWidget(space);
    auto *hint = new QLabel(QStringLiteral("一点记录，慢慢积累。"), m_sidebar); hint->setObjectName(QStringLiteral("spaceHint")); side->addWidget(hint);
    auto *createRow = new QHBoxLayout; createRow->setSpacing(7);
    auto *newNote = new QPushButton(AppTheme::icon(QStringLiteral("plus"), Qt::white), QStringLiteral("新建笔记"), m_sidebar); newNote->setObjectName(QStringLiteral("newNoteButton")); newNote->setToolTip(QStringLiteral("在当前目录创建笔记 · Ctrl+N"));
    auto *newFolder = tool(m_sidebar, QStringLiteral("new-folder"), QStringLiteral("新建文件夹 · Ctrl+Shift+N")); newFolder->setObjectName(QStringLiteral("newFolderButton"));
    createRow->addWidget(newNote, 1); createRow->addWidget(newFolder); side->addLayout(createRow);
    m_search = new QLineEdit(m_sidebar); m_search->setObjectName(QStringLiteral("searchEdit")); m_search->setPlaceholderText(QStringLiteral("搜索笔记…")); m_search->setClearButtonEnabled(true); m_search->addAction(AppTheme::icon(QStringLiteral("search")), QLineEdit::LeadingPosition); side->addWidget(m_search);
    m_sectionLabel = new QLabel(QStringLiteral("笔记目录"), m_sidebar); m_sectionLabel->setObjectName(QStringLiteral("sectionLabel")); side->addWidget(m_sectionLabel);
    auto *tree = new DirectoryTree(m_sidebar); m_tree = tree; m_tree->setObjectName(QStringLiteral("directoryTree")); m_tree->setHeaderHidden(true); m_tree->setIndentation(17); m_tree->setIconSize(QSize(16, 16)); m_tree->setAnimated(true); m_tree->setExpandsOnDoubleClick(true); m_tree->setSelectionMode(QAbstractItemView::SingleSelection); m_tree->setDragDropMode(QAbstractItemView::InternalMove); m_tree->setDefaultDropAction(Qt::MoveAction); m_tree->setContextMenuPolicy(Qt::CustomContextMenu); m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); side->addWidget(m_tree, 1);
    tree->onMove = [this](const QString &id, const QString &parentId) { if (!saveCurrent()) return; if (!m_store.moveNode(id, parentId)) { showError(m_store.errorString()); return; } refreshTree(id); updateNavigationContext(); status(QStringLiteral("已移动到新的目录")); };
    auto *bottom = new QWidget(m_sidebar); bottom->setObjectName(QStringLiteral("sidebarBottom")); auto *bottomLayout = new QVBoxLayout(bottom); bottomLayout->setContentsMargins(0, 8, 0, 0); bottomLayout->setSpacing(3);
    auto nav = [&](const QString &icon, const QString &text, const std::function<void()> &callback) { auto *button = new QPushButton(AppTheme::icon(icon), text, bottom); button->setFlat(true); button->setCursor(Qt::PointingHandCursor); bottomLayout->addWidget(button); connect(button, &QPushButton::clicked, this, callback); return button; };
    nav(QStringLiteral("book"), QStringLiteral("全部笔记"), [this]() { setView(View::Library); });
    nav(QStringLiteral("sun"), QStringLiteral("今日日记"), [this]() { createDailyNote(); });
    nav(QStringLiteral("star"), QStringLiteral("我的收藏"), [this]() { setView(View::Favorites); });
    nav(QStringLiteral("trash"), QStringLiteral("回收站"), [this]() { setView(View::Trash); });
    nav(QStringLiteral("settings"), QStringLiteral("偏好设置"), [this]() { showPreferences(); });
    side->addWidget(bottom);
    auto *offline = new QLabel(QStringLiteral("●  离线也能安心记录"), m_sidebar); offline->setObjectName(QStringLiteral("footerHint")); side->addWidget(offline);

    auto *editorPane = new QFrame(m_splitter); editorPane->setObjectName(QStringLiteral("editorPane")); auto *right = new QVBoxLayout(editorPane); right->setContentsMargins(0, 0, 0, 0); right->setSpacing(0);
    auto *header = new QFrame(editorPane); header->setObjectName(QStringLiteral("editorHeader")); header->setFixedHeight(51); auto *headerLayout = new QHBoxLayout(header); headerLayout->setContentsMargins(26, 0, 24, 0); headerLayout->setSpacing(7);
    m_breadcrumb = new QLabel(QStringLiteral("我的笔记"), header); m_breadcrumb->setObjectName(QStringLiteral("breadcrumbLabel")); m_breadcrumb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); headerLayout->addWidget(m_breadcrumb, 1);
    m_saveLabel = new QLabel(QStringLiteral("已保存"), header); m_saveLabel->setObjectName(QStringLiteral("saveLabel")); headerLayout->addWidget(m_saveLabel);
    m_updateButton = new QPushButton(QStringLiteral("检查更新"), header); m_updateButton->setObjectName(QStringLiteral("updateButton")); m_updateButton->setFlat(true); headerLayout->addWidget(m_updateButton);
    connect(m_updateButton, &QPushButton::clicked, this, &MainWindow::showUpdates);
    m_backupLabel = new QLabel(header); m_backupLabel->setObjectName(QStringLiteral("backupStatus")); headerLayout->addWidget(m_backupLabel);
    m_favoriteButton = tool(header, QStringLiteral("star"), QStringLiteral("收藏笔记")); m_favoriteButton->setCheckable(true); headerLayout->addWidget(m_favoriteButton);
    auto *outlineButton = tool(header, QStringLiteral("list"), QStringLiteral("文章大纲")); headerLayout->addWidget(outlineButton);
    m_focusButton = tool(header, QStringLiteral("expand"), QStringLiteral("专注模式 · Esc 返回")); m_focusButton->setCheckable(true); headerLayout->addWidget(m_focusButton);
    auto *moreButton = tool(header, QStringLiteral("more"), QStringLiteral("导出与资料库")); headerLayout->addWidget(moreButton);
    auto *more = new QMenu(moreButton); more->addAction(QStringLiteral("导出 Markdown…"), this, [this]() { exportNote(false); }); more->addAction(QStringLiteral("导出 HTML（含附件）…"), this, [this]() { exportNote(true); }); more->addSeparator(); more->addAction(QStringLiteral("完整备份资料库…"), this, [this]() { createBackup(); }); more->addAction(QStringLiteral("从备份恢复…"), this, [this]() { restoreBackup(); }); more->addSeparator(); more->addAction(QStringLiteral("打开资料库文件夹"), this, [this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(m_store.dataDirectory())); }); moreButton->setMenu(more); moreButton->setPopupMode(QToolButton::InstantPopup);
    auto *historyAction = new QAction(QStringLiteral("笔记历史版本…"), more); historyAction->setObjectName(QStringLiteral("noteHistoryAction"));
    connect(historyAction, &QAction::triggered, this, &MainWindow::showHistory); more->insertAction(more->actions().first(), historyAction);
    auto *protectionAction = more->addAction(QStringLiteral("数据保护与自动备份…"), this, &MainWindow::showDataProtection); protectionAction->setObjectName(QStringLiteral("dataProtectionAction"));
    auto *importAction = new QAction(QStringLiteral("导入 Markdown 文件…"), more); importAction->setObjectName(QStringLiteral("importMarkdownAction"));
    connect(importAction, &QAction::triggered, this, &MainWindow::chooseMarkdownFiles); more->insertAction(historyAction, importAction);
    more->addAction(QStringLiteral("文内查找与替换…"), this, [this]() { showFindBar(true); });
    more->addSeparator(); auto *updates = more->addAction(QStringLiteral("软件更新…"), this, &MainWindow::showUpdates); updates->setObjectName(QStringLiteral("updatesAction"));
    right->addWidget(header);
    m_pages = new QStackedWidget(editorPane); right->addWidget(m_pages, 1);
    m_notePage = new QWidget(m_pages); m_notePage->setObjectName(QStringLiteral("notePage")); auto *noteLayout = new QVBoxLayout(m_notePage); noteLayout->setContentsMargins(0, 0, 0, 0); noteLayout->setSpacing(0);
    auto *cover = new GardenCover(m_notePage); cover->setObjectName(QStringLiteral("cover")); noteLayout->addWidget(cover);
    auto *documentRow = new QHBoxLayout; documentRow->setContentsMargins(28, 19, 28, 12); documentRow->setSpacing(0); documentRow->addStretch();
    auto *documentFrame = new QFrame(m_notePage); documentFrame->setObjectName(QStringLiteral("documentFrame")); documentFrame->setMaximumWidth(920); auto *document = new QVBoxLayout(documentFrame); document->setContentsMargins(0, 0, 0, 0); document->setSpacing(8);
    auto *documentTop = new QHBoxLayout; auto *symbol = new QLabel(documentFrame); symbol->setPixmap(AppTheme::icon(QStringLiteral("leaf"), QColor("#9baa77")).pixmap(31, 31)); documentTop->addWidget(symbol); documentTop->addStretch(); auto *templates = new QPushButton(AppTheme::icon(QStringLiteral("book")), QStringLiteral("使用模板"), documentFrame); templates->setFlat(true); documentTop->addWidget(templates); document->addLayout(documentTop);
    m_title = new QLineEdit(documentFrame); m_title->setObjectName(QStringLiteral("titleEdit")); m_title->setPlaceholderText(QStringLiteral("无标题笔记")); m_title->setFrame(false); document->addWidget(m_title);
    m_meta = new QLabel(documentFrame); m_meta->setObjectName(QStringLiteral("metaLabel")); document->addWidget(m_meta);
    auto *formatting = new QFrame(documentFrame); formatting->setObjectName(QStringLiteral("formattingToolbar")); auto *formats = new QHBoxLayout(formatting); formats->setContentsMargins(0, 8, 0, 8); formats->setSpacing(4);
    auto *block = new QComboBox(formatting);
    block->setObjectName(QStringLiteral("blockTypeSelector"));
    block->setToolTip(QStringLiteral("段落格式 · Ctrl+Alt+1/2/3 切换标题"));
    block->setMinimumWidth(145);
    block->addItem(QStringLiteral("正文"), QStringLiteral("paragraph"));
    const QStringList headingNames = {QStringLiteral("一级标题"), QStringLiteral("二级标题"), QStringLiteral("三级标题"), QStringLiteral("四级标题"), QStringLiteral("五级标题"), QStringLiteral("六级标题")};
    for (int level = 1; level <= 6; ++level)
        block->addItem(headingNames.at(level - 1) + QStringLiteral("  H%1").arg(level), QStringLiteral("h%1").arg(level));
    block->addItem(QStringLiteral("项目列表"), QStringLiteral("bullet"));
    block->addItem(QStringLiteral("编号列表"), QStringLiteral("number"));
    block->addItem(QStringLiteral("待办事项"), QStringLiteral("todo"));
    block->addItem(QStringLiteral("引用"), QStringLiteral("quote"));
    block->addItem(QStringLiteral("代码块"), QStringLiteral("code"));
    block->addItem(QStringLiteral("分割线"), QStringLiteral("divider"));
    formats->addWidget(block);
    for (int level = 1; level <= 3; ++level) {
        auto *heading = new QToolButton(formatting);
        heading->setObjectName(QStringLiteral("heading%1Button").arg(level));
        heading->setText(QStringLiteral("H%1").arg(level));
        heading->setCheckable(true);
        heading->setCursor(Qt::PointingHandCursor);
        heading->setAccessibleName(headingNames.at(level - 1));
        heading->setToolTip(headingNames.at(level - 1) + QStringLiteral(" · ") + QString(level, QLatin1Char('#')) + QStringLiteral(" 后按空格 · Ctrl+Alt+%1").arg(level));
        formats->addWidget(heading);
        connect(heading, &QToolButton::clicked, this, [this, level]() { m_editor->applyBlock(QStringLiteral("h%1").arg(level)); updateEditorControls(); });
    }
    auto *bold = tool(formatting, QStringLiteral("bold"), QStringLiteral("加粗 · Ctrl+B")); auto *italic = tool(formatting, QStringLiteral("italic"), QStringLiteral("斜体 · Ctrl+I")); auto *undo = tool(formatting, QStringLiteral("undo"), QStringLiteral("撤销 · Ctrl+Z")); auto *redo = tool(formatting, QStringLiteral("redo"), QStringLiteral("重做 · Ctrl+Y")); formats->addWidget(bold); formats->addWidget(italic); formats->addWidget(undo); formats->addWidget(redo); formats->addStretch();
    auto *image = new QPushButton(AppTheme::icon(QStringLiteral("image")), QStringLiteral("图片"), formatting); image->setFlat(true); image->setToolTip(QStringLiteral("导入图片，也可以粘贴或拖入")); formats->addWidget(image); auto *video = new QPushButton(AppTheme::icon(QStringLiteral("video")), QStringLiteral("视频"), formatting); video->setFlat(true); video->setToolTip(QStringLiteral("导入本地视频")); formats->addWidget(video); noteLayout->addLayout(documentRow, 1); document->addWidget(formatting);
    m_codeTools = new QFrame(documentFrame);
    m_codeTools->setObjectName(QStringLiteral("codeTools"));
    auto *codeToolsLayout = new QHBoxLayout(m_codeTools);
    codeToolsLayout->setContentsMargins(11, 6, 11, 6);
    codeToolsLayout->setSpacing(9);
    auto *codeIcon = new QLabel(m_codeTools);
    codeIcon->setPixmap(AppTheme::icon(QStringLiteral("code")).pixmap(17, 17));
    codeToolsLayout->addWidget(codeIcon);
    auto *codeLabel = new QLabel(QStringLiteral("代码语言"), m_codeTools);
    codeToolsLayout->addWidget(codeLabel);
    m_codeLanguage = new QComboBox(m_codeTools);
    m_codeLanguage->setObjectName(QStringLiteral("codeLanguageSelector"));
    m_codeLanguage->setAccessibleName(QStringLiteral("代码块语言"));
    m_codeLanguage->setEditable(true);
    m_codeLanguage->setInsertPolicy(QComboBox::NoInsert);
    m_codeLanguage->setMinimumWidth(160);
    m_codeLanguage->setMaximumWidth(210);
    const QList<QPair<QString, QString>> languages = {
        {QStringLiteral("纯文本"), QStringLiteral("plaintext")},
        {QStringLiteral("C++"), QStringLiteral("cpp")},
        {QStringLiteral("C"), QStringLiteral("c")},
        {QStringLiteral("Python"), QStringLiteral("python")},
        {QStringLiteral("JavaScript"), QStringLiteral("javascript")},
        {QStringLiteral("TypeScript"), QStringLiteral("typescript")},
        {QStringLiteral("JSON"), QStringLiteral("json")},
        {QStringLiteral("HTML"), QStringLiteral("html")},
        {QStringLiteral("CSS"), QStringLiteral("css")},
        {QStringLiteral("SQL"), QStringLiteral("sql")},
        {QStringLiteral("Bash / Shell"), QStringLiteral("bash")}
    };
    for (const auto &language : languages) m_codeLanguage->addItem(language.first, language.second);
    m_codeLanguage->setToolTip(QStringLiteral("选择语言，或输入语言名称后按回车；也可输入 ```python 后按回车创建代码块"));
    codeToolsLayout->addWidget(m_codeLanguage);
    codeToolsLayout->addStretch();
    auto *copyCode = new QPushButton(QStringLiteral("复制代码"), m_codeTools);
    copyCode->setObjectName(QStringLiteral("copyCodeButton"));
    copyCode->setFlat(true);
    codeToolsLayout->addWidget(copyCode);
    document->addWidget(m_codeTools);
    m_codeTools->hide();
    m_imageTools = new QFrame(documentFrame);
    m_imageTools->setObjectName(QStringLiteral("imageTools"));
    auto *imageTools = new QHBoxLayout(m_imageTools);
    imageTools->setContentsMargins(10, 5, 10, 5);
    m_imageInfo = new QLabel(QStringLiteral("图片"), m_imageTools);
    imageTools->addWidget(m_imageInfo);
    auto *imageSize = new QComboBox(m_imageTools);
    imageSize->setObjectName(QStringLiteral("imageSizeSelector"));
    imageSize->setProperty("imageMutation", true);
    imageSize->addItem(QStringLiteral("调整尺寸"), -1);
    imageSize->addItem(QStringLiteral("适应正文"), 0);
    imageSize->addItem(QStringLiteral("小图 · 320 px"), 320);
    imageSize->addItem(QStringLiteral("中图 · 480 px"), 480);
    imageSize->addItem(QStringLiteral("大图 · 640 px"), 640);
    imageTools->addWidget(imageSize);
    for (const auto &choice : {qMakePair(QStringLiteral("左对齐"), Qt::AlignLeft), qMakePair(QStringLiteral("居中"), Qt::AlignHCenter)}) {
        auto *button = new QPushButton(choice.first, m_imageTools);
        button->setProperty("imageMutation", true);
        imageTools->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, choice]() { m_editor->alignSelectedImage(choice.second); m_editor->setFocus(); });
    }
    imageTools->addStretch();
    auto *preview = new QPushButton(QStringLiteral("预览"), m_imageTools);
    preview->setObjectName(QStringLiteral("previewImageButton"));
    auto *removeImage = new QPushButton(QStringLiteral("移除"), m_imageTools);
    removeImage->setProperty("imageMutation", true);
    removeImage->setToolTip(QStringLiteral("从正文移除图片，原图仍保留在附件中"));
    imageTools->addWidget(preview); imageTools->addWidget(removeImage);
    document->addWidget(m_imageTools); m_imageTools->hide();
    m_findBar = new QFrame(documentFrame); m_findBar->setObjectName(QStringLiteral("findBar"));
    auto *findLayout = new QVBoxLayout(m_findBar); findLayout->setContentsMargins(10, 8, 10, 8); findLayout->setSpacing(6);
    auto *findRow = new QHBoxLayout; findLayout->addLayout(findRow);
    m_findText = new QLineEdit(m_findBar); m_findText->setObjectName(QStringLiteral("findText")); m_findText->setPlaceholderText(QStringLiteral("在当前正文中查找…")); m_findText->setClearButtonEnabled(true); findRow->addWidget(m_findText, 1);
    m_findStatus = new QLabel(m_findBar); m_findStatus->setMinimumWidth(70); findRow->addWidget(m_findStatus);
    auto *previousMatch = new QPushButton(QStringLiteral("上一处"), m_findBar); previousMatch->setToolTip(QStringLiteral("Shift+F3")); findRow->addWidget(previousMatch);
    auto *nextMatch = new QPushButton(QStringLiteral("下一处"), m_findBar); nextMatch->setToolTip(QStringLiteral("Enter / F3")); findRow->addWidget(nextMatch);
    auto *closeFind = new QPushButton(QStringLiteral("关闭"), m_findBar); closeFind->setToolTip(QStringLiteral("Esc")); findRow->addWidget(closeFind);
    auto *options = new QHBoxLayout; findLayout->addLayout(options);
    m_findCase = new QCheckBox(QStringLiteral("区分大小写"), m_findBar); options->addWidget(m_findCase);
    m_findWholeWords = new QCheckBox(QStringLiteral("全字匹配"), m_findBar); options->addWidget(m_findWholeWords); options->addStretch();
    auto *toggleReplace = new QPushButton(QStringLiteral("替换…"), m_findBar); options->addWidget(toggleReplace);
    m_replaceRow = new QWidget(m_findBar); auto *replaceLayout = new QHBoxLayout(m_replaceRow); replaceLayout->setContentsMargins(0, 0, 0, 0);
    m_replaceText = new QLineEdit(m_replaceRow); m_replaceText->setPlaceholderText(QStringLiteral("替换为…（留空则删除匹配内容）")); replaceLayout->addWidget(m_replaceText, 1);
    m_replaceOne = new QPushButton(QStringLiteral("替换当前"), m_replaceRow); replaceLayout->addWidget(m_replaceOne);
    m_replaceAll = new QPushButton(QStringLiteral("全部替换"), m_replaceRow); replaceLayout->addWidget(m_replaceAll);
    findLayout->addWidget(m_replaceRow); document->addWidget(m_findBar); m_findBar->hide(); m_replaceRow->hide();
    m_editor = new NoteEditor(documentFrame); m_editor->setObjectName(QStringLiteral("noteEditor")); m_editor->setLibraryDirectory(m_store.dataDirectory()); m_editor->setFrameStyle(QFrame::NoFrame); document->addWidget(m_editor, 1);
    auto queryChanged = [this]() { m_editor->setSearch(m_findText->text(), m_findCase->isChecked(), m_findWholeWords->isChecked()); m_editor->findSearchMatch(); updateFindStatus(); };
    connect(m_findText, &QLineEdit::textChanged, this, queryChanged); connect(m_findCase, &QCheckBox::toggled, this, queryChanged); connect(m_findWholeWords, &QCheckBox::toggled, this, queryChanged);
    connect(m_editor, &NoteEditor::searchResultsChanged, this, &MainWindow::updateFindStatus);
    connect(previousMatch, &QPushButton::clicked, this, [this]() { m_editor->findSearchMatch(true); });
    connect(nextMatch, &QPushButton::clicked, this, [this]() { m_editor->findSearchMatch(); });
    connect(m_findText, &QLineEdit::returnPressed, nextMatch, &QPushButton::click);
    connect(closeFind, &QPushButton::clicked, this, &MainWindow::closeFindBar);
    connect(toggleReplace, &QPushButton::clicked, this, [this]() { m_replaceRow->setVisible(!m_replaceRow->isVisible()); updateFindStatus(); });
    connect(m_replaceOne, &QPushButton::clicked, this, [this]() { m_editor->replaceSearchMatch(m_replaceText->text()); });
    connect(m_replaceAll, &QPushButton::clicked, this, [this]() {
        if (m_editor->isReadOnly() || !saveCurrent()) return;
        const int count = m_editor->replaceAllSearchMatches(m_replaceText->text());
        status(count < 0 ? QStringLiteral("匹配超过 10000 项，请使用更具体的关键词。") : QStringLiteral("已替换 %1 处，Ctrl+Z 可一次撤销全部替换。").arg(count));
    });
    m_attachmentToggle = new QToolButton(documentFrame); m_attachmentToggle->setObjectName(QStringLiteral("attachmentToggle")); m_attachmentToggle->setCheckable(true); m_attachmentToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); m_attachmentToggle->setArrowType(Qt::RightArrow); document->addWidget(m_attachmentToggle);
    m_attachmentList = new QListWidget(documentFrame); m_attachmentList->setObjectName(QStringLiteral("attachmentList")); m_attachmentList->setMaximumHeight(90); m_attachmentList->setMinimumHeight(0); m_attachmentList->setIconSize(QSize(18, 18)); document->addWidget(m_attachmentList);
    m_footerHint = new QLabel(QStringLiteral("Markdown 快捷输入：# 一级标题  ·  ## 二级标题  ·  ### 三级标题（标记后按空格）"), documentFrame); m_footerHint->setObjectName(QStringLiteral("footerHint")); m_footerHint->setWordWrap(true); document->addWidget(m_footerHint);
    documentRow->addWidget(documentFrame, 1); documentRow->addStretch(); m_pages->addWidget(m_notePage);

    m_folderPage = new QWidget(m_pages); auto *folderLayout = new QVBoxLayout(m_folderPage); folderLayout->setContentsMargins(60, 55, 60, 36); folderLayout->setSpacing(15);
    m_folderTitle = new QLabel(QStringLiteral("我的笔记"), m_folderPage); m_folderTitle->setObjectName(QStringLiteral("folderTitle")); folderLayout->addWidget(m_folderTitle);
    m_folderHint = new QLabel(m_folderPage); m_folderHint->setObjectName(QStringLiteral("folderHint")); folderLayout->addWidget(m_folderHint);
    m_folderList = new QListWidget(m_folderPage); m_folderList->setObjectName(QStringLiteral("folderList")); m_folderList->setIconSize(QSize(26, 26)); folderLayout->addWidget(m_folderList, 1);
    auto *folderActions = new QHBoxLayout; auto *folderNew = new QPushButton(AppTheme::icon(QStringLiteral("plus")), QStringLiteral("在这里新建笔记"), m_folderPage); auto *folderChild = new QPushButton(AppTheme::icon(QStringLiteral("new-folder")), QStringLiteral("新建子文件夹"), m_folderPage); folderActions->addWidget(folderNew); folderActions->addWidget(folderChild); folderActions->addStretch(); folderLayout->addLayout(folderActions); m_pages->addWidget(m_folderPage);
    m_outline = new QListWidget(editorPane); m_outline->setWindowFlags(Qt::Popup); m_outline->setMinimumWidth(220); m_outline->setMaximumHeight(330); m_outline->setObjectName(QStringLiteral("outlineList"));
    m_splitter->addWidget(m_sidebar); m_splitter->addWidget(editorPane); m_splitter->setSizes({260, 1180}); m_splitter->setStretchFactor(0, 0); m_splitter->setStretchFactor(1, 1);
    auto *statusBar = new QStatusBar(this); setStatusBar(statusBar); statusBar->setSizeGripEnabled(false); statusBar->showMessage(QStringLiteral("●  所有内容保存在这台设备")); auto *shortcut = new QLabel(QStringLiteral("Ctrl+N 新建   ·   Ctrl+Shift+F 搜索   ·   Ctrl+S 保存"), statusBar); shortcut->setObjectName(QStringLiteral("footerHint")); statusBar->addPermanentWidget(shortcut);

    connect(newNote, &QPushButton::clicked, this, [this]() { createNote(); }); connect(newFolder, &QToolButton::clicked, this, [this]() { createFolder(); });
    connect(folderNew, &QPushButton::clicked, this, [this]() { createNote(); }); connect(folderChild, &QPushButton::clicked, this, [this]() { createFolder(); });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this]() { treeSelectionChanged(); }); connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &MainWindow::showTreeMenu);
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) { item->setIcon(0, AppTheme::icon(QStringLiteral("folder-open"))); }); connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *item) { item->setIcon(0, AppTheme::icon(QStringLiteral("folder"))); });
    connect(m_folderList, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) { selectTreeId(item->data(IdRole).toString()); });
    connect(m_title, &QLineEdit::textEdited, this, [this]() { scheduleSave(); }); connect(m_title, &QLineEdit::returnPressed, m_editor, qOverload<>(&QWidget::setFocus));
    connect(m_editor, &QTextEdit::textChanged, this, [this]() { scheduleSave(); }); connect(m_editor, &NoteEditor::saveRequested, this, [this]() { saveCurrent(); }); connect(m_editor, &NoteEditor::filesDropped, this, &MainWindow::importFiles); connect(m_editor, &NoteEditor::imagePasted, this, &MainWindow::importPastedImage); connect(m_editor, &NoteEditor::attachmentActivated, this, &MainWindow::openAttachment); connect(m_editor, &NoteEditor::slashRequested, this, &MainWindow::showBlockMenu);
    connect(m_editor, &NoteEditor::blockTypeChanged, this, [this]() { updateEditorControls(); });
    connect(m_editor, &NoteEditor::externalLinkActivated, this, [](const QUrl &url) { QDesktopServices::openUrl(url); });
    connect(m_editor, &NoteEditor::codeLanguageChanged, this, [this]() { updateEditorControls(); });
    connect(m_editor, &NoteEditor::contentMetadataChanged, this, [this]() { scheduleSave(); });
    connect(m_editor, &QTextEdit::cursorPositionChanged, this, [this]() { updateEditorControls(); });
    connect(m_editor, &QTextEdit::selectionChanged, this, [this]() { updateEditorControls(); });
    connect(m_editor, &QTextEdit::currentCharFormatChanged, this, [this]() { updateEditorControls(); });
    connect(m_editor, &NoteEditor::imageActivated, this, &MainWindow::previewImage);
    connect(preview, &QPushButton::clicked, this, [this]() { previewImage(m_editor->selectedImageSource()); });
    connect(removeImage, &QPushButton::clicked, this, [this]() { m_editor->removeSelectedImage(); });
    connect(imageSize, qOverload<int>(&QComboBox::activated), this, [this, imageSize](int index) {
        const int width = imageSize->itemData(index).toInt();
        if (width >= 0) m_editor->resizeSelectedImage(width);
        imageSize->setCurrentIndex(0); updateEditorControls(); m_editor->setFocus();
    });
    connect(m_attachmentToggle, &QToolButton::toggled, this, [this](bool expanded) {
        m_attachmentToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        m_attachmentList->setVisible(expanded && m_attachmentList->count() > 0);
    });
    auto chooseCodeLanguage = [this]() {
        if (m_loading || m_editor->isReadOnly() || m_editor->currentBlockType() != QStringLiteral("code")) return;
        QString language = m_codeLanguage->currentText().trimmed();
        const int index = m_codeLanguage->currentIndex();
        if (index >= 0 && m_codeLanguage->itemText(index) == language) language = m_codeLanguage->itemData(index).toString();
        const QString before = m_editor->currentCodeLanguage();
        m_editor->setCurrentCodeLanguage(language);
        if (m_editor->currentCodeLanguage() != before) scheduleSave();
        updateEditorControls();
    };
    connect(m_codeLanguage, qOverload<int>(&QComboBox::activated), this, [this, chooseCodeLanguage]() { chooseCodeLanguage(); m_editor->setFocus(); });
    connect(m_codeLanguage->lineEdit(), &QLineEdit::editingFinished, this, chooseCodeLanguage);
    connect(copyCode, &QPushButton::clicked, this, [this]() {
        QTextBlock block = m_editor->textCursor().block();
        if (!NoteEditor::isCodeBlock(block)) return;
        const QString id = NoteEditor::codeBlockId(block);
        while (block.previous().isValid() && NoteEditor::isCodeBlock(block.previous()) && NoteEditor::codeBlockId(block.previous()) == id) block = block.previous();
        QStringList lines;
        while (block.isValid() && NoteEditor::isCodeBlock(block) && NoteEditor::codeBlockId(block) == id) { lines << block.text(); block = block.next(); }
        QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
        status(QStringLiteral("当前代码块已复制"));
    });
    connect(m_attachmentList, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) { openAttachment(item->data(IdRole).toString()); });
    connect(m_favoriteButton, &QToolButton::clicked, this, [this](bool checked) { if (m_noteId.isEmpty() || !saveCurrent()) return; if (!m_store.setFavorite(m_noteId, checked)) showError(m_store.errorString()); refreshTree(m_noteId); });
    connect(m_focusButton, &QToolButton::clicked, this, [this](bool checked) { m_focus = checked; m_sidebar->setVisible(!checked); });
    connect(outlineButton, &QToolButton::clicked, this, [this, outlineButton]() { updateOutline(); m_outline->move(outlineButton->mapToGlobal(QPoint(-160, outlineButton->height() + 6))); m_outline->show(); });
    connect(m_outline, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { QTextCursor cursor(m_editor->document()->findBlockByNumber(item->data(IdRole).toInt())); m_editor->setTextCursor(cursor); m_editor->ensureCursorVisible(); m_outline->hide(); m_editor->setFocus(); });
    connect(templates, &QPushButton::clicked, this, &MainWindow::showTemplateMenu);
    connect(block, qOverload<int>(&QComboBox::activated), this, [this, block](int index) { m_editor->applyBlock(block->itemData(index).toString()); updateEditorControls(); m_editor->setFocus(); });
    connect(bold, &QToolButton::clicked, this, [this]() { QTextCharFormat format; format.setFontWeight(m_editor->fontWeight() >= QFont::Bold ? QFont::Normal : QFont::Bold); m_editor->mergeCurrentCharFormat(format); m_editor->setFocus(); }); connect(italic, &QToolButton::clicked, this, [this]() { m_editor->setFontItalic(!m_editor->fontItalic()); m_editor->setFocus(); }); connect(undo, &QToolButton::clicked, m_editor, &QTextEdit::undo); connect(redo, &QToolButton::clicked, m_editor, &QTextEdit::redo);
    connect(image, &QPushButton::clicked, this, [this]() { importMedia(QStringLiteral("image")); }); connect(video, &QPushButton::clicked, this, [this]() { importMedia(QStringLiteral("video")); });
    auto shortcutAction = [this](const QKeySequence &key, const std::function<void()> &callback, QWidget *context = nullptr) { auto *sc = new QShortcut(key, context ? context : this); if (context) sc->setContext(Qt::WidgetWithChildrenShortcut); connect(sc, &QShortcut::activated, this, callback); };
    shortcutAction(QKeySequence::Bold, [bold]() { bold->click(); }, m_editor);
    shortcutAction(QKeySequence::Italic, [italic]() { italic->click(); }, m_editor);
    shortcutAction(QKeySequence::Find, [this]() { showFindBar(); });
    shortcutAction(QKeySequence(Qt::CTRL | Qt::Key_H), [this]() { showFindBar(true); });
    shortcutAction(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I), [this]() { chooseMarkdownFiles(); });
    shortcutAction(QKeySequence(Qt::Key_F3), [this]() { if (!m_findBar->isVisible()) showFindBar(); else m_editor->findSearchMatch(); });
    shortcutAction(QKeySequence(Qt::SHIFT | Qt::Key_F3), [this]() { if (!m_findBar->isVisible()) showFindBar(); else m_editor->findSearchMatch(true); });
    for (int level = 0; level <= 3; ++level)
        shortcutAction(QKeySequence(Qt::CTRL | Qt::ALT | (Qt::Key_0 + level)), [this, level]() { m_editor->applyBlock(level == 0 ? QStringLiteral("paragraph") : QStringLiteral("h%1").arg(level)); updateEditorControls(); }, m_editor);
    shortcutAction(QKeySequence::New, [this]() { createNote(); }); shortcutAction(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), [this]() { createFolder(); }); shortcutAction(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), [this]() { if (m_focus) m_focusButton->click(); m_search->setFocus(); m_search->selectAll(); }); shortcutAction(QKeySequence::Save, [this]() { saveCurrent(); }); shortcutAction(QKeySequence(Qt::Key_F2), [this]() { renameSelected(); }, m_tree); shortcutAction(QKeySequence::Delete, [this]() { trashOrRestoreSelected(); }, m_tree); shortcutAction(QKeySequence(Qt::Key_Escape), [this]() { if (m_findBar->isVisible()) { closeFindBar(); return; } if (m_focus) m_focusButton->click(); m_outline->hide(); });
}

void MainWindow::applyAppearance()
{
    QPalette palette = qApp->palette();
    const QColor background(m_dark ? QStringLiteral("#22271f") : QStringLiteral("#ffffff"));
    const QColor foreground(m_dark ? QStringLiteral("#d8decf") : QStringLiteral("#30392d"));
    palette.setColor(QPalette::Window, background); palette.setColor(QPalette::Base, background);
    palette.setColor(QPalette::WindowText, foreground); palette.setColor(QPalette::Text, foreground);
    palette.setColor(QPalette::Button, background); palette.setColor(QPalette::ButtonText, foreground);
    qApp->setPalette(palette);
    qApp->setStyleSheet(AppTheme::styleSheet(m_dark, m_accent));
    const bool previousLoading = m_loading; m_loading = true;
    if (m_editor) {
        m_editor->setDarkMode(m_dark);
        QFont body(QStringLiteral("Microsoft YaHei UI")); body.setPixelSize(m_fontSize);
        m_editor->setFont(body); m_editor->document()->setDefaultFont(body);
    }
    if (auto *cover = static_cast<GardenCover *>(findChild<QWidget *>(QStringLiteral("cover")))) { cover->dark = m_dark; cover->update(); }
    m_loading = previousLoading;
}

QString MainWindow::selectedTreeId() const { return m_tree->currentItem() ? m_tree->currentItem()->data(0, IdRole).toString() : QString(); }

QTreeWidgetItem *MainWindow::findItem(const QString &id) const
{
    QTreeWidgetItemIterator iterator(m_tree);
    while (*iterator) { if ((*iterator)->data(0, IdRole).toString() == id) return *iterator; ++iterator; }
    return nullptr;
}

void MainWindow::selectTreeId(const QString &id)
{
    if (auto *item = findItem(id)) {
        for (auto *parent = item->parent(); parent; parent = parent->parent()) parent->setExpanded(true);
        m_tree->setCurrentItem(item); m_tree->scrollToItem(item);
        if (m_store.node(id).folder) showFolder(id); else showNote(id);
    }
}

void MainWindow::refreshTree(const QString &selectId)
{
    QStringList expanded;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) if ((*it)->isExpanded()) expanded << (*it)->data(0, IdRole).toString();
    const QSignalBlocker blocker(m_tree);
    m_tree->clear();
    const bool trash = m_view == View::Trash;
    QList<NoteNode> nodes = m_search->text().trimmed().isEmpty() ? m_store.nodes(trash) : m_store.search(m_search->text().trimmed(), trash);
    if (m_view == View::Favorites) {
        for (auto it = nodes.begin(); it != nodes.end();) { if (it->folder || !it->favorite) it = nodes.erase(it); else ++it; }
    }
    std::sort(nodes.begin(), nodes.end(), [](const NoteNode &a, const NoteNode &b) { if (a.folder != b.folder) return a.folder; return QString::localeAwareCompare(a.title, b.title) < 0; });
    const bool flat = m_view == View::Favorites || !m_search->text().trimmed().isEmpty();
    QHash<QString, QTreeWidgetItem *> items;
    for (const auto &node : nodes) {
        auto *item = new QTreeWidgetItem;
        item->setText(0, node.title);
        item->setData(0, IdRole, node.id); item->setData(0, FolderRole, node.folder);
        item->setIcon(0, AppTheme::icon(node.folder ? QStringLiteral("folder") : QStringLiteral("file"), node.favorite ? m_accent : QColor(m_dark ? "#a2af94" : "#8c9a7e")));
        item->setToolTip(0, folderPath(node.parentId) + QStringLiteral(" / ") + node.title);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | (node.folder ? Qt::ItemIsDropEnabled : Qt::NoItemFlags));
        item->setSizeHint(0, QSize(0, 32)); items.insert(node.id, item);
    }
    for (const auto &node : nodes) {
        auto *item = items.value(node.id);
        auto *parent = flat ? nullptr : items.value(node.parentId, nullptr);
        if (parent && parent != item && parent->data(0, FolderRole).toBool()) parent->addChild(item); else m_tree->addTopLevelItem(item);
    }
    for (const auto &node : nodes) {
        auto *item = items.value(node.id);
        if (node.folder && (expanded.contains(node.id) || !m_search->text().isEmpty())) item->setExpanded(true);
    }
    const QString target = selectId.isEmpty() ? m_noteId : selectId;
    if (items.contains(target)) {
        auto *item = items.value(target); for (auto *parent = item->parent(); parent; parent = parent->parent()) parent->setExpanded(true);
        m_tree->setCurrentItem(item); m_tree->scrollToItem(item);
    }
    QString heading = m_view == View::Trash ? QStringLiteral("回收站") : m_view == View::Favorites ? QStringLiteral("我的收藏") : QStringLiteral("笔记目录");
    if (!m_search->text().trimmed().isEmpty()) heading = QStringLiteral("搜索结果 · %1").arg(nodes.size());
    m_sectionLabel->setText(heading);
    m_tree->setDragDropMode(!flat && m_view == View::Library ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
}

void MainWindow::treeSelectionChanged()
{
    auto *item = m_tree->currentItem(); if (!item) return;
    const QString id = item->data(0, IdRole).toString();
    if (item->data(0, FolderRole).toBool()) showFolder(id); else showNote(id);
}

QString MainWindow::folderPath(const QString &id) const
{
    QStringList path; QString current = id; QSet<QString> seen;
    while (!current.isEmpty() && !seen.contains(current)) { seen.insert(current); const auto node = m_store.node(current); if (node.id.isEmpty()) break; path.prepend(node.title); current = node.parentId; }
    path.prepend(QStringLiteral("我的笔记")); return path.join(QStringLiteral(" / "));
}

QString MainWindow::currentFolder() const
{
    if (!m_noteId.isEmpty() && m_pages->currentWidget() == m_notePage) {
        const auto note = m_store.node(m_noteId);
        return note.trashed ? QString() : note.parentId;
    }
    const auto folder = m_store.node(m_folderId);
    return folder.folder && !folder.trashed ? folder.id : QString();
}

void MainWindow::updateNavigationContext()
{
    if (!m_noteId.isEmpty()) {
        const auto note = m_store.node(m_noteId); m_folderId = note.parentId;
        m_breadcrumb->setText(folderPath(note.parentId) + QStringLiteral(" / ") + note.title);
        m_breadcrumb->setToolTip(m_breadcrumb->text());
    } else showFolder(m_folderId);
}

void MainWindow::showNote(const QString &id)
{
    if (m_noteId == id && m_pages->currentWidget() == m_notePage) return;
    if (!saveCurrent()) { selectTreeId(m_noteId); return; }
    const auto node = m_store.node(id); if (node.id.isEmpty() || node.folder) return;
    m_loading = true; m_noteId = id; m_folderId = node.parentId;
    m_title->setText(node.title); m_title->setReadOnly(node.trashed);
    m_editor->setNoteHtml(node.html); m_editor->setReadOnly(node.trashed);
    m_favoriteButton->setChecked(node.favorite); m_favoriteButton->setEnabled(!node.trashed);
    m_breadcrumb->setText(folderPath(node.parentId) + QStringLiteral(" / ") + node.title);
    m_breadcrumb->setToolTip(m_breadcrumb->text());
    const QDateTime updated = QDateTime::fromString(node.updatedAt, Qt::ISODate);
    m_meta->setText(QStringLiteral("%1  ·  %2 字%3").arg(updated.isValid() ? updated.toLocalTime().toString(QStringLiteral("yyyy年M月d日 HH:mm")) : QStringLiteral("刚刚")).arg(node.plainText.size()).arg(node.trashed ? QStringLiteral("  ·  回收站内只读，右键可恢复") : QString()));
    m_saveLabel->setText(node.trashed ? QStringLiteral("回收站") : QStringLiteral("✓ 已保存"));
    findChild<QWidget *>(QStringLiteral("formattingToolbar"))->setEnabled(!node.trashed);
    m_pages->setCurrentWidget(m_notePage); m_dirty = false; m_loading = false;
    updateEditorControls();
    refreshAttachments(); setWindowTitle(node.title + QStringLiteral(" · 拾页"));
    updateFindStatus();
    m_settings->setValue(QStringLiteral("session/selectedNote"), id);
}

void MainWindow::showFolder(const QString &id)
{
    if (!saveCurrent()) return;
    m_noteId.clear(); m_folderId = id;
    if (id.isEmpty()) { const QSignalBlocker blocker(m_tree); m_tree->clearSelection(); m_tree->setCurrentItem(nullptr); }
    const auto folder = m_store.node(id);
    m_folderTitle->setText(id.isEmpty() ? (m_view == View::Trash ? QStringLiteral("回收站") : m_view == View::Favorites ? QStringLiteral("我的收藏") : QStringLiteral("我的笔记")) : folder.title);
    m_breadcrumb->setText(folderPath(id)); m_saveLabel->clear(); m_favoriteButton->setEnabled(false);
    m_folderList->clear();
    QList<NoteNode> entries; const auto all = m_store.nodes(m_view == View::Trash); QSet<QString> included;
    for (const auto &node : all) included.insert(node.id);
    for (const auto &node : all) {
        const bool rootTrashItem = m_view == View::Trash && id.isEmpty() && !included.contains(node.parentId);
        if (m_view == View::Favorites ? (!node.folder && node.favorite) : rootTrashItem || node.parentId == id) entries << node;
    }
    std::sort(entries.begin(), entries.end(), [](const NoteNode &a, const NoteNode &b) { if (a.folder != b.folder) return a.folder; return QString::localeAwareCompare(a.title, b.title) < 0; });
    for (const auto &node : entries) { auto *item = new QListWidgetItem(AppTheme::icon(node.folder ? QStringLiteral("folder") : QStringLiteral("file")), node.title, m_folderList); item->setData(IdRole, node.id); item->setSizeHint(QSize(0, 58)); }
    m_folderHint->setText(entries.isEmpty() ? QStringLiteral("这里还有很多空间。创建一篇笔记，或把文件拖入目录整理。") : QStringLiteral("%1 个项目 · 双击打开，右键目录可整理或移动").arg(entries.size()));
    m_pages->setCurrentWidget(m_folderPage); setWindowTitle(QStringLiteral("拾页 · ") + m_folderTitle->text());
}

void MainWindow::scheduleSave()
{
    if (m_loading || m_noteId.isEmpty() || m_editor->isReadOnly()) return;
    m_dirty = true; m_saveLabel->setText(QStringLiteral("正在保存…")); m_saveTimer.start();
    if (!m_saveDeadlineTimer.isActive()) m_saveDeadlineTimer.start();
}

bool MainWindow::saveCurrent()
{
    if (m_saving) return false;
    QScopedValueRollback<bool> saving(m_saving, true);
    m_saveTimer.stop();
    m_saveDeadlineTimer.stop();
    if (!m_dirty || m_noteId.isEmpty() || m_loading) return true;
    const QString title = m_title->text().trimmed().isEmpty() ? QStringLiteral("无标题笔记") : m_title->text().trimmed();
    if (!m_store.updateNote(m_noteId, title, m_editor->noteHtml(), m_editor->toPlainText())) { m_saveLabel->setText(QStringLiteral("保存失败")); showError(m_store.errorString()); return false; }
    m_dirty = false; m_saveLabel->setText(QStringLiteral("✓ 已保存"));
    if (auto *item = findItem(m_noteId)) item->setText(0, title);
    const auto node = m_store.node(m_noteId); m_breadcrumb->setText(folderPath(node.parentId) + QStringLiteral(" / ") + title);
    m_meta->setText(QStringLiteral("%1  ·  %2 字").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy年M月d日 HH:mm"))).arg(m_editor->toPlainText().size()));
    setWindowTitle(title + QStringLiteral(" · 拾页")); return true;
}

void MainWindow::createNote(const QString &templateName)
{
    if (!saveCurrent()) return;
    const QString parent = m_view == View::Trash ? QString() : currentFolder();
    QString title = QStringLiteral("无标题笔记"); QString html;
    if (templateName == QStringLiteral("reading")) { title = QStringLiteral("读书笔记 · 一本新书"); html = QStringLiteral("<p>书名：<br>作者：</p><h2>这本书在回答什么问题？</h2><p></p><h2>让我停下来思考的观点</h2><blockquote>记下一段触动你的文字，也写下自己的理解。</blockquote><h2>我想尝试的行动</h2><p>☐ 用一个小行动，实践书中的想法</p>"); }
    else if (templateName == QStringLiteral("meeting")) { title = QStringLiteral("会议记录 · ") + QDate::currentDate().toString(QStringLiteral("M月d日")); html = QStringLiteral("<p>时间：%1<br>参与人：<br>目标：</p><h2>讨论要点</h2><p></p><h2>达成的共识</h2><p></p><h2>下一步行动</h2><p>☐ 行动事项 · 负责人 · 完成时间</p>").arg(QDate::currentDate().toString(Qt::ISODate)); }
    else if (templateName == QStringLiteral("project")) { title = QStringLiteral("项目计划 · 从一个想法开始"); html = QStringLiteral("<p>用一句话，说明这个项目要解决的问题。</p><h2>目标与预期结果</h2><p></p><h2>里程碑</h2><p>☐ 明确需求与范围</p><p>☐ 完成第一版</p><p>☐ 收集反馈并迭代</p><h2>资料与想法</h2><p></p>"); }
    if (html.isEmpty()) html = QStringLiteral("<p></p>");
    QTextDocument plain; plain.setHtml(html);
    const QString id = m_store.createNote(title, parent, html, plain.toPlainText());
    if (id.isEmpty()) { showError(m_store.errorString()); return; }
    m_view = View::Library; m_search->clear(); refreshTree(id); showNote(id); m_title->setFocus(); m_title->selectAll(); status(QStringLiteral("已创建笔记"));
}

void MainWindow::createFolder()
{
    if (!saveCurrent()) return;
    bool accepted = false; const QString title = QInputDialog::getText(this, QStringLiteral("新建文件夹"), QStringLiteral("文件夹名称"), QLineEdit::Normal, QStringLiteral("新文件夹"), &accepted).trimmed();
    if (!accepted || title.isEmpty()) return;
    const QString id = m_store.createFolder(title, m_view == View::Trash ? QString() : currentFolder());
    if (id.isEmpty()) { showError(m_store.errorString()); return; }
    m_view = View::Library; m_search->clear(); refreshTree(id); selectTreeId(id); status(QStringLiteral("已创建文件夹，可以继续建立子目录"));
}

void MainWindow::createDailyNote()
{
    if (!saveCurrent()) return;
    QString dailyFolder;
    for (const auto &node : m_store.nodes()) if (node.folder && node.parentId.isEmpty() && node.title == QStringLiteral("日记")) { dailyFolder = node.id; break; }
    if (dailyFolder.isEmpty()) dailyFolder = m_store.createFolder(QStringLiteral("日记"));
    if (dailyFolder.isEmpty()) { showError(m_store.errorString()); return; }
    const QString title = QDate::currentDate().toString(QStringLiteral("yyyy年M月d日"));
    QString id;
    for (const auto &node : m_store.nodes()) if (!node.folder && node.parentId == dailyFolder && node.title == title) { id = node.id; break; }
    if (id.isEmpty()) {
        const QString html = QStringLiteral("<p>今天，也有值得记下的小事。</p><h2>今天的心情</h2><p></p><h2>值得记住的瞬间</h2><p></p><h2>今天的小计划</h2><p>☐ 给自己留一点专注的时间</p><p>☐ 记录一个新的发现</p><h2>写给明天的自己</h2><p></p>");
        QTextDocument plain; plain.setHtml(html); id = m_store.createNote(title, dailyFolder, html, plain.toPlainText());
    }
    if (id.isEmpty()) { showError(m_store.errorString()); return; }
    m_view = View::Library; m_search->clear(); refreshTree(id); showNote(id); m_editor->setFocus(); status(QStringLiteral("已打开今天的日记"));
}

void MainWindow::renameSelected()
{
    if (!saveCurrent()) return;
    const auto node = m_store.node(selectedTreeId()); if (node.id.isEmpty() || node.trashed) return;
    bool accepted = false; const QString title = QInputDialog::getText(this, QStringLiteral("重命名"), QStringLiteral("名称"), QLineEdit::Normal, node.title, &accepted).trimmed();
    if (!accepted || title.isEmpty()) return;
    if (!m_store.renameNode(node.id, title)) { showError(m_store.errorString()); return; }
    refreshTree(node.id);
    if (node.folder) showFolder(node.id); else { m_noteId.clear(); showNote(node.id); }
}

void MainWindow::moveSelected()
{
    if (!saveCurrent()) return;
    const auto node = m_store.node(selectedTreeId()); if (node.id.isEmpty() || node.trashed) return;
    QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("移动到文件夹")); dialog.resize(390, 440); auto *layout = new QVBoxLayout(&dialog); auto *tree = new QTreeWidget(&dialog); tree->setHeaderHidden(true); tree->setIndentation(18); layout->addWidget(tree);
    auto *root = new QTreeWidgetItem(tree, {QStringLiteral("我的笔记（根目录）")}); root->setData(0, IdRole, QString()); root->setIcon(0, AppTheme::icon(QStringLiteral("folder")));
    QHash<QString, QTreeWidgetItem *> folders; folders.insert(QString(), root);
    const auto nodes = m_store.nodes(); for (const auto &entry : nodes) if (entry.folder) { auto *item = new QTreeWidgetItem({entry.title}); item->setData(0, IdRole, entry.id); item->setIcon(0, AppTheme::icon(QStringLiteral("folder"))); folders.insert(entry.id, item); }
    for (const auto &entry : nodes) if (entry.folder) folders.value(entry.parentId, root)->addChild(folders.value(entry.id));
    tree->expandAll(); tree->setCurrentItem(folders.value(node.parentId, root));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); layout->addWidget(buttons); connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || !tree->currentItem()) return;
    if (!m_store.moveNode(node.id, tree->currentItem()->data(0, IdRole).toString())) { showError(m_store.errorString()); return; }
    refreshTree(node.id); updateNavigationContext(); status(QStringLiteral("已移动"));
}

void MainWindow::trashOrRestoreSelected()
{
    if (!saveCurrent()) return;
    const QString id = selectedTreeId().isEmpty() ? m_noteId : selectedTreeId(); const auto node = m_store.node(id); if (node.id.isEmpty()) return;
    if (!m_store.setTrashed(id, !node.trashed)) { showError(m_store.errorString()); return; }
    m_noteId.clear(); m_dirty = false; refreshTree(); showFolder(QString()); status(node.trashed ? QStringLiteral("已恢复笔记及其目录内容") : QStringLiteral("已移入回收站，右键可恢复"));
}

void MainWindow::setView(View view)
{
    if (!saveCurrent()) return;
    m_view = view; m_search->clear(); refreshTree(); showFolder(QString());
}

void MainWindow::showTreeMenu(const QPoint &position)
{
    auto *item = m_tree->itemAt(position); if (item) m_tree->setCurrentItem(item); else showFolder(QString());
    const auto node = m_store.node(item ? item->data(0, IdRole).toString() : QString());
    QMenu menu(this);
    if (m_view == View::Trash) { if (!node.id.isEmpty()) menu.addAction(AppTheme::icon(QStringLiteral("restore")), QStringLiteral("恢复"), this, [this]() { trashOrRestoreSelected(); }); }
    else {
        menu.addAction(AppTheme::icon(QStringLiteral("plus")), QStringLiteral("新建笔记"), this, [this]() { createNote(); }); menu.addAction(AppTheme::icon(QStringLiteral("new-folder")), QStringLiteral("新建文件夹"), this, [this]() { createFolder(); });
        if (!node.id.isEmpty()) {
            menu.addSeparator(); menu.addAction(QStringLiteral("重命名…"), this, [this]() { renameSelected(); }); menu.addAction(QStringLiteral("移动到…"), this, [this]() { moveSelected(); });
            if (!node.folder) menu.addAction(node.favorite ? QStringLiteral("取消收藏") : QStringLiteral("加入收藏"), this, [this]() { m_favoriteButton->click(); });
            menu.addSeparator(); menu.addAction(AppTheme::icon(QStringLiteral("trash")), QStringLiteral("移入回收站"), this, [this]() { trashOrRestoreSelected(); });
        }
    }
    if (!menu.isEmpty()) menu.exec(m_tree->viewport()->mapToGlobal(position));
}

void MainWindow::importMedia(const QString &kind)
{
    if (m_view == View::Trash || (!m_noteId.isEmpty() && m_store.node(m_noteId).trashed)) { status(QStringLiteral("请先恢复笔记，再导入媒体")); return; }
    const QString filter = kind == QStringLiteral("image")
        ? QStringLiteral("图片 (*.png *.jpg *.jpeg *.webp *.gif *.bmp);;所有文件 (*)")
        : kind == QStringLiteral("video") ? QStringLiteral("视频 (*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.wmv);;所有文件 (*)") : QStringLiteral("所有文件 (*)");
    const auto files = QFileDialog::getOpenFileNames(this, kind == QStringLiteral("video") ? QStringLiteral("导入视频") : QStringLiteral("导入图片"), m_settings->value(QStringLiteral("files/lastDirectory"), QDir::homePath()).toString(), filter);
    if (files.isEmpty()) return;
    m_settings->setValue(QStringLiteral("files/lastDirectory"), QFileInfo(files.first()).absolutePath());
    importFiles(files);
}

void MainWindow::importFiles(const QStringList &paths)
{
    if (paths.isEmpty() || m_view == View::Trash || (!m_noteId.isEmpty() && m_store.node(m_noteId).trashed)) return;
    QStringList markdown, other;
    for (const auto &path : paths) {
        const QString suffix = QFileInfo(path).suffix().toLower();
        (suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown") ? markdown : other).append(path);
    }
    if (!markdown.isEmpty()) { importMarkdownFiles(markdown); if (!other.isEmpty()) importFiles(other); return; }
    if (!saveCurrent()) return;
    if (m_noteId.isEmpty()) { createNote(); if (m_noteId.isEmpty()) return; m_title->setText(QFileInfo(paths.first()).completeBaseName()); m_dirty = true; }
    QProgressDialog progress(QStringLiteral("正在把媒体复制到资料库…"), QString(), 0, paths.size(), this); progress.setWindowTitle(QStringLiteral("导入媒体")); progress.setWindowModality(Qt::WindowModal); progress.setMinimumDuration(0); progress.setCancelButton(nullptr);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int imported = 0; QStringList errors;
    for (int index = 0; index < paths.size(); ++index) {
        progress.setValue(index); progress.setLabelText(QStringLiteral("正在导入 %1").arg(QFileInfo(paths[index]).fileName())); QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QString error; const auto attachment = m_store.importAttachment(m_noteId, paths[index], &error);
        if (!attachment.isValid()) { errors << QFileInfo(paths[index]).fileName() + QStringLiteral("：") + error; continue; }
        if (attachment.mime.startsWith(QStringLiteral("image/"))) m_editor->insertImage(attachment.relativePath, attachment.originalName);
        else if (isVideo(attachment)) m_editor->insertVideo(attachment.id, attachment.originalName);
        else m_editor->insertAttachment(attachment.id, attachment.originalName);
        ++imported;
    }
    progress.setValue(paths.size()); progress.close(); QApplication::restoreOverrideCursor();
    if (imported) { m_dirty = true; saveCurrent(); refreshAttachments(); m_editor->setFocus(); status(QStringLiteral("已导入 %1 个文件，媒体副本已保存在资料库").arg(imported)); }
    if (!errors.isEmpty()) showError(errors.join(QLatin1Char('\n')));
}

void MainWindow::chooseMarkdownFiles()
{
    if (m_view == View::Trash) { status(QStringLiteral("请先切换到笔记目录再导入。")); return; }
    const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("导入 Markdown（原文件与本地附件会一同保存）"), QString(), QStringLiteral("Markdown (*.md *.markdown);;所有文件 (*)"));
    importMarkdownFiles(files);
}

void MainWindow::importMarkdownFiles(const QStringList &paths)
{
    if (paths.isEmpty() || m_view == View::Trash || !saveCurrent()) return;
    const QString parent = currentFolder();
    QString last; QStringList messages; int imported = 0;
    QProgressDialog progress(QStringLiteral("正在导入 Markdown 与本地附件…"), QString(), 0, paths.size(), this);
    progress.setWindowModality(Qt::WindowModal); progress.setCancelButton(nullptr); progress.setMinimumDuration(0);
    for (int i = 0; i < paths.size(); ++i) {
        progress.setValue(i); progress.setLabelText(QStringLiteral("正在导入 %1").arg(QFileInfo(paths.at(i)).fileName()));
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        const auto result = MarkdownImporter::importFile(m_store, paths.at(i), parent);
        if (result.noteId.isEmpty()) messages << QFileInfo(paths.at(i)).fileName() + QStringLiteral("：导入失败 — ") + result.error;
        else { last = result.noteId; ++imported; for (const auto &warning : result.warnings) messages << QFileInfo(paths.at(i)).fileName() + QStringLiteral("：") + warning; }
    }
    progress.close();
    if (!last.isEmpty()) { m_view = View::Library; m_search->clear(); refreshTree(last); showNote(last); }
    status(QStringLiteral("已导入 %1/%2 篇笔记；原始 Markdown 可在附件中找到。").arg(imported).arg(paths.size()));
    if (!messages.isEmpty()) {
        QMessageBox report(QMessageBox::Information, QStringLiteral("Markdown 导入结果"), QStringLiteral("已导入 %1/%2 篇。部分内容需要查看，详细说明如下。\n原始 Markdown 会随成功导入的笔记保存。").arg(imported).arg(paths.size()), QMessageBox::Ok, this);
        report.setDetailedText(messages.join(QLatin1Char('\n'))); report.exec();
    }
}

void MainWindow::showFindBar(bool replace)
{
    if (m_noteId.isEmpty() || m_pages->currentWidget() != m_notePage) { status(QStringLiteral("先打开一篇笔记，再使用文内查找。")); return; }
    const QString selected = m_editor->textCursor().selectedText();
    m_findBar->show(); m_replaceRow->setVisible(replace);
    if (!selected.isEmpty() && selected.size() < 256 && !selected.contains(QChar::ParagraphSeparator) && !selected.contains(QChar::ObjectReplacementCharacter)) m_findText->setText(selected);
    m_editor->setSearch(m_findText->text(), m_findCase->isChecked(), m_findWholeWords->isChecked());
    m_findText->setFocus(); m_findText->selectAll(); updateFindStatus();
}

void MainWindow::closeFindBar()
{
    m_findBar->hide(); m_editor->setSearch(QString()); m_editor->setFocus();
}

void MainWindow::updateFindStatus()
{
    const int total = m_editor->searchMatchCount(), current = m_editor->currentSearchMatch();
    m_findStatus->setText(m_findText->text().isEmpty() ? QStringLiteral("输入关键词") : m_editor->searchLimitReached() ? QStringLiteral("超过 10000 项") : total == 0 ? QStringLiteral("无匹配") : QStringLiteral("%1 / %2").arg(current).arg(total));
    m_findStatus->setToolTip(m_editor->searchLimitReached() ? QStringLiteral("匹配过多，请缩小关键词后再全部替换。") : QStringLiteral("高亮最多显示前 1000 项；查找支持首尾循环。"));
    m_replaceOne->setEnabled(!m_editor->isReadOnly() && total > 0);
    m_replaceAll->setEnabled(!m_editor->isReadOnly() && total > 0 && !m_editor->searchLimitReached());
}

void MainWindow::importPastedImage(const QImage &image)
{
    if (image.isNull() || m_editor->isReadOnly()) return;
    QTemporaryFile file(QDir(m_store.dataDirectory()).filePath(QStringLiteral(".paste-XXXXXX.png")));
    if (!file.open()) { showError(QStringLiteral("无法建立图片临时文件")); return; }
    const QString path = file.fileName(); file.close();
    if (!image.save(path, "PNG")) { showError(QStringLiteral("无法保存剪贴板图片")); return; }
    importFiles({path});
}

void MainWindow::refreshAttachments()
{
    m_attachmentList->clear(); const auto attachments = m_store.attachments(m_noteId);
    for (const auto &attachment : attachments) {
        const QString kind = isVideo(attachment) ? QStringLiteral("video") : attachment.mime.startsWith(QStringLiteral("image/")) ? QStringLiteral("image") : QStringLiteral("file");
        auto *item = new QListWidgetItem(AppTheme::icon(kind), attachment.originalName + QStringLiteral("   ·   ") + humanSize(attachment.size), m_attachmentList); item->setData(IdRole, attachment.id); item->setToolTip(QStringLiteral("双击打开 · 副本保存在资料库")); item->setSizeHint(QSize(0, 31));
    }
    m_attachmentToggle->setText(QStringLiteral("附件 · %1").arg(attachments.size())); m_attachmentToggle->setVisible(!attachments.isEmpty()); m_attachmentList->setVisible(!attachments.isEmpty() && m_attachmentToggle->isChecked()); m_attachmentList->setFixedHeight(qMin(3, attachments.size()) * 31 + 4);
}

void MainWindow::previewImage(const QString &relativePath)
{
    for (const auto &attachment : m_store.attachments(m_noteId)) {
        if (attachment.relativePath == relativePath && attachment.mime.startsWith(QStringLiteral("image/"))) {
            openAttachment(attachment.id);
            return;
        }
    }
}

void MainWindow::openAttachment(const QString &id)
{
    const auto attachment = m_store.attachment(id); if (!attachment.isValid()) { showError(QStringLiteral("没有找到这个附件")); return; }
    const QString path = QDir(m_store.dataDirectory()).filePath(attachment.relativePath);
    if (!QFileInfo::exists(path)) { showError(QStringLiteral("附件文件不存在，请从完整备份恢复：\n") + attachment.originalName); return; }
    if (isVideo(attachment)) { auto *dialog = new MediaPlayerDialog(path, this); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->show(); return; }
    if (attachment.mime.startsWith(QStringLiteral("image/"))) {
        auto *dialog = new ImagePreviewDialog(path, attachment.originalName, this); dialog->show(); return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::exportNote(bool html)
{
    if (m_noteId.isEmpty() || !saveCurrent()) return;
    const auto node = m_store.node(m_noteId);
    const QString path = QFileDialog::getSaveFileName(this, html ? QStringLiteral("导出 HTML") : QStringLiteral("导出 Markdown"), safeFileName(node.title) + (html ? QStringLiteral(".html") : QStringLiteral(".md")), html ? QStringLiteral("HTML (*.html)") : QStringLiteral("Markdown (*.md)"));
    if (path.isEmpty()) return;
    const QFileInfo output(path); const QString assetsName = output.completeBaseName() + QStringLiteral(".assets"); const QString assetsDirectory = output.dir().filePath(assetsName);
    QHash<QString, QString> references;
    for (const auto &attachment : m_store.attachments(m_noteId)) {
        if (!QDir().mkpath(assetsDirectory)) { showError(QStringLiteral("无法建立附件导出目录")); return; }
        const QString name = QFileInfo(attachment.relativePath).fileName(); const QString target = QDir(assetsDirectory).filePath(name); const QString source = QDir(m_store.dataDirectory()).filePath(attachment.relativePath);
        if (!QFileInfo::exists(source) || (!QFileInfo::exists(target) && !QFile::copy(source, target))) { showError(QStringLiteral("附件导出失败：") + attachment.originalName); return; }
        const QString reference = QString::fromLatin1(QUrl::toPercentEncoding(assetsName + QLatin1Char('/') + name, "/"));
        references.insert(attachment.relativePath, reference); references.insert(QUrl::fromLocalFile(source).toString(), reference); references.insert(QStringLiteral("attachment:") + attachment.id, reference);
    }
    QString content;
    if (html) {
        content = m_editor->noteHtml();
        for (auto it = references.cbegin(); it != references.cend(); ++it) content.replace(it.key(), it.value());
        const int body = content.indexOf(QLatin1Char('>'), content.indexOf(QStringLiteral("<body")));
        if (body >= 0) content.insert(body + 1, QStringLiteral("<h1>") + node.title.toHtmlEscaped() + QStringLiteral("</h1>"));
    } else {
        content = MarkdownExporter::fromDocument(m_editor->document(), node.title, references);
    }
    QSaveFile file(path); const QByteArray bytes = content.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { showError(QStringLiteral("导出失败：") + file.errorString()); return; }
    status(QStringLiteral("已导出笔记与附件：") + QDir::toNativeSeparators(path));
}

void MainWindow::createBackup()
{
    if (!saveCurrent()) return;
    const QString parent = QFileDialog::getExistingDirectory(this, QStringLiteral("选择备份保存位置")); if (parent.isEmpty()) return;
    const QString target = QDir(parent).filePath(QStringLiteral("拾页备份-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QProgressDialog progress(QStringLiteral("正在备份笔记和全部媒体…"), QString(), 0, 0, this); progress.setWindowModality(Qt::WindowModal); progress.setCancelButton(nullptr); progress.show(); QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QString error; const bool ok = m_store.backupTo(target, &error); progress.close();
    if (!ok) { showError(error); return; } status(QStringLiteral("完整备份已保存：") + QDir::toNativeSeparators(target));
}

void MainWindow::restoreBackup()
{
    if (!saveCurrent()) return;
    const QString source = QFileDialog::getExistingDirectory(this, QStringLiteral("选择包含 notes.sqlite 与 attachments 的备份目录")); if (source.isEmpty()) return;
    if (QMessageBox::question(this, QStringLiteral("恢复资料库"), QStringLiteral("恢复此备份：\n%1\n\n当前资料库会先保存一份恢复前备份。是否继续？").arg(QDir::toNativeSeparators(source))) != QMessageBox::Yes) return;
    QProgressDialog progress(QStringLiteral("正在验证并恢复完整资料库…"), QString(), 0, 0, this); progress.setWindowModality(Qt::WindowModal); progress.setCancelButton(nullptr); progress.show(); QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QString error; const bool ok = m_store.restoreFrom(source, &error); progress.close();
    if (!ok) { showError(error); return; }
    m_noteId.clear(); m_folderId.clear(); m_dirty = false; m_view = View::Library; m_search->clear(); refreshTree(); showFolder(QString()); status(QStringLiteral("已恢复资料库，恢复前的资料保存在资料库 backups 文件夹"));
}

void MainWindow::showHistory()
{
    if (m_noteId.isEmpty()) { status(QStringLiteral("先打开一篇笔记，再查看历史版本。")); return; }
    if (!saveCurrent()) return;
    const QString noteId = m_noteId;
    QDialog dialog(this); dialog.setObjectName(QStringLiteral("historyDialog")); dialog.setWindowTitle(QStringLiteral("笔记历史版本")); dialog.resize(960, 660);
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(QStringLiteral("每次保存内容变化时保留上一版。恢复前，当前内容也会存入历史。"), &dialog); hint->setWordWrap(true); layout->addWidget(hint);
    auto *splitter = new QSplitter(&dialog); layout->addWidget(splitter, 1);
    auto *list = new QListWidget(splitter); list->setObjectName(QStringLiteral("revisionList"));
    auto *preview = new NoteEditor(splitter); preview->setObjectName(QStringLiteral("revisionPreview")); preview->setLibraryDirectory(m_store.dataDirectory()); preview->setDarkMode(m_dark); preview->setReadOnly(true);
    splitter->setStretchFactor(0, 1); splitter->setStretchFactor(1, 2);
    auto *row = new QHBoxLayout; layout->addLayout(row);
    auto *more = new QPushButton(QStringLiteral("加载更早版本"), &dialog); row->addWidget(more); row->addStretch();
    auto *restore = new QPushButton(QStringLiteral("恢复所选版本"), &dialog); restore->setObjectName(QStringLiteral("restoreRevisionButton")); restore->setEnabled(false); row->addWidget(restore);
    auto *close = new QPushButton(QStringLiteral("关闭"), &dialog); row->addWidget(close); connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    auto loadMore = [&, this]() {
        const auto revisions = m_store.revisions(noteId, 100, list->count());
        for (const auto &r : revisions) {
            const QString date = QDateTime::fromString(r.savedAt, Qt::ISODateWithMs).toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
            auto *item = new QListWidgetItem(date + QLatin1Char('\n') + r.title, list); item->setData(Qt::UserRole, r.id);
        }
        more->setEnabled(revisions.size() == 100);
        if (list->count() == 0) hint->setText(QStringLiteral("还没有历史版本。下一次修改并保存后，这一版内容会自动保留。"));
        if (!list->currentItem() && list->count()) list->setCurrentRow(0);
    };
    connect(list, &QListWidget::currentRowChanged, &dialog, [&, this]() {
        const auto *item = list->currentItem();
        const auto r = item ? m_store.revision(item->data(Qt::UserRole).toString()) : NoteRevision();
        preview->setNoteHtml(r.html); restore->setEnabled(!r.id.isEmpty() && !m_store.node(noteId).trashed);
    });
    connect(more, &QPushButton::clicked, &dialog, loadMore);
    connect(restore, &QPushButton::clicked, &dialog, [&, this]() {
        if (!list->currentItem()) return;
        if (!m_store.restoreRevision(noteId, list->currentItem()->data(Qt::UserRole).toString())) { showError(m_store.errorString()); return; }
        m_noteId.clear(); refreshTree(noteId); showNote(noteId); dialog.accept(); status(QStringLiteral("已恢复所选版本；恢复前的内容仍可在历史版本中找回。"));
    });
    loadMore(); dialog.exec();
}

void MainWindow::updateBackupStatus(const QString &failure)
{
    if (!failure.isEmpty()) {
        m_backupLabel->setText(QStringLiteral("⚠ 自动备份失败")); m_backupLabel->setToolTip(failure);
        m_settings->setValue(QStringLiteral("backup/lastError"), failure); return;
    }
    if (!m_settings->value(QStringLiteral("backup/enabled"), true).toBool()) {
        m_backupLabel->setText(QStringLiteral("自动备份已关闭")); m_backupLabel->setToolTip(QStringLiteral("可在“数据保护与自动备份”中开启。")); return;
    }
    const QString previousError = m_settings->value(QStringLiteral("backup/lastError")).toString();
    if (!previousError.isEmpty()) { m_backupLabel->setText(QStringLiteral("⚠ 自动备份失败")); m_backupLabel->setToolTip(previousError); return; }
    const auto date = QDateTime::fromString(m_settings->value(QStringLiteral("backup/lastSuccess")).toString(), Qt::ISODateWithMs);
    m_backupLabel->setText(date.isValid() ? QStringLiteral("备份 %1").arg(date.toLocalTime().toString(QStringLiteral("MM-dd HH:mm"))) : QStringLiteral("自动备份待完成"));
    m_backupLabel->setToolTip(QStringLiteral("启动后及每 30 分钟检查一次；有修改时备份，保留最近 10 份。\n") + m_settings->value(QStringLiteral("backup/lastPath")).toString());
}

bool MainWindow::runAutomaticBackup(bool force)
{
    if (m_backupBusy) return false;
    if (!force && (!m_settings->value(QStringLiteral("backup/enabled"), true).toBool() || (!m_needsBackup && !m_dirty))) return true;
    if (!saveCurrent()) { updateBackupStatus(QStringLiteral("笔记尚未保存成功，自动备份已暂停。")); return false; }
    m_backupBusy = true;
    const QString root = m_settings->value(QStringLiteral("backup/directory"), QDir(m_store.dataDirectory()).filePath(QStringLiteral("backups/automatic"))).toString();
    QProgressDialog progress(QStringLiteral("正在备份并校验笔记、历史版本和附件…"), QString(), 0, 0, this);
    progress.setWindowModality(Qt::ApplicationModal); progress.setCancelButton(nullptr); progress.show();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QString path, error; const bool ok = m_store.automaticBackup(root, &path, &error); progress.close(); m_backupBusy = false;
    if (!ok) { updateBackupStatus(error); status(QStringLiteral("自动备份失败，请在“数据保护与自动备份”中检查目录。")); return false; }
    m_needsBackup = false; m_settings->remove(QStringLiteral("backup/lastError"));
    m_settings->setValue(QStringLiteral("backup/lastSuccess"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    m_settings->setValue(QStringLiteral("backup/lastPath"), path); m_settings->sync(); updateBackupStatus(); return true;
}

void MainWindow::showDataProtection()
{
    if (!saveCurrent()) return;
    QDialog dialog(this); dialog.setObjectName(QStringLiteral("dataProtectionDialog")); dialog.setWindowTitle(QStringLiteral("数据保护与自动备份")); dialog.resize(620, 380);
    auto *layout = new QVBoxLayout(&dialog);
    auto *enabled = new QCheckBox(QStringLiteral("启用自动备份（启动后及每 30 分钟检查，有修改时备份）"), &dialog);
    enabled->setChecked(m_settings->value(QStringLiteral("backup/enabled"), true).toBool()); layout->addWidget(enabled);
    auto *description = new QLabel(QStringLiteral("每份备份包含笔记、历史版本和全部附件，并校验文件内容。保留最近 10 份自动备份，手动备份与恢复前备份单独保留。"), &dialog); description->setWordWrap(true); layout->addWidget(description);
    auto *row = new QHBoxLayout; layout->addLayout(row);
    auto *directory = new QLineEdit(m_settings->value(QStringLiteral("backup/directory"), QDir(m_store.dataDirectory()).filePath(QStringLiteral("backups/automatic"))).toString(), &dialog); directory->setReadOnly(true); row->addWidget(directory, 1);
    auto *browse = new QPushButton(QStringLiteral("选择目录…"), &dialog); row->addWidget(browse);
    connect(browse, &QPushButton::clicked, &dialog, [&]() { const QString path = QFileDialog::getExistingDirectory(&dialog, QStringLiteral("选择独立的备份目录"), directory->text()); if (!path.isEmpty()) directory->setText(path); });
    auto *advice = new QLabel(QStringLiteral("默认备份与资料库在同一磁盘。长期存放知识，建议把备份目录设在另一块磁盘或外置硬盘，以便磁盘故障后仍能恢复。"), &dialog); advice->setWordWrap(true); layout->addWidget(advice);
    auto *result = new QLabel(m_backupLabel->text() + QLatin1Char('\n') + m_backupLabel->toolTip(), &dialog); result->setWordWrap(true); result->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(result);
    auto saveOptions = [&, this]() {
        if (m_settings->value(QStringLiteral("backup/directory"), QDir(m_store.dataDirectory()).filePath(QStringLiteral("backups/automatic"))).toString() != directory->text()) {
            m_needsBackup = true; m_settings->remove(QStringLiteral("backup/lastSuccess"));
            m_settings->remove(QStringLiteral("backup/lastPath")); m_settings->remove(QStringLiteral("backup/lastError"));
        }
        m_settings->setValue(QStringLiteral("backup/enabled"), enabled->isChecked());
        m_settings->setValue(QStringLiteral("backup/directory"), directory->text()); m_settings->sync(); updateBackupStatus();
    };
    auto *actions = new QHBoxLayout; layout->addLayout(actions);
    auto *backup = new QPushButton(QStringLiteral("保存设置并立即备份"), &dialog); actions->addWidget(backup);
    connect(backup, &QPushButton::clicked, &dialog, [&, this]() { saveOptions(); runAutomaticBackup(true); result->setText(m_backupLabel->text() + QLatin1Char('\n') + m_backupLabel->toolTip()); });
    auto *check = new QPushButton(QStringLiteral("检查资料库"), &dialog); actions->addWidget(check);
    connect(check, &QPushButton::clicked, &dialog, [&, this]() { QString error; result->setText(m_store.checkIntegrity(&error) ? QStringLiteral("检查通过：数据库与历史版本关系正常，附件均存在且大小一致。备份恢复时还会校验文件内容。") : error); });
    auto *open = new QPushButton(QStringLiteral("打开备份目录"), &dialog); actions->addWidget(open);
    connect(open, &QPushButton::clicked, &dialog, [&]() { QDesktopServices::openUrl(QUrl::fromLocalFile(directory->text())); });
    layout->addStretch(); auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog); layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() { saveOptions(); dialog.accept(); }); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void MainWindow::checkUpdates()
{
    if (m_updater->busy()) return;
    const QString repo = m_settings->value(QStringLiteral("updates/repository"), QStringLiteral("ricarrdooo/PersonalNote")).toString();
    m_updateMessage = QStringLiteral("正在检查 GitHub 正式发布版本…"); m_updateDirectory.clear(); m_updateVersion.clear(); m_updateAvailable = false;
    m_updateButton->setText(QStringLiteral("检查更新…"));
    m_settings->setValue(QStringLiteral("updates/lastCheck"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    m_updater->check(repo); emit updateStateChanged();
}

void MainWindow::showUpdates()
{
    QDialog dialog(this); dialog.setObjectName(QStringLiteral("updatesDialog")); dialog.setWindowTitle(QStringLiteral("拾页 · 软件更新")); dialog.resize(650, 510);
    auto *layout = new QVBoxLayout(&dialog);
    auto *heading = new QLabel(QStringLiteral("当前版本 %1 · Windows 64 位").arg(QCoreApplication::applicationVersion()), &dialog); layout->addWidget(heading);
    auto *form = new QFormLayout;
    auto *repository = new QLineEdit(m_settings->value(QStringLiteral("updates/repository"), QStringLiteral("ricarrdooo/PersonalNote")).toString(), &dialog); repository->setObjectName(QStringLiteral("updateRepository")); form->addRow(QStringLiteral("GitHub 公开仓库"), repository); layout->addLayout(form);
    auto *automatic = new QCheckBox(QStringLiteral("启动后自动检查更新（最多每 4 小时一次）"), &dialog); automatic->setChecked(m_settings->value(QStringLiteral("updates/automatic"), true).toBool()); layout->addWidget(automatic);
    auto *message = new QLabel(&dialog); message->setObjectName(QStringLiteral("updateMessage")); message->setWordWrap(true); layout->addWidget(message);
    auto *notes = new QPlainTextEdit(&dialog); notes->setObjectName(QStringLiteral("releaseNotes")); notes->setReadOnly(true); notes->setPlaceholderText(QStringLiteral("发现新版本后，这里显示 GitHub Release 的更新说明。")); layout->addWidget(notes, 1);
    auto *progress = new QProgressBar(&dialog); progress->hide(); layout->addWidget(progress);
    auto *hint = new QLabel(QStringLiteral("更新前会保存并完整备份资料。安装只替换程序文件，更新完成后会重新打开当前资料库。"), &dialog); hint->setWordWrap(true); layout->addWidget(hint);
    auto *row = new QHBoxLayout; layout->addLayout(row);
    auto *check = new QPushButton(QStringLiteral("保存设置并检查"), &dialog); row->addWidget(check);
    auto *download = new QPushButton(QStringLiteral("下载更新"), &dialog); row->addWidget(download);
    auto *install = new QPushButton(QStringLiteral("安装并重启"), &dialog); row->addWidget(install);
    auto *page = new QPushButton(QStringLiteral("发布页面"), &dialog); row->addWidget(page);
    auto *cancel = new QPushButton(QStringLiteral("取消下载"), &dialog); row->addWidget(cancel);
    auto *close = new QPushButton(QStringLiteral("关闭"), &dialog); row->addWidget(close);
    auto refresh = [&, this]() {
        message->setText(m_updateMessage.isEmpty() ? QStringLiteral("点击检查，可从 GitHub 获取最新正式版本。") : m_updateMessage);
        notes->setPlainText(m_updater->release().notes); check->setEnabled(!m_updater->busy()); download->setEnabled(!m_updater->busy() && m_updateAvailable);
        install->setEnabled(!m_updater->busy() && !m_updateDirectory.isEmpty()); page->setEnabled(!m_updater->release().page.isEmpty()); cancel->setEnabled(m_updater->busy());
    };
    connect(this, &MainWindow::updateStateChanged, &dialog, refresh);
    connect(m_updater, &AppUpdater::downloadProgress, &dialog, [progress](qint64 received, qint64 total) { progress->show(); progress->setRange(0, total > 0 ? 100 : 0); if (total > 0) progress->setValue(int(received * 100 / total)); });
    connect(automatic, &QCheckBox::toggled, &dialog, [this](bool enabled) { m_settings->setValue(QStringLiteral("updates/automatic"), enabled); });
    connect(check, &QPushButton::clicked, &dialog, [&, this]() {
        const QString repo = UpdateProtocol::repository(repository->text());
        if (repo.isEmpty()) { message->setText(QStringLiteral("请填写 用户名/仓库名 或 https://github.com/用户名/仓库名。")); return; }
        m_settings->setValue(QStringLiteral("updates/repository"), repo); m_settings->sync(); checkUpdates(); refresh();
    });
    connect(download, &QPushButton::clicked, &dialog, [&, this]() { m_updateMessage = QStringLiteral("正在下载并校验更新包…"); progress->setRange(0, 0); progress->show(); m_updater->download(); refresh(); });
    connect(cancel, &QPushButton::clicked, &dialog, [&, this]() { m_updater->cancel(); m_updateMessage = QStringLiteral("正在取消，请等待当前网络请求结束…"); refresh(); });
    connect(page, &QPushButton::clicked, &dialog, [this]() { QDesktopServices::openUrl(m_updater->release().page); });
    connect(install, &QPushButton::clicked, &dialog, [&, this]() { dialog.accept(); installUpdate(); });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    refresh(); dialog.exec();
}

void MainWindow::installUpdate()
{
    if (m_updateDirectory.isEmpty() || m_updater->busy() || m_backupBusy || !saveCurrent()) return;
    if (QMessageBox::question(this, QStringLiteral("安装更新"), QStringLiteral("安装 %1？\n\n将先备份资料，关闭拾页并安装更新，然后重新打开当前资料库。").arg(m_updateVersion)) != QMessageBox::Yes) return;
    const QString app = QCoreApplication::applicationDirPath();
    QTemporaryFile writable(QDir(app).filePath(QStringLiteral(".shiye-write-XXXXXX")));
    if (!writable.open()) { showError(QStringLiteral("程序目录不可写，请把整个拾页文件夹放到有写入权限的位置后再更新。")); return; }
    writable.close();
    const QString helper = QDir(m_updateDirectory).filePath(QStringLiteral("update-installer.ps1"));
    if (QFileInfo::exists(helper)) QFile::remove(helper);
    if (!QFile::copy(QDir(app).filePath(QStringLiteral("update-installer.ps1")), helper)) { showError(QStringLiteral("更新安装组件缺失，请重新下载当前完整程序包。")); return; }
    const QString backup = QDir(m_store.dataDirectory()).filePath(QStringLiteral("backups/pre-update-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-")) + QUuid::createUuid().toString(QUuid::WithoutBraces));
    setEnabled(false); m_backupBusy = true;
    QString error; const bool saved = m_store.backupTo(backup, &error); m_backupBusy = false;
    if (!saved) { setEnabled(true); showError(QStringLiteral("更新前备份失败，已停止安装：") + error); return; }
    const QString plan = QDir(m_updateDirectory).filePath(QStringLiteral("plan.json"));
    const QJsonObject object{{QStringLiteral("schema"), 1}, {QStringLiteral("version"), m_updateVersion}, {QStringLiteral("currentVersion"), QCoreApplication::applicationVersion()}, {QStringLiteral("parentPid"), double(QCoreApplication::applicationPid())}, {QStringLiteral("installDirectory"), app}, {QStringLiteral("dataDirectory"), m_store.dataDirectory()}};
    QSaveFile file(plan); const QByteArray bytes = QJsonDocument(object).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { setEnabled(true); showError(QStringLiteral("无法写入更新安装计划，资料备份已保留。")); return; }
    const QString powershell = QDir(qEnvironmentVariable("WINDIR", "C:/Windows")).filePath(QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"));
    if (!QProcess::startDetached(powershell, {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-File"), helper, QStringLiteral("-Plan"), plan})) {
        setEnabled(true); showError(QStringLiteral("无法启动更新安装程序，当前程序未改变。")); return;
    }
    m_backupTimer.stop();
    if (!close()) { QFile cancel(QDir(m_updateDirectory).filePath(QStringLiteral("cancel"))); cancel.open(QIODevice::WriteOnly); setEnabled(true); m_backupTimer.start(); }
}

void MainWindow::showPreferences()
{
    if (!saveCurrent()) return;
    QDialog dialog(this); dialog.setObjectName(QStringLiteral("preferencesDialog")); dialog.setWindowTitle(QStringLiteral("偏好设置")); dialog.resize(470, 360); auto *layout = new QVBoxLayout(&dialog); auto *heading = new QLabel(QStringLiteral("让拾页，更像你"), &dialog); heading->setObjectName(QStringLiteral("folderTitle")); layout->addWidget(heading);
    auto *form = new QFormLayout; form->setVerticalSpacing(19); auto *theme = new QComboBox(&dialog); theme->addItems({QStringLiteral("暖白浅色"), QStringLiteral("安静深色")}); theme->setCurrentIndex(m_dark ? 1 : 0); form->addRow(QStringLiteral("外观"), theme); auto *font = new QSpinBox(&dialog); font->setRange(14, 24); font->setValue(m_fontSize); font->setSuffix(QStringLiteral(" px")); form->addRow(QStringLiteral("正文字号"), font);
    QColor chosen = m_accent; auto *accent = new QPushButton(chosen.name(), &dialog); form->addRow(QStringLiteral("强调色"), accent); connect(accent, &QPushButton::clicked, &dialog, [&]() { const QColor color = QColorDialog::getColor(chosen, &dialog, QStringLiteral("选择强调色")); if (color.isValid()) { chosen = color; accent->setText(color.name()); } }); layout->addLayout(form); layout->addStretch();
    auto *location = new QLabel(QStringLiteral("资料库：\n") + QDir::toNativeSeparators(m_store.dataDirectory()), &dialog); location->setWordWrap(true); location->setTextInteractionFlags(Qt::TextSelectableByMouse); location->setObjectName(QStringLiteral("footerHint")); layout->addWidget(location); auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog); layout->addWidget(buttons); connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    m_dark = theme->currentIndex() == 1; m_fontSize = font->value(); m_accent = chosen; m_settings->setValue(QStringLiteral("appearance/dark"), m_dark); m_settings->setValue(QStringLiteral("appearance/fontSize"), m_fontSize); m_settings->setValue(QStringLiteral("appearance/accent"), chosen.name()); applyAppearance(); refreshTree(m_noteId); status(QStringLiteral("偏好已保存"));
}

void MainWindow::showTemplateMenu()
{
    QMenu menu(this); menu.addAction(QStringLiteral("☀  今日日记"), this, [this]() { createDailyNote(); }); menu.addAction(QStringLiteral("▤  阅读笔记"), this, [this]() { createNote(QStringLiteral("reading")); }); menu.addAction(QStringLiteral("☷  会议记录"), this, [this]() { createNote(QStringLiteral("meeting")); }); menu.addAction(QStringLiteral("◇  项目计划"), this, [this]() { createNote(QStringLiteral("project")); }); menu.exec(QCursor::pos());
}

void MainWindow::showBlockMenu(const QPoint &position)
{
    if (m_editor->isReadOnly()) return;
    QMenu menu(this); const QList<QPair<QString, QString>> blocks = {{QStringLiteral("正文"), QStringLiteral("paragraph")}, {QStringLiteral("一级标题 H1    #"), QStringLiteral("h1")}, {QStringLiteral("二级标题 H2    ##"), QStringLiteral("h2")}, {QStringLiteral("三级标题 H3    ###"), QStringLiteral("h3")}, {QStringLiteral("项目列表"), QStringLiteral("bullet")}, {QStringLiteral("编号列表"), QStringLiteral("number")}, {QStringLiteral("待办事项"), QStringLiteral("todo")}, {QStringLiteral("引用"), QStringLiteral("quote")}, {QStringLiteral("代码块"), QStringLiteral("code")}, {QStringLiteral("分割线"), QStringLiteral("divider")}};
    for (const auto &block : blocks) menu.addAction(block.first, this, [this, block]() { m_editor->applyBlock(block.second); });
    menu.addSeparator(); menu.addAction(QStringLiteral("导入图片…"), this, [this]() { importMedia(QStringLiteral("image")); }); menu.addAction(QStringLiteral("导入视频…"), this, [this]() { importMedia(QStringLiteral("video")); }); menu.exec(position);
}

void MainWindow::updateOutline()
{
    m_outline->clear();
    for (QTextBlock block = m_editor->document()->begin(); block.isValid(); block = block.next()) if (block.blockFormat().headingLevel() > 0) { auto *item = new QListWidgetItem(block.text(), m_outline); item->setData(IdRole, block.blockNumber()); }
    if (m_outline->count() == 0) { auto *item = new QListWidgetItem(QStringLiteral("添加标题后，这里会出现文章目录"), m_outline); item->setFlags(Qt::NoItemFlags); }
}

void MainWindow::updateEditorControls()
{
    if (!m_editor) return;
    const QString type = m_editor->currentBlockType();
    if (auto *selector = findChild<QComboBox *>(QStringLiteral("blockTypeSelector"))) {
        const QSignalBlocker blocker(selector);
        selector->setCurrentIndex(qMax(0, selector->findData(type)));
    }
    for (int level = 1; level <= 3; ++level) {
        if (auto *button = findChild<QToolButton *>(QStringLiteral("heading%1Button").arg(level))) {
            const QSignalBlocker blocker(button);
            button->setChecked(type == QStringLiteral("h%1").arg(level));
        }
    }
    if (m_codeTools && m_codeLanguage) {
        const bool code = type == QStringLiteral("code");
        m_codeTools->setVisible(code);
        m_codeLanguage->setEnabled(!m_editor->isReadOnly());
        if (code) {
            const QSignalBlocker comboBlocker(m_codeLanguage);
            const QSignalBlocker editBlocker(m_codeLanguage->lineEdit());
            const QString language = m_editor->currentCodeLanguage();
            const int index = m_codeLanguage->findData(language);
            m_codeLanguage->setCurrentIndex(index);
            if (index < 0) m_codeLanguage->setEditText(language);
        }
    }
    if (m_imageTools) {
        const bool image = !m_editor->selectedImageSource().isEmpty();
        m_imageTools->setVisible(image);
        if (image) {
            const auto size = m_editor->selectedImageSize();
            m_imageInfo->setText(QStringLiteral("图片 · %1 × %2").arg(qRound(size.width())).arg(qRound(size.height())));
            for (QWidget *control : m_imageTools->findChildren<QWidget *>())
                if (control->property("imageMutation").toBool()) control->setEnabled(!m_editor->isReadOnly());
        }
    }
}

void MainWindow::showError(const QString &message) { QMessageBox::warning(this, QStringLiteral("拾页"), message); }
void MainWindow::status(const QString &message) { statusBar()->showMessage(message, 9000); }

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!saveCurrent()) { event->ignore(); return; }
    if (m_settings) { m_settings->setValue(QStringLiteral("window/geometry"), saveGeometry()); m_settings->setValue(QStringLiteral("window/splitter"), m_splitter->saveState()); m_settings->sync(); }
    event->accept();
}

bool MainWindow::runSmokeTest()
{
    QJsonArray checks; bool success = true;
    auto check = [&](const QString &name, bool ok) { checks.append(QJsonObject{{QStringLiteral("check"), name}, {QStringLiteral("passed"), ok}}); success = success && ok; };
    const QString folder = m_store.createFolder(QStringLiteral("桌面验证")); const QString child = m_store.createFolder(QStringLiteral("子目录"), folder); const QString id = m_store.createNote(QStringLiteral("界面集成验证"), child, QStringLiteral("<p>初始内容</p>"), QStringLiteral("初始内容"));
    refreshTree(id); showNote(id); check(QStringLiteral("tree_nested_note_selected"), findItem(id) && findItem(id)->parent() && m_noteId == id);
    m_title->setText(QStringLiteral("桌面保存验证")); m_editor->setPlainText(QStringLiteral("中文内容在原生桌面编辑器中保存。")); m_dirty = true; check(QStringLiteral("native_editor_persistence"), saveCurrent() && m_store.node(id).plainText.contains(QStringLiteral("中文内容")));
    m_editor->applyBlock(QStringLiteral("h2")); m_dirty = true; saveCurrent(); check(QStringLiteral("heading_and_outline"), m_editor->textCursor().blockFormat().headingLevel() == 2);
    m_editor->setPlainText(QStringLiteral("#")); m_editor->moveCursor(QTextCursor::End);
    QKeyEvent headingSpace(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
    QApplication::sendEvent(m_editor, &headingSpace);
    m_editor->insertPlainText(QStringLiteral("Markdown 一级标题"));
    auto *selector = findChild<QComboBox *>(QStringLiteral("blockTypeSelector"));
    auto *headingButton = findChild<QToolButton *>(QStringLiteral("heading1Button"));
    check(QStringLiteral("markdown_heading_updates_toolbar"), m_editor->textCursor().blockFormat().headingLevel() == 1 && selector && selector->currentData().toString() == QStringLiteral("h1") && headingButton && headingButton->isChecked());
    QKeyEvent headingReturn(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    QApplication::sendEvent(m_editor, &headingReturn);
    check(QStringLiteral("heading_enter_resets_toolbar_to_body"), m_editor->textCursor().blockFormat().headingLevel() == 0 && selector && selector->currentData().toString() == QStringLiteral("paragraph"));
    m_editor->insertPlainText(QStringLiteral("Markdown 标题后可以自然续写正文。")); m_dirty = true;
    check(QStringLiteral("markdown_heading_saved_as_html"), saveCurrent() && m_store.node(id).html.contains(QStringLiteral("<h1")));
    QImage image(320, 180, QImage::Format_RGB32); image.fill(QColor(QStringLiteral("#e5eadb"))); const QString imagePath = QDir(m_store.dataDirectory()).filePath(QStringLiteral("smoke-source.png")); image.save(imagePath); importFiles({imagePath});
    const auto attachments = m_store.attachments(id); check(QStringLiteral("native_image_import"), !attachments.isEmpty() && m_store.node(id).html.contains(QStringLiteral("attachments/")));
    QFile::remove(imagePath); if (!attachments.isEmpty()) check(QStringLiteral("media_copy_survives_source_removal"), QFileInfo::exists(QDir(m_store.dataDirectory()).filePath(attachments.first().relativePath)));
    const QString videoPath = QDir(m_store.dataDirectory()).filePath(QStringLiteral("smoke-source.mp4")); QFile video(videoPath); video.open(QIODevice::WriteOnly); video.write(QByteArray::fromHex("000000186674797069736F6D0000020069736F6D69736F32")); video.close(); importFiles({videoPath}); QFile::remove(videoPath);
    check(QStringLiteral("video_import_and_card"), m_store.attachments(id).size() == 2 && m_store.node(id).html.contains(QStringLiteral("attachment:")));
    check(QStringLiteral("folder_cycle_rejected"), !m_store.moveNode(folder, child));
    m_store.moveNode(id, folder); updateNavigationContext();
    check(QStringLiteral("move_updates_editor_context"), currentFolder() == folder && m_breadcrumb->text().contains(QStringLiteral("桌面验证 / 桌面保存验证")));
    setView(View::Library);
    check(QStringLiteral("root_overview_clears_creation_context"), currentFolder().isEmpty() && selectedTreeId().isEmpty());
    m_store.setTrashed(id, true); setView(View::Trash);
    bool listed = false; for (int i = 0; i < m_folderList->count(); ++i) if (m_folderList->item(i)->data(IdRole).toString() == id) listed = true;
    check(QStringLiteral("trash_overview_lists_deleted_child_note"), listed);
    m_store.setTrashed(id, false); setView(View::Library); refreshTree(id); showNote(id);
    const QString backup = QDir(m_store.dataDirectory()).filePath(QStringLiteral("integration-backup")); QString error; check(QStringLiteral("complete_library_backup"), m_store.backupTo(backup, &error));
    m_dark = true; applyAppearance(); check(QStringLiteral("dark_theme_applied"), m_dark); m_dark = false; applyAppearance();
    m_noteId.clear(); refreshTree();
    const QString demo = m_store.createNote(QStringLiteral("用 Markdown，自然地记录"), folder);
    if (!demo.isEmpty()) {
        refreshTree(demo); showNote(demo);
        auto typeAscii = [this](const QString &text) {
            for (const auto character : text) {
                QKeyEvent key(QEvent::KeyPress, character.toUpper().unicode(), Qt::NoModifier, QString(character));
                QApplication::sendEvent(m_editor, &key);
            }
        };
        auto newLine = [this]() {
            QKeyEvent key(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
            QApplication::sendEvent(m_editor, &key);
        };
        typeAscii(QStringLiteral("# ")); m_editor->insertPlainText(QStringLiteral("让想法，自然成形")); newLine();
        m_editor->insertPlainText(QStringLiteral("行首输入 #、##、###，空格后自动排版。回车就能继续写正文。")); newLine();
        typeAscii(QStringLiteral("## ")); m_editor->insertPlainText(QStringLiteral("清晰的层次，顺手写出来")); newLine();
        m_editor->insertPlainText(QStringLiteral("支持 ")); typeAscii(QStringLiteral("**bold**")); m_editor->insertPlainText(QStringLiteral("、")); typeAscii(QStringLiteral("*italic*")); m_editor->insertPlainText(QStringLiteral(" 和 ")); typeAscii(QStringLiteral("`inline code`")); m_editor->insertPlainText(QStringLiteral("，让重点一目了然。")); newLine();
        typeAscii(QStringLiteral("### ")); m_editor->insertPlainText(QStringLiteral("一步一步，积累自己的知识")); newLine();
        typeAscii(QStringLiteral("- ")); m_editor->insertPlainText(QStringLiteral("写下一个值得记住的想法")); newLine();
        m_editor->insertPlainText(QStringLiteral("用自己的话，把知识连接起来")); newLine(); newLine();
        typeAscii(QStringLiteral("[] ")); m_editor->insertPlainText(QStringLiteral("试试 # + 空格，创建你的第一个标题"));
        m_dirty = true; saveCurrent();
        m_editor->moveCursor(QTextCursor::Start);
    }
    const QString codeDemo = m_store.createNote(QStringLiteral("代码，也要清晰好读"), folder);
    if (!codeDemo.isEmpty()) {
        refreshTree(codeDemo); showNote(codeDemo);
        auto type = [this](const QString &text) {
            for (const QChar character : text) {
                QKeyEvent key(QEvent::KeyPress, character == QLatin1Char('\n') ? int(Qt::Key_Return) : int(character.toUpper().unicode()), Qt::NoModifier, character == QLatin1Char('\n') ? QStringLiteral("\r") : QString(character));
                QApplication::sendEvent(m_editor, &key);
            }
        };
        m_editor->insertPlainText(QStringLiteral("为每段代码选择语言，语法高亮和底色会跟随当前主题。")); type(QStringLiteral("\n\n```python\n"));
        check(QStringLiteral("code_fence_language_updates_toolbar"), m_editor->currentCodeLanguage() == QStringLiteral("python") && m_codeLanguage->currentData().toString() == QStringLiteral("python") && m_codeTools->isVisible());
        m_editor->insertPlainText(QStringLiteral("# 语言选择会作用于当前整个代码块"));
        type(QStringLiteral("\ndef greet(name):\n    message = \"Hello, \" + name\n    return message\n\nprint(greet(\"Shiye\"))"));
        const int cpp = m_codeLanguage->findData(QStringLiteral("cpp")); m_codeLanguage->setCurrentIndex(cpp);
        QMetaObject::invokeMethod(m_codeLanguage, "activated", Qt::DirectConnection, Q_ARG(int, cpp));
        check(QStringLiteral("code_language_picker_applies"), m_editor->currentCodeLanguage() == QStringLiteral("cpp"));
        m_editor->setCurrentCodeLanguage(QStringLiteral("python")); m_dirty = true;
        check(QStringLiteral("code_language_saved"), saveCurrent());
        const QString codeHtml = m_store.node(codeDemo).html;
        const QString codeText = m_editor->toPlainText();
        const QColor lightBackground = m_editor->extraSelections().isEmpty() ? QColor() : m_editor->extraSelections().first().format.background().color();
        m_dark = true; applyAppearance();
        const QColor darkBackground = m_editor->extraSelections().isEmpty() ? QColor() : m_editor->extraSelections().first().format.background().color();
        check(QStringLiteral("code_theme_refreshes_all_blocks"), lightBackground.isValid() && darkBackground.isValid() && lightBackground != darkBackground && m_editor->toPlainText() == codeText);
        QApplication::processEvents(); grab().save(QDir(m_store.dataDirectory()).filePath(QStringLiteral("code-preview-dark.png")));
        m_dark = false; applyAppearance();
        const QColor restoredBackground = m_editor->extraSelections().isEmpty() ? QColor() : m_editor->extraSelections().first().format.background().color();
        check(QStringLiteral("code_light_theme_restored"), restoredBackground == lightBackground && m_editor->toPlainText() == codeText);
        m_editor->setNoteHtml(codeHtml); m_editor->moveCursor(QTextCursor::End); updateEditorControls();
        check(QStringLiteral("code_language_survives_html_reload"), m_editor->currentCodeLanguage() == QStringLiteral("python") && m_editor->toPlainText() == codeText);
        QApplication::processEvents(); grab().save(QDir(m_store.dataDirectory()).filePath(QStringLiteral("code-preview-light.png")));
    }
    const QString imageDemo = m_store.createNote(QStringLiteral("让图片，自然融入笔记"), folder);
    if (!imageDemo.isEmpty()) {
        refreshTree(imageDemo); showNote(imageDemo);
        m_editor->insertPlainText(QStringLiteral("记录一段文字，配上一张图片，再继续写下你的想法。"));
        QImage sample(720, 280, QImage::Format_RGB32); sample.fill(QColor(QStringLiteral("#e8ede0")));
        QPainter painter(&sample); painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen); painter.setBrush(QColor(QStringLiteral("#c7d4b3"))); painter.drawEllipse(480, -70, 320, 320);
        painter.setBrush(QColor(QStringLiteral("#a8bb8a"))); painter.drawEllipse(580, 90, 220, 220);
        painter.setPen(QColor(QStringLiteral("#46553b"))); QFont headline(QStringLiteral("Microsoft YaHei")); headline.setPixelSize(28); painter.setFont(headline);
        painter.drawText(QRect(40, 62, 530, 55), QStringLiteral("每一张图片，都有自己的位置"));
        headline.setPixelSize(16); painter.setFont(headline);
        painter.drawText(QRect(42, 130, 430, 60), QStringLiteral("点击调整尺寸与对齐\n双击查看原图")); painter.end();
        const QString samplePath = QDir(m_store.dataDirectory()).filePath(QStringLiteral("image-layout-sample.png")); sample.save(samplePath);
        importFiles({samplePath}); QFile::remove(samplePath);
        check(QStringLiteral("image_returns_to_body"), m_editor->currentBlockType() == QStringLiteral("paragraph"));
        m_editor->insertPlainText(QStringLiteral("图片下方可以直接续写，间距保持紧凑。"));
        QTextBlock imageBlock;
        for (QTextBlock block = m_editor->document()->begin(); block.isValid(); block = block.next()) {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                if (!it.fragment().charFormat().isImageFormat()) continue;
                imageBlock = block;
                QTextCursor selection(m_editor->document()); selection.setPosition(it.fragment().position()); selection.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor); m_editor->setTextCursor(selection);
                break;
            }
        }
        check(QStringLiteral("image_selection_shows_tools"), !m_editor->selectedImageSource().isEmpty() && m_imageTools->isVisible() && !m_codeTools->isVisible());
        auto *sizePicker = findChild<QComboBox *>(QStringLiteral("imageSizeSelector"));
        const int medium = sizePicker->findData(480); sizePicker->setCurrentIndex(medium);
        QMetaObject::invokeMethod(sizePicker, "activated", Qt::DirectConnection, Q_ARG(int, medium));
        check(QStringLiteral("image_size_picker_applies"), qRound(m_editor->selectedImageSize().width()) == 480);
        check(QStringLiteral("image_has_no_large_empty_band"), imageBlock.isValid() && m_editor->document()->documentLayout()->blockBoundingRect(imageBlock).height() <= m_editor->selectedImageSize().height() + 20);
        check(QStringLiteral("attachments_start_collapsed"), !m_attachmentList->isVisible() && m_attachmentToggle->isVisible());
        m_attachmentToggle->click(); check(QStringLiteral("attachments_expand"), m_attachmentList->isVisible()); m_attachmentToggle->click();
        findChild<QPushButton *>(QStringLiteral("previewImageButton"))->click();
        auto *previewDialog = findChild<QDialog *>(QStringLiteral("imagePreviewDialog"));
        check(QStringLiteral("image_preview_opens"), previewDialog && previewDialog->isVisible());
        if (previewDialog) previewDialog->close();
        m_dirty = true; check(QStringLiteral("image_layout_saved"), saveCurrent());
        m_dark = true; applyAppearance(); QApplication::processEvents();
        grab().save(QDir(m_store.dataDirectory()).filePath(QStringLiteral("image-preview-dark.png")));
        m_dark = false; applyAppearance(); QApplication::processEvents();
        grab().save(QDir(m_store.dataDirectory()).filePath(QStringLiteral("image-preview-light.png")));
    }
    if (!imageDemo.isEmpty()) {
        const QString original = m_editor->toPlainText();
        m_editor->moveCursor(QTextCursor::End); m_editor->insertPlainText(QStringLiteral("\n这段修改用于验证历史恢复。"));
        check(QStringLiteral("history_edit_saved"), saveCurrent());
        const int count = m_store.revisions(imageDemo).size();
        m_dark = true; applyAppearance();
        QTimer::singleShot(100, this, [&, this]() {
            auto *dialog = findChild<QDialog *>(QStringLiteral("historyDialog"));
            auto *preview = dialog ? dialog->findChild<NoteEditor *>(QStringLiteral("revisionPreview")) : nullptr;
            auto *button = dialog ? dialog->findChild<QPushButton *>(QStringLiteral("restoreRevisionButton")) : nullptr;
            check(QStringLiteral("history_preview_readonly_and_preserves_media"), preview && preview->isReadOnly() && preview->toPlainText() == original && preview->noteHtml().contains(QStringLiteral("<img")));
            if (dialog) dialog->grab().save(QDir(m_store.dataDirectory()).filePath(QStringLiteral("history-preview-dark.png")));
            if (button && button->isEnabled()) button->click(); else if (dialog) dialog->reject();
        });
        showHistory();
        check(QStringLiteral("history_restores_editor_and_keeps_previous_content"), m_editor->toPlainText() == original && m_store.revisions(imageDemo).size() == count + 1);
        m_dark = false; applyAppearance();
        check(QStringLiteral("automatic_backup_verified"), runAutomaticBackup(true));
        check(QStringLiteral("backup_status_saved"), !m_settings->value(QStringLiteral("backup/lastPath")).toString().isEmpty() && m_settings->value(QStringLiteral("backup/lastError")).toString().isEmpty());
        QTimer::singleShot(100, this, [&, this]() {
            auto *dialog = findChild<QDialog *>(QStringLiteral("dataProtectionDialog"));
            check(QStringLiteral("data_protection_dialog_opens"), dialog && dialog->isVisible());
            if (dialog) { dialog->grab().save(QDir(m_store.dataDirectory()).filePath(QStringLiteral("data-protection-preview.png"))); dialog->reject(); }
        });
        showDataProtection();
        const bool enabled = m_settings->value(QStringLiteral("backup/enabled"), true).toBool();
        m_settings->setValue(QStringLiteral("backup/enabled"), false);
        const QString beforeTyping = m_store.node(imageDemo).plainText;
        QEventLoop typing; QTimer keystrokes;
        connect(&keystrokes, &QTimer::timeout, this, [this]() { m_editor->moveCursor(QTextCursor::End); m_editor->insertPlainText(QStringLiteral("·")); });
        keystrokes.start(80); QTimer::singleShot(5500, &typing, &QEventLoop::quit); typing.exec(); keystrokes.stop();
        check(QStringLiteral("continuous_typing_is_saved_before_idle"), m_store.node(imageDemo).plainText.size() > beforeTyping.size());
        saveCurrent(); m_settings->setValue(QStringLiteral("backup/enabled"), enabled); updateBackupStatus();
    }
    const QString markdownSource = QDir(m_store.dataDirectory()).filePath(QStringLiteral("markdown-source")); QDir().mkpath(markdownSource);
    QImage importImage(680, 200, QImage::Format_RGB32); importImage.fill(QColor(QStringLiteral("#e7eddf")));
    { QPainter p(&importImage); p.setPen(QColor(QStringLiteral("#526246"))); QFont font(QStringLiteral("Microsoft YaHei")); font.setPixelSize(25); p.setFont(font); p.drawText(QRect(30, 65, 620, 60), Qt::AlignCenter, QStringLiteral("让已有的资料，成为自己的知识")); }
    importImage.save(QDir(markdownSource).filePath(QStringLiteral("sample.png")));
    const QString sourcePath = QDir(markdownSource).filePath(QStringLiteral("从 Markdown 建立知识库.md"));
    QFile source(sourcePath); const QByteArray sourceBytes = QStringLiteral("# 我的知识库\n\n这份知识来自 Markdown，支持持续整理。\n\n## 记录与连接\n\n把知识连接起来。\n\n![资料图片](sample.png)\n\n```python\ntopic = \"知识\"\nprint(topic)\n```\n\n| 类型 | 内容 |\n| --- | --- |\n| 笔记 | 知识 |\n").toUtf8();
    check(QStringLiteral("markdown_fixture_written"), source.open(QIODevice::WriteOnly) && source.write(sourceBytes) == sourceBytes.size()); source.close();
    const QString existingNote = m_noteId; const QString existingHtml = m_store.node(existingNote).html;
    importMarkdownFiles({sourcePath});
    const QString imported = m_noteId;
    check(QStringLiteral("markdown_import_creates_note_without_overwriting_current"), imported != existingNote && m_store.node(existingNote).html == existingHtml && m_store.node(imported).title == QStringLiteral("从 Markdown 建立知识库"));
    check(QStringLiteral("markdown_import_copies_source_and_image"), m_store.attachments(imported).size() == 2 && m_editor->noteHtml().contains(QStringLiteral("<img")));
    const QString beforeReplace = m_editor->noteHtml();
    m_editor->moveCursor(QTextCursor::Start); showFindBar(true); m_findText->setText(QStringLiteral("知识")); m_replaceText->setText(QStringLiteral("资料"));
    check(QStringLiteral("find_bar_counts_matches_and_enables_replace"), m_findBar->isVisible() && m_replaceRow->isVisible() && m_editor->searchMatchCount() == 5 && m_replaceAll->isEnabled());
    m_replaceAll->click();
    check(QStringLiteral("replace_all_saves_and_preserves_code"), !m_editor->toPlainText().contains(QStringLiteral("知识")) && saveCurrent() && m_store.node(imported).plainText.contains(QStringLiteral("资料")) && NoteEditor::codeLanguageForBlock(m_editor->document()->find(QStringLiteral("topic")).block()) == QStringLiteral("python"));
    m_editor->undo(); check(QStringLiteral("replace_all_undo_restores_original_document"), m_editor->noteHtml() == beforeReplace && saveCurrent());
    m_editor->moveCursor(QTextCursor::Start); m_editor->setSearch(QStringLiteral("知识")); m_editor->findSearchMatch();
    m_dark = true; applyAppearance(); QApplication::processEvents(); grab().save(QDir(m_store.dataDirectory()).filePath(QStringLiteral("find-replace-dark.png")));
    m_dark = false; applyAppearance(); QApplication::processEvents(); grab().save(QDir(m_store.dataDirectory()).filePath(QStringLiteral("find-replace-light.png")));
    closeFindBar(); check(QStringLiteral("find_close_clears_highlights"), !m_findBar->isVisible() && m_editor->searchMatchCount() == 0);
    m_store.setTrashed(imported, true); m_noteId.clear(); setView(View::Trash); showNote(imported); showFindBar(true); m_findText->setText(QStringLiteral("知识"));
    check(QStringLiteral("readonly_note_can_find_but_cannot_replace"), m_editor->isReadOnly() && m_editor->searchMatchCount() == 5 && !m_replaceOne->isEnabled() && !m_replaceAll->isEnabled());
    closeFindBar(); m_store.setTrashed(imported, false); m_noteId.clear(); setView(View::Library); refreshTree(imported); showNote(imported);
    check(QStringLiteral("imported_note_has_recoverable_history"), m_store.revisions(imported).size() >= 2 && m_store.restoreRevision(imported, m_store.revisions(imported).first().id));
    m_noteId.clear(); refreshTree(imported); showNote(imported); check(QStringLiteral("post_import_library_integrity"), m_store.checkIntegrity());
    QTimer::singleShot(100, this, [&, this]() {
        auto *dialog = findChild<QDialog *>(QStringLiteral("updatesDialog"));
        auto *repository = dialog ? dialog->findChild<QLineEdit *>(QStringLiteral("updateRepository")) : nullptr;
        check(QStringLiteral("updates_dialog_has_configured_public_repository"), repository && repository->text() == QStringLiteral("ricarrdooo/PersonalNote"));
        check(QStringLiteral("updates_dialog_opens_without_network_or_note_changes"), dialog && dialog->isVisible() && !m_updater->busy());
        if (dialog) { dialog->grab().save(QDir(m_store.dataDirectory()).filePath(QStringLiteral("updates-preview.png"))); dialog->reject(); }
    });
    showUpdates();
    QJsonObject report{{QStringLiteral("passed"), success}, {QStringLiteral("checks"), checks}, {QStringLiteral("videoPlayback"), QStringLiteral("Import tested; decoding requires a valid video and an available system codec")}}; QSaveFile result(QDir(m_store.dataDirectory()).filePath(QStringLiteral("smoke-test-results.json"))); if (result.open(QIODevice::WriteOnly)) { result.write(QJsonDocument(report).toJson()); result.commit(); }
    const QString preview = QDir(m_store.dataDirectory()).filePath(QStringLiteral("desktop-preview.png")); QApplication::processEvents(); grab().save(preview);
    return success;
}
