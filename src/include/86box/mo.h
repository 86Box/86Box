/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of a generic Magneto-Optical Disk drive
 *          commands, for both ATAPI and SCSI usage.
 *
 * Authors: Natalia Portillo <claunia@claunia.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2020-2025 Natalia Portillo.
 *          Copyright 2020-2025 Miran Grca.
 *          Copyright 2020-2025 Fred N. van Kempen
 */
#ifndef EMU_MO_H
#define EMU_MO_H

#define MO_NUM   4

#define BUF_SIZE 32768

#define MO_TIME  10.0

#define MO_IMAGE_HISTORY 10

typedef struct mo_type_t {
    const char *name;
    uint32_t    sectors;
    uint16_t    bytes_per_sector;
    uint8_t     medium_type_code; /* In the mode parameter header. */
} mo_type_t;

#define KNOWN_MO_TYPES 19
static const mo_type_t mo_types[KNOWN_MO_TYPES] = {
  // 3.5" standard M.O. disks
    { "3.5\" 128 MB (ISO 10090)", 248826,   512 },
    { "3.5\" 230 MB (ISO 13963)", 446325,   512 },
    { "3.5\" 540 MB (ISO 15498)", 1041500,  512 },
    { "3.5\" 640 MB (ISO 15498)", 310352,  2048 },
    { "3.5\" 1.3 GB (GigaMO)",    605846,  2048 },
    { "3.5\" 2.3 GB (GigaMO 2)",  1063146, 2048 },
  // 5.25" M.O. disks
    { "5.25\" 600 MB",            573624,   512 },
    { "5.25\" 650 MB",            314568,  1024 },
    { "5.25\" 1 GB",              904995,   512 },
    { "5.25\" 1.3 GB",            637041,  1024 },
  /* MiniDisc, as Aaru device reports 173, 273 and 324 have them.  The medium type is 03h in reports 273 and
     324; MDW-60 has none in report 273 and is taken to be the same.  The blocks of a 2336-byte disc are not
     data: READ only worked on the MMD-140A and, with Hi-MD, on the four Hi-MD formats. */
    { "MiniDisc (audio)",         45070,   2336, 0x01 },
    { "MD Data 140 MB (MMD-140A)", 70464,  2048, 0x03 },
    { "MD 60 min (MDW-60)",       57312,   2336, 0x03 },
    { "MD 74 min (MDW-74)",       70464,   2336, 0x03 },
    { "MD 80 min (MDW-80)",       76096,   2336, 0x03 },
  /* Hi-MD: the 1 GB disc and standard discs in Hi-MD format */
    { "Hi-MD 1 GB",               494023,  2048, 0x44 },
    { "Hi-MD on MDW-60",          112311,  2048, 0x00 },
    { "Hi-MD on MDW-74",          138363,  2048, 0x00 },
    { "Hi-MD on MDW-80",          149373,  2048, 0x00 },
};

enum {
    MO_BUS_DISABLED =  0,
    MO_BUS_LPT      =  6,
    MO_BUS_ATAPI    =  8,
    MO_BUS_SCSI     =  9,
    MO_BUS_USB      = 10
};

#define MO_DRIVE_TYPE_LPT            MO_BUS_LPT
#define MO_DRIVE_TYPE_ATAPI          MO_BUS_ATAPI
#define MO_DRIVE_TYPE_SCSI           MO_BUS_SCSI
#define MO_DRIVE_TYPE_SCSI_ATAPI     (-2)
#define MO_DRIVE_TYPE_NONE           (-1)
#define MO_DRIVE_TYPE_ATAPI_LPT      (-3)
#define MO_DRIVE_TYPE_SCSI_ATAPI_LPT (-4)

typedef struct mo_drive_type_t {
    const char *vendor;
    const char *model;
    const char *revision;
    uint8_t     default_media;
    int8_t      drive_type;
    int8_t      supported_media[KNOWN_MO_TYPES];
} mo_drive_type_t;

/* The number of a drive type is saved in configuration files, so new types go at the end of the table. */
#define MO_TYPE_SONY_MDM110 28
#define MO_TYPE_SONY_MDH10  29
#define MO_TYPE_SONY_HIMD   30

#define KNOWN_MO_DRIVE_TYPES 31
static const mo_drive_type_t mo_drive_types[KNOWN_MO_DRIVE_TYPES] = {
    { "86BOX",    "MAGNETO OPTICAL", "1.00", 0, MO_DRIVE_TYPE_SCSI_ATAPI, { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }},
    { "FUJITSU",  "M2512A",          "1314", 0, MO_DRIVE_TYPE_SCSI,       { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "FUJITSU",  "M25-MCC3064AP",   "0051", 0, MO_DRIVE_TYPE_ATAPI,      { 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }}, /* ATAPI */
    { "FUJITSU",  "M2513-MCC3064SS", "1.00", 0, MO_DRIVE_TYPE_SCSI,       { 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "FUJITSU",  "MCE3130AP",       "0011", 0, MO_DRIVE_TYPE_ATAPI,      { 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }}, /* ATAPI */
    { "FUJITSU",  "MCE3130SS",       "0070", 0, MO_DRIVE_TYPE_SCSI,       { 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "FUJITSU",  "MCF3064AP",       "0030", 0, MO_DRIVE_TYPE_ATAPI,      { 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }}, /* ATAPI */
    { "FUJITSU",  "MCF3064SS",       "0030", 0, MO_DRIVE_TYPE_SCSI,       { 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "FUJITSU",  "MCF3064UB",       "0030", 0, MO_DRIVE_TYPE_SCSI,       { 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }}, /* USB */
    { "FUJITSU",  "MCJ3230AP-S",     "0030", 0, MO_DRIVE_TYPE_ATAPI,      { 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }}, /* ATAPI */
    { "FUJITSU",  "MCJ3230SS-S",     "0030", 0, MO_DRIVE_TYPE_SCSI,       { 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "FUJITSU",  "MCJ3230UB-S",     "0040", 0, MO_DRIVE_TYPE_SCSI,       { 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }}, /* USB */
    { "HP",       "S6300.65",        "1.00", 6, MO_DRIVE_TYPE_SCSI,       { 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "HP",       "C1716C",          "1.00", 6, MO_DRIVE_TYPE_SCSI,       { 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "IBM",      "0632AAA",         "1.00", 6, MO_DRIVE_TYPE_SCSI,       { 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "IBM",      "0632CHC",         "1.00", 6, MO_DRIVE_TYPE_SCSI,       { 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "IBM",      "0632CHX",         "1.00", 6, MO_DRIVE_TYPE_SCSI,       { 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "IBM",      "MD3125A",         "1.00", 0, MO_DRIVE_TYPE_SCSI,       { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "IBM",      "MD3125B",         "1.00", 0, MO_DRIVE_TYPE_SCSI,       { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "IBM",      "MTA-3127",        "1.00", 0, MO_DRIVE_TYPE_SCSI,       { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "IBM",      "MTA-3230",        "1.00", 0, MO_DRIVE_TYPE_SCSI,       { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "MATSHITA", "LF-3000",         "1.00", 0, MO_DRIVE_TYPE_SCSI,       { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "MOST",     "RMD-5100",        "1.00", 0, MO_DRIVE_TYPE_SCSI,       { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "RICOH",    "RO-5031E",        "1.00", 6, MO_DRIVE_TYPE_SCSI,       { 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "SONY",     "SMO-C301",        "1.00", 0, MO_DRIVE_TYPE_SCSI,       { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "SONY",     "SMO-C501",        "1.00", 0, MO_DRIVE_TYPE_SCSI,       { 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "TEAC",     "OD-3000",         "1.00", 0, MO_DRIVE_TYPE_SCSI,       { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "TOSHIBA",  "OD-D300",         "1.00", 0, MO_DRIVE_TYPE_SCSI,       { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    /* SONY MD Data and Hi-MD: Aaru device reports 324, 273 and 173.  Report 173 is a USB Walkman, kept here as SCSI
       like the other USB drives. */
    { "SONY",     "MDM110",          "1.01", 11, MO_DRIVE_TYPE_SCSI,      { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0 }},
    { "SONY",     "MDH-10",          "1.11", 11, MO_DRIVE_TYPE_SCSI,      { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0 }},
    { "SONY",     "Hi-MD WALKMAN",   "1000", 15, MO_DRIVE_TYPE_SCSI,      { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 }},
};

typedef struct mo_drive_t {
    uint8_t            id;

    union {
        uint8_t            res;
        /* Reserved for other ID's. */
        uint8_t            res0;
        uint8_t            res1;
        uint8_t            ide_channel;
        uint8_t            scsi_device_id;
    };

    uint8_t            bus_type;  /* 0 = ATAPI, 1 = SCSI */
    uint8_t            bus_mode;  /* Bit 0 = PIO suported;
                                     Bit 1 = DMA supportd. */
    uint8_t            read_only; /* Struct variable reserved for
                                     media status. */
    uint8_t            pad;
    uint8_t            pad0;

    FILE              *fp;
    void              *priv;

    char               image_path[MAX_IMAGE_PATH_LEN];
    char               prev_image_path[MAX_IMAGE_PATH_LEN + 256];

    char              *image_history[MO_IMAGE_HISTORY];

    uint32_t           type;
    uint32_t           medium_size;
    uint32_t           base;
    uint16_t           sector_size;

    int                supported;

    uint32_t           medium_type;
} mo_drive_t;

typedef struct mo_t {
    mode_sense_pages_t ms_pages_saved;

    mo_drive_t        *drv;
#ifdef EMU_IDE_H
    ide_tf_t          *tf;
#else
    void              *tf;
#endif

    void *             log;

    uint8_t           *buffer;
    size_t             buffer_sz;
    uint8_t            atapi_cdb[16];
    uint8_t            current_cdb[16];
    uint8_t            sense[256];

    uint8_t            id;
    uint8_t            cur_lun;
    uint8_t            pad0;
    uint8_t            pad1;

    uint16_t           max_transfer_len;
    uint16_t           pad2;

    int                requested_blocks;
    int                packet_status;
    int                total_length;
    int                do_page_save;
    int                unit_attention;
    int                request_pos;
    int                old_len;
    int                transition;

    uint32_t           sector_pos;
    uint32_t           sector_len;
    uint32_t           packet_len;
    uint32_t           block_len;

    double             callback;

    uint8_t            (*ven_cmd)(void *sc, uint8_t *cdb, int32_t *BufLen);
} mo_t;

extern mo_drive_t  mo_drives[MO_NUM];

#define mo_sense_error dev->sense[0]
#define mo_sense_key   dev->sense[2]
#define mo_info        *(uint32_t *) &(dev->sense[3])
#define mo_asc         dev->sense[12]
#define mo_ascq        dev->sense[13]

#ifdef __cplusplus
extern "C" {
#endif

extern void mo_disk_close(const mo_t *dev);
extern void mo_disk_reload(const mo_t *dev);
extern void mo_insert(mo_t *dev);

extern void mo_global_init(void);
extern void mo_hard_reset(void);

extern void mo_reset(scsi_common_t *sc);
extern int  mo_is_empty(const uint8_t id);
extern void mo_load(const mo_t *dev, const char *fn, const int skip_insert);
extern void mo_close(void);

#ifdef __cplusplus
}
#endif

#endif /*EMU_MO_H*/
