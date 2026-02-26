/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Media menu UI module.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *          Teemu Korhonen
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2021-2022 Cacodemon345
 *          Copyright 2021-2022 Teemu Korhonen
 */
#include "qt_progsettings.hpp"
#include "qt_machinestatus.hpp"

#include <QMenu>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QStringBuilder>
#include <QApplication>
#include <QStyle>

extern "C" {
#ifdef Q_OS_WINDOWS
#    define BITMAP WINDOWS_BITMAP
#    include <windows.h>
#    include <windowsx.h>
#    undef BITMAP
#endif
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/cassette.h>
#include <86box/machine.h>
#include <86box/cartridge.h>
#include <86box/fdd.h>
#include <86box/fdd_86f.h>
#include <86box/cdrom.h>
#include <86box/scsi_device.h>
#include <86box/rdisk.h>
#include <86box/mo.h>
#include <86box/sound.h>
#include <86box/ui.h>
#include <86box/thread.h>
#include <86box/network.h>
};

#include "qt_newfloppydialog.hpp"
#include "qt_util.hpp"
#include "qt_deviceconfig.hpp"
#include "qt_mediahistorymanager.hpp"
#include "qt_mediamenu.hpp"
#include "qt_iconindicators.hpp"

std::shared_ptr<MediaMenu> MediaMenu::ptr;

static QSize pixmap_size(16, 16);

MediaMenu::MediaMenu(QWidget *parent)
    : QObject(parent)
{
    parentWidget = parent;
    connect(this, &MediaMenu::onCdromUpdateUi, this, &MediaMenu::cdromUpdateUi, Qt::QueuedConnection);
}

void
MediaMenu::refresh(QMenu *parentMenu)
{
    parentMenu->clear();

    if (MachineStatus::hasCassette()) {
        cassetteMenu   = parentMenu->addMenu("");
        QIcon img_icon = QIcon(":/settings/qt/icons/cassette_image.ico");
        cassetteMenu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, New), tr("&New image…"), [this]() { cassetteNewImage(); });
        cassetteMenu->addSeparator();
        cassetteMenu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, Browse), tr("&Existing image…"), [this]() { cassetteSelectImage(false); });
        cassetteMenu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, WriteProtectedBrowse), tr("Existing image (&Write-protected)…"), [this]() { cassetteSelectImage(true); });
        cassetteMenu->addSeparator();
        for (int slot = 0; slot < MAX_PREV_IMAGES; slot++) {
            cassetteImageHistoryPos[slot] = cassetteMenu->children().count();
            cassetteMenu->addAction(img_icon, tr("Image %1").arg(slot), [this, slot]() { cassetteMenuSelect(slot); })->setCheckable(false);
        }
        cassetteMenu->addSeparator();
        cassetteRecordPos = cassetteMenu->children().count();
        cassetteMenu->addAction(QIcon(":/settings/qt/icons/record.ico"), tr("&Record"), [this] { pc_cas_set_mode(cassette, 1); cassetteUpdateMenu(); })->setCheckable(true);
        cassettePlayPos = cassetteMenu->children().count();
        cassetteMenu->addAction(QIcon(":/menuicons/qt/icons/run.ico"), tr("&Play"), [this] { pc_cas_set_mode(cassette, 0); cassetteUpdateMenu(); })->setCheckable(true);
        cassetteRewindPos = cassetteMenu->children().count();
        cassetteMenu->addAction(QIcon(":/settings/qt/icons/rewind.ico"), tr("&Rewind to the beginning"), [] { pc_cas_rewind(cassette); });
        cassetteFastFwdPos = cassetteMenu->children().count();
        cassetteMenu->addAction(QIcon(":/settings/qt/icons/fast_forward.ico"), tr("&Fast forward to the end"), [] { pc_cas_append(cassette); });
        cassetteMenu->addSeparator();
        cassetteEjectPos = cassetteMenu->children().count();
        cassetteMenu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, Eject), tr("E&ject"), [this]() { cassetteEject(); });
        cassetteUpdateMenu();
    }

    cartridgeMenus.clear();
    if (machine_has_cartridge(machine)) {
        for (int i = 0; i < 2; i++) {
            auto *menu     = parentMenu->addMenu("");
            QIcon img_icon = QIcon(":/settings/qt/icons/cartridge_image.ico");
            menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, Browse), tr("&Image…"), [this, i]() { cartridgeSelectImage(i); });
            menu->addSeparator();
            for (int slot = 0; slot < MAX_PREV_IMAGES; slot++) {
                cartridgeImageHistoryPos[slot] = menu->children().count();
                menu->addAction(img_icon, tr("Image %1").arg(slot), [this, i, slot]() { cartridgeMenuSelect(i, slot); })->setCheckable(false);
            }
            menu->addSeparator();
            cartridgeEjectPos = menu->children().count();
            menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, Eject), tr("E&ject"), [this, i]() { cartridgeEject(i); });
            cartridgeMenus[i] = menu;
            cartridgeUpdateMenu(i);
        }
    }

    floppyMenus.clear();
    MachineStatus::iterateFDD([this, parentMenu](int i) {
        auto *menu     = parentMenu->addMenu("");
        QIcon img_icon = fdd_is_525(i) ? QIcon(":/settings/qt/icons/floppy_525_image.ico") : QIcon(":/settings/qt/icons/floppy_35_image.ico");
        menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, New), tr("&New image…"), [this, i]() { floppyNewImage(i); });
        menu->addSeparator();
        menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, Browse), tr("&Existing image…"), [this, i]() { floppySelectImage(i, false); });
        menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, WriteProtectedBrowse), tr("Existing image (&Write-protected)…"), [this, i]() { floppySelectImage(i, true); });
        menu->addSeparator();
        for (int slot = 0; slot < MAX_PREV_IMAGES; slot++) {
            floppyImageHistoryPos[slot] = menu->children().count();
            menu->addAction(img_icon, tr("Image %1").arg(slot), [this, i, slot]() { floppyMenuSelect(i, slot); })->setCheckable(false);
        }
        menu->addSeparator();
        floppyExportPos = menu->children().count();
        menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, Export), tr("E&xport to 86F…"), [this, i]() { floppyExportTo86f(i); });
        menu->addSeparator();
        floppyEjectPos = menu->children().count();
        menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, Eject), tr("E&ject"), [this, i]() { floppyEject(i); });
        floppyMenus[i] = menu;
        floppyUpdateMenu(i);
    });

    cdromMenus.clear();
    MachineStatus::iterateCDROM([this, parentMenu](int i) {
        auto *menu   = parentMenu->addMenu("");
        cdromMutePos = menu->children().count();
        menu->addAction(QIcon(":/settings/qt/icons/cdrom_mute.ico"), tr("&Mute"), [this, i]() { cdromMute(i); })->setCheckable(true);
        menu->addSeparator();
        menu->addAction(getIconWithIndicator(QIcon(":/settings/qt/icons/cdrom_image.ico"), pixmap_size, QIcon::Normal, Browse), tr("&Image…"), [this, i]() { cdromMount(i, 0, nullptr); })->setCheckable(false);
        menu->addAction(getIconWithIndicator(QIcon(":/settings/qt/icons/cdrom_folder.ico"), pixmap_size, QIcon::Normal, Browse), tr("&Folder…"), [this, i]() { cdromMount(i, 1, nullptr); })->setCheckable(false);
        menu->addSeparator();
        for (int slot = 0; slot < MAX_PREV_IMAGES; slot++) {
            cdromImageHistoryPos[slot] = menu->children().count();
            menu->addAction(tr("Image %1").arg(slot), [this, i, slot]() { cdromReload(i, slot); })->setCheckable(false);
        }
        menu->addSeparator();
#ifdef Q_OS_WINDOWS
        /* Loop through each Windows drive letter and test to see if
           it's a CDROM */
        for (const auto &letter : driveLetters) {
            auto drive = QString("%1:\\").arg(letter);
            if (GetDriveTypeA(drive.toUtf8().constData()) == DRIVE_CDROM)
                menu->addAction(QIcon(":/settings/qt/icons/cdrom_host.ico"), tr("&Host CD/DVD Drive (%1:)").arg(letter), [this, i, letter] { cdromMount(i, 2, QString(R"(\\.\%1:)").arg(letter)); })->setCheckable(false);
        }
        menu->addSeparator();
#elif defined(Q_OS_LINUX)
        /* Enumerate Linux CD/DVD drives (/dev/sr0 .. /dev/sr15). */
        for (int sr = 0; sr < 16; sr++) {
            QString devPath = QString("/dev/sr%1").arg(sr);
            if (QFile::exists(devPath))
                menu->addAction(QIcon(":/settings/qt/icons/cdrom_host.ico"), tr("&Host CD/DVD Drive (%1)").arg(devPath), [this, i, devPath] { cdromMount(i, 2, devPath); })->setCheckable(false);
        }
        menu->addSeparator();
#endif
        cdromEjectPos = menu->children().count();
        menu->addAction(tr("E&ject"), [this, i]() { cdromEject(i); })->setCheckable(false);
        cdromMenus[i] = menu;
        cdromUpdateMenu(i);
    });

    rdiskMenus.clear();
    MachineStatus::iterateRDisk([this, parentMenu](int i) {
        auto *menu     = parentMenu->addMenu("");
        int   t        = rdisk_drives[i].type;
        QIcon img_icon = ((t == RDISK_TYPE_ZIP_100) || (t == RDISK_TYPE_ZIP_250)) ? QIcon(":/settings/qt/icons/zip_image.ico") : QIcon(":/settings/qt/icons/rdisk_image.ico");
        menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, New), tr("&New image…"), [this, i]() { rdiskNewImage(i); });
        menu->addSeparator();
        menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, Browse), tr("&Existing image…"), [this, i]() { rdiskSelectImage(i, false); });
        menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, WriteProtectedBrowse), tr("Existing image (&Write-protected)…"), [this, i]() { rdiskSelectImage(i, true); });
        menu->addSeparator();
        for (int slot = 0; slot < MAX_PREV_IMAGES; slot++) {
            rdiskImageHistoryPos[slot] = menu->children().count();
            menu->addAction(img_icon, tr("Image %1").arg(slot), [this, i, slot]() { rdiskReload(i, slot); })->setCheckable(false);
        }
        menu->addSeparator();
        rdiskEjectPos = menu->children().count();
        menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, Eject), tr("E&ject"), [this, i]() { rdiskEject(i); });
        rdiskMenus[i] = menu;
        rdiskUpdateMenu(i);
    });

    moMenus.clear();
    MachineStatus::iterateMO([this, parentMenu](int i) {
        auto *menu     = parentMenu->addMenu("");
        QIcon img_icon = QIcon(":/settings/qt/icons/mo_image.ico");
        menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, New), tr("&New image…"), [this, i]() { moNewImage(i); });
        menu->addSeparator();
        menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, Browse), tr("&Existing image…"), [this, i]() { moSelectImage(i, false); });
        menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, WriteProtectedBrowse), tr("Existing image (&Write-protected)…"), [this, i]() { moSelectImage(i, true); });
        menu->addSeparator();
        for (int slot = 0; slot < MAX_PREV_IMAGES; slot++) {
            moImageHistoryPos[slot] = menu->children().count();
            menu->addAction(img_icon, tr("Image %1").arg(slot), [this, i, slot]() { moReload(i, slot); })->setCheckable(false);
        }
        menu->addSeparator();
        moEjectPos = menu->children().count();
        menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, Eject), tr("E&ject"), [this, i]() { moEject(i); });
        moMenus[i] = menu;
        moUpdateMenu(i);
    });

    netMenus.clear();
    MachineStatus::iterateNIC([this, parentMenu](int i) {
        auto *menu    = parentMenu->addMenu("");
        netDisconnPos = menu->children().count();
        auto *action  = menu->addAction(tr("&Connected"), [this, i] { network_is_connected(i) ? nicDisconnect(i) : nicConnect(i); });
        action->setCheckable(true);
        netMenus[i] = menu;
        nicUpdateMenu(i);
    });
    parentMenu->addAction(tr("Clear image &history"), [this]() { clearImageHistory(); });
}

void
MediaMenu::cassetteNewImage()
{
    auto      filename = QFileDialog::getSaveFileName(parentWidget, tr("Create…"));
    QFileInfo fileinfo(filename);
    if (fileinfo.suffix().isEmpty()) {
        filename.append(".cas");
    }
    if (!filename.isNull()) {
        if (filename.isEmpty())
            cassetteEject();
        else
            cassetteMount(filename, false);
    }
}

void
MediaMenu::cassetteSelectImage(bool wp)
{
    auto filename = QFileDialog::getOpenFileName(parentWidget,
                                                 QString(),
                                                 getMediaOpenDirectory(),
                                                 tr("Cassette images") % util::DlgFilter({ "pcm", "raw", "wav", "cas" }) % tr("All files") % util::DlgFilter({ "*" }, true));

    if (!filename.isEmpty())
        cassetteMount(filename, wp);
}

void
MediaMenu::cassetteMenuSelect(int slot)
{
    QString filename = mhm.getImageForSlot(0, slot, ui::MediaType::Cassette);
    cassetteMount(filename, 0);
    cassetteUpdateMenu();
    ui_sb_update_tip(SB_CASSETTE);
}

void
MediaMenu::cassetteMount(const QString &filename, bool wp)
{
    auto previous_image = QFileInfo(cassette_fname);
    pc_cas_set_fname(cassette, nullptr);
    memset(cassette_fname, 0, sizeof(cassette_fname));
    cassette_ui_writeprot = wp ? 1 : 0;

    if (!filename.isEmpty()) {
        QByteArray filenameBytes = filename.toUtf8();

        if (filename.left(5) == "wp://")
            cassette_ui_writeprot = 1;
        else if (cassette_ui_writeprot)
            filenameBytes = QString::asprintf(R"(wp://%s)", filename.toUtf8().data()).toUtf8();

        strncpy(cassette_fname, filenameBytes.data(), sizeof(cassette_fname) - 1);
        pc_cas_set_fname(cassette, cassette_fname);
    }

    ui_sb_update_icon_state(SB_CASSETTE, cassette->fname == nullptr);
    ui_sb_update_icon_wp(SB_CASSETTE, cassette_ui_writeprot);
    mhm.addImageToHistory(0, ui::MediaType::Cassette, previous_image.filePath(), filename);
    cassetteUpdateMenu();
    ui_sb_update_tip(SB_CASSETTE);
    config_save();
}

void
MediaMenu::cassetteEject()
{
    mhm.addImageToHistory(0, ui::MediaType::Cassette, cassette_fname, QString());
    pc_cas_set_fname(cassette, nullptr);
    memset(cassette_fname, 0, sizeof(cassette_fname));
    ui_sb_update_icon_state(SB_CASSETTE, 1);
    cassetteUpdateMenu();
    ui_sb_update_tip(SB_CASSETTE);
    config_save();
}

void
MediaMenu::cassetteUpdateMenu()
{
    QString       name = cassette_fname;
    QFileInfo     fi(cassette_fname);
    const QString mode        = cassette_mode;
    auto          childs      = cassetteMenu->children();
    auto         *recordMenu  = dynamic_cast<QAction *>(childs[cassetteRecordPos]);
    auto         *playMenu    = dynamic_cast<QAction *>(childs[cassettePlayPos]);
    auto         *rewindMenu  = dynamic_cast<QAction *>(childs[cassetteRewindPos]);
    auto         *fastFwdMenu = dynamic_cast<QAction *>(childs[cassetteFastFwdPos]);
    auto         *ejectMenu   = dynamic_cast<QAction *>(childs[cassetteEjectPos]);

    recordMenu->setEnabled(!name.isEmpty());
    playMenu->setEnabled(!name.isEmpty());
    rewindMenu->setEnabled(!name.isEmpty());
    fastFwdMenu->setEnabled(!name.isEmpty());
    ejectMenu->setEnabled(!name.isEmpty());
    ejectMenu->setText(name.isEmpty() ? tr("E&ject") : tr("E&ject %1").arg(fi.fileName()));

    const bool isSaving = (mode == QStringLiteral("save"));
    recordMenu->setChecked(isSaving);
    playMenu->setChecked(!isSaving);

    cassetteMenu->setTitle(tr("C&assette: %1").arg(name.isEmpty() ? tr("(empty)") : name));
    cassetteMenu->setToolTip(tr("Cassette: %1").arg(name.isEmpty() ? tr("(empty)") : name));

    for (int slot = 0; slot < MAX_PREV_IMAGES; slot++) {
        updateImageHistory(0, slot, ui::MediaType::Cassette);
    }
}

void
MediaMenu::cartridgeMount(int i, const QString &filename)
{
    auto previous_image = QFileInfo(cart_fns[i]);
    cart_close(i);
    QByteArray filenameBytes = filename.toUtf8();
    cart_load(i, filenameBytes.data());

    ui_sb_update_icon_state(SB_CARTRIDGE | i, cart_fns[i][0] == 0);
    mhm.addImageToHistory(i, ui::MediaType::Cartridge, previous_image.filePath(), filename);
    cartridgeUpdateMenu(i);
    ui_sb_update_tip(SB_CARTRIDGE | i);
    config_save();
}

void
MediaMenu::cartridgeSelectImage(int i)
{
    const auto filename = QFileDialog::getOpenFileName(
        parentWidget,
        QString(),
        getMediaOpenDirectory(),
        tr("Cartridge images") % util::DlgFilter({ "a", "b", "jrc" }) % tr("All files") % util::DlgFilter({ "*" }, true));

    if (filename.isEmpty()) {
        return;
    }
    cartridgeMount(i, filename);
}

void
MediaMenu::cartridgeMenuSelect(int index, int slot)
{
    QString filename = mhm.getImageForSlot(index, slot, ui::MediaType::Cartridge);
    cartridgeMount(index, filename);
    cartridgeUpdateMenu(index);
    ui_sb_update_tip(SB_CARTRIDGE | index);
}

void
MediaMenu::cartridgeEject(int i)
{
    mhm.addImageToHistory(i, ui::MediaType::Cartridge, cart_fns[i], QString());
    cart_close(i);
    ui_sb_update_icon_state(SB_CARTRIDGE | i, 1);
    cartridgeUpdateMenu(i);
    ui_sb_update_tip(SB_CARTRIDGE | i);
    config_save();
}

void
MediaMenu::cartridgeUpdateMenu(int i)
{
    const QString name = cart_fns[i];
    QFileInfo     fi(cart_fns[i]);
    auto         *menu      = cartridgeMenus[i];
    auto          childs    = menu->children();
    auto         *ejectMenu = dynamic_cast<QAction *>(childs[cartridgeEjectPos]);
    ejectMenu->setEnabled(!name.isEmpty());
    ejectMenu->setText(name.isEmpty() ? tr("E&ject") : tr("E&ject %1").arg(fi.fileName()));
    menu->setTitle(tr("Car&tridge %1: %2").arg(QString::number(i + 1), name.isEmpty() ? tr("(empty)") : name));
    menu->setToolTip(tr("Cartridge %1: %2").arg(QString::number(i + 1), name.isEmpty() ? tr("(empty)") : name));

    for (int slot = 0; slot < MAX_PREV_IMAGES; slot++) {
        updateImageHistory(i, slot, ui::MediaType::Cartridge);
    }
}

void
MediaMenu::floppyNewImage(int i)
{
    NewFloppyDialog dialog(NewFloppyDialog::MediaType::Floppy, parentWidget);
    switch (dialog.exec()) {
        default:
            break;
        case QDialog::Accepted:
            const QByteArray filename = dialog.fileName().toUtf8();
            floppyMount(i, filename, false);
            break;
    }
}

void
MediaMenu::floppySelectImage(int i, bool wp)
{
    auto filename = QFileDialog::getOpenFileName(
        parentWidget,
        QString(),
        getMediaOpenDirectory(),
        tr("All images") %
        util::DlgFilter({ "0??","1??","??0","86f","bin","cq?","d??","flp","hdm","im?","json","td0","*fd?","mfm","xdf" }) %
        tr("Advanced sector images") %
        util::DlgFilter({ "imd","json","td0" }) %
        tr("Basic sector images") %
        util::DlgFilter({ "0??","1??","??0","bin","cq?","d??","flp","hdm","im?","xdf","*fd?" }) %
        tr("Flux images") %
        util::DlgFilter({ "fdi" }) %
        tr("Surface images") %
        util::DlgFilter({ "86f","mfm" }) %
        tr("All files") %
        util::DlgFilter({ "*" }, true));

    if (!filename.isEmpty())
        floppyMount(i, filename, wp);
}

void
MediaMenu::floppyMount(int i, const QString &filename, bool wp)
{
    auto previous_image = QFileInfo(floppyfns[i]);
    fdd_close(i);
    ui_writeprot[i] = wp ? 1 : 0;
    if (!filename.isEmpty()) {
        QByteArray filenameBytes = filename.toUtf8();

        if (filename.left(5) == "wp://")
            ui_writeprot[i] = 1;
        else if (ui_writeprot[i])
            filenameBytes = QString::asprintf(R"(wp://%s)", filename.toUtf8().data()).toUtf8();

        fdd_load(i, filenameBytes.data());
        mhm.addImageToHistory(i, ui::MediaType::Floppy, previous_image.filePath(), QString(filenameBytes));
    } else
        mhm.addImageToHistory(i, ui::MediaType::Floppy, previous_image.filePath(), filename);
    ui_sb_update_icon_state(SB_FLOPPY | i, drive_empty[i]);
    ui_sb_update_icon_wp(SB_FLOPPY | i, ui_writeprot[i]);
    floppyUpdateMenu(i);
    ui_sb_update_tip(SB_FLOPPY | i);
    config_save();
}

void
MediaMenu::floppyEject(int i)
{
    mhm.addImageToHistory(i, ui::MediaType::Floppy, floppyfns[i], QString());
    fdd_close(i);
    ui_sb_update_icon_state(SB_FLOPPY | i, 1);
    floppyUpdateMenu(i);
    ui_sb_update_tip(SB_FLOPPY | i);
    config_save();
}

void
MediaMenu::floppyExportTo86f(int i)
{
    auto filename = QFileDialog::getSaveFileName(parentWidget, QString(), QString(), tr("Surface images") % util::DlgFilter({ "86f" }, true));
    if (!filename.isEmpty()) {
        QByteArray filenameBytes = filename.toUtf8();
        plat_pause(1);
        if (d86f_export(i, filenameBytes.data()) == 0) {
            QMessageBox::critical(parentWidget, tr("Unable to write file"), tr("Make sure the file is being saved to a writable directory"));
        }
        plat_pause(0);
    }
}

void
MediaMenu::floppyUpdateMenu(int i)
{
    QString   name = floppyfns[i];
    QFileInfo fi(floppyfns[i]);

    if (!floppyMenus.contains(i))
        return;

    auto *menu   = floppyMenus[i];
    auto  childs = menu->children();

    auto *ejectMenu  = dynamic_cast<QAction *>(childs[floppyEjectPos]);
    auto *exportMenu = dynamic_cast<QAction *>(childs[floppyExportPos]);
    ejectMenu->setEnabled(!name.isEmpty());
    ejectMenu->setText(name.isEmpty() ? tr("E&ject") : tr("E&ject %1").arg(fi.fileName()));
    exportMenu->setEnabled(!name.isEmpty());

    for (int slot = 0; slot < MAX_PREV_IMAGES; slot++) {
        updateImageHistory(i, slot, ui::MediaType::Floppy);
    }

    int type = fdd_get_type(i);
    floppyMenus[i]->setTitle(tr("&Floppy %1 (%2): %3").arg(QString::number(i + 1), fdd_getname(type), name.isEmpty() ? tr("(empty)") : name));
    floppyMenus[i]->setToolTip(tr("Floppy %1 (%2): %3").arg(QString::number(i + 1), fdd_getname(type), name.isEmpty() ? tr("(empty)") : name));
}

void
MediaMenu::floppyMenuSelect(int index, int slot)
{
    QString filename = mhm.getImageForSlot(index, slot, ui::MediaType::Floppy);
    floppyMount(index, filename, false);
    floppyUpdateMenu(index);
    ui_sb_update_tip(SB_FLOPPY | index);
}

void
MediaMenu::cdromMute(int i)
{
    cdrom[i].sound_on ^= 1;
    config_save();
    cdromUpdateMenu(i);
    sound_cd_thread_reset();
}

void
MediaMenu::cdromMount(int i, const QString &filename)
{
    QByteArray fn        = filename.toUtf8().data();
    int        was_empty = cdrom_is_empty(i);

    cdrom_exit(i);

    memset(cdrom[i].image_path, 0, sizeof(cdrom[i].image_path));
#ifdef Q_OS_WINDOWS
    if ((fn.data() != nullptr) && (strlen(fn.data()) >= 1) && (fn.data()[strlen(fn.data()) - 1] == '/'))
        fn.data()[strlen(fn.data()) - 1] = '\\';
#else
    if ((fn.data() != NULL) && (strlen(fn.data()) >= 1) && (fn.data()[strlen(fn.data()) - 1] == '\\'))
        fn.data()[strlen(fn.data()) - 1] = '/';
#endif
    cdrom_load(&(cdrom[i]), fn.data(), 1);

    /* Signal media change to the emulated machine. */
    if (cdrom[i].insert) {
        cdrom[i].insert(cdrom[i].priv);

        /* The drive was previously empty, transition directly to UNIT ATTENTION. */
        if (was_empty)
            cdrom[i].insert(cdrom[i].priv);
    }

    if (strlen(cdrom[i].image_path) > 0)
        ui_sb_update_icon_state(SB_CDROM | i, 0);
    else
        ui_sb_update_icon_state(SB_CDROM | i, 1);
    mhm.addImageToHistory(i, ui::MediaType::Optical, cdrom[i].prev_image_path, cdrom[i].image_path);

    cdromUpdateMenu(i);
    ui_sb_update_tip(SB_CDROM | i);
    config_save();
}

void
MediaMenu::cdromMount(int i, int dir, const QString &arg)
{
    QString   filename;
    QFileInfo fi(cdrom[i].image_path);

    if (dir > 1)
        filename = QString::asprintf(R"(ioctl://%s)", arg.toUtf8().data());
    else if (dir == 1)
        filename = QFileDialog::getExistingDirectory(parentWidget, QString(), getMediaOpenDirectory());
    else {
        filename = QFileDialog::getOpenFileName(parentWidget, QString(),
                                                getMediaOpenDirectory(),
                                                tr("CD-ROM images") % util::DlgFilter({ "iso", "cue", "mds", "mdx" }) % tr("All files") % util::DlgFilter({ "*" }, true));
    }

    if (filename.isEmpty())
        return;

    cdromMount(i, filename);
}

void
MediaMenu::cdromEject(int i)
{
    mhm.addImageToHistory(i, ui::MediaType::Optical, cdrom[i].image_path, QString());
    cdrom_eject(i);
    cdromUpdateMenu(i);
    ui_sb_update_tip(SB_CDROM | i);
}

void
MediaMenu::cdromReload(int index, int slot)
{
    const QString filename = mhm.getImageForSlot(index, slot, ui::MediaType::Optical);
    cdromMount(index, filename);
    cdromUpdateMenu(index);
    ui_sb_update_tip(SB_CDROM | index);
}

void
MediaMenu::cdromUpdateUi(int i)
{
    cdrom_t *drv = &cdrom[i];

    if (strlen(cdrom[i].image_path) == 0) {
        mhm.addImageToHistory(i, ui::MediaType::Optical, drv->prev_image_path, QString());
        ui_sb_update_icon_state(SB_CDROM | i, 1);
    } else {
        mhm.addImageToHistory(i, ui::MediaType::Optical, drv->prev_image_path, drv->image_path);
        ui_sb_update_icon_state(SB_CDROM | i, 0);
    }

    cdromUpdateMenu(i);
    ui_sb_update_tip(SB_CDROM | i);
}

void
MediaMenu::updateImageHistory(int index, int slot, ui::MediaType type)
{
    QMenu      *menu;
    QAction    *imageHistoryUpdatePos;
    QObjectList children;
    QFileInfo   fi;
    QIcon       menu_icon;
    const auto  fn = mhm.getImageForSlot(index, slot, type);

    QString menu_item_name;

    switch (type) {
        default:
            menu_item_name = fi.fileName().isEmpty() ? tr("Reload previous image") : fn;
            return;
        case ui::MediaType::Cassette:
            if (!MachineStatus::hasCassette())
                return;
            menu                  = cassetteMenu;
            children              = menu->children();
            imageHistoryUpdatePos = dynamic_cast<QAction *>(children[cassetteImageHistoryPos[slot]]);
            menu_icon             = QIcon(":/settings/qt/icons/cassette_image.ico");
            if (fn.left(5) == "wp://")
                fi.setFile(fn.right(fn.length() - 5));
            else
                fi.setFile(fn);
            if (!fi.fileName().isEmpty() && (fn.left(5) == "wp://")) {
                menu_item_name = fi.fileName().isEmpty() ? tr("Reload previous image") : "🔒 " + fn.right(fn.length() - 5);
                imageHistoryUpdatePos->setIcon(getIconWithIndicator(menu_icon, pixmap_size, QIcon::Normal, WriteProtected));
            } else {
                menu_item_name = fi.fileName().isEmpty() ? tr("Reload previous image") : fn;
                imageHistoryUpdatePos->setIcon(menu_icon);
            }
            break;
        case ui::MediaType::Cartridge:
            if (!machine_has_cartridge(machine))
                return;
            menu                  = cartridgeMenus[index];
            children              = menu->children();
            imageHistoryUpdatePos = dynamic_cast<QAction *>(children[cartridgeImageHistoryPos[slot]]);
            fi.setFile(fn);
            menu_item_name = fi.fileName().isEmpty() ? tr("Reload previous image") : fn;
            break;
        case ui::MediaType::Floppy:
            if (!floppyMenus.contains(index))
                return;
            menu                  = floppyMenus[index];
            children              = menu->children();
            imageHistoryUpdatePos = dynamic_cast<QAction *>(children[floppyImageHistoryPos[slot]]);
            menu_icon             = fdd_is_525(index) ? QIcon(":/settings/qt/icons/floppy_525_image.ico") : QIcon(":/settings/qt/icons/floppy_35_image.ico");
            if (fn.left(5) == "wp://")
                fi.setFile(fn.right(fn.length() - 5));
            else
                fi.setFile(fn);
            if (!fi.fileName().isEmpty() && (fn.left(5) == "wp://")) {
                menu_item_name = fi.fileName().isEmpty() ? tr("Reload previous image") : "🔒 " + fn.right(fn.length() - 5);
                imageHistoryUpdatePos->setIcon(getIconWithIndicator(menu_icon, pixmap_size, QIcon::Normal, WriteProtected));
            } else {
                menu_item_name = fi.fileName().isEmpty() ? tr("Reload previous image") : fn;
                imageHistoryUpdatePos->setIcon(menu_icon);
            }
            break;
        case ui::MediaType::Optical:
            if (!cdromMenus.contains(index))
                return;
            menu                  = cdromMenus[index];
            children              = menu->children();
            imageHistoryUpdatePos = dynamic_cast<QAction *>(children[cdromImageHistoryPos[slot]]);
            if (fn.left(8) == "ioctl://") {
                menu_icon = QIcon(":/settings/qt/icons/cdrom_host.ico");
#ifdef Q_OS_WINDOWS
                menu_item_name = tr("Host CD/DVD Drive (%1)").arg(fn.right(2));
#else
                menu_item_name = tr("Host CD/DVD Drive (%1)").arg(fn.right(fn.length() - 8));
#endif
            } else {
                fi.setFile(fn);
                menu_icon      = fi.isDir() ? QIcon(":/settings/qt/icons/cdrom_folder.ico") : QIcon(":/settings/qt/icons/cdrom_image.ico");
                menu_item_name = fn.isEmpty() ? tr("Reload previous image") : fn;
            }
            imageHistoryUpdatePos->setIcon(menu_icon);
            break;
        case ui::MediaType::RDisk:
            if (!rdiskMenus.contains(index))
                return;
            menu                  = rdiskMenus[index];
            children              = menu->children();
            imageHistoryUpdatePos = dynamic_cast<QAction *>(children[rdiskImageHistoryPos[slot]]);
            menu_icon             = QIcon(":/settings/qt/icons/mo_image.ico");
            if (fn.left(5) == "wp://")
                fi.setFile(fn.right(fn.length() - 5));
            else
                fi.setFile(fn);
            if (!fi.fileName().isEmpty() && (fn.left(5) == "wp://")) {
                menu_item_name = fi.fileName().isEmpty() ? tr("Reload previous image") : "🔒 " + fn.right(fn.length() - 5);
                imageHistoryUpdatePos->setIcon(getIconWithIndicator(menu_icon, pixmap_size, QIcon::Normal, WriteProtected));
            } else {
                menu_item_name = fi.fileName().isEmpty() ? tr("Reload previous image") : fn;
                imageHistoryUpdatePos->setIcon(menu_icon);
            }
            break;
        case ui::MediaType::Mo:
            if (!moMenus.contains(index))
                return;
            menu                  = moMenus[index];
            children              = menu->children();
            imageHistoryUpdatePos = dynamic_cast<QAction *>(children[moImageHistoryPos[slot]]);
            menu_icon             = QIcon(":/settings/qt/icons/mo_image.ico");
            if (fn.left(5) == "wp://")
                fi.setFile(fn.right(fn.length() - 5));
            else
                fi.setFile(fn);
            if (!fi.fileName().isEmpty() && (fn.left(5) == "wp://")) {
                menu_item_name = fi.fileName().isEmpty() ? tr("Reload previous image") : "🔒 " + fn.right(fn.length() - 5);
                imageHistoryUpdatePos->setIcon(getIconWithIndicator(menu_icon, pixmap_size, QIcon::Normal, WriteProtected));
            } else {
                menu_item_name = fi.fileName().isEmpty() ? tr("Reload previous image") : fn;
                imageHistoryUpdatePos->setIcon(menu_icon);
            }
            break;
    }

#ifndef Q_OS_MACOS
    if ((slot >= 0) && (slot <= 9))
        imageHistoryUpdatePos->setText(menu_item_name.prepend("&%1 ").arg((slot == 9) ? 0 : (slot + 1)));
    else
#endif
        imageHistoryUpdatePos->setText(menu_item_name);

    if (fn.left(8) == "ioctl://")
        imageHistoryUpdatePos->setVisible(true);
    else
        imageHistoryUpdatePos->setVisible(!fn.isEmpty() && fi.exists());
}

void
MediaMenu::clearImageHistory()
{
    mhm.clearImageHistory();
    ui_sb_update_panes();
}

void
MediaMenu::cdromUpdateMenu(int i)
{
    QString name = cdrom[i].image_path;
    QString name2;
    QIcon   menu_icon;

    if (!cdromMenus.contains(i))
        return;
    auto *menu   = cdromMenus[i];
    auto  childs = menu->children();

    auto *muteMenu = dynamic_cast<QAction *>(childs[cdromMutePos]);
    muteMenu->setIcon(QIcon((cdrom[i].sound_on == 0) ? ":/settings/qt/icons/cdrom_unmute.ico" : ":/settings/qt/icons/cdrom_mute.ico"));
    muteMenu->setText((cdrom[i].sound_on == 0) ? tr("&Unmute") : tr("&Mute"));

    auto *ejectMenu = dynamic_cast<QAction *>(childs[cdromEjectPos]);
    ejectMenu->setEnabled(!name.isEmpty());
    QString menu_item_name;
    if (name.left(8) == "ioctl://") {
#ifdef Q_OS_WINDOWS
        menu_item_name = tr("Host CD/DVD Drive (%1)").arg(name.right(2));
#else
        menu_item_name = tr("Host CD/DVD Drive (%1)").arg(name.right(name.length() - 8));
#endif
        name2     = menu_item_name;
        menu_icon = QIcon(":/settings/qt/icons/cdrom_host.ico");
    } else {
        QFileInfo fi(cdrom[i].image_path);

        menu_item_name = name.isEmpty() ? QString() : fi.fileName();
        name2          = name;
        if (name.isEmpty())
            menu_icon = QIcon(":/settings/qt/icons/cdrom.ico");
        else
            menu_icon = fi.isDir() ? QIcon(":/settings/qt/icons/cdrom_folder.ico") : QIcon(":/settings/qt/icons/cdrom_image.ico");
    }
    ejectMenu->setIcon(getIconWithIndicator(menu_icon, pixmap_size, QIcon::Normal, Eject));
    ejectMenu->setText(name.isEmpty() ? tr("E&ject") : tr("E&ject %1").arg(menu_item_name));

    for (int slot = 0; slot < MAX_PREV_IMAGES; slot++)
        updateImageHistory(i, slot, ui::MediaType::Optical);

    QString busName = tr("Unknown Bus");
    switch (cdrom[i].bus_type) {
        default:
            break;
        case CDROM_BUS_ATAPI:
            busName = "ATAPI";
            break;
        case CDROM_BUS_SCSI:
            busName = "SCSI";
            break;
        case CDROM_BUS_MITSUMI:
            busName = "Mitsumi";
            break;
        case CDROM_BUS_MKE:
            busName = "Panasonic/MKE";
            break;
    }

    menu->setTitle(tr("&CD-ROM %1 (%2): %3").arg(QString::number(i + 1), busName, name.isEmpty() ? tr("(empty)") : name2));
    menu->setToolTip(tr("CD-ROM %1 (%2): %3").arg(QString::number(i + 1), busName, name.isEmpty() ? tr("(empty)") : name2));
}

void
MediaMenu::rdiskNewImage(int i)
{
    NewFloppyDialog dialog(NewFloppyDialog::MediaType::RDisk, parentWidget);
    switch (dialog.exec()) {
        default:
            break;
        case QDialog::Accepted:
            QByteArray filename = dialog.fileName().toUtf8();
            rdiskMount(i, filename, false);
            break;
    }
}

void
MediaMenu::rdiskSelectImage(int i, bool wp)
{
    const auto filename = QFileDialog::getOpenFileName(
        parentWidget,
        QString(),
        QString(),
        tr("Removable disk images") % util::DlgFilter({ "im?", "rdi", "zdi" }) % tr("All files") % util::DlgFilter({ "*" }, true));

    if (!filename.isEmpty())
        rdiskMount(i, filename, wp);
}

void
MediaMenu::rdiskMount(int i, const QString &filename, bool wp)
{
    const auto dev       = static_cast<rdisk_t *>(rdisk_drives[i].priv);
    int        was_empty = rdisk_is_empty(i);

    rdisk_disk_close(dev);
    rdisk_drives[i].read_only = wp;
    if (!filename.isEmpty()) {
        QByteArray filenameBytes = filename.toUtf8();

        if (filename.left(5) == "wp://")
            rdisk_drives[i].read_only = 1;
        else if (rdisk_drives[i].read_only)
            filenameBytes = QString::asprintf(R"(wp://%s)", filename.toUtf8().data()).toUtf8();

        rdisk_load(dev, filenameBytes.data(), 1);

        /* Signal media change to the emulated machine. */
        rdisk_insert(dev);

        /* The drive was previously empty, transition directly to UNIT ATTENTION. */
        if (was_empty)
            rdisk_insert(dev);
    }
    mhm.addImageToHistory(i, ui::MediaType::RDisk, rdisk_drives[i].prev_image_path, rdisk_drives[i].image_path);

    ui_sb_update_icon_state(SB_RDISK | i, dev->drv->fp == NULL);
    ui_sb_update_icon_wp(SB_RDISK | i, wp);
    rdiskUpdateMenu(i);
    ui_sb_update_tip(SB_RDISK | i);

    config_save();
}

void
MediaMenu::rdiskEject(int i)
{
    const auto dev = static_cast<rdisk_t *>(rdisk_drives[i].priv);

    mhm.addImageToHistory(i, ui::MediaType::RDisk, rdisk_drives[i].image_path, QString());
    rdisk_disk_close(dev);
    rdisk_drives[i].image_path[0] = 0;
    if (rdisk_drives[i].bus_type) {
        /* Signal disk change to the emulated machine. */
        rdisk_insert(dev);
    }

    ui_sb_update_icon_state(SB_RDISK | i, 1);
    rdiskUpdateMenu(i);
    ui_sb_update_tip(SB_RDISK | i);
    config_save();
}

void
MediaMenu::rdiskReloadPrev(int i)
{
    const auto dev = static_cast<rdisk_t *>(rdisk_drives[i].priv);

    rdisk_disk_reload(dev);
    if (strlen(rdisk_drives[i].image_path) == 0) {
        ui_sb_update_icon_state(SB_RDISK | i, 1);
    } else {
        ui_sb_update_icon_state(SB_RDISK | i, 0);
    }
    ui_sb_update_icon_wp(SB_RDISK | i, rdisk_drives[i].read_only);

    rdiskUpdateMenu(i);
    ui_sb_update_tip(SB_RDISK | i);

    config_save();
}

void
MediaMenu::rdiskReload(int index, int slot)
{
    const QString filename = mhm.getImageForSlot(index, slot, ui::MediaType::RDisk);
    rdiskMount(index, filename, rdisk_drives[index].read_only);
    rdiskUpdateMenu(index);
    ui_sb_update_tip(SB_RDISK | index);
}

void
MediaMenu::moUpdateMenu(int i)
{
    QString   name      = mo_drives[i].image_path;
    QString   prev_name = mo_drives[i].prev_image_path;
    QFileInfo fi(mo_drives[i].image_path);
    if (!moMenus.contains(i))
        return;
    auto *menu   = moMenus[i];
    auto  childs = menu->children();

    auto *ejectMenu = dynamic_cast<QAction *>(childs[moEjectPos]);
    ejectMenu->setEnabled(!name.isEmpty());
    ejectMenu->setText(name.isEmpty() ? tr("E&ject") : tr("E&ject %1").arg(fi.fileName()));

    QString busName = tr("Unknown Bus");
    switch (mo_drives[i].bus_type) {
        default:
            break;
        case MO_BUS_ATAPI:
            busName = "ATAPI";
            break;
        case MO_BUS_SCSI:
            busName = "SCSI";
            break;
    }

    menu->setTitle(tr("&MO %1 (%2): %3").arg(QString::number(i + 1), busName, name.isEmpty() ? tr("(empty)") : name));
    menu->setToolTip(tr("MO %1 (%2): %3").arg(QString::number(i + 1), busName, name.isEmpty() ? tr("(empty)") : name));

    for (int slot = 0; slot < MAX_PREV_IMAGES; slot++)
        updateImageHistory(i, slot, ui::MediaType::Mo);
}

void
MediaMenu::rdiskUpdateMenu(int i)
{
    const QString name      = rdisk_drives[i].image_path;
    const QString prev_name = rdisk_drives[i].prev_image_path;
    QFileInfo     fi(rdisk_drives[i].image_path);
    if (!rdiskMenus.contains(i))
        return;
    auto *menu   = rdiskMenus[i];
    auto  childs = menu->children();

    auto *ejectMenu = dynamic_cast<QAction *>(childs[rdiskEjectPos]);
    ejectMenu->setEnabled(!name.isEmpty());
    ejectMenu->setText(name.isEmpty() ? tr("E&ject") : tr("E&ject %1").arg(fi.fileName()));

    QString busName = tr("Unknown Bus");
    switch (rdisk_drives[i].bus_type) {
        default:
            break;
        case RDISK_BUS_ATAPI:
            busName = "ATAPI";
            break;
        case RDISK_BUS_SCSI:
            busName = "SCSI";
            break;
    }

    menu->setTitle(tr("&Removable disk %1 (%2): %3").arg(QString::number(i + 1), busName, name.isEmpty() ? tr("(empty)") : name));
    menu->setToolTip(tr("Removable disk %1 (%2): %3").arg(QString::number(i + 1), busName, name.isEmpty() ? tr("(empty)") : name));

    for (int slot = 0; slot < MAX_PREV_IMAGES; slot++)
        updateImageHistory(i, slot, ui::MediaType::RDisk);
}

void
MediaMenu::moNewImage(int i)
{
    NewFloppyDialog dialog(NewFloppyDialog::MediaType::Mo, parentWidget);
    switch (dialog.exec()) {
        default:
            break;
        case QDialog::Accepted:
            QByteArray filename = dialog.fileName().toUtf8();
            moMount(i, filename, false);
            break;
    }
}

void
MediaMenu::moSelectImage(int i, bool wp)
{
    const auto filename = QFileDialog::getOpenFileName(
        parentWidget,
        QString(),
        getMediaOpenDirectory(),
        tr("MO images") % util::DlgFilter({ "im?", "mdi" }) % tr("All files") % util::DlgFilter({
                                                                                                    "*",
                                                                                                },
                                                                                                true));

    if (!filename.isEmpty())
        moMount(i, filename, wp);
}

void
MediaMenu::moMount(int i, const QString &filename, bool wp)
{
    const auto dev       = static_cast<mo_t *>(mo_drives[i].priv);
    int        was_empty = mo_is_empty(i);

    mo_disk_close(dev);
    mo_drives[i].read_only = wp;
    if (!filename.isEmpty()) {
        QByteArray filenameBytes = filename.toUtf8();

        if (filename.left(5) == "wp://")
            mo_drives[i].read_only = 1;
        else if (mo_drives[i].read_only)
            filenameBytes = QString::asprintf(R"(wp://%s)", filename.toUtf8().data()).toUtf8();

        mo_load(dev, filenameBytes.data(), 1);

        /* Signal media change to the emulated machine. */
        mo_insert(dev);

        /* The drive was previously empty, transition directly to UNIT ATTENTION. */
        if (was_empty)
            mo_insert(dev);
    }
    mhm.addImageToHistory(i, ui::MediaType::Mo, mo_drives[i].prev_image_path, mo_drives[i].image_path);

    ui_sb_update_icon_state(SB_MO | i, dev->drv->fp == nullptr);
    moUpdateMenu(i);
    ui_sb_update_tip(SB_MO | i);

    config_save();
}

void
MediaMenu::moEject(int i)
{
    const auto dev = static_cast<mo_t *>(mo_drives[i].priv);

    mhm.addImageToHistory(i, ui::MediaType::Mo, mo_drives[i].image_path, QString());
    mo_disk_close(dev);
    mo_drives[i].image_path[0] = 0;
    if (mo_drives[i].bus_type) {
        /* Signal disk change to the emulated machine. */
        mo_insert(dev);
    }

    ui_sb_update_icon_state(SB_MO | i, 1);
    moUpdateMenu(i);
    ui_sb_update_tip(SB_MO | i);
    config_save();
}

void
MediaMenu::moReloadPrev(int i)
{
    mo_t *dev = (mo_t *) mo_drives[i].priv;

    mo_disk_reload(dev);
    if (strlen(mo_drives[i].image_path) == 0) {
        ui_sb_update_icon_state(SB_MO | i, 1);
    } else {
        ui_sb_update_icon_state(SB_MO | i, 0);
    }
    ui_sb_update_icon_state(SB_MO | i, mo_drives[i].read_only);

    moUpdateMenu(i);
    ui_sb_update_tip(SB_MO | i);

    config_save();
}

void
MediaMenu::moReload(int index, int slot)
{
    const QString filename = mhm.getImageForSlot(index, slot, ui::MediaType::Mo);
    moMount(index, filename, false);
    moUpdateMenu(index);
    ui_sb_update_tip(SB_MO | index);
}

void
MediaMenu::nicConnect(int i)
{
    network_connect(i, 1);
    ui_sb_update_icon_state(SB_NETWORK | i, 0);
    nicUpdateMenu(i);
    config_save();
}

void
MediaMenu::nicDisconnect(int i)
{
    network_connect(i, 0);
    ui_sb_update_icon_state(SB_NETWORK | i, 1);
    nicUpdateMenu(i);
    config_save();
}

void
MediaMenu::nicUpdateMenu(int i)
{
    if (!netMenus.contains(i))
        return;

    QString netType;
    switch (net_cards_conf[i].net_type) {
        default:
            netType = tr("Null Driver");
            break;
        case NET_TYPE_SLIRP:
            netType = "SLiRP";
            break;
        case NET_TYPE_PCAP:
            netType = "PCAP";
            break;
        case NET_TYPE_VDE:
            netType = "VDE";
            break;
        case NET_TYPE_TAP:
            netType = "TAP";
            break;
        case NET_TYPE_NLSWITCH:
            netType = tr("Local Switch");
            break;
        case NET_TYPE_NRSWITCH:
            netType = tr("Remote Switch");
            break;
    }

    QString devName = DeviceConfig::DeviceName(network_card_getdevice(net_cards_conf[i].device_num), network_card_get_internal_name(net_cards_conf[i].device_num), 1);

    auto *menu            = netMenus[i];
    auto  childs          = menu->children();
    auto *connectedAction = dynamic_cast<QAction *>(childs[netDisconnPos]);
    connectedAction->setChecked(network_is_connected(i));

    menu->setTitle(tr("&NIC %1 (%2) %3").arg(QString::number(i + 1), netType, devName));
    menu->setToolTip(tr("NIC %1 (%2) %3").arg(QString::number(i + 1), netType, devName));
}

QString
MediaMenu::getMediaOpenDirectory()
{
    static bool firstCall = true;
    QString     openDirectory;

    if (open_dir_usr_path > 0 && firstCall) {
        openDirectory = QString::fromUtf8(usr_path);
        firstCall     = false;
    }

    return openDirectory;
}

// callbacks from 86box C code
extern "C" {
void
cassette_mount(char *fn, uint8_t wp)
{
    MediaMenu::ptr->cassetteMount(QString(fn), wp);
}

void
cassette_eject(void)
{
    MediaMenu::ptr->cassetteEject();
}

void
cartridge_mount(uint8_t id, char *fn, uint8_t wp)
{
    MediaMenu::ptr->cartridgeMount(id, QString(fn));
}

void
cartridge_eject(uint8_t id)
{
    MediaMenu::ptr->cartridgeEject(id);
}

void
floppy_mount(uint8_t id, char *fn, uint8_t wp)
{
    MediaMenu::ptr->floppyMount(id, QString(fn), wp);
}

void
floppy_eject(uint8_t id)
{
    MediaMenu::ptr->floppyEject(id);
}

void
cdrom_mount(uint8_t id, char *fn)
{
    MediaMenu::ptr->cdromMount(id, QString(fn));
}

void
plat_cdrom_ui_update(uint8_t id, uint8_t reload)
{
    emit MediaMenu::ptr->onCdromUpdateUi(id);
}

void
rdisk_eject(uint8_t id)
{
    MediaMenu::ptr->rdiskEject(id);
}

void
rdisk_mount(uint8_t id, char *fn, uint8_t wp)
{
    MediaMenu::ptr->rdiskMount(id, QString(fn), wp);
}

void
rdisk_reload(uint8_t id)
{
    MediaMenu::ptr->rdiskReloadPrev(id);
}

void
mo_eject(uint8_t id)
{
    MediaMenu::ptr->moEject(id);
}

void
mo_mount(uint8_t id, char *fn, uint8_t wp)
{
    MediaMenu::ptr->moMount(id, QString(fn), wp);
}

void
mo_reload(uint8_t id)
{
    MediaMenu::ptr->moReloadPrev(id);
}
}
