/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Style override class.
 *
 *
 *
 * Authors:	Teemu Korhonen
 *
 *		Copyright 2022 Teemu Korhonen
 */
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

void StyleOverride::polish(QWidget* widget)
{
    QProxyStyle::polish(widget);
    /* Disable title bar context help buttons globally as they are unused. */
    if (widget->isWindow())
        widget->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
}
