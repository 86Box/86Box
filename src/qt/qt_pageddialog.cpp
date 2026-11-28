/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Paged dialog UI module, shared by the settings and the
 *          preferences.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2021-2022 Cacodemon345
 */
#include "qt_pageddialog.hpp"
#include "ui_qt_pageddialog.h"

#include <QAbstractListModel>
#include <QApplication>
#include <QFontMetrics>
#include <QIcon>
#include <QLayout>

PagedDialogStack::PagedDialogStack(QWidget *parent)
    : QStackedWidget(parent)
{
    connect(this, &QStackedWidget::currentChanged, this, [this]() { updateGeometry(); });
}

void
PagedDialogStack::setFitCurrentPage(bool enabled)
{
    fitCurrentPage = enabled;
    updateGeometry();
}

QSize
PagedDialogStack::sizeHint() const
{
    const QWidget *page = currentWidget();
    if (!fitCurrentPage || page == nullptr)
        return QStackedWidget::sizeHint();

    const QMargins margins = contentsMargins();
    return page->sizeHint() + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

QSize
PagedDialogStack::minimumSizeHint() const
{
    const QWidget *page = currentWidget();
    if (!fitCurrentPage || page == nullptr)
        return QStackedWidget::minimumSizeHint();

    const QMargins margins = contentsMargins();
    return page->minimumSizeHint().expandedTo(page->minimumSize()) +
           QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

bool
PagedDialogStack::hasHeightForWidth() const
{
    if (!fitCurrentPage)
        return QStackedWidget::hasHeightForWidth();

    return currentWidget() != nullptr && currentWidget()->hasHeightForWidth();
}

int
PagedDialogStack::heightForWidth(int width) const
{
    const QWidget *page = currentWidget();
    if (!fitCurrentPage)
        return QStackedWidget::heightForWidth(width);
    if (page == nullptr || !page->hasHeightForWidth())
        return -1;

    const QMargins margins = contentsMargins();
    return page->heightForWidth(qMax(0, width - margins.left() - margins.right())) + margins.top() + margins.bottom();
}

class PageListModel : public QAbstractListModel {
public:
    PageListModel(const QVector<PagedDialog::Page> &pages, QObject *parent)
        : QAbstractListModel(parent)
        , pages(pages)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        fontHeight = QFontMetrics(qApp->font()).height();
#else
        fontHeight = QApplication::fontMetrics().height();
#endif
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;

private:
    QVector<PagedDialog::Page> pages;
    int                        fontHeight;
};

QVariant
PageListModel::data(const QModelIndex &index, int role) const
{
    Q_ASSERT(checkIndex(index, QAbstractItemModel::CheckIndexOption::IndexIsValid | QAbstractItemModel::CheckIndexOption::ParentIsInvalid));

    switch (role) {
        case Qt::DisplayRole:
            return tr(pages.at(index.row()).name);
        case Qt::DecorationRole:
            return QIcon(QString(":/settings/qt/icons/%1.ico").arg(pages.at(index.row()).icon));
        case Qt::SizeHintRole:
            return QSize(-1, fontHeight * 2);
        default:
            return {};
    }
}

int
PageListModel::rowCount(const QModelIndex &parent) const
{
    (void) parent;
    return pages.size();
}

PagedDialog::PagedDialog(const QString &title, const QSize &defaultSize, const QVector<Page> &pages,
                         QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PagedDialog)
    , defaultSize(defaultSize)
{
    ui->setupUi(this);
    setWindowTitle(title);

    ui->listView->setModel(new PageListModel(pages, this));

    connect(ui->listView->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current, const QModelIndex &previous) {
                aboutToShowPage(current.row());
                ui->stackedWidget->setCurrentIndex(current.row());
                ui->headerIcon->setPixmap(qvariant_cast<QIcon>(ui->listView->model()->data(current, Qt::DecorationRole)).pixmap(QSize(16, 16)));
                ui->headerLabel->setText(ui->listView->model()->data(current, Qt::DisplayRole).toString());
                growToFitPages();
            });
}

PagedDialog::~PagedDialog()
{
    delete ui;
}

QStackedWidget *
PagedDialog::pageStack() const
{
    return ui->stackedWidget;
}

void
PagedDialog::setFitCurrentPage(bool enabled)
{
    ui->stackedWidget->setFitCurrentPage(enabled);
}

void
PagedDialog::showFirstPage()
{
    showPage(0);
}

void
PagedDialog::showPage(int index)
{
    ui->listView->setCurrentIndex(ui->listView->model()->index(index, 0));
}

void
PagedDialog::aboutToShowPage(int index)
{
    (void) index;
}

/* Take the initial size on the first show, when the pages are in place,
   using the stack's sizing policy and the text of the language in use. */
void
PagedDialog::showEvent(QShowEvent *event)
{
    if (!sized) {
        resize(minimumSizeHint().expandedTo(defaultSize));
        sized = true;
    }
    QDialog::showEvent(event);
}

void
PagedDialog::growToFitPages()
{
    if (!sized)
        return;

    /* The stack sits inside another widget's layout. Refresh those inner
       layouts first so the top-level size hint includes the selected page. */
    for (QWidget *widget = ui->stackedWidget; widget != this; widget = widget->parentWidget()) {
        if (widget->layout() != nullptr) {
            widget->layout()->invalidate();
            widget->layout()->activate();
        }
        widget->updateGeometry();
    }
    layout()->invalidate();
    layout()->activate();
    resize(size().expandedTo(minimumSizeHint()));
}
