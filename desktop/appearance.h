#ifndef XLIBRARY_APPEARANCE_H
#define XLIBRARY_APPEARANCE_H

#include <QIcon>
#include <QPalette>
#include <QSettings>

class QPainter;

namespace appearance {
QIcon icon(const QString &name, const QColor &color = QColor("#AAB4C1"));
QIcon logo();
void apply(QSettings &settings, const QString &additional_style = {});
bool is_dark(QSettings &settings);
int scaled(QSettings &settings, int size);
void paint_selection(QPainter &painter, const QRectF &rect, const QPalette &palette, qreal radius = 10);
}

#endif
