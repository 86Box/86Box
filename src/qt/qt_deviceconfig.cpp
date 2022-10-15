/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Device configuration UI code.
 *
 *
 *
 * Authors:	Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *
 *		Copyright 2021 Joakim L. Gilje
 *      Copyright 2022 Cacodemon345
 */
#include "qt_deviceconfig.hpp"
#include "ui_qt_deviceconfig.h"
#include "qt_settings.hpp"

#include <QDebug>
#include <QComboBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QCheckBox>

extern "C" {
#include <86box/86box.h>
#include <86box/ini.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/midi_rtmidi.h>
#include <86box/mem.h>
#include <86box/rom.h>
}

#include "qt_filefield.hpp"
#include "qt_models_common.hpp"

DeviceConfig::DeviceConfig(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DeviceConfig)
{
    ui->setupUi(this);
}

DeviceConfig::~DeviceConfig()
{
    delete ui;
}

void DeviceConfig::ConfigureDevice(const _device_* device, int instance, Settings* settings) {
    DeviceConfig dc(settings);
    dc.setWindowTitle(QString("%1 Device Configuration").arg(device->name));
    int combo_to_struct[256];
    int c, d, p, q;

    device_context_t device_context;
    device_set_context(&device_context, device, instance);

    const auto* config = device->config;
    while (config->type != -1) {
        switch (config->type) {
        case CONFIG_BINARY:
        {
            auto value = config_get_int(device_context.name, const_cast<char*>(config->name), config->default_int);
            auto* cbox = new QCheckBox();
            cbox->setObjectName(config->name);
            cbox->setChecked(value > 0);
            dc.ui->formLayout->addRow(config->description, cbox);
            break;
        }
#ifdef USE_RTMIDI
        case CONFIG_MIDI_OUT:
        {
            auto* cbox = new QComboBox();
            cbox->setObjectName(config->name);
            auto* model = cbox->model();
            int currentIndex = -1;
            int selected = config_get_int(device_context.name, const_cast<char*>(config->name), config->default_int);
            for (int i = 0; i < rtmidi_out_get_num_devs(); i++) {
                char midiName[512] = { 0 };
                rtmidi_out_get_dev_name(i, midiName);

                Models::AddEntry(model, midiName, i);
                if (selected == i) {
                    currentIndex = i;
                }
            }
            dc.ui->formLayout->addRow(config->description, cbox);
            cbox->setCurrentIndex(currentIndex);
            break;
        }
        case CONFIG_MIDI_IN:
        {
            auto* cbox = new QComboBox();
            cbox->setObjectName(config->name);
            auto* model = cbox->model();
            int currentIndex = -1;
            int selected = config_get_int(device_context.name, const_cast<char*>(config->name), config->default_int);
            for (int i = 0; i < rtmidi_in_get_num_devs(); i++) {
                char midiName[512] = { 0 };
                rtmidi_in_get_dev_name(i, midiName);

                Models::AddEntry(model, midiName, i);
                if (selected == i) {
                    currentIndex = i;
                }
            }
            dc.ui->formLayout->addRow(config->description, cbox);
            cbox->setCurrentIndex(currentIndex);
            break;
        }
#endif
        case CONFIG_SELECTION:
        case CONFIG_HEX16:
        case CONFIG_HEX20:
        {
            auto* cbox = new QComboBox();
            cbox->setObjectName(config->name);
            auto* model = cbox->model();
            int currentIndex = -1;
            int selected = 0;
            switch (config->type) {
            case CONFIG_SELECTION:
                selected = config_get_int(device_context.name, const_cast<char*>(config->name), config->default_int);
                break;
            case CONFIG_HEX16:
                selected = config_get_hex16(device_context.name, const_cast<char*>(config->name), config->default_int);
                break;
            case CONFIG_HEX20:
                selected = config_get_hex20(device_context.name, const_cast<char*>(config->name), config->default_int);
                break;
            }

            for (auto* sel = config->selection; (sel != nullptr) && (sel->description != nullptr) && (strlen(sel->description) > 0); ++sel) {
                int row = Models::AddEntry(model, sel->description, sel->value);
                if (selected == sel->value) {
                    currentIndex = row;
                }
            }
            dc.ui->formLayout->addRow(config->description, cbox);
            cbox->setCurrentIndex(currentIndex);
            break;
        }
        case CONFIG_BIOS:
        {
            auto* cbox = new QComboBox();
            cbox->setObjectName(config->name);
            auto* model = cbox->model();
            int currentIndex = -1;
            char *selected;
            selected = config_get_string(device_context.name, const_cast<char*>(config->name), const_cast<char*>(config->default_string));

            c = q = 0;
            for (auto* bios = config->bios; (bios != nullptr) && (bios->name != nullptr) && (strlen(bios->name) > 0); ++bios) {
                p = 0;
                for (d = 0; d < bios->files_no; d++)
                    p += !!rom_present(const_cast<char*>(bios->files[d]));
                if (p == bios->files_no) {
                    int row = Models::AddEntry(model, bios->name, q);
                    if (!strcmp(selected, bios->internal_name)) {
                        currentIndex = row;
                    }
                    c++;
               }
               q++;
            }
            dc.ui->formLayout->addRow(config->description, cbox);
            cbox->setCurrentIndex(currentIndex);
            break;
        }
        case CONFIG_SPINNER:
        {
            int value = config_get_int(device_context.name, const_cast<char*>(config->name), config->default_int);
            auto* spinBox = new QSpinBox();
            spinBox->setObjectName(config->name);
            spinBox->setMaximum(config->spinner.max);
            spinBox->setMinimum(config->spinner.min);
            if (config->spinner.step > 0) {
                spinBox->setSingleStep(config->spinner.step);
            }
            spinBox->setValue(value);
            dc.ui->formLayout->addRow(config->description, spinBox);
            break;
        }
        case CONFIG_FNAME:
        {
            auto* fileName = config_get_string(device_context.name, const_cast<char*>(config->name), const_cast<char*>(config->default_string));
            auto* fileField = new FileField();
            fileField->setObjectName(config->name);
            fileField->setFileName(fileName);
            fileField->setFilter(QString(config->file_filter).left(strcspn(config->file_filter, "|")));
            dc.ui->formLayout->addRow(config->description, fileField);
            break;
        }
        }
        ++config;
    }

    dc.setFixedSize(dc.minimumSizeHint());
    int res = dc.exec();
    if (res == QDialog::Accepted) {
        config = device->config;
        while (config->type != -1) {
            switch (config->type) {
            case CONFIG_BINARY:
            {
                auto* cbox = dc.findChild<QCheckBox*>(config->name);
                config_set_int(device_context.name, const_cast<char*>(config->name), cbox->isChecked() ? 1 : 0);
                break;
            }
            case CONFIG_MIDI_OUT:
            case CONFIG_MIDI_IN:
            case CONFIG_SELECTION:
            {
                auto* cbox = dc.findChild<QComboBox*>(config->name);
                config_set_int(device_context.name, const_cast<char*>(config->name), cbox->currentData().toInt());
                break;
            }
            case CONFIG_BIOS:
            {
                auto* cbox = dc.findChild<QComboBox*>(config->name);
                int idx = cbox->currentData().toInt();
                config_set_string(device_context.name, const_cast<char*>(config->name), const_cast<char*>(config->bios[idx].internal_name));
                break;
            }
            case CONFIG_HEX16:
            {
                auto* cbox = dc.findChild<QComboBox*>(config->name);
                config_set_hex16(device_context.name, const_cast<char*>(config->name), cbox->currentData().toInt());
                break;
            }
            case CONFIG_HEX20:
            {
                auto* cbox = dc.findChild<QComboBox*>(config->name);
                config_set_hex20(device_context.name, const_cast<char*>(config->name), cbox->currentData().toInt());
                break;
            }
            case CONFIG_FNAME:
            {
                auto* fbox = dc.findChild<FileField*>(config->name);
                auto fileName = fbox->fileName().toUtf8();
                config_set_string(device_context.name, const_cast<char*>(config->name), fileName.data());
                break;
            }
            case CONFIG_SPINNER:
            {
                auto* spinBox = dc.findChild<QSpinBox*>(config->name);
                config_set_int(device_context.name, const_cast<char*>(config->name), spinBox->value());
                break;
            }
            }
            config++;
        }
    }
}

QString DeviceConfig::DeviceName(const _device_* device, const char *internalName, int bus) {
    if (QStringLiteral("none") == internalName) {
        return tr("None");
    } else if (QStringLiteral("internal") == internalName) {
        return tr("Internal controller");
    } else if (device == nullptr) {
        return QString();
    } else {
        char temp[512];
        device_get_name(device, bus, temp);
        return tr(temp, nullptr, 512);
    }
}
