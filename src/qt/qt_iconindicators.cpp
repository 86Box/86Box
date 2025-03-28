#include <QSize>
#include <QPainter>
#include "qt_iconindicators.hpp"

QIcon
getIndicatorIcon(IconIndicator indicator)
{
    switch (indicator) {
        case Active:
            return QIcon(":/settings/qt/icons/active.ico");
        case Disabled:
            return QIcon(":/settings/qt/icons/disabled.ico");
        default:
            return QIcon();
    }
}

QPixmap
getIconWithIndicator(const QIcon &icon, const QSize &size, QIcon::Mode iconMode, IconIndicator indicator)
{
    auto iconPixmap = icon.pixmap(size, iconMode);

    if (indicator == None)
        return iconPixmap;

    auto painter = QPainter(&iconPixmap);
    auto indicatorPixmap = getIndicatorIcon(indicator).pixmap(size);

    painter.drawPixmap(0, 0, indicatorPixmap);
    painter.end();

    return iconPixmap;
}