/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header for 86Box VM manager list view delegate module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#ifndef QT_VMMANAGER_LISTVIEWDELEGATE_H
#define QT_VMMANAGER_LISTVIEWDELEGATE_H

#include <QPainter>
#include <QStyledItemDelegate>
#include "qt_vmmanager_system.hpp"

class VMManagerListViewDelegateStyle {
    VMManagerListViewDelegateStyle();

    [[nodiscard]] inline QRect systemNameBox(const QStyleOptionViewItem &option,
                                             const QModelIndex          &index) const;
    [[nodiscard]] inline qreal statusFontPointSize(const QFont &f) const;
    [[nodiscard]] inline QRect statusBox(const QStyleOptionViewItem &option, const QModelIndex &index) const;

    QSize    iconSize;
    QSize    smallIconSize;
    QMargins margins;
    int      spacingHorizontal;
    int      spacingVertical;

    friend class VMManagerListViewDelegate;
};

class VMManagerListViewDelegate final : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit VMManagerListViewDelegate(QObject *parent = nullptr);
    ~VMManagerListViewDelegate() override;
    using QStyledItemDelegate::QStyledItemDelegate;

    [[nodiscard]] QMargins contentsMargins() const;
    void                   setContentsMargins(int left, int top, int right, int bottom) const;

    [[nodiscard]] int horizontalSpacing() const;
    void              setHorizontalSpacing(int spacing) const;

    [[nodiscard]] int verticalSpacing() const;
    void              setVerticalSpacing(int spacing) const;

    void                paint(QPainter *painter, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem &option,
                                 const QModelIndex          &index) const override;

private:
    VMManagerListViewDelegateStyle *m_ptr;

    QIcon default_icon;
    QIcon stop_icon;
    QIcon running_icon;
    QIcon stopped_icon;
    QIcon paused_icon;
    QIcon unknown_icon;

    QColor bg_color;
    QColor highlight_color;
};
#endif // QT_VMMANAGER_LISTVIEWDELEGATE_H
