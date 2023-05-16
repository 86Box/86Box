/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Generic CD-ROM drive core header.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2019 Miran Grca.
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
    CDROM_BUS_MITSUMI,
    CDROM_BUS_USB
};

#define KNOWN_CDROM_DRIVE_TYPES 35
#define BUS_TYPE_ALL 0
#define BUS_TYPE_IDE 1
#define BUS_TYPE_SCSI 2

static const struct
{
    const char vendor[9];
    const char model[17];
    const char revision[5];
    const char *name;
    const char *internal_name;
    const int  bus_type;
} cdrom_drive_types[] =
{
    { "86BOX",    "CD-ROM",             "1.00", "(ATAPI/SCSI) 86BOX CD-ROM 1.00", "86BOX_CD-ROM_1.00", BUS_TYPE_ALL}, /*1*/
    { "AZT",      "CDA46802I",          "1.15", "(ATAPI) AZT CDA46802I 1.15", "AZT_CDA46802I_1.15", BUS_TYPE_IDE}, /*2*/
    { "BTC",      "CD-ROM BCD36XH",     "U1.0", "(ATAPI) BTC CD-ROM BCD36XH U1.0", "BTC_CD-ROM_BCD36XH_U1.0", BUS_TYPE_IDE}, /*3*/
    { "GOLDSTAR", "CRD-8160B",          "3.14", "(ATAPI) GOLDSTAR CRD-8160B 3.14", "GOLDSTAR_CRD-8160B_3.14", BUS_TYPE_IDE}, /*4*/
    { "HITACHI",  "CDR-8130",           "0020", "(ATAPI) HITACHI CDR-8130 0020", "HITACHI_CDR-8130_0020", BUS_TYPE_IDE}, /*5*/
    { "KENWOOD",  "CD-ROM UCR-421",     "208E", "(ATAPI) KENWOOD CD-ROM UCR-421 208E", "KENWOOD_CD-ROM_UCR-421_208E", BUS_TYPE_IDE}, /*6*/
    { "MATSHITA", "CD-ROM CR-587",      "7S13", "(ATAPI) MATSHITA CD-ROM CR-587 7S13", "MATSHITA_CD-ROM_CR-587_7S13", BUS_TYPE_IDE}, /*7*/
    { "MATSHITA", "CD-ROM CR-588",      "LS15", "(ATAPI) MATSHITA CD-ROM CR-588 LS15", "MATSHITA_CD-ROM_CR-588_LS15", BUS_TYPE_IDE}, /*8*/
    { "MATSHITA", "CR-571",             "1.0e", "(ATAPI) MATSHITA CR-571 1.0e", "MATSHITA_CR-571_1.0e", BUS_TYPE_IDE}, /*9*/
    { "MATSHITA", "CR-572",             "1.0j", "(ATAPI) MATSHITA CR-572 1.0j", "MATSHITA_CR-572_1.0j", BUS_TYPE_IDE}, /*10*/
    { "MITSUMI",  "CRMC-FX4820T",       "D02A", "(ATAPI) MITSUMI CRMC-FX4820T D02A", "MITSUMI_CRMC-FX4820T_D02A", BUS_TYPE_IDE}, /*11*/
    { "NEC",      "CD-ROM DRIVE:260",   "1.00", "(ATAPI) NEC CD-ROM DRIVE:260 1.00", "NEC_CD-ROM_DRIVE260_1.00", BUS_TYPE_IDE}, /*12*/
    { "NEC",      "CD-ROM DRIVE:260",   "1.01", "(ATAPI) NEC CD-ROM DRIVE:260 1.01", "NEC_CD-ROM_DRIVE260_1.01", BUS_TYPE_IDE}, /*13*/
    { "NEC",      "CD-ROM DRIVE:273",   "4.20", "(ATAPI) NEC CD-ROM DRIVE:273 4.20", "NEC_CD-ROM_DRIVE273_4.20", BUS_TYPE_IDE}, /*14*/
    { "NEC",      "CD-ROM DRIVE:280",   "1.05", "(ATAPI) NEC CD-ROM DRIVE:280 1.05", "NEC_CD-ROM_DRIVE280_1.05", BUS_TYPE_IDE}, /*15*/
    { "NEC",      "CD-ROM DRIVE:280",   "3.08", "(ATAPI) NEC CD-ROM DRIVE:280 3.08", "NEC_CD-ROM_DRIVE280_3.08", BUS_TYPE_IDE}, /*16*/
    { "PHILIPS",  "CD-ROM PCA403CD",    "U31P", "(ATAPI) PHILIPS CD-ROM PCA403CD U31P", "PHILIPS_CD-ROM_PCA403CD_U31P", BUS_TYPE_IDE}, /*17*/
    { "SONY",     "CD-ROM CDU76",       "1.0i", "(ATAPI) SONY CD-ROM CDU76 1.0i", "SONY_CD-ROM_CDU76_1.0i", BUS_TYPE_IDE}, /*18*/
    { "SONY",     "CD-ROM CDU311",      "3.0h", "(ATAPI) SONY CD-ROM CDU311 3.0h", "SONY_CD-ROM_CDU311_3.0h", BUS_TYPE_IDE}, /*19*/
    { "TOSHIBA",  "CD-ROM XM-5302TA",   "0305", "(ATAPI) TOSHIBA CD-ROM XM-5302TA 0305", "TOSHIBA_CD-ROM_XM-5302TA_0305", BUS_TYPE_IDE}, /*20*/
    { "TOSHIBA",  "CD-ROM XM-5702B",    "TA70", "(ATAPI) TOSHIBA CD-ROM XM-5702B TA70", "TOSHIBA_CD-ROM_XM-5702B_TA70", BUS_TYPE_IDE}, /*21*/
    { "CHINON",   "CD-ROM CDS-431",     "H42 ", "(SCSI) CHINON CD-ROM CDS-431 H42", "CHINON_CD-ROM_CDS-431_H42", BUS_TYPE_SCSI}, /*22*/
    { "DEC",      "RRD45   (C) DEC",    "0436", "(SCSI) DEC RRD45 0436", "DEC_RRD45_0436", BUS_TYPE_SCSI}, /*23*/
    { "MATSHITA", "CD-ROM CR-501",      "1.0b", "(SCSI) MATSHITA CD-ROM CR-501 1.0b", "MATSHITA_CD-ROM_CR-501_1.0b", BUS_TYPE_SCSI}, /*24*/
    { "NEC",      "CD-ROM DRIVE:74",    "1.00", "(SCSI) NEC CD-ROM DRIVE:74 1.00", "NEC_CD-ROM_DRIVE74_1.00", BUS_TYPE_SCSI}, /*25*/
    { "NEC",      "CD-ROM DRIVE:464",   "1.05", "(SCSI) NEC CD-ROM DRIVE:464 1.05", "NEC_CD-ROM_DRIVE464_1.05", BUS_TYPE_SCSI}, /*26*/
    { "SONY",     "CD-ROM CDU-541",     "1.0i", "(SCSI) SONY CD-ROM CDU-541 1.0i", "SONY_CD-ROM_CDU-541_1.0i", BUS_TYPE_SCSI}, /*27*/
    { "SONY",     "CD-ROM CDU-76S",     "1.00", "(SCSI) SONY CD-ROM CDU-76S 1.00", "SONY_CD-ROM_CDU-76S_1.00", BUS_TYPE_SCSI}, /*28*/
    { "PHILIPS",  "CDD2600",            "1.07", "(SCSI) PHILIPS CDD2600 1.07", "PHILIPS_CDD2600_1.07", BUS_TYPE_SCSI}, /*29*/
    { "PIONEER",  "CD-ROM DRM-604X",    "2403", "(SCSI) PIONEER CD-ROM DRM-604X 2403", "PIONEER_CD-ROM_DRM-604X_2403", BUS_TYPE_SCSI}, /*30*/
    { "PLEXTOR",  "CD-ROM PX-32TS",     "1.03", "(SCSI) PLEXTOR CD-ROM PX-32TS 1.03", "PLEXTOR_CD-ROM_PX-32TS_1.03", BUS_TYPE_SCSI}, /*31*/
    { "TEAC",     "CD-R55S",            "1.0R", "(SCSI) TEAC CD-R55S 1.0R", "TEAC_CD-R55S_1.0R", BUS_TYPE_SCSI}, /*32*/
    { "TOSHIBA",  "CD-ROM DRIVE:XM",    "3433", "(SCSI) TOSHIBA CD-ROM DRIVE:XM 3433", "TOSHIBA_CD-ROM_DRIVEXM_3433", BUS_TYPE_SCSI}, /*33*/
    { "TOSHIBA",  "CD-ROM XM-3301TA",   "0272", "(SCSI) TOSHIBA CD-ROM XM-3301TA 0272", "TOSHIBA_CD-ROM_XM-3301TA_0272", BUS_TYPE_SCSI}, /*34*/
    { "TOSHIBA",  "CD-ROM XM-5701TA",   "3136", "(SCSI) TOSHIBA CD-ROM XM-5701TA 3136", "TOSHIBA_CD-ROM_XM-5701TA_3136", BUS_TYPE_SCSI}, /*35*/
    { "",         "",                   "",     "",                             "", -1},
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
        seek_pos,
        seek_diff, cd_end, type;

    int host_drive, prev_host_drive,
        cd_buflen, audio_op;

    const cdrom_ops_t *ops;

    void *image;

    void (*insert)(void *p);
    void (*close)(void *p);
    uint32_t (*get_volume)(void *p, int channel);
    uint32_t (*get_channel)(void *p, int channel);

    int16_t cd_buffer[BUF_SIZE];
} cdrom_t;

extern cdrom_t cdrom[CDROM_NUM];

extern char   *cdrom_getname(int type);

extern char   *cdrom_get_internal_name(int type);
extern int     cdrom_get_from_internal_name(char *s);
extern void    cdrom_set_type(int model, int type);
extern int     cdrom_get_type(int model);

extern int     cdrom_lba_to_msf_accurate(int lba);
extern double  cdrom_seek_time(cdrom_t *dev);
extern void    cdrom_stop(cdrom_t *dev);
extern int     cdrom_is_pre(cdrom_t *dev, uint32_t lba);
extern int     cdrom_audio_callback(cdrom_t *dev, int16_t *output, int len);
extern uint8_t cdrom_audio_play(cdrom_t *dev, uint32_t pos, uint32_t len, int ismsf);
extern uint8_t cdrom_audio_track_search(cdrom_t *dev, uint32_t pos, int type, uint8_t playbit);
extern uint8_t cdrom_audio_play_toshiba(cdrom_t *dev, uint32_t pos, int type);
extern void    cdrom_audio_pause_resume(cdrom_t *dev, uint8_t resume);
extern uint8_t cdrom_audio_scan(cdrom_t *dev, uint32_t pos, int type);
extern uint8_t cdrom_get_audio_status_sony(cdrom_t *dev, uint8_t *b, int msf);
extern uint8_t cdrom_get_current_subchannel(cdrom_t *dev, uint8_t *b, int msf);
extern void    cdrom_get_current_subchannel_sony(cdrom_t *dev, uint8_t *b, int msf);
extern uint8_t cdrom_get_current_subcodeq_playstatus(cdrom_t *dev, uint8_t *b);
extern int     cdrom_read_toc(cdrom_t *dev, unsigned char *b, int type,
                              unsigned char start_track, int msf, int max_len);
extern int     cdrom_read_toc_sony(cdrom_t *dev, unsigned char *b, unsigned char start_track, int msf, int max_len);
extern void    cdrom_get_track_buffer(cdrom_t *dev, uint8_t *buf);
extern void    cdrom_get_q(cdrom_t *dev, uint8_t *buf, int *curtoctrk, uint8_t mode);
extern uint8_t cdrom_mitsumi_audio_play(cdrom_t *dev, uint32_t pos, uint32_t len);
extern int     cdrom_readsector_raw(cdrom_t *dev, uint8_t *buffer, int sector, int ismsf,
                                    int cdrom_sector_type, int cdrom_sector_flags, int *len, uint8_t vendor_type);
extern uint8_t cdrom_read_disc_info_toc(cdrom_t *dev, unsigned char *b, unsigned char track, int type);

extern void cdrom_seek(cdrom_t *dev, uint32_t pos, uint8_t vendor_type);

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
