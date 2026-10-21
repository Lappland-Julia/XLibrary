#ifndef XLIBRARY_UI_HELPERS_H
#define XLIBRARY_UI_HELPERS_H

#include <QDialog>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "ui_registry.h"
#include "windows_integration.h"

namespace ui_helpers {
class PageTransition : public QObject {
private:
    QWidget *contents;
    QGraphicsOpacityEffect *opacity;
    QPropertyAnimation *animation;

public:
    explicit PageTransition(QWidget *contents)
        : QObject(contents), contents(contents), opacity(new QGraphicsOpacityEffect(contents)),
          animation(new QPropertyAnimation(opacity, "opacity", this)) {
        contents->setGraphicsEffect(opacity);
        opacity->setEnabled(false);
        animation->setDuration(150);
        animation->setEndValue(1.0);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        connect(animation, &QPropertyAnimation::finished, this, [this] { opacity->setEnabled(false); });
    }

    void start() {
        const auto from = animation->state() == QAbstractAnimation::Running ? qMax(0.55, opacity->opacity()) : 0.45;
        animation->stop();
        opacity->setEnabled(false);
        if (!contents->isVisible() || !desktop_windows::animations_enabled())
            return;
        animation->setStartValue(from);
        opacity->setOpacity(from);
        opacity->setEnabled(true);
        animation->start();
    }
};

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
