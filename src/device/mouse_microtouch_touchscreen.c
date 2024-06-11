/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          3M MicroTouch SMT3 emulation.
 *
 *
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2024 Cacodemon345
 */

/* Reference: https://www.touchwindow.com/mm5/drivers/mtsctlrm.pdf */

/* TODO:
    Properly implement GP/SP commands (formats are not documented at all, like anywhere; no dumps yet).
    - Dynamic baud rate selection from software following this.
*/
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/mouse.h>
#include <86box/serial.h>
#include <86box/plat.h>
#include <86box/fifo8.h>
#include <86box/fifo.h>
#include <86box/video.h> /* Needed to account for overscan. */

enum mtouch_modes {
    MODE_TABLET = 1,
    MODE_RAW    = 2
};

typedef struct mouse_microtouch_t {
    double     abs_x;
    double     abs_y;
    double     baud_rate;
    int        b;
    char       cmd[512];
    int        cmd_pos;
    int        mode;
    uint8_t    cal_cntr, pen_mode;
    bool       soh;
    bool       in_reset;
    serial_t  *serial;
    Fifo8      resp;
    pc_timer_t host_to_serial_timer;
    pc_timer_t reset_timer;
} mouse_microtouch_t;

static mouse_microtouch_t *mtouch_inst = NULL;

void
microtouch_reset_complete(void *priv)
{
    mouse_microtouch_t *mtouch = (mouse_microtouch_t *) priv;

    mtouch->in_reset = false;
    fifo8_push(&mtouch->resp, 1);
    fifo8_push_all(&mtouch->resp, (uint8_t *) "0\r", 2);
}

void
microtouch_calibrate_timer(void *priv)
{
    mouse_microtouch_t *mtouch = (mouse_microtouch_t *) priv;

    if (!fifo8_num_used(&mtouch->resp)) {
        mtouch->cal_cntr--;
        fifo8_push(&mtouch->resp, 1);
        fifo8_push_all(&mtouch->resp, (uint8_t *) "1\r", 2);
    }
}

void
microtouch_process_commands(mouse_microtouch_t *mtouch)
{
    int i                                   = 0;
    int fifo_used                           = fifo8_num_used(&mtouch->resp);
    mtouch->cmd[strcspn(mtouch->cmd, "\r")] = '\0';
    mtouch->cmd_pos                         = 0;
    for (i = 0; i < strlen(mtouch->cmd); i++) {
        mtouch->cmd[i] = toupper(mtouch->cmd[i]);
    }
    if (mtouch->cmd[0] == 'Z') {
        fifo8_push(&mtouch->resp, 1);
        fifo8_push_all(&mtouch->resp, (uint8_t *) "0\r", 2);
    }
    if (mtouch->cmd[0] == 'F' && mtouch->cmd[1] == 'O') {
        mtouch->pen_mode = 1;
        fifo8_push(&mtouch->resp, 1);
        fifo8_push_all(&mtouch->resp, (uint8_t *) "0\r", 2);
    }
    if (mtouch->cmd[0] == 'U' && mtouch->cmd[1] == 'T') {
        fifo8_push(&mtouch->resp, 1);
        fifo8_push_all(&mtouch->resp, (uint8_t *) "TP****00\r", sizeof("TP****00\r") - 1);
    }
    if (mtouch->cmd[0] == 'O' && mtouch->cmd[1] == 'I') {
        fifo8_push(&mtouch->resp, 1);
        fifo8_push_all(&mtouch->resp, (uint8_t *) "P50200\r", sizeof("P50200\r") - 1);
    }
    if (mtouch->cmd[0] == 'F' && mtouch->cmd[1] == 'T') {
        mtouch->mode = MODE_TABLET;
        fifo8_push(&mtouch->resp, 1);
        fifo8_push_all(&mtouch->resp, (uint8_t *) "0\r", 2);
    }
    if (mtouch->cmd[0] == 'F' && mtouch->cmd[1] == 'R') {
        mtouch->mode     = MODE_RAW;
        mtouch->cal_cntr = 0;
        fifo8_push(&mtouch->resp, 1);
        fifo8_push_all(&mtouch->resp, (uint8_t *) "0\r", 2);
    }
    if (mtouch->cmd[0] == 'M' && mtouch->cmd[1] == 'S') {
        fifo8_push(&mtouch->resp, 1);
        fifo8_push_all(&mtouch->resp, (uint8_t *) "0\r", 2);
    }
    if (mtouch->cmd[0] == 'R') {
        mtouch->in_reset = true;
        mtouch->mode     = MODE_TABLET;
        mtouch->cal_cntr = 0;
        mtouch->pen_mode = 3;
        timer_on_auto(&mtouch->reset_timer, 500. * 1000.);
    }
    if (mtouch->cmd[0] == 'A' && (mtouch->cmd[1] == 'D' || mtouch->cmd[1] == 'E')) {
        fifo8_push(&mtouch->resp, 1);
        fifo8_push_all(&mtouch->resp, (uint8_t *) "0\r", 2);
    }
    if (mtouch->cmd[0] == 'N' && mtouch->cmd[1] == 'M') {
        fifo8_push(&mtouch->resp, 1);
        fifo8_push_all(&mtouch->resp, (uint8_t *) "1\r", 2);
    }
    if (mtouch->cmd[0] == 'F' && mtouch->cmd[1] == 'Q') {
        fifo8_push(&mtouch->resp, 1);
        fifo8_push_all(&mtouch->resp, (uint8_t *) "1\r", 2);
    }
    if (mtouch->cmd[0] == 'G' && mtouch->cmd[1] == 'F') {
        fifo8_push(&mtouch->resp, 1);
        fifo8_push_all(&mtouch->resp, (uint8_t *) "1\r", 2);
    }
    if (mtouch->cmd[0] == 'P') {
        if (mtouch->cmd[1] == 'F') mtouch->pen_mode = 3;
        else if (mtouch->cmd[1] == 'O') mtouch->pen_mode = 2;
        fifo8_push(&mtouch->resp, 1);
        fifo8_push_all(&mtouch->resp, (uint8_t *) "0\r", 2);
    }
    if (mtouch->cmd[0] == 'C' && (mtouch->cmd[1] == 'N' || mtouch->cmd[1] == 'X')) {
        fifo8_push(&mtouch->resp, 1);
        fifo8_push_all(&mtouch->resp, (uint8_t *) "0\r", 2);
        mtouch->cal_cntr = 2;
    }
    if (mtouch->cmd[0] == 'G' && mtouch->cmd[1] == 'P' && mtouch->cmd[2] == '1') {
        fifo8_push(&mtouch->resp, 1);
        fifo8_push_all(&mtouch->resp, (uint8_t *) "A\r", 2);
        fifo8_push_all(&mtouch->resp, (uint8_t *) "0000000000000000000000000\r", sizeof("0000000000000000000000000\r") - 1);
        fifo8_push(&mtouch->resp, 1);
        fifo8_push_all(&mtouch->resp, (uint8_t *) "0\r", 2);
    }
    if (mtouch->cmd[0] == 'S' && mtouch->cmd[1] == 'P' && mtouch->cmd[2] == '1') {
        fifo8_push(&mtouch->resp, 1);
        fifo8_push_all(&mtouch->resp, (uint8_t *) "A\r", 2);
    }
    if (fifo8_num_used(&mtouch->resp) != fifo_used)
        pclog("Command received: %s\n", mtouch->cmd);
}

void
mtouch_write_to_host(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;
    if ((dev->serial->type >= SERIAL_16550) && dev->serial->fifo_enabled) {
        if (fifo_get_full(dev->serial->rcvr_fifo)) {
            goto no_write_to_machine;
        }
    } else {
        if (dev->serial->lsr & 1) {
            goto no_write_to_machine;
        }
    }

    if (dev->in_reset)
        goto no_write_to_machine;

    if (fifo8_num_used(&dev->resp)) {
        serial_write_fifo(dev->serial, fifo8_pop(&dev->resp));
    }

no_write_to_machine:
    timer_on_auto(&dev->host_to_serial_timer, (1000000.0 / (double) 9600.0) * (double) (1 + 8 + 1));
}

void
mtouch_write(serial_t *serial, void *priv, uint8_t data)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;
    if (data == '\x1') {
        dev->soh = 1;
    } else if (dev->soh) {
        if (data != '\r') {
            dev->cmd[dev->cmd_pos++] = data;
        } else {
            dev->cmd[dev->cmd_pos++] = data;
            microtouch_process_commands(dev);
        }
    }
}

static int
mtouch_poll(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;

    if (dev->mode != MODE_RAW && fifo8_num_free(&dev->resp) >= 10) {
        unsigned int abs_x_int = 0, abs_y_int = 0;
        double       abs_x;
        double       abs_y;
        int          b = mouse_get_buttons_ex();
        mouse_get_abs_coords(&abs_x, &abs_y);
        dev->b |= !!(b & 3);
        
        if (abs_x >= 1.0)
            abs_x = 1.0;
        if (abs_y >= 1.0)
            abs_y = 1.0;
        if (abs_x <= 0.0)
            abs_x = 0.0;
        if (abs_y <= 0.0)
            abs_y = 0.0;
        if (enable_overscan) {
            int index = mouse_tablet_in_proximity - 1;
            if (mouse_tablet_in_proximity == -1)
                mouse_tablet_in_proximity = 0;

            abs_x *= monitors[index].mon_unscaled_size_x - 1;
            abs_y *= monitors[index].mon_efscrnsz_y - 1;

            if (abs_x <= (monitors[index].mon_overscan_x / 2.)) {
                abs_x = (monitors[index].mon_overscan_x / 2.);
            }
            if (abs_y <= (monitors[index].mon_overscan_y / 2.)) {
                abs_y = (monitors[index].mon_overscan_y / 2.);
            }
            abs_x -= (monitors[index].mon_overscan_x / 2.);
            abs_y -= (monitors[index].mon_overscan_y / 2.);
            abs_x = abs_x / (double) monitors[index].mon_xsize;
            abs_y = abs_y / (double) monitors[index].mon_ysize;
            if (abs_x >= 1.0)
                abs_x = 1.0;
            if (abs_y >= 1.0)
                abs_y = 1.0;
        }
        if (dev->cal_cntr && (!(dev->b & 1) && !!(b & 3))) {
            dev->b |= 1;
        } else if (dev->cal_cntr && ((dev->b & 1) && !(b & 3))) {
            dev->b &= ~1;
            microtouch_calibrate_timer(dev);
        }
        if (dev->cal_cntr) {
            return 0;
        }
        if (!!(b & 3)) {
            dev->abs_x = abs_x;
            dev->abs_y = abs_y;
            dev->b |= 1;

            abs_x_int = abs_x * 16383;
            abs_y_int = 16383 - abs_y * 16383;

            fifo8_push(&dev->resp, 0b11000000 | ((dev->pen_mode == 2) ? ((1 << 5) | ((b & 3))) : 0));
            fifo8_push(&dev->resp, abs_x_int & 0b1111111);
            fifo8_push(&dev->resp, (abs_x_int >> 7) & 0b1111111);
            fifo8_push(&dev->resp, abs_y_int & 0b1111111);
            fifo8_push(&dev->resp, (abs_y_int >> 7) & 0b1111111);
        } else if ((dev->b & 1) && !(b & 3)) {
            dev->b &= ~1;
            abs_x_int = dev->abs_x * 16383;
            abs_y_int = 16383 - dev->abs_y * 16383;
            fifo8_push(&dev->resp, 0b11000000 | ((dev->pen_mode == 2) ? ((1 << 5)) : 0));
            fifo8_push(&dev->resp, abs_x_int & 0b1111111);
            fifo8_push(&dev->resp, (abs_x_int >> 7) & 0b1111111);
            fifo8_push(&dev->resp, abs_y_int & 0b1111111);
            fifo8_push(&dev->resp, (abs_y_int >> 7) & 0b1111111);
            fifo8_push(&dev->resp, 0b10000000 | ((dev->pen_mode == 2) ? ((1 << 5)) : 0));
            fifo8_push(&dev->resp, abs_x_int & 0b1111111);
            fifo8_push(&dev->resp, (abs_x_int >> 7) & 0b1111111);
            fifo8_push(&dev->resp, abs_y_int & 0b1111111);
            fifo8_push(&dev->resp, (abs_y_int >> 7) & 0b1111111);
        }
    }
    return 0;
}

static void
mtouch_poll_global(void)
{
    mtouch_poll(mtouch_inst);
}

void *
mtouch_init(const device_t *info)
{
    mouse_microtouch_t *dev = calloc(1, sizeof(mouse_microtouch_t));

    dev->serial = serial_attach(device_get_config_int("port"), NULL, mtouch_write, dev);
    dev->baud_rate = device_get_config_int("baudrate");
    fifo8_create(&dev->resp, 512);
    timer_add(&dev->host_to_serial_timer, mtouch_write_to_host, dev, 0);
    timer_add(&dev->reset_timer, microtouch_reset_complete, dev, 0);
    timer_on_auto(&dev->host_to_serial_timer, (1000000. / dev->baud_rate) * 10);
    dev->mode        = MODE_TABLET;
    dev->pen_mode    = 3;
    mouse_input_mode = 1;
    mouse_set_buttons(2);
    mouse_set_poll_ex(mtouch_poll_global);

    mtouch_inst = dev;

    return dev;
}

void
mtouch_close(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;

    fifo8_destroy(&dev->resp);
    /* Detach serial port from the mouse. */
    if (dev && dev->serial && dev->serial->sd)
        memset(dev->serial->sd, 0, sizeof(serial_device_t));

    free(dev);
    mtouch_inst = NULL;
}

static const device_config_t mtouch_config[] = {
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
    {
        .name = "baudrate",
        .description = "Baud Rate",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 9600,
        .file_filter = NULL,
        .spinner = { 0 },
        .selection = {
            { .description =  "19200", .value =  19200 },
            { .description =   "9600", .value =   9600 },
            { .description =   "4800", .value =   4800 },
            { .description =   "2400", .value =   2400 },
            { .description =   "1200", .value =   1200 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t mouse_mtouch_device = {
    .name          = "3M MicroTouch TouchPen 4",
    .internal_name = "microtouch_touchpen",
    .flags         = DEVICE_COM,
    .local         = 0,
    .init          = mtouch_init,
    .close         = mtouch_close,
    .reset         = NULL,
    { .poll = mtouch_poll },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = mtouch_config
};
