/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implement the application's icon changing system.
 *
 *
 * Authors:	Laci bá'
 *
 *		Copyright 2021 Laci bá'.
 */

#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/win.h>

HICON hIcon[256];		    /* icon data loaded from resources */
char  icon_set[256] = "";  /* name of the iconset to be used */

void win_clear_icon_set()
{
	int i;

	for (i = 0; i < 256; i++)
		if (hIcon[i] != 0)
		{
			DestroyIcon(hIcon[i]);
			hIcon[i] = 0;
		}
}

void win_system_icon_set()
{
	int i, x = win_get_system_metrics(SM_CXSMICON, dpi), y = win_get_system_metrics(SM_CYSMICON, dpi);

	for (i = 0; i < 256; i++)
		hIcon[i] = LoadImage(hinstance, MAKEINTRESOURCE(i), IMAGE_ICON, x, y, LR_DEFAULTCOLOR);
}

typedef struct
{
	int id;
	char* filename;
} _ICON_DATA;

const _ICON_DATA icon_files[] =
	{
		{16, "floppy_525.ico"},
		{17, "floppy_525_active.ico"},
		{24, "floppy_35.ico"},
		{25, "floppy_35_active.ico"},
		{32, "cdrom.ico"},
		{33, "cdrom_active.ico"},
		{48, "zip.ico"},
		{49, "zip_active.ico"},
		{56, "mo.ico"},
		{57, "mo_active.ico"},
		{64, "cassette.ico"},
		{65, "cassette_active.ico"},
		{80, "hard_disk.ico"},
		{81, "hard_disk_active.ico"},
		{96, "network.ico"},
		{97, "network_active.ico"},
		{104, "cartridge.ico"},
		{144, "floppy_525_empty.ico"},
		{145, "floppy_525_empty_active.ico"},
		{152, "floppy_35_empty.ico"},
		{153, "floppy_35_empty_active.ico"},
		{160, "cdrom_empty.ico"},
		{161, "cdrom_empty_active.ico"},
		{176, "zip_empty.ico"},
		{177, "zip_empty_active.ico"},
		{184, "mo_empty.ico"},
		{185, "mo_empty_active.ico"},
		{192, "cassette_empty.ico"},
		{193, "cassette_empty_active.ico"},
		{200, "run.ico"},
		{201, "pause.ico"},
		{202, "send_cad.ico"},
		{203, "send_cae.ico"},
		{204, "hard_reset.ico"},
		{205, "acpi_shutdown.ico"},
		{206, "settings.ico"},
		{232, "cartridge_empty.ico"},
		{240, "machine.ico"},
		{241, "display.ico"},
		{242, "input_devices.ico"},
		{243, "sound.ico"},
		{244, "ports.ico"},
		{245, "other_peripherals.ico"},
		{246, "floppy_and_cdrom_drives.ico"},
		{247, "other_removable_devices.ico"},
		{248, "floppy_disabled.ico"},
		{249, "cdrom_disabled.ico"},
		{250, "zip_disabled.ico"},
		{251, "mo_disabled.ico"},
		{252, "storage_controllers.ico"}
	};

void win_get_icons_path(char* path_root)
{
	char roms_root[1024] = {0};
	if (rom_path[0])
		strcpy(roms_root, rom_path);
	else
		path_append_filename(roms_root, exe_path, "roms");

	path_append_filename(path_root, roms_root, "icons");
	path_slash(path_root);
}

void win_load_icon_set()
{
	win_clear_icon_set();
	win_system_icon_set();

	if (strlen(icon_set) == 0) {
		ToolBarLoadIcons();
		return;
	}

	char path_root[2048] = {0}, temp[2048] = {0};
	wchar_t wtemp[2048] = {0};

	win_get_icons_path(path_root);
	strcat(path_root, icon_set);
	path_slash(path_root);

	int i, count = sizeof(icon_files) / sizeof(_ICON_DATA),
	    x = win_get_system_metrics(SM_CXSMICON, dpi), y = win_get_system_metrics(SM_CYSMICON, dpi);
	for (i = 0; i < count; i++)
	{
		path_append_filename(temp, path_root, icon_files[i].filename);
		mbstoc16s(wtemp, temp, strlen(temp) + 1);

		HICON ictemp;
		ictemp = LoadImageW(NULL, (LPWSTR)wtemp, IMAGE_ICON, x, y, LR_LOADFROMFILE | LR_DEFAULTCOLOR);
		if (ictemp)
		{
			if (hIcon[icon_files[i].id])
				DestroyIcon(hIcon[icon_files[i].id]);
			hIcon[icon_files[i].id] = ictemp;
		}
	}

	uint32_t curr_lang = lang_id;
	lang_id = 0;
	set_language(curr_lang);

	ToolBarLoadIcons();
}
