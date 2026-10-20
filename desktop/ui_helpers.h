#ifndef XLIBRARY_UI_HELPERS_H
#define XLIBRARY_UI_HELPERS_H

#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "ui_registry.h"

namespace ui_helpers {
inline QLabel *label(const QString &text, const char *property = nullptr) {
    auto *result = new QLabel(text);
    result->setTextFormat(Qt::PlainText);
    result->setWordWrap(true);
    if (property)
        result->setProperty(property, true);
    return result;
}
inline QPushButton *button(const QString &text, const char *property = nullptr) {
    auto *result = new QPushButton(text);
    if (property)
        result->setProperty(property, true);
    return result;
}
inline QPushButton *action_button(QAction *action, bool quiet = false, const QString &text = {}) {
    auto *result = button(text.isNull() ? action->text() : text, quiet ? "quiet" : nullptr);
    result->setIcon(action->icon());
    result->setEnabled(action->isEnabled());
    result->setToolTip(action->toolTip());
    QObject::connect(result, &QPushButton::clicked, action, &QAction::trigger);
    QObject::connect(action, &QAction::changed, result, [action, result, text] {
        result->setText(text.isNull() ? action->text() : text);
        result->setEnabled(action->isEnabled());
        result->setIcon(action->icon());
        result->setToolTip(action->toolTip());
    });
    return result;
}
inline QScrollArea *scroll(QWidget *contents) {
    auto *result = new QScrollArea;
    result->setWidgetResizable(true);
    result->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    result->setWidget(contents);
    return result;
}
inline void field(QVBoxLayout *layout, const QString &name, QWidget *control) {
    layout->addWidget(label(name, "muted"));
    layout->addWidget(control);
    layout->addSpacing(8);
}
inline void open_dialog(const QString &id, ui_context context) {
    if (auto *existing = context.window->findChild<QWidget *>(id, Qt::FindDirectChildrenOnly)) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }
    auto *widget = context.registry->create(id, context, context.window);
    if (auto *dialog = qobject_cast<QDialog *>(widget)) {
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowModality(Qt::WindowModal);
        dialog->show();
    } else {
        widget->setWindowFlag(Qt::Window, true);
        widget->setAttribute(Qt::WA_DeleteOnClose);
        widget->show();
    }
}
}

#endif
