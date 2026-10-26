#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace AppTheme {
QIcon icon(const QString &name, const QColor &color = QColor("#85917a"));
QString styleSheet(bool dark, const QColor &accent = QColor("#7771b5"));
}
