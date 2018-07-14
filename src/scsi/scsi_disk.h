/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of SCSI fixed and removable disks.
 *
 * Version:	@(#)scsi_disk.h	1.0.5	2018/06/02
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2017,2018 Miran Grca.
 */


typedef struct {
    mode_sense_pages_t ms_pages_saved;

    hard_disk_t *drv;

    /* Stuff for SCSI hard disks. */
    uint8_t status, phase,
	    error, id,
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
} scsi_disk_t;


extern scsi_disk_t *scsi_disk[HDD_NUM];
extern uint8_t scsi_disks[16];


extern void	scsi_loadhd(int scsi_id, int id);
extern void	scsi_disk_global_init(void);
extern void	scsi_disk_hard_reset(void);
extern void	scsi_disk_close(void);

extern int	scsi_disk_read_capacity(scsi_disk_t *dev, uint8_t *cdb, uint8_t *buffer, uint32_t *len);
extern int	scsi_disk_err_stat_to_scsi(scsi_disk_t *dev);
extern int	scsi_disk_phase_to_scsi(scsi_disk_t *dev);
extern int	find_hdd_for_scsi_id(uint8_t scsi_id);
extern void	build_scsi_disk_map(void);
extern void	scsi_disk_reset(scsi_disk_t *dev);
extern void	scsi_disk_request_sense_for_scsi(scsi_disk_t *dev, uint8_t *buffer, uint8_t alloc_length);
extern void	scsi_disk_command(scsi_disk_t *dev, uint8_t *cdb);
extern void	scsi_disk_callback(scsi_disk_t *dev);
