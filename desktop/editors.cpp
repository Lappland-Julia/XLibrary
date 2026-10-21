#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTableWidget>

#include "desktop_service.h"
#include "editors.h"
#include "ui_helpers.h"

namespace {
using namespace ui_helpers;

QStringList parse_tags(const QString &input) {
    QStringList result;
    for (auto tag : input.split(',', Qt::SkipEmptyParts)) {
        tag = tag.trimmed();
        if (tag.startsWith('#'))
            tag.remove(0, 1);
        if (!tag.isEmpty() && !result.contains(tag))
            result.append(tag);
    }
    return result;
}

QDialog *application_editor(const ui_context &context, QWidget *parent) {
    auto *service = static_cast<DesktopService *>(context.service);
    const auto record = context.parameters.value("record").value<app_record>();
    auto *dialog = new QDialog(parent);
    dialog->setWindowTitle(record.id ? "Изменить приложение" : "Добавить приложение");
    dialog->resize(640, 700);
    auto *root = new QVBoxLayout(dialog);
    root->setContentsMargins(24, 24, 24, 24);
    root->addWidget(label(dialog->windowTitle(), "heading"));
    auto *form = new QWidget;
    auto *layout = new QVBoxLayout(form);
    layout->setContentsMargins(0, 16, 0, 8);
    layout->setSpacing(6);
    auto *path = new QLineEdit(record.path);
    path->setObjectName("application_path");
    auto *browse = button("Обзор…");
    auto *file_row = new QWidget;
    auto *file_layout = new QHBoxLayout(file_row);
    file_layout->setContentsMargins(0, 0, 0, 0);
    file_layout->addWidget(path, 1);
    file_layout->addWidget(browse);
    field(layout, "Файл приложения или ярлык", file_row);
    auto *name = new QLineEdit(record.name);
    name->setObjectName("application_name");
    name->setMaxLength(256);
    field(layout, "Название", name);
    auto *arguments = new QLineEdit(record.arguments);
    arguments->setObjectName("application_arguments");
    field(layout, "Аргументы запуска", arguments);
    auto *directory = new QLineEdit(record.working_directory);
    directory->setPlaceholderText("По умолчанию — папка приложения");
    field(layout, "Рабочая папка", directory);
    auto *tags = new QLineEdit(record.tags.join(", "));
    tags->setPlaceholderText("разработка, работа");
    field(layout, "Теги через запятую", tags);
    auto *favorite = new QCheckBox("В избранном");
    favorite->setChecked(record.favorite);
    layout->addWidget(favorite);
    auto *error = label("");
    error->setStyleSheet("color: #C88888;");
    layout->addWidget(error);
    layout->addStretch();
    root->addWidget(scroll(form), 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save);
    buttons->button(QDialogButtonBox::Cancel)->setText("Отмена");
    auto *save = buttons->button(QDialogButtonBox::Save);
    save->setText(record.id ? "Сохранить" : "Добавить");
    save->setProperty("primary", true);
    root->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(browse, &QPushButton::clicked, dialog, [=] {
        const auto file = QFileDialog::getOpenFileName(dialog, "Выберите приложение", {},
                                                       "Приложения (*.exe *.lnk *.bat *.cmd);;Все файлы (*)");
        if (!file.isEmpty()) {
            path->setText(file);
            error->clear();
            browse->setEnabled(false);
            service->resolve_application(file);
        }
    });
    QObject::connect(service, &DesktopService::application_resolved, dialog,
                     [=](const QString &requested_path, const app_record &resolved) {
                         if (QDir::cleanPath(path->text()) != QDir::cleanPath(requested_path))
                             return;
                         path->setText(resolved.path);
                         if (name->text().isEmpty() || name->text() == name->property("resolved_name").toString())
                             name->setText(resolved.name);
                         name->setProperty("resolved_name", resolved.name);
                         arguments->setText(resolved.arguments);
                         directory->setText(resolved.working_directory);
                         browse->setEnabled(true);
                     });
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
        app_record updated = record;
        updated.name = name->text().trimmed();
        updated.path = QDir::fromNativeSeparators(path->text().trimmed());
        updated.arguments = arguments->text();
        updated.working_directory = directory->text().trimmed();
        updated.tags = parse_tags(tags->text());
        updated.favorite = favorite->isChecked();
        if (updated.name.isEmpty() || !QFileInfo(updated.path).isAbsolute() ||
            QFileInfo(updated.path).suffix().isEmpty()) {
            error->setText("Укажите название и абсолютный путь к файлу приложения.");
            return;
        }
        if (!updated.working_directory.isEmpty() && !QFileInfo(updated.working_directory).isAbsolute()) {
            error->setText("Рабочая папка должна быть абсолютным путём.");
            return;
        }
        for (const auto &tag : updated.tags)
            if (!valid_tag_name(tag)) {
                error->setText("Тег: до 64 символов, без пробелов, запятых и #.");
                return;
            }
        if (QFileInfo(updated.path).suffix().compare("lnk", Qt::CaseInsensitive) == 0) {
            service->resolve_application(updated.path);
            error->setText("Ярлык прочитан. Проверьте параметры и сохраните ещё раз.");
            return;
        }
        error->clear();
        save->setEnabled(false);
        service->save_application(std::move(updated));
    });
    QObject::connect(service, &DesktopService::message, dialog, [=](const QString &text) {
        if (!save->isEnabled() || !browse->isEnabled())
            error->setText(text);
    });
    QObject::connect(service, &DesktopService::operation_finished, dialog, [=](const QString &operation, bool success) {
        if (operation == "resolve")
            browse->setEnabled(true);
        if (operation == "application" && !save->isEnabled()) {
            if (success)
                dialog->accept();
            else {
                save->setEnabled(true);
                if (error->text().isEmpty())
                    error->setText("Не удалось сохранить приложение.");
            }
        }
    });
    return dialog;
}

QDialog *tag_editor(const ui_context &context, QWidget *parent) {
    auto *service = static_cast<DesktopService *>(context.service);
    const auto old_name = context.parameters.value("tag").toString();
    auto *dialog = new QDialog(parent);
    dialog->setWindowTitle(old_name.isEmpty() ? "Добавить тег" : "Изменить тег");
    dialog->resize(420, 220);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->addWidget(label(dialog->windowTitle(), "subheading"));
    auto *name = new QLineEdit(old_name);
    name->setPlaceholderText("Название тега");
    layout->addWidget(name);
    auto *error = label("", "muted");
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText("Сохранить");
    buttons->button(QDialogButtonBox::Cancel)->setText("Отмена");
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
        const auto tag = name->text().trimmed();
        if (!valid_tag_name(tag)) {
            error->setText("До 64 символов, без пробелов, запятых и #.");
            return;
        }
        error->clear();
        buttons->button(QDialogButtonBox::Save)->setEnabled(false);
        service->save_tag(old_name, tag);
    });
    QObject::connect(service, &DesktopService::message, dialog, [=](const QString &text) {
        if (!buttons->button(QDialogButtonBox::Save)->isEnabled())
            error->setText(text);
    });
    QObject::connect(service, &DesktopService::operation_finished, dialog, [=](const QString &operation, bool success) {
        if (operation == "tag") {
            if (success)
                dialog->accept();
            else {
                buttons->button(QDialogButtonBox::Save)->setEnabled(true);
                if (error->text().isEmpty())
                    error->setText("Не удалось сохранить тег.");
            }
        }
    });
    return dialog;
}

QDialog *scenario_editor(const ui_context &context, QWidget *parent) {
    auto *service = static_cast<DesktopService *>(context.service);
    const auto record = context.parameters.value("scenario").value<scenario_record>();
    auto *dialog = new QDialog(parent);
    dialog->setWindowTitle(record.id ? "Изменить сценарий" : "Создать сценарий");
    dialog->resize(760, 620);
    auto *root = new QVBoxLayout(dialog);
    root->setContentsMargins(24, 24, 24, 24);
    root->addWidget(label(dialog->windowTitle(), "heading"));
    auto *name = new QLineEdit(record.name);
    name->setMaxLength(128);
    root->addWidget(label("Название сценария", "muted"));
    root->addWidget(name);
    root->addSpacing(16);
    root->addWidget(label("Приложения · задержка перед запуском", "muted"));
    auto *table = new QTableWidget(0, 3);
    table->setHorizontalHeaderLabels({"Включено", "Приложение", "Задержка, с"});
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->resizeSection(0, 95);
    table->horizontalHeader()->resizeSection(2, 125);
    table->verticalHeader()->hide();
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    auto add_step = [=](const scenario_step &step) {
        const auto row = table->rowCount();
        table->insertRow(row);
        auto *enabled = new QTableWidgetItem;
        enabled->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        enabled->setCheckState(step.enabled ? Qt::Checked : Qt::Unchecked);
        table->setItem(row, 0, enabled);
        auto *apps = new QComboBox;
        apps->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        for (const auto &application : service->model.applications())
            apps->addItem(application.name, application.id);
        apps->setCurrentIndex(qMax(0, apps->findData(step.application_id)));
        table->setCellWidget(row, 1, apps);
        auto *delay = new QSpinBox;
        delay->setRange(0, 3600);
        delay->setValue(step.delay_ms / 1000);
        table->setCellWidget(row, 2, delay);
        table->setRowHeight(row, 52);
    };
    for (const auto &step : record.steps)
        add_step(step);
    root->addWidget(table, 1);
    auto *tools = new QHBoxLayout;
    auto *add = button("+ Добавить приложение");
    auto *remove = button("Удалить шаг");
    auto *up = button("Выше");
    auto *down = button("Ниже");
    tools->addWidget(add);
    tools->addWidget(remove);
    tools->addStretch();
    tools->addWidget(up);
    tools->addWidget(down);
    root->addLayout(tools);
    auto read_steps = [=] {
        QVector<scenario_step> steps;
        for (int row = 0; row < table->rowCount(); ++row) {
            auto *apps = qobject_cast<QComboBox *>(table->cellWidget(row, 1));
            auto *delay = qobject_cast<QSpinBox *>(table->cellWidget(row, 2));
            steps.append({apps->currentData().toLongLong(), delay->value() * 1000,
                          table->item(row, 0)->checkState() == Qt::Checked});
        }
        return steps;
    };
    QObject::connect(add, &QPushButton::clicked, dialog, [=] {
        if (service->model.rowCount() > 0) {
            add_step({});
            table->selectRow(table->rowCount() - 1);
        }
    });
    QObject::connect(remove, &QPushButton::clicked, dialog, [=] {
        if (table->currentRow() >= 0)
            table->removeRow(table->currentRow());
    });
    auto move_step = [=](int offset) {
        const auto row = table->currentRow();
        if (row < 0 || row + offset < 0 || row + offset >= table->rowCount())
            return;
        auto steps = read_steps();
        steps.swapItemsAt(row, row + offset);
        table->setRowCount(0);
        for (const auto &step : steps)
            add_step(step);
        table->selectRow(row + offset);
    };
    QObject::connect(up, &QPushButton::clicked, dialog, [=] { move_step(-1); });
    QObject::connect(down, &QPushButton::clicked, dialog, [=] { move_step(1); });
    auto *error = label("", "muted");
    root->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText("Сохранить");
    buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Cancel)->setText("Отмена");
    auto *save_and_run = buttons->addButton("Запустить сценарий", QDialogButtonBox::ActionRole);
    save_and_run->setProperty("primary", true);
    root->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    auto save_scenario = [=](bool run) {
        scenario_record updated{record.id, name->text().trimmed(), read_steps()};
        if (updated.name.isEmpty() || updated.steps.isEmpty()) {
            error->setText("Укажите название и добавьте приложение.");
            return;
        }
        dialog->setProperty("run_after_save", run);
        error->clear();
        buttons->button(QDialogButtonBox::Save)->setEnabled(false);
        save_and_run->setEnabled(false);
        service->save_scenario(std::move(updated));
    };
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] { save_scenario(false); });
    QObject::connect(save_and_run, &QPushButton::clicked, dialog, [=] { save_scenario(true); });
    QObject::connect(service, &DesktopService::message, dialog, [=](const QString &text) {
        if (!save_and_run->isEnabled())
            error->setText(text);
    });
    QObject::connect(service, &DesktopService::scenario_saved, dialog, [=](qint64 id) {
        if (!save_and_run->isEnabled() && dialog->property("run_after_save").toBool())
            service->run_scenario(id);
    });
    QObject::connect(service, &DesktopService::operation_finished, dialog, [=](const QString &operation, bool success) {
        if (operation == "scenario") {
            if (success)
                dialog->accept();
            else {
                buttons->button(QDialogButtonBox::Save)->setEnabled(true);
                save_and_run->setEnabled(true);
                if (error->text().isEmpty())
                    error->setText("Не удалось сохранить сценарий.");
            }
        }
    });
    return dialog;
}
}

void register_editors(UiRegistry &registry) {
    registry.add_default("application_editor", application_editor);
    registry.add_default("tag_editor", tag_editor);
    registry.add_default("scenario_editor", scenario_editor);
}
