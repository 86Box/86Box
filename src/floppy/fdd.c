/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the floppy drive emulation.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          Toni Riikonen, <riikonen.toni@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2018-2019 Fred N. van Kempen.
 *          Copyright 2025 Toni Riikonen.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/machine.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/fdd.h>
#include <86box/fdd_86f.h>
#include <86box/fdd_fdi.h>
#include <86box/fdd_imd.h>
#include <86box/fdd_img.h>
#include <86box/fdd_pcjs.h>
#include <86box/fdd_mfm.h>
#include <86box/fdd_td0.h>
#include <86box/fdc.h>
#include <86box/fdd_audio.h>

/* Flags:
   Bit  0:  300 rpm supported;
   Bit  1:  360 rpm supported;
   Bit  2:  size (0 = 3.5", 1 = 5.25");
   Bit  3:  sides (0 = 1, 1 = 2);
   Bit  4:  double density supported;
   Bit  5:  high density supported;
   Bit  6:  extended density supported;
   Bit  7:  double step for 40-track media;
   Bit  8:  invert DENSEL polarity;
   Bit  9:  ignore DENSEL;
   Bit 10:  drive is a PS/2 drive;
*/
#define FLAG_RPM_300       1
#define FLAG_RPM_360       2
#define FLAG_525           4
#define FLAG_DS            8
#define FLAG_HOLE0         16
#define FLAG_HOLE1         32
#define FLAG_HOLE2         64
#define FLAG_DOUBLE_STEP   128
#define FLAG_INVERT_DENSEL 256
#define FLAG_IGNORE_DENSEL 512
#define FLAG_PS2           1024

typedef struct fdd_t {
    int type;
    int track;
    int densel;
    int head;
    int turbo;
    int check_bpb;
} fdd_t;

fdd_t fdd[FDD_NUM];

enum {
    FDD_OP_NONE = 0,
    FDD_OP_READ,
    FDD_OP_WRITE,
    FDD_OP_COMPARE,
    FDD_OP_READADDR,
    FDD_OP_FORMAT
};

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

static fdd_pending_op_t fdd_pending[FDD_NUM];

char  floppyfns[FDD_NUM][512];
char *fdd_image_history[FDD_NUM][FLOPPY_IMAGE_HISTORY];

pc_timer_t fdd_poll_time[FDD_NUM];
pc_timer_t fdd_seek_timer[FDD_NUM];
int        fdd_seek_in_progress[FDD_NUM] = { 0, 0, 0, 0 };

static int fdd_notfound = 0;
static int driveloaders[FDD_NUM];
static int fdd_audio_profile[FDD_NUM] = { 0 };

int writeprot[FDD_NUM];
int fwriteprot[FDD_NUM];
int fdd_changed[FDD_NUM];
int ui_writeprot[FDD_NUM] = { 0, 0, 0, 0 };
int drive_empty[FDD_NUM]  = { 1, 1, 1, 1 };

DRIVE drives[FDD_NUM];

uint64_t motoron[FDD_NUM];

fdc_t *fdd_fdc;

d86f_handler_t d86f_handler[FDD_NUM];

static const struct
{
    const char *ext;
    void (*load)(int drive, char *fn);
    void (*close)(int drive);
    int size;
} loaders[] = {
    { "001",  img_load,  img_close,  -1 },
    { "002",  img_load,  img_close,  -1 },
    { "003",  img_load,  img_close,  -1 },
    { "004",  img_load,  img_close,  -1 },
    { "005",  img_load,  img_close,  -1 },
    { "006",  img_load,  img_close,  -1 },
    { "007",  img_load,  img_close,  -1 },
    { "008",  img_load,  img_close,  -1 },
    { "009",  img_load,  img_close,  -1 },
    { "010",  img_load,  img_close,  -1 },
    { "12",   img_load,  img_close,  -1 },
    { "144",  img_load,  img_close,  -1 },
    { "360",  img_load,  img_close,  -1 },
    { "720",  img_load,  img_close,  -1 },
    { "86F",  d86f_load, d86f_close, -1 },
    { "BIN",  img_load,  img_close,  -1 },
    { "CQ",   img_load,  img_close,  -1 },
    { "CQM",  img_load,  img_close,  -1 },
    { "DDI",  img_load,  img_close,  -1 },
    { "DSK",  img_load,  img_close,  -1 },
    { "FDI",  fdi_load,  fdi_close,  -1 },
    { "FDF",  img_load,  img_close,  -1 },
    { "FLP",  img_load,  img_close,  -1 },
    { "HDM",  img_load,  img_close,  -1 },
    { "IMA",  img_load,  img_close,  -1 },
    { "IMD",  imd_load,  imd_close,  -1 },
    { "IMG",  img_load,  img_close,  -1 },
    { "JSON", pcjs_load, pcjs_close, -1 },
    { "MFM",  mfm_load,  mfm_close,  -1 },
    { "TD0",  td0_load,  td0_close,  -1 },
    { "VFD",  img_load,  img_close,  -1 },
    { "XDF",  img_load,  img_close,  -1 },
    { 0,      0,         0,          0  }
};

static const struct {
    int         max_track;
    int         flags;
    const char *name;
    const char *internal_name;
} drive_types[] = {
    /* None */
    { 0,  0,                                                                                                       "None",                    "none"            },
    /* 5.25" 1DD */
    { 43, FLAG_RPM_300 | FLAG_525 | FLAG_HOLE0,                                                                    "5.25\" 180k",             "525_1dd"         },
    /* 5.25" DD */
    { 43, FLAG_RPM_300 | FLAG_525 | FLAG_DS | FLAG_HOLE0,                                                          "5.25\" 360k",             "525_2dd"         },
    /* 5.25" QD */
    { 86, FLAG_RPM_300 | FLAG_525 | FLAG_DS | FLAG_HOLE0 | FLAG_DOUBLE_STEP,                                       "5.25\" 720k",             "525_2qd"         },
    /* 5.25" HD */
    { 86, FLAG_RPM_360 | FLAG_525 | FLAG_DS | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP | FLAG_PS2,               "5.25\" 1.2M",             "525_2hd"         },
    /* 5.25" HD Dual RPM */
    { 86, FLAG_RPM_300 | FLAG_RPM_360 | FLAG_525 | FLAG_DS | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP,           "5.25\" 1.2M 300/360 RPM", "525_2hd_dualrpm" },
    /* 3.5" 1DD */
    { 86, FLAG_RPM_300 | FLAG_HOLE0 | FLAG_DOUBLE_STEP,                                                            "3.5\" 360k",              "35_1dd"          },
    /* 3.5" DD, Equivalent to TEAC FD-235F */
    { 86, FLAG_RPM_300 | FLAG_DS | FLAG_HOLE0 | FLAG_DOUBLE_STEP,                                                  "3.5\" 720k",              "35_2dd"          },
    /* 3.5" HD, Equivalent to TEAC FD-235HF */
    { 86, FLAG_RPM_300 | FLAG_DS | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP | FLAG_PS2,                          "3.5\" 1.44M",             "35_2hd"          },
    /* TODO: 3.5" DD, Equivalent to TEAC FD-235GF */
    //    { 86, FLAG_RPM_300 | FLAG_RPM_360 | FLAG_DS | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP, "3.5\" 1.25M", "35_2hd_2mode" },
    /* 3.5" HD PC-98 */
    { 86, FLAG_RPM_300 | FLAG_RPM_360 | FLAG_DS | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP | FLAG_INVERT_DENSEL, "3.5\" 1.25M PC-98",       "35_2hd_nec"      },
    /* 3.5" HD 3-Mode, Equivalent to TEAC FD-235HG */
    { 86, FLAG_RPM_300 | FLAG_RPM_360 | FLAG_DS | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP,                      "3.5\" 1.44M 300/360 RPM", "35_2hd_3mode"    },
    /* 3.5" ED, Equivalent to TEAC FD-235J */
    { 86, FLAG_RPM_300 | FLAG_DS | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_HOLE2 | FLAG_DOUBLE_STEP,                        "3.5\" 2.88M",             "35_2ed"          },
    /* 3.5" ED Dual RPM, Equivalent to TEAC FD-335J */
    { 86, FLAG_RPM_300 | FLAG_RPM_360 | FLAG_DS | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_HOLE2 | FLAG_DOUBLE_STEP,         "3.5\" 2.88M 300/360 RPM", "35_2ed_dualrpm"  },
    /* End of list */
    { -1, -1,                                                                                                      "",                        ""                }
};

#ifdef ENABLE_FDD_LOG
int fdd_do_log = ENABLE_FDD_LOG;

static void
fdd_log(const char *fmt, ...)
{
    va_list ap, ap_copy;
    char    timebuf[32];
    char    fullbuf[1056]; // 32 + 1024 bytes for timestamp + message

    if (fdd_do_log) {
        uint32_t ticks        = plat_get_ticks();
        uint32_t seconds      = ticks / 1000;
        uint32_t milliseconds = ticks % 1000;

        snprintf(timebuf, sizeof(timebuf), "[%07u.%03u] ", seconds, milliseconds);

        va_start(ap, fmt);
        va_copy(ap_copy, ap);

        strcpy(fullbuf, timebuf);
        vsnprintf(fullbuf + strlen(timebuf), sizeof(fullbuf) - strlen(timebuf), fmt, ap_copy);

        va_end(ap_copy);
        va_end(ap);

        va_start(ap, fmt);
        va_end(ap);

        char *msg = fullbuf;
        va_start(ap, fmt);
        pclog_ex("%s", (va_list) &msg);
        va_end(ap);
    }
}
#else
#    define fdd_log(fmt, ...)
#endif

void
fdd_set_audio_profile(int drive, int profile)
{
    if (drive < 0 || drive >= FDD_NUM)
        return;
    if (profile < 0 || profile >= FDD_AUDIO_PROFILE_MAX)
        profile = 0;
    fdd_audio_profile[drive] = profile;
}

int
fdd_get_audio_profile(int drive)
{
    if (drive < 0 || drive >= FDD_NUM)
        return 0;
    return fdd_audio_profile[drive];
}

char *
fdd_getname(int type)
{
    return (char *) drive_types[type].name;
}

char *
fdd_get_internal_name(int type)
{
    return (char *) drive_types[type].internal_name;
}

int
fdd_get_from_internal_name(char *s)
{
    int c = 0;

    while (strlen(drive_types[c].internal_name)) {
        if (!strcmp((char *) drive_types[c].internal_name, s))
            return c;
        c++;
    }

    return 0;
}

/* This is needed for the dump as 86F feature. */
void
fdd_do_seek(int drive, int track)
{
    if (drives[drive].seek)
        drives[drive].seek(drive, track);
}

void
fdd_forced_seek(int drive, int track_diff)
{
    fdd[drive].track += track_diff;

    if (fdd[drive].track < 0)
        fdd[drive].track = 0;

    if (fdd[drive].track > drive_types[fdd[drive].type].max_track)
        fdd[drive].track = drive_types[fdd[drive].type].max_track;

    fdd_do_seek(drive, fdd[drive].track);
}

static void
fdd_seek_complete_callback(void *priv)
{
    DRIVE *drive = (DRIVE *) priv;

    fdd_seek_in_progress[drive->id] = 0;

    fdd_log("fdd_seek_complete_callback(drive=%d) - TIMER FIRED! seek_in_progress=1\n", drive->id);
    fdd_log("Notifying FDC of seek completion\n");
    fdd_do_seek(drive->id, fdd[drive->id].track);

    int had_pending = fdd_pending[drive->id].pending;
    if (had_pending) {
        fdd_pending_op_t *po = &fdd_pending[drive->id];
        fdd_log("Starting deferred op %d after seek on drive %d (trk=%d, side=%d, sec=%d)\n",
                po->op, drive->id, po->track, po->side, po->sector);

        switch (po->op) {
            case FDD_OP_READ:
                if (drives[drive->id].readsector)
                    drives[drive->id].readsector(drive->id, po->sector, po->track, po->side, po->density, po->sector_size);
                break;
            case FDD_OP_WRITE:
                if (drives[drive->id].writesector)
                    drives[drive->id].writesector(drive->id, po->sector, po->track, po->side, po->density, po->sector_size);
                break;
            case FDD_OP_COMPARE:
                if (drives[drive->id].comparesector)
                    drives[drive->id].comparesector(drive->id, po->sector, po->track, po->side, po->density, po->sector_size);
                break;
            case FDD_OP_READADDR:
                if (drives[drive->id].readaddress)
                    drives[drive->id].readaddress(drive->id, po->side, po->density);
                break;
            case FDD_OP_FORMAT:
                if (drives[drive->id].format)
                    drives[drive->id].format(drive->id, po->side, po->density, po->fill);
                break;
            default:
                break;
        }

        po->pending = 0;
        po->op      = FDD_OP_NONE;
    }

    if (!had_pending)
        fdc_seek_complete_interrupt(fdd_fdc, drive->id);
}

void
fdd_seek(int drive, int track_diff)
{
    fdd_log("fdd_seek(drive=%d, track_diff=%d)\n", drive, track_diff);
    if (!track_diff)
        return;

    if (fdd_seek_in_progress[drive]) {
        fdd_log("Seek already in progress for drive %d, ignoring new seek request\n", drive);
        return;
    }

    int old_track = fdd[drive].track;

    fdd[drive].track += track_diff;

    if (fdd[drive].track < 0)
        fdd[drive].track = 0;

    if (fdd[drive].track > drive_types[fdd[drive].type].max_track)
        fdd[drive].track = drive_types[fdd[drive].type].max_track;

    fdd_changed[drive] = 0;

    if (fdd[drive].turbo)
        fdd_do_seek(drive, fdd[drive].track);
    else {
        /* Trigger appropriate audio for track movements */
        int actual_track_diff = abs(old_track - fdd[drive].track);
        if (actual_track_diff == 1) {
            /* Single track movement */
            fdd_audio_play_single_track_step(drive, old_track, fdd[drive].track);
        } else if (actual_track_diff > 1) {
            /* Multi-track seek */
            fdd_audio_play_multi_track_seek(drive, old_track, fdd[drive].track);
        }

        if (old_track + track_diff < 0) {
            fdd_do_seek(drive, fdd[drive].track);
            return;
        }

        fdd_seek_in_progress[drive] = 1;

        if (!fdd_seek_timer[drive].callback) {
            timer_add(&(fdd_seek_timer[drive]), fdd_seek_complete_callback, &drives[drive], 0);
        }

        /* Get seek timings from audio profile configuration */
        double   initial_seek_time = fdd_audio_get_seek_time(drive, 1, actual_track_diff);
        double   track_seek_time   = fdd_audio_get_seek_time(drive, 0, actual_track_diff);
        fdd_log("Seek timing for drive %d: initial %.2f ms, per track %.2f ms\n", drive, initial_seek_time, track_seek_time);
        uint64_t seek_time_us      = (initial_seek_time + (abs(actual_track_diff) * track_seek_time)) * TIMER_USEC;
        timer_set_delay_u64(&fdd_seek_timer[drive], seek_time_us);
    }
}

int
fdd_track0(int drive)
{
    fdd_log("fdd_track0(drive=%d)\n", drive);

    /* If drive is disabled, TRK0 never gets set. */
    if (!drive_types[fdd[drive].type].max_track)
        return 0;

    return !fdd[drive].track;
}

int
fdd_current_track(int drive)
{
    return fdd[drive].track;
}

static int
fdd_type_invert_densel(int type)
{
    int ret;

    if (drive_types[type].flags & FLAG_PS2)
        ret = (!!strstr(machine_getname(), "PS/1")) || (!!strstr(machine_getname(), "PS/2")) || (!!strstr(machine_getname(), "PS/55"));
    else
        ret = drive_types[type].flags & FLAG_INVERT_DENSEL;

    return ret;
}

static int
fdd_invert_densel(int drive)
{
    int ret = fdd_type_invert_densel(fdd[drive].type);

    return ret;
}

void
fdd_set_densel(int densel)
{
    for (uint8_t i = 0; i < FDD_NUM; i++) {
        if (fdd_invert_densel(i))
            fdd[i].densel = densel ^ 1;
        else
            fdd[i].densel = densel;
    }
}

int
fdd_getrpm(int drive)
{
    int densel = 0;
    int hole;

    hole   = fdd_hole(drive);
    densel = fdd[drive].densel;

    if (fdd_invert_densel(drive))
        densel ^= 1;

    if (!(drive_types[fdd[drive].type].flags & FLAG_RPM_360))
        return 300;
    if (!(drive_types[fdd[drive].type].flags & FLAG_RPM_300))
        return 360;

    if (drive_types[fdd[drive].type].flags & FLAG_525)
        return densel ? 360 : 300;
    else {
        /* fdd_hole(drive) returns 0 for double density media, 1 for high density, and 2 for extended density. */
        if (hole == 1)
            return densel ? 300 : 360;
        else
            return 300;
    }
}

int
fdd_can_read_medium(int drive)
{
    int hole = fdd_hole(drive);

    hole = 1 << (hole + 4);

    return !!(drive_types[fdd[drive].type].flags & hole);
}

int
fdd_doublestep_40(int drive)
{
    return !!(drive_types[fdd[drive].type].flags & FLAG_DOUBLE_STEP);
}

void
fdd_set_type(int drive, int type)
{
    if (fdd_type_invert_densel(fdd[drive].type) != fdd_type_invert_densel(type))
        fdd[drive].densel ^= 1;
    fdd[drive].type = type;
}

int
fdd_get_type(int drive)
{
    return fdd[drive].type;
}

int
fdd_get_flags(int drive)
{
    return drive_types[fdd[drive].type].flags;
}

int
fdd_is_525(int drive)
{
    return drive_types[fdd[drive].type].flags & FLAG_525;
}

int
fdd_is_dd(int drive)
{
    return (drive_types[fdd[drive].type].flags & 0x70) == 0x10;
}

int
fdd_is_hd(int drive)
{
    return drive_types[fdd[drive].type].flags & FLAG_HOLE1;
}

int
fdd_is_ed(int drive)
{
    return drive_types[fdd[drive].type].flags & FLAG_HOLE2;
}

int
fdd_is_double_sided(int drive)
{
    return drive_types[fdd[drive].type].flags & FLAG_DS;
}

void
fdd_set_head(int drive, int head)
{
    fdd_log("fdd_set_head(%d, %d)\n", drive, head);
    if (head && !fdd_is_double_sided(drive))
        fdd[drive].head = 0;
    else
        fdd[drive].head = head;
}

int
fdd_get_head(int drive)
{
    if (!fdd_is_double_sided(drive))
        return 0;
    return fdd[drive].head;
}

void
fdd_set_turbo(int drive, int turbo)
{
    fdd[drive].turbo = turbo;
}

int
fdd_get_turbo(int drive)
{
    return fdd[drive].turbo;
}

void
fdd_set_check_bpb(int drive, int check_bpb)
{
    fdd[drive].check_bpb = check_bpb;
}

int
fdd_get_check_bpb(int drive)
{
    return fdd[drive].check_bpb;
}

int
fdd_get_densel(int drive)
{
    return fdd[drive].densel;
}

void
fdd_load(int drive, char *fn)
{
    fdd_log("fdd_load(%d, %s)\n", drive, fn);
    int         c = 0;
    int         size;
    const char *p;
    FILE       *fp;
    int         offs = 0;

    if (!fn)
        return;
    if (strstr(fn, "wp://") == fn) {
        offs                = 5;
        ui_writeprot[drive] = 1;
    }
    fn += offs;
    p = path_get_extension(fn);
    if (!p)
        return;
    fp = plat_fopen(fn, "rb");
    if (fp) {
        if (fseek(fp, -1, SEEK_END) == -1)
            fatal("fdd_load(): Error seeking to the end of the file\n");
        size = ftell(fp) + 1;
        fclose(fp);
        while (loaders[c].ext) {
            if (!strcasecmp(p, (char *) loaders[c].ext) && (size == loaders[c].size || loaders[c].size == -1)) {
                driveloaders[drive] = c;
                if (floppyfns[drive] != (fn - offs))
                    strcpy(floppyfns[drive], fn - offs);
                d86f_setup(drive);
                loaders[c].load(drive, floppyfns[drive] + offs);
                drive_empty[drive] = 0;
                fdd_forced_seek(drive, 0);
                fdd_changed[drive] = 1;
                ui_sb_update_icon_wp(SB_FLOPPY | drive, ui_writeprot[drive]);
                return;
            }
            c++;
        }
    }
    drive_empty[drive] = 1;
    fdd_set_head(drive, 0);
    memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
    ui_sb_update_icon_state(SB_FLOPPY | drive, 1);
}

void
fdd_close(int drive)
{
    d86f_stop(drive); /* Call this first of all to make sure the 86F poll is back to idle state. */
    if (loaders[driveloaders[drive]].close)
        loaders[driveloaders[drive]].close(drive);
    drive_empty[drive] = 1;
    fdd_set_head(drive, 0);
    floppyfns[drive][0]         = 0;
    drives[drive].hole          = NULL;
    drives[drive].poll          = NULL;
    drives[drive].seek          = NULL;
    drives[drive].readsector    = NULL;
    drives[drive].writesector   = NULL;
    drives[drive].comparesector = NULL;
    drives[drive].readaddress   = NULL;
    drives[drive].format        = NULL;
    drives[drive].byteperiod    = NULL;
    drives[drive].stop          = NULL;
    d86f_destroy(drive);
    ui_sb_update_icon_state(SB_FLOPPY | drive, 1);
}

int
fdd_hole(int drive)
{
    if (drives[drive].hole)
        return drives[drive].hole(drive);
    else
        return 0;
}

static __inline uint64_t
fdd_byteperiod(int drive)
{
    if (drives[drive].byteperiod)
        return drives[drive].byteperiod(drive);
    else
        return 32ULL * TIMER_USEC;
}

void
fdd_set_motor_enable(int drive, int motor_enable)
{
    fdd_log("fdd_set_motor_enable(%d, %d)\n", drive, motor_enable);
    fdd_audio_set_motor_enable(drive, motor_enable);

    if (motor_enable && !motoron[drive]) {
        timer_set_delay_u64(&fdd_poll_time[drive], fdd_byteperiod(drive));
    } else if (!motor_enable && motoron[drive]) {
        timer_disable(&fdd_poll_time[drive]);
    }
    motoron[drive] = motor_enable;
}

static void
fdd_poll(void *priv)
{
    int          drive;
    const DRIVE *drv = (DRIVE *) priv;

    drive = drv->id;

    if (drive >= FDD_NUM)
        fatal("Attempting to poll floppy drive %i that is not supposed to be there\n", drive);

    timer_advance_u64(&fdd_poll_time[drive], fdd_byteperiod(drive));

    if (drv->poll)
        drv->poll(drive);

    if (fdd_notfound) {
        fdd_notfound--;
        if (!fdd_notfound)
            fdc_noidam(fdd_fdc);
    }
}

int
fdd_get_bitcell_period(int rate)
{
    int bit_rate = 250;

    switch (rate) {
        case 0: /*High density*/
            bit_rate = 500;
            break;
        case 1: /*Double density (360 rpm)*/
            bit_rate = 300;
            break;
        case 2: /*Double density*/
            bit_rate = 250;
            break;
        case 3: /*Extended density*/
            bit_rate = 1000;
            break;

        default:
            break;
    }

    return 1000000 / bit_rate * 2; /*Bitcell period in ns*/
}

void
fdd_reset(void)
{
    for (uint8_t i = 0; i < FDD_NUM; i++) {
        drives[i].id = i;
        timer_add(&(fdd_poll_time[i]), fdd_poll, &drives[i], 0);
    }
}

void
fdd_readsector(int drive, int sector, int track, int side, int density, int sector_size)
{
    fdd_log("fdd_readsector(%d, %d, %d, %d, %d, %d)\n", drive, sector, track, side, density, sector_size);

    if (fdd_seek_in_progress[drive]) {
        fdd_log("Seek in progress on drive %d, deferring READ (trk=%d->%d, side=%d, sec=%d)\n",
                drive, fdd[drive].track, track, side, sector);
        fdd_pending[drive] = (fdd_pending_op_t) {
            .pending     = 1,
            .op          = FDD_OP_READ,
            .sector      = sector,
            .track       = track,
            .side        = side,
            .density     = density,
            .sector_size = sector_size
        };
        return;
    }

    if (drives[drive].readsector)
        drives[drive].readsector(drive, sector, track, side, density, sector_size);
    else
        fdd_notfound = 1000;
}

void
fdd_writesector(int drive, int sector, int track, int side, int density, int sector_size)
{
    fdd_log("fdd_writesector(%d, %d, %d, %d, %d, %d)\n", drive, sector, track, side, density, sector_size);

    if (fdd_seek_in_progress[drive]) {
        fdd_log("Seek in progress on drive %d, deferring WRITE (trk=%d->%d, side=%d, sec=%d)\n",
                drive, fdd[drive].track, track, side, sector);
        fdd_pending[drive] = (fdd_pending_op_t) {
            .pending     = 1,
            .op          = FDD_OP_WRITE,
            .sector      = sector,
            .track       = track,
            .side        = side,
            .density     = density,
            .sector_size = sector_size
        };
        return;
    }

    if (drives[drive].writesector)
        drives[drive].writesector(drive, sector, track, side, density, sector_size);
    else
        fdd_notfound = 1000;
}

void
fdd_comparesector(int drive, int sector, int track, int side, int density, int sector_size)
{
    if (fdd_seek_in_progress[drive]) {
        fdd_log("Seek in progress on drive %d, deferring COMPARE (trk=%d->%d, side=%d, sec=%d)\n",
                drive, fdd[drive].track, track, side, sector);
        fdd_pending[drive] = (fdd_pending_op_t) {
            .pending     = 1,
            .op          = FDD_OP_COMPARE,
            .sector      = sector,
            .track       = track,
            .side        = side,
            .density     = density,
            .sector_size = sector_size
        };
        return;
    }

    if (drives[drive].comparesector)
        drives[drive].comparesector(drive, sector, track, side, density, sector_size);
    else
        fdd_notfound = 1000;
}

void
fdd_readaddress(int drive, int side, int density)
{
    if (fdd_seek_in_progress[drive]) {
        fdd_log("Seek in progress on drive %d, deferring READADDRESS (trk=%d, side=%d)\n",
                drive, fdd[drive].track, side);
        fdd_pending[drive] = (fdd_pending_op_t) {
            .pending = 1,
            .op      = FDD_OP_READADDR,
            .track   = fdd[drive].track,
            .side    = side,
            .density = density
        };
        return;
    }

    if (drives[drive].readaddress)
        drives[drive].readaddress(drive, side, density);
}

void
fdd_format(int drive, int side, int density, uint8_t fill)
{
    if (fdd_seek_in_progress[drive]) {
        fdd_log("Seek in progress on drive %d, deferring FORMAT (trk=%d, side=%d)\n",
                drive, fdd[drive].track, side);
        fdd_pending[drive] = (fdd_pending_op_t) {
            .pending = 1,
            .op      = FDD_OP_FORMAT,
            .track   = fdd[drive].track,
            .side    = side,
            .density = density,
            .fill    = fill
        };
        return;
    }

    if (drives[drive].format)
        drives[drive].format(drive, side, density, fill);
    else
        fdd_notfound = 1000;
}

void
fdd_stop(int drive)
{
    if (drives[drive].stop)
        drives[drive].stop(drive);
}

void
fdd_set_fdc(void *fdc)
{
    fdd_fdc = (fdc_t *) fdc;
}

void
fdd_init(void)
{
    int i;

    for (i = 0; i < FDD_NUM; i++) {
        drives[i].poll       = 0;
        drives[i].seek       = 0;
        drives[i].readsector = 0;
    }

    img_init();
    d86f_init();
    td0_init();
    imd_init();
    pcjs_init();

    for (i = 0; i < FDD_NUM; i++) {
        fdd_load(i, floppyfns[i]);
    }

    if (fdd_sounds_enabled) {
        fdd_audio_init();
    }
}

void
fdd_do_writeback(int drive)
{
    d86f_handler[drive].writeback(drive);
}