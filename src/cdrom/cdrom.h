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
 * Version:	@(#)cdrom.h	1.0.13	2018/06/18
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016,2017 Miran Grca.
 */
#ifndef EMU_CDROM_H
#define EMU_CDROM_H


#define CDROM_NUM			4

#define CD_STATUS_EMPTY			0
#define CD_STATUS_DATA_ONLY		1
#define CD_STATUS_PLAYING		2
#define CD_STATUS_PAUSED		3
#define CD_STATUS_STOPPED		4

#define CDROM_PHASE_IDLE		0x00
#define CDROM_PHASE_COMMAND		0x01
#define CDROM_PHASE_COMPLETE		0x02
#define CDROM_PHASE_DATA_IN		0x03
#define CDROM_PHASE_DATA_IN_DMA		0x04
#define CDROM_PHASE_DATA_OUT		0x05
#define CDROM_PHASE_DATA_OUT_DMA	0x06
#define CDROM_PHASE_ERROR		0x80

#define BUF_SIZE 32768

#define CDROM_IMAGE 200

#define CDROM_TIME (5LL * 100LL * (1LL << TIMER_SHIFT))


enum {
    CDROM_BUS_DISABLED = 0,
    CDROM_BUS_ATAPI = 4,
    CDROM_BUS_SCSI,
    CDROM_BUS_USB
};


typedef struct {
    int		(*ready)(uint8_t id);
    int		(*medium_changed)(uint8_t id);
    int		(*media_type_id)(uint8_t id);

    int		(*audio_callback)(uint8_t id, int16_t *output, int len);
    void	(*audio_stop)(uint8_t id);
    int		(*readtoc)(uint8_t id, uint8_t *b, uint8_t starttrack, int msf, int maxlen, int single);
    int		(*readtoc_session)(uint8_t id, uint8_t *b, int msf, int maxlen);
    int		(*readtoc_raw)(uint8_t id, uint8_t *b, int maxlen);
    uint8_t	(*getcurrentsubchannel)(uint8_t id, uint8_t *b, int msf);
    int		(*readsector_raw)(uint8_t id, uint8_t *buffer, int sector, int ismsf, int cdrom_sector_type, int cdrom_sector_flags, int *len);
    uint8_t	(*playaudio)(uint8_t id, uint32_t pos, uint32_t len, int ismsf);
    void	(*pause)(uint8_t id);
    void	(*resume)(uint8_t id);
    uint32_t	(*size)(uint8_t id);
    int		(*status)(uint8_t id);
    void	(*stop)(uint8_t id);
    void	(*exit)(uint8_t id);
} CDROM;

typedef struct {
    int host_drive;
    int prev_host_drive;

    unsigned int bus_type;	/* 0 = ATAPI, 1 = SCSI */

    uint8_t speed, ide_channel,
	    bus_mode;		/* Bit 0 = PIO suported;
				   Bit 1 = DMA supportd. */

    unsigned int scsi_device_id, sound_on;
} cdrom_drive_t;

typedef struct {
    mode_sense_pages_t ms_pages_saved;

    CDROM *handler;
    cdrom_drive_t *drv;

    uint8_t previous_command,
	    error, features,
	    status, phase,
	    id, *buffer,
	    atapi_cdb[16],
	    current_cdb[16],
	    sense[256];

    uint16_t request_length, max_transfer_len;
    int16_t cd_buffer[BUF_SIZE];

    int media_status, is_dma,
	packet_status, requested_blocks,
	current_page_len, current_page_pos,
	mode_select_phase, do_page_save,
	total_length, written_length,
	callback, data_pos,
	cd_status, prev_status,
	unit_attention, request_pos,
	total_read, cur_speed,
	block_total, all_blocks_total,
	old_len, block_descriptor_len,
	init_length, last_subchannel_pos,
	cd_buflen, cd_state,
	handler_inited, disc_changed;

    uint32_t sector_pos, sector_len,
	     seek_pos, seek_diff,
	     pos, packet_len,
	     cdb_len, cd_end,
	     cdrom_capacity;

    uint64_t current_page_code;
} cdrom_t;

typedef struct {
    int image_is_iso;
    wchar_t image_path[1024],
	    *prev_image_path;
    FILE* image;
} cdrom_image_t;


extern cdrom_t		*cdrom[CDROM_NUM];
extern cdrom_drive_t	cdrom_drives[CDROM_NUM];
extern cdrom_image_t	cdrom_image[CDROM_NUM];
extern uint8_t		atapi_cdrom_drives[8];
extern uint8_t		scsi_cdrom_drives[16];

#define cdrom_sense_error dev->sense[0]
#define cdrom_sense_key dev->sense[2]
#define cdrom_asc dev->sense[12]
#define cdrom_ascq dev->sense[13]
#define cdrom_drive cdrom_drives[id].host_drive


#ifdef __cplusplus
extern "C" {
#endif

extern int	(*ide_bus_master_read)(int channel, uint8_t *data, int transfer_length, void *priv);
extern int	(*ide_bus_master_write)(int channel, uint8_t *data, int transfer_length, void *priv);
extern void	(*ide_bus_master_set_irq)(int channel, void *priv);
extern void	*ide_bus_master_priv[2];

extern uint32_t	cdrom_mode_sense_get_channel(cdrom_t *dev, int channel);
extern uint32_t	cdrom_mode_sense_get_volume(cdrom_t *dev, int channel);
extern void	build_atapi_cdrom_map(void);
extern void	build_scsi_cdrom_map(void);
extern int	cdrom_CDROM_PHASE_to_scsi(cdrom_t *dev);
extern int	cdrom_atapi_phase_to_scsi(cdrom_t *dev);
extern void	cdrom_command(cdrom_t *dev, uint8_t *cdb);
extern void	cdrom_phase_callback(cdrom_t *dev);
extern uint32_t	cdrom_read(uint8_t channel, int length);
extern void	cdrom_write(uint8_t channel, uint32_t val, int length);

extern int	cdrom_lba_to_msf_accurate(int lba);

extern void     cdrom_close_handler(uint8_t id);
extern void     cdrom_close(void);
extern void	cdrom_reset(cdrom_t *dev);
extern void	cdrom_set_signature(cdrom_t *dev);
extern void	cdrom_request_sense_for_scsi(cdrom_t *dev, uint8_t *buffer, uint8_t alloc_length);
extern void	cdrom_update_cdb(uint8_t *cdb, int lba_pos, int number_of_blocks);
extern void	cdrom_insert(cdrom_t *dev);

extern int	find_cdrom_for_scsi_id(uint8_t scsi_id);
extern int	cdrom_read_capacity(cdrom_t *dev, uint8_t *cdb, uint8_t *buffer, uint32_t *len);

extern void	cdrom_global_init(void);
extern void	cdrom_global_reset(void);
extern void	cdrom_hard_reset(void);

#ifdef __cplusplus
}
#endif


#endif	/*EMU_CDROM_H*/
