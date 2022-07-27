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

#include <QComboBox>
#include <QAbstractItemView>

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
    if (widget->isWindow()) {
        if (widget->layout() && widget->minimumSize() == widget->maximumSize()) {
            if (widget->minimumSize().width() < widget->minimumSizeHint().width()
                || widget->minimumSize().height() < widget->minimumSizeHint().height()) {
                widget->setFixedSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
                widget->layout()->setSizeConstraint(QLayout::SetFixedSize);
            }
            widget->setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, true);
        }
        widget->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    }

    if (qobject_cast<QComboBox*>(widget)) {
        qobject_cast<QComboBox*>(widget)->view()->setMinimumWidth(widget->minimumSizeHint().width());
    }
}
