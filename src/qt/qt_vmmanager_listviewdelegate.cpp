/*
* 86Box    A hypervisor and IBM PC system emulator that specializes in
*          running old operating systems and software designed for IBM
*          PC systems and compatibles from 1981 through fairly recent
*          system designs based on the PCI bus.
*
*          This file is part of the 86Box distribution.
*
*          86Box VM manager list view delegate module
*
* Authors: cold-brewed
*
*          Copyright 2024 cold-brewed
*/
#include <QApplication>

#include "qt_util.hpp"
#include "qt_vmmanager_listviewdelegate.hpp"
#include "qt_vmmanager_model.hpp"

// Thanks to scopchanov https://github.com/scopchanov/SO-MessageLog
// from https://stackoverflow.com/questions/53105343/is-it-possible-to-add-a-custom-widget-into-a-qlistview

VMManagerListViewDelegate::VMManagerListViewDelegate(QObject *parent)
    : QStyledItemDelegate(parent),
    m_ptr(new VMManagerListViewDelegateStyle)
{
    default_icon = QIcon(":/settings/qt/icons/86Box-gray.ico");
    stop_icon = QApplication::style()->standardIcon(QStyle::SP_MediaStop);
    running_icon = QIcon(":/menuicons/qt/icons/run.ico");
    stopped_icon = QIcon(":/menuicons/qt/icons/acpi_shutdown.ico");
    paused_icon = QIcon(":/menuicons/qt/icons/pause.ico");
    unknown_icon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxQuestion);

    highlight_color = QColor("#616161");
    bg_color = QColor("#272727");
}

VMManagerListViewDelegate::~VMManagerListViewDelegate()
= default;

void VMManagerListViewDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                      const QModelIndex &index) const {
    bool windows_light_mode = true;
#ifdef Q_OS_WINDOWS
    windows_light_mode = util::isWindowsLightTheme();
#endif
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    const QPalette &palette(opt.palette);
    // opt.rect = opt.rect.adjusted(0, 0, 0, 20);
    const QRect &rect(opt.rect);
    const QRect &contentRect(rect.adjusted(m_ptr->margins.left(),
                                              m_ptr->margins.top(),
                                              -m_ptr->margins.right(),
                                              -m_ptr->margins.bottom()));

    // The status icon represents the current state of the vm. Initially set to a default state.
    auto process_variant = index.data(VMManagerModel::Roles::ProcessStatus);
    auto process_status = process_variant.value<VMManagerSystem::ProcessStatus>();
    // The main icon, configurable. Falls back to default if it cannot be loaded.
    auto customIcon = index.data(VMManagerModel::Roles::Icon).toString();
    opt.icon = default_icon;
    if (!customIcon.isEmpty()) {
        const auto customPixmap = QPixmap(customIcon);
        if (!customPixmap.isNull())
            opt.icon = customPixmap;
    }

    // Set the status icon based on the process status
    QIcon status_icon;
    switch(process_status) {
        case VMManagerSystem::ProcessStatus::Running:
            status_icon = running_icon;
            break;
        case VMManagerSystem::ProcessStatus::Stopped:
            status_icon = stopped_icon;
            break;
        case VMManagerSystem::ProcessStatus::PausedWaiting:
        case VMManagerSystem::ProcessStatus::RunningWaiting:
        case VMManagerSystem::ProcessStatus::Paused:
            status_icon = paused_icon;
            break;
        default:
            status_icon = unknown_icon;
    }


    // Used to determine if the horizontal separator should be drawn
    const bool lastIndex = (index.model()->rowCount() - 1) == index.row();
    const bool hasIcon = !opt.icon.isNull();
    const int bottomEdge = rect.bottom();
    QFont f(opt.font);

    f.setPointSizeF(m_ptr->statusFontPointSize(opt.font));

    painter->save();
    painter->setClipping(true);
    painter->setClipRect(rect);
    painter->setFont(opt.font);

    // Draw the background
    if (opt.state & QStyle::State_Selected) {
        // When selected, only draw the highlighted part until the horizontal separator
        int offset = 2;
        auto highlightRect = rect.adjusted(0, 0, 0, -offset);
        painter->fillRect(highlightRect, windows_light_mode ? palette.highlight().color() : highlight_color);
        // Then fill the remainder with the normal color
        auto regularRect = rect.adjusted(0, rect.height()-offset, 0, 0);
        painter->fillRect(regularRect, windows_light_mode ? palette.light().color() : bg_color);
    } else {
        // Otherwise just draw the background color as usual
        painter->fillRect(rect, windows_light_mode ? palette.light().color() : bg_color);
    }

    // Draw bottom line. Last line gets a different color
    painter->setPen(lastIndex ? palette.dark().color()
                              : palette.mid().color());
    painter->drawLine(lastIndex ? rect.left() : m_ptr->margins.left(),
                      bottomEdge, rect.right(), bottomEdge);

    // Draw system icon
    if (hasIcon) {
        painter->drawPixmap(contentRect.left(), contentRect.top(),
                            opt.icon.pixmap(m_ptr->iconSize));
    }

    // System name
    QRect systemNameRect(m_ptr->systemNameBox(opt, index));

    systemNameRect.moveTo(m_ptr->margins.left() + m_ptr->iconSize.width()
                             + m_ptr->spacingHorizontal, contentRect.top());
    // If desired, font can be changed here
//    painter->setFont(f);
    painter->setFont(opt.font);
    painter->setPen(palette.text().color());
    painter->drawText(systemNameRect, Qt::TextSingleLine, opt.text);

    // Draw status icon
    painter->drawPixmap(systemNameRect.left(), systemNameRect.bottom()
                            + m_ptr->spacingVertical,
                        status_icon.pixmap(m_ptr->smallIconSize));

    // This rectangle is around the status icon
    // auto point = QPoint(systemNameRect.left(), systemNameRect.bottom()
    //                         + m_ptr->spacingVertical);
    // auto point2 = QPoint(point.x() + m_ptr->smallIconSize.width(), point.y() + m_ptr->smallIconSize.height());
    // auto arect = QRect(point, point2);
    // painter->drawRect(arect);

    // Draw status text
    QRect statusRect(m_ptr->statusBox(opt, index));
    int extraaa = 2;
    statusRect.moveTo(systemNameRect.left() + m_ptr->margins.left() + m_ptr->smallIconSize.width(),
                      systemNameRect.bottom() + m_ptr->spacingVertical + extraaa + (m_ptr->smallIconSize.height() - systemNameRect.height() ));

//    painter->setFont(opt.font);
    painter->setFont(f);
    painter->setPen(palette.windowText().color());
    painter->drawText(statusRect, Qt::TextSingleLine,
                      index.data(VMManagerModel::Roles::ProcessStatusString).toString());

    painter->restore();

}

QMargins VMManagerListViewDelegate::contentsMargins() const
{
    return m_ptr->margins;
}

void VMManagerListViewDelegate::setContentsMargins(const int left, const int top, const int right, const int bottom) const
{
    m_ptr->margins = QMargins(left, top, right, bottom);
}

int VMManagerListViewDelegate::horizontalSpacing() const
{
    return m_ptr->spacingHorizontal;
}

void VMManagerListViewDelegate::setHorizontalSpacing(const int spacing) const
{
    m_ptr->spacingHorizontal = spacing;
}

int VMManagerListViewDelegate::verticalSpacing() const
{
    return m_ptr->spacingVertical;
}

void VMManagerListViewDelegate::setVerticalSpacing(const int spacing) const
{
    m_ptr->spacingVertical = spacing;
}


QSize VMManagerListViewDelegate::sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const
{
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);

    const int textHeight = m_ptr->systemNameBox(opt, index).height()
        + m_ptr->spacingVertical + m_ptr->statusBox(opt, index).height();
    const int iconHeight = m_ptr->iconSize.height();
    const int h = textHeight > iconHeight ? textHeight : iconHeight;

    // return the same width
    // for height, add margins on top and bottom *plus* either the text or icon height, whichever is greater
    // Note: text height is the combined value of the system name and the status just below the name
    return {opt.rect.width(), m_ptr->margins.top() + h
                     + m_ptr->margins.bottom()};
}

VMManagerListViewDelegateStyle::VMManagerListViewDelegateStyle() :
    iconSize(32, 32),
    smallIconSize(16, 16),
    // bottom gets a little more than the top because of the custom separator
    margins(4, 10, 8, 12),
    // Spacing between icon and text
    spacingHorizontal(8),
    spacingVertical(4)
{
    //
}

QRect VMManagerListViewDelegateStyle::statusBox(const QStyleOptionViewItem &option,
                              const QModelIndex &index) const
{
    QFont f(option.font);

    f.setPointSizeF(statusFontPointSize(option.font));

    return QFontMetrics(f).boundingRect(index.data(VMManagerModel::Roles::ProcessStatusString).toString())
        .adjusted(0, 0, 1, 1);
}

qreal VMManagerListViewDelegateStyle::statusFontPointSize(const QFont &f) const
{
    return 0.75*f.pointSize();
//    return 1*f.pointSize();
}

QRect VMManagerListViewDelegateStyle::systemNameBox(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    return option.fontMetrics.boundingRect(option.text).adjusted(0, 0, 1, 1);
}
