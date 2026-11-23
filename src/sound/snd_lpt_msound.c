/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the MSound Stereo (MSound) parallel-port DSP
 *          sound device, based on reverse-engineering of Access Software's
 *          COPYRIGHT MSOUND INT.1990 driver (Countdown, Links, Martian
 *          Memorandum).
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2026 Jasmine Iwanek.
 */
#ifdef ENABLE_MSOUND_LOG
#include <stdarg.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/filters.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/sound.h>
#include <86box/plat_unused.h>
#include <86box/fifo.h>
#include <86box/fifo8.h>
#include <86box/log.h>

#ifdef ENABLE_MSOUND_LOG
uint8_t msound_do_log = ENABLE_MSOUND_LOG;

static void
msound_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (msound_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define msound_log(priv, fmt, ...)
#endif

enum {
    MSOUND_STATE_IDLE = 0,
    MSOUND_STATE_UPLOAD,
    MSOUND_STATE_READY,
    MSOUND_STATE_RATE_PARAM,
    MSOUND_STATE_FLUSH_PARAM,
    MSOUND_STATE_PLAYING,
    MSOUND_STATE_DRAINING
};

typedef struct msound_s {
    void *lpt;

    Fifo8 fifo;

    uint8_t control;
    uint8_t prev_data;
    uint8_t status;

    /* Bit-banged serial control channel (LPT_BASE bit 0 = data, bit 1 = clk) */
    uint8_t serial_shift;
    uint8_t serial_bits;
    uint8_t last_cmd;
    uint8_t last_cmd_param;
    uint8_t serial_rate_div;
    uint8_t detect_step;
    uint8_t in_detect;

    /* 0xFF-prefixed packet / sample stream state */
    uint8_t ff_prefix;
    uint8_t state;
    uint8_t rate_param;
    uint8_t frame_rem;
    uint8_t irq_requested;

    /* Audio output state (8-bit unsigned PCM, 0x80 = silence) */
    uint8_t  dac_val_l;
    uint8_t  dac_val_r;
    uint32_t sample_hz;

    pc_timer_t sample_timer;

    int16_t  buffer[2][SOUNDBUFLEN];
    uint16_t pos;

    void *log;
} msound_t;

static void
msound_update(msound_t *const msound)
{
    const int16_t sample_l = (int16_t) ((int8_t) (msound->dac_val_l ^ 0x80) * 0x40);
    const int16_t sample_r = (int16_t) ((int8_t) (msound->dac_val_r ^ 0x80) * 0x40);

    for (; msound->pos < sound_pos_global; msound->pos++) {
        msound->buffer[0][msound->pos] = sample_l;
        msound->buffer[1][msound->pos] = sample_r;
    }
}

static void
msound_arm_timer(msound_t *const msound)
{
    const uint32_t hz            = msound->sample_hz ? msound->sample_hz : 11025U;
    const uint64_t sample_period = (TIMER_USEC * 1000000ULL) / hz;

    timer_set_delay_u64(&msound->sample_timer, sample_period);
}

static uint32_t
msound_rate_from_param(const uint8_t val)
{
    /* Calibrated against TITLE.EXE's parallel Sound Blaster time-constant table:
       SB {60, 120, 181, 241} us vs MSound {0x0e, 0x20, 0x32, 0x46, 0x58}
       -> period_us ≈ 15.0 + 3.24 * val (16567, 8426, 5650, 4136, 3332 Hz). */
    uint32_t hz = val ? (100000000U / (1500U + ((uint32_t) val * 324U))) : 8333U;

    if (hz < 3000U)
        hz = 3000U;
    else if (hz > 48000U)
        hz = 48000U;

    return hz;
}

static void
msound_handle_serial_byte(msound_t *const msound, const uint8_t byte)
{
    msound_log(msound->log, "serial_byte: 0x%02x (prev=0x%02x, state=%u)\n",
               byte, msound->last_cmd, msound->state);

    /* Post-upload and per-sound rate config: 0x01, 0x03, <rate_div>, 0x04, 0x01, 0x13 */
    if (msound->last_cmd_param == 0x01 && msound->last_cmd == 0x03) {
        msound->serial_rate_div = byte;
        msound_log(msound->log, "serial rate_div: 0x%02x (%u Hz)\n",
                   byte, msound_rate_from_param(byte));
    }

    /* Hardware detection:
       1. Driver sends BH=0x00, 0x0D, then writes BL=0x01, followed by
          serial bytes 0x01, 0x03, 0x00, 0x01.
       2. Then it loops di=0..47, sending (BH, 0x0D) + BL, and expects
          ((~status) >> 5) & 0x02 == (di & 2). */
    if (byte == 0x0D) {
        if (msound->last_cmd == 0x00 && !msound->in_detect) {
            msound->detect_step = 0;
            msound->status |= 0x40;
        } else {
            msound->in_detect = 1;
            if (msound->detect_step & 0x02)
                msound->status &= ~0x40;
            else
                msound->status |= 0x40;
            msound->detect_step++;
        }
    } else if (byte == 0x15) {
        /* End of DSP register config (0x10406): enter firmware/table upload */
        msound->in_detect = 0;
        msound->state     = MSOUND_STATE_UPLOAD;
        msound_log(msound->log, "entering UPLOAD state\n");
    } else if (byte == 0x13) {
        /* End of post-upload or mid-game rate setup (0x102cc / TITLE 0x14f5) */
        if (msound->state == MSOUND_STATE_UPLOAD) {
            msound->state = MSOUND_STATE_READY;
            msound_log(msound->log, "upload complete, entering READY state\n");
        }
        msound->sample_hz = msound_rate_from_param(msound->serial_rate_div);
    }

    msound->last_cmd_param = msound->last_cmd;
    msound->last_cmd       = byte;
}

static void
msound_handle_ff_packet(msound_t *const msound, const uint8_t val)
{
    /* Ignore playback command opcodes while streaming the 1,268-byte upload table */
    if (msound->state == MSOUND_STATE_UPLOAD)
        return;

    if (msound->frame_rem > 0) {
        /* 0xFF-prefixed sample byte inside a 32-byte (0x20) IRQ7 frame */
        if (!fifo8_is_full(&msound->fifo))
            fifo8_push(&msound->fifo, val);
        msound->frame_rem--;
        return;
    }

    msound_log(msound->log, "ff_pkt: val=0x%02x state=%u\n", val, msound->state);

    if (msound->state == MSOUND_STATE_RATE_PARAM) {
        const uint8_t effective_div = msound->serial_rate_div ? msound->serial_rate_div : val;
        msound->rate_param          = val;
        msound->sample_hz           = msound_rate_from_param(effective_div);
        msound->state               = MSOUND_STATE_PLAYING;
        msound->irq_requested       = 1;
        msound_log(msound->log, "playback start: pkt_param=0x%02x serial_div=0x%02x sample_hz=%u\n",
                   val, msound->serial_rate_div, msound->sample_hz);
        msound_arm_timer(msound);
        lpt_irq(msound->lpt, 1);
        return;
    }

    if (msound->state == MSOUND_STATE_FLUSH_PARAM) {
        msound->state = MSOUND_STATE_READY;
        if (val == 0x00) {
            /* Consumes trailing 0x00 after 0xFF, 0x11 (sound-start buffer flush) */
            msound_log(msound->log, "cmd 0x11: flush complete (param=0x00)\n");
            return;
        }
        /* If an IRQ7 frame (0x10) interrupted 0x11 before 0x00, fall through to handle it */
    }

    switch (val) {
        case 0x04:
            /* Set rate parameter (next 0xFF-prefixed byte is 0x7F) */
            msound->state = MSOUND_STATE_RATE_PARAM;
            msound_log(msound->log, "cmd 0x04: awaiting rate_param\n");
            break;

        case 0x10:
            /* Begin 32-byte (0x20) sample frame from IRQ7 ISR */
            msound->frame_rem     = 0x20;
            msound->irq_requested = 0;
            if (msound->state != MSOUND_STATE_PLAYING) {
                msound->state = MSOUND_STATE_PLAYING;
                msound_arm_timer(msound);
                msound_log(msound->log, "cmd 0x10: playback started (%u Hz)\n", msound->sample_hz);
            }
            break;

        case 0x11:
            /* Flush/reset sample buffer at sound start (COUNT.EXE 0x14cf: 0xFF, 0x11, 0xFF, 0x00) */
            msound_update(msound);
            msound->state         = MSOUND_STATE_FLUSH_PARAM;
            msound->frame_rem     = 0;
            msound->irq_requested = 0;
            msound->dac_val_l     = 0x80;
            msound->dac_val_r     = 0x80;
            timer_disable(&msound->sample_timer);
            fifo8_reset(&msound->fifo);
            msound_log(msound->log, "cmd 0x11: flushing FIFO\n");
            break;

        case 0x12:
            /* Stop playback command (followed by 0xFF, 0x00): drain any remaining
               buffered samples (including trailing 0x80 padding) before idling. */
            msound->frame_rem     = 0;
            msound->irq_requested = 0;
            if (!fifo8_is_empty(&msound->fifo)) {
                msound->state = MSOUND_STATE_DRAINING;
                msound_arm_timer(msound);
            } else {
                msound_update(msound);
                msound->state     = MSOUND_STATE_READY;
                msound->dac_val_l = 0x80;
                msound->dac_val_r = 0x80;
                timer_disable(&msound->sample_timer);
            }
            msound_log(msound->log, "cmd 0x12: playback stopped\n");
            break;

        default:
            break;
    }
}

static void
msound_write_data(const uint8_t val, void *priv)
{
    msound_t *const msound = (msound_t *) priv;
    const uint8_t   prev   = msound->prev_data;

    msound->prev_data = val;

    /* 0xFF-prefixed packet / sample stream */
    if (msound->ff_prefix) {
        /* Outside a 32-byte sample frame, a repeated 0xFF means an IRQ7 (0xFF, 0x10)
           interrupted an unguarded OUT 0xFF; stay in ff_prefix so 0x10 is not lost. */
        if (val == 0xFF && msound->frame_rem == 0)
            return;
        msound->ff_prefix = 0;
        msound_handle_ff_packet(msound, val);
        return;
    }

    if (val == 0xFF) {
        msound->ff_prefix = 1;
        return;
    }

    /* Bit-banged 11-clock serial frame (rising edge on bit 1 clocks bit 0):
       preamble 0, 1 -> 8 data bits (LSB-first) -> postamble 0 */
    if (!(prev & 0x02) && (val & 0x02)) {
        const uint8_t bit = val & 0x01;

        if (msound->serial_bits == 0) {
            if (bit == 0)
                msound->serial_bits = 1;
        } else if (msound->serial_bits == 1) {
            if (bit == 1) {
                msound->serial_shift = 0;
                msound->serial_bits  = 2;
            }
            /* if bit == 0, remain at serial_bits == 1 to stay synced on preamble */
        } else if (msound->serial_bits < 10) {
            msound->serial_shift = (uint8_t) ((msound->serial_shift >> 1) | (bit << 7));
            msound->serial_bits++;
        } else {
            /* Clock 10: postamble bit (0) */
            const uint8_t cmd_byte = msound->serial_shift;
            msound->serial_shift   = 0;
            msound->serial_bits    = 0;
            msound_handle_serial_byte(msound, cmd_byte);
        }
    }
}

static void
msound_write_ctrl(const uint8_t val, void *priv)
{
    msound_t *const msound = (msound_t *) priv;

    msound_log(msound->log, "write_ctrl: 0x%02x\n", val);

    /* LPT /INIT low pulse (INT 17h AH=01h) resynchronizes serial, detection & packet framing;
       MM.EXE also writes 0x1C directly to LPT_BASE+2 (IRQ enable + /INIT=1). */
    if (!(val & 0x04)) {
        msound_update(msound);
        msound->serial_shift  = 0;
        msound->serial_bits   = 0;
        msound->ff_prefix     = 0;
        msound->frame_rem     = 0;
        msound->irq_requested = 0;
        msound->in_detect     = 0;
        msound->detect_step   = 0;
        msound->dac_val_l     = 0x80;
        msound->dac_val_r     = 0x80;
        timer_disable(&msound->sample_timer);
        fifo8_reset(&msound->fifo);
    }

    msound->control = val;
}

static uint8_t
msound_read_status(void *priv)
{
    const msound_t *const msound = (const msound_t *) priv;

    /* Bit 3 (0x08) = handshake ready; bit 6 (0x40) = detection response */
    return (msound->status & 0x40) | 0x08 | 0x07;
}

static void
msound_get_buffer(int32_t *const buffer, const uint16_t len, void *priv)
{
    msound_t *const msound = (msound_t *) priv;

    msound_update(msound);

    for (uint16_t c = 0; c < len; c++) {
        const int32_t filtered_l = (int32_t) dac_iir(0, msound->buffer[0][c]);
        const int32_t filtered_r = (int32_t) dac_iir(1, msound->buffer[1][c]);

        buffer[c * 2]     += filtered_l;
        buffer[c * 2 + 1] += filtered_r;
    }

    msound->pos = 0;
}

static void
msound_sample_cb(void *priv)
{
    msound_t *const msound = (msound_t *) priv;

    if (msound->state != MSOUND_STATE_PLAYING && msound->state != MSOUND_STATE_DRAINING)
        return;

    msound_update(msound);

    if (!fifo8_is_empty(&msound->fifo)) {
        const uint8_t sample = fifo8_pop(&msound->fifo);
        msound->dac_val_l    = sample;
        msound->dac_val_r    = sample;
    } else if (msound->state == MSOUND_STATE_DRAINING) {
        msound->dac_val_l = 0x80;
        msound->dac_val_r = 0x80;
        msound->state     = MSOUND_STATE_READY;
        return;
    }

    /* Request next 32-byte frame via IRQ7 once per low-FIFO threshold crossing,
       and only after the current 32-byte frame has finished (ISR uses STI). */
    if (msound->state == MSOUND_STATE_PLAYING && msound->frame_rem == 0
        && fifo8_num_used(&msound->fifo) <= 16 && !msound->irq_requested) {
        msound->irq_requested = 1;
        lpt_irq(msound->lpt, 1);
    }

    msound_arm_timer(msound);
}

static void *
msound_init(UNUSED(const device_t *info))
{
    msound_t *const msound = calloc(1, sizeof(msound_t));

    msound->lpt = lpt_attach(msound_write_data,
                             msound_write_ctrl,
                             NULL,
                             msound_read_status,
                             NULL,
                             NULL,
                             NULL,
                             msound);

    sound_add_handler(msound_get_buffer, msound);
    fifo8_create(&msound->fifo, 128);

    msound->dac_val_l       = 0x80;
    msound->dac_val_r       = 0x80;
    msound->serial_rate_div = 0x20;
    msound->sample_hz       = msound_rate_from_param(msound->serial_rate_div);
    msound->status          = 0x48;

    memset(&msound->sample_timer, 0x00, sizeof(pc_timer_t));
    timer_add(&msound->sample_timer, msound_sample_cb, msound, 0);

    msound->log = log_open("MSOUND");
    msound_log(msound->log, "Init\n");

    return msound;
}

static void
msound_close(void *priv)
{
    msound_t *const msound = (msound_t *) priv;

    msound_log(msound->log, "Close\n");
    if (msound->log)
        log_close(msound->log);

    timer_disable(&msound->sample_timer);
    fifo8_destroy(&msound->fifo);
    free(msound);
}

const device_t lpt_msound_device = {
    .name          = "MSound Stereo",
    .internal_name = "lpt_msound",
    .flags         = DEVICE_LPT | DEVICE_HOTPLUG_IN,
    .local         = 0,
    .init          = msound_init,
    .close         = msound_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
