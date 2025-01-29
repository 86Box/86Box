/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the CD-ROM drive with SCSI(-like)
 *          commands, for both ATAPI and SCSI usage.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2018-2019 Miran Grca.
 */

#ifndef EMU_SCSI_CDROM_H
#define EMU_SCSI_CDROM_H

#define CDROM_TIME 10.0

#ifdef SCSI_DEVICE_H
typedef struct scsi_cdrom_t {
    /* Common block. */
    mode_sense_pages_t ms_pages_saved;

    cdrom_t *          drv;
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
    uint8_t            early;
    uint8_t            sector_type;

    uint16_t           max_transfer_len;
    uint16_t           sector_flags;

    int                requested_blocks;
    int                packet_status;
    int                total_length;
    int                do_page_save;
    int                unit_attention;
    int                request_pos;
    int                old_len;
    int                media_status;

    uint32_t           sector_pos;
    uint32_t           sector_len;
    uint32_t           packet_len;

    double             callback;

    int                is_sony;
    int                use_cdb_9;

    uint8_t            ven_cmd_is_data[256];

    mode_sense_pages_t ms_drive_status_pages_saved;

    uint64_t           ms_page_flags;

    mode_sense_pages_t ms_pages_default;
    mode_sense_pages_t ms_pages_changeable;

    uint8_t            (*ven_cmd)(void *sc, const uint8_t *cdb, int32_t *BufLen);
} scsi_cdrom_t;
#endif

extern scsi_cdrom_t *scsi_cdrom[CDROM_NUM];

#define scsi_cdrom_sense_error dev->sense[0]
#define scsi_cdrom_sense_key   dev->sense[2]
#define scsi_cdrom_info        *(uint32_t *) &(dev->sense[3])
#define scsi_cdrom_asc         dev->sense[12]
#define scsi_cdrom_ascq        dev->sense[13]
#define scsi_cdrom_drive       cdrom_drives[id].host_drive

extern void scsi_cdrom_reset(scsi_common_t *sc);
extern void scsi_cdrom_drive_reset(const int c);

#endif /*EMU_SCSI_CDROM_H*/
