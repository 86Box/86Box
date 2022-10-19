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
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2019 Miran Grca.
 */
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#undef BITMAP
#include <commctrl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/random.h>
#include <86box/ui.h>
#include <86box/scsi_device.h>
#include <86box/mo.h>
#include <86box/zip.h>
#include <86box/win.h>

typedef struct {
    int hole;
    int sides;
    int data_rate;
    int encoding;
    int rpm;
    int tracks;
    int sectors;    /* For IMG and Japanese FDI only. */
    int sector_len; /* For IMG and Japanese FDI only. */
    int media_desc;
    int spc;
    int num_fats;
    int spfat;
    int root_dir_entries;
} disk_size_t;

static const disk_size_t disk_sizes[14] = {
  //						{	1,  1, 2, 1, 1,  77, 26, 0,    0, 4, 2,  6,  68 },		/* 250k 8" */
  //						{	1,  2, 2, 1, 1,  77, 26, 0,    0, 4, 2,  6,  68 },		/* 500k 8" */
  //						{	1,  1, 2, 1, 1,  77,  8, 3,    0, 1, 2,  2, 192 },		/* 616k 8" */
  //						{	1,  2, 0, 1, 1,  77,  8, 3,    0, 1, 2,  2, 192 },		/* 1232k 8" */
    {0,  1,  2, 1, 0, 40,  8,  2, 0xfe, 1, 2, 1, 64 }, /* 160k */
    { 0, 1,  2, 1, 0, 40,  9,  2, 0xfc, 1, 2, 2, 64 }, /* 180k */
    { 0, 2,  2, 1, 0, 40,  8,  2, 0xff, 2, 2, 1, 112}, /* 320k */
    { 0, 2,  2, 1, 0, 40,  9,  2, 0xfd, 2, 2, 2, 112}, /* 360k */
    { 0, 2,  2, 1, 0, 80,  8,  2, 0xfb, 2, 2, 2, 112}, /* 640k */
    { 0, 2,  2, 1, 0, 80,  9,  2, 0xf9, 2, 2, 3, 112}, /* 720k */
    { 1, 2,  0, 1, 1, 80,  15, 2, 0xf9, 1, 2, 7, 224}, /* 1.2M */
    { 1, 2,  0, 1, 1, 77,  8,  3, 0xfe, 1, 2, 2, 192}, /* 1.25M */
    { 1, 2,  0, 1, 0, 80,  18, 2, 0xf0, 1, 2, 9, 224}, /* 1.44M */
    { 1, 2,  0, 1, 0, 80,  21, 2, 0xf0, 2, 2, 5, 16 }, /* DMF cluster 1024 */
    { 1, 2,  0, 1, 0, 80,  21, 2, 0xf0, 4, 2, 3, 16 }, /* DMF cluster 2048 */
    { 2, 2,  3, 1, 0, 80,  36, 2, 0xf0, 2, 2, 9, 240}, /* 2.88M */
    { 0, 64, 0, 0, 0, 96,  32, 2, 0,    0, 0, 0, 0  }, /* ZIP 100 */
    { 0, 64, 0, 0, 0, 239, 32, 2, 0,    0, 0, 0, 0  }
}; /* ZIP 250 */

static unsigned char *empty;

static int
create_86f(char *file_name, disk_size_t disk_size, uint8_t rpm_mode)
{
    FILE *f;

    uint32_t magic          = 0x46423638;
    uint16_t version        = 0x020C;
    uint16_t dflags         = 0;
    uint16_t tflags         = 0;
    uint32_t index_hole_pos = 0;
    uint32_t tarray[512];
    uint32_t array_size;
    uint32_t track_base, track_size;
    int      i;
    uint32_t shift = 0;

    dflags = 0;                             /* Has surface data? - Assume no for now. */
    dflags |= (disk_size.hole << 1);        /* Hole */
    dflags |= ((disk_size.sides - 1) << 3); /* Sides. */
    dflags |= (0 << 4);                     /* Write protect? - Assume no for now. */
    dflags |= (rpm_mode << 5);              /* RPM mode. */
    dflags |= (0 << 7);                     /* Has extra bit cells? - Assume no for now. */

    tflags = disk_size.data_rate;        /* Data rate. */
    tflags |= (disk_size.encoding << 3); /* Encoding. */
    tflags |= (disk_size.rpm << 5);      /* RPM. */

    switch (disk_size.hole) {
        case 0:
        case 1:
        default:
            switch (rpm_mode) {
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
            switch (rpm_mode) {
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

    empty = (unsigned char *) malloc(array_size);

    memset(tarray, 0, 2048);
    memset(empty, 0, array_size);

    f = plat_fopen(file_name, "wb");
    if (!f)
        return 0;

    fwrite(&magic, 4, 1, f);
    fwrite(&version, 2, 1, f);
    fwrite(&dflags, 2, 1, f);

    track_size = array_size + 6;

    track_base = 8 + ((disk_size.sides == 2) ? 2048 : 1024);

    if (disk_size.tracks <= 43)
        shift = 1;

    for (i = 0; i < (disk_size.tracks * disk_size.sides) << shift; i++)
        tarray[i] = track_base + (i * track_size);

    fwrite(tarray, 1, (disk_size.sides == 2) ? 2048 : 1024, f);

    for (i = 0; i < (disk_size.tracks * disk_size.sides) << shift; i++) {
        fwrite(&tflags, 2, 1, f);
        fwrite(&index_hole_pos, 4, 1, f);
        fwrite(empty, 1, array_size, f);
    }

    free(empty);

    fclose(f);

    return 1;
}

static int is_zip;
static int is_mo;

static int
create_sector_image(char *file_name, disk_size_t disk_size, uint8_t is_fdi)
{
    FILE    *f;
    uint32_t total_size     = 0;
    uint32_t total_sectors  = 0;
    uint32_t sector_bytes   = 0;
    uint32_t root_dir_bytes = 0;
    uint32_t fat_size       = 0;
    uint32_t fat1_offs      = 0;
    uint32_t fat2_offs      = 0;
    uint32_t zero_bytes     = 0;
    uint16_t base           = 0x1000;

    f = plat_fopen(file_name, "wb");
    if (!f)
        return 0;

    sector_bytes  = (128 << disk_size.sector_len);
    total_sectors = disk_size.sides * disk_size.tracks * disk_size.sectors;
    if (total_sectors > ZIP_SECTORS)
        total_sectors = ZIP_250_SECTORS;
    total_size     = total_sectors * sector_bytes;
    root_dir_bytes = (disk_size.root_dir_entries << 5);
    fat_size       = (disk_size.spfat * sector_bytes);
    fat1_offs      = sector_bytes;
    fat2_offs      = fat1_offs + fat_size;
    zero_bytes     = fat2_offs + fat_size + root_dir_bytes;

    if (!is_zip && !is_mo && is_fdi) {
        empty = (unsigned char *) malloc(base);
        memset(empty, 0, base);

        *(uint32_t *) &(empty[0x08]) = (uint32_t) base;
        *(uint32_t *) &(empty[0x0C]) = total_size;
        *(uint16_t *) &(empty[0x10]) = (uint16_t) sector_bytes;
        *(uint8_t *) &(empty[0x14])  = (uint8_t) disk_size.sectors;
        *(uint8_t *) &(empty[0x18])  = (uint8_t) disk_size.sides;
        *(uint8_t *) &(empty[0x1C])  = (uint8_t) disk_size.tracks;

        fwrite(empty, 1, base, f);
        free(empty);
    }

    empty = (unsigned char *) malloc(total_size);
    memset(empty, 0x00, zero_bytes);

    if (!is_zip && !is_mo) {
        memset(empty + zero_bytes, 0xF6, total_size - zero_bytes);

        empty[0x00] = 0xEB; /* Jump to make MS-DOS happy. */
        empty[0x01] = 0x58;
        empty[0x02] = 0x90;

        empty[0x03] = 0x38; /* '86BOX5.0' OEM ID. */
        empty[0x04] = 0x36;
        empty[0x05] = 0x42;
        empty[0x06] = 0x4F;
        empty[0x07] = 0x58;
        empty[0x08] = 0x35;
        empty[0x09] = 0x2E;
        empty[0x0A] = 0x30;

        *(uint16_t *) &(empty[0x0B]) = (uint16_t) sector_bytes;
        *(uint8_t *) &(empty[0x0D])  = (uint8_t) disk_size.spc;
        *(uint16_t *) &(empty[0x0E]) = (uint16_t) 1;
        *(uint8_t *) &(empty[0x10])  = (uint8_t) disk_size.num_fats;
        *(uint16_t *) &(empty[0x11]) = (uint16_t) disk_size.root_dir_entries;
        *(uint16_t *) &(empty[0x13]) = (uint16_t) total_sectors;
        *(uint8_t *) &(empty[0x15])  = (uint8_t) disk_size.media_desc;
        *(uint16_t *) &(empty[0x16]) = (uint16_t) disk_size.spfat;
        *(uint8_t *) &(empty[0x18])  = (uint8_t) disk_size.sectors;
        *(uint8_t *) &(empty[0x1A])  = (uint8_t) disk_size.sides;

        empty[0x26] = 0x29; /* ')' followed by randomly-generated volume serial number. */
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
        memset(&(empty[0x3B]), 0x20, 0x0003);

        empty[0x1FE] = 0x55;
        empty[0x1FF] = 0xAA;

        empty[fat1_offs + 0x00] = empty[fat2_offs + 0x00] = empty[0x15];
        empty[fat1_offs + 0x01] = empty[fat2_offs + 0x01] = 0xFF;
        empty[fat1_offs + 0x02] = empty[fat2_offs + 0x02] = 0xFF;
    }

    fwrite(empty, 1, total_size, f);
    free(empty);

    fclose(f);

    return 1;
}

static int
create_zip_sector_image(char *file_name, disk_size_t disk_size, uint8_t is_zdi, HWND hwnd)
{
    HWND     h;
    FILE    *f;
    uint32_t total_size     = 0;
    uint32_t total_sectors  = 0;
    uint32_t sector_bytes   = 0;
    uint32_t root_dir_bytes = 0;
    uint32_t fat_size       = 0;
    uint32_t fat1_offs      = 0;
    uint32_t fat2_offs      = 0;
    uint32_t zero_bytes     = 0;
    uint16_t base           = 0x1000;
    uint32_t pbar_max       = 0;
    uint32_t i;
    MSG      msg;

    f = plat_fopen(file_name, "wb");
    if (!f)
        return 0;

    sector_bytes  = (128 << disk_size.sector_len);
    total_sectors = disk_size.sides * disk_size.tracks * disk_size.sectors;
    if (total_sectors > ZIP_SECTORS)
        total_sectors = ZIP_250_SECTORS;
    total_size     = total_sectors * sector_bytes;
    root_dir_bytes = (disk_size.root_dir_entries << 5);
    fat_size       = (disk_size.spfat * sector_bytes);
    fat1_offs      = sector_bytes;
    fat2_offs      = fat1_offs + fat_size;
    zero_bytes     = fat2_offs + fat_size + root_dir_bytes;

    pbar_max = total_size;
    if (is_zdi)
        pbar_max += base;
    pbar_max >>= 11;
    pbar_max--;

    h = GetDlgItem(hwnd, IDC_COMBO_RPM_MODE);
    EnableWindow(h, FALSE);
    ShowWindow(h, SW_HIDE);
    h = GetDlgItem(hwnd, IDT_FLP_RPM_MODE);
    EnableWindow(h, FALSE);
    ShowWindow(h, SW_HIDE);
    h = GetDlgItem(hwnd, IDC_PBAR_IMG_CREATE);
    SendMessage(h, PBM_SETRANGE32, (WPARAM) 0, (LPARAM) pbar_max);
    SendMessage(h, PBM_SETPOS, (WPARAM) 0, (LPARAM) 0);
    EnableWindow(h, TRUE);
    ShowWindow(h, SW_SHOW);
    h = GetDlgItem(hwnd, IDT_FLP_PROGRESS);
    EnableWindow(h, TRUE);
    ShowWindow(h, SW_SHOW);

    h = GetDlgItem(hwnd, IDC_PBAR_IMG_CREATE);
    pbar_max++;

    if (is_zdi) {
        empty = (unsigned char *) malloc(base);
        memset(empty, 0, base);

        *(uint32_t *) &(empty[0x08]) = (uint32_t) base;
        *(uint32_t *) &(empty[0x0C]) = total_size;
        *(uint16_t *) &(empty[0x10]) = (uint16_t) sector_bytes;
        *(uint8_t *) &(empty[0x14])  = (uint8_t) disk_size.sectors;
        *(uint8_t *) &(empty[0x18])  = (uint8_t) disk_size.sides;
        *(uint8_t *) &(empty[0x1C])  = (uint8_t) disk_size.tracks;

        fwrite(empty, 1, 2048, f);
        SendMessage(h, PBM_SETPOS, (WPARAM) 1, (LPARAM) 0);

        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE | PM_NOYIELD)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        fwrite(&empty[0x0800], 1, 2048, f);
        free(empty);

        SendMessage(h, PBM_SETPOS, (WPARAM) 2, (LPARAM) 0);

        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE | PM_NOYIELD)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        pbar_max -= 2;
    }

    empty = (unsigned char *) malloc(total_size);
    memset(empty, 0x00, zero_bytes);

    if (total_sectors == ZIP_SECTORS) {
        /* ZIP 100 */
        /* MBR */
        *(uint64_t *) &(empty[0x0000]) = 0x2054524150492EEBLL;
        *(uint64_t *) &(empty[0x0008]) = 0x3930302065646F63LL;
        *(uint64_t *) &(empty[0x0010]) = 0x67656D6F49202D20LL;
        *(uint64_t *) &(empty[0x0018]) = 0x726F70726F432061LL;
        *(uint64_t *) &(empty[0x0020]) = 0x202D206E6F697461LL;
        *(uint64_t *) &(empty[0x0028]) = 0x30392F33322F3131LL;

        *(uint64_t *) &(empty[0x01AE]) = 0x0116010100E90644LL;
        *(uint64_t *) &(empty[0x01B6]) = 0xED08BBE5014E0135LL;
        *(uint64_t *) &(empty[0x01BE]) = 0xFFFFFE06FFFFFE80LL;
        *(uint64_t *) &(empty[0x01C6]) = 0x0002FFE000000020LL;

        *(uint16_t *) &(empty[0x01FE]) = 0xAA55;

        /* 31 sectors filled with 0x48 */
        memset(&(empty[0x0200]), 0x48, 0x3E00);

        /* Boot sector */
        *(uint64_t *) &(empty[0x4000]) = 0x584F4236389058EBLL;
        *(uint64_t *) &(empty[0x4008]) = 0x0008040200302E35LL;
        *(uint64_t *) &(empty[0x4010]) = 0x00C0F80000020002LL;
        *(uint64_t *) &(empty[0x4018]) = 0x0000002000FF003FLL;
        *(uint32_t *) &(empty[0x4020]) = 0x0002FFE0;
        *(uint16_t *) &(empty[0x4024]) = 0x0080;

        empty[0x4026] = 0x29; /* ')' followed by randomly-generated volume serial number. */
        empty[0x4027] = random_generate();
        empty[0x4028] = random_generate();
        empty[0x4029] = random_generate();
        empty[0x402A] = random_generate();

        memset(&(empty[0x402B]), 0x00, 0x000B);
        memset(&(empty[0x4036]), 0x20, 0x0008);

        empty[0x4036] = 'F';
        empty[0x4037] = 'A';
        empty[0x4038] = 'T';
        empty[0x4039] = '1';
        empty[0x403A] = '6';
        memset(&(empty[0x403B]), 0x20, 0x0003);

        empty[0x41FE] = 0x55;
        empty[0x41FF] = 0xAA;

        empty[0x5000] = empty[0x1D000] = empty[0x4015];
        empty[0x5001] = empty[0x1D001] = 0xFF;
        empty[0x5002] = empty[0x1D002] = 0xFF;
        empty[0x5003] = empty[0x1D003] = 0xFF;

        /* Root directory = 0x35000
           Data = 0x39000 */
    } else {
        /* ZIP 250 */
        /* MBR */
        *(uint64_t *) &(empty[0x0000]) = 0x2054524150492EEBLL;
        *(uint64_t *) &(empty[0x0008]) = 0x3930302065646F63LL;
        *(uint64_t *) &(empty[0x0010]) = 0x67656D6F49202D20LL;
        *(uint64_t *) &(empty[0x0018]) = 0x726F70726F432061LL;
        *(uint64_t *) &(empty[0x0020]) = 0x202D206E6F697461LL;
        *(uint64_t *) &(empty[0x0028]) = 0x30392F33322F3131LL;

        *(uint64_t *) &(empty[0x01AE]) = 0x0116010100E900E9LL;
        *(uint64_t *) &(empty[0x01B6]) = 0x2E32A7AC014E0135LL;

        *(uint64_t *) &(empty[0x01EE]) = 0xEE203F0600010180LL;
        *(uint64_t *) &(empty[0x01F6]) = 0x000777E000000020LL;
        *(uint16_t *) &(empty[0x01FE]) = 0xAA55;

        /* 31 sectors filled with 0x48 */
        memset(&(empty[0x0200]), 0x48, 0x3E00);

        /* The second sector begins with some strange data
           in my reference image. */
        *(uint64_t *) &(empty[0x0200]) = 0x3831393230334409LL;
        *(uint64_t *) &(empty[0x0208]) = 0x6A57766964483130LL;
        *(uint64_t *) &(empty[0x0210]) = 0x3C3A34676063653FLL;
        *(uint64_t *) &(empty[0x0218]) = 0x586A56A8502C4161LL;
        *(uint64_t *) &(empty[0x0220]) = 0x6F2D702535673D6CLL;
        *(uint64_t *) &(empty[0x0228]) = 0x255421B8602D3456LL;
        *(uint64_t *) &(empty[0x0230]) = 0x577B22447B52603ELL;
        *(uint64_t *) &(empty[0x0238]) = 0x46412CC871396170LL;
        *(uint64_t *) &(empty[0x0240]) = 0x704F55237C5E2626LL;
        *(uint64_t *) &(empty[0x0248]) = 0x6C7932C87D5C3C20LL;
        *(uint64_t *) &(empty[0x0250]) = 0x2C50503E47543D6ELL;
        *(uint64_t *) &(empty[0x0258]) = 0x46394E807721536ALL;
        *(uint64_t *) &(empty[0x0260]) = 0x505823223F245325LL;
        *(uint64_t *) &(empty[0x0268]) = 0x365C79B0393B5B6ELL;

        /* Boot sector */
        *(uint64_t *) &(empty[0x4000]) = 0x584F4236389058EBLL;
        *(uint64_t *) &(empty[0x4008]) = 0x0001080200302E35LL;
        *(uint64_t *) &(empty[0x4010]) = 0x00EFF80000020002LL;
        *(uint64_t *) &(empty[0x4018]) = 0x0000002000400020LL;
        *(uint32_t *) &(empty[0x4020]) = 0x000777E0;
        *(uint16_t *) &(empty[0x4024]) = 0x0080;

        empty[0x4026] = 0x29; /* ')' followed by randomly-generated volume serial number. */
        empty[0x4027] = random_generate();
        empty[0x4028] = random_generate();
        empty[0x4029] = random_generate();
        empty[0x402A] = random_generate();

        memset(&(empty[0x402B]), 0x00, 0x000B);
        memset(&(empty[0x4036]), 0x20, 0x0008);

        empty[0x4036] = 'F';
        empty[0x4037] = 'A';
        empty[0x4038] = 'T';
        empty[0x4039] = '1';
        empty[0x403A] = '6';
        memset(&(empty[0x403B]), 0x20, 0x0003);

        empty[0x41FE] = 0x55;
        empty[0x41FF] = 0xAA;

        empty[0x4200] = empty[0x22000] = empty[0x4015];
        empty[0x4201] = empty[0x22001] = 0xFF;
        empty[0x4202] = empty[0x22002] = 0xFF;
        empty[0x4203] = empty[0x22003] = 0xFF;

        /* Root directory = 0x3FE00
           Data = 0x38200 */
    }

    for (i = 0; i < pbar_max; i++) {
        fwrite(&empty[i << 11], 1, 2048, f);
        SendMessage(h, PBM_SETPOS, (WPARAM) i + 2, (LPARAM) 0);

        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE | PM_NOYIELD)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    free(empty);

    fclose(f);

    return 1;
}

static int
create_mo_sector_image(char *file_name, int8_t disk_size, uint8_t is_mdi, HWND hwnd)
{
    HWND             h;
    FILE            *f;
    const mo_type_t *dp = &mo_types[disk_size];
    uint8_t         *empty, *empty2 = NULL;
    uint32_t         total_size    = 0, total_size2;
    uint32_t         total_sectors = 0;
    uint32_t         sector_bytes  = 0;
    uint16_t         base          = 0x1000;
    uint32_t         pbar_max      = 0, blocks_num;
    uint32_t         i, j;
    MSG              msg;

    f = plat_fopen(file_name, "wb");
    if (!f)
        return 0;

    sector_bytes  = dp->bytes_per_sector;
    total_sectors = dp->sectors;
    total_size    = total_sectors * sector_bytes;

    total_size2 = (total_size >> 20) << 20;
    total_size2 = total_size - total_size2;

    pbar_max = total_size;
    pbar_max >>= 20;
    blocks_num = pbar_max;
    if (is_mdi)
        pbar_max++;
    if (total_size2 == 0)
        pbar_max++;

    j = is_mdi ? 1 : 0;

    h = GetDlgItem(hwnd, IDC_COMBO_RPM_MODE);
    EnableWindow(h, FALSE);
    ShowWindow(h, SW_HIDE);
    h = GetDlgItem(hwnd, IDT_FLP_RPM_MODE);
    EnableWindow(h, FALSE);
    ShowWindow(h, SW_HIDE);
    h = GetDlgItem(hwnd, IDC_PBAR_IMG_CREATE);
    SendMessage(h, PBM_SETRANGE32, (WPARAM) 0, (LPARAM) pbar_max - 1);
    SendMessage(h, PBM_SETPOS, (WPARAM) 0, (LPARAM) 0);
    EnableWindow(h, TRUE);
    ShowWindow(h, SW_SHOW);
    h = GetDlgItem(hwnd, IDT_FLP_PROGRESS);
    EnableWindow(h, TRUE);
    ShowWindow(h, SW_SHOW);

    h = GetDlgItem(hwnd, IDC_PBAR_IMG_CREATE);

    if (is_mdi) {
        empty = (unsigned char *) malloc(base);
        memset(empty, 0, base);

        *(uint32_t *) &(empty[0x08]) = (uint32_t) base;
        *(uint32_t *) &(empty[0x0C]) = total_size;
        *(uint16_t *) &(empty[0x10]) = (uint16_t) sector_bytes;
        *(uint8_t *) &(empty[0x14])  = (uint8_t) 25;
        *(uint8_t *) &(empty[0x18])  = (uint8_t) 64;
        *(uint8_t *) &(empty[0x1C])  = (uint8_t) (dp->sectors / 64) / 25;

        fwrite(empty, 1, 2048, f);
        SendMessage(h, PBM_SETPOS, (WPARAM) 1, (LPARAM) 0);

        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE | PM_NOYIELD)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        fwrite(&empty[0x0800], 1, 2048, f);
        free(empty);

        SendMessage(h, PBM_SETPOS, (WPARAM) 1, (LPARAM) 0);

        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE | PM_NOYIELD)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    empty = (unsigned char *) malloc(1048576);
    memset(empty, 0x00, 1048576);

    if (total_size2 > 0) {
        empty2 = (unsigned char *) malloc(total_size2);
        memset(empty, 0x00, total_size2);
    }

    for (i = 0; i < blocks_num; i++) {
        fwrite(empty, 1, 1048576, f);

        SendMessage(h, PBM_SETPOS, (WPARAM) i + j, (LPARAM) 0);

        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE | PM_NOYIELD)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    if (total_size2 > 0) {
        fwrite(empty2, 1, total_size2, f);

        SendMessage(h, PBM_SETPOS, (WPARAM) pbar_max - 1, (LPARAM) 0);

        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE | PM_NOYIELD)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    if (empty2 != NULL)
        free(empty2);
    free(empty);

    fclose(f);

    return 1;
}

static int fdd_id, sb_part;

static int  file_type = 0; /* 0 = IMG, 1 = Japanese FDI, 2 = 86F */
static char fd_file_name[1024];

/* Show a MessageBox dialog.  This is nasty, I know.  --FvK */
static int
new_floppy_msgbox_header(HWND hwnd, int flags, void *header, void *message)
{
    HWND h;
    int  i;

    h        = hwndMain;
    hwndMain = hwnd;

    i = ui_msgbox_header(flags, header, message);

    hwndMain = h;

    return (i);
}

static int
new_floppy_msgbox_ex(HWND hwnd, int flags, void *header, void *message, void *btn1, void *btn2, void *btn3)
{
    HWND h;
    int  i;

    h        = hwndMain;
    hwndMain = hwnd;

    i = ui_msgbox_ex(flags, header, message, btn1, btn2, btn3);

    hwndMain = h;

    return (i);
}

#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
NewFloppyDialogProcedure(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND     h;
    int      i = 0;
    int      wcs_len, ext_offs;
    wchar_t *ext;
    uint8_t  disk_size, rpm_mode;
    int      ret;
    FILE    *f;
    int      zip_types, mo_types, floppy_types;
    wchar_t *twcs;

    switch (message) {
        case WM_INITDIALOG:
            plat_pause(1);
            memset(fd_file_name, 0, 1024);
            h = GetDlgItem(hdlg, IDC_COMBO_DISK_SIZE);
            if (is_zip) {
                zip_types = zip_drives[fdd_id].is_250 ? 2 : 1;
                for (i = 0; i < zip_types; i++)
                    SendMessage(h, CB_ADDSTRING, 0, win_get_string(IDS_5900 + i));
            } else if (is_mo) {
                mo_types = 10;
                /* TODO: Proper string ID's. */
                for (i = 0; i < mo_types; i++)
                    SendMessage(h, CB_ADDSTRING, 0, win_get_string(IDS_5902 + i));
            } else {
                floppy_types = 12;
                for (i = 0; i < floppy_types; i++)
                    SendMessage(h, CB_ADDSTRING, 0, win_get_string(IDS_5888 + i));
            }
            SendMessage(h, CB_SETCURSEL, 0, 0);
            EnableWindow(h, FALSE);
            h = GetDlgItem(hdlg, IDC_COMBO_RPM_MODE);
            for (i = 0; i < 4; i++)
                SendMessage(h, CB_ADDSTRING, 0, win_get_string(IDS_6144 + i));
            SendMessage(h, CB_SETCURSEL, 0, 0);
            EnableWindow(h, FALSE);
            ShowWindow(h, SW_HIDE);
            h = GetDlgItem(hdlg, IDT_FLP_RPM_MODE);
            EnableWindow(h, FALSE);
            ShowWindow(h, SW_HIDE);
            h = GetDlgItem(hdlg, IDOK);
            EnableWindow(h, FALSE);
            h = GetDlgItem(hdlg, IDC_PBAR_IMG_CREATE);
            EnableWindow(h, FALSE);
            ShowWindow(h, SW_HIDE);
            h = GetDlgItem(hdlg, IDT_FLP_PROGRESS);
            EnableWindow(h, FALSE);
            ShowWindow(h, SW_HIDE);
            break;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK:
                    h         = GetDlgItem(hdlg, IDC_COMBO_DISK_SIZE);
                    disk_size = SendMessage(h, CB_GETCURSEL, 0, 0);
                    if (is_zip)
                        disk_size += 12;
                    if (!is_zip && !is_mo && (file_type == 2)) {
                        h        = GetDlgItem(hdlg, IDC_COMBO_RPM_MODE);
                        rpm_mode = SendMessage(h, CB_GETCURSEL, 0, 0);
                        ret      = create_86f(fd_file_name, disk_sizes[disk_size], rpm_mode);
                    } else {
                        if (is_zip)
                            ret = create_zip_sector_image(fd_file_name, disk_sizes[disk_size], file_type, hdlg);
                        if (is_mo)
                            ret = create_mo_sector_image(fd_file_name, disk_size, file_type, hdlg);
                        else
                            ret = create_sector_image(fd_file_name, disk_sizes[disk_size], file_type);
                    }
                    if (ret) {
                        if (is_zip)
                            zip_mount(fdd_id, fd_file_name, 0);
                        else if (is_mo)
                            mo_mount(fdd_id, fd_file_name, 0);
                        else
                            floppy_mount(fdd_id, fd_file_name, 0);
                    } else {
                        new_floppy_msgbox_header(hdlg, MBX_ERROR, (wchar_t *) IDS_4108, (wchar_t *) IDS_4115);
                        return TRUE;
                    }
                    /*FALLTHROUGH*/
                case IDCANCEL:
                    EndDialog(hdlg, 0);
                    plat_pause(0);
                    return TRUE;

                case IDC_CFILE:
                    if (!file_dlg_w(hdlg, plat_get_string(is_mo ? IDS_2139 : (is_zip ? IDS_2055 : IDS_2062)), L"", NULL, 1)) {
                        if (!wcschr(wopenfilestring, L'.')) {
                            if (wcslen(wopenfilestring) && (wcslen(wopenfilestring) <= 256)) {
                                twcs    = &wopenfilestring[wcslen(wopenfilestring)];
                                twcs[0] = L'.';
                                if (!is_zip && !is_mo && (filterindex == 3)) {
                                    twcs[1] = L'8';
                                    twcs[2] = L'6';
                                    twcs[3] = L'f';
                                } else {
                                    twcs[1] = L'i';
                                    twcs[2] = L'm';
                                    twcs[3] = L'g';
                                }
                            }
                        }
                        h = GetDlgItem(hdlg, IDC_EDIT_FILE_NAME);
                        f = _wfopen(wopenfilestring, L"rb");
                        if (f != NULL) {
                            fclose(f);
                            if (new_floppy_msgbox_ex(hdlg, MBX_QUESTION, (wchar_t *) IDS_4111, (wchar_t *) IDS_4118, (wchar_t *) IDS_4120, (wchar_t *) IDS_4121, NULL) != 0) /* yes */
                                return FALSE;
                        }
                        SendMessage(h, WM_SETTEXT, 0, (LPARAM) wopenfilestring);
                        memset(fd_file_name, 0, sizeof(fd_file_name));
                        c16stombs(fd_file_name, wopenfilestring, sizeof(fd_file_name));
                        h = GetDlgItem(hdlg, IDC_COMBO_DISK_SIZE);
                        if (!is_zip || zip_drives[fdd_id].is_250)
                            EnableWindow(h, TRUE);
                        wcs_len  = wcslen(wopenfilestring);
                        ext_offs = wcs_len - 4;
                        ext      = &(wopenfilestring[ext_offs]);
                        if (is_zip) {
                            if (((wcs_len >= 4) && !wcsicmp(ext, L".ZDI")))
                                file_type = 1;
                            else
                                file_type = 0;
                        } else if (is_mo) {
                            if (((wcs_len >= 4) && !wcsicmp(ext, L".MDI")))
                                file_type = 1;
                            else
                                file_type = 0;
                        } else {
                            if (((wcs_len >= 4) && !wcsicmp(ext, L".FDI")))
                                file_type = 1;
                            else if ((((wcs_len >= 4) && !wcsicmp(ext, L".86F")) || (filterindex == 3)))
                                file_type = 2;
                            else
                                file_type = 0;
                        }
                        h = GetDlgItem(hdlg, IDT_FLP_RPM_MODE);
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

    return (FALSE);
}

void
NewFloppyDialogCreate(HWND hwnd, int id, int part)
{
    fdd_id  = id & 0x7f;
    sb_part = part;
    is_zip  = !!(id & 0x80);
    is_mo   = !!(id & 0x100);
    if (is_zip && is_mo) {
        fatal("Attempting to create a new image dialog that is for both ZIP and MO at the same time\n");
        return;
    }
    DialogBox(hinstance, (LPCTSTR) DLG_NEW_FLOPPY, hwnd, NewFloppyDialogProcedure);
}
