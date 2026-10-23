#include <QResizeEvent>

#include "appearance.h"
#include "library_splitter.h"

LibrarySplitter::LibrarySplitter(QSettings &settings, QWidget *navigation, QWidget *center, QWidget *details,
                                 QWidget *parent)
    : QSplitter(Qt::Horizontal, parent), settings(settings) {
    setObjectName("library_splitter");
    setChildrenCollapsible(false);
    setHandleWidth(6);
    addWidget(navigation);
    addWidget(center);
    addWidget(details);
    setStretchFactor(0, 0);
    setStretchFactor(1, 1);
    setStretchFactor(2, 0);
    handle(1)->setToolTip("Потяните, чтобы изменить ширину навигации");
    handle(1)->setAccessibleName("Ширина навигации");
    handle(2)->setToolTip("Потяните, чтобы изменить ширину сведений");
    handle(2)->setAccessibleName("Ширина сведений");
    connect(this, &QSplitter::splitterMoved, this, [this] {
        if (applying_layout)
            return;
        const auto scale = appearance::scaled(this->settings, 100) / 100.0;
        this->settings.setValue("window/navigation_width", qRound(widget(0)->width() / scale));
        if (widget(2)->isVisible())
            this->settings.setValue("window/details_width", qRound(widget(2)->width() / scale));
    });
}

void LibrarySplitter::update_layout(bool show_details) {
    details_requested = show_details;
    if (applying_layout || count() != 3)
        return;
    applying_layout = true;
    const auto navigation_min = qMin(appearance::scaled(settings, 176), qMax(160, width() / 3));
    const auto center_min = qMin(appearance::scaled(settings, 280), qMax(200, width() / 2));
    const auto details_min = appearance::scaled(settings, 264);
    widget(0)->setMinimumWidth(navigation_min);
    widget(0)->setMaximumWidth(appearance::scaled(settings, 420));
    widget(1)->setMinimumWidth(center_min);
    widget(2)->setMinimumWidth(details_min);
    widget(2)->setMaximumWidth(appearance::scaled(settings, 640));
    const bool visible = details_requested && width() >= navigation_min + center_min + details_min + 80;
    widget(2)->setVisible(visible);
    const auto navigation_width =
        appearance::scaled(settings, qBound(176, settings.value("window/navigation_width", 208).toInt(), 420));
    const auto details_width =
        visible ? appearance::scaled(settings, qBound(264, settings.value("window/details_width", 304).toInt(), 640))
                : 0;
    setSizes({navigation_width, qMax(center_min, width() - navigation_width - details_width - handleWidth() * 2),
              details_width});
    applying_layout = false;
}

void LibrarySplitter::resizeEvent(QResizeEvent *event) {
    QSplitter::resizeEvent(event);
    update_layout(details_requested);
}
