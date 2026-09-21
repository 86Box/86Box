/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of removable disk drives with SCSI(-like)
 *          commands, for both ATAPI and SCSI usage.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2018-2025 Miran Grca.
 */
#ifndef EMU_RDISK_H
#define EMU_RDISK_H

#define RDISK_NUM                  4

#define BUF_SIZE               32768

#define RDISK_TIME              10.0

#define ZIP_SECTORS      (96 * 2048)

#define ZIP_250_SECTORS     (489532)

#define ZIP_750_SECTORS     (1468596) /* Estimated */

#define JAZ_1GB_SECTORS     (2091050)

#define JAZ_2GB_SECTORS     (3915600)

#define SUPERDISK_SECTORS      (963 * 256)

#define SUPERDISK_240_SECTORS     (469504)

/* Last LBA of 2,929,799 (i.e. 2,929,800 sectors) per the SyQuest SyJet SCSI
   Technical Reference (1997, P/N 112857-001A), section "Read Capacity (25H)". */
#define SYJET_SECTORS       (2929800)

/* Total addressable LBA count of 1,961,069 sectors per the SyQuest SparQ
   Internal EIDE Technical Reference (1997, P/N 113277-001A), section
   "Identify Device (ECh)", words 60-61. The SparQ only ever shipped as an
   IDE/EIDE (and external parallel-port) drive - it has no real SCSI variant. */
#define SPARQ_SECTORS       (1961069)

#define RDISK_IMAGE_HISTORY       10

enum {
    RDISK_TYPE_GENERIC = 0,
    RDISK_TYPE_ZIP_100,
    RDISK_TYPE_ZIP_250,
#if 0
    RDISK_TYPE_ZIP_750,
#endif
    RDISK_TYPE_JAZ_1GB,
    RDISK_TYPE_JAZ_2GB,
    RDISK_TYPE_SYJET_1_5GB,
    RDISK_TYPE_SPARQ_1GB,
    RDISK_TYPE_SUPERDISK_120,
    RDISK_TYPE_SUPERDISK_240
};

typedef struct rdisk_type_t {
    uint32_t sectors;
    uint16_t bytes_per_sector;
} rdisk_type_t;

#define KNOWN_RDISK_TYPES 8
static const rdisk_type_t rdisk_types[KNOWN_RDISK_TYPES] = {
    { ZIP_SECTORS,           512 },
    { ZIP_250_SECTORS,       512 },
#if 0
    { ZIP_750_SECTORS,       512 },
#endif
    { JAZ_1GB_SECTORS,       512 },
    { JAZ_2GB_SECTORS,       512 },
    { SYJET_SECTORS,         512 },
    { SPARQ_SECTORS,         512 },
    { SUPERDISK_SECTORS,     512 },
    { SUPERDISK_240_SECTORS, 512 }
};

typedef struct rdisk_drive_type_t {
    const char *vendor;
    const char *model;
    const char *revision;
    int8_t      supported_media[KNOWN_RDISK_TYPES];
} rdisk_drive_type_t;

#define KNOWN_RDISK_DRIVE_TYPES 9
static const rdisk_drive_type_t rdisk_drive_types[KNOWN_RDISK_DRIVE_TYPES] = {
    { "86BOX",    "REMOVABLE DISK",             "5.00", { 1, 1, 1, 1, 1, 1, 1, 1 }},
    { "IOMEGA",   "ZIP 100",                    "E.08", { 1, 0, 0, 0, 0, 0, 0, 0 }},
    { "IOMEGA",   "ZIP 250",                    "42.S", { 1, 1, 0, 0, 0, 0, 0, 0 }},
#if 0
    { "IOMEGA",   "ZIP 750",                    "42.S", { 1, 1, 1, 0, 0 }}, /* Guess */
#endif
    { "IOMEGA",   "JAZ 1GB",                    "H.72", { 0, 0, 1, 0, 0, 0, 0, 0 }},
    { "IOMEGA",   "JAZ 2GB",                    "E.17", { 0, 0, 1, 1, 0, 0, 0 ,0 }},
    /* Firmware revision "1.06" is a placeholder: the real SyJet's revision
       string could not be confirmed from the SCSI Technical Reference or
       any real-world SCSI probe log. Correct it if a real value surfaces. */
    { "SYQUEST",  "SyJet 1.5GB",                "1.06", { 0, 0, 0, 0, 1, 0, 0, 0 }},
    /* Firmware revision "1.03" is likewise an unconfirmed placeholder - the
       SparQ's EIDE Technical Reference doesn't give a real example string. */
    { "SYQUEST",  "SparQ 1.0GB",                "1.03", { 0, 0, 0, 0, 0, 1, 0, 0 }},
    { "IMATION",  "SUPERDISK 120 ATAPI",        "04",   { 0, 0, 0, 0, 0, 0, 1, 0 }},
    { "IMATION",  "SUPERDISK  240       ATAPI", "04",   { 0, 0, 0, 0, 0, 0, 1, 1 }}
};

enum {
    RDISK_BUS_DISABLED =  0,
    RDISK_BUS_LPT      =  6,
    RDISK_BUS_IDE      =  7,
    RDISK_BUS_ATAPI    =  8,
    RDISK_BUS_SCSI     =  9,
    RDISK_BUS_USB      = 10
};

typedef struct rdisk_drive_t {
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

    char              *image_history[RDISK_IMAGE_HISTORY];

    uint32_t           type;
    uint32_t           medium_size;
    uint32_t           base;
} rdisk_drive_t;

typedef struct rdisk_t {
    mode_sense_pages_t ms_pages_saved;

    rdisk_drive_t       *drv;
#ifdef EMU_IDE_H
    ide_tf_t          *tf;
#else
    void              *tf;
#endif

    void              *log;

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
} rdisk_t;

extern rdisk_t      *rdisk[RDISK_NUM];
extern rdisk_drive_t rdisk_drives[RDISK_NUM];
extern uint8_t     atapi_rdisk_drives[8];
extern uint8_t     scsi_rdisk_drives[16];

#define rdisk_sense_error dev->sense[0]
#define rdisk_sense_key   dev->sense[2]
#define rdisk_info        *(uint32_t *) &(dev->sense[3])
#define rdisk_asc         dev->sense[12]
#define rdisk_ascq        dev->sense[13]

#ifdef __cplusplus
extern "C" {
#endif

extern void rdisk_disk_close(const rdisk_t *dev);
extern void rdisk_disk_reload(const rdisk_t *dev);
extern void rdisk_insert(rdisk_t *dev);

extern void rdisk_global_init(void);
extern void rdisk_hard_reset(void);

extern void rdisk_reset(scsi_common_t *sc);
extern int  rdisk_is_empty(const uint8_t id);
extern void rdisk_load(const rdisk_t *dev, const char *fn, const int skip_insert);
extern void rdisk_close(void);
#ifdef SCSI_DEVICE_H
extern scsi_device_t *rdisk_get_lpt_device(const uint8_t port);
#endif

#ifdef __cplusplus
}
#endif

#endif /*EMU_RDISK_H*/
