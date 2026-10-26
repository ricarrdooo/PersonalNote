#pragma once
#include <QString>
#include <QStringList>

class NoteStore;

struct MarkdownImportResult {
    QString noteId;
    QString error;
    QStringList warnings;
};

namespace MarkdownImporter {
MarkdownImportResult importFile(NoteStore &store, const QString &path, const QString &parentId = QString());
}
