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

#ifdef ENABLE_ADLIB_LOG
int adlib_do_log = ENABLE_ADLIB_LOG;

static void
adlib_log(const char *fmt, ...)
{
    va_list ap;

    if (adlib_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define adlib_log(fmt, ...)
#endif

typedef struct adlib_t {
    opl_t opl;

    uint8_t pos_regs[8];
} adlib_t;

static void
adlib_get_buffer(int32_t *buffer, int len, void *p)
{
    adlib_t *adlib = (adlib_t *) p;
    int      c;

    opl2_update(&adlib->opl);

    for (c = 0; c < len * 2; c++)
        buffer[c] += (int32_t) adlib->opl.buffer[c];

    adlib->opl.pos = 0;
}

uint8_t
adlib_mca_read(int port, void *p)
{
    adlib_t *adlib = (adlib_t *) p;

    adlib_log("adlib_mca_read: port=%04x\n", port);

    return adlib->pos_regs[port & 7];
}

void
adlib_mca_write(int port, uint8_t val, void *p)
{
    adlib_t *adlib = (adlib_t *) p;

    if (port < 0x102)
        return;

    adlib_log("adlib_mca_write: port=%04x val=%02x\n", port, val);

    switch (port) {
        case 0x102:
            if ((adlib->pos_regs[2] & 1) && !(val & 1))
                io_removehandler(0x0388, 0x0002,
                                 opl2_read, NULL, NULL,
                                 opl2_write, NULL, NULL,
                                 &adlib->opl);
            if (!(adlib->pos_regs[2] & 1) && (val & 1))
                io_sethandler(0x0388, 0x0002,
                              opl2_read, NULL, NULL,
                              opl2_write, NULL, NULL,
                              &adlib->opl);
            break;
    }
    adlib->pos_regs[port & 7] = val;
}

uint8_t
adlib_mca_feedb(void *p)
{
    adlib_t *adlib = (adlib_t *) p;

    return (adlib->pos_regs[2] & 1);
}

void *
adlib_init(const device_t *info)
{
    adlib_t *adlib = malloc(sizeof(adlib_t));
    memset(adlib, 0, sizeof(adlib_t));

    adlib_log("adlib_init\n");
    opl2_init(&adlib->opl);
    io_sethandler(0x0388, 0x0002,
                  opl2_read, NULL, NULL,
                  opl2_write, NULL, NULL,
                  &adlib->opl);
    sound_add_handler(adlib_get_buffer, adlib);
    return adlib;
}

void *
adlib_mca_init(const device_t *info)
{
    adlib_t *adlib = adlib_init(info);

    io_removehandler(0x0388, 0x0002,
                     opl2_read, NULL, NULL,
                     opl2_write, NULL, NULL,
                     &adlib->opl);
    mca_add(adlib_mca_read,
            adlib_mca_write,
            adlib_mca_feedb,
            NULL,
            adlib);
    adlib->pos_regs[0] = 0xd7;
    adlib->pos_regs[1] = 0x70;

    return adlib;
}

void
adlib_close(void *p)
{
    adlib_t *adlib = (adlib_t *) p;

    free(adlib);
}

const device_t adlib_device = {
    .name = "AdLib",
    .internal_name = "adlib",
    .flags = DEVICE_ISA,
    .local = 0,
    .init = adlib_init,
    .close = adlib_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t adlib_mca_device = {
    .name = "AdLib (MCA)",
    .internal_name = "adlib_mca",
    .flags = DEVICE_MCA,
    .local = 0,
    .init = adlib_init,
    .close = adlib_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
