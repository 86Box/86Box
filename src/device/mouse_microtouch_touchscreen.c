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
 * Authors: Cacodemon345, mourix
 *
 *          Copyright 2024 Cacodemon345
 */

/* Reference: https://www.touchwindow.com/mm5/drivers/mtsctlrm.pdf */

/* TODO:
    - Properly implement GP/SP commands (formats are not documented at all, like anywhere; no dumps yet).
    - Decouple serial packet generation from mouse poll rate.
    - Dynamic baud rate selection from software following this.
    - Add additional SMT2/3 formats as we currently only support Tablet, Hex and Dec.
    - Add additional SMT2/3 modes as we currently hardcode Mode Stream.
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

enum mtouch_formats {
    FORMAT_DEC    = 1,
    FORMAT_HEX    = 2,
    FORMAT_RAW    = 3,
    FORMAT_TABLET = 4
};

const char* mtouch_identity[] = {
    "A30100", /* SMT2 Serial / SMT3(R)V */
    "A40100", /* SMT2 PCBus */
    "P50100", /* TouchPen 4(+) */
    "Q10100", /* SMT3(R) Serial */
};

typedef struct mouse_microtouch_t {
    double       baud_rate;
    unsigned int abs_x_int, abs_y_int;
    int          b;
    char         cmd[256];
    int          cmd_pos;
    uint8_t      format;
    bool         mode_status;
    uint8_t      id, cal_cntr, pen_mode;
    bool         soh;
    bool         in_reset;
    serial_t    *serial;
    Fifo8        resp;
    pc_timer_t   host_to_serial_timer;
    pc_timer_t   reset_timer;
} mouse_microtouch_t;

static mouse_microtouch_t *mtouch_inst = NULL;

void
microtouch_reset_complete(void *priv)
{
    mouse_microtouch_t *mtouch = (mouse_microtouch_t *) priv;
    
    mtouch->in_reset = false;
    fifo8_push_all(&mtouch->resp, (uint8_t *) "\x01\x30\x0D", 3); /* <SOH>0<CR>  */
}

void
microtouch_calibrate_timer(void *priv)
{
    mouse_microtouch_t *mtouch = (mouse_microtouch_t *) priv;
    
    if (!fifo8_num_used(&mtouch->resp)) {
        mtouch->cal_cntr--;
        fifo8_push_all(&mtouch->resp, (uint8_t *) "\x01\x31\x0D", 3); /* <SOH>1<CR>  */
    }
}

void
microtouch_process_commands(mouse_microtouch_t *mtouch)
{
    mtouch->cmd[strcspn(mtouch->cmd, "\r")] = '\0';
    pclog("MT Command: %s\n", mtouch->cmd);
    
    if (mtouch->cmd[0] == 'C' && (mtouch->cmd[1] == 'N' || mtouch->cmd[1] == 'X')) { /* Calibrate New/Extended */
        mtouch->cal_cntr = 2;
    }
    if (mtouch->cmd[0] == 'F' && mtouch->cmd[1] == 'D') { /* Format Decimal */
        mouse_set_sample_rate(106);
        mtouch->format = FORMAT_DEC;
    }
    if (mtouch->cmd[0] == 'F' && mtouch->cmd[1] == 'O') { /* Finger Only */
        mtouch->pen_mode = 1;
    }
    if (mtouch->cmd[0] == 'F' && mtouch->cmd[1] == 'H') { /* Format Hexadecimal */
        mouse_set_sample_rate(106);
        mtouch->format = FORMAT_HEX;
    }
    if (mtouch->cmd[0] == 'F' && mtouch->cmd[1] == 'R') { /* Format Raw */
        mouse_set_sample_rate(106);
        mtouch->format = FORMAT_RAW;
        mtouch->cal_cntr = 0;
    }
    if (mtouch->cmd[0] == 'F' && mtouch->cmd[1] == 'T') { /* Format Tablet */
        mouse_set_sample_rate(192);
        mtouch->format = FORMAT_TABLET;
    }
    if (mtouch->cmd[0] == 'G' && mtouch->cmd[1] == 'P' && mtouch->cmd[2] == '1') { /* Get Parameter Block 1 */
        fifo8_push_all(&mtouch->resp, (uint8_t *) "\x01\x41\x0D", 3); /* <SOH>A<CR>  */
        fifo8_push_all(&mtouch->resp, (uint8_t *) "0000000000000000000000000\r", 26);
    }
    if (mtouch->cmd[0] == 'M' && mtouch->cmd[1] == 'T') { /* Mode Status */
        mtouch->mode_status = true;
    }
    if (mtouch->cmd[0] == 'O' && mtouch->cmd[1] == 'I') { /* Output Identity */
        fifo8_push(&mtouch->resp, 0x01);
        fifo8_push_all(&mtouch->resp, (uint8_t *) mtouch_identity[mtouch->id], 6);
        fifo8_push(&mtouch->resp, 0x0D);
        return;
    }
    if (mtouch->cmd[0] == 'P') { 
        if (mtouch->cmd[1] == 'F') mtouch->pen_mode = 3;      /* Pen or Finger */
        else if (mtouch->cmd[1] == 'O') mtouch->pen_mode = 2; /* Pen Only */
    }
    if (mtouch->cmd[0] == 'R') { /* Reset */
        mtouch->in_reset = true;
        mtouch->cal_cntr = 0;
        mtouch->pen_mode = 3;
        mtouch->mode_status = false;
        
        if (mtouch->id < 2) {
            mouse_set_sample_rate(106);
            mtouch->format = FORMAT_DEC;
        } else {
            mouse_set_sample_rate(192);
            mtouch->format = FORMAT_TABLET;
        }
        
        timer_on_auto(&mtouch->reset_timer, 500. * 1000.);
        return;
    }
    if (mtouch->cmd[0] == 'S' && mtouch->cmd[1] == 'P' && mtouch->cmd[2] == '1') { /* Set Parameter Block 1 */
        fifo8_push_all(&mtouch->resp, (uint8_t *) "\x01\x41\x0D", 3); /* <SOH>A<CR>  */
        return;
    }
    if (mtouch->cmd[0] == 'U' && mtouch->cmd[1] == 'T') { /* Unit Type */
        fifo8_push(&mtouch->resp, 0x01);
        
        if (mtouch->id == 2) {
            fifo8_push_all(&mtouch->resp, (uint8_t *) "TP****00", 8);
        } else {
            fifo8_push_all(&mtouch->resp, (uint8_t *) "QM****00", 8);
        }
        fifo8_push(&mtouch->resp, 0x0D);
        return;
    }
    
    fifo8_push_all(&mtouch->resp, (uint8_t *) "\x01\x30\x0D", 3); /* <SOH>0<CR>  */
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
    if (dev->in_reset) {
        goto no_write_to_machine;
    }
    if (fifo8_num_used(&dev->resp)) {
        serial_write_fifo(dev->serial, fifo8_pop(&dev->resp));
    }

no_write_to_machine:
    timer_on_auto(&dev->host_to_serial_timer, (1000000.0 / (double) dev->baud_rate) * (double) (1 + 8 + 1));
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
            dev->soh = 0;
            
            if (!dev->cmd_pos) {
                return;
            }
            
            dev->cmd[dev->cmd_pos++] = data;
            dev->cmd_pos = 0;
            microtouch_process_commands(dev);
        }
    }
}

static int
mtouch_poll(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;
    
    if (fifo8_num_free(&dev->resp) <= 256 - 10 || dev->format == FORMAT_RAW) {
        return 0;
    }
    
    unsigned int abs_x_int = 0, abs_y_int = 0;
    double       abs_x;
    double       abs_y;
    int          b = mouse_get_buttons_ex();
    
    mouse_get_abs_coords(&abs_x, &abs_y);
    
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
        if (mouse_tablet_in_proximity == -1) {
            mouse_tablet_in_proximity = 0;
        }
        
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
    
    if (dev->cal_cntr || (!b && !dev->b)) { /* Calibration or no buttonpress */
        if (!b && dev->b) {
            microtouch_calibrate_timer(dev);
        }
        dev->b = b; /* Save buttonpress */
        return 0;
    }
    
    if (dev->format == FORMAT_DEC) {
        abs_x_int = abs_x * 999;
        abs_y_int = 999 - (abs_y * 999);
        char buffer[10];
        
        if (!dev->mode_status) {
            if (b) { // Touch
                snprintf(buffer, sizeof(buffer), "\x1%03d,%03d\r", abs_x_int, abs_y_int);
                fifo8_push_all(&dev->resp, (uint8_t *)buffer, strlen(buffer));
            }
        } else {
            if (b) {
                if (!dev->b) { /* Touchdown Status */
                    snprintf(buffer, sizeof(buffer), "\x19%03d,%03d\r", abs_x_int, abs_y_int);
                } else { /* Touch Continuation Status */
                    snprintf(buffer, sizeof(buffer), "\x1c%03d,%03d\r", abs_x_int, abs_y_int);
                }
            } else if (dev->b) { /* Liftoff Status */
                snprintf(buffer, sizeof(buffer), "\x18%03d,%03d\r", dev->abs_x_int, dev->abs_y_int);
            }
            fifo8_push_all(&dev->resp, (uint8_t *)buffer, strlen(buffer));
        }
    }
    
    else if (dev->format == FORMAT_HEX) {
        abs_x_int = abs_x * 1023;
        abs_y_int = 1023 - (abs_y * 1023);
        char buffer[10];
        
        if (!dev->mode_status) {
            if (b) { // Touch
                snprintf(buffer, sizeof(buffer), "\x1%03X,%03X\r", abs_x_int, abs_y_int);
                fifo8_push_all(&dev->resp, (uint8_t *)buffer, strlen(buffer));
            }
        } else {
            if (b) {
                if (!dev->b) { /* Touchdown Status */
                    snprintf(buffer, sizeof(buffer), "\x19%03X,%03X\r", abs_x_int, abs_y_int);
                } else { /* Touch Continuation Status */
                    snprintf(buffer, sizeof(buffer), "\x1c%03X,%03X\r", abs_x_int, abs_y_int);
                }
            } else if (dev->b) { /* Liftoff Status */
                snprintf(buffer, sizeof(buffer), "\x18%03X,%03X\r", dev->abs_x_int, dev->abs_y_int);
            }
            fifo8_push_all(&dev->resp, (uint8_t *)buffer, strlen(buffer));
        }
    }
    
    else if (dev->format == FORMAT_TABLET) {
        abs_x_int = abs_x * 16383;
        abs_y_int = 16383 - abs_y * 16383;
        
        if (b) { /* Touchdown/Continuation */
            fifo8_push(&dev->resp, 0b11000000 | ((dev->pen_mode == 2) ? ((1 << 5) | ((b & 3))) : 0));
            fifo8_push(&dev->resp, abs_x_int & 0b1111111);
            fifo8_push(&dev->resp, (abs_x_int >> 7) & 0b1111111);
            fifo8_push(&dev->resp, abs_y_int & 0b1111111);
            fifo8_push(&dev->resp, (abs_y_int >> 7) & 0b1111111);
        } else if (dev->b) { /* Liftoff */
            fifo8_push(&dev->resp, 0b10000000 | ((dev->pen_mode == 2) ? ((1 << 5)) : 0));
            fifo8_push(&dev->resp, dev->abs_x_int & 0b1111111);
            fifo8_push(&dev->resp, (dev->abs_x_int >> 7) & 0b1111111);
            fifo8_push(&dev->resp, dev->abs_y_int & 0b1111111);
            fifo8_push(&dev->resp, (dev->abs_y_int >> 7) & 0b1111111);
        }
    }
    
    /* Save old states*/
    dev->abs_x_int = abs_x_int;
    dev->abs_y_int = abs_y_int;
    dev->b = b; 
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
    serial_set_cts(dev->serial, 1);
    serial_set_dsr(dev->serial, 1);
    serial_set_dcd(dev->serial, 1);
    
    fifo8_create(&dev->resp, 256);
    timer_add(&dev->host_to_serial_timer, mtouch_write_to_host, dev, 0);
    timer_add(&dev->reset_timer, microtouch_reset_complete, dev, 0);
    timer_on_auto(&dev->host_to_serial_timer, (1000000. / dev->baud_rate) * 10);
    dev->id          = device_get_config_int("identity");
    dev->pen_mode    = 3;
    dev->mode_status = false;
    
    if (dev->id < 2) { /* legacy controllers */
        dev->format = FORMAT_DEC;
        mouse_set_sample_rate(106);
    }
    else {
        dev->format = FORMAT_TABLET;
        mouse_set_sample_rate(192);
    }
    
    mouse_input_mode = device_get_config_int("crosshair") + 1;
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
    {
        .name = "identity",
        .description = "Controller",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 0,
        .file_filter = NULL,
        .spinner = { 0 },
        .selection = {
            { .description =  "A3 - SMT2 Serial / SMT3(R)V", .value =   0 },
            { .description =  "A4 - SMT2 PCBus",             .value =   1 },
            { .description =  "P5 - TouchPen 4(+)",          .value =   2 },
            { .description =  "Q1 - SMT3(R) Serial",         .value =   3 }
        }
    },
    {
        .name = "crosshair",
        .description = "Show Crosshair",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t mouse_mtouch_device = {
    .name          = "3M MicroTouch (Serial)",
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
