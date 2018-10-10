/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Generic CD-ROM drive core header.
 *
 * Version:	@(#)cdrom.h	1.0.14	2018/10/09
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

#define BUF_SIZE 32768

#define CDROM_IMAGE 200


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
    CDROM *handler;

    int16_t cd_buffer[BUF_SIZE];

    uint8_t speed, ide_channel,
	    pad, bus_mode;	/* Bit 0 = PIO suported;
				   Bit 1 = DMA supportd. */
    int host_drive, prev_host_drive,
	cd_status, prev_status,
	cd_buflen, cd_state,
	handler_inited, cur_speed,
	id;

    unsigned int bus_type,	/* 0 = ATAPI, 1 = SCSI */
		 scsi_device_id, sound_on;

    uint32_t seek_pos, seek_diff,
	     cd_end, cdrom_capacity;

    void	*p;

    void	(*insert)(void *p);
    uint32_t	(*get_volume)(void *p, int channel);
    uint32_t	(*get_channel)(void *p, int channel);
    void	(*close)(void *p);
} cdrom_drive_t;

typedef struct {
    wchar_t image_path[1024],
	    *prev_image_path;
    FILE* image;

    int image_is_iso;
} cdrom_image_t;


extern cdrom_drive_t	cdrom_drives[CDROM_NUM];
extern cdrom_image_t	cdrom_image[CDROM_NUM];


#ifdef __cplusplus
extern "C" {
#endif

extern int	cdrom_lba_to_msf_accurate(int lba);
extern double	cdrom_get_short_seek(cdrom_drive_t *dev);
extern double	cdrom_get_long_seek(cdrom_drive_t *dev);
extern double	cdrom_seek_time(cdrom_drive_t *dev);
extern void	cdrom_seek(cdrom_drive_t *dev, uint32_t pos);
extern int	cdrom_playing_completed(cdrom_drive_t *dev);

extern void     cdrom_close_handler(uint8_t id);
extern void     cdrom_close(void);
extern void	cdrom_update_cdb(uint8_t *cdb, int lba_pos, int number_of_blocks);

extern int	find_cdrom_for_scsi_id(uint8_t scsi_id);

extern void	cdrom_global_init(void);
extern void	cdrom_global_reset(void);
extern void	cdrom_hard_reset(void);
extern void	scsi_cdrom_drive_reset(int c);

#ifdef __cplusplus
}
#endif


#endif	/*EMU_CDROM_H*/
