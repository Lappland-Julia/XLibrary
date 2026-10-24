#ifndef XLIBRARY_LOCALIZATION_H
#define XLIBRARY_LOCALIZATION_H

#include <QCoreApplication>
#include <QTranslator>

class Localization {
    QTranslator russian;
    QTranslator qt_russian;

public:
    void apply(const QString &preference = "system");
};

#endif
