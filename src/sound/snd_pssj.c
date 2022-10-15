#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/sound.h>
#include <86box/snd_sn76489.h>
#include <86box/timer.h>

typedef struct pssj_t {
    sn76489_t sn76489;

    uint8_t  ctrl;
    uint8_t  wave;
    uint8_t  dac_val;
    uint16_t freq;
    int      amplitude;

    int        irq;
    pc_timer_t timer_count;
    int        enable;

    int wave_pos;
    int pulse_width;

    int16_t buffer[SOUNDBUFLEN];
    int     pos;
} pssj_t;

static void
pssj_update_irq(pssj_t *pssj)
{
    if (pssj->irq && (pssj->ctrl & 0x10) && (pssj->ctrl & 0x08))
        picint(1 << 7);
}

static void
pssj_write(uint16_t port, uint8_t val, void *p)
{
    pssj_t *pssj = (pssj_t *) p;

    switch (port & 3) {
        case 0:
            pssj->ctrl = val;
            if (!pssj->enable && ((val & 4) && (pssj->ctrl & 3)))
                timer_set_delay_u64(&pssj->timer_count, (TIMER_USEC * (1000000.0 / 3579545.0) * (double) (pssj->freq ? pssj->freq : 0x400)));
            pssj->enable = (val & 4) && (pssj->ctrl & 3);
            if (!pssj->enable)
                timer_disable(&pssj->timer_count);
            sn74689_set_extra_divide(&pssj->sn76489, val & 0x40);
            if (!(val & 8))
                pssj->irq = 0;
            pssj_update_irq(pssj);
            break;
        case 1:
            switch (pssj->ctrl & 3) {
                case 1: /*Sound channel*/
                    pssj->wave        = val;
                    pssj->pulse_width = val & 7;
                    break;
                case 3: /*Direct DAC*/
                    pssj->dac_val = val;
                    break;
            }
            break;
        case 2:
            pssj->freq = (pssj->freq & 0xf00) | val;
            break;
        case 3:
            pssj->freq      = (pssj->freq & 0x0ff) | ((val & 0xf) << 8);
            pssj->amplitude = val >> 4;
            break;
    }
}
static uint8_t
pssj_read(uint16_t port, void *p)
{
    pssj_t *pssj = (pssj_t *) p;

    switch (port & 3) {
        case 0:
            return (pssj->ctrl & ~0x88) | (pssj->irq ? 8 : 0);
        case 1:
            switch (pssj->ctrl & 3) {
                case 0: /*Joystick*/
                    return 0;
                case 1: /*Sound channel*/
                    return pssj->wave;
                case 2: /*Successive approximation*/
                    return 0x80;
                case 3: /*Direct DAC*/
                    return pssj->dac_val;
            }
            break;
        case 2:
            return pssj->freq & 0xff;
        case 3:
            return (pssj->freq >> 8) | (pssj->amplitude << 4);
        default:
            return 0xff;
    }

    return 0xff;
}

static void
pssj_update(pssj_t *pssj)
{
    for (; pssj->pos < sound_pos_global; pssj->pos++)
        pssj->buffer[pssj->pos] = (((int8_t) (pssj->dac_val ^ 0x80) * 0x20) * pssj->amplitude) / 15;
}

static void
pssj_callback(void *p)
{
    pssj_t *pssj = (pssj_t *) p;
    int     data;

    pssj_update(pssj);
    if (pssj->ctrl & 2) {
        if ((pssj->ctrl & 3) == 3) {
            data = dma_channel_read(1);

            if (data != DMA_NODATA) {
                pssj->dac_val = data & 0xff;
            }
        } else {
            data = dma_channel_write(1, 0x80);
        }

        if ((data & DMA_OVER) && data != DMA_NODATA) {
            if (pssj->ctrl & 0x08) {
                pssj->irq = 1;
                pssj_update_irq(pssj);
            }
        }
    } else {
        switch (pssj->wave & 0xc0) {
            case 0x00: /*Pulse*/
                pssj->dac_val = (pssj->wave_pos > (pssj->pulse_width << 1)) ? 0xff : 0;
                break;
            case 0x40: /*Ramp*/
                pssj->dac_val = pssj->wave_pos << 3;
                break;
            case 0x80: /*Triangle*/
                if (pssj->wave_pos & 16)
                    pssj->dac_val = (pssj->wave_pos ^ 31) << 4;
                else
                    pssj->dac_val = pssj->wave_pos << 4;
                break;
            case 0xc0:
                pssj->dac_val = 0x80;
                break;
        }
        pssj->wave_pos = (pssj->wave_pos + 1) & 31;
    }

    timer_advance_u64(&pssj->timer_count, (TIMER_USEC * (1000000.0 / 3579545.0) * (double) (pssj->freq ? pssj->freq : 0x400)));
}

static void
pssj_get_buffer(int32_t *buffer, int len, void *p)
{
    pssj_t *pssj = (pssj_t *) p;
    int     c;

    pssj_update(pssj);

    for (c = 0; c < len * 2; c++)
        buffer[c] += pssj->buffer[c >> 1];

    pssj->pos = 0;
}

void *
pssj_init(const device_t *info)
{
    pssj_t *pssj = malloc(sizeof(pssj_t));
    memset(pssj, 0, sizeof(pssj_t));

    sn76489_init(&pssj->sn76489, 0x00c0, 0x0004, PSSJ, 3579545);

    io_sethandler(0x00C4, 0x0004, pssj_read, NULL, NULL, pssj_write, NULL, NULL, pssj);
    timer_add(&pssj->timer_count, pssj_callback, pssj, pssj->enable);
    sound_add_handler(pssj_get_buffer, pssj);

    return pssj;
}

void *
pssj_1e0_init(const device_t *info)
{
    pssj_t *pssj = malloc(sizeof(pssj_t));
    memset(pssj, 0, sizeof(pssj_t));

    sn76489_init(&pssj->sn76489, 0x01e0, 0x0004, PSSJ, 3579545);

    io_sethandler(0x01E4, 0x0004, pssj_read, NULL, NULL, pssj_write, NULL, NULL, pssj);
    timer_add(&pssj->timer_count, pssj_callback, pssj, pssj->enable);
    sound_add_handler(pssj_get_buffer, pssj);

    return pssj;
}

void *
pssj_isa_init(const device_t *info)
{
    pssj_t *pssj = malloc(sizeof(pssj_t));
    memset(pssj, 0, sizeof(pssj_t));

    uint16_t addr = device_get_config_hex16("base");

    sn76489_init(&pssj->sn76489, addr, 0x0004, PSSJ, 3579545);

    io_sethandler(addr + 0x04, 0x0004, pssj_read, NULL, NULL, pssj_write, NULL, NULL, pssj);
    timer_add(&pssj->timer_count, pssj_callback, pssj, pssj->enable);
    sound_add_handler(pssj_get_buffer, pssj);

    return pssj;
}

void
pssj_close(void *p)
{
    pssj_t *pssj = (pssj_t *) p;

    free(pssj);
}

static const device_config_t pssj_isa_config[] = {
  // clang-format off
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x2C0,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "0x0C0",
                .value = 0x0C0
            },
            {
                .description = "0x0E0",
                .value = 0x0E0
            },
            {
                .description = "0x1C0",
                .value = 0x1C0
            },
            {
                .description = "0x1E0",
                .value = 0x1E0
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
    { .name = "", .description = "", .type = CONFIG_END }
// clang-format on
};

const device_t pssj_device = {
    .name          = "Tandy PSSJ",
    .internal_name = "pssj",
    .flags         = 0,
    .local         = 0,
    .init          = pssj_init,
    .close         = pssj_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t pssj_1e0_device = {
    .name          = "Tandy PSSJ (port 1e0h)",
    .internal_name = "pssj_1e0",
    .flags         = 0,
    .local         = 0,
    .init          = pssj_1e0_init,
    .close         = pssj_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t pssj_isa_device = {
    .name          = "Tandy PSSJ Clone",
    .internal_name = "pssj_isa",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = pssj_isa_init,
    .close         = pssj_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pssj_isa_config
};
