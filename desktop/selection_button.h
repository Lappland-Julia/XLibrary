#ifndef XLIBRARY_SELECTION_BUTTON_H
#define XLIBRARY_SELECTION_BUTTON_H

#include <QPushButton>
#include <QVariantAnimation>

// Общий элемент выбора для штатных групп и областей, добавляемых плагинами.
class SelectionButton : public QPushButton {
    QVariantAnimation animation;
    qreal selection_opacity = 0;

protected:
    void paintEvent(QPaintEvent *event) override;

public:
    explicit SelectionButton(const QString &text, QWidget *parent = nullptr);
};

#endif
