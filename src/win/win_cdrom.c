/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handle the platform-side of CDROM drives.
 *
 * Version:	@(#)win_cdrom.c	1.0.6	2018/03/17
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
#include "../config.h"
#include "../cdrom/cdrom.h"
#include "../cdrom/cdrom_image.h"
#include "../cdrom/cdrom_null.h"
#include "../disk/hdd.h"
#include "../disk/zip.h"
#include "../scsi/scsi.h"
#include "../scsi/scsi_disk.h"
#include "../plat.h"
#include "../ui.h"
#include "win.h"


uint8_t	host_cdrom_drive_available[26];
uint8_t	host_cdrom_drive_available_num = 0;


void
cdrom_init_host_drives(void)
{
    WCHAR s[64];
    int i = 0;

    host_cdrom_drive_available_num = 0;
    for (i='A'; i<='Z'; i++) {
	_swprintf(s, L"%c:\\", i);

	if (GetDriveType(s)==DRIVE_CDROM) {
		host_cdrom_drive_available[i - 'A'] = 1;

		host_cdrom_drive_available_num++;
	} else {
		host_cdrom_drive_available[i - 'A'] = 0;
	}
    }
}


void
cdrom_eject(uint8_t id)
{
    if (cdrom_drives[id].host_drive == 0) {
	/* Switch from empty to empty. Do nothing. */
	return;
    }

    if ((cdrom_drives[id].host_drive >= 'A') &&
	(cdrom_drives[id].host_drive <= 'Z')) {
	ui_sb_check_menu_item(SB_CDROM|id,
		IDM_CDROM_HOST_DRIVE | id | ((cdrom_drives[id].host_drive - 'A') << 3), MF_UNCHECKED);
    }

    if (cdrom_image[id].prev_image_path) {
	free(cdrom_image[id].prev_image_path);
	cdrom_image[id].prev_image_path = NULL;
    }

    if (cdrom_drives[id].host_drive == 200) {
	cdrom_image[id].prev_image_path = (wchar_t *) malloc(1024);
	wcscpy(cdrom_image[id].prev_image_path, cdrom_image[id].image_path);
    }
    cdrom_drives[id].prev_host_drive = cdrom_drives[id].host_drive;
    cdrom_drives[id].handler->exit(id);
    cdrom_close(id);
    cdrom_null_open(id, 0);
    if (cdrom_drives[id].bus_type) {
	/* Signal disc change to the emulated machine. */
	cdrom_insert(id);
    }

    ui_sb_check_menu_item(SB_CDROM|id, IDM_CDROM_IMAGE | id, MF_UNCHECKED);
    cdrom_drives[id].host_drive=0;
    ui_sb_check_menu_item(SB_CDROM|id, IDM_CDROM_EMPTY | id, MF_CHECKED);
    ui_sb_update_icon_state(SB_CDROM|id, 1);
    ui_sb_enable_menu_item(SB_CDROM|id, IDM_CDROM_RELOAD | id, MF_BYCOMMAND | MF_ENABLED);
    ui_sb_update_tip(SB_CDROM|id);

    config_save();
}


void
cdrom_reload(uint8_t id)
{
    int new_cdrom_drive;

    if ((cdrom_drives[id].host_drive == cdrom_drives[id].prev_host_drive) || (cdrom_drives[id].prev_host_drive == 0) || (cdrom_drives[id].host_drive != 0)) {
	/* Switch from empty to empty. Do nothing. */
	return;
    }

    cdrom_close(id);

    if (cdrom_drives[id].prev_host_drive == 200) {
	wcscpy(cdrom_image[id].image_path, cdrom_image[id].prev_image_path);
	free(cdrom_image[id].prev_image_path);
	cdrom_image[id].prev_image_path = NULL;
	image_open(id, cdrom_image[id].image_path);
	if (cdrom_drives[id].bus_type) {
		/* Signal disc change to the emulated machine. */
		cdrom_insert(id);
	}
	if (wcslen(cdrom_image[id].image_path) == 0) {
		ui_sb_check_menu_item(SB_CDROM|id, IDM_CDROM_EMPTY | id, MF_CHECKED);
		cdrom_drives[id].host_drive = 0;
		ui_sb_check_menu_item(SB_CDROM|id, IDM_CDROM_IMAGE | id, MF_UNCHECKED);
		ui_sb_update_icon_state(SB_CDROM|id, 1);
	} else {
		ui_sb_check_menu_item(SB_CDROM|id, IDM_CDROM_EMPTY | id, MF_UNCHECKED);
		cdrom_drives[id].host_drive = 200;
		ui_sb_check_menu_item(SB_CDROM|id, IDM_CDROM_IMAGE | id, MF_CHECKED);
		ui_sb_update_icon_state(SB_CDROM|id, 0);
	}
    } else {
	new_cdrom_drive = cdrom_drives[id].prev_host_drive;
	ioctl_open(id, new_cdrom_drive);
	if (cdrom_drives[id].bus_type) {
		/* Signal disc change to the emulated machine. */
		cdrom_insert(id);
	}
	ui_sb_check_menu_item(SB_CDROM|id, IDM_CDROM_EMPTY | id, MF_UNCHECKED);
	cdrom_drives[id].host_drive = new_cdrom_drive;
	ui_sb_check_menu_item(SB_CDROM|id, IDM_CDROM_HOST_DRIVE | id | ((cdrom_drives[id].host_drive - 'A') << 3), MF_CHECKED);
	ui_sb_update_icon_state(SB_CDROM|id, 0);
    }

    ui_sb_enable_menu_item(SB_CDROM|id, IDM_CDROM_RELOAD | id, MF_BYCOMMAND | MF_GRAYED);
    ui_sb_update_tip(SB_CDROM|id);

    config_save();
}


void
zip_eject(uint8_t id)
{
    zip_close(id);
    if (zip_drives[id].bus_type) {
	/* Signal disk change to the emulated machine. */
	zip_insert(id);
    }

    ui_sb_update_icon_state(SB_ZIP | id, 1);
    ui_sb_enable_menu_item(SB_ZIP|id, IDM_ZIP_EJECT | id, MF_BYCOMMAND | MF_GRAYED);
    ui_sb_enable_menu_item(SB_ZIP|id, IDM_ZIP_RELOAD | id, MF_BYCOMMAND | MF_ENABLED);
    ui_sb_update_tip(SB_ZIP | id);
    config_save();
}


void
zip_reload(uint8_t id)
{
    zip_disk_reload(id);
    if (wcslen(zip_drives[id].image_path) == 0) {
	ui_sb_enable_menu_item(SB_ZIP|id, IDM_ZIP_EJECT | id, MF_BYCOMMAND | MF_GRAYED);
	ui_sb_update_icon_state(SB_ZIP|id, 1);
    } else {
	ui_sb_enable_menu_item(SB_ZIP|id, IDM_ZIP_EJECT | id, MF_BYCOMMAND | MF_ENABLED);
	ui_sb_update_icon_state(SB_ZIP|id, 0);
    }

    ui_sb_enable_menu_item(SB_ZIP|id, IDM_ZIP_RELOAD | id, MF_BYCOMMAND | MF_GRAYED);
    ui_sb_update_tip(SB_ZIP|id);

    config_save();
}


void
removable_disk_unload(uint8_t id)
{
    if (wcslen(hdd[id].fn) == 0) {
	/* Switch from empty to empty. Do nothing. */
	return;
    }

    scsi_unloadhd(hdd[id].scsi_id, hdd[id].scsi_lun, id);
    scsi_disk_insert(id);
}


void
removable_disk_eject(uint8_t id)
{
    removable_disk_unload(id);
    ui_sb_update_icon_state(SB_RDISK|id, 1);
    ui_sb_enable_menu_item(SB_RDISK|id, IDM_RDISK_EJECT | id, MF_BYCOMMAND | MF_GRAYED);
    ui_sb_enable_menu_item(SB_RDISK|id, IDM_RDISK_RELOAD | id, MF_BYCOMMAND | MF_ENABLED);
    ui_sb_enable_menu_item(SB_RDISK|id, IDM_RDISK_SEND_CHANGE | id, MF_BYCOMMAND | MF_GRAYED);

    ui_sb_update_tip(SB_RDISK|id);

    config_save();
}


void
removable_disk_reload(uint8_t id)
{
    if (wcslen(hdd[id].fn) != 0) {
	/* Attempting to reload while an image is already loaded. Do nothing. */
	return;
    }

    scsi_reloadhd(id);
#if 0
    scsi_disk_insert(id);
#endif

    ui_sb_update_icon_state(SB_RDISK|id, wcslen(hdd[id].fn) ? 0 : 1);

    ui_sb_enable_menu_item(SB_RDISK|id, IDM_RDISK_EJECT | id, MF_BYCOMMAND | (wcslen(hdd[id].fn) ? MF_ENABLED : MF_GRAYED));
    ui_sb_enable_menu_item(SB_RDISK|id, IDM_RDISK_RELOAD | id, MF_BYCOMMAND | MF_GRAYED);
    ui_sb_enable_menu_item(SB_RDISK|id, IDM_RDISK_SEND_CHANGE | id, MF_BYCOMMAND | (wcslen(hdd[id].fn) ? MF_ENABLED : MF_GRAYED));

    ui_sb_update_tip(SB_RDISK|id);

    config_save();
}
