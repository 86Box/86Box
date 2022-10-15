#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/sound.h>
#include <86box/snd_sn76489.h>

int sn76489_mute;

static float volslog[16] = {
    0.00000f, 0.59715f, 0.75180f, 0.94650f,
    1.19145f, 1.50000f, 1.88835f, 2.37735f,
    2.99295f, 3.76785f, 4.74345f, 5.97165f,
    7.51785f, 9.46440f, 11.9194f, 15.0000f
};

void
sn76489_update(sn76489_t *sn76489)
{
    for (; sn76489->pos < sound_pos_global; sn76489->pos++) {
        int     c;
        int16_t result = 0;

        for (c = 1; c < 4; c++) {
            if (sn76489->latch[c] > 256)
                result += (int16_t) (volslog[sn76489->vol[c]] * sn76489->stat[c]);
            else
                result += (int16_t) (volslog[sn76489->vol[c]] * 127);

            sn76489->count[c] -= (256 * sn76489->psgconst);
            while ((int) sn76489->count[c] < 0) {
                sn76489->count[c] += sn76489->latch[c];
                sn76489->stat[c] = -sn76489->stat[c];
            }
        }
        result += (((sn76489->shift & 1) ^ 1) * 127 * volslog[sn76489->vol[0]] * 2);

        sn76489->count[0] -= (512 * sn76489->psgconst);
        while ((int) sn76489->count[0] < 0 && sn76489->latch[0]) {
            sn76489->count[0] += (sn76489->latch[0] * 4);
            if (!(sn76489->noise & 4)) {
                if (sn76489->shift & 1)
                    sn76489->shift |= 0x8000;
                sn76489->shift >>= 1;
            } else {
                if ((sn76489->shift & 1) ^ ((sn76489->shift >> 1) & 1))
                    sn76489->shift |= 0x8000;
                sn76489->shift >>= 1;
            }
        }

        sn76489->buffer[sn76489->pos] = result;
    }
}

void
sn76489_get_buffer(int32_t *buffer, int len, void *p)
{
    sn76489_t *sn76489 = (sn76489_t *) p;

    int c;

    sn76489_update(sn76489);

    if (!sn76489_mute) {
        for (c = 0; c < len * 2; c++)
            buffer[c] += sn76489->buffer[c >> 1];
    }

    sn76489->pos = 0;
}

void
sn76489_write(uint16_t addr, uint8_t data, void *p)
{
    sn76489_t *sn76489 = (sn76489_t *) p;
    int        freq;

    sn76489_update(sn76489);

    if (data & 0x80) {
        sn76489->firstdat = data;
        switch (data & 0x70) {
            case 0:
                sn76489->freqlo[3] = data & 0xf;
                sn76489->latch[3]  = (sn76489->freqlo[3] | (sn76489->freqhi[3] << 4)) << 6;
                if (sn76489->extra_divide)
                    sn76489->latch[3] &= 0x3ff;
                if (!sn76489->latch[3])
                    sn76489->latch[3] = (sn76489->extra_divide ? 2048 : 1024) << 6;
                sn76489->lasttone = 3;
                break;
            case 0x10:
                data &= 0xf;
                sn76489->vol[3] = 0xf - data;
                break;
            case 0x20:
                sn76489->freqlo[2] = data & 0xf;
                sn76489->latch[2]  = (sn76489->freqlo[2] | (sn76489->freqhi[2] << 4)) << 6;
                if (sn76489->extra_divide)
                    sn76489->latch[2] &= 0x3ff;
                if (!sn76489->latch[2])
                    sn76489->latch[2] = (sn76489->extra_divide ? 2048 : 1024) << 6;
                sn76489->lasttone = 2;
                break;
            case 0x30:
                data &= 0xf;
                sn76489->vol[2] = 0xf - data;
                break;
            case 0x40:
                sn76489->freqlo[1] = data & 0xf;
                sn76489->latch[1]  = (sn76489->freqlo[1] | (sn76489->freqhi[1] << 4)) << 6;
                if (sn76489->extra_divide)
                    sn76489->latch[1] &= 0x3ff;
                if (!sn76489->latch[1])
                    sn76489->latch[1] = (sn76489->extra_divide ? 2048 : 1024) << 6;
                sn76489->lasttone = 1;
                break;
            case 0x50:
                data &= 0xf;
                sn76489->vol[1] = 0xf - data;
                break;
            case 0x60:
                if ((data & 4) != (sn76489->noise & 4) || sn76489->type == SN76496)
                    sn76489->shift = 0x4000;
                sn76489->noise = data & 0xf;
                if ((data & 3) == 3)
                    sn76489->latch[0] = sn76489->latch[1];
                else
                    sn76489->latch[0] = 0x400 << (data & 3);
                if (sn76489->extra_divide)
                    sn76489->latch[0] &= 0x3ff;
                if (!sn76489->latch[0])
                    sn76489->latch[0] = (sn76489->extra_divide ? 2048 : 1024) << 6;
                break;
            case 0x70:
                data &= 0xf;
                sn76489->vol[0] = 0xf - data;
                break;
        }
    } else {
        if ((sn76489->firstdat & 0x70) == 0x60 && (sn76489->type == SN76496)) {
            if ((data & 4) != (sn76489->noise & 4) || sn76489->type == SN76496)
                sn76489->shift = 0x4000;
            sn76489->noise = data & 0xf;
            if ((data & 3) == 3)
                sn76489->latch[0] = sn76489->latch[1];
            else
                sn76489->latch[0] = 0x400 << (data & 3);
            if (!sn76489->latch[0])
                sn76489->latch[0] = 1024 << 6;
        } else if ((sn76489->firstdat & 0x70) != 0x60) {
            sn76489->freqhi[sn76489->lasttone] = data & 0x7F;
            freq                               = sn76489->freqlo[sn76489->lasttone] | (sn76489->freqhi[sn76489->lasttone] << 4);
            if (sn76489->extra_divide)
                freq &= 0x3ff;
            if (!freq)
                freq = sn76489->extra_divide ? 2048 : 1024;
            if ((sn76489->noise & 3) == 3 && sn76489->lasttone == 1)
                sn76489->latch[0] = freq << 6;
            sn76489->latch[sn76489->lasttone] = freq << 6;
        }
    }
}

void
sn74689_set_extra_divide(sn76489_t *sn76489, int enable)
{
    sn76489->extra_divide = enable;
}

void
sn76489_init(sn76489_t *sn76489, uint16_t base, uint16_t size, int type, int freq)
{
    sound_add_handler(sn76489_get_buffer, sn76489);

    sn76489->latch[0] = sn76489->latch[1] = sn76489->latch[2] = sn76489->latch[3] = 0x3FF << 6;
    sn76489->vol[0]                                                               = 0;
    sn76489->vol[1] = sn76489->vol[2] = sn76489->vol[3] = 8;
    sn76489->stat[0] = sn76489->stat[1] = sn76489->stat[2] = sn76489->stat[3] = 127;
    srand(time(NULL));
    sn76489->count[0] = 0;
    sn76489->count[1] = (rand() & 0x3FF) << 6;
    sn76489->count[2] = (rand() & 0x3FF) << 6;
    sn76489->count[3] = (rand() & 0x3FF) << 6;
    sn76489->noise    = 3;
    sn76489->shift    = 0x4000;
    sn76489->type     = type;
    sn76489->psgconst = (((double) freq / 64.0) / 48000.0);

    sn76489_mute = 0;

    io_sethandler(base, size, NULL, NULL, NULL, sn76489_write, NULL, NULL, sn76489);
}

void *
sn76489_device_init(const device_t *info)
{
    sn76489_t *sn76489 = malloc(sizeof(sn76489_t));
    memset(sn76489, 0, sizeof(sn76489_t));

    sn76489_init(sn76489, 0x00c0, 0x0008, SN76496, 3579545);

    return sn76489;
}

void *
ncr8496_device_init(const device_t *info)
{
    sn76489_t *sn76489 = malloc(sizeof(sn76489_t));
    memset(sn76489, 0, sizeof(sn76489_t));

    sn76489_init(sn76489, 0x00c0, 0x0008, NCR8496, 3579545);

    return sn76489;
}

void *
tndy_device_init(const device_t *info)
{
    sn76489_t *sn76489 = malloc(sizeof(sn76489_t));
    memset(sn76489, 0, sizeof(sn76489_t));

    uint16_t addr = device_get_config_hex16("base");

    sn76489_init(sn76489, addr, 0x0008, SN76496, 3579545);

    return sn76489;
}

void
sn76489_device_close(void *p)
{
    sn76489_t *sn76489 = (sn76489_t *) p;

    free(sn76489);
}

static const device_config_t tndy_config[] = {
  // clang-format off
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x0C0,
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

const device_t sn76489_device = {
    .name          = "TI SN74689 PSG",
    .internal_name = "sn76489",
    .flags         = 0,
    .local         = 0,
    .init          = sn76489_device_init,
    .close         = sn76489_device_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ncr8496_device = {
    .name          = "NCR8496 PSG",
    .internal_name = "ncr8496",
    .flags         = 0,
    .local         = 0,
    .init          = ncr8496_device_init,
    .close         = sn76489_device_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t tndy_device = {
    .name          = "TNDY",
    .internal_name = "tndy",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = tndy_device_init,
    .close         = sn76489_device_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = tndy_config
};
