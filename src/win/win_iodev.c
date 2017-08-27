/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Windows IO device menu handler.
 *
 * Version:	@(#)win_iodev.c	1.0.0	2017/05/30
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2016-2017 Miran Grca.
 */
#define UNICODE
#define  _WIN32_WINNT 0x0501
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#undef BITMAP

#include <commctrl.h>
#include <commdlg.h>
#include <process.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>

#include "../ibm.h"
#include "../device.h"
#include "../cdrom.h"
#include "../cdrom_image.h"
#include "../cdrom_ioctl.h"
#include "../cdrom_null.h"
#include "../scsi/scsi_disk.h"
#include "plat_iodev.h"
#include "win.h"


void cdrom_eject(uint8_t id)
{
	int part;

	part = find_status_bar_part(SB_CDROM | id);

	if ((part == -1) || (sb_menu_handles == NULL))
	{
		return;
	}

	if (cdrom_drives[id].host_drive == 0)
	{
		/* Switch from empty to empty. Do nothing. */
		return;
	}
	if ((cdrom_drives[id].host_drive >= 'A') && (cdrom_drives[id].host_drive <= 'Z'))
	{
		CheckMenuItem(sb_menu_handles[part], IDM_CDROM_HOST_DRIVE | id | ((cdrom_drives[id].host_drive - 'A') << 3), MF_UNCHECKED);
	}
	if (cdrom_drives[id].host_drive == 200)
	{
		wcscpy(cdrom_image[id].prev_image_path, cdrom_image[id].image_path);
	}
	cdrom_drives[id].prev_host_drive = cdrom_drives[id].host_drive;
	cdrom_drives[id].handler->exit(id);
	cdrom_close(id);
	cdrom_null_open(id, 0);
	if (cdrom_drives[id].bus_type)
	{
		/* Signal disc change to the emulated machine. */
		cdrom_insert(id);
	}
	CheckMenuItem(sb_menu_handles[part], IDM_CDROM_IMAGE | id, MF_UNCHECKED);
	cdrom_drives[id].host_drive=0;
	CheckMenuItem(sb_menu_handles[part], IDM_CDROM_EMPTY | id, MF_CHECKED);
	update_status_bar_icon_state(SB_CDROM | id, 1);
	EnableMenuItem(sb_menu_handles[part], IDM_CDROM_RELOAD | id, MF_BYCOMMAND | MF_ENABLED);
	update_tip(SB_CDROM | id);
	saveconfig();
}

void cdrom_reload(uint8_t id)
{
	int part;
	int new_cdrom_drive;

	part = find_status_bar_part(SB_CDROM | id);

	if ((part == -1) || (sb_menu_handles == NULL))
	{
		return;
	}

	if ((cdrom_drives[id].host_drive == cdrom_drives[id].prev_host_drive) || (cdrom_drives[id].prev_host_drive == 0) || (cdrom_drives[id].host_drive != 0))
	{
		/* Switch from empty to empty. Do nothing. */
		return;
	}
	cdrom_close(id);
	if (cdrom_drives[id].prev_host_drive == 200)
	{
		wcscpy(cdrom_image[id].image_path, cdrom_image[id].prev_image_path);
		image_open(id, cdrom_image[id].image_path);
		if (cdrom_drives[id].bus_type)
		{
			/* Signal disc change to the emulated machine. */
			cdrom_insert(id);
		}
		if (wcslen(cdrom_image[id].image_path) == 0)
		{
			CheckMenuItem(sb_menu_handles[part], IDM_CDROM_EMPTY | id, MF_CHECKED);
			cdrom_drives[id].host_drive = 0;
			CheckMenuItem(sb_menu_handles[part], IDM_CDROM_IMAGE | id, MF_UNCHECKED);
			update_status_bar_icon_state(SB_CDROM | id, 1);
		}
		else
		{
			CheckMenuItem(sb_menu_handles[part], IDM_CDROM_EMPTY | id, MF_UNCHECKED);
			cdrom_drives[id].host_drive = 200;
			CheckMenuItem(sb_menu_handles[part], IDM_CDROM_IMAGE | id, MF_CHECKED);
			update_status_bar_icon_state(SB_CDROM | id, 0);
		}
	}
	else 
	{
		new_cdrom_drive = cdrom_drives[id].prev_host_drive;
		ioctl_open(id, new_cdrom_drive);
		if (cdrom_drives[id].bus_type)
		{
			/* Signal disc change to the emulated machine. */
			cdrom_insert(id);
		}
		CheckMenuItem(sb_menu_handles[part], IDM_CDROM_EMPTY | id, MF_UNCHECKED);
		cdrom_drives[id].host_drive = new_cdrom_drive;
		CheckMenuItem(sb_menu_handles[part], IDM_CDROM_HOST_DRIVE | id | ((cdrom_drives[id].host_drive - 'A') << 3), MF_CHECKED);
		update_status_bar_icon_state(SB_CDROM | id, 0);
	}
	EnableMenuItem(sb_menu_handles[part], IDM_CDROM_RELOAD | id, MF_BYCOMMAND | MF_GRAYED);
	update_tip(SB_CDROM | id);
	saveconfig();
}

void removable_disk_unload(uint8_t id)
{
	if (wcslen(hdc[id].fn) == 0)
	{
		/* Switch from empty to empty. Do nothing. */
		return;
	}
	scsi_unloadhd(hdc[id].scsi_id, hdc[id].scsi_lun, id);
	scsi_disk_insert(id);
}

void removable_disk_eject(uint8_t id)
{
	int part = 0;

	part = find_status_bar_part(SB_CDROM | id);

	if ((part == -1) || (sb_menu_handles == NULL))
	{
		return;
	}

	removable_disk_unload(id);
	update_status_bar_icon_state(SB_RDISK | id, 1);
	EnableMenuItem(sb_menu_handles[part], IDM_RDISK_EJECT | id, MF_BYCOMMAND | MF_GRAYED);
	EnableMenuItem(sb_menu_handles[part], IDM_RDISK_RELOAD | id, MF_BYCOMMAND | MF_ENABLED);
	EnableMenuItem(sb_menu_handles[part], IDM_RDISK_SEND_CHANGE | id, MF_BYCOMMAND | MF_GRAYED);
	update_tip(SB_RDISK | id);
	saveconfig();
}

void removable_disk_reload(uint8_t id)
{
	int part = 0;

	part = find_status_bar_part(SB_CDROM | id);

	if ((part == -1) || (sb_menu_handles == NULL))
	{
		return;
	}

	if (wcslen(hdc[id].fn) != 0)
	{
		/* Attempting to reload while an image is already loaded. Do nothing. */
		return;
	}
	scsi_reloadhd(id);
	/* scsi_disk_insert(id); */
	update_status_bar_icon_state(SB_RDISK | id, wcslen(hdc[id].fn) ? 0 : 1);
	EnableMenuItem(sb_menu_handles[part], IDM_RDISK_EJECT | id, MF_BYCOMMAND | (wcslen(hdc[id].fn) ? MF_ENABLED : MF_GRAYED));
	EnableMenuItem(sb_menu_handles[part], IDM_RDISK_RELOAD | id, MF_BYCOMMAND | MF_GRAYED);
	EnableMenuItem(sb_menu_handles[part], IDM_RDISK_SEND_CHANGE | id, MF_BYCOMMAND | (wcslen(hdc[id].fn) ? MF_ENABLED : MF_GRAYED));
	update_tip(SB_RDISK | id);
	saveconfig();
}

