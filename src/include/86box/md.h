/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the Sony MD DATA (MiniDisc) SCSI drives.
 *
 * Authors: Natalia Portillo <claunia@claunia.com>
 *
 *          Copyright 2026 Natalia Portillo.
 */
#ifndef EMU_MD_H
#define EMU_MD_H

#define MD_NUM           4

#define MD_TIME          10.0

#define MD_IMAGE_HISTORY 10

/* MD DATA discs hold 70464 logical blocks of 2048 bytes, the first 32 sectors of clusters 50 to 2251. */
#define MD_BLOCK_SIZE    2048
#define MD_BLOCKS        70464

/* TOC sectors are raw 2352-byte sectors without their sync and header. */
#define MD_TOC_SECTOR_SIZE 2336
#define MD_UTOC_SECTORS    8

enum {
    MD_BUS_DISABLED = 0,
    MD_BUS_SCSI     = 9
};

typedef struct md_drive_type_t {
    const char *vendor;
    const char *model;
    const char *revision;
    const char *name;
} md_drive_type_t;

/* The number of a drive type is saved in configuration files, so new types go at the end of the table. */
#define MD_TYPE_SONY_MDM110 0
#define MD_TYPE_SONY_MDH10  1

#define KNOWN_MD_DRIVE_TYPES 2
static const md_drive_type_t md_drive_types[KNOWN_MD_DRIVE_TYPES] = {
    { "SONY", "MDM110", "1.01", "Sony MDM-110" },
    { "SONY", "MDH-10", "1.11", "Sony MDH-10"  },
};

typedef struct md_drive_t {
    uint8_t            id;
    uint8_t            scsi_device_id;
    uint8_t            bus_type;
    uint8_t            read_only;

    FILE              *fp;
    void              *priv;

    char               image_path[MAX_IMAGE_PATH_LEN];
    char               prev_image_path[MAX_IMAGE_PATH_LEN + 256];

    char              *image_history[MD_IMAGE_HISTORY];

    uint32_t           type;
    uint32_t           medium_size;

    /* User TOC, kept next to the image as <image>.utoc.bin */
    uint8_t            utoc[MD_UTOC_SECTORS * MD_TOC_SECTOR_SIZE];
} md_drive_t;

/* Its leading members must match scsi_common_t. */
typedef struct md_t {
    mode_sense_pages_t ms_pages_saved;

    md_drive_t        *drv;
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

    int                prevent;
} md_t;

extern md_drive_t md_drives[MD_NUM];

#ifdef __cplusplus
extern "C" {
#endif

extern void md_disk_close(const md_t *dev);
extern void md_disk_reload(const md_t *dev);
extern void md_insert(md_t *dev);

extern void md_global_init(void);
extern void md_hard_reset(void);

extern void md_reset(scsi_common_t *sc);
extern int  md_is_empty(const uint8_t id);
extern void md_load(md_t *dev, const char *fn, const int skip_insert);
extern void md_close(void);

/* Creates a blank, unformatted MD DATA image. Returns 0 on success. */
extern int  md_create_image(const char *fn);

#ifdef __cplusplus
}
#endif

#endif /*EMU_MD_H*/
