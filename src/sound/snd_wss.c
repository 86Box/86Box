/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Windows Sound System emulation.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          TheCollector1995, <mariogplayer@gmail.com>
 *
 *          Copyright 2012-2018 Sarah Walker.
 *          Copyright 2018      TheCollector1995.
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
#include <86box/plat_unused.h>

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

static double wss_input_gain_vols_4bits[16];

typedef struct wss_t {
    uint8_t config;

    ad1848_t ad1848;
    fm_drv_t opl;

    int     opl_enabled;
    uint8_t pos_regs[8];
} wss_t;

uint8_t
wss_read(UNUSED(uint16_t addr), void *priv)
{
    const wss_t *wss = (wss_t *) priv;
    return 4 | (wss->config & 0x40);
}

void
wss_write(UNUSED(uint16_t addr), uint8_t val, void *priv)
{
    wss_t *wss = (wss_t *) priv;

    wss->config = val;
    ad1848_setdma(&wss->ad1848, wss_dma[val & 3]);
    ad1848_setirq(&wss->ad1848, wss_irq[(val >> 3) & 7]);
}

#define WSS_RECORD_CLAMP(x) (((x) < -32768) ? -32768 : (((x) > 32767) ? 32767 : (x)))

/* filter when freq < capture rate */
#define WSS_RECORD_ANTIALIAS 1

/* nyquist anti alias */
#define WSS_RECORD_AA_NYQ 0.9

/* audio filter called on filter rate change */
static void
wss_record_aa_design(ad1848_t *ad1848, int out_rate, int in_rate)
{
    const double fc    = (WSS_RECORD_AA_NYQ * 0.5) * ((double) out_rate);
    const double w0    = (2.0 * M_PI * fc) / ((double) in_rate);
    const double cw    = cos(w0);
    const double sw    = sin(w0);
    const double alpha = sw / (2.0 * 0.70710678118654752);
    const double a0    = 1.0 + alpha;

    ad1848->record_aa_b0_mic = ((1.0 - cw) / 2.0) / a0;
    ad1848->record_aa_b1_mic = (1.0 - cw) / a0;
    ad1848->record_aa_b2_mic = ad1848->record_aa_b0_mic;
    ad1848->record_aa_a1_mic = (-2.0 * cw) / a0;
    ad1848->record_aa_a2_mic = (1.0 - alpha) / a0;
}

static double
wss_record_aa_step(ad1848_t *ad1848, int ch, double x)
{
    const double y = (ad1848->record_aa_b0_mic * x) + ad1848->record_aa_z1_mic[ch];

    ad1848->record_aa_z1_mic[ch] = (ad1848->record_aa_b1_mic * x) - (ad1848->record_aa_a1_mic * y)
                                + ad1848->record_aa_z2_mic[ch];
    ad1848->record_aa_z2_mic[ch] = (ad1848->record_aa_b2_mic * x) - (ad1848->record_aa_a2_mic * y);

    return y;
}

static void
wss_put_buffer(int16_t *buffer, int len, void *priv)
{
    wss_t                *wss = (wss_t *) priv;

    /* divisor is rate capture device opened at*/
    const int cap_rate = al_capture_get_rate();
    const int denom    = (cap_rate > 0) ? cap_rate : SOUND_FREQ;
    const int rate     = wss->ad1848.freq;

    int c;
    int gain_l;
    int gain_r;
    int sel_l_mic, sel_l_linel;
    int sel_r_mic, sel_r_liner;
    int interp;
    int filt;

    /* freq is 0 until the guest programs a rate  */
    if (rate <= 0)
        return;

    if ((denom != wss->ad1848.record_denom_mic) || (rate != wss->ad1848.record_rate_mic)) {
        wss->ad1848.record_denom_mic      = denom;
        wss->ad1848.record_rate_mic       = rate;
        wss->ad1848.record_phase_mic      = 0;
        wss->ad1848.record_prev_l_mic     = 0;
        wss->ad1848.record_prev_r_mic     = 0;
        wss->ad1848.record_prev_valid_mic = 0;

        wss->ad1848.record_aa_z1_mic[0] = 0.0;
        wss->ad1848.record_aa_z1_mic[1] = 0.0;
        wss->ad1848.record_aa_z2_mic[0] = 0.0;
        wss->ad1848.record_aa_z2_mic[1] = 0.0;
        wss->ad1848.record_aa_active_mic = 0;

#if WSS_RECORD_ANTIALIAS
        /* only when decimating */
        if (rate < denom) {
            wss_record_aa_design(&wss->ad1848, rate, denom);
            wss->ad1848.record_aa_active_mic = 1;
        }
#endif
    }

    interp = (rate != denom);
    filt   = wss->ad1848.record_aa_active_mic;

    gain_l = wss->ad1848.regs[0] & 0x0f;
    gain_r = wss->ad1848.regs[1] & 0x0f;

    sel_l_mic   = (((wss->ad1848.regs[0] & 0xc0) == 0x80) ? 1 : 0);
    sel_l_linel = (((wss->ad1848.regs[0] & 0xc0) == 0x40) ? 1 : 0);

    sel_r_mic   = (((wss->ad1848.regs[1] & 0xc0) == 0x80) ? 1 : 0);
    sel_r_liner = (((wss->ad1848.regs[1] & 0xc0) == 0x40) ? 1 : 0);

    for (c = 0; c < len * 2; c += 2) {
        const int32_t cap_l = (int32_t) buffer[c];
        const int32_t cap_r = (int32_t) buffer[c + 1];

        /* mic is the mono sum of line-in. truncating division for dc symmetry */
        const int32_t mic = (cap_l + cap_r) / 2;

        int32_t mix_l = (mic * sel_l_mic) + (cap_l * sel_l_linel);
        int32_t mix_r = (mic * sel_r_mic) + (cap_r * sel_r_liner);
        int32_t in_l;
        int32_t in_r;

        /* run on every input frame*/
        if (filt) {
            mix_l = (int32_t) lrint(wss_record_aa_step(&wss->ad1848, 0, (double) mix_l));
            mix_r = (int32_t) lrint(wss_record_aa_step(&wss->ad1848, 1, (double) mix_r));
        }

        in_l = WSS_RECORD_CLAMP(mix_l * wss_input_gain_vols_4bits[gain_l]);
        in_r = WSS_RECORD_CLAMP(mix_r * wss_input_gain_vols_4bits[gain_r]);

        /* start new device change with first frame in interpolartor queue */
        if (!wss->ad1848.record_prev_valid_mic) {
            wss->ad1848.record_prev_l_mic     = in_l;
            wss->ad1848.record_prev_r_mic     = in_r;
            wss->ad1848.record_prev_valid_mic = 1;
        }

        /* phase ticks this forward, while-loop for new samples so they arent dropped */
        wss->ad1848.record_phase_mic += rate;
        while (wss->ad1848.record_phase_mic >= denom) {
            int32_t out_l;
            int32_t out_r;

            wss->ad1848.record_phase_mic -= denom; /* denom tracks input frame vs emitted frame , (rate - phase) / rate */

            if (interp) {

                const int32_t num = rate - wss->ad1848.record_phase_mic;

                out_l = wss->ad1848.record_prev_l_mic
                        + (int32_t) ((((int64_t) (in_l - wss->ad1848.record_prev_l_mic)) * num) / rate);
                out_r = wss->ad1848.record_prev_r_mic
                        + (int32_t) ((((int64_t) (in_r - wss->ad1848.record_prev_r_mic)) * num) / rate);
            } else {
                out_l = in_l;
                out_r = in_r;
            }

            wss->ad1848.record_buffer[wss->ad1848.record_pos_write_mic]                = (int16_t) out_l;
            wss->ad1848.record_buffer[(wss->ad1848.record_pos_write_mic + 1) & 0xffff] = (int16_t) out_r;

            wss->ad1848.record_pos_write_mic = (wss->ad1848.record_pos_write_mic + 2) & 0xffff;
        }

        wss->ad1848.record_prev_l_mic = in_l;
        wss->ad1848.record_prev_r_mic = in_r;
    }
}

static void
wss_get_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    wss_t *wss = (wss_t *) priv;

    ad1848_update(&wss->ad1848);
    for (uint16_t c = 0; c < len * 2; c++)
        buffer[c] += wss->ad1848.buffer[c] / 2;

    wss->ad1848.pos = 0;
}

static void
wss_get_music_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    wss_t *wss = (wss_t *) priv;
    const int32_t *opl_buf = NULL;

    opl_buf = wss->opl.update(wss->opl.priv);

    for (uint16_t c = 0; c < len * 2; c++) {
        if (opl_buf)
            buffer[c] += opl_buf[c];
    }

    wss->opl.reset_buffer(wss->opl.priv);
}

void *
wss_init(UNUSED(const device_t *info))
{
    wss_t *wss = calloc(1, sizeof(wss_t));

    uint16_t addr    = device_get_config_hex16("base");
    wss->opl_enabled = device_get_config_int("opl");

    if (wss->opl_enabled)
        fm_driver_get_cs(FM_YMF262, &wss->opl);

    ad1848_init(&wss->ad1848, AD1848_TYPE_DEFAULT);

    ad1848_setirq(&wss->ad1848, 7);
    ad1848_setdma(&wss->ad1848, 3);

    if (wss->opl_enabled)
        io_sethandler(0x0388, 0x0004,
                      wss->opl.read, NULL, NULL,
                      wss->opl.write, NULL, NULL,
                      wss->opl.priv);

    io_sethandler(addr, 0x0004,
                  wss_read, NULL, NULL,
                  wss_write, NULL, NULL,
                  wss);
    io_sethandler(addr + 4, 0x0004,
                  ad1848_read, NULL, NULL,
                  ad1848_write, NULL, NULL,
                  &wss->ad1848);

    sound_add_handler(wss_get_buffer, wss);
    sound_in_add_handler(wss_put_buffer, wss);
    sound_in_start_input();

    if (wss->opl_enabled)
        music_add_handler(wss_get_music_buffer, wss);

    int c = 0;
    double  attenuation;
    for (c = 0; c < 16; c++) {
        attenuation = 0.0;
        if (c & 0x01)
            attenuation += 1.5;
        if (c & 0x02)
            attenuation += 3.0;
        if (c & 0x04)
            attenuation += 6.0;
        if (c & 0x08)
            attenuation += 12.0;

        attenuation = pow(10, attenuation / 10);

        wss_input_gain_vols_4bits[c] = (int) (attenuation);
    }

    return wss;
}

static uint8_t
ncr_audio_mca_read(const uint16_t port, void *priv)
{
    const wss_t *wss = (wss_t *) priv;
    return wss->pos_regs[port & 7];
}

static void
ncr_audio_mca_write(const uint16_t port, uint8_t val, void *priv)
{
    wss_t   *wss      = (wss_t *) priv;
    uint16_t ports[4] = { 0x530, 0xE80, 0xF40, 0x604 };
    uint16_t addr;

    if (port < 0x102)
        return;

    wss->opl_enabled = (wss->pos_regs[2] & 0x20) ? 1 : 0;
    addr             = ports[(wss->pos_regs[2] & 0x18) >> 3];

    io_removehandler(0x0388, 0x0004,
                     wss->opl.read, NULL, NULL,
                     wss->opl.write, NULL, NULL,
                     wss->opl.priv);
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
                          wss->opl.read, NULL, NULL,
                          wss->opl.write, NULL, NULL,
                          wss->opl.priv);

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
    const wss_t *wss = (wss_t *) priv;
    return (wss->pos_regs[2] & 1);
}

void *
ncr_audio_init(UNUSED(const device_t *info))
{
    wss_t *wss = calloc(1, sizeof(wss_t));

    fm_driver_get_cs(FM_YMF262, &wss->opl);
    ad1848_init(&wss->ad1848, AD1848_TYPE_DEFAULT);

    ad1848_setirq(&wss->ad1848, 7);
    ad1848_setdma(&wss->ad1848, 3);

    mca_add(ncr_audio_mca_read, ncr_audio_mca_write, ncr_audio_mca_feedb, NULL, wss);
    wss->pos_regs[0] = 0x16;
    wss->pos_regs[1] = 0x51;

    sound_add_handler(wss_get_buffer, wss);
    sound_in_add_handler(wss_put_buffer, wss);
    sound_in_start_input();

    if (wss->opl_enabled)
        music_add_handler(wss_get_music_buffer, wss);

    int c = 0;
    double  attenuation;
    for (c = 0; c < 16; c++) {
        attenuation = 0.0;
        if (c & 0x01)
            attenuation += 1.5;
        if (c & 0x02)
            attenuation += 3.0;
        if (c & 0x04)
            attenuation += 6.0;
        if (c & 0x08)
            attenuation += 12.0;

        attenuation = pow(10, attenuation / 10);

        wss_input_gain_vols_4bits[c] = (int) (attenuation);
    }

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
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x530,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x530", .value = 0x530 },
            { .description = "0x604", .value = 0x604 },
            { .description = "0xe80", .value = 0xe80 },
            { .description = "0xf40", .value = 0xf40 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "opl",
        .description    = "Enable OPL",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t wss_device = {
    .name          = "Windows Sound System",
    .internal_name = "wss",
    .flags         = DEVICE_ISA16 | DEVICE_AUDIO_IN,
    .local         = 0,
    .init          = wss_init,
    .close         = wss_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = wss_speed_changed,
    .force_redraw  = NULL,
    .config        = wss_config
};

const device_t ncr_business_audio_device = {
    .name          = "NCR Business Audio",
    .internal_name = "ncraudio",
    .flags         = DEVICE_MCA | DEVICE_AUDIO_IN,
    .local         = 0,
    .init          = ncr_audio_init,
    .close         = wss_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = wss_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};
