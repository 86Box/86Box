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
    uint32_t sectors;
    uint16_t bytes_per_sector;
} mo_type_t;

#define KNOWN_MO_TYPES 10
static const mo_type_t mo_types[KNOWN_MO_TYPES] = {
  // 3.5" standard M.O. disks
    { 248826,   512 },
    { 446325,   512 },
    { 1041500,  512 },
    { 310352,  2048 },
    { 605846,  2048 },
    { 1063146, 2048 },
 // 5.25" M.O. disks
    { 573624,   512 },
    { 314568,  1024 },
    { 904995,   512 },
    { 637041,  1024 },
};

typedef struct mo_drive_type_t {
    const char vendor[9];
    const char model[16];
    const char revision[5];
    int8_t     supported_media[KNOWN_MO_TYPES];
} mo_drive_type_t;

#define KNOWN_MO_DRIVE_TYPES 22
static const mo_drive_type_t mo_drive_types[KNOWN_MO_DRIVE_TYPES] = {
    {"86BOX",     "MAGNETO OPTICAL", "1.00", { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }},
    { "FUJITSU",  "M2512A",          "1314", { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "FUJITSU",  "M2513-MCC3064SS", "1.00", { 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 }},
    { "FUJITSU",  "MCE3130SS",       "0070", { 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 }},
    { "FUJITSU",  "MCF3064SS",       "0030", { 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 }},
    { "FUJITSU",  "MCJ3230UB-S",     "0040", { 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 }},
    { "HP",       "S6300.65",        "1.00", { 0, 0, 0, 0, 0, 0, 1, 1, 0, 0 }},
    { "HP",       "C1716C",          "1.00", { 0, 0, 0, 0, 0, 0, 1, 1, 0, 1 }},
    { "IBM",      "0632AAA",         "1.00", { 0, 0, 0, 0, 0, 0, 1, 1, 0, 0 }},
    { "IBM",      "0632CHC",         "1.00", { 0, 0, 0, 0, 0, 0, 1, 1, 0, 1 }},
    { "IBM",      "0632CHX",         "1.00", { 0, 0, 0, 0, 0, 0, 1, 1, 0, 1 }},
    { "IBM",      "MD3125A",         "1.00", { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "IBM",      "MD3125B",         "1.00", { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "IBM",      "MTA-3127",        "1.00", { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "IBM",      "MTA-3230",        "1.00", { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "MATSHITA", "LF-3000",         "1.00", { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "MOST",     "RMD-5100",        "1.00", { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "RICOH",    "RO-5031E",        "1.00", { 0, 0, 0, 0, 0, 0, 1, 1, 0, 0 }},
    { "SONY",     "SMO-C301",        "1.00", { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "SONY",     "SMO-C501",        "1.00", { 0, 0, 0, 0, 0, 0, 1, 1, 0, 0 }},
    { "TEAC",     "OD-3000",         "1.00", { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
    { "TOSHIBA",  "OD-D300",         "1.00", { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }},
};

enum {
    MO_BUS_DISABLED = 0,
    MO_BUS_ATAPI    = 5,
    MO_BUS_SCSI     = 6,
    MO_BUS_USB      = 7
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

    FILE *             fp;
    void *             priv;

    char               image_path[1024];
    char               prev_image_path[1024];

    char *             image_history[MO_IMAGE_HISTORY];

    uint32_t           type;
    uint32_t           medium_size;
    uint32_t           base;
    uint16_t           sector_size;
} mo_drive_t;

typedef struct mo_t {
    mode_sense_pages_t ms_pages_saved;

    mo_drive_t *       drv;
#ifdef EMU_IDE_H
    ide_tf_t *         tf;
#else
    void *             tf;
#endif

    void *             log;

    uint8_t *          buffer;
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
