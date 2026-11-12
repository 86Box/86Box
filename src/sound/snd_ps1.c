/*
 * Copyright 2026 Xeon3D.
 */

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
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/sound.h>
#include <86box/snd_sn76489.h>
#include <86box/timer.h>
#include <86box/plat_unused.h>

#define PS1SND_IN_RING 16384 /* host input frames held for the ADC */

/* the ADC's up/down counter is clocked at 2.5 MHz, one step (of
   256 over +/- 380 mV) a clock: it follows the input at 2.5 steps a
   microsecond at most (7.42 mV/us, the Technical Reference's slew limit). */
#define PS1SND_ADC_STEPS_PER_US 2.5

/* the host's input is closed after this many 20 ms input blocks
   without a read of the ADC data port. */
#define PS1SND_IN_IDLE_BLOCKS 50

typedef struct ps1snd_t {
    sn76489_t  sn76489;
    uint8_t    status;
    uint8_t    ctrl;
    uint64_t   timer_latch;
    pc_timer_t timer_count;
    int        timer_enable;
    uint8_t    fifo[2048];
    int        fifo_read_idx;
    int        fifo_write_idx;
    int        fifo_threshold;
    uint8_t    dac_val;
    int16_t    buffer[SOUNDBUFLEN];
    int        pos;

    /* the ADC. The host's input frames (mono, at in_rate) wait in
       in_ring; at each FIFO timer overflow the up/down counter is walked
       through the frames of the time that passed, then latched. */
    int16_t    in_ring[PS1SND_IN_RING];
    uint32_t   in_wr;
    uint32_t   in_rd;
    double     in_frac;
    int        in_rate;
    int        in_started;
    int        in_primed;
    int        in_idle;
    double     in_last;
    double     adc_counter;
    uint8_t    adc_latch;
} ps1snd_t;

/* ADC Data Ready (status bit 4) interrupts "at all times" (the
   Technical Reference; control bit 4 is a reserved write bit), Almost Empty
   (bit 1) only with control bit 1; either reaches IRQ 7 through the external
   interrupt enable (control bit 0). IBM's Windows driver records that way:
   control 01h, 200h read in its interrupt handler. */
static void
ps1snd_update_irq_status(ps1snd_t *snd)
{
    if (((snd->status & 0x10) || (snd->status & snd->ctrl & 0x02)) && (snd->ctrl & 0x01))
        picint(1 << 7);
    else
        picintc(1 << 7);
}

/* the host's input is opened only while the ADC is being read. */
static void
ps1snd_adc_input(ps1snd_t *ps1snd, int on)
{
    if (on && !ps1snd->in_started) {
        ps1snd->in_wr = ps1snd->in_rd = 0;
        ps1snd->in_frac               = 0.0;
        ps1snd->in_primed             = 0;
        ps1snd->in_idle               = 0;
        ps1snd->in_last               = 0.0;
        sound_in_start_input();
        ps1snd->in_started = 1;
    } else if (!on && ps1snd->in_started) {
        sound_in_stop_input();
        ps1snd->in_started = 0;
    }
}

/* the next host input frame in ADC steps (full scale is the
   converter's +/- 380 mV, -128 to 127); silence while none has come. The
   input comes in 20 ms blocks: 40 ms in hand keep the ADC from running dry
   at the end of each. */
static double
ps1snd_adc_input_frame(ps1snd_t *ps1snd)
{
    const uint32_t avail = ps1snd->in_wr - ps1snd->in_rd;

    if (!ps1snd->in_primed && (avail >= (uint32_t) (ps1snd->in_rate / 25)))
        ps1snd->in_primed = 1;
    if (!ps1snd->in_primed || (avail < 1)) {
        ps1snd->in_primed = 0;
        return 0.0;
    }

    return ps1snd->in_ring[ps1snd->in_rd++ % PS1SND_IN_RING] / 256.0;
}

/* moves the up/down counter towards target by at most max_step. */
static void
ps1snd_adc_track(ps1snd_t *ps1snd, double target, double max_step)
{
    if (target > (ps1snd->adc_counter + max_step))
        ps1snd->adc_counter += max_step;
    else if (target < (ps1snd->adc_counter - max_step))
        ps1snd->adc_counter -= max_step;
    else
        ps1snd->adc_counter = target;
}

/* the FIFO timer overflowed period_us after the last time: the
   up/down counter follows the input over that time, slew-limited, and is
   latched. */
static void
ps1snd_adc_convert(ps1snd_t *ps1snd, double period_us)
{
    if (ps1snd->in_started && (ps1snd->in_rate > 0)) {
        const double frame_us = 1000000.0 / (double) ps1snd->in_rate;
        const double step     = PS1SND_ADC_STEPS_PER_US * frame_us;
        double       next;

        ps1snd->in_frac += period_us / frame_us;
        while (ps1snd->in_frac >= 1.0) {
            ps1snd->in_frac -= 1.0;
            ps1snd->in_last = ps1snd_adc_input_frame(ps1snd);
            ps1snd_adc_track(ps1snd, ps1snd->in_last, step);
        }

        /* Part way to the next frame: towards the input between the two,
           so the latch instant is not rounded to the host's frame clock. */
        next = (ps1snd->in_primed && (ps1snd->in_wr != ps1snd->in_rd)) ?
               (ps1snd->in_ring[ps1snd->in_rd % PS1SND_IN_RING] / 256.0) : ps1snd->in_last;
        ps1snd_adc_track(ps1snd, ps1snd->in_last + ((next - ps1snd->in_last) * ps1snd->in_frac),
                         step * ps1snd->in_frac);
    } else
        ps1snd_adc_track(ps1snd, 0.0, PS1SND_ADC_STEPS_PER_US * period_us);

    if (ps1snd->adc_counter > 127.0)
        ps1snd->adc_counter = 127.0;
    else if (ps1snd->adc_counter < -128.0)
        ps1snd->adc_counter = -128.0;

    /* Offset binary, 80h at 0 V, as the DAC takes it. */
    ps1snd->adc_latch = (uint8_t) (((int) floor(ps1snd->adc_counter + 128.5)) & 0xff);
}

/* the host's input, as sound.c hands it over (interleaved stereo at
   the capture rate), kept mono and within a quarter second of the ADC. */
static void
ps1snd_put_buffer(int16_t *buffer, int len, void *priv)
{
    ps1snd_t *ps1snd   = (ps1snd_t *) priv;
    const int cap_rate = al_capture_get_rate();

    if (!ps1snd->in_started)
        return;

    /* Nothing read the ADC for a second: give the host's input back. */
    if (++ps1snd->in_idle > PS1SND_IN_IDLE_BLOCKS) {
        ps1snd_adc_input(ps1snd, 0);
        return;
    }

    ps1snd->in_rate = (cap_rate > 0) ? cap_rate : SOUND_FREQ;

    for (int c = 0; c < len; c++)
        ps1snd->in_ring[ps1snd->in_wr++ % PS1SND_IN_RING] = (int16_t) ((buffer[c * 2] + buffer[c * 2 + 1]) / 2);

    uint32_t keep = (uint32_t) (ps1snd->in_rate / 4);
    if (keep > (PS1SND_IN_RING / 2))
        keep = PS1SND_IN_RING / 2;
    if ((ps1snd->in_wr - ps1snd->in_rd) > keep)
        ps1snd->in_rd = ps1snd->in_wr - (keep / 2);
}

static uint8_t
ps1snd_read(uint16_t port, void *priv)
{
    ps1snd_t *ps1snd = (ps1snd_t *) priv;
    uint8_t   ret    = 0xff;

    switch (port & 7) {
        case 0: /* ADC data */
            /* the counter's value latched at the last FIFO timer
               overflow. Reading it opens the host's input, until a second
               passes without a read. */
            ps1snd_adc_input(ps1snd, 1);
            ps1snd->in_idle = 0;
            ps1snd->status &= ~0x10;
            ps1snd_update_irq_status(ps1snd);
            ret = ps1snd->adc_latch;
            break;

        case 2: /* status */
            ret = ps1snd->status;
            ret |= (ps1snd->ctrl & 0x01);
            if ((ps1snd->fifo_write_idx - ps1snd->fifo_read_idx) >= 2048)
                ret |= 0x08; /* FIFO full */
            if (ps1snd->fifo_read_idx == ps1snd->fifo_write_idx)
                ret |= 0x04; /* FIFO empty */
            break;

        case 3: /* FIFO timer */
            /*
             * The PS/1 Technical Reference says this should return
             * thecurrent value, but the PS/1 BIOS and Stunt Island
             * expect it not to change.
             */
            ret = ps1snd->timer_latch;
            break;

        case 4:
        case 5:
        case 6:
        case 7:
            ret = 0;
            break;

        default:
            break;
    }

    return ret;
}

static void
ps1snd_write(uint16_t port, uint8_t val, void *priv)
{
    ps1snd_t *ps1snd = (ps1snd_t *) priv;

    switch (port & 7) {
        case 0: /* DAC output */
            if ((ps1snd->fifo_write_idx - ps1snd->fifo_read_idx) < 2048) {
                ps1snd->fifo[ps1snd->fifo_write_idx & 2047] = val;
                ps1snd->fifo_write_idx++;
            }
            break;

        case 2: /* control */
            ps1snd->ctrl = val;
            if (!(val & 0x02))
                ps1snd->status &= ~0x02;
            ps1snd_update_irq_status(ps1snd);
            break;

        case 3: /* timer reload value */
            ps1snd->timer_latch = val;
            /* the 1 MHz timer overflows every reload + 1 us (the
               Technical Reference: "one cycle longer than the value
               written", 256 us at most); 0 stops it "and holds it in the 0
               state". A write to the running timer changes only its reload
               value: the count goes on, so rewriting the same value does not
               put the next overflow off. IBM's Windows 3.1 driver (PS1.DLL)
               rewrites it from its multimedia-timer callback more often than
               it overflows while it plays: restarted at each write, the timer
               never ran out and the FIFO stayed full (2043 bytes) for as long
               as the driver kept writing. A lower value takes effect at once
               when the count is already past it.
               A write of 0 also empties the FIFO (its read address is set to
               the write address); bytes written after it stay until the timer
               runs again. Not in the Technical Reference, but both of IBM's
               uses need it: PS1.DLL pauses a stream by writing 0 with up to
               2 KB still queued and its close then waits for FIFO Empty
               (status bit 2) before anything restarts the timer -- with the
               bytes kept, Windows hung at Exit Windows after the exit sound --
               while the PS/1 POST writes 0, then 32 bytes, and reports an
               error when the FIFO reads empty before it starts the timer. */
            if (!val) {
                timer_disable(&ps1snd->timer_count);
                ps1snd->fifo_read_idx = ps1snd->fifo_write_idx;
            } else if (!timer_is_enabled(&ps1snd->timer_count) ||
                     (timer_get_remaining_u64(&ps1snd->timer_count) > ((uint64_t) (val + 1) * TIMER_USEC)))
                timer_set_delay_u64(&ps1snd->timer_count, ((uint64_t) (val + 1) * TIMER_USEC));
            break;

        case 4: /* almost empty */
            ps1snd->fifo_threshold = val * 4;
            break;

        default:
            break;
    }
}

static void
ps1snd_update(ps1snd_t *ps1snd)
{
    for (; ps1snd->pos < sound_pos_global; ps1snd->pos++)
        ps1snd->buffer[ps1snd->pos] = (int8_t) (ps1snd->dac_val ^ 0x80) * 0x20;
}

static void
ps1snd_callback(void *priv)
{
    ps1snd_t *ps1snd = (ps1snd_t *) priv;

    ps1snd_update(ps1snd);

    if (ps1snd->fifo_read_idx != ps1snd->fifo_write_idx) {
        ps1snd->dac_val = ps1snd->fifo[ps1snd->fifo_read_idx & 2047];
        ps1snd->fifo_read_idx++;
    }

    if ((ps1snd->fifo_write_idx - ps1snd->fifo_read_idx) == ps1snd->fifo_threshold)
        ps1snd->status |= 0x02; /*FIFO almost empty*/

    /* the ADC counter is latched on the same overflow. */
    ps1snd_adc_convert(ps1snd, (double) (ps1snd->timer_latch + 1));
    ps1snd->status |= 0x10; /*ADC data ready*/
    ps1snd_update_irq_status(ps1snd);

    /* reload + 1 us (see the reload register's write). */
    timer_advance_u64(&ps1snd->timer_count, (uint64_t) (ps1snd->timer_latch + 1) * TIMER_USEC);
}

static void
ps1snd_get_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    ps1snd_t *ps1snd = (ps1snd_t *) priv;

    ps1snd_update(ps1snd);

    for (uint16_t c = 0; c < len * 2; c++)
        buffer[c] += ps1snd->buffer[c >> 1];

    ps1snd->pos = 0;
}

static void *
ps1snd_init(UNUSED(const device_t *info))
{
    ps1snd_t *ps1snd = calloc(1, sizeof(ps1snd_t));

    sn76489_init(&ps1snd->sn76489, 0x0205, 0x0001, SN76496, 4000000);

    io_sethandler(0x0200, 1,
                  ps1snd_read, NULL, NULL,
                  ps1snd_write, NULL, NULL,
                  ps1snd);
    io_sethandler(0x0202, 6,
                  ps1snd_read, NULL, NULL,
                  ps1snd_write, NULL, NULL,
                  ps1snd);

    timer_add(&ps1snd->timer_count, ps1snd_callback, ps1snd, 0);

    sound_add_handler(ps1snd_get_buffer, ps1snd);
    sound_in_add_handler(ps1snd_put_buffer, ps1snd); /* the ADC */

    ps1snd->adc_latch = 0x80;

    device_add(&ps1midi_device);

    return ps1snd;
}

static void
ps1snd_close(void *priv)
{
    ps1snd_t *ps1snd = (ps1snd_t *) priv;

    ps1snd_adc_input(ps1snd, 0);

    free(ps1snd);
}

const device_t ps1snd_device = {
    .name          = "IBM PS/1 Audio Card",
    .internal_name = "ps1snd",
    .flags         = DEVICE_AUDIO_IN, /* the ADC */
    .local         = 0,
    .init          = ps1snd_init,
    .close         = ps1snd_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
