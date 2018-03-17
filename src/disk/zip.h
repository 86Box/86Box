/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Iomega ZIP drive with SCSI(-like)
 *		commands, for both ATAPI and SCSI usage.
 *
 * Version:	@(#)zip.h	1.0.3	2018/03/17
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2018 Miran Grca.
 */
#ifndef EMU_ZIP_H
#define EMU_ZIP_H


#define ZIP_NUM		4

#define ZIP_PHASE_IDLE            0
#define ZIP_PHASE_COMMAND         1
#define ZIP_PHASE_COMPLETE        2
#define ZIP_PHASE_DATA_IN         3
#define ZIP_PHASE_DATA_IN_DMA     4
#define ZIP_PHASE_DATA_OUT        5
#define ZIP_PHASE_DATA_OUT_DMA    6
#define ZIP_PHASE_ERROR           0x80

#define BUF_SIZE 32768

#define IDE_TIME (5LL * 100LL * (1LL << TIMER_SHIFT))
#define ZIP_TIME (5LL * 100LL * (1LL << TIMER_SHIFT))

#define ZIP_SECTORS (96*2048)

#define ZIP_250_SECTORS (489532)


enum {
    ZIP_BUS_DISABLED = 0,
    ZIP_BUS_ATAPI_PIO_ONLY = 4,
    ZIP_BUS_ATAPI_PIO_AND_DMA,
    ZIP_BUS_SCSI,
    ZIP_BUS_USB = 8
};


typedef struct {
	uint8_t previous_command;

	int toctimes;
	int media_status;

	int is_dma;

	int requested_blocks;		/* This will be set to something other than 1 when block reads are implemented. */

	uint64_t current_page_code;
	int current_page_len;

	int current_page_pos;

	int mode_select_phase;

	int total_length;
	int written_length;

	int do_page_save;

	uint8_t error;
	uint8_t features;
	uint16_t request_length;
	uint16_t max_transfer_len;
	uint8_t status;
	uint8_t phase;

	uint32_t sector_pos;
	uint32_t sector_len;

	uint32_t packet_len;
	int packet_status;

	uint8_t atapi_cdb[16];
	uint8_t current_cdb[16];

	uint32_t pos;

	int callback;

	int data_pos;

	int cdb_len_setting;
	int cdb_len;

	int cd_status;
	int prev_status;

	int unit_attention;
	uint8_t sense[256];

	int request_pos;

	uint8_t *buffer;

	int times;

	uint32_t seek_pos;
	
	int total_read;

	int block_total;
	int all_blocks_total;
	
	int old_len;
	int block_descriptor_len;
	
	int init_length;
} zip_t;

typedef struct {
	int max_blocks_at_once;

	int host_drive;
	int prev_host_drive;

	unsigned int bus_type;		/* 0 = ATAPI, 1 = SCSI */
	uint8_t bus_mode;		/* Bit 0 = PIO suported;
					   Bit 1 = DMA supportd. */

	uint8_t ide_channel;

	unsigned int scsi_device_id;
	unsigned int scsi_device_lun;
	
	unsigned int is_250;
	unsigned int atapi_dma;

	wchar_t image_path[1024];
	wchar_t prev_image_path[1024];

	uint32_t medium_size;

	int read_only;
	int ui_writeprot;

	uint32_t base;

	FILE *f;
} zip_drive_t;


extern zip_t		zip[ZIP_NUM];
extern zip_drive_t	zip_drives[ZIP_NUM];
extern uint8_t		atapi_zip_drives[8];
extern uint8_t		scsi_zip_drives[16][8];

#define zip_sense_error zip[id].sense[0]
#define zip_sense_key zip[id].sense[2]
#define zip_asc zip[id].sense[12]
#define zip_ascq zip[id].sense[13]
#define zip_drive zip_drives[id].host_drive


#ifdef __cplusplus
extern "C" {
#endif

extern int	(*ide_bus_master_read)(int channel, uint8_t *data, int transfer_length);
extern int	(*ide_bus_master_write)(int channel, uint8_t *data, int transfer_length);
extern void	(*ide_bus_master_set_irq)(int channel);
extern void	ioctl_close(uint8_t id);

extern uint32_t	zip_mode_sense_get_channel(uint8_t id, int channel);
extern uint32_t	zip_mode_sense_get_volume(uint8_t id, int channel);
extern void	build_atapi_zip_map(void);
extern void	build_scsi_zip_map(void);
extern int	zip_ZIP_PHASE_to_scsi(uint8_t id);
extern int	zip_atapi_phase_to_scsi(uint8_t id);
extern void	zip_command(uint8_t id, uint8_t *cdb);
extern void	zip_phase_callback(uint8_t id);
extern uint32_t	zip_read(uint8_t channel, int length);
extern void	zip_write(uint8_t channel, uint32_t val, int length);

extern int	zip_lba_to_msf_accurate(int lba);

extern void     zip_close(uint8_t id);
extern void     zip_disk_reload(uint8_t id);
extern void	zip_reset(uint8_t id);
extern void	zip_set_signature(int id);
extern void	zip_request_sense_for_scsi(uint8_t id, uint8_t *buffer, uint8_t alloc_length);
extern void	zip_update_cdb(uint8_t *cdb, int lba_pos, int number_of_blocks);
extern void	zip_insert(uint8_t id);

extern int	find_zip_for_scsi_id(uint8_t scsi_id, uint8_t scsi_lun);
extern int	zip_read_capacity(uint8_t id, uint8_t *cdb, uint8_t *buffer, uint32_t *len);

extern void	zip_global_init(void);
extern void	zip_hard_reset(void);

extern int	zip_load(uint8_t id, wchar_t *fn);
extern void	zip_close(uint8_t id);

#ifdef __cplusplus
}
#endif


#endif	/*EMU_ZIP_H*/
