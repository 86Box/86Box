/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Sound gain dialog UI module.
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2021-2022 Cacodemon345
 */
#include "qt_soundgain.hpp"
#include "ui_qt_soundgain.h"

#include <QLabel>
#include <QSlider>

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/plat.h>
#include <86box/sound.h>
}

SoundGain::SoundGain(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SoundGain)
{
    ui->setupUi(this);
    ui->verticalSlider->setValue(sound_gain);
    sound_gain_orig = sound_gain;

    /* One slider per source that exists right now, after the master gain. */
    const int count = sound_source_get_count();
    int       col   = 4;

    ui->gridLayout->removeWidget(ui->buttonBox);

    for (int i = 0; i < count; i++) {
        auto *label = new QLabel(QString::fromUtf8(sound_source_get_label(i)), this);
        label->setAlignment(Qt::AlignCenter);
        label->setWordWrap(true);
        label->setMaximumWidth(90);

        auto *slider = new QSlider(Qt::Vertical, this);
        slider->setRange(-24, 18);
        slider->setSingleStep(2);
        slider->setPageStep(4);
        slider->setTickPosition(QSlider::TicksBothSides);
        slider->setToolTip(tr("%1 dB").arg(sound_source_get_gain(i)));
        slider->setValue(sound_source_get_gain(i));

        source_gain_orig.push_back(sound_source_get_gain(i));

        connect(slider, &QSlider::valueChanged, this, [slider, i](int value) {
            sound_source_set_gain(i, value);
            slider->setToolTip(tr("%1 dB").arg(value));
        });

        ui->gridLayout->addWidget(label, 0, col);
        ui->gridLayout->addWidget(slider, 1, col, 10, 1, Qt::AlignHCenter);
        col++;
    }

    ui->gridLayout->addWidget(ui->buttonBox, 0, col, 7, 1);
}

SoundGain::~SoundGain()
{
    delete ui;
}

void
SoundGain::on_verticalSlider_valueChanged(int value)
{
    sound_gain = value;
}

void
SoundGain::on_SoundGain_accepted()
{
    sound_source_save_gains();
    config_save();
}

void
SoundGain::on_SoundGain_rejected()
{
    sound_gain = sound_gain_orig;

    for (int i = 0; i < (int) source_gain_orig.size(); i++)
        sound_source_set_gain(i, source_gain_orig[i]);
}
