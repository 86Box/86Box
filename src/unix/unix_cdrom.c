/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Handle the platform-side of CDROM/ZIP/MO drives.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2017-2018 Fred N. van Kempen.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/cassette.h>
#include <86box/cartridge.h>
#include <86box/fdd.h>
#include <86box/hdd.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/cdrom_image.h>
#include <86box/mo.h>
#include <86box/zip.h>
#include <86box/scsi_disk.h>
#include <86box/plat.h>
#include <86box/ui.h>

void
cassette_mount(char *fn, uint8_t wp)
{
    pc_cas_set_fname(cassette, NULL);
    memset(cassette_fname, 0, sizeof(cassette_fname));
    cassette_ui_writeprot = wp;
    pc_cas_set_fname(cassette, fn);
    if (fn != NULL)
        memcpy(cassette_fname, fn, MIN(511, strlen(fn)));
    ui_sb_update_icon_state(SB_CASSETTE, (fn == NULL) ? 1 : 0);
#if 0
    media_menu_update_cassette();
#endif
    ui_sb_update_tip(SB_CASSETTE);
    config_save();
}

void
cassette_eject(void)
{
    pc_cas_set_fname(cassette, NULL);
    memset(cassette_fname, 0x00, sizeof(cassette_fname));
    ui_sb_update_icon_state(SB_CASSETTE, 1);
#if 0
    media_menu_update_cassette();
#endif
    ui_sb_update_tip(SB_CASSETTE);
    config_save();
}

void
cartridge_mount(uint8_t id, char *fn, UNUSED(uint8_t wp))
{
    cart_close(id);
    cart_load(id, fn);
    ui_sb_update_icon_state(SB_CARTRIDGE | id, strlen(cart_fns[id]) ? 0 : 1);
#if 0
    media_menu_update_cartridge(id);
#endif
    ui_sb_update_tip(SB_CARTRIDGE | id);
    config_save();
}

void
cartridge_eject(uint8_t id)
{
    cart_close(id);
    ui_sb_update_icon_state(SB_CARTRIDGE | id, 1);
#if 0
    media_menu_update_cartridge(id);
#endif
    ui_sb_update_tip(SB_CARTRIDGE | id);
    config_save();
}

void
floppy_mount(uint8_t id, char *fn, uint8_t wp)
{
    fdd_close(id);
    ui_writeprot[id] = wp;
    fdd_load(id, fn);
    ui_sb_update_icon_state(SB_FLOPPY | id, strlen(floppyfns[id]) ? 0 : 1);
#if 0
    media_menu_update_floppy(id);
#endif
    ui_sb_update_tip(SB_FLOPPY | id);
    config_save();
}

void
floppy_eject(uint8_t id)
{
    fdd_close(id);
    ui_sb_update_icon_state(SB_FLOPPY | id, 1);
#if 0
    media_menu_update_floppy(id);
#endif
    ui_sb_update_tip(SB_FLOPPY | id);
    config_save();
}

void
plat_cdrom_ui_update(uint8_t id, UNUSED(uint8_t reload))
{
    cdrom_t *drv = &cdrom[id];

    if (drv->image_path[0] == 0x00) {
        ui_sb_update_icon_state(SB_CDROM | id, 1);
    } else {
        ui_sb_update_icon_state(SB_CDROM | id, 0);
    }

#if 0
    media_menu_update_cdrom(id);
#endif
    ui_sb_update_tip(SB_CDROM | id);
}

void
cdrom_mount(uint8_t id, char *fn)
{
    strcpy(cdrom[id].prev_image_path, cdrom[id].image_path);
    if (cdrom[id].ops && cdrom[id].ops->close)
        cdrom[id].ops->close(cdrom[id].local);
    cdrom[id].ops = NULL;
    memset(cdrom[id].image_path, 0, sizeof(cdrom[id].image_path));
    if ((fn != NULL) && (strlen(fn) >= 1) && (fn[strlen(fn) - 1] == '\\'))
            fn[strlen(fn) - 1] = '/';
    image_open(&(cdrom[id]), fn);
    /* Signal media change to the emulated machine. */
    if (cdrom[id].insert)
        cdrom[id].insert(cdrom[id].priv);
    if (cdrom[id].image_path[0] == 0x00)
        ui_sb_update_icon_state(SB_CDROM | id, 0);
    else
        ui_sb_update_icon_state(SB_CDROM | id, 1);
#if 0
    media_menu_update_cdrom(id);
#endif
    ui_sb_update_tip(SB_CDROM | id);
    config_save();
}

void
mo_eject(uint8_t id)
{
    mo_t *dev = (mo_t *) mo_drives[id].priv;

    mo_disk_close(dev);
    if (mo_drives[id].bus_type) {
        /* Signal disk change to the emulated machine. */
        mo_insert(dev);
    }

    ui_sb_update_icon_state(SB_MO | id, 1);
#if 0
    media_menu_update_mo(id);
#endif
    ui_sb_update_tip(SB_MO | id);
    config_save();
}

void
mo_mount(uint8_t id, char *fn, uint8_t wp)
{
    mo_t *dev = (mo_t *) mo_drives[id].priv;

    mo_disk_close(dev);
    mo_drives[id].read_only = wp;
    mo_load(dev, fn, 0);

    ui_sb_update_icon_state(SB_MO | id, strlen(mo_drives[id].image_path) ? 0 : 1);
#if 0
    media_menu_update_mo(id);
#endif
    ui_sb_update_tip(SB_MO | id);

    config_save();
}

void
mo_reload(uint8_t id)
{
    mo_t *dev = (mo_t *) mo_drives[id].priv;

    mo_disk_reload(dev);
    if (strlen(mo_drives[id].image_path) == 0) {
        ui_sb_update_icon_state(SB_MO | id, 1);
    } else {
        ui_sb_update_icon_state(SB_MO | id, 0);
    }

#if 0
    media_menu_update_mo(id);
#endif
    ui_sb_update_tip(SB_MO | id);

    config_save();
}

void
zip_eject(uint8_t id)
{
    zip_t *dev = (zip_t *) zip_drives[id].priv;

    zip_disk_close(dev);
    if (zip_drives[id].bus_type) {
        /* Signal disk change to the emulated machine. */
        zip_insert(dev);
    }

    ui_sb_update_icon_state(SB_ZIP | id, 1);
#if 0
    media_menu_update_zip(id);
#endif
    ui_sb_update_tip(SB_ZIP | id);
    config_save();
}

void
zip_mount(uint8_t id, char *fn, uint8_t wp)
{
    zip_t *dev = (zip_t *) zip_drives[id].priv;

    zip_disk_close(dev);
    zip_drives[id].read_only = wp;
    zip_load(dev, fn, 0);

    ui_sb_update_icon_state(SB_ZIP | id, strlen(zip_drives[id].image_path) ? 0 : 1);
#if 0
    media_menu_update_zip(id);
#endif
    ui_sb_update_tip(SB_ZIP | id);

    config_save();
}

void
zip_reload(uint8_t id)
{
    zip_t *dev = (zip_t *) zip_drives[id].priv;

    zip_disk_reload(dev);
    if (strlen(zip_drives[id].image_path) == 0) {
        ui_sb_update_icon_state(SB_ZIP | id, 1);
    } else {
        ui_sb_update_icon_state(SB_ZIP | id, 0);
    }

#if 0
    media_menu_update_zip(id);
#endif
    ui_sb_update_tip(SB_ZIP | id);

    config_save();
}
