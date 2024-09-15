/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          3M MicroTouch Serial emulation.
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
    - Add additional SMT2/3 formats as we currently only support Tablet, Hex and Dec.
    - Mode Polled.
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

enum mtouch_modes {
    MODE_DOWNUP   = 1,
    MODE_INACTIVE = 2,
    MODE_POINT    = 3,
    MODE_STREAM   = 4,
};

const char* mtouch_identity[] = {
    "A30100", /* SMT2 Serial / SMT3(R)V */
    "A40100", /* SMT2 PCBus */
    "P50100", /* TouchPen 4(+) */
    "Q10100", /* SMT3(R) Serial */
};

typedef struct mouse_microtouch_t {
    double       baud_rate, abs_x, abs_x_old, abs_y, abs_y_old;
    int          but, but_old;
    char         cmd[256];
    int          cmd_pos;
    uint8_t      format, mode;
    bool         mode_status;
    uint8_t      id, cal_cntr, pen_mode;
    bool         soh;
    bool         in_reset, reset;
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
    
    mtouch->reset = true;
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
    else if (mtouch->cmd[0] == 'F' && mtouch->cmd[1] == 'D') { /* Format Decimal */
        mtouch->format = FORMAT_DEC;
        mtouch->mode_status = false;
    }
    else if (mtouch->cmd[0] == 'F' && mtouch->cmd[1] == 'O') { /* Finger Only */
        mtouch->pen_mode = 1;
    }
    else if (mtouch->cmd[0] == 'F' && mtouch->cmd[1] == 'H') { /* Format Hexadecimal */
        mtouch->format = FORMAT_HEX;
        mtouch->mode_status = false;
    }
    else if (mtouch->cmd[0] == 'F' && mtouch->cmd[1] == 'R') { /* Format Raw */
        mtouch->format = FORMAT_RAW;
        mtouch->mode = MODE_INACTIVE;
        mtouch->cal_cntr = 0;
    }
    else if (mtouch->cmd[0] == 'F' && mtouch->cmd[1] == 'T') { /* Format Tablet */
        mtouch->format = FORMAT_TABLET;
    }
    else if (mtouch->cmd[0] == 'G' && mtouch->cmd[1] == 'P' && mtouch->cmd[2] == '1') { /* Get Parameter Block 1 */
        fifo8_push_all(&mtouch->resp, (uint8_t *) "\x01\x41\x0D", 3); /* <SOH>A<CR>  */
        fifo8_push_all(&mtouch->resp, (uint8_t *) "0000000000000000000000000\r", 26);
    }
    else if (mtouch->cmd[0] == 'M' && mtouch->cmd[1] == 'D' && mtouch->cmd[2] == 'U') { /* Mode Down/Up */
        mtouch->mode = MODE_DOWNUP;
    }
    else if (mtouch->cmd[0] == 'M' && mtouch->cmd[1] == 'I') { /* Mode Inactive */
        mtouch->mode = MODE_INACTIVE;
    }
    else if (mtouch->cmd[0] == 'M' && mtouch->cmd[1] == 'P') { /* Mode Point */
        mtouch->mode = MODE_POINT;
    }
    else if (mtouch->cmd[0] == 'M' && mtouch->cmd[1] == 'T') { /* Mode Status */
        mtouch->mode_status = true;
    }
    else if (mtouch->cmd[0] == 'M' && mtouch->cmd[1] == 'S') { /* Mode Stream */
        mtouch->mode = MODE_STREAM;
    }
    else if (mtouch->cmd[0] == 'O' && mtouch->cmd[1] == 'I') { /* Output Identity */
        fifo8_push(&mtouch->resp, 0x01);
        fifo8_push_all(&mtouch->resp, (uint8_t *) mtouch_identity[mtouch->id], 6);
        fifo8_push(&mtouch->resp, 0x0D);
        return;
    }
    else if (mtouch->cmd[0] == 'O' && mtouch->cmd[1] == 'S') { /* Output Status */
        if (mtouch->reset) {
            fifo8_push_all(&mtouch->resp, (uint8_t *) "\x01\x40\x60\x0D", 4);
        } else {
            fifo8_push_all(&mtouch->resp, (uint8_t *) "\x01\x40\x40\x0D", 4);
        }
        return;
    }
    else if (mtouch->cmd[0] == 'P') { 
        if (strlen(mtouch->cmd) == 2) { /* Pen */
            if (mtouch->cmd[1] == 'F') mtouch->pen_mode = 3;      /* Pen or Finger */
            else if (mtouch->cmd[1] == 'O') mtouch->pen_mode = 2; /* Pen Only */
        } 
        else if (strlen(mtouch->cmd) == 5) { /* Serial Options */
            if      (mtouch->cmd[4] == 1) mtouch->baud_rate = 19200;
            else if (mtouch->cmd[4] == 2) mtouch->baud_rate = 9600;
            else if (mtouch->cmd[4] == 3) mtouch->baud_rate = 4600;
            else if (mtouch->cmd[4] == 4) mtouch->baud_rate = 2400;
            else if (mtouch->cmd[4] == 5) mtouch->baud_rate = 1200;
            
            timer_stop(&mtouch->host_to_serial_timer);
            timer_on_auto(&mtouch->host_to_serial_timer, (1000000. / mtouch->baud_rate) * 10);
        }
    }
    else if (mtouch->cmd[0] == 'R') { /* Reset */
        mtouch->in_reset = true;
        mtouch->cal_cntr = 0;
        mtouch->pen_mode = 3;
        
        if (mtouch->cmd[0] == 'D') { /* Restore Defaults */
            mtouch->mode = MODE_STREAM;
            mtouch->mode_status = false;
            
            if (mtouch->id < 2) {
                mtouch->format = FORMAT_DEC;
            } else {
                mtouch->format = FORMAT_TABLET;
            }
        }
        
        timer_on_auto(&mtouch->reset_timer, 500. * 1000.);
        return;
    }
    else if (mtouch->cmd[0] == 'S' && mtouch->cmd[1] == 'P' && mtouch->cmd[2] == '1') { /* Set Parameter Block 1 */
        fifo8_push_all(&mtouch->resp, (uint8_t *) "\x01\x41\x0D", 3); /* <SOH>A<CR>  */
        return;
    }
    else if (mtouch->cmd[0] == 'U' && mtouch->cmd[1] == 'T') { /* Unit Type */
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
mtouch_write(serial_t *serial, void *priv, uint8_t data)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;
    if (data == '\x1') {
        dev->soh = 1;
    } 
    else if (dev->soh) {
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
mtouch_prepare_transmit(void *priv)    
{
    char buffer[16];
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;
    
    double abs_x = dev->abs_x;
    double abs_y = dev->abs_y;
    int but      = dev->but;
    
    if (dev->mode == MODE_INACTIVE) {
        return 0;
    }
    
    if (dev->cal_cntr || (!dev->but && !dev->but_old)) { /* Calibration or no buttonpress */
        if (!dev->but && dev->but_old) {
            microtouch_calibrate_timer(dev);
        }
        dev->but_old = but; /* Save buttonpress */
        return 0;
    }
    
    if (dev->format == FORMAT_TABLET) {
        if (but) { /* Touchdown/Continuation */
            fifo8_push(&dev->resp, 0b11000000 | ((dev->pen_mode == 2) ? ((1 << 5) | ((but & 3))) : 0));
            fifo8_push(&dev->resp, (uint16_t)(16383 * abs_x) & 0b1111111);
            fifo8_push(&dev->resp, ((uint16_t)(16383 * abs_x) >> 7) & 0b1111111);
            fifo8_push(&dev->resp, (uint16_t)(16383 * (1 - abs_y)) & 0b1111111);
            fifo8_push(&dev->resp, ((uint16_t)(16383 * (1 - abs_y)) >> 7) & 0b1111111);
        } 
        else if (dev->but_old) { /* Liftoff */
            fifo8_push(&dev->resp, 0b10000000 | ((dev->pen_mode == 2) ? ((1 << 5)) : 0));
            fifo8_push(&dev->resp, (uint16_t)(16383 * dev->abs_x_old) & 0b1111111);
            fifo8_push(&dev->resp, ((uint16_t)(16383 * dev->abs_x_old) >> 7) & 0b1111111);
            fifo8_push(&dev->resp, (uint16_t)(16383 * (1 - dev->abs_y_old))& 0b1111111);
            fifo8_push(&dev->resp, ((uint16_t)(16383 * (1 - dev->abs_y_old)) >> 7) & 0b1111111);
        }
    }
    
    else if (dev->format == FORMAT_DEC || dev->format == FORMAT_HEX) {
        if (but) {
            if (!dev->but_old) { /* Touchdown (MS, MP, MDU) */
                fifo8_push(&dev->resp, (dev->mode_status) ? 0x19 : 0x01);
                if (dev->format == FORMAT_DEC){
                    snprintf(buffer, sizeof(buffer), "%03d,%03d\r", (uint16_t)(999 * abs_x), (uint16_t)(999 * (1 - abs_y)));
                } 
                else if (dev->format == FORMAT_HEX) {
                    snprintf(buffer, sizeof(buffer), "%03X,%03X\r", (uint16_t)(1023 * abs_x), (uint16_t)(1023 * (1 - abs_y)));
                }
                fifo8_push_all(&dev->resp, (uint8_t *)buffer, strlen(buffer));
            } 
            else if (dev->mode == MODE_STREAM){ /* Touch Continuation (MS) */
                fifo8_push(&dev->resp, (dev->mode_status) ? 0x1c : 0x01);
                if (dev->format == FORMAT_DEC){
                    snprintf(buffer, sizeof(buffer), "%03d,%03d\r", (uint16_t)(999 * abs_x), (uint16_t)(999 * (1 - abs_y)));
                } 
                else if (dev->format == FORMAT_HEX) {
                    snprintf(buffer, sizeof(buffer), "%03X,%03X\r", (uint16_t)(1023 * abs_x), (uint16_t)(1023 * (1 - abs_y)));
                }
                fifo8_push_all(&dev->resp, (uint8_t *)buffer, strlen(buffer));
            }
        } 
        else if (dev->but_old && dev->mode != MODE_POINT) { /* Touch Liftoff (MS, MDU) */
            fifo8_push(&dev->resp, (dev->mode_status) ? 0x18 : 0x01);
            if (dev->format == FORMAT_DEC) {
                snprintf(buffer, sizeof(buffer), "%03d,%03d\r", (uint16_t)(999 * dev->abs_x_old), (uint16_t)(999 * (1 - dev->abs_y_old)));
            } 
            else if (dev->format == FORMAT_HEX) {
                snprintf(buffer, sizeof(buffer), "%03X,%03X\r", (uint16_t)(1023 * dev->abs_x_old), (uint16_t)(1023 * (1 - dev->abs_y_old)));
            }
            fifo8_push_all(&dev->resp, (uint8_t *)buffer, strlen(buffer));
        }
    }
    
    /* Save old states*/
    dev->abs_x_old = abs_x;
    dev->abs_y_old = abs_y;
    dev->but_old = but;
    return 0;
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
    else {
        mtouch_prepare_transmit(dev);
    }

no_write_to_machine:
    timer_on_auto(&dev->host_to_serial_timer, (1000000.0 / (double) dev->baud_rate) * (double) (1 + 8 + 1));
}

static int
mtouch_poll(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;
    
    dev->but = mouse_get_buttons_ex();
    mouse_get_abs_coords(&dev->abs_x, &dev->abs_y);
    
    if (enable_overscan) {
        int index = mouse_tablet_in_proximity - 1;
        if (mouse_tablet_in_proximity == -1) {
            mouse_tablet_in_proximity = 0;
        }
        
        dev->abs_x *= monitors[index].mon_unscaled_size_x - 1;
        dev->abs_y *= monitors[index].mon_efscrnsz_y - 1;
        
        if (dev->abs_x <= (monitors[index].mon_overscan_x / 2.)) {
            dev->abs_x = (monitors[index].mon_overscan_x / 2.);
        }
        if (dev->abs_y <= (monitors[index].mon_overscan_y / 2.)) {
            dev->abs_y = (monitors[index].mon_overscan_y / 2.);
        }
        dev->abs_x -= (monitors[index].mon_overscan_x / 2.);
        dev->abs_y -= (monitors[index].mon_overscan_y / 2.);
        dev->abs_x = dev->abs_x / (double) monitors[index].mon_xsize;
        dev->abs_y = dev->abs_y / (double) monitors[index].mon_ysize;
    }
    
    if (dev->abs_x >= 1.0) dev->abs_x = 1.0;
    if (dev->abs_y >= 1.0) dev->abs_y = 1.0;
    if (dev->abs_x <= 0.0) dev->abs_x = 0.0;
    if (dev->abs_y <= 0.0) dev->abs_y = 0.0;
    
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
    dev->baud_rate = 9600;
    serial_set_cts(dev->serial, 1);
    serial_set_dsr(dev->serial, 1);
    serial_set_dcd(dev->serial, 1);
    
    fifo8_create(&dev->resp, 256);
    timer_add(&dev->host_to_serial_timer, mtouch_write_to_host, dev, 0);
    timer_add(&dev->reset_timer, microtouch_reset_complete, dev, 0);
    timer_on_auto(&dev->host_to_serial_timer, (1000000. / dev->baud_rate) * 10);
    dev->id          = device_get_config_int("identity");
    dev->pen_mode    = 3;
    dev->mode        = MODE_STREAM;
    
    if (dev->id < 2) { /* legacy controllers */
        dev->format = FORMAT_DEC;
    } else {
        dev->format = FORMAT_TABLET;
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
    if (dev && dev->serial && dev->serial->sd) {
        memset(dev->serial->sd, 0, sizeof(serial_device_t));
    }
    
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