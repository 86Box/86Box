/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Display settings UI module.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *
 *          Copyright 2021 Joakim L. Gilje
 */
#include <QDebug>
#include <QFileDialog>
#include <QStringBuilder>
#include <QLineEdit>

#include <cstdint>

extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/video.h>
#include <86box/vid_ega.h>
#include <86box/vid_8514a_device.h>
#include <86box/vid_xga_device.h>
#include <86box/vid_ps55da2.h>
#include <86box/vid_ddc.h>
}

#include "qt_deviceconfig.hpp"
#include "qt_models_common.hpp"

#include "qt_settings_completer.hpp"
#include "qt_settingsdisplay.hpp"
#include "ui_qt_settingsdisplay.h"
#include "qt_util.hpp"
#include "qt_defs.hpp"

SettingsDisplay::SettingsDisplay(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsDisplay)
{
    ui->setupUi(this);

    sc                              = new SettingsCompleter(ui->comboBoxVideo, nullptr);
    scSecondary                     = new SettingsCompleter(ui->comboBoxVideoSecondary, nullptr);

    for (uint8_t i = 0; i < GFXCARD_MAX; i++)
        gfxcard_cfg_changed[i]         = 0;
    voodoo_cfg_changed             = 0;
    ibm8514_cfg_changed            = 0;
    xga_cfg_changed                = 0;
    ps55da2_cfg_changed            = 0;

    for (uint8_t i = 0; i < GFXCARD_MAX; i++)
        videoCard[i] = gfxcard[i];

    ui->lineEditCustomEDID->setFilter(tr("EDID") % util::DlgFilter({ "bin", "dat", "edid", "txt" }) % tr("All files") % util::DlgFilter({ "*" }, true));

    ui->comboBoxScreenType->addItem(tr("RGB Color"), 0);
    ui->comboBoxScreenType->addItem(tr("RGB Grayscale"), 1);
    ui->comboBoxScreenType->addItem(tr("Amber monitor"), 2);
    ui->comboBoxScreenType->addItem(tr("Green monitor"), 3);
    ui->comboBoxScreenType->addItem(tr("White monitor"), 4);
    ui->comboBoxScreenType->setCurrentIndex(video_grayscale);

    ui->comboBoxConversionType->addItem(tr("BT601 (NTSC/PAL)"), 0);
    ui->comboBoxConversionType->addItem(tr("BT709 (HDTV)"), 1);
    ui->comboBoxConversionType->addItem(tr("Average"), 2);
    ui->comboBoxConversionType->setCurrentIndex(video_graytype);

    ui->checkBoxOverscan->setChecked(enable_overscan);
    ui->checkBoxContrast->setChecked(vid_cga_contrast);

    ui->checkBoxInverted->setChecked(invert_display);

    onCurrentMachineChanged(machine);
}

SettingsDisplay::~SettingsDisplay()
{
    delete scSecondary;
    delete sc;

    delete ui;
}

int
SettingsDisplay::changed()
{
    int has_changed  = 0;
    int soft_changed = 0;

    has_changed  |= (gfxcard[0]                 != ui->comboBoxVideo->currentData().toInt());
    for (uint8_t i = 1; i < GFXCARD_MAX; i++)
        has_changed  |= (gfxcard[1]                 != ui->comboBoxVideoSecondary->currentData().toInt());

    has_changed  |= (voodoo_enabled             != (ui->checkBoxVoodoo->isChecked() ? 1 : 0));
    has_changed  |= (ibm8514_standalone_enabled != (ui->checkBox8514->isChecked() ? 1 : 0));
    has_changed  |= (xga_standalone_enabled     != (ui->checkBoxXga->isChecked() ? 1 : 0));
    has_changed  |= (da2_standalone_enabled     != (ui->checkBoxDa2->isChecked() ? 1 : 0));
    has_changed  |= (monitor_edid               != (ui->radioButtonCustom->isChecked() ? 1 : 0));

    has_changed  |= strcmp(monitor_edid_path, ui->lineEditCustomEDID->fileName().toUtf8().data());

    soft_changed |= (video_grayscale                != ui->comboBoxScreenType->currentData().toInt());
    soft_changed |= (video_graytype                 != ui->comboBoxConversionType->currentData().toInt());

    soft_changed |= (enable_overscan                != (ui->checkBoxOverscan->isChecked() ? 1 : 0));
    soft_changed |= (vid_cga_contrast               != (ui->checkBoxContrast->isChecked() ? 1 : 0));

    soft_changed |= (invert_display                 != (ui->checkBoxInverted->isChecked() ? 1 : 0));

    return has_changed ? (SETTINGS_CHANGED | SETTINGS_REQUIRE_HARD_RESET) :
                         (soft_changed ? SETTINGS_CHANGED : 0);
}

void
SettingsDisplay::save()
{
    // TODO
#if 0
    for (uint8_t i = 0; i < GFXCARD_MAX; ++i) {
        QComboBox *cbox = findChild<QComboBox *>(QString("comboBoxVideo%1").arg(i + 1));
        gfxcard[i]      = cbox->currentData().toInt();
    }
#else
    gfxcard[0] = ui->comboBoxVideo->currentData().toInt();
    for (uint8_t i = 1; i < GFXCARD_MAX; i++)
        gfxcard[i] = ui->comboBoxVideoSecondary->currentData().toInt();
#endif

    voodoo_enabled             = ui->checkBoxVoodoo->isChecked() ? 1 : 0;
    ibm8514_standalone_enabled = ui->checkBox8514->isChecked() ? 1 : 0;
    xga_standalone_enabled     = ui->checkBoxXga->isChecked() ? 1 : 0;
    da2_standalone_enabled     = ui->checkBoxDa2->isChecked() ? 1 : 0;
    monitor_edid               = ui->radioButtonCustom->isChecked() ? 1 : 0;

    strncpy(monitor_edid_path, ui->lineEditCustomEDID->fileName().toUtf8().data(), sizeof(monitor_edid_path) - 1);

    video_grayscale         = ui->comboBoxScreenType->currentData().toInt();
    video_graytype          = ui->comboBoxConversionType->currentData().toInt();

    update_overscan         = 1;

    enable_overscan         = ui->checkBoxOverscan->isChecked() ? 1 : 0;
    vid_cga_contrast        = ui->checkBoxContrast->isChecked() ? 1 : 0;

    invert_display          = ui->checkBoxInverted->isChecked() ? 1 : 0;

    for (int i = 0; i < MONITORS_NUM; i++)
        cgapal_rebuild_monitor(i);
}

void
SettingsDisplay::onCurrentMachineChanged(int machineId)
{
    // win_settings_video_proc, WM_INITDIALOG
    this->machineId   = machineId;
    auto curVideoCard = videoCard[0];

    sc->removeRows();

    auto *model      = ui->comboBoxVideo->model();
    auto  removeRows = model->rowCount();

    int c           = 0;
    int selectedRow = 0;
    while (true) {
        /* Skip "internal" if machine doesn't have it. */
        if ((c == 1) && (machine_has_flags(machineId, MACHINE_VIDEO) == 0)) {
            c++;
            continue;
        }

        const device_t *video_dev = video_card_getdevice(c);
        QString         name      = DeviceConfig::DeviceName(video_dev, video_get_internal_name(c), 1);
        if (name.isEmpty()) {
            break;
        }

        if (video_card_available(c) && device_is_valid(video_dev, machineId)) {
            if (c == 1 && machine_get_vid_device(machineId)) {
                name += QString(" (%1)").arg(DeviceConfig::DeviceName(machine_get_vid_device(machineId), machine_get_vid_device(machineId)->internal_name, 0));
            }
            int row = Models::AddEntry(model, name, c);
            sc->addDevice(video_dev, name);
            if (c == curVideoCard)
                selectedRow = row - removeRows;
        }

        c++;
    }
    model->removeRows(0, removeRows);

    // TODO
    if (machine_has_flags(machineId, MACHINE_VIDEO_ONLY) > 0) {
        ui->comboBoxVideo->setEnabled(false);
        ui->comboBoxVideoSecondary->setEnabled(false);
        ui->pushButtonConfigureVideoSecondary->setEnabled(false);
        selectedRow = 1;
    } else {
        ui->comboBoxVideo->setEnabled(true);
        ui->comboBoxVideoSecondary->setEnabled(true);
        ui->pushButtonConfigureVideoSecondary->setEnabled(true);
    }
    ui->comboBoxVideo->setCurrentIndex(selectedRow);
    // TODO
    for (uint8_t i = 1; i < GFXCARD_MAX; i++)
        if (gfxcard[i] == 0)
            ui->pushButtonConfigureVideoSecondary->setEnabled(false);

    ui->radioButtonDefault->setChecked(monitor_edid == 0);
    ui->radioButtonCustom->setChecked(monitor_edid == 1);
    ui->lineEditCustomEDID->setFileName(monitor_edid_path);
    ui->lineEditCustomEDID->setEnabled(monitor_edid == 1);
}

void
SettingsDisplay::on_pushButtonConfigureVideo_clicked()
{
    int   videoCard = ui->comboBoxVideo->currentData().toInt();
    auto *device    = video_card_getdevice(videoCard);
    if (videoCard == VID_INTERNAL)
        device = machine_get_vid_device(machineId);
    gfxcard_cfg_changed[0] |= DeviceConfig::ConfigureDevice(device);
}

void
SettingsDisplay::on_pushButtonConfigureVoodoo_clicked()
{
    voodoo_cfg_changed |= DeviceConfig::ConfigureDevice(&voodoo_device);
}

void
SettingsDisplay::on_pushButtonConfigure8514_clicked()
{
    if (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0) {
        ibm8514_cfg_changed |= DeviceConfig::ConfigureDevice(&ibm8514_mca_device);
    } else {
        ibm8514_cfg_changed |= DeviceConfig::ConfigureDevice(&gen8514_isa_device);
    }
}

void
SettingsDisplay::on_pushButtonConfigureXga_clicked()
{
    if (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0)
        xga_cfg_changed |= DeviceConfig::ConfigureDevice(&xga_device);
}

void
SettingsDisplay::on_pushButtonConfigureDa2_clicked()
{
    ps55da2_cfg_changed |= DeviceConfig::ConfigureDevice(&ps55da2_device);
}

void
SettingsDisplay::on_comboBoxVideo_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    static QRegularExpression voodooRegex("3dfx|voodoo|banshee|raven", QRegularExpression::CaseInsensitiveOption);
    auto                      curVideoCard_2 = videoCard[1];
    videoCard[0]                             = ui->comboBoxVideo->currentData().toInt();
    if (videoCard[0] == VID_INTERNAL)
        ui->pushButtonConfigureVideo->setEnabled(machine_has_flags(machineId, MACHINE_VIDEO) && device_has_config(machine_get_vid_device(machineId)));
    else
        ui->pushButtonConfigureVideo->setEnabled(video_card_has_config(videoCard[0]) > 0);
    bool machineHasPci = machine_has_bus(machineId, MACHINE_BUS_PCI) > 0;
    ui->pushButtonConfigureVoodoo->setEnabled(machineHasPci && ui->checkBoxVoodoo->isChecked());

    bool machineHasIsa16 = machine_has_bus(machineId, MACHINE_BUS_ISA16) > 0;
    bool machineHasMca   = machine_has_bus(machineId, MACHINE_BUS_MCA) > 0;

    bool videoCardHas8514 = ((videoCard[0] == VID_INTERNAL) ? machine_has_flags(machineId, MACHINE_VIDEO_8514A) : (video_card_get_flags(videoCard[0]) == VIDEO_FLAG_TYPE_8514));
    bool videoCardHasXga  = ((videoCard[0] == VID_INTERNAL) ? 0 : (video_card_get_flags(videoCard[0]) == VIDEO_FLAG_TYPE_XGA));

    bool machineSupports8514 = ((machineHasIsa16 || machineHasMca) && !videoCardHas8514);
    bool machineSupportsXga  = ((machineHasMca && device_available(&xga_device)) && !videoCardHasXga);
    bool machineSupportsDa2  = machineHasMca && device_available(&ps55da2_device);

    ui->checkBox8514->setEnabled(machineSupports8514);
    ui->checkBox8514->setChecked(ibm8514_standalone_enabled && machineSupports8514);

    ui->pushButtonConfigure8514->setEnabled(ui->checkBox8514->isEnabled() && ui->checkBox8514->isChecked());

    ui->checkBoxXga->setEnabled(machineSupportsXga);
    ui->checkBoxXga->setChecked(xga_standalone_enabled && machineSupportsXga);

    ui->checkBoxDa2->setEnabled(machineSupportsDa2);
    ui->checkBoxDa2->setChecked(da2_standalone_enabled && machineSupportsDa2);

    ui->pushButtonConfigureXga->setEnabled(ui->checkBoxXga->isEnabled() && ui->checkBoxXga->isChecked());
    ui->pushButtonConfigureDa2->setEnabled(ui->checkBoxDa2->isEnabled() && ui->checkBoxDa2->isChecked());

    int c = 2;

    scSecondary->removeRows();

    ui->comboBoxVideoSecondary->clear();
    ui->comboBoxVideoSecondary->addItem(QObject::tr("None"), 0);
    sc->addDevice(NULL, "None");

    ui->comboBoxVideoSecondary->setCurrentIndex(0);
    // TODO: Implement support for selecting non-MDA secondary cards properly when MDA cards are the primary ones.
    if (video_card_get_flags(videoCard[0]) == VIDEO_FLAG_TYPE_MDA) {
        ui->comboBoxVideoSecondary->setCurrentIndex(0);
        return;
    }
    while (true) {
        const device_t *video_dev = video_card_getdevice(c);
        QString         name      = DeviceConfig::DeviceName(video_dev, video_get_internal_name(c), 1);
        if (name.isEmpty()) {
            break;
        }

        int primaryFlags   = video_card_get_flags(videoCard[0]);
        int secondaryFlags = video_card_get_flags(c);
        if (video_card_available(c)
            && device_is_valid(video_dev, machineId)
            && !((secondaryFlags == primaryFlags) && (secondaryFlags != VIDEO_FLAG_TYPE_SECONDARY))
            && !(((primaryFlags == VIDEO_FLAG_TYPE_8514) || (primaryFlags == VIDEO_FLAG_TYPE_XGA)) && (secondaryFlags != VIDEO_FLAG_TYPE_MDA) && (secondaryFlags != VIDEO_FLAG_TYPE_SECONDARY))
            && !((primaryFlags != VIDEO_FLAG_TYPE_MDA) && (primaryFlags != VIDEO_FLAG_TYPE_SECONDARY) && ((secondaryFlags == VIDEO_FLAG_TYPE_8514) || (secondaryFlags == VIDEO_FLAG_TYPE_XGA)))) {
            ui->comboBoxVideoSecondary->addItem(name, c);
            scSecondary->addDevice(video_dev, name);
            if (c == curVideoCard_2)
                ui->comboBoxVideoSecondary->setCurrentIndex(ui->comboBoxVideoSecondary->count() - 1);
        }

        c++;
    }

    if ((videoCard[1] == 0) || (machine_has_flags(machineId, MACHINE_VIDEO_ONLY) > 0)) {
        ui->comboBoxVideoSecondary->setCurrentIndex(0);
        ui->pushButtonConfigureVideoSecondary->setEnabled(false);
    }

    // Is the currently selected video card a voodoo?
    if (ui->comboBoxVideo->currentText().contains(voodooRegex)) {
        // Get the name of the video card currently in use
        const device_t *video_dev        = video_card_getdevice(gfxcard[0]);
        const QString   currentVideoName = DeviceConfig::DeviceName(video_dev, video_get_internal_name(gfxcard[0]), 1);
        // Is it a voodoo?
        const bool currentCardIsVoodoo = currentVideoName.contains(voodooRegex);
        // Don't uncheck if
        // * Current card is voodoo
        // * Add-on voodoo was manually overridden in config
        if (ui->checkBoxVoodoo->isChecked() && !currentCardIsVoodoo) {
            // Otherwise, uncheck the add-on voodoo when a main voodoo is selected
            ui->checkBoxVoodoo->setCheckState(Qt::Unchecked);
        }
        ui->checkBoxVoodoo->setDisabled(true);
    } else {
        ui->checkBoxVoodoo->setEnabled(machineHasPci);
        if (machineHasPci) {
            ui->checkBoxVoodoo->setChecked(voodoo_enabled);
        }
    }
}

void
SettingsDisplay::on_checkBoxVoodoo_stateChanged(int state)
{
    ui->pushButtonConfigureVoodoo->setEnabled(state == Qt::Checked);
}

void
SettingsDisplay::on_checkBox8514_stateChanged(int state)
{
    ui->pushButtonConfigure8514->setEnabled(state == Qt::Checked);
}

void
SettingsDisplay::on_checkBoxXga_stateChanged(int state)
{
    ui->pushButtonConfigureXga->setEnabled(state == Qt::Checked);
}

void
SettingsDisplay::on_checkBoxDa2_stateChanged(int state)
{
    ui->pushButtonConfigureDa2->setEnabled(state == Qt::Checked);
}

void
SettingsDisplay::on_comboBoxVideoSecondary_currentIndexChanged(int index)
{
    if (index < 0) {
        ui->pushButtonConfigureVideoSecondary->setEnabled(false);
        return;
    }
    videoCard[1] = ui->comboBoxVideoSecondary->currentData().toInt();
    ui->pushButtonConfigureVideoSecondary->setEnabled(index != 0 && video_card_has_config(videoCard[1]) > 0);
}

void
SettingsDisplay::on_pushButtonConfigureVideoSecondary_clicked()
{
    auto *device = video_card_getdevice(ui->comboBoxVideoSecondary->currentData().toInt());
    gfxcard_cfg_changed[1] |= DeviceConfig::ConfigureDevice(device);
}

void
SettingsDisplay::on_radioButtonDefault_clicked()
{
    ui->radioButtonDefault->setChecked(true);
    ui->radioButtonCustom->setChecked(false);
    ui->lineEditCustomEDID->setEnabled(false);
}

void
SettingsDisplay::on_radioButtonCustom_clicked()
{
    ui->radioButtonDefault->setChecked(false);
    ui->radioButtonCustom->setChecked(true);
    ui->lineEditCustomEDID->setEnabled(true);
}

void
SettingsDisplay::on_pushButtonExportDefault_clicked()
{
    auto str = QFileDialog::getSaveFileName(this, tr("Export EDID"));
    if (!str.isEmpty()) {
        QFile file(str);
        if (file.open(QFile::WriteOnly)) {
            uint8_t *bytes = nullptr;
            auto     size  = ddc_create_default_edid(&bytes);
            file.write((char *) bytes, size);
            file.close();
        }
    }
}
