#ifndef QT_ICONINDICATORS_HPP
#    define QT_ICONINDICATORS_HPP

#include <QPixmap>
#include <QIcon>

enum IconIndicator {
    None,
    Active,
    WriteActive,
    ReadWriteActive,
    Disabled,
};

QPixmap getIconWithIndicator(const QIcon &icon, const QSize &size, QIcon::Mode iconMode, IconIndicator indicator);

#endif
