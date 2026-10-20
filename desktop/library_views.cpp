#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QStackedLayout>
#include <QStyledItemDelegate>
#include <QTreeView>

#include "appearance.h"
#include "desktop_service.h"
#include "library_views.h"
#include "ui_helpers.h"

namespace {
using namespace ui_helpers;

class SearchInput : public QLineEdit {
public:
    std::function<void(int)> move_selection;

protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (move_selection && (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up)) {
            move_selection(event->key() == Qt::Key_Down ? 1 : -1);
            event->accept();
            return;
        }
        QLineEdit::keyPressEvent(event);
    }
};

class ApplicationDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override {
        return {100, qMax(52, option.fontMetrics.height() * 3)};
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const auto rect = option.rect.adjusted(0, 2, 0, -2);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hover = option.state & QStyle::State_MouseOver;
        if (selected || hover) {
            painter->fillRect(rect, selected ? option.palette.highlight() : option.palette.alternateBase());
        }
        if (selected && (option.state & QStyle::State_HasFocus)) {
            painter->setPen(QPen(QColor("#AAB4C1"), 1));
            painter->drawRect(rect.adjusted(1, 0, -1, -1));
        }
        const auto text_color = option.palette.color(QPalette::Text);
        painter->setPen(text_color);
        auto text_rect = rect.adjusted(12, 0, -12, 0);
        if (index.column() == 0) {
            const int size = qMin(32, rect.height() - 12);
            const QRect icon_rect(text_rect.left(), rect.center().y() - size / 2, size, size);
            auto icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
            if (icon.isNull())
                icon = appearance::icon("app");
            icon.paint(painter, icon_rect);
            text_rect.setLeft(icon_rect.right() + 12);
            if (index.data(LibraryModel::favorite_role).toBool()) {
                appearance::icon("favorite")
                    .paint(painter, QRect(text_rect.right() - 16, rect.center().y() - 8, 16, 16));
                text_rect.adjust(0, 0, -24, 0);
            }
            painter->drawText(
                text_rect, Qt::AlignVCenter,
                option.fontMetrics.elidedText(index.data().toString(), Qt::ElideRight, text_rect.width()));
        } else if (index.column() == 1) {
            QFont small = option.font;
            small.setPixelSize(qMax(12, option.font.pixelSize() - 2));
            painter->setFont(small);
            const QFontMetrics metrics(small);
            const auto tags = index.data(LibraryModel::tags_role).toStringList();
            int x = text_rect.left();
            for (qsizetype i = 0; i < tags.size(); ++i) {
                QString text = '#' + tags[i];
                int width = metrics.horizontalAdvance(text) + 16;
                if (x + width > text_rect.right() && i > 0) {
                    text = '+' + QString::number(tags.size() - i);
                    width = metrics.horizontalAdvance(text) + 16;
                }
                width = qMin(width, text_rect.right() - x);
                if (width < 20)
                    break;
                const QRect badge(x, rect.center().y() - 12, width, 24);
                painter->setBrush(option.palette.color(QPalette::Button));
                painter->setPen(Qt::NoPen);
                painter->drawRoundedRect(badge, 6, 6);
                painter->setPen(text_color);
                painter->drawText(badge.adjusted(8, 0, -8, 0), Qt::AlignVCenter,
                                  metrics.elidedText(text, Qt::ElideRight, width - 16));
                x += width + 5;
                if (text.startsWith('+'))
                    break;
            }
        } else {
            const bool running = index.data(LibraryModel::running_role).toBool();
            const bool available = index.data(LibraryModel::available_role).toBool();
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(running ? "#85A28A" : available ? "#80828A" : "#C2AA7D"));
            painter->drawEllipse(QPointF(text_rect.left() + 4, rect.center().y()), 3.5, 3.5);
            text_rect.adjust(16, 0, 0, 0);
            auto small = option.font;
            small.setPixelSize(qMax(12, option.font.pixelSize() - 2));
            painter->setFont(small);
            painter->setPen(option.palette.color(QPalette::PlaceholderText));
            painter->drawText(
                text_rect, Qt::AlignVCenter,
                QFontMetrics(small).elidedText(index.data().toString(), Qt::ElideRight, text_rect.width()));
        }
        painter->restore();
    }
};

QWidget *navigation(const ui_context &context, QWidget *parent) {
    auto *service = static_cast<DesktopService *>(context.service);
    auto *widget = new QFrame(parent);
    widget->setProperty("panel", true);
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(12, 20, 12, 16);
    layout->setSpacing(8);
    auto *brand = label("XLibrary", "subheading");
    layout->addWidget(brand);
    layout->addSpacing(16);
    auto *group = new QButtonGroup(widget);
    group->setExclusive(true);
    const QList<QStringList> links{{"library", "Библиотека", "library"},
                                   {"favorites", "Избранное", "favorite"},
                                   {"running", "Запущенные", "play"},
                                   {"scenarios", "Сценарии", "scenarios"}};
    for (const auto &link : links) {
        auto *item = button(link[1], "quiet");
        item->setIcon(appearance::icon(link[2]));
        item->setCheckable(true);
        item->setProperty("navigation_section", link[0]);
        item->setChecked(link[0] == "library");
        item->setMinimumHeight(30);
        group->addButton(item);
        layout->addWidget(item);
        QObject::connect(item, &QPushButton::clicked, widget, [=] { service->navigate(link[0]); });
    }
    layout->addSpacing(20);
    layout->addWidget(label("Мои теги", "muted"));
    auto *tags = new QListWidget;
    tags->setObjectName("tag_list");
    tags->setProperty("panel", true);
    tags->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(tags, 1);
    auto rebuild = [=] {
        const auto current = tags->currentItem() ? tags->currentItem()->text() : QString{};
        tags->clear();
        for (const auto &tag : service->catalog().tags) {
            auto *item = new QListWidgetItem('#' + tag, tags);
            item->setData(Qt::UserRole, tag);
            item->setSizeHint({100, 36});
            if (item->text() == current)
                tags->setCurrentItem(item);
        }
    };
    QObject::connect(service, &DesktopService::catalog_changed, widget, rebuild);
    QObject::connect(service, &DesktopService::navigation_changed, widget, [=] {
        group->setExclusive(false);
        for (auto *item : group->buttons())
            item->setChecked(service->tag().isEmpty() &&
                             item->property("navigation_section").toString() == service->section());
        group->setExclusive(true);
        if (service->tag().isEmpty())
            tags->setCurrentItem(nullptr);
    });
    QObject::connect(tags, &QListWidget::itemClicked, widget, [=](QListWidgetItem *item) {
        group->setExclusive(false);
        for (auto *entry : group->buttons())
            entry->setChecked(false);
        group->setExclusive(true);
        service->navigate("library", item->data(Qt::UserRole).toString());
    });
    QObject::connect(tags, &QListWidget::customContextMenuRequested, widget, [=](const QPoint &position) {
        const auto *item = tags->itemAt(position);
        if (!item)
            return;
        const auto tag = item->data(Qt::UserRole).toString();
        QMenu menu;
        auto *edit = menu.addAction("Переименовать");
        auto *remove = menu.addAction("Удалить тег");
        const auto *chosen = menu.exec(tags->viewport()->mapToGlobal(position));
        if (chosen == edit) {
            auto edit_context = context;
            edit_context.parameters.insert("tag", tag);
            open_dialog("tag_editor", edit_context);
        }
        if (chosen == remove &&
            QMessageBox::question(widget, "Удалить тег", "Удалить #" + tag + " у всех приложений?",
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
            service->remove_tag(tag);
    });
    layout->addWidget(action_button(context.actions.value("add_tag"), true));
    layout->addSpacing(8);
    layout->addWidget(action_button(context.actions.value("add_application")));
    rebuild();
    return widget;
}

QWidget *search_bar(const ui_context &context, QWidget *parent) {
    auto *service = static_cast<DesktopService *>(context.service);
    auto *widget = new QWidget(parent);
    auto *layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    auto *search = new SearchInput;
    search->setObjectName("search_input");
    search->setAccessibleName("Поиск приложений и тегов");
    search->setPlaceholderText("Найти приложение или #тег");
    search->setClearButtonEnabled(true);
    search->setMinimumHeight(26);
    search->addAction(appearance::icon("search"), QLineEdit::LeadingPosition);
    auto *timer = new QTimer(widget);
    timer->setInterval(35);
    timer->setSingleShot(true);
    QObject::connect(search, &QLineEdit::textChanged, timer, qOverload<>(&QTimer::start));
    QObject::connect(timer, &QTimer::timeout, widget, [=] { service->filter.set_query(search->text()); });
    QObject::connect(context.actions.value("focus_search"), &QAction::triggered, search, [=] {
        search->setFocus();
        search->selectAll();
    });
    QObject::connect(search, &QLineEdit::returnPressed, widget, [=] {
        timer->stop();
        if (service->section() == "scenarios")
            service->navigate("library");
        service->filter.set_query(search->text());
        if (service->selection())
            service->launch(service->selection());
    });
    search->move_selection = [=](int offset) {
        timer->stop();
        if (service->section() == "scenarios")
            service->navigate("library");
        service->filter.set_query(search->text());
        if (auto *view = context.window->findChild<QTreeView *>("application_list")) {
            const auto count = service->filter.rowCount();
            if (!count)
                return;
            const auto row = qBound(0, view->currentIndex().row() + offset, count - 1);
            const auto index = service->filter.index(row, 0);
            view->setCurrentIndex(index);
            view->scrollTo(index);
        }
    };
    layout->addWidget(search, 1);
    auto *settings = action_button(context.actions.value("settings"), false, "");
    settings->setAccessibleName("Настройки");
    settings->setFixedWidth(44);
    layout->addWidget(settings);
    return widget;
}

QWidget *library(const ui_context &context, QWidget *parent) {
    auto *service = static_cast<DesktopService *>(context.service);
    auto *widget = new QWidget(parent);
    auto *root = new QVBoxLayout(widget);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(12);
    auto *header = new QHBoxLayout;
    auto *title = label("Библиотека", "heading");
    header->addWidget(title, 1);
    auto *sort = new QComboBox;
    sort->addItems({"По названию", "Недавние запуски", "Сначала избранное"});
    sort->setAccessibleName("Сортировка приложений");
    sort->setCurrentIndex(service->settings.value("library/sort", 0).toInt());
    service->filter.set_sort_mode(sort->currentIndex());
    QObject::connect(sort, &QComboBox::currentIndexChanged, widget, [=](int index) {
        service->filter.set_sort_mode(index);
        service->settings.setValue("library/sort", index);
    });
    header->addWidget(sort);
    auto *scan = action_button(context.actions.value("discover"), false, "");
    scan->setAccessibleName("Найти установленные приложения");
    scan->setFixedWidth(44);
    header->addWidget(scan);
    root->addLayout(header);
    auto *stack_widget = new QWidget;
    auto *stack = new QStackedLayout(stack_widget);
    auto *view = new QTreeView;
    view->setObjectName("application_list");
    view->setModel(&service->filter);
    view->setItemDelegate(new ApplicationDelegate(view));
    view->setRootIsDecorated(false);
    view->setUniformRowHeights(true);
    view->setAllColumnsShowFocus(true);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setMouseTracking(true);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->header()->setMinimumSectionSize(70);
    view->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    view->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    view->header()->resizeSection(1, 140);
    view->header()->resizeSection(2, 142);
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    stack->addWidget(view);
    auto *empty = new QWidget;
    auto *empty_layout = new QVBoxLayout(empty);
    empty_layout->addStretch();
    auto *empty_title = label("Загрузка библиотеки…", "subheading");
    empty_title->setAlignment(Qt::AlignCenter);
    empty_layout->addWidget(empty_title);
    auto *hint = label("", "muted");
    hint->setAlignment(Qt::AlignCenter);
    empty_layout->addWidget(hint);
    auto *first = action_button(context.actions.value("add_application"));
    empty_layout->addWidget(first, 0, Qt::AlignCenter);
    empty_layout->addStretch();
    stack->addWidget(empty);
    root->addWidget(stack_widget, 1);
    auto *footer = new QHBoxLayout;
    footer->addWidget(label("↑↓  Выбор     Enter  Запустить     Ctrl+K  Поиск", "muted"), 1);
    auto *details = action_button(context.actions.value("details"), true);
    footer->addWidget(details);
    root->addLayout(footer);
    auto update = [=] {
        const bool has_rows = service->filter.rowCount() > 0;
        stack->setCurrentWidget(has_rows ? static_cast<QWidget *>(view) : empty);
        if (!service->is_ready()) {
            empty_title->setText("Загрузка библиотеки…");
            first->hide();
        } else {
            const bool no_apps = service->model.rowCount() == 0;
            empty_title->setText(no_apps ? "Добавьте первое приложение" : "Ничего не найдено");
            hint->setText(no_apps ? "Выберите файл или найдите установленные приложения."
                                  : "Измените запрос или выберите другой раздел.");
            first->setVisible(no_apps);
        }
        if (!has_rows) {
            service->select(0);
            return;
        }
        QModelIndex target;
        for (int row = 0; row < service->filter.rowCount(); ++row) {
            const auto index = service->filter.index(row, 0);
            if (index.data(LibraryModel::id_role).toLongLong() == service->selection()) {
                target = index;
                break;
            }
        }
        if (!target.isValid())
            target = service->filter.index(0, 0);
        if (view->currentIndex().siblingAtColumn(0) != target)
            view->setCurrentIndex(target);
        service->select(target.data(LibraryModel::id_role).toLongLong());
    };
    QObject::connect(view->selectionModel(), &QItemSelectionModel::currentChanged, widget,
                     [=](const QModelIndex &index) {
                         if (index.isValid())
                             service->select(index.data(LibraryModel::id_role).toLongLong());
                     });
    QObject::connect(view, &QTreeView::activated, widget, [=](const QModelIndex &index) {
        service->launch(index.data(LibraryModel::id_role).toLongLong());
    });
    QObject::connect(view, &QTreeView::customContextMenuRequested, widget, [=](const QPoint &position) {
        const auto index = view->indexAt(position);
        if (!index.isValid())
            return;
        view->setCurrentIndex(index);
        service->select(index.data(LibraryModel::id_role).toLongLong());
        auto *menu_widget = context.registry->create("application_menu", context, view);
        if (auto *menu = qobject_cast<QMenu *>(menu_widget))
            menu->exec(view->viewport()->mapToGlobal(position));
        delete menu_widget;
    });
    QObject::connect(service, &DesktopService::catalog_changed, widget, update);
    QObject::connect(&service->filter, &QAbstractItemModel::rowsRemoved, widget, update);
    QObject::connect(&service->filter, &QAbstractItemModel::rowsInserted, widget, update);
    QObject::connect(&service->filter, &QAbstractItemModel::modelReset, widget, update);
    QObject::connect(&service->filter, &QAbstractItemModel::layoutChanged, widget, update);
    QObject::connect(service, &DesktopService::navigation_changed, widget, [=] {
        title->setText(service->section() == "favorites" ? "Избранное"
                       : service->section() == "running" ? "Запущенные"
                                                         : "Библиотека");
        update();
    });
    update();
    return widget;
}

QWidget *details(const ui_context &context, QWidget *parent) {
    auto *service = static_cast<DesktopService *>(context.service);
    auto *contents = new QWidget;
    contents->setProperty("panel", true);
    auto *root = new QVBoxLayout(contents);
    root->setContentsMargins(20, 24, 20, 24);
    root->setSpacing(12);
    auto *icon = new QLabel;
    icon->setFixedSize(56, 56);
    auto *name = label("Выберите приложение", "subheading");
    root->addWidget(icon);
    root->addWidget(name);
    auto *status = label("", "muted");
    root->addWidget(status);
    root->addSpacing(12);
    root->addWidget(label("Теги", "muted"));
    auto *tags = label("—");
    root->addWidget(tags);
    auto *edit_tags = action_button(context.actions.value("edit_application"), true, "Изменить теги");
    root->addWidget(edit_tags);
    root->addSpacing(12);
    root->addWidget(label("Путь к приложению", "muted"));
    auto *path = new QLineEdit;
    path->setReadOnly(true);
    path->setMinimumWidth(0);
    root->addWidget(path);
    root->addWidget(action_button(context.actions.value("open_folder"), true));
    auto *arguments = label("", "muted");
    root->addWidget(arguments);
    root->addSpacing(8);
    auto *launch = action_button(context.actions.value("launch"));
    launch->setProperty("primary", true);
    root->addWidget(launch);
    root->addWidget(action_button(context.actions.value("edit_application")));
    root->addWidget(action_button(context.actions.value("favorite")));
    root->addStretch();
    root->addWidget(action_button(context.actions.value("remove_application"), true));
    auto update = [=] {
        const auto *record = service->selected();
        name->setText(record ? record->name : "Выберите приложение");
        status->setText(record ? record->running     ? "● Запущено"
                                 : record->available ? "Не запущено"
                                                     : "Файл недоступен"
                               : "Сведения появятся здесь.");
        tags->setText(record && !record->tags.isEmpty() ? '#' + record->tags.join("   #") : "Нет тегов");
        path->setText(record ? QDir::toNativeSeparators(record->path) : QString{});
        path->setCursorPosition(0);
        path->setToolTip(path->text());
        arguments->setText(record && !record->arguments.isEmpty() ? "Аргументы: " + record->arguments : QString{});
        QIcon image = appearance::icon("app");
        if (record)
            for (int row = 0; row < service->model.rowCount(); ++row) {
                const auto index = service->model.index(row, 0);
                if (index.data(LibraryModel::id_role).toLongLong() == record->id) {
                    const auto cached = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
                    if (!cached.isNull())
                        image = cached;
                    break;
                }
            }
        icon->setPixmap(image.pixmap(56, 56));
    };
    QObject::connect(service, &DesktopService::selection_changed, contents, update);
    QObject::connect(&service->model, &QAbstractItemModel::dataChanged, contents, [=] { update(); });
    update();
    auto *result = scroll(contents);
    result->setParent(parent);
    result->setProperty("panel", true);
    return result;
}

QWidget *application_menu(const ui_context &context, QWidget *parent) {
    auto *menu = new QMenu(parent);
    for (const auto &id : {"launch", "new_instance", "open_folder", "favorite", "edit_application"})
        menu->addAction(context.actions.value(id));
    menu->addSeparator();
    menu->addAction(context.actions.value("remove_application"));
    return menu;
}

QWidget *scenarios(const ui_context &context, QWidget *parent) {
    auto *service = static_cast<DesktopService *>(context.service);
    auto *widget = new QWidget(parent);
    auto *root = new QVBoxLayout(widget);
    root->setContentsMargins(0, 0, 0, 0);
    auto *header = new QHBoxLayout;
    header->addWidget(label("Сценарии", "heading"), 1);
    header->addWidget(action_button(context.actions.value("add_scenario")));
    root->addLayout(header);
    auto *list = new QListWidget;
    list->setSpacing(2);
    root->addWidget(list, 1);
    auto *empty = label("Создайте сценарий, чтобы открыть несколько приложений по порядку.", "muted");
    root->addWidget(empty);
    auto *status = label("", "muted");
    root->addWidget(status);
    auto *tools = new QHBoxLayout;
    auto *run = button("Запустить сценарий", "primary");
    auto *edit = button("Изменить");
    auto *remove = button("Удалить");
    auto *stop = button("Остановить");
    tools->addWidget(run);
    tools->addWidget(edit);
    tools->addWidget(remove);
    tools->addStretch();
    tools->addWidget(stop);
    root->addLayout(tools);
    auto rebuild = [=] {
        const auto selected = list->currentItem() ? list->currentItem()->data(Qt::UserRole).toLongLong() : 0;
        list->clear();
        for (const auto &record : service->catalog().scenarios) {
            auto *item = new QListWidgetItem(appearance::icon("scenarios"),
                                             record.name + QString("\nПриложений: %1").arg(record.steps.size()), list);
            item->setData(Qt::UserRole, record.id);
            item->setSizeHint({100, 64});
            if (record.id == selected)
                list->setCurrentItem(item);
        }
        if (!list->currentItem() && list->count())
            list->setCurrentRow(0);
        const bool has = list->count() > 0;
        run->setEnabled(has && !service->is_scenario_active());
        edit->setEnabled(has);
        remove->setEnabled(has);
        empty->setVisible(!has);
        stop->setEnabled(service->is_scenario_active());
    };
    auto run_selected = [=] {
        if (list->currentItem())
            service->run_scenario(list->currentItem()->data(Qt::UserRole).toLongLong());
    };
    QObject::connect(run, &QPushButton::clicked, widget, run_selected);
    QObject::connect(list, &QListWidget::itemDoubleClicked, widget, [=] { run_selected(); });
    QObject::connect(edit, &QPushButton::clicked, widget, [=] {
        if (!list->currentItem())
            return;
        const auto id = list->currentItem()->data(Qt::UserRole).toLongLong();
        for (const auto &record : service->catalog().scenarios)
            if (record.id == id) {
                auto edit_context = context;
                edit_context.parameters.insert("scenario", QVariant::fromValue(record));
                open_dialog("scenario_editor", edit_context);
                break;
            }
    });
    QObject::connect(remove, &QPushButton::clicked, widget, [=] {
        if (list->currentItem() &&
            QMessageBox::question(widget, "Удалить сценарий", "Удалить выбранный сценарий?",
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
            service->remove_scenario(list->currentItem()->data(Qt::UserRole).toLongLong());
    });
    QObject::connect(stop, &QPushButton::clicked, service, &DesktopService::cancel_scenario);
    QObject::connect(service, &DesktopService::catalog_changed, widget, rebuild);
    QObject::connect(service, &DesktopService::scenario_changed, widget, [=](const QString &text) {
        status->setText(text);
        run->setEnabled(list->count() > 0 && !service->is_scenario_active());
        stop->setEnabled(service->is_scenario_active());
    });
    rebuild();
    return widget;
}
}

void register_library_views(UiRegistry &registry) {
    registry.add_default("navigation", navigation);
    registry.add_default("search_bar", search_bar);
    registry.add_default("library", library);
    registry.add_default("details", details);
    registry.add_default("scenarios", scenarios);
    registry.add_default("application_menu", application_menu);
}
