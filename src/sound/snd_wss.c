/*
 * 86Box     A hypervisor and IBM PC system emulator that specializes in
 *           running old operating systems and software designed for IBM
 *           PC systems and compatibles from 1981 through fairly recent
 *           system designs based on the PCI bus.
 *
 *           This file is part of the 86Box distribution.
 *
 *           Windows Sound System emulation.
 *
 *
 *
 * Authors:  Sarah Walker, <http://pcem-emulator.co.uk/>
 *           TheCollector1995, <mariogplayer@gmail.com>
 *
 *           Copyright 2012-2018 Sarah Walker.
 *           Copyright 2018 TheCollector1995.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/mca.h>
#include <86box/pic.h>
#include <86box/sound.h>
#include <86box/timer.h>
#include <86box/snd_ad1848.h>
#include <86box/snd_opl.h>

/* 530, 11, 3 - 530=23
 * 530, 11, 1 - 530=22
 * 530, 11, 0 - 530=21
 * 530, 10, 1 - 530=1a
 * 530, 9,  1 - 530=12
 * 530, 7,  1 - 530=0a
 * 604, 11, 1 - 530=22
 * e80, 11, 1 - 530=22
 * f40, 11, 1 - 530=22
 */

static const int wss_dma[4] = { 0, 0, 1, 3 };
static const int wss_irq[8] = { 5, 7, 9, 10, 11, 12, 14, 15 }; /* W95 only uses 7-9, others may be wrong */

typedef struct wss_t {
    uint8_t config;

    ad1848_t ad1848;
    opl_t    opl;

    int     opl_enabled;
    uint8_t pos_regs[8];
} wss_t;

uint8_t
wss_read(uint16_t addr, void *priv)
{
    wss_t *wss = (wss_t *) priv;
    return 4 | (wss->config & 0x40);
}

void
wss_write(uint16_t addr, uint8_t val, void *priv)
{
    wss_t *wss = (wss_t *) priv;

    wss->config = val;
    ad1848_setdma(&wss->ad1848, wss_dma[val & 3]);
    ad1848_setirq(&wss->ad1848, wss_irq[(val >> 3) & 7]);
}

static void
wss_get_buffer(int32_t *buffer, int len, void *priv)
{
    wss_t *wss = (wss_t *) priv;
    int    c;

    opl3_update(&wss->opl);
    ad1848_update(&wss->ad1848);
    for (c = 0; c < len * 2; c++) {
        buffer[c] += wss->opl.buffer[c];
        buffer[c] += wss->ad1848.buffer[c] / 2;
    }

    wss->opl.pos    = 0;
    wss->ad1848.pos = 0;
}

void *
wss_init(const device_t *info)
{
    wss_t *wss = malloc(sizeof(wss_t));
    memset(wss, 0, sizeof(wss_t));

    uint16_t addr    = device_get_config_hex16("base");
    wss->opl_enabled = device_get_config_int("opl");

    if (wss->opl_enabled)
        opl3_init(&wss->opl);

    ad1848_init(&wss->ad1848, AD1848_TYPE_DEFAULT);

    ad1848_setirq(&wss->ad1848, 7);
    ad1848_setdma(&wss->ad1848, 3);

    if (wss->opl_enabled)
        io_sethandler(0x0388, 0x0004,
                      opl3_read, NULL, NULL,
                      opl3_write, NULL, NULL,
                      &wss->opl);

    io_sethandler(addr, 0x0004,
                  wss_read, NULL, NULL,
                  wss_write, NULL, NULL,
                  wss);
    io_sethandler(addr + 4, 0x0004,
                  ad1848_read, NULL, NULL,
                  ad1848_write, NULL, NULL,
                  &wss->ad1848);

    sound_add_handler(wss_get_buffer, wss);

    return wss;
}

static uint8_t
ncr_audio_mca_read(int port, void *priv)
{
    wss_t *wss = (wss_t *) priv;
    return wss->pos_regs[port & 7];
}

static void
ncr_audio_mca_write(int port, uint8_t val, void *priv)
{
    wss_t   *wss      = (wss_t *) priv;
    uint16_t ports[4] = { 0x530, 0xE80, 0xF40, 0x604 };
    uint16_t addr;

    if (port < 0x102)
        return;

    wss->opl_enabled = (wss->pos_regs[2] & 0x20) ? 1 : 0;
    addr             = ports[(wss->pos_regs[2] & 0x18) >> 3];

    io_removehandler(0x0388, 0x0004,
                     opl3_read, NULL, NULL,
                     opl3_write, NULL, NULL,
                     &wss->opl);
    io_removehandler(addr, 0x0004,
                     wss_read, NULL, NULL,
                     wss_write, NULL, NULL,
                     wss);
    io_removehandler(addr + 4, 0x0004,
                     ad1848_read, NULL, NULL,
                     ad1848_write, NULL, NULL,
                     &wss->ad1848);

    wss->pos_regs[port & 7] = val;

    if (wss->pos_regs[2] & 1) {
        addr = ports[(wss->pos_regs[2] & 0x18) >> 3];

        if (wss->opl_enabled)
            io_sethandler(0x0388, 0x0004,
                          opl3_read, NULL, NULL,
                          opl3_write, NULL, NULL,
                          &wss->opl);

        io_sethandler(addr, 0x0004,
                      wss_read, NULL, NULL,
                      wss_write, NULL, NULL,
                      wss);
        io_sethandler(addr + 4, 0x0004,
                      ad1848_read, NULL, NULL,
                      ad1848_write, NULL, NULL,
                      &wss->ad1848);
    }
}

static uint8_t
ncr_audio_mca_feedb(void *priv)
{
    wss_t *wss = (wss_t *) priv;
    return (wss->pos_regs[2] & 1);
}

void *
ncr_audio_init(const device_t *info)
{
    wss_t *wss = malloc(sizeof(wss_t));
    memset(wss, 0, sizeof(wss_t));

    opl3_init(&wss->opl);
    ad1848_init(&wss->ad1848, AD1848_TYPE_DEFAULT);

    ad1848_setirq(&wss->ad1848, 7);
    ad1848_setdma(&wss->ad1848, 3);

    mca_add(ncr_audio_mca_read, ncr_audio_mca_write, ncr_audio_mca_feedb, NULL, wss);
    wss->pos_regs[0] = 0x16;
    wss->pos_regs[1] = 0x51;

    sound_add_handler(wss_get_buffer, wss);

    return wss;
}

void
wss_close(void *priv)
{
    wss_t *wss = (wss_t *) priv;
    free(wss);
}

void
wss_speed_changed(void *priv)
{
    wss_t *wss = (wss_t *) priv;
    ad1848_speed_changed(&wss->ad1848);
}

static const device_config_t wss_config[] = {
// clang-format off
    { "base", "Address", CONFIG_HEX16, "", 0x530, "", { 0 },
        {
            { "0x530", 0x530 },
            { "0x604", 0x604 },
            { "0xe80", 0xe80 },
            { "0xf40", 0xf40 },
            { ""             }
        }
    },
    { "opl", "Enable OPL", CONFIG_BINARY, "", 1 },
    { "", "", -1 }
// clang-format on
};

const device_t wss_device = {
    .name = "Windows Sound System",
    .internal_name = "wss",
    .flags = DEVICE_ISA | DEVICE_AT,
    .local = 0,
    .init = wss_init,
    .close = wss_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = wss_speed_changed,
    .force_redraw = NULL,
    .config = wss_config
};

const device_t ncr_business_audio_device = {
    .name = "NCR Business Audio",
    .internal_name = "ncraudio",
    .flags = DEVICE_MCA,
    .local = 0,
    .init = ncr_audio_init,
    .close = wss_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = wss_speed_changed,
    .force_redraw = NULL,
    .config = NULL
};
