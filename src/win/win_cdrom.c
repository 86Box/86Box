/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handle the platform-side of CDROM/ZIP/MO drives.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#undef BITMAP
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/config.h>
#include <86box/hdd.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/mo.h>
#include <86box/zip.h>
#include <86box/scsi_disk.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/win.h>


void
plat_cdrom_ui_update(uint8_t id, uint8_t reload)
{
    cdrom_t *drv = &cdrom[id];

    if (drv->host_drive == 0) {
	ui_sb_update_icon_state(SB_CDROM|id, 1);
    } else {
	ui_sb_update_icon_state(SB_CDROM|id, 0);
    }

    media_menu_update_cdrom(id);
    ui_sb_update_tip(SB_CDROM|id);
}

void
cdrom_mount(uint8_t id, wchar_t *fn)
{
    cdrom[id].prev_host_drive = cdrom[id].host_drive;
    wcscpy(cdrom[id].prev_image_path, cdrom[id].image_path);
    if (cdrom[id].ops && cdrom[id].ops->exit)
	cdrom[id].ops->exit(&(cdrom[id]));
    cdrom[id].ops = NULL;
    memset(cdrom[id].image_path, 0, sizeof(cdrom[id].image_path));
    cdrom_image_open(&(cdrom[id]), fn);
    /* Signal media change to the emulated machine. */
    if (cdrom[id].insert)
	cdrom[id].insert(cdrom[id].priv);
    cdrom[id].host_drive = (wcslen(cdrom[id].image_path) == 0) ? 0 : 200;
    if (cdrom[id].host_drive == 200) {
	ui_sb_update_icon_state(SB_CDROM | id, 0);
    } else {
	ui_sb_update_icon_state(SB_CDROM | id, 1);
    }
    media_menu_update_cdrom(id);
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
    media_menu_update_mo(id);
    ui_sb_update_tip(SB_MO | id);
    config_save();
}


void
mo_mount(uint8_t id, wchar_t *fn, uint8_t wp)
{
    mo_t *dev = (mo_t *) mo_drives[id].priv;

    mo_disk_close(dev);
    mo_drives[id].read_only = wp;
    mo_load(dev, fn);
    mo_insert(dev);

    ui_sb_update_icon_state(SB_MO | id, wcslen(mo_drives[id].image_path) ? 0 : 1);
    media_menu_update_mo(id);
    ui_sb_update_tip(SB_MO | id);

    config_save();
}


void
mo_reload(uint8_t id)
{
    mo_t *dev = (mo_t *) mo_drives[id].priv;

    mo_disk_reload(dev);
    if (wcslen(mo_drives[id].image_path) == 0) {
	ui_sb_update_icon_state(SB_MO|id, 1);
    } else {
	ui_sb_update_icon_state(SB_MO|id, 0);
    }

    media_menu_update_mo(id);
    ui_sb_update_tip(SB_MO|id);

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
    media_menu_update_zip(id);
    ui_sb_update_tip(SB_ZIP | id);
    config_save();
}


void
zip_mount(uint8_t id, wchar_t *fn, uint8_t wp)
{
    zip_t *dev = (zip_t *) zip_drives[id].priv;

    zip_disk_close(dev);
    zip_drives[id].read_only = wp;
    zip_load(dev, fn);
    zip_insert(dev);

    ui_sb_update_icon_state(SB_ZIP | id, wcslen(zip_drives[id].image_path) ? 0 : 1);
    media_menu_update_zip(id);
    ui_sb_update_tip(SB_ZIP | id);

    config_save();
}


void
zip_reload(uint8_t id)
{
    zip_t *dev = (zip_t *) zip_drives[id].priv;

    zip_disk_reload(dev);
    if (wcslen(zip_drives[id].image_path) == 0) {
	ui_sb_update_icon_state(SB_ZIP|id, 1);
    } else {
	ui_sb_update_icon_state(SB_ZIP|id, 0);
    }

    media_menu_update_zip(id);
    ui_sb_update_tip(SB_ZIP|id);

    config_save();
}
