#include <QSize>
#include <QPainter>
#include "qt_iconindicators.hpp"

QIcon
getIndicatorIcon(IconIndicator indicator)
{
    switch (indicator) {
        case Play:
            return QIcon(":/menuicons/qt/icons/run.ico");
        case Pause:
            return QIcon(":/menuicons/qt/icons/pause.ico");
        case Active:
            return QIcon(":/settings/qt/icons/active.ico");
        case WriteActive:
            return QIcon(":/settings/qt/icons/write_active.ico");
        case Record:
            return QIcon(":/settings/qt/icons/record.ico");
        case Disabled:
            return QIcon(":/settings/qt/icons/disabled.ico");
        case WriteProtected:
            return QIcon(":/settings/qt/icons/write_protected.ico");
        case New:
            return QIcon(":/settings/qt/icons/new.ico");
        case Browse:
            return QIcon(":/settings/qt/icons/browse.ico");
        case Eject:
            return QIcon(":/settings/qt/icons/eject.ico");
        case Export:
            return QIcon(":/settings/qt/icons/eject.ico");
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

    auto painter         = QPainter(&iconPixmap);
    auto indicatorPixmap = getIndicatorIcon((indicator == ReadWriteActive || indicator == WriteProtectedActive
                                            || indicator == PlayActive || indicator == PauseActive) ? Active :
                                            (indicator == RecordWriteActive) ? Record : indicator)
                                            .pixmap((indicator == Play || indicator == Pause || indicator == Record || indicator == RecordWriteActive) ? size / 2. : size);

    if (indicator == WriteProtectedBrowse)
        indicatorPixmap = getIndicatorIcon(WriteProtected).pixmap(size);

    if (indicator == Record || indicator == RecordWriteActive)
        painter.drawPixmap(size.width() / 2, size.height() / 2, indicatorPixmap);
    else
        painter.drawPixmap(0, (indicator == Play || indicator == Pause) ? (size.height() / 2) : 0, indicatorPixmap);
    if (indicator == PlayActive || indicator == PauseActive) {
        auto playPauseIndicatorPixmap = getIndicatorIcon(indicator == PlayActive ? Play : Pause).pixmap(size / 2.);
        painter.drawPixmap(0, size.height() / 2, playPauseIndicatorPixmap);
    } else if ((indicator == ReadWriteActive) || (indicator == WriteProtectedActive) || (indicator == RecordWriteActive)) {
        auto writeIndicatorPixmap = getIndicatorIcon(indicator == WriteProtectedActive ? WriteProtected : WriteActive).pixmap(size);
        painter.drawPixmap(0, 0, writeIndicatorPixmap);
    } else if (indicator == WriteProtectedBrowse) {
        auto browseIndicatorPixmap = getIndicatorIcon(Browse).pixmap(size);
        painter.drawPixmap(0, 0, browseIndicatorPixmap);
    }
    painter.end();

    return iconPixmap;
}
