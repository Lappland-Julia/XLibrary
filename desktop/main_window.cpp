#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QStackedWidget>
#include <QStyleHints>

#include "appearance.h"
#include "editors.h"
#include "library_views.h"
#include "main_window.h"
#include "settings_view.h"
#include "ui_helpers.h"

namespace {
using namespace ui_helpers;

QWidget *shell(const ui_context &context, QWidget *parent) {
    auto *service = static_cast<DesktopService *>(context.service);
    auto *widget = new QWidget(parent);
    auto *root = new QHBoxLayout(widget);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(context.registry->create("navigation", context, widget));
    auto *center = new QWidget;
    auto *body = new QVBoxLayout(center);
    body->setContentsMargins(20, 16, 20, 0);
    body->setSpacing(20);
    body->addWidget(context.registry->create("search_bar", context, center));
    auto *pages = new QStackedWidget;
    pages->addWidget(context.registry->create("library", context, pages));
    pages->addWidget(context.registry->create("scenarios", context, pages));
    body->addWidget(pages, 1);
    auto *notice = new QWidget;
    notice->setProperty("panel", true);
    auto *notice_layout = new QHBoxLayout(notice);
    auto *notice_text = label("");
    notice_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *dismiss = button("×");
    dismiss->setAccessibleName("Закрыть уведомление");
    notice_layout->addWidget(notice_text, 1);
    notice_layout->addWidget(dismiss);
    notice->hide();
    body->addWidget(notice);
    QObject::connect(dismiss, &QPushButton::clicked, notice, &QWidget::hide);
    QObject::connect(service, &DesktopService::message, notice, [=](const QString &text) {
        notice_text->setText(text);
        notice->show();
    });
    QObject::connect(service, &DesktopService::scenario_changed, notice, [=](const QString &text) {
        notice_text->setText(text);
        notice->show();
    });
    root->addWidget(center, 1);
    root->addWidget(context.registry->create("details", context, widget));
    QObject::connect(service, &DesktopService::navigation_changed, widget,
                     [=] { pages->setCurrentIndex(service->section() == "scenarios" ? 1 : 0); });
    return widget;
}
}

MainWindow::MainWindow(DesktopService &service) : service(service), hotkey(this), tray(this) {
    setWindowTitle("XLibrary");
    setWindowIcon(appearance::logo());
    setMinimumSize(960, 640);
    resize(1280, 800);
    context.service = &service;
    context.catalog = &service.model;
    context.filtered_catalog = &service.filter;
    context.settings = &service.settings;
    context.registry = &registry;
    context.window = this;
    context.parameters.insert("hotkey", QVariant::fromValue(static_cast<QObject *>(&hotkey)));
    register_editors(registry);
    register_library_views(registry);
    register_settings_views(registry);
    registry.add_default("shell", shell);
    create_actions();
    load_plugins();
    appearance::apply(service.settings, registry.style_sheet());
    setCentralWidget(registry.create("shell", context, this));
    auto *tray_menu = new QMenu(this);
    auto *show_action = tray_menu->addAction("Открыть XLibrary");
    connect(show_action, &QAction::triggered, this, &MainWindow::show_library);
    tray_menu->addAction(context.actions.value("settings"));
    tray_menu->addSeparator();
    tray_menu->addAction(context.actions.value("quit"));
    tray.setIcon(windowIcon());
    tray.setToolTip("XLibrary");
    tray.setContextMenu(tray_menu);
    if (QSystemTrayIcon::isSystemTrayAvailable())
        tray.show();
    connect(&tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            show_library();
    });
    connect(&hotkey, &GlobalHotkey::activated, this, &MainWindow::show_library);
    connect(&service, &DesktopService::selection_changed, this, &MainWindow::refresh_actions);
    connect(&service, &DesktopService::catalog_changed, this, &MainWindow::refresh_actions);
    connect(&service, &DesktopService::discovery_changed, this, &MainWindow::refresh_actions);
    connect(&service, &DesktopService::navigation_changed, this, &MainWindow::update_layout);
    connect(&service, &DesktopService::appearance_changed, this, &MainWindow::apply_settings);
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (this->service.settings.value("appearance/theme").toString() == "system")
            apply_settings();
    });
    connect(&service, &DesktopService::launch_succeeded, this, [this] {
        const auto behavior = this->service.settings.value("behavior/after_launch", "keep").toString();
        if (behavior == "minimize")
            showMinimized();
        if (behavior == "hide") {
            if (tray.isVisible())
                hide();
            else
                showMinimized();
        }
    });
    if (!restoreGeometry(service.settings.value("window/geometry").toByteArray()))
        resize(1280, 800);
    apply_settings();
    refresh_actions();
    update_layout();
}

MainWindow::~MainWindow() {
    // Фабрики и виджеты плагинов должны исчезнуть до выгрузки их кода.
    delete takeCentralWidget();
    for (auto *dialog : findChildren<QDialog *>(QString{}, Qt::FindDirectChildrenOnly))
        delete dialog;
    registry = UiRegistry{};
}

void MainWindow::create_actions() {
    auto add = [this](const QString &id, const QString &title, const QString &icon, auto callback) {
        auto *action = new QAction(appearance::icon(icon), title, this);
        action->setObjectName(id);
        context.actions.insert(id, action);
        addAction(action);
        connect(action, &QAction::triggered, this, callback);
        return action;
    };
    add("launch", "Запустить", "play", [this] {
        if (service.selection())
            service.launch(service.selection());
    });
    add("new_instance", "Новый экземпляр", "play", [this] {
        if (service.selection())
            service.launch(service.selection(), true);
    });
    add("focus_search", "Поиск", "search", [] {});
    add("settings", "Настройки", "settings", [this] {
        if (!isVisible())
            show_library();
        open_dialog("settings", context);
    });
    add("add_application", "Добавить приложение", "add", [this] { open_dialog("application_editor", context); });
    add("edit_application", "Изменить", "settings", [this] {
        if (const auto *record = service.selected()) {
            auto edit_context = context;
            edit_context.parameters.insert("record", QVariant::fromValue(*record));
            open_dialog("application_editor", edit_context);
        }
    });
    add("remove_application", "Удалить из библиотеки", "trash", [this] {
        const auto *record = service.selected();
        if (!record)
            return;
        const auto id = record->id;
        const auto name = record->name;
        if (QMessageBox::question(this, "Удалить приложение",
                                  "Удалить «" + name + "» из библиотеки? Файлы приложения сохранятся.",
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
            service.remove_application(id);
    });
    context.actions.value("remove_application")
        ->setToolTip("Удалить выбранное приложение из библиотеки (Delete). Файлы приложения сохранятся.");
    add("favorite", "В избранное", "favorite", [this] {
        if (const auto *record = service.selected()) {
            auto updated = *record;
            updated.favorite = !updated.favorite;
            service.save_application(std::move(updated));
        }
    });
    add("open_folder", "Открыть папку", "folder", [this] { service.open_folder(service.selection()); });
    add("discover", "Найти установленные приложения", "refresh", [this] {
        if (service.is_discovering())
            service.cancel_discovery();
        else
            service.discover();
    });
    add("add_tag", "Добавить тег", "add", [this] { open_dialog("tag_editor", context); });
    add("add_scenario", "Создать сценарий", "add", [this] { open_dialog("scenario_editor", context); });
    add("details", "Сведения", "app", [this] {
        auto *dialog = new QDialog(this);
        dialog->setWindowTitle("Сведения о приложении");
        dialog->resize(380, 660);
        auto *layout = new QVBoxLayout(dialog);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(registry.create("details", context, dialog));
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
    add("quit", "Выход", "x", [this] {
        quitting = true;
        close();
        qApp->quit();
    });
    auto *escape = new QAction(this);
    escape->setShortcut(QKeySequence(Qt::Key_Escape));
    addAction(escape);
    connect(escape, &QAction::triggered, this, [this] {
        if (auto *search = findChild<QLineEdit *>("search_input"))
            search->clear();
    });
}

void MainWindow::refresh_actions() {
    const auto *record = service.selected();
    for (const auto &id :
         {"launch", "new_instance", "edit_application", "remove_application", "favorite", "open_folder", "details"})
        context.actions.value(id)->setEnabled(record != nullptr);
    context.actions.value("add_application")->setEnabled(service.is_ready());
    context.actions.value("add_tag")->setEnabled(service.is_ready());
    context.actions.value("add_scenario")->setEnabled(service.is_ready() && service.model.rowCount() > 0);
    context.actions.value("discover")->setEnabled(service.is_ready());
    context.actions.value("discover")
        ->setText(service.is_discovering() ? "Отменить поиск" : "Найти установленные приложения");
    context.actions.value("launch")->setText(
        record && record->running && service.settings.value("behavior/relaunch", "activate").toString() == "activate"
            ? "Перейти к окну"
            : "Запустить");
    context.actions.value("favorite")->setText(record && record->favorite ? "Убрать из избранного" : "В избранное");
}

void MainWindow::apply_settings() {
    appearance::apply(service.settings, registry.style_sheet());
    QString error;
    if (!hotkey.set_sequence(
            QKeySequence::fromString(service.settings.value("shortcuts/global", "Ctrl+Alt+Space").toString(),
                                     QKeySequence::PortableText),
            error))
        service.notify(error);
    const QList<QStringList> shortcuts{{"focus_search", "shortcuts/search", "Ctrl+K"},
                                       {"add_application", "shortcuts/add", "Ctrl+N"},
                                       {"settings", "shortcuts/settings", "Ctrl+,"}};
    for (const auto &value : shortcuts)
        context.actions.value(value[0])->setShortcut(QKeySequence::fromString(
            service.settings.value(value[1], value[2]).toString(), QKeySequence::PortableText));
    desktop_windows::apply_backdrop(this, appearance::is_dark(service.settings),
                                    service.settings.value("appearance/glass", false).toBool());
    refresh_actions();
    update_layout();
}

void MainWindow::load_plugins() {
    const QDir directory(QApplication::applicationDirPath() + "/plugins");
    QStringList available;
    const auto enabled = service.settings.value("plugins/enabled").toStringList();
    for (const auto &file : directory.entryInfoList({"*.dll"}, QDir::Files | QDir::NoSymLinks, QDir::Name)) {
        auto loader = std::make_unique<QPluginLoader>(file.absoluteFilePath());
        if (loader->metaData().value("IID").toString() != XLIBRARY_UI_PLUGIN_IID)
            continue;
        const auto id = file.fileName();
        available.append(id);
        if (!enabled.contains(id))
            continue;
        loader->setLoadHints(QLibrary::PreventUnloadHint);
        auto *plugin = qobject_cast<UiPlugin *>(loader->instance());
        if (!plugin) {
            QTimer::singleShot(0, &service, [this, id, error = loader->errorString()] {
                service.notify("Не удалось загрузить " + id + ": " + error);
            });
            continue;
        }
        try {
            plugin->configure(registry);
            plugins.push_back(std::move(loader));
        } catch (const std::exception &error) {
            throw std::runtime_error("UI plugin " + id.toStdString() + ": " + error.what());
        }
    }
    context.parameters.insert("available_plugins", available);
}

void MainWindow::update_layout() {
    if (auto *navigation = findChild<QWidget *>("navigation"))
        navigation->setFixedWidth(appearance::scaled(service.settings, 208));
    if (auto *details = centralWidget() ? centralWidget()->findChild<QWidget *>("details") : nullptr) {
        details->setFixedWidth(appearance::scaled(service.settings, 304));
        details->setVisible(width() >= appearance::scaled(service.settings, 1120) && service.section() != "scenarios");
    }
}

void MainWindow::show_library() {
    if (isMinimized())
        showNormal();
    else
        show();
    raise();
    activateWindow();
    context.actions.value("focus_search")->trigger();
}

void MainWindow::start() {
    const bool background = QCoreApplication::arguments().contains("--background") ||
                            service.settings.value("behavior/startup", "show").toString() == "tray";
    if (!background || !tray.isVisible())
        show_library();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    service.settings.setValue("window/geometry", saveGeometry());
    service.settings.sync();
    if (!quitting && service.settings.value("behavior/close_to_tray", true).toBool() && tray.isVisible()) {
        hide();
        event->ignore();
        return;
    }
    quitting = true;
    event->accept();
    qApp->quit();
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    update_layout();
}
