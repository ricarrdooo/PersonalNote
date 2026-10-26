#include "markdownimporter.h"
#include "notestore.h"
#include "noteeditor.h"
#include "../third_party/md4c/md4c.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTextDocumentFragment>
#include <QUrl>
#include <QUuid>

namespace {
QString attribute(const MD_ATTRIBUTE &value) {
    const QString raw = QString::fromUtf8(value.text, int(value.size));
    // Attributes from MD4C have backslash escapes resolved, but still contain entities.
    return QTextDocumentFragment::fromHtml(raw.toHtmlEscaped().replace(QStringLiteral("&amp;"), QStringLiteral("&"))).toPlainText();
}

struct Asset { QString token, target; bool image = false; int fileIndex = -1; QString alt; };
struct Code { QString token, text, language; };
struct Renderer {
    QString html, root;
    QStringList files, warnings;
    QList<Asset> assets;
    QList<Code> codes;
    bool inCode = false;
    int tableDepth = 0;
    int inlineCodeDepth = 0;
    int imageDepth = 0;
    QString imageTarget, imageAlt;
    QList<bool> links;
    QString token() const { return QStringLiteral("shiye-import-") + QUuid::createUuid().toString(QUuid::WithoutBraces); }
    void warn(const QString &message) { if (!warnings.contains(message)) warnings.append(message); }
    int localAsset(const QString &target) {
        const QUrl url(target);
        if (!url.scheme().isEmpty() || !url.host().isEmpty() || target.startsWith(QLatin1Char('#'))) return -1;
        const QString decoded = QUrl::fromPercentEncoding(url.path(QUrl::FullyEncoded).toUtf8());
        if (decoded.isEmpty() || QDir::isAbsolutePath(decoded) || decoded.startsWith(QStringLiteral("\\"))) return -1;
        const QFileInfo info(QDir(root).filePath(decoded));
        const QString canonical = info.canonicalFilePath();
        if (!info.isFile() || !info.isReadable() || canonical.isEmpty() ||
            !canonical.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive)) return -1;
        for (int i = 0; i < files.size(); ++i) if (files.at(i).compare(canonical, Qt::CaseInsensitive) == 0) return i;
        files.append(canonical); return files.size() - 1;
    }
    QString asset(const QString &target, bool image, const QString &alt = QString()) {
        const int index = localAsset(target);
        const QUrl url(target);
        if (index < 0) {
            if (image) warn(QStringLiteral("图片未复制：%1（保留地址，未下载网络图片或读取源目录外文件）").arg(target));
            else if (url.scheme().isEmpty() && !target.startsWith(QLatin1Char('#'))) warn(QStringLiteral("本地链接未复制：%1").arg(target));
        }
        Asset item; item.token = token(); item.target = target; item.image = image; item.fileIndex = index; item.alt = alt;
        assets.append(item); return item.token;
    }
    int bounded() const { return html.size() > 32 * 1024 * 1024 ? -1 : 0; }
};

int enterBlock(MD_BLOCKTYPE type, void *detail, void *userdata) {
    auto &r = *static_cast<Renderer *>(userdata);
    switch (type) {
    case MD_BLOCK_P: r.html += QStringLiteral("<p>"); break;
    case MD_BLOCK_H: r.html += QStringLiteral("<h%1>").arg(static_cast<MD_BLOCK_H_DETAIL *>(detail)->level); break;
    case MD_BLOCK_QUOTE: r.html += QStringLiteral("<blockquote>"); break;
    case MD_BLOCK_UL: r.html += QStringLiteral("<ul>"); break;
    case MD_BLOCK_OL: r.html += QStringLiteral("<ol start=\"%1\">").arg(static_cast<MD_BLOCK_OL_DETAIL *>(detail)->start); break;
    case MD_BLOCK_LI: {
        r.html += QStringLiteral("<li>");
        const auto *li = static_cast<MD_BLOCK_LI_DETAIL *>(detail);
        if (li->is_task) r.html += li->task_mark == ' ' ? QStringLiteral("☐ ") : QStringLiteral("☑ ");
        break;
    }
    case MD_BLOCK_HR: r.html += QStringLiteral("<hr />"); break;
    case MD_BLOCK_CODE: {
        const auto *code = static_cast<MD_BLOCK_CODE_DETAIL *>(detail);
        Code item; item.token = r.token(); item.language = attribute(code->lang);
        if (item.language.startsWith(QStringLiteral("language-"))) item.language.remove(0, 9);
        r.codes.append(item); r.inCode = true;
        r.html += QStringLiteral("<p>") + item.token + QStringLiteral("</p>"); break;
    }
    case MD_BLOCK_TABLE: ++r.tableDepth; r.html += QStringLiteral("<table border=\"1\" cellspacing=\"0\" cellpadding=\"6\">"); break;
    case MD_BLOCK_TR: r.html += QStringLiteral("<tr>"); break;
    case MD_BLOCK_TH: r.html += QStringLiteral("<th>"); break;
    case MD_BLOCK_TD: r.html += QStringLiteral("<td>"); break;
    default: break;
    }
    return r.bounded();
}
int leaveBlock(MD_BLOCKTYPE type, void *detail, void *userdata) {
    auto &r = *static_cast<Renderer *>(userdata);
    switch (type) {
    case MD_BLOCK_P: r.html += QStringLiteral("</p>"); break;
    case MD_BLOCK_H: r.html += QStringLiteral("</h%1>").arg(static_cast<MD_BLOCK_H_DETAIL *>(detail)->level); break;
    case MD_BLOCK_QUOTE: r.html += QStringLiteral("</blockquote>"); break;
    case MD_BLOCK_UL: r.html += QStringLiteral("</ul>"); break;
    case MD_BLOCK_OL: r.html += QStringLiteral("</ol>"); break;
    case MD_BLOCK_LI: r.html += QStringLiteral("</li>"); break;
    case MD_BLOCK_CODE: r.inCode = false; if (r.codes.last().text.endsWith(QLatin1Char('\n'))) r.codes.last().text.chop(1); break;
    case MD_BLOCK_TABLE: --r.tableDepth; r.html += QStringLiteral("</table>"); break;
    case MD_BLOCK_TR: r.html += QStringLiteral("</tr>"); break;
    case MD_BLOCK_TH: r.html += QStringLiteral("</th>"); break;
    case MD_BLOCK_TD: r.html += QStringLiteral("</td>"); break;
    default: break;
    }
    return r.bounded();
}
int enterSpan(MD_SPANTYPE type, void *detail, void *userdata) {
    auto &r = *static_cast<Renderer *>(userdata);
    if (type == MD_SPAN_IMG) {
        if (r.imageDepth++ == 0) { r.imageTarget = attribute(static_cast<MD_SPAN_IMG_DETAIL *>(detail)->src); r.imageAlt.clear(); }
        return 0;
    }
    if (r.imageDepth) return 0;
    switch (type) {
    case MD_SPAN_EM: r.html += QStringLiteral("<em>"); break;
    case MD_SPAN_STRONG: r.html += QStringLiteral("<strong>"); break;
    case MD_SPAN_DEL: r.html += QStringLiteral("<s>"); break;
    case MD_SPAN_CODE: ++r.inlineCodeDepth; r.html += QStringLiteral("<code style=\"font-family:Consolas\">"); break;
    case MD_SPAN_A: {
        const QString target = attribute(static_cast<MD_SPAN_A_DETAIL *>(detail)->href);
        const QString scheme = QUrl(target).scheme().toLower();
        const bool allowed = scheme.isEmpty() || scheme == QStringLiteral("https") || scheme == QStringLiteral("http") || scheme == QStringLiteral("mailto");
        r.links.append(allowed);
        if (allowed) r.html += QStringLiteral("<a href=\"") + r.asset(target, false) + QStringLiteral("\">");
        else r.warn(QStringLiteral("链接地址未启用：%1").arg(target));
        break;
    }
    default: break;
    }
    return r.bounded();
}
int leaveSpan(MD_SPANTYPE type, void *, void *userdata) {
    auto &r = *static_cast<Renderer *>(userdata);
    if (type == MD_SPAN_IMG) {
        if (--r.imageDepth == 0) r.html += r.asset(r.imageTarget, true, r.imageAlt);
        return r.bounded();
    }
    if (r.imageDepth) return 0;
    switch (type) {
    case MD_SPAN_EM: r.html += QStringLiteral("</em>"); break;
    case MD_SPAN_STRONG: r.html += QStringLiteral("</strong>"); break;
    case MD_SPAN_DEL: r.html += QStringLiteral("</s>"); break;
    case MD_SPAN_CODE: --r.inlineCodeDepth; r.html += QStringLiteral("</code>"); break;
    case MD_SPAN_A: if (!r.links.isEmpty() && r.links.takeLast()) r.html += QStringLiteral("</a>"); break;
    default: break;
    }
    return r.bounded();
}
int text(MD_TEXTTYPE type, const MD_CHAR *content, MD_SIZE size, void *userdata) {
    auto &r = *static_cast<Renderer *>(userdata);
    QString value = QString::fromUtf8(content, int(size));
    if (r.tableDepth && r.inlineCodeDepth) value.replace(QStringLiteral("\\|"), QStringLiteral("|"));
    if (r.inCode) { r.codes.last().text += value; return 0; }
    if (r.imageDepth) { r.imageAlt += value; return 0; }
    if (type == MD_TEXT_BR) r.html += QStringLiteral("<br />");
    else if (type == MD_TEXT_SOFTBR) r.html += QLatin1Char(' ');
    else if (type == MD_TEXT_ENTITY) r.html += value;
    else if (type == MD_TEXT_NULLCHAR) r.html += QChar::ReplacementCharacter;
    else r.html += value.toHtmlEscaped();
    return r.bounded();
}
}

MarkdownImportResult MarkdownImporter::importFile(NoteStore &store, const QString &path, const QString &parentId) {
    MarkdownImportResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { result.error = QStringLiteral("无法读取 Markdown 文件：") + file.errorString(); return result; }
    if (file.size() > 8 * 1024 * 1024) { result.error = QStringLiteral("单篇 Markdown 文件最大支持 8 MB，请拆分后导入。"); return result; }
    const QByteArray originalBytes = file.readAll(); QByteArray bytes = originalBytes;
    if (file.error() != QFile::NoError) { result.error = QStringLiteral("Markdown 文件读取未完成。"); return result; }
    file.close();
    if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);
    const QString decoded = QString::fromUtf8(bytes);
    if (decoded.toUtf8() != bytes || bytes.contains('\0')) { result.error = QStringLiteral("请将 Markdown 文件保存为 UTF-8 编码后再导入。"); return result; }
    Renderer renderer; renderer.root = QFileInfo(QFileInfo(path).absolutePath()).canonicalFilePath();
    renderer.files.append(QFileInfo(path).canonicalFilePath());
    MD_PARSER parser{}; parser.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS | MD_FLAG_NOHTML;
    parser.enter_block = enterBlock; parser.leave_block = leaveBlock; parser.enter_span = enterSpan; parser.leave_span = leaveSpan; parser.text = text;
    if (md_parse(bytes.constData(), MD_SIZE(bytes.size()), &parser, &renderer) != 0) { result.error = QStringLiteral("Markdown 转换失败或文档过于复杂，原文件未改变。"); return result; }
    result.noteId = store.importNoteBundle(QFileInfo(path).completeBaseName(), parentId, renderer.files, [&](const QList<Attachment> &attachments) {
        QFile savedSource(QDir(store.dataDirectory()).filePath(attachments.first().relativePath));
        if (!savedSource.open(QIODevice::ReadOnly) || savedSource.readAll() != originalBytes) return QPair<QString, QString>();
        QString html = renderer.html;
        for (const auto &asset : renderer.assets) {
            QString replacement;
            if (asset.fileIndex >= 0) {
                const auto &attachment = attachments.at(asset.fileIndex);
                if (asset.image) {
                    QImageReader image(QDir(store.dataDirectory()).filePath(attachment.relativePath)); const QSize size = image.size();
                    if (size.isValid()) {
                        const QSize display = size.width() > 720 || size.height() > 1600 ? size.scaled(720, 1600, Qt::KeepAspectRatio) : size;
                        replacement = QStringLiteral("<img src=\"%1\" alt=\"%2\" width=\"%3\" height=\"%4\" />").arg(attachment.relativePath.toHtmlEscaped(), asset.alt.toHtmlEscaped()).arg(display.width()).arg(display.height());
                    } else {
                        replacement = QStringLiteral("<a href=\"attachment:%1\">[图片附件：%2]</a>").arg(attachment.id, attachment.originalName.toHtmlEscaped());
                        renderer.warn(QStringLiteral("图片暂不能预览，已作为附件保存：%1").arg(asset.target));
                    }
                } else replacement = QStringLiteral("attachment:") + attachment.id;
            } else if (asset.image) replacement = QStringLiteral("[图片：%1 — %2]").arg(asset.alt.toHtmlEscaped(), asset.target.toHtmlEscaped());
            else replacement = asset.target.toHtmlEscaped();
            html.replace(asset.token, replacement);
        }
        NoteEditor editor; editor.setLibraryDirectory(store.dataDirectory()); editor.setNoteHtml(html);
        for (const auto &code : renderer.codes) {
            QTextCursor cursor = editor.document()->find(code.token);
            if (cursor.isNull()) return QPair<QString, QString>();
            cursor.removeSelectedText(); editor.setTextCursor(cursor); editor.applyBlock(QStringLiteral("code"));
            editor.insertPlainText(code.text); editor.setCurrentCodeLanguage(code.language);
        }
        return qMakePair(editor.noteHtml(), editor.toPlainText());
    }, &result.error);
    result.warnings = renderer.warnings;
    return result;
}
