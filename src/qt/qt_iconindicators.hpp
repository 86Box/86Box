#ifndef QT_ICONINDICATORS_HPP
#    define QT_INDICATORS_HPP

#include <QPixmap>
#include <QIcon>

enum IconIndicator {
    None,
    Active,
    Disabled,
};

QPixmap getIconWithIndicator(const QIcon &icon, const QSize &size, QIcon::Mode iconMode, IconIndicator indicator);

#endif