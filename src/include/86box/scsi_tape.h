/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of a generic SCSI QIC Tape drive
 *          commands, for SCSI usage.
 *
 * Authors: Plamen Ivanov
 *
 *          Copyright 2025-2026 Plamen Ivanov.
 */
#ifndef EMU_SCSI_TAPE_H
#define EMU_SCSI_TAPE_H

#define TAPE_NUM           4
#define TAPE_BUF_SIZE      65536
#define TAPE_TIME          10.0
#define TAPE_IMAGE_HISTORY 10

/* SIMH tape image record markers. */
#define TAPE_SIMH_FILEMARK 0x00000000
#define TAPE_SIMH_EOD      0xFFFFFFFF
#define TAPE_SIMH_GAP      0xFFFFFFFE
#define TAPE_SIMH_BAD_REC  0xFFFF0000

/* QIC tape type definitions. */
typedef struct tape_type_t {
    const char *name;
    uint32_t    capacity_bytes;
    uint16_t    default_block_size;
    uint8_t     density_code;
} tape_type_t;

#define KNOWN_TAPE_TYPES 3
static const tape_type_t tape_types[KNOWN_TAPE_TYPES] = {
    { "QIC-150",  157286400, 512, 0x10 },
    { "QIC-525",  549978112, 512, 0x11 },
    { "QIC-1000", 1073741824, 512, 0x12 },
};

/* Tape drive type definitions. */
typedef struct tape_drive_type_t {
    const char *vendor;
    const char *model;
    const char *revision;
    int8_t      supported_media[KNOWN_TAPE_TYPES];
} tape_drive_type_t;

#define KNOWN_TAPE_DRIVE_TYPES 2
static const tape_drive_type_t tape_drive_types[KNOWN_TAPE_DRIVE_TYPES] = {
    { "86BOX",   "TAPE",             "1.00", { 1, 1, 1 } },
    { "ARCHIVE", "VIPER 150 21247",  "2.10", { 1, 0, 0 } },
};

enum {
    TAPE_BUS_DISABLED = 0,
    TAPE_BUS_ATAPI    = 8,
    TAPE_BUS_SCSI     = 9
};

typedef struct tape_drive_t {
    uint8_t            id;

    union {
        uint8_t            res;
        /* Reserved for other ID's. */
        uint8_t            res0;
        uint8_t            res1;
        uint8_t            ide_channel;
        uint8_t            scsi_device_id;
    };

    uint8_t            bus_type;
    uint8_t            bus_mode;
    uint8_t            read_only;
    uint8_t            pad;
    uint8_t            pad0;

    FILE              *fp;
    void              *priv;

    char               image_path[MAX_IMAGE_PATH_LEN];
    char               prev_image_path[MAX_IMAGE_PATH_LEN + 256];

    char              *image_history[TAPE_IMAGE_HISTORY];

    uint32_t           type;
    uint32_t           medium_type;
} tape_drive_t;

typedef struct tape_t {
    mode_sense_pages_t ms_pages_saved;

    tape_drive_t      *drv;
#ifdef EMU_IDE_H
    ide_tf_t          *tf;
#else
    void              *tf;
#endif

    void              *log;

    uint8_t           *buffer;
    size_t             buffer_sz;
    uint8_t            pad_cdb[16]; /* Pad for scsi_common_t alignment */
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

    /* Tape-specific state. */
    uint32_t           tape_pos;       /* Current byte position in the .tap file. */
    uint32_t           block_size;     /* Current fixed block size (0 = variable block mode). */
    uint32_t           num_blocks;     /* Current logical block number. */
    uint32_t           tape_length;    /* File size of the .tap image. */
    int                eot;            /* End-of-tape reached. */
    int                bot;            /* Beginning-of-tape. */
    int                filemark_pending; /* A filemark was just encountered. */

    /* Read-ahead buffer for re-blocking: when a SIMH record is larger than
       the requested fixed block size, the unconsumed remainder is stored here
       so the next READ can pick up where we left off. */
    uint8_t           *rec_buf;        /* Residual record data. */
    uint32_t           rec_buf_size;   /* Allocated size of rec_buf. */
    uint32_t           rec_remaining;  /* Bytes remaining in rec_buf. */
    uint32_t           rec_offset;     /* Current read offset in rec_buf. */
} tape_t;

extern tape_drive_t tape_drives[TAPE_NUM];

#define tape_sense_error dev->sense[0]
#define tape_sense_key   dev->sense[2]
#define tape_info        *(uint32_t *) &(dev->sense[3])
#define tape_asc         dev->sense[12]
#define tape_ascq        dev->sense[13]

#ifdef __cplusplus
extern "C" {
#endif

extern void tape_disk_close(const tape_t *dev);
extern void tape_disk_reload(const tape_t *dev);
extern void tape_insert(tape_t *dev);

extern void tape_global_init(void);
extern void tape_hard_reset(void);

extern void tape_reset(scsi_common_t *sc);
extern int  tape_is_empty(const uint8_t id);
extern void tape_load(const tape_t *dev, const char *fn, const int skip_insert);
extern void tape_close(void);

#ifdef __cplusplus
}
#endif

#endif /*EMU_SCSI_TAPE_H*/
