/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Iomega ZIP drive with SCSI(-like)
 *          commands, for both ATAPI and SCSI usage.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2018-2025 Miran Grca.
 */

#ifndef EMU_ZIP_H
#define EMU_ZIP_H

#define ZIP_NUM         4

#define BUF_SIZE        32768

#define ZIP_TIME        10.0

#define ZIP_SECTORS     (96 * 2048)

#define ZIP_250_SECTORS (489532)

#define ZIP_IMAGE_HISTORY 10

enum {
    ZIP_BUS_DISABLED = 0,
    ZIP_BUS_ATAPI    = 5,
    ZIP_BUS_SCSI     = 6,
    ZIP_BUS_USB      = 7
};

typedef struct zip_drive_t {
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

    char *             image_history[ZIP_IMAGE_HISTORY];

    uint32_t           is_250;
    uint32_t           medium_size;
    uint32_t           base;
} zip_drive_t;

typedef struct zip_t {
    mode_sense_pages_t ms_pages_saved;

    zip_drive_t *      drv;
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
} zip_t;

extern zip_t      *zip[ZIP_NUM];
extern zip_drive_t zip_drives[ZIP_NUM];
extern uint8_t     atapi_zip_drives[8];
extern uint8_t     scsi_zip_drives[16];

#define zip_sense_error dev->sense[0]
#define zip_sense_key   dev->sense[2]
#define zip_info        *(uint32_t *) &(dev->sense[3])
#define zip_asc         dev->sense[12]
#define zip_ascq        dev->sense[13]

#ifdef __cplusplus
extern "C" {
#endif

extern void zip_disk_close(const zip_t *dev);
extern void zip_disk_reload(const zip_t *dev);
extern void zip_insert(zip_t *dev);

extern void zip_global_init(void);
extern void zip_hard_reset(void);

extern void zip_reset(scsi_common_t *sc);
extern int  zip_is_empty(const uint8_t id);
extern void zip_load(const zip_t *dev, const char *fn, const int skip_insert);
extern void zip_close(void);

#ifdef __cplusplus
}
#endif

#endif /*EMU_ZIP_H*/
