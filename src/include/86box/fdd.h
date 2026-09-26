/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the floppy drive emulation.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          Toni Riikonen, <riikonen.toni@gmail.com>
 *
 *          Copyright 2008-2025 Sarah Walker.
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2018-2025 Fred N. van Kempen.
 *          Copyright 2025 Toni Riikonen.
 */
#ifndef EMU_FDD_H
#define EMU_FDD_H

#define FDD_NUM              8
#define FLOPPY_IMAGE_HISTORY 10
#define SEEK_RECALIBRATE     (-999)
#define DEFAULT_SEEK_TIME_MS 10.0

/* BIOS boot status - used to detect POST vs normal operation */
typedef enum {
    BIOS_BOOT_POST = 0,     /* System is in POST (Power-On Self Test) */
    BIOS_BOOT_NORMAL = 1    /* POST complete, normal operation */
} bios_boot_status_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int fdd_swap;

extern void fdd_set_motor_enable(void *priv, int motor_enable);
extern void fdd_do_seek(void *priv, int track);
extern void fdd_forced_seek(void *priv, int track_diff);
extern void fdd_seek(void *priv, int track_diff);
extern int  fdd_track0(void *priv);
extern int  fdd_index(void *priv);
extern int  fdd_get_type_max_track(int type);
extern int  fdd_getrpm(void *priv);
extern void fdd_set_densel(int bus, int densel);
extern int  fdd_can_read_medium(void *priv);
extern int  fdd_doublestep_40(void *priv);
extern int  fdd_is_pcjx_360(void *priv);
extern int  fdd_is_525(void *priv);
extern int  fdd_supports_360_rpm(void *priv);
extern int  fdd_is_dd(void *priv);
extern int  fdd_is_hd(void *priv);
extern int  fdd_is_ed(void *priv);
extern int  fdd_is_double_sided(void *priv);
extern void fdd_set_head(void *priv, int head);
extern int  fdd_get_head(void *priv);
extern void fdd_set_turbo(void *priv, int turbo);
extern int  fdd_get_turbo(void *priv);
extern void fdd_set_check_bpb(void *priv, int check_bpb);
extern int  fdd_get_check_bpb(void *priv);

extern void fdd_set_type(void *priv, int type);
extern int  fdd_get_type(void *priv);

/* New audio profile accessors */
extern void fdd_set_audio_profile(void *priv, int profile);
extern int  fdd_get_audio_profile(void *priv);

extern int fdd_get_flags(void *priv);
extern int fdd_get_densel(void *priv);

extern char *fdd_getname(int type);

extern char *fdd_get_internal_name(int type);
extern int   fdd_get_from_internal_name(char *s);

extern int fdd_current_track(void *priv);

typedef uint8_t d86f_format_id_t[4];

typedef struct d86f_handler_t {
    uint16_t (*disk_flags)(void *priv);
    uint16_t (*side_flags)(void *priv);
    void (*writeback)(void *priv);
    void (*set_sector)(void *priv, int side, uint8_t c, uint8_t h,
                       uint8_t r, uint8_t n);
    int (*format_track)(void *priv, int side,
                        const d86f_format_id_t *ids, uint16_t count,
                        uint8_t fill);
    uint8_t (*read_data)(void *priv, int side, uint16_t pos);
    void (*write_data)(void *priv, int side, uint16_t pos,
                       uint8_t data);
    int (*format_conditions)(void *priv);
    int32_t (*extra_bit_cells)(void *priv, int side);
    uint16_t *(*encoded_data)(void *priv, int side);
    void (*read_revolution)(void *priv);
    uint32_t (*index_hole_pos)(void *priv, int side);
    uint32_t (*get_raw_size)(void *priv, int side);

    uint8_t check_crc;
} d86f_handler_t;

typedef struct fdd_pending_op_t {
    int     pending;
    int     op;
    int     sector;
    int     track;
    int     side;
    int     density;
    int     sector_size;
    uint8_t fill;
} fdd_pending_op_t;

#ifndef DISABLE_FDD_AUDIO
/* Motor sound states */
typedef enum {
    MOTOR_STATE_STOPPED = 0,
    MOTOR_STATE_STARTING,
    MOTOR_STATE_RUNNING,
    MOTOR_STATE_STOPPING
} motor_state_t;

/* Maximum number of simultaneous seek sounds per drive */
#define MAX_CONCURRENT_SEEKS 8

/* Multi-track seek audio state */
typedef struct {
    int position;
    int active;
    int duration_samples;
    int from_track;
    int to_track;
    int track_diff;
#ifdef EMU_FDD_AUDIO_H
    audio_sample_t *sample_to_play;
#else
    void *sample_to_play;
#endif
} multi_seek_state_t;
#endif

typedef struct fdd_drive_t {
    uint8_t            id;

    char               image_path[MAX_IMAGE_PATH_LEN];
    char *             image_history[FLOPPY_IMAGE_HISTORY];

    pc_timer_t         poll_time;
    pc_timer_t         seek_timer;

    int                seek_in_progress;
    int                driveloader;
    int                audio_profile;
    int                writeprot;
    int                fwriteprot;
    int                read_only;
    int                changed;
    int                empty;

#ifndef DISABLE_FDD_AUDIO
    int                spindlemotor_pos;
    int                spindlemotor_fade_samples_remaining;

    float              spindlemotor_fade_volume;

    motor_state_t      spindlemotor_state;

    multi_seek_state_t seek_state[MAX_CONCURRENT_SEEKS];
#endif

    uint64_t           motoron;

    fdd_pending_op_t   pending;

    d86f_handler_t     d86f_handler;

    void *             d86f_priv;
    void *             fdc;

    void (*seek)(void *priv, int track);
    void (*readsector)(void *priv, int sector, int track, int side,
                       int density, int sector_size);
    void (*writesector)(void *priv, int sector, int track, int side,
                        int density, int sector_size);
    void (*comparesector)(void *priv, int sector, int track, int side,
                          int density, int sector_size);
    void (*readaddress)(void *priv, int side, int density);
    void (*format)(void *priv, int side, int density, uint8_t fill);
    int (*hole)(void *priv);
    uint64_t (*byteperiod)(void *priv);
    void (*stop)(void *priv);
    void (*poll)(void *priv);
} fdd_drive_t;

extern fdd_drive_t drives[FDD_NUM];

extern int curdrive;

extern int     fdd_time;
extern int64_t floppytime;

extern void fdd_load(void *priv, char *fn);
extern void fdd_new(void *priv, char *fn);
extern void fdd_close(void *priv);
extern void fdd_init(void);
extern void fdd_reset(void);
extern void fdd_readsector(void *priv, int sector, int track,
                           int side, int density, int sector_size);
extern void fdd_writesector(void *priv, int sector, int track,
                            int side, int density, int sector_size);
extern void fdd_comparesector(void *priv, int sector, int track,
                              int side, int density, int sector_size);
extern void fdd_readaddress(void *priv, int side, int density);
extern void fdd_format(void *priv, int side, int density, uint8_t fill);
extern int  fdd_hole(void *priv);
extern void fdd_stop(void *priv);
extern void fdd_do_writeback(void *priv);

/* BIOS boot status functions */
extern bios_boot_status_t fdd_get_boot_status(void);
extern void fdd_set_boot_status(bios_boot_status_t status);
extern void fdd_boot_status_reset(void);
extern int fdd_is_post_complete(void);

extern int      motorspin;

extern int swwp;
extern int disable_write;

extern int defaultwriteprot;

/*Used in the Read A Track command. Only valid for fdd_readsector(). */
#define SECTOR_FIRST (-2)
#define SECTOR_NEXT  (-1)

extern const int gap3_sizes[5][8][48];

extern const uint8_t  dmf_r[21];
extern const uint8_t  xdf_physical_sectors[2][2];
extern const uint8_t  xdf_gap3_sizes[2][2];
extern const uint16_t xdf_trackx_spos[2][8];

typedef struct xdf_id_t {
    uint8_t h;
    uint8_t r;
} xdf_id_t;

typedef union {
    uint16_t word;
    xdf_id_t id;
} xdf_sector_t;

extern const xdf_sector_t xdf_img_layout[2][2][46];
extern const xdf_sector_t xdf_disk_layout[2][2][38];

#ifdef __cplusplus
}
#endif

#endif /*EMU_FDD_H*/
