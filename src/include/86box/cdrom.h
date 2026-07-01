/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Generic CD-ROM drive core header.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2019 Miran Grca.
 */
#ifndef EMU_CDROM_H
#define EMU_CDROM_H

#ifndef EMU_VERSION_H
#include <86box/version.h>
#endif

#define CDROM_NUM                   8

#define CD_STATUS_EMPTY             0
#define CD_STATUS_DATA_ONLY         1
#define CD_STATUS_DVD               2
#define CD_STATUS_PAUSED            4
#define CD_STATUS_PLAYING           5
#define CD_STATUS_STOPPED           6
#define CD_STATUS_PLAYING_COMPLETED 7
#define CD_STATUS_HOLD              8
#define CD_STATUS_DVD_REJECTED      16
#define CD_STATUS_HAS_AUDIO         0xc
#define CD_STATUS_MASK              0x1f

/* Medium changed flag. */
#define CD_STATUS_TRANSITION     0x40
#define CD_STATUS_MEDIUM_CHANGED 0x80

#define CD_TRACK_UNK_DATA        0x04
#define CD_TRACK_NORMAL          0x00
#define CD_TRACK_AUDIO           0x08
#define CD_TRACK_CDI             0x10
#define CD_TRACK_XA              0x20
#define CD_TRACK_MODE_MASK       0x30
#define CD_TRACK_MODE2           0x04
#define CD_TRACK_MODE2_MASK      0x07

#define CD_TOC_NORMAL            0
#define CD_TOC_SESSION           1
#define CD_TOC_RAW               2

#define CD_IMAGE_HISTORY         10

#define CDROM_IMAGE              200

/* This is so that if/when this is changed to something else,
   changing this one define will be enough. */
#define CDROM_EMPTY              !dev->host_drive

#define DVD_LAYER_0_SECTORS      0x00210558ULL

#define RAW_SECTOR_SIZE          2352
#define COOKED_SECTOR_SIZE       2048

#define CD_BUF_SIZE              (16 * RAW_SECTOR_SIZE)

#define DATA_TRACK               0x14
#define AUDIO_TRACK              0x10

#define CD_FPS                   75

#define _LUT_SIZE                0x100

#define FRAMES_TO_MSF(f, M, S, F)                 \
    {                                             \
        uint64_t value = f;                       \
        *(F)           = (value % CD_FPS) & 0xff; \
        value /= CD_FPS;                          \
        *(S) = (value % 60) & 0xff;               \
        value /= 60;                              \
        *(M) = value & 0xff;                      \
    }
#define MSF_TO_FRAMES(M, S, F) ((M) *60 * CD_FPS + (S) *CD_FPS + (F))

typedef struct SMSF {
    uint16_t min;
    uint8_t  sec;
    uint8_t  fr;
} TMSF;

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CDROM_BUS_DISABLED =  0,
    CDROM_BUS_PHILIPS  =  1,
    CDROM_BUS_SONY     =  2,
    CDROM_BUS_HITACHI  =  3,
    CDROM_BUS_MKE      =  4,
    CDROM_BUS_MITSUMI  =  5,
    CDROM_BUS_LPT      =  6,
    CDROM_BUS_ATAPI    =  8,
    CDROM_BUS_SCSI     =  9,
    CDROM_BUS_USB      = 10
};

#define BUS_TYPE_MKE                CDROM_BUS_MKE
#define BUS_TYPE_IDE                CDROM_BUS_ATAPI
#define BUS_TYPE_SCSI               CDROM_BUS_SCSI
#define BUS_TYPE_BOTH              -2
#define BUS_TYPE_NONE              -1

#define CDV                         EMU_VERSION_EX

static const struct cdrom_drive_types_s {
    const char   *vendor;
    const char   *model;
    const char   *revision;
    const char   *bios_name;
    const char   *internal_name;
    const int     bus_type;
    /* SCSI standard for SCSI (or both) devices, early for IDE. */
    const int     scsi_std;
    const int     speed;
    const int     inquiry_len;
    const int     caddy;
    const int     is_dvd;
    const int     transfer_max[4];
} cdrom_drive_types[] = {
    { EMU_NAME,   "86B_CD",           CDV,    "",          "86cd",           BUS_TYPE_BOTH, 2, -1, 36, 0, 0, {  4,  2,  2,  5 } },
    { EMU_NAME,   "86B_CD",           "1.00", "",          "86cd100",        BUS_TYPE_BOTH, 1, -1, 36, 1, 0, {  0, -1, -1, -1 } }, /* SCSI-1 / early ATAPI generic - second on purpose so the later variant is the default. */
    { EMU_NAME,   "86B_DVD",          "5.00", "",          "86dvd",          BUS_TYPE_BOTH, 2, -1, 36, 0, 1, {  4,  2,  2,  5 } },
    { "ACER",     "656A",             "8.4D", "656A 043",  "acer_656a",      BUS_TYPE_IDE,  0,  8, 36, 0, 0, {  3,  2,  2, -1 } },
    { "ACER",     "8432IA",           "5.AX", "",          "acer_8432ia",    BUS_TYPE_IDE,  0, 32, 36, 0, 0, {  4,  2,  2,  2 } }, /* TODO: to find the real dump of this CD-ROM model. */
    { "AOpen",    "CD-924E",          "A205", "",          "aopen_924e",     BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  4,  2,  2,  0 } },
    { "AOpen",    "CD-948E",          "4.02", "",          "aopen_948e",     BUS_TYPE_IDE,  0, 48, 36, 0, 0, {  4,  2,  2,  2 } },
    { "AOpen",    "CD-952E",          "2.01", "",          "aopen_952e",     BUS_TYPE_IDE,  0, 52, 36, 0, 0, {  4,  2,  2,  4 } },
    { "AOpen",    "CD-956E",          "2480", "",          "aopen_956e",     BUS_TYPE_IDE,  0, 56, 36, 0, 0, {  4,  2,  2,  4 } },
    { "AOpen",    "DVD-9632",         "1.15", "",          "aopen_9632",     BUS_TYPE_IDE,  0, 32, 36, 0, 1, {  4,  2,  2,  2 } },
    { "ASUS",     "CD-S500/A",        "1.41", "",          "asus_500",       BUS_TYPE_IDE,  0, 50, 36, 0, 0, {  4,  2,  2,  2 } },
    { "ASUS",     "CD-S520/A4",       "1.6K", "",          "asus_520",       BUS_TYPE_IDE,  0, 52, 36, 0, 0, {  4,  2,  2,  4 } },
    { "ASUS",     "DVD-E616P2",       "1.08", "",          "asus_e616",      BUS_TYPE_IDE,  0, 48, 36, 0, 1, {  4,  2,  2,  4 } },
    { "AZT",      "CDA46802I",        "1.15", "",          "azt_cda",        BUS_TYPE_IDE,  0,  4, 36, 0, 0, {  3,  0,  0,  0 } },
    { "BENQ",     "CD-656A",          "56AI", "",          "benq_656a",      BUS_TYPE_IDE,  0, 56, 36, 0, 0, {  4,  2,  2,  2 } },
    { "BTC",      "CD-ROM BCD16XA",   "U2.2", "",          "btc_16xa",       BUS_TYPE_IDE,  0, 16, 36, 0, 0, {  4,  2,  2, -1 } },
    { "BTC",      "CD-ROM BCD24X",    "U2.0", "",          "btc_24x",        BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  4,  2,  2,  0 } },
    { "BTC",      "CD-ROM BCD24XHM",  "V1.0", "",          "btc_24xhm",      BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  4,  2,  2,  1 } }, /* Later version of BCD24X */
    { "BTC",      "CD-ROM BCD36XH",   "U1.0", "",          "btc_36xh",       BUS_TYPE_IDE,  0, 36, 36, 0, 0, {  4,  2,  2,  1 } },
    { "CREATIVE", "CD2422E",          "MC10", "",          "creative_2422",  BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  4,  2,  2,  2 } },
    { "CREATIVE", "CD3621E",          "ZC10", "",          "creative_3621",  BUS_TYPE_IDE,  0, 36, 36, 0, 0, {  4,  2,  2,  2 } },
    { "CREATIVE", "CD5220E",          "2.02", "",          "creative_5220",  BUS_TYPE_IDE,  0, 52, 36, 0, 0, {  4,  2,  2,  4 } },
    { "ECS",      "300ESD",           "V200", "",          "ecs_300",        BUS_TYPE_IDE,  0,  3, 36, 0, 0, {  2, -1, -1, -1 } }, /* Firmware revision not yet confirmed */
    { "ECS",      "600ESD",           "V300", "",          "ecs_600",        BUS_TYPE_IDE,  0,  6, 36, 0, 0, {  3, -1, -1, -1 } },
    { "GOLDSTAR", "CRD-8160B",        "3.14", "",          "goldstar",       BUS_TYPE_IDE,  0, 16, 36, 0, 0, {  4,  2,  1, -1 } },
    { "GOLDSTAR", "CRD-8240B",        "1.11", "",          "goldstar_8240b", BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  4,  2,  1, -1 } },
    { "GOLDSTAR", "CRD-8320B",        "1.10", "",          "goldstar_8320b", BUS_TYPE_IDE,  0, 32, 36, 0, 0, {  4,  2,  1, -1 } },
    { "GOLDSTAR", "CRD-8400B",        "1.12", "",          "goldstar_8400b", BUS_TYPE_IDE,  0, 40, 36, 0, 0, {  4,  2,  2, -1 } },
    { "GOLDSTAR", "CRD-8484B",        "1.03", "",          "goldstar_8484b", BUS_TYPE_IDE,  0, 48, 36, 0, 0, {  4,  2,  2,  2 } },
    { "GOLDSTAR", "GCD-R542B",        "1.20", "",          "goldstar_r542b", BUS_TYPE_IDE,  0,  4, 36, 0, 0, {  3,  2,  1, -1 } },
    { "GOLDSTAR", "GCD-R560B",        "1.00", "",          "goldstar_r560b", BUS_TYPE_IDE,  0,  6, 36, 0, 0, {  3,  2,  2, -1 } },
    { "GOLDSTAR", "GCD-R580B",        "1.04", "",          "goldstar_r580b", BUS_TYPE_IDE,  0,  8, 36, 0, 0, {  3,  2,  2, -1 } },
    { "HITACHI",  "CDR-7930",         "A5  ", "",          "hitachi_r7930",  BUS_TYPE_IDE,  0,  8, 36, 0, 0, {  4,  2,  2, -1 } },
    { "HITACHI",  "CDR-8130",         "0020", "",          "hitachi_r8130",  BUS_TYPE_IDE,  0, 16, 36, 0, 0, {  4,  2,  2, -1 } },
    { "HITACHI",  "CDR-8330",         "0007", "",          "hitachi_r8330",  BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  4,  2,  2, -1 } },
    { "HITACHI",  "CDR-8435",         "0010", "",          "hitachi_r8435",  BUS_TYPE_IDE,  0, 32, 36, 0, 0, {  4,  2,  2, -1 } },
    { "HITACHI",  "DVD-ROM GD-2000",  "A012", "",          "hitachi_2000",   BUS_TYPE_IDE,  0, 20, 36, 0, 1, {  4,  2,  2,  2 } },
    { "HITACHI",  "DVD-ROM GD-2500",  "0101", "",          "hitachi_2500",   BUS_TYPE_IDE,  0, 24, 36, 0, 1, {  4,  2,  2,  2 } },
    { "HITACHI",  "GD-7500",          "A1  ", "",          "hitachi_7500",   BUS_TYPE_IDE,  0, 40, 36, 0, 1, {  4,  2,  2,  4 } },
    { "HL-DT-ST", "CD-ROM GCR-8526B", "1.01", "",          "hldtst_8526b",   BUS_TYPE_IDE,  0, 52, 36, 0, 0, {  4,  2,  2,  2 } },
    { "HL-DT-ST", "DVDROM GDR-8082N", "0002", "",          "hldtst_8082",    BUS_TYPE_IDE,  0, 24, 36, 0, 1, {  4,  2,  2,  2 } },
    { "HL-DT-ST", "DVDROM GDR-8163B", "0L23", "",          "hldtst_8163",    BUS_TYPE_IDE,  0, 52, 36, 0, 1, {  4,  2,  2,  4 } }, /* DVD version of GCR-8526B */
    { "HL-DT-ST", "DVDRAM GSA-4160B", "A302", "",          "hldtst_4160",    BUS_TYPE_IDE,  0, 40, 36, 0, 1, {  4,  2,  2,  2 } },
    { "HL-DT-ST", "DVDRAM GSA-H42L",  "SL01", "",          "hldtst_h42l",    BUS_TYPE_IDE,  0, 48, 36, 0, 1, {  4,  2,  2,  5 } },
    { "HP",       "7200e",            "1.34", "",          "hp_7200",        BUS_TYPE_IDE,  0,  6, 36, 0, 0, {  3,  1,  1,  1 } },
    { "HP",       "DVD1040i",         "4H13", "",          "hp_1040i",       BUS_TYPE_IDE,  0, 48, 36, 0, 1, {  4,  2,  2,  2 } },
    { "KENWOOD",  "CD-ROM UCR-421",   "208E", "",          "kenwood_421",    BUS_TYPE_IDE,  0, 72, 36, 0, 0, {  4,  2,  2,  4 } },
    { "LEOPTICS", "CD-ROM 24X",       "4.6C", "",          "leoptics_24x",   BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  4,  2,  2,  2 } },
    { "LG",       "CD-ROM CRD-8160B", "1.15", "",          "lg_8160b",       BUS_TYPE_IDE,  0, 16, 36, 0, 0, {  4,  2,  1, -1 } }, /* Later version of GoldStar CRD-8160B */
    { "LG",       "CD-ROM CRD-8240B", "1.19", "",          "lg_8240b",       BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  4,  2,  1, -1 } }, /* Later version of GoldStar CRD-8240B */
    { "LG",       "CD-ROM CRN-8245B", "1.30", "",          "lg_8245b",       BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  4,  2,  2, -1 } }, /* Notebook version of CRD-8240B */
    { "LG",       "CD-ROM CRD-8322B", "1.24", "",          "lg_8322b",       BUS_TYPE_IDE,  0, 32, 36, 0, 0, {  4,  2,  2,  0 } },
    { "LG",       "CD-ROM CRD-8400C", "1.02", "",          "lg_8400c",       BUS_TYPE_IDE,  0, 40, 36, 0, 0, {  4,  2,  2,  2 } },
    { "LG",       "CD-ROM CRD-8482B", "1.00", "",          "lg_8482b",       BUS_TYPE_IDE,  0, 48, 36, 0, 0, {  4,  2,  2,  2 } },
    { "LG",       "CD-ROM CRD-8522B", "2.03", "",          "lg_8522b",       BUS_TYPE_IDE,  0, 52, 36, 0, 0, {  4,  2,  2,  4 } },
    { "LG",       "DVDROM DRD-820B",  "1.04", "",          "lg_d820b",       BUS_TYPE_IDE,  0, 24, 36, 0, 1, {  4,  2,  2,  2 } },
    { "LG",       "DVDROM DRD-8160B", "1.01", "",          "lg_d8160b",      BUS_TYPE_IDE,  0, 48, 36, 0, 1, {  4,  2,  2,  4 } },
    { "LITEON",  "CD-ROM LTN204",     "1017", "LTN204",    "liteon_204",     BUS_TYPE_IDE,  0, 20, 36, 0, 0, {  4,  2,  2,  1 } },
    { "LITEON",  "CD-ROM LTN242",     "2048", "LTN242",    "liteon_242",     BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  4,  2,  2,  1 } },
    { "LITEON",  "CD-ROM LTN242",     "HP12", "LTN242",    "liteon_ltn242",  BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  4,  2,  2,  2 } },
    { "LITEON",  "CD-ROM LTN301",     "1027", "LTN301",    "liteon_301",     BUS_TYPE_IDE,  0, 32, 36, 0, 0, {  4,  2,  2,  1 } },
    { "LITEON",  "CD-ROM LTN382",     "2019", "LTN382",    "liteon_382",     BUS_TYPE_IDE,  0, 40, 36, 0, 0, {  4,  2,  2,  1 } }, /* Early version of LTN-403L */
    { "LITEON",  "CD-ROM LTN403L",    "DQ19", "LTN403L",   "liteon_403",     BUS_TYPE_IDE,  0, 40, 36, 0, 0, {  4,  2,  2,  2 } },
    { "LITEON",  "CD-ROM LTR48125S",  "1S07", "LTR48125S", "liteon_48125s",  BUS_TYPE_IDE,  0, 48, 36, 0, 0, {  4,  2,  2,  2 } },
    { "LITEON",  "CD-ROM LTN526D",    "YSR5", "LTN526D",   "liteon_526d",    BUS_TYPE_IDE,  0, 52, 36, 0, 0, {  4,  2,  2,  2 } }, /* Confirmed to be 52x, was the basis for deducing the other one's speed. */
    { "LITEON",  "CD-ROM LTD166",     "9S14", "LTD166",    "liteon_166d",    BUS_TYPE_IDE,  0, 48, 36, 0, 1, {  4,  2,  2,  4 } },
    { "MAD DOG",  "LIGHTNING 56X",    "1.0 ", "",          "maddog_56x",     BUS_TYPE_IDE,  0, 56, 36, 0, 0, {  4,  2,  2,  4 } }, /* TODO: to find the real dump of this CD-ROM model. */
    { "MAD DOG",  "ENTERTAINER 16X",  "1.0 ", "",          "maddog_16x",     BUS_TYPE_IDE,  0, 48, 36, 0, 1, {  4,  2,  2,  4 } }, /* TODO: to find the real dump of this CD-ROM model. */
    { "MATSHITA", "CR-571",           "1.0e", "",          "matshita_571",   BUS_TYPE_IDE,  0,  2, 36, 0, 0, {  0, -1, -1, -1 } },
    { "MATSHITA", "CR-572",           "1.0j", "",          "matshita_572",   BUS_TYPE_IDE,  0,  4, 36, 0, 0, {  0, -1, -1, -1 } },
    { "MATSHITA", "CR-574",           "P.11", "",          "matshita_574",   BUS_TYPE_IDE,  0,  4, 36, 0, 0, {  2, -1, -1, -1 } },
    { "MATSHITA", "CD-ROM CR-581-B",  "1.05", "",          "matshita_581",   BUS_TYPE_IDE,  0,  4, 36, 0, 0, {  4,  2,  2, -1 } },
    { "MATSHITA", "CD-ROM CR-583",    "1.07", "",          "matshita_583",   BUS_TYPE_IDE,  0,  8, 36, 0, 0, {  3,  2,  1, -1 } },
    { "MATSHITA", "CD-ROM CR-585",    "Z18P", "",          "matshita_585",   BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  4,  2,  2,  0 } }, /* Early version of CR-587(?) */
    { "MATSHITA", "CD-ROM CR-587",    "7S13", "",          "matshita_587",   BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  4,  2,  2,  2 } },
    { "MATSHITA", "CD-ROM CR-588",    "LS15", "",          "matshita_588",   BUS_TYPE_IDE,  0, 32, 36, 0, 0, {  4,  2,  2,  2 } },
    { "MATSHITA", "CD-ROM CR-594-C",  "PA05", "",          "matshita_594",   BUS_TYPE_IDE,  0, 48, 36, 0, 0, {  4,  2,  2,  2 } },
    { "MATSHITA", "DVD-ROM SR-8587",  "CA5B", "",          "matshita_8587",  BUS_TYPE_IDE,  0, 48, 36, 0, 1, {  4,  2,  2,  4 } },
    { "MATSHITA", "DVD-ROM SR-8584",  "e15C", "",          "matshita_8584",  BUS_TYPE_IDE,  0, 52, 36, 0, 1, {  4,  2,  2,  4 } },
    { "MITSUMI",  "FX400E",           "K02 ", "",          "mitsumi_400e",   BUS_TYPE_IDE,  0,  4, 36, 0, 0, {  3,  2,  2, -1 } }, /* Early version of CRMC-FX400G */
    { "MITSUMI",  "CRMC-FX400G",      "l07 ", "",          "mitsumi_400c",   BUS_TYPE_IDE,  0,  4, 36, 0, 0, {  4,  2,  2, -1 } },
    { "MITSUMI",  "CRMC-FX600S",      "p07 ", "",          "mitsumi_600s",   BUS_TYPE_IDE,  0,  6, 36, 0, 0, {  4,  2,  2, -1 } },
    { "MITSUMI",  "CRMC-FX810T4",     "a03 ", "",          "mitsumi_810t4",  BUS_TYPE_IDE,  0,  8, 36, 0, 0, {  4,  2,  2, -1 } },
    { "MITSUMI",  "CRMC-FX120T",      "w02 ", "",          "mitsumi_120t",   BUS_TYPE_IDE,  0, 12, 36, 0, 0, {  4,  2,  2, -1 } },
    { "MITSUMI",  "CRMC-FX240S",      "g05 ", "",          "mitsumi_240s",   BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  4,  2,  2, -1 } },
    { "MITSUMI",  "CRMC-FX322M",      "p01 ", "",          "mitsumi_322m",   BUS_TYPE_IDE,  0, 32, 36, 0, 0, {  4,  2,  2, -1 } },
    { "MITSUMI",  "CR-480ATE",        "1.0E", "",          "mitsumi_480ate", BUS_TYPE_IDE,  0, 40, 36, 0, 0, {  4,  2,  2,  1 } },
    { "MITSUMI",  "CRMC-FX4820T",     "D02A", "",          "mitsumi_4820t",  BUS_TYPE_IDE,  0, 48, 36, 0, 0, {  4,  2,  2,  2 } },
    { "NEC",      "CD-ROM DRIVE:260", "1.00", "",          "nec_260_early",  BUS_TYPE_IDE,  1,  2, 36, 1, 0, {  0, -1, -1, -1 } },
    { "NEC",      "CD-ROM DRIVE:260", "1.01", "",          "nec_260",        BUS_TYPE_IDE,  1,  4, 36, 1, 0, {  0, -1, -1, -1 } },
    { "NEC",      "CD-ROM DRIVE:272", "3.02", "",          "nec_272",        BUS_TYPE_IDE,  0,  4, 36, 0, 0, {  0, -1, -1, -1 } },
    { "NEC",      "CD-ROM DRIVE:273", "4.20", "",          "nec_273_early",  BUS_TYPE_IDE,  0,  4, 36, 0, 0, {  2, -1, -1, -1 } },
    { "NEC",      "CD-ROM DRIVE:273", "4.25", "",          "nec_273",        BUS_TYPE_IDE,  0,  4, 36, 0, 0, {  3, -1, -1, -1 } },
    { "NEC",      "CD-ROM DRIVE:280", "1.05", "",          "nec_280_early",  BUS_TYPE_IDE,  0,  6, 36, 1, 0, {  3,  2,  2, -1 } },
    { "NEC",      "CD-ROM DRIVE:280", "3.08", "",          "nec_280",        BUS_TYPE_IDE,  0,  8, 36, 1, 0, {  4,  2,  1, -1 } },
    { "NEC",      "CD-ROM DRIVE:289", "1.00", "",          "nec_289",        BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  4,  2,  2,  0 } },
    { "NEC",      "CDR-1300A",        "1.05", "",          "nec_1300a",      BUS_TYPE_IDE,  0,  6, 36, 0, 0, {  4,  2,  2, -1 } },
    { "NEC",      "CDR-1801A",        "J111", "",          "nec_1801a",      BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  4,  2,  2,  1 } },
    { "NEC",      "CDR-1900A",        "1.00", "",          "nec_1900a",      BUS_TYPE_IDE,  0, 32, 36, 0, 0, {  4,  2,  2,  1 } },
    { "NEC",      "CDR-3002A",        "C000", "",          "nec_3002a",      BUS_TYPE_IDE,  0, 52, 36, 0, 0, {  4,  2,  2,  4 } },
    { "NEC",      "ND-1300A",         "1.0B", "",          "nec_d1300a",     BUS_TYPE_IDE,  0, 40, 36, 0, 1, {  4,  2,  2,  5 } },
    { "NEC",      "ND-3500A",         "2.1A", "",          "nec_d3500a",     BUS_TYPE_IDE,  0, 48, 36, 0, 1, {  4,  2,  2,  5 } }, /* 48x version of ND-1300A */
    { "NEWMAX",   "CCD-7120",         "4.00", "",          "newmax_7120",    BUS_TYPE_IDE,  0, 16, 36, 0, 0, {  4,  2,  2, -1 } },
    { "OCTEK",    "CDR-810",          "1020", "",          "octek_810",      BUS_TYPE_IDE,  0, 10, 36, 0, 0, {  3,  2,  2, -1 } },
    { "PHILIPS",  "CD-ROM PCA323CD",  "2.5 ", "",          "philips_323",    BUS_TYPE_IDE,  0, 32, 36, 0, 0, {  4,  2,  2, -1 } },
    { "PHILIPS",  "CDD4401/31",       "C1.7", "",          "philips_4401",   BUS_TYPE_IDE,  0, 32, 36, 0, 0, {  4,  2,  2,  1 } },
    { "PHILIPS",  "CD-ROM PCA403CD",  "U31P", "",          "philips_403",    BUS_TYPE_IDE,  0, 40, 36, 0, 0, {  4,  2,  2,  1 } },
    { "PHILIPS",  "CDD4801/71",       "C1.3", "",          "philips_4801",   BUS_TYPE_IDE,  0, 48, 36, 0, 0, {  4,  2,  2,  2 } },
    { "PIONEER",  "CD-ROM DR-A12X",   "1.00", "",          "pioneer_a12x",   BUS_TYPE_IDE,  0, 12, 36, 0, 0, {  4,  2,  1, -1 } },
    { "PIONEER",  "CD-ROM DR-U24X",   "1.00", "",          "pioneer_u24x",   BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  4,  2,  2 , 0 } },
    { "PIONEER",  "DVD-115LH",        "1.24", "",          "pioneer_115lh",  BUS_TYPE_IDE,  0, 40, 36, 0, 1, {  4,  2,  2 , 2 } },
    { "PIONEER",  "DVD-121",          "196L", "",          "pioneer_121",    BUS_TYPE_IDE,  0, 40, 36, 0, 1, {  4,  2,  2 , 4 } }, /* Later(?) variant of DVD-115LH */
    { "PIONEER",  "DVD-RAM DVR-MCC",  "1.00", "",          "pioneer_mcc",    BUS_TYPE_IDE,  0, 24, 36, 0, 1, {  4,  2,  2 , 4 } },
    { "PIONEER",  "DVD-RAM DVR-106D", "1.08", "",          "pioneer_106d",   BUS_TYPE_IDE,  0, 32, 36, 0, 1, {  4,  2,  2 , 5 } },
    { "PIONEER",  "DVD-RAM DVR-110D", "1.41", "",          "pioneer_110d",   BUS_TYPE_IDE,  0, 40, 36, 0, 1, {  4,  2,  2 , 5 } }, /* 40x version of DVR-106D */
    { "PLEXTOR",  "DVD-ROM PX-800A",  "81HA", "",          "pioneer_800a",   BUS_TYPE_IDE,  0, 48, 36, 0, 1, {  4,  2,  2 , 2 } },
    { "RICOH",    "MP7040A",          "1.60", "",          "ricoh_7040",     BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  4,  2,  2,  2 } },
    { "SAMSUNG",  "CD-ROM SCR-3231",  "S101", "",          "samsung_3231",   BUS_TYPE_IDE,  0, 32, 36, 0, 0, {  4,  2,  2, -1 } },
    { "SAMSUNG",  "CD-ROM SC-140",    "BS14", "",          "samsung_140",    BUS_TYPE_IDE,  0, 40, 36, 0, 0, {  4,  2,  2,  0 } },
    { "SAMSUNG",  "CD-ROM SC-148F",   "PS07", "",          "samsung_148f",   BUS_TYPE_IDE,  0, 48, 36, 0, 0, {  4,  2,  2,  2 } },
    { "SAMSUNG",  "CD-ROM SC-152",    "C400", "",          "samsung_152",    BUS_TYPE_IDE,  0, 52, 36, 0, 0, {  4,  2,  2,  4 } },
    { "SAMSUNG",  "DVD-ROM SD-616E",  "F503", "",          "samsung_616e",   BUS_TYPE_IDE,  0, 48, 36, 0, 1, {  4,  2,  2,  2 } },
    { "SAMSUNG",  "DVD-ROM SH-D162C", "TS05", "",          "samsung_162c",   BUS_TYPE_IDE,  0, 48, 36, 0, 1, {  4,  2,  2,  4 } }, /* Later variant of SD-616E */
    { "SANYO",    "CRD-254P",         "1.05", "",          "sanyo_crd254p",  BUS_TYPE_IDE,  0,  4, 36, 0, 0, {  2, -1, -1, -1 } },
    { "SANYO",    "CRD-820P",         "1.04", "",          "sanyo_crd820p",  BUS_TYPE_IDE,  0, 20, 36, 0, 0, {  3,  2,  2, -1 } },
    { "SONY",     "CD-ROM CDU76",     "1.0i", "",          "sony_76",        BUS_TYPE_IDE,  0,  4, 36, 0, 0, {  2, -1, -1, -1 } },
    { "SONY",     "CD-ROM CDU311",    "3.0h", "",          "sony_311",       BUS_TYPE_IDE,  0,  8, 36, 0, 0, {  3,  2,  1, -1 } },
    { "SONY",     "CD-ROM CDU611",    "2.2c", "",          "sony_611",       BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  3,  2,  2, -1 } },
    { "SONY",     "CD-ROM CDU4011",   "B2BA", "",          "sony_4011",      BUS_TYPE_IDE,  0, 40, 36, 0, 0, {  4,  2,  2,  1 } },
    { "SONY",     "CD-ROM CDU5225",   "NYS4", "",          "sony_5225",      BUS_TYPE_IDE,  0, 52, 36, 0, 0, {  4,  2,  2,  4 } },
    { "SONY",     "DVD-ROM DRU-530A", "U305", "",          "sony_530a",      BUS_TYPE_IDE,  0, 40, 36, 0, 1, {  4,  2,  2,  2 } },
    { "SONY",     "DVD-ROM DRU-710A", "11BA", "",          "sony_710a",      BUS_TYPE_IDE,  0, 48, 36, 0, 1, {  4,  2,  2,  2 } },
    { "SONY",     "DVD-ROM DRU-810A", "71BD", "",          "sony_810a",      BUS_TYPE_IDE,  0, 48, 36, 0, 1, {  4,  2,  2,  4 } }, /* Updated version of DRU-710A */
    { "SONY",     "DVD-ROM DDU1612",  "BA01", "",          "sony_1612",      BUS_TYPE_IDE,  0, 48, 36, 0, 1, {  4,  2,  2,  4 } },
    { "SONY",     "DVD-ROM DDU1615",  "B2EE", "",          "sony_1615",      BUS_TYPE_IDE,  0, 48, 36, 0, 1, {  4,  2,  2,  5 } }, /* Updated version of DDU1612 */
    { "TEAC",     "CD 55A",           "2.10", "",          "teac_55a",       BUS_TYPE_IDE,  1,  4, 36, 0, 0, {  2, -1, -1, -1 } }, /* Firmware revision confirmed in its manual, although it's not %100 confirmed yet. */
    { "TEAC",     "CD-SN250",         "N.0A", "",          "teac_520",       BUS_TYPE_IDE,  0, 10, 36, 0, 0, {  3,  2,  1,  0 } },
    { "TEAC",     "CD-516E",          "1.0G", "",          "teac_516e",      BUS_TYPE_IDE,  0, 16, 36, 0, 0, {  3,  2,  2,  1 } },
    { "TEAC",     "CD-224E",          "4.0D", "",          "teac_224e",      BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  4,  2,  2,  1 } }, /* Slimline CD-ROM drive */
    { "TEAC",     "CD-524EA",         "3.0D", "",          "teac_524ea",     BUS_TYPE_IDE,  0, 24, 36, 0, 0, {  4,  2,  2,  2 } },
    { "TEAC",     "CD-532E",          "2.0A", "",          "teac_532e",      BUS_TYPE_IDE,  0, 32, 36, 0, 0, {  3,  2,  2, -1 } },
    { "TEAC",     "CD-532EA",         "3.0A", "",          "teac_532ea",     BUS_TYPE_IDE,  0, 32, 36, 0, 0, {  4,  2,  2,  2 } },
    { "TEAC",     "CD-540E",          "2.0U", "",          "teac_540e",      BUS_TYPE_IDE,  0, 40, 36, 0, 0, {  4,  2,  2,  2 } },
    { "TEAC",     "CD-P520E",         "2.0R", "",          "teac_520e",      BUS_TYPE_IDE,  0, 52, 36, 0, 0, {  4,  2,  2,  4 } },
    { "TEAC",     "DV-516D",          "1.0A", "",          "teac_516d",      BUS_TYPE_IDE,  0, 48, 36, 0, 1, {  4,  2,  2,  2 } }, /* Firmware revision not yet confirmed */
    { "TOSHIBA",  "CD-ROM XM-5302TA", "0305", "",          "toshiba_5302ta", BUS_TYPE_IDE,  0,  4, 96, 0, 0, {  0, -1, -1, -1 } },
    { "TOSHIBA",  "CD-ROM XM-1502B",  "RA70", "",          "toshiba_1502b",  BUS_TYPE_IDE,  0, 10, 96, 0, 0, {  3,  2,  1, -1 } }, /* Slimline CD-ROM drive */
    { "TOSHIBA",  "CD-ROM XM-5702B",  "TA70", "",          "toshiba_5702b",  BUS_TYPE_IDE,  0, 12, 96, 0, 0, {  3,  2,  1, -1 } },
    { "TOSHIBA",  "CD-ROM XM-6002B",  "VE70", "",          "toshiba_6002b",  BUS_TYPE_IDE,  0, 16, 96, 0, 0, {  3,  2,  2, -1 } },
    { "TOSHIBA",  "CD-ROM XM-6102B",  "WA70", "",          "toshiba_6102b",  BUS_TYPE_IDE,  0, 24, 96, 0, 0, {  3,  2,  2, -1 } },
    { "TOSHIBA",  "CD-ROM SD-C2612",  "1D21", "",          "toshiba_c2612",  BUS_TYPE_IDE,  0, 24, 96, 0, 0, {  4,  2,  2,  1 } }, /* Slimline version of XM-6102B */
    { "TOSHIBA",  "CD-ROM XM-6202B",  "1512", "",          "toshiba_6202b",  BUS_TYPE_IDE,  0, 32, 96, 0, 0, {  4,  2,  2,  0 } },
    { "TOSHIBA",  "CD-ROM XM-6402B",  "1008", "",          "toshiba_6402b",  BUS_TYPE_IDE,  0, 32, 96, 0, 0, {  4,  2,  2,  2 } }, /* Updated version of XM-6202B */
    { "TOSHIBA",  "CD-ROM XM-6502B",  "1013", "",          "toshiba_6502b",  BUS_TYPE_IDE,  0, 40, 96, 0, 0, {  4,  2,  2,  2 } },
    { "TOSHIBA",  "CD-ROM XM-6702B",  "1007", "",          "toshiba_6702b",  BUS_TYPE_IDE,  0, 48, 96, 0, 0, {  4,  2,  2,  2 } },
    { "TOSHIBA",  "DVD-ROM SD-M1202", "1020", "",          "toshiba_m1202",  BUS_TYPE_IDE,  0, 32, 96, 0, 1, {  4,  2,  2,  2 } },
    { "TOSHIBA",  "DVD-ROM SD-M1402", "1010", "",          "toshiba_m1402",  BUS_TYPE_IDE,  0, 40, 96, 0, 1, {  4,  2,  2,  2 } },
    { "TOSHIBA",  "DVD-ROM SD-M1802", "1B08", "",          "toshiba_m1802",  BUS_TYPE_IDE,  0, 48, 96, 0, 1, {  4,  2,  2,  2 } },
    { "TOSHIBA",  "DVD-ROM SD-M1912", "TM01", "",          "toshiba_m1912",  BUS_TYPE_IDE,  0, 48, 96, 0, 1, {  4,  2,  2,  4 } }, /* DVD-ROM model produced under their TSST brand */
    { "TOSHIBA",  "DVD-ROM SD-M2012", "TU01", "",          "toshiba_m2012",  BUS_TYPE_IDE,  0, 48, 96, 0, 1, {  4,  2,  2,  5 } }, /* DVD-ROM model produced under their TSST brand; updated version of SD-M1912 */
    { "WEARNES",  "CDD-110",          "1.02", "",          "wearnes_110",    BUS_TYPE_IDE,  1,  2, 36, 0, 0, {  0, -1, -1, -1 } },
    { "CHINON",   "CD-ROM CDS-431",   "H42 ", "",          "chinon_431",     BUS_TYPE_SCSI, 1,  1, 36, 1, 0, { -1, -1, -1, -1 } },
    { "CHINON",   "CD-ROM CDX-435",   "M62 ", "",          "chinon_435",     BUS_TYPE_SCSI, 1,  2, 36, 1, 0, { -1, -1, -1, -1 } },
    { "DEC",      "RRD42   (C) DEC",  "4.5d", "",          "dec_42",         BUS_TYPE_SCSI, 1,  1, 36, 0, 0, { -1, -1, -1, -1 } },
    { "DEC",      "RRD45   (C) DEC",  "0436", "",          "dec_45",         BUS_TYPE_SCSI, 1,  4, 36, 0, 0, { -1, -1, -1, -1 } },
    { "GRUNDIG",  "CDR100",           "1.20", "",          "grundig_100",    BUS_TYPE_SCSI, 2,  4, 36, 0, 0, { -1, -1, -1, -1 } }, /* Early version of Philips CDD2000 */
    { "MATSHITA", "CD-ROM CR-501",    "1.0b", "",          "matshita_501",   BUS_TYPE_SCSI, 1,  1, 36, 1, 0, { -1, -1, -1, -1 } },
    { "MATSHITA", "CD-ROM CR-504",    "4.0i", "",          "matshita_504",   BUS_TYPE_SCSI, 1,  4, 36, 0, 0, { -1, -1, -1, -1 } }, /* Also known as AppleCD 600i */
    { "MATSHITA", "CD-ROM CR-506",    "8.0h", "",          "matshita_506",   BUS_TYPE_SCSI, 1,  8, 36, 0, 0, { -1, -1, -1, -1 } }, /* Also known as AppleCD 1200i */
    { "MATSHITA", "CD-ROM CR-508",    "XS03", "",          "matshita_508",   BUS_TYPE_SCSI, 2, 24, 36, 0, 0, { -1, -1, -1, -1 } }, /* SCSI version of CR-585 */
    { "NEC",      "CD-ROM DRIVE:25",  "1.0a", "",          "nec_25",         BUS_TYPE_SCSI, 1,  2, 36, 0, 0, { -1, -1, -1, -1 } },
    { "NEC",      "CD-ROM DRIVE:38",  "1.00", "",          "nec_38",         BUS_TYPE_SCSI, 2,  1, 36, 0, 0, { -1, -1, -1, -1 } },
    { "NEC",      "CD-ROM DRIVE:75",  "1.03", "",          "nec_75",         BUS_TYPE_SCSI, 1,  1, 36, 1, 0, { -1, -1, -1, -1 } }, /* The speed of the following two is guesswork based on the CDR-74. */
    { "NEC",      "CD-ROM DRIVE:77",  "1.06", "",          "nec_77",         BUS_TYPE_SCSI, 1,  1, 36, 1, 0, { -1, -1, -1, -1 } },
    { "NEC",      "CD-ROM DRIVE:211", "1.00", "",          "nec_211",        BUS_TYPE_SCSI, 2,  3, 36, 0, 0, { -1, -1, -1, -1 } },
    { "NEC",      "CD-ROM DRIVE:464", "1.05", "",          "nec_464",        BUS_TYPE_SCSI, 2,  3, 36, 0, 0, { -1, -1, -1, -1 } }, /* The speed of the following two is guesswork based on the CDR-400. */
    { "NEC",      "CD-ROM DRIVE:900", "2.5a", "",          "nec_900",        BUS_TYPE_SCSI, 2,  4, 36, 0, 0, { -1, -1, -1, -1 } },
    { "PHILIPS",  "CDD2000",          "1.26", "",          "philips_2000",   BUS_TYPE_SCSI, 2,  4, 36, 0, 0, { -1, -1, -1, -1 } },
    { "PHILIPS",  "CDD2600",          "1.07", "",          "philips_2600",   BUS_TYPE_SCSI, 2,  6, 36, 0, 0, { -1, -1, -1, -1 } },
    { "PIONEER",  "CD-ROM DRM-604X",  "2403", "",          "pioneer_604x",   BUS_TYPE_SCSI, 2,  4, 47, 0, 0, { -1, -1, -1, -1 } }, /* NOTE: The real thing is a CD changer drive! */
    { "PIONEER",  "CD-ROM DR-U124X",  "4021", "",          "pioneer_u124x",  BUS_TYPE_SCSI, 2,  4, 47, 0, 0, { -1, -1, -1, -1 } }, /* Another (updated?) variant of DRM-604X */
    { "PIONEER",  "CD-ROM DR-U16S",   "0066", "",          "pioneer_u16s",   BUS_TYPE_SCSI, 2, 36, 47, 0, 0, { -1, -1, -1, -1 } },
    { "PLEXTOR",  "CD-ROM PX-43CH",   "0204", "",          "plextor_43ch",   BUS_TYPE_SCSI, 2,  4, 36, 1, 0, { -1, -1, -1, -1 } }, /* Caddy. */
    { "PLEXTOR",  "CD-ROM PX-83CS",   "1.01", "",          "plextor_83cs",   BUS_TYPE_SCSI, 2,  8, 36, 1, 0, { -1, -1, -1, -1 } }, /* Caddy. */
    { "PLEXTOR",  "CD-ROM PX-12CS",   "1.04", "",          "plextor_12cs",   BUS_TYPE_SCSI, 2, 12, 36, 1, 0, { -1, -1, -1, -1 } }, /* Caddy. */
    { "PLEXTOR",  "CD-ROM PX-12TS",   "1.01", "",          "plextor_12ts",   BUS_TYPE_SCSI, 2, 12, 36, 0, 0, { -1, -1, -1, -1 } },
    { "PLEXTOR",  "CD-ROM PX-20TSi",  "0101", "",          "plextor_20ts",   BUS_TYPE_SCSI, 1, 20, 36, 0, 0, { -1, -1, -1, -1 } },
    { "PLEXTOR",  "CD-ROM PX-32TS",   "1.03", "",          "plextor_32ts",   BUS_TYPE_SCSI, 2, 32, 36, 0, 0, { -1, -1, -1, -1 } },
    { "PLEXTOR",  "CD-ROM PX-40TS",   "1.14", "",          "plextor_40ts",   BUS_TYPE_SCSI, 2, 40, 36, 0, 0, { -1, -1, -1, -1 } },
    { "SANYO",    "CRD-254SH",        "1.05", "",          "sanyo_crd254sh", BUS_TYPE_SCSI, 2,  4, 36, 0, 0, { -1, -1, -1, -1 } },
    { "ShinaKen", "CD-ROM DM-3x1S",   "1.04", "",          "shinaken_3x1s",  BUS_TYPE_SCSI, 1,  3, 36, 0, 0, { -1, -1, -1, -1 } }, /* The speed of the following two is guesswork based on the name. */
    { "SONY",     "CD-ROM CDU-541",   "1.0i", "",          "sony_541",       BUS_TYPE_SCSI, 1,  1, 36, 1, 0, { -1, -1, -1, -1 } },
    { "SONY",     "CD-ROM CDU-561",   "1.9a", "",          "sony_561",       BUS_TYPE_SCSI, 2,  2, 36, 1, 0, { -1, -1, -1, -1 } }, /* Also known as AppleCD 300 */
    { "SONY",     "CD-ROM CDU-76S",   "1.00", "",          "sony_76s",       BUS_TYPE_SCSI, 2,  4, 36, 0, 0, { -1, -1, -1, -1 } },
    { "TEAC",     "CD 50",            "1.00", "",          "teac_50",        BUS_TYPE_SCSI, 2,  4, 36, 1, 0, { -1, -1, -1, -1 } }, /* The speed of the following two is guesswork based on the R55S. */
    { "TEAC",     "CD-ROM R55S",      "1.0R", "",          "teac_55s",       BUS_TYPE_SCSI, 2,  4, 36, 0, 0, { -1, -1, -1, -1 } },
    { "TEAC",     "CD-516S",          "2.0H", "",          "teac_516s",      BUS_TYPE_SCSI, 1, 16, 36, 0, 0, { -1, -1, -1, -1 } },
    { "TEAC",     "CD-ROM R56S",      "1.0R", "",          "teac_56s",       BUS_TYPE_SCSI, 2, 24, 36, 0, 0, { -1, -1, -1, -1 } },
    { "TEAC",     "CD-532S",          "3.0A", "",          "teac_532s",      BUS_TYPE_SCSI, 1, 32, 36, 0, 0, { -1, -1, -1, -1 } },
    { "TEXEL",    "CD-ROM DM-3024",   "1.00", "",          "texel_3024",     BUS_TYPE_SCSI, 2,  2, 36, 1, 0, { -1, -1, -1, -1 } }, /* Texel is Plextor according to Plextor's own EU website. */
    /*
       Unusual 2.23x according to Google, I'm rounding it upwards to 3x.
       Assumed caddy based on the DM-3024.
     */
    { "TEXEL",    "CD-ROM DM-3028",   "1.06", "", "texel_3028",     BUS_TYPE_SCSI, 2,  3, 36, 1, 0, { -1, -1, -1, -1 } }, /* Caddy. */
    /*
       The characteristics are a complete guesswork because I can't find
       this one on Google.

       Also, INQUIRY length is always 96 on these Toshiba drives.
     */
    { "TOSHIBA",  "CD-ROM DRIVE:XM",  "3433", "",          "toshiba_xm",     BUS_TYPE_SCSI, 2,  2, 96, 0, 0, { -1, -1, -1, -1 } }, /* Tray. */
    { "TOSHIBA",  "CD-ROM XM-3201B",  "3232", "",          "toshiba_3201b",  BUS_TYPE_SCSI, 1,  1, 96, 1, 0, { -1, -1, -1, -1 } }, /* Caddy. */
    { "TOSHIBA",  "CD-ROM XM-3301TA", "0272", "",          "toshiba_3301ta", BUS_TYPE_SCSI, 2,  2, 96, 0, 0, { -1, -1, -1, -1 } }, /* Tray. */
    { "TOSHIBA",  "CD-ROM XM-3701B",  "0236", "",          "toshiba_3701b",  BUS_TYPE_SCSI, 2,  6, 96, 0, 0, { -1, -1, -1, -1 } }, /* Tray. */
    { "TOSHIBA",  "CD-ROM XM-4101B",  "EA40", "",          "toshiba_4101b",  BUS_TYPE_SCSI, 1,  2, 96, 0, 0, { -1, -1, -1, -1 } }, /* Tray. */
    { "TOSHIBA",  "CD-ROM XM-5401B",  "1036", "",          "toshiba_5401b",  BUS_TYPE_SCSI, 2,  4, 96, 0, 0, { -1, -1, -1, -1 } }, /* Tray. */
    { "TOSHIBA",  "CD-ROM XM-5701TA", "3136", "",          "toshiba_5701a",  BUS_TYPE_SCSI, 2, 12, 96, 0, 0, { -1, -1, -1, -1 } }, /* Tray; SCSI version of XM-5702B. */
    { "TOSHIBA",  "CD-ROM XM-6401TA", "1404", "",          "toshiba_6401a",  BUS_TYPE_SCSI, 2, 32, 96, 0, 0, { -1, -1, -1, -1 } }, /* Tray; SCSI version of XM-6402B. */
    { "TOSHIBA",  "DVD-ROM SD-M1401", "1008", "",          "toshiba_m1401",  BUS_TYPE_SCSI, 2, 40, 96, 0, 1, { -1, -1, -1, -1 } }, /* Tray. */
    { "MATSHITA", "CR-562",           "0.75", "",          "cr562",          BUS_TYPE_MKE , 0,  2,  0, 0, 0, { -1, -1, -1, -1 } },
    { "MATSHITA", "CR-562",           "0.76", "",          "cr562_076",      BUS_TYPE_MKE , 0,  2,  0, 0, 0, { -1, -1, -1, -1 } },
    { "MATSHITA", "CR-562",           "0.80", "",          "cr562_080",      BUS_TYPE_MKE , 0,  2,  0, 0, 0, { -1, -1, -1, -1 } },
    { "MATSHITA", "CR-562",           "081k", "",          "cr562_081k",     BUS_TYPE_MKE , 0,  2,  0, 0, 0, { -1, -1, -1, -1 } },
    { "MATSHITA", "CR-563",           "0.74", "",          "cr563",          BUS_TYPE_MKE , 0,  2,  0, 0, 0, { -1, -1, -1, -1 } },
    { "MATSHITA", "CR-563",           "0.75", "",          "cr563_075",      BUS_TYPE_MKE , 0,  2,  0, 0, 0, { -1, -1, -1, -1 } },
    { "MATSHITA", "CR-563",           "0.80", "",          "cr563_080",      BUS_TYPE_MKE , 0,  2,  0, 0, 0, { -1, -1, -1, -1 } },
    { "",         "",                 "",     "",          "",               BUS_TYPE_NONE, 0, -1,  0, 0, 0, { -1, -1, -1, -1 } }
};

/* To shut up the GCC compilers. */
struct cdrom;

typedef struct subchannel_t {
    uint8_t attr;
    uint8_t track;
    uint8_t index;
    uint8_t abs_m;
    uint8_t abs_s;
    uint8_t abs_f;
    uint8_t rel_m;
    uint8_t rel_s;
    uint8_t rel_f;
} subchannel_t;

typedef struct track_info_t {
    int     number;
    uint8_t attr;
    uint8_t m;
    uint8_t s;
    uint8_t f;
} track_info_t;

typedef struct raw_track_info_t {
    uint8_t  session;
    uint8_t  adr_ctl;
    uint8_t  tno;
    uint8_t  point;
    uint8_t  m;
    uint8_t  s;
    uint8_t  f;
    uint8_t  zero;
    uint8_t  pm;
    uint8_t  ps;
    uint8_t  pf;
} raw_track_info_t;

/* Define the various CD-ROM drive operations (ops). */
typedef struct cdrom_ops_t {
    int      (*get_track_info)(const void *local, const uint32_t track,
                               const int end, track_info_t *ti);
    void     (*get_raw_track_info)(const void *local, int *num,
                                   uint8_t *rti);
    int      (*is_track_pre)(const void *local, const uint32_t sector);
    int      (*read_sector)(const void *local, uint8_t *buffer,
                            const uint32_t sector);
    uint8_t  (*get_track_type)(const void *local, const uint32_t sector);
    uint32_t (*get_last_block)(const void *local);
    int      (*read_dvd_structure)(const void *local, const uint8_t layer,
                                   const uint8_t format, uint8_t *buffer,
                                   uint32_t *info);
    int      (*is_dvd)(const void *local);
    int      (*has_audio)(const void *local);
    int      (*is_empty)(const void *local);
    void     (*close)(void *local);
    void     (*load)(const void *local);
} cdrom_ops_t;

typedef struct cdrom {
    uint8_t           id;

    union {
        uint8_t           res;
        uint8_t           res0;      /* Reserved for other ID's. */
        uint8_t           mke_channel;
        uint8_t           ide_channel;
        uint8_t           scsi_device_id;
    };

    uint8_t            bus_type;     /* 0 = ATAPI, 1 = SCSI */
    uint8_t            bus_mode;     /* Bit 0 = PIO suported;
                                        Bit 1 = DMA supportd. */
    uint8_t            cd_status;    /* Struct variable reserved for
                                        media status. */
    uint8_t            speed;
    uint8_t            cur_speed;

    void              *priv;

    char               image_path[MAX_IMAGE_PATH_LEN];
    char               prev_image_path[MAX_IMAGE_PATH_LEN + 256];

    uint32_t           sound_on;
    uint32_t           cdrom_capacity;
    uint32_t           seek_pos;
    uint32_t           seek_diff;
    uint32_t           cd_end;
    uint32_t           type;
    uint32_t           sector_size;

    uint32_t           inv_field;
    int32_t            cached_sector;
    int32_t            cd_buflen;
    int32_t            sony_msf;
    int32_t            real_speed;
    int32_t            is_early;
    int32_t            is_nec;
    int32_t            is_bcd;

    int32_t            cdrom_sector_size;

    const cdrom_ops_t *ops;

    char              *image_history[CD_IMAGE_HISTORY];

    void              *local;
    void              *log;

    void               (*insert)(void *priv);
    void               (*close)(void *priv);
    uint32_t           (*get_volume)(void *p, int channel);
    uint32_t           (*get_channel)(void *p, int channel);

    int16_t            cd_buffer[CD_BUF_SIZE];

    uint8_t            subch_buffer[96];

    /* Needs some extra breathing space in case of overflows. */
    uint8_t            raw_buffer[2][4096];
    uint8_t            extra_buffer[296];

    int32_t            is_chinon;
    int32_t            is_pioneer;
    int32_t            is_plextor;
    int32_t            is_sony;
    int32_t            is_toshiba;

    int32_t            c2_first;
    int32_t            cur_buf;

    /* Only used on Windows hosts for disc change notifications. */
    uint8_t            host_letter;
    uint8_t            mode2;

    int                no_check;

    uint8_t            _F_LUT[_LUT_SIZE];
    uint8_t            _B_LUT[_LUT_SIZE];

    uint8_t            p_parity[172];
    uint8_t            q_parity[104];
} cdrom_t;

extern cdrom_t cdrom[CDROM_NUM];

#define MSFtoLBA(m, s, f)  ((((m * 60) + s) * 75) + f)

static __inline int
bin2bcd(int x)
{
    return (x % 10) | ((x / 10) << 4);
}

static __inline int
bcd2bin(int x)
{
    return (x >> 4) * 10 + (x & 0x0f);
}

extern char           *cdrom_get_vendor(const int type);
extern void            cdrom_get_model(const int type, char *name, const int id);
extern char           *cdrom_get_revision(const int type);
extern int             cdrom_get_scsi_std(const int type);
extern int             cdrom_is_early(const int type);
extern int             cdrom_is_dvd(const int type);
extern int             cdrom_is_generic(const int type);
extern int             cdrom_is_caddy(const int type);
extern int             cdrom_get_speed(const int type);
extern int             cdrom_get_inquiry_len(const int type);
extern int             cdrom_has_dma(const int type);
extern int             cdrom_get_transfer_max(const int type, const int mode);
extern int             cdrom_get_type_count(void);
extern void            cdrom_generate_name_mke(const int type, char *name);
extern void            cdrom_get_identify_model(const int type, char *name, const int id);
extern void            cdrom_get_name(const int type, char *name);
extern char           *cdrom_get_internal_name(const int type);
extern int             cdrom_get_from_internal_name(const char *s);
/* TODO: Configuration migration, remove when no longer needed. */
extern int             cdrom_get_from_name(const char *s);
extern void            cdrom_set_type(const int model, const int type);
extern int             cdrom_get_type(const int model);

extern int             cdrom_lba_to_msf_accurate(const int lba);
extern void            cdrom_interleave_subch(uint8_t *d, const uint8_t *s);
extern double          cdrom_seek_time(const cdrom_t *dev);
extern void            cdrom_stop(cdrom_t *dev);
extern void            cdrom_seek(cdrom_t *dev, const uint32_t pos, const uint8_t vendor_type);
extern int             cdrom_is_pre(const cdrom_t *dev, const uint32_t lba);

extern int             cdrom_audio_callback(cdrom_t *dev, int16_t *output, const int len);
extern uint8_t         cdrom_audio_play(cdrom_t *dev, const uint32_t pos, const uint32_t len, const int ismsf);
extern uint8_t         cdrom_audio_track_search(cdrom_t *dev, const uint32_t pos,
                                                const int type, const uint8_t playbit);
extern uint8_t         cdrom_audio_track_search_pioneer(cdrom_t *dev, const uint32_t pos, const uint8_t playbit);
extern uint8_t         cdrom_audio_play_pioneer(cdrom_t *dev, const uint32_t pos);
extern uint8_t         cdrom_audio_play_toshiba(cdrom_t *dev, const uint32_t pos, const int type);
extern uint8_t         cdrom_audio_scan(cdrom_t *dev, const uint32_t pos);
extern void            cdrom_audio_pause_resume(cdrom_t *dev, const uint8_t resume);

extern uint8_t         cdrom_get_current_status(const cdrom_t *dev);
extern void            cdrom_get_current_subchannel(cdrom_t *dev, uint8_t *b, const int msf);
extern void            cdrom_get_current_subchannel_sony(cdrom_t *dev, uint8_t *b, const int msf);
extern uint8_t         cdrom_get_audio_status_pioneer(cdrom_t *dev, uint8_t *b);
extern uint8_t         cdrom_get_audio_status_sony(cdrom_t *dev, uint8_t *b, const int msf);
extern void            cdrom_get_current_subcodeq(cdrom_t *dev, uint8_t *b);
extern uint8_t         cdrom_get_current_subcodeq_playstatus(cdrom_t *dev, uint8_t *b);
extern int             cdrom_read_toc(const cdrom_t *dev, uint8_t *b, const int type,
                                      const uint8_t start_track, const int msf, const int max_len);
extern int             cdrom_read_toc_sony(const cdrom_t *dev, uint8_t *b, const uint8_t start_track,
                                           const int msf, const int max_len);
#ifdef USE_CDROM_MITSUMI
extern void            cdrom_get_track_buffer(cdrom_t *dev, uint8_t *buf);
extern void            cdrom_get_q(cdrom_t *dev, uint8_t *buf, int *curtoctrk, uint8_t mode);
extern uint8_t         cdrom_mitsumi_audio_play(cdrom_t *dev, uint32_t pos, uint32_t len);
#endif
extern uint8_t         cdrom_read_disc_info_toc(cdrom_t *dev, uint8_t *b,
                                                const uint8_t track, const int type);
extern int             cdrom_is_track_audio(cdrom_t *dev, const int sector, const int ismsf,
                                            int cdrom_sector_type, const uint8_t vendor_type);
extern int             cdrom_readsector_raw(cdrom_t *dev, uint8_t *buffer, const int sector, const int ismsf,
                                            int cdrom_sector_type, const int cdrom_sector_flags,
                                            int *len, const uint8_t vendor_type);
extern int             cdrom_read_dvd_structure(const cdrom_t *dev, const uint8_t layer, const uint8_t format,
                                                uint8_t *buffer, uint32_t *info);
extern void            cdrom_read_disc_information(const cdrom_t *dev, uint8_t *buffer);
extern int             cdrom_read_track_information(cdrom_t *dev, const uint8_t *cdb, uint8_t *buffer);
extern uint8_t         cdrom_get_current_mode(cdrom_t *dev);
extern void            cdrom_set_empty(cdrom_t *dev);
extern void            cdrom_update_status(cdrom_t *dev);
extern int             cdrom_load(cdrom_t *dev, const char *fn, const int skip_insert);

extern void            cdrom_global_init(void);
extern void            cdrom_hard_reset(void);
extern void            cdrom_close(void);
extern void            cdrom_insert(const uint8_t id);
extern void            cdrom_exit(const uint8_t id);
extern int             cdrom_is_empty(const uint8_t id);
extern int             cdrom_is_playing(const uint8_t id);
extern int             cdrom_is_paused(const uint8_t id);
extern void            cdrom_eject(const uint8_t id);
extern void            cdrom_reload(const uint8_t id);

extern void            cdrom_compute_ecc_block(cdrom_t *dev, uint8_t *parity, const uint8_t *data,
                                               uint32_t major_count, uint32_t minor_count,
                                               uint32_t major_mult, uint32_t minor_inc, int m2f1);
extern unsigned long   cdrom_crc32(unsigned long crc, const unsigned char *buf,
                                   size_t len);

extern int             cdrom_assigned_letters;

#ifdef __cplusplus
}
#endif

#endif /*EMU_CDROM_H*/
