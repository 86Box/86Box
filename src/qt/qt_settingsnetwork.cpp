/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Network devices configuration UI module.
 *
 *
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *
 *          Copyright 2021 Joakim L. Gilje
 */
#include "qt_settingsnetwork.hpp"
#include "ui_qt_settingsnetwork.h"

extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/thread.h>
#include <86box/network.h>
}

#include "qt_models_common.hpp"
#include "qt_deviceconfig.hpp"

void
SettingsNetwork::enableElements(Ui::SettingsNetwork *ui)
{
    for (int i = 0; i < NET_CARD_MAX; ++i) {
        auto *nic_cbox      = findChild<QComboBox *>(QString("comboBoxNIC%1").arg(i + 1));
        auto *net_type_cbox = findChild<QComboBox *>(QString("comboBoxNet%1").arg(i + 1));
        auto *intf_cbox     = findChild<QComboBox *>(QString("comboBoxIntf%1").arg(i + 1));
        auto *conf_btn      = findChild<QPushButton *>(QString("pushButtonConf%1").arg(i + 1));
        auto *socket_line   = findChild<QLineEdit *>(QString("socketVDENIC%1").arg(i + 1));

        int  netType         = net_type_cbox->currentData().toInt();
        bool adaptersEnabled = netType == NET_TYPE_SLIRP 
                                    || NET_TYPE_VDE  
                                    || (netType == NET_TYPE_PCAP && intf_cbox->currentData().toInt() > 0);

        intf_cbox->setEnabled(net_type_cbox->currentData().toInt() == NET_TYPE_PCAP);
        nic_cbox->setEnabled(adaptersEnabled);
        conf_btn->setEnabled(adaptersEnabled && network_card_has_config(nic_cbox->currentData().toInt()));
        socket_line->setEnabled(net_type_cbox->currentData().toInt() == NET_TYPE_VDE);
    }
}

SettingsNetwork::SettingsNetwork(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsNetwork)
{
    ui->setupUi(this);

    onCurrentMachineChanged(machine);
    enableElements(ui);
    for (int i = 0; i < NET_CARD_MAX; i++) {
        auto *nic_cbox      = findChild<QComboBox *>(QString("comboBoxNIC%1").arg(i + 1));
        auto *net_type_cbox = findChild<QComboBox *>(QString("comboBoxNet%1").arg(i + 1));
        auto *intf_cbox     = findChild<QComboBox *>(QString("comboBoxIntf%1").arg(i + 1));
        auto *socket_line   = findChild<QLineEdit *>(QString("socketVDENIC%1").arg(i + 1));
        connect(nic_cbox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsNetwork::on_comboIndexChanged);
        connect(net_type_cbox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsNetwork::on_comboIndexChanged);
        connect(intf_cbox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsNetwork::on_comboIndexChanged);
    }
}

SettingsNetwork::~SettingsNetwork()
{
    delete ui;
}

void
SettingsNetwork::save()
{
    for (int i = 0; i < NET_CARD_MAX; ++i) {
        auto *cbox                   = findChild<QComboBox *>(QString("comboBoxNIC%1").arg(i + 1));
        auto *socket_line            = findChild<QLineEdit *>(QString("socketVDENIC%1").arg(i + 1));
        net_cards_conf[i].device_num = cbox->currentData().toInt();
        cbox                         = findChild<QComboBox *>(QString("comboBoxNet%1").arg(i + 1));
        net_cards_conf[i].net_type   = cbox->currentData().toInt();
        cbox                         = findChild<QComboBox *>(QString("comboBoxIntf%1").arg(i + 1));
        memset(net_cards_conf[i].host_dev_name, '\0', sizeof(net_cards_conf[i].host_dev_name));
        if (net_cards_conf[i].net_type == NET_TYPE_PCAP) {
            strncpy(net_cards_conf[i].host_dev_name, network_devs[cbox->currentData().toInt()].device, sizeof(net_cards_conf[i].host_dev_name) - 1);
        } else if (net_cards_conf[i].net_type == NET_TYPE_VDE) {
            const char *str_socket = socket_line->text().toStdString().c_str();
            strncpy(net_cards_conf[i].host_dev_name, str_socket, strlen(str_socket));
        }
    }
}

void
SettingsNetwork::onCurrentMachineChanged(int machineId)
{
    this->machineId = machineId;

    int c           = 0;
    int selectedRow = 0;

    for (int i = 0; i < NET_CARD_MAX; ++i) {
        auto *cbox       = findChild<QComboBox *>(QString("comboBoxNIC%1").arg(i + 1));
        auto *model      = cbox->model();
        auto  removeRows = model->rowCount();
        c                = 0;
        selectedRow      = 0;

        while (true) {
            auto name = DeviceConfig::DeviceName(network_card_getdevice(c), network_card_get_internal_name(c), 1);
            if (name.isEmpty()) {
                break;
            }

            if (network_card_available(c) && device_is_valid(network_card_getdevice(c), machineId)) {
                int row = Models::AddEntry(model, name, c);
                if (c == net_cards_conf[i].device_num) {
                    selectedRow = row - removeRows;
                }
            }
            c++;
        }

        model->removeRows(0, removeRows);
        cbox->setEnabled(model->rowCount() > 0);
        cbox->setCurrentIndex(-1);
        cbox->setCurrentIndex(selectedRow);

        cbox       = findChild<QComboBox *>(QString("comboBoxNet%1").arg(i + 1));
        model      = cbox->model();
        removeRows = model->rowCount();
        Models::AddEntry(model, tr("None"), NET_TYPE_NONE);
        Models::AddEntry(model, "SLiRP", NET_TYPE_SLIRP);

        if (network_ndev > 1) {
            Models::AddEntry(model, "PCap", NET_TYPE_PCAP);
        }
        if (network_devmap.has_vde) {
            Models::AddEntry(model, "VDE", NET_TYPE_VDE);
        }
        
        model->removeRows(0, removeRows);
        cbox->setCurrentIndex(net_cards_conf[i].net_type);

        selectedRow = 0;

        if (network_ndev > 0) {
            QString currentPcapDevice = net_cards_conf[i].host_dev_name;
            cbox                      = findChild<QComboBox *>(QString("comboBoxIntf%1").arg(i + 1));
            model                     = cbox->model();
            removeRows                = model->rowCount();
            for (int c = 0; c < network_ndev; c++) {
                Models::AddEntry(model, tr(network_devs[c].description), c);
                if (QString(network_devs[c].device) == currentPcapDevice) {
                    selectedRow = c;
                }
            }
            model->removeRows(0, removeRows);
            cbox->setCurrentIndex(selectedRow);
        }  
        if (net_cards_conf[i].net_type == NET_TYPE_VDE) {
            QString currentVdeSocket = net_cards_conf[i].host_dev_name;
            auto editline = findChild<QLineEdit *>(QString("socketVDENIC%1").arg(i+1));
            editline->setText(currentVdeSocket);
        }
    }
}

void
SettingsNetwork::on_comboIndexChanged(int index)
{
    if (index < 0) {
        return;
    }

    enableElements(ui);
}

void
SettingsNetwork::on_pushButtonConf1_clicked()
{
    DeviceConfig::ConfigureDevice(network_card_getdevice(ui->comboBoxNIC1->currentData().toInt()), 1, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsNetwork::on_pushButtonConf2_clicked()
{
    DeviceConfig::ConfigureDevice(network_card_getdevice(ui->comboBoxNIC2->currentData().toInt()), 2, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsNetwork::on_pushButtonConf3_clicked()
{
    DeviceConfig::ConfigureDevice(network_card_getdevice(ui->comboBoxNIC3->currentData().toInt()), 3, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsNetwork::on_pushButtonConf4_clicked()
{
    DeviceConfig::ConfigureDevice(network_card_getdevice(ui->comboBoxNIC4->currentData().toInt()), 4, qobject_cast<Settings *>(Settings::settings));
}
