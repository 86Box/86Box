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

        auto *intf_label = findChild<QLabel *>(QString("labelIntf%1").arg(i + 1));
        auto *intf_cbox  = findChild<QComboBox *>(QString("comboBoxIntf%1").arg(i + 1));

        auto *conf_btn = findChild<QPushButton *>(QString("pushButtonConf%1").arg(i + 1));
        // auto *net_type_conf_btn      = findChild<QPushButton *>(QString("pushButtonNetTypeConf%1").arg(i + 1));

        auto *vde_socket_label = findChild<QLabel *>(QString("labelSocketVDENIC%1").arg(i + 1));
        auto *socket_line      = findChild<QLineEdit *>(QString("socketVDENIC%1").arg(i + 1));

        auto *bridge_label = findChild<QLabel *>(QString("labelBridgeTAPNIC%1").arg(i + 1));
        auto *bridge_line  = findChild<QLineEdit *>(QString("bridgeTAPNIC%1").arg(i + 1));

        auto *option_list_label = findChild<QLabel *>(QString("labelOptionList%1").arg(i + 1));
        auto *option_list_line  = findChild<QWidget *>(QString("lineOptionList%1").arg(i + 1));

        // Shared secret
        auto *secret_label = findChild<QLabel *>(QString("labelSecret%1").arg(i + 1));
        auto *secret_value = findChild<QLineEdit *>(QString("secretSwitch%1").arg(i + 1));

        // Promiscuous option
        auto *promisc_label = findChild<QLabel *>(QString("labelPromisc%1").arg(i + 1));
        auto *promisc_value = findChild<QCheckBox *>(QString("boxPromisc%1").arg(i + 1));

        // Remote switch hostname
        auto *hostname_label = findChild<QLabel *>(QString("labelHostname%1").arg(i + 1));
        auto *hostname_value = findChild<QLineEdit *>(QString("hostnameSwitch%1").arg(i + 1));

        bridge_line->setEnabled(net_type_cbox->currentData().toInt() == NET_TYPE_TAP);
        intf_cbox->setEnabled(net_type_cbox->currentData().toInt() == NET_TYPE_PCAP);
        conf_btn->setEnabled(network_card_has_config(nic_cbox->currentData().toInt()));
        // net_type_conf_btn->setEnabled(network_type_has_config(netType));

        // NEW STUFF
        // Make all options invisible by default

        secret_label->setVisible(false);
        secret_value->setVisible(false);

        // Promiscuous options
        promisc_label->setVisible(false);
        promisc_value->setVisible(false);

        // Hostname
        hostname_label->setVisible(false);
        hostname_value->setVisible(false);

        // Option list label and line
        option_list_label->setVisible(false);
        option_list_line->setVisible(false);

        // VDE
        vde_socket_label->setVisible(false);
        socket_line->setVisible(false);

        // TAP
        bridge_label->setVisible(false);
        bridge_line->setVisible(false);

        // PCAP
        intf_cbox->setVisible(false);
        intf_label->setVisible(false);

        // Don't enable anything unless there's a nic selected
        if (nic_cbox->currentData().toInt() != 0) {
            // Then only enable as needed based on network type
            switch (net_type_cbox->currentData().toInt()) {
#ifdef HAS_VDE
                case NET_TYPE_VDE:
                    // option_list_label->setText("VDE Options");
                    option_list_label->setVisible(true);
                    option_list_line->setVisible(true);

                    vde_socket_label->setVisible(true);
                    socket_line->setVisible(true);
                    break;
#endif

                case NET_TYPE_PCAP:
                    // option_list_label->setText("PCAP Options");
                    option_list_label->setVisible(true);
                    option_list_line->setVisible(true);

                    intf_cbox->setVisible(true);
                    intf_label->setVisible(true);
                    break;

#if defined(__unix__) || defined(__APPLE__)
                case NET_TYPE_TAP:
                    // option_list_label->setText("TAP Options");
                    option_list_label->setVisible(true);
                    option_list_line->setVisible(true);

                    bridge_label->setVisible(true);
                    bridge_line->setVisible(true);
                    break;
#endif

                case NET_TYPE_NLSWITCH:
                    // option_list_label->setText("Local Switch Options");
                    option_list_label->setVisible(true);
                    option_list_line->setVisible(true);

                    // Shared secret
                    secret_label->setVisible(true);
                    secret_value->setVisible(true);

                    // Promiscuous options
                    promisc_label->setVisible(true);
                    promisc_value->setVisible(true);
                    break;

                case NET_TYPE_NRSWITCH:
                    // option_list_label->setText("Remote Switch Options");
                    option_list_label->setVisible(true);
                    option_list_line->setVisible(true);

                    // Shared secret
                    secret_label->setVisible(true);
                    secret_value->setVisible(true);

                    // Hostname
                    hostname_label->setVisible(true);
                    hostname_value->setVisible(true);
                    break;

                case NET_TYPE_SLIRP:
                default:
                    break;
            }
        }
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
        auto *cbox = findChild<QComboBox *>(QString("comboBoxNIC%1").arg(i + 1));
#ifdef HAS_VDE
        auto *socket_line = findChild<QLineEdit *>(QString("socketVDENIC%1").arg(i + 1));
#endif
#if defined(__unix__) || defined(__APPLE__)
        auto *bridge_line = findChild<QLineEdit *>(QString("bridgeTAPNIC%1").arg(i + 1));
#endif
        net_cards_conf[i].device_num = cbox->currentData().toInt();
        cbox                         = findChild<QComboBox *>(QString("comboBoxNet%1").arg(i + 1));
        net_cards_conf[i].net_type   = cbox->currentData().toInt();
        cbox                         = findChild<QComboBox *>(QString("comboBoxIntf%1").arg(i + 1));
        auto *hostname_value         = findChild<QLineEdit *>(QString("hostnameSwitch%1").arg(i + 1));
        auto *promisc_value          = findChild<QCheckBox *>(QString("boxPromisc%1").arg(i + 1));
        auto *secret_value           = findChild<QLineEdit *>(QString("secretSwitch%1").arg(i + 1));
        memset(net_cards_conf[i].host_dev_name, '\0', sizeof(net_cards_conf[i].host_dev_name));
        if (net_cards_conf[i].net_type == NET_TYPE_PCAP)
            strncpy(net_cards_conf[i].host_dev_name, network_devs[cbox->currentData().toInt()].device, sizeof(net_cards_conf[i].host_dev_name) - 1);
#ifdef HAS_VDE
        else if (net_cards_conf[i].net_type == NET_TYPE_VDE)
            strncpy(net_cards_conf[i].host_dev_name, socket_line->text().toUtf8().constData(), sizeof(net_cards_conf[i].host_dev_name));
#endif
#if defined(__unix__) || defined(__APPLE__)
        else if (net_cards_conf[i].net_type == NET_TYPE_TAP)
            strncpy(net_cards_conf[i].host_dev_name, bridge_line->text().toUtf8().constData(), sizeof(net_cards_conf[i].host_dev_name));
#endif
        else if (net_cards_conf[i].net_type == NET_TYPE_NRSWITCH) {
            memset(net_cards_conf[i].nrs_hostname, '\0', sizeof(net_cards_conf[i].nrs_hostname));
            strncpy(net_cards_conf[i].nrs_hostname, hostname_value->text().toUtf8().constData(), sizeof(net_cards_conf[i].nrs_hostname) - 1);
            memset(net_cards_conf[i].secret, '\0', sizeof(net_cards_conf[i].secret));
            strncpy(net_cards_conf[i].secret, secret_value->text().toUtf8().constData(), sizeof(net_cards_conf[i].secret) - 1);
        } else if (net_cards_conf[i].net_type == NET_TYPE_NLSWITCH) {
            net_cards_conf[i].promisc_mode = promisc_value->isChecked();
            memset(net_cards_conf[i].secret, '\0', sizeof(net_cards_conf[i].secret));
            strncpy(net_cards_conf[i].secret, secret_value->text().toUtf8().constData(), sizeof(net_cards_conf[i].secret) - 1);
        }
    }
}

void
SettingsNetwork::onCurrentMachineChanged(int machineId)
{
    this->machineId = machineId;

    int c           = 0;
    int selectedRow = 0;

    // Network Card
    QComboBox          *cbox_[NET_CARD_MAX]        = { 0 };
    QAbstractItemModel *models[NET_CARD_MAX]       = { 0 };
    int                 removeRows_[NET_CARD_MAX]  = { 0 };
    int                 selectedRows[NET_CARD_MAX] = { 0 };
    int                 m_has_net                  = machine_has_flags(machineId, MACHINE_NIC);

    for (uint8_t i = 0; i < NET_CARD_MAX; ++i) {
        cbox_[i]       = findChild<QComboBox *>(QString("comboBoxNIC%1").arg(i + 1));
        models[i]      = cbox_[i]->model();
        removeRows_[i] = models[i]->rowCount();
    }

    c = 0;
    while (true) {
        const QString name = DeviceConfig::DeviceName(network_card_getdevice(c),
                                                      network_card_get_internal_name(c), 1);

        if (name.isEmpty())
            break;

        if (network_card_available(c)) {
            if (device_is_valid(network_card_getdevice(c), machineId)) {
                for (uint8_t i = 0; i < NET_CARD_MAX; ++i) {
                    if ((c != 1) || ((i == 0) && m_has_net)) {
                        int row = Models::AddEntry(models[i], name, c);

                        if (c == net_cards_conf[i].device_num)
                            selectedRows[i] = row - removeRows_[i];
                    }
                }
            }
        }

        c++;
    }

    for (uint8_t i = 0; i < NET_CARD_MAX; ++i) {
        models[i]->removeRows(0, removeRows_[i]);
        cbox_[i]->setEnabled(models[i]->rowCount() > 1);
        cbox_[i]->setCurrentIndex(-1);
        cbox_[i]->setCurrentIndex(selectedRows[i]);

        auto cbox       = findChild<QComboBox *>(QString("comboBoxNet%1").arg(i + 1));
        auto model      = cbox->model();
        auto removeRows = model->rowCount();
        Models::AddEntry(model, tr("Null Driver"), NET_TYPE_NONE);
        Models::AddEntry(model, "SLiRP", NET_TYPE_SLIRP);

        if (network_ndev > 1)
            Models::AddEntry(model, "PCap", NET_TYPE_PCAP);

#ifdef HAS_VDE
        if (network_devmap.has_vde)
            Models::AddEntry(model, "VDE", NET_TYPE_VDE);
#endif

#if defined(__unix__) || defined(__APPLE__)
        Models::AddEntry(model, "TAP", NET_TYPE_TAP);
#endif

        Models::AddEntry(model, "Local Switch", NET_TYPE_NLSWITCH);
#ifdef ENABLE_NET_NRSWITCH
        Models::AddEntry(model, "Remote Switch", NET_TYPE_NRSWITCH);
#endif /* ENABLE_NET_NRSWITCH */

        model->removeRows(0, removeRows);
        cbox->setCurrentIndex(cbox->findData(net_cards_conf[i].net_type));

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
#ifdef HAS_VDE
            QString currentVdeSocket = net_cards_conf[i].host_dev_name;
            auto    editline         = findChild<QLineEdit *>(QString("socketVDENIC%1").arg(i + 1));
            editline->setText(currentVdeSocket);
#else
            ;
#endif
#if defined(__unix__) || defined(__APPLE__)
        } else if (net_cards_conf[i].net_type == NET_TYPE_TAP) {
            QString currentTapDevice = net_cards_conf[i].host_dev_name;
            auto    editline         = findChild<QLineEdit *>(QString("bridgeTAPNIC%1").arg(i + 1));
            editline->setText(currentTapDevice);
#endif
        } else if (net_cards_conf[i].net_type == NET_TYPE_NLSWITCH) {
            auto *promisc_value = findChild<QCheckBox *>(QString("boxPromisc%1").arg(i + 1));
            promisc_value->setCheckState(net_cards_conf[i].promisc_mode == 1 ? Qt::CheckState::Checked : Qt::CheckState::Unchecked);
            auto *secret_value = findChild<QLineEdit *>(QString("secretSwitch%1").arg(i + 1));
            secret_value->setText(net_cards_conf[i].secret);
        } else if (net_cards_conf[i].net_type == NET_TYPE_NRSWITCH) {
            auto *hostname_value = findChild<QLineEdit *>(QString("hostnameSwitch%1").arg(i + 1));
            hostname_value->setText(net_cards_conf[i].nrs_hostname);
            auto *secret_value = findChild<QLineEdit *>(QString("secretSwitch%1").arg(i + 1));
            secret_value->setText(net_cards_conf[i].secret);
        }
    }
}

void
SettingsNetwork::on_comboIndexChanged(int index)
{
    if (index < 0)
        return;

    enableElements(ui);
}

void
SettingsNetwork::on_pushButtonConf1_clicked()
{
    int   netCard = ui->comboBoxNIC1->currentData().toInt();
    auto *device  = network_card_getdevice(netCard);
    if (netCard == NET_INTERNAL)
        device = machine_get_net_device(machineId);
    DeviceConfig::ConfigureDevice(device, 1);
}

void
SettingsNetwork::on_pushButtonConf2_clicked()
{
    int   netCard = ui->comboBoxNIC2->currentData().toInt();
    auto *device  = network_card_getdevice(netCard);
    DeviceConfig::ConfigureDevice(device, 2);
}

void
SettingsNetwork::on_pushButtonConf3_clicked()
{
    int   netCard = ui->comboBoxNIC3->currentData().toInt();
    auto *device  = network_card_getdevice(netCard);
    DeviceConfig::ConfigureDevice(device, 3);
}

void
SettingsNetwork::on_pushButtonConf4_clicked()
{
    int   netCard = ui->comboBoxNIC4->currentData().toInt();
    auto *device  = network_card_getdevice(netCard);
    DeviceConfig::ConfigureDevice(device, 4);
}
