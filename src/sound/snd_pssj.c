#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/sound.h>
#include <86box/snd_sn76489.h>
#include <86box/timer.h>
#include <86box/plat_unused.h>

#define PSSJ_REC_SAFEFTY_MARGIN 4096
#define PSSJ_REC_MAX_MARGIN (PSSJ_REC_SAFEFTY_MARGIN * 2)

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

    /* Recording */
    int     record_pos_read;
    int     record_pos_write_mic;
    int     record_phase_mic;
    int     record_denom_mic;
    int     record_rate_mic;
    int32_t record_prev_l_mic;
    int32_t record_prev_r_mic;
    int     record_prev_valid_mic;
    int     record_aa_active_mic;
    double  record_aa_b0_mic;
    double  record_aa_b1_mic;
    double  record_aa_b2_mic;
    double  record_aa_a1_mic;
    double  record_aa_a2_mic;
    double  record_aa_z1_mic[2];
    double  record_aa_z2_mic[2];
    int16_t record_buffer[0x10000];

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
pssj_write(uint16_t port, uint8_t val, void *priv)
{
    pssj_t *pssj = (pssj_t *) priv;
    uint8_t record = 0;

    switch (port & 3) {
        case 0:
            pssj->ctrl = val;
            if ((pssj->ctrl & 3) == 2)
                record = 1;
            if (!pssj->enable && ((val & 4) && (pssj->ctrl & 3)))
                timer_set_delay_u64(&pssj->timer_count, (TIMER_USEC * (1000000.0 / 3579545.0) * (double) (pssj->freq ? pssj->freq : 0x400)) * (record ? 10 : 1) );
            pssj->enable = (val & 4) && (pssj->ctrl & 3);
            if (!pssj->enable)
                timer_disable(&pssj->timer_count);
            sn76489_set_extra_divide(&pssj->sn76489, val & 0x40);
            if (!(val & 8)) {
                pssj->irq = 0;
                picintc(1 << 7);
            }
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

                default:
                    break;
            }
            break;
        case 2:
            pssj->freq = (pssj->freq & 0xf00) | val;
            break;
        case 3:
            pssj->freq      = (pssj->freq & 0x0ff) | ((val & 0xf) << 8);
            pssj->amplitude = (val & 0xef) >> 4;
            break;

        default:
            break;
    }
}
static uint8_t
pssj_read(uint16_t port, void *priv)
{
    pssj_t *pssj = (pssj_t *) priv;
    uint8_t adc_val = 0x80;

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
                    adc_val = (pssj->record_buffer[pssj->record_pos_read] >> 8) ^ 0x80;
                    pssj->record_pos_read += 2;
                    pssj->record_pos_read &= 0xFFFF;
                    return adc_val;
                case 3: /*Direct DAC*/
                    return pssj->dac_val;

                default:
                    break;
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
pssj_record_resync(pssj_t *pssj)
{
    int pos = (pssj->record_pos_write_mic - PSSJ_REC_SAFEFTY_MARGIN) & 0xFFFF;

    pssj->record_pos_read = pos;

    for (int i = 0; i < PSSJ_REC_SAFEFTY_MARGIN; i++) {
        pssj->record_buffer[pos] = 0;
        pos                     = (pos + 1) & 0xFFFF;
    }
}

static void
pssj_callback(void *priv)
{
    pssj_t *pssj = (pssj_t *) priv;
    int     data;
    uint8_t record = 0;

    pssj_update(pssj);
    if (pssj->ctrl & 2) {
        if ((pssj->ctrl & 3) == 3) {
            data = dma_channel_read(1);

            if (data != DMA_NODATA) {
                pssj->dac_val = data & 0xff;
            }
        } else {
            const int diff = (int) (int16_t) (pssj->record_pos_write_mic - pssj->record_pos_read);
            if ((diff <= 0) || (diff > PSSJ_REC_MAX_MARGIN))
                pssj_record_resync(pssj);
            data = dma_channel_write(1, (pssj->record_buffer[pssj->record_pos_read] >> 8) ^ 0x80);
            pssj->record_pos_read += 2;
            pssj->record_pos_read &= 0xFFFF;
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

            default:
                break;
        }
        pssj->wave_pos = (pssj->wave_pos + 1) & 31;
    }
    if ((pssj->ctrl & 3) == 2)
        record = 1;

    timer_advance_u64(&pssj->timer_count, (TIMER_USEC * (1000000.0 / 3579545.0) * (double) (pssj->freq ? pssj->freq : 0x400)) * (record ? 10 : 1) );
}

#define PSSJ_RECORD_CLAMP(x) (((x) < -32768) ? -32768 : (((x) > 32767) ? 32767 : (x)))

/* filter when freq < capture rate */
#define PSSJ_RECORD_ANTIALIAS 1

/* nyquist anti alias */
#define PSSJ_RECORD_AA_NYQ 0.9

/* audio filter called on filter rate change */
static void
pssj_record_aa_design(pssj_t *pssj, int out_rate, int in_rate)
{
    const double fc    = (PSSJ_RECORD_AA_NYQ * 0.5) * ((double) out_rate);
    const double w0    = (2.0 * M_PI * fc) / ((double) in_rate);
    const double cw    = cos(w0);
    const double sw    = sin(w0);
    const double alpha = sw / (2.0 * 0.70710678118654752);
    const double a0    = 1.0 + alpha;

    pssj->record_aa_b0_mic = ((1.0 - cw) / 2.0) / a0;
    pssj->record_aa_b1_mic = (1.0 - cw) / a0;
    pssj->record_aa_b2_mic = pssj->record_aa_b0_mic;
    pssj->record_aa_a1_mic = (-2.0 * cw) / a0;
    pssj->record_aa_a2_mic = (1.0 - alpha) / a0;
}

static double
pssj_record_aa_step(pssj_t *pssj, int ch, double x)
{
    const double y = (pssj->record_aa_b0_mic * x) + pssj->record_aa_z1_mic[ch];

    pssj->record_aa_z1_mic[ch] = (pssj->record_aa_b1_mic * x) - (pssj->record_aa_a1_mic * y)
                                + pssj->record_aa_z2_mic[ch];
    pssj->record_aa_z2_mic[ch] = (pssj->record_aa_b2_mic * x) - (pssj->record_aa_a2_mic * y);

    return y;
}

static void
pssj_put_buffer(int16_t *buffer, int len, void *priv)
{
    pssj_t                *pssj = (pssj_t *) priv;
    uint8_t               record = 0;
    if ((pssj->ctrl & 3) == 2)
        record = 1;

    /* divisor is rate capture device opened at*/
    const int cap_rate = al_capture_get_rate();
    const int denom    = (cap_rate > 0) ? cap_rate : SOUND_FREQ;
    const int rate     = 3579545.0 / (record ? pssj->freq * 10 : pssj->freq);

    int c;
    int gain_l;
    int gain_r;
    int interp;
    int filt;

    /* freq is 0 until the guest programs a rate  */
    if (rate <= 0)
        return;

    if ((denom != pssj->record_denom_mic) || (rate != pssj->record_rate_mic)) {
        pssj->record_denom_mic      = denom;
        pssj->record_rate_mic       = rate;
        pssj->record_phase_mic      = 0;
        pssj->record_prev_l_mic     = 0;
        pssj->record_prev_r_mic     = 0;
        pssj->record_prev_valid_mic = 0;

        pssj->record_aa_z1_mic[0] = 0.0;
        pssj->record_aa_z1_mic[1] = 0.0;
        pssj->record_aa_z2_mic[0] = 0.0;
        pssj->record_aa_z2_mic[1] = 0.0;
        pssj->record_aa_active_mic = 0;

#if PSSJ_RECORD_ANTIALIAS
        /* only when decimating */
        if (rate < denom) {
            pssj_record_aa_design(pssj, rate, denom);
            pssj->record_aa_active_mic = 1;
        }
#endif
    }

    interp = (rate != denom);
    filt   = pssj->record_aa_active_mic;

    gain_l = 16;
    gain_r = 16;

    for (c = 0; c < len * 2; c += 2) {
        const int32_t cap_l = (int32_t) buffer[c];
        const int32_t cap_r = (int32_t) buffer[c + 1];

        /* mic is the mono sum of line-in. truncating division for dc symmetry */
        const int32_t mic = (cap_l + cap_r) / 2;

        int32_t mix_l = mic;
        int32_t mix_r = mic;
        int32_t in_l;
        int32_t in_r;

        /* run on every input frame*/
        if (filt) {
            mix_l = (int32_t) lrint(pssj_record_aa_step(pssj, 0, (double) mix_l));
            mix_r = (int32_t) lrint(pssj_record_aa_step(pssj, 1, (double) mix_r));
        }

        in_l = PSSJ_RECORD_CLAMP(mix_l * gain_l);
        in_r = PSSJ_RECORD_CLAMP(mix_r * gain_r);

        /* start new device change with first frame in interpolartor queue */
        if (!pssj->record_prev_valid_mic) {
            pssj->record_prev_l_mic     = in_l;
            pssj->record_prev_r_mic     = in_r;
            pssj->record_prev_valid_mic = 1;
        }

        /* phase ticks this forward, while-loop for new samples so they arent dropped */
        pssj->record_phase_mic += rate;
        while (pssj->record_phase_mic >= denom) {
            int32_t out_l;
            int32_t out_r;

            pssj->record_phase_mic -= denom; /* denom tracks input frame vs emitted frame , (rate - phase) / rate */

            if (interp) {

                const int32_t num = rate - pssj->record_phase_mic;

                out_l = pssj->record_prev_l_mic
                        + (int32_t) ((((int64_t) (in_l - pssj->record_prev_l_mic)) * num) / rate);
                out_r = pssj->record_prev_r_mic
                        + (int32_t) ((((int64_t) (in_r - pssj->record_prev_r_mic)) * num) / rate);
            } else {
                out_l = in_l;
                out_r = in_r;
            }

            pssj->record_buffer[pssj->record_pos_write_mic]                = (int16_t) out_l;
            pssj->record_buffer[(pssj->record_pos_write_mic + 1) & 0xffff] = (int16_t) out_r;

            pssj->record_pos_write_mic = (pssj->record_pos_write_mic + 2) & 0xffff;
        }

        pssj->record_prev_l_mic = in_l;
        pssj->record_prev_r_mic = in_r;
    }
}

static void
pssj_get_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    pssj_t *pssj = (pssj_t *) priv;

    pssj_update(pssj);

    for (uint16_t c = 0; c < len * 2; c++)
        buffer[c] += pssj->buffer[c >> 1];

    pssj->pos = 0;
}

void *
pssj_init(UNUSED(const device_t *info))
{
    pssj_t *pssj = calloc(1, sizeof(pssj_t));

    sn76489_init(&pssj->sn76489, 0x00c0, 0x0004, PSSJ, 3579545);

    io_sethandler(0x00C4, 0x0004, pssj_read, NULL, NULL, pssj_write, NULL, NULL, pssj);
    timer_add(&pssj->timer_count, pssj_callback, pssj, pssj->enable);
    sound_add_handler(pssj_get_buffer, pssj);
    sound_in_add_handler(pssj_put_buffer, pssj);
    sound_in_start_input();

    return pssj;
}

void *
pssj_1e0_init(UNUSED(const device_t *info))
{
    pssj_t *pssj = calloc(1, sizeof(pssj_t));

    sn76489_init(&pssj->sn76489, 0x01e0, 0x0004, PSSJ, 3579545);

    io_sethandler(0x01E4, 0x0004, pssj_read, NULL, NULL, pssj_write, NULL, NULL, pssj);
    timer_add(&pssj->timer_count, pssj_callback, pssj, pssj->enable);
    sound_add_handler(pssj_get_buffer, pssj);
    sound_in_add_handler(pssj_put_buffer, pssj);
    sound_in_start_input();

    return pssj;
}

void *
pssj_isa_init(UNUSED(const device_t *info))
{
    pssj_t *pssj = calloc(1, sizeof(pssj_t));

    uint16_t addr = device_get_config_hex16("base");

    sn76489_init(&pssj->sn76489, addr, 0x0004, PSSJ, 3579545);

    io_sethandler(addr + 0x04, 0x0004, pssj_read, NULL, NULL, pssj_write, NULL, NULL, pssj);
    timer_add(&pssj->timer_count, pssj_callback, pssj, pssj->enable);
    sound_add_handler(pssj_get_buffer, pssj);
    sound_in_add_handler(pssj_put_buffer, pssj);
    sound_in_start_input();

    return pssj;
}

void
pssj_close(void *priv)
{
    pssj_t *pssj = (pssj_t *) priv;

    free(pssj);
}

static const device_config_t pssj_isa_config[] = {
  // clang-format off
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x2C0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x0C0", .value = 0x0C0 },
            { .description = "0x0E0", .value = 0x0E0 },
            { .description = "0x1C0", .value = 0x1C0 },
            { .description = "0x1E0", .value = 0x1E0 },
            { .description = "0x2C0", .value = 0x2C0 },
            { .description = "0x2E0", .value = 0x2E0 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t pssj_device = {
    .name          = "Tandy PSSJ",
    .internal_name = "pssj",
    .flags         = DEVICE_AUDIO_IN,
    .local         = 0,
    .init          = pssj_init,
    .close         = pssj_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t pssj_1e0_device = {
    .name          = "Tandy PSSJ (port 1e0h)",
    .internal_name = "pssj_1e0",
    .flags         = DEVICE_AUDIO_IN,
    .local         = 0,
    .init          = pssj_1e0_init,
    .close         = pssj_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t pssj_isa_device = {
    .name          = "Tandy PSSJ Clone",
    .internal_name = "pssj_isa",
    .flags         = DEVICE_ISA | DEVICE_AUDIO_IN,
    .local         = 0,
    .init          = pssj_isa_init,
    .close         = pssj_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pssj_isa_config
};
