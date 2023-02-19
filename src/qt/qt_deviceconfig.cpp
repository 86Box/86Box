/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Device configuration UI code.
 *
 *
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2022 Cacodemon345
 */
#include "qt_deviceconfig.hpp"
#include "ui_qt_deviceconfig.h"
#include "qt_settings.hpp"

#include <QDebug>
#include <QComboBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QCheckBox>
#include <QFrame>
#include <QLineEdit>
#include <QLabel>
#include <QDir>
#include <QSettings>

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
#ifdef Q_OS_LINUX
#    include <sys/stat.h>
#    include <sys/sysmacros.h>
#endif
#ifdef Q_OS_WINDOWS
#include <windows.h>
#endif

DeviceConfig::DeviceConfig(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DeviceConfig)
{
    ui->setupUi(this);
}

DeviceConfig::~DeviceConfig()
{
    delete ui;
}

static QStringList
EnumerateSerialDevices()
{
    QStringList serialDevices, ttyEntries;
    QByteArray  devstr(1024, 0);
#ifdef Q_OS_LINUX
    QDir class_dir("/sys/class/tty/");
    QDir dev_dir("/dev/");
    ttyEntries = class_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System, QDir::SortFlag::Name);
    for (int i = 0; i < ttyEntries.size(); i++) {
        if (class_dir.exists(ttyEntries[i] + "/device/driver/") && dev_dir.exists(ttyEntries[i])
            && QFileInfo(dev_dir.canonicalPath() + '/' + ttyEntries[i]).isReadable()
            && QFileInfo(dev_dir.canonicalPath() + '/' + ttyEntries[i]).isWritable()) {
            serialDevices.push_back("/dev/" + ttyEntries[i]);
        }
    }
#endif
#ifdef Q_OS_WINDOWS
    for (int i = 1; i < 256; i++) {
        devstr[0] = 0;
        snprintf(devstr.data(), 1024, "\\\\.\\COM%d", i);
        auto handle = CreateFileA(devstr.data(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, 0);
        auto dwError = GetLastError();
        if (handle != INVALID_HANDLE_VALUE || (handle == INVALID_HANDLE_VALUE && ((dwError == ERROR_ACCESS_DENIED) || (dwError == ERROR_GEN_FAILURE) || (dwError == ERROR_SHARING_VIOLATION) || (dwError == ERROR_SEM_TIMEOUT)))) {
            if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
            serialDevices.push_back(QString(devstr));
        }
    }
#endif
#ifdef Q_OS_MACOS
    QDir dev_dir("/dev/");
    dev_dir.setNameFilters({ "tty.*", "cu.*" });
    QDir::Filters serial_dev_flags = QDir::Files | QDir::NoSymLinks | QDir::Readable | QDir::Writable | QDir::NoDotAndDotDot | QDir::System;
    for (const auto &device : dev_dir.entryInfoList(serial_dev_flags, QDir::SortFlag::Name)) {
        serialDevices.push_back(device.canonicalFilePath());
    }
#endif
    return serialDevices;
}

void
DeviceConfig::ConfigureDevice(const _device_ *device, int instance, Settings *settings)
{
    DeviceConfig dc(settings);
    dc.setWindowTitle(QString("%1 Device Configuration").arg(device->name));
    int c, d, p, q;

    device_context_t device_context;
    device_set_context(&device_context, device, instance);

    auto device_label = new QLabel(device->name);
    dc.ui->formLayout->addRow(device_label);
    auto line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    dc.ui->formLayout->addRow(line);
    const auto *config = device->config;
    while (config->type != -1) {
        switch (config->type) {
            case CONFIG_BINARY:
                {
                    auto  value = config_get_int(device_context.name, const_cast<char *>(config->name), config->default_int);
                    auto *cbox  = new QCheckBox();
                    cbox->setObjectName(config->name);
                    cbox->setChecked(value > 0);
                    dc.ui->formLayout->addRow(config->description, cbox);
                    break;
                }
#ifdef USE_RTMIDI
            case CONFIG_MIDI_OUT:
                {
                    auto *cbox = new QComboBox();
                    cbox->setObjectName(config->name);
                    auto *model        = cbox->model();
                    int   currentIndex = -1;
                    int   selected     = config_get_int(device_context.name, const_cast<char *>(config->name), config->default_int);
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
                    auto *cbox = new QComboBox();
                    cbox->setObjectName(config->name);
                    auto *model        = cbox->model();
                    int   currentIndex = -1;
                    int   selected     = config_get_int(device_context.name, const_cast<char *>(config->name), config->default_int);
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
                    auto *cbox = new QComboBox();
                    cbox->setObjectName(config->name);
                    auto *model        = cbox->model();
                    int   currentIndex = -1;
                    int   selected     = 0;
                    switch (config->type) {
                        case CONFIG_SELECTION:
                            selected = config_get_int(device_context.name, const_cast<char *>(config->name), config->default_int);
                            break;
                        case CONFIG_HEX16:
                            selected = config_get_hex16(device_context.name, const_cast<char *>(config->name), config->default_int);
                            break;
                        case CONFIG_HEX20:
                            selected = config_get_hex20(device_context.name, const_cast<char *>(config->name), config->default_int);
                            break;
                    }

                    for (auto *sel = config->selection; (sel != nullptr) && (sel->description != nullptr) && (strlen(sel->description) > 0); ++sel) {
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
                    auto *cbox = new QComboBox();
                    cbox->setObjectName(config->name);
                    auto *model        = cbox->model();
                    int   currentIndex = -1;
                    char *selected;
                    selected = config_get_string(device_context.name, const_cast<char *>(config->name), const_cast<char *>(config->default_string));

                    c = q = 0;
                    for (auto *bios = config->bios; (bios != nullptr) && (bios->name != nullptr) && (strlen(bios->name) > 0); ++bios) {
                        p = 0;
                        for (d = 0; d < bios->files_no; d++)
                            p += !!rom_present(const_cast<char *>(bios->files[d]));
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
                    int   value   = config_get_int(device_context.name, const_cast<char *>(config->name), config->default_int);
                    auto *spinBox = new QSpinBox();
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
                    auto *fileName  = config_get_string(device_context.name, const_cast<char *>(config->name), const_cast<char *>(config->default_string));
                    auto *fileField = new FileField();
                    fileField->setObjectName(config->name);
                    fileField->setFileName(fileName);
                    fileField->setFilter(QString(config->file_filter).left(strcspn(config->file_filter, "|")));
                    dc.ui->formLayout->addRow(config->description, fileField);
                    break;
                }
            case CONFIG_STRING:
                {
                    auto lineEdit = new QLineEdit;
                    lineEdit->setObjectName(config->name);
                    lineEdit->setCursor(Qt::IBeamCursor);
                    lineEdit->setText(config_get_string(device_context.name, const_cast<char *>(config->name), const_cast<char *>(config->default_string)));
                    dc.ui->formLayout->addRow(config->description, lineEdit);
                    break;
                }
            case CONFIG_SERPORT:
                {
                    auto *cbox = new QComboBox();
                    cbox->setObjectName(config->name);
                    auto *model         = cbox->model();
                    int   currentIndex  = 0;
                    auto  serialDevices = EnumerateSerialDevices();
                    char *selected      = config_get_string(device_context.name, const_cast<char *>(config->name), const_cast<char *>(config->default_string));

                    Models::AddEntry(model, "None", -1);
                    for (int i = 0; i < serialDevices.size(); i++) {
                        int row = Models::AddEntry(model, serialDevices[i], i);
                        if (selected == serialDevices[i]) {
                            currentIndex = row;
                        }
                    }

                    dc.ui->formLayout->addRow(config->description, cbox);
                    cbox->setCurrentIndex(currentIndex);
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
                        auto *cbox = dc.findChild<QCheckBox *>(config->name);
                        config_set_int(device_context.name, const_cast<char *>(config->name), cbox->isChecked() ? 1 : 0);
                        break;
                    }
                case CONFIG_MIDI_OUT:
                case CONFIG_MIDI_IN:
                case CONFIG_SELECTION:
                    {
                        auto *cbox = dc.findChild<QComboBox *>(config->name);
                        config_set_int(device_context.name, const_cast<char *>(config->name), cbox->currentData().toInt());
                        break;
                    }
                case CONFIG_BIOS:
                    {
                        auto *cbox = dc.findChild<QComboBox *>(config->name);
                        int   idx  = cbox->currentData().toInt();
                        config_set_string(device_context.name, const_cast<char *>(config->name), const_cast<char *>(config->bios[idx].internal_name));
                        break;
                    }
                case CONFIG_SERPORT:
                    {
                        auto *cbox = dc.findChild<QComboBox *>(config->name);
                        auto  path = cbox->currentText().toUtf8();
                        if (path == "None")
                            path = "";
                        config_set_string(device_context.name, const_cast<char *>(config->name), path);
                        break;
                    }
                case CONFIG_STRING:
                    {
                        auto *lineEdit = dc.findChild<QLineEdit *>(config->name);
                        config_set_string(device_context.name, const_cast<char *>(config->name), lineEdit->text().toUtf8());
                        break;
                    }
                case CONFIG_HEX16:
                    {
                        auto *cbox = dc.findChild<QComboBox *>(config->name);
                        config_set_hex16(device_context.name, const_cast<char *>(config->name), cbox->currentData().toInt());
                        break;
                    }
                case CONFIG_HEX20:
                    {
                        auto *cbox = dc.findChild<QComboBox *>(config->name);
                        config_set_hex20(device_context.name, const_cast<char *>(config->name), cbox->currentData().toInt());
                        break;
                    }
                case CONFIG_FNAME:
                    {
                        auto *fbox     = dc.findChild<FileField *>(config->name);
                        auto  fileName = fbox->fileName().toUtf8();
                        config_set_string(device_context.name, const_cast<char *>(config->name), fileName.data());
                        break;
                    }
                case CONFIG_SPINNER:
                    {
                        auto *spinBox = dc.findChild<QSpinBox *>(config->name);
                        config_set_int(device_context.name, const_cast<char *>(config->name), spinBox->value());
                        break;
                    }
            }
            config++;
        }
    }
}

QString
DeviceConfig::DeviceName(const _device_ *device, const char *internalName, int bus)
{
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
