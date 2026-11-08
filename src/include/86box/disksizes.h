/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header containing disk sizes.
 *
 * Authors: Jasmine Iwanek <jriwanek@gmail.com/>
 *
 *          Copyright 2022-2026 Jasmine Iwanek
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

#define KNOWN_DISK_SIZES 14
static const disk_size_t disk_sizes[KNOWN_DISK_SIZES] = {
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
    { 0, 64, 0, 0, 0,  96, 32, 2,    0, 0, 0, 0,   0 }, /* ZIP 100: 96 x 64 x 32 = 196608 sectors (Aaru report 312 page 5) */
    { 0, 64, 0, 0, 0, 239, 32, 2,    0, 0, 0, 0,   0 }, /* ZIP 250: 239 x 64 x 32 = 489472 sectors (Aaru reports 52 and 149 page 5);
                                                           the medium itself has 489532, 60 more than its CHS */
#if 0
    /* Removable disk (RDISK) media.  These are not floppy disks and are not used by the floppy code, so they
       stay out of the build (and out of KNOWN_DISK_SIZES); the table is the CHS reference for rdisk.h.

       sides = heads, tracks = cylinders, sector_len 2 = 512 bytes; the BPB columns are not used.
       "measured" means the geometry is what a real drive reported (Aaru device reports, see rdisk.h and
       rdisk.c); "ESTIMATED" means it is not, and the comment on the line says how it was derived.  Unless
       said otherwise the cylinder count of an estimate is the block count / (heads * sectors per track),
       rounded down, so an estimated CHS never describes more sectors than the medium has.  Many real drives
       also report a CHS that is a little short of their block count; the comment gives the difference. */
    /* Alpha-10H track count (306) x 64 sectors of 512 bytes; the head count is assumed.  rdisk has 256-byte blocks */
    { 0,   1, 0, 0, 0,  306,  64, 2,    0, 0, 0, 0,   0 }, /* Bernoulli 10: ESTIMATED, 19584 sectors of 512 bytes (= 39168 blocks of 256) */
    /* the default BPB of BBII-20.DEV (OS/2), in sectors of 512 bytes; rdisk has blocks of 256 bytes */
    { 0,   4, 0, 0, 0,  498,  21, 2,    0, 0, 0, 0,   0 }, /* Bernoulli 20: ESTIMATED, 41832 sectors of 512 bytes (= 83664 blocks of 256) */
    /* the default BPB of BBII-44.DEV (OS/2): 64 heads, 32 sectors per track */
    { 0,  64, 0, 0, 0,   42,  32, 2,    0, 0, 0, 0,   0 }, /* Bernoulli 44: ESTIMATED, 86016 sectors (1024 short of the 87040 blocks) */
    /* Aaru reports 186 and 202 */
    { 0,   8, 0, 0, 0,  758,  29, 2,    0, 0, 0, 0,   0 }, /* Bernoulli 90: measured, 175856 sectors (= blocks) */
    /* the 64 heads and 32 sectors per track of the OS/2 drivers' default BPB, cylinders rounded down */
    { 0,  64, 0, 0, 0,   34,  32, 2,    0, 0, 0, 0,   0 }, /* Bernoulli 35: ESTIMATED, 69632 sectors (22 short of the 69654 blocks) */
    /* the 64 heads and 32 sectors per track of the OS/2 drivers' default BPB, cylinders rounded down */
    { 0,  64, 0, 0, 0,   63,  32, 2,    0, 0, 0, 0,   0 }, /* Bernoulli 65: ESTIMATED, 129024 sectors (66 short of the 129090 blocks) */
    /* the 64 heads and 32 sectors per track of the OS/2 drivers' default BPB, cylinders rounded down */
    { 0,  64, 0, 0, 0,  101,  32, 2,    0, 0, 0, 0,   0 }, /* Bernoulli 105: ESTIMATED, 206848 sectors (8 short of the 206856 blocks) */
    /* the default BPB of BETA-MD.DEV (OS/2): 64 heads, 32 sectors per track */
    { 0,  64, 0, 0, 0,  144,  32, 2,    0, 0, 0, 0,   0 }, /* Bernoulli 150: ESTIMATED, 294912 sectors (6 short of the 294918 blocks) */
    /* the 64 heads and 32 sectors per track of the OS/2 drivers' default BPB, cylinders rounded down */
    { 0,  64, 0, 0, 0,  220,  32, 2,    0, 0, 0, 0,   0 }, /* Bernoulli 230: ESTIMATED, 450560 sectors (6 short of the 450566 blocks) */
    /* the drive reports no geometry (Aaru report 50); virtual 16 x 63 translation, cylinders rounded down */
    { 0,  16, 0, 0, 0,   78,  63, 2,    0, 0, 0, 0,   0 }, /* Clik! 40: ESTIMATED, 78624 sectors (258 short of the block count) */
    /* rdisk.h gives 1024 x 2 x 32; Aaru report 67 only has the block count */
    { 0,   2, 0, 0, 0, 1024,  32, 2,    0, 0, 0, 0,   0 }, /* FD32MB: ESTIMATED, 65536 sectors (= blocks) */
    /* Aaru report 258 reports no geometry; 753 x 2 x 27 is the exact factorization of the measured block count into a plausible geometry */
    { 0,   2, 0, 0, 0,  753,  27, 2,    0, 0, 0, 0,   0 }, /* Floptical 20: ESTIMATED, 40662 sectors (= blocks) */
    /* HiFD 200 heads and sectors per track, cylinders rounded down; HIFD_150_SECTORS is an estimate itself */
    { 0,  13, 0, 0, 0,  662,  34, 2,    0, 0, 0, 0,   0 }, /* HiFD 150: ESTIMATED, 292604 sectors (365 short of the block count) */
    /* Aaru report 118 page 5 */
    { 0,  13, 0, 0, 0,  890,  34, 2,    0, 0, 0, 0,   0 }, /* HiFD 200: measured, 393380 sectors (= blocks); also in Sony's hifdnt4.sys */
    /* no geometry in Aaru report 49; Zip-style 64 x 32, cylinders = blocks / 2048 rounded down */
    { 0,  64, 0, 0, 0, 1021,  32, 2,    0, 0, 0, 0,   0 }, /* Jaz 1 GB: ESTIMATED, 2091008 sectors (42 short of the block count) */
    /* no geometry in Aaru report 147; Zip-style 64 x 32, cylinders = blocks / 2048 rounded down */
    { 0,  64, 0, 0, 0, 1911,  32, 2,    0, 0, 0, 0,   0 }, /* Jaz 2 GB: ESTIMATED, 3913728 sectors (1872 short of the block count) */
    /* Aaru report 200 pages 4 (heads, cylinders) and 3 (sectors per track) */
    { 0, 128, 0, 0, 0,  534,  63, 2,    0, 0, 0, 0,   0 }, /* ORB 2.2 GB: measured, 4306176 sectors (1008 short of the block count) */
    /* The usual ATA 16 x 63 of 2.5 inch disks of these sizes */
    { 0,  16, 0, 0, 0, 9690,  63, 2,    0, 0, 0, 0,   0 }, /* Peerless 5 GB: ESTIMATED, 9767520 sectors (= blocks) */
    { 0,  16, 0, 0, 0, 19485, 63, 2,    0, 0, 0, 0,   0 }, /* Peerless 10 GB: ESTIMATED, 19640880 sectors (= blocks) */
    { 0,  16, 0, 0, 0, 38760, 63, 2,    0, 0, 0, 0,   0 }, /* Peerless 20 GB: ESTIMATED, 39070080 sectors (= blocks) */
    /* 2048-byte blocks.  Page 5 of Aaru reports 148, 187 and 188 has 8 heads and no other geometry; 255 x 63, cylinders rounded down */
    { 0, 255, 0, 0, 0, 1063,  63, 4,    0, 0, 0, 0,   0 }, /* REV 35 GB: ESTIMATED, 17077095 sectors (13785 short of the block count) */
    { 0, 255, 0, 0, 0, 2127,  63, 4,    0, 0, 0, 0,   0 }, /* REV 70 GB: ESTIMATED, 34170255 sectors (11505 short of the block count) */
    { 0, 255, 0, 0, 0, 3648,  63, 4,    0, 0, 0, 0,   0 }, /* REV 120 GB: ESTIMATED, 58605120 sectors (15424 short of the block count) */
    /* Aaru report 66 page 5 */
    { 0,   8, 0, 0, 0,  963,  32, 2,    0, 0, 0, 0,   0 }, /* LS-120: measured, 246528 sectors (= blocks) */
    /* Aaru report 67 page 5 */
    { 0,  32, 0, 0, 0,  262,  56, 2,    0, 0, 0, 0,   0 }, /* LS-240: measured, 469504 sectors (= blocks) */
    /* no per-medium geometry (Aaru report 197); virtual 16 x 63 translation, cylinders rounded down */
    { 0,  16, 0, 0, 0,   86,  63, 2,    0, 0, 0, 0,   0 }, /* SQ400: ESTIMATED, 86688 sectors (12 short of the block count) */
    /* no per-medium geometry (Aaru report 197); virtual 16 x 63 translation, cylinders rounded down */
    { 0,  16, 0, 0, 0,  172,  63, 2,    0, 0, 0, 0,   0 }, /* SQ800: ESTIMATED, 173376 sectors (80 short of the block count) */
    /* no geometry in Aaru report 196; virtual 16 x 63 translation, cylinders rounded down */
    { 0,  16, 0, 0, 0,  213,  63, 2,    0, 0, 0, 0,   0 }, /* SQ310: ESTIMATED, 214704 sectors (736 short of the block count) */
    /* SyQuest's SQ3270A sheet */
    { 0,  16, 0, 0, 0, 1024,  32, 2,    0, 0, 0, 0,   0 }, /* SQ327: ESTIMATED, 524288 sectors (= blocks) */
    /* no per-medium geometry (the SQ5200C reports 2 x 2260 x 96 for all its media, Aaru report 197); virtual 16 x 63 translation, cylinders rounded down */
    { 0,  16, 0, 0, 0,  387,  63, 2,    0, 0, 0, 0,   0 }, /* SQ2000: ESTIMATED, 390096 sectors (600 short of the block count) */
    /* Aaru report 195 block count; CHS 512 / 16 / 32 from a real EZ135A's Linux IDE identification */
    { 0,  16, 0, 0, 0,  512,  32, 2,    0, 0, 0, 0,   0 }, /* EZ135: measured, 262144 sectors (= blocks) */
    /* the EZ135 translation extended to the EZFlyer 230 block count (exact) */
    { 0,  16, 0, 0, 0,  880,  32, 2,    0, 0, 0, 0,   0 }, /* EZFlyer 230: ESTIMATED, 450560 sectors (= blocks) */
    /* 4 heads and 5258 cylinders are the SyJet SCSI Technical Reference's page 4 values; sectors per track are zoned, 139 is the average */
    { 0,   4, 0, 0, 0, 5258, 139, 2,    0, 0, 0, 0,   0 }, /* SyJet 1.5 GB: ESTIMATED, 2923448 sectors (6352 short of the block count) */
    /* Aaru report 175 (CHS 1945 / 16 / 63) */
    { 0,  16, 0, 0, 0, 1945,  63, 2,    0, 0, 0, 0,   0 }, /* SparQ 1 GB: measured, 1960560 sectors (509 short of the block count) */
    /* 8 heads and 32 sectors per track like the LS-120: unlike the 64 heads on real media, that is a whole number of cylinders */
    { 0,   8, 0, 0, 0, 1101,  32, 2,    0, 0, 0, 0,   0 }, /* UHD144: ESTIMATED, 281856 sectors (= blocks) */
    /* a last LBA of BFFFh in GetMediaType() of SCSIDLL.DLL; the Zip 100's 64 x 32 */
    { 0,  64, 0, 0, 0,   24,  32, 2,    0, 0, 0, 0,   0 }, /* ZIP 25: ESTIMATED, 49152 sectors (= blocks) */
    /* 718 cylinders: Aaru reports 53 and 189 page 5, and the raw page of ZuluIDE issue 39 (0x02CE); the block count is the ESTIMATED ZIP_750_SECTORS, within the range ioReady.dll allows */
    { 0,  64, 0, 0, 0,  718,  32, 2,    0, 0, 0, 0,   0 }  /* ZIP 750: measured, 1470464 sectors (60 under the block count, as the Zip 250) */
#endif
// clang-format on
};

#ifdef __cplusplus
}
#endif

#endif /*DISK_SIZES_H*/
