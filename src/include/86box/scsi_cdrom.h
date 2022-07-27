/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the CD-ROM drive with SCSI(-like)
 *		commands, for both ATAPI and SCSI usage.
 *
 *
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2018,2019 Miran Grca.
 */

#ifndef EMU_SCSI_CDROM_H
#define EMU_SCSI_CDROM_H

#define CDROM_TIME	10.0


#ifdef SCSI_DEVICE_H
typedef struct {
    /* Common block. */
    mode_sense_pages_t ms_pages_saved;

    cdrom_t *drv;

    uint8_t *buffer,
	    atapi_cdb[16],
	    current_cdb[16],
	    sense[256];

    uint8_t status, phase,
	    error, id,
	    features, cur_lun,
	    pad0, pad1;

    uint16_t request_length, max_transfer_len;

    int requested_blocks, packet_status,
	total_length, do_page_save,
	unit_attention, request_pos,
	old_len, media_status;

    uint32_t sector_pos, sector_len,
	     packet_len, pos;

    double callback;
} scsi_cdrom_t;
#endif


extern scsi_cdrom_t	*scsi_cdrom[CDROM_NUM];

#define scsi_cdrom_sense_error dev->sense[0]
#define scsi_cdrom_sense_key dev->sense[2]
#define scsi_cdrom_asc dev->sense[12]
#define scsi_cdrom_ascq dev->sense[13]
#define scsi_cdrom_drive cdrom_drives[id].host_drive


extern void	scsi_cdrom_reset(scsi_common_t *sc);


#endif	/*EMU_SCSI_CDROM_H*/
