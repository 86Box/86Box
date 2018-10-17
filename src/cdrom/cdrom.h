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
 * Version:	@(#)cdrom.h	1.0.15	2018/10/17
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

/* This is so that if/when this is changed to something else,
   changing this one define will be enough. */
#define CDROM_EMPTY !dev->host_drive


#ifdef __cplusplus
extern "C" {
#endif

enum {
    CDROM_BUS_DISABLED = 0,
    CDROM_BUS_ATAPI = 4,
    CDROM_BUS_SCSI,
    CDROM_BUS_USB
};


/* To shut up the GCC compilers. */
struct cdrom;


/* Define the various CD-ROM drive operations (ops). */
typedef struct {
    int		(*ready)(struct cdrom *dev);
    int		(*medium_changed)(struct cdrom *dev);
    int		(*media_type_id)(struct cdrom *dev);

    int		(*audio_callback)(struct cdrom *dev, int16_t *output, int len);
    void	(*audio_stop)(struct cdrom *dev);
    int		(*readtoc)(struct cdrom *dev, uint8_t *b, uint8_t starttrack, int msf, int maxlen, int single);
    int		(*readtoc_session)(struct cdrom *dev, uint8_t *b, int msf, int maxlen);
    int		(*readtoc_raw)(struct cdrom *dev, uint8_t *b, int maxlen);
    uint8_t	(*getcurrentsubchannel)(struct cdrom *dev, uint8_t *b, int msf);
    int		(*readsector_raw)(struct cdrom *dev, uint8_t *buffer, int sector, int ismsf, int cdrom_sector_type, int cdrom_sector_flags, int *len);
    uint8_t	(*playaudio)(struct cdrom *dev, uint32_t pos, uint32_t len, int ismsf);
    void	(*pause)(struct cdrom *dev);
    void	(*resume)(struct cdrom *dev);
    uint32_t	(*size)(struct cdrom *dev);
    int		(*status)(struct cdrom *dev);
    void	(*stop)(struct cdrom *dev);
    void	(*exit)(struct cdrom *dev);
} cdrom_ops_t;

typedef struct cdrom {
    uint8_t id,
	    speed, cur_speed,
	    ide_channel, scsi_device_id,
	    bus_type,		/* 0 = ATAPI, 1 = SCSI */
	    bus_mode,		/* Bit 0 = PIO suported;
				   Bit 1 = DMA supportd. */
	    sound_on;

    FILE* img_fp;
    int img_is_iso,
	host_drive, prev_host_drive,
	cd_status, prev_status,
	cd_buflen, cd_state;

    uint32_t seek_pos, seek_diff,
	     cd_end,
	     cdrom_capacity;

    const cdrom_ops_t	*ops;

    void	*image;

    void	*p;

    void	(*insert)(void *p);
    void	(*close)(void *p);
    uint32_t	(*get_volume)(void *p, int channel);
    uint32_t	(*get_channel)(void *p, int channel);

    wchar_t image_path[1024],
	    *prev_image_path;

    int16_t cd_buffer[BUF_SIZE];
} cdrom_t;


extern cdrom_t	cdrom[CDROM_NUM];

extern int	cdrom_lba_to_msf_accurate(int lba);
extern double	cdrom_seek_time(cdrom_t *dev);
extern void	cdrom_seek(cdrom_t *dev, uint32_t pos);
extern int	cdrom_playing_completed(cdrom_t *dev);

extern void     cdrom_close_handler(uint8_t id);
extern void	cdrom_insert(uint8_t id);
extern void	cdrom_eject(uint8_t id);
extern void	cdrom_reload(uint8_t id);

extern int	cdrom_image_open(cdrom_t *dev, const wchar_t *fn);
extern void	cdrom_image_close(cdrom_t *dev);
extern void	cdrom_image_reset(cdrom_t *dev);

extern void	cdrom_update_cdb(uint8_t *cdb, int lba_pos,
				 int number_of_blocks);

extern int	find_cdrom_for_scsi_id(uint8_t scsi_id);

extern void     cdrom_close(void);
extern void	cdrom_global_init(void);
extern void	cdrom_global_reset(void);
extern void	cdrom_hard_reset(void);
extern void	scsi_cdrom_drive_reset(int c);

#ifdef __cplusplus
}
#endif


#endif	/*EMU_CDROM_H*/
