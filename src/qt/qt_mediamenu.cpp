#include "qt_mediamenu.hpp"

#include "qt_machinestatus.hpp"

#include <QMenu>
#include <QFileDialog>
#include <QMessageBox>

extern "C" {
#include <86box/config.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/cassette.h>
#include <86box/cartridge.h>
#include <86box/fdd.h>
#include <86box/fdd_86f.h>
#include <86box/cdrom.h>
#include <86box/scsi_device.h>
#include <86box/zip.h>
#include <86box/mo.h>
#include <86box/sound.h>
#include <86box/ui.h>
};

#include "qt_newfloppydialog.hpp"

std::shared_ptr<MediaMenu> MediaMenu::ptr;

MediaMenu::MediaMenu(QWidget* parent) : QObject(parent) {
    parentWidget = parent;
}

void MediaMenu::refresh(QMenu *parentMenu) {
    parentMenu->clear();

    if(MachineStatus::hasCassette()) {
        cassetteMenu = parentMenu->addMenu("");
        cassetteMenu->addAction("New Image", [this]() { cassetteNewImage(); });
        cassetteMenu->addSeparator();
        cassetteMenu->addAction("Existing Image", [this]() { cassetteSelectImage(false); });
        cassetteMenu->addAction("Existing Image (Write Protected)", [this]() { cassetteSelectImage(true); });
        cassetteMenu->addSeparator();
        cassetteRecordPos = cassetteMenu->children().count();
        cassetteMenu->addAction("Record", [this] { pc_cas_set_mode(cassette, 1); cassetteUpdateMenu(); })->setCheckable(true);
        cassettePlayPos = cassetteMenu->children().count();
        cassetteMenu->addAction("Play", [this] { pc_cas_set_mode(cassette, 0); cassetteUpdateMenu(); })->setCheckable(true);
        cassetteRewindPos = cassetteMenu->children().count();
        cassetteMenu->addAction("Rewind", [] { pc_cas_rewind(cassette); });
        cassetteFastFwdPos = cassetteMenu->children().count();
        cassetteMenu->addAction("Fast Forward", [] { pc_cas_append(cassette); });
        cassetteMenu->addSeparator();
        cassetteEjectPos = cassetteMenu->children().count();
        cassetteMenu->addAction("Eject", [this]() { cassetteEject(); });
        cassetteUpdateMenu();
    }

    cartridgeMenus.clear();
    if (MachineStatus::hasCartridge()) {
        for(int i = 0; i < 2; i++) {
            auto* menu = parentMenu->addMenu("");
            menu->addAction("Image", [this, i]() { cartridgeSelectImage(i); });
            menu->addSeparator();
            cartridgeEjectPos = menu->children().count();
            menu->addAction("Eject", [this, i]() { cartridgeEject(i); });
            cartridgeMenus[i] = menu;
            cartridgeUpdateMenu(i);
        }
    }

    floppyMenus.clear();
    MachineStatus::iterateFDD([this, parentMenu](int i) {
        auto* menu = parentMenu->addMenu("");
        menu->addAction("New Image", [this, i]() { floppyNewImage(i); });
        menu->addSeparator();
        menu->addAction("Existing Image", [this, i]() { floppySelectImage(i, false); });
        menu->addAction("Existing Image (Write Protected)", [this, i]() { floppySelectImage(i, true); });
        menu->addSeparator();
        floppyExportPos = menu->children().count();
        menu->addAction("Export to 86F", [this, i]() { floppyExportTo86f(i); });
        menu->addSeparator();
        floppyEjectPos = menu->children().count();
        menu->addAction("Eject", [this, i]() { floppyEject(i); });
        floppyMenus[i] = menu;
        floppyUpdateMenu(i);
    });

    cdromMenus.clear();
    MachineStatus::iterateCDROM([this, parentMenu](int i) {
        auto* menu = parentMenu->addMenu("");
        cdromMutePos = menu->children().count();
        menu->addAction("Mute", [this, i]() { cdromMute(i); })->setCheckable(true);
        menu->addSeparator();
        cdromEmptyPos = menu->children().count();
        menu->addAction("Empty", [this, i]() { cdromEject(i); })->setCheckable(true);
        cdromReloadPos = menu->children().count();
        menu->addAction("Reload previous image", [this, i]() { cdromReload(i); });
        menu->addSeparator();
        cdromImagePos = menu->children().count();
        menu->addAction("Image", [this, i]() { cdromMount(i); })->setCheckable(true);
        cdromMenus[i] = menu;
        cdromUpdateMenu(i);
    });

    zipMenus.clear();
    MachineStatus::iterateZIP([this, parentMenu](int i) {
        auto* menu = parentMenu->addMenu("");
        menu->addAction("New Image", [this, i]() { zipNewImage(i); });
        menu->addSeparator();
        menu->addAction("Existing Image", [this, i]() { zipSelectImage(i, false); });
        menu->addAction("Existing Image (Write Protected)", [this, i]() { zipSelectImage(i, true); });
        menu->addSeparator();
        zipEjectPos = menu->children().count();
        menu->addAction("Eject", [this, i]() { zipEject(i); });
        zipReloadPos = menu->children().count();
        menu->addAction("Reload previous image", [this, i]() { zipReload(i); });
        zipMenus[i] = menu;
        zipUpdateMenu(i);
    });

    moMenus.clear();
    MachineStatus::iterateMO([this, parentMenu](int i) {
        auto* menu = parentMenu->addMenu("");
        menu->addAction("New Image", [this, i]() { moNewImage(i); });
        menu->addSeparator();
        menu->addAction("Existing Image", [this, i]() { moSelectImage(i, false); });
        menu->addAction("Existing Image (Write Protected)", [this, i]() { moSelectImage(i, true); });
        menu->addSeparator();
        moEjectPos = menu->children().count();
        menu->addAction("Eject", [this, i]() { moEject(i); });
        moReloadPos = menu->children().count();
        menu->addAction("Reload previous image", [this, i]() { moReload(i); });
        moMenus[i] = menu;
        moUpdateMenu(i);
    });
}

void MediaMenu::cassetteNewImage() {
    auto filename = QFileDialog::getSaveFileName(parentWidget, "Create...");
    QFileInfo fileinfo(filename);
    if (fileinfo.suffix().isEmpty()) {
        filename.append(".cas");
    }
    cassetteMount(filename, false);
}

void MediaMenu::cassetteSelectImage(bool wp) {
    auto filename = QFileDialog::getOpenFileName(parentWidget, "Open", QString(), "Cassette images (*.pcm;*.raw;*.wav;*.cas);;All files (*)");
    cassetteMount(filename, wp);
}

void MediaMenu::cassetteMount(const QString& filename, bool wp) {
    pc_cas_set_fname(cassette, nullptr);
    memset(cassette_fname, 0, sizeof(cassette_fname));
    cassette_ui_writeprot = wp ? 1 : 0;

    if (! filename.isEmpty()) {
        QByteArray filenameBytes = filename.toUtf8();
        strncpy(cassette_fname, filenameBytes.data(), sizeof(cassette_fname));
        pc_cas_set_fname(cassette, cassette_fname);
    }

    ui_sb_update_icon_state(SB_CASSETTE, filename.isEmpty() ? 1 : 0);
    cassetteUpdateMenu();
    ui_sb_update_tip(SB_CASSETTE);
    config_save();
}

void MediaMenu::cassetteEject() {
    pc_cas_set_fname(cassette, nullptr);
    memset(cassette_fname, 0, sizeof(cassette_fname));
    ui_sb_update_icon_state(SB_CASSETTE, 1);
    cassetteUpdateMenu();
    ui_sb_update_tip(SB_CASSETTE);
    config_save();
}

void MediaMenu::cassetteUpdateMenu() {
    QString name = cassette_fname;
    QString mode = cassette_mode;
    auto childs = cassetteMenu->children();
    auto* recordMenu = dynamic_cast<QAction*>(childs[cassetteRecordPos]);
    auto* playMenu = dynamic_cast<QAction*>(childs[cassettePlayPos]);
    auto* rewindMenu = dynamic_cast<QAction*>(childs[cassetteRewindPos]);
    auto* fastFwdMenu = dynamic_cast<QAction*>(childs[cassetteFastFwdPos]);
    auto* ejectMenu = dynamic_cast<QAction*>(childs[cassetteEjectPos]);

    recordMenu->setEnabled(!name.isEmpty());
    playMenu->setEnabled(!name.isEmpty());
    rewindMenu->setEnabled(!name.isEmpty());
    fastFwdMenu->setEnabled(!name.isEmpty());
    ejectMenu->setEnabled(!name.isEmpty());

    bool isSaving = mode == QStringLiteral("save");
    recordMenu->setChecked(isSaving);
    playMenu->setChecked(! isSaving);

    cassetteMenu->setTitle(QString("Cassette: %1").arg(name.isEmpty() ? "(empty)" : name));
}

void MediaMenu::cartridgeSelectImage(int i) {
    auto filename = QFileDialog::getOpenFileName(parentWidget, "Open", QString(), "Cartridge images (*.a;*.b;*.jrc);;All files (*)");
    if (filename.isEmpty()) {
        return;
    }
    cart_close(i);
    QByteArray filenameBytes = filename.toUtf8();
    cart_load(i, filenameBytes.data());

    ui_sb_update_icon_state(SB_CARTRIDGE | i, filename.isEmpty() ? 1 : 0);
    cartridgeUpdateMenu(i);
    ui_sb_update_tip(SB_CARTRIDGE | i);
    config_save();
}

void MediaMenu::cartridgeEject(int i) {
    cart_close(i);
    ui_sb_update_icon_state(SB_CARTRIDGE | i, 1);
    cartridgeUpdateMenu(i);
    ui_sb_update_tip(SB_CARTRIDGE | i);
    config_save();
}

void MediaMenu::cartridgeUpdateMenu(int i) {
    QString name = cart_fns[i];
    auto* menu = cartridgeMenus[i];
    auto childs = menu->children();
    auto* ejectMenu = dynamic_cast<QAction*>(childs[cartridgeEjectPos]);
    ejectMenu->setEnabled(!name.isEmpty());
    menu->setTitle(QString("Cartridge %1: %2").arg(QString::number(i+1), name.isEmpty() ? "(empty)" : name));
}

void MediaMenu::floppyNewImage(int i) {
    NewFloppyDialog dialog(NewFloppyDialog::MediaType::Floppy, parentWidget);
    switch (dialog.exec()) {
    case QDialog::Accepted:
        QByteArray filename = dialog.fileName().toUtf8();
        floppyMount(i, filename, false);
        break;
    }
}

void MediaMenu::floppySelectImage(int i, bool wp) {
    auto filename = QFileDialog::getOpenFileName(parentWidget, "Open", QString(), "All images (*.0??;*.1??;*.??0;*.86F;*.BIN;*.CQ?;*.D??;*.FLP;*.HDM;*.IM?;*.JSON;*.TD0;*.*FD?;*.MFM;*.XDF);;Advanced sector images (*.IMD;*.JSON;*.TD0);;Basic sector images (*.0??;*.1??;*.??0;*.BIN;*.CQ?;*.D??;*.FLP;*.HDM;*.IM?;*.XDF;*.*FD?);;Flux images (*.FDI);;Surface images (*.86F;*.MFM);;All files (*)");
    floppyMount(i, filename, wp);
}

void MediaMenu::floppyMount(int i, const QString &filename, bool wp) {
    fdd_close(i);
    ui_writeprot[i] = wp ? 1 : 0;
    if (! filename.isEmpty()) {
        QByteArray filenameBytes = filename.toUtf8();
        fdd_load(i, filenameBytes.data());
    }
    ui_sb_update_icon_state(SB_FLOPPY | i, filename.isEmpty() ? 1 : 0);
    floppyUpdateMenu(i);
    ui_sb_update_tip(SB_FLOPPY | i);
    config_save();
}

void MediaMenu::floppyEject(int i) {
    fdd_close(i);
    ui_sb_update_icon_state(SB_FLOPPY | i, 1);
    floppyUpdateMenu(i);
    ui_sb_update_tip(SB_FLOPPY | i);
    config_save();
}

void MediaMenu::floppyExportTo86f(int i) {
    auto filename = QFileDialog::getSaveFileName(parentWidget, "Save as 86f", QString(), "Surface images (*.86f)");
    if (! filename.isEmpty()) {
        QByteArray filenameBytes = filename.toUtf8();
        plat_pause(1);
        if (d86f_export(i, filenameBytes.data()) == 0) {
            QMessageBox::critical(parentWidget, "Unable to write file", "Make sure the file is being saved to a writable directory");
        }
        plat_pause(0);
    }
}

void MediaMenu::floppyUpdateMenu(int i) {
    QString name = floppyfns[i];

    auto* menu = floppyMenus[i];
    auto childs = menu->children();

    auto* ejectMenu = dynamic_cast<QAction*>(childs[floppyEjectPos]);
    auto* exportMenu = dynamic_cast<QAction*>(childs[floppyExportPos]);
    ejectMenu->setEnabled(!name.isEmpty());
    exportMenu->setEnabled(!name.isEmpty());

    int type = fdd_get_type(i);
    floppyMenus[i]->setTitle(QString("Floppy %1 (%2): %3").arg(QString::number(i+1), fdd_getname(type), name.isEmpty() ? "(empty)" : name));
}

void MediaMenu::cdromMute(int i) {
    cdrom[i].sound_on ^= 1;
    config_save();
    cdromUpdateMenu(i);
    sound_cd_thread_reset();
}

void MediaMenu::cdromMount(int i) {
    QString dir;
    QFileInfo fi(cdrom[i].image_path);

    auto filename = QFileDialog::getOpenFileName(parentWidget, "CD-ROM images (*.ISO;*.CUE)\0*.ISO;*.CUE\0All files (*.*)\0*.*\0", fi.canonicalPath());
    if (filename.isEmpty()) {
        auto* imageMenu = dynamic_cast<QAction*>(cdromMenus[i]->children()[cdromImagePos]);
        imageMenu->setChecked(false);
        return;
    }
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
    cdromUpdateMenu(i);
    ui_sb_update_tip(SB_CDROM | i);
    config_save();
}

void MediaMenu::cdromEject(int i) {
    cdrom_eject(i);
    cdromUpdateMenu(i);
    ui_sb_update_tip(SB_CDROM | i);
}

void MediaMenu::cdromReload(int i) {
    cdrom_reload(i);
    cdromUpdateMenu(i);
    ui_sb_update_tip(SB_CDROM | i);
}

void MediaMenu::cdromUpdateMenu(int i) {
    QString name = cdrom[i].image_path;
    auto* menu = cdromMenus[i];
    auto childs = menu->children();

    auto* muteMenu = dynamic_cast<QAction*>(childs[cdromMutePos]);
    muteMenu->setChecked(cdrom[i].sound_on == 0);

    auto* imageMenu = dynamic_cast<QAction*>(childs[cdromImagePos]);
    auto* emptyMenu = dynamic_cast<QAction*>(childs[cdromEmptyPos]);
    imageMenu->setChecked(cdrom[i].host_drive == 200);
    emptyMenu->setChecked(cdrom[i].host_drive != 200);

    auto* prevMenu = dynamic_cast<QAction*>(childs[cdromReloadPos]);
    prevMenu->setEnabled(cdrom[i].prev_host_drive != 0);

    QString busName = "Unknown Bus";
    switch (cdrom[i].bus_type) {
    case CDROM_BUS_ATAPI:
        busName = "ATAPI";
        break;
    case CDROM_BUS_SCSI:
        busName = "SCSI";
        break;
    }

    menu->setTitle(QString("CD-ROM %1 (%2): %3").arg(QString::number(i+1), busName, name.isEmpty() ? "(empty)" : name));
}

void MediaMenu::zipNewImage(int i) {
    NewFloppyDialog dialog(NewFloppyDialog::MediaType::Zip, parentWidget);
    switch (dialog.exec()) {
    case QDialog::Accepted:
        QByteArray filename = dialog.fileName().toUtf8();
        zipMount(i, filename, false);
        break;
    }
}

void MediaMenu::zipSelectImage(int i, bool wp) {
    auto filename = QFileDialog::getOpenFileName(parentWidget, "Open", QString(), "ZIP images (*.im?;*.zdi);;All files (*)");
    zipMount(i, filename, wp);
}

void MediaMenu::zipMount(int i, const QString &filename, bool wp) {
    zip_t *dev = (zip_t *) zip_drives[i].priv;

    zip_disk_close(dev);
    zip_drives[i].read_only = wp;
    if (! filename.isEmpty()) {
        QByteArray filenameBytes = filename.toUtf8();
        zip_load(dev, filenameBytes.data());
        zip_insert(dev);
    }

    ui_sb_update_icon_state(SB_ZIP | i, filename.isEmpty() ? 1 : 0);
    zipUpdateMenu(i);
    ui_sb_update_tip(SB_ZIP | i);

    config_save();
}

void MediaMenu::zipEject(int i) {
    zip_t *dev = (zip_t *) zip_drives[i].priv;

    zip_disk_close(dev);
    if (zip_drives[i].bus_type) {
        /* Signal disk change to the emulated machine. */
        zip_insert(dev);
    }

    ui_sb_update_icon_state(SB_ZIP | i, 1);
    zipUpdateMenu(i);
    ui_sb_update_tip(SB_ZIP | i);
    config_save();
}

void MediaMenu::zipReload(int i) {
    zip_t *dev = (zip_t *) zip_drives[i].priv;

    zip_disk_reload(dev);
    if (strlen(zip_drives[i].image_path) == 0) {
        ui_sb_update_icon_state(SB_ZIP|i, 1);
    } else {
        ui_sb_update_icon_state(SB_ZIP|i, 0);
    }

    zipUpdateMenu(i);
    ui_sb_update_tip(SB_ZIP|i);

    config_save();
}

void MediaMenu::zipUpdateMenu(int i) {
    QString name = zip_drives[i].image_path;
    QString prev_name = zip_drives[i].prev_image_path;
    auto* menu = zipMenus[i];
    auto childs = menu->children();

    auto* ejectMenu = dynamic_cast<QAction*>(childs[zipEjectPos]);
    auto* reloadMenu = dynamic_cast<QAction*>(childs[zipReloadPos]);
    ejectMenu->setEnabled(!name.isEmpty());
    reloadMenu->setEnabled(!prev_name.isEmpty());

    QString busName = "Unknown Bus";
    switch (zip_drives[i].bus_type) {
    case ZIP_BUS_ATAPI:
        busName = "ATAPI";
        break;
    case ZIP_BUS_SCSI:
        busName = "SCSI";
        break;
    }

    menu->setTitle(QString("ZIP %1 %2 (%3): %4").arg((zip_drives[i].is_250 > 0) ? "250" : "100", QString::number(i+1), busName, name.isEmpty() ? "(empty)" : name));
}

void MediaMenu::moNewImage(int i) {
    NewFloppyDialog dialog(NewFloppyDialog::MediaType::Mo, parentWidget);
    switch (dialog.exec()) {
    case QDialog::Accepted:
        QByteArray filename = dialog.fileName().toUtf8();
        moMount(i, filename, false);
        break;
    }
}

void MediaMenu::moSelectImage(int i, bool wp) {
    auto filename = QFileDialog::getOpenFileName(parentWidget, "Open", QString(), "MO images (*.im?;*.mdi);;All files (*)");
    moMount(i, filename, wp);
}

void MediaMenu::moMount(int i, const QString &filename, bool wp) {
    mo_t *dev = (mo_t *) mo_drives[i].priv;

    mo_disk_close(dev);
    mo_drives[i].read_only = wp;
    if (! filename.isEmpty()) {
        QByteArray filenameBytes = filename.toUtf8();
        mo_load(dev, filenameBytes.data());
        mo_insert(dev);
    }

    ui_sb_update_icon_state(SB_MO | i, filename.isEmpty() ? 1 : 0);
    moUpdateMenu(i);
    ui_sb_update_tip(SB_MO | i);

    config_save();
}

void MediaMenu::moEject(int i) {
    mo_t *dev = (mo_t *) mo_drives[i].priv;

    mo_disk_close(dev);
    if (mo_drives[i].bus_type) {
        /* Signal disk change to the emulated machine. */
        mo_insert(dev);
    }

    ui_sb_update_icon_state(SB_MO | i, 1);
    moUpdateMenu(i);
    ui_sb_update_tip(SB_MO | i);
    config_save();
}

void MediaMenu::moReload(int i) {
    mo_t *dev = (mo_t *) mo_drives[i].priv;

    mo_disk_reload(dev);
    if (strlen(mo_drives[i].image_path) == 0) {
        ui_sb_update_icon_state(SB_MO|i, 1);
    } else {
        ui_sb_update_icon_state(SB_MO|i, 0);
    }

    moUpdateMenu(i);
    ui_sb_update_tip(SB_MO|i);

    config_save();
}

void MediaMenu::moUpdateMenu(int i) {
    QString name = mo_drives[i].image_path;
    QString prev_name = mo_drives[i].prev_image_path;
    auto* menu = moMenus[i];
    auto childs = menu->children();

    auto* ejectMenu = dynamic_cast<QAction*>(childs[moEjectPos]);
    auto* reloadMenu = dynamic_cast<QAction*>(childs[moReloadPos]);
    ejectMenu->setEnabled(!name.isEmpty());
    reloadMenu->setEnabled(!prev_name.isEmpty());

    QString busName = "Unknown Bus";
    switch (mo_drives[i].bus_type) {
    case MO_BUS_ATAPI:
        busName = "ATAPI";
        break;
    case MO_BUS_SCSI:
        busName = "SCSI";
        break;
    }

    menu->setTitle(QString("MO %1 (%2): %3").arg(QString::number(i+1), busName, name.isEmpty() ? "(empty)" : name));
}


// callbacks from 86box C code
extern "C" {

void zip_eject(uint8_t id) {
    MediaMenu::ptr->zipEject(id);
}

void zip_reload(uint8_t id) {
    MediaMenu::ptr->zipReload(id);
}

void mo_eject(uint8_t id) {
    MediaMenu::ptr->moEject(id);
}

void mo_reload(uint8_t id) {
    MediaMenu::ptr->moReload(id);
}

}
