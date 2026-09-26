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
#include <math.h>
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
#include <86box/fdd_tape.h>
#include <86box/fdc.h>
#include <86box/fdd_audio.h>
#include <86box/plat_floppy_ioctl.h>

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

/* BIOS boot status tracking */
static bios_boot_status_t bios_boot_status = BIOS_BOOT_POST;

static int fdd_notfound = 0;

fdd_drive_t drives[FDD_NUM];

static const struct
{
    const char *ext;
    void (*load)(void *priv, char *fn);
    void (*close)(void *priv);
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
    va_list ap;
    char    timebuf[32];
    char    fullbuf[1056]; /* 32 + 1024 bytes for timestamp + message */

    if (fdd_do_log) {
        uint32_t ticks        = plat_get_ticks();
        uint32_t seconds      = ticks / 1000;
        uint32_t milliseconds = ticks % 1000;

        snprintf(timebuf, sizeof(timebuf), "[%07u.%03u] ", seconds, milliseconds);

        va_start(ap, fmt);
        strcpy(fullbuf, timebuf);
        vsnprintf(fullbuf + strlen(timebuf), sizeof(fullbuf) - strlen(timebuf), fmt, ap);
        va_end(ap);

        pclog("%s", fullbuf);
    }
}
#else
#    define fdd_log(fmt, ...)
#endif

/*
 * BIOS boot status functions
 *
 * These functions track whether the system is in BIOS POST (Power-On Self Test)
 * or has transitioned to normal operation. The POST state is set on:
 *   - System hard reset (fdd_reset)
 *   - FDC soft reset (fdd_boot_status_reset)
 *
 * POST is considered complete when the first floppy read operation occurs,
 * indicating that BIOS has finished POST and is attempting to boot.
 */
bios_boot_status_t
fdd_get_boot_status(void)
{
    return bios_boot_status;
}

void
fdd_set_boot_status(bios_boot_status_t status)
{
    if (bios_boot_status != status) {
        fdd_log("BIOS boot status changed: %s -> %s\n",
                bios_boot_status == BIOS_BOOT_POST ? "POST" : "NORMAL",
                status == BIOS_BOOT_POST ? "POST" : "NORMAL");
        bios_boot_status = status;
    }
}

void
fdd_boot_status_reset(void)
{
    fdd_log("BIOS boot status reset to POST\n");
    bios_boot_status = BIOS_BOOT_POST;
}

int
fdd_is_post_complete(void)
{
    return (bios_boot_status == BIOS_BOOT_NORMAL);
}

void
fdd_set_audio_profile(void *priv, int profile)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    if (drv == NULL)
        return;
    if ((profile < 0) || (profile >= FDD_AUDIO_PROFILE_MAX))
        profile = 0;
    drv->audio_profile = profile;
}

int
fdd_get_audio_profile(void *priv)
{
    const fdd_drive_t *drv = (fdd_drive_t *) priv;
    int                ret = 0;

    if (drv != NULL)
        ret = drv->audio_profile;

    return ret;
}

char *
fdd_getname(const int type)
{
    return (char *) drive_types[type].name;
}

char *
fdd_get_internal_name(const int type)
{
    return (char *) drive_types[type].internal_name;
}

int
fdd_get_from_internal_name(char *s)
{
    int c   = 0;
    int ret = 0;

    while (strlen(drive_types[c].internal_name)) {
        if (!strcmp((char *) drive_types[c].internal_name, s)) {
            ret = c;
            break;
        }
        c++;
    }

    return ret;
}

/* This is needed for the dump as 86F feature. */
void
fdd_do_seek(void *priv, const int track)
{
    const fdd_drive_t *drv = (fdd_drive_t *) priv;

    if ((drv != NULL) && (drv->seek != NULL))
        drv->seek(priv, track);
}

static void
fdd_do_seek_ex(const void *priv, const int track)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    if (drv == NULL)
        return;

    if (fdd_tape_present(drv))
        fdd_do_seek(drv, track);
    else {
        const int      head = fdd_get_head(drv);
        uint32_t       pos  = d86f_get_track_pos(drv);
        const uint32_t old  = d86f_get_raw_size(drv, head);

        fdd_do_seek(drv, track);

        const uint32_t new  = d86f_get_raw_size(drv, head);
        pos                 = (uint32_t) round((((double) pos) / ((double) old)) * ((double) new));
        d86f_set_track_pos(drv, pos);
    }
}

void
fdd_forced_seek(void *priv, const int track_diff)
{
    const fdd_drive_t *drv     = (fdd_drive_t *) priv;
    fdd_t *            fdd_drv = &(fdd[drv->id]);

    fdd_drv->track += track_diff;

    if (fdd_drv->track < 0)
        fdd_drv->track = 0;

    if (fdd_drv->track > drive_types[fdd_drv->type].max_track)
        fdd_drv->track = drive_types[fdd_drv->type].max_track;

    fdd_do_seek_ex(drv, fdd_drv->track);
}

static void
fdd_seek_complete_callback(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    drv->seek_in_progress = 0;

    fdd_log("fdd_seek_complete_callback(drive=%d) - TIMER FIRED! seek_in_progress=1\n", drive->id);
    fdd_log("Notifying FDC of seek completion\n");
    fdd_do_seek_ex(drv, fdd[drv->id].track);

    int had_pending = drv->pending.pending;
    if (had_pending) {
        fdd_pending_op_t *po = &drv->pending;
        fdd_log("Starting deferred op %d after seek on drive %d (trk=%d, side=%d, sec=%d)\n",
                po->op, drive->id, po->track, po->side, po->sector);

        switch (po->op) {
            case FDD_OP_READ:
                if (drv->readsector)
                    drv->readsector(drv, po->sector, po->track, po->side, po->density, po->sector_size);
                break;
            case FDD_OP_WRITE:
                if (drv->writesector)
                    drv->writesector(drv, po->sector, po->track, po->side, po->density, po->sector_size);
                break;
            case FDD_OP_COMPARE:
                if (drv->comparesector)
                    drv->comparesector(drv, po->sector, po->track, po->side, po->density, po->sector_size);
                break;
            case FDD_OP_READADDR:
                if (drv->readaddress)
                    drv->readaddress(drv, po->side, po->density);
                break;
            case FDD_OP_FORMAT:
                if (drv->format)
                    drv->format(drv, po->side, po->density, po->fill);
                break;
            default:
                break;
        }

        po->pending = 0;
        po->op      = FDD_OP_NONE;
    }

    if (!had_pending || fdd_tape_present(drv))
        fdc_seek_complete_interrupt(drv->fdc, drv->id & 3);
}

/*
   Whether an I/O request has to wait for a seek to finish first.
   Applies only if this is not a tape drive.
 */
static int
fdd_defer_op(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    return drv->seek_in_progress && !fdd_tape_present(drv);
}

void
fdd_seek(void *priv, const int track_diff)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    fdd_log("fdd_seek(drive=%d, track_diff=%d)\n", drive, track_diff);
    if (track_diff == 0)
        return;

    if (drv->seek_in_progress) {
        fdd_log("Seek already in progress for drive %d, ignoring new seek request\n", drive);
        return;
    }

    if (fdd_tape_present(drv)) {
        /*
           A floppy tape drive has no cylinders and no head to position: it
           counts the step pulses in each burst and reads them as a QIC-117
           command. So hand it the pulse count directly and leave the track
           position alone - tracking an absolute head position would clamp
           at the end of travel and leave the drive permanently deaf, since
           the host zeroes the controller's cylinder counter between
           commands while the pulses keep coming in the same direction.

           The seek must also complete promptly: the host sends one command
           per seek and waits for the interrupt each time.
         */
        const int step_time_us = fdd_tape_step(drv, abs(track_diff));

        drv->seek_in_progress = 1;

        if (!drv->seek_timer.callback)
            timer_add(&drv->seek_timer, fdd_seek_complete_callback, drv, 0);

        timer_set_delay_u64(&drv->seek_timer, (uint64_t) step_time_us * TIMER_USEC);
        return;
    }

    fdd_t *fdd_drv = &(fdd[drv->id]);
    fdc_t *fdc     = (fdc_t *) drv->fdc;

    int old_track = fdd_drv->track;
    const int ibm5140 = (fdc != NULL) && (fdc->flags & FDC_FLAG_IBM5140);
    const int degated = ibm5140 && fdc->drive_interface_gated;

    if (!degated)
        fdd_drv->track += track_diff;

    if (fdd_drv->track < 0)
        fdd_drv->track = 0;

    if (fdd_drv->track > drive_types[fdd_drv->type].max_track)
        fdd_drv->track = drive_types[fdd_drv->type].max_track;

    if (!degated && (!ibm5140 || !drv->empty))
        drv->changed = 0;

    if (fdd_drv->turbo) {
        fdd_do_seek_ex(drv, fdd_drv->track);
    } else {
        /* Trigger appropriate audio for track movements */
        int actual_track_diff = abs(old_track - fdd_drv->track);
        if (actual_track_diff > 0) {
            /* Multi-track seek */
            fdd_audio_play_multi_track_seek(drv, old_track,
                                            fdd_drv->track);
        }

        drv->seek_in_progress = 1;

        if (!drv->seek_timer.callback) {
            timer_add(&drv->seek_timer, fdd_seek_complete_callback,
                      drv, 0);
        }

        /* Determine seek direction - seeking down means moving toward track 0 */
        const int is_seek_down = (fdd_drv->track < old_track);

        /* Get seek timings from audio profile configuration with direction awareness */
        const int step_count = ibm5140 && !degated &&
                               ((fdc->command & 0x1f) == 0x07) ?
                               actual_track_diff : abs(track_diff);
        double seek_time_us = ibm5140 ? 6000.0 * step_count :
                              fdd_audio_get_seek_time(drv, actual_track_diff,
                                                      is_seek_down);
        if (seek_time_us < 1) {
            seek_time_us = DEFAULT_SEEK_TIME_MS * 1000;
        }

        fdd_log("Seek timing for drive %d: %.2f µs (%s)\n",
                drive, seek_time_us, is_seek_down ? "DOWN" : "UP");
        const uint64_t seek_delay_us = (uint64_t) (seek_time_us * (double) TIMER_USEC);
        timer_set_delay_u64(&drv->seek_timer, seek_delay_us);
    }
}

int
fdd_track0(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    if (drv == NULL)
        return 0;

    fdd_log("fdd_track0(drive=%d)\n", drv-id);

    /* On a floppy tape drive, TRK0 is the drive's result line. */
    if (fdd_tape_present(drv))
        return fdd_tape_track0(drv);

    fdd_t *fdd_drv = &(fdd[drv->id]);

    /* If drive is disabled, TRK0 never gets set. */
    if (!drive_types[fdd_drv->type].max_track)
        return 0;

    return !fdd_drv->track;
}

int
fdd_get_type_max_track(int type)
{
    if (type < 0 || type >= (sizeof(drive_types) / sizeof(drive_types[0])))
        return 0;

    return drive_types[type].max_track;
}

int
fdd_current_track(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    fdd_t *fdd_drv = &(fdd[drv->id]);

    return fdd_drv->track;
}

static int
fdd_type_invert_densel(int type)
{
    int ret;

    if (drive_types[type].flags & FLAG_PS2) {
        /* The Model 25/30 planar also belongs to the 7690, whose display
           name does not contain "PS/2". Its drive wiring is unchanged. */
        ret = (machines[machine].init == machine_ps2_8086_init) ||
              (!!strstr(machine_getname(machine), "PS/1")) ||
              (!!strstr(machine_getname(machine), "PS/2")) ||
              (!!strstr(machine_getname(machine), "PS/55"));
    } else
        ret = drive_types[type].flags & FLAG_INVERT_DENSEL;

    return ret;
}

static int
fdd_invert_densel(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    fdd_t *fdd_drv = &(fdd[drv->id]);

    int ret = fdd_type_invert_densel(fdd_drv->type);

    return ret;
}

void
fdd_set_densel(const int bus, const int densel)
{
    for (uint8_t i = 0; i < 4; i++) {
        fdd_drive_t *drv = &(drives[bus + i]);

        if (fdd_invert_densel(drv))
            fdd[i].densel = densel ^ 1;
        else
            fdd[i].densel = densel;
    }
}

int
fdd_getrpm(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    int densel = 0;
    int hole;

    fdd_t *fdd_drv = &(fdd[drv->id]);

    hole   = fdd_hole(drv);
    densel = fdd_drv->densel;

    if (fdd_invert_densel(drv))
        densel ^= 1;

    if (!(drive_types[fdd_drv->type].flags & FLAG_RPM_360))
        return 300;
    if (!(drive_types[fdd_drv->type].flags & FLAG_RPM_300))
        return 360;

    if (drive_types[fdd_drv->type].flags & FLAG_525)
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
fdd_can_read_medium(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    int hole = fdd_hole(drv);

    hole = 1 << (hole + 4);

    return !!(fdd_get_flags(drv) & hole);
}

int
fdd_doublestep_40(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    fdd_t *fdd_drv = &(fdd[drv->id]);

    return !!(drive_types[fdd_drv->type].flags & FLAG_DOUBLE_STEP);
}

int
fdd_is_pcjx_360(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    if ((drv == NULL) || !machine_is_pcjx(machine))
        return 0;

    fdd_t *fdd_drv = &(fdd[drv->id]);
    const int flags = fdd_get_flags(drv);
    return (drive_types[fdd_drv->type].max_track >= 80) &&
           !(flags & FLAG_525) && ((flags & (FLAG_DS | FLAG_HOLE0)) == (FLAG_DS | FLAG_HOLE0));
}

void
fdd_set_type(void *priv, int type)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    fdd_t *fdd_drv = &(fdd[drv->id]);

    if (fdd_type_invert_densel(fdd_drv->type) != fdd_type_invert_densel(type))
        fdd_drv->densel ^= 1;
    fdd_drv->type = type;
}

int
fdd_get_type(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    fdd_t *fdd_drv = &(fdd[drv->id]);

    return fdd_drv->type;
}

int
fdd_get_flags(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    fdd_t *fdd_drv = &(fdd[drv->id]);

    /* A floppy tape drive answers on this drive select line instead of
       whatever floppy drive may be configured on it. */
    if (fdd_tape_present(drv))
        return fdd_tape_get_flags(drv);

    return drive_types[fdd_drv->type].flags;
}

int
fdd_is_525(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    return fdd_get_flags(drv) & FLAG_525;
}

int
fdd_supports_360_rpm(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    return fdd_get_flags(drv) & FLAG_RPM_360;
}

int
fdd_is_dd(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    return (fdd_get_flags(drv) & 0x70) == 0x10;
}

int
fdd_is_hd(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    return fdd_get_flags(drv) & FLAG_HOLE1;
}

int
fdd_is_ed(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    return fdd_get_flags(drv) & FLAG_HOLE2;
}

int
fdd_is_double_sided(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    return fdd_get_flags(drv) & FLAG_DS;
}

void
fdd_set_head(void *priv, int head)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    fdd_t *fdd_drv = &(fdd[drv->id]);

    fdd_log("fdd_set_head(%d, %d)\n", drv->id, head);
    if (head && !fdd_is_double_sided(drv))
        fdd_drv->head = 0;
    else
        fdd_drv->head = head;
}

int
fdd_get_head(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    fdd_t *fdd_drv = &(fdd[drv->id]);

    if (!fdd_is_double_sided(drv))
        return 0;
    return fdd_drv->head;
}

void
fdd_set_turbo(void *priv, const int turbo)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    fdd_t *fdd_drv = &(fdd[drv->id]);

    fdd_drv->turbo = turbo;
}

int
fdd_get_turbo(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    fdd_t *fdd_drv = &(fdd[drv->id]);

    return fdd_drv->turbo;
}

void
fdd_set_check_bpb(void *priv, int check_bpb)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    fdd_t *fdd_drv = &(fdd[drv->id]);

    fdd_drv->check_bpb = check_bpb;
}

int
fdd_get_check_bpb(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    fdd_t *fdd_drv = &(fdd[drv->id]);

    return fdd_drv->check_bpb;
}

int
fdd_get_densel(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    fdd_t *fdd_drv = &(fdd[drv->id]);

    return fdd_drv->densel;
}

void
fdd_load(void *priv, char *fn)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    fdd_log("fdd_load(%d, %s)\n", drv->id, fn);
    int         offs = 0;

    if (!fn)
        return;

    /* This drive select line belongs to the tape drive, not to a floppy. */
    if (fdd_tape_present(drv))
        return;

    if (strstr(fn, "wp://") == fn) {
        offs           = 5;
        drv->read_only = 1;
    }
    fn += offs;

    /* Check for physical floppy device (ioctl://) prefix */
    if (strstr(fn, "ioctl://") == fn) {
        const char *device_path = fn + 8;

        if (drv->image_path != (fn - offs)) {
            strncpy(drv->image_path, fn - offs, sizeof(drv->image_path) - 1);
            drv->image_path[sizeof(drv->image_path) - 1] = '\0';
        }

        d86f_setup(drv);

        img_load_raw_device(drv, device_path);

        if (drv->image_path[0] == '\0') {
            drv->empty = 1;
            fdd_set_head(drv, 0);
            ui_sb_update_icon_state(SB_FLOPPY | drv->id, 1);
            return;
        }

        drv->empty = 0;

        fdd_forced_seek(drv, 0);
        drv->changed = 1;
        ui_sb_update_icon_wp(SB_FLOPPY | drv->id, drv->read_only);
        return;
    }

    const char *p = path_get_extension(fn);
    if (p == NULL)
        return;
    FILE *fp = plat_fopen(fn, "rb");
    if (fp) {
        if (fseek(fp, -1, SEEK_END) == -1)
            fatal("fdd_load(): Error seeking to the end of the file\n");
        const int size = ftell(fp) + 1;
        fclose(fp);
        int c = 0;
        while (loaders[c].ext) {
            if (!strcasecmp(p, (char *) loaders[c].ext) && (size == loaders[c].size || loaders[c].size == -1)) {
                drv->driveloader = c;
                if (drv->image_path != (fn - offs)) {
                    strncpy(drv->image_path, fn - offs,
                           sizeof(drv->image_path) - 1);
                    drv->image_path[sizeof(drv->image_path) - 1] = '\0';
                }
                d86f_setup(drv);
                loaders[c].load(drv, drv->image_path + offs);
                drv->empty = 0;
                fdd_forced_seek(drv, 0);
                drv->changed = 1;
                ui_sb_update_icon_wp(SB_FLOPPY | drv->id, drv->read_only);
                return;
            }
            c++;
        }
    }
    drv->empty = 1;
    fdd_set_head(drv, 0);
    memset(drv->image_path, 0, sizeof(drv->image_path));
    ui_sb_update_icon_state(SB_FLOPPY | drv->id, 1);
}

void
fdd_close(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    /* Closing a tape drive ejects the cartridge; the drive stays on the cable. */
    if (fdd_tape_present(drv)) {
        fdd_tape_eject();
        return;
    }

    d86f_stop(drv); /* Call this first of all to make sure the 86F poll is back to idle state. */

    drv->hole          = NULL;
    drv->poll          = NULL;
    drv->seek          = NULL;
    drv->readsector    = NULL;
    drv->writesector   = NULL;
    drv->comparesector = NULL;
    drv->readaddress   = NULL;
    drv->format        = NULL;
    drv->byteperiod    = NULL;
    drv->stop          = NULL;
    drv->seek_in_progress = 0;

    if (strstr(drv->image_path, "ioctl://") != NULL) {
        floppy_ioctl_close(drv);
        img_close(drv);
    } else if (loaders[drv->driveloader].close)
        loaders[drv->driveloader].close(drv);

    drv->empty = 1;
    fdd_set_head(drv, 0);
    drv->image_path[0] = 0;
    d86f_destroy(drv);
    ui_sb_update_icon_state(SB_FLOPPY | drv->id, 1);
}

int
fdd_hole(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    int          ret = 0;

    if ((drv != NULL) && (drv->hole != NULL))
        ret = drv->hole(drv);

    return ret;
}

int
fdd_index(void *priv)
{
    fdd_drive_t *drv   = (fdd_drive_t *) priv;

    if ((drv == NULL) || !drv->motoron ||
        drv->empty || (drv->d86f_handler.index_hole_pos == NULL) ||
        (drv->d86f_handler.get_raw_size == NULL))
        return 0;

    const int      side      = fdd_get_head(drv);
    const uint32_t raw_size  = d86f_get_raw_size(drv, side);
    if (raw_size == 0)
        return 0;
    const uint32_t index_pos = drv->d86f_handler.index_hole_pos(drv, side);
    const uint32_t pulse_pos = (d86f_get_track_pos(drv) + raw_size - index_pos) % raw_size;

    /* A typical index pulse is active for about 4 ms of a 200 ms revolution. */
    return pulse_pos < (raw_size / 50);
}

static __inline uint64_t
fdd_byteperiod(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    uint64_t     ret = 32ULL * TIMER_USEC;

    if ((drv != NULL) && (drv->byteperiod != NULL))
        ret = drv->byteperiod(drv);

    return ret;
}

void
fdd_set_motor_enable(void *priv, int motor_enable)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    fdd_log("fdd_set_motor_enable(%d, %d)\n", drive, motor_enable);
    fdd_audio_set_motor_enable(drv, motor_enable);

    if (motor_enable && !drv->motoron)
        timer_set_delay_u64(&drv->poll_time, fdd_byteperiod(drv));
    else if (!motor_enable && drv->motoron)
        timer_disable(&drv->poll_time);

    drv->motoron = motor_enable;
}

static void
fdd_poll(void *priv)
{
    fdd_drive_t *drv   = (fdd_drive_t *) priv;
    fdc_t       *fdc   = (fdc_t *) drv->fdc;

    if (drv == NULL)
        return;

    timer_advance_u64(&drv->poll_time, fdd_byteperiod((void *) drv));

    if (drv->poll)
        drv->poll((void *) drv);

    if (fdd_notfound) {
        fdd_notfound--;
        if (!fdd_notfound)
            fdc_noidam(fdc);
    }

    if (drv->changed)
        fdc_diskchange_interrupt(fdc, drv->id);
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
    /* Reset boot status to POST on system reset */
    fdd_boot_status_reset();

    for (uint8_t i = 0; i < FDD_NUM; i++) {
        fdd_drive_t *drv = &drives[i];

        drives[i].id = i;
        timer_add(&drv->poll_time, fdd_poll, &drives[i], 0);

        /* Clear any pending seek state */
        drv->seek_in_progress = 0;
    }

    /* The tape drive keeps its own timer, which the reset has just torn down. */
    fdd_tape_init();
}

void
fdd_readsector(void *priv, const int sector, const int track, const int side,
               const int density, const int sector_size)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    if (drv == NULL)
        return;

    fdd_log("fdd_readsector(%d, %d, %d, %d, %d, %d)\n", drv->id, sector, track, side,
                                                        density, sector_size);

    /* First floppy read operation marks POST as complete */
    if (bios_boot_status == BIOS_BOOT_POST)
        fdd_set_boot_status(BIOS_BOOT_NORMAL);

    if (fdd_defer_op(drv)) {
        fdd_log("Seek in progress on drive %d, deferring READ (trk=%d->%d, side=%d, sec=%d)\n",
                drv->id, fdd[drive].track, track, side, sector);
        drv->pending = (fdd_pending_op_t) {
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

    if (drv->readsector != NULL)
        drv->readsector(drv, sector, track, side, density, sector_size);
    else
        fdd_notfound = 1000;
}

void
fdd_writesector(void *priv, const int sector, const int track, const int side,
                const int density, const int sector_size)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    if (drv == NULL)
        return;

    fdd_log("fdd_writesector(%d, %d, %d, %d, %d, %d)\n", drive, sector, track, side, density, sector_size);

    if (fdd_defer_op(drv)) {
        fdd_log("Seek in progress on drive %d, deferring WRITE (trk=%d->%d, side=%d, sec=%d)\n",
                drv->id, fdd[drv->id].track, track, side, sector);
        drv->pending = (fdd_pending_op_t) {
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

    if (drv->writesector != NULL)
        drv->writesector(drv, sector, track, side, density, sector_size);
    else
        fdd_notfound = 1000;
}

void
fdd_comparesector(void *priv, const int sector, const int track,const  int side,
                  const int density, const int sector_size)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    if (drv == NULL)
        return;

    if (fdd_defer_op(drv)) {
        fdd_log("Seek in progress on drive %d, deferring COMPARE (trk=%d->%d, side=%d, sec=%d)\n",
                drive, fdd[drv->id].track, track, side, sector);
        drv->pending = (fdd_pending_op_t) {
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

    if (drv->comparesector != NULL)
        drv->comparesector(drv, sector, track, side, density, sector_size);
    else
        fdd_notfound = 1000;
}

void
fdd_readaddress(void *priv, const int side, const int density)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    if (drv == NULL)
        return;

    if (fdd_defer_op(drv)) {
        fdd_log("Seek in progress on drive %d, deferring READADDRESS (trk=%d, side=%d)\n",
                drv->id, fdd[drv->id].track, side);
        drv->pending = (fdd_pending_op_t) {
            .pending = 1,
            .op      = FDD_OP_READADDR,
            .track   = fdd[drv->id].track,
            .side    = side,
            .density = density
        };
        return;
    }

    if (drv->readaddress != NULL)
        drv->readaddress(drv, side, density);
}

void
fdd_format(void *priv, int side, int density, uint8_t fill)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    if (drv == NULL)
        return;

    if (fdd_defer_op(drv)) {
        fdd_log("Seek in progress on drive %d, deferring FORMAT (trk=%d, side=%d)\n",
                drv->id, fdd[drv->id].track, side);
        drv->pending = (fdd_pending_op_t) {
            .pending = 1,
            .op      = FDD_OP_FORMAT,
            .track   = fdd[drv->id].track,
            .side    = side,
            .density = density,
            .fill    = fill
        };
        return;
    }

    if (drv->format == NULL)
        fdd_notfound = 1000;
    else
        drv->format(drv, side, density, fill);
}

void
fdd_stop(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    fdc_t *      fdc = (fdc_t *) drv->fdc;

    if (drv == NULL)
        return;

    if ((fdc != NULL) && (fdc->flags & FDC_FLAG_PCJX)) {
        /* Abort the pending operation, not the physical head position. */
        const int was_seeking = drv->seek_in_progress;
        timer_disable(&drv->seek_timer);
        drv->seek_in_progress = 0;
        drv->pending.pending = 0;
        drv->pending.op = FDD_OP_NONE;
        fdd_notfound = 0;
        if (was_seeking)
            fdd_do_seek(drv, fdd[drv->id].track);
    }

    if (drv->stop != NULL)
        drv->stop(drv);
}

void
fdd_init(void)
{
    int i;

    for (i = 0; i < FDD_NUM; i++) {
        drives[i].poll       = 0;
        drives[i].seek       = 0;
        drives[i].readsector = 0;
        drives[i].id         = i;
    }

    img_init();
    d86f_init();
    td0_init();
    imd_init();
    pcjs_init();
    fdd_tape_init();

    for (i = 0; i < FDD_NUM; i++) {
        fdd_drive_t *drv = &drives[i];

        if (fdd_tape_present(drv))
            continue;

        fdd_load(drv, drv->image_path);
    }

    if (fdd_sounds_enabled)
        fdd_audio_init();
}

void
fdd_do_writeback(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    if (drv == NULL)
        return;

    /* A floppy tape drive commits each sector to its image as the transfer
       runs, and has no d86f handler behind it to flush. */
    if (fdd_tape_present(drv))
        return;

    if (drv->d86f_handler.writeback != NULL)
        drv->d86f_handler.writeback(drv);
}
