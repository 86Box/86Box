/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of PS/2 series Mouse devices.
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
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
#include <86box/plat_unused.h>

enum {
    MODE_STREAM,
    MODE_REMOTE,
    MODE_ECHO
};

#define FLAG_EXPLORER 0x200  /* Has 5 buttons */
#define FLAG_5BTN     0x100  /* using Intellimouse Optical mode */
#define FLAG_INTELLI   0x80  /* device is IntelliMouse */
#define FLAG_INTMODE   0x40  /* using Intellimouse mode */
#define FLAG_SCALED    0x20  /* enable delta scaling */
#define FLAG_ENABLED   0x10  /* dev is enabled for use */
#define FLAG_CTRLDAT   0x08  /* ctrl or data mode */

#define FIFO_SIZE      16

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
    atkbc_dev_t *dev = (atkbc_dev_t *) priv;

    dev->flags &= ~FLAG_CTRLDAT;
}

static void
ps2_report_coordinates(atkbc_dev_t *dev, int main)
{
    uint8_t buff[3] = { 0x08, 0x00, 0x00 };
    int temp_z;

    if (dev->x > 255) {
        dev->x = 255;        
        buff[0] |= 0x40;
    }
    if (dev->x < -256) {
        dev->x = -256;
        buff[0] |= 0x40;
    }
    if (dev->y > 255) {
        dev->y = 255;
        buff[0] |= 0x80;
    }
    if (dev->y < -256) {
        dev->y = -256;
        buff[0] |= 0x80;
    }
    if (dev->z < -8)
        dev->z = -8;
    if (dev->z > 7)
        dev->z = 7;

    if (dev->x < 0)
        buff[0] |= 0x10;
    if (dev->y < 0)
        buff[0] |= 0x20;
    buff[0] |= (dev->b & ((dev->flags & FLAG_INTELLI) ? 0x07 : 0x03));
    buff[1] = (dev->x & 0xff);
    buff[2] = (dev->y & 0xff);

    kbc_at_dev_queue_add(dev, buff[0], main);
    kbc_at_dev_queue_add(dev, buff[1], main);
    kbc_at_dev_queue_add(dev, buff[2], main);
    if (dev->flags & FLAG_INTMODE) {
        temp_z = dev->z & 0x0f;
        if (dev->flags & FLAG_5BTN) {
            if (mouse_buttons & 8)
                temp_z |= 0x10;
            if (mouse_buttons & 16)
                temp_z |= 0x20;
        } else {
            /* The wheel coordinate is sign-extended. */
            if (temp_z & 0x08)
                temp_z |= 0xf0;
        }
        kbc_at_dev_queue_add(dev, temp_z, main);
    }

    dev->x = dev->y = dev->z = 0;
}

static void
ps2_set_defaults(atkbc_dev_t *dev)
{
    dev->mode = MODE_STREAM;
    dev->rate = 100;
    mouse_set_sample_rate(100.0);
    dev->resolution = 2;
    dev->flags &= 0x88;
    mouse_scan = 0;
}

static void
ps2_bat(void *priv)
{
    atkbc_dev_t *dev = (atkbc_dev_t *) priv;

    ps2_set_defaults(dev);

    kbc_at_dev_queue_add(dev, 0xaa, 0);
    kbc_at_dev_queue_add(dev, 0x00, 0);
}

static void
ps2_write(void *priv)
{
    atkbc_dev_t *dev = (atkbc_dev_t *) priv;
    uint8_t  temp;
    uint8_t  val;
    static uint8_t last_data[6] = { 0x00 };

    if (dev->port == NULL)
        return;

    val = dev->port->dat;

    dev->state = DEV_STATE_MAIN_OUT;

    if (dev->flags & FLAG_CTRLDAT) {
        dev->flags &= ~FLAG_CTRLDAT;

        if (val == 0xff)
            kbc_at_dev_reset(dev, 1);
        else  switch (dev->command) {
            case 0xe8: /* set mouse resolution */
                dev->resolution = val;
                kbc_at_dev_queue_add(dev, 0xfa, 0);
                mouse_ps2_log("%s: Set mouse resolution [%02X]\n", dev->name, val);
                break;

            case 0xf3: /* set sample rate */
                dev->rate = val;
                mouse_set_sample_rate((double) val);
                kbc_at_dev_queue_add(dev, 0xfa, 0); /* Command response */
                mouse_ps2_log("%s: Set sample rate [%02X]\n", dev->name, val);
                break;

            default:
                kbc_at_dev_queue_add(dev, 0xfc, 0);
        }
    } else {
        dev->command = val;

        switch (dev->command) {
            case 0xe6: /* set scaling to 1:1 */
                mouse_ps2_log("%s: Set scaling to 1:1\n", dev->name);
                dev->flags &= ~FLAG_SCALED;
                kbc_at_dev_queue_add(dev, 0xfa, 0);
                break;

            case 0xe7: /* set scaling to 2:1 */
                mouse_ps2_log("%s: Set scaling to 2:1\n", dev->name);
                dev->flags |= FLAG_SCALED;
                kbc_at_dev_queue_add(dev, 0xfa, 0);
                break;

            case 0xe8: /* set mouse resolution */
                mouse_ps2_log("%s: Set mouse resolution\n", dev->name);
                dev->flags |= FLAG_CTRLDAT;
                kbc_at_dev_queue_add(dev, 0xfa, 0);
                dev->state = DEV_STATE_MAIN_WANT_IN;
                break;

            case 0xe9: /* status request */
                mouse_ps2_log("%s: Status request\n", dev->name);
                kbc_at_dev_queue_add(dev, 0xfa, 0);
                temp = (dev->flags & 0x20);
                if (mouse_scan)
                    temp |= FLAG_ENABLED;
                if (mouse_buttons & 1)
                    temp |= 4;
                if (mouse_buttons & 2)
                    temp |= 1;
                if ((mouse_buttons & 4) && (dev->flags & FLAG_INTELLI))
                    temp |= 2;
                kbc_at_dev_queue_add(dev, temp, 0);
                kbc_at_dev_queue_add(dev, dev->resolution, 0);
                kbc_at_dev_queue_add(dev, dev->rate, 0);
                break;

            case 0xea: /* set stream */
                mouse_ps2_log("%s: Set stream\n", dev->name);
                dev->flags &= ~FLAG_CTRLDAT;
                dev->mode = MODE_STREAM;
                mouse_scan = 1;
                kbc_at_dev_queue_add(dev, 0xfa, 0); /* ACK for command byte */
                break;

            case 0xeb: /* Get mouse data */
                mouse_ps2_log("%s: Get mouse data\n", dev->name);
                kbc_at_dev_queue_add(dev, 0xfa, 0);

                ps2_report_coordinates(dev, 0);
                break;

            case 0xf0: /* set remote */
                mouse_ps2_log("%s: Set remote\n", dev->name);
                dev->flags &= ~FLAG_CTRLDAT;
                dev->mode = MODE_REMOTE;
                mouse_scan = 1;
                kbc_at_dev_queue_add(dev, 0xfa, 0); /* ACK for command byte */
                break;

            case 0xf2: /* read ID */
                mouse_ps2_log("%s: Read ID\n", dev->name);
                kbc_at_dev_queue_add(dev, 0xfa, 0);
                if (dev->flags & FLAG_INTMODE)
                    kbc_at_dev_queue_add(dev, (dev->flags & FLAG_5BTN) ? 0x04 : 0x03, 0);
                else
                    kbc_at_dev_queue_add(dev, 0x00, 0);
                break;

            case 0xf3: /* set sample rate */
                mouse_ps2_log("%s: Set sample rate\n", dev->name);
                dev->flags |= FLAG_CTRLDAT;
                kbc_at_dev_queue_add(dev, 0xfa, 0); /* ACK for command byte */
                dev->state = DEV_STATE_MAIN_WANT_IN;
                break;

            case 0xf4: /* enable */
                mouse_ps2_log("%s: Enable\n", dev->name);
                mouse_scan = 1;
                kbc_at_dev_queue_add(dev, 0xfa, 0);
                break;

            case 0xf5: /* disable */
                mouse_ps2_log("%s: Disable\n", dev->name);
                mouse_scan = 0;
                kbc_at_dev_queue_add(dev, 0xfa, 0);
                break;

            case 0xf6: /* set defaults */
                mouse_ps2_log("%s: Set defaults\n", dev->name);
                ps2_set_defaults(dev);
                kbc_at_dev_queue_add(dev, 0xfa, 0);
                break;

            case 0xff: /* reset */
                mouse_ps2_log("%s: Reset\n", dev->name);
                kbc_at_dev_reset(dev, 1);
                break;

            default:
                kbc_at_dev_queue_add(dev, 0xfe, 0);
        }
    }

    if (dev->flags & FLAG_INTELLI) {
        for (temp = 0; temp < 5; temp++)
            last_data[temp] = last_data[temp + 1];

        last_data[5] = val;

        if ((last_data[0] == 0xf3) && (last_data[1] == 0xc8) &&
            (last_data[2] == 0xf3) && (last_data[3] == 0x64) &&
            (last_data[4] == 0xf3) && (last_data[5] == 0x50))
            dev->flags |= FLAG_INTMODE;

        if ((dev->flags & FLAG_EXPLORER) && (dev->flags & FLAG_INTMODE) &&
            (last_data[0] == 0xf3) && (last_data[1] == 0xc8) &&
            (last_data[2] == 0xf3) && (last_data[3] == 0xc8) &&
            (last_data[4] == 0xf3) && (last_data[5] == 0x50))
            dev->flags |= FLAG_5BTN;
    }
}

static int
ps2_poll(int x, int y, int z, int b, UNUSED(double abs_x), UNUSED(double abs_y), void *priv)
{
    atkbc_dev_t *dev = (atkbc_dev_t *) priv;
    int packet_size = (dev->flags & FLAG_INTMODE) ? 4 : 3;

    if (!mouse_scan || (!x && !y && !z && (b == dev->b)))
        return 0xff;

    if ((dev->mode == MODE_STREAM) && (kbc_at_dev_queue_pos(dev, 1) < (FIFO_SIZE - packet_size))) {
        dev->x = x;
        dev->y = -y;
        dev->z = -z;
        dev->b = b;
    } else {
        dev->x += x;
        dev->y -= y;
        dev->z -= z;
        dev->b = b;
    }

    if ((dev->mode == MODE_STREAM) && (kbc_at_dev_queue_pos(dev, 1) < (FIFO_SIZE - packet_size)))
        ps2_report_coordinates(dev, 1);

    return 0;
}

/*
 * Initialize the device for use by the user.
 *
 * We also get called from the various machines.
 */
void *
mouse_ps2_init(const device_t *info)
{
    atkbc_dev_t *dev = kbc_at_dev_init(DEV_AUX);
    int      i;

    dev->name = info->name;
    dev->type = info->local;

    dev->mode = MODE_STREAM;
    i         = device_get_config_int("buttons");
    if (i > 2)
        dev->flags |= FLAG_INTELLI;
    if (i > 4)
        dev->flags |= FLAG_EXPLORER;

    if (i >= 4)
        i = 3;

    mouse_ps2_log("%s: buttons=%d\n", dev->name, i);

    /* Tell them how many buttons we have. */
    mouse_set_buttons(i);

    dev->process_cmd = ps2_write;
    dev->execute_bat = ps2_bat;

    dev->scan        = &mouse_scan;

    dev->fifo_mask   = FIFO_SIZE - 1;

    if (dev->port != NULL)
        kbc_at_dev_reset(dev, 0);

    /* Return our private data to the I/O layer. */
    return dev;
}

static void
ps2_close(void *priv)
{
    atkbc_dev_t *dev = (atkbc_dev_t *) priv;

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
