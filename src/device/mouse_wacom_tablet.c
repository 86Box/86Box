#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/mouse.h>
#include <86box/serial.h>
#include <86box/plat.h>
#include <86box/fifo8.h>

#define FLAG_3BTN 0x20 /* enable 3-button mode */

extern double cpuclock;

enum wacom_modes {
    WACOM_MODE_SUPPRESSED = 0,
    WACOM_MODE_POINT      = 1,
    WACOM_MODE_STREAM     = 2,
    WACOM_MODE_SWITCH     = 3,
};

/* Operation mode DIP value for remote request; only used by the config. */
#define WACOM_DIP_MODE_REMOTE 4

/* SD-510C command set DIP selection (DS1-1,2 plus the MM tablet selection). */
enum sd510_cmd_set {
    SD510_CMDSET_WACOM_IIS = 0,
    SD510_CMDSET_WACOM_II  = 1,
    SD510_CMDSET_BITPAD    = 2,
    SD510_CMDSET_MM1201    = 3,
    SD510_CMDSET_MM961     = 4
};

/* Summagraphics emulations. */
enum summa_set {
    SUMMA_NONE = 0,
    SUMMA_BITPAD,  /* Bit Pad Two */
    SUMMA_MM1201,
    SUMMA_MM961
};

enum summa_mode {
    SUMMA_MODE_STREAM = 0,
    SUMMA_MODE_SWITCH,
    SUMMA_MODE_POINT,
    SUMMA_MODE_REMOTE
};

/* Emulated SD-510C active area, in inches (4566 x 2972 counts at 500 lpi). */
#define SD510_WIDTH_IN  (4566.0 / 500.0)
#define SD510_HEIGHT_IN (2972.0 / 500.0)

enum wacom_handshake_modes {
    WACOM_HANDSHAKE_NONE = 0,
    WACOM_HANDSHAKE_CTS  = 1,
    WACOM_HANDSHAKE_DTS  = 2,
    WACOM_HANDSHAKE_BOTH = 3, 
};

enum wacom_cmd_set {
    WACOM_CMDSET_BITPAD = 0,
    WACOM_CMDSET_MM1201 = 1,
    WACOM_CMDSET_IIS    = 2,
    WACOM_CMDSET_IV     = 3
};

enum wacom_tablet_type {
    WACOM_TYPE_IISONLY = 0,
    WACOM_TYPE_IV,
};

enum {
    REPORT_PHASE_PREPARE,
    REPORT_PHASE_TRANSMIT
};

typedef struct wacom_tablet_id {
    char id[64];
    int type;
} wacom_tablet_id;

static const wacom_tablet_id sd510_id = {
    .id = "~#SD51C V3.2.1.01\r",
    .type = WACOM_TYPE_IISONLY
};

static const wacom_tablet_id artpad_id = {
    .id = "~#KT-0405-R00 V1.1-0\r",
    .type = WACOM_TYPE_IV
};

static const uint32_t wacom_resolution_values[4] = {
    500,
    508,
    1000,
    1270
};

typedef struct mouse_wacom_t {
    const char *name; /* name of this device */
    int8_t      type; /* type of this device */
    int8_t      port;
    uint8_t     flags; /* device flags */
    uint8_t     status;
    uint8_t     bits;
    uint8_t     data_rec[0x200];
    int         abs_x;
    int         abs_y;
    int         relative_origin_valid;
    int         oldb;
    int         b;

    Fifo8 data;

    int data_rec_pos;
    int mode;
    int interval;
    int increment;
    int suppressed_increment;
    int transmission_stopped;
    int reset;
    int transmit_id;
    int transmit_id_pending;
    int pressure_mode;
    int suppressed;
    int measurement;
    int remote_req;
    int baud;

    /* SD-510C DIP switch settings, applied on power-up and on reset. */
    struct {
        int data_format;     /* DS1-3:    1 = ASCII, 0 = binary */
        int operation_mode;  /* DS1-4,5:  enum wacom_modes */
        int origin_type;     /* DS1-6:    1 = relative, 0 = absolute */
        int unit;            /* DS1-7:    1 = inches, 0 = millimeters */
        int always_transmit; /* DS1-8 */
        int baud;            /* DS2-1..3 */
        int parity;          /* DS2-4,5:  0 = none, 2 = odd, 3 = even */
        int stop_bits;       /* DS2-6:    1 or 2 */
        int dsr_monitor;     /* DS2-7 */
        int data_bits;       /* DS2-8:    7 or 8 */
        int transfer_rate;   /* DS3-1..3: points per second, 0 = maximum */
        int origin_location; /* DS3-4:    1 = lower left, 0 = upper left */
        int terminator;      /* DS3-6,7:  0 = CR, 1 = LF, 2 = CR/LF */
        int cmd_set;         /* DS1-1,2:  enum sd510_cmd_set */
        int resolution;      /* Bit Pad Two / MM resolution in lpi, 0 = default */
        int cursor_code;     /* Bit Pad Two cursor output code: 0 = A, 1 = B */
    } dip;
    int has_dip;

    /* Summagraphics Bit Pad Two and MM 961/1201 emulation state. */
    struct {
        int     set;          /* enum summa_set, SUMMA_NONE = WACOM II/II-S */
        int     binary;       /* 1 = packed binary, 0 = ASCII BCD */
        int     mode;         /* enum summa_mode */
        int     relative;     /* Bit Pad relative mode, MM delta mode */
        int     rate;         /* reports per second, 0 = maximum */
        int     increment;    /* increment mode distance in counts */
        int     axis_update;  /* MM axis update distance in counts */
        int     res_x;        /* lines per inch */
        int     res_y;
        int     origin_upper; /* MM 1201: origin in the upper left */
        int     horizontal;   /* MM 961: horizontal (9" x 6") orientation */
        int     tablet_id;    /* MM tablet identifier, 0 or 1 */
        int     always;       /* also report while out of proximity */
        int     crlf;         /* Bit Pad ASCII terminator is CR LF */
        int     output_b;     /* Bit Pad cursor output code B */
        int     echo;         /* MM echo diagnostic */
        int     test_result;  /* MM stored self test result byte */
        uint8_t cmd;          /* MM command awaiting argument bytes */
        uint8_t args[4];
        int     nargs;
        int     args_needed;
        int     trigger;      /* remote request pending */
        int     point_latch;  /* button pressed since the last point report */
        int     prev_b;
        int     last_b;       /* buttons in the last report */
        int     last_x;       /* last reported position, in counts */
        int     last_y;
        int     have_last;
        int     prox_x;       /* last in-proximity position, in counts */
        int     prox_y;
        int     was_prox;
        int     out_prox_left; /* MM: out-of-proximity reports still to send */
        double  last_tsc;
    } summa;

    uint32_t               x_res;
    uint32_t               y_res;
    const wacom_tablet_id *tablet_type;

    int last_abs_x; /* Suppressed/Increment Mode. */
    int last_abs_y; /* Suppressed/Increment Mode. */
    union {
        uint32_t settings; /* Settings DWORD */
        /* We don't target any architectures except x64/ARM64.
           (The ABIs for those are explicit in little-endian bit ordering) */
        struct settings_bits {
            uint8_t remote_mode            : 1;
            uint8_t bitpad_two_cursor_data : 1;
            uint8_t mm961_orientation      : 1;
            uint8_t mm_command_set         : 1;
            uint8_t tilt                   : 1;
            uint8_t multi_device           : 1;
            uint8_t reading_height         : 1;
            uint8_t pressure_sensitivity   : 1;

            uint8_t pnp                    : 1; /* Unused. */
            uint8_t dummy                  : 1;
            uint8_t terminator             : 2;
            uint8_t out_of_range_data      : 1;
            uint8_t origin_location        : 1;
            uint8_t resolution             : 2;

            uint8_t transfer_rate          : 2;
            uint8_t coord_sys              : 1;
            uint8_t output_format          : 1;
            uint8_t transfer_mode          : 2;
            uint8_t handshake              : 2;

            uint8_t stop_bits_conf         : 1;
            uint8_t data_bits_conf         : 1;
            uint8_t parity                 : 2;
            uint8_t baud_rate              : 2;
            uint8_t cmd_set                : 2;
        } settings_bits;
    };

    double     transmit_period;
    double     old_tsc;
    double     reset_tsc;
    pc_timer_t report_timer;

    ser_port_t *serial;
} mouse_wacom_t;

/* TODO: What is this needed for? */
#if 0
static unsigned int
reverse(register unsigned int x)
{
    x = (((x & 0xaaaaaaaa) >> 1) | ((x & 0x55555555) << 1));
    x = (((x & 0xcccccccc) >> 2) | ((x & 0x33333333) << 2));
    x = (((x & 0xf0f0f0f0) >> 4) | ((x & 0x0f0f0f0f) << 4));
    x = (((x & 0xff00ff00) >> 8) | ((x & 0x00ff00ff) << 8));
    return ((x >> 16) | (x << 16));
}
#endif

static double
wacom_transmit_period(mouse_wacom_t *dev, int bps, int rps)
{
    double dbps     = (double) bps;
    double temp     = 0.0;
    int    word_len = dev->bits;

    if (rps == -1)
        temp = (double) word_len;
    else {
        temp = (double) rps;
        temp = (9600.0 - (temp * 33.0));
        temp /= rps;
    }
    temp = (1000000.0 / dbps) * temp;

    return temp;
}

/* Recompute the serial frame length and per-character period from the
   current data bits, parity, stop bits and baud rate. */
static void
wacom_update_frame(mouse_wacom_t *wacom)
{
    wacom->bits = 1 + 7 + wacom->settings_bits.data_bits_conf;   /* start + data */
    wacom->bits += 1 + wacom->settings_bits.stop_bits_conf;      /* stop */
    if (wacom->settings_bits.parity & 2)
        wacom->bits++;                                           /* parity */

    wacom->transmit_period = wacom_transmit_period(wacom, wacom->baud, -1);
}

static void
wacom_process_settings_dword(mouse_wacom_t *wacom, uint32_t dword)
{
    static const int bauds[4] = { 2400, 4800, 9600, 19200 };

    wacom->settings = dword;

    wacom->mode = wacom->settings_bits.transfer_mode;
    wacom->baud = bauds[wacom->settings_bits.baud_rate];
    wacom_update_frame(wacom);

    wacom->relative_origin_valid = 0;
    wacom->x_res = wacom->y_res = wacom_resolution_values[wacom->settings_bits.resolution];
}

/* Apply the SD-510C DIP switches (read by the tablet at power-up and reset). */
static void
wacom_apply_dip(mouse_wacom_t *wacom)
{
    if (!wacom->has_dip)
        return;

    int remote = (wacom->dip.operation_mode == WACOM_DIP_MODE_REMOTE);
    int mode   = remote ? WACOM_MODE_STREAM : wacom->dip.operation_mode;

    wacom->settings_bits.output_format     = wacom->dip.data_format;
    wacom->settings_bits.transfer_mode     = mode;
    wacom->mode                            = mode;
    wacom->settings_bits.remote_mode       = remote;
    wacom->remote_req                      = 0;
    wacom->settings_bits.coord_sys         = wacom->dip.origin_type;
    wacom->measurement                     = wacom->dip.unit;
    wacom->settings_bits.out_of_range_data = wacom->dip.always_transmit;
    wacom->settings_bits.parity            = wacom->dip.parity;
    wacom->settings_bits.stop_bits_conf    = (wacom->dip.stop_bits == 2);
    wacom->settings_bits.data_bits_conf    = (wacom->dip.data_bits == 8);
    wacom->settings_bits.handshake         = wacom->dip.dsr_monitor ? WACOM_HANDSHAKE_DTS : WACOM_HANDSHAKE_NONE;
    wacom->settings_bits.origin_location   = wacom->dip.origin_location;
    wacom->settings_bits.terminator        = wacom->dip.terminator;
    wacom->baud                            = wacom->dip.baud;

    /* The report interval is kept in 5 ms units (IT command). */
    wacom->interval = wacom->dip.transfer_rate ? ((200 + (wacom->dip.transfer_rate / 2)) / wacom->dip.transfer_rate) : 0;

    /* Suppressed mode reports on any coordinate change by default. */
    wacom->suppressed_increment = (wacom->mode == WACOM_MODE_SUPPRESSED) ? 1 : 0;

    wacom->relative_origin_valid = 0;
    wacom_update_frame(wacom);

    /* Summagraphics command sets. */
    memset(&wacom->summa, 0, sizeof(wacom->summa));
    switch (wacom->dip.cmd_set) {
        case SD510_CMDSET_BITPAD:
            wacom->summa.set = SUMMA_BITPAD;
            break;
        case SD510_CMDSET_MM1201:
            wacom->summa.set = SUMMA_MM1201;
            break;
        case SD510_CMDSET_MM961:
            wacom->summa.set = SUMMA_MM961;
            break;
        default:
            wacom->summa.set = SUMMA_NONE;
            break;
    }
    wacom->settings_bits.cmd_set = WACOM_CMDSET_IIS;
    if (wacom->summa.set == SUMMA_NONE)
        return;

    wacom->settings_bits.cmd_set = (wacom->summa.set == SUMMA_BITPAD) ? WACOM_CMDSET_BITPAD : WACOM_CMDSET_MM1201;
    wacom->settings_bits.mm_command_set = (wacom->summa.set == SUMMA_MM961);
    wacom->settings_bits.remote_mode    = 0;

    wacom->summa.binary   = !wacom->dip.data_format;
    wacom->summa.relative = wacom->dip.origin_type;
    wacom->summa.rate     = wacom->dip.transfer_rate;
    wacom->summa.always   = wacom->dip.always_transmit;
    wacom->summa.crlf     = (wacom->dip.terminator == 2);
    wacom->summa.output_b = wacom->dip.cursor_code;
    switch (wacom->dip.operation_mode) {
        case WACOM_MODE_POINT:
            wacom->summa.mode = SUMMA_MODE_POINT;
            break;
        case WACOM_MODE_SWITCH:
            wacom->summa.mode = SUMMA_MODE_SWITCH;
            break;
        case WACOM_DIP_MODE_REMOTE:
            wacom->summa.mode = SUMMA_MODE_REMOTE;
            break;
        case WACOM_MODE_SUPPRESSED:
            /* No suppressed mode here; stream without redundant reports. */
            wacom->summa.mode      = SUMMA_MODE_STREAM;
            wacom->summa.increment = 1;
            break;
        default:
            wacom->summa.mode = SUMMA_MODE_STREAM;
            break;
    }
    if (wacom->dip.resolution)
        wacom->summa.res_x = wacom->dip.resolution;
    else
        wacom->summa.res_x = (wacom->summa.set == SUMMA_BITPAD) ? 200 : 500;
    wacom->summa.res_y       = wacom->summa.res_x;
    wacom->summa.test_result = 0x87; /* all tests passed */
}

static void
wacom_reset(mouse_wacom_t *wacom)
{
    wacom->transmit_period             = wacom_transmit_period(wacom, 9600, -1);
    wacom->mode                        = WACOM_MODE_POINT;
    wacom->transmission_stopped        = 0;
    wacom->interval                    = 0;
    wacom->transmit_id                 = 0;
    wacom->settings_bits.output_format = 1; /* ASCII */
    wacom->settings_bits.cmd_set       = 1;
    wacom->measurement                 = 1;
    wacom->increment = wacom->suppressed_increment = 0;
    wacom->reset_tsc                               = tsc;
    wacom->settings_bits.remote_mode = wacom->remote_req = 0;
    wacom->settings_bits.out_of_range_data               = 0;

    wacom_process_settings_dword(wacom, 0xA21BC800);
    wacom_apply_dip(wacom);
}

static void
wacom_reset_artpad(mouse_wacom_t *wacom)
{
    wacom->transmit_period                 = wacom_transmit_period(wacom, 9600, -1);
    wacom->mode                            = WACOM_MODE_SUPPRESSED;
    wacom->transmission_stopped            = 0;
    wacom->interval                        = 0;
    wacom->transmit_id                     = 0;
    wacom->settings_bits.output_format     = 0; /* Binary */
    wacom->measurement                     = 1;
    wacom->increment                       = 0; 
    wacom->suppressed_increment            = 1;
    wacom->reset_tsc                       = tsc;
    wacom->settings_bits.remote_mode       = 0;
    wacom->remote_req                      = 0;
    wacom->settings_bits.out_of_range_data = 0;

    wacom_process_settings_dword(wacom, 0xE203C000);
}

static void
wacom_callback(UNUSED(struct serial_s *serial), void *priv)
{
    mouse_wacom_t *wacom = (mouse_wacom_t *) priv;

    wacom->transmit_period = wacom_transmit_period(wacom, wacom->baud, -1);
    timer_stop(&wacom->report_timer);
    timer_on_auto(&wacom->report_timer, wacom->transmit_period);
}

/* ---- Summagraphics Bit Pad Two and MM 961/1201 command sets ------------- */

static void
summa_push_str(mouse_wacom_t *wacom, const char *str)
{
    fifo8_push_all(&wacom->data, (const uint8_t *) str, (uint32_t) strlen(str));
}

/* Bit Pad Two and MM 1201 origin is the lower left corner. The MM 1201 can
   move it to the upper left; the MM 961 is 6" x 9" (vertical) by default
   and 9" x 6" when set horizontal, which scales the area accordingly. */
static void
summa_get_position(mouse_wacom_t *wacom, int *x, int *y)
{
    double w  = SD510_WIDTH_IN;
    double h  = SD510_HEIGHT_IN;
    double fx;
    double fy;

    /* Read the pointer now: the poll callback can lag behind a pen that
       has just come into proximity. */
    mouse_get_abs_coords(&fx, &fy);
    fx = (fx < 0.0) ? 0.0 : ((fx > 1.0) ? 1.0 : fx);
    fy = (fy < 0.0) ? 0.0 : ((fy > 1.0) ? 1.0 : fy);

    if ((wacom->summa.set == SUMMA_MM961) && !wacom->summa.horizontal) {
        w = SD510_HEIGHT_IN;
        h = SD510_WIDTH_IN;
    }
    if (!((wacom->summa.set == SUMMA_MM1201) && wacom->summa.origin_upper))
        fy = 1.0 - fy;

    *x = (int) (fx * w * wacom->summa.res_x);
    *y = (int) (fy * h * wacom->summa.res_y);
}

static int
summa_clamp(int v, int lo, int hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

/* Bit Pad Two button flag (Fd..Fa). */
static int
summa_bitpad_flag(mouse_wacom_t *wacom, int b)
{
    if (tablet_tool_type == 1)
        return b & 3; /* stylus: tip = 1, barrel = 2 */

    /* 3-button cursor: buttons 1, 2, 3; output code A or B. */
    if (b & 4)
        return wacom->summa.output_b ? 4 : 8;
    if (b & 2)
        return 2;
    if (b & 1)
        return 1;
    return 0;
}

static void
summa_report_bitpad(mouse_wacom_t *wacom, int x, int y, int b, int prox)
{
    int  flag = summa_bitpad_flag(wacom, b);
    char buf[32];

    if (wacom->summa.binary) {
        uint8_t data[5];

        if (wacom->summa.relative) {
            x = summa_clamp(x, -2047, 2047) & 0xfff;
            y = summa_clamp(y, -2047, 2047) & 0xfff;
        } else {
            x = summa_clamp(x, 0, 4095);
            y = summa_clamp(y, 0, 4095);
        }
        data[0] = 0x40 | (flag << 2) | (prox ? 0 : 1);
        data[1] = x & 0x3f;
        data[2] = (x >> 6) & 0x3f;
        data[3] = y & 0x3f;
        data[4] = (y >> 6) & 0x3f;
        fifo8_push_all(&wacom->data, data, 5);
        return;
    }

    if (wacom->summa.relative) {
        char xs[8];
        char ys[8];

        if (x < 0)
            snprintf(xs, sizeof(xs), "-%03d",
                     summa_clamp(-x, 0, 999) % 1000);
        else
            snprintf(xs, sizeof(xs), "%04d",
                     summa_clamp(x, 0, 9999) % 10000);
        if (y < 0)
            snprintf(ys, sizeof(ys), "-%03d",
                     summa_clamp(-y, 0, 999) % 1000);
        else
            snprintf(ys, sizeof(ys), "%04d",
                     summa_clamp(y, 0, 9999) % 10000);
        snprintf(buf, sizeof(buf), "%s,%s,", xs, ys);
    } else
        snprintf(buf, sizeof(buf), "%04d,%04d,", summa_clamp(x, 0, 9999) % 10000,
                                                         summa_clamp(y, 0, 9999) % 10000);
    summa_push_str(wacom, buf);

    snprintf(buf, sizeof(buf), "%c\r%s", prox ? ('0' + flag) : '9', wacom->summa.crlf ? "\n" : "");
    summa_push_str(wacom, buf);
}

static void
summa_report_mm(mouse_wacom_t *wacom, int x, int y, int b, int prox)
{
    int  sw     = (tablet_tool_type == 1) ? (b & 3) : (b & 7);
    int  digits = ((wacom->summa.set == SUMMA_MM1201) && (wacom->summa.res_x >= 1000)) ? 5 : 4;
    char buf[48];

    if (wacom->summa.binary) {
        uint8_t data[5];
        uint8_t flag = 0x80 | (prox ? 0x00 : 0x40) | (wacom->summa.tablet_id << 5) | sw;

        if (wacom->summa.relative) {
            /* Delta mode: three bytes, sign bit set for positive values. */
            data[0] = flag | ((x >= 0) ? 0x10 : 0) | ((y >= 0) ? 0x08 : 0);
            data[1] = summa_clamp(abs(x), 0, 127);
            data[2] = summa_clamp(abs(y), 0, 127);
            fifo8_push_all(&wacom->data, data, 3);
            return;
        }
        x       = summa_clamp(x, 0, 16383);
        y       = summa_clamp(y, 0, 16383);
        data[0] = flag | 0x18;
        data[1] = x & 0x7f;
        data[2] = (x >> 7) & 0x7f;
        data[3] = y & 0x7f;
        data[4] = (y >> 7) & 0x7f;
        fifo8_push_all(&wacom->data, data, 5);
        return;
    }

    if (wacom->summa.relative) {
        /* S0XXX (or S00XXX), the sign followed by leading zeroes. */
        snprintf(buf, sizeof(buf), "%c%s%03d,%c%s%03d,%d\r\n",
                 (x < 0) ? '-' : '+', (digits == 5) ? "00" : "0", summa_clamp(abs(x), 0, 999),
                 (y < 0) ? '-' : '+', (digits == 5) ? "00" : "0", summa_clamp(abs(y), 0, 999), sw);
    } else {
        int max = (digits == 5) ? 99999 : 9999;

        snprintf(buf, sizeof(buf), "%0*d,%0*d,%d\r\n",
                 digits, summa_clamp(x, 0, max), digits, summa_clamp(y, 0, max), sw);
    }
    summa_push_str(wacom, buf);
}

/* Build one report; out of proximity, absolute reports repeat the last
   in-proximity position and relative ones report no motion. */
static void
summa_report(mouse_wacom_t *wacom, int x, int y, int b, int prox)
{
    int rx = x;
    int ry = y;

    if (wacom->summa.relative) {
        rx = (prox && wacom->summa.have_last) ? (x - wacom->summa.last_x) : 0;
        ry = (prox && wacom->summa.have_last) ? (y - wacom->summa.last_y) : 0;
    }

    if (wacom->summa.set == SUMMA_BITPAD)
        summa_report_bitpad(wacom, rx, ry, b, prox);
    else
        summa_report_mm(wacom, rx, ry, b, prox);

    if (prox) {
        wacom->summa.last_x    = x;
        wacom->summa.last_y    = y;
        wacom->summa.have_last = 1;
    }
    wacom->summa.last_b = b;
}

static void
summa_send_config_mm(mouse_wacom_t *wacom)
{
    uint8_t data[5];
    double  w = SD510_WIDTH_IN;
    double  h = SD510_HEIGHT_IN;
    int     mx;
    int     my;

    if ((wacom->summa.set == SUMMA_MM961) && !wacom->summa.horizontal) {
        w = SD510_HEIGHT_IN;
        h = SD510_WIDTH_IN;
    }
    mx = summa_clamp((int) (w * wacom->summa.res_x), 0, 16383);
    my = summa_clamp((int) (h * wacom->summa.res_y), 0, 16383);

    data[0] = 0x80 | (wacom->summa.tablet_id << 5) | 0x18;
    data[1] = mx & 0x7f;
    data[2] = (mx >> 7) & 0x7f;
    data[3] = my & 0x7f;
    data[4] = (my >> 7) & 0x7f;
    fifo8_push_all(&wacom->data, data, 5);
}

/* Reset to the DIP switch settings. XOFF survives a reset. */
static void
summa_reset(mouse_wacom_t *wacom)
{
    int stopped = wacom->transmission_stopped;

    wacom_reset(wacom);
    wacom->transmission_stopped = stopped;
}

static void
summa_write_bitpad(mouse_wacom_t *wacom, uint8_t data)
{
    static const int rates[8]       = { 2, 4, 10, 20, 40, 70, 100, 0 };
    static const int increments[8]  = { 0, 1, 2, 3, 4, 5, 10, 20 };
    static const int resolutions[7] = { 100, 127, 200, 254, 400, 500, 508 };

    if ((data >= '@') && (data <= 'G')) {
        wacom->summa.mode = SUMMA_MODE_SWITCH;
        wacom->summa.rate = rates[data - '@'];
    } else if ((data >= 'H') && (data <= 'O')) {
        wacom->summa.mode = SUMMA_MODE_STREAM;
        wacom->summa.rate = rates[data - 'H'];
    } else if ((data >= 'a') && (data <= 'h'))
        wacom->summa.increment = increments[data - 'a'];
    else if ((data >= 'i') && (data <= 'o')) {
        wacom->summa.res_x = wacom->summa.res_y = resolutions[data - 'i'];
        wacom->summa.have_last = 0;
    } else switch (data) {
        case 0x00:
            summa_reset(wacom);
            break;
        case 0x05:
        case 's':
            summa_push_str(wacom, "MM1103  BIT PAD II  by Summagraphics Version 1.0\r");
            if (wacom->summa.crlf)
                summa_push_str(wacom, "\n");
            break;
        case 0x11:
            wacom->transmission_stopped = 0;
            break;
        case 0x13:
            wacom->transmission_stopped = 1;
            break;
        case 'P':
            wacom->summa.mode        = SUMMA_MODE_POINT;
            wacom->summa.point_latch = 0;
            break;
        case 'Q':
            wacom->summa.relative = 0;
            break;
        case 'R':
            wacom->summa.relative  = 1;
            wacom->summa.have_last = 0;
            break;
        case 'S':
            wacom->summa.mode    = SUMMA_MODE_REMOTE;
            wacom->summa.trigger = 0;
            break;
        case 'T':
            wacom->summa.trigger = 1;
            break;
        case 't':
            {
                /* T, PR (cursor/stylus on the tablet), D, C, A. */
                uint8_t result = 0x47 | (mouse_tablet_in_proximity ? 0x08 : 0x00);
                fifo8_push(&wacom->data, result);
                break;
            }
        default:
            /* CR and SP are NOPs, anything else is ignored. */
            break;
    }
}

static void
summa_mm_command(mouse_wacom_t *wacom, uint8_t data)
{
    switch (data) {
        case 0x00:
            summa_reset(wacom);
            break;
        case 0x11:
            wacom->transmission_stopped = 0;
            break;
        case 0x13:
            wacom->transmission_stopped = 1;
            break;
        case '0':
        case '1':
            wacom->summa.tablet_id = data - '0';
            break;
        case '@':
            wacom->summa.mode = SUMMA_MODE_STREAM;
            break;
        case 'A':
            wacom->summa.mode = SUMMA_MODE_SWITCH;
            break;
        case 'B':
            wacom->summa.mode        = SUMMA_MODE_POINT;
            wacom->summa.point_latch = 0;
            break;
        case 'D':
            wacom->summa.mode    = SUMMA_MODE_REMOTE;
            wacom->summa.trigger = 0;
            break;
        case 'P':
            wacom->summa.trigger = 1;
            break;
        case 'E':
            wacom->summa.relative  = 1;
            wacom->summa.have_last = 0;
            break;
        case 'G':
        case 'I':
        case 'r':
            wacom->summa.cmd         = data;
            wacom->summa.nargs       = 0;
            wacom->summa.args_needed = (data == 'r') ? 4 : 1;
            break;
        case 'T':
            wacom->summa.rate = 2;
            break;
        case 'S':
            wacom->summa.rate = 20;
            break;
        case 'R':
            wacom->summa.rate = 50;
            break;
        case 'Q':
            wacom->summa.rate = 100;
            break;
        case 'a':
            summa_send_config_mm(wacom);
            break;
        case 'b':
            wacom->summa.origin_upper = 1; /* MM 1201 */
            wacom->summa.horizontal   = 1; /* MM 961 */
            wacom->summa.have_last    = 0;
            break;
        case 'c':
            wacom->summa.origin_upper = 0;
            wacom->summa.horizontal   = 0;
            wacom->summa.have_last    = 0;
            break;
        case 'd':
        case 'e':
        case 'f':
        case 'g':
        case 'h':
        case 'i':
        case 'j':
        case 'l':
        case 'n':
        case 'p':
        case 'q':
            {
                int res;

                switch (data) {
                    case 'd': res = 100;  break;
                    case 'e': res = 200;  break;
                    case 'f': res = 254;  break; /* 10 lpmm */
                    case 'g': res = 400;  break;
                    case 'h': res = 500;  break;
                    case 'i': res = 508;  break; /* 20 lpmm */
                    case 'j': res = 1000; break;
                    case 'l': res = 1;    break; /* grid round off */
                    case 'n': res = 2;    break;
                    case 'p': res = 4;    break;
                    default:  res = 1016; break; /* 'q', 40 lpmm */
                }
                wacom->summa.res_x = wacom->summa.res_y = res;
                wacom->summa.have_last = 0;
                break;
            }
        case 't':
            wacom->summa.test_result = 0x87 | (mouse_tablet_in_proximity ? 0x08 : 0x00);
            break;
        case 'w':
            fifo8_push(&wacom->data, (uint8_t) wacom->summa.test_result);
            break;
        case 'k':
            wacom->summa.echo = 1;
            break;
        case 'x':
            summa_push_str(wacom, ".#5D3A");
            break;
        default:
            /* SP (automatic baud rate), 'z' (factory test) and unknown
               commands are ignored. */
            break;
    }
}

static void
summa_write_mm(mouse_wacom_t *wacom, uint8_t data)
{
    if (wacom->summa.echo) {
        /* Echo repeats everything until a Reset. */
        if (data == 0x00) {
            wacom->summa.echo = 0;
            summa_reset(wacom);
        } else
            fifo8_push(&wacom->data, data);
        return;
    }

    if (wacom->summa.args_needed) {
        wacom->summa.args[wacom->summa.nargs++] = data;
        if (wacom->summa.nargs < wacom->summa.args_needed)
            return;
        wacom->summa.args_needed = 0;

        switch (wacom->summa.cmd) {
            case 'I':
                /* Value is sent with 20h added; 0 to 90. */
                if ((data >= 0x20) && (data <= 0x20 + 90))
                    wacom->summa.increment = data - 0x20;
                break;
            case 'G':
                if ((data >= 0x20) && (data <= 0x20 + 90))
                    wacom->summa.axis_update = data - 0x20;
                break;
            case 'r':
                {
                    /* Set X,Y Scale: an axis outside 1..508 keeps its value. */
                    int sx = wacom->summa.args[0] | (wacom->summa.args[1] << 8);
                    int sy = wacom->summa.args[2] | (wacom->summa.args[3] << 8);

                    if ((sx >= 1) && (sx <= 508))
                        wacom->summa.res_x = sx;
                    if ((sy >= 1) && (sy <= 508))
                        wacom->summa.res_y = sy;
                    wacom->summa.have_last = 0;
                    break;
                }
            default:
                break;
        }
        return;
    }

    summa_mm_command(wacom, data);
}

static void
summa_write(mouse_wacom_t *wacom, uint8_t data)
{
    if (wacom->summa.set == SUMMA_BITPAD)
        summa_write_bitpad(wacom, data);
    else
        summa_write_mm(wacom, data);
}

/* Decide whether a report is due and queue it. Called on each report timer
   tick while no bytes are waiting. */
static void
summa_poll_report(mouse_wacom_t *wacom)
{
    int    prox = mouse_tablet_in_proximity;
    int    b    = prox ? wacom->b : 0;
    int    x;
    int    y;
    double ms;

    /* Latch button presses for point mode so none are missed. */
    if (b & ~wacom->summa.prev_b)
        wacom->summa.point_latch = 1;
    wacom->summa.prev_b = b;

    summa_get_position(wacom, &x, &y);
    if (prox) {
        wacom->summa.prox_x = x;
        wacom->summa.prox_y = y;
    } else {
        x = wacom->summa.prox_x;
        y = wacom->summa.prox_y;
    }

    if (wacom->summa.mode == SUMMA_MODE_REMOTE) {
        /* Requested reports are sent even out of proximity. */
        if (wacom->summa.trigger) {
            wacom->summa.trigger = 0;
            summa_report(wacom, x, y, b, prox);
        }
        return;
    }

    if (wacom->summa.rate) {
        ms = ((double) (tsc - wacom->summa.last_tsc)) / cpuclock * 1000.0;
        if (ms < (1000.0 / wacom->summa.rate))
            return;
    }

    if (!prox) {
        if (wacom->summa.was_prox && (wacom->summa.set != SUMMA_BITPAD))
            wacom->summa.out_prox_left = 3; /* MM: three reports on leaving */
        wacom->summa.was_prox = 0;
        if (!wacom->summa.always && !wacom->summa.out_prox_left)
            return;
        if (wacom->summa.out_prox_left)
            wacom->summa.out_prox_left--;
    } else {
        wacom->summa.was_prox      = 1;
        wacom->summa.out_prox_left = 0;
    }

    switch (wacom->summa.mode) {
        case SUMMA_MODE_SWITCH:
            if (!b)
                return;
            break;
        case SUMMA_MODE_POINT:
            if (!wacom->summa.point_latch)
                return;
            wacom->summa.point_latch = 0;
            break;
        default:
            break;
    }

    if (prox && wacom->summa.have_last && (b == wacom->summa.last_b)) {
        int dx = abs(x - wacom->summa.last_x);
        int dy = abs(y - wacom->summa.last_y);

        if (wacom->summa.increment && (dx < wacom->summa.increment) && (dy < wacom->summa.increment))
            return;
        if (wacom->summa.axis_update && !wacom->summa.relative) {
            /* Only the axis that moved far enough is updated. */
            if (dx < wacom->summa.axis_update)
                x = wacom->summa.last_x;
            if (dy < wacom->summa.axis_update)
                y = wacom->summa.last_y;
            if ((x == wacom->summa.last_x) && (y == wacom->summa.last_y))
                return;
        }
    }

    wacom->summa.last_tsc = tsc;
    summa_report(wacom, x, y, b, prox);
}

static void
summa_report_timer(mouse_wacom_t *wacom)
{
    uint8_t byte;

    /* XOFF holds transmission at a byte boundary. */
    if (wacom->transmission_stopped)
        return;

    if (!fifo8_num_used(&wacom->data))
        summa_poll_report(wacom);

    if (!fifo8_num_used(&wacom->data))
        return;

    byte = fifo8_pop(&wacom->data);
    if (!wacom->settings_bits.data_bits_conf)
        byte &= 0x7f;
    if (wacom->serial)
        serial_port_receive(wacom->serial, byte);
}

static void
wacom_write(UNUSED(struct serial_s *serial), void *priv, uint8_t data)
{
    mouse_wacom_t *wacom           = (mouse_wacom_t *) priv;
    static int     special_command = 0;

    if (wacom->summa.set != SUMMA_NONE) {
        summa_write(wacom, data);
        return;
    }

    if (data == '~') {
        special_command = 1;
        return;
    }
    if (special_command) {
        switch (data) {
            case '#':
                {
                    wacom->transmit_id = 1;
                    break;
                }
            case 'C':
            case '*':
            case 'R':
                {
                    wacom->data_rec[wacom->data_rec_pos++] = '~';
                    wacom->data_rec[wacom->data_rec_pos++] = data;
                    break;
                }
            default:
                break;
        }
        special_command = 0;
        return;
    }

    if (data == '@') {
        wacom->remote_req  = 1;
        wacom->settings_bits.remote_mode = 1;
        return;
    }
    if (data == '#' && wacom->tablet_type->type == WACOM_TYPE_IV) {
        wacom_reset_artpad(wacom);
        return;
    }
    if (data == '$') {
        wacom_reset(wacom);
        return;
    }
    if (data == 0x13) {
        wacom->transmission_stopped = 1;
        return;
    }
    if (data == 0x11) {
        wacom->transmission_stopped = 0;
        wacom->settings_bits.remote_mode = wacom->remote_req = 0;
        return;
    }
    wacom->data_rec[wacom->data_rec_pos++] = data;
    if (data == '\r' || data == '\n') {
        wacom->data_rec[wacom->data_rec_pos] = 0;
        wacom->data_rec_pos                  = 0;

        if (data == '\n')
            pclog("Wacom: written %s", wacom->data_rec);
        else
            pclog("Wacom: written %s\n", wacom->data_rec);
        if (!memcmp(wacom->data_rec, "AS", 2) && wacom->settings_bits.cmd_set == WACOM_CMDSET_IIS) {
            wacom->settings_bits.output_format = !(wacom->data_rec[2] == '1');
        } else if (!memcmp(wacom->data_rec, "SR", 2)) {
            wacom->mode                 = WACOM_MODE_STREAM;
        } else if (!memcmp(wacom->data_rec, "IN", 2)) {
            sscanf((const char *) wacom->data_rec, "IN%d", &wacom->increment);
        } else if (!memcmp(wacom->data_rec, "RE", 2)) {
            if (wacom->tablet_type->type == WACOM_TYPE_IV) wacom_reset_artpad(wacom);
            else wacom_reset(wacom);
        } else if (!memcmp(wacom->data_rec, "IT", 2)) {
            sscanf((const char *) wacom->data_rec, "IT%d", &wacom->interval);
        } else if (!memcmp(wacom->data_rec, "DE", 2) && wacom->settings_bits.cmd_set == WACOM_CMDSET_IIS) {
            int scan_res = 0;
            sscanf((const char *) wacom->data_rec, "DE%d", &scan_res);
            wacom->settings_bits.coord_sys = scan_res;
            wacom->relative_origin_valid = 0;
        } else if (!memcmp(wacom->data_rec, "SU", 2)) {
            sscanf((const char *) wacom->data_rec, "SU%d", &wacom->suppressed_increment);
            wacom->settings_bits.transfer_mode = wacom->mode = WACOM_MODE_SUPPRESSED;
        } else if (!memcmp(wacom->data_rec, "PH", 2) && wacom->settings_bits.cmd_set == WACOM_CMDSET_IIS) {
            sscanf((const char *) wacom->data_rec, "PH%d", &wacom->pressure_mode);
        } else if (!memcmp(wacom->data_rec, "IC", 2)) {
            sscanf((const char *) wacom->data_rec, "IC%d", &wacom->measurement);
            wacom->relative_origin_valid = 0;
        } else if (!memcmp(wacom->data_rec, "SW", 2)) {
            wacom->mode = WACOM_MODE_SWITCH;
        } else if (!memcmp(wacom->data_rec, "AL", 2)) {
            int out_of_range_data = wacom->settings_bits.out_of_range_data;
            sscanf((const char *) wacom->data_rec, "AL%d", &out_of_range_data);
            wacom->settings_bits.out_of_range_data = !!out_of_range_data;
        } else if (!memcmp(wacom->data_rec, "RQ", 2)) {
            int remote_mode = 0;
            sscanf((const char *) wacom->data_rec, "RQ%d", &remote_mode);
            wacom->settings_bits.remote_mode = !!remote_mode;
            if (wacom->settings_bits.remote_mode)
                wacom->remote_req = 1;
        } else if (!memcmp(wacom->data_rec, "SP", 2)) {
            wacom->transmission_stopped = 1;
        } else if (!memcmp(wacom->data_rec, "ST", 2)) {
            wacom->transmission_stopped = 0;
            wacom->settings_bits.remote_mode = wacom->remote_req = 0; 
        } else if (!memcmp(wacom->data_rec, "NR", 2)) {
            sscanf((const char *) wacom->data_rec, "NR%d", &wacom->x_res);
            wacom->y_res = wacom->x_res;
            wacom->relative_origin_valid = 0;
        } else if (wacom->tablet_type->type == WACOM_TYPE_IV && wacom->data_rec[0] == '~') {
            if (!memcmp(wacom->data_rec, "~*", 2)) {
                uint32_t settings_dword = wacom->settings;
                if (strstr((const char *) wacom->data_rec, ",")) {
                    uint32_t x_res = wacom->x_res;
                    uint32_t y_res = wacom->y_res;
                    uint32_t increment = wacom->increment;
                    uint32_t interval = wacom->interval;

                    sscanf((const char *) wacom->data_rec, "~*%08X,%d,%d,%d,%d", &settings_dword, &increment, &interval, &x_res, &y_res);
                    
                    wacom->interval = interval;
                    wacom->increment = increment;
                    wacom->x_res = x_res;
                    wacom->y_res = y_res;
                } else {
                    sscanf((const char *) wacom->data_rec, "~*%X", &settings_dword);
                }
                wacom_process_settings_dword(wacom, settings_dword);
            } else if (!memcmp(wacom->data_rec, "~C", 2)) {
                fifo8_push_all(&wacom->data, (const uint8_t *) "~C5039,3779\r", sizeof("~C5039,3779\r") - 1);
            } else if (!memcmp(wacom->data_rec, "~R", 2)) {
                uint8_t data[256] = { 0 };
                snprintf((char *)data, sizeof(data), (const char *) "~*%08X,%d,%d,%d,%d\r", wacom->settings, wacom->increment, wacom->interval, wacom->x_res, wacom->y_res);
                fifo8_push_all(&wacom->data, data, strlen((const char *) data));
            }
        }
    }
}

static int
wacom_poll(void *priv)
{
    mouse_wacom_t *wacom = (mouse_wacom_t *) priv;
    int b = tablet_get_buttons_ex();
    double abs_x;
    double abs_y;

    mouse_get_abs_coords(&abs_x, &abs_y);

    if (wacom->settings_bits.cmd_set == WACOM_CMDSET_IV) {
        wacom->abs_x = abs_x * 5039. * (wacom->x_res / 1000.);
        wacom->abs_y = abs_y * 3779. * (wacom->y_res / 1000.);
    } else {
        wacom->abs_x         = abs_x * (wacom->measurement ? 4566. : 5800.);
        wacom->abs_y         = abs_y * (wacom->measurement ? 2972. : 3774.);
        if (wacom->abs_x > (wacom->measurement ? 4566 : 5800))
            wacom->abs_x = (wacom->measurement ? 4566 : 5800);
        if (wacom->abs_y > (wacom->measurement ? 2972 : 3774))
            wacom->abs_y = (wacom->measurement ? 2972 : 3774);
        if (wacom->abs_x < 0)
            wacom->abs_x = 0;
        if (wacom->abs_y < 0)
            wacom->abs_y = 0;
        /* Lower-left origin only applies to absolute coordinates. */
        if (wacom->settings_bits.origin_location && !wacom->settings_bits.coord_sys)
            wacom->abs_y = (wacom->measurement ? 2972 : 3774) - wacom->abs_y;
    }
    if (!mouse_tablet_in_proximity) {
        wacom->relative_origin_valid = 0;
    } else if (wacom->settings_bits.coord_sys && !wacom->relative_origin_valid) {
        /* The first in-range sample establishes the origin without producing motion. */
        wacom->last_abs_x = wacom->abs_x;
        wacom->last_abs_y = wacom->abs_y;
        wacom->relative_origin_valid = 1;
    }
    if (wacom->b != b)
        wacom->oldb = wacom->b;
    wacom->b = b;
    return 0;
}

static int
wacom_switch_off_to_on(int b, int oldb)
{
    if (!(oldb & 0x1) && (b & 1))
        return 1;
    if (!(oldb & 0x2) && (b & 2))
        return 1;
    if (!(oldb & 0x4) && (b & 4))
        return 1;

    return 0;
}

static uint8_t
wacom_get_switch(int b)
{
    if (b & 0x4)
        return 0x23;
    if (b & 0x2)
        return 0x22;
    if (b & 0x1)
        return 0x21;

    return 0x00;
}

static void
wacom_transmit_prepare(mouse_wacom_t *wacom, int x, int y)
{
    if (wacom->transmit_id) {
        uint8_t data[128] = { 0 };
        snprintf((char *) data, sizeof(data), "%s", wacom->tablet_type->id);
        fifo8_push_all(&wacom->data, data, strlen((char *)data));
        wacom->transmit_id = 0;
        return;
    }
    wacom->last_abs_x          = wacom->abs_x;
    wacom->last_abs_y          = wacom->abs_y;
    wacom->remote_req          = 0;

    wacom->oldb = wacom->b;
    if (wacom->settings_bits.output_format == 0) {
        uint8_t data[7];
        data[0] = 0xC0;
        if (wacom->settings_bits.cmd_set == WACOM_CMDSET_IV) {
            if (tablet_tool_type == 0)
                data[6] = ((wacom->b & 0x1) ? (uint8_t) 31 : (uint8_t) -1);
            else
                data[6] = ((wacom->b & 0x1) ? (uint8_t) 63 : (uint8_t) -63);
        }
        else
            data[6] = (wacom->pressure_mode || wacom->settings_bits.cmd_set == WACOM_CMDSET_IV) ? ((wacom->b & 0x1) ? (uint8_t) 31 : (uint8_t) -31) : wacom_get_switch(wacom->b);

        data[5] = (y & 0x7F);
        data[4] = ((y & 0x3F80) >> 7) & 0x7F;
        data[3] = (((y & 0xC000) >> 14) & 3);

        data[2] = (x & 0x7F);
        data[1] = ((x & 0x3F80) >> 7) & 0x7F;
        data[0] |= (((x & 0xC000) >> 14) & 3);

        if (wacom->settings_bits.coord_sys && wacom->settings_bits.cmd_set == WACOM_CMDSET_IIS) {
            data[0] |= (!!(x < 0)) << 2;
            data[3] |= (!!(y < 0)) << 2;
        }

        if (wacom->settings_bits.cmd_set == WACOM_CMDSET_IV) {
            data[6] &= 0x7F;
            data[3] &= 0x3;
            if (wacom_get_switch(wacom->b) != 0x21) {
                data[3] |= (wacom_get_switch(wacom->b) & 0xF) << 3;
                data[0] |= 0x8;
            }
        }

        if (wacom->pressure_mode && wacom->settings_bits.cmd_set == WACOM_CMDSET_IIS) {
            data[0] |= 0x10;
            data[6] &= 0x7F;
        }

        if (tablet_tool_type == 1) {
            data[0] |= 0x20;
        }

        if (!mouse_tablet_in_proximity) {
            data[0] &= ~0x40;
        }
        fifo8_push_all(&wacom->data, data, 7);
    } else {
        static const char *terminators[4] = { "\r", "\n", "\r\n", "\r\n" };
        uint8_t data[128];
        data[0] = 0;
        snprintf((char *) data, sizeof(data), "*,%05d,%05d,%d%s",
                 x, y,
                 wacom->pressure_mode ? ((wacom->b & 0x1) ? (uint8_t) -31 : (uint8_t) 15) : ((wacom->b & 0x1) ? 21 : 00),
                 terminators[wacom->settings_bits.terminator]);
        fifo8_push_all(&wacom->data, data, strlen((char *)data));
    }
}

static void
wacom_report_timer(void *priv)
{
    mouse_wacom_t *wacom           = (mouse_wacom_t *) priv;
    double         milisecond_diff = ((double) (tsc - wacom->old_tsc)) / cpuclock * 1000.0;
    int            relative_mode   = wacom->settings_bits.coord_sys;
    int            dx              = wacom->abs_x - wacom->last_abs_x;
    int            dy              = wacom->abs_y - wacom->last_abs_y;
    int            x               = wacom->abs_x;
    int            y               = wacom->abs_y;
    int            increment       = wacom->suppressed_increment ? wacom->suppressed_increment : wacom->increment;

    if (!mouse_tablet_in_proximity)
        wacom->relative_origin_valid = 0;
    if (relative_mode) {
        x = wacom->relative_origin_valid ? dx : 0;
        y = wacom->relative_origin_valid ? dy : 0;
        dx = x;
        dy = y;
    }

    timer_on_auto(&wacom->report_timer, wacom->transmit_period);
    if ((((double) (tsc - wacom->reset_tsc)) / cpuclock * 1000.0) <= 10)
        return;
    /* DSR monitor: the tablet's DSR input is the host's DTR output. */
    if ((wacom->settings_bits.handshake & WACOM_HANDSHAKE_DTS) && wacom->serial &&
        !serial_port_get_line(wacom->serial, CHAR_COM_DTR))
        return;
    if (wacom->summa.set != SUMMA_NONE) {
        summa_report_timer(wacom);
        return;
    }
    if (wacom->transmit_id)
        goto transmit_prepare;
    if (fifo8_num_used(&wacom->data))
        goto transmit;
    else if (wacom->settings_bits.remote_mode && !wacom->remote_req)
        return;
    else {
        if (wacom->settings_bits.remote_mode && wacom->remote_req) {
            goto transmit_prepare;
        }
        if (wacom->transmission_stopped || (!mouse_tablet_in_proximity && !wacom->settings_bits.out_of_range_data))
            return;

        if (milisecond_diff >= (wacom->interval * 5)) {
            wacom->old_tsc = tsc;
        } else
            return;

        switch (wacom->mode) {
            default:
            case WACOM_MODE_STREAM:
                break;

            case WACOM_MODE_POINT:
                {
                    if (wacom->suppressed_increment)
                        break;
                    if (!(wacom_switch_off_to_on(wacom->b, wacom->oldb)))
                        return;
                    break;
                }

            case WACOM_MODE_SWITCH:
                {
                    if (!wacom->b)
                        return;

                    break;
                }
        }

        if (increment && !mouse_tablet_in_proximity)
            return;

        if (increment && !(abs(dx) > increment || abs(dy) > increment)) {
            if (wacom->suppressed_increment && (wacom->b == wacom->oldb))
                return;

            if (wacom->increment && !wacom_switch_off_to_on(wacom->b, wacom->oldb))
                return;
        }
    }

transmit_prepare:
    wacom_transmit_prepare(wacom, x, y);

transmit:
    if (wacom->serial)
        serial_port_receive(wacom->serial, fifo8_pop(&wacom->data));
    if (fifo8_num_used(&wacom->data) == 0) {
        wacom->old_tsc              = tsc;
    }
    return;
}

static void *
wacom_init_tablet(const device_t *info)
{
    mouse_wacom_t *dev;

    dev       = (mouse_wacom_t *) calloc(1, sizeof(mouse_wacom_t));
    dev->name = info->name;
    dev->bits = 10;
    if (info->local == 0) {
        dev->tablet_type = &sd510_id;
    } else
        dev->tablet_type = (wacom_tablet_id*)info->local;

    fifo8_create(&dev->data, 512);

    dev->port = device_get_config_int("port");

    if (dev->tablet_type->type == WACOM_TYPE_IISONLY) {
        dev->has_dip             = 1;
        dev->dip.data_format     = device_get_config_int("data_format");
        dev->dip.operation_mode  = device_get_config_int("operation_mode");
        dev->dip.origin_type     = device_get_config_int("origin_type");
        dev->dip.unit            = device_get_config_int("unit");
        dev->dip.always_transmit = device_get_config_int("always_transmit");
        dev->dip.baud            = device_get_config_int("baud");
        dev->dip.parity          = device_get_config_int("parity");
        dev->dip.stop_bits       = device_get_config_int("stop_bits");
        dev->dip.dsr_monitor     = device_get_config_int("dsr_monitor");
        dev->dip.data_bits       = device_get_config_int("data_bits");
        dev->dip.transfer_rate   = device_get_config_int("transfer_rate");
        dev->dip.origin_location = device_get_config_int("origin_location");
        dev->dip.terminator      = device_get_config_int("terminator");
        dev->dip.cmd_set         = device_get_config_int("command_set");
        dev->dip.resolution      = device_get_config_int("resolution");
        dev->dip.cursor_code     = device_get_config_int("cursor_code");
    }

    dev->serial = serial_attach(dev->port, wacom_callback, wacom_write, dev);
    timer_add(&dev->report_timer, wacom_report_timer, dev, 0);
    mouse_set_buttons(3);

    if (dev->tablet_type->type == WACOM_TYPE_IV) {
        wacom_reset_artpad(dev);
        wacom_process_settings_dword(dev, 0xE2018000);
    } else
        wacom_reset(dev);

    mouse_set_poll_ex(wacom_poll, dev);

    return dev;
}

static void
wacom_speed_changed(void *priv)
{
    mouse_wacom_t *dev = (mouse_wacom_t *) priv;

    wacom_callback(NULL, dev);
}

static void
wacom_close(void *priv)
{
    mouse_wacom_t *dev = (mouse_wacom_t *) priv;

    fifo8_destroy(&dev->data);

    /* Detach serial port from the mouse. */
    if (dev && dev->serial)
        serial_port_detach(dev->serial);

    free(dev);
}

static const device_config_t wacom_config[] = {
  // clang-format off
    {
        .name           = "port",
        .description    = "Serial Port",
        .type           = CONFIG_SELECTION,
        .default_string = "",
        .default_int    = 0,
        .file_filter    = "",
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
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

/* SD-510C tablet processor DIP switches (WACOM II-S command set chart).
   Defaults match the factory settings, except that the unit of measure
   is inches. */
static const device_config_t wacom_sd510_config[] = {
  // clang-format off
    {
        .name           = "port",
        .description    = "Serial Port",
        .type           = CONFIG_SELECTION,
        .default_string = "",
        .default_int    = 0,
        .file_filter    = "",
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
        .name           = "command_set",
        .description    = "Command set (DS1-1,2)",
        .type           = CONFIG_SELECTION,
        .default_string = "",
        .default_int    = SD510_CMDSET_WACOM_IIS,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = {
            { .description = "WACOM II-S",    .value = SD510_CMDSET_WACOM_IIS },
            { .description = "WACOM II",      .value = SD510_CMDSET_WACOM_II  },
            { .description = "Bit Pad Two",   .value = SD510_CMDSET_BITPAD    },
            { .description = "MM 1201",       .value = SD510_CMDSET_MM1201    },
            { .description = "MM 961",        .value = SD510_CMDSET_MM961     },
            { .description = ""                                               }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "data_format",
        .description    = "Data format (DS1-3)",
        .type           = CONFIG_SELECTION,
        .default_string = "",
        .default_int    = 1,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = {
            { .description = "ASCII",  .value = 1 },
            { .description = "Binary", .value = 0 },
            { .description = ""                   }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "operation_mode",
        .description    = "Operation mode (DS1-4,5)",
        .type           = CONFIG_SELECTION,
        .default_string = "",
        .default_int    = WACOM_MODE_POINT,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = {
            { .description = "Point",         .value = WACOM_MODE_POINT      },
            { .description = "Suppressed",    .value = WACOM_MODE_SUPPRESSED },
            { .description = "Switch stream", .value = WACOM_MODE_SWITCH     },
            { .description = "Stream",        .value = WACOM_MODE_STREAM     },
            { .description = "Remote request", .value = WACOM_DIP_MODE_REMOTE },
            { .description = ""                                              }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "origin_type",
        .description    = "Origin type (DS1-6)",
        .type           = CONFIG_SELECTION,
        .default_string = "",
        .default_int    = 0,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = {
            { .description = "Absolute", .value = 0 },
            { .description = "Relative", .value = 1 },
            { .description = ""                     }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "unit",
        .description    = "Unit of measure (DS1-7)",
        .type           = CONFIG_SELECTION,
        .default_string = "",
        .default_int    = 1,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = {
            { .description = "Inches",      .value = 1 },
            { .description = "Millimeters", .value = 0 },
            { .description = ""                        }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "always_transmit",
        .description    = "Always transmit (DS1-8)",
        .type           = CONFIG_BINARY,
        .default_string = "",
        .default_int    = 0,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "baud",
        .description    = "Baud rate (DS2-1..3)",
        .type           = CONFIG_SELECTION,
        .default_string = "",
        .default_int    = 9600,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = {
            { .description = "150",   .value = 150   },
            { .description = "300",   .value = 300   },
            { .description = "600",   .value = 600   },
            { .description = "1200",  .value = 1200  },
            { .description = "2400",  .value = 2400  },
            { .description = "4800",  .value = 4800  },
            { .description = "9600",  .value = 9600  },
            { .description = "19200", .value = 19200 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "parity",
        .description    = "Parity (DS2-4,5)",
        .type           = CONFIG_SELECTION,
        .default_string = "",
        .default_int    = 0,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = {
            { .description = "None", .value = 0 },
            { .description = "Odd",  .value = 2 },
            { .description = "Even", .value = 3 },
            { .description = ""                 }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "stop_bits",
        .description    = "Stop bits (DS2-6)",
        .type           = CONFIG_SELECTION,
        .default_string = "",
        .default_int    = 1,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = {
            { .description = "1", .value = 1 },
            { .description = "2", .value = 2 },
            { .description = ""              }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "dsr_monitor",
        .description    = "DSR monitor (DS2-7)",
        .type           = CONFIG_BINARY,
        .default_string = "",
        .default_int    = 0,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "data_bits",
        .description    = "Data length (DS2-8)",
        .type           = CONFIG_SELECTION,
        .default_string = "",
        .default_int    = 8,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = {
            { .description = "7 bits", .value = 7 },
            { .description = "8 bits", .value = 8 },
            { .description = ""                   }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "transfer_rate",
        .description    = "Transfer rate (DS3-1..3)",
        .type           = CONFIG_SELECTION,
        .default_string = "",
        .default_int    = 0,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = {
            { .description = "1 pps",   .value = 1   },
            { .description = "2 pps",   .value = 2   },
            { .description = "4 pps",   .value = 4   },
            { .description = "5 pps",   .value = 5   },
            { .description = "10 pps",  .value = 10  },
            { .description = "20 pps",  .value = 20  },
            { .description = "40 pps",  .value = 40  },
            { .description = "50 pps",  .value = 50  },
            { .description = "67 pps",  .value = 67  },
            { .description = "70 pps",  .value = 70  },
            { .description = "100 pps", .value = 100 },
            { .description = "Maximum", .value = 0   },
            { .description = ""                     }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "origin_location",
        .description    = "Origin location (DS3-4)",
        .type           = CONFIG_SELECTION,
        .default_string = "",
        .default_int    = 0,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = {
            { .description = "Upper left", .value = 0 },
            { .description = "Lower left", .value = 1 },
            { .description = ""                       }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "terminator",
        .description    = "Data terminator (DS3-6,7)",
        .type           = CONFIG_SELECTION,
        .default_string = "",
        .default_int    = 2,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = {
            { .description = "CR/LF", .value = 2 },
            { .description = "CR",    .value = 0 },
            { .description = "LF",    .value = 1 },
            { .description = ""                  }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "resolution",
        .description    = "Resolution (Bit Pad Two / MM)",
        .type           = CONFIG_SELECTION,
        .default_string = "",
        .default_int    = 0,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = {
            { .description = "Command set default",  .value = 0    },
            { .description = "100 lpi",              .value = 100  },
            { .description = "127 lpi (5 lpmm)",     .value = 127  },
            { .description = "200 lpi",              .value = 200  },
            { .description = "254 lpi (10 lpmm)",    .value = 254  },
            { .description = "400 lpi",              .value = 400  },
            { .description = "500 lpi",              .value = 500  },
            { .description = "508 lpi (20 lpmm)",    .value = 508  },
            { .description = "1000 lpi",             .value = 1000 },
            { .description = "1016 lpi (40 lpmm)",   .value = 1016 },
            { .description = ""                                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "cursor_code",
        .description    = "Cursor output code (Bit Pad Two)",
        .type           = CONFIG_SELECTION,
        .default_string = "",
        .default_int    = 0,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = {
            { .description = "Output A", .value = 0 },
            { .description = "Output B", .value = 1 },
            { .description = ""                     }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t mouse_wacom_tablet_device = {
    .name          = "Wacom SD-510C",
    .internal_name = "wacom_serial_tablet",
    .flags         = DEVICE_COM,
    .local         = 0,
    .init          = wacom_init_tablet,
    .close         = wacom_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = wacom_speed_changed,
    .force_redraw  = NULL,
    .config        = wacom_sd510_config
};

const device_t mouse_wacom_artpad_tablet_device = {
    .name          = "Wacom ArtPad",
    .internal_name = "wacom_serial_artpad_tablet",
    .flags         = DEVICE_COM,
    .local         = (uintptr_t) &artpad_id,
    .init          = wacom_init_tablet,
    .close         = wacom_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = wacom_speed_changed,
    .force_redraw  = NULL,
    .config        = wacom_config
};
