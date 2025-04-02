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
#include "saasound/SAASound.h"
#include <86box/snd_cms.h>
#include <86box/sound.h>
#include <86box/plat_unused.h>

void
cms_update(cms_t *cms)
{
    if (cms->pos < wavetable_pos_global) {
        SAASNDGenerateMany(cms->saasound, (unsigned char*)&cms->buffer[cms->pos], wavetable_pos_global - cms->pos);
        cms->pos = wavetable_pos_global;
    }
    if (cms->pos2 < wavetable_pos_global) {
        SAASNDGenerateMany(cms->saasound2, (unsigned char*)&cms->buffer2[cms->pos2], wavetable_pos_global - cms->pos2);
        cms->pos2 = wavetable_pos_global;
    }
}

void
cms_get_buffer(int32_t *buffer, int len, void *priv)
{
    cms_t *cms = (cms_t *) priv;

    cms_update(cms);

    for (int c = 0; c < len * 2; c++)
        buffer[c] += cms->buffer[c];

    cms->pos = 0;
}

void
cms_get_buffer_2(int32_t *buffer, int len, void *priv)
{
    cms_t *cms = (cms_t *) priv;

    cms_update(cms);

    for (int c = 0; c < len * 2; c++)
        buffer[c] += cms->buffer2[c];

    cms->pos2 = 0;
}

void
cms_write(uint16_t addr, uint8_t val, void *priv)
{
    cms_t *cms = (cms_t *) priv;

    switch (addr & 0xf) {
        case 0x1: /* SAA #1 Register Select Port */
            SAASNDWriteAddress(cms->saasound, val & 31);
            break;
        case 0x3: /* SAA #2 Register Select Port */
            SAASNDWriteAddress(cms->saasound2, val & 31);
            break;

        case 0x0: /* SAA #1 Data Port */
            cms_update(cms);
            SAASNDWriteData(cms->saasound, val);
            break;
        case 0x2: /* SAA #2 Data Port */
            cms_update(cms);
            SAASNDWriteData(cms->saasound2, val);
            break;

        case 0x6: /* GameBlaster Write Port */
        case 0x7: /* GameBlaster Write Port */
            cms->latched_data = val;
            break;

        default:
            break;
    }
}

uint8_t
cms_read(uint16_t addr, void *priv)
{
    const cms_t *cms = (cms_t *) priv;

    switch (addr & 0xf) {
        case 0x1: /* SAA #1 Register Select Port */
            return SAASNDReadAddress(cms->saasound);
        case 0x3: /* SAA #2 Register Select Port */
            return SAASNDReadAddress(cms->saasound2);
        case 0x4: /* GameBlaster Read port (Always returns 0x7F) */
            return 0x7f;
        case 0xa: /* GameBlaster Read Port */
        case 0xb: /* GameBlaster Read Port */
            return cms->latched_data;

        default:
            break;
    }
    return 0xff;
}

void *
cms_init(UNUSED(const device_t *info))
{
    cms_t *cms = calloc(1, sizeof(cms_t));

    uint16_t addr = device_get_config_hex16("base");
    io_sethandler(addr, 0x0010, cms_read, NULL, NULL, cms_write, NULL, NULL, cms);
    cms->saasound = newSAASND();
    SAASNDSetSoundParameters(cms->saasound, SAAP_44100 | SAAP_16BIT | SAAP_NOFILTER | SAAP_STEREO);
    cms->saasound2 = newSAASND();
    SAASNDSetSoundParameters(cms->saasound2, SAAP_44100 | SAAP_16BIT | SAAP_NOFILTER | SAAP_STEREO);
    wavetable_add_handler(cms_get_buffer, cms);
    wavetable_add_handler(cms_get_buffer_2, cms);
    return cms;
}

void
cms_close(void *priv)
{
    cms_t *cms = (cms_t *) priv;

    deleteSAASND(cms->saasound);
    deleteSAASND(cms->saasound2);

    free(cms);
}

static const device_config_t cms_config[] = {
  // clang-format off
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x220,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x210", .value = 0x210 },
            { .description = "0x220", .value = 0x220 },
            { .description = "0x230", .value = 0x230 },
            { .description = "0x240", .value = 0x240 },
            { .description = "0x250", .value = 0x250 },
            { .description = "0x260", .value = 0x260 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t cms_device = {
    .name          = "Creative Music System / Game Blaster",
    .internal_name = "cms",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = cms_init,
    .close         = cms_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = cms_config
};
