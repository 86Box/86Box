/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          GPU Debugging Tools - VRAM Viewer implementation
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
#include <QLabel>
#include <QDir>
#include <QSettings>
#include <qt_gpudebug_visualnv.hpp>
#include "ui_qt_gpudebug_visualnv.h"

/* 86Box core includes */
extern "C"
{

}

VisualNVDialog::VisualNVDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::VisualNVDialog)
{
    ui->setupUi(this);
}

VisualNVDialog::~VisualNVDialog()
{
    
}