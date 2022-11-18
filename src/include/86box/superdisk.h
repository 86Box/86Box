/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Imation SuperDisk drive with SCSI(-like)
 *          commands, for both ATAPI and SCSI usage.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2018-2019 Miran Grca.
 *          Copyright 2022-2023 Jasmine Iwanek.
 */

#ifndef EMU_SUPERDISK_H
#define EMU_SUPERDISK_H

#define SUPERDISK_NUM           4

#define BUF_SIZE                32768

#define SUPERDISK_TIME          10.0

#define SUPERDISK_SECTORS       (963 * 256)

#define SUPERDISK_240_SECTORS   (469504)

#define SUPERDISK_IMAGE_HISTORY 4

enum {
    SUPERDISK_BUS_DISABLED = 0,
    SUPERDISK_BUS_ATAPI    = 5,
    SUPERDISK_BUS_SCSI     = 6,
    SUPERDISK_BUS_USB      = 7
};

typedef struct superdisk_drive_t {
    uint8_t id;

    union {
        uint8_t res;
        uint8_t res0; /* Reserved for other ID's. */
        uint8_t res1;
        uint8_t ide_channel;
        uint8_t scsi_device_id;
    };

    uint8_t bus_type;  /* 0 = ATAPI, 1 = SCSI */
    uint8_t bus_mode;  /* Bit 0 = PIO suported;
                          Bit 1 = DMA supportd. */
    uint8_t read_only; /* Struct variable reserved for
                          media status. */
    uint8_t pad;
    uint8_t pad0;

    FILE *fp;
    void *priv;

    char image_path[1024];
    char prev_image_path[1024];

    char *image_history[SUPERDISK_IMAGE_HISTORY];

    uint32_t is_240;
    uint32_t medium_size;
    uint32_t base;
} superdisk_drive_t;

typedef struct superdisk_t {
    mode_sense_pages_t ms_pages_saved;

    superdisk_drive_t *drv;
#ifdef EMU_IDE_H
    ide_tf_t          *tf;
#else
    void              *tf;
#endif

    uint8_t *buffer;
    uint8_t  atapi_cdb[16];
    uint8_t  current_cdb[16];
    uint8_t  sense[256];

#ifdef ANCIENT_CODE
    /* Task file. */
    uint8_t features;
    uint8_t phase;
    uint16_t request_length;
    uint8_t status;
    uint8_t error;
    uint16_t pad;
    uint32_t pos;
#endif

    uint8_t id;
    uint8_t cur_lun;
    uint8_t pad0;
    uint8_t pad1;

    uint16_t max_transfer_len;
    uint16_t pad2;

    int requested_blocks;
    int packet_status;
    int total_length;
    int do_page_save;
    int unit_attention;
    int request_pos;
    int old_len;
    int pad3;

    uint32_t sector_pos;
    uint32_t sector_len;
    uint32_t packet_len;

    double callback;
} superdisk_t;

extern superdisk_t      *superdisk[SUPERDISK_NUM];
extern superdisk_drive_t superdisk_drives[SUPERDISK_NUM];

#define superdisk_sense_error dev->sense[0]
#define superdisk_sense_key   dev->sense[2]
#define superdisk_asc         dev->sense[12]
#define superdisk_ascq        dev->sense[13]

#ifdef __cplusplus
extern "C" {
#endif

extern void superdisk_disk_close(superdisk_t *dev);
extern void superdisk_disk_reload(superdisk_t *dev);
extern void superdisk_insert(superdisk_t *dev);

extern void superdisk_global_init(void);
extern void superdisk_hard_reset(void);

extern void superdisk_reset(scsi_common_t *sc);
extern int  superdisk_load(superdisk_t *dev, char *fn);
extern void superdisk_close(void);

#ifdef __cplusplus
}
#endif

#endif /*EMU_SUPERDISK_H*/
