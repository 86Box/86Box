/*
 * 86Box       A hypervisor and IBM PC system emulator.
 *
 *             The PC Card menu, behind the PC Card icon in the status bar:
 *             put any card into either socket, or take it out, while the
 *             machine runs.  The guest's socket services see the card come
 *             and go (pcmcia.c, pcic_pd6722.c), and the choice is kept in the
 *             configuration, so the card is there after a restart too.  A
 *             card's own settings are made in Settings > Other peripherals >
 *             PCMCIA.
 *
 * Authors:    Marcos Alves
 *
 *             Copyright 2026 Marcos Alves.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#include "qt_pccard_menu.hpp"

#include <QAction>
#include <QActionGroup>
#include <QIcon>
#include <QMenu>
#include <QWidget>

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/pcmcia.h>
}

PcCardMenu::PcCardMenu(QWidget *parent)
    : QObject(parent)
{
    m_menu = new QMenu(parent);
    connect(m_menu, &QMenu::aboutToShow, this, &PcCardMenu::buildMenu);
}

QString
PcCardMenu::toolTip() const
{
    QString tip = tr("PC Cards");
    for (int s = 0; s < PCMCIA_SOCKETS; s++) {
        const int t = pcmcia_card_type[s];
        tip += "\n" + tr("Socket %1: %2").arg(QChar('A' + s), (t > 0) ? QString::fromUtf8(pcmcia_card_get_name(t)) : tr("empty"));
    }
    return tip;
}

void
PcCardMenu::buildMenu()
{
    m_menu->clear();
    for (int s = 0; s < PCMCIA_SOCKETS; s++) {
        const int     cur = pcmcia_card_type[s];
        const QString in  = (cur > 0) ? QString::fromUtf8(pcmcia_card_get_name(cur)) : tr("empty");
        QMenu        *sub = m_menu->addMenu(QIcon(":/settings/qt/icons/pcmcia.ico"), tr("Socket %1: %2").arg(QChar('A' + s), in));
        auto         *grp = new QActionGroup(sub);

        for (int t = 0; t < pcmcia_card_count(); t++) {
            QAction *a = sub->addAction((t == 0) ? tr("Empty (take the card out)") : QString::fromUtf8(pcmcia_card_get_name(t)));
            a->setCheckable(true);
            a->setChecked(t == cur);
            grp->addAction(a);
            connect(a, &QAction::triggered, this, [this, s, t]() {
                if (t == pcmcia_card_type[s])
                    return;
                pcmcia_request_card(s, t);
                config_save();
                emit changed();
            });
            if (t == 0)
                sub->addSeparator();
        }
        sub->setEnabled(pcmcia_slots_active());
    }
}
