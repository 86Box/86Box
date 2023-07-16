/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          Emulation of SCSI fixed and removable disks.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2017-2018 Miran Grca.
 */

#ifndef SCSI_DISK_H
#define SCSI_DISK_H

typedef struct scsi_disk_t {
    mode_sense_pages_t ms_pages_saved;

    hard_disk_t *drv;

    uint8_t *temp_buffer;
    uint8_t pad[16]; /* This is atapi_cdb in ATAPI-supporting devices,
                        and pad in SCSI-only devices. */
    uint8_t current_cdb[16];
    uint8_t sense[256];

    uint8_t status;
    uint8_t phase;
    uint8_t error;
    uint8_t id;
    uint8_t pad0;
    uint8_t cur_lun;
    uint8_t pad1;
    uint8_t pad2;

    uint16_t request_length;
    uint16_t pad4;

    int requested_blocks;
    int packet_status;
    int total_length;
    int do_page_save;
    int unit_attention;
    int pad5;
    int pad6;
    int pad7;

    uint32_t sector_pos;
    uint32_t sector_len;
    uint32_t packet_len;
    uint32_t pos;

    double callback;
} scsi_disk_t;

extern scsi_disk_t *scsi_disk[HDD_NUM];

extern void scsi_disk_hard_reset(void);
extern void scsi_disk_close(void);

#endif /*SCSI_DISK_H*/
