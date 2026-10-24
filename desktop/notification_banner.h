#ifndef XLIBRARY_NOTIFICATION_BANNER_H
#define XLIBRARY_NOTIFICATION_BANNER_H

#include "ui_helpers.h"

class QProgressBar;
class QPropertyAnimation;
class QToolButton;

class NotificationBanner : public ui_helpers::AnimatedReveal {
private:
    QSettings &settings;
    QLabel *message_icon;
    QLabel *message_label;
    QToolButton *dismiss_button;
    QProgressBar *progress;
    QPropertyAnimation *countdown;
    QString message;
    QString description;
    QString link_text;

public:
    std::function<void()> activated;

    explicit NotificationBanner(QSettings &settings, int spacing = 0, int duration_ms = 7000,
                                QWidget *parent = nullptr);
    void refresh_appearance();
    void show_message(const QString &text, const QString &description = {}, const QString &link_text = {});
    void dismiss();
};

#endif
