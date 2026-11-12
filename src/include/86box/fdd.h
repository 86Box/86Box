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

/* The flux view of one surface: what the head sees, one bit cell at a time.
   An 86F image implements this directly from its stored bit-cell arrays; the
   sector images (img, imd, fdi, mfm, pcjs, td0) implement it by encoding their
   sectors on the fly. Everything below the bitstream engine speaks this
   interface, so adding a medium format does not touch the controller. */
typedef struct d86f_handler_t {
    /* Medium attributes: what this surface is, and its nominal timing. */
    uint16_t (*disk_flags)(void *priv);
    uint16_t (*side_flags)(void *priv);
    void (*writeback)(void *priv);
    /* Sector and track level, for the formatter. */
    void (*set_sector)(void *priv, int side, uint8_t c, uint8_t h,
                       uint8_t r, uint8_t n);
    int (*format_track)(void *priv, int side,
                        const d86f_format_id_t *ids, uint16_t count,
                        uint8_t fill);
    int (*format_conditions)(void *priv);
    /* Flux: one bit cell at the given position on the surface. */
    uint8_t (*read_data)(void *priv, int side, uint16_t pos);
    void (*write_data)(void *priv, int side, uint16_t pos,
                       uint8_t data);
    /* Revolution geometry, in bit cells. */
    int32_t (*extra_bit_cells)(void *priv, int side);
    uint16_t *(*encoded_data)(void *priv, int side);
    void (*read_revolution)(void *priv);
    uint32_t (*index_hole_pos)(void *priv, int side);
    uint32_t (*get_raw_size)(void *priv, int side);

    uint8_t check_crc;
} d86f_handler_t;

/* Same layout as sector_id_t in fdc.h, kept here so this header stays
   standalone. */
typedef union fdd_sector_id_t {
    uint32_t dword;
    uint8_t  byte_array[4];
    struct {
        uint8_t c;
        uint8_t h;
        uint8_t r;
        uint8_t n;
    } id;
} fdd_sector_id_t;

/* The seam between the drive layer and the controller. A drive reports its
   mechanical state and the result of the decode to the controller through
   these hooks, and asks the controller for the parameters the transfer runs
   with. Two groups, following the hardware split:

     mech  - the mechanical side: drive mapping, write protect, and the two
             interrupts the drive raises.
     flux  - the head side: bit timing and encoding, the data path, and every
             event the bitstream engine reports (record found, CRC, RNF, ...).
     cmd   - controller state the bitstream engine happens to read for now.
             Temporary: these belong to the controller and will move there,
             at which point the group goes away.

   Every hook is optional; a controller that has nothing to say about one
   leaves it NULL and the caller keeps its previous behaviour. */
typedef struct fdd_ctrl_t {
    /* ---- Mechanical side ------------------------------------------ */
    /* Drive index this controller sees for `drive` (swap and gating). */
    int         (*real_drive)(void *priv, int drive);
    /* Software write-protect switch. */
    uint8_t     (*get_swwp)(void *priv);
    void        (*seek_complete_interrupt)(void *priv, int drive);
    void        (*diskchange_interrupt)(void *priv, int drive);

    /* ---- Flux side ------------------------------------------------ */
    int         (*get_bit_rate)(void *priv);
    int         (*get_bitcell_period)(void *priv);
    int         (*is_mfm)(void *priv);
    uint8_t     (*get_diswr)(void *priv);
    int         (*get_gap)(void *priv);
    int         (*get_gap2)(void *priv, int drive);

    /* Data path: one byte at a time, in both directions. */
    int         (*data)(void *priv, uint8_t data, int last);
    int         (*getdata)(void *priv, int last);
    int         (*data_available)(const void *priv);

    /* Decode results and error conditions. */
    void        (*sector_finishread)(void *priv);
    void        (*sector_finishcompare)(void *priv, int satisfying);
    void        (*track_finishread)(void *priv, int condition);
    void        (*noidam)(void *priv);
    void        (*nosector)(void *priv);
    void        (*nodataam)(void *priv);
    void        (*cannotformat)(void *priv);
    void        (*datacrcerror)(void *priv);
    void        (*headercrcerror)(void *priv);
    void        (*writeprotect)(void *priv);
    void        (*wrongcylinder)(void *priv);
    void        (*badcylinder)(void *priv);
    void        (*set_wrong_am)(void *priv);
    void        (*sectorid)(void *priv, uint8_t track, uint8_t side,
                            uint8_t sector, uint8_t size, uint8_t crc1,
                            uint8_t crc2);

    /* Read Address: the ID last found on the track. */
    fdd_sector_id_t (*get_read_track_sector)(void *priv);
    void        (*request_next_sector_id)(void *priv);
    void        (*stop_id_request)(void *priv);

    /* ---- Command context (temporary, see above) ------------------- */
    int         (*get_dtl)(void *priv);
    int         (*get_format_n)(void *priv);
    int         (*get_format_sectors)(void *priv);
    int         (*get_compare_condition)(void *priv);
    int         (*is_dma)(void *priv);
    int         (*is_verify)(void *priv);
    int         (*is_sk)(void *priv);
    int         (*is_deleted)(void *priv);
} fdd_ctrl_t;

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

    int                type;
    int                track;
    int                densel;
    int                head;
    int                turbo;
    int                check_bpb;
    int                seek_in_progress;
    int                driveloader;
    int                audio_profile;
    /*
       The image is write-protected (is either an advanced sector-based image, has
       failed to open as writable, or has a write-protect marker inside.
     */
    int                writeprot;
    /* The image was deliberately mounted as write-protected. */
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
    void *             local;

    /* The controller this drive reports to. `fdc` above is the concrete
       uPD765 instance the old code reaches for; ctrl is the seam the drive
       and the bitstream engine are meant to use, with ctrl_priv pointing at
       the controller's own state. Until the call sites are converted both
       are kept in step by the controller's init. */
    void *             fdc;
    const fdd_ctrl_t * ctrl;
    void *             ctrl_priv;

    /* The mounted medium's command entry points (seek, sector access,
       address read, format, hole sensor, byte timing). */
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
