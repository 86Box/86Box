/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Network devices configuration UI module.
 *
 *
 *
 * Authors:	Joakim L. Gilje <jgilje@jgilje.net>
 *
 *		Copyright 2021 Joakim L. Gilje
 */
#include "qt_settingsnetwork.hpp"
#include "ui_qt_settingsnetwork.h"

extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/network.h>
}

#include "qt_models_common.hpp"
#include "qt_deviceconfig.hpp"

static void enableElements(Ui::SettingsNetwork *ui) {
    int netType = ui->comboBoxNetwork->currentData().toInt();
    ui->comboBoxPcap->setEnabled(netType == NET_TYPE_PCAP);

    bool adaptersEnabled = netType == NET_TYPE_SLIRP ||
                           (netType == NET_TYPE_PCAP && ui->comboBoxPcap->currentData().toInt() > 0);
    ui->comboBoxAdapter->setEnabled(adaptersEnabled);
    ui->pushButtonConfigure->setEnabled(adaptersEnabled && ui->comboBoxAdapter->currentIndex() > 0 && network_card_has_config(ui->comboBoxAdapter->currentData().toInt()));
}

SettingsNetwork::SettingsNetwork(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsNetwork)
{
    ui->setupUi(this);

    auto* model = ui->comboBoxNetwork->model();
    Models::AddEntry(model, tr("None"), NET_TYPE_NONE);
    Models::AddEntry(model, "PCap", NET_TYPE_PCAP);
    Models::AddEntry(model, "SLiRP", NET_TYPE_SLIRP);
    ui->comboBoxNetwork->setCurrentIndex(network_type);

    int selectedRow = 0;
    model = ui->comboBoxPcap->model();
    QString currentPcapDevice = network_host;
    for (int c = 0; c < network_ndev; c++) {

        Models::AddEntry(model, tr(network_devs[c].description), c);
        if (QString(network_devs[c].device) == currentPcapDevice) {
            selectedRow = c;
        }
    }
    ui->comboBoxPcap->setCurrentIndex(-1);
    ui->comboBoxPcap->setCurrentIndex(selectedRow);

    onCurrentMachineChanged(machine);
    enableElements(ui);
}

SettingsNetwork::~SettingsNetwork()
{
    delete ui;
}

void SettingsNetwork::save() {
    network_type = ui->comboBoxNetwork->currentData().toInt();
    memset(network_host, '\0', sizeof(network_host));
    strcpy(network_host, network_devs[ui->comboBoxPcap->currentData().toInt()].device);
    network_card = ui->comboBoxAdapter->currentData().toInt();
}

void SettingsNetwork::onCurrentMachineChanged(int machineId) {
    this->machineId = machineId;

    auto* model = ui->comboBoxAdapter->model();
    auto removeRows = model->rowCount();
    int c = 0;
    int selectedRow = 0;
    while (true) {
        auto name = DeviceConfig::DeviceName(network_card_getdevice(c), network_card_get_internal_name(c), 1);
        if (name.isEmpty()) {
            break;
        }

        if (network_card_available(c) && device_is_valid(network_card_getdevice(c), machineId)) {
            int row = Models::AddEntry(model, name, c);
            if (c == network_card) {
                selectedRow = row - removeRows;
            }
        }

        c++;
    }
    model->removeRows(0, removeRows);
    ui->comboBoxAdapter->setEnabled(model->rowCount() > 0);
    ui->comboBoxAdapter->setCurrentIndex(-1);
    ui->comboBoxAdapter->setCurrentIndex(selectedRow);
}

void SettingsNetwork::on_comboBoxNetwork_currentIndexChanged(int index) {
    if (index < 0) {
        return;
    }

    enableElements(ui);
}

void SettingsNetwork::on_comboBoxAdapter_currentIndexChanged(int index) {
    if (index < 0) {
        return;
    }

    enableElements(ui);
}

void SettingsNetwork::on_pushButtonConfigure_clicked() {
    DeviceConfig::ConfigureDevice(network_card_getdevice(ui->comboBoxAdapter->currentData().toInt()), 0, qobject_cast<Settings*>(Settings::settings));
}


void SettingsNetwork::on_comboBoxPcap_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }

    enableElements(ui);
}
