/*
* 86Box	A hypervisor and IBM PC system emulator that specializes in
*		running old operating systems and software designed for IBM
*		PC systems and compatibles from 1981 through fairly recent
*		system designs based on the PCI bus.
*
*		This file is part of the 86Box distribution.
*
*		86Box VM manager preferences module
*
*
*
* Authors:	cold-brewed
*
*		Copyright 2024 cold-brewed
*/

#include <QFileDialog>
#include <QStyle>

#include "qt_vmmanager_preferences.hpp"
#include "qt_vmmanager_config.hpp"
#include "ui_qt_vmmanager_preferences.h"

extern "C" {
#include <86box/86box.h>
}

VMManagerPreferences::
VMManagerPreferences(QWidget *parent) : ui(new Ui::VMManagerPreferences)
{
    ui->setupUi(this);
    ui->dirSelectButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
    connect(ui->dirSelectButton, &QPushButton::clicked, this, &VMManagerPreferences::chooseDirectoryLocation);

    const auto config = new VMManagerConfig(VMManagerConfig::ConfigType::General);
    const auto configSystemDir = config->getStringValue("system_directory");
    if(!configSystemDir.isEmpty()) {
        // Prefer this one
        ui->systemDirectory->setText(configSystemDir);
    } else if(!QString(vmm_path).isEmpty()) {
        // If specified on command line
        ui->systemDirectory->setText(QDir(vmm_path).path());
    }

    // TODO: Defaults
    const auto configUpdateCheck = config->getStringValue("update_check").toInt();
    ui->updateCheckBox->setChecked(configUpdateCheck);
    const auto useRegexSearch = config->getStringValue("regex_search").toInt();
    ui->regexSearchCheckBox->setChecked(useRegexSearch);


}

VMManagerPreferences::~
VMManagerPreferences()
    = default;

// Bad copy pasta from machine add
void
VMManagerPreferences::chooseDirectoryLocation()
{
    // TODO: FIXME: This is pulling in the CLI directory! Needs to be set properly elsewhere
    const auto directory = QFileDialog::getExistingDirectory(this, "Choose directory", QDir(vmm_path).path());
    ui->systemDirectory->setText(QDir::toNativeSeparators(directory));
}

void
VMManagerPreferences::accept()
{
    const auto config = new VMManagerConfig(VMManagerConfig::ConfigType::General);
    config->setStringValue("system_directory", ui->systemDirectory->text());
    config->setStringValue("update_check", ui->updateCheckBox->isChecked() ? "1" : "0");
    config->setStringValue("regex_search", ui->regexSearchCheckBox->isChecked() ? "1" : "0");
    QDialog::accept();
}

void
VMManagerPreferences::reject()
{
    QDialog::reject();
}