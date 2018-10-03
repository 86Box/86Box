/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of SCSI fixed and removable disks.
 *
 * Version:	@(#)scsi_disk.h	1.0.4	2018/04/24
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2017,2018 Miran Grca.
 */


typedef struct {
	/* Stuff for SCSI hard disks. */
	uint8_t status, phase,
		error,		
		current_cdb[16],
		sense[256];

	uint16_t request_length;

	int requested_blocks, block_total,
	    packet_status, callback,
	    block_descriptor_len,
	    total_length, do_page_save;

	uint32_t sector_pos, sector_len,
		 packet_len;

	uint64_t current_page_code;

	uint8_t *temp_buffer;
} scsi_hard_disk_t;


extern scsi_hard_disk_t shdc[HDD_NUM];
extern FILE		*shdf[HDD_NUM];


extern void	scsi_loadhd(int scsi_id, int scsi_lun, int id);

int scsi_hd_read_capacity(uint8_t id, uint8_t *cdb, uint8_t *buffer, uint32_t *len);
