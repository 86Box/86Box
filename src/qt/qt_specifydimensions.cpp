/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Specify dimensions UI module.
 *
 *
 *
 * Authors:	Cacodemon345
 *
 *		Copyright 2021-2022 Cacodemon345
 */
#include "qt_specifydimensions.h"
#include "ui_qt_specifydimensions.h"

#include "qt_mainwindow.hpp"
#include "ui_qt_mainwindow.h"

#include <QStatusBar>
#include <QMenuBar>
#include <QTimer>

extern "C"
{
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/video.h>
}

extern MainWindow* main_window;

SpecifyDimensions::SpecifyDimensions(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SpecifyDimensions)
{
    ui->setupUi(this);
    ui->checkBox->setChecked(vid_resize == 2);
    ui->spinBoxWidth->setRange(16, 2048);
    ui->spinBoxWidth->setValue(main_window->getRenderWidgetSize().width());
    ui->spinBoxHeight->setRange(16, 2048);
    ui->spinBoxHeight->setValue(main_window->getRenderWidgetSize().height());
}

SpecifyDimensions::~SpecifyDimensions()
{
    delete ui;
}

void SpecifyDimensions::on_SpecifyDimensions_accepted()
{
    if (ui->checkBox->isChecked())
    {
        vid_resize = 2;
        main_window->setWindowFlag(Qt::WindowMaximizeButtonHint, false);
        main_window->setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);
        window_remember = 0;
        fixed_size_x = ui->spinBoxWidth->value();
        fixed_size_y = ui->spinBoxHeight->value();

        main_window->setFixedSize(ui->spinBoxWidth->value(),
            ui->spinBoxHeight->value()
            + (!hide_status_bar ? main_window->statusBar()->height() : 0)
            + (!hide_tool_bar ? main_window->ui->toolBar->height() : 0)
            + main_window->menuBar()->height());

        emit main_window->updateMenuResizeOptions();
        main_window->show();
        main_window->ui->stackedWidget->switchRenderer((RendererStack::Renderer)vid_api);
    }
    else
    {
        main_window->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        main_window->ui->actionResizable_window->setChecked(false);
        vid_resize = 0;
        main_window->ui->actionResizable_window->trigger();
        window_remember = 1;
        window_w = ui->spinBoxWidth->value();
        window_h = ui->spinBoxHeight->value();
        main_window->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        emit main_window->resizeContents(ui->spinBoxWidth->value(), ui->spinBoxHeight->value());
        vid_resize = 1;
        emit main_window->updateMenuResizeOptions();
    }
    main_window->show();
    emit main_window->updateWindowRememberOption();
}
