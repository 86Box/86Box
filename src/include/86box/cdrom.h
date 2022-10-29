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
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2019 Miran Grca.
 */
#ifndef EMU_CDROM_H
#define EMU_CDROM_H

#define CDROM_NUM                   4

#define CD_STATUS_EMPTY             0
#define CD_STATUS_DATA_ONLY         1
#define CD_STATUS_PAUSED            2
#define CD_STATUS_PLAYING           3
#define CD_STATUS_STOPPED           4
#define CD_STATUS_PLAYING_COMPLETED 5

/* Medium changed flag. */
#define CD_STATUS_MEDIUM_CHANGED 0x80

#define CD_TRACK_AUDIO           0x08
#define CD_TRACK_MODE2           0x04

#define CD_READ_DATA             0
#define CD_READ_AUDIO            1
#define CD_READ_RAW              2

#define CD_TOC_NORMAL            0
#define CD_TOC_SESSION           1
#define CD_TOC_RAW               2

#define CD_IMAGE_HISTORY         4

#define BUF_SIZE                 32768

#define CDROM_IMAGE              200

/* This is so that if/when this is changed to something else,
   changing this one define will be enough. */
#define CDROM_EMPTY !dev->host_drive

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CDROM_BUS_DISABLED = 0,
    CDROM_BUS_ATAPI    = 5,
    CDROM_BUS_SCSI,
    CDROM_BUS_USB
};

/* To shut up the GCC compilers. */
struct cdrom;

typedef struct {
    uint8_t attr, track,
        index,
        abs_m, abs_s, abs_f,
        rel_m, rel_s, rel_f;
} subchannel_t;

typedef struct {
    int     number;
    uint8_t attr, m, s, f;
} track_info_t;

/* Define the various CD-ROM drive operations (ops). */
typedef struct {
    void (*get_tracks)(struct cdrom *dev, int *first, int *last);
    void (*get_track_info)(struct cdrom *dev, uint32_t track, int end, track_info_t *ti);
    void (*get_subchannel)(struct cdrom *dev, uint32_t lba, subchannel_t *subc);
    int (*is_track_pre)(struct cdrom *dev, uint32_t lba);
    int (*sector_size)(struct cdrom *dev, uint32_t lba);
    int (*read_sector)(struct cdrom *dev, int type, uint8_t *b, uint32_t lba);
    int (*track_type)(struct cdrom *dev, uint32_t lba);
    void (*exit)(struct cdrom *dev);
} cdrom_ops_t;

typedef struct cdrom {
    uint8_t id;

    union {
        uint8_t res, res0, /* Reserved for other ID's. */
            res1,
            ide_channel, scsi_device_id;
    };

    uint8_t bus_type, /* 0 = ATAPI, 1 = SCSI */
        bus_mode,     /* Bit 0 = PIO suported;
                         Bit 1 = DMA supportd. */
        cd_status,    /* Struct variable reserved for
                         media status. */
        speed, cur_speed;

    int   is_dir;
    void *priv;

    char image_path[1024],
        prev_image_path[1024];

    char *image_history[CD_IMAGE_HISTORY];

    uint32_t sound_on, cdrom_capacity,
        early, seek_pos,
        seek_diff, cd_end;

    int host_drive, prev_host_drive,
        cd_buflen, noplay;

    const cdrom_ops_t *ops;

    void *image;

    void (*insert)(void *p);
    void (*close)(void *p);
    uint32_t (*get_volume)(void *p, int channel);
    uint32_t (*get_channel)(void *p, int channel);

    int16_t cd_buffer[BUF_SIZE];
} cdrom_t;

extern cdrom_t cdrom[CDROM_NUM];

extern int     cdrom_lba_to_msf_accurate(int lba);
extern double  cdrom_seek_time(cdrom_t *dev);
extern void    cdrom_stop(cdrom_t *dev);
extern int     cdrom_is_pre(cdrom_t *dev, uint32_t lba);
extern int     cdrom_audio_callback(cdrom_t *dev, int16_t *output, int len);
extern uint8_t cdrom_audio_play(cdrom_t *dev, uint32_t pos, uint32_t len, int ismsf);
extern uint8_t cdrom_audio_track_search(cdrom_t *dev, uint32_t pos, int type, uint8_t playbit);
extern uint8_t cdrom_toshiba_audio_play(cdrom_t *dev, uint32_t pos, int type);
extern void    cdrom_audio_pause_resume(cdrom_t *dev, uint8_t resume);
extern uint8_t cdrom_get_current_subchannel(cdrom_t *dev, uint8_t *b, int msf);
extern uint8_t cdrom_get_current_subcodeq_playstatus(cdrom_t *dev, uint8_t *b);
extern int     cdrom_read_toc(cdrom_t *dev, unsigned char *b, int type,
                              unsigned char start_track, int msf, int max_len);
extern void    cdrom_get_track_buffer(cdrom_t *dev, uint8_t *buf);
extern int     cdrom_readsector_raw(cdrom_t *dev, uint8_t *buffer, int sector, int ismsf,
                                    int cdrom_sector_type, int cdrom_sector_flags, int *len);
extern void    cdrom_read_disc_info_toc(cdrom_t *dev, unsigned char *b, unsigned char track, int type);

extern void cdrom_seek(cdrom_t *dev, uint32_t pos);

extern void cdrom_close_handler(uint8_t id);
extern void cdrom_insert(uint8_t id);
extern void cdrom_eject(uint8_t id);
extern void cdrom_reload(uint8_t id);

extern int  cdrom_image_open(cdrom_t *dev, const char *fn);
extern void cdrom_image_close(cdrom_t *dev);
extern void cdrom_image_reset(cdrom_t *dev);

extern void cdrom_update_cdb(uint8_t *cdb, int lba_pos,
                             int number_of_blocks);

extern int find_cdrom_for_scsi_id(uint8_t scsi_id);

extern void cdrom_close(void);
extern void cdrom_global_init(void);
extern void cdrom_global_reset(void);
extern void cdrom_hard_reset(void);
extern void scsi_cdrom_drive_reset(int c);

#ifdef __cplusplus
}
#endif

#endif /*EMU_CDROM_H*/
