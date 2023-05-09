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
 *
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
#include <QFileDialog>
#include <QMessageBox>
#include <QStringBuilder>
#include <QApplication>
#include <QStyle>

extern "C" {
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
#include <86box/zip.h>
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

std::shared_ptr<MediaMenu> MediaMenu::ptr;

MediaMenu::MediaMenu(QWidget *parent)
    : QObject(parent)
{
    parentWidget = parent;
}

void
MediaMenu::refresh(QMenu *parentMenu)
{
    parentMenu->clear();

    if (MachineStatus::hasCassette()) {
        cassetteMenu = parentMenu->addMenu("");
        cassetteMenu->addAction(tr("&New image..."), [this]() { cassetteNewImage(); });
        cassetteMenu->addSeparator();
        cassetteMenu->addAction(tr("&Existing image..."), [this]() { cassetteSelectImage(false); });
        cassetteMenu->addAction(tr("Existing image (&Write-protected)..."), [this]() { cassetteSelectImage(true); });
        cassetteMenu->addSeparator();
        cassetteRecordPos = cassetteMenu->children().count();
        cassetteMenu->addAction(tr("&Record"), [this] { pc_cas_set_mode(cassette, 1); cassetteUpdateMenu(); })->setCheckable(true);
        cassettePlayPos = cassetteMenu->children().count();
        cassetteMenu->addAction(tr("&Play"), [this] { pc_cas_set_mode(cassette, 0); cassetteUpdateMenu(); })->setCheckable(true);
        cassetteRewindPos = cassetteMenu->children().count();
        cassetteMenu->addAction(tr("&Rewind to the beginning"), [] { pc_cas_rewind(cassette); });
        cassetteFastFwdPos = cassetteMenu->children().count();
        cassetteMenu->addAction(tr("&Fast forward to the end"), [] { pc_cas_append(cassette); });
        cassetteMenu->addSeparator();
        cassetteEjectPos = cassetteMenu->children().count();
        cassetteMenu->addAction(tr("E&ject"), [this]() { cassetteEject(); });
        cassetteUpdateMenu();
    }

    cartridgeMenus.clear();
    if (machine_has_cartridge(machine)) {
        for (int i = 0; i < 2; i++) {
            auto *menu = parentMenu->addMenu("");
            menu->addAction(tr("&Image..."), [this, i]() { cartridgeSelectImage(i); });
            menu->addSeparator();
            cartridgeEjectPos = menu->children().count();
            menu->addAction(tr("E&ject"), [this, i]() { cartridgeEject(i); });
            cartridgeMenus[i] = menu;
            cartridgeUpdateMenu(i);
        }
    }

    floppyMenus.clear();
    MachineStatus::iterateFDD([this, parentMenu](int i) {
        auto *menu = parentMenu->addMenu("");
        menu->addAction(tr("&New image..."), [this, i]() { floppyNewImage(i); });
        menu->addSeparator();
        menu->addAction(tr("&Existing image..."), [this, i]() { floppySelectImage(i, false); });
        menu->addAction(tr("Existing image (&Write-protected)..."), [this, i]() { floppySelectImage(i, true); });
        menu->addSeparator();
        for (int slot = 0; slot < MAX_PREV_IMAGES; slot++) {
            floppyImageHistoryPos[slot] = menu->children().count();
            menu->addAction(QString::asprintf(tr("Image %i").toUtf8().constData(), slot), [this, i, slot]() { floppyMenuSelect(i, slot); })->setCheckable(false);
        }
        menu->addSeparator();
        floppyExportPos = menu->children().count();
        menu->addAction(tr("E&xport to 86F..."), [this, i]() { floppyExportTo86f(i); });
        menu->addSeparator();
        floppyEjectPos = menu->children().count();
        menu->addAction(tr("E&ject"), [this, i]() { floppyEject(i); });
        floppyMenus[i] = menu;
        floppyUpdateMenu(i);
    });

    cdromMenus.clear();
    MachineStatus::iterateCDROM([this, parentMenu](int i) {
        auto *menu   = parentMenu->addMenu("");
        cdromMutePos = menu->children().count();
        menu->addAction(QApplication::style()->standardIcon(QStyle::SP_MediaVolumeMuted), tr("&Mute"), [this, i]() { cdromMute(i); })->setCheckable(true);
        menu->addSeparator();
        menu->addAction(ProgSettings::loadIcon("/cdrom.ico"), tr("&Image..."), [this, i]() { cdromMount(i, 0); })->setCheckable(false);
        menu->addAction(QApplication::style()->standardIcon(QStyle::SP_DirIcon), tr("&Folder..."), [this, i]() { cdromMount(i, 1); })->setCheckable(false);
        menu->addSeparator();
        for (int slot = 0; slot < MAX_PREV_IMAGES; slot++) {
            cdromImageHistoryPos[slot] = menu->children().count();
            menu->addAction(QString::asprintf(tr("Image %i").toUtf8().constData(), slot), [this, i, slot]() { cdromReload(i, slot); })->setCheckable(false);
        }
        menu->addSeparator();
        cdromImagePos = menu->children().count();
        cdromDirPos   = menu->children().count();
        menu->addAction(tr("E&ject"), [this, i]() { cdromEject(i); })->setCheckable(false);
        cdromMenus[i] = menu;
        cdromUpdateMenu(i);
    });

    zipMenus.clear();
    MachineStatus::iterateZIP([this, parentMenu](int i) {
        auto *menu = parentMenu->addMenu("");
        menu->addAction(tr("&New image..."), [this, i]() { zipNewImage(i); });
        menu->addSeparator();
        menu->addAction(tr("&Existing image..."), [this, i]() { zipSelectImage(i, false); });
        menu->addAction(tr("Existing image (&Write-protected)..."), [this, i]() { zipSelectImage(i, true); });
        menu->addSeparator();
        zipEjectPos = menu->children().count();
        menu->addAction(tr("E&ject"), [this, i]() { zipEject(i); });
        zipReloadPos = menu->children().count();
        menu->addAction(tr("&Reload previous image"), [this, i]() { zipReload(i); });
        zipMenus[i] = menu;
        zipUpdateMenu(i);
    });

    moMenus.clear();
    MachineStatus::iterateMO([this, parentMenu](int i) {
        auto *menu = parentMenu->addMenu("");
        menu->addAction(tr("&New image..."), [this, i]() { moNewImage(i); });
        menu->addSeparator();
        menu->addAction(tr("&Existing image..."), [this, i]() { moSelectImage(i, false); });
        menu->addAction(tr("Existing image (&Write-protected)..."), [this, i]() { moSelectImage(i, true); });
        menu->addSeparator();
        moEjectPos = menu->children().count();
        menu->addAction(tr("E&ject"), [this, i]() { moEject(i); });
        moReloadPos = menu->children().count();
        menu->addAction(tr("&Reload previous image"), [this, i]() { moReload(i); });
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
    parentMenu->addAction(tr("Clear image history"), [this]() { clearImageHistory(); });
}

void
MediaMenu::cassetteNewImage()
{
    auto      filename = QFileDialog::getSaveFileName(parentWidget, tr("Create..."));
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
MediaMenu::cassetteMount(const QString &filename, bool wp)
{
    pc_cas_set_fname(cassette, nullptr);
    memset(cassette_fname, 0, sizeof(cassette_fname));
    cassette_ui_writeprot = wp ? 1 : 0;

    if (!filename.isEmpty()) {
        QByteArray filenameBytes = filename.toUtf8();
        strncpy(cassette_fname, filenameBytes.data(), sizeof(cassette_fname) - 1);
        pc_cas_set_fname(cassette, cassette_fname);
    }

    ui_sb_update_icon_state(SB_CASSETTE, filename.isEmpty() ? 1 : 0);
    cassetteUpdateMenu();
    ui_sb_update_tip(SB_CASSETTE);
    config_save();
}

void
MediaMenu::cassetteEject()
{
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
    QString name        = cassette_fname;
    QString mode        = cassette_mode;
    auto    childs      = cassetteMenu->children();
    auto   *recordMenu  = dynamic_cast<QAction *>(childs[cassetteRecordPos]);
    auto   *playMenu    = dynamic_cast<QAction *>(childs[cassettePlayPos]);
    auto   *rewindMenu  = dynamic_cast<QAction *>(childs[cassetteRewindPos]);
    auto   *fastFwdMenu = dynamic_cast<QAction *>(childs[cassetteFastFwdPos]);
    auto   *ejectMenu   = dynamic_cast<QAction *>(childs[cassetteEjectPos]);

    recordMenu->setEnabled(!name.isEmpty());
    playMenu->setEnabled(!name.isEmpty());
    rewindMenu->setEnabled(!name.isEmpty());
    fastFwdMenu->setEnabled(!name.isEmpty());
    ejectMenu->setEnabled(!name.isEmpty());

    bool isSaving = mode == QStringLiteral("save");
    recordMenu->setChecked(isSaving);
    playMenu->setChecked(!isSaving);

    cassetteMenu->setTitle(QString::asprintf(tr("Cassette: %s").toUtf8().constData(), (name.isEmpty() ? tr("(empty)") : name).toUtf8().constData()));
}

void
MediaMenu::cartridgeMount(int i, const QString &filename)
{
    cart_close(i);
    QByteArray filenameBytes = filename.toUtf8();
    cart_load(i, filenameBytes.data());

    ui_sb_update_icon_state(SB_CARTRIDGE | i, filename.isEmpty() ? 1 : 0);
    cartridgeUpdateMenu(i);
    ui_sb_update_tip(SB_CARTRIDGE | i);
    config_save();
}

void
MediaMenu::cartridgeSelectImage(int i)
{
    auto filename = QFileDialog::getOpenFileName(
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
MediaMenu::cartridgeEject(int i)
{
    cart_close(i);
    ui_sb_update_icon_state(SB_CARTRIDGE | i, 1);
    cartridgeUpdateMenu(i);
    ui_sb_update_tip(SB_CARTRIDGE | i);
    config_save();
}

void
MediaMenu::cartridgeUpdateMenu(int i)
{
    QString name      = cart_fns[i];
    auto   *menu      = cartridgeMenus[i];
    auto    childs    = menu->children();
    auto   *ejectMenu = dynamic_cast<QAction *>(childs[cartridgeEjectPos]);
    ejectMenu->setEnabled(!name.isEmpty());
    // menu->setTitle(tr("Cartridge %1: %2").arg(QString::number(i+1), name.isEmpty() ? tr("(empty)") : name));
    menu->setTitle(QString::asprintf(tr("Cartridge %i: %ls").toUtf8().constData(), i + 1, name.isEmpty() ? tr("(empty)").toStdU16String().data() : name.toStdU16String().data()));
}

void
MediaMenu::floppyNewImage(int i)
{
    NewFloppyDialog dialog(NewFloppyDialog::MediaType::Floppy, parentWidget);
    switch (dialog.exec()) {
        case QDialog::Accepted:
            QByteArray filename = dialog.fileName().toUtf8();
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

    if (!filename.isEmpty()) floppyMount(i, filename, wp);
}

void
MediaMenu::floppyMount(int i, const QString &filename, bool wp)
{
    auto previous_image = QFileInfo(floppyfns[i]);
    fdd_close(i);
    ui_writeprot[i] = wp ? 1 : 0;
    if (!filename.isEmpty()) {
        QByteArray filenameBytes = filename.toUtf8();
        fdd_load(i, filenameBytes.data());
    }
    ui_sb_update_icon_state(SB_FLOPPY | i, filename.isEmpty() ? 1 : 0);
    mhm.addImageToHistory(i, ui::MediaType::Floppy, previous_image.filePath(), filename);
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
    ejectMenu->setText(QString::asprintf(tr("Eject %s").toUtf8().constData(), name.isEmpty() ? QString().toUtf8().constData() : fi.fileName().toUtf8().constData()));
    exportMenu->setEnabled(!name.isEmpty());

    for (int slot = 0; slot < MAX_PREV_IMAGES; slot++) {
        updateImageHistory(i, slot, ui::MediaType::Floppy);
    }

    int type = fdd_get_type(i);
    // floppyMenus[i]->setTitle(tr("Floppy %1 (%2): %3").arg(QString::number(i+1), fdd_getname(type), name.isEmpty() ? tr("(empty)") : name));
    floppyMenus[i]->setTitle(QString::asprintf(tr("Floppy %i (%s): %ls").toUtf8().constData(), i + 1, fdd_getname(type), name.isEmpty() ? tr("(empty)").toStdU16String().data() : name.toStdU16String().data()));
}

void
MediaMenu::floppyMenuSelect(int index, int slot)
{
    QString filename = mhm.getImageForSlot(index, slot, ui::MediaType::Floppy);
    floppyMount(index, filename.toUtf8().constData(), false);
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
    QByteArray fn = filename.toUtf8().data();

    cdrom[i].prev_host_drive = cdrom[i].host_drive;
    strcpy(cdrom[i].prev_image_path, cdrom[i].image_path);
    if (cdrom[i].ops && cdrom[i].ops->exit)
        cdrom[i].ops->exit(&(cdrom[i]));

    cdrom[i].ops = nullptr;
    memset(cdrom[i].image_path, 0, sizeof(cdrom[i].image_path));
    cdrom_image_open(&(cdrom[i]), fn.data());
    /* Signal media change to the emulated machine. */
    if (cdrom[i].insert)
        cdrom[i].insert(cdrom[i].priv);
    cdrom[i].host_drive = (strlen(cdrom[i].image_path) == 0) ? 0 : 200;
    if (cdrom[i].host_drive == 200) {
        ui_sb_update_icon_state(SB_CDROM | i, 0);
    } else {
        ui_sb_update_icon_state(SB_CDROM | i, 1);
    }
    mhm.addImageToHistory(i, ui::MediaType::Optical, cdrom[i].prev_image_path, cdrom[i].image_path);
    cdromUpdateMenu(i);
    ui_sb_update_tip(SB_CDROM | i);
    config_save();
}

void
MediaMenu::cdromMount(int i, int dir)
{
    QString   filename;
    QFileInfo fi(cdrom[i].image_path);

    if (dir) {
        filename = QFileDialog::getExistingDirectory(
            parentWidget);
    } else {
        filename = QFileDialog::getOpenFileName(
            parentWidget,
            QString(),
            QString(),
            tr("CD-ROM images") % util::DlgFilter({ "iso", "cue" }) % tr("All files") % util::DlgFilter({ "*" }, true));
    }

    if (filename.isEmpty()) {
        return;
    }

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
    QString filename = mhm.getImageForSlot(index, slot, ui::MediaType::Optical);
    cdromMount(index, filename.toUtf8().constData());
    cdromUpdateMenu(index);
    ui_sb_update_tip(SB_CDROM | index);
}

void
MediaMenu::updateImageHistory(int index, int slot, ui::MediaType type)
{
    QMenu      *menu;
    QAction    *imageHistoryUpdatePos;
    QObjectList children;
    QFileInfo   fi;
    QIcon       menu_icon;

    switch (type) {
        case ui::MediaType::Optical:
            if (!cdromMenus.contains(index))
                return;
            menu                  = cdromMenus[index];
            children              = menu->children();
            imageHistoryUpdatePos = dynamic_cast<QAction *>(children[cdromImageHistoryPos[slot]]);
            fi.setFile(mhm.getImageForSlot(index, slot, type));
            menu_icon = fi.isDir() ? QApplication::style()->standardIcon(QStyle::SP_DirIcon) : ProgSettings::loadIcon("/cdrom.ico");
            imageHistoryUpdatePos->setIcon(menu_icon);
            break;
        case ui::MediaType::Floppy:
            if (!floppyMenus.contains(index))
                return;
            menu                  = floppyMenus[index];
            children              = menu->children();
            imageHistoryUpdatePos = dynamic_cast<QAction *>(children[floppyImageHistoryPos[slot]]);
            fi.setFile(mhm.getImageForSlot(index, slot, type));
            break;
        default:
            pclog("History not yet implemented for media type %s\n", qPrintable(mhm.mediaTypeToString(type)));
            return;
    }

    QString menu_item_name = fi.fileName().isEmpty() ? tr("previous image").toUtf8().constData() : fi.fileName().toUtf8().constData();
    imageHistoryUpdatePos->setText(QString::asprintf(tr("%s").toUtf8().constData(), menu_item_name.toUtf8().constData()));
    imageHistoryUpdatePos->setVisible(!fi.fileName().isEmpty());
    imageHistoryUpdatePos->setVisible(fi.exists());
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
    QString   name = cdrom[i].image_path;
    QFileInfo fi(cdrom[i].image_path);

    if (!cdromMenus.contains(i))
        return;
    auto *menu   = cdromMenus[i];
    auto  childs = menu->children();

    auto *muteMenu = dynamic_cast<QAction *>(childs[cdromMutePos]);
    muteMenu->setIcon(QApplication::style()->standardIcon((cdrom[i].sound_on == 0) ? QStyle::SP_MediaVolume : QStyle::SP_MediaVolumeMuted));
    muteMenu->setText((cdrom[i].sound_on == 0) ? tr("&Unmute") : tr("&Mute"));

    auto *imageMenu = dynamic_cast<QAction *>(childs[cdromImagePos]);
    imageMenu->setEnabled(!name.isEmpty());
    QString menu_item_name = name.isEmpty() ? QString().toUtf8().constData() : fi.fileName().toUtf8().constData();
    auto    menu_icon      = fi.isDir() ? QApplication::style()->standardIcon(QStyle::SP_DirIcon) : ProgSettings::loadIcon("/cdrom.ico");
    imageMenu->setIcon(menu_icon);
    imageMenu->setText(QString::asprintf(tr("Eject %s").toUtf8().constData(), menu_item_name.toUtf8().constData()));

    for (int slot = 0; slot < MAX_PREV_IMAGES; slot++) {
        updateImageHistory(i, slot, ui::MediaType::Optical);
    }

    QString busName = tr("Unknown Bus");
    switch (cdrom[i].bus_type) {
        case CDROM_BUS_ATAPI:
            busName = "ATAPI";
            break;
        case CDROM_BUS_SCSI:
            busName = "SCSI";
            break;
	    case CDROM_BUS_MITSUMI:
	        busName = "Mitsumi";
	        break;
    }

    // menu->setTitle(tr("CD-ROM %1 (%2): %3").arg(QString::number(i+1), busName, name.isEmpty() ? tr("(empty)") : name));
    menu->setTitle(QString::asprintf(tr("CD-ROM %i (%s): %s").toUtf8().constData(), i + 1, busName.toUtf8().data(), name.isEmpty() ? tr("(empty)").toUtf8().data() : name.toUtf8().data()));
}

void
MediaMenu::zipNewImage(int i)
{
    NewFloppyDialog dialog(NewFloppyDialog::MediaType::Zip, parentWidget);
    switch (dialog.exec()) {
        case QDialog::Accepted:
            QByteArray filename = dialog.fileName().toUtf8();
            zipMount(i, filename, false);
            break;
    }
}

void
MediaMenu::zipSelectImage(int i, bool wp)
{
    auto filename = QFileDialog::getOpenFileName(
        parentWidget,
        QString(),
        QString(),
        tr("ZIP images") % util::DlgFilter({ "im?", "zdi" }) % tr("All files") % util::DlgFilter({ "*" }, true));

    if (!filename.isEmpty())
        zipMount(i, filename, wp);
}

void
MediaMenu::zipMount(int i, const QString &filename, bool wp)
{
    zip_t *dev = (zip_t *) zip_drives[i].priv;

    zip_disk_close(dev);
    zip_drives[i].read_only = wp;
    if (!filename.isEmpty()) {
        QByteArray filenameBytes = filename.toUtf8();
        zip_load(dev, filenameBytes.data());
        zip_insert(dev);
    }

    ui_sb_update_icon_state(SB_ZIP | i, filename.isEmpty() ? 1 : 0);
    zipUpdateMenu(i);
    ui_sb_update_tip(SB_ZIP | i);

    config_save();
}

void
MediaMenu::zipEject(int i)
{
    zip_t *dev = (zip_t *) zip_drives[i].priv;

    zip_disk_close(dev);
    zip_drives[i].image_path[0] = 0;
    if (zip_drives[i].bus_type) {
        /* Signal disk change to the emulated machine. */
        zip_insert(dev);
    }

    ui_sb_update_icon_state(SB_ZIP | i, 1);
    zipUpdateMenu(i);
    ui_sb_update_tip(SB_ZIP | i);
    config_save();
}

void
MediaMenu::zipReload(int i)
{
    zip_t *dev = (zip_t *) zip_drives[i].priv;

    zip_disk_reload(dev);
    if (strlen(zip_drives[i].image_path) == 0) {
        ui_sb_update_icon_state(SB_ZIP | i, 1);
    } else {
        ui_sb_update_icon_state(SB_ZIP | i, 0);
    }

    zipUpdateMenu(i);
    ui_sb_update_tip(SB_ZIP | i);

    config_save();
}

void
MediaMenu::zipUpdateMenu(int i)
{
    QString name      = zip_drives[i].image_path;
    QString prev_name = zip_drives[i].prev_image_path;
    if (!zipMenus.contains(i))
        return;
    auto *menu   = zipMenus[i];
    auto  childs = menu->children();

    auto *ejectMenu  = dynamic_cast<QAction *>(childs[zipEjectPos]);
    auto *reloadMenu = dynamic_cast<QAction *>(childs[zipReloadPos]);
    ejectMenu->setEnabled(!name.isEmpty());
    reloadMenu->setEnabled(!prev_name.isEmpty());

    QString busName = tr("Unknown Bus");
    switch (zip_drives[i].bus_type) {
        case ZIP_BUS_ATAPI:
            busName = "ATAPI";
            break;
        case ZIP_BUS_SCSI:
            busName = "SCSI";
            break;
    }

    // menu->setTitle(tr("ZIP %1 %2 (%3): %4").arg((zip_drives[i].is_250 > 0) ? "250" : "100", QString::number(i+1), busName, name.isEmpty() ? tr("(empty)") : name));
    menu->setTitle(QString::asprintf(tr("ZIP %03i %i (%s): %ls").toUtf8().constData(), (zip_drives[i].is_250 > 0) ? 250 : 100, i + 1, busName.toUtf8().data(), name.isEmpty() ? tr("(empty)").toStdU16String().data() : name.toStdU16String().data()));
}

void
MediaMenu::moNewImage(int i)
{
    NewFloppyDialog dialog(NewFloppyDialog::MediaType::Mo, parentWidget);
    switch (dialog.exec()) {
        case QDialog::Accepted:
            QByteArray filename = dialog.fileName().toUtf8();
            moMount(i, filename, false);
            break;
    }
}

void
MediaMenu::moSelectImage(int i, bool wp)
{
    auto filename = QFileDialog::getOpenFileName(
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
    mo_t *dev = (mo_t *) mo_drives[i].priv;

    mo_disk_close(dev);
    mo_drives[i].read_only = wp;
    if (!filename.isEmpty()) {
        QByteArray filenameBytes = filename.toUtf8();
        mo_load(dev, filenameBytes.data());
        mo_insert(dev);
    }

    ui_sb_update_icon_state(SB_MO | i, filename.isEmpty() ? 1 : 0);
    moUpdateMenu(i);
    ui_sb_update_tip(SB_MO | i);

    config_save();
}

void
MediaMenu::moEject(int i)
{
    mo_t *dev = (mo_t *) mo_drives[i].priv;

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
MediaMenu::moReload(int i)
{
    mo_t *dev = (mo_t *) mo_drives[i].priv;

    mo_disk_reload(dev);
    if (strlen(mo_drives[i].image_path) == 0) {
        ui_sb_update_icon_state(SB_MO | i, 1);
    } else {
        ui_sb_update_icon_state(SB_MO | i, 0);
    }

    moUpdateMenu(i);
    ui_sb_update_tip(SB_MO | i);

    config_save();
}

void
MediaMenu::moUpdateMenu(int i)
{
    QString name      = mo_drives[i].image_path;
    QString prev_name = mo_drives[i].prev_image_path;
    if (!moMenus.contains(i))
        return;
    auto *menu   = moMenus[i];
    auto  childs = menu->children();

    auto *ejectMenu  = dynamic_cast<QAction *>(childs[moEjectPos]);
    auto *reloadMenu = dynamic_cast<QAction *>(childs[moReloadPos]);
    ejectMenu->setEnabled(!name.isEmpty());
    reloadMenu->setEnabled(!prev_name.isEmpty());

    QString busName = tr("Unknown Bus");
    switch (mo_drives[i].bus_type) {
        case MO_BUS_ATAPI:
            busName = "ATAPI";
            break;
        case MO_BUS_SCSI:
            busName = "SCSI";
            break;
    }

    menu->setTitle(QString::asprintf(tr("MO %i (%ls): %ls").toUtf8().constData(), i + 1, busName.toStdU16String().data(), name.isEmpty() ? tr("(empty)").toStdU16String().data() : name.toStdU16String().data()));
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

    QString netType = tr("None");
    switch (net_cards_conf[i].net_type) {
        case NET_TYPE_SLIRP:
            netType = "SLiRP";
            break;
        case NET_TYPE_PCAP:
            netType = "PCAP";
            break;
        case NET_TYPE_VDE:
            netType = "VDE";
            break;
    }

    QString devName = DeviceConfig::DeviceName(network_card_getdevice(net_cards_conf[i].device_num), network_card_get_internal_name(net_cards_conf[i].device_num), 1);

    auto *menu            = netMenus[i];
    auto  childs          = menu->children();
    auto *connectedAction = dynamic_cast<QAction *>(childs[netDisconnPos]);
    connectedAction->setChecked(network_is_connected(i));

    menu->setTitle(QString::asprintf(tr("NIC %02i (%ls) %ls").toUtf8().constData(), i + 1, netType.toStdU16String().data(), devName.toStdU16String().data()));
}

QString
MediaMenu::getMediaOpenDirectory()
{
    QString openDirectory;
    if (open_dir_usr_path > 0) {
        openDirectory = QString::fromUtf8(usr_path);
    }
    return openDirectory;
}

// callbacks from 86box C code
extern "C" {

void
zip_eject(uint8_t id)
{
    MediaMenu::ptr->zipEject(id);
}

void
zip_reload(uint8_t id)
{
    MediaMenu::ptr->zipReload(id);
}

void
mo_eject(uint8_t id)
{
    MediaMenu::ptr->moEject(id);
}

void
mo_reload(uint8_t id)
{
    MediaMenu::ptr->moReload(id);
}
}
