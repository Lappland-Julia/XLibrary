#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QKeySequenceEdit>
#include <QStackedWidget>
#include <QUrl>

#include "desktop_service.h"
#include "platform_integration.h"
#include "selection_button.h"
#include "settings_view.h"
#include "ui_helpers.h"

namespace {
using namespace ui_helpers;

QWidget *page(const QString &title) {
    auto *widget = new QWidget;
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(24, 20, 24, 24);
    layout->setSpacing(12);
    layout->addWidget(label(title, "subheading"));
    layout->addSpacing(8);
    return widget;
}

void checkbox(QWidget *page, QSettings &settings, const QString &key, const QString &text, bool default_value) {
    auto *control = new QCheckBox(text);
    control->setProperty("setting_key", key);
    control->setChecked(settings.value(key, default_value).toBool());
    page->layout()->addWidget(control);
}

void choices(QWidget *page, QSettings &settings, const QString &key, const QString &text,
             const QList<QPair<QString, QVariant>> &values, const QVariant &default_value) {
    auto *control = new QComboBox;
    control->setProperty("setting_key", key);
    for (const auto &value : values)
        control->addItem(value.first, value.second);
    const auto index = control->findData(settings.value(key, default_value));
    control->setCurrentIndex(qMax(0, index));
    field(static_cast<QVBoxLayout *>(page->layout()), text, control);
}

QWidget *general(const ui_context &context, QWidget *) {
    auto *widget = page("Поведение приложения");
    auto &settings = *context.settings;
    checkbox(widget, settings, "behavior/autostart", "Запускать при входе в систему", false);
    checkbox(widget, settings, "behavior/close_to_tray", "Сворачивать в трей при закрытии", true);
    choices(widget, settings, "behavior/after_launch", "После запуска приложения",
            {{"Оставить окно открытым", "keep"}, {"Свернуть окно", "minimize"}, {"Скрыть в трей", "hide"}}, "keep");
    choices(widget, settings, "behavior/relaunch", "Повторный запуск",
            {{"Переключиться на открытое окно", "activate"}, {"Открыть новый экземпляр", "new"}}, "activate");
    choices(widget, settings, "behavior/startup", "При запуске",
            {{"Показать библиотеку", "show"}, {"Запустить в трее", "tray"}}, "show");
    static_cast<QVBoxLayout *>(widget->layout())->addStretch();
    return widget;
}

QWidget *appearance_page(const ui_context &context, QWidget *) {
    auto *widget = page("Оформление");
    auto &settings = *context.settings;
    choices(widget, settings, "appearance/theme", "Тема",
            {{"Тёмная", "dark"}, {"Светлая", "light"}, {"Системная", "system"}}, "dark");
#ifdef Q_OS_WIN
    checkbox(widget, settings, "appearance/glass", "Системный материал рамки Windows 11", false);
    widget->layout()->addWidget(
        label("В Windows 10 используется обычная непрозрачная рамка. Список и текст остаются непрозрачными.", "muted"));
#endif
    choices(widget, settings, "appearance/text_scale", "Размер текста",
            {{"100%", 100}, {"125%", 125}, {"150%", 150}, {"200%", 200}}, 100);
    widget->layout()->addWidget(
        label("Короткие плавные переходы учитывают доступные системные настройки анимации.", "muted"));
    static_cast<QVBoxLayout *>(widget->layout())->addStretch();
    return widget;
}

QWidget *hotkeys(const ui_context &context, QWidget *) {
    auto *widget = page("Горячие клавиши");
    const QList<QStringList> values{
        {"shortcuts/global", "Показать библиотеку из любого приложения", desktop_platform::default_global_shortcut()},
        {"shortcuts/search", "Поиск в библиотеке", "Ctrl+K"},
        {"shortcuts/add", "Добавить приложение", "Ctrl+N"},
        {"shortcuts/settings", "Открыть настройки", "Ctrl+,"}};
    for (const auto &value : values) {
        auto *control = new QKeySequenceEdit(QKeySequence::fromString(
            context.settings->value(value[0], value[2]).toString(), QKeySequence::PortableText));
        control->setMaximumSequenceLength(1);
        control->setClearButtonEnabled(true);
        control->setProperty("setting_key", value[0]);
        field(static_cast<QVBoxLayout *>(widget->layout()), value[1], control);
    }
#ifdef Q_OS_LINUX
    widget->layout()->addWidget(
        label("В Wayland назначьте сочетание в настройках рабочего стола для команды XLibrary. "
              "Она открывает уже запущенную библиотеку. Встроенное глобальное сочетание доступно в X11.",
              "muted"));
#endif
    widget->layout()->addWidget(label("Пустое поле отключает сочетание. Enter запускает выбранное приложение; ↑↓ "
                                      "выбирают строку. Escape очищает поиск.",
                                      "muted"));
    static_cast<QVBoxLayout *>(widget->layout())->addStretch();
    return widget;
}

QWidget *library_settings(const ui_context &context, QWidget *) {
    auto *service = static_cast<DesktopService *>(context.service);
    auto *widget = page("Источники приложений");
#ifdef Q_OS_WIN
    checkbox(widget, *context.settings, "sources/registry", "Зарегистрированные приложения Windows", true);
    checkbox(widget, *context.settings, "sources/start_menu", "Ярлыки меню «Пуск»", true);
#else
    checkbox(widget, *context.settings, "sources/registry", "Системные приложения Linux", true);
    checkbox(widget, *context.settings, "sources/start_menu", "Приложения пользователя Linux", true);
#endif
    widget->layout()->addWidget(label(
        "Поиск выполняется по запросу. Изменённые источники будут использованы после сохранения настроек.", "muted"));
    widget->layout()->addWidget(action_button(context.actions.value("discover")));
    widget->layout()->addWidget(context.registry->create("discovery_progress", context, widget));
    widget->layout()->addWidget(label("Импорт и экспорт библиотеки", "subheading"));
    widget->layout()->addWidget(label("JSON сохраняет приложения, параметры, избранное и теги. Существующие записи при "
                                      "импорте сохраняются. Приложения автоматически не запускаются.",
                                      "muted"));
    auto *import = button("Импортировать…");
    auto *export_button = button("Экспортировать…");
    widget->layout()->addWidget(import);
    widget->layout()->addWidget(export_button);
    QObject::connect(import, &QPushButton::clicked, widget, [=] {
        const auto path = QFileDialog::getOpenFileName(widget, "Импорт библиотеки", {}, "Библиотека XLibrary (*.json)");
        if (!path.isEmpty())
            service->import_catalog(path);
    });
    QObject::connect(export_button, &QPushButton::clicked, widget, [=] {
        const auto path =
            QFileDialog::getSaveFileName(widget, "Экспорт библиотеки", "xlibrary.json", "Библиотека XLibrary (*.json)");
        if (!path.isEmpty())
            service->export_catalog(path);
    });
    auto *clear = button("Очистить историю запусков");
    widget->layout()->addWidget(clear);
    QObject::connect(clear, &QPushButton::clicked, widget, [=] {
        if (confirm(widget, "История запусков", "Очистить время последних запусков? Приложения и теги сохранятся.",
                    "Очистить"))
            service->clear_history();
    });
    static_cast<QVBoxLayout *>(widget->layout())->addStretch();
    return widget;
}

QWidget *plugins(const ui_context &context, QWidget *) {
    auto *widget = page("Модули интерфейса");
    widget->layout()->addWidget(
        label("Нативные плагины могут менять области интерфейса и оформление. Включайте только доверенные модули; они "
              "выполняются внутри XLibrary. Изменения применяются после перезапуска.",
              "muted"));
    const auto available = context.parameters.value("available_plugins").toStringList();
    const auto enabled = context.settings->value("plugins/enabled").toStringList();
    for (const auto &id : available) {
        auto *control = new QCheckBox(id);
        control->setProperty("plugin_id", id);
        control->setChecked(enabled.contains(id));
        widget->layout()->addWidget(control);
    }
    if (available.isEmpty())
        widget->layout()->addWidget(
            label("Модули не установлены. Каталог: plugins рядом с исполняемым файлом XLibrary.", "muted"));
    static_cast<QVBoxLayout *>(widget->layout())->addStretch();
    return widget;
}

QWidget *about(const ui_context &context, QWidget *) {
    auto *widget = page("О программе");
    auto *card = new QFrame;
    card->setProperty("card", true);
    auto *card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(24, 24, 24, 24);
    card_layout->setSpacing(12);
    auto *identity = new QHBoxLayout;
    identity->setSpacing(16);
    auto *logo = new QLabel;
    const auto logo_size = appearance::scaled(*context.settings, 64);
    logo->setPixmap(appearance::logo().pixmap(logo_size, logo_size));
    identity->addWidget(logo, 0, Qt::AlignTop);
    auto *title = new QVBoxLayout;
    title->addWidget(label("XLibrary", "heading"));
    auto *version = label("Версия " + QCoreApplication::applicationVersion(), "muted");
    version->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    title->addWidget(version);
    identity->addLayout(title, 1);
    card_layout->addLayout(identity);
    card_layout->addWidget(label("Библиотека приложений для Windows и Linux", "subheading"));
    card_layout->addWidget(label("Приложения, теги и сценарии в одном месте. Каталог хранится локально; интернет для "
                                 "работы не требуется.",
                                 "muted"));
    auto *github = button("Открыть GitHub ↗");
    auto *issues = button("Сообщить о проблеме ↗");
    card_layout->addWidget(github);
    card_layout->addWidget(issues);
    QObject::connect(github, &QPushButton::clicked, widget,
                     [] { QDesktopServices::openUrl(QUrl("https://github.com/Lappland-Julia/XLibrary")); });
    QObject::connect(issues, &QPushButton::clicked, widget,
                     [] { QDesktopServices::openUrl(QUrl("https://github.com/Lappland-Julia/XLibrary/issues")); });
    widget->layout()->addWidget(card);
    widget->layout()->addWidget(label("Технологии и лицензии", "subheading"));
    widget->layout()->addWidget(label(
        QString("Qt %1 · C++23 · SQLite\nXLibrary — MIT\nЛицензии сторонних компонентов входят в комплект программы.")
            .arg(QString::fromLatin1(qVersion())),
        "muted"));
    static_cast<QVBoxLayout *>(widget->layout())->addStretch();
    return widget;
}

QDialog *settings_dialog(const ui_context &context, QWidget *parent) {
    auto *service = static_cast<DesktopService *>(context.service);
    auto *hotkey = static_cast<GlobalHotkey *>(context.parameters.value("hotkey").value<QObject *>());
    auto *dialog = new QDialog(parent);
    dialog->setWindowTitle("Настройки XLibrary");
    prepare_dialog(dialog, context, {960, 700}, {640, 420});
    auto *root = new QVBoxLayout(dialog);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(16);
    root->addWidget(label("Настройки", "heading"));
    root->addWidget(label("Оформление, поведение и возможности XLibrary", "muted"));
    auto *body = new QHBoxLayout;
    body->setSpacing(16);
    auto *categories = new QButtonGroup(dialog);
    auto *navigation = new QWidget;
    navigation->setObjectName("settings_categories");
    auto *navigation_layout = new QVBoxLayout(navigation);
    navigation_layout->setContentsMargins(0, 4, 0, 4);
    navigation_layout->setSpacing(8);
    auto *pages = new QStackedWidget;
    const QList<QPair<QString, QString>> sections{
        {"Общие", "settings_general"},           {"Внешний вид", "settings_appearance"},
        {"Горячие клавиши", "settings_hotkeys"}, {"Библиотека", "settings_library"},
        {"Плагины", "settings_plugins"},         {"О программе", "settings_about"}};
    for (const auto &section : sections) {
        const auto index = pages->count();
        auto *item = new SelectionButton(section.first);
        categories->addButton(item, index);
        navigation_layout->addWidget(item);
        auto *holder = new QWidget;
        auto *page_layout = new QHBoxLayout(holder);
        page_layout->setContentsMargins(0, 0, 0, 0);
        auto *contents = context.registry->create(section.second, context, holder);
        contents->setMaximumWidth(appearance::scaled(*context.settings, 640));
        page_layout->addWidget(contents, 1);
        page_layout->addStretch();
        pages->addWidget(scroll(holder));
    }
    navigation_layout->addStretch();
    auto *navigation_scroll = scroll(navigation);
    navigation_scroll->setMinimumWidth(appearance::scaled(*context.settings, 184));
    navigation_scroll->setMaximumWidth(appearance::scaled(*context.settings, 220));
    auto *transition = new PageTransition(pages);
    body->addWidget(navigation_scroll);
    auto *surface = new QFrame;
    surface->setObjectName("settings_surface");
    surface->setProperty("card", true);
    auto *surface_layout = new QVBoxLayout(surface);
    surface_layout->setContentsMargins(12, 12, 12, 12);
    surface_layout->addWidget(transition);
    body->addWidget(surface, 1);
    root->addLayout(body, 1);
    QObject::connect(categories, &QButtonGroup::idClicked, pages, [=](int index) {
        if (pages->currentIndex() == index)
            return;
        pages->setCurrentIndex(index);
        transition->start();
    });
    categories->button(0)->setChecked(true);
    auto *error = label("", "error");
    error->hide();
    root->addWidget(error);
    auto show_error = [=](const QString &text) {
        error->setText(text);
        error->setVisible(!text.isEmpty());
    };
    QObject::connect(service, &DesktopService::message, error, show_error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText("Сохранить");
    buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Cancel)->setText("Отмена");
    root->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
        QVariantMap values;
        QSet<QString> sequences;
        for (auto *control : dialog->findChildren<QWidget *>()) {
            const auto key = control->property("setting_key").toString();
            if (key.isEmpty())
                continue;
            if (auto *box = qobject_cast<QCheckBox *>(control))
                values.insert(key, box->isChecked());
            if (auto *combo = qobject_cast<QComboBox *>(control))
                values.insert(key, combo->currentData());
            if (auto *edit = qobject_cast<QKeySequenceEdit *>(control)) {
                const auto value = edit->keySequence().toString(QKeySequence::PortableText);
                if (!value.isEmpty() && sequences.contains(value)) {
                    show_error("Одинаковые сочетания назначены нескольким действиям.");
                    categories->button(2)->click();
                    edit->setFocus();
                    return;
                }
                if (!value.isEmpty())
                    sequences.insert(value);
                values.insert(key, value);
            }
        }
        QStringList enabled;
        for (auto *box : dialog->findChildren<QCheckBox *>()) {
            const auto id = box->property("plugin_id").toString();
            if (!id.isEmpty() && box->isChecked())
                enabled.append(id);
        }
        values.insert("plugins/enabled", enabled);
        const auto previous_global = QKeySequence::fromString(
            service->settings.value("shortcuts/global", desktop_platform::default_global_shortcut()).toString(),
            QKeySequence::PortableText);
        QString failure;
        if (hotkey &&
            previous_global !=
                QKeySequence::fromString(values.value("shortcuts/global").toString(), QKeySequence::PortableText) &&
            !hotkey->set_sequence(
                QKeySequence::fromString(values.value("shortcuts/global").toString(), QKeySequence::PortableText),
                failure)) {
            show_error(failure);
            categories->button(2)->click();
            return;
        }
        const bool old_autostart = service->settings.value("behavior/autostart", false).toBool();
        const bool new_autostart = values.value("behavior/autostart").toBool();
        if (old_autostart != new_autostart && !desktop_platform::set_autostart(new_autostart, failure)) {
            if (hotkey) {
                QString ignored;
                hotkey->set_sequence(previous_global, ignored);
            }
            show_error(failure);
            return;
        }
        QVariantMap previous;
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            previous.insert(it.key(), service->settings.value(it.key()));
            service->settings.setValue(it.key(), it.value());
        }
        service->settings.sync();
        if (service->settings.status() != QSettings::NoError) {
            for (auto it = previous.cbegin(); it != previous.cend(); ++it)
                service->settings.setValue(it.key(), it.value());
            if (hotkey) {
                QString ignored;
                hotkey->set_sequence(previous_global, ignored);
            }
            if (old_autostart != new_autostart) {
                QString ignored;
                desktop_platform::set_autostart(old_autostart, ignored);
            }
            show_error("Не удалось сохранить файл настроек.");
            return;
        }
        emit service->appearance_changed();
        dialog->accept();
    });
    return dialog;
}
}

void register_settings_views(UiRegistry &registry) {
    registry.add_default("settings", settings_dialog);
    registry.add_default("settings_general", general);
    registry.add_default("settings_appearance", appearance_page);
    registry.add_default("settings_hotkeys", hotkeys);
    registry.add_default("settings_library", library_settings);
    registry.add_default("settings_plugins", plugins);
    registry.add_default("settings_about", about);
}
