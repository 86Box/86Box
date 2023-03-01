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

#define FLAG_3BTN 0x20 /* enable 3-button mode */

enum wacom_modes {
    WACOM_MODE_SUPPRESSED = 0,
    WACOM_MODE_POINT      = 1,
    WACOM_MODE_STREAM     = 2,
    WACOM_MODE_SWITCH     = 3,
};

enum {
    REPORT_PHASE_PREPARE,
    REPORT_PHASE_TRANSMIT
};

typedef struct {
    const char *name; /* name of this device */
    int8_t      type, /* type of this device */
        port;
    uint8_t flags, but, /* device flags */
        status, format,
        data_len, data[64],
        data_rec[0x200];
    int abs_x, abs_y,
        rel_x, rel_y,
        oldb, b;

    int data_pos, data_rec_pos, mode, transmission_ongoing, transmission_format, interval;
    int increment, suppressed_increment;
    int transmission_stopped;
    int reset;
    int transmit_id, transmit_id_pending;
    int pressure_mode;
    int suppressed, measurement, always_report;
    int remote_req, remote_mode;

    int      last_abs_x, last_abs_y; /* Suppressed/Increment Mode. */
    uint32_t settings;               /* Settings DWORD */

    double     transmit_period;
    double     old_tsc, reset_tsc;
    pc_timer_t report_timer;

    serial_t *serial;
} mouse_wacom_t;

static double
wacom_transmit_period(mouse_wacom_t *dev, int bps, int rps)
{
    double dbps     = (double) bps;
    double temp     = 0.0;
    int    word_len = 10;

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

static void
wacom_reset(mouse_wacom_t *wacom)
{
    wacom->transmit_period      = wacom_transmit_period(wacom, 9600, -1);
    wacom->mode                 = WACOM_MODE_POINT;
    wacom->data_pos             = 0;
    wacom->transmission_ongoing = 0;
    wacom->mode                 = 0;
    wacom->transmission_stopped = 0;
    wacom->interval             = 0;
    wacom->transmit_id          = 0;
    wacom->format               = 0; /* ASCII */
    wacom->measurement          = 1;
    wacom->increment = wacom->suppressed_increment = 0;
    wacom->reset_tsc                               = tsc;
    wacom->remote_mode = wacom->remote_req = 0;
    wacom->always_report                   = 0;

    mouse_mode = 1;
}

static void
wacom_callback(struct serial_s *serial, void *priv)
{
    mouse_wacom_t *wacom = (mouse_wacom_t *) priv;

    wacom->transmit_period = wacom_transmit_period(wacom, 9600, -1);
    timer_stop(&wacom->report_timer);
    timer_on_auto(&wacom->report_timer, wacom->transmit_period);
}

static void
wacom_write(struct serial_s *serial, void *priv, uint8_t data)
{
    mouse_wacom_t *wacom           = (mouse_wacom_t *) priv;
    static int     special_command = 0;

    if (data == '~') {
        special_command = 1;
        return;
    }
    if (special_command) {
        switch (data) {
            case '#':
                {
                    if (!wacom->transmission_ongoing)
                        wacom->transmit_id++;
                    break;
                }
        }
        special_command = 0;
        return;
    }

    if (data == '@') {
        wacom->remote_req  = 1;
        wacom->remote_mode = 1;
        return;
    }
    if (data == 0x13) {
        wacom->transmission_stopped = 1;
        return;
    }
    if (data == 0x11) {
        wacom->transmission_stopped = 0;
        wacom->remote_mode = wacom->remote_req = 0;
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
        if (!memcmp(wacom->data_rec, "AS", 2)) {
            wacom->format               = (wacom->data_rec[2] == '1');
            wacom->transmission_ongoing = 0;
        } else if (!memcmp(wacom->data_rec, "SR", 2)) {
            wacom->mode                 = WACOM_MODE_STREAM;
            wacom->suppressed_increment = 0;
        } else if (!memcmp(wacom->data_rec, "IN", 2)) {
            sscanf((const char *) wacom->data_rec, "IN%d", &wacom->increment);
        } else if (!memcmp(wacom->data_rec, "RE", 2) || wacom->data_rec[0] == '$' || wacom->data_rec[0] == '#') {
            wacom_reset(wacom);
        } else if (!memcmp(wacom->data_rec, "IT", 2)) {
            sscanf((const char *) wacom->data_rec, "IT%d", &wacom->interval);
        } else if (!memcmp(wacom->data_rec, "DE", 2)) {
            sscanf((const char *) wacom->data_rec, "DE%d", &mouse_mode);
            mouse_mode = !mouse_mode;
            plat_mouse_capture(0);
        } else if (!memcmp(wacom->data_rec, "SU", 2)) {
            sscanf((const char *) wacom->data_rec, "SU%d", &wacom->suppressed_increment);
        } else if (!memcmp(wacom->data_rec, "PH", 2)) {
            sscanf((const char *) wacom->data_rec, "PH%d", &wacom->pressure_mode);
        } else if (!memcmp(wacom->data_rec, "IC", 2)) {
            sscanf((const char *) wacom->data_rec, "IC%d", &wacom->measurement);
        } else if (!memcmp(wacom->data_rec, "AL", 2)) {
            sscanf((const char *) wacom->data_rec, "AL%d", &wacom->always_report);
        } else if (!memcmp(wacom->data_rec, "RQ", 2)) {
            sscanf((const char *) wacom->data_rec, "RQ%d", &wacom->remote_mode);
            if (wacom->remote_mode)
                wacom->remote_req = 1;
        } else if (!memcmp(wacom->data_rec, "SP", 2)) {
            wacom->transmission_stopped = 1;
        } else if (!memcmp(wacom->data_rec, "ST", 2)) {
            wacom->transmission_stopped = 0;
            wacom->remote_mode = wacom->remote_req = 0;
        }
    }
}

static int
wacom_poll(int x, int y, int z, int b, double abs_x, double abs_y, void *priv)
{
    mouse_wacom_t *wacom = (mouse_wacom_t *) priv;
    wacom->abs_x         = abs_x * (wacom->measurement ? 4566. : 5800.);
    wacom->abs_y         = abs_y * (wacom->measurement ? 2972. : 3774.);
    if (wacom->abs_x > (wacom->measurement ? 4566 : 5800))
        wacom->abs_x = (wacom->measurement ? 4566 : 5800);
    if (wacom->abs_y > (wacom->measurement ? 2972 : 3774))
        wacom->abs_x = (wacom->measurement ? 2972 : 3774);
    if (wacom->abs_x < 0)
        wacom->abs_x = 0;
    if (wacom->abs_y < 0)
        wacom->abs_y = 0;
    wacom->rel_x = x;
    wacom->rel_y = y;
    if (wacom->b != b)
        wacom->oldb = wacom->b;
    wacom->b = b;
    return (0);
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
    wacom->transmission_ongoing = 1;
    wacom->data_pos             = 0;
    memset(wacom->data, 0, sizeof(wacom->data));
    if (wacom->transmit_id) {
        wacom->transmission_format = 0;
        snprintf((char *) wacom->data, sizeof(wacom->data), "~#SD51C V3.2.1.01\r");
        return;
    }
    wacom->transmission_format = wacom->format;
    wacom->last_abs_x          = wacom->abs_x;
    wacom->last_abs_y          = wacom->abs_y;
    wacom->remote_req          = 0;

    wacom->oldb = wacom->b;
    if (wacom->format == 1) {
        wacom->data[0] = 0xC0;
        wacom->data[6] = wacom->pressure_mode ? ((wacom->b & 0x1) ? (uint8_t) 31 : (uint8_t) -31) : wacom_get_switch(wacom->b);

        wacom->data[5] = (y & 0x7F);
        wacom->data[4] = ((y & 0x3F80) >> 7) & 0x7F;
        wacom->data[3] = (((y & 0xC000) >> 14) & 3);

        wacom->data[2] = (x & 0x7F);
        wacom->data[1] = ((x & 0x3F80) >> 7) & 0x7F;
        wacom->data[0] |= (((x & 0xC000) >> 14) & 3);

        if (mouse_mode == 0) {
            wacom->data[0] |= (!!(x < 0)) << 2;
            wacom->data[3] |= (!!(y < 0)) << 2;
        }

        if (wacom->pressure_mode) {
            wacom->data[0] |= 0x10;
            wacom->data[6] &= 0x7F;
        }

        if (tablet_tool_type == 1) {
            wacom->data[0] |= 0x20;
        }

        if (!mouse_tablet_in_proximity) {
            wacom->data[0] &= ~0x40;
        }
    } else {
        wacom->data[0] = 0;
        snprintf((char *) wacom->data, sizeof(wacom->data), "*,%05d,%05d,%d\r\n",
                 wacom->abs_x, wacom->abs_y,
                 wacom->pressure_mode ? ((wacom->b & 0x1) ? (uint8_t) -31 : (uint8_t) 15) : ((wacom->b & 0x1) ? 21 : 00));
    }
}

extern double cpuclock;
static void
wacom_report_timer(void *priv)
{
    mouse_wacom_t *wacom           = (mouse_wacom_t *) priv;
    double         milisecond_diff = ((double) (tsc - wacom->old_tsc)) / cpuclock * 1000.0;
    int            relative_mode   = (mouse_mode == 0);
    int            x               = (relative_mode ? wacom->rel_x : wacom->abs_x);
    int            y               = (relative_mode ? wacom->rel_y : wacom->abs_y);
    int            x_diff          = abs(relative_mode ? wacom->rel_x : (wacom->abs_x - wacom->last_abs_x));
    int            y_diff          = abs(relative_mode ? wacom->rel_y : (wacom->abs_y - wacom->last_abs_y));
    int            increment       = wacom->suppressed_increment ? wacom->suppressed_increment : wacom->increment;

    timer_on_auto(&wacom->report_timer, wacom->transmit_period);
    if ((((double) (tsc - wacom->reset_tsc)) / cpuclock * 1000.0) <= 10)
        return;
    if (wacom->transmit_id && !wacom->transmission_ongoing)
        goto transmit_prepare;
    if (wacom->transmission_ongoing)
        goto transmit;
    else if (wacom->remote_mode && !wacom->remote_req)
        return;
    else {
        if (wacom->remote_mode && wacom->remote_req) {
            goto transmit_prepare;
        }
        if (wacom->transmission_stopped || (!mouse_tablet_in_proximity && !wacom->always_report))
            return;

        if (milisecond_diff >= (wacom->interval * 5)) {
            wacom->old_tsc = tsc;
        } else
            return;

        switch (wacom->mode) {
            case WACOM_MODE_STREAM:
            default:
                break;

            case WACOM_MODE_POINT:
                {
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

        if (increment && !(x_diff > increment || y_diff > increment)) {
            if (wacom->suppressed_increment && (wacom->b == wacom->oldb))
                return;

            if (wacom->increment && !wacom_switch_off_to_on(wacom->b, wacom->oldb))
                return;
        }
    }

transmit_prepare:
    wacom_transmit_prepare(wacom, x, y);

transmit:
    serial_write_fifo(wacom->serial, wacom->data[wacom->data_pos++]);
    if ((wacom->transmission_format == 0 && wacom->data[wacom->data_pos] == 0)
        || (wacom->transmission_format == 1 && wacom->data_pos == 7)) {
        wacom->transmission_ongoing = 0;
        wacom->transmit_id          = 0;
        wacom->data_pos             = 0;
        wacom->old_tsc              = tsc;
    }
    return;
}

static void *
wacom_init(const device_t *info)
{
    mouse_wacom_t *dev;

    dev       = (mouse_wacom_t *) calloc(1, sizeof(mouse_wacom_t));
    dev->name = info->name;
    dev->but  = 3;

    dev->port = device_get_config_int("port");

    dev->serial = serial_attach(dev->port, wacom_callback, wacom_write, dev);
    timer_add(&dev->report_timer, wacom_report_timer, dev, 0);
    mouse_set_buttons(dev->but);

    wacom_reset(dev);

    return dev;
}

static void
wacom_speed_changed(void *priv)
{
    mouse_wacom_t *dev = (mouse_wacom_t *) priv;

    wacom_callback(dev->serial, dev);
}

static void
wacom_close(void *priv)
{
    mouse_wacom_t *dev = (mouse_wacom_t *) priv;

    /* Detach serial port from the mouse. */
    if (dev && dev->serial && dev->serial->sd)
        memset(dev->serial->sd, 0, sizeof(serial_device_t));

    free(dev);
}

static const device_config_t wacom_config[] = {
  // clang-format off
    {
        .name = "port",
        .description = "Serial Port",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "COM1", .value = 0 },
            { .description = "COM2", .value = 1 },
            { .description = "COM3", .value = 2 },
            { .description = "COM4", .value = 3 },
            { .description = ""                 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t mouse_wacom_device = {
    .name          = "Wacom SD-510C",
    .internal_name = "wacom_serial",
    .flags         = DEVICE_COM,
    .local         = MOUSE_TYPE_WACOM,
    .init          = wacom_init,
    .close         = wacom_close,
    .reset         = NULL,
    { .poll = wacom_poll },
    .speed_changed = wacom_speed_changed,
    .force_redraw  = NULL,
    .config        = wacom_config
};
