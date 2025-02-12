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

enum wacom_modes {
    WACOM_MODE_SUPPRESSED = 0,
    WACOM_MODE_POINT      = 1,
    WACOM_MODE_STREAM     = 2,
    WACOM_MODE_SWITCH     = 3,
};

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
    uint8_t     but;
    uint8_t     status;
    uint8_t     bits;
    uint8_t     data_rec[0x200];
    int         abs_x;
    int         abs_y;
    int         rel_x;
    int         rel_y;
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
    
    uint32_t               x_res;
    uint32_t               y_res;
    const wacom_tablet_id *tablet_type;

    int last_abs_x; /* Suppressed/Increment Mode. */
    int last_abs_y; /* Suppressed/Increment Mode. */
    union {
        uint32_t settings; /* Settings DWORD */
        /* We don't target any architectures except x86/x64/ARM32/ARM64.
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

    serial_t *serial;
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

static void
wacom_process_settings_dword(mouse_wacom_t *wacom, uint32_t dword)
{
    wacom->settings = dword;

    wacom->mode = wacom->settings_bits.transfer_mode;

    wacom->bits  = 1 + 7 + wacom->settings_bits.data_bits_conf;
    wacom->bits += 1 + wacom->settings_bits.stop_bits_conf;
    if (wacom->settings_bits.parity == 2 && !(wacom->bits % 2)) {
        wacom->bits++;
    } else if (wacom->settings_bits.parity == 3 && (wacom->bits % 2)) {
        wacom->bits++;
    }
    
    switch(wacom->settings_bits.baud_rate) {
        case 0:
            wacom->transmit_period = wacom_transmit_period(wacom, 2400, -1);
            break;

        case 1:
            wacom->transmit_period = wacom_transmit_period(wacom, 4800, -1);
            break;
        
        case 2:
            wacom->transmit_period = wacom_transmit_period(wacom, 9600, -1);
            break;
        
        case 3:
            wacom->transmit_period = wacom_transmit_period(wacom, 19200, -1);
            break;

        default:
            break;
    }

    mouse_input_mode = !wacom->settings_bits.coord_sys;
    wacom->x_res = wacom->y_res = wacom_resolution_values[wacom->settings_bits.resolution];
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

    mouse_input_mode = 1;
    wacom_process_settings_dword(wacom, 0xA21BC800);
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
    mouse_input_mode = 1;
}

static void
wacom_callback(UNUSED(struct serial_s *serial), void *priv)
{
    mouse_wacom_t *wacom = (mouse_wacom_t *) priv;

    switch(wacom->settings_bits.baud_rate) {
        case 0:
            wacom->transmit_period = wacom_transmit_period(wacom, 2400, -1);
            break;

        case 1:
            wacom->transmit_period = wacom_transmit_period(wacom, 4800, -1);
            break;
        
        case 2:
            wacom->transmit_period = wacom_transmit_period(wacom, 9600, -1);
            break;
        
        case 3:
            wacom->transmit_period = wacom_transmit_period(wacom, 19200, -1);
            break;

        default:
            break;
    }
    timer_stop(&wacom->report_timer);
    timer_on_auto(&wacom->report_timer, wacom->transmit_period);
}

static void
wacom_write(UNUSED(struct serial_s *serial), void *priv, uint8_t data)
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
            sscanf((const char *) wacom->data_rec, "DE%d", &mouse_input_mode);
            mouse_input_mode = !mouse_input_mode;
            plat_mouse_capture(0);
        } else if (!memcmp(wacom->data_rec, "SU", 2)) {
            sscanf((const char *) wacom->data_rec, "SU%d", &wacom->suppressed_increment);
            wacom->settings_bits.transfer_mode = wacom->mode = WACOM_MODE_SUPPRESSED;
        } else if (!memcmp(wacom->data_rec, "PH", 2) && wacom->settings_bits.cmd_set == WACOM_CMDSET_IIS) {
            sscanf((const char *) wacom->data_rec, "PH%d", &wacom->pressure_mode);
        } else if (!memcmp(wacom->data_rec, "IC", 2)) {
            sscanf((const char *) wacom->data_rec, "IC%d", &wacom->measurement);
        } else if (!memcmp(wacom->data_rec, "SW", 2)) {
            wacom->mode = WACOM_MODE_SWITCH;
        } else if (!memcmp(wacom->data_rec, "AL", 2)) {
            uint8_t out_of_range_data = wacom->settings_bits.out_of_range_data;
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
    int delta_x;
    int delta_y;
    int b = mouse_get_buttons_ex();
    double abs_x;
    double abs_y;

    mouse_subtract_coords(&delta_x, &delta_y, NULL, NULL, -32768, 32767, 0, 0);
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
            wacom->abs_x = (wacom->measurement ? 2972 : 3774);
        if (wacom->abs_x < 0)
            wacom->abs_x = 0;
        if (wacom->abs_y < 0)
            wacom->abs_y = 0;
        wacom->rel_x = delta_x;
        wacom->rel_y = delta_y;
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

        if (mouse_input_mode == 0 && wacom->settings_bits.cmd_set == WACOM_CMDSET_IIS) {
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
        uint8_t data[128];
        data[0] = 0;
        snprintf((char *) data, sizeof(data), "*,%05d,%05d,%d\r\n",
                 wacom->abs_x, wacom->abs_y,
                 wacom->pressure_mode ? ((wacom->b & 0x1) ? (uint8_t) -31 : (uint8_t) 15) : ((wacom->b & 0x1) ? 21 : 00));
        fifo8_push_all(&wacom->data, data, strlen((char *)data));
    }
}

extern double cpuclock;
static void
wacom_report_timer(void *priv)
{
    mouse_wacom_t *wacom           = (mouse_wacom_t *) priv;
    double         milisecond_diff = ((double) (tsc - wacom->old_tsc)) / cpuclock * 1000.0;
    int            relative_mode   = (mouse_input_mode == 0);
    int            x               = (relative_mode ? wacom->rel_x : wacom->abs_x);
    int            y               = (relative_mode ? wacom->rel_y : wacom->abs_y);
    int            x_diff          = abs(relative_mode ? wacom->rel_x : (wacom->abs_x - wacom->last_abs_x));
    int            y_diff          = abs(relative_mode ? wacom->rel_y : (wacom->abs_y - wacom->last_abs_y));
    int            increment       = wacom->suppressed_increment ? wacom->suppressed_increment : wacom->increment;

    timer_on_auto(&wacom->report_timer, wacom->transmit_period);
    if ((((double) (tsc - wacom->reset_tsc)) / cpuclock * 1000.0) <= 10)
        return;
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
    serial_write_fifo(wacom->serial, fifo8_pop(&wacom->data));
    if (fifo8_num_used(&wacom->data) == 0) {
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
    dev->bits = 10;
    if (info->local == 0) {
        dev->tablet_type = &sd510_id;
    } else
        dev->tablet_type = (wacom_tablet_id*)info->local;

    fifo8_create(&dev->data, 512);

    dev->port = device_get_config_int("port");

    dev->serial = serial_attach(dev->port, wacom_callback, wacom_write, dev);
    timer_add(&dev->report_timer, wacom_report_timer, dev, 0);
    mouse_set_buttons(dev->but);

    if (dev->tablet_type->type == WACOM_TYPE_IV) {
        wacom_reset_artpad(dev);
        wacom_process_settings_dword(dev, 0xE2018000);
    } else
        wacom_reset(dev);

    mouse_set_poll(wacom_poll, dev);

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

    fifo8_destroy(&dev->data);

    /* Detach serial port from the mouse. */
    if (dev && dev->serial && dev->serial->sd)
        memset(dev->serial->sd, 0, sizeof(serial_device_t));

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

const device_t mouse_wacom_device = {
    .name          = "Wacom SD-510C",
    .internal_name = "wacom_serial",
    .flags         = DEVICE_COM,
    .local         = 0,
    .init          = wacom_init,
    .close         = wacom_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = wacom_speed_changed,
    .force_redraw  = NULL,
    .config        = wacom_config
};

const device_t mouse_wacom_artpad_device = {
    .name          = "Wacom ArtPad",
    .internal_name = "wacom_serial_artpad",
    .flags         = DEVICE_COM,
    .local         = (uintptr_t) &artpad_id,
    .init          = wacom_init,
    .close         = wacom_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = wacom_speed_changed,
    .force_redraw  = NULL,
    .config        = wacom_config
};
