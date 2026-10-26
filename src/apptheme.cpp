#include "apptheme.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QtMath>
#include <initializer_list>

namespace AppTheme {

QIcon icon(const QString &name, const QColor &color)
{
    QPixmap canvas(48, 48);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.scale(2, 2);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    const auto line = [&painter](qreal x1, qreal y1, qreal x2, qreal y2) {
        painter.drawLine(QPointF(x1, y1), QPointF(x2, y2));
    };
    const auto polyline = [&painter](std::initializer_list<QPointF> points) {
        QPainterPath path;
        bool first = true;
        for (const auto &point : points) {
            if (first) { path.moveTo(point); first = false; }
            else path.lineTo(point);
        }
        painter.drawPath(path);
    };
    const auto folder = [&painter]() {
        QPainterPath path;
        path.moveTo(3, 7); path.quadTo(3, 5, 5, 5);
        path.lineTo(9, 5); path.lineTo(11, 7); path.lineTo(19, 7);
        path.quadTo(21, 7, 21, 9); path.lineTo(21, 18);
        path.quadTo(21, 20, 19, 20); path.lineTo(5, 20);
        path.quadTo(3, 20, 3, 18); path.closeSubpath();
        painter.drawPath(path);
    };

    if (name == "leaf") {
        QPainterPath path;
        path.moveTo(20, 3); path.cubicTo(12, 2, 4, 5, 4, 12);
        path.cubicTo(4, 19, 12, 20, 16, 15); path.cubicTo(19, 12, 19, 7, 20, 3);
        painter.drawPath(path);
        QPainterPath stem; stem.moveTo(3, 22); stem.cubicTo(5, 15, 9, 11, 15, 8);
        painter.drawPath(stem);
    } else if (name == "folder" || name == "new-folder") {
        folder();
        if (name == "new-folder") { line(9, 13, 15, 13); line(12, 10, 12, 16); }
    } else if (name == "folder-open") {
        QPainterPath path;
        path.moveTo(3, 17); path.lineTo(3, 6); path.quadTo(3, 4, 5, 4);
        path.lineTo(9, 4); path.lineTo(11, 6); path.lineTo(18, 6);
        path.quadTo(20, 6, 20, 8); path.lineTo(20, 10);
        path.moveTo(3, 20); path.lineTo(6, 10); path.lineTo(22, 10);
        path.lineTo(19, 20); path.closeSubpath(); painter.drawPath(path);
    } else if (name == "search") {
        painter.drawEllipse(QRectF(3.5, 3.5, 12.5, 12.5)); line(14, 14, 21, 21);
    } else if (name == "plus") {
        line(12, 5, 12, 19); line(5, 12, 19, 12);
    } else if (name == "image") {
        painter.drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2);
        painter.drawEllipse(QRectF(7, 7, 3, 3));
        polyline({{4, 18}, {10, 12}, {14, 16}, {17, 13}, {21, 17}});
    } else if (name == "video") {
        painter.drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2);
        painter.drawPolygon(QPolygonF() << QPointF(10, 8) << QPointF(10, 16) << QPointF(16, 12));
    } else if (name == "star") {
        QPolygonF star;
        for (int i = 0; i < 10; ++i) {
            const qreal angle = qDegreesToRadians(-90.0 + i * 36.0);
            const qreal radius = i % 2 == 0 ? 9.0 : 4.2;
            star << QPointF(12 + qCos(angle) * radius, 12 + qSin(angle) * radius);
        }
        painter.drawPolygon(star);
    } else if (name == "trash") {
        line(4, 6, 20, 6); polyline({{9, 6}, {9, 3}, {15, 3}, {15, 6}});
        polyline({{6, 6}, {7, 21}, {17, 21}, {18, 6}});
        line(10, 10, 10, 17); line(14, 10, 14, 17);
    } else if (name == "settings") {
        painter.drawEllipse(QRectF(5.5, 5.5, 13, 13));
        painter.drawEllipse(QRectF(9, 9, 6, 6));
        for (int i = 0; i < 8; ++i) {
            const qreal angle = qDegreesToRadians(i * 45.0);
            line(12 + qCos(angle) * 7, 12 + qSin(angle) * 7,
                 12 + qCos(angle) * 9.5, 12 + qSin(angle) * 9.5);
        }
    } else if (name == "arrow-right" || name == "back") {
        line(4, 12, 20, 12);
        if (name == "back") polyline({{10, 6}, {4, 12}, {10, 18}});
        else polyline({{14, 6}, {20, 12}, {14, 18}});
    } else if (name == "chevron-down") {
        polyline({{6, 9}, {12, 15}, {18, 9}});
    } else if (name == "more") {
        painter.setBrush(color); painter.setPen(Qt::NoPen);
        for (int x : {5, 12, 19}) painter.drawEllipse(QPointF(x, 12), 1.5, 1.5);
    } else if (name == "expand") {
        polyline({{8, 3}, {3, 3}, {3, 8}}); polyline({{16, 3}, {21, 3}, {21, 8}});
        polyline({{3, 16}, {3, 21}, {8, 21}}); polyline({{21, 16}, {21, 21}, {16, 21}});
    } else if (name == "download") {
        line(12, 3, 12, 15); polyline({{7, 10}, {12, 15}, {17, 10}});
        polyline({{4, 17}, {4, 21}, {20, 21}, {20, 17}});
    } else if (name == "bold" || name == "italic") {
        QFont font("Segoe UI", 15); font.setBold(name == "bold"); font.setItalic(name == "italic");
        painter.setFont(font); painter.drawText(QRectF(1, 0, 22, 24), Qt::AlignCenter, name == "bold" ? "B" : "I");
    } else if (name == "list") {
        painter.setBrush(color);
        for (int y : {6, 12, 18}) { painter.drawEllipse(QPointF(4, y), 1, 1); line(9, y, 21, y); }
    } else if (name == "code") {
        polyline({{8, 6}, {2, 12}, {8, 18}}); polyline({{16, 6}, {22, 12}, {16, 18}});
        line(14, 3, 10, 21);
    } else if (name == "quote") {
        painter.drawRoundedRect(QRectF(3, 5, 7, 7), 1, 1); painter.drawRoundedRect(QRectF(14, 5, 7, 7), 1, 1);
        QPainterPath path; path.moveTo(3, 12); path.quadTo(3, 17, 8, 19);
        path.moveTo(14, 12); path.quadTo(14, 17, 19, 19); painter.drawPath(path);
    } else if (name == "check") {
        polyline({{20, 12}, {20, 21}, {3, 21}, {3, 4}, {14, 4}});
        polyline({{8, 10}, {12, 14}, {21, 4}});
    } else if (name == "book") {
        QPainterPath path; path.moveTo(12, 5); path.cubicTo(9, 3, 5, 3, 2, 4);
        path.lineTo(2, 20); path.cubicTo(6, 19, 9, 19, 12, 21);
        path.cubicTo(15, 19, 18, 19, 22, 20); path.lineTo(22, 4);
        path.cubicTo(19, 3, 15, 3, 12, 5); path.lineTo(12, 21); painter.drawPath(path);
    } else if (name == "sun") {
        painter.drawEllipse(QRectF(8, 8, 8, 8));
        for (int i = 0; i < 8; ++i) {
            const qreal angle = qDegreesToRadians(i * 45.0);
            line(12 + qCos(angle) * 7, 12 + qSin(angle) * 7,
                 12 + qCos(angle) * 9.5, 12 + qSin(angle) * 9.5);
        }
    } else if (name == "moon") {
        QPainterPath path; path.moveTo(20, 15); path.cubicTo(12, 18, 6, 11, 10, 3);
        path.cubicTo(1, 5, 0, 18, 9, 21); path.cubicTo(14, 23, 19, 20, 20, 15);
        painter.drawPath(path);
    } else if (name == "restore") {
        painter.drawArc(QRectF(5, 5, 15, 15), -45 * 16, 290 * 16);
        polyline({{3, 5}, {3, 11}, {9, 11}}); line(12, 9, 12, 14); line(12, 14, 15, 16);
    } else if (name == "save") {
        polyline({{3, 3}, {17, 3}, {21, 7}, {21, 21}, {3, 21}, {3, 3}});
        polyline({{7, 3}, {7, 9}, {16, 9}, {16, 3}});
        painter.drawRect(QRectF(7, 14, 10, 7));
    } else if (name == "undo" || name == "redo") {
        QPainterPath path;
        if (name == "undo") {
            path.moveTo(4, 9); path.cubicTo(13, 2, 23, 8, 20, 17);
            painter.drawPath(path); polyline({{4, 3}, {4, 9}, {10, 9}});
        } else {
            path.moveTo(20, 9); path.cubicTo(11, 2, 1, 8, 4, 17);
            painter.drawPath(path); polyline({{20, 3}, {20, 9}, {14, 9}});
        }
    } else {
        polyline({{14, 3}, {5, 3}, {5, 21}, {19, 21}, {19, 8}, {14, 3}, {14, 8}, {19, 8}});
        line(8, 12, 16, 12); line(8, 16, 14, 16);
    }

    painter.end();
    canvas.setDevicePixelRatio(2);
    return QIcon(canvas);
}

QString styleSheet(bool dark, const QColor &accent)
{
    const QString background = dark ? "#22271f" : "#ffffff";
    const QString sidebar = dark ? "#272d23" : "#f4f5f0";
    const QString text = dark ? "#d8decf" : "#30392d";
    const QString secondary = dark ? "#a6b299" : "#76816b";
    const QString muted = dark ? "#8d9a80" : "#929d87";
    const QString line = dark ? "#3a4432" : "#e5e9df";
    const QString hover = dark ? "#343e2c" : "#e9ede3";
    const QString selected = dark ? "#403a52" : "#eeebf7";
    const QString cover = dark ? "#303b27" : "#edf1e6";
    const QString input = dark ? "#252c20" : "#ffffff";
    const QString accentHex = accent.isValid() ? accent.name() : "#7771b5";
    const QString codeBackground = dark ? "#263244" : "#f1f4f8";
    const QString codeBorder = dark ? "#44546c" : "#d8e0eb";
    const QString codeText = dark ? "#dce6f5" : "#243041";

    QString sheet = QStringLiteral(R"QSS(
QWidget {
    color: @TEXT@;
    font-family: "Segoe UI", "Microsoft YaHei UI", "Microsoft YaHei";
    font-size: 14px;
    selection-background-color: @SELECTED@;
    selection-color: @TEXT@;
}
QMainWindow, QWidget#app, QWidget#editorPane, QWidget#documentFrame, QWidget#notePage {
    background: @BG@;
}
QWidget#titlebar {
    background: @BG@;
    border-bottom: 1px solid @LINE@;
}
QLabel#brandLabel { font-size: 21px; font-weight: 600; letter-spacing: 2px; }
QLabel#brandTagline { color: @MUTED@; font-size: 12px; padding-left: 12px; }
QWidget#sidebar { background: @SIDEBAR@; border-right: 1px solid @LINE@; }
QLabel#spaceLabel { font-size: 16px; font-weight: 600; }
QLabel#spaceHint { color: @MUTED@; font-size: 12px; }
QPushButton#newNoteButton {
    color: #ffffff; background: @ACCENT@; border: none;
    border-radius: 7px; padding: 10px 14px; text-align: left;
    font-size: 14px; font-weight: 500;
}
QPushButton#newNoteButton:hover { background: @ACCENT@; border: 1px solid #a8a0ce; padding: 9px 13px; }
QPushButton#newFolderButton { background: transparent; color: @SECONDARY@; border: 1px solid @LINE@; border-radius: 7px; padding: 9px 12px; }
QPushButton#newFolderButton:hover { background: @HOVER@; }
QLineEdit {
    background: @INPUT@; border: 1px solid @LINE@; border-radius: 6px;
    padding: 8px 10px; color: @TEXT@; min-height: 20px;
}
QLineEdit:focus { border-color: @ACCENT@; }
QLineEdit#searchEdit { font-size: 13px; padding: 9px 11px; }
QLabel#sectionLabel { color: @MUTED@; font-size: 12px; letter-spacing: 1px; padding: 10px 4px 7px; }
QTreeWidget#directoryTree {
    border: none; background: transparent; outline: none;
    alternate-background-color: transparent; font-size: 14px;
    show-decoration-selected: 1;
}
QTreeWidget#directoryTree::item { min-height: 34px; border: none; padding: 2px 5px; }
QTreeWidget#directoryTree::item:hover { background: @HOVER@; border-radius: 5px; }
QTreeWidget#directoryTree::item:selected { background: @SELECTED@; color: @ACCENT@; border-radius: 5px; }
QTreeWidget#directoryTree::branch { background: transparent; }
QTreeWidget#directoryTree QLineEdit { border: 1px solid @ACCENT@; border-radius: 4px; padding: 0 5px; background: @INPUT@; }
QWidget#sidebarBottom { border-top: 1px solid @LINE@; background: transparent; }
QWidget#sidebarBottom QPushButton { text-align: left; padding: 8px 11px; background: transparent; border: none; color: @SECONDARY@; }
QWidget#sidebarBottom QPushButton:hover { background: @HOVER@; }
QWidget#editorHeader { background: @BG@; border-bottom: 1px solid @LINE@; }
QLabel#breadcrumbLabel { color: @SECONDARY@; font-size: 13px; }
QLabel#saveLabel { color: @MUTED@; font-size: 12px; padding-right: 8px; }
QLabel#backupStatus { color: @SECONDARY@; font-size: 11px; padding-right: 4px; }
QWidget#cover { background: @COVER@; border: none; }
QWidget#cover QLabel { color: @SECONDARY@; font-size: 14px; letter-spacing: 1px; background: transparent; }
QLineEdit#titleEdit {
    border: none; border-radius: 0; background: transparent;
    font-size: 30px; font-weight: 600; padding: 4px 0; min-height: 46px;
}
QLineEdit#titleEdit:focus { border: none; }
QLabel#metaLabel { color: @MUTED@; font-size: 12px; padding: 7px 0 14px; }
QWidget#formattingToolbar, QToolBar#formattingToolbar {
    background: transparent; border: none; border-top: 1px solid @LINE@;
    border-bottom: 1px solid @LINE@; padding: 5px 0;
}
QTextEdit#noteEditor {
    background: @BG@; border: none; color: @TEXT@; padding: 6px 0;
    font-size: 16px; selection-background-color: @SELECTED@;
}
QTextEdit#revisionPreview { background: @BG@; color: @TEXT@; border: 1px solid @LINE@; padding: 14px; }
QListWidget#revisionList { background: @SIDEBAR@; border: 1px solid @LINE@; border-radius: 6px; }
QListWidget#revisionList::item { padding: 12px 8px; border-bottom: 1px solid @LINE@; }
QListWidget#revisionList::item:selected { background: @SELECTED@; color: @TEXT@; }
QFrame#codeTools {
    background: @CODEBG@; border: 1px solid @CODEBORDER@; border-radius: 6px;
}
QFrame#findBar { background: @SIDEBAR@; border: 1px solid @LINE@; border-radius: 6px; }
QFrame#findBar QLabel, QFrame#findBar QCheckBox { background: transparent; color: @SECONDARY@; font-size: 12px; }
QFrame#findBar QPushButton { padding: 4px 8px; font-size: 12px; }
QFrame#codeTools QLabel { background: transparent; color: @CODETEXT@; font-size: 12px; }
QComboBox#codeLanguageSelector {
    background: @CODEBG@; color: @CODETEXT@; border: 1px solid @CODEBORDER@;
    padding: 4px 8px; min-height: 18px; font-size: 12px;
}
QComboBox#codeLanguageSelector QLineEdit { background: transparent; color: @CODETEXT@; border: none; padding: 0; min-height: 16px; }
QPushButton#copyCodeButton { background: transparent; color: @CODETEXT@; border: none; padding: 5px 8px; font-size: 12px; }
QPushButton#copyCodeButton:hover { background: @HOVER@; }
QFrame#imageTools { background: @SIDEBAR@; border: 1px solid @LINE@; border-radius: 6px; }
QFrame#imageTools QLabel { background: transparent; font-size: 12px; color: @SECONDARY@; }
QFrame#imageTools QPushButton { padding: 4px 8px; font-size: 12px; background: transparent; border: none; }
QFrame#imageTools QPushButton:hover { background: @HOVER@; }
QComboBox#imageSizeSelector { padding: 4px 8px; font-size: 12px; min-height: 18px; }
QToolButton#attachmentToggle { text-align: left; color: @SECONDARY@; font-size: 12px; padding: 5px; }
QDialog#imagePreviewDialog QScrollArea { background: @SIDEBAR@; border: none; }
QDialog#imagePreviewDialog QScrollArea QLabel { background: transparent; }
QListWidget#attachmentList {
    background: @SIDEBAR@; border: 1px solid @LINE@; border-radius: 7px;
    padding: 6px; outline: none;
}
QListWidget#attachmentList::item { padding: 10px; border-radius: 5px; min-height: 30px; }
QListWidget#attachmentList::item:hover { background: @HOVER@; }
QListWidget#attachmentList::item:selected { background: @SELECTED@; color: @ACCENT@; }
QLabel#footerHint { color: @MUTED@; font-size: 12px; border-top: 1px solid @LINE@; padding-top: 15px; }
QLabel#folderTitle { font-size: 30px; font-weight: 600; }
QLabel#folderHint { font-size: 14px; color: @SECONDARY@; }
QListWidget#folderList {
    border: 1px solid @LINE@; background: @BG@; border-radius: 8px;
    padding: 8px; outline: none;
}
QListWidget#folderList::item { padding: 17px 14px; min-height: 34px; border-bottom: 1px solid @LINE@; }
QListWidget#folderList::item:hover { background: @HOVER@; border-radius: 5px; }
QListWidget#folderList::item:selected { background: @SELECTED@; color: @ACCENT@; border-radius: 5px; }
QLabel#emptyTitle { font-size: 25px; font-weight: 500; color: @SECONDARY@; }
QLabel#statusLabel { font-size: 12px; color: @MUTED@; padding: 5px 14px; background: @BG@; }
QDialog, QDialog#preferencesDialog { background: @BG@; }
QDialog QLabel { background: transparent; }
QPushButton {
    background: @INPUT@; border: 1px solid @LINE@; border-radius: 6px;
    padding: 8px 13px; min-height: 18px;
}
QPushButton:hover { background: @HOVER@; }
QPushButton:pressed { background: @SELECTED@; }
QPushButton:checked { background: @SELECTED@; border-color: @ACCENT@; color: @ACCENT@; }
QPushButton:default { border-color: @ACCENT@; }
QPushButton:disabled { color: @MUTED@; background: @SIDEBAR@; }
QToolButton {
    background: transparent; border: 1px solid transparent; border-radius: 5px;
    padding: 6px; min-width: 20px; min-height: 20px;
}
QToolButton:hover { background: @HOVER@; }
QToolButton:pressed, QToolButton:checked { background: @SELECTED@; color: @ACCENT@; }
QToolButton:disabled { color: @MUTED@; }
QToolButton::menu-indicator { image: none; width: 0px; }
QMenu {
    background: @BG@; color: @TEXT@; border: 1px solid @LINE@;
    padding: 6px; border-radius: 7px;
}
QMenu::item { padding: 8px 24px 8px 12px; border-radius: 4px; }
QMenu::item:selected { background: @SELECTED@; color: @ACCENT@; }
QMenu::item:disabled { color: @MUTED@; }
QMenu::separator { height: 1px; background: @LINE@; margin: 5px 4px; }
QToolTip { color: @TEXT@; background: @SIDEBAR@; border: 1px solid @LINE@; padding: 6px; }
QScrollBar:vertical { width: 9px; background: transparent; margin: 2px 0; }
QScrollBar::handle:vertical { background: @LINE@; border-radius: 4px; min-height: 32px; margin: 1px 2px; }
QScrollBar::handle:vertical:hover { background: @MUTED@; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollBar:horizontal { height: 9px; background: transparent; margin: 0 2px; }
QScrollBar::handle:horizontal { background: @LINE@; border-radius: 4px; min-width: 32px; margin: 2px 1px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }
QSlider::groove:horizontal { height: 4px; background: @LINE@; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @ACCENT@; border-radius: 2px; }
QSlider::handle:horizontal { background: @ACCENT@; border: 2px solid @BG@; width: 13px; height: 13px; margin: -6px 0; border-radius: 8px; }
QComboBox { border: 1px solid @LINE@; border-radius: 5px; padding: 7px 10px; background: @INPUT@; min-height: 20px; }
QComboBox:hover { border-color: @ACCENT@; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox QAbstractItemView { background: @BG@; selection-background-color: @SELECTED@; border: 1px solid @LINE@; }
QCheckBox { spacing: 9px; }
QCheckBox::indicator { width: 15px; height: 15px; border: 1px solid @MUTED@; background: @BG@; border-radius: 3px; }
QCheckBox::indicator:checked { background: @ACCENT@; border-color: @ACCENT@; }
QSplitter::handle { background: @LINE@; }
QSplitter::handle:horizontal { width: 1px; }
QStatusBar { background: @BG@; border-top: 1px solid @LINE@; color: @MUTED@; }
QStatusBar::item { border: none; }
)QSS");
    sheet.replace("@BG@", background).replace("@SIDEBAR@", sidebar)
        .replace("@TEXT@", text).replace("@SECONDARY@", secondary)
        .replace("@MUTED@", muted).replace("@LINE@", line)
        .replace("@HOVER@", hover).replace("@SELECTED@", selected)
        .replace("@COVER@", cover).replace("@INPUT@", input)
        .replace("@ACCENT@", accentHex);
    sheet.replace("@CODEBG@", codeBackground).replace("@CODEBORDER@", codeBorder).replace("@CODETEXT@", codeText);
    return sheet;
}

}
