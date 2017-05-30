/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of SCSI fixed and removable disks.
 *
 * Version:	@(#)scsi_disk.h	1.0.0	2017/05/30
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2017-2017 Miran Grca.
 */

#pragma pack(push,1)
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
	uint32_t last_sector;
	uint32_t seek_pos;
	int data_pos;
	int old_len;
	int cdb_len_setting;
	int cdb_len;
	int request_pos;
	uint64_t base;
	uint8_t hd_cdb[16];
} scsi_hard_disk_t;
#pragma pack(pop)

extern scsi_hard_disk_t shdc[HDC_NUM];

extern void	scsi_disk_insert(uint8_t id);
extern void	scsi_loadhd(int scsi_id, int scsi_lun, int id);
extern void	scsi_reloadhd(int id);
extern void	scsi_unloadhd(int scsi_id, int scsi_lun, int id);

extern FILE *shdf[HDC_NUM];
