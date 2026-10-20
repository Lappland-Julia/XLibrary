#ifndef XLIBRARY_APPEARANCE_H
#define XLIBRARY_APPEARANCE_H

#include <QIcon>
#include <QSettings>

namespace appearance {
QIcon icon(const QString &name, const QColor &color = QColor("#AAB4C1"));
void apply(QSettings &settings, const QString &additional_style = {});
bool is_dark(QSettings &settings);
int scaled(QSettings &settings, int size);
}

#endif
