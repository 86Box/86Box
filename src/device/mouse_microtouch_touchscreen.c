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
#include <86box/video.h>
#include <86box/nvr.h>

#define NVR_SIZE    16

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
    char         cmd[256];
    double       abs_x, abs_x_old, abs_y, abs_y_old;
    float        scale_x, scale_y, off_x, off_y;
    int          but, but_old;
    int          baud_rate, cmd_pos;
    uint8_t      format, mode;
    uint8_t      id, cal_cntr, pen_mode;
    bool         mode_status, cal_ex, soh;   
    bool         in_reset, reset;
    uint8_t     *nvr;
    char         nvr_path[64];    
    serial_t    *serial;
    Fifo8        resp;
    pc_timer_t   host_to_serial_timer;
    pc_timer_t   reset_timer;
} mouse_microtouch_t;

static mouse_microtouch_t *mtouch_inst = NULL;

static void
mtouch_savenvr(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;
    
    FILE *fp;

    fp = nvr_fopen(dev->nvr_path, "wb");
    if (fp) {
        fwrite(dev->nvr, 1, NVR_SIZE, fp);
        fclose(fp);
        fp = NULL;
    }
}

static void
mtouch_writenvr(void *priv, float scale_x, float scale_y, float off_x, float off_y)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;
    
    memcpy(&dev->nvr[0], &scale_x, 4);
    memcpy(&dev->nvr[4], &scale_y, 4);
    memcpy(&dev->nvr[8], &off_x, 4);
    memcpy(&dev->nvr[12], &off_y, 4);
}

static void
mtouch_readnvr(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;
    memcpy(&dev->scale_x, &dev->nvr[0], 4);
    memcpy(&dev->scale_y, &dev->nvr[4], 4);
    memcpy(&dev->off_x, &dev->nvr[8], 4);
    memcpy(&dev->off_y, &dev->nvr[12], 4);
    
    pclog("MT NVR CAL: scale_x=%f, scale_y=%f, off_x=%f, off_y=%f\n", dev->scale_x, dev->scale_y, dev->off_x, dev->off_y);
}

static void
mtouch_initnvr(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;
    FILE *fp;

    /* Allocate and initialize the EEPROM. */
    dev->nvr = (uint8_t *) malloc(NVR_SIZE);
    memset(dev->nvr, 0x00, NVR_SIZE);

    fp = nvr_fopen(dev->nvr_path, "rb");
    if (fp) {
        if (fread(dev->nvr, 1, NVR_SIZE, fp) != NVR_SIZE)
            fatal("mtouch_initnvr(): Error reading data\n");
        fclose(fp);
        fp = NULL;
    } else
        mtouch_writenvr(dev, 1, 1, 0, 0);
}

static void
mtouch_reset_complete(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;
    
    dev->reset = true;
    dev->in_reset = false;
    fifo8_push_all(&dev->resp, (uint8_t *) "\x01\x30\x0D", 3); /* <SOH>0<CR>  */
}

static void
mtouch_calibrate_timer(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;
    
    if ((dev->cal_cntr == 2 && (dev->abs_x > 0.25 || dev->abs_y < 0.75)) || \
        (dev->cal_cntr == 1 && (dev->abs_x < 0.75 || dev->abs_y > 0.25))) {
        return;
    }
    
    dev->cal_cntr--;    
    fifo8_push_all(&dev->resp, (uint8_t *) "\x01\x31\x0D", 3); /* <SOH>1<CR>  */
    
    if (dev->cal_ex) {
        if (!dev->cal_cntr) {
            double x1_ref = 0.125;
            double y1_ref = 0.875;
            double x2_ref = 0.875;
            double y2_ref = 0.125;
            double x1 = dev->abs_x_old;
            double y1 = dev->abs_y_old;
            double x2 = dev->abs_x;
            double y2 = dev->abs_y;
            
            dev->scale_x = (x2_ref - x1_ref) / (x2 - x1);
            dev->off_x   = x1_ref - dev->scale_x * x1;
            dev->scale_y = (y2_ref - y1_ref) / (y2 - y1);
            dev->off_y   = y1_ref - dev->scale_y * y1;
            dev->cal_ex = false;
            
            pclog("MT NEW CAL: scale_x=%f, scale_y=%f, off_x=%f, off_y=%f\n", dev->scale_x, dev->scale_y, dev->off_x, dev->off_y);
            mtouch_writenvr(dev, dev->scale_x, dev->scale_y, dev->off_x, dev->off_y);
            mtouch_savenvr(dev);
        }
        dev->abs_x_old = dev->abs_x;
        dev->abs_y_old = dev->abs_y;
    }    
}

static void
mtouch_process_commands(mouse_microtouch_t *dev)
{
    dev->cmd[strcspn(dev->cmd, "\r")] = '\0';
    pclog("MT Command: %s\n", dev->cmd);
    
    if (dev->cmd[0] == 'C' && dev->cmd[1] == 'N') {      /* Calibrate New */
        dev->cal_cntr = 2;
    }
    else if (dev->cmd[0] == 'C' && dev->cmd[1] == 'X') { /* Calibrate Extended */
        dev->scale_x = 1;
        dev->scale_y = 1;
        dev->off_x   = 0;
        dev->off_y   = 0;
        dev->cal_ex = true;
        dev->cal_cntr = 2;
    }
    else if (dev->cmd[0] == 'F' && dev->cmd[1] == 'D') { /* Format Decimal */
        dev->format = FORMAT_DEC;
        dev->mode_status = false;
    }
    else if (dev->cmd[0] == 'F' && dev->cmd[1] == 'O') { /* Finger Only */
        dev->pen_mode = 1;
    }
    else if (dev->cmd[0] == 'F' && dev->cmd[1] == 'H') { /* Format Hexadecimal */
        dev->format = FORMAT_HEX;
        dev->mode_status = false;
    }
    else if (dev->cmd[0] == 'F' && dev->cmd[1] == 'R') { /* Format Raw */
        dev->format = FORMAT_RAW;
        dev->mode = MODE_INACTIVE;
        dev->cal_cntr = 0;
    }
    else if (dev->cmd[0] == 'F' && dev->cmd[1] == 'T') { /* Format Tablet */
        dev->format = FORMAT_TABLET;
    }
    else if (dev->cmd[0] == 'G' && dev->cmd[1] == 'P' && dev->cmd[2] == '1') { /* Get Parameter Block 1 */
        fifo8_push_all(&dev->resp, (uint8_t *) "\x01\x41\x0D", 3); /* <SOH>A<CR>  */
        fifo8_push_all(&dev->resp, (uint8_t *) "0000000000000000000000000\r", 26);
    }
    else if (dev->cmd[0] == 'M' && dev->cmd[1] == 'D' && dev->cmd[2] == 'U') { /* Mode Down/Up */
        dev->mode = MODE_DOWNUP;
    }
    else if (dev->cmd[0] == 'M' && dev->cmd[1] == 'I') { /* Mode Inactive */
        dev->mode = MODE_INACTIVE;
    }
    else if (dev->cmd[0] == 'M' && dev->cmd[1] == 'P') { /* Mode Point */
        dev->mode = MODE_POINT;
    }
    else if (dev->cmd[0] == 'M' && dev->cmd[1] == 'T') { /* Mode Status */
        dev->mode_status = true;
    }
    else if (dev->cmd[0] == 'M' && dev->cmd[1] == 'S') { /* Mode Stream */
        dev->mode = MODE_STREAM;
    }
    else if (dev->cmd[0] == 'O' && dev->cmd[1] == 'I') { /* Output Identity */
        fifo8_push(&dev->resp, 0x01);
        fifo8_push_all(&dev->resp, (uint8_t *) mtouch_identity[dev->id], 6);
        fifo8_push(&dev->resp, 0x0D);
        return;
    }
    else if (dev->cmd[0] == 'O' && dev->cmd[1] == 'S') { /* Output Status */
        if (dev->reset) {
            fifo8_push_all(&dev->resp, (uint8_t *) "\x01\x40\x60\x0D", 4);
        } else {
            fifo8_push_all(&dev->resp, (uint8_t *) "\x01\x40\x40\x0D", 4);
        }
        return;
    }
    else if (dev->cmd[0] == 'P') { 
        if (strlen(dev->cmd) == 2) { /* Pen */
            if (dev->cmd[1] == 'F') dev->pen_mode = 3;      /* Pen or Finger */
            else if (dev->cmd[1] == 'O') dev->pen_mode = 2; /* Pen Only */
        } 
        else if (strlen(dev->cmd) == 5) { /* Serial Options */
            if      (dev->cmd[4] == 1) dev->baud_rate = 19200;
            else if (dev->cmd[4] == 2) dev->baud_rate = 9600;
            else if (dev->cmd[4] == 3) dev->baud_rate = 4600;
            else if (dev->cmd[4] == 4) dev->baud_rate = 2400;
            else if (dev->cmd[4] == 5) dev->baud_rate = 1200;
            
            timer_stop(&dev->host_to_serial_timer);
            timer_on_auto(&dev->host_to_serial_timer, (1000000. / dev->baud_rate) * 10);
        }
    }
    else if (dev->cmd[0] == 'R') { /* Reset */
        dev->in_reset = true;
        dev->cal_cntr = 0;
        dev->pen_mode = 3;
        
        if (dev->cmd[0] == 'D') { /* Restore Defaults */
            dev->mode = MODE_STREAM;
            dev->mode_status = false;
            
            if (dev->id < 2) {
                dev->format = FORMAT_DEC;
            } else {
                dev->format = FORMAT_TABLET;
            }
        }
        
        timer_on_auto(&dev->reset_timer, 500. * 1000.);
        return;
    }
    else if (dev->cmd[0] == 'S' && dev->cmd[1] == 'P' && dev->cmd[2] == '1') { /* Set Parameter Block 1 */
        fifo8_push_all(&dev->resp, (uint8_t *) "\x01\x41\x0D", 3); /* <SOH>A<CR>  */
        return;
    }
    else if (dev->cmd[0] == 'U' && dev->cmd[1] == 'T') { /* Unit Type */
        fifo8_push(&dev->resp, 0x01);
        
        if (dev->id == 2) {
            fifo8_push_all(&dev->resp, (uint8_t *) "TP****00", 8);
        } else {
            fifo8_push_all(&dev->resp, (uint8_t *) "QM****00", 8);
        }
        fifo8_push(&dev->resp, 0x0D);
        return;
    }
    
    fifo8_push_all(&dev->resp, (uint8_t *) "\x01\x30\x0D", 3); /* <SOH>0<CR>  */
}

static void
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
            mtouch_process_commands(dev);
        }
    }
}
    
static int
mtouch_prepare_transmit(void *priv)    
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;
    
    char buffer[16];
    double abs_x = dev->abs_x;
    double abs_y = dev->abs_y;
    int but      = dev->but;
    
    if (dev->mode == MODE_INACTIVE) {
        return 0;
    }
    
    if (dev->cal_cntr || (!dev->but && !dev->but_old)) { /* Calibration or no buttonpress */
        if (!dev->but && dev->but_old) {
            mtouch_calibrate_timer(dev);
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

static void
mtouch_write_to_host(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;
    
    if (dev->serial == NULL)
        goto no_write_to_machine;
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
    
    dev->abs_x = dev->scale_x * dev->abs_x + dev->off_x;
    dev->abs_y = dev->scale_y * dev->abs_y + dev->off_y;
    
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
    timer_add(&dev->reset_timer, mtouch_reset_complete, dev, 0);
    timer_on_auto(&dev->host_to_serial_timer, (1000000. / dev->baud_rate) * 10);
    dev->id          = device_get_config_int("identity");
    dev->pen_mode    = 3;
    dev->mode        = MODE_STREAM;
    
    sprintf(dev->nvr_path, "mtouch_%s.nvr", mtouch_identity[dev->id]);
    mtouch_initnvr(dev);
    mtouch_readnvr(dev);
    
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