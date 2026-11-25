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
#include <QScrollArea>
#include <QVBoxLayout>
#include <functional>
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

    sound_gain_orig = sound_gain;

    /* A column per slider: the master gain first, then every source that
       exists right now. All of them go from -18 dB to +18 dB. */
    auto addColumn = [this](const QString &name, const int value, std::function<void(int)> apply) {
        auto *column = new QVBoxLayout();
        auto *label  = new QLabel(name, ui->scrollAreaWidgetContents);
        auto *slider = new QSlider(Qt::Vertical, ui->scrollAreaWidgetContents);
        auto *dB     = new QLabel(ui->scrollAreaWidgetContents);

        label->setAlignment(Qt::AlignCenter);
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        label->setFixedWidth(110);
        label->setFixedHeight(label->fontMetrics().lineSpacing() * 3);
        dB->setFixedHeight(dB->fontMetrics().lineSpacing());
        dB->setAlignment(Qt::AlignCenter);

        /* Every slider, the master's included, has the same scale. */
        slider->setRange(-18, 18);
        slider->setSingleStep(2);
        slider->setPageStep(4);
        slider->setTickInterval(6);
        slider->setTickPosition(QSlider::TicksBothSides);
        slider->setValue(value);
        dB->setText(tr("%1 dB").arg(value));

        connect(slider, &QSlider::valueChanged, this, [dB, apply](int v) {
            apply(v);
            dB->setText(tr("%1 dB").arg(v));
        });

        column->addWidget(label);
        column->addWidget(slider, 1, Qt::AlignHCenter);
        column->addWidget(dB);
        ui->slidersLayout->addLayout(column);
    };

    addColumn(tr("Master"), sound_gain, [](int v) { sound_gain = v; });

    const int count = sound_source_get_count();

    for (int i = 0; i < count; i++) {
        source_gain_orig.push_back(sound_source_get_gain(i));
        addColumn(QString::fromUtf8(sound_source_get_label(i)), sound_source_get_gain(i),
                  [i](int v) { sound_source_set_gain(i, v); });
    }

    ui->slidersLayout->addStretch(1);
    setMinimumSize(360, 320);
}

SoundGain::~SoundGain()
{
    delete ui;
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
