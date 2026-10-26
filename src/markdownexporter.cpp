#include "markdownexporter.h"
#include "noteeditor.h"
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextList>
#include <QTextTable>

namespace {
QString escaped(QString text) {
    QString result; const QString markers = QStringLiteral("\\`*_{}[]<>#+.!|~-");
    for (const auto c : text) {
        if (c == QChar::LineSeparator) result += QStringLiteral("  \n");
        else { if (markers.contains(c)) result += QLatin1Char('\\'); result += c; }
    }
    return result;
}
int longestRun(const QString &text, QChar character) {
    int best = 0, current = 0;
    for (const auto c : text) { if (c == character) best = qMax(best, ++current); else current = 0; }
    return best;
}
QString destination(QString value) {
    value.replace(QLatin1Char('<'), QStringLiteral("%3C")); value.replace(QLatin1Char('>'), QStringLiteral("%3E"));
    value.replace(QLatin1Char('\n'), QStringLiteral("%0A")); value.replace(QLatin1Char('\r'), QStringLiteral("%0D"));
    return QLatin1Char('<') + value + QLatin1Char('>');
}
QString inlineMarkdown(const QTextBlock &block, const QHash<QString, QString> &references) {
    QString result;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const auto fragment = it.fragment(); if (!fragment.isValid()) continue;
        const auto format = fragment.charFormat(); const QString raw = fragment.text(); QString text = escaped(raw);
        if (format.isImageFormat()) {
            const QString name = format.toImageFormat().name();
            result += QStringLiteral("![图片](") + destination(references.value(name, name)) + QLatin1Char(')'); continue;
        }
        if (format.fontFixedPitch() || format.fontFamily() == QStringLiteral("Consolas")) {
            const QString fence(qMax(1, longestRun(raw, QLatin1Char('`')) + 1), QLatin1Char('`'));
            const bool padding = raw.startsWith(QLatin1Char('`')) || raw.endsWith(QLatin1Char('`')) || (raw.startsWith(QLatin1Char(' ')) && raw.endsWith(QLatin1Char(' ')) && !raw.trimmed().isEmpty());
            QString literal = raw;
            if (QTextCursor(block).currentTable()) literal.replace(QLatin1Char('|'), QStringLiteral("\\|"));
            text = fence + (padding ? QStringLiteral(" ") : QString()) + literal + (padding ? QStringLiteral(" ") : QString()) + fence;
        } else if (!raw.trimmed().isEmpty()) {
            // Delimiters cannot enclose leading/trailing whitespace in CommonMark.
            int start = 0, end = text.size();
            while (start < end && text.at(start).isSpace()) ++start;
            while (end > start && text.at(end - 1).isSpace()) --end;
            QString middle = text.mid(start, end - start);
            if (format.fontWeight() >= QFont::Bold && block.blockFormat().headingLevel() == 0) middle = QStringLiteral("**") + middle + QStringLiteral("**");
            if (format.fontItalic()) middle = QLatin1Char('*') + middle + QLatin1Char('*');
            if (format.fontStrikeOut()) middle = QStringLiteral("~~") + middle + QStringLiteral("~~");
            text = text.left(start) + middle + text.mid(end);
        }
        if (format.isAnchor() && !format.anchorHref().isEmpty()) text = QLatin1Char('[') + text + QStringLiteral("](") + destination(references.value(format.anchorHref(), format.anchorHref())) + QLatin1Char(')');
        result += text;
    }
    return result;
}
QString tableMarkdown(QTextTable *table, const QHash<QString, QString> &references) {
    QString result;
    for (int row = 0; row < table->rows(); ++row) {
        result += QLatin1Char('|');
        for (int column = 0; column < table->columns(); ++column) {
            const auto cell = table->cellAt(row, column); QStringList parts;
            for (QTextBlock block = cell.firstCursorPosition().block(); block.isValid() && block.position() <= cell.lastCursorPosition().block().position(); block = block.next()) parts << inlineMarkdown(block, references);
            QString value = parts.join(QLatin1Char(' ')); value.replace(QLatin1Char('\n'), QLatin1Char(' '));
            result += QLatin1Char(' ') + value + QStringLiteral(" |");
        }
        result += QLatin1Char('\n');
        if (row == 0) { result += QLatin1Char('|'); for (int c = 0; c < table->columns(); ++c) result += QStringLiteral(" --- |"); result += QLatin1Char('\n'); }
    }
    return result + QLatin1Char('\n');
}
}

QString MarkdownExporter::fromDocument(QTextDocument *document, const QString &title, const QHash<QString, QString> &references) {
    QString result = title.isEmpty() ? QString() : QStringLiteral("# ") + escaped(title) + QStringLiteral("\n\n");
    for (QTextBlock block = document->begin(); block.isValid();) {
        QTextCursor cursor(block);
        if (auto *table = cursor.currentTable()) {
            result += tableMarkdown(table, references); block = table->lastCursorPosition().block().next(); continue;
        }
        if (NoteEditor::isCodeBlock(block)) {
            const QString id = NoteEditor::codeBlockId(block); QStringList lines; QString language = NoteEditor::codeLanguageForBlock(block);
            if (language == QStringLiteral("plaintext")) language.clear();
            while (block.isValid() && NoteEditor::isCodeBlock(block) && NoteEditor::codeBlockId(block) == id) { lines << block.text(); block = block.next(); }
            const QString body = lines.join(QLatin1Char('\n'));
            const QString fence(qMax(3, longestRun(body, QLatin1Char('`')) + 1), QLatin1Char('`'));
            result += fence + language + QLatin1Char('\n') + body + QLatin1Char('\n') + fence + QStringLiteral("\n\n"); continue;
        }
        QString line = inlineMarkdown(block, references); const auto format = block.blockFormat();
        if (format.hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth)) line = QStringLiteral("---");
        else if (format.headingLevel() > 0) line.prepend(QString(format.headingLevel(), QLatin1Char('#')) + QLatin1Char(' '));
        else if (block.text().startsWith(QChar(0x2610)) || block.text().startsWith(QChar(0x2611))) {
            line.remove(0, 1); line = (block.text().startsWith(QChar(0x2611)) ? QStringLiteral("- [x] ") : QStringLiteral("- [ ] ")) + line.trimmed();
            if (block.textList()) line.prepend(QString(qMax(0, block.textList()->format().indent() - 1) * 4, QLatin1Char(' ')));
        } else if (auto *list = block.textList()) {
            line.prepend(list->format().style() == QTextListFormat::ListDecimal ? QString::number(list->itemNumber(block) + 1) + QStringLiteral(". ") : QStringLiteral("- "));
            line.prepend(QString(qMax(0, list->format().indent() - 1) * 4, QLatin1Char(' ')));
        } else if (format.leftMargin() >= 18) line.prepend(QStringLiteral("> "));
        result += line + QStringLiteral("\n\n"); block = block.next();
    }
    return result;
}
