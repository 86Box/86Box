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
 * Version:	@(#)zip.h	1.0.6	2018/04/30
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2018 Miran Grca.
 */
#ifndef EMU_ZIP_H
#define EMU_ZIP_H


#define ZIP_NUM			  4

#define ZIP_PHASE_IDLE            0x00
#define ZIP_PHASE_COMMAND         0x01
#define ZIP_PHASE_COMPLETE        0x02
#define ZIP_PHASE_DATA_IN         0x03
#define ZIP_PHASE_DATA_IN_DMA     0x04
#define ZIP_PHASE_DATA_OUT        0x05
#define ZIP_PHASE_DATA_OUT_DMA    0x06
#define ZIP_PHASE_ERROR           0x80

#define BUF_SIZE 32768

#define ZIP_TIME (5LL * 100LL * (1LL << TIMER_SHIFT))

#define ZIP_SECTORS (96*2048)

#define ZIP_250_SECTORS (489532)


enum {
    ZIP_BUS_DISABLED = 0,
    ZIP_BUS_ATAPI = 4,
    ZIP_BUS_SCSI,
    ZIP_BUS_USB
};


typedef struct {
    uint8_t previous_command, error,
	    features, status,
	    phase, *buffer,
	    atapi_cdb[16],
	    current_cdb[16],
	    sense[256];

    uint16_t request_length, max_transfer_len;

    int toctimes, media_status,
	is_dma, requested_blocks,
	current_page_len, current_page_pos,
	total_length, written_length,
	mode_select_phase, do_page_save,
	callback, data_pos,
	packet_status, unit_attention,
	cdb_len_setting, cdb_len,
	request_pos, total_read,
	block_total, all_blocks_total,
	old_len, block_descriptor_len,
	init_length;

    uint32_t sector_pos, sector_len,
	     packet_len, pos,
	     seek_pos;

    uint64_t current_page_code;
} zip_t;

typedef struct {
    unsigned int bus_type;	/* 0 = ATAPI, 1 = SCSI */
    uint8_t ide_channel,
	    bus_mode;		/* Bit 0 = PIO suported;
				   Bit 1 = DMA supportd. */

    unsigned int scsi_device_id, scsi_device_lun,
		 is_250;

    wchar_t image_path[1024],
	    prev_image_path[1024];

    int read_only, ui_writeprot;

    uint32_t medium_size, base;

    FILE *f;
} zip_drive_t;


extern zip_t		*zip[ZIP_NUM];
extern zip_drive_t	zip_drives[ZIP_NUM];
extern uint8_t		atapi_zip_drives[8];
extern uint8_t		scsi_zip_drives[16][8];

#define zip_sense_error zip[id]->sense[0]
#define zip_sense_key zip[id]->sense[2]
#define zip_asc zip[id]->sense[12]
#define zip_ascq zip[id]->sense[13]


#ifdef __cplusplus
extern "C" {
#endif

extern int	(*ide_bus_master_read)(int channel, uint8_t *data, int transfer_length, void *priv);
extern int	(*ide_bus_master_write)(int channel, uint8_t *data, int transfer_length, void *priv);
extern void	(*ide_bus_master_set_irq)(int channel, void *priv);
extern void	*ide_bus_master_priv[2];
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

extern void	zip_destroy_drives(void);
extern void	zip_close(uint8_t id);

#ifdef __cplusplus
}
#endif


#endif	/*EMU_ZIP_H*/
