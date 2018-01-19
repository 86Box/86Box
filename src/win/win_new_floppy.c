/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handle the New Floppy Image dialog.
 *
 * Version:	@(#)win_new_floppy.c	1.0.0	2018/01/18
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2018 Miran Grca.
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
#include "../86box.h"
#include "../plat.h"
#include "../random.h"
#include "../ui.h"
#include "win.h"


typedef struct {
	int hole;
	int sides;
	int data_rate;
	int encoding;
	int rpm;
	int tracks;
	int sectors;	/* For IMG and Japanese FDI only. */
	int sector_len;	/* For IMG and Japanese FDI only. */
	int media_desc;
	int spc;
	int num_fats;
	int spfat;
	int root_dir_entries;
} disk_size_t;


disk_size_t disk_sizes[12] = {	{	0, 1, 2, 1, 0, 40,  8, 2, 0xFE, 2, 2,  1, 112 },	/* 160k */
				{	0, 1, 2, 1, 0, 40,  9, 2, 0xFC, 2, 2,  1, 112 },	/* 180k */
				{	0, 2, 2, 1, 0, 40,  8, 2, 0xFF, 2, 2,  1, 112 },	/* 320k */
				{	0, 2, 2, 1, 0, 40,  9, 2, 0xFD, 2, 2,  2, 112 },	/* 360k */
				{	0, 2, 2, 1, 0, 80,  8, 2, 0xFB, 2, 2,  2, 112 },	/* 640k */
				{	0, 2, 2, 1, 0, 80,  9, 2, 0xF9, 2, 2,  3, 112 },	/* 720k */
				{	1, 2, 0, 1, 1, 80, 15, 2, 0xF9, 1, 2,  7, 224 },	/* 1.2M */
				{	1, 2, 0, 1, 1, 77,  8, 3, 0xFE, 1, 2,  2, 192 },	/* 1.25M */
				{	1, 2, 0, 1, 0, 80, 18, 2, 0xF0, 1, 2,  9, 224 },	/* 1.44M */
				{	1, 2, 0, 1, 0, 80, 21, 2, 0xF0, 2, 2,  5,  16 },	/* DMF cluster 1024 */
				{	1, 2, 0, 1, 0, 80, 21, 2, 0xF0, 4, 2,  3,  16 },	/* DMF cluster 2048 */
				{	2, 2, 3, 1, 0, 80, 36, 2, 0xF0, 2, 2,  9, 240 }	};	/* 2.88M */

static char	*empty;


static int
create_86f(WCHAR *file_name, disk_size_t disk_size, uint8_t rpm_mode)
{
    FILE *f;

    uint32_t magic = 0x46423638;
    uint16_t version = 0x020B;
    uint16_t dflags = 0;
    uint16_t tflags = 0;
    uint16_t index_hole_pos = 0;
    uint32_t tarray[512];
    uint32_t array_size, array_size2;
    uint32_t track_base, track_size;
    int i;

    dflags = 0;					/* Has surface data? - Assume no for now. */
    dflags |= (disk_size.hole << 1);		/* Hole */
    dflags |= ((disk_size.sides - 1) << 3);	/* Sides. */
    dflags |= (0 << 4);				/* Write protect? - Assume no for now. */
    dflags |= (rpm_mode << 5);			/* RPM mode. */
    dflags |= (0 << 7);				/* Has extra bit cells? - Assume no for now. */

    tflags = disk_size.data_rate;		/* Data rate. */
    tflags |= (disk_size.encoding << 3);	/* Encoding. */
    tflags |= (disk_size.rpm << 5);		/* RPM. */

    switch (disk_size.hole) {
	case 0:
	case 1:
	default:
		switch(rpm_mode) {
			case 1:
				array_size = 25250;
				break;
			case 2:
				array_size = 25374;
				break;
			case 3:
				array_size = 25750;
				break;
			default:
				array_size = 25000;
				break;
		}
		break;
	case 2:
		switch(rpm_mode) {
			case 1:
				array_size = 50500;
				break;
			case 2:
				array_size = 50750;
				break;
			case 3:
				array_size = 51000;
				break;
			default:
				array_size = 50000;
				break;
		}
		break;
    }

    array_size2 = (array_size << 3);
    array_size = (array_size2 >> 4) << 1;
    if (array_size2 & 15)
	array_size += 2;

    empty = (char *) malloc(array_size);

    memset(tarray, 0, 2048);
    memset(empty, 0, array_size);

    f = plat_fopen(file_name, L"wb");
    if (!f)
	return 0;

    fwrite(&magic, 4, 1, f);
    fwrite(&version, 2, 1, f);
    fwrite(&dflags, 2, 1, f);

    track_size = array_size + 6;

    track_base = 8 + ((disk_size.sides == 2) ? 2048 : 1024);

    for (i = 0; i < disk_size.tracks * disk_size.sides; i++)
	tarray[i] = track_base + (i * track_size);

    fwrite(tarray, 1, (disk_size.sides == 2) ? 2048 : 1024, f);

    for (i = 0; i < disk_size.tracks * disk_size.sides; i++) {
	fwrite(&tflags, 2, 1, f);
	fwrite(&index_hole_pos, 4, 1, f);
	fwrite(empty, 1, array_size, f);
    }

    free(empty);

    fclose(f);

    return 1;
}


static int
create_sector_image(WCHAR *file_name, disk_size_t disk_size, uint8_t is_fdi)
{
    FILE *f;
    uint32_t total_size = 0;
    uint32_t total_sectors = 0;
    uint32_t sector_bytes = 0;
    uint32_t root_dir_bytes = 0;
    uint32_t fat_size = 0;
    uint32_t fat1_offs = 0;
    uint32_t fat2_offs = 0;
    uint32_t zero_bytes = 0;
    uint16_t base = 0x1000;
    
    f = plat_fopen(file_name, L"wb");
    if (!f)
	return 0;

    sector_bytes = (128 << disk_size.sector_len);
    total_sectors = disk_size.sides * disk_size.tracks * disk_size.sectors;
    total_size = total_sectors * sector_bytes;
    root_dir_bytes = (disk_size.root_dir_entries << 5);
    fat_size = (disk_size.spfat * sector_bytes);
    fat1_offs = sector_bytes;
    fat2_offs = fat1_offs + fat_size;
    zero_bytes = fat2_offs + fat_size + root_dir_bytes;

    if (is_fdi) {
	empty = (char *) malloc(base);
	memset(empty, 0, base);

	*(uint32_t *) &(empty[0x08]) = (uint32_t) base;
	*(uint32_t *) &(empty[0x0C]) = total_size;
	*(uint16_t *) &(empty[0x10]) = (uint16_t) sector_bytes;
	*(uint8_t *)  &(empty[0x14]) = (uint8_t)  disk_size.sectors;
	*(uint8_t *)  &(empty[0x18]) = (uint8_t)  disk_size.sides;
	*(uint8_t *)  &(empty[0x1C]) = (uint8_t)  disk_size.tracks;

	fwrite(empty, 1, base, f);
	free(empty);
    }

    empty = (char *) malloc(total_size);
    memset(empty, 0x00, zero_bytes);
    memset(empty + zero_bytes, 0xF6, total_size - zero_bytes);

    empty[0x00] = 0xEB;			/* Jump to make MS-DOS happy. */
    empty[0x01] = 0x58;
    empty[0x02] = 0x90;

    empty[0x03] = 0x38;			/* '86BOX5.0' OEM ID. */
    empty[0x04] = 0x36;
    empty[0x05] = 0x42;
    empty[0x06] = 0x4F;
    empty[0x07] = 0x58;
    empty[0x08] = 0x35;
    empty[0x09] = 0x2E;
    empty[0x0A] = 0x30;

    *(uint16_t *) &(empty[0x0B]) = (uint16_t) sector_bytes;
    *(uint8_t  *) &(empty[0x0D]) = (uint8_t)  disk_size.spc;
    *(uint16_t *) &(empty[0x0E]) = (uint16_t) 1;
    *(uint8_t  *) &(empty[0x10]) = (uint8_t)  disk_size.num_fats;
    *(uint16_t *) &(empty[0x11]) = (uint16_t) disk_size.root_dir_entries;
    *(uint16_t *) &(empty[0x13]) = (uint16_t) total_sectors;
    *(uint8_t *)  &(empty[0x15]) = (uint8_t)  disk_size.media_desc;
    *(uint16_t *) &(empty[0x16]) = (uint16_t) disk_size.spfat;
    *(uint8_t *)  &(empty[0x18]) = (uint8_t)  disk_size.sectors;
    *(uint8_t *)  &(empty[0x1A]) = (uint8_t)  disk_size.sides;

    empty[0x26] = 0x29;			/* ')' followed by randomly-generated volume serial number. */
    empty[0x27] = random_generate();
    empty[0x28] = random_generate();
    empty[0x29] = random_generate();
    empty[0x2A] = random_generate();

    memset(&(empty[0x2B]), 0x20, 11);

    empty[0x36] = 'F';
    empty[0x37] = 'A';
    empty[0x38] = 'T';
    empty[0x39] = '1';
    empty[0x3A] = '2';

    empty[0x1FE] = 0x55;
    empty[0x1FF] = 0xAA;

    empty[fat1_offs + 0x00] = empty[fat2_offs + 0x00] = empty[0x15];
    empty[fat1_offs + 0x01] = empty[fat2_offs + 0x01] = empty[0xFF];
    empty[fat1_offs + 0x02] = empty[fat2_offs + 0x02] = empty[0xFF];

    fwrite(empty, 1, total_size, f);
    free(empty);

    fclose(f);

    return 1;
}


static int	fdd_id, sb_part;

static int	file_type = 0;		/* 0 = IMG, 1 = Japanese FDI, 2 = 86F */
static wchar_t	fd_file_name[512];


/* Show a MessageBox dialog.  This is nasty, I know.  --FvK */
static int
new_floppy_msgbox(HWND hwnd, int type, void *arg)
{
    HWND h;
    int i;

    h = hwndMain;
    hwndMain = hwnd;

    i = ui_msgbox(type, arg);

    hwndMain = h;

    return(i);
}


#ifdef __amd64__
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
NewFloppyDialogProcedure(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND h;
    int i = 0;
    int wcs_len, ext_offs;
    wchar_t *ext;
    uint8_t disk_size, rpm_mode;
    int ret;
    FILE *f;

    switch (message) {
	case WM_INITDIALOG:
		plat_pause(1);
		memset(fd_file_name, 0, 512 * sizeof(wchar_t));
		h = GetDlgItem(hdlg, IDC_COMBO_DISK_SIZE);
		for (i = 0; i < 12; i++)
	                SendMessage(h, CB_ADDSTRING, 0, (LPARAM) plat_get_string(IDS_5888 + i));
		EnableWindow(h, FALSE);
		h = GetDlgItem(hdlg, IDC_COMBO_RPM_MODE);
		for (i = 0; i < 4; i++)
	                SendMessage(h, CB_ADDSTRING, 0, (LPARAM) plat_get_string(IDS_6144 + i));
		EnableWindow(h, FALSE);
		ShowWindow(h, SW_HIDE);
		h = GetDlgItem(hdlg, IDT_1751);
		EnableWindow(h, FALSE);
		ShowWindow(h, SW_HIDE);
		h = GetDlgItem(hdlg, IDOK);
		EnableWindow(h, FALSE);
		break;

	case WM_COMMAND:
                switch (LOWORD(wParam)) {
			case IDOK:
				h = GetDlgItem(hdlg, IDC_COMBO_DISK_SIZE);
				disk_size = SendMessage(h, CB_GETCURSEL, 0, 0);
				if (file_type == 2) {
					h = GetDlgItem(hdlg, IDC_COMBO_RPM_MODE);
					rpm_mode = SendMessage(h, CB_GETCURSEL, 0, 0);
					ret = create_86f(fd_file_name, disk_sizes[disk_size], rpm_mode);
				} else
					ret = create_sector_image(fd_file_name, disk_sizes[disk_size], file_type);
				if (ret)
					ui_sb_mount_floppy_img(fdd_id, sb_part, 0, fd_file_name);
				else {
					new_floppy_msgbox(hdlg, MBX_ERROR, (wchar_t *)IDS_4108);
					return TRUE;
				}
			case IDCANCEL:
				EndDialog(hdlg, 0);
				plat_pause(0);
				return TRUE;

			case IDC_CFILE:
	                        if (!file_dlg_w(hdlg, plat_get_string(IDS_2174), L"", 1)) {
					h = GetDlgItem(hdlg, IDC_EDIT_FILE_NAME);
					f = _wfopen(wopenfilestring, L"rb");
					if (f != NULL) {
						fclose(f);
						if (new_floppy_msgbox(hdlg, MBX_QUESTION, (wchar_t *)IDS_4111) != 0)	/* yes */
							return FALSE;
					}
					SendMessage(h, WM_SETTEXT, 0, (LPARAM) wopenfilestring);
					memset(fd_file_name, 0, sizeof(fd_file_name));
					wcscpy(fd_file_name, wopenfilestring);
					h = GetDlgItem(hdlg, IDC_COMBO_DISK_SIZE);
					EnableWindow(h, TRUE);
					wcs_len = wcslen(wopenfilestring);
					ext_offs = wcs_len - 4;
					ext = &(wopenfilestring[ext_offs]);
					if ((wcs_len >= 4) && !wcsicmp(ext, L".FDI"))
						file_type = 1;
					else if ((wcs_len >= 4) && !wcsicmp(ext, L".86F"))
						file_type = 2;
					else
						file_type = 0;
					h = GetDlgItem(hdlg, IDT_1751);
					if (file_type == 2) {
						EnableWindow(h, TRUE);
						ShowWindow(h, SW_SHOW);
					} else {
						EnableWindow(h, FALSE);
						ShowWindow(h, SW_HIDE);
					}
					h = GetDlgItem(hdlg, IDC_COMBO_RPM_MODE);
					if (file_type == 2) {
						EnableWindow(h, TRUE);
						ShowWindow(h, SW_SHOW);
					} else {
						EnableWindow(h, FALSE);
						ShowWindow(h, SW_HIDE);
					}
					h = GetDlgItem(hdlg, IDOK);
					EnableWindow(h, TRUE);
					return TRUE;
				} else
					return FALSE;

			default:
				break;
		}
		break;
    }

    return(FALSE);
}


void
NewFloppyDialogCreate(HWND hwnd, int id, int part)
{
    fdd_id = id;
    sb_part = part;
    DialogBox(hinstance, (LPCTSTR)DLG_NEW_FLOPPY, hwnd, NewFloppyDialogProcedure);
}
