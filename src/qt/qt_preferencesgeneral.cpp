/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          General preferences UI module, shared by the emulator and the
 *          VM manager.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2026 David Hrdlička
 */
#include "qt_settings_completer.hpp"
#include "qt_preferences.hpp"
#include "qt_preferencesgeneral.hpp"
#include "ui_qt_preferencesgeneral.h"

extern "C" {
#include <86box/86box.h>
}

PreferencesGeneral::PreferencesGeneral(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PreferencesGeneral)
{
    ui->setupUi(this);

    scLanguage = new SettingsCompleter(ui->comboBoxLanguage, nullptr);

    ui->comboBoxLanguage->setItemData(0, 0);
    for (int i = 1; i < Preferences::languages.length(); i++) {
        ui->comboBoxLanguage->addItem(Preferences::languages[i].second, i);
        scLanguage->addDevice(nullptr, Preferences::languages[i].second);

        if (i == lang_id)
            ui->comboBoxLanguage->setCurrentIndex(ui->comboBoxLanguage->findData(i));
    }
    ui->comboBoxLanguage->model()->sort(Qt::AscendingOrder);

    ui->radioButtonSystem->setChecked(color_scheme == 0);
    ui->radioButtonLight->setChecked(color_scheme == 1);
    ui->radioButtonDark->setChecked(color_scheme == 2);

#ifndef Q_OS_WINDOWS
    ui->groupBoxColorScheme->setHidden(true);
#endif
}

PreferencesGeneral::~PreferencesGeneral()
{
    delete scLanguage;

    delete ui;
}

void
PreferencesGeneral::save()
{
    lang_id      = ui->comboBoxLanguage->currentData().toInt();
    color_scheme = (ui->radioButtonSystem->isChecked()) ? 0 : (ui->radioButtonLight->isChecked() ? 1 : 2);
}

void
PreferencesGeneral::on_pushButtonLanguage_released()
{
    ui->comboBoxLanguage->setCurrentIndex(0);
}
