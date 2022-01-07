#include "qt_styleoverride.hpp"

int StyleOverride::styleHint(
        StyleHint hint,
        const QStyleOption *option,
        const QWidget *widget,
        QStyleHintReturn *returnData) const
{
    /* Disable using menu with alt key */
    if (hint == QStyle::SH_MenuBar_AltKeyNavigation)
        return 0;

    return QProxyStyle::styleHint(hint, option, widget, returnData);
}