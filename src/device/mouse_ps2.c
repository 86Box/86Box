/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of PS/2 series Mouse devices.
 *
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>

enum {
    MODE_STREAM,
    MODE_REMOTE,
    MODE_ECHO
};

typedef struct {
    const char *name; /* name of this device */
    int8_t      type; /* type of this device */

    int mode;

    uint16_t flags;
    uint8_t resolution;
    uint8_t sample_rate;

    uint8_t command;

    int x, y, z, b;

    uint8_t last_data[6];
} mouse_t;
#define FLAG_5BTN    0x100 /* using Intellimouse Optical mode */
#define FLAG_INTELLI 0x80  /* device is IntelliMouse */
#define FLAG_INTMODE 0x40  /* using Intellimouse mode */
#define FLAG_SCALED  0x20  /* enable delta scaling */
#define FLAG_ENABLED 0x10  /* dev is enabled for use */
#define FLAG_CTRLDAT 0x08  /* ctrl or data mode */

int mouse_scan = 0;

#ifdef ENABLE_MOUSE_PS2_LOG
int mouse_ps2_do_log = ENABLE_MOUSE_PS2_LOG;

static void
mouse_ps2_log(const char *fmt, ...)
{
    va_list ap;

    if (mouse_ps2_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define mouse_ps2_log(fmt, ...)
#endif

void
mouse_clear_data(void *priv)
{
    mouse_t *dev = (mouse_t *) priv;

    dev->flags &= ~FLAG_CTRLDAT;
}

static void
ps2_report_coordinates(mouse_t *dev)
{
    uint8_t buff[3] = {0x08, 0x00, 0x00};

    if (dev->x > 255)
        dev->x = 255;
    if (dev->x < -256)
        dev->x = -256;
    if (dev->y > 255)
        dev->y = 255;
    if (dev->y < -256)
        dev->y = -256;
    if (dev->z < -8)
        dev->z = -8;
    if (dev->z > 7)
        dev->z = 7;

    if (dev->x < 0)
        buff[0] |= 0x10;
    if (dev->y < 0)
        buff[0] |= 0x20;
    if (mouse_buttons & 0x01)
        buff[0] |= 0x01;
    if (mouse_buttons & 0x02)
        buff[0] |= 0x02;
    if (dev->flags & FLAG_INTELLI) {
        if (mouse_buttons & 0x04)
            buff[0] |= 0x04;
    }
    buff[1] = (dev->x & 0xff);
    buff[2] = (dev->y & 0xff);

    keyboard_at_adddata_mouse(buff[0]);
    keyboard_at_adddata_mouse(buff[1]);
    keyboard_at_adddata_mouse(buff[2]);
    if (dev->flags & FLAG_INTMODE) {
        int temp_z = dev->z;
        if ((dev->flags & FLAG_5BTN)) {
            temp_z &= 0xF;
            if (mouse_buttons & 8)
                temp_z |= 0x10;
            if (mouse_buttons & 16)
                temp_z |= 0x20;
        }
        keyboard_at_adddata_mouse(temp_z);
    }

    dev->x = dev->y = dev->z = 0;
}

static void
ps2_write(uint8_t val, void *priv)
{
    mouse_t *dev = (mouse_t *) priv;
    uint8_t  temp;

    if (dev->flags & FLAG_CTRLDAT) {
        dev->flags &= ~FLAG_CTRLDAT;

        if (val == 0xff)
            goto mouse_reset;

        switch (dev->command) {
            case 0xe8: /* set mouse resolution */
                dev->resolution = val;
                keyboard_at_adddata_mouse(0xfa);
                break;

            case 0xf3: /* set sample rate */
                dev->sample_rate = val;
                keyboard_at_adddata_mouse(0xfa); /* Command response */
                break;

            default:
                keyboard_at_adddata_mouse(0xfc);
        }
    } else {
        dev->command = val;

        switch (dev->command) {
            case 0xe6: /* set scaling to 1:1 */
                dev->flags &= ~FLAG_SCALED;
                keyboard_at_adddata_mouse(0xfa);
                break;

            case 0xe7: /* set scaling to 2:1 */
                dev->flags |= FLAG_SCALED;
                keyboard_at_adddata_mouse(0xfa);
                break;

            case 0xe8: /* set mouse resolution */
                dev->flags |= FLAG_CTRLDAT;
                keyboard_at_adddata_mouse(0xfa);
                break;

            case 0xe9: /* status request */
                keyboard_at_adddata_mouse(0xfa);
                temp = (dev->flags & 0x30);
                if (mouse_buttons & 1)
                    temp |= 4;
                if (mouse_buttons & 2)
                    temp |= 1;
                if ((mouse_buttons & 4) && (dev->flags & FLAG_INTELLI))
                    temp |= 2;
                keyboard_at_adddata_mouse(temp);
                keyboard_at_adddata_mouse(dev->resolution);
                keyboard_at_adddata_mouse(dev->sample_rate);
                break;

            case 0xeb: /* Get mouse data */
                keyboard_at_adddata_mouse(0xfa);

                ps2_report_coordinates(dev);
                break;

            case 0xf2: /* read ID */
                keyboard_at_adddata_mouse(0xfa);
                if (dev->flags & FLAG_INTMODE)
                    keyboard_at_adddata_mouse((dev->flags & FLAG_5BTN) ? 0x04 : 0x03);
                else
                    keyboard_at_adddata_mouse(0x00);
                break;

            case 0xf3: /* set command mode */
                dev->flags |= FLAG_CTRLDAT;
                keyboard_at_adddata_mouse(0xfa); /* ACK for command byte */
                break;

            case 0xf4: /* enable */
                dev->flags |= FLAG_ENABLED;
                mouse_scan = 1;
                keyboard_at_adddata_mouse(0xfa);
                break;

            case 0xf5: /* disable */
                dev->flags &= ~FLAG_ENABLED;
                mouse_scan = 0;
                keyboard_at_adddata_mouse(0xfa);
                break;

            case 0xf6: /* set defaults */
            case 0xff: /* reset */
mouse_reset:
                dev->mode = MODE_STREAM;
                dev->flags &= 0x88;
                mouse_scan = 1;
                keyboard_at_mouse_reset();
                keyboard_at_adddata_mouse(0xfa);
                if (dev->command == 0xff) {
                    keyboard_at_adddata_mouse(0xaa);
                    keyboard_at_adddata_mouse(0x00);
                }
                break;

            default:
                keyboard_at_adddata_mouse(0xfe);
        }
    }

    if (dev->flags & FLAG_INTELLI) {
        for (temp = 0; temp < 5; temp++)
            dev->last_data[temp] = dev->last_data[temp + 1];

        dev->last_data[5] = val;

        if (dev->last_data[0] == 0xf3 && dev->last_data[1] == 0xc8
        && dev->last_data[2] == 0xf3 && dev->last_data[3] == 0xc8
        && dev->last_data[4] == 0xf3 && dev->last_data[5] == 0x50
        && mouse_get_buttons() == 5) {
            dev->flags |= FLAG_INTMODE | FLAG_5BTN;
        }
        else if (dev->last_data[0] == 0xf3 && dev->last_data[1] == 0xc8
        && dev->last_data[2] == 0xf3 && dev->last_data[3] == 0x64
        && dev->last_data[4] == 0xf3 && dev->last_data[5] == 0x50) {
            dev->flags |= FLAG_INTMODE;
        }
    }
}

static int
ps2_poll(int x, int y, int z, int b, void *priv)
{
    mouse_t *dev     = (mouse_t *) priv;
    uint8_t  buff[3] = { 0x08, 0x00, 0x00 };

    if (!x && !y && !z && (b == dev->b))
        return (0xff);

#if 0
    if (!(dev->flags & FLAG_ENABLED))
	return(0xff);
#endif

    if (!mouse_scan)
        return (0xff);

    dev->x += x;
    dev->y -= y;
    dev->z -= z;
    if ((dev->mode == MODE_STREAM) && (dev->flags & FLAG_ENABLED) && (keyboard_at_mouse_pos() < 13)) {
        dev->b = b;

        ps2_report_coordinates(dev);
    }

    return (0);
}

/*
 * Initialize the device for use by the user.
 *
 * We also get called from the various machines.
 */
void *
mouse_ps2_init(const device_t *info)
{
    mouse_t *dev;
    int      i;

    dev = (mouse_t *) malloc(sizeof(mouse_t));
    memset(dev, 0x00, sizeof(mouse_t));
    dev->name = info->name;
    dev->type = info->local;

    dev->mode = MODE_STREAM;
    i         = device_get_config_int("buttons");
    if (i > 2)
        dev->flags |= FLAG_INTELLI;

    if (i == 4) i = 3;

    /* Hook into the general AT Keyboard driver. */
    keyboard_at_set_mouse(ps2_write, dev);

    mouse_ps2_log("%s: buttons=%d\n", dev->name, i);

    /* Tell them how many buttons we have. */
    mouse_set_buttons(i);

    /* Return our private data to the I/O layer. */
    return (dev);
}

static void
ps2_close(void *priv)
{
    mouse_t *dev = (mouse_t *) priv;

    /* Unhook from the general AT Keyboard driver. */
    keyboard_at_set_mouse(NULL, NULL);

    free(dev);
}

static const device_config_t ps2_config[] = {
  // clang-format off
    {
        .name = "buttons",
        .description = "Buttons",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 2,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "Two",          .value = 2 },
            { .description = "Three",        .value = 3 },
            { .description = "Wheel",        .value = 4 },
            { .description = "Five + Wheel", .value = 5 },
            { .description = ""                         }
        }
    },
    {
        .name = "", .description = "", .type = CONFIG_END
    }
// clang-format on
};

const device_t mouse_ps2_device = {
    .name          = "Standard PS/2 Mouse",
    .internal_name = "ps2",
    .flags         = DEVICE_PS2,
    .local         = MOUSE_TYPE_PS2,
    .init          = mouse_ps2_init,
    .close         = ps2_close,
    .reset         = NULL,
    { .poll = ps2_poll },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ps2_config
};
