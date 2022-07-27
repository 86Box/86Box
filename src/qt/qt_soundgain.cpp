/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Sound gain dialog UI module.
 *
 *
 *
 * Authors:	Cacodemon345
 *
 *		Copyright 2021-2022 Cacodemon345
 */
#include "qt_soundgain.hpp"
#include "ui_qt_soundgain.h"

extern "C"
{
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/sound.h>
}

SoundGain::SoundGain(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SoundGain)
{
    ui->setupUi(this);
    ui->verticalSlider->setValue(sound_gain);
    sound_gain_orig = sound_gain;
}

SoundGain::~SoundGain()
{
    delete ui;
}

void SoundGain::on_verticalSlider_valueChanged(int value)
{
    sound_gain = value;
}


void SoundGain::on_SoundGain_rejected()
{
    sound_gain = sound_gain_orig;
}
