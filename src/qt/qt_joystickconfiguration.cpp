/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Joystick configuration UI module.
 *
 *
 *
 * Authors:	Joakim L. Gilje <jgilje@jgilje.net>
 *
 *		Copyright 2021 Joakim L. Gilje
 */
#include "qt_joystickconfiguration.hpp"
#include "ui_qt_joystickconfiguration.h"

extern "C" {
#include <86box/device.h>
#include <86box/gameport.h>
}


#include <QLabel>
#include <QComboBox>
#include <QDialogButtonBox>
#include "qt_models_common.hpp"

JoystickConfiguration::JoystickConfiguration(int type, int joystick_nr, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::JoystickConfiguration),
    type(type),
    joystick_nr(joystick_nr)
{
    ui->setupUi(this);

    auto model = ui->comboBoxDevice->model();
    Models::AddEntry(model, "None", 0);
    for (int c = 0; c < joysticks_present; c++) {
        Models::AddEntry(model, plat_joystick_state[c].name, c+1);
    }

    ui->comboBoxDevice->setCurrentIndex(joystick_state[joystick_nr].plat_joystick_nr);
    layout()->setSizeConstraint(QLayout::SetFixedSize);
}

JoystickConfiguration::~JoystickConfiguration()
{
    delete ui;
}

int JoystickConfiguration::selectedDevice() {
    return ui->comboBoxDevice->currentIndex();
}

int JoystickConfiguration::selectedAxis(int axis) {
    auto* cbox = findChild<QComboBox*>(QString("cboxAxis%1").arg(QString::number(axis)));
    if (cbox == nullptr) {
        return 0;
    }
    return cbox->currentIndex();
}

int JoystickConfiguration::selectedButton(int button) {
    auto* cbox = findChild<QComboBox*>(QString("cboxButton%1").arg(QString::number(button)));
    if (cbox == nullptr) {
        return 0;
    }
    return cbox->currentIndex();
}

int JoystickConfiguration::selectedPov(int pov) {
    auto* cbox = findChild<QComboBox*>(QString("cboxPov%1").arg(QString::number(pov)));
    if (cbox == nullptr) {
        return 0;
    }
    return cbox->currentIndex();
}

void JoystickConfiguration::on_comboBoxDevice_currentIndexChanged(int index) {
    for (auto w : widgets) {
        ui->ct->removeWidget(w);
        w->deleteLater();
    }
    widgets.clear();

    if (index == 0) {
        return;
    }

    int joystick = index - 1;
    int row = 0;
    for (int c = 0; c < joystick_get_axis_count(type); c++) {
        /*Combo box*/
        auto label = new QLabel(joystick_get_axis_name(type, c), this);
        auto cbox = new QComboBox(this);
        cbox->setObjectName(QString("cboxAxis%1").arg(QString::number(c)));
        auto model = cbox->model();

        for (int d = 0; d < plat_joystick_state[joystick].nr_axes; d++) {
            Models::AddEntry(model, plat_joystick_state[joystick].axis[d].name, 0);
        }

        for (int d = 0; d < plat_joystick_state[joystick].nr_povs; d++) {
            Models::AddEntry(model, QString("%1 (X axis)").arg(plat_joystick_state[joystick].pov[d].name), 0);
            Models::AddEntry(model, QString("%1 (Y axis)").arg(plat_joystick_state[joystick].pov[d].name), 0);
        }

        for (int d = 0; d < plat_joystick_state[joystick].nr_sliders; d++) {
            Models::AddEntry(model, plat_joystick_state[joystick].slider[d].name, 0);
        }

        int nr_axes = plat_joystick_state[joystick].nr_axes;
        int nr_povs = plat_joystick_state[joystick].nr_povs;
        int mapping = joystick_state[joystick_nr].axis_mapping[c];
        if (mapping & POV_X)
            cbox->setCurrentIndex(nr_axes + (mapping & 3) * 2);
        else if (mapping & POV_Y)
            cbox->setCurrentIndex(nr_axes + (mapping & 3) * 2 + 1);
        else if (mapping & SLIDER)
            cbox->setCurrentIndex(nr_axes + nr_povs * 2 + (mapping & 3));
        else
            cbox->setCurrentIndex(mapping);

        ui->ct->addWidget(label, row, 0);
        ui->ct->addWidget(cbox, row, 1);

        widgets.append(label);
        widgets.append(cbox);

        ++row;
    }

    for (int c = 0; c < joystick_get_button_count(type); c++) {
        auto label = new QLabel(joystick_get_button_name(type, c), this);
        auto cbox = new QComboBox(this);
        cbox->setObjectName(QString("cboxButton%1").arg(QString::number(c)));
        auto model = cbox->model();

        for (int d = 0; d < plat_joystick_state[joystick].nr_buttons; d++) {
            Models::AddEntry(model, plat_joystick_state[joystick].button[d].name, 0);
        }

        cbox->setCurrentIndex(joystick_state[joystick_nr].button_mapping[c]);

        ui->ct->addWidget(label, row, 0);
        ui->ct->addWidget(cbox, row, 1);

        widgets.append(label);
        widgets.append(cbox);

        ++row;
    }

    for (int c = 0; c < joystick_get_pov_count(type) * 2; c++) {
        QLabel* label;
        if (c & 1) {
            label = new QLabel(QString("%1 (Y axis)").arg(joystick_get_pov_name(type, c/2)), this);
        } else {
            label = new QLabel(QString("%1 (X axis)").arg(joystick_get_pov_name(type, c/2)), this);
        }
        auto cbox = new QComboBox(this);
        cbox->setObjectName(QString("cboxPov%1").arg(QString::number(c)));
        auto model = cbox->model();

        for (int d = 0; d < plat_joystick_state[joystick].nr_povs; d++) {
            Models::AddEntry(model, QString("%1 (X axis)").arg(plat_joystick_state[joystick].pov[d].name), 0);
            Models::AddEntry(model, QString("%1 (Y axis)").arg(plat_joystick_state[joystick].pov[d].name), 0);
        }

        for (int d = 0; d < plat_joystick_state[joystick].nr_axes; d++) {
            Models::AddEntry(model, plat_joystick_state[joystick].axis[d].name, 0);
        }

        int mapping = joystick_state[joystick_nr].pov_mapping[c][0];
        int nr_povs = plat_joystick_state[joystick].nr_povs;
        if (mapping & POV_X)
            cbox->setCurrentIndex((mapping & 3) * 2);
        else if (mapping & POV_Y)
            cbox->setCurrentIndex((mapping & 3)*2 + 1);
        else
            cbox->setCurrentIndex(mapping + nr_povs * 2);

        mapping = joystick_state[joystick_nr].pov_mapping[c][1];
        if (mapping & POV_X)
            cbox->setCurrentIndex((mapping & 3)*2);
        else if (mapping & POV_Y)
            cbox->setCurrentIndex((mapping & 3)*2 + 1);
        else
            cbox->setCurrentIndex(mapping + nr_povs*2);

        ui->ct->addWidget(label, row, 0);
        ui->ct->addWidget(cbox, row, 1);

        widgets.append(label);
        widgets.append(cbox);

        ++row;
    }
}
