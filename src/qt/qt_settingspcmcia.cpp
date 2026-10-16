/*
 * 86Box       A hypervisor and IBM PC system emulator.
 *
 *             The PCMCIA settings page: the PC Card controller, and the card
 *             in each of its sockets with that card's settings -- the card's
 *             own (Configure), and for a network card its link, as on the
 *             Network page.
 *
 * Authors:    Marcos Alves
 *
 *             Copyright 2026 Marcos Alves.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/network.h>
#include <86box/pcmcia.h>
}

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "qt_settingspcmcia.hpp"
#include "qt_deviceconfig.hpp"
#include "qt_defs.hpp"

static bool
hasIsaBus(int machineId)
{
    return machine_has_bus(machineId, MACHINE_BUS_ISA | MACHINE_BUS_SIDECAR) > 0;
}

SettingsPcmcia::SettingsPcmcia(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    enable = new QCheckBox(tr("PC Card controller (Cirrus Logic CL-PD6722, ISA, ports 3E0h-3E1h)"));
    enable->setChecked(pcmcia_enabled);
    enable->setToolTip(tr("An Intel 82365SL-compatible PC Card (PCMCIA) controller with two sockets, "
                          "as found in laptops and on PC Card adapter cards.\n"
                          "DOS card and socket services, Windows 95/98 and Linux (i82365) find it."));
    layout->addWidget(enable);

    hint = new QLabel;
    hint->setWordWrap(true);
    layout->addWidget(hint);

    /* One label width for both sockets, so their boxes line up whichever
       rows each shows. */
    const QStringList labels = { tr("Card:"), tr("Network:"), tr("Interface:") };
    int               labelWidth = 0;
    for (const auto &l : labels)
        labelWidth = qMax(labelWidth, QLabel(l).sizeHint().width());
    auto label = [labelWidth](const QString &text) {
        auto *l = new QLabel(text);
        l->setFixedWidth(labelWidth);
        return l;
    };

    for (int s = 0; s < PCMCIA_SOCKETS; s++) {
        Socket &k = sock[s];

        k.group     = new QGroupBox(tr("Socket %1").arg(QChar('A' + s)));
        auto *form  = new QFormLayout(k.group);
        auto *row   = new QHBoxLayout;
        k.card      = new QComboBox;
        k.configure = new QPushButton(tr("Configure"));
        k.card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        row->addWidget(k.card);
        row->addWidget(k.configure);
        form->addRow(label(labels[0]), row);

        for (int c = 0; c < pcmcia_card_count(); c++)
            k.card->addItem((c == 0) ? tr("None") : DeviceConfig::DeviceName(pcmcia_card_get_device(c), pcmcia_card_get_internal_name(c), 0), c);
        k.card->setCurrentIndex(k.card->findData(pcmcia_card_type[s]));

        k.netLabel = label(labels[1]);
        k.netType  = new QComboBox;
        k.netType->addItem(tr("Null Driver"), NET_TYPE_NONE);
        k.netType->addItem("SLiRP", NET_TYPE_SLIRP);
        if (network_ndev > 1)
            k.netType->addItem("PCap", NET_TYPE_PCAP);
        int ti = k.netType->findData(pcmcia_net_type[s]);
        k.netType->setCurrentIndex((ti >= 0) ? ti : k.netType->findData(NET_TYPE_SLIRP));
        form->addRow(k.netLabel, k.netType);

        k.hostLabel = label(labels[2]);
        k.host      = new QComboBox;
        for (int c = 0; c < network_ndev; c++) {
            k.host->addItem(tr(network_devs[c].description), c);
            if (!strcmp(network_devs[c].device, pcmcia_net_host[s]))
                k.host->setCurrentIndex(k.host->count() - 1);
        }
        form->addRow(k.hostLabel, k.host);

        layout->addWidget(k.group);

        connect(k.card, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, s](int) {
            sock[s].cfgChanged = 0;
            updateSocket(s);
        });
        connect(k.netType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, s](int) { updateSocket(s); });
        connect(k.configure, &QPushButton::clicked, this, [this, s]() {
            sock[s].cfgChanged |= DeviceConfig::ConfigureDevice(pcmcia_card_get_device(sock[s].card->currentData().toInt()), s + 1);
        });
    }
    layout->addStretch();

    connect(enable, &QCheckBox::toggled, this, [this](bool) { updateState(); });
    onCurrentMachineChanged(::machine);
}

void
SettingsPcmcia::onCurrentMachineChanged(int machineId)
{
    this->machineId = machineId;
    updateState();
}

/* Greyed out: say why, and what to do about it. */
void
SettingsPcmcia::updateState()
{
    const bool isa = hasIsaBus(machineId);
    QString    why;
    QString    fix;

    enable->setEnabled(isa);
    if (!isa) {
        why = tr("The PC Card controller is an ISA card, and this machine has no ISA slot.");
        fix = tr("Choose a machine with ISA slots on the Machine page to fit one.");
    } else if (!enable->isChecked()) {
        why = tr("The sockets are unavailable while the controller is not fitted.");
        fix = tr("Tick the box above to fit it.");
    }
    hint->setVisible(!why.isEmpty());
    hint->setText(QString("<small>&#9432; %1 %2</small>").arg(why.toHtmlEscaped(), fix.toHtmlEscaped()));

    for (int s = 0; s < PCMCIA_SOCKETS; s++) {
        sock[s].group->setEnabled(isa && enable->isChecked());
        updateSocket(s);
    }
}

void
SettingsPcmcia::updateSocket(int s)
{
    Socket    &k   = sock[s];
    const int  c   = k.card->currentData().toInt();
    const bool net = pcmcia_card_is_network(c);
    const bool cap = net && (k.netType->currentData().toInt() == NET_TYPE_PCAP);

    k.configure->setEnabled(pcmcia_card_has_config(c));
    k.configure->setToolTip((c == 0) ? tr("Configure is unavailable because the socket is empty.\nSelect a card first.")
                                     : (pcmcia_card_has_config(c) ? tr("Configure the %1.").arg(k.card->currentText())
                                                                  : tr("This card has no settings to configure.")));
    k.netLabel->setVisible(net);
    k.netType->setVisible(net);
    k.hostLabel->setVisible(cap);
    k.host->setVisible(cap);
}

int
SettingsPcmcia::hostIndex(int s) const
{
    return sock[s].host->currentData().isValid() ? sock[s].host->currentData().toInt() : -1;
}

/* The card in socket s is the same one but its settings (its own, or a
   network card's link) are not. */
bool
SettingsPcmcia::cardSettingsChanged(int s) const
{
    const int c       = sock[s].card->currentData().toInt();
    bool      changed = sock[s].cfgChanged;

    if (pcmcia_card_is_network(c)) {
        const int t = sock[s].netType->currentData().toInt();
        changed |= (t != pcmcia_net_type[s]);
        if ((t == NET_TYPE_PCAP) && (hostIndex(s) >= 0))
            changed |= strcmp(network_devs[hostIndex(s)].device, pcmcia_net_host[s]) != 0;
    }
    return changed;
}

/* PC Cards are hot-pluggable, so with the controller fitted
   before and after, a card put in, taken out, swapped or reconfigured is a
   soft change: save() hands it to the sockets as the PC Card icon does.
   Fitting or removing the controller (an ISA card) takes a hard reset. */
int
SettingsPcmcia::changed()
{
    const bool on = hasIsaBus(machineId) && enable->isChecked();

    if (on != !!pcmcia_enabled)
        return SETTINGS_CHANGED | SETTINGS_REQUIRE_HARD_RESET;
    if (!on)
        return 0;

    bool changed = false;
    for (int s = 0; s < PCMCIA_SOCKETS; s++)
        changed |= (sock[s].card->currentData().toInt() != pcmcia_card_type[s]) || cardSettingsChanged(s);
    if (!changed)
        return 0;

    /* The sockets are not running (the controller was fitted only in the
       saved settings so far): the next hard reset makes the cards anyway. */
    return pcmcia_slots_active() ? SETTINGS_CHANGED : (SETTINGS_CHANGED | SETTINGS_REQUIRE_HARD_RESET);
}

int
SettingsPcmcia::socketCard(int s) const
{
    return (hasIsaBus(machineId) && enable->isChecked()) ? sock[s].card->currentData().toInt() : 0;
}

void
SettingsPcmcia::save(int soft)
{
    /* A soft change: what each socket gets, worked out before the settings
       below take the new values. */
    int  type[PCMCIA_SOCKETS];
    bool again[PCMCIA_SOCKETS];
    for (int s = 0; s < PCMCIA_SOCKETS; s++) {
        type[s]  = sock[s].card->currentData().toInt();
        again[s] = (type[s] == pcmcia_card_type[s]) && (type[s] > 0) && cardSettingsChanged(s);
    }

    pcmcia_enabled = hasIsaBus(machineId) && enable->isChecked();
    for (int s = 0; s < PCMCIA_SOCKETS; s++) {
        if (soft && (type[s] != pcmcia_card_type[s]))
            pcmcia_request_card(s, type[s]);
        pcmcia_card_type[s] = type[s];
        pcmcia_net_type[s]  = sock[s].netType->currentData().toInt();
        if ((pcmcia_net_type[s] == NET_TYPE_PCAP) && (hostIndex(s) >= 0))
            snprintf(pcmcia_net_host[s], sizeof(pcmcia_net_host[s]), "%s", network_devs[hostIndex(s)].device);
        /* After the link settings, which the card reads when it goes back in. */
        if (soft && again[s])
            pcmcia_request_reinsert(s);
    }
}
