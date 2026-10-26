#include "codehighlighter.h"
#include "noteeditor.h"

#include <QColor>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QVector>

namespace {

enum Token { Plain, Keyword, String, Comment, Number, Function, Type, Tag, Attribute, ObjectKey };
enum Continuation { Normal, CComment, PythonSingle, PythonDouble, HtmlComment, JsTemplate, SqlString };

QSet<QString> words(const char *text)
{
    QSet<QString> result;
    for (const QString &word : QString::fromLatin1(text).split(QLatin1Char(' ')))
        if (!word.isEmpty()) result.insert(word);
    return result;
}

const QHash<QString, QSet<QString>> &keywords()
{
    static const QHash<QString, QSet<QString>> values = {
        {QStringLiteral("cpp"), words("alignas alignof and asm auto bitand bitor bool break case catch char char16_t char32_t class compl concept const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not nullptr operator or private protected public register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while xor signals slots emit override final Q_OBJECT Q_PROPERTY Q_INVOKABLE")},
        {QStringLiteral("c"), words("auto break case char const continue default do double else enum extern float for goto if inline int long register restrict return short signed sizeof static struct switch typedef union unsigned void volatile while _Bool _Complex _Imaginary NULL")},
        {QStringLiteral("python"), words("False None True and as assert async await break case class continue def del elif else except finally for from global if import in is lambda match nonlocal not or pass raise return try while with yield self")},
        {QStringLiteral("javascript"), words("async await break case catch class const continue debugger default delete do else export extends false finally for from function get if import in instanceof let new null of return set static super switch this throw true try typeof undefined var void while with yield")},
        {QStringLiteral("typescript"), words("abstract any as async await boolean break case catch class const constructor continue debugger declare default delete do else enum export extends false finally for from function get if implements import in infer instanceof interface is keyof let module namespace never new null number object of private protected public readonly require return set static string super switch symbol this throw true try type typeof undefined unique unknown var void while with yield")},
        {QStringLiteral("json"), words("true false null")},
        {QStringLiteral("sql"), words("ADD ALL ALTER AND ANY AS ASC BACKUP BETWEEN BY CASE CHECK COLUMN CONSTRAINT CREATE CROSS DATABASE DEFAULT DELETE DESC DISTINCT DROP ELSE END EXISTS FOREIGN FROM FULL GROUP HAVING IF IN INDEX INNER INSERT INTO IS JOIN KEY LEFT LIKE LIMIT NOT NULL OFFSET ON OR ORDER OUTER PRIMARY PROCEDURE REFERENCES RIGHT ROW SELECT SET TABLE THEN TOP TRUNCATE UNION UNIQUE UPDATE VALUES VIEW WHEN WHERE WITH TRUE FALSE BEGIN COMMIT ROLLBACK")},
        {QStringLiteral("bash"), words("if then else elif fi for while until in do done case esac function select time coproc break continue return exit export readonly local declare typeset source alias unalias unset set shift trap eval exec echo printf read cd pwd test true false")},
        {QStringLiteral("css"), words("important inherit initial unset revert none auto block inline inline-block flex grid relative absolute fixed sticky hidden visible solid transparent currentColor")},
        {QStringLiteral("java"), words("abstract assert boolean break byte case catch char class const continue default do double else enum extends final finally float for if implements import instanceof int interface long native new package private protected public return short static strictfp super switch synchronized this throw throws transient try void volatile while true false null record sealed permits var")},
        {QStringLiteral("go"), words("break case chan const continue default defer else fallthrough for func go goto if import interface map package range return select struct switch type var true false nil")},
        {QStringLiteral("rust"), words("as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod move mut pub ref return self Self static struct super trait true type unsafe use where while")}
    };
    return values;
}

bool escaped(const QString &text, int position)
{
    int slashes = 0;
    while (position > 0 && text.at(--position) == QLatin1Char('\\')) ++slashes;
    return (slashes % 2) != 0;
}

int closingDelimiter(const QString &text, const QString &delimiter, int start)
{
    int position = text.indexOf(delimiter, start);
    while (position >= 0 && escaped(text, position))
        position = text.indexOf(delimiter, position + delimiter.length());
    return position;
}

QString canonicalLanguage(QString language)
{
    language = language.trimmed().toLower();
    if (language == QStringLiteral("c++") || language == QStringLiteral("cc") || language == QStringLiteral("cxx")) return QStringLiteral("cpp");
    if (language == QStringLiteral("py")) return QStringLiteral("python");
    if (language == QStringLiteral("js") || language == QStringLiteral("jsx") || language == QStringLiteral("qml")) return QStringLiteral("javascript");
    if (language == QStringLiteral("ts") || language == QStringLiteral("tsx")) return QStringLiteral("typescript");
    if (language == QStringLiteral("sh") || language == QStringLiteral("shell") || language == QStringLiteral("zsh")) return QStringLiteral("bash");
    if (language == QStringLiteral("xml") || language == QStringLiteral("svg")) return QStringLiteral("html");
    return language;
}

QTextCharFormat tokenFormat(Token token, bool dark)
{
    static const char *light[] = {"#243041", "#7c3aed", "#15803d", "#6b7280", "#b45309", "#1d4ed8", "#0369a1", "#be185d", "#7c3aed", "#1d4ed8"};
    static const char *night[] = {"#dce6f5", "#c4b5fd", "#86efac", "#94a3b8", "#fdba74", "#93c5fd", "#67e8f9", "#f9a8d4", "#e9d5ff", "#7dd3fc"};
    QTextCharFormat format;
    format.setForeground(QColor(QString::fromLatin1(dark ? night[token] : light[token])));
    if (token == Comment) format.setFontItalic(true);
    return format;
}

} // namespace

CodeHighlighter::CodeHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {}

void CodeHighlighter::setDarkMode(bool dark)
{
    m_dark = dark;
    rehighlight();
}

void CodeHighlighter::highlightBlock(const QString &text)
{
    setCurrentBlockState(Normal);
    const QTextBlock block = currentBlock();
    if (!NoteEditor::isCodeBlock(block)) return;

    setFormat(0, text.length(), tokenFormat(Plain, m_dark));
    const QString language = canonicalLanguage(NoteEditor::codeLanguageForBlock(block));
    const bool cLike = language == QStringLiteral("cpp") || language == QStringLiteral("c")
        || language == QStringLiteral("javascript") || language == QStringLiteral("typescript")
        || language == QStringLiteral("java") || language == QStringLiteral("go") || language == QStringLiteral("rust");
    const bool python = language == QStringLiteral("python");
    const bool bash = language == QStringLiteral("bash");
    const bool sql = language == QStringLiteral("sql");
    const bool html = language == QStringLiteral("html");
    const bool css = language == QStringLiteral("css");
    const bool json = language == QStringLiteral("json");
    if (!cLike && !python && !bash && !sql && !html && !css && !json) return;

    QVector<Token> tokens(text.length(), Plain);
    auto mark = [&](int start, int end, Token token) {
        start = qMax(0, start); end = qMin(text.length(), end);
        for (int i = start; i < end; ++i) tokens[i] = token;
    };
    auto unmarked = [&](int start, int length) {
        if (start < 0 || start + length > tokens.size()) return false;
        for (int i = start; i < start + length; ++i) if (tokens[i] != Plain) return false;
        return true;
    };

    const QTextBlock previous = block.previous();
    int state = Normal;
    if (previous.isValid() && NoteEditor::isCodeBlock(previous)
        && NoteEditor::codeBlockId(previous) == NoteEditor::codeBlockId(block)
        && canonicalLanguage(NoteEditor::codeLanguageForBlock(previous)) == language)
        state = qMax(0, previousBlockState());

    int position = 0;
    while (position < text.length()) {
        if (state != Normal) {
            const QString delimiter = state == CComment ? QStringLiteral("*/")
                : state == PythonSingle ? QStringLiteral("'''")
                : state == PythonDouble ? QStringLiteral("\"\"\"")
                : state == HtmlComment ? QStringLiteral("-->")
                : state == JsTemplate ? QStringLiteral("`") : QStringLiteral("'");
            const int end = state == CComment || state == HtmlComment ? text.indexOf(delimiter, position)
                : closingDelimiter(text, delimiter, position);
            mark(position, end < 0 ? text.length() : end + delimiter.length(),
                 state == CComment || state == HtmlComment ? Comment : String);
            if (end < 0) { position = text.length(); break; }
            position = end + delimiter.length(); state = Normal; continue;
        }
        const bool lineComment = ((cLike && text.mid(position, 2) == QStringLiteral("//"))
            || (sql && text.mid(position, 2) == QStringLiteral("--"))
            || ((python || bash) && text.at(position) == QLatin1Char('#')));
        if (lineComment) {
            const int softBreak = text.indexOf(QChar::LineSeparator, position);
            mark(position, softBreak < 0 ? text.length() : softBreak, Comment);
            if (softBreak < 0) break;
            position = softBreak + 1; continue;
        }
        if ((cLike || css) && text.mid(position, 2) == QStringLiteral("/*")) {
            const int end = text.indexOf(QStringLiteral("*/"), position + 2);
            mark(position, end < 0 ? text.length() : end + 2, Comment);
            if (end < 0) { state = CComment; break; }
            position = end + 2; continue;
        }
        if (html && text.mid(position, 4) == QStringLiteral("<!--")) {
            const int end = text.indexOf(QStringLiteral("-->"), position + 4);
            mark(position, end < 0 ? text.length() : end + 3, Comment);
            if (end < 0) { state = HtmlComment; break; }
            position = end + 3; continue;
        }
        if (python && (text.mid(position, 3) == QStringLiteral("'''") || text.mid(position, 3) == QStringLiteral("\"\"\""))) {
            const QString delimiter = text.mid(position, 3);
            const int end = closingDelimiter(text, delimiter, position + 3);
            mark(position, end < 0 ? text.length() : end + 3, String);
            if (end < 0) { state = delimiter.at(0) == QLatin1Char('\'') ? PythonSingle : PythonDouble; break; }
            position = end + 3; continue;
        }
        const QChar character = text.at(position);
        const bool templateString = (language == QStringLiteral("javascript") || language == QStringLiteral("typescript")) && character == QLatin1Char('`');
        if (character == QLatin1Char('"') || character == QLatin1Char('\'') || templateString) {
            const QString delimiter(character);
            int end = closingDelimiter(text, delimiter, position + 1);
            if (sql && character == QLatin1Char('\'')) {
                // SQL doubles a quote inside a string rather than escaping it.
                while (end >= 0 && end + 1 < text.length() && text.at(end + 1) == character)
                    end = closingDelimiter(text, delimiter, end + 2);
            }
            mark(position, end < 0 ? text.length() : end + 1, String);
            if (end < 0) {
                if (templateString) state = JsTemplate;
                else if (sql && character == QLatin1Char('\'')) state = SqlString;
                break;
            }
            position = end + 1; continue;
        }
        ++position;
    }
    setCurrentBlockState(state);

    auto apply = [&](const QRegularExpression &expression, Token token, int capture = 0) {
        auto matches = expression.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            const int start = match.capturedStart(capture), length = match.capturedLength(capture);
            if (unmarked(start, length)) mark(start, start + length, token);
        }
    };
    static const QRegularExpression number(QStringLiteral("\\b(?:0[xX][0-9A-Fa-f]+|0[bB][01]+|[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)\\b"));
    static const QRegularExpression identifier(QStringLiteral("\\b[A-Za-z_$][A-Za-z0-9_$]*\\b"));
    static const QRegularExpression function(QStringLiteral("\\b[A-Za-z_$][A-Za-z0-9_$]*(?=\\s*\\()"));
    apply(number, Number);
    const QSet<QString> languageWords = keywords().value(language);
    auto identifiers = identifier.globalMatch(text);
    while (identifiers.hasNext()) {
        const auto match = identifiers.next();
        if (languageWords.contains(sql ? match.captured().toUpper() : match.captured())
            && unmarked(match.capturedStart(), match.capturedLength()))
            mark(match.capturedStart(), match.capturedEnd(), Keyword);
    }
    if (cLike || python || bash) apply(function, Function);
    if (language == QStringLiteral("cpp")) apply(QRegularExpression(QStringLiteral("\\bQ[A-Z][A-Za-z0-9_]*\\b")), Type);
    if (bash) apply(QRegularExpression(QStringLiteral("\\$(?:[A-Za-z_][A-Za-z0-9_]*|\\{[A-Za-z_][A-Za-z0-9_]*\\})")), Type);
    if (html) {
        apply(QRegularExpression(QStringLiteral("</?\\s*([A-Za-z][A-Za-z0-9:_-]*)")), Tag, 1);
        apply(QRegularExpression(QStringLiteral("\\b([A-Za-z_:][A-Za-z0-9:_-]*)(?=\\s*=)")), Attribute, 1);
    }
    if (css) {
        apply(QRegularExpression(QStringLiteral("(?<![A-Za-z0-9_-])[.#][A-Za-z_][A-Za-z0-9_-]*")), Tag);
        apply(QRegularExpression(QStringLiteral("\\b([A-Za-z_-][A-Za-z0-9_-]*)(?=\\s*:)")), Attribute, 1);
        apply(QRegularExpression(QStringLiteral("#[A-Fa-f0-9]{3,8}\\b")), Number);
    }
    if (json) {
        static const QRegularExpression key(QStringLiteral("\"(?:\\\\.|[^\"\\\\])*\"(?=\\s*:)"));
        auto matches = key.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            bool string = true;
            for (int i = match.capturedStart(); i < match.capturedEnd(); ++i) if (tokens[i] != String) string = false;
            if (string) mark(match.capturedStart(), match.capturedEnd(), ObjectKey);
        }
    }

    for (int start = 0; start < tokens.size();) {
        int end = start + 1;
        while (end < tokens.size() && tokens[end] == tokens[start]) ++end;
        if (tokens[start] != Plain) setFormat(start, end - start, tokenFormat(tokens[start], m_dark));
        start = end;
    }
}
