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
    : QStyledItemDelegate(parent)
    , m_ptr(new VMManagerListViewDelegateStyle)
{
    default_icon = QIcon(":/settings/qt/icons/86Box-gray.ico");
    stop_icon    = QApplication::style()->standardIcon(QStyle::SP_MediaStop);
    running_icon = QIcon(":/menuicons/qt/icons/run.ico");
    stopped_icon = QIcon(":/menuicons/qt/icons/acpi_shutdown.ico");
    paused_icon  = QIcon(":/menuicons/qt/icons/pause.ico");
    unknown_icon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxQuestion);
}

VMManagerListViewDelegate::~VMManagerListViewDelegate()
    = default;

void
VMManagerListViewDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
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
    auto process_status  = process_variant.value<VMManagerSystem::ProcessStatus>();
    // The main icon, configurable. Falls back to default if it cannot be loaded.
    auto customIcon = index.data(VMManagerModel::Roles::Icon).toString();
    opt.icon        = default_icon;
    if (!customIcon.isEmpty()) {
        const auto customPixmap = QPixmap(customIcon);
        if (!customPixmap.isNull())
            opt.icon = customPixmap;
    }

    // Set the status icon based on the process status
    QIcon status_icon;
    switch (process_status) {
        case VMManagerSystem::ProcessStatus::Running:
        case VMManagerSystem::ProcessStatus::RunningWaiting:
            status_icon = running_icon;
            break;
        case VMManagerSystem::ProcessStatus::Stopped:
            status_icon = stopped_icon;
            break;
        case VMManagerSystem::ProcessStatus::PausedWaiting:
        case VMManagerSystem::ProcessStatus::Paused:
            status_icon = paused_icon;
            break;
        default:
            status_icon = unknown_icon;
    }

    const bool hasIcon = !opt.icon.isNull();
    QFont      f(opt.font);

    f.setPointSizeF(m_ptr->statusFontPointSize(opt.font));

    painter->save();
    painter->setClipping(true);
    painter->setClipRect(rect);
    painter->setFont(opt.font);

    // Draw the background, then let the style draw the selection and hover states
    // the same way as in other item views
    const bool    selected = (opt.state & QStyle::State_Selected);
    const QStyle *style    = opt.widget ? opt.widget->style() : QApplication::style();
    // Pick the colors from the same group as the style does for the selection, so
    // the text of an unfocused selection matches its inactive highlight
    const QPalette::ColorGroup cg = !(opt.state & QStyle::State_Enabled) ? QPalette::Disabled
                                  : (opt.state & QStyle::State_Active)   ? QPalette::Active
                                                                         : QPalette::Inactive;
    painter->fillRect(rect, palette.color(cg, QPalette::Base));
    // Some styles (windows11) only draw the selection as a part of the whole item,
    // so draw an item without its contents
    QStyleOptionViewItem panelOpt(opt);
    panelOpt.text.clear();
    panelOpt.icon = QIcon();
    panelOpt.features &= ~(QStyleOptionViewItem::HasDisplay | QStyleOptionViewItem::HasDecoration);
    style->drawControl(QStyle::CE_ItemViewItem, &panelOpt, painter, opt.widget);

    // Styles can move the contents of list view items to make room for a selection
    // indicator of their own (windows11), so move ours by the same amount. They don't
    // know the view without the widget, so the difference is that shift.
    QStyleOptionViewItem decorationOpt(panelOpt);
    decorationOpt.features |= QStyleOptionViewItem::HasDecoration;
    decorationOpt.decorationSize = m_ptr->iconSize;
    const int contentShift = style->subElementRect(QStyle::SE_ItemViewItemDecoration, &decorationOpt, opt.widget).left()
                           - style->subElementRect(QStyle::SE_ItemViewItemDecoration, &decorationOpt, nullptr).left();

    // Styles that fill the selection with the highlight color draw its text in the
    // highlighted text color, the others keep the regular text color
    const bool highlightedText = selected && util::selectionUsesHighlightedText(style);

    // Draw system icon
    if (hasIcon) {
        painter->drawPixmap(contentRect.left() + contentShift, contentRect.top(),
                            opt.icon.pixmap(m_ptr->iconSize));
    }

    // System name
    QRect systemNameRect(m_ptr->systemNameBox(opt, index));

    systemNameRect.moveTo(m_ptr->margins.left() + contentShift + m_ptr->iconSize.width()
                              + m_ptr->spacingHorizontal,
                          contentRect.top());
    // If desired, font can be changed here
    //    painter->setFont(f);
    painter->setFont(opt.font);
    painter->setPen(palette.color(cg, highlightedText ? QPalette::HighlightedText : QPalette::Text));
    painter->drawText(systemNameRect, Qt::TextSingleLine, opt.text);

    // Draw status icon
    painter->drawPixmap(systemNameRect.left(), systemNameRect.bottom() + m_ptr->spacingVertical,
                        status_icon.pixmap(m_ptr->smallIconSize));

    // This rectangle is around the status icon
    // auto point = QPoint(systemNameRect.left(), systemNameRect.bottom()
    //                         + m_ptr->spacingVertical);
    // auto point2 = QPoint(point.x() + m_ptr->smallIconSize.width(), point.y() + m_ptr->smallIconSize.height());
    // auto arect = QRect(point, point2);
    // painter->drawRect(arect);

    // Draw status text
    QRect statusRect(m_ptr->statusBox(opt, index));
    int   extraaa = 2;
    statusRect.moveTo(systemNameRect.left() + m_ptr->margins.left() + m_ptr->smallIconSize.width(),
                      systemNameRect.bottom() + m_ptr->spacingVertical + extraaa + (m_ptr->smallIconSize.height() - systemNameRect.height()));

    //    painter->setFont(opt.font);
    painter->setFont(f);
    painter->setPen(palette.color(cg, highlightedText ? QPalette::HighlightedText : QPalette::WindowText));
    painter->drawText(statusRect, Qt::TextSingleLine,
                      index.data(VMManagerModel::Roles::ProcessStatusString).toString());

    painter->restore();
}

QMargins
VMManagerListViewDelegate::contentsMargins() const
{
    return m_ptr->margins;
}

void
VMManagerListViewDelegate::setContentsMargins(const int left, const int top, const int right, const int bottom) const
{
    m_ptr->margins = QMargins(left, top, right, bottom);
}

int
VMManagerListViewDelegate::horizontalSpacing() const
{
    return m_ptr->spacingHorizontal;
}

void
VMManagerListViewDelegate::setHorizontalSpacing(const int spacing) const
{
    m_ptr->spacingHorizontal = spacing;
}

int
VMManagerListViewDelegate::verticalSpacing() const
{
    return m_ptr->spacingVertical;
}

void
VMManagerListViewDelegate::setVerticalSpacing(const int spacing) const
{
    m_ptr->spacingVertical = spacing;
}

QSize
VMManagerListViewDelegate::sizeHint(const QStyleOptionViewItem &option,
                                    const QModelIndex          &index) const
{
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);

    const int textHeight = m_ptr->systemNameBox(opt, index).height()
        + m_ptr->spacingVertical + m_ptr->statusBox(opt, index).height();
    const int iconHeight = m_ptr->iconSize.height();
    const int h          = textHeight > iconHeight ? textHeight : iconHeight;

    // return the same width
    // for height, add margins on top and bottom *plus* either the text or icon height, whichever is greater
    // Note: text height is the combined value of the system name and the status just below the name
    return { opt.rect.width(), m_ptr->margins.top() + h + m_ptr->margins.bottom() };
}

VMManagerListViewDelegateStyle::VMManagerListViewDelegateStyle()
    : iconSize(32, 32)
    , smallIconSize(16, 16)
    ,
    margins(4, 10, 8, 10)
    ,
    // Spacing between icon and text
    spacingHorizontal(8)
    , spacingVertical(4)
{
    //
}

/* Returns the box text is drawn into from its left edge. The bounding rectangle
   only covers the ink, which can start to the right of where the text is drawn
   from, so cover both the ink and the advance from there on. */
static QRect
textBox(const QFontMetrics &fm, const QString &text)
{
    const QRect ink = fm.boundingRect(text);

    return QRect(0, ink.top(), qMax(fm.horizontalAdvance(text), ink.right() + 1), ink.height()).adjusted(0, 0, 1, 1);
}

QRect
VMManagerListViewDelegateStyle::statusBox(const QStyleOptionViewItem &option,
                                          const QModelIndex          &index) const
{
    QFont f(option.font);

    f.setPointSizeF(statusFontPointSize(option.font));

    return textBox(QFontMetrics(f), index.data(VMManagerModel::Roles::ProcessStatusString).toString());
}

qreal
VMManagerListViewDelegateStyle::statusFontPointSize(const QFont &f) const
{
    return 0.9 * f.pointSize();
    //    return 1*f.pointSize();
}

QRect
VMManagerListViewDelegateStyle::systemNameBox(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    return textBox(option.fontMetrics, option.text);
}
