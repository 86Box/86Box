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

#define CDROM_NUM                   8

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
    CDROM_BUS_SCSI     = 6,
    CDROM_BUS_MITSUMI  = 7,
    CDROM_BUS_USB      = 8
};

enum
{
    CDROM_TYPE_86BOX_100,
    CDROM_TYPE_AZT_CDA46802I_115,
    CDROM_TYPE_BTC_BCD36XH_U10,
    CDROM_TYPE_GOLDSTAR_CRD_8160B_314,
    CDROM_TYPE_HITACHI_CDR_8130_0020,
    CDROM_TYPE_KENWOOD_UCR_421_208E,
    CDROM_TYPE_MATSHITA_587_7S13,
    CDROM_TYPE_MATSHITA_588_LS15,
    CDROM_TYPE_MATSHITA_571_10e,
    CDROM_TYPE_MATSHITA_572_10j,
    CDROM_TYPE_MITSUMI_FX4820T_D02A,
    CDROM_TYPE_NEC_260_100,
    CDROM_TYPE_NEC_260_101,
    CDROM_TYPE_NEC_273_420,
    CDROM_TYPE_NEC_280_105,
    CDROM_TYPE_NEC_280_308,
    CDROM_TYPE_PHILIPS_PCA403CD_U31P,
    CDROM_TYPE_SONY_CDU76_10i,
    CDROM_TYPE_SONY_CDU311_30h,
    CDROM_TYPE_TOSHIBA_5302TA_0305,
    CDROM_TYPE_TOSHIBA_5702B_TA70,
    CDROM_TYPE_CHINON_CDS431_H42,
    CDROM_TYPE_DEC_RRD45_0436,
    CDROM_TYPE_MATSHITA_501_10b,
    CDROM_TYPE_NEC_25_10a,
    CDROM_TYPE_NEC_38_103,
    CDROM_TYPE_NEC_75_103,
    CDROM_TYPE_NEC_77_106,
    CDROM_TYPE_NEC_211_100,
    CDROM_TYPE_NEC_464_105,
    CDROM_TYPE_SONY_CDU541_10i,
    CDROM_TYPE_SONY_CDU561_18k,
    CDROM_TYPE_SONY_CDU76S_100,
    CDROM_TYPE_PHILIPS_CDD2600_107,
    CDROM_TYPE_PIONEER_DRM604X_2403,
    CDROM_TYPE_PLEXTOR_PX32TS_103,
    CDROM_TYPE_TEAC_CD50_100,
    CDROM_TYPE_TEAC_R55S_10R,
    CDROM_TYPE_TEXEL_DMXX24_100,
    CDROM_TYPE_TOSHIBA_XM_3433,
    CDROM_TYPE_TOSHIBA_XM3201B_3232,
    CDROM_TYPE_TOSHIBA_XM3301TA_0272,
    CDROM_TYPE_TOSHIBA_XM5701TA_3136,
    CDROM_TYPE_TOSHIBA_SDM1401_1008,
    CDROM_TYPES_NUM
};

#define KNOWN_CDROM_DRIVE_TYPES     CDROM_TYPES_NUM
#define BUS_TYPE_IDE                CDROM_BUS_ATAPI
#define BUS_TYPE_SCSI               CDROM_BUS_SCSI
#define BUS_TYPE_BOTH              -2
#define BUS_TYPE_NONE              -1

static const struct
{
    const char  vendor[9];
    const char  model[17];
    const char  revision[5];
    const char *name;
    const char *internal_name;
    const int   bus_type;
} cdrom_drive_types[] = {
    { "86BOX",    "CD-ROM",             "1.00", "86BOX CD-ROM 1.00",             "86BOX_CD-ROM_1.00",             BUS_TYPE_BOTH  },
    { "AZT",      "CDA46802I",          "1.15", "AZT CDA46802I 1.15",            "AZT_CDA46802I_1.15",            BUS_TYPE_IDE  },
    { "BTC",      "CD-ROM BCD36XH",     "U1.0", "BTC CD-ROM BCD36XH U1.0",       "BTC_CD-ROM_BCD36XH_U1.0",       BUS_TYPE_IDE  },
    { "GOLDSTAR", "CRD-8160B",          "3.14", "GOLDSTAR CRD-8160B 3.14",       "GOLDSTAR_CRD-8160B_3.14",       BUS_TYPE_IDE  },
    { "HITACHI",  "CDR-8130",           "0020", "HITACHI CDR-8130 0020",         "HITACHI_CDR-8130_0020",         BUS_TYPE_IDE  },
    { "KENWOOD",  "CD-ROM UCR-421",     "208E", "KENWOOD CD-ROM UCR-421 208E",   "KENWOOD_CD-ROM_UCR-421_208E",   BUS_TYPE_IDE  },
    { "MATSHITA", "CD-ROM CR-587",      "7S13", "MATSHITA CD-ROM CR-587 7S13",   "MATSHITA_CD-ROM_CR-587_7S13",   BUS_TYPE_IDE  },
    { "MATSHITA", "CD-ROM CR-588",      "LS15", "MATSHITA CD-ROM CR-588 LS15",   "MATSHITA_CD-ROM_CR-588_LS15",   BUS_TYPE_IDE  },
    { "MATSHITA", "CR-571",             "1.0e", "MATSHITA CR-571 1.0e",          "MATSHITA_CR-571_1.0e",          BUS_TYPE_IDE  },
    { "MATSHITA", "CR-572",             "1.0j", "MATSHITA CR-572 1.0j",          "MATSHITA_CR-572_1.0j",          BUS_TYPE_IDE  },
    { "MITSUMI",  "CRMC-FX4820T",       "D02A", "MITSUMI CRMC-FX4820T D02A",     "MITSUMI_CRMC-FX4820T_D02A",     BUS_TYPE_IDE  },
    { "NEC",      "CD-ROM DRIVE:260",   "1.00", "NEC CD-ROM DRIVE:260 1.00",     "NEC_CD-ROM_DRIVE260_1.00",      BUS_TYPE_IDE  },
    { "NEC",      "CD-ROM DRIVE:260",   "1.01", "NEC CD-ROM DRIVE:260 1.01",     "NEC_CD-ROM_DRIVE260_1.01",      BUS_TYPE_IDE  },
    { "NEC",      "CD-ROM DRIVE:273",   "4.20", "NEC CD-ROM DRIVE:273 4.20",     "NEC_CD-ROM_DRIVE273_4.20",      BUS_TYPE_IDE  },
    { "NEC",      "CD-ROM DRIVE:280",   "1.05", "NEC CD-ROM DRIVE:280 1.05",     "NEC_CD-ROM_DRIVE280_1.05",      BUS_TYPE_IDE  },
    { "NEC",      "CD-ROM DRIVE:280",   "3.08", "NEC CD-ROM DRIVE:280 3.08",     "NEC_CD-ROM_DRIVE280_3.08",      BUS_TYPE_IDE  },
    { "PHILIPS",  "CD-ROM PCA403CD",    "U31P", "PHILIPS CD-ROM PCA403CD U31P",  "PHILIPS_CD-ROM_PCA403CD_U31P",  BUS_TYPE_IDE  },
    { "SONY",     "CD-ROM CDU76",       "1.0i", "SONY CD-ROM CDU76 1.0i",        "SONY_CD-ROM_CDU76_1.0i",        BUS_TYPE_IDE  },
    { "SONY",     "CD-ROM CDU311",      "3.0h", "SONY CD-ROM CDU311 3.0h",       "SONY_CD-ROM_CDU311_3.0h",       BUS_TYPE_IDE  },
    { "TOSHIBA",  "CD-ROM XM-5302TA",   "0305", "TOSHIBA CD-ROM XM-5302TA 0305", "TOSHIBA_CD-ROM_XM-5302TA_0305", BUS_TYPE_IDE  },
    { "TOSHIBA",  "CD-ROM XM-5702B",    "TA70", "TOSHIBA CD-ROM XM-5702B TA70",  "TOSHIBA_CD-ROM_XM-5702B_TA70",  BUS_TYPE_IDE  },
    { "CHINON",   "CD-ROM CDS-431",     "H42 ", "CHINON CD-ROM CDS-431 H42",     "CHINON_CD-ROM_CDS-431_H42",     BUS_TYPE_SCSI },
    { "DEC",      "RRD45   (C) DEC",    "0436", "DEC RRD45 0436",                "DEC_RRD45_0436",                BUS_TYPE_SCSI },
    { "MATSHITA", "CD-ROM CR-501",      "1.0b", "MATSHITA CD-ROM CR-501 1.0b",   "MATSHITA_CD-ROM_CR-501_1.0b",   BUS_TYPE_SCSI },
    { "NEC",      "CD-ROM DRIVE:25",    "1.0a", "NEC CD-ROM DRIVE:25 1.0a",      "NEC_CD-ROM_DRIVE25_1.0a",       BUS_TYPE_SCSI },
    { "NEC",      "CD-ROM DRIVE:38",    "1.00", "NEC CD-ROM DRIVE:38 1.00",      "NEC_CD-ROM_DRIVE38_1.00",       BUS_TYPE_SCSI },
    { "NEC",      "CD-ROM DRIVE:75",    "1.03", "NEC CD-ROM DRIVE:75 1.03",      "NEC_CD-ROM_DRIVE75_1.03",       BUS_TYPE_SCSI },
    { "NEC",      "CD-ROM DRIVE:77",    "1.06", "NEC CD-ROM DRIVE:77 1.06",      "NEC_CD-ROM_DRIVE77_1.06",       BUS_TYPE_SCSI },
    { "NEC",      "CD-ROM DRIVE:211",   "1.00", "NEC CD-ROM DRIVE:211 1.00",     "NEC_CD-ROM_DRIVE211_1.00",      BUS_TYPE_SCSI },
    { "NEC",      "CD-ROM DRIVE:464",   "1.05", "NEC CD-ROM DRIVE:464 1.05",     "NEC_CD-ROM_DRIVE464_1.05",       BUS_TYPE_SCSI },
    { "SONY",     "CD-ROM CDU-541",     "1.0i", "SONY CD-ROM CDU-541 1.0i",      "SONY_CD-ROM_CDU-541_1.0i",      BUS_TYPE_SCSI },
    { "SONY",     "CD-ROM CDU-561",     "1.8k", "SONY CD-ROM CDU-561 1.8k",      "SONY_CD-ROM_CDU-561_1.8k",      BUS_TYPE_SCSI },
    { "SONY",     "CD-ROM CDU-76S",     "1.00", "SONY CD-ROM CDU-76S 1.00",      "SONY_CD-ROM_CDU-76S_1.00",      BUS_TYPE_SCSI },
    { "PHILIPS",  "CDD2600",            "1.07", "PHILIPS CDD2600 1.07",          "PHILIPS_CDD2600_1.07",          BUS_TYPE_SCSI },
    { "PIONEER",  "CD-ROM DRM-604X",    "2403", "PIONEER CD-ROM DRM-604X 2403",  "PIONEER_CD-ROM_DRM-604X_2403",  BUS_TYPE_SCSI },
    { "PLEXTOR",  "CD-ROM PX-32TS",     "1.03", "PLEXTOR CD-ROM PX-32TS 1.03",   "PLEXTOR_CD-ROM_PX-32TS_1.03",   BUS_TYPE_SCSI },
    { "TEAC",     "CD 50",              "1.00", "TEAC CD 50 1.00",               "TEAC_CD_50_1.00",               BUS_TYPE_SCSI },
    { "TEAC",     "CD-ROM R55S",        "1.0R", "TEAC CD-ROM R55S 1.0R",         "TEAC_CD-ROM_R55S_1.0R",         BUS_TYPE_SCSI },
    { "TEXEL",    "CD-ROM DM-XX24",     "1.00", "TEXEL CD-ROM DM-XX24 1.00",     "TEXEL_CD-ROM_DM-XX24_1.00",     BUS_TYPE_SCSI },
    { "TOSHIBA",  "CD-ROM DRIVE:XM",    "3433", "TOSHIBA CD-ROM DRIVE:XM 3433",  "TOSHIBA_CD-ROM_DRIVEXM_3433",   BUS_TYPE_SCSI },
    { "TOSHIBA",  "CD-ROM XM-3201B",    "3232", "TOSHIBA CD-ROM XM-3201B 3232",  "TOSHIBA_CD-ROM_XM-3201B_3232",  BUS_TYPE_SCSI },
    { "TOSHIBA",  "CD-ROM XM-3301TA",   "0272", "TOSHIBA CD-ROM XM-3301TA 0272", "TOSHIBA_CD-ROM_XM-3301TA_0272", BUS_TYPE_SCSI },
    { "TOSHIBA",  "CD-ROM XM-5701TA",   "3136", "TOSHIBA CD-ROM XM-5701TA 3136", "TOSHIBA_CD-ROM_XM-5701TA_3136", BUS_TYPE_SCSI },
    { "TOSHIBA",  "DVD-ROM SD-M1401",   "1008", "TOSHIBA DVD-ROM SD-M1401 1008", "TOSHIBA_DVD-ROM_SD-M1401_1008", BUS_TYPE_SCSI },
    { "",         "",                   "",     "",                              "",                              BUS_TYPE_NONE },
};

/* To shut up the GCC compilers. */
struct cdrom;

typedef struct subchannel_t {
    uint8_t attr;
    uint8_t track;
    uint8_t index;
    uint8_t abs_m;
    uint8_t abs_s;
    uint8_t abs_f;
    uint8_t rel_m;
    uint8_t rel_s;
    uint8_t rel_f;
} subchannel_t;

typedef struct track_info_t {
    int     number;
    uint8_t attr;
    uint8_t m;
    uint8_t s;
    uint8_t f;
} track_info_t;

/* Define the various CD-ROM drive operations (ops). */
typedef struct cdrom_ops_t {
    void (*get_tracks)(struct cdrom *dev, int *first, int *last);
    void (*get_track_info)(struct cdrom *dev, uint32_t track, int end, track_info_t *ti);
    void (*get_subchannel)(struct cdrom *dev, uint32_t lba, subchannel_t *subc);
    int  (*is_track_pre)(struct cdrom *dev, uint32_t lba);
    int  (*sector_size)(struct cdrom *dev, uint32_t lba);
    int  (*read_sector)(struct cdrom *dev, int type, uint8_t *b, uint32_t lba);
    int  (*track_type)(struct cdrom *dev, uint32_t lba);
    int  (*ext_medium_changed)(struct cdrom *dev);
    void (*exit)(struct cdrom *dev);
} cdrom_ops_t;

typedef struct cdrom {
    uint8_t id;

    union {
        uint8_t res;
        uint8_t res0; /* Reserved for other ID's. */
        uint8_t res1;
        uint8_t ide_channel;
        uint8_t scsi_device_id;
    };

    uint8_t bus_type;  /* 0 = ATAPI, 1 = SCSI */
    uint8_t bus_mode;  /* Bit 0 = PIO suported;
                          Bit 1 = DMA supportd. */
    uint8_t cd_status; /* Struct variable reserved for
                          media status. */
    uint8_t speed;
    uint8_t cur_speed;

    void *priv;

    char image_path[1024];
    char prev_image_path[1024];

    char *image_history[CD_IMAGE_HISTORY];

    uint32_t sound_on;
    uint32_t cdrom_capacity;
    uint32_t seek_pos;
    uint32_t seek_diff;
    uint32_t cd_end;
    uint32_t type;

    int cd_buflen;
    int audio_op;
    int audio_muted_soft;
    int sony_msf;

    const cdrom_ops_t *ops;

    void *image;

    void (*insert)(void *priv);
    void (*close)(void *priv);
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
extern uint8_t cdrom_audio_track_search_pioneer(cdrom_t *dev, uint32_t pos, uint8_t playbit);
extern uint8_t cdrom_audio_play_pioneer(cdrom_t *dev, uint32_t pos);
extern uint8_t cdrom_audio_play_toshiba(cdrom_t *dev, uint32_t pos, int type);
extern void    cdrom_audio_pause_resume(cdrom_t *dev, uint8_t resume);
extern uint8_t cdrom_audio_scan(cdrom_t *dev, uint32_t pos, int type);
extern uint8_t cdrom_get_audio_status_pioneer(cdrom_t *dev, uint8_t *b);
extern uint8_t cdrom_get_audio_status_sony(cdrom_t *dev, uint8_t *b, int msf);
extern uint8_t cdrom_get_current_subchannel(cdrom_t *dev, uint8_t *b, int msf);
extern void    cdrom_get_current_subchannel_sony(cdrom_t *dev, uint8_t *b, int msf);
extern void    cdrom_get_current_subcodeq(cdrom_t *dev, uint8_t *b);
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

extern int  cdrom_ioctl_open(cdrom_t *dev, const char *drv);
extern void cdrom_ioctl_close(cdrom_t *dev);

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
