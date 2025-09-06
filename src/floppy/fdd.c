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
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2018-2019 Fred N. van Kempen.
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
#include <86box/sound.h>    

static int16_t *load_wav(const char *filename, int *sample_count);

// TODO:
// OK 1. Implement spindle motor spin-up and spin-down
// 2. Move audio emulation to separate code file
// 3. Implement sound support for all drives (not only for drive 0)
// 4. Single sector read/write sound emulation
// 5. Multi-track seek sound emulation
// 6. Limit sound emulation only for 3,5" 300 rpm drives, until we have sound samples for other rpm drives

// Motor sound states
typedef enum {
    MOTOR_STATE_STOPPED = 0,
    MOTOR_STATE_STARTING,
    MOTOR_STATE_RUNNING,
    MOTOR_STATE_STOPPING
} motor_state_t;

static int16_t *spindlemotor_start_wav          = NULL;
static int      spindlemotor_start_wav_samples  = 0;
static char    *spindlemotor_start_wav_filename = "mitsumi_spindle_motor_start_48000_16_1_PCM.wav";

static int16_t *spindlemotor_loop_wav          = NULL;
static int      spindlemotor_loop_wav_samples  = 0;
static char    *spindlemotor_loop_wav_filename = "mitsumi_spindle_motor_loop_48000_16_1_PCM.wav";

static int16_t *spindlemotor_stop_wav          = NULL;
static int      spindlemotor_stop_wav_samples  = 0;
static char    *spindlemotor_stop_wav_filename = "mitsumi_spindle_motor_stop_48000_16_1_PCM.wav";

static int16_t *steptrackup_wav[80];
static int      steptrackup_wav_samples[80];
static int16_t *steptrackdown_wav[80];
static int      steptrackdown_wav_samples[80];
static int16_t *seekmultipletracks_wav[79]; // Seek 2, 3, 4 ... 80 tracks = 79 sounds
static int      seekmultipletracks_wav_samples[79];

static int           spindlemotor_pos[FDD_NUM]                    = {};
static motor_state_t spindlemotor_state[FDD_NUM]                  = {};
static float         spindlemotor_fade_volume[FDD_NUM]            = {};
static int           spindlemotor_fade_samples_remaining[FDD_NUM] = {};

// Fade duration: 75ms at 48kHz = 3600 samples
#define FADE_DURATION_MS 75
#define FADE_SAMPLES     (48000 * FADE_DURATION_MS / 1000)

// WAV-header
typedef struct {
    char     riff[4];
    uint32_t size;
    char     wave[4];
    char     fmt[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char     data[4];
    uint32_t data_size;
} wav_header_t;

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

char  floppyfns[FDD_NUM][512];
char *fdd_image_history[FDD_NUM][FLOPPY_IMAGE_HISTORY];

pc_timer_t fdd_poll_time[FDD_NUM];

static int fdd_notfound = 0;
static int driveloaders[FDD_NUM];

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
    va_list ap;

    if (fdd_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define fdd_log(fmt, ...)
#endif

static const char *
motor_state_name(motor_state_t state)
{
    switch (state) {
        case MOTOR_STATE_STOPPED:
            return "STOPPED";
        case MOTOR_STATE_STARTING:
            return "STARTING";
        case MOTOR_STATE_RUNNING:
            return "RUNNING";
        case MOTOR_STATE_STOPPING:
            return "STOPPING";
        default:
            return "UNKNOWN";
    }
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

void
fdd_seek(int drive, int track_diff)
{
    if (!track_diff)
        return;

    fdd[drive].track += track_diff;

    if (fdd[drive].track < 0)
        fdd[drive].track = 0;

    if (fdd[drive].track > drive_types[fdd[drive].type].max_track)
        fdd[drive].track = drive_types[fdd[drive].type].max_track;

    fdd_changed[drive] = 0;

    fdd_do_seek(drive, fdd[drive].track);
}

int
fdd_track0(int drive)
{
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
    if (motor_enable && !motoron[drive]) {
        // Motor starting up
        if (spindlemotor_state[drive] == MOTOR_STATE_STOPPING) {
            // Interrupt stop sequence and transition back to loop
            spindlemotor_state[drive]                  = MOTOR_STATE_RUNNING;
            spindlemotor_pos[drive]                    = 0;
            spindlemotor_fade_volume[drive]            = 1.0f;
            spindlemotor_fade_samples_remaining[drive] = 0;
        } else {
            // Normal startup
            spindlemotor_state[drive]                  = MOTOR_STATE_STARTING;
            spindlemotor_pos[drive]                    = 0;
            spindlemotor_fade_volume[drive]            = 1.0f;
            spindlemotor_fade_samples_remaining[drive] = 0;
        }
        timer_set_delay_u64(&fdd_poll_time[drive], fdd_byteperiod(drive));
    } else if (!motor_enable && motoron[drive]) {
        // Motor stopping
        spindlemotor_state[drive]                  = MOTOR_STATE_STOPPING;
        spindlemotor_pos[drive]                    = 0;
        spindlemotor_fade_volume[drive]            = 1.0f;
        spindlemotor_fade_samples_remaining[drive] = FADE_SAMPLES;
        // Don't disable timer yet - let the stop sound finish
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
    if (drives[drive].readsector)
        drives[drive].readsector(drive, sector, track, side, density, sector_size);
    else
        fdd_notfound = 1000;
}

void
fdd_writesector(int drive, int sector, int track, int side, int density, int sector_size)
{
    if (drives[drive].writesector)
        drives[drive].writesector(drive, sector, track, side, density, sector_size);
    else
        fdd_notfound = 1000;
}

void
fdd_comparesector(int drive, int sector, int track, int side, int density, int sector_size)
{
    if (drives[drive].comparesector)
        drives[drive].comparesector(drive, sector, track, side, density, sector_size);
    else
        fdd_notfound = 1000;
}

void
fdd_readaddress(int drive, int side, int density)
{
    if (drives[drive].readaddress)
        drives[drive].readaddress(drive, side, density);
}

void
fdd_format(int drive, int side, int density, uint8_t fill)
{
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

    spindlemotor_start_wav = load_wav(spindlemotor_start_wav_filename, &spindlemotor_start_wav_samples);
    spindlemotor_loop_wav  = load_wav(spindlemotor_loop_wav_filename, &spindlemotor_loop_wav_samples);
    spindlemotor_stop_wav  = load_wav(spindlemotor_stop_wav_filename, &spindlemotor_stop_wav_samples);

    for (i = 0; i < FDD_NUM; i++) {
        drives[i].poll                         = 0;
        drives[i].seek                         = 0;
        drives[i].readsector                   = 0;
        spindlemotor_pos[i]                    = 0;
        spindlemotor_state[i]                  = MOTOR_STATE_STOPPED;
        spindlemotor_fade_volume[i]            = 1.0f;
        spindlemotor_fade_samples_remaining[i] = 0;
    }

    img_init();
    d86f_init();
    td0_init();
    imd_init();
    pcjs_init();

    for (i = 0; i < FDD_NUM; i++) {
        fdd_load(i, floppyfns[i]);
    }

    sound_fdd_thread_init();
}

void
fdd_do_writeback(int drive)
{
    d86f_handler[drive].writeback(drive);
}

static int16_t *
load_wav(const char *filename, int *sample_count)
{
    FILE *f = fopen(filename, "rb");
    if (!f)
        return NULL;

    wav_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
        fclose(f);
        return NULL;
    }

    if (memcmp(hdr.riff, "RIFF", 4) || memcmp(hdr.wave, "WAVE", 4) || memcmp(hdr.fmt, "fmt ", 4) || memcmp(hdr.data, "data", 4)) {
        fclose(f);
        return NULL;
    }

    // Accept both mono and stereo, 16-bit PCM
    if (hdr.audio_format != 1 || hdr.bits_per_sample != 16 || (hdr.num_channels != 1 && hdr.num_channels != 2)) {
        fclose(f);
        return NULL;
    }

    int      input_samples = hdr.data_size / 2; // 2 bytes per sample
    int16_t *input_data    = malloc(hdr.data_size);
    if (!input_data) {
        fclose(f);
        return NULL;
    }

    if (fread(input_data, 1, hdr.data_size, f) != hdr.data_size) {
        free(input_data);
        fclose(f);
        return NULL;
    }
    fclose(f);

    int16_t *output_data;
    int      output_samples;

    if (hdr.num_channels == 1) {
        // Convert mono to stereo
        output_samples = input_samples;                               // Number of stereo sample pairs
        output_data    = malloc(input_samples * 2 * sizeof(int16_t)); // Allocate for stereo
        if (!output_data) {
            free(input_data);
            return NULL;
        }

        // Convert mono to stereo by duplicating each sample
        for (int i = 0; i < input_samples; i++) {
            output_data[i * 2]     = input_data[i]; // Left channel
            output_data[i * 2 + 1] = input_data[i]; // Right channel
        }

        free(input_data);
    } else {
        // Already stereo
        output_data    = input_data;
        output_samples = input_samples / 2; // Number of stereo sample pairs
    }

    if (sample_count)
        *sample_count = output_samples;

    return output_data;
}

void
fdd_audio_callback(int16_t *buffer, int length)
{
    // Clear buffer
    memset(buffer, 0, length * sizeof(int16_t));

    // Check if any motor is running or transitioning
    int any_motor_active = 0;
    for (int drive = 0; drive < FDD_NUM; drive++) {
        if (spindlemotor_state[drive] != MOTOR_STATE_STOPPED) {
            any_motor_active = 1;
            break;
        }
    }

    if (!any_motor_active)
        return;

    float *float_buffer      = (float *) buffer;
    int    samples_in_buffer = length / 2;

    // Process audio for drive 0 only for now
    int drive = 0;

    if (spindlemotor_state[drive] == MOTOR_STATE_STOPPED)
        return;

    for (int i = 0; i < samples_in_buffer; i++) {
        float left_sample  = 0.0f;
        float right_sample = 0.0f;

        switch (spindlemotor_state[drive]) {
            case MOTOR_STATE_STARTING:
                if (spindlemotor_start_wav && spindlemotor_pos[drive] < spindlemotor_start_wav_samples) {
                    // Play start sound
                    left_sample  = (float) spindlemotor_start_wav[spindlemotor_pos[drive] * 2] / 32768.0f;
                    right_sample = (float) spindlemotor_start_wav[spindlemotor_pos[drive] * 2 + 1] / 32768.0f;
                    spindlemotor_pos[drive]++;
                } else {
                    // Start sound finished, transition to loop
                    spindlemotor_state[drive] = MOTOR_STATE_RUNNING;
                    spindlemotor_pos[drive]   = 0;
                }
                break;

            case MOTOR_STATE_RUNNING:
                if (spindlemotor_loop_wav && spindlemotor_loop_wav_samples > 0) {
                    // Play loop sound
                    left_sample  = (float) spindlemotor_loop_wav[spindlemotor_pos[drive] * 2] / 32768.0f;
                    right_sample = (float) spindlemotor_loop_wav[spindlemotor_pos[drive] * 2 + 1] / 32768.0f;
                    spindlemotor_pos[drive]++;

                    // Loop back to beginning
                    if (spindlemotor_pos[drive] >= spindlemotor_loop_wav_samples) {
                        spindlemotor_pos[drive] = 0;
                    }
                }
                break;

            case MOTOR_STATE_STOPPING:
                if (spindlemotor_fade_samples_remaining[drive] > 0) {
                    // Mix fading loop sound with rising stop sound
                    float loop_volume = spindlemotor_fade_volume[drive];
                    float stop_volume = 1.0f - loop_volume;

                    float loop_left = 0.0f, loop_right = 0.0f;
                    float stop_left = 0.0f, stop_right = 0.0f;

                    // Get loop sample (continue from current position)
                    if (spindlemotor_loop_wav && spindlemotor_loop_wav_samples > 0) {
                        int loop_pos = spindlemotor_pos[drive] % spindlemotor_loop_wav_samples;
                        loop_left    = (float) spindlemotor_loop_wav[loop_pos * 2] / 32768.0f;
                        loop_right   = (float) spindlemotor_loop_wav[loop_pos * 2 + 1] / 32768.0f;
                    }

                    // Get stop sample
                    if (spindlemotor_stop_wav && spindlemotor_pos[drive] < spindlemotor_stop_wav_samples) {
                        stop_left  = (float) spindlemotor_stop_wav[spindlemotor_pos[drive] * 2] / 32768.0f;
                        stop_right = (float) spindlemotor_stop_wav[spindlemotor_pos[drive] * 2 + 1] / 32768.0f;
                    }

                    // Mix the sounds
                    left_sample  = loop_left * loop_volume + stop_left * stop_volume;
                    right_sample = loop_right * loop_volume + stop_right * stop_volume;

                    spindlemotor_pos[drive]++;
                    spindlemotor_fade_samples_remaining[drive]--;

                    // Update fade volume
                    spindlemotor_fade_volume[drive] = (float) spindlemotor_fade_samples_remaining[drive] / FADE_SAMPLES;
                } else {
                    // Fade completed, play remaining stop sound
                    if (spindlemotor_stop_wav && spindlemotor_pos[drive] < spindlemotor_stop_wav_samples) {
                        left_sample  = (float) spindlemotor_stop_wav[spindlemotor_pos[drive] * 2] / 32768.0f;
                        right_sample = (float) spindlemotor_stop_wav[spindlemotor_pos[drive] * 2 + 1] / 32768.0f;
                        spindlemotor_pos[drive]++;
                    } else {
                        // Stop sound finished
                        spindlemotor_state[drive] = MOTOR_STATE_STOPPED;
                        timer_disable(&fdd_poll_time[drive]);
                    }
                }
                break;

            default:
                break;
        }

        float_buffer[i * 2]     = left_sample;
        float_buffer[i * 2 + 1] = right_sample;
    }
}