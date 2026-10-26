#include <QLocale>

#include "localization.h"

QString Localization::ShortcutTranslator::translate(const char *context, const char *source_text, const char *,
                                                    int) const {
    // Меняем только подпись Qt; коды клавиш и PortableText в настройках остаются прежними.
    if (qstrcmp(context, "QShortcut") == 0 && qstrcmp(source_text, "Return") == 0)
        return QStringLiteral("Enter");
    return {};
}

void Localization::apply(const QString &preference) {
    QCoreApplication::removeTranslator(&shortcut_names);
    QCoreApplication::removeTranslator(&russian);
    QCoreApplication::removeTranslator(&qt_russian);
    QCoreApplication::installTranslator(&shortcut_names);
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
