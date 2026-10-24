#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QKeySequenceEdit>
#include <QStackedWidget>
#include <QUrl>

#include "desktop_service.h"
#include "localization.h"
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
    auto *widget = page(QCoreApplication::translate("XLibrary", "Application behavior"));
    auto &settings = *context.settings;
    choices(
        widget, settings, "interface/language", QCoreApplication::translate("XLibrary", "Interface language"),
        {{QCoreApplication::translate("XLibrary", "System language"), "system"}, {"English", "en"}, {"Русский", "ru"}},
        "system");
    widget->layout()->addWidget(label(
        QCoreApplication::translate("XLibrary", "Language changes take effect after restarting XLibrary."), "muted"));
    checkbox(widget, settings, "behavior/autostart", QCoreApplication::translate("XLibrary", "Start at login"), false);
    checkbox(widget, settings, "behavior/close_to_tray",
             QCoreApplication::translate("XLibrary", "Minimize to tray on close"), true);
    choices(widget, settings, "behavior/after_launch",
            QCoreApplication::translate("XLibrary", "After launching an application"),
            {{QCoreApplication::translate("XLibrary", "Keep the window open"), "keep"},
             {QCoreApplication::translate("XLibrary", "Minimize the window"), "minimize"},
             {QCoreApplication::translate("XLibrary", "Hide to tray"), "hide"}},
            "keep");
    choices(widget, settings, "behavior/relaunch", QCoreApplication::translate("XLibrary", "Launch again"),
            {{QCoreApplication::translate("XLibrary", "Switch to the open window"), "activate"},
             {QCoreApplication::translate("XLibrary", "Open a new instance"), "new"}},
            "activate");
    choices(widget, settings, "behavior/startup", QCoreApplication::translate("XLibrary", "At startup"),
            {{QCoreApplication::translate("XLibrary", "Show the library"), "show"},
             {QCoreApplication::translate("XLibrary", "Start in the tray"), "tray"}},
            "show");
    static_cast<QVBoxLayout *>(widget->layout())->addStretch();
    return widget;
}

QWidget *appearance_page(const ui_context &context, QWidget *) {
    auto *widget = page(QCoreApplication::translate("XLibrary", "Appearance"));
    auto &settings = *context.settings;
    choices(widget, settings, "appearance/theme", QCoreApplication::translate("XLibrary", "Theme"),
            {{QCoreApplication::translate("XLibrary", "Dark"), "dark"},
             {QCoreApplication::translate("XLibrary", "Light"), "light"},
             {QCoreApplication::translate("XLibrary", "System"), "system"}},
            "dark");
#ifdef Q_OS_WIN
    checkbox(widget, settings, "appearance/glass",
             QCoreApplication::translate("XLibrary", "Windows 11 system window material"), false);
    widget->layout()->addWidget(
        label(QCoreApplication::translate("XLibrary",
                                          "Windows 10 uses a standard opaque frame. The list and text remain opaque."),
              "muted"));
#endif
    choices(widget, settings, "appearance/text_scale", QCoreApplication::translate("XLibrary", "Text size"),
            {{"100%", 100}, {"125%", 125}, {"150%", 150}, {"200%", 200}}, 100);
    widget->layout()->addWidget(
        label(QCoreApplication::translate("XLibrary",
                                          "Short, smooth transitions follow available system animation settings."),
              "muted"));
    static_cast<QVBoxLayout *>(widget->layout())->addStretch();
    return widget;
}

QWidget *hotkeys(const ui_context &context, QWidget *) {
    auto *widget = page(QCoreApplication::translate("XLibrary", "Keyboard shortcuts"));
    const QList<QStringList> values{
        {"shortcuts/global", QCoreApplication::translate("XLibrary", "Show the library from any application"),
         desktop_platform::default_global_shortcut()},
        {"shortcuts/search", QCoreApplication::translate("XLibrary", "Search the library"), "Ctrl+K"},
        {"shortcuts/add", QCoreApplication::translate("XLibrary", "Add application"), "Ctrl+N"},
        {"shortcuts/settings", QCoreApplication::translate("XLibrary", "Open settings"), "Ctrl+,"}};
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
        label(QCoreApplication::translate("XLibrary",
                                          "On Wayland, assign a desktop shortcut to the XLibrary command. It opens "
                                          "the running library. The built-in global shortcut is available on X11."),
              "muted"));
#endif
    widget->layout()->addWidget(label(
        QCoreApplication::translate("XLibrary", "An empty field disables the shortcut. Enter launches the selected "
                                                "application; ↑↓ selects a row. Escape clears the search."),
        "muted"));
    static_cast<QVBoxLayout *>(widget->layout())->addStretch();
    return widget;
}

QWidget *library_settings(const ui_context &context, QWidget *) {
    auto *service = static_cast<DesktopService *>(context.service);
    auto *widget = page(QCoreApplication::translate("XLibrary", "Application sources"));
#ifdef Q_OS_WIN
    checkbox(widget, *context.settings, "sources/registry",
             QCoreApplication::translate("XLibrary", "Registered Windows applications"), true);
    checkbox(widget, *context.settings, "sources/start_menu",
             QCoreApplication::translate("XLibrary", "Start menu shortcuts"), true);
#else
    checkbox(widget, *context.settings, "sources/registry",
             QCoreApplication::translate("XLibrary", "Linux system applications"), true);
    checkbox(widget, *context.settings, "sources/start_menu",
             QCoreApplication::translate("XLibrary", "Linux user applications"), true);
#endif
    widget->layout()->addWidget(
        label(QCoreApplication::translate(
                  "XLibrary", "Discovery runs on request. Changed sources will be used after saving settings."),
              "muted"));
    widget->layout()->addWidget(action_button(context.actions.value("discover")));
    widget->layout()->addWidget(context.registry->create("discovery_progress", context, widget));
    widget->layout()->addWidget(
        label(QCoreApplication::translate("XLibrary", "Library import and export"), "subheading"));
    widget->layout()->addWidget(
        label(QCoreApplication::translate(
                  "XLibrary",
                  "JSON saves applications, parameters, favorites, and tags. Existing entries are preserved during "
                  "import. Applications are never launched automatically."),
              "muted"));
    auto *import = button(QCoreApplication::translate("XLibrary", "Import…"));
    auto *export_button = button(QCoreApplication::translate("XLibrary", "Export…"));
    widget->layout()->addWidget(import);
    widget->layout()->addWidget(export_button);
    QObject::connect(import, &QPushButton::clicked, widget, [=] {
        const auto path =
            QFileDialog::getOpenFileName(widget, QCoreApplication::translate("XLibrary", "Import library"), {},
                                         QCoreApplication::translate("XLibrary", "XLibrary catalog (*.json)"));
        if (!path.isEmpty())
            service->import_catalog(path);
    });
    QObject::connect(export_button, &QPushButton::clicked, widget, [=] {
        const auto path = QFileDialog::getSaveFileName(
            widget, QCoreApplication::translate("XLibrary", "Export library"), "xlibrary.json",
            QCoreApplication::translate("XLibrary", "XLibrary catalog (*.json)"));
        if (!path.isEmpty())
            service->export_catalog(path);
    });
    auto *clear = button(QCoreApplication::translate("XLibrary", "Clear launch history"));
    widget->layout()->addWidget(clear);
    QObject::connect(clear, &QPushButton::clicked, widget, [=] {
        if (confirm(widget, QCoreApplication::translate("XLibrary", "Launch history"),
                    QCoreApplication::translate(
                        "XLibrary", "Clear the most recent launch times? Applications and tags will be kept."),
                    QCoreApplication::translate("XLibrary", "Clear")))
            service->clear_history();
    });
    static_cast<QVBoxLayout *>(widget->layout())->addStretch();
    return widget;
}

QWidget *plugins(const ui_context &context, QWidget *) {
    auto *widget = page(QCoreApplication::translate("XLibrary", "Interface modules"));
    widget->layout()->addWidget(
        label(QCoreApplication::translate(
                  "XLibrary",
                  "Native plugins can change interface areas and appearance. Enable only trusted modules; they run "
                  "inside XLibrary. Changes take effect after restart."),
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
            label(QCoreApplication::translate("XLibrary",
                                              "No modules installed. Folder: plugins next to the XLibrary executable."),
                  "muted"));
    static_cast<QVBoxLayout *>(widget->layout())->addStretch();
    return widget;
}

QWidget *about(const ui_context &context, QWidget *) {
    auto *widget = page(QCoreApplication::translate("XLibrary", "About"));
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
    auto *version =
        label(QCoreApplication::translate("XLibrary", "Version ") + QCoreApplication::applicationVersion(), "muted");
    version->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    title->addWidget(version);
    identity->addLayout(title, 1);
    card_layout->addLayout(identity);
    card_layout->addWidget(
        label(QCoreApplication::translate("XLibrary", "Application library for Windows and Linux"), "subheading"));
    card_layout->addWidget(label(
        QCoreApplication::translate("XLibrary", "Applications, tags, and scenarios in one place. The catalog is stored "
                                                "locally; no internet connection is required."),
        "muted"));
    auto *github = button(QCoreApplication::translate("XLibrary", "Open GitHub ↗"));
    auto *issues = button(QCoreApplication::translate("XLibrary", "Report an issue ↗"));
    card_layout->addWidget(github);
    card_layout->addWidget(issues);
    QObject::connect(github, &QPushButton::clicked, widget,
                     [] { desktop_platform::open_url(QUrl("https://github.com/Lappland-Julia/XLibrary")); });
    QObject::connect(issues, &QPushButton::clicked, widget,
                     [] { desktop_platform::open_url(QUrl("https://github.com/Lappland-Julia/XLibrary/issues")); });
    widget->layout()->addWidget(card);
    widget->layout()->addWidget(
        label(QCoreApplication::translate("XLibrary", "Technologies and licenses"), "subheading"));
    widget->layout()->addWidget(label(
        QString(QCoreApplication::translate(
                    "XLibrary",
                    "Qt %1 · C++23 · SQLite\nXLibrary — MIT\nThird-party licenses are included with the application."))
            .arg(QString::fromLatin1(qVersion())),
        "muted"));
    static_cast<QVBoxLayout *>(widget->layout())->addStretch();
    return widget;
}

QDialog *settings_dialog(const ui_context &context, QWidget *parent) {
    auto *service = static_cast<DesktopService *>(context.service);
    auto *hotkey = static_cast<GlobalHotkey *>(context.parameters.value("hotkey").value<QObject *>());
    auto *dialog = new QDialog(parent);
    dialog->setWindowTitle(QCoreApplication::translate("XLibrary", "XLibrary settings"));
    prepare_dialog(dialog, context, {960, 700}, {640, 420});
    auto *root = new QVBoxLayout(dialog);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(16);
    root->addWidget(label(QCoreApplication::translate("XLibrary", "Settings"), "heading"));
    root->addWidget(
        label(QCoreApplication::translate("XLibrary", "XLibrary appearance, behavior, and features"), "muted"));
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
        {QCoreApplication::translate("XLibrary", "General"), "settings_general"},
        {QCoreApplication::translate("XLibrary", "Appearance settings"), "settings_appearance"},
        {QCoreApplication::translate("XLibrary", "Keyboard shortcuts"), "settings_hotkeys"},
        {QCoreApplication::translate("XLibrary", "Library"), "settings_library"},
        {QCoreApplication::translate("XLibrary", "Plugins"), "settings_plugins"},
        {QCoreApplication::translate("XLibrary", "About"), "settings_about"}};
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
    buttons->button(QDialogButtonBox::Save)->setText(QCoreApplication::translate("XLibrary", "Save"));
    buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Cancel)->setText(QCoreApplication::translate("XLibrary", "Cancel"));
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
                    show_error(
                        QCoreApplication::translate("XLibrary", "The same shortcut is assigned to multiple actions."));
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
            show_error(QCoreApplication::translate("XLibrary", "Could not save the settings file."));
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
