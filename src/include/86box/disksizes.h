/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header of the emulation of the PC speaker.
 *
 * Authors: Jasmine Iwanek <jriwanek@gmail.com/>
 *
 *          Copyright 2022-2025 Jasmine Iwanek
 */
#ifndef DISK_SIZES_H
#define DISK_SIZES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct disk_size_t {
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
// clang-format off
#if 0
    { 1,  1, 2, 1, 1,  77, 26, 0, 0,    4, 2, 6,  68 }, /* 250k 8" */
    { 1,  2, 2, 1, 1,  77, 26, 0, 0,    4, 2, 6,  68 }, /* 500k 8" */
    { 1,  1, 2, 1, 1,  77,  8, 3, 0,    1, 2, 2, 192 }, /* 616k 8" */
    { 1,  2, 0, 1, 1,  77,  8, 3, 0,    1, 2, 2, 192 }, /* 1232k 8" */
#endif
    { 0,  1, 2, 1, 0,  40,  8, 2, 0xfe, 2, 2, 1,  64 }, /* 160k */
    { 0,  1, 2, 1, 0,  40,  9, 2, 0xfc, 2, 2, 1,  64 }, /* 180k */
    { 0,  2, 2, 1, 0,  40,  8, 2, 0xff, 2, 2, 1, 112 }, /* 320k */
    { 0,  2, 2, 1, 0,  40,  9, 2, 0xfd, 2, 2, 2, 112 }, /* 360k */
    { 0,  2, 2, 1, 0,  80,  8, 2, 0xfb, 2, 2, 2, 112 }, /* 640k */
    { 0,  2, 2, 1, 0,  80,  9, 2, 0xf9, 2, 2, 3, 112 }, /* 720k */
    { 1,  2, 0, 1, 1,  80, 15, 2, 0xf9, 1, 2, 7, 224 }, /* 1.2M */
    { 1,  2, 0, 1, 1,  77,  8, 3, 0xfe, 1, 2, 2, 192 }, /* 1.25M */
    { 1,  2, 0, 1, 0,  80, 18, 2, 0xf0, 1, 2, 9, 224 }, /* 1.44M */
    { 1,  2, 0, 1, 0,  80, 21, 2, 0xf0, 2, 2, 5,  16 }, /* DMF cluster 1024 */
    { 1,  2, 0, 1, 0,  80, 21, 2, 0xf0, 4, 2, 3,  16 }, /* DMF cluster 2048 */
    { 2,  2, 3, 1, 0,  80, 36, 2, 0xf0, 2, 2, 9, 240 }, /* 2.88M */
    { 0, 64, 0, 0, 0,  96, 32, 2,    0, 0, 0, 0,   0 }, /* ZIP 100 */
    { 0, 64, 0, 0, 0, 239, 32, 2,    0, 0, 0, 0,   0 }, /* ZIP 250 */
#if 0
    { 0,  8, 0, 0, 0, 963, 32, 2,    0, 0, 0, 0,   0 }, /* LS-120 */
    { 0, 32, 0, 0, 0, 262, 56, 2,    0, 0, 0, 0,   0 }  /* LS-240 */
#endif
// clang-format on
};

#ifdef __cplusplus
}
#endif

#endif /*DISK_SIZES_H*/
