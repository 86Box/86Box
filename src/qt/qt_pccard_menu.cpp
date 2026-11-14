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
#include "qt_newflashdialog.hpp"
#include "qt_util.hpp"

#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QStringBuilder>
#include <QWidget>

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/pcmcia.h>
}

PcCardMenu::PcCardMenu(QWidget *parent)
    : QObject(parent)
    , m_parent(parent)
{
    m_menu = new QMenu(parent);
    connect(m_menu, &QMenu::aboutToShow, this, &PcCardMenu::buildMenu);
}

QString
PcCardMenu::toolTip() const
{
    QString tip = tr("PC Cards");
    for (int s = 0; s < PCMCIA_SOCKETS; s++)
        tip += "\n" + tr("Socket %1: %2").arg(QChar('A' + s), socketCardName(s));
    return tip;
}

QString
PcCardMenu::socketCardName(int socket) const
{
    const int type = pcmcia_card_type[socket];

    if (pcmcia_card_is_cf(type)) {
        char fn[MAX_IMAGE_PATH_LEN];
        int  wp;
        if (!pcmcia_cf_get_image(socket, fn, sizeof(fn), &wp))
            return tr("empty");
        const QString name = QFileInfo(QString::fromUtf8(fn)).fileName();
        return wp ? tr("CompactFlash: %1 (write-protected)").arg(name) : tr("CompactFlash: %1").arg(name);
    }
    return type > 0 ? QString::fromUtf8(pcmcia_card_get_name(type)) : tr("empty");
}

void
PcCardMenu::buildMenu()
{
    m_menu->clear();
    for (int s = 0; s < PCMCIA_SOCKETS; s++) {
        const int     cur = pcmcia_card_type[s];
        QMenu        *sub = m_menu->addMenu(QIcon(":/settings/qt/icons/pcmcia.ico"),
                                           tr("Socket %1: %2").arg(QChar('A' + s), socketCardName(s)));
        auto         *grp = new QActionGroup(sub);

        for (int t = 0; t < pcmcia_card_count(); t++) {
            if (pcmcia_card_is_cf(t))
                continue;
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
        sub->addSeparator();
        addCompactFlashMenu(sub->addMenu(QIcon(":/settings/qt/icons/usb_flash.ico"), tr("CompactFlash")), s);
        sub->setEnabled(pcmcia_slots_active());
    }
}

void
PcCardMenu::addCompactFlashMenu(QMenu *menu, int socket)
{
    menu->addAction(tr("&New CompactFlash card…"), this, [this, socket] {
        NewFlashDialog dialog(m_parent, tr("New CompactFlash card"));
        if (dialog.exec() == QDialog::Accepted)
            insertCompactFlash(socket, dialog.fileName(), false);
    });
    const auto browse = [this, socket](bool wp) {
        const QString fn = QFileDialog::getOpenFileName(
            m_parent, tr("Insert CompactFlash card"), open_dir_usr_path ? QString::fromUtf8(usr_path) : QString(),
            tr("Flash drive images") % util::DlgFilter({ "img", "ima", "bin", "raw" }) %
            tr("All files") % util::DlgFilter({ "*" }, true));
        if (!fn.isEmpty())
            insertCompactFlash(socket, fn, wp);
    };
    menu->addAction(tr("&Existing image…"), this, [browse] { browse(false); });
    menu->addAction(tr("Existing image (&Write-protected)…"), this, [browse] { browse(true); });

    char current[MAX_IMAGE_PATH_LEN] = {};
    const bool mounted = pcmcia_card_is_cf(pcmcia_card_type[socket]) &&
                         pcmcia_cf_get_image(socket, current, sizeof(current), nullptr);
    menu->addSeparator();
    for (int i = 0; i < MAX_PREV_IMAGES; i++) {
        char recent[MAX_IMAGE_PATH_LEN];
        if (!pcmcia_cf_history_get(i, recent, sizeof(recent)))
            continue;
        const QString fn = QString::fromUtf8(recent);
        auto *action = menu->addAction(QFileInfo(fn).fileName(), this, [this, socket, fn] {
            insertCompactFlash(socket, fn, false);
        });
        action->setToolTip(fn);
        action->setCheckable(true);
        action->setChecked(fn == QString::fromUtf8(current));
    }
    menu->addSeparator();
    menu->addAction(tr("&Remove CompactFlash card"), this, [this, socket] {
        pcmcia_request_card(socket, 0);
        config_save();
        emit changed();
    })->setEnabled(mounted);
}

void
PcCardMenu::insertCompactFlash(int socket, const QString &fn, bool wp)
{
    const QFileInfo file(fn);
    const QString canonical = file.canonicalFilePath();

    if (!file.isFile() || !pcmcia_cf_insert_image(socket, canonical.toUtf8().constData(), wp)) {
        QMessageBox::warning(m_parent, tr("CompactFlash"), tr("Could not use %1 as a CompactFlash image.").arg(fn));
        return;
    }
    config_save();
    emit changed();
}
