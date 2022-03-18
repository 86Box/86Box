/*Jazz sample rates :
  386-33 - 12kHz
  486-33 - 20kHz
  486-50 - 32kHz
  Pentium - 45kHz*/

#define _USE_MATH_DEFINES
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/filters.h>
#include <86box/io.h>
#include <86box/midi.h>
#include <86box/pic.h>
#include <86box/snd_azt2316a.h>
#include <86box/sound.h>
#include <86box/timer.h>
#include <86box/snd_sb.h>

#define ADPCM_4  1
#define ADPCM_26 2
#define ADPCM_2  3

/*The recording safety margin is intended for uneven "len" calls to the get_buffer mixer calls on sound_sb*/
#define SB_DSP_REC_SAFEFTY_MARGIN 4096

void pollsb(void *p);
void sb_poll_i(void *p);

static int sbe2dat[4][9] = {
    {0x01,   -0x02, -0x04, 0x08,  -0x10, 0x20,  0x40,  -0x80, -106},
    { -0x01, 0x02,  -0x04, 0x08,  0x10,  -0x20, 0x40,  -0x80, 165 },
    { -0x01, 0x02,  0x04,  -0x08, 0x10,  -0x20, -0x40, 0x80,  -151},
    { 0x01,  -0x02, 0x04,  -0x08, -0x10, 0x20,  -0x40, 0x80,  90  }
};

static int sb_commands[256] = {
    -1, 2, -1, 0, 1, 2, -1, 0, 1, -1, -1, -1, -1, -1, 2, 1,
    1, -1, -1, -1, 2, -1, 2, 2, -1, -1, -1, -1, 0, -1, -1, 0,
    0, -1, -1, -1, 2, -1, -1, -1, -1, -1, -1, -1, 0, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    1, 2, 2, -1, -1, -1, -1, -1, 2, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, 2, 2, 2, 2, -1, -1, -1, -1, -1, 0, -1, 0,
    2, 2, -1, -1, -1, -1, -1, -1, 2, 2, -1, -1, -1, -1, -1, -1,
    0, -1, -1, -1, -1, -1, -1, -1, 0, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, -1, -1, -1, -1, -1,
    1, 0, 1, 0, 1, -1, -1, 0, 0, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, 0, 0, -1, -1, -1, -1, -1, 1, 2, -1, -1, -1, -1, 0
};

char     sb16_copyright[]  = "COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.";
uint16_t sb_dsp_versions[] = { 0, 0, 0x105, 0x200, 0x201, 0x300, 0x302, 0x405, 0x40d, 0x410 };

/*These tables were 'borrowed' from DOSBox*/
int8_t scaleMap4[64] = {
    0, 1, 2, 3, 4, 5, 6, 7, 0, -1, -2, -3, -4, -5, -6, -7,
    1, 3, 5, 7, 9, 11, 13, 15, -1, -3, -5, -7, -9, -11, -13, -15,
    2, 6, 10, 14, 18, 22, 26, 30, -2, -6, -10, -14, -18, -22, -26, -30,
    4, 12, 20, 28, 36, 44, 52, 60, -4, -12, -20, -28, -36, -44, -52, -60
};

uint8_t adjustMap4[64] = {
    0, 0, 0, 0, 0, 16, 16, 16,
    0, 0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0, 0, 0, 0,
    240, 0, 0, 0, 0, 0, 0, 0
};

int8_t scaleMap26[40] = {
    0, 1, 2, 3, 0, -1, -2, -3,
    1, 3, 5, 7, -1, -3, -5, -7,
    2, 6, 10, 14, -2, -6, -10, -14,
    4, 12, 20, 28, -4, -12, -20, -28,
    5, 15, 25, 35, -5, -15, -25, -35
};

uint8_t adjustMap26[40] = {
    0, 0, 0, 8, 0, 0, 0, 8,
    248, 0, 0, 8, 248, 0, 0, 8,
    248, 0, 0, 8, 248, 0, 0, 8,
    248, 0, 0, 8, 248, 0, 0, 8,
    248, 0, 0, 0, 248, 0, 0, 0
};

int8_t scaleMap2[24] = {
    0, 1, 0, -1, 1, 3, -1, -3,
    2, 6, -2, -6, 4, 12, -4, -12,
    8, 24, -8, -24, 6, 48, -16, -48
};

uint8_t adjustMap2[24] = {
    0, 4, 0, 4,
    252, 4, 252, 4, 252, 4, 252, 4,
    252, 4, 252, 4, 252, 4, 252, 4,
    252, 0, 252, 0
};

double low_fir_sb16_coef[2][SB16_NCoef];

#ifdef ENABLE_SB_DSP_LOG
int sb_dsp_do_log = ENABLE_SB_DSP_LOG;

static void
sb_dsp_log(const char *fmt, ...)
{
    va_list ap;

    if (sb_dsp_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sb_dsp_log(fmt, ...)
#endif

static __inline double
sinc(double x)
{
    return sin(M_PI * x) / (M_PI * x);
}

static void
recalc_sb16_filter(int c, int playback_freq)
{
    /* Cutoff frequency = playback / 2 */
    int    n;
    double w, h;
    double fC = ((double) playback_freq) / 96000.0;
    double gain;

    for (n = 0; n < SB16_NCoef; n++) {
        /* Blackman window */
        w = 0.42 - (0.5 * cos((2.0 * n * M_PI) / (double) (SB16_NCoef - 1))) + (0.08 * cos((4.0 * n * M_PI) / (double) (SB16_NCoef - 1)));
        /* Sinc filter */
        h = sinc(2.0 * fC * ((double) n - ((double) (SB16_NCoef - 1) / 2.0)));

        /* Create windowed-sinc filter */
        low_fir_sb16_coef[c][n] = w * h;
    }

    low_fir_sb16_coef[c][(SB16_NCoef - 1) / 2] = 1.0;

    gain = 0.0;
    for (n = 0; n < SB16_NCoef; n++)
        gain += low_fir_sb16_coef[c][n];

    /* Normalise filter, to produce unity gain */
    for (n = 0; n < SB16_NCoef; n++)
        low_fir_sb16_coef[c][n] /= gain;
}

static void
sb_irq_update_pic(void *priv, int set)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;
    if (set)
        picint(1 << dsp->sb_irqnum);
    else
        picintc(1 << dsp->sb_irqnum);
}

void
sb_update_mask(sb_dsp_t *dsp, int irqm8, int irqm16, int irqm401)
{
    int clear = 0;

    if (!dsp->sb_irqm8 && irqm8)
        clear |= 1;
    dsp->sb_irqm8 = irqm8;
    if (!dsp->sb_irqm16 && irqm16)
        clear |= 1;
    dsp->sb_irqm16 = irqm16;
    if (!dsp->sb_irqm401 && irqm401)
        clear |= 1;
    dsp->sb_irqm401 = irqm401;

    if (clear)
        dsp->irq_update(dsp->irq_priv, 0);
}

void
sb_update_status(sb_dsp_t *dsp, int bit, int set)
{
    int masked = 0;

    switch (bit) {
        case 0:
        default:
            dsp->sb_irq8 = set;
            masked       = dsp->sb_irqm8;
            break;
        case 1:
            dsp->sb_irq16 = set;
            masked        = dsp->sb_irqm16;
            break;
        case 2:
            dsp->sb_irq401 = set;
            masked         = dsp->sb_irqm401;
            break;
    }

    if (set && !masked)
        dsp->irq_update(dsp->irq_priv, 1);
    else if (!set)
        dsp->irq_update(dsp->irq_priv, 0);
}

void
sb_irq(sb_dsp_t *dsp, int irq8)
{
    sb_update_status(dsp, !irq8, 1);
}

void
sb_irqc(sb_dsp_t *dsp, int irq8)
{
    sb_update_status(dsp, !irq8, 0);
}

static void
sb_dsp_irq_update(void *priv, int set)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;

    sb_update_status(dsp, 2, set);
}

static int
sb_dsp_irq_pending(void *priv)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;

    return dsp->sb_irq401;
}

void
sb_dsp_set_mpu(sb_dsp_t *dsp, mpu_t *mpu)
{
    dsp->mpu = mpu;

    if (mpu != NULL)
        mpu401_irq_attach(mpu, sb_dsp_irq_update, sb_dsp_irq_pending, dsp);
}

void
sb_dsp_reset(sb_dsp_t *dsp)
{
    midi_clear_buffer();

    timer_disable(&dsp->output_timer);
    timer_disable(&dsp->input_timer);

    dsp->sb_command = 0;

    dsp->sb_8_length  = 0xffff;
    dsp->sb_8_autolen = 0xffff;

    dsp->sb_irq8     = 0;
    dsp->sb_irq16    = 0;
    dsp->sb_irq401   = 0;
    dsp->sb_16_pause = 0;
    dsp->sb_read_wp = dsp->sb_read_rp = 0;
    dsp->sb_data_stat                 = -1;
    dsp->sb_speaker                   = 0;
    dsp->sb_pausetime                 = -1LL;
    dsp->sbe2                         = 0xAA;
    dsp->sbe2count                    = 0;

    dsp->sbreset = 0;

    dsp->record_pos_read  = 0;
    dsp->record_pos_write = SB_DSP_REC_SAFEFTY_MARGIN;

    dsp->irq_update(dsp->irq_priv, 0);

    dsp->asp_data_len = 0;
}

void
sb_doreset(sb_dsp_t *dsp)
{
    int c;

    sb_dsp_reset(dsp);

    if (IS_AZTECH(dsp)) {
        sb_commands[8] = 1;
        sb_commands[9] = 1;
    } else {
        if (dsp->sb_type >= SB16)
            sb_commands[8] = 1;
        else
            sb_commands[8] = -1;
    }

    dsp->sb_asp_mode      = 0;
    dsp->sb_asp_ram_index = 0;
    for (c = 0; c < 256; c++)
        dsp->sb_asp_regs[c] = 0;

    dsp->sb_asp_regs[5] = 0x01;
    dsp->sb_asp_regs[9] = 0xf8;
}

void
sb_dsp_speed_changed(sb_dsp_t *dsp)
{
    if (dsp->sb_timeo < 256)
        dsp->sblatcho = TIMER_USEC * (256 - dsp->sb_timeo);
    else
        dsp->sblatcho = (uint64_t) (TIMER_USEC * (1000000.0f / (float) (dsp->sb_timeo - 256)));

    if (dsp->sb_timei < 256)
        dsp->sblatchi = TIMER_USEC * (256 - dsp->sb_timei);
    else
        dsp->sblatchi = (uint64_t) (TIMER_USEC * (1000000.0f / (float) (dsp->sb_timei - 256)));
}

void
sb_add_data(sb_dsp_t *dsp, uint8_t v)
{
    dsp->sb_read_data[dsp->sb_read_wp++] = v;
    dsp->sb_read_wp &= 0xff;
}

void
sb_start_dma(sb_dsp_t *dsp, int dma8, int autoinit, uint8_t format, int len)
{
    dsp->sb_pausetime = -1;

    if (dma8) {
        dsp->sb_8_length   = dsp->sb_8_origlength = len;
        dsp->sb_8_format   = format;
        dsp->sb_8_autoinit = autoinit;
        dsp->sb_8_pause    = 0;
        dsp->sb_8_enable   = 1;

        if (dsp->sb_16_enable && dsp->sb_16_output)
            dsp->sb_16_enable = 0;
        dsp->sb_8_output = 1;
        if (!timer_is_enabled(&dsp->output_timer))
            timer_set_delay_u64(&dsp->output_timer, dsp->sblatcho);
        dsp->sbleftright = dsp->sbleftright_default;
        dsp->sbdacpos    = 0;
    } else {
        dsp->sb_16_length   = dsp->sb_16_origlength = len;
        dsp->sb_16_format   = format;
        dsp->sb_16_autoinit = autoinit;
        dsp->sb_16_pause    = 0;
        dsp->sb_16_enable   = 1;
        if (dsp->sb_8_enable && dsp->sb_8_output)
            dsp->sb_8_enable = 0;
        dsp->sb_16_output = 1;
        if (!timer_is_enabled(&dsp->output_timer))
            timer_set_delay_u64(&dsp->output_timer, dsp->sblatcho);
    }
}

void
sb_start_dma_i(sb_dsp_t *dsp, int dma8, int autoinit, uint8_t format, int len)
{
    if (dma8) {
        dsp->sb_8_length   = dsp->sb_8_origlength = len;
        dsp->sb_8_format   = format;
        dsp->sb_8_autoinit = autoinit;
        dsp->sb_8_pause    = 0;
        dsp->sb_8_enable   = 1;
        if (dsp->sb_16_enable && !dsp->sb_16_output)
            dsp->sb_16_enable = 0;
        dsp->sb_8_output = 0;
        if (!timer_is_enabled(&dsp->input_timer))
            timer_set_delay_u64(&dsp->input_timer, dsp->sblatchi);
    } else {
        dsp->sb_16_length   = dsp->sb_16_origlength = len;
        dsp->sb_16_format   = format;
        dsp->sb_16_autoinit = autoinit;
        dsp->sb_16_pause    = 0;
        dsp->sb_16_enable   = 1;
        if (dsp->sb_8_enable && !dsp->sb_8_output)
            dsp->sb_8_enable = 0;
        dsp->sb_16_output = 0;
        if (!timer_is_enabled(&dsp->input_timer))
            timer_set_delay_u64(&dsp->input_timer, dsp->sblatchi);
    }

    memset(dsp->record_buffer, 0, sizeof(dsp->record_buffer));
}

int
sb_8_read_dma(void *priv)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;
    return dma_channel_read(dsp->sb_8_dmanum);
}

int
sb_8_write_dma(void *priv, uint8_t val)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;
    return dma_channel_write(dsp->sb_8_dmanum, val) == DMA_NODATA;
}

int
sb_16_read_dma(void *priv)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;
    return dma_channel_read(dsp->sb_16_dmanum);
}

int
sb_16_write_dma(void *priv, uint16_t val)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;
    return dma_channel_write(dsp->sb_16_dmanum, val) == DMA_NODATA;
}

void
sb_dsp_setirq(sb_dsp_t *dsp, int irq)
{
    dsp->sb_irqnum = irq;
}

void
sb_dsp_setdma8(sb_dsp_t *dsp, int dma)
{
    dsp->sb_8_dmanum = dma;
}

void
sb_dsp_setdma16(sb_dsp_t *dsp, int dma)
{
    dsp->sb_16_dmanum = dma;
}

void
sb_exec_command(sb_dsp_t *dsp)
{
    int temp, c;

    sb_dsp_log("sb_exec_command : SB command %02X\n", dsp->sb_command);

    /* Update 8051 ram with the current DSP command.
       See https://github.com/joncampbell123/dosbox-x/issues/1044 */
    if (dsp->sb_type >= SB16)
        dsp->sb_8051_ram[0x20] = dsp->sb_command;

    switch (dsp->sb_command) {
        case 0x01: /* ???? */
            if (dsp->sb_type >= SB16)
                dsp->asp_data_len = dsp->sb_data[0] + (dsp->sb_data[1] << 8) + 1;
            break;
        case 0x03: /* ASP status */
            if (dsp->sb_type >= SB16)
                sb_add_data(dsp, 0);
            break;
        case 0x10: /* 8-bit direct mode */
            sb_dsp_update(dsp);
            dsp->sbdat = dsp->sbdatl = dsp->sbdatr = (dsp->sb_data[0] ^ 0x80) << 8;
            break;
        case 0x14: /* 8-bit single cycle DMA output */
            sb_start_dma(dsp, 1, 0, 0, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
            break;
        case 0x17: /* 2-bit ADPCM output with reference */
            dsp->sbref  = dsp->dma_readb(dsp->dma_priv);
            dsp->sbstep = 0;
            /* Fall through */
        case 0x16: /* 2-bit ADPCM output */
            sb_start_dma(dsp, 1, 0, ADPCM_2, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
            dsp->sbdat2 = dsp->dma_readb(dsp->dma_priv);
            dsp->sb_8_length--;
            if (dsp->sb_command == 0x17)
                dsp->sb_8_length--;
            break;
        case 0x1C: /* 8-bit autoinit DMA output */
            if (dsp->sb_type >= SB15)
                sb_start_dma(dsp, 1, 1, 0, dsp->sb_8_autolen);
            break;
        case 0x1F: /* 2-bit ADPCM autoinit output */
            if (dsp->sb_type >= SB15) {
                sb_start_dma(dsp, 1, 1, ADPCM_2, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
                dsp->sbdat2 = dsp->dma_readb(dsp->dma_priv);
                dsp->sb_8_length--;
            }
            break;
        case 0x20: /* 8-bit direct input */
            sb_add_data(dsp, (dsp->record_buffer[dsp->record_pos_read] >> 8) ^ 0x80);
            /* Due to the current implementation, I need to emulate a samplerate, even if this
               mode does not imply such samplerate. Position is increased in sb_poll_i(). */
            if (!timer_is_enabled(&dsp->input_timer)) {
                dsp->sb_timei = 256 - 22;
                dsp->sblatchi = TIMER_USEC * 22;
                temp          = 1000000 / 22;
                dsp->sb_freq  = temp;
                timer_set_delay_u64(&dsp->input_timer, dsp->sblatchi);
            }
            break;
        case 0x24: /* 8-bit single cycle DMA input */
            sb_start_dma_i(dsp, 1, 0, 0, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
            break;
        case 0x28: /* Direct ADC, 8-bit (Burst) */
            break;
        case 0x2C: /* 8-bit autoinit DMA input */
            if (dsp->sb_type >= SB15)
                sb_start_dma_i(dsp, 1, 1, 0, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
            break;
        case 0x30: /* MIDI Polling mode input */
            sb_dsp_log("MIDI polling mode input\n");
            dsp->midi_in_poll = 1;
            dsp->uart_irq     = 0;
            break;
        case 0x31: /* MIDI Interrupt mode input */
            sb_dsp_log("MIDI interrupt mode input\n");
            dsp->midi_in_poll = 0;
            dsp->uart_irq     = 1;
            break;
        case 0x32: /* MIDI Read Timestamp Poll */
            break;
        case 0x33: /* MIDI Read Timestamp Interrupt */
            break;
        case 0x34: /* MIDI In poll  */
            if (dsp->sb_type < SB2)
                break;
            sb_dsp_log("MIDI poll in\n");
            dsp->midi_in_poll = 1;
            dsp->uart_midi    = 1;
            dsp->uart_irq     = 0;
            break;
        case 0x35: /* MIDI In irq */
            if (dsp->sb_type < SB2)
                break;
            sb_dsp_log("MIDI irq in\n");
            dsp->midi_in_poll = 0;
            dsp->uart_midi    = 1;
            dsp->uart_irq     = 1;
            break;
        case 0x36:
        case 0x37: /* MIDI timestamps */
            break;
        case 0x38: /* Write to SB MIDI Output (Raw) */
            dsp->onebyte_midi = 1;
            break;
        case 0x40: /* Set time constant */
            dsp->sb_timei = dsp->sb_timeo = dsp->sb_data[0];
            dsp->sblatcho = dsp->sblatchi = TIMER_USEC * (256 - dsp->sb_data[0]);
            temp                          = 256 - dsp->sb_data[0];
            temp                          = 1000000 / temp;
            sb_dsp_log("Sample rate - %ihz (%i)\n", temp, dsp->sblatcho);
            if ((dsp->sb_freq != temp) && (dsp->sb_type >= SB16))
                recalc_sb16_filter(0, temp);
            dsp->sb_freq = temp;
            break;
        case 0x41: /* Set output sampling rate */
        case 0x42: /* Set input sampling rate */
            if (dsp->sb_type >= SB16) {
                dsp->sblatcho = (uint64_t) (TIMER_USEC * (1000000.0f / (float) (dsp->sb_data[1] + (dsp->sb_data[0] << 8))));
                sb_dsp_log("Sample rate - %ihz (%i)\n", dsp->sb_data[1] + (dsp->sb_data[0] << 8), dsp->sblatcho);
                temp          = dsp->sb_freq;
                dsp->sb_freq  = dsp->sb_data[1] + (dsp->sb_data[0] << 8);
                dsp->sb_timeo = 256LL + dsp->sb_freq;
                dsp->sblatchi = dsp->sblatcho;
                dsp->sb_timei = dsp->sb_timeo;
                if (dsp->sb_freq != temp && dsp->sb_type >= SB16)
                    recalc_sb16_filter(0, dsp->sb_freq);
                dsp->sb_8051_ram[0x13] = dsp->sb_freq & 0xff;
                dsp->sb_8051_ram[0x14] = (dsp->sb_freq >> 8) & 0xff;
            }
            break;
        case 0x45: /* Continue Auto-Initialize DMA, 8-bit */
            break;
        case 0x47: /* Continue Auto-Initialize DMA, 16-bit */
            break;
        case 0x48: /* Set DSP block transfer size */
            dsp->sb_8_autolen = dsp->sb_data[0] + (dsp->sb_data[1] << 8);
            break;
        case 0x75: /* 4-bit ADPCM output with reference */
            dsp->sbref  = dsp->dma_readb(dsp->dma_priv);
            dsp->sbstep = 0;
            /* Fall through */
        case 0x74: /* 4-bit ADPCM output */
            sb_start_dma(dsp, 1, 0, ADPCM_4, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
            dsp->sbdat2 = dsp->dma_readb(dsp->dma_priv);
            dsp->sb_8_length--;
            if (dsp->sb_command == 0x75)
                dsp->sb_8_length--;
            break;
        case 0x77: /* 2.6-bit ADPCM output with reference */
            dsp->sbref  = dsp->dma_readb(dsp->dma_priv);
            dsp->sbstep = 0;
            /* Fall through */
        case 0x76: /* 2.6-bit ADPCM output */
            sb_start_dma(dsp, 1, 0, ADPCM_26, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
            dsp->sbdat2 = dsp->dma_readb(dsp->dma_priv);
            dsp->sb_8_length--;
            if (dsp->sb_command == 0x77)
                dsp->sb_8_length--;
            break;
        case 0x7D: /* 4-bit ADPCM autoinit output */
            if (dsp->sb_type >= SB15) {
                sb_start_dma(dsp, 1, 1, ADPCM_4, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
                dsp->sbdat2 = dsp->dma_readb(dsp->dma_priv);
                dsp->sb_8_length--;
            }
            break;
        case 0x7F: /* 2.6-bit ADPCM autoinit output */
            if (dsp->sb_type >= SB15) {
                sb_start_dma(dsp, 1, 1, ADPCM_26, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
                dsp->sbdat2 = dsp->dma_readb(dsp->dma_priv);
                dsp->sb_8_length--;
            }
            break;
        case 0x80: /* Pause DAC */
            dsp->sb_pausetime = dsp->sb_data[0] + (dsp->sb_data[1] << 8);
            if (!timer_is_enabled(&dsp->output_timer))
                timer_set_delay_u64(&dsp->output_timer, dsp->sblatcho);
            break;
        case 0x90: /* High speed 8-bit autoinit DMA output */
            if (dsp->sb_type >= SB2)
                sb_start_dma(dsp, 1, 1, 0, dsp->sb_8_autolen);
            break;
        case 0x91: /* High speed 8-bit single cycle DMA output */
            if (dsp->sb_type >= SB2)
                sb_start_dma(dsp, 1, 0, 0, dsp->sb_8_autolen);
            break;
        case 0x98: /* High speed 8-bit autoinit DMA input */
            if (dsp->sb_type >= SB2)
                sb_start_dma_i(dsp, 1, 1, 0, dsp->sb_8_autolen);
            break;
        case 0x99: /* High speed 8-bit single cycle DMA input */
            if (dsp->sb_type >= SB2)
                sb_start_dma_i(dsp, 1, 0, 0, dsp->sb_8_autolen);
            break;
        case 0xA0: /* Set input mode to mono */
        case 0xA8: /* Set input mode to stereo */
            if ((dsp->sb_type < SB2) || (dsp->sb_type > SBPRO2))
                break;
            /* TODO: Implement. 3.xx-only command. */
            break;
        case 0xB0:
        case 0xB1:
        case 0xB2:
        case 0xB3:
        case 0xB4:
        case 0xB5:
        case 0xB6:
        case 0xB7: /* 16-bit DMA output */
            if (dsp->sb_type >= SB16) {
                sb_start_dma(dsp, 0, dsp->sb_command & 4, dsp->sb_data[0], dsp->sb_data[1] + (dsp->sb_data[2] << 8));
                dsp->sb_16_autolen = dsp->sb_data[1] + (dsp->sb_data[2] << 8);
            }
            break;
        case 0xB8:
        case 0xB9:
        case 0xBA:
        case 0xBB:
        case 0xBC:
        case 0xBD:
        case 0xBE:
        case 0xBF: /* 16-bit DMA input */
            if (dsp->sb_type >= SB16) {
                sb_start_dma_i(dsp, 0, dsp->sb_command & 4, dsp->sb_data[0], dsp->sb_data[1] + (dsp->sb_data[2] << 8));
                dsp->sb_16_autolen = dsp->sb_data[1] + (dsp->sb_data[2] << 8);
            }
            break;
        case 0xC0:
        case 0xC1:
        case 0xC2:
        case 0xC3:
        case 0xC4:
        case 0xC5:
        case 0xC6:
        case 0xC7: /* 8-bit DMA output */
            if (dsp->sb_type >= SB16) {
                sb_start_dma(dsp, 1, dsp->sb_command & 4, dsp->sb_data[0], dsp->sb_data[1] + (dsp->sb_data[2] << 8));
                dsp->sb_8_autolen = dsp->sb_data[1] + (dsp->sb_data[2] << 8);
            }
            break;
        case 0xC8:
        case 0xC9:
        case 0xCA:
        case 0xCB:
        case 0xCC:
        case 0xCD:
        case 0xCE:
        case 0xCF: /* 8-bit DMA input */
            if (dsp->sb_type >= SB16) {
                sb_start_dma_i(dsp, 1, dsp->sb_command & 4, dsp->sb_data[0], dsp->sb_data[1] + (dsp->sb_data[2] << 8));
                dsp->sb_8_autolen = dsp->sb_data[1] + (dsp->sb_data[2] << 8);
            }
            break;
        case 0xD0: /* Pause 8-bit DMA */
            dsp->sb_8_pause = 1;
            break;
        case 0xD1: /* Speaker on */
            if (dsp->sb_type < SB15)
                dsp->sb_8_pause = 1;
            else if (dsp->sb_type < SB16)
                dsp->muted = 0;
            dsp->sb_speaker = 1;
            break;
        case 0xD3: /* Speaker off */
            if (dsp->sb_type < SB15)
                dsp->sb_8_pause = 1;
            else if (dsp->sb_type < SB16)
                dsp->muted = 1;
            dsp->sb_speaker = 0;
            break;
        case 0xD4: /* Continue 8-bit DMA */
            dsp->sb_8_pause = 0;
            break;
        case 0xD5: /* Pause 16-bit DMA */
            if (dsp->sb_type >= SB16)
                dsp->sb_16_pause = 1;
            break;
        case 0xD6: /* Continue 16-bit DMA */
            if (dsp->sb_type >= SB16)
                dsp->sb_16_pause = 0;
            break;
        case 0xD8: /* Get speaker status */
            sb_add_data(dsp, dsp->sb_speaker ? 0xff : 0);
            break;
        case 0xD9: /* Exit 16-bit auto-init mode */
            if (dsp->sb_type >= SB16)
                dsp->sb_16_autoinit = 0;
            break;
        case 0xDA: /* Exit 8-bit auto-init mode */
            dsp->sb_8_autoinit = 0;
            break;
        case 0xE0: /* DSP identification */
            sb_add_data(dsp, ~dsp->sb_data[0]);
            break;
        case 0xE1: /* Get DSP version */
            if (IS_AZTECH(dsp)) {
                if (dsp->sb_subtype == SB_SUBTYPE_CLONE_AZT2316A_0X11) {
                    sb_add_data(dsp, 0x3);
                    sb_add_data(dsp, 0x1);
                } else if (dsp->sb_subtype == SB_SUBTYPE_CLONE_AZT1605_0X0C) {
                    sb_add_data(dsp, 0x2);
                    sb_add_data(dsp, 0x1);
                }
                break;
            }
            sb_add_data(dsp, sb_dsp_versions[dsp->sb_type] >> 8);
            sb_add_data(dsp, sb_dsp_versions[dsp->sb_type] & 0xff);
            break;
        case 0xE2: /* Stupid ID/protection */
            for (c = 0; c < 8; c++) {
                if (dsp->sb_data[0] & (1 << c))
                    dsp->sbe2 += sbe2dat[dsp->sbe2count & 3][c];
            }
            dsp->sbe2 += sbe2dat[dsp->sbe2count & 3][8];
            dsp->sbe2count++;
            dsp->dma_writeb(dsp->dma_priv, dsp->sbe2);
            break;
        case 0xE3: /* DSP copyright */
            if (dsp->sb_type >= SB16) {
                c = 0;
                while (sb16_copyright[c])
                    sb_add_data(dsp, sb16_copyright[c++]);
                sb_add_data(dsp, 0);
            }
            break;
        case 0xE4: /* Write test register */
            dsp->sb_test = dsp->sb_data[0];
            break;
        case 0xE8: /* Read test register */
            sb_add_data(dsp, dsp->sb_test);
            break;
        case 0xF2: /* Trigger 8-bit IRQ */
            sb_dsp_log("Trigger IRQ\n");
            sb_irq(dsp, 1);
            break;
        case 0xF3: /* Trigger 16-bit IRQ */
            sb_dsp_log("Trigger IRQ\n");
            sb_irq(dsp, 0);
            break;
        case 0xE7: /* ???? */
            break;
        case 0x07:
        case 0xFF: /* No, that's not how you program auto-init DMA */
            break;
        case 0x08: /* ASP get version / AZTECH type/EEPROM access */
            if (IS_AZTECH(dsp)) {
                if ((dsp->sb_data[0] == 0x05 || dsp->sb_data[0] == 0x55) && dsp->sb_subtype == SB_SUBTYPE_CLONE_AZT2316A_0X11)
                    sb_add_data(dsp, 0x11); /* AZTECH get type, WASHINGTON/latest - according to devkit. E.g.: The one in the Itautec Infoway Multimidia */
                else if ((dsp->sb_data[0] == 0x05 || dsp->sb_data[0] == 0x55) && dsp->sb_subtype == SB_SUBTYPE_CLONE_AZT1605_0X0C)
                    sb_add_data(dsp, 0x0C); /* AZTECH get type, CLINTON - according to devkit. E.g.: The one in the Packard Bell Legend 100CD */
                else if (dsp->sb_data[0] == 0x08) {
                    /* EEPROM address to write followed by byte */
                    if (dsp->sb_data[1] < 0 || dsp->sb_data[1] >= AZTECH_EEPROM_SIZE)
                        fatal("AZT EEPROM: out of bounds write to %02X\n", dsp->sb_data[1]);
                    sb_dsp_log("EEPROM write = %02x\n", dsp->sb_data[2]);
                    dsp->azt_eeprom[dsp->sb_data[1]] = dsp->sb_data[2];
                    break;
                } else if (dsp->sb_data[0] == 0x07) {
                    /* EEPROM address to read */
                    if (dsp->sb_data[1] < 0 || dsp->sb_data[1] >= AZTECH_EEPROM_SIZE)
                        fatal("AZT EEPROM: out of bounds read to %02X\n", dsp->sb_data[1]);
                    sb_dsp_log("EEPROM read = %02x\n", dsp->azt_eeprom[dsp->sb_data[1]]);
                    sb_add_data(dsp, dsp->azt_eeprom[dsp->sb_data[1]]);
                    break;
                } else
                    sb_dsp_log("AZT2316A: UNKNOWN 0x08 COMMAND: %02X\n", dsp->sb_data[0]); /* 0x08 (when shutting down, driver tries to read 1 byte of response), 0x55, 0x0D, 0x08D seen */
                break;
            }
            if (dsp->sb_type == SBAWE64) /* AWE64 has no ASP or a socket for it */
                sb_add_data(dsp, 0xFF);
            else if (dsp->sb_type >= SB16)
                sb_add_data(dsp, 0x18);
            break;
        case 0x0E: /* ASP set register */
            if (dsp->sb_type >= SB16) {
                dsp->sb_asp_regs[dsp->sb_data[0]] = dsp->sb_data[1];

                if ((dsp->sb_data[0] == 0x83) && (dsp->sb_asp_mode & 128) && (dsp->sb_asp_mode & 8)) { /* ASP memory write */
                    if (dsp->sb_asp_mode & 8)
                        dsp->sb_asp_ram_index = 0;

                    dsp->sb_asp_ram[dsp->sb_asp_ram_index] = dsp->sb_data[1];

                    if (dsp->sb_asp_mode & 2) {
                        dsp->sb_asp_ram_index++;
                        if (dsp->sb_asp_ram_index >= 2048)
                            dsp->sb_asp_ram_index = 0;
                    }
                }
                sb_dsp_log("SB16 ASP write reg %02X, val %02X\n", dsp->sb_data[0], dsp->sb_data[1]);
            }
            break;
        case 0x0F: /* ASP get register */
            if (dsp->sb_type >= SB16) {
                if ((dsp->sb_data[0] == 0x83) && (dsp->sb_asp_mode & 128) && (dsp->sb_asp_mode & 8)) { /* ASP memory read */
                    if (dsp->sb_asp_mode & 8)
                        dsp->sb_asp_ram_index = 0;

                    dsp->sb_asp_regs[0x83] = dsp->sb_asp_ram[dsp->sb_asp_ram_index];

                    if (dsp->sb_asp_mode & 1) {
                        dsp->sb_asp_ram_index++;
                        if (dsp->sb_asp_ram_index >= 2048)
                            dsp->sb_asp_ram_index = 0;
                    }
                } else if (dsp->sb_data[0] == 0x83) {
                    dsp->sb_asp_regs[0x83] = 0x18;
                }
                sb_add_data(dsp, dsp->sb_asp_regs[dsp->sb_data[0]]);
                sb_dsp_log("SB16 ASP read reg %02X, val %02X\n", dsp->sb_data[0], dsp->sb_asp_regs[dsp->sb_data[0]]);
            }
            break;
        case 0xF8:
            if (dsp->sb_type < SB16)
                sb_add_data(dsp, 0);
            break;
        case 0xF9: /* SB16 8051 RAM read */
            if (dsp->sb_type >= SB16)
                sb_add_data(dsp, dsp->sb_8051_ram[dsp->sb_data[0]]);
            break;
        case 0xFA: /* SB16 8051 RAM write */
            if (dsp->sb_type >= SB16)
                dsp->sb_8051_ram[dsp->sb_data[0]] = dsp->sb_data[1];
            break;
        case 0x04: /* ASP set mode register */
            if (dsp->sb_type >= SB16) {
                dsp->sb_asp_mode = dsp->sb_data[0];
                if (dsp->sb_asp_mode & 4)
                    dsp->sb_asp_ram_index = 0;
                sb_dsp_log("SB16 ASP set mode %02X\n", dsp->sb_asp_mode);
            } /* else DSP Status (Obsolete) */
            break;
        case 0x05: /* ASP set codec parameter */
            if (dsp->sb_type >= SB16)
                sb_dsp_log("SB16 ASP unknown codec params %02X, %02X\n", dsp->sb_data[0], dsp->sb_data[1]);
            break;

        case 0x09: /* AZTECH mode set */
            if (IS_AZTECH(dsp)) {
                if (dsp->sb_data[0] == 0x00) {
                    sb_dsp_log("AZT2316A: WSS MODE!\n");
                    azt2316a_enable_wss(1, dsp->parent);
                } else if (dsp->sb_data[0] == 0x01) {
                    sb_dsp_log("AZT2316A: SB8PROV2 MODE!\n");
                    azt2316a_enable_wss(0, dsp->parent);
                } else
                    sb_dsp_log("AZT2316A: UNKNOWN MODE! = %02x\n", dsp->sb_data[0]); // sequences 0x02->0xFF, 0x04->0xFF seen
            }
            break;

            /* TODO: Some more data about the DSP registeres
             * http://the.earth.li/~tfm/oldpage/sb_dsp.html
             * http://www.synchrondata.com/pheaven/www/area19.htm
             * http://www.dcee.net/Files/Programm/Sound/
             *  0E3h           DSP Copyright                                       SBPro2???
             *  0F0h           Sine Generator                                      SB
             *  0F1h           DSP Auxiliary Status (Obsolete)                     SB-Pro2
             *  0F2h           IRQ Request, 8-bit                                  SB
             *  0F3h           IRQ Request, 16-bit                                 SB16
             *  0FBh           DSP Status                                          SB16
             *  0FCh           DSP Auxiliary Status                                SB16
             *  0FDh           DSP Command Status                                  SB16
             */
    }

    /* Update 8051 ram with the last DSP command.
       See https://github.com/joncampbell123/dosbox-x/issues/1044 */
    if (dsp->sb_type >= SB16)
        dsp->sb_8051_ram[0x30] = dsp->sb_command;
}

void
sb_write(uint16_t a, uint8_t v, void *priv)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;

    switch (a & 0xF) {
        case 6: /* Reset */
            if (!dsp->uart_midi) {
                if (!(v & 1) && (dsp->sbreset & 1)) {
                    sb_dsp_reset(dsp);
                    sb_add_data(dsp, 0xAA);
                }
                dsp->sbreset = v;
            }
            dsp->uart_midi    = 0;
            dsp->uart_irq     = 0;
            dsp->onebyte_midi = 0;
            return;
        case 0xC: /* Command/data write */
            if (dsp->uart_midi || dsp->onebyte_midi) {
                midi_raw_out_byte(v);
                dsp->onebyte_midi = 0;
                return;
            }
            timer_set_delay_u64(&dsp->wb_timer, TIMER_USEC * 1);
            if (dsp->asp_data_len) {
                sb_dsp_log("ASP data %i\n", dsp->asp_data_len);
                dsp->asp_data_len--;
                if (!dsp->asp_data_len)
                    sb_add_data(dsp, 0);
                return;
            }
            if (dsp->sb_data_stat == -1) {
                dsp->sb_command = v;
                if (v == 0x01)
                    sb_add_data(dsp, 0);
                dsp->sb_data_stat++;
            } else {
                dsp->sb_data[dsp->sb_data_stat++] = v;
                if (IS_AZTECH(dsp)) {
                    /* variable length commands */
                    if (dsp->sb_command == 0x08 && dsp->sb_data_stat == 1 && dsp->sb_data[0] == 0x08)
                        sb_commands[dsp->sb_command] = 3;
                    else if (dsp->sb_command == 0x08 && dsp->sb_data_stat == 1 && dsp->sb_data[0] == 0x07)
                        sb_commands[dsp->sb_command] = 2;
                }
            }
            if (dsp->sb_data_stat == sb_commands[dsp->sb_command] || sb_commands[dsp->sb_command] == -1) {
                sb_exec_command(dsp);
                dsp->sb_data_stat = -1;
                if (IS_AZTECH(dsp)) {
                    /* variable length commands */
                    if (dsp->sb_command == 0x08)
                        sb_commands[dsp->sb_command] = 1;
                }
            }
            break;
    }
}

uint8_t
sb_read(uint16_t a, void *priv)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;
    uint8_t   ret = 0x00;

    switch (a & 0xf) {
        case 0xA: /* Read data */
            if (dsp->mpu && dsp->uart_midi) {
                ret = MPU401_ReadData(dsp->mpu);
            } else {
                dsp->sbreaddat = dsp->sb_read_data[dsp->sb_read_rp];
                if (dsp->sb_read_rp != dsp->sb_read_wp) {
                    dsp->sb_read_rp++;
                    dsp->sb_read_rp &= 0xff;
                }
                return dsp->sbreaddat;
            }
            break;
        case 0xC: /* Write data ready */
            if (dsp->sb_8_enable || dsp->sb_type >= SB16)
                dsp->busy_count = (dsp->busy_count + 1) & 3;
            else
                dsp->busy_count = 0;
            if (dsp->wb_full || (dsp->busy_count & 2)) {
                dsp->wb_full = timer_is_enabled(&dsp->wb_timer);
                if (IS_AZTECH(dsp)) {
                    sb_dsp_log("SB Write Data Aztech read 0x80\n");
                    return 0x80;
                } else {
                    sb_dsp_log("SB Write Data Creative read 0xff\n");
                    return 0xff;
                }
            }
            if (IS_AZTECH(dsp)) {
                sb_dsp_log("SB Write Data Aztech read 0x00\n");
                ret = 0x00;
            } else {
                sb_dsp_log("SB Write Data Creative read 0x7f\n");
                ret = 0x7f;
            }
            break;
        case 0xE: /* Read data ready */
            dsp->irq_update(dsp->irq_priv, 0);
            dsp->sb_irq8 = dsp->sb_irq16 = 0;
            /* Only bit 7 is defined but aztech diagnostics fail if the others are set. Keep the original behavior to not interfere with what's already working. */
            if (IS_AZTECH(dsp)) {
                sb_dsp_log("SB Read Data Aztech read %02X, Read RP = %d, Read WP = %d\n", (dsp->sb_read_rp == dsp->sb_read_wp) ? 0x00 : 0x80, dsp->sb_read_rp, dsp->sb_read_wp);
                ret = (dsp->sb_read_rp == dsp->sb_read_wp) ? 0x00 : 0x80;
            } else {
                sb_dsp_log("SB Read Data Creative read %02X\n", (dsp->sb_read_rp == dsp->sb_read_wp) ? 0x7f : 0xff);
                ret = (dsp->sb_read_rp == dsp->sb_read_wp) ? 0x7f : 0xff;
            }
            break;
        case 0xF: /* 16-bit ack */
            dsp->sb_irq16 = 0;
            if (!dsp->sb_irq8)
                dsp->irq_update(dsp->irq_priv, 0);
            sb_dsp_log("SB 16-bit ACK read 0xFF\n");
            ret = 0xff;
            break;
    }

    return ret;
}

void
sb_dsp_input_msg(void *p, uint8_t *msg, uint32_t len)
{
    sb_dsp_t *dsp = (sb_dsp_t *) p;
    uint8_t   i   = 0;

    sb_dsp_log("MIDI in sysex = %d, uart irq = %d, msg = %d\n", dsp->midi_in_sysex, dsp->uart_irq, len);

    if (!dsp->uart_irq && !dsp->midi_in_poll && (dsp->mpu != NULL)) {
        MPU401_InputMsg(dsp->mpu, msg, len);
        return;
    }

    if (dsp->midi_in_sysex)
        return;

    if (dsp->uart_irq) {
        for (i = 0; i < len; i++)
            sb_add_data(dsp, msg[i]);
        sb_irq(dsp, 1);
    } else if (dsp->midi_in_poll) {
        for (i = 0; i < len; i++)
            sb_add_data(dsp, msg[i]);
    }
}

int
sb_dsp_input_sysex(void *p, uint8_t *buffer, uint32_t len, int abort)
{
    sb_dsp_t *dsp = (sb_dsp_t *) p;
    uint32_t  i;

    if (!dsp->uart_irq && !dsp->midi_in_poll && (dsp->mpu != NULL))
        return MPU401_InputSysex(dsp->mpu, buffer, len, abort);

    if (abort) {
        dsp->midi_in_sysex = 0;
        return 0;
    }

    dsp->midi_in_sysex = 1;

    for (i = 0; i < len; i++) {
        if (dsp->sb_read_rp == dsp->sb_read_wp) {
            sb_dsp_log("Length sysex SB = %d\n", len - i);
            return (len - i);
        }

        sb_add_data(dsp, buffer[i]);
    }

    dsp->midi_in_sysex = 0;

    return 0;
}

void
sb_dsp_init(sb_dsp_t *dsp, int type, int subtype, void *parent)
{
    dsp->sb_type    = type;
    dsp->sb_subtype = subtype;
    dsp->parent     = parent;

    /* Default values. Use sb_dsp_setxxx() methods to change. */
    dsp->sb_irqnum    = 7;
    dsp->sb_8_dmanum  = 1;
    dsp->sb_16_dmanum = 5;
    dsp->mpu          = NULL;

    dsp->sbleftright_default = 0;

    dsp->irq_update = sb_irq_update_pic;
    dsp->irq_priv   = dsp;
    dsp->dma_readb  = sb_8_read_dma;
    dsp->dma_readw  = sb_16_read_dma;
    dsp->dma_writeb = sb_8_write_dma;
    dsp->dma_writew = sb_16_write_dma;
    dsp->dma_priv   = dsp;

    sb_doreset(dsp);

    timer_add(&dsp->output_timer, pollsb, dsp, 0);
    timer_add(&dsp->input_timer, sb_poll_i, dsp, 0);
    timer_add(&dsp->wb_timer, NULL, dsp, 0);

    /* Initialise SB16 filter to same cutoff as 8-bit SBs (3.2 kHz). This will be recalculated when
       a set frequency command is sent. */
    recalc_sb16_filter(0, 3200 * 2);
    recalc_sb16_filter(1, 44100);

    /* Initialize SB16 8051 RAM and ASP internal RAM */
    memset(dsp->sb_8051_ram, 0x00, sizeof(dsp->sb_8051_ram));
    dsp->sb_8051_ram[0x0e] = 0xff;
    dsp->sb_8051_ram[0x0f] = 0x07;
    dsp->sb_8051_ram[0x37] = 0x38;

    memset(dsp->sb_asp_ram, 0xff, sizeof(dsp->sb_asp_ram));
}

void
sb_dsp_setaddr(sb_dsp_t *dsp, uint16_t addr)
{
    sb_dsp_log("sb_dsp_setaddr : %04X\n", addr);
    if (dsp->sb_addr != 0) {
        io_removehandler(dsp->sb_addr + 6, 0x0002, sb_read, NULL, NULL, sb_write, NULL, NULL, dsp);
        io_removehandler(dsp->sb_addr + 0xa, 0x0006, sb_read, NULL, NULL, sb_write, NULL, NULL, dsp);
    }
    dsp->sb_addr = addr;
    if (dsp->sb_addr != 0) {
        io_sethandler(dsp->sb_addr + 6, 0x0002, sb_read, NULL, NULL, sb_write, NULL, NULL, dsp);
        io_sethandler(dsp->sb_addr + 0xa, 0x0006, sb_read, NULL, NULL, sb_write, NULL, NULL, dsp);
    }
}

void
sb_dsp_set_stereo(sb_dsp_t *dsp, int stereo)
{
    dsp->stereo = stereo;
}

void
sb_dsp_irq_attach(sb_dsp_t *dsp, void (*irq_update)(void *priv, int set), void *priv)
{
    dsp->irq_update = irq_update;
    dsp->irq_priv   = priv;
}

void
sb_dsp_dma_attach(sb_dsp_t *dsp,
                  int (*dma_readb)(void *priv),
                  int (*dma_readw)(void *priv),
                  int (*dma_writeb)(void *priv, uint8_t val),
                  int (*dma_writew)(void *priv, uint16_t val),
                  void *priv)
{
    dsp->dma_readb  = dma_readb;
    dsp->dma_readw  = dma_readw;
    dsp->dma_writeb = dma_writeb;
    dsp->dma_writew = dma_writew;
    dsp->dma_priv   = priv;
}

void
pollsb(void *p)
{
    sb_dsp_t *dsp = (sb_dsp_t *) p;
    int       tempi, ref;
    int       data[2];

    timer_advance_u64(&dsp->output_timer, dsp->sblatcho);
    if (dsp->sb_8_enable && !dsp->sb_8_pause && dsp->sb_pausetime < 0 && dsp->sb_8_output) {
        sb_dsp_update(dsp);

        switch (dsp->sb_8_format) {
            case 0x00: /* Mono unsigned */
                data[0] = dsp->dma_readb(dsp->dma_priv);
                /* Needed to prevent clicking in Worms, which programs the DSP to
                   auto-init DMA but programs the DMA controller to single cycle */
                if (data[0] == DMA_NODATA)
                    break;
                dsp->sbdat = (data[0] ^ 0x80) << 8;
                if (dsp->stereo) {
                    sb_dsp_log("pollsb: Mono unsigned, dsp->stereo, %s channel, %04X\n",
                               dsp->sbleftright ? "left" : "right", dsp->sbdat);
                    if (dsp->sbleftright)
                        dsp->sbdatl = dsp->sbdat;
                    else
                        dsp->sbdatr = dsp->sbdat;
                    dsp->sbleftright = !dsp->sbleftright;
                } else
                    dsp->sbdatl = dsp->sbdatr = dsp->sbdat;
                dsp->sb_8_length--;
                break;
            case 0x10: /* Mono signed */
                data[0] = dsp->dma_readb(dsp->dma_priv);
                if (data[0] == DMA_NODATA)
                    break;
                dsp->sbdat = data[0] << 8;
                if (dsp->stereo) {
                    sb_dsp_log("pollsb: Mono signed, dsp->stereo, %s channel, %04X\n",
                               dsp->sbleftright ? "left" : "right", data[0], dsp->sbdat);
                    if (dsp->sbleftright)
                        dsp->sbdatl = dsp->sbdat;
                    else
                        dsp->sbdatr = dsp->sbdat;
                    dsp->sbleftright = !dsp->sbleftright;
                } else
                    dsp->sbdatl = dsp->sbdatr = dsp->sbdat;
                dsp->sb_8_length--;
                break;
            case 0x20: /* Stereo unsigned */
                data[0] = dsp->dma_readb(dsp->dma_priv);
                data[1] = dsp->dma_readb(dsp->dma_priv);
                if ((data[0] == DMA_NODATA) || (data[1] == DMA_NODATA))
                    break;
                dsp->sbdatl = (data[0] ^ 0x80) << 8;
                dsp->sbdatr = (data[1] ^ 0x80) << 8;
                dsp->sb_8_length -= 2;
                break;
            case 0x30: /* Stereo signed */
                data[0] = dsp->dma_readb(dsp->dma_priv);
                data[1] = dsp->dma_readb(dsp->dma_priv);
                if ((data[0] == DMA_NODATA) || (data[1] == DMA_NODATA))
                    break;
                dsp->sbdatl = data[0] << 8;
                dsp->sbdatr = data[1] << 8;
                dsp->sb_8_length -= 2;
                break;

            case ADPCM_4:
                if (dsp->sbdacpos)
                    tempi = (dsp->sbdat2 & 0xF) + dsp->sbstep;
                else
                    tempi = (dsp->sbdat2 >> 4) + dsp->sbstep;
                if (tempi < 0)
                    tempi = 0;
                if (tempi > 63)
                    tempi = 63;

                ref = dsp->sbref + scaleMap4[tempi];
                if (ref > 0xff)
                    dsp->sbref = 0xff;
                else if (ref < 0x00)
                    dsp->sbref = 0x00;
                else
                    dsp->sbref = ref;

                dsp->sbstep = (dsp->sbstep + adjustMap4[tempi]) & 0xff;
                dsp->sbdat  = (dsp->sbref ^ 0x80) << 8;

                dsp->sbdacpos++;

                if (dsp->sbdacpos >= 2) {
                    dsp->sbdacpos = 0;
                    dsp->sbdat2   = dsp->dma_readb(dsp->dma_priv);
                    dsp->sb_8_length--;
                }

                if (dsp->stereo) {
                    sb_dsp_log("pollsb: ADPCM 4, dsp->stereo, %s channel, %04X\n",
                               dsp->sbleftright ? "left" : "right", dsp->sbdat);
                    if (dsp->sbleftright)
                        dsp->sbdatl = dsp->sbdat;
                    else
                        dsp->sbdatr = dsp->sbdat;
                    dsp->sbleftright = !dsp->sbleftright;
                } else
                    dsp->sbdatl = dsp->sbdatr = dsp->sbdat;
                break;

            case ADPCM_26:
                if (!dsp->sbdacpos)
                    tempi = (dsp->sbdat2 >> 5) + dsp->sbstep;
                else if (dsp->sbdacpos == 1)
                    tempi = ((dsp->sbdat2 >> 2) & 7) + dsp->sbstep;
                else
                    tempi = ((dsp->sbdat2 << 1) & 7) + dsp->sbstep;

                if (tempi < 0)
                    tempi = 0;
                if (tempi > 39)
                    tempi = 39;

                ref = dsp->sbref + scaleMap26[tempi];
                if (ref > 0xff)
                    dsp->sbref = 0xff;
                else if (ref < 0x00)
                    dsp->sbref = 0x00;
                else
                    dsp->sbref = ref;
                dsp->sbstep = (dsp->sbstep + adjustMap26[tempi]) & 0xff;

                dsp->sbdat = (dsp->sbref ^ 0x80) << 8;

                dsp->sbdacpos++;
                if (dsp->sbdacpos >= 3) {
                    dsp->sbdacpos = 0;
                    dsp->sbdat2   = dsp->dma_readb(dsp->dma_priv);
                    dsp->sb_8_length--;
                }

                if (dsp->stereo) {
                    sb_dsp_log("pollsb: ADPCM 26, dsp->stereo, %s channel, %04X\n",
                               dsp->sbleftright ? "left" : "right", dsp->sbdat);
                    if (dsp->sbleftright)
                        dsp->sbdatl = dsp->sbdat;
                    else
                        dsp->sbdatr = dsp->sbdat;
                    dsp->sbleftright = !dsp->sbleftright;
                } else
                    dsp->sbdatl = dsp->sbdatr = dsp->sbdat;
                break;

            case ADPCM_2:
                tempi = ((dsp->sbdat2 >> ((3 - dsp->sbdacpos) * 2)) & 3) + dsp->sbstep;
                if (tempi < 0)
                    tempi = 0;
                if (tempi > 23)
                    tempi = 23;

                ref = dsp->sbref + scaleMap2[tempi];
                if (ref > 0xff)
                    dsp->sbref = 0xff;
                else if (ref < 0x00)
                    dsp->sbref = 0x00;
                else
                    dsp->sbref = ref;
                dsp->sbstep = (dsp->sbstep + adjustMap2[tempi]) & 0xff;

                dsp->sbdat = (dsp->sbref ^ 0x80) << 8;

                dsp->sbdacpos++;
                if (dsp->sbdacpos >= 4) {
                    dsp->sbdacpos = 0;
                    dsp->sbdat2   = dsp->dma_readb(dsp->dma_priv);
                }

                if (dsp->stereo) {
                    sb_dsp_log("pollsb: ADPCM 2, dsp->stereo, %s channel, %04X\n",
                               dsp->sbleftright ? "left" : "right", dsp->sbdat);
                    if (dsp->sbleftright)
                        dsp->sbdatl = dsp->sbdat;
                    else
                        dsp->sbdatr = dsp->sbdat;
                    dsp->sbleftright = !dsp->sbleftright;
                } else
                    dsp->sbdatl = dsp->sbdatr = dsp->sbdat;
                break;
        }

        if (dsp->sb_8_length < 0) {
            if (dsp->sb_8_autoinit)
                dsp->sb_8_length = dsp->sb_8_origlength = dsp->sb_8_autolen;
            else {
                dsp->sb_8_enable = 0;
                timer_disable(&dsp->output_timer);
            }
            sb_irq(dsp, 1);
        }
    }
    if (dsp->sb_16_enable && !dsp->sb_16_pause && (dsp->sb_pausetime < 0LL) && dsp->sb_16_output) {
        sb_dsp_update(dsp);

        switch (dsp->sb_16_format) {
            case 0x00: /* Mono unsigned */
                data[0] = dsp->dma_readw(dsp->dma_priv);
                if (data[0] == DMA_NODATA)
                    break;
                dsp->sbdatl = dsp->sbdatr = data[0] ^ 0x8000;
                dsp->sb_16_length--;
                break;
            case 0x10: /* Mono signed */
                data[0] = dsp->dma_readw(dsp->dma_priv);
                if (data[0] == DMA_NODATA)
                    break;
                dsp->sbdatl = dsp->sbdatr = data[0];
                dsp->sb_16_length--;
                break;
            case 0x20: /* Stereo unsigned */
                data[0] = dsp->dma_readw(dsp->dma_priv);
                data[1] = dsp->dma_readw(dsp->dma_priv);
                if ((data[0] == DMA_NODATA) || (data[1] == DMA_NODATA))
                    break;
                dsp->sbdatl = data[0] ^ 0x8000;
                dsp->sbdatr = data[1] ^ 0x8000;
                dsp->sb_16_length -= 2;
                break;
            case 0x30: /* Stereo signed */
                data[0] = dsp->dma_readw(dsp->dma_priv);
                data[1] = dsp->dma_readw(dsp->dma_priv);
                if ((data[0] == DMA_NODATA) || (data[1] == DMA_NODATA))
                    break;
                dsp->sbdatl = data[0];
                dsp->sbdatr = data[1];
                dsp->sb_16_length -= 2;
                break;
        }

        if (dsp->sb_16_length < 0) {
            sb_dsp_log("16DMA over %i\n", dsp->sb_16_autoinit);
            if (dsp->sb_16_autoinit)
                dsp->sb_16_length = dsp->sb_16_origlength = dsp->sb_16_autolen;
            else {
                dsp->sb_16_enable = 0;
                timer_disable(&dsp->output_timer);
            }
            sb_irq(dsp, 0);
        }
    }
    if (dsp->sb_pausetime > -1) {
        dsp->sb_pausetime--;
        if (dsp->sb_pausetime < 0) {
            sb_irq(dsp, 1);
            if (!dsp->sb_8_enable)
                timer_disable(&dsp->output_timer);
            sb_dsp_log("SB pause over\n");
        }
    }
}

void
sb_poll_i(void *p)
{
    sb_dsp_t *dsp       = (sb_dsp_t *) p;
    int       processed = 0;

    timer_advance_u64(&dsp->input_timer, dsp->sblatchi);

    if (dsp->sb_8_enable && !dsp->sb_8_pause && dsp->sb_pausetime < 0 && !dsp->sb_8_output) {
        switch (dsp->sb_8_format) {
            case 0x00: /* Mono unsigned As the manual says, only the left channel is recorded */
                dsp->dma_writeb(dsp->dma_priv, (dsp->record_buffer[dsp->record_pos_read] >> 8) ^ 0x80);
                dsp->sb_8_length--;
                dsp->record_pos_read += 2;
                dsp->record_pos_read &= 0xFFFF;
                break;
            case 0x10: /* Mono signed As the manual says, only the left channel is recorded */
                dsp->dma_writeb(dsp->dma_priv, (dsp->record_buffer[dsp->record_pos_read] >> 8));
                dsp->sb_8_length--;
                dsp->record_pos_read += 2;
                dsp->record_pos_read &= 0xFFFF;
                break;
            case 0x20: /* Stereo unsigned */
                dsp->dma_writeb(dsp->dma_priv, (dsp->record_buffer[dsp->record_pos_read] >> 8) ^ 0x80);
                dsp->dma_writeb(dsp->dma_priv, (dsp->record_buffer[dsp->record_pos_read + 1] >> 8) ^ 0x80);
                dsp->sb_8_length -= 2;
                dsp->record_pos_read += 2;
                dsp->record_pos_read &= 0xFFFF;
                break;
            case 0x30: /* Stereo signed */
                dsp->dma_writeb(dsp->dma_priv, (dsp->record_buffer[dsp->record_pos_read] >> 8));
                dsp->dma_writeb(dsp->dma_priv, (dsp->record_buffer[dsp->record_pos_read + 1] >> 8));
                dsp->sb_8_length -= 2;
                dsp->record_pos_read += 2;
                dsp->record_pos_read &= 0xFFFF;
                break;
        }

        if (dsp->sb_8_length < 0) {
            if (dsp->sb_8_autoinit)
                dsp->sb_8_length = dsp->sb_8_origlength = dsp->sb_8_autolen;
            else {
                dsp->sb_8_enable = 0;
                timer_disable(&dsp->input_timer);
            }
            sb_irq(dsp, 1);
        }
        processed = 1;
    }
    if (dsp->sb_16_enable && !dsp->sb_16_pause && (dsp->sb_pausetime < 0LL) && !dsp->sb_16_output) {
        switch (dsp->sb_16_format) {
            case 0x00: /* Unsigned mono. As the manual says, only the left channel is recorded */
                if (dsp->dma_writew(dsp->dma_priv, dsp->record_buffer[dsp->record_pos_read] ^ 0x8000))
                    return;
                dsp->sb_16_length--;
                dsp->record_pos_read += 2;
                dsp->record_pos_read &= 0xFFFF;
                break;
            case 0x10: /* Signed mono. As the manual says, only the left channel is recorded */
                if (dsp->dma_writew(dsp->dma_priv, dsp->record_buffer[dsp->record_pos_read]))
                    return;
                dsp->sb_16_length--;
                dsp->record_pos_read += 2;
                dsp->record_pos_read &= 0xFFFF;
                break;
            case 0x20: /* Unsigned stereo */
                if (dsp->dma_writew(dsp->dma_priv, dsp->record_buffer[dsp->record_pos_read] ^ 0x8000))
                    return;
                dsp->dma_writew(dsp->dma_priv, dsp->record_buffer[dsp->record_pos_read + 1] ^ 0x8000);
                dsp->sb_16_length -= 2;
                dsp->record_pos_read += 2;
                dsp->record_pos_read &= 0xFFFF;
                break;
            case 0x30: /* Signed stereo */
                if (dsp->dma_writew(dsp->dma_priv, dsp->record_buffer[dsp->record_pos_read]))
                    return;
                dsp->dma_writew(dsp->dma_priv, dsp->record_buffer[dsp->record_pos_read + 1]);
                dsp->sb_16_length -= 2;
                dsp->record_pos_read += 2;
                dsp->record_pos_read &= 0xFFFF;
                break;
        }

        if (dsp->sb_16_length < 0) {
            if (dsp->sb_16_autoinit)
                dsp->sb_16_length = dsp->sb_16_origlength = dsp->sb_16_autolen;
            else {
                dsp->sb_16_enable = 0;
                timer_disable(&dsp->input_timer);
            }
            sb_irq(dsp, 0);
        }
        processed = 1;
    }
    /* Assume this is direct mode */
    if (!processed) {
        dsp->record_pos_read += 2;
        dsp->record_pos_read &= 0xFFFF;
    }
}

void
sb_dsp_update(sb_dsp_t *dsp)
{
    if (dsp->muted) {
        dsp->sbdatl = 0;
        dsp->sbdatr = 0;
    }
    for (; dsp->pos < sound_pos_global; dsp->pos++) {
        dsp->buffer[dsp->pos * 2]     = dsp->sbdatl;
        dsp->buffer[dsp->pos * 2 + 1] = dsp->sbdatr;
    }
}

void
sb_dsp_close(sb_dsp_t *dsp)
{
}
