/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Elo TouchSystems SmartSet Serial touchscreen emulation.
 *
 * Authors: Claude (Anthropic)
 *
 *          Copyright 2026 Xeon3D.
 */

/* Reference: Elo TouchSystems, "SmartSet Touchscreen Controller Family
 *            Technical Reference Manual", 1993, P/N 008217 (DOC # SW000027).
 *            The section and page numbers in the comments below are that
 *            manual's.
 *
 *            This implements the native "SmartSet" binary serial protocol
 *            used by the E271-2200/E271-2210 controllers -- a 10-byte packet
 *            of an 0x55 lead-in, 8 command or response bytes and a checksum
 *            -- which is also what the Linux kernel's
 *            drivers/input/touchscreen/elo.c calls its "10-byte" format.
 *
 *            The whole documented command set is here, including the Emulate
 *            command's eight legacy output formats (E271-140, E261-280 and
 *            E281A-4002, binary and ASCII).  Those are the same wire formats
 *            the kernel driver calls its 6-, 4- and 3-byte protocols, so a
 *            guest can be pointed at any of them.
 *
 * Two things are deliberately not emulated, rather than missing:
 *
 *   - Filter ('F') is stored and reported but has no effect, because there is
 *     nothing here to filter.  Its four bytes tune the analog sampling of a
 *     real resistive screen -- how many samples to average, how far they may
 *     deviate, how many contiguous readings make a state change, the
 *     touch-down voltage threshold.  The host's pointer has no noise for any
 *     of that to remove, so storing the settings faithfully is the whole of
 *     the behaviour.
 *
 *   - Low Power ('L') is likewise stored and reported.  On real hardware it
 *     idles the controller between touches; it has no effect on the wire, so
 *     there is nothing to observe from the guest.
 */
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/mouse.h>
#include <86box/serial.h>
#include <86box/plat.h>
#include <86box/fifo8.h>
#include <86box/fifo.h>
#include <86box/video.h>
#include <86box/nvr.h>

#define ELO_RAW_MAX 4095

enum elo_mode1_bits {
    ELO_M1_INITIAL   = 0x01,
    ELO_M1_STREAM    = 0x02,
    ELO_M1_UNTOUCH   = 0x04,
    ELO_M1_WARNING   = 0x10,
    ELO_M1_RANGECHK  = 0x40,
    ELO_M1_ZRESERVED = 0x80
};

enum elo_mode2_bits {
    ELO_M2_TRIM  = 0x02,
    ELO_M2_CAL   = 0x04,
    ELO_M2_SCALE = 0x08,
    ELO_M2_TRACK = 0x40
};

/* Status bit 6 of a Touch packet: the touch was outside the calibration points.
   Only ever set when Range Checking is on, which itself needs Calibration. */
#define ELO_ST_OUTOFRANGE 0x40

/* Acknowledge error codes, from the table in the command reference.  Only the
   ones this controller can actually raise are named. */
#define ELO_ERR_NONE       '0'
#define ELO_ERR_CHECKSUM   '3' /* bad input checksum */
#define ELO_ERR_ILLEGALCMD '5' /* illegal command */
#define ELO_ERR_CALCANCEL  '6' /* calibration command cancelled */
#define ELO_ERR_NOSET      'A' /* no set available for this command */
#define ELO_ERR_SUBCMD     'C' /* illegal subcommand */
#define ELO_ERR_RANGE      'D' /* operand out of range */
#define ELO_ERR_NOQUERY    'G' /* no query available for this command */

/* Emulate ('E') output formats.  These replace the Touch packet only: the
   command set stays SmartSet, which is what the manual calls partial
   emulation.  Full emulation is a jumper on real hardware and would mean not
   answering SmartSet at all, so it is not something a guest can ask for. */
enum elo_emul_format {
    ELO_FMT_140_BIN  = 0, /* E271-140 binary, 4 bytes                 */
    ELO_FMT_140_ASC  = 1, /* E271-140 ASCII                           */
    ELO_FMT_280_BIN  = 2, /* E261-280 binary, 3 bytes                 */
    ELO_FMT_280_ASC  = 3, /* E261-280 ASCII, hex pairs                */
    ELO_FMT_2200_BIN = 4, /* the native SmartSet Touch packet         */
    ELO_FMT_2200_ASC = 5, /* SmartSet ASCII mode                      */
    ELO_FMT_4002_BIN = 6, /* E281A-4002 binary, 6 bytes with Z        */
    ELO_FMT_4002_ASC = 7  /* E281A-4002 ASCII with Z                  */
};

/* The E261-280 formats are defined in terms of a 2..255 coordinate range, and
   selecting one forces scaling to it -- losing whatever scaling was in use. */
#define ELO_280_LO 2
#define ELO_280_HI 255

typedef struct mouse_elo_t {
    /* Packet framing state. */
    uint8_t body[8];
    int     cmd_pos;  /* -1 = waiting for lead byte, 0..7 = body index, then checksum */
    bool    in_keyed; /* this packet arrived in the extended, keyed form */
    uint8_t in_key;   /* and is addressed to this key */

    /* Response FIFO -> host, paced at baud rate. */
    Fifo8 resp;

    /* Serial port. */
    ser_port_t *serial;
    int       baud_rate;
    uint8_t   ser1, ser2; /* raw Parameter bytes, echoed back verbatim */
    bool      in_reset;

    /* Modes. */
    uint8_t mode1, mode2;
    bool    quiet_all, quiet_timer_flag, quiet_touch;

    /* Calibration (raw 0..4095 native space -> 0..4095 "calibrated" space).
       Whether it is applied is Mode2 bit 2, not a flag of its own: setting
       points and turning the mapping on are separate steps on real hardware. */
    int32_t cal_lo[2], cal_hi[2];
    bool    swap_axes;

    /* Two-point auto-calibration ('C2') in progress. */
    bool    cal_active;
    int     cal_step;
    bool    cal_but_old;
    int32_t cal_points[2][2];

    /* Scaling (0..4095 calibrated space -> arbitrary signed output range).
       Applied when Mode2 bit 3 is set; usable without calibration, which is
       how the emulation formats get their own coordinate ranges. */
    int32_t scale_lo[2], scale_hi[2];
    uint8_t invert_mask;

    /* Output format emulation. */
    uint8_t emul_format;    /* enum elo_emul_format */
    bool    emul_touchflag; /* include touch/untouch information */

    /* Stored configuration with no behaviour of its own -- see the file
       comment for why Filter and Low Power are complete as they stand. */
    uint8_t diag_result;
    uint8_t filter_slen, filter_width, filter_states, filter_control;
    uint8_t low_power;
    char    owner[7];

    /* Report ('B'): both delays are in 10ms units. */
    uint8_t    untouch_delay, rep_delay;
    bool       untouch_pending; /* holding off, in case the touch resumes */
    bool       rep_ready;       /* the inter-packet delay has elapsed */
    pc_timer_t untouch_timer;
    pc_timer_t rep_delay_timer;

    /* Timer ('H'): a countdown that sends the host a packet when it expires. */
    uint8_t    timer_enable, timer_mode;
    uint16_t   timer_interval;
    pc_timer_t user_timer;

    /* Key ('K'): non-zero switches every packet to the extended form. */
    uint8_t key_value;

    /* Warnings not related to a command, collected for an Acknowledge query. */
    uint8_t warn[4];

    /* Touch/pointer state.  but_old belongs to the transmit path, which runs
       on the baud clock; but_poll_prev is the polling side's own edge
       detector, and the two cannot be shared because they tick at different
       rates. */
    int     but, but_old;
    bool    but_poll_prev;
    int32_t raw_x, raw_y, raw_x_old, raw_y_old;

    /* Tracking mode drops a Stream packet repeating the last coordinate. */
    bool    have_sent;
    int32_t sent_x, sent_y;

    char nvr_path[64];

    pc_timer_t host_to_serial_timer;
    pc_timer_t reset_timer;
} mouse_elo_t;

/* The rates the SmartSet's 'P' command indexes, in its own order. */
static const int elo_baud_table[8] = { 300, 600, 1200, 2400, 4800, 9600, 19200, 38400 };

/* The touch path is defined below the command handlers, but a Touch query and
   the Emulate command both reach into it. */
static void    elo_send_touch(mouse_elo_t *dev, uint8_t status, int32_t x, int32_t y, int32_t z);
static int32_t elo_z_value(mouse_elo_t *dev);

/* What lives in the NVRAM file, in order: the Setup area, then the primary and
   secondary Calibration and Scaling pages.  The Areas byte of the 'N' command
   picks which of the three are written or read; Page picks primary or
   secondary, and only applies to Calibration and Scaling because the
   controller has just one Setup area. */
enum elo_nvr_area {
    ELO_AREA_SETUP = 0x01,
    ELO_AREA_CAL   = 0x02,
    ELO_AREA_SCALE = 0x04
};

typedef struct elo_nvr_file_t {
    uint32_t magic;
    /* Setup */
    uint8_t  mode1, mode2, ser1, ser2;
    uint8_t  untouch_delay, rep_delay, key_value, low_power;
    uint8_t  filter_slen, filter_width, filter_states, filter_control;
    uint8_t  timer_enable, timer_mode, emul_format, emul_touchflag;
    uint16_t timer_interval;
    uint8_t  swap_axes, invert_mask;
    char     owner[8];
    /* Calibration and Scaling, primary then secondary */
    int32_t cal[2][2][2];   /* [page][axis][lo,hi] */
    int32_t scale[2][2][2]; /* [page][axis][lo,hi] */
} elo_nvr_file_t;

#define ELO_NVR_MAGIC 0x314f4c45 /* "ELO1" */

static void
elo_nvr_read_file(mouse_elo_t *dev, elo_nvr_file_t *f)
{
    FILE *fp = nvr_fopen(dev->nvr_path, "rb");

    memset(f, 0, sizeof(*f));
    if (fp) {
        if (fread(f, sizeof(*f), 1, fp) != 1)
            memset(f, 0, sizeof(*f));
        fclose(fp);
    }
    if (f->magic != ELO_NVR_MAGIC)
        memset(f, 0, sizeof(*f));
}

static void
elo_save_nvr_areas(mouse_elo_t *dev, uint8_t areas, int page)
{
    elo_nvr_file_t f;
    FILE          *fp;

    /* Read-modify-write: a save of one area must not blank the others. */
    elo_nvr_read_file(dev, &f);
    f.magic = ELO_NVR_MAGIC;

    if (areas & ELO_AREA_SETUP) {
        f.mode1          = dev->mode1;
        f.mode2          = dev->mode2;
        f.ser1           = dev->ser1;
        f.ser2           = dev->ser2;
        f.untouch_delay  = dev->untouch_delay;
        f.rep_delay      = dev->rep_delay;
        f.key_value      = dev->key_value;
        f.low_power      = dev->low_power;
        f.filter_slen    = dev->filter_slen;
        f.filter_width   = dev->filter_width;
        f.filter_states  = dev->filter_states;
        f.filter_control = dev->filter_control;
        f.timer_enable   = dev->timer_enable;
        f.timer_mode     = dev->timer_mode;
        f.timer_interval = dev->timer_interval;
        f.emul_format    = dev->emul_format;
        f.emul_touchflag = dev->emul_touchflag ? 1 : 0;
        f.swap_axes      = dev->swap_axes ? 1 : 0;
        f.invert_mask    = dev->invert_mask;
        memcpy(f.owner, dev->owner, 7);
    }
    if (areas & ELO_AREA_CAL)
        for (int a = 0; a < 2; a++) {
            f.cal[page][a][0] = dev->cal_lo[a];
            f.cal[page][a][1] = dev->cal_hi[a];
        }
    if (areas & ELO_AREA_SCALE)
        for (int a = 0; a < 2; a++) {
            f.scale[page][a][0] = dev->scale_lo[a];
            f.scale[page][a][1] = dev->scale_hi[a];
        }

    fp = nvr_fopen(dev->nvr_path, "wb");
    if (fp) {
        fwrite(&f, sizeof(f), 1, fp);
        fclose(fp);
    }
}

static void
elo_load_defaults(mouse_elo_t *dev)
{
    dev->mode1       = ELO_M1_ZRESERVED | ELO_M1_INITIAL | ELO_M1_STREAM | ELO_M1_UNTOUCH;
    dev->mode2       = 0;
    dev->swap_axes   = false;
    dev->invert_mask = 0;
    dev->cal_lo[0] = dev->cal_lo[1] = 0;
    dev->cal_hi[0] = dev->cal_hi[1] = ELO_RAW_MAX;
    dev->scale_lo[0] = dev->scale_lo[1] = 0;
    dev->scale_hi[0] = dev->scale_hi[1] = ELO_RAW_MAX;

    /* Factory defaults from the command reference. */
    dev->untouch_delay  = 0;
    dev->rep_delay      = 2; /* 20ms between touch packets */
    dev->filter_slen    = 4;
    dev->filter_width   = 8;
    dev->filter_states  = 8;
    dev->filter_control = 0x90;
    dev->timer_enable   = 0;
    dev->timer_mode     = 0;   /* one-shot */
    dev->timer_interval = 100; /* one second */
    dev->key_value      = 0;   /* keyed packets off */
    dev->low_power      = 0;
    dev->emul_format    = ELO_FMT_2200_BIN;
    dev->emul_touchflag = true; /* the '1','4' the manual calls the default */
    memcpy(dev->owner, "EloInc.", 7);
}

static void
elo_load_nvr_areas(mouse_elo_t *dev, uint8_t areas, int page)
{
    elo_nvr_file_t f;

    elo_nvr_read_file(dev, &f);
    if (f.magic != ELO_NVR_MAGIC)
        return; /* nothing saved yet: leave what is running alone */

    if (areas & ELO_AREA_SETUP) {
        dev->mode1          = f.mode1 | ELO_M1_ZRESERVED;
        dev->mode2          = f.mode2;
        dev->ser1           = f.ser1;
        dev->ser2           = f.ser2;
        dev->untouch_delay  = f.untouch_delay;
        dev->rep_delay      = f.rep_delay;
        dev->key_value      = f.key_value;
        dev->low_power      = f.low_power;
        dev->filter_slen    = f.filter_slen;
        dev->filter_width   = f.filter_width;
        dev->filter_states  = f.filter_states;
        dev->filter_control = f.filter_control;
        dev->timer_enable   = f.timer_enable;
        dev->timer_mode     = f.timer_mode;
        dev->timer_interval = f.timer_interval;
        dev->emul_format    = f.emul_format & 7;
        dev->emul_touchflag = f.emul_touchflag != 0;
        dev->swap_axes      = f.swap_axes != 0;
        dev->invert_mask    = f.invert_mask;
        memcpy(dev->owner, f.owner, 7);
    }
    if (areas & ELO_AREA_CAL)
        for (int a = 0; a < 2; a++) {
            dev->cal_lo[a] = f.cal[page][a][0];
            dev->cal_hi[a] = f.cal[page][a][1];
        }
    if (areas & ELO_AREA_SCALE)
        for (int a = 0; a < 2; a++) {
            dev->scale_lo[a] = f.scale[page][a][0];
            dev->scale_hi[a] = f.scale[page][a][1];
        }
}

/* Power-on: defaults first, then whatever NVRAM holds on top -- the J7 jumper
   decides that on real hardware, and there is no reason to boot from anything
   else here. */
static void
elo_load_nvr(mouse_elo_t *dev)
{
    elo_load_defaults(dev);
    elo_load_nvr_areas(dev, ELO_AREA_SETUP | ELO_AREA_CAL | ELO_AREA_SCALE, 0);
}

/* Bytes that are not a SmartSet packet: the emulation formats are raw streams
   with their own framing, and get no lead-in byte or checksum. */
static void
elo_emit_raw(mouse_elo_t *dev, const uint8_t *buf, int len)
{
    if (dev->quiet_all)
        return;
    if ((int) fifo8_num_free(&dev->resp) < len)
        return;
    fifo8_push_all(&dev->resp, (uint8_t *) buf, (uint32_t) len);
}

/* A SmartSet packet.  With a Key set, every packet goes out in the extended
   form instead: a Control-V lead-in, the key ahead of the checksum, and the
   key counted in the sum.  The host is expected to look at the lead-in byte to
   know whether 9 or 10 bytes follow. */
static void
elo_enqueue(mouse_elo_t *dev, uint8_t cmd, const uint8_t data[7])
{
    uint8_t buf[11];
    uint8_t sum = 0xAA;
    int     len;

    if (dev->quiet_all)
        return;

    buf[0] = dev->key_value ? 0x16 : 0x55;
    buf[1] = cmd;
    memcpy(&buf[2], data, 7);
    len = 9;
    if (dev->key_value)
        buf[len++] = dev->key_value;

    for (int i = 0; i < len; i++)
        sum += buf[i];
    buf[len++] = sum;

    if ((int) fifo8_num_free(&dev->resp) < len)
        return;
    fifo8_push_all(&dev->resp, buf, (uint32_t) len);
}

/* The Acknowledge that follows every command.  Warnings that were not caused
   by a command queue up instead, and an Acknowledge query drains them -- which
   is what the Warning-pending bit in a Mode query is telling the host to do. */
static void
elo_ack(mouse_elo_t *dev, uint8_t err)
{
    uint8_t data[7] = { err, 0, 0, 0, 0, 0, 0 };

    elo_enqueue(dev, 'A', data);
}

static void
elo_ack_warnings(mouse_elo_t *dev)
{
    uint8_t data[7] = { 0 };

    for (int i = 0; i < 4; i++) {
        data[i]      = dev->warn[i] ? dev->warn[i] : ELO_ERR_NONE;
        dev->warn[i] = 0;
    }
    elo_enqueue(dev, 'A', data);
}

static void
elo_warn(mouse_elo_t *dev, uint8_t code)
{
    for (int i = 0; i < 4; i++)
        if (!dev->warn[i]) {
            dev->warn[i] = code;
            return;
        }
}

/* Raw 0..4095 -> what goes out on the wire, and whether the touch fell outside
   the calibrated area.

   The order is the one the Mode command describes: calibration maps the raw
   reading onto the image and lands back in 0..4095, then scaling takes that to
   whatever signed range was asked for.  Either may be enabled without the
   other -- scaling alone is how the emulation formats get their ranges. */
static int32_t
elo_transform(mouse_elo_t *dev, int axis, int32_t raw, bool *out_of_range)
{
    int32_t mid = raw;
    int32_t lo, hi, out;

    if (dev->mode2 & ELO_M2_CAL) {
        lo  = dev->cal_lo[axis];
        hi  = dev->cal_hi[axis];
        mid = (hi != lo) ? (int32_t) (((int64_t) (raw - lo) * ELO_RAW_MAX) / (hi - lo)) : 0;

        if ((mid < 0) || (mid > ELO_RAW_MAX)) {
            /* Range Checking is what makes this visible to the host, and it
               needs Calibration -- which is why the test lives in here. */
            if ((dev->mode1 & ELO_M1_RANGECHK) && (out_of_range != NULL))
                *out_of_range = true;

            /* Trim pulls the coordinate back to the edge of the calibrated
               area.  The manual makes it depend on Range Checking, so an
               out-of-range touch reads as the edge only when the host asked
               to be told about out-of-range touches at all. */
            if ((dev->mode2 & ELO_M2_TRIM) && (dev->mode1 & ELO_M1_RANGECHK)) {
                if (mid < 0)
                    mid = 0;
                if (mid > ELO_RAW_MAX)
                    mid = ELO_RAW_MAX;
            }
        }
    }

    if (!(dev->mode2 & ELO_M2_SCALE))
        return mid;

    lo  = dev->scale_lo[axis];
    hi  = dev->scale_hi[axis];
    out = lo + (int32_t) (((int64_t) mid * (hi - lo)) / ELO_RAW_MAX);

    if (dev->invert_mask & (1 << axis))
        out = lo + hi - out;

    return out;
}

static void
elo_reset_complete(void *priv)
{
    mouse_elo_t *dev = (mouse_elo_t *) priv;

    dev->in_reset = false;
}

static void
elo_soft_reset(mouse_elo_t *dev)
{
    fifo8_reset(&dev->resp);
    dev->cal_active = false;
}

/* RepDelay elapsed: the next Stream packet may go. */
static void
elo_rep_delay_done(void *priv)
{
    mouse_elo_t *dev = (mouse_elo_t *) priv;

    dev->rep_ready = true;
}

/* The Untouch delay elapsed with the screen still untouched, so the release
   was real rather than a skip while sliding.  Letting the flag go lets the
   normal path see but=0 against but_old=1 and report the untouch. */
static void
elo_untouch_done(void *priv)
{
    mouse_elo_t *dev = (mouse_elo_t *) priv;

    dev->untouch_pending = false;
}

/* The User Timer expired.  Current reads zero at this point, which is what
   tells a host that queried it that this is the expiry. */
static void
elo_user_timer_fire(void *priv)
{
    mouse_elo_t *dev  = (mouse_elo_t *) priv;
    uint8_t      d[7] = { 0 };

    if (!dev->timer_enable)
        return;

    if (!dev->quiet_timer_flag) {
        d[0] = dev->timer_enable;
        d[1] = dev->timer_mode;
        d[2] = dev->timer_interval & 0xff;
        d[3] = (dev->timer_interval >> 8) & 0xff;
        d[4] = 0; /* Current: zero at expiry */
        d[5] = 0;
        elo_enqueue(dev, 'H', d);
    }

    if (dev->timer_mode & 1) /* continuous: reload */
        timer_on_auto(&dev->user_timer, dev->timer_interval * 10000.0);
    else
        dev->timer_enable = 0; /* one-shot: disables itself */
}

/* Arm or stop the User Timer to match the current settings. */
static void
elo_timer_apply(mouse_elo_t *dev)
{
    timer_stop(&dev->user_timer);
    if (dev->timer_enable)
        timer_on_auto(&dev->user_timer, dev->timer_interval * 10000.0);
}

/* Everything the Configuration query dumps, in order.  Each entry is a packet
   the host can send straight back as a set command, which is the whole point
   of the command -- so the shapes here have to match what the set paths below
   parse.  ELO_CONFIG_PACKETS is what the ID command reports in its P byte;
   the two must agree or a host will read the wrong number of packets. */
#define ELO_CONFIG_PACKETS 15

static void
elo_dump_config(mouse_elo_t *dev)
{
    uint8_t d[7];
    int     count = 0;

#define ELO_PUT(c, ...)                         \
    do {                                        \
        const uint8_t tmp[7] = { __VA_ARGS__ }; \
        memcpy(d, tmp, 7);                      \
        elo_enqueue(dev, (c), d);               \
        count++;                                \
    } while (0)

    /* Mode, with the null that marks the binary form. */
    ELO_PUT('M', 0, dev->mode1, dev->mode2, 0, 0, 0, 0);
    ELO_PUT('B', dev->untouch_delay, dev->rep_delay, 0, 0, 0, 0, 0);

    for (int a = 0; a < 2; a++)
        ELO_PUT('C', a ? 'Y' : 'X',
                dev->cal_lo[a] & 0xff, (dev->cal_lo[a] >> 8) & 0xff,
                dev->cal_hi[a] & 0xff, (dev->cal_hi[a] >> 8) & 0xff, 0, 0);
    ELO_PUT('C', 'S', dev->swap_axes ? 1 : 0, 0, 0, 0, 0, 0);

    for (int a = 0; a < 2; a++)
        ELO_PUT('S', a ? 'Y' : 'X',
                dev->scale_lo[a] & 0xff, (dev->scale_lo[a] >> 8) & 0xff,
                dev->scale_hi[a] & 0xff, (dev->scale_hi[a] >> 8) & 0xff, 0, 0);
    ELO_PUT('S', 'I', dev->invert_mask, 0, 0, 0, 0, 0);

    ELO_PUT('F', '0', dev->filter_slen, dev->filter_width, dev->filter_states,
            dev->filter_control, 0, 0);
    ELO_PUT('H', dev->timer_enable, dev->timer_mode,
            dev->timer_interval & 0xff, (dev->timer_interval >> 8) & 0xff, 0, 0, 0);
    ELO_PUT('E', dev->emul_touchflag ? '1' : '0',
            (uint8_t) ('0' + dev->emul_format), 0, 0, 0, 0, 0);
    ELO_PUT('L', dev->low_power, 0, 0, 0, 0, 0, 0);
    ELO_PUT('K', dev->key_value, 0, 0, 0, 0, 0, 0);
    ELO_PUT('O', dev->owner[0], dev->owner[1], dev->owner[2], dev->owner[3],
            dev->owner[4], dev->owner[5], dev->owner[6]);
    /* Last, because replaying it changes the line the rest arrived on. */
    ELO_PUT('P', '0', dev->ser1, dev->ser2, 0, 0, 0, 0);

#undef ELO_PUT

    /* The host reads exactly the number of packets ID promised it, so the two
       drifting apart would desynchronise it rather than fail visibly.  Two of
       the entries above are loops, which is how the hand count went wrong the
       first time. */
    if (count != ELO_CONFIG_PACKETS)
        pclog("ELO: config dump sent %d packets, ID promises %d\n", count, ELO_CONFIG_PACKETS);
}

static void
elo_process_mode_ascii(mouse_elo_t *dev, const uint8_t *str)
{
    dev->mode1 = ELO_M1_ZRESERVED;
    dev->mode2 = 0;

    for (int i = 0; i < 7 && str[i]; i++) {
        switch (str[i]) {
            case 'I':
                dev->mode1 |= ELO_M1_INITIAL;
                break;
            case 'S':
                dev->mode1 |= ELO_M1_STREAM;
                break;
            case 'U':
                dev->mode1 |= ELO_M1_UNTOUCH;
                break;
            case 'T':
                dev->mode2 |= ELO_M2_TRACK;
                break;
            case 'P':
                dev->mode2 |= ELO_M2_TRIM;
                dev->mode1 |= ELO_M1_RANGECHK;
                dev->mode2 |= ELO_M2_CAL;
                break;
            case 'C':
                dev->mode2 |= ELO_M2_CAL;
                break;
            case 'M':
                dev->mode2 |= ELO_M2_SCALE;
                break;
            case 'B':
                dev->mode1 |= ELO_M1_RANGECHK;
                break;
            default:
                return; /* invalid char: rest of string ignored */
        }
    }
}

static void
elo_process_command(mouse_elo_t *dev, const uint8_t *body)
{
    uint8_t        cmd          = body[0];
    const uint8_t *data         = &body[1];
    bool           query        = islower((unsigned char) cmd) != 0;
    uint8_t        base         = (uint8_t) toupper((unsigned char) cmd);
    uint8_t        resp[7]      = { 0 };
    bool           has_response = true;

    switch (base) {
        case 'A':
            /* An Acknowledge query drains pending warnings and, per the
               manual, also interrupts a calibration that is waiting for
               touches -- which is the only way out of one short of a reset. */
            if (dev->cal_active) {
                dev->cal_active = false;
                elo_warn(dev, ELO_ERR_CALCANCEL);
            }
            elo_ack_warnings(dev);
            return;

        case 'T':
            /* Polling for touch data, used when automatic reporting is off.
               It answers in whatever format Emulate selected, the same as an
               unsolicited report would. */
            if (query) {
                bool    oor = false;
                int32_t x   = elo_transform(dev, 0, dev->raw_x, &oor);
                int32_t y   = elo_transform(dev, 1, dev->raw_y, &oor);
                uint8_t st  = dev->but ? ELO_M1_STREAM : ELO_M1_UNTOUCH;

                if (oor)
                    st |= ELO_ST_OUTOFRANGE;
                elo_send_touch(dev, st, x, y, elo_z_value(dev));
                elo_ack(dev, ELO_ERR_NONE);
            } else
                elo_ack(dev, ELO_ERR_NOSET);
            return;

        case 'R':
            if (data[0] & 1) { /* soft reset */
                elo_soft_reset(dev);
                resp[0] = data[0];
            } else { /* hard reset: no output until reset completes */
                dev->in_reset = true;
                timer_on_auto(&dev->reset_timer, 500. * 1000.);
                return;
            }
            break;

        case 'B':
            /* Both delays are in 10ms units: Untouch holds off reporting a
               release so a skip while sliding does not read as one, RepDelay
               paces the stream.  Applied in elo_poll and elo_prepare_transmit
               respectively. */
            if (!query) {
                dev->untouch_delay = data[0] & 0x0f;
                dev->rep_delay     = data[1];
                dev->rep_ready     = true; /* a new pace starts unblocked */
            }
            resp[0] = dev->untouch_delay;
            resp[1] = dev->rep_delay;
            break;

        case 'E':
            {
                /* Emulate: the touch data takes the shape of another controller's
                   while everything else stays SmartSet. */
                uint8_t tf  = data[0];
                uint8_t fmt = data[1];

                if (!query) {
                    if ((tf != '0') && (tf != '1')) {
                        elo_ack(dev, ELO_ERR_SUBCMD);
                        return;
                    }
                    /* Format takes an ASCII digit or the bare number. */
                    if ((fmt >= '0') && (fmt <= '7'))
                        fmt -= '0';
                    if (fmt > 7) {
                        elo_ack(dev, ELO_ERR_RANGE);
                        return;
                    }
                    dev->emul_touchflag = (tf == '1');
                    dev->emul_format    = fmt;

                    /* The E261-280 formats are defined over a 2..255 range, and
                       selecting one takes the scaling with it -- the manual is
                       explicit that whatever scaling was in use is lost. */
                    if ((fmt == ELO_FMT_280_BIN) || (fmt == ELO_FMT_280_ASC)) {
                        for (int a = 0; a < 2; a++) {
                            dev->scale_lo[a] = ELO_280_LO;
                            dev->scale_hi[a] = ELO_280_HI;
                        }
                        dev->mode2 |= ELO_M2_SCALE;
                    }
                }
                resp[0] = dev->emul_touchflag ? '1' : '0';
                resp[1] = (uint8_t) ('0' + dev->emul_format);
                break;
            }

        case 'C':
            if (data[0] == '2') {
                if (!query) {
                    dev->cal_active  = true;
                    dev->cal_step    = 0;
                    dev->cal_but_old = dev->but != 0;
                }
                resp[0] = '2';
            } else if (data[0] == 'S') {
                if (!query)
                    dev->swap_axes = (data[1] & 1) != 0;
                resp[0] = 'S';
                resp[1] = dev->swap_axes ? 1 : 0;
            } else if (data[0] == 'X' || data[0] == 'Y') {
                int axis = (data[0] == 'X') ? 0 : 1;
                if (!query) {
                    dev->cal_lo[axis] = data[1] | (data[2] << 8);
                    dev->cal_hi[axis] = data[3] | (data[4] << 8);
                }
                resp[0] = data[0];
                resp[1] = dev->cal_lo[axis] & 0xff;
                resp[2] = (dev->cal_lo[axis] >> 8) & 0xff;
                resp[3] = dev->cal_hi[axis] & 0xff;
                resp[4] = (dev->cal_hi[axis] >> 8) & 0xff;
            } else if (data[0] == 'x' || data[0] == 'y') {
                /* Offset/Numerator/Denominator form: HighPoint = Offset + Denominator
                 * (manual App. B); Numerator is fixed at 1 by our convention, both on
                 * the query response below and here on set, so the two stay symmetric. */
                int axis = (data[0] == 'x') ? 0 : 1;
                if (!query) {
                    int32_t offset    = (int16_t) (data[1] | (data[2] << 8));
                    int32_t denom     = (int16_t) (data[5] | (data[6] << 8));
                    dev->cal_lo[axis] = offset;
                    dev->cal_hi[axis] = offset + denom;
                    pclog("ELO: cal SET (o/n/d form) axis=%d lo=%d hi=%d\n", axis, dev->cal_lo[axis], dev->cal_hi[axis]);
                }
                int32_t offset = dev->cal_lo[axis];
                int32_t denom  = dev->cal_hi[axis] - dev->cal_lo[axis];
                resp[0]        = data[0]; /* the axis, echoed back as the manual has it */
                resp[1]        = offset & 0xff;
                resp[2]        = (offset >> 8) & 0xff;
                resp[3]        = 1;
                resp[4]        = 0;
                resp[5]        = denom & 0xff;
                resp[6]        = (denom >> 8) & 0xff;
            } else {
                resp[0] = data[0];
            }
            break;

        case 'D':
            if (!query)
                dev->diag_result = 0; /* all tests always pass */
            resp[0] = dev->diag_result;
            break;

        case 'F':
            if (!query) {
                dev->filter_slen    = data[1];
                dev->filter_width   = data[2];
                dev->filter_states  = data[3];
                dev->filter_control = data[4];
            }
            resp[0] = '0';
            resp[1] = dev->filter_slen;
            resp[2] = dev->filter_width;
            resp[3] = dev->filter_states;
            resp[4] = dev->filter_control;
            break;

        case 'G':
            /* Configuration: a dump of everything that can be set, as packets
               the host can hand straight back one at a time to restore it.
               Query only, and the count has to agree with ID's P byte. */
            if (!query) {
                elo_ack(dev, ELO_ERR_NOSET);
                return;
            }
            elo_dump_config(dev);
            has_response = false;
            break;

        case 'H':
            if (!query) {
                dev->timer_enable   = data[0] & 1;
                dev->timer_mode     = data[1] & 1;
                dev->timer_interval = data[2] | (data[3] << 8);
                elo_timer_apply(dev);
            }
            resp[0] = dev->timer_enable;
            resp[1] = dev->timer_mode;
            resp[2] = dev->timer_interval & 0xff;
            resp[3] = (dev->timer_interval >> 8) & 0xff;
            {
                /* Current: what is left to run, in the same 10ms ticks.  Zero
                   once it has expired, which is how a host that polls rather
                   than waits for the packet can tell. */
                uint16_t cur = 0;

                if (dev->timer_enable && timer_is_enabled(&dev->user_timer))
                    cur = (uint16_t) (timer_get_remaining_us(&dev->user_timer) / 10000);
                resp[4] = cur & 0xff;
                resp[5] = (cur >> 8) & 0xff;
            }
            break;

        case 'I':
            resp[0] = '0';                /* AccuTouch-class touchscreen */
            resp[1] = '0';                /* serial interface */
            resp[2] = 0x80;               /* features: Z reported (constant) */
            resp[3] = 1;                  /* firmware minor */
            resp[4] = 2;                  /* firmware major */
            resp[5] = ELO_CONFIG_PACKETS; /* what a 'g' query will send */
            resp[6] = 0;                  /* IFlag: E271-2200-class */
            break;

        case 'J':
            resp[0] = '0';                       /* AccuTouch */
            resp[1] = '0';                       /* serial */
            resp[2] = '0';                       /* booting from jumpers */
            resp[3] = '1';                       /* Stream mode */
            resp[4] = (uint8_t) (dev->ser1 & 7); /* baud index */
            resp[5] = 1;                         /* hardware handshaking enabled */
            resp[6] = 1;                         /* binary mode */
            break;

        case 'K':
            /* The Key multiplexes several controllers onto one line.  Setting
               a non-zero value switches every packet from here on -- starting
               with this command's own Acknowledge -- to the extended form. */
            if (!query)
                dev->key_value = data[0];
            resp[0] = dev->key_value;
            break;

        case 'L':
            if (!query)
                dev->low_power = data[0] & 1;
            resp[0] = dev->low_power;
            break;

        case 'M':
            if (!query) {
                if (data[0] == 0) {
                    dev->mode1 = data[1] | ELO_M1_ZRESERVED;
                    dev->mode2 = data[2];
                } else {
                    elo_process_mode_ascii(dev, data);
                }
            }
            /* The null in the first byte is the binary-form marker -- the same one
               the set path above tests for.  Without it, a host that queries the
               modes, flips a bit and sends the packet straight back (which is how
               the manual says to change one) would have mode1's value read as that
               discriminator and the rest parsed as ASCII mode letters. */
            resp[0] = 0;
            resp[1] = dev->mode1;
            resp[2] = dev->mode2;
            break;

        case 'N':
            {
                /* Direction picks save or restore, Areas which of the three
                   (Setup, Calibration, Scaling), and Page the primary or secondary
                   copy -- which only means anything for Calibration and Scaling,
                   there being one Setup area. */
                uint8_t areas = data[1] & (ELO_AREA_SETUP | ELO_AREA_CAL | ELO_AREA_SCALE);
                int     page  = data[2] & 1;

                if (query) {
                    elo_ack(dev, ELO_ERR_NOQUERY);
                    return;
                }
                if (!areas) {
                    elo_ack(dev, ELO_ERR_RANGE);
                    return;
                }
                if (data[0] & 1)
                    elo_save_nvr_areas(dev, areas, page);
                else {
                    elo_load_nvr_areas(dev, areas, page);
                    elo_timer_apply(dev);
                }
                resp[0] = data[0];
                resp[1] = data[1];
                resp[2] = data[2];
                break;
            }

        case 'O':
            if (!query)
                memcpy(dev->owner, data, 7);
            memcpy(resp, dev->owner, 7);
            break;

        case 'P':
            if (!query) {
                dev->ser1      = data[1];
                dev->ser2      = data[2];
                dev->baud_rate = elo_baud_table[dev->ser1 & 7];
                timer_stop(&dev->host_to_serial_timer);
                timer_on_auto(&dev->host_to_serial_timer, (1000000. / dev->baud_rate) * 10.);
            }
            resp[0] = '0';
            resp[1] = dev->ser1;
            resp[2] = dev->ser2;
            break;

        case 'Q':
            if (!query) {
                dev->quiet_all        = (data[0] & 1) != 0;
                dev->quiet_timer_flag = (data[0] & 2) != 0;
                dev->quiet_touch      = (data[0] & 4) != 0;
            }
            resp[0] = (dev->quiet_all ? 1 : 0) | (dev->quiet_timer_flag ? 2 : 0) | (dev->quiet_touch ? 4 : 0);
            break;

        case 'S':
            if (data[0] == 'I') {
                if (!query)
                    dev->invert_mask = data[1] & 0x07;
                resp[0] = 'I';
                resp[1] = dev->invert_mask;
            } else if (data[0] == 'X' || data[0] == 'Y') {
                int axis = (data[0] == 'X') ? 0 : 1;
                if (!query) {
                    dev->scale_lo[axis] = (int16_t) (data[1] | (data[2] << 8));
                    dev->scale_hi[axis] = (int16_t) (data[3] | (data[4] << 8));
                }
                resp[0] = data[0];
                resp[1] = dev->scale_lo[axis] & 0xff;
                resp[2] = (dev->scale_lo[axis] >> 8) & 0xff;
                resp[3] = dev->scale_hi[axis] & 0xff;
                resp[4] = (dev->scale_hi[axis] >> 8) & 0xff;
            } else if (data[0] == 'x' || data[0] == 'y') {
                /* Offset/Numerator/Denominator form: HighPoint = Offset + Numerator
                 * (manual App. B); Denominator is fixed at 1 by our convention, both
                 * on the query response below and here on set. */
                int axis = (data[0] == 'x') ? 0 : 1;
                if (!query) {
                    int32_t offset      = (int16_t) (data[1] | (data[2] << 8));
                    int32_t numer       = (int16_t) (data[3] | (data[4] << 8));
                    dev->scale_lo[axis] = offset;
                    dev->scale_hi[axis] = offset + numer;
                    pclog("ELO: scale SET (o/n/d form) axis=%d lo=%d hi=%d\n", axis, dev->scale_lo[axis], dev->scale_hi[axis]);
                }
                int32_t offset = dev->scale_lo[axis];
                int32_t numer  = dev->scale_hi[axis] - dev->scale_lo[axis];
                resp[0]        = data[0]; /* the axis, echoed back as the manual has it */
                resp[1]        = offset & 0xff;
                resp[2]        = (offset >> 8) & 0xff;
                resp[3]        = numer & 0xff;
                resp[4]        = (numer >> 8) & 0xff;
                resp[5]        = 1;
                resp[6]        = 0;
            } else {
                resp[0] = data[0];
            }
            break;

        default:
            has_response = false;
            elo_ack(dev, '5'); /* illegal command */
            return;
    }

    if (query && has_response)
        elo_enqueue(dev, base, resp);
    elo_ack(dev, 0);
}

/* Incoming framing.  A 'U' lead-in is the ordinary packet and every controller
   on the line takes it; a Control-V lead-in carries a key byte after the
   command, and only the controller holding that key acts on it.  The checksum
   is only enforced if the host asked for it with the Parameter command --
   factory default is not to, and the drivers rely on that. */
static void
elo_write(UNUSED(serial_t *serial), void *priv, uint8_t data)
{
    mouse_elo_t *dev = (mouse_elo_t *) priv;

    if (dev->cmd_pos < 0) {
        if (data == 0x55) {
            dev->cmd_pos  = 0;
            dev->in_keyed = false;
        } else if (data == 0x16) {
            dev->cmd_pos  = 0;
            dev->in_keyed = true;
        }
        return;
    }

    if (dev->cmd_pos < 8) {
        dev->body[dev->cmd_pos++] = data;
        return;
    }

    if (dev->in_keyed && (dev->cmd_pos == 8)) {
        dev->in_key = data; /* the key this packet is addressed to */
        dev->cmd_pos++;
        return;
    }

    /* The trailing checksum. */
    dev->cmd_pos = -1;

    if (dev->ser2 & 1) {
        uint8_t sum = 0xAA + (dev->in_keyed ? 0x16 : 0x55);

        for (int i = 0; i < 8; i++)
            sum += dev->body[i];
        if (dev->in_keyed)
            sum += dev->in_key;
        if (sum != data) {
            elo_ack(dev, ELO_ERR_CHECKSUM);
            return;
        }
    }

    /* A keyed packet is for one controller; an unkeyed one is for all of
       them.  With no key set, a keyed packet cannot be addressed to us. */
    if (dev->in_keyed && (dev->in_key != dev->key_value))
        return;

    elo_process_command(dev, dev->body);
}

/* An ASCII coordinate as the SmartSet ASCII format writes them: at least four
   digits, zero padded, a minus sign where needed and never a plus. */
static int
elo_ascii_coord(char *out, size_t sz, int32_t v)
{
    if (v < 0)
        return snprintf(out, sz, "-%04d", -v);
    return snprintf(out, sz, "%04d", v);
}

/* Emit one touch report in whichever format the Emulate command selected.
   Only the touch data changes; commands and their responses stay SmartSet. */
static void
elo_send_touch(mouse_elo_t *dev, uint8_t status, int32_t x, int32_t y, int32_t z)
{
    const bool untouch = (status & ELO_M1_UNTOUCH) != 0;
    uint8_t    buf[32];
    char       txt[48];
    int        n;

    switch (dev->emul_format) {
        default:
        case ELO_FMT_2200_BIN:
            {
                /* The native packet. */
                uint8_t data[7];
                int16_t xs = (int16_t) x;
                int16_t ys = (int16_t) y;
                int16_t zs = (int16_t) z;

                data[0] = status;
                data[1] = xs & 0xff;
                data[2] = (xs >> 8) & 0xff;
                data[3] = ys & 0xff;
                data[4] = (ys >> 8) & 0xff;
                data[5] = zs & 0xff;
                data[6] = (zs >> 8) & 0xff;
                elo_enqueue(dev, 'T', data);
                return;
            }

        case ELO_FMT_140_BIN:
        case ELO_FMT_4002_BIN:
            /* Four bytes of 12-bit X and Y, each byte carrying its position in
               the top two bits so the host can find the start of a packet; the
               4002 form adds Z in the same shape.  Z is a 4-bit number, so its
               high byte is always zero. */
            buf[0] = (uint8_t) (0xc0 | ((x >> 6) & 0x3f));
            buf[1] = (uint8_t) (0x80 | (x & 0x3f));
            buf[2] = (uint8_t) (0x40 | ((y >> 6) & 0x3f));
            buf[3] = (uint8_t) (0x00 | (y & 0x3f));
            n      = 4;
            if (dev->emul_format == ELO_FMT_4002_BIN) {
                int32_t zz = z;

                /* With the touch flag on, an untouch is signalled by Z = 0. */
                if (dev->emul_touchflag && untouch)
                    zz = 0;
                buf[4] = (uint8_t) ((zz >> 6) & 0x3f);
                buf[5] = (uint8_t) (zz & 0x3f);
                n      = 6;
            }
            elo_emit_raw(dev, buf, n);
            return;

        case ELO_FMT_140_ASC:
        case ELO_FMT_4002_ASC:
            n = snprintf(txt, sizeof(txt), "\r\n%04d %04d", (int) x, (int) y);
            if (dev->emul_format == ELO_FMT_4002_ASC) {
                int32_t zz = (dev->emul_touchflag && untouch) ? 0 : z;

                n += snprintf(txt + n, sizeof(txt) - n, " %04d", (int) zz);
            } else if (dev->emul_touchflag)
                n += snprintf(txt + n, sizeof(txt) - n, " %c", untouch ? 'U' : 'T');
            elo_emit_raw(dev, (const uint8_t *) txt, n);
            return;

        case ELO_FMT_280_BIN:
            /* Three bytes: a start-of-header, then one byte per axis.  With
               the touch flag on, an untouch swaps the header for 81h. */
            buf[0] = (dev->emul_touchflag && untouch) ? 0x81 : 0x01;
            buf[1] = (uint8_t) x;
            buf[2] = (uint8_t) y;
            elo_emit_raw(dev, buf, 3);
            return;

        case ELO_FMT_280_ASC:
            /* Uppercase hex pairs. */
            if (dev->emul_touchflag)
                n = snprintf(txt, sizeof(txt), "%02X,%02X,%c\r\n",
                             (unsigned) (x & 0xff), (unsigned) (y & 0xff), untouch ? 'U' : 'T');
            else
                n = snprintf(txt, sizeof(txt), "%02X,%02X\r\n",
                             (unsigned) (x & 0xff), (unsigned) (y & 0xff));
            elo_emit_raw(dev, (const uint8_t *) txt, n);
            return;

        case ELO_FMT_2200_ASC:
            {
                char xs[16];
                char ys[16];
                char zs[16];

                elo_ascii_coord(xs, sizeof(xs), x);
                elo_ascii_coord(ys, sizeof(ys), y);
                elo_ascii_coord(zs, sizeof(zs), z);
                if (dev->emul_touchflag)
                    n = snprintf(txt, sizeof(txt), "%s %s %s %c\r\n", xs, ys, zs, untouch ? 'U' : 'T');
                else
                    n = snprintf(txt, sizeof(txt), "%s %s %s\r\n", xs, ys, zs);
                elo_emit_raw(dev, (const uint8_t *) txt, n);
                return;
            }
    }
}

/* The Z the host is told about.  There is no pressure to measure, and the
   manual has Z pinned to the top of its range rather than sensed, so it is a
   constant: 255, or the 4 bits the 4002 binary format has room for.

   Real hardware scales Z on its own axis, which this does not model -- there
   is nothing meaningful to scale.  Tying it to the X range instead would just
   invent a coupling that hardware does not have. */
static int32_t
elo_z_value(mouse_elo_t *dev)
{
    return (dev->emul_format == ELO_FMT_4002_BIN) ? 15 : 255;
}

static void
elo_prepare_transmit(mouse_elo_t *dev)
{
    uint8_t status = 0;
    bool    send   = false;
    bool    stream = false;
    bool    oor    = false;
    int32_t rx, ry, x, y;

    /* Nothing goes out mid-calibration, and nothing while the Untouch delay is
       deciding whether the finger really left the screen. */
    if (dev->cal_active || dev->untouch_pending)
        return;
    if (!dev->but && !dev->but_old)
        return;

    if (dev->but && !dev->but_old) {
        if (dev->mode1 & ELO_M1_INITIAL) {
            status |= ELO_M1_INITIAL;
            send = true;
        } else if (dev->mode1 & ELO_M1_STREAM) {
            status |= ELO_M1_STREAM;
            send   = true;
            stream = true;
        }
    } else if (dev->but && dev->but_old) {
        if (dev->mode1 & ELO_M1_STREAM) {
            status |= ELO_M1_STREAM;
            send   = true;
            stream = true;
        }
    } else {
        if (dev->mode1 & ELO_M1_UNTOUCH) {
            status |= ELO_M1_UNTOUCH;
            send = true;
        }
    }

    /* RepDelay paces the stream without touching the baud rate.  Edges are not
       paced: an initial touch or an untouch is a state change the host needs. */
    if (send && stream && dev->rep_delay && !dev->rep_ready)
        send = false;

    rx = dev->but ? dev->raw_x : dev->raw_x_old;
    ry = dev->but ? dev->raw_y : dev->raw_y_old;
    x  = elo_transform(dev, 0, rx, &oor);
    y  = elo_transform(dev, 1, ry, &oor);

    /* Tracking drops a Stream packet that would repeat the coordinate already
       sent.  It only makes sense against Stream, which is why it is tested
       here rather than at the top. */
    if (send && stream && (dev->mode2 & ELO_M2_TRACK) && dev->have_sent && (x == dev->sent_x) && (y == dev->sent_y))
        send = false;

    if (oor)
        status |= ELO_ST_OUTOFRANGE;

    if (send && !dev->quiet_touch) {
        elo_send_touch(dev, status, x, y, elo_z_value(dev));

        dev->have_sent = true;
        dev->sent_x    = x;
        dev->sent_y    = y;

        if (stream && dev->rep_delay) {
            dev->rep_ready = false;
            timer_on_auto(&dev->rep_delay_timer, dev->rep_delay * 10000.0);
        }
    }

    dev->raw_x_old = dev->raw_x;
    dev->raw_y_old = dev->raw_y;
    dev->but_old   = dev->but;
    if (!dev->but)
        dev->have_sent = false;
}

static void
elo_write_to_host(void *priv)
{
    mouse_elo_t *dev = (mouse_elo_t *) priv;

    if (dev->serial == NULL)
        goto no_write;
    if (!serial_port_can_receive(dev->serial))
        goto no_write;
    if (dev->in_reset)
        goto no_write;

    if (fifo8_num_used(&dev->resp))
        serial_port_receive(dev->serial, fifo8_pop(&dev->resp));
    else
        elo_prepare_transmit(dev);

no_write:
    timer_on_auto(&dev->host_to_serial_timer, (1000000.0 / (double) dev->baud_rate) * 10.0);
}

static int
elo_poll(void *priv)
{
    mouse_elo_t *dev = (mouse_elo_t *) priv;
    double       abs_x, abs_y;

    dev->but = tablet_get_buttons_ex();
    mouse_get_abs_coords(&abs_x, &abs_y);

    if (enable_overscan && mouse_tablet_in_proximity > 0) {
        int index = mouse_tablet_in_proximity - 1;

        abs_x *= monitors[index].mon_unscaled_size_x - 1;
        abs_y *= monitors[index].mon_efscrnsz_y - 1;

        if (abs_x <= (monitors[index].mon_overscan_x / 2.))
            abs_x = (monitors[index].mon_overscan_x / 2.);
        if (abs_y <= (monitors[index].mon_overscan_y / 2.))
            abs_y = (monitors[index].mon_overscan_y / 2.);
        abs_x -= (monitors[index].mon_overscan_x / 2.);
        abs_y -= (monitors[index].mon_overscan_y / 2.);
        abs_x = abs_x / (double) monitors[index].mon_xsize;
        abs_y = abs_y / (double) monitors[index].mon_ysize;
    }

    if (abs_x >= 1.0)
        abs_x = 1.0;
    if (abs_y >= 1.0)
        abs_y = 1.0;
    if (abs_x <= 0.0)
        abs_x = 0.0;
    if (abs_y <= 0.0)
        abs_y = 0.0;

    if (dev->swap_axes) {
        double t = abs_x;
        abs_x    = abs_y;
        abs_y    = t;
    }

    dev->raw_x = (int32_t) lround(abs_x * ELO_RAW_MAX);
    dev->raw_y = (int32_t) lround(abs_y * ELO_RAW_MAX);

    /* The Untouch delay: hold a release for the configured time in case the
       finger comes back, which is what it is for -- a skip while sliding
       should not read as letting go.  Nothing is transmitted while the hold is
       running, so if the touch does resume the gap simply closes and it stays
       one continuous touch rather than becoming a new one. */
    if (dev->but) {
        if (dev->untouch_pending) {
            timer_stop(&dev->untouch_timer);
            dev->untouch_pending = false;
        }
    } else if (dev->but_poll_prev && dev->but_old && dev->untouch_delay && !dev->untouch_pending) {
        dev->untouch_pending = true;
        timer_on_auto(&dev->untouch_timer, dev->untouch_delay * 10000.0);
    }
    dev->but_poll_prev = dev->but != 0;

    if (dev->cal_active) {
        bool but_now = dev->but != 0;
        if (but_now && !dev->cal_but_old) {
            dev->cal_points[dev->cal_step][0] = dev->raw_x;
            dev->cal_points[dev->cal_step][1] = dev->raw_y;
            dev->cal_step++;
            elo_ack(dev, 0);
            if (dev->cal_step >= 2) {
                dev->cal_lo[0]  = dev->cal_points[0][0];
                dev->cal_hi[0]  = dev->cal_points[1][0];
                dev->cal_lo[1]  = dev->cal_points[0][1];
                dev->cal_hi[1]  = dev->cal_points[1][1];
                dev->cal_active = false;
            }
        }
        dev->cal_but_old = but_now;
    }

    return 0;
}

/* The rate the controller reports, and accepts, as an index into its own table.
   Falls back to 9600 for anything not in it. */
static int
elo_baud_index(int baud)
{
    for (uint8_t i = 0; i < 8; i++)
        if (elo_baud_table[i] == baud)
            return i;
    return 5;
}

static void *
elo_init(UNUSED(const device_t *info))
{
    mouse_elo_t *dev = calloc(1, sizeof(mouse_elo_t));

    dev->cmd_pos = -1;
    dev->serial  = serial_attach(device_get_config_int("port"), NULL, elo_write, dev);
    /* Where the line starts.  'P' can still move it from the guest; this is the
       power-on rate, and ser1's low three bits are the same rate as the controller
       reports it back, so the two have to be set together. */
    dev->baud_rate = device_get_config_int("speed");
    if (dev->baud_rate <= 0)
        dev->baud_rate = 9600;
    dev->ser1 = (uint8_t) elo_baud_index(dev->baud_rate); /* rate index, 8N1 */
    dev->ser2 = 0x04;                                     /* hardware handshaking enabled */
    if (dev->serial) {
        serial_port_set_line(dev->serial, CHAR_COM_CTS, 1);
        serial_port_set_line(dev->serial, CHAR_COM_DSR, 1);
        serial_port_set_line(dev->serial, CHAR_COM_DCD, 1);
    }

    fifo8_create(&dev->resp, 512);
    timer_add(&dev->host_to_serial_timer, elo_write_to_host, dev, 0);
    timer_add(&dev->reset_timer, elo_reset_complete, dev, 0);
    timer_add(&dev->rep_delay_timer, elo_rep_delay_done, dev, 0);
    timer_add(&dev->untouch_timer, elo_untouch_done, dev, 0);
    timer_add(&dev->user_timer, elo_user_timer_fire, dev, 0);
    timer_on_auto(&dev->host_to_serial_timer, (1000000. / dev->baud_rate) * 10.);

    dev->rep_ready = true;

    /* Factory defaults, then whatever NVRAM has saved over the top. */
    snprintf(dev->nvr_path, sizeof(dev->nvr_path), "elo_smartset.nvr");
    elo_load_nvr(dev);
    elo_timer_apply(dev);

    /* The line speed is the one the user picked, not one a stale NVRAM saved:
       the setting in the dialog would otherwise appear to do nothing.  A
       restore asked for by the guest still applies whatever was saved. */
    dev->ser1      = (uint8_t) ((dev->ser1 & ~7) | elo_baud_index(dev->baud_rate));
    dev->baud_rate = elo_baud_table[dev->ser1 & 7];

    mouse_set_buttons(2);
    mouse_set_poll_ex(elo_poll, dev);

    return dev;
}

static void
elo_close(void *priv)
{
    mouse_elo_t *dev = (mouse_elo_t *) priv;

    timer_stop(&dev->host_to_serial_timer);
    timer_stop(&dev->reset_timer);
    timer_stop(&dev->rep_delay_timer);
    timer_stop(&dev->untouch_timer);
    timer_stop(&dev->user_timer);
    if (dev->serial)
        serial_port_detach(dev->serial);

    fifo8_destroy(&dev->resp);
    free(dev);
}

static const device_config_t elo_config[] = {
    // clang-format off
    {
        .name           = "port",
        .description    = "Serial Port",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "COM1", .value = 0 },
            { .description = "COM2", .value = 1 },
            { .description = "COM3", .value = 2 },
            { .description = "COM4", .value = 3 },
            { .description = ""                 }
        },
        .bios           = { { 0 } }
    },
    {
        /* Where the line starts.  The controller's own 'P' command can change it at
           runtime, so this is the power-on rate rather than a fixed one.  The
           usual default is 9600. */
        .name           = "speed",
        .description    = "Speed",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 9600,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "300 baud",   .value =   300 },
            { .description = "600 baud",   .value =   600 },
            { .description = "1200 baud",  .value =  1200 },
            { .description = "2400 baud",  .value =  2400 },
            { .description = "4800 baud",  .value =  4800 },
            { .description = "9600 baud",  .value =  9600 },
            { .description = "19200 baud", .value = 19200 },
            { .description = "38400 baud", .value = 38400 },
            { .description = ""                           }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t mouse_elo_device = {
    .name          = "Elo TouchSystems SmartSet E271-2200 / E271-2210 (Serial)",
    .internal_name = "elo_touchscreen",
    .flags         = DEVICE_COM,
    .local         = 0,
    .init          = elo_init,
    .close         = elo_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = elo_config
};
