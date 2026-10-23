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
PagedDialog::showFirstPage()
{
    ui->listView->setCurrentIndex(ui->listView->model()->index(0, 0));
}

void
PagedDialog::aboutToShowPage(int index)
{
    (void) index;
}

/* The size is taken once, on the first show, when the pages are in place:
   the page stack needs as much room as its largest page, so the dialog fits
   all of them, with the text of the language in use. */
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

    /* The new page is not counted until the layout is brought up to date. */
    layout()->activate();
    resize(size().expandedTo(minimumSizeHint()));
}
