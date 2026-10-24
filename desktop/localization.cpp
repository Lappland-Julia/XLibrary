#include <QLocale>

#include "localization.h"

void Localization::apply(const QString &preference) {
    QCoreApplication::removeTranslator(&russian);
    QCoreApplication::removeTranslator(&qt_russian);
    QString language = preference;
    if (language == "system") {
        language = "en";
        for (const auto &name : QLocale::system().uiLanguages()) {
            const auto candidate = QLocale(name).language();
            if (candidate == QLocale::Russian || candidate == QLocale::English) {
                language = candidate == QLocale::Russian ? "ru" : "en";
                break;
            }
        }
    }
    // Английский текст исходников остаётся доступен при неизвестной локали или ошибке загрузки.
    if (language != "ru" || !russian.load(":/i18n/xlibrary_ru.qm") || !qt_russian.load(":/i18n/qt_widgets_ru.qm"))
        return;
    QCoreApplication::installTranslator(&qt_russian);
    QCoreApplication::installTranslator(&russian);
}
