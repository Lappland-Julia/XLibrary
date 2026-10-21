#ifndef XLIBRARY_UI_HELPERS_H
#define XLIBRARY_UI_HELPERS_H

#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include "ui_registry.h"
#include "windows_integration.h"

namespace ui_helpers {
class TransitionOverlay : public QWidget {
public:
    QPixmap snapshot;
    qreal opacity = 1.0;

    explicit TransitionOverlay(QWidget *parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        hide();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        // Непрозрачный фон сохраняет тон поверхности во время перехода.
        painter.fillRect(rect(), palette().window());
        painter.setOpacity(opacity);
        painter.drawPixmap(rect(), snapshot);
    }
};

class PageTransition : public QWidget {
private:
    QWidget *contents;
    TransitionOverlay *overlay;
    QVariantAnimation *animation;

protected:
    void resizeEvent(QResizeEvent *event) override {
        animation->stop();
        overlay->hide();
        overlay->snapshot = QPixmap{};
        QWidget::resizeEvent(event);
    }

public:
    explicit PageTransition(QWidget *contents)
        : contents(contents), overlay(new TransitionOverlay(this)), animation(new QVariantAnimation(this)) {
        setAutoFillBackground(true);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(contents);
        animation->setDuration(150);
        animation->setStartValue(0.45);
        animation->setEndValue(1.0);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        connect(animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            overlay->opacity = value.toReal();
            overlay->update();
        });
        connect(animation, &QVariantAnimation::finished, this, [this] {
            overlay->hide();
            overlay->snapshot = QPixmap{};
        });
    }

    void start() {
        animation->stop();
        overlay->hide();
        overlay->snapshot = QPixmap{};
        if (!contents->isVisible() || !desktop_windows::animations_enabled())
            return;
        overlay->snapshot = contents->grab();
        overlay->setGeometry(rect());
        overlay->opacity = animation->startValue().toReal();
        overlay->show();
        overlay->raise();
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
