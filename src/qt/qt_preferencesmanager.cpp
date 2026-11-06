/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VM manager preferences UI module.
 *
 * Authors: cold-brewed
 *          David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *          Copyright 2024 cold-brewed
 *          Copyright 2026 David Hrdlička
 */
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QStyle>
#include <cstring>

#include "qt_preferencesmanager.hpp"
#include "ui_qt_preferencesmanager.h"

extern "C" {
#include <86box/86box.h>
#include <86box/plat.h>
}

PreferencesManager::PreferencesManager(bool machinesRunning, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PreferencesManager)
{
    ui->setupUi(this);
    ui->dirSelectButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
    connect(ui->dirSelectButton, &QPushButton::clicked, this, &PreferencesManager::chooseDirectoryLocation);

    const auto configSystemDir = QString(vmm_path_cfg);
    if (!configSystemDir.isEmpty()) {
        // Prefer this one
        ui->systemDirectory->setText(QDir::toNativeSeparators(configSystemDir));
    } else if (!QString(vmm_path).isEmpty()) {
        // If specified on command line
        ui->systemDirectory->setText(QDir::toNativeSeparators(QDir(vmm_path).path()));
    }

    if (machinesRunning) {
        ui->systemDirectory->setEnabled(false);
        ui->dirSelectButton->setEnabled(false);
        ui->pushButtonDefaultSystemDir->setEnabled(false);
        ui->dirSelectButton->setToolTip(tr("To change the system directory, stop all running machines."));
    }

    ui->updateCheckBox->setChecked(vmm_update_check);
    ui->regexSearchCheckBox->setChecked(vmm_regex_search);
    ui->rememberSizePositionCheckBox->setChecked(vmm_window_remember);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    ui->deleteToTrashCheckBox->setChecked(vmm_delete_to_trash);
#else
    ui->deleteToTrashCheckBox->setVisible(false);
#endif
#ifdef DISCORD
    ui->discordCheckBox->setChecked(vmm_discord_presence != 0);
    ui->comboBoxDiscordPrivacy->setCurrentIndex(qMax(vmm_discord_presence - 1, 0));
    ui->comboBoxDiscordPrivacy->setEnabled(ui->discordCheckBox->isChecked());
    ui->labelDiscordPrivacy->setEnabled(ui->discordCheckBox->isChecked());
    connect(ui->discordCheckBox, &QCheckBox::toggled, ui->comboBoxDiscordPrivacy, &QWidget::setEnabled);
    connect(ui->discordCheckBox, &QCheckBox::toggled, ui->labelDiscordPrivacy, &QWidget::setEnabled);
#else
    ui->groupBoxDiscord->setVisible(false);
#endif
}

PreferencesManager::~PreferencesManager()
{
    delete ui;
}

void
PreferencesManager::save()
{
    strncpy(vmm_path_cfg, QDir::cleanPath(ui->systemDirectory->text()).toUtf8().constData(), sizeof(vmm_path_cfg) - 1);
    vmm_path_cfg[sizeof(vmm_path_cfg) - 1] = '\0';
#ifdef DISCORD
    vmm_discord_presence = ui->discordCheckBox->isChecked() ? (ui->comboBoxDiscordPrivacy->currentIndex() + 1) : 0;
#endif
    vmm_update_check    = ui->updateCheckBox->isChecked() ? 1 : 0;
    vmm_window_remember = ui->rememberSizePositionCheckBox->isChecked() ? 1 : 0;
    vmm_regex_search    = ui->regexSearchCheckBox->isChecked() ? 1 : 0;
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    vmm_delete_to_trash = ui->deleteToTrashCheckBox->isChecked() ? 1 : 0;
#endif
}

// Bad copy pasta from machine add
void
PreferencesManager::chooseDirectoryLocation()
{
    QFileDialog::Options options = QFileDialog::ShowDirsOnly;
#ifdef Q_OS_LINUX
    options |= QFileDialog::DontUseNativeDialog;
#endif
    const auto directory = QFileDialog::getExistingDirectory(this, tr("Choose directory"), ui->systemDirectory->text(), options);
    if (!directory.isEmpty())
        ui->systemDirectory->setText(QDir::toNativeSeparators(directory));
}

void
PreferencesManager::on_pushButtonDefaultSystemDir_released()
{
    char temp[1024];
    plat_get_vmm_dir(temp, sizeof(temp));
    ui->systemDirectory->setText(QDir::toNativeSeparators(QDir(temp).path()));
}
