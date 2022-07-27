#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/io.h>
#include <86box/snd_resid.h>
#include <86box/sound.h>

typedef struct ssi2001_t {
    void   *psid;
    int16_t buffer[SOUNDBUFLEN * 2];
    int     pos;
    int     gameport_enabled;
} ssi2001_t;

static void
ssi2001_update(ssi2001_t *ssi2001)
{
    if (ssi2001->pos >= sound_pos_global)
        return;

    sid_fillbuf(&ssi2001->buffer[ssi2001->pos], sound_pos_global - ssi2001->pos, ssi2001->psid);
    ssi2001->pos = sound_pos_global;
}

static void
ssi2001_get_buffer(int32_t *buffer, int len, void *p)
{
    ssi2001_t *ssi2001 = (ssi2001_t *) p;
    int        c;

    ssi2001_update(ssi2001);

    for (c = 0; c < len * 2; c++)
        buffer[c] += ssi2001->buffer[c >> 1] / 2;

    ssi2001->pos = 0;
}

static uint8_t
ssi2001_read(uint16_t addr, void *p)
{
    ssi2001_t *ssi2001 = (ssi2001_t *) p;

    ssi2001_update(ssi2001);

    return sid_read(addr, p);
}

static void
ssi2001_write(uint16_t addr, uint8_t val, void *p)
{
    ssi2001_t *ssi2001 = (ssi2001_t *) p;

    ssi2001_update(ssi2001);
    sid_write(addr, val, p);
}

void *
ssi2001_init(const device_t *info)
{
    ssi2001_t *ssi2001 = malloc(sizeof(ssi2001_t));
    memset(ssi2001, 0, sizeof(ssi2001_t));

    ssi2001->psid = sid_init();
    sid_reset(ssi2001->psid);
    uint16_t addr             = device_get_config_hex16("base");
    ssi2001->gameport_enabled = device_get_config_int("gameport");
    io_sethandler(addr, 0x0020, ssi2001_read, NULL, NULL, ssi2001_write, NULL, NULL, ssi2001);
    if (ssi2001->gameport_enabled)
        gameport_remap(gameport_add(&gameport_201_device), 0x201);
    sound_add_handler(ssi2001_get_buffer, ssi2001);
    return ssi2001;
}

void
ssi2001_close(void *p)
{
    ssi2001_t *ssi2001 = (ssi2001_t *) p;

    sid_close(ssi2001->psid);

    free(ssi2001);
}

static const device_config_t ssi2001_config[] = {
// clang-format off
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x280,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "0x280",
                .value = 0x280
            },
            {
                .description = "0x2A0",
                .value = 0x2A0
            },
            {
                .description = "0x2C0",
                .value = 0x2C0
            },
            {
                .description = "0x2E0",
                .value = 0x2E0
            },
            { .description = "" }
        }
    },
    { "gameport", "Enable Game port", CONFIG_BINARY, "",  1 },
    { "",         "",                                    -1 }
// clang-format off
};

const device_t ssi2001_device =
{
    .name = "Innovation SSI-2001",
    .internal_name = "ssi2001",
    .flags = DEVICE_ISA,
    .local = 0,
    .init = ssi2001_init,
    .close = ssi2001_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = ssi2001_config
};
