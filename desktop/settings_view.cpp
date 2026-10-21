#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QKeySequenceEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QStackedWidget>

#include "desktop_service.h"
#include "settings_view.h"
#include "ui_helpers.h"
#include "windows_integration.h"

namespace {
using namespace ui_helpers;

QWidget *page(const QString &title) {
    auto *widget = new QWidget;
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(24, 16, 24, 24);
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
    checkbox(widget, settings, "behavior/autostart", "Запускать вместе с Windows", false);
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
    checkbox(widget, settings, "appearance/glass", "Системный материал рамки Windows 11", false);
    widget->layout()->addWidget(
        label("В Windows 10 используется обычная непрозрачная рамка. Список и текст остаются непрозрачными.", "muted"));
    choices(widget, settings, "appearance/text_scale", "Размер текста",
            {{"100%", 100}, {"125%", 125}, {"150%", 150}, {"200%", 200}}, 100);
    widget->layout()->addWidget(
        label("Интерфейс меняет состояния сразу, без длительных анимаций и постоянного размытия.", "muted"));
    static_cast<QVBoxLayout *>(widget->layout())->addStretch();
    return widget;
}

QWidget *hotkeys(const ui_context &context, QWidget *) {
    auto *widget = page("Горячие клавиши");
    const QList<QStringList> values{{"shortcuts/global", "Показать библиотеку из любого приложения", "Ctrl+Alt+Space"},
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
    widget->layout()->addWidget(label("Пустое поле отключает сочетание. Enter запускает выбранное приложение; ↑↓ "
                                      "выбирают строку. Escape очищает поиск.",
                                      "muted"));
    static_cast<QVBoxLayout *>(widget->layout())->addStretch();
    return widget;
}

QWidget *library_settings(const ui_context &context, QWidget *) {
    auto *service = static_cast<DesktopService *>(context.service);
    auto *widget = page("Источники приложений");
    checkbox(widget, *context.settings, "sources/registry", "Зарегистрированные приложения Windows", true);
    checkbox(widget, *context.settings, "sources/start_menu", "Ярлыки меню «Пуск»", true);
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
        if (QMessageBox::question(widget, "История запусков",
                                  "Очистить время последних запусков? Приложения и теги сохранятся.",
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
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
        widget->layout()->addWidget(label("Модули не установлены. Каталог: plugins рядом с XLibrary.exe.", "muted"));
    static_cast<QVBoxLayout *>(widget->layout())->addStretch();
    return widget;
}

QWidget *about(const ui_context &, QWidget *) {
    auto *widget = page("XLibrary 0.1.0");
    widget->layout()->addWidget(label("Библиотека приложений для Windows", "subheading"));
    widget->layout()->addWidget(
        label("Приложения, теги и сценарии в одном месте. Каталог хранится локально; интернет для работы не требуется.",
              "muted"));
    widget->layout()->addWidget(label(
        "Qt 6 · C++23 · SQLite\nЛицензия XLibrary: MIT\nQt распространяется по условиям своей лицензии.", "muted"));
    static_cast<QVBoxLayout *>(widget->layout())->addStretch();
    return widget;
}

QDialog *settings_dialog(const ui_context &context, QWidget *parent) {
    auto *service = static_cast<DesktopService *>(context.service);
    auto *hotkey = static_cast<GlobalHotkey *>(context.parameters.value("hotkey").value<QObject *>());
    auto *dialog = new QDialog(parent);
    dialog->setWindowTitle("Настройки XLibrary");
    dialog->resize(920, 680);
    dialog->setMinimumSize(640, 480);
    auto *root = new QVBoxLayout(dialog);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(16);
    root->addWidget(label("Настройки", "heading"));
    auto *body = new QHBoxLayout;
    auto *categories = new QListWidget;
    categories->setObjectName("settings_categories");
    categories->setSpacing(2);
    categories->setFixedWidth(180);
    auto *pages = new QStackedWidget;
    const QList<QPair<QString, QString>> sections{
        {"Общие", "settings_general"},           {"Внешний вид", "settings_appearance"},
        {"Горячие клавиши", "settings_hotkeys"}, {"Библиотека", "settings_library"},
        {"Плагины", "settings_plugins"},         {"О программе", "settings_about"}};
    for (const auto &section : sections) {
        auto *item = new QListWidgetItem(section.first, categories);
        item->setSizeHint({160, 44});
        pages->addWidget(scroll(context.registry->create(section.second, context, pages)));
    }
    body->addWidget(categories);
    body->addWidget(pages, 1);
    root->addLayout(body, 1);
    auto *transition = new PageTransition(pages);
    QObject::connect(categories, &QListWidget::currentRowChanged, pages, [=](int index) {
        if (pages->currentIndex() == index)
            return;
        pages->setCurrentIndex(index);
        transition->start();
    });
    categories->setCurrentRow(0);
    auto *error = label("", "muted");
    root->addWidget(error);
    QObject::connect(service, &DesktopService::message, error, &QLabel::setText);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText("Готово");
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
                    error->setText("Одинаковые сочетания назначены нескольким действиям.");
                    categories->setCurrentRow(2);
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
            service->settings.value("shortcuts/global", "Ctrl+Alt+Space").toString(), QKeySequence::PortableText);
        QString failure;
        if (hotkey && !hotkey->set_sequence(QKeySequence::fromString(values.value("shortcuts/global").toString(),
                                                                     QKeySequence::PortableText),
                                            failure)) {
            error->setText(failure);
            categories->setCurrentRow(2);
            return;
        }
        const bool old_autostart = service->settings.value("behavior/autostart", false).toBool();
        const bool new_autostart = values.value("behavior/autostart").toBool();
        if (old_autostart != new_autostart && !desktop_windows::set_autostart(new_autostart, failure)) {
            if (hotkey) {
                QString ignored;
                hotkey->set_sequence(previous_global, ignored);
            }
            error->setText(failure);
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
                desktop_windows::set_autostart(old_autostart, ignored);
            }
            error->setText("Не удалось сохранить файл настроек.");
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
