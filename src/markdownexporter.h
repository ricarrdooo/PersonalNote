#pragma once
#include <QHash>
#include <QString>
class QTextDocument;
namespace MarkdownExporter {
QString fromDocument(QTextDocument *document, const QString &title, const QHash<QString, QString> &references = {});
}
