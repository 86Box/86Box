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

    hard_disk_t *      drv;
#ifdef EMU_IDE_H
    ide_tf_t *         tf;
#else
    void *             tf;
#endif

    void *             log;

    uint8_t *          temp_buffer;
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
    int                pad6;
    int                pad7;

    uint32_t           sector_pos;
    uint32_t           sector_len;
    uint32_t           packet_len;

    double             callback;

    uint8_t            (*ven_cmd)(void *sc, uint8_t *cdb, int32_t *BufLen);
} scsi_disk_t;

extern scsi_disk_t *scsi_disk[HDD_NUM];

extern void scsi_disk_reset(scsi_common_t *sc);

extern void scsi_disk_hard_reset(void);
extern void scsi_disk_close(void);

#endif /*SCSI_DISK_H*/
