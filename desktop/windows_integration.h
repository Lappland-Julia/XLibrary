#ifndef XLIBRARY_WINDOWS_INTEGRATION_H
#define XLIBRARY_WINDOWS_INTEGRATION_H

#include <atomic>
#include <functional>
#include <QAbstractNativeEventFilter>
#include <QImage>
#include <QKeySequence>
#include <QObject>
#include <QSet>

#include "catalog_types.h"
#include "Application.h"

class QWidget;

namespace desktop_windows {
QSet<QString> running_paths(const QVector<app_record> &applications);
bool activate_window(const QString &path);
void launch_application(const app_record &record);
void open_folder(const QString &path);
QImage application_icon(const QString &path);
app_record resolve_application(const QString &path);
QVector<app_record> discover(bool registry, bool start_menu, QStringList &issues, const std::atomic_bool &cancelled,
                             const std::function<void(const discovery_progress &)> &progress = {});
bool set_autostart(bool enabled, QString &error);
bool animations_enabled();
void apply_backdrop(QWidget *window, bool dark, bool glass);
}

class GlobalHotkey : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
private:
    int current_id = 0;
    int next_id = 1;
    QKeySequence sequence;

public:
    explicit GlobalHotkey(QObject *parent = nullptr);
    ~GlobalHotkey() override;
    bool set_sequence(const QKeySequence &value, QString &error);
    bool nativeEventFilter(const QByteArray &event_type, void *message, qintptr *result) override;

signals:
    void activated();
};

#endif
