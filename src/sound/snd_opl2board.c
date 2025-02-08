/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Interface to the OPL2Board External audio device (USB)
 *        
 *
 * Authors: Jose Phillips <jose@hddlive.net>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          Miran Grca, <mgrca8@gmail.com>
 * 
 *          Copyright 2024 Jose Phillips.
 *          Copyright 2017-2020 Fred N. van Kempen.
 *          Copyright 2016-2020 Miran Grca.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mca.h>
#include <86box/sound.h>
#include <86box/timer.h>
#include <86box/snd_opl.h>
#include <86box/plat_unused.h>


#ifdef ENABLE_OPL2DEVICE_LOG
int opl2board_device_do_log = ENABLE_OPL2DEVICE_LOG;

static void
opl2board_device_log(const char *fmt, ...)
{
    va_list ap;

    if (opl2board_device_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define opl2board_device_log(fmt, ...)
#endif

typedef struct opl2board_device_t {
    fm_drv_t opl;
    uint8_t  pos_regs[8];
} opl2board_device_t;

static void
opl2board_device_get_buffer(int32_t *buffer, int len, void *priv)
{
    opl2board_device_t *serial = (opl2board_device_t *) priv;

    const int32_t *opl_buf = serial->opl.update(serial->opl.priv);

    for (int c = 0; c < len * 2; c++)
        buffer[c] += opl_buf[c];

    serial->opl.reset_buffer(serial->opl.priv);
}

uint8_t
opl2board_device_mca_read(int port, void *priv)
{
    const opl2board_device_t *serial = (opl2board_device_t *) priv;

    opl2board_device_log("opl2board_device_mca_read: port=%04x\n", port);

    return serial->pos_regs[port & 7];
}

void
opl2board_device_mca_write(int port, uint8_t val, void *priv)
{
    opl2board_device_t *serial = (opl2board_device_t *) priv;

    if (port < 0x102)
        return;

    opl2board_device_log("opl2board_device_mca_write: port=%04x val=%02x\n", port, val);

    switch (port) {
        case 0x102:
            if ((serial->pos_regs[2] & 1) && !(val & 1))
                io_removehandler(0x0388, 0x0002,
                                 serial->opl.read, NULL, NULL,
                                 serial->opl.write, NULL, NULL,
                                 serial->opl.priv);
            if (!(serial->pos_regs[2] & 1) && (val & 1))
                io_sethandler(0x0388, 0x0002,
                              serial->opl.read, NULL, NULL,
                              serial->opl.write, NULL, NULL,
                              serial->opl.priv);
            break;

        default:
            break;
    }

    serial->pos_regs[port & 7] = val;
}

uint8_t
opl2board_device_mca_feedb(void *priv)
{
    const opl2board_device_t *serial = (opl2board_device_t *) priv;

    return (serial->pos_regs[2] & 1);
}

void *
opl2board_device_init(UNUSED(const device_t *info))
{
    opl2board_device_t *serial = calloc(1, sizeof(opl2board_device_t));

    opl2board_device_log("opl2board_device_init\n");
    fm_driver_get(FM_OPL2BOARD, &serial->opl);
    io_sethandler(0x0388, 0x0002,
                  serial->opl.read, NULL, NULL,
                  serial->opl.write, NULL, NULL,
                  serial->opl.priv);
    music_add_handler(opl2board_device_get_buffer, serial);

    return serial;
}

void *
opl2board_device_mca_init(const device_t *info)
{
    opl2board_device_t *serial = opl2board_device_init(info);

    io_removehandler(0x0388, 0x0002,
                     serial->opl.read, NULL, NULL,
                     serial->opl.write, NULL, NULL,
                     serial->opl.priv);
    mca_add(opl2board_device_mca_read,
            opl2board_device_mca_write,
            opl2board_device_mca_feedb,
            NULL,
            serial);
    serial->pos_regs[0] = 0xd7;
    serial->pos_regs[1] = 0x70;

    return serial;
}

void
opl2board_device_close(void *priv)
{
    opl2board_device_t *serial = (opl2board_device_t *) priv;
    free(serial);
}


static const device_config_t opl2board_config[] = {
    {
        .name           = "host_serial_path",
        .description    = "Host Serial Device",
        .type           = CONFIG_SERPORT,
        .default_string = "",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t opl2board_device = {
    .name          = "OPL2Board (External Device)",
    .internal_name = "opl2board_device",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = opl2board_device_init,
    .close         = opl2board_device_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = opl2board_config
};
