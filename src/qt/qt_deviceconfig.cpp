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
#include <QPushButton>
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
#include <86box/random.h>
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
    QStringList serialDevices;
    QStringList ttyEntries;
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
        snprintf(devstr.data(), 1024, R"(\\.\COM%d)", i);
        const auto handle = CreateFileA(devstr.data(),
                                               GENERIC_READ | GENERIC_WRITE, 0,
                                               nullptr, OPEN_EXISTING,
                                               0, nullptr);
        const auto dwError = GetLastError();
        if ((handle != INVALID_HANDLE_VALUE) || (dwError == ERROR_ACCESS_DENIED) ||
            (dwError == ERROR_GEN_FAILURE) || (dwError == ERROR_SHARING_VIOLATION) ||
            (dwError == ERROR_SEM_TIMEOUT)) {
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
DeviceConfig::ProcessConfig(void *dc, const void *c, const bool is_dep)
{
    auto *        device_context = static_cast<device_context_t *>(dc);
    const auto *  config         = static_cast<const _device_config_ *>(c);
    const QString blank          = "";
    int           p;
    int           q;

    if (config == NULL)
        return;

    while (config->type != -1) {
        const int config_type = config->type & CONFIG_TYPE_MASK;

        /* Ignore options of the wrong class. */
        if (!!(config->type & CONFIG_DEP) != is_dep)
            continue;

        /* If this is a BIOS-dependent option and it's BIOS, ignore it. */
        if (!!(config->type & CONFIG_DEP) && (config_type == CONFIG_BIOS))
            continue;

        const int config_major_type = (config_type >> CONFIG_SHIFT) << CONFIG_SHIFT;

        int value = 0;
        auto selected = blank;

        switch (config_major_type) {
            default:
                break;
            case CONFIG_TYPE_INT:
                value = config_get_int(device_context->name, const_cast<char *>(config->name),
                                       config->default_int);
                break;
            case CONFIG_TYPE_HEX16:
                value = config_get_hex16(device_context->name, const_cast<char *>(config->name),
                                         config->default_int);
                break;
            case CONFIG_TYPE_HEX20:
                value = config_get_hex20(device_context->name, const_cast<char *>(config->name),
                                         config->default_int);
                break;
            case CONFIG_TYPE_STRING:
                selected = config_get_string(device_context->name, const_cast<char *>(config->name),
                                             const_cast<char *>(config->default_string));
                break;
        }

        switch (config->type) {
            default:
                break;
            case CONFIG_BINARY:
            {
                auto *cbox  = new QCheckBox();
                cbox->setObjectName(config->name);
                cbox->setChecked(value > 0);
                this->ui->formLayout->addRow(tr(config->description), cbox);
                break;
            }
#ifdef USE_RTMIDI
            case CONFIG_MIDI_OUT:
            {
                auto *cbox = new QComboBox();
                cbox->setObjectName(config->name);
                cbox->setMaxVisibleItems(30);
                auto *model        = cbox->model();
                int   currentIndex = -1;
                for (int i = 0; i < rtmidi_out_get_num_devs(); i++) {
                    char midiName[512] = { 0 };
                    rtmidi_out_get_dev_name(i, midiName);

                    Models::AddEntry(model, midiName, i);
                    if (i == value)
                        currentIndex = i;
                }
                this->ui->formLayout->addRow(tr(config->description), cbox);
                cbox->setCurrentIndex(currentIndex);
                break;
            }
            case CONFIG_MIDI_IN:
            {
                auto *cbox = new QComboBox();
                cbox->setObjectName(config->name);
                cbox->setMaxVisibleItems(30);
                auto *model        = cbox->model();
                int   currentIndex = -1;
                for (int i = 0; i < rtmidi_in_get_num_devs(); i++) {
                    char midiName[512] = { 0 };
                    rtmidi_in_get_dev_name(i, midiName);

                    Models::AddEntry(model, midiName, i);
                    if (i == value)
                        currentIndex = i;
                }
                this->ui->formLayout->addRow(tr(config->description), cbox);
                cbox->setCurrentIndex(currentIndex);
                break;
            }
#endif
            case CONFIG_INT:
            case CONFIG_SELECTION:
            case CONFIG_HEX16:
            case CONFIG_HEX20:
            {
                auto *cbox = new QComboBox();
                cbox->setObjectName(config->name);
                cbox->setMaxVisibleItems(30);
                auto *model        = cbox->model();
                int   currentIndex = -1;

                for (auto *sel = config->selection; (sel != nullptr) && (sel->description != nullptr) &&
                                                    (strlen(sel->description) > 0); ++sel) {
                    int row = Models::AddEntry(model, tr(sel->description), sel->value);

                    if (sel->value == value)
                        currentIndex = row;
                }
                this->ui->formLayout->addRow(tr(config->description), cbox);
                cbox->setCurrentIndex(currentIndex);
                break;
            }
            case CONFIG_BIOS:
            {
                auto *cbox = new QComboBox();
                cbox->setObjectName(config->name);
                cbox->setMaxVisibleItems(30);
                auto *model        = cbox->model();
                int   currentIndex = -1;

                q = 0;
                for (auto *bios = config->bios; (bios != nullptr) && (bios->name != nullptr) &&
                                                (strlen(bios->name) > 0); ++bios) {
                    p = 0;
                    for (int d = 0; d < bios->files_no; d++)
                        p += !!rom_present(const_cast<char *>(bios->files[d]));
                    if (p == bios->files_no) {
                        const int row = Models::AddEntry(model, tr(bios->name), q);
                        if (!strcmp(selected.toUtf8().constData(), bios->internal_name))
                            currentIndex = row;
                    }
                    q++;
                }
                this->ui->formLayout->addRow(tr(config->description), cbox);
                cbox->setCurrentIndex(currentIndex);
                break;
            }
            case CONFIG_SPINNER:
            {
                auto *spinBox = new QSpinBox();
                spinBox->setObjectName(config->name);
                spinBox->setMaximum(config->spinner.max);
                spinBox->setMinimum(config->spinner.min);
                if (config->spinner.step > 0)
                    spinBox->setSingleStep(config->spinner.step);
                spinBox->setValue(value);
                this->ui->formLayout->addRow(tr(config->description), spinBox);
                break;
            }
            case CONFIG_FNAME:
            {
                auto *fileField = new FileField();
                fileField->setObjectName(config->name);
                fileField->setFileName(selected);
                fileField->setFilter(QString(config->file_filter).left(static_cast<int>(strcspn(config->file_filter,
                                                                                                "|"))));
                this->ui->formLayout->addRow(tr(config->description), fileField);
                break;
            }
            case CONFIG_STRING:
            {
                const auto lineEdit = new QLineEdit;
                lineEdit->setObjectName(config->name);
                lineEdit->setCursor(Qt::IBeamCursor);
                lineEdit->setText(selected);
                this->ui->formLayout->addRow(tr(config->description), lineEdit);
                break;
            }
            case CONFIG_SERPORT:
            {
                auto *cbox = new QComboBox();
                cbox->setObjectName(config->name);
                cbox->setMaxVisibleItems(30);
                auto *model         = cbox->model();
                int   currentIndex  = 0;
                auto  serialDevices = EnumerateSerialDevices();

                Models::AddEntry(model, tr("None"), -1);
                for (int i = 0; i < serialDevices.size(); i++) {
                    const int row = Models::AddEntry(model, serialDevices[i], i);
                    if (selected == serialDevices[i])
                        currentIndex = row;
                }

                this->ui->formLayout->addRow(tr(config->description), cbox);
                cbox->setCurrentIndex(currentIndex);
                break;
            }
            case CONFIG_MAC:
            {
                // QHBoxLayout for the line edit widget and the generate button
                const auto hboxLayout     = new QHBoxLayout();
                const auto generateButton = new QPushButton(tr("Generate"));
                const auto lineEdit       = new QLineEdit;
                // Allow the line edit to expand and fill available space
                lineEdit->setSizePolicy(QSizePolicy::MinimumExpanding,QSizePolicy::Preferred);
                lineEdit->setInputMask("HH:HH:HH;0");
                lineEdit->setObjectName(config->name);
                // Display the current or generated MAC in uppercase
                // When stored it will be converted to lowercase
                if (config_get_mac(device_context->name, config->name,
                                   config->default_int) & 0xFF000000) {
                    lineEdit->setText(QString::asprintf("%02X:%02X:%02X", random_generate(),
                                      random_generate(), random_generate()));
                } else {
                    auto current_mac = QString(config_get_string(device_context->name, config->name,
                                               const_cast<char *>(config->default_string)));
                    lineEdit->setText(current_mac.toUpper());
                }
                // Action for the generate button
                connect(generateButton, &QPushButton::clicked, [lineEdit] {
                    lineEdit->setText(QString::asprintf("%02X:%02X:%02X", random_generate(),
                                      random_generate(), random_generate()));
                });
                hboxLayout->addWidget(lineEdit);
                hboxLayout->addWidget(generateButton);
                this->ui->formLayout->addRow(tr(config->description), hboxLayout);
                break;
            }
        }
        ++config;
    }
}

void
DeviceConfig::ConfigureDevice(const _device_ *device, int instance, Settings *settings)
{
    DeviceConfig dc(settings);
    dc.setWindowTitle(tr("%1 Device Configuration").arg(tr(device->name)));

    device_context_t device_context;
    device_set_context(&device_context, device, instance);

    const auto device_label = new QLabel(tr(device->name));
    device_label->setAlignment(Qt::AlignCenter);
    dc.ui->formLayout->addRow(device_label);
    const auto line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    dc.ui->formLayout->addRow(line);
    const _device_config_ *config = device->config;

    dc.ProcessConfig(&device_context, config, false);

    dc.setFixedSize(dc.minimumSizeHint());

    if (dc.exec() == QDialog::Accepted) {
        if (config == NULL)
            return;

        config = device->config;
        while (config->type != -1) {
            switch (config->type) {
                default:
                    break;
                case CONFIG_BINARY:
                    {
                        const auto *cbox = dc.findChild<QCheckBox *>(config->name);
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
                        if (cbox->currentData().toInt() == -1)
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
                case CONFIG_MAC:
                    {
                        const auto *lineEdit = dc.findChild<QLineEdit *>(config->name);
                        // Store the mac address as lowercase
                        auto macText = lineEdit->displayText().toLower();
                        config_set_string(device_context.name, config->name, macText.toUtf8().constData());
                        break;
                    }
            }
            config++;
        }
    }
}

QString
DeviceConfig::DeviceName(const _device_ *device, const char *internalName, const int bus)
{
    if (QStringLiteral("none") == internalName)
        return tr("None");
    else if (QStringLiteral("internal") == internalName)
        return tr("Internal device");
    else if (device == nullptr)
        return "";
    else {
        char temp[512];
        device_get_name(device, bus, temp);
        return tr((const char *) temp);
    }
}
