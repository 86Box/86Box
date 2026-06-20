/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          About dialog module.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *          Teemu Korhonen
 *          dob205
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2021-2022 Cacodemon345
 *          Copyright 2021-2022 Teemu Korhonen
 *          Copyright 2022 dob205
 */
#include "qt_about.hpp"
#include "qt_defs.hpp"

extern "C" {
#include <86box/86box.h>
#include <86box/version.h>
}

#include <QMessageBox>
#include <QIcon>
#include <QPushButton>
#include <QUrl>
#include <QDesktopServices>

About::About(QWidget *parent)
{
    setTextFormat(Qt::RichText);
    QString versioninfo;
#ifdef EMU_GIT_HASH
    versioninfo = QString(" [%1]").arg(EMU_GIT_HASH);
#endif
#ifdef USE_DYNAREC
#    ifdef USE_NEW_DYNAREC
#        define DYNAREC_STR "new dynarec"
#    else
#        define DYNAREC_STR "old dynarec"
#    endif
#else
#    define DYNAREC_STR "no dynarec"
#endif
    versioninfo.append(QString(" [%1, %2]").arg(QSysInfo::buildCpuArchitecture(), tr(DYNAREC_STR)));
    setText(QString("<b>%1 v%2%3</b>").arg(EMU_NAME, EMU_VERSION_FULL, versioninfo));
    setInformativeText(tr("An emulator of old computers\n\nAuthors: Miran Grča (OBattler), RichardG867, Jasmine Iwanek, TC1995, coldbrewed, Teemu Korhonen (Manaatti), Joakim L. Gilje, Adrien Moulin (elyosh), Daniel Balsom (gloriouscow), Cacodemon345, Fred N. van Kempen (waltje), Tiseno100, reenigne, and others.\n\nWith previous core contributions from Sarah Walker, leilei, JohnElliott, greatpsycho, and others.\n\nReleased under the GNU General Public License version 2 or later. See LICENSE for more information.").replace("\n", "<br>"));
    setWindowTitle(tr("About %1").arg(EMU_NAME));
    const auto closeButton = addButton("OK", QMessageBox::ButtonRole::AcceptRole);
    setEscapeButton(closeButton);
    const auto webSiteButton = addButton(EMU_SITE, QMessageBox::ButtonRole::HelpRole);
    webSiteButton->connect(webSiteButton, &QPushButton::released, []() {
        QDesktopServices::openUrl(QUrl("https://" EMU_SITE));
    });
    setIconPixmap(QIcon(EMU_ICON_PATH).pixmap(32, 32));
    setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
}

About::~About()
    = default;
