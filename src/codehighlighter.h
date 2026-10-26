#pragma once

#include <QSyntaxHighlighter>

class QTextDocument;

class CodeHighlighter : public QSyntaxHighlighter
{
public:
    explicit CodeHighlighter(QTextDocument *document);
    void setDarkMode(bool dark);

protected:
    void highlightBlock(const QString &text) override;

private:
    bool m_dark = false;
};
