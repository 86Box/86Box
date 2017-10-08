/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of SCSI fixed and removable disks.
 *
 * Version:	@(#)scsi_disk.h	1.0.2	2017/09/29
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2017 Miran Grca.
 */


typedef struct {
	/* Stuff for SCSI hard disks. */
	uint8_t cdb[16];
	uint8_t current_cdb[16];
	uint8_t max_cdb_len;
	int requested_blocks;
	int max_blocks_at_once;
	uint16_t request_length;
	int block_total;
	int all_blocks_total;
	uint32_t packet_len;
	int packet_status;
	uint8_t status;
	uint8_t phase;
	uint32_t pos;
	int callback;
	int total_read;
	int unit_attention;
	uint8_t sense[256];
	uint8_t previous_command;
	uint8_t error;
	uint32_t sector_pos;
	uint32_t sector_len;
	uint32_t seek_pos;
	int data_pos;
	int old_len;
	int request_pos;
	uint8_t hd_cdb[16];

	uint64_t current_page_code;
	int current_page_len;

	int current_page_pos;

	int mode_select_phase;

	int total_length;
	int written_length;

	int do_page_save;
	int block_descriptor_len;
} scsi_hard_disk_t;


extern scsi_hard_disk_t shdc[HDD_NUM];
extern FILE		*shdf[HDD_NUM];


extern void	scsi_disk_insert(uint8_t id);
extern void	scsi_loadhd(int scsi_id, int scsi_lun, int id);
extern void	scsi_reloadhd(int id);
extern void	scsi_unloadhd(int scsi_id, int scsi_lun, int id);

int scsi_hd_read_capacity(uint8_t id, uint8_t *cdb, uint8_t *buffer, uint32_t *len);
