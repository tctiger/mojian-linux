#pragma once
#include <QtWidgets>
namespace Mojian {
QString expandMath(const QString &source, const QString &preamble, bool *ok = nullptr);
QImage mathImage(const QString &source, qreal size = 20, QColor color = Qt::black);
QImage diagramImage(const QString &source);
}
