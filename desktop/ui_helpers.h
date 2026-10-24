#ifndef XLIBRARY_UI_HELPERS_H
#define XLIBRARY_UI_HELPERS_H

#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include "appearance.h"
#include "localization.h"
#include "platform_integration.h"
#include "ui_registry.h"

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
        animation->setDuration(140);
        animation->setStartValue(0.88);
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
        if (!contents->isVisible() || !desktop_platform::animations_enabled())
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
    result->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    result->setWidget(contents);
    return result;
}
inline void field(QVBoxLayout *layout, const QString &name, QWidget *control) {
    auto *caption = label(name, "muted");
    caption->setBuddy(control);
    if (control->accessibleName().isEmpty())
        control->setAccessibleName(name);
    layout->addWidget(caption);
    layout->addWidget(control);
    layout->addSpacing(8);
}
inline void prepare_dialog(QDialog *dialog, const ui_context &context, const QSize &preferred,
                           const QSize &minimum = QSize(360, 240)) {
    const auto available = dialog->screen()->availableGeometry().size() - QSize(32, 64);
    dialog->setSizeGripEnabled(true);
    dialog->setMinimumSize(QSize(appearance::scaled(*context.settings, minimum.width()),
                                 appearance::scaled(*context.settings, minimum.height()))
                               .boundedTo(available));
    dialog->resize(QSize(appearance::scaled(*context.settings, preferred.width()),
                         appearance::scaled(*context.settings, preferred.height()))
                       .boundedTo(available));
}
inline bool confirm(QWidget *parent, const QString &title, const QString &text,
                    const QString &accept_text = QCoreApplication::translate("XLibrary", "Remove")) {
    QMessageBox dialog(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::Cancel, parent);
    dialog.setTextFormat(Qt::PlainText);
    dialog.button(QMessageBox::Yes)->setText(accept_text);
    dialog.button(QMessageBox::Cancel)->setText(QCoreApplication::translate("XLibrary", "Cancel"));
    dialog.setDefaultButton(QMessageBox::Cancel);
    dialog.setEscapeButton(QMessageBox::Cancel);
    return dialog.exec() == QMessageBox::Yes;
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
        // Ограничение действует и для заменяемых плагинами диалогов.
        const auto available = dialog->screen()->availableGeometry().size() - QSize(32, 64);
        const auto size_key = "dialogs/" + id + "/size";
        const auto saved = context.settings->value(size_key).toSize();
        if (saved.isValid() && !saved.isEmpty())
            dialog->resize(appearance::scaled(*context.settings, saved.width()),
                           appearance::scaled(*context.settings, saved.height()));
        dialog->setMinimumSize(dialog->minimumSize().boundedTo(available));
        dialog->resize(dialog->size().boundedTo(available));
        dialog->setSizeGripEnabled(true);
        QObject::connect(dialog, &QDialog::finished, dialog, [dialog, settings = context.settings, size_key] {
            const auto scale = appearance::scaled(*settings, 100) / 100.0;
            settings->setValue(size_key, QSize(qRound(dialog->width() / scale), qRound(dialog->height() / scale)));
        });
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
