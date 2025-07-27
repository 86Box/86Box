/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          GPU Debugging Tools - Visual NV Debugger implementation
 *
 *
 *
 * Authors: starfrost
 *
 *          Copyright 2025 starfrost
 */

/* C++ includes */
#include <cstdbool>
#include <cstdint>


/* Qt includes*/
#include <QDebug>
#include <QComboBox>
#include <QPushButton>
#include <QFormLayout>
#include <QSpinBox>
#include <QCheckBox>
#include <QFrame>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QLabel>
#include <QDir>
#include <QSettings>
#include <QFileDialog>
#include <qt_gpudebug_visualnv.hpp>
#include "ui_qt_gpudebug_visualnv.h"

/* 86Box core includes */
extern "C"
{
    /* NOTE: DO NOT REMOVE */
    #include <86box/86box.h>
    #include <86box/device.h>
    #include <86box/mem.h>
    #include <86box/pci.h>
    #include <86box/rom.h>
    #include <86box/video.h>
    #include <86box/nv/vid_nv.h>
    #include <86box/nv/vid_nv3.h>
}

VisualNVDialog::VisualNVDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::VisualNVDialog)
{
    ui->setupUi(this);

    connect(ui->btnLoadSavestate, &QPushButton::clicked, this, &VisualNVDialog::on_btnLoadSavestate_clicked);
    connect(ui->fbStartAddress, &QPlainTextEdit::textChanged, this, &VisualNVDialog::on_fbStartAddress_changed);
    connect(ui->bPitch0Value, &QPlainTextEdit::textChanged, this, &VisualNVDialog::on_bPitch0Value_changed);
    connect(ui->bPitch1Value, &QPlainTextEdit::textChanged, this, &VisualNVDialog::on_bPitch1Value_changed);
}

// VisualNV dialog destructor
VisualNVDialog::~VisualNVDialog()
{
    
}

void VisualNVDialog::on_btnLoadSavestate_clicked()
{
    if (!nv3)
        return; 

    QString bar0_file_name = QFileDialog::getOpenFileName
    (
        this,
        tr("Please provide NVPlay 0.3.0.7+ NV3BAR0.BIN file"),
        ".",
        tr("NVPlay MMIO Dump Files (*.bin)")
    );

    QString bar1_file_name = QFileDialog::getOpenFileName
    (
        this,
        tr("Please provide NVPlay 0.3.0.7+ NV3BAR1.BIN file"),
        ".",
        tr("NVPlay VRAM/RAMIN Dump Files (*.bin)")
    );

    //
    // Open both dump files
    //

    QFile bar0(bar0_file_name);
    QFile bar1(bar1_file_name); 

    if (!bar0.open(QIODevice::ReadOnly))
    {
        warning("Failed to open NV3BAR0.bin!");
        return;
    }

    if (!bar1.open(QIODevice::ReadOnly))
    {
        warning("Failed to open NV3BAR1.bin!");
        return;
    }

    if (bar0.size() != NV3_MMIO_SIZE
        || bar1.size() != NV3_MMIO_SIZE)
    {
        warning("NV3BAR0.bin and NV3BAR1.bin must be 16MB!");
        bar0.close();
        bar1.close();
        return; 
    }

    // Load VRAM contents only for now. Todo: MMIO+RAMIN
    QString oldTitle = this->windowTitle();

    this->setWindowTitle(tr("RIVA 128 Realtime Debugger: Savestate Loading..."));

    bar1.read((char*)nv3->nvbase.svga.vram, nv3->nvbase.vram_amount);

    this->setWindowTitle(oldTitle);

}

void VisualNVDialog::on_fbStartAddress_changed()
{
    if (nv3)
    {
        nv3->nvbase.debug_dba_enabled = true; 

        bool ok = true;

        nv3->nvbase.debug_dba = ui->fbStartAddress->toPlainText().toInt(&ok);

        if (!ok)
            nv3->nvbase.debug_dba_enabled = false; 
    }
}

void VisualNVDialog::on_bPitch0Value_changed()
{
    if (nv3)
    {
        bool ok = true;

        uint32_t old_bpitch = nv3->pgraph.bpitch[0];

        nv3->pgraph.bpitch[0] = ui->bPitch0Value->toPlainText().toInt(&ok);

        if (!ok)
            nv3->pgraph.bpitch[0] = old_bpitch;
    }

}

void VisualNVDialog::on_bPitch1Value_changed()
{
    if (nv3)
    {
        bool ok = true;

        uint32_t old_bpitch = nv3->pgraph.bpitch[1];

        nv3->pgraph.bpitch[1] = ui->bPitch0Value->toPlainText().toInt(&ok);

        if (!ok)
            nv3->pgraph.bpitch[1] = old_bpitch;
    }

}