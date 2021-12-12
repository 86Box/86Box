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

