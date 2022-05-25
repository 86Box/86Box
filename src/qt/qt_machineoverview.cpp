#include "qt_machineoverview.hpp"
#include "ui_qt_machineoverview.h"

#include "qt_mainwindow.hpp"

#include <QSet>

extern "C"
{
#include "../cpu/cpu.h"

#include <86box/86box.h>
#include <86box/machine.h>
#include <86box/mca.h>
#include <86box/device.h>
#include <86box/config.h>
#include <86box/midi_rtmidi.h>

extern void* mca_priv[8];
}

extern MainWindow* main_window;
MachineOverview::MachineOverview(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MachineOverview)
{
    ui->setupUi(this);

    connect(main_window, &MainWindow::updateStatusBarPanes, this, &MachineOverview::refresh);
    refresh();
}

void MachineOverview::refresh()
{
    this->ui->treeWidget->clear();
    auto machineItem = new QTreeWidgetItem({QObject::tr("Machine")});
    ui->treeWidget->addTopLevelItem(machineItem);
    machineItem->setExpanded(true);
    machineItem->addChildren
    (
        {
            new QTreeWidgetItem({QObject::tr("Machine type:") + ' ' + QString(machines[machine].name)}),
            new QTreeWidgetItem({QObject::tr("CPU type:") + ' ' + QString(cpu_f->name)}),
            new QTreeWidgetItem({QObject::tr("Speed:") + ' ' + QString(cpu_f->cpus[cpu].name)}),
            new QTreeWidgetItem({(QObject::tr("Wait states:").append(' ')) + (cpu_waitstates == 0 ? "Default" : QString::asprintf(tr("%i Wait state(s)").toUtf8().constData(), cpu_waitstates - 1))})
        }
    );
    int i = 0;
    for (const char* fpuName = fpu_get_name_from_index(cpu_f, cpu, i); fpuName != nullptr; fpuName = fpu_get_name_from_index(cpu_f, cpu, ++i))
    {
        auto fpuType = fpu_get_type_from_index(cpu_f, cpu, i);
        if (fpuType == fpu_type) {
            machineItem->addChild(new QTreeWidgetItem({QObject::tr("FPU:") + ' ' + QString(fpuName)}));
        }
    }

    if (machine_get_ram_granularity(machine) < 1024)
    {
        machineItem->addChild(new QTreeWidgetItem({QObject::tr("Memory:") + QString::number(mem_size).prepend(' ') + QCoreApplication::translate("", "KB").prepend(' ')}));
    }
    else
    {
        machineItem->addChild(new QTreeWidgetItem({QObject::tr("Memory:") + QString::number(mem_size / 1024).prepend(' ') + QCoreApplication::translate("", "MB").prepend(' ')}));
    }
    auto devicesItem = new QTreeWidgetItem(machineItem, {QObject::tr("Devices")});
    uint32_t index = 0;

    while (index < 256)
    {
        auto context = device_get_context(index);
        if (context == nullptr) {
            index++;
            continue;
        }

        if (strstr(context->name, "None") || strstr(context->name, "Internal")) {
            index++;
            continue;
        }

        QSet<void*> mcaDevIdSet;
        auto deviceItem = new QTreeWidgetItem(devicesItem, {QString(context->name)});
        if (context->dev->flags & DEVICE_MCA)
        {
            auto priv = device_get_priv(context->dev);
            if (priv)
            {
                for (i = 0; i < mca_get_nr_cards(); i++)
                {
                    if (mca_priv[i] == priv)
                    {
                        uint32_t deviceId = (mca_read_index(0x00, i) | (mca_read_index(0x01, i) << 8));
                        if (deviceId != 0xFFFF && !mcaDevIdSet.contains(priv))
                        {
                            mcaDevIdSet.insert(priv);
                            QString hexRepresentation = QString::number(deviceId, 16).toUpper();
                            deviceItem->addChild(new QTreeWidgetItem({QString("MCA Slot: ") + QString::number(i)}));
                            deviceItem->addChild(new QTreeWidgetItem({QString("MCA Device ID: 0x") + hexRepresentation}));
                            break;
                        }
                    }
                }
            }
        }
        if (device_has_config(context->dev))
        {
            const auto* config = context->dev->config;

            while (config->type != -1)
            {
                switch (config->type)
                {
                case CONFIG_BINARY:
                {
                    auto treeItem = new QTreeWidgetItem(deviceItem, {QString("%1").arg(config->description)});
                    treeItem->setCheckState(0, config_get_int(context->name, const_cast<char*>(config->name), config->default_int) ? Qt::Checked : Qt::Unchecked);
                    treeItem->setFlags(treeItem->flags() & ~Qt::ItemIsUserCheckable);
                    break;
                }
                case CONFIG_SELECTION:
                case CONFIG_HEX16:
                case CONFIG_HEX20:
                {
                    int selected = 0;
                    switch (config->type)
                    {
                    case CONFIG_SELECTION:
                        selected = config_get_int(context->name, const_cast<char*>(config->name), config->default_int);
                        break;
                    case CONFIG_HEX16:
                        selected = config_get_hex16(context->name, const_cast<char*>(config->name), config->default_int);
                        break;
                    case CONFIG_HEX20:
                        selected = config_get_hex20(context->name, const_cast<char*>(config->name), config->default_int);
                        break;
                    }
                    QString selDesc;
                    for (auto* sel = config->selection; (sel != nullptr) && (sel->description != nullptr) && (strlen(sel->description) > 0); ++sel)
                    {
                        if (selected == sel->value)
                        {
                            selDesc = sel->description;
                            break;
                        }
                    }
                    if (selDesc.isEmpty() == false)
                    {
                        auto treeItem = new QTreeWidgetItem(deviceItem, {QString("%1: %2").arg(config->description, selDesc)});
                    }
                    break;
                }
#ifdef USE_RTMIDI
                case CONFIG_MIDI_OUT:
                {
                    char midi_name[512] = { 0 };

                    int selected = config_get_int(context->name, const_cast<char*>(config->name), config->default_int);
                    if (selected >= rtmidi_out_get_num_devs()) break;
                    rtmidi_out_get_dev_name(selected, midi_name);
                    QString selDesc = midi_name;
                    if (selDesc.isEmpty() == false)
                    {
                        auto treeItem = new QTreeWidgetItem(deviceItem, {QString("%1: %2").arg(config->description, selDesc)});
                    }
                    break;
                }
                case CONFIG_MIDI_IN:
                {
                    char midi_name[512] = { 0 };

                    int selected = config_get_int(context->name, const_cast<char*>(config->name), config->default_int);
                    if (selected >= rtmidi_in_get_num_devs()) break;
                    rtmidi_in_get_dev_name(selected, midi_name);
                    QString selDesc = midi_name;
                    if (selDesc.isEmpty() == false)
                    {
                        auto treeItem = new QTreeWidgetItem(deviceItem, {QString("%1: %2").arg(config->description, selDesc)});
                    }
                    break;
                }
#endif
                case CONFIG_SPINNER:
                {
                    auto treeItem = new QTreeWidgetItem(deviceItem, {QString("%1: %2").arg(config->description, QString::number(config_get_int(context->name, const_cast<char*>(config->name), config->default_int)))});
                    break;
                }
                case CONFIG_FNAME:
                {
                    QString filename = config_get_string(context->name, const_cast<char*>(config->name), const_cast<char*>(config->default_string));
                    if (filename.isEmpty() == false)
                    auto treeItem = new QTreeWidgetItem(deviceItem, {QString("%1: %2").arg(config->description, filename)});
                    break;
                }
                }
                //deviceItem->addChildren(new QTreeWidgetItem({QString("1%: 2%").arg(config->description, )}));
                config++;
            }
        }
        index++;
    }
}

MachineOverview::~MachineOverview()
{
    disconnect(main_window, &MainWindow::updateStatusBarPanes, this, &MachineOverview::refresh);
    delete ui;
}
