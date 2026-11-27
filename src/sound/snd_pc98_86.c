/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NEC PC-9801-86 sound board.
 *
 *          A YM2608 (OPNA), plus the board's own 16-bit
 *          PCM engine at A460h-A46Eh: a 32 KiB FIFO feeding a pair of DACs
 *          at one of eight sample clocks, with an interrupt raised when the
 *          FIFO drains below a programmed level.  That second engine is
 *          what PC-98 Windows drivers and later games use; the OPNA alone
 *          is not enough to call the board implemented.
 */
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
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/plat_unused.h>
#include <86box/pc98_rom.h>
#include <86box/rom.h>
#include <86box/snd_pc98_opn.h>
#include <86box/sound.h>
#include <86box/timer.h>

#define PC98_86_CLOCK    7987000u /* YM2608B, 7.987 MHz */

#define PC98_86_ROM_SIZE 0x4000
#define PC98_86_ROM_ADDR 0x000cc000u /* hardwired on this board */

/* Each list is tried under every ROM directory 86Box is configured with. */
static const char *const pc98_86_rom_bios[] = {
    "roms/machines/pc9801/sound_rx.rom",
    "roms/machines/pc9801_86/sound_rx.rom",
    "roms/sound/pc9801_86/sound_rx.rom",
    "roms/pc9801_86/sound_rx.rom",
    "roms/machines/pc9801/sound_486mu.rom",
    "roms/machines/pc9801_86/sound_486mu.rom",
    "roms/sound/pc9801_86/sound_486mu.rom",
    "roms/pc9801_86/sound_486mu.rom",
    NULL
};

static const char *const pc98_86_rom_adpcm[] = {
    "roms/sound/yamaha/ym2608_adpcm_rom.bin",
    "roms/machines/pc9801/ym2608_adpcm_rom.bin",
    "roms/machines/pc9801_86/ym2608_adpcm_rom.bin",
    "roms/sound/pc9801_86/ym2608_adpcm_rom.bin",
    "roms/sound/ym2608/ym2608_adpcm_rom.bin",
    "roms/ym2608/ym2608_adpcm_rom.bin",
    NULL
};

#define PC98_86_ADPCM_ROM_SIZE 0x2000u

/* The board's PCM FIFO is 32 KiB. */
#define PC98_86_FIFO_SIZE 32768u

/* PCM control register, A468h. */
#define PC98_86_CTL_RATE 0x07u
#define PC98_86_CTL_RST  0x08u /* FIFO reset            */
#define PC98_86_CTL_IRQC 0x10u /* interrupt clear       */
#define PC98_86_CTL_IRQE 0x20u /* interrupt enable      */
#define PC98_86_CTL_PAUS 0x40u /* pause                 */
#define PC98_86_CTL_RUN  0x80u /* transfer enable       */

/* PCM mode register, A46Ah: bits 6:4 select width and channels. */
#define PC98_86_MODE_OUT 0x70u

typedef struct pc98_86_t {
    pc98_opn_t *opn;
    void       *src_fm;
    void       *src_pcm;
    const int  *clock_pcm;
    rom_t      *bios;
    int         has_rom;

    uint8_t *adpcm_rom;

    uint16_t base;
    int      irq;
    int      irq_sel;
    int      irq_state;
    uint8_t  opna_addr[2];
    uint8_t  joy_select;
    uint8_t  joy_axis_state;
    uint8_t  joy_alias_addr;
    uint8_t  joy_alias_dir;
    uint8_t  joy_alias_select;
    int      fm_irq;
    int      pcm_irq;

    uint8_t id_mask;
    uint8_t vol[8];

    /* PCM engine */
    pc_timer_t dac_timer;
    uint8_t    fifo[PC98_86_FIFO_SIZE];
    uint32_t   head;
    uint32_t   tail;
    uint32_t   count;
    uint8_t    pcm_ctrl;
    uint8_t    pcm_mode;
    uint8_t    pcm_mute;
    uint8_t    pcm_clk;
    uint32_t   irq_rate;
    int        dac_l;
    int        dac_r;
    int32_t    pcm_buffer[SOUNDBUFLEN * 2];
    uint32_t   pcm_buffer_pos;
    int        initialised;
    double     dac_period_us;
} pc98_86_t;

/*
 * YM2608 PSG Port A bits 7:6 reflect PC-9801-86 SW3/SW4.
 *
 * Config ordering:
 *   0 = INT5
 *   1 = INT6
 *   2 = INT41
 *   3 = INT0
 *
 * PSG encoding:
 *   00 = INT0
 *   01 = INT6
 *   10 = INT41
 *   11 = INT5
 */
static const uint8_t pc98_86_irq_pa_bits[4] = {
    0xc0, /* INT5  */
    0x40, /* INT6  */
    0x80, /* INT41 */
    0x00  /* INT0  */
};
/*
 * PC-98 C-BUS interrupt levels, in 86Box's flattened IRQ numbering, in the
 * order of the board's DSW3/DSW4 pair.
 */
static const int pc98_86_int_irq[4] = {
    12, /* INT5, the factory setting */
    13, /* INT6  */
    10, /* INT41 */
    3   /* INT0  */
};

/* A468h bits 2:0 pick a divisor of 25.4 MHz / 16. */
static const int pc98_86_dac_div[8] = { 36, 48, 72, 96, 144, 192, 288, 384 };

#define PC98_86_DAC_BASE_HZ (25400000.0 / 16.0)

static void
pc98_86_update_irq(pc98_86_t *dev)
{
    const int level = (dev->fm_irq || (dev->pcm_irq && (dev->pcm_ctrl & PC98_86_CTL_IRQE))) ? 1 : 0;

    if (level == dev->irq_state)
        return;
    dev->irq_state = level;

    if (level)
        picint(1u << dev->irq);
    else
        picintc(1u << dev->irq);
}

static void
pc98_86_fm_irq(void *priv, int level)
{
    pc98_86_t *dev = (pc98_86_t *) priv;

    dev->fm_irq = level;
    pc98_86_update_irq(dev);
}

/* ------------------------------------------------------------------ */
/* PCM FIFO                                                            */
/* ------------------------------------------------------------------ */

static void
pc98_86_pcm_update(pc98_86_t *dev, uint32_t position)
{
    if (position > SOUNDBUFLEN)
        position = SOUNDBUFLEN;

    if (position < dev->pcm_buffer_pos)
        dev->pcm_buffer_pos = 0;

    /* Preserve the DAC output over time. The mixer runs once per output
     * buffer, whereas the FIFO can advance hundreds of times within it.
     * Flush before changing the DACs, attenuation or mute state. */
    const int     volume = (dev->pcm_mute & 1u) ? 0 : dev->vol[5];
    const int32_t left   = (dev->dac_l * volume) / 15;
    const int32_t right  = (dev->dac_r * volume) / 15;
    for (; dev->pcm_buffer_pos < position; dev->pcm_buffer_pos++) {
        dev->pcm_buffer[dev->pcm_buffer_pos * 2]     = left;
        dev->pcm_buffer[dev->pcm_buffer_pos * 2 + 1] = right;
    }
}

static uint8_t
pc98_86_fifo_pop(pc98_86_t *dev)
{
    uint8_t v;

    if (!dev->count)
        return 0u;

    v         = dev->fifo[dev->tail];
    dev->tail = (dev->tail + 1u) % PC98_86_FIFO_SIZE;
    dev->count--;
    return v;
}

static int16_t
pc98_86_pcm8_to_s16(uint8_t sample)
{
    /* The PC-9801-86 PCM FIFO carries signed two's-complement 8-bit audio. */
    return (int16_t) ((int16_t) (int8_t) sample * 256);
}

/*
 * One DAC tick.  The mode register's bits 6:4 say how many bytes a frame
 * takes and which side each goes to; an unlisted combination means no PCM
 * output at all, which is also how the board is left after a reset.
 */
static void
pc98_86_dac_transfer(pc98_86_t *dev)
{
    switch (dev->pcm_mode & PC98_86_MODE_OUT) {
        case 0x70u: /* 8-bit stereo */
            dev->dac_l = pc98_86_pcm8_to_s16(pc98_86_fifo_pop(dev));
            dev->dac_r = pc98_86_pcm8_to_s16(pc98_86_fifo_pop(dev));
            break;
        case 0x60u: /* 8-bit left */
            dev->dac_l = pc98_86_pcm8_to_s16(pc98_86_fifo_pop(dev));
            break;
        case 0x50u: /* 8-bit right */
            dev->dac_r = pc98_86_pcm8_to_s16(pc98_86_fifo_pop(dev));
            break;
        case 0x30u:
            { /* 16-bit stereo, high byte first */
                int16_t l = (int16_t) (pc98_86_fifo_pop(dev) << 8);
                l |= (int16_t) pc98_86_fifo_pop(dev);
                int16_t r = (int16_t) (pc98_86_fifo_pop(dev) << 8);
                r |= (int16_t) pc98_86_fifo_pop(dev);
                dev->dac_l = l;
                dev->dac_r = r;
                break;
            }
        case 0x20u:
            { /* 16-bit left */
                int16_t l = (int16_t) (pc98_86_fifo_pop(dev) << 8);
                l |= (int16_t) pc98_86_fifo_pop(dev);
                dev->dac_l = l;
                break;
            }
        case 0x10u:
            { /* 16-bit right */
                int16_t r = (int16_t) (pc98_86_fifo_pop(dev) << 8);
                r |= (int16_t) pc98_86_fifo_pop(dev);
                dev->dac_r = r;
                break;
            }
        default:
            break;
    }
}

static void
pc98_86_dac_tick(void *priv)
{
    pc98_86_t *dev = (pc98_86_t *) priv;

    /* The clock bit toggles whether or not a sample moves; software polls
     * it at A466h bit 0 to time itself. */
    dev->pcm_clk = !dev->pcm_clk;

    /* Advance from the scheduled tick, preserving the oscillator phase
     * even when the CPU services this callback slightly late. */
    timer_advance_u64(&dev->dac_timer, (uint64_t) (dev->dac_period_us * TIMER_USEC));

    if ((dev->pcm_ctrl & PC98_86_CTL_PAUS) || !(dev->pcm_ctrl & PC98_86_CTL_RUN))
        return;

    if (dev->count) {
        pc98_86_pcm_update(dev, (uint32_t) SOUND_CLOCK(dev->clock_pcm, sound_pos_global));
        pc98_86_dac_transfer(dev);
    }

    /* Keep the request latched until acknowledged. Repeated DAC ticks
     * below the threshold must not manufacture new interrupt edges. */
    if (!dev->pcm_irq && (dev->count < dev->irq_rate) && (dev->pcm_ctrl & PC98_86_CTL_IRQE)) {
        dev->pcm_irq = 1;
        pc98_86_update_irq(dev);
    }
}

static void
pc98_86_set_rate(pc98_86_t *dev, uint8_t ctrl)
{
    const double hz = PC98_86_DAC_BASE_HZ / (double) pc98_86_dac_div[ctrl & PC98_86_CTL_RATE];

    dev->dac_period_us = 1000000.0 / hz;
    timer_stop(&dev->dac_timer);
    timer_on_auto(&dev->dac_timer, dev->dac_period_us);
    sound_source_set_rate(dev->src_pcm, hz);
}

/* ------------------------------------------------------------------ */
/* I/O                                                                 */
/* ------------------------------------------------------------------ */

/*
 * The PC-9801-86 exposes its joystick through YM2608 PSG Port A. The card
 * only returns connector 1 when Port B's joystick select is exactly 10b in
 * bits 7:6; otherwise those pins are electrically idle.
 */
#define PC98_86_JOY_AXIS_ON  18000
#define PC98_86_JOY_AXIS_OFF 14000

static uint8_t
pc98_86_joystick_axis(int value, uint8_t old, uint8_t negative, uint8_t positive)
{
    if (old & negative) {
        if (value <= -PC98_86_JOY_AXIS_OFF)
            return negative;
        if (value >= PC98_86_JOY_AXIS_ON)
            return positive;
        return 0;
    }

    if (old & positive) {
        if (value >= PC98_86_JOY_AXIS_OFF)
            return positive;
        if (value <= -PC98_86_JOY_AXIS_ON)
            return negative;
        return 0;
    }

    if (value <= -PC98_86_JOY_AXIS_ON)
        return negative;
    if (value >= PC98_86_JOY_AXIS_ON)
        return positive;
    return 0;
}

static uint8_t
pc98_86_joystick_read(pc98_86_t *dev, uint8_t select)
{
    const joystick_state_t *js         = &joystick_state[0][0];
    uint8_t                 value      = 0x3fu;
    uint8_t                 axis_state = 0;

    /* The -86 exposes connector 1 only, selected by Port B bits 7:6 = 10b. */
    if ((select & 0xc0u) != 0x80u) {
        dev->joy_axis_state = 0;
        return value;
    }

    if (JOYSTICK_PRESENT(0, 0)) {
        axis_state = pc98_86_joystick_axis(js->axis[1],
                                           dev->joy_axis_state & 0x03u,
                                           0x01u, 0x02u);
        axis_state |= pc98_86_joystick_axis(js->axis[0],
                                            dev->joy_axis_state & 0x0cu,
                                            0x04u, 0x08u);

        if (axis_state & 0x01u)
            value &= (uint8_t) ~0x01u; /* up */
        if (axis_state & 0x02u)
            value &= (uint8_t) ~0x02u; /* down */
        if (axis_state & 0x04u)
            value &= (uint8_t) ~0x04u; /* left */
        if (axis_state & 0x08u)
            value &= (uint8_t) ~0x08u; /* right */
        if (js->button[0])
            value &= (uint8_t) ~0x10u; /* button 1 */
        if (js->button[1])
            value &= (uint8_t) ~0x20u; /* button 2 */

        /* POV hats often have no useful X/Y axes; use the first mapped hat
         * as another digital source, with the same octants as 86Box's other
         * joystick devices. */
        if ((joystick_type[0] != JS_TYPE_NONE) && (joystick_get_pov_count(joystick_type[0]) > 0) && (js->pov[0] >= 0)) {
            const int pov = js->pov[0];
            if (pov > 315 || pov < 45)
                value &= (uint8_t) ~0x01u;
            if (pov >= 45 && pov < 135)
                value &= (uint8_t) ~0x08u;
            if (pov >= 135 && pov < 225)
                value &= (uint8_t) ~0x02u;
            if (pov >= 225 && pov < 315)
                value &= (uint8_t) ~0x04u;
        }
    }
    dev->joy_axis_state = axis_state;

    /* A keyboard-only setup can still drive games that poll this port. */
    if (keyboard_recv(0x148u) || keyboard_recv(0x048u))
        value &= (uint8_t) ~0x01u; /* up */
    if (keyboard_recv(0x150u) || keyboard_recv(0x050u))
        value &= (uint8_t) ~0x02u; /* down */
    if (keyboard_recv(0x14bu) || keyboard_recv(0x04bu))
        value &= (uint8_t) ~0x04u; /* left */
    if (keyboard_recv(0x14du) || keyboard_recv(0x04du))
        value &= (uint8_t) ~0x08u; /* right */
    if (keyboard_recv(0x02cu))
        value &= (uint8_t) ~0x10u; /* Z: button 1 */
    if (keyboard_recv(0x02du))
        value &= (uint8_t) ~0x20u; /* X: button 2 */

    return value;
}

/*
 * Some games hardcode the -86's factory 188h joystick access even when the
 * board's OPNA has been moved to 288h. Decode only the PSG Port-B select and
 * Port-A read sequence at 188h in that configuration.
 */
static uint8_t
pc98_86_joystick_alias_in(uint16_t port, void *priv)
{
    pc98_86_t     *dev   = (pc98_86_t *) priv;
    const uint32_t index = (uint32_t) ((port - 0x0188u) >> 1);

    if (port & 1u)
        return 0xffu;

    if ((index == 1u) && (dev->joy_alias_addr == 0x0eu)) {
        uint8_t val = pc98_86_joystick_read(dev, dev->joy_alias_select);
        val |= pc98_86_irq_pa_bits[dev->irq_sel];
        return val;
    }

    return 0xffu;
}

static void
pc98_86_joystick_alias_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_86_t     *dev   = (pc98_86_t *) priv;
    const uint32_t index = (uint32_t) ((port - 0x0188u) >> 1);

    if (port & 1u)
        return;

    if (index == 0u) {
        dev->joy_alias_addr = val;
    } else if (index == 1u) {
        if (dev->joy_alias_addr == 0x07u)
            dev->joy_alias_dir = val;
        else if ((dev->joy_alias_addr == 0x0fu) && (dev->joy_alias_dir & 0x80u))
            dev->joy_alias_select = val;
    }
}

static uint8_t
pc98_86_opna_in(uint16_t port, void *priv)
{
    pc98_86_t     *dev   = (pc98_86_t *) priv;
    uint8_t        val   = 0xFF;
    const uint32_t index = (uint32_t) ((port - dev->base) >> 1);
    if (port & 1u)
        return 0xffu;

    /* A460h bit 0 enables the extended OPNA ports. In OPN mode they
     * float high for every read, including PMD's board detection. */
    if ((index >= 2u) && !(dev->id_mask & 1u))
        return 0xffu;

    if ((index == 1u) && (dev->opna_addr[0] == 0x0eu))
        pc98_opn_set_port_a(dev->opn, pc98_86_joystick_read(dev, dev->joy_select));

    val = pc98_opn_read(dev->opn, index);

    if ((index == 1u) && (dev->opna_addr[0] == 0x0eu)) {
        val &= 0x3fu;
        val |= pc98_86_irq_pa_bits[dev->irq_sel];
    }

    return val;
}

static void
pc98_86_opna_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_86_t     *dev   = (pc98_86_t *) priv;
    const uint32_t index = (uint32_t) ((port - dev->base) >> 1);
    if (port & 1u)
        return;
    if ((index >= 2u) && !(dev->id_mask & 1u))
        return;

    if (index == 0u)
        dev->opna_addr[0] = val;
    else if (index == 2u)
        dev->opna_addr[1] = val;
    pc98_opn_write(dev->opn, index, val);

    if ((index == 1u) && (dev->opna_addr[0] == 0x0fu))
        dev->joy_select = pc98_opn_get_port_b(dev->opn);
}

static uint8_t
pc98_86_pcm_in(uint16_t port, void *priv)
{
    pc98_86_t *dev = (pc98_86_t *) priv;

    switch (port) {
        case 0xa460u:
            /*
             * Board ID.  Bit 4 reflects the port base, and the low two bits
             * read back the OPNA mask register.  4x means "PC-9801-86 at
             * 188h", which is what drivers look for.
             */
            return (uint8_t) (0x40u | ((dev->base == 0x0288u) ? 0x10u : 0x00u) | dev->id_mask);

        case 0xa466u:
            {
                uint8_t v = 0u;
                if (dev->count >= PC98_86_FIFO_SIZE)
                    v |= 0x80u; /* full  */
                if (!dev->count)
                    v |= 0x40u; /* empty */
                v |= (uint8_t) (dev->pcm_clk & 1u);
                return v;
            }

        case 0xa468u:
            return (uint8_t) (dev->pcm_ctrl | (dev->pcm_irq ? 0x10u : 0x00u));

        case 0xa46au:
            return dev->pcm_mode;

        case 0xa46cu:
            return 0u; /* recording, which this board cannot do */

        case 0xa66eu:
            return dev->pcm_mute;

        default:
            return 0xffu;
    }
}

static void
pc98_86_pcm_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_86_t *dev = (pc98_86_t *) priv;

    switch (port) {
        case 0xa460u:
            if ((dev->id_mask ^ val) & 1u)
                pc98_opn_set_extended(dev->opn, val & 1u);
            if ((dev->id_mask ^ val) & 2u)
                pc98_opn_set_output_enabled(dev->opn, !(val & 2u));
            dev->id_mask = (uint8_t) (val & 3u);
            break;

        case 0xa466u:
            /* The PCM output is mixer line 5; its four-bit level is active
             * low. Keep the other line latches for their shared register
             * behavior, but only line 5 affects this board's PCM DAC. */
            pc98_86_pcm_update(dev, (uint32_t) SOUND_CLOCK(dev->clock_pcm, sound_pos_global));
            if ((val & 0xe0u) == 0xa0u)
                dev->vol[5] = (uint8_t) ((~val) & 0x0fu);
            else
                dev->vol[(val >> 5) & 7u] = (uint8_t) (val & 0x0fu);
            break;

        case 0xa468u:
            if (((val & PC98_86_CTL_RATE) != (dev->pcm_ctrl & PC98_86_CTL_RATE)) || !dev->initialised)
                pc98_86_set_rate(dev, val);

            if ((val & PC98_86_CTL_RST) && !(dev->pcm_ctrl & PC98_86_CTL_RST))
                dev->head = dev->tail = dev->count = 0u;

            if (!(val & PC98_86_CTL_IRQC))
                dev->pcm_irq = 0;

            dev->initialised = 1;
            dev->pcm_ctrl    = (uint8_t) (val & ~PC98_86_CTL_IRQC);
            pc98_86_update_irq(dev);
            break;

        case 0xa46au:
            /*
             * One register, two meanings: with the interrupt-enable bit set
             * this is the FIFO level that triggers one, otherwise it is the
             * sample format.
             */
            if (dev->pcm_ctrl & PC98_86_CTL_IRQE)
                dev->irq_rate = ((uint32_t) val + 1u) * 128u;
            else
                dev->pcm_mode = val;
            break;

        case 0xa46cu:
            /* The DAC advances on its oscillator, including when the FIFO
             * is full. A write must not force playback while paused or
             * stopped. CPU WAIT timing on overflow is not modelled. */
            if (dev->count < PC98_86_FIFO_SIZE) {
                dev->fifo[dev->head] = val;
                dev->head            = (dev->head + 1u) % PC98_86_FIFO_SIZE;
                dev->count++;
            }
            break;

        case 0xa66eu:
            pc98_86_pcm_update(dev, (uint32_t) SOUND_CLOCK(dev->clock_pcm, sound_pos_global));
            dev->pcm_mute = (uint8_t) (val & 1u);
            break;

        default:
            break;
    }
}

static void
pc98_86_get_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    pc98_86_t *dev = (pc98_86_t *) priv;

    pc98_opn_generate(dev->opn, buffer, len);
}

static void
pc98_86_get_pcm_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    pc98_86_t *dev = (pc98_86_t *) priv;

    pc98_86_pcm_update(dev, len);

    const uint32_t frames = (len < SOUNDBUFLEN) ? len : SOUNDBUFLEN;
    for (uint32_t i = 0; i < frames; i++) {
        buffer[i * 2] += dev->pcm_buffer[i * 2];
        buffer[i * 2 + 1] += dev->pcm_buffer[i * 2 + 1];
    }
    dev->pcm_buffer_pos = 0;
}

/* ------------------------------------------------------------------ */
/* Device plumbing                                                     */
/* ------------------------------------------------------------------ */

static void
pc98_86_load_adpcm_rom(pc98_86_t *dev)
{
    char path[PC98_ROM_PATH_MAX];

    if (!pc98_rom_find_quiet(pc98_86_rom_adpcm, path, sizeof(path))) {
        pclog("[PC98] 86: ym2608_adpcm_rom.bin not found under any "
              "configured ROM directory - the OPNA's rhythm channels will "
              "be silent\n");
        return;
    }

    dev->adpcm_rom = (uint8_t *) malloc(PC98_86_ADPCM_ROM_SIZE);
    if (!dev->adpcm_rom)
        return;

    if (rom_load_linear(path, 0, PC98_86_ADPCM_ROM_SIZE, 0, dev->adpcm_rom)) {
        pc98_opn_set_adpcm_rom(dev->opn, dev->adpcm_rom,
                               PC98_86_ADPCM_ROM_SIZE);
    } else {
        free(dev->adpcm_rom);
        dev->adpcm_rom = NULL;
    }
}

static void
pc98_86_reset(void *priv)
{
    pc98_86_t *dev = (pc98_86_t *) priv;

    pc98_86_pcm_update(dev, (uint32_t) SOUND_CLOCK(dev->clock_pcm, sound_pos_global));
    pc98_opn_reset(dev->opn);

    /* A460h resets to OPN compatibility mode (three FM channels). */
    dev->opna_addr[0] = 0u;
    dev->opna_addr[1] = 0u;

    dev->head = dev->tail = dev->count = 0u;
    dev->pcm_ctrl = dev->pcm_mode = 0u;
    dev->pcm_clk                  = 0u;
    dev->pcm_mute                 = 0x00u;
    dev->irq_rate                 = 0u;
    dev->dac_l = dev->dac_r = 0;
    dev->initialised        = 0;
    dev->id_mask            = 0u;
    dev->joy_select         = 0u;
    dev->joy_axis_state     = 0u;
    dev->joy_alias_addr     = 0u;
    dev->joy_alias_dir      = 0u;
    dev->joy_alias_select   = 0u;
    dev->fm_irq = dev->pcm_irq = 0;
    memset(dev->vol, 0x0f, sizeof(dev->vol));

    pc98_86_update_irq(dev);
    pc98_86_set_rate(dev, 0u);
}

static void *
pc98_86_init(UNUSED(const device_t *info))
{
    (void) info; /* the stand-ins in tests/stub define UNUSED() as a no-op */

    pc98_86_t *dev = (pc98_86_t *) calloc(1, sizeof(pc98_86_t));

    if (!dev)
        return NULL;

    dev->base    = device_get_config_int("base") ? 0x0288u : 0x0188u;
    dev->irq_sel = device_get_config_int("irq") & 3;
    dev->irq     = pc98_86_int_irq[dev->irq_sel];
    dev->opn     = pc98_opn_create(PC98_OPN_YM2608, PC98_86_CLOCK, pc98_86_fm_irq,
                                   dev);
    if (!dev->opn) {
        free(dev);
        return NULL;
    }

    dev->src_fm = sound_source_add_stream(I_NORMAL, pc98_86_get_buffer, dev);
    sound_source_set_label(dev->src_fm, "FM");
    pc98_opn_set_clock(dev->opn, sound_source_get_pos_ptr(dev->src_fm));
    dev->src_pcm = sound_source_add_stream(I_NORMAL, pc98_86_get_pcm_buffer, dev);
    sound_source_set_label(dev->src_pcm, "PCM");
    dev->clock_pcm = sound_source_get_pos_ptr(dev->src_pcm);

    /* The FIFO is separate from the OPNA's ADPCM-B memory bus. A stock
     * -86 has no RAM on that bus; Chibi-oto adds it as a modification. */
    if (!pc98_opn_set_adpcm_ram(dev->opn, device_get_config_int("adpcm_ram")))
        pclog("[PC98] 86: unable to allocate optional ADPCM-B memory\n");

    pc98_86_load_adpcm_rom(dev);

    if (device_get_config_int("rom")) {
        char bios_path[PC98_ROM_PATH_MAX];

        if (pc98_rom_find_quiet(pc98_86_rom_bios, bios_path,
                                sizeof(bios_path))) {
            dev->bios = (rom_t *) calloc(1, sizeof(rom_t));
            if (dev->bios)
                dev->has_rom = !rom_init(dev->bios, bios_path, PC98_86_ROM_ADDR,
                                         PC98_86_ROM_SIZE, PC98_86_ROM_SIZE - 1, 0,
                                         MEM_MAPPING_EXTERNAL);
            if (!dev->has_rom) {
                free(dev->bios);
                dev->bios = NULL;
            }
            if (dev->has_rom)
                pclog("[PC98] 86: %s mapped at CC000h\n", bios_path);
        }
        if (!dev->has_rom)
            pclog("[PC98] 86: no sound BIOS ROM found under any configured "
                  "ROM directory; the OPNA and the PCM engine still work, "
                  "the BIOS entry points do not\n");
    }

    /* OPNA: 188h, 18Ah, 18Ch, 18Eh. */
    io_sethandler(dev->base, 8, pc98_86_opna_in, NULL, NULL, pc98_86_opna_out,
                  NULL, NULL, dev);
    if (dev->base == 0x0288u)
        io_sethandler(0x0188u, 4, pc98_86_joystick_alias_in, NULL, NULL,
                      pc98_86_joystick_alias_out, NULL, NULL, dev);

    /* PCM engine.  A462h/A464h belong to the PC-9801-73's volume chip and
     * are deliberately left undecoded here. */
    io_sethandler(0xa460, 1, pc98_86_pcm_in, NULL, NULL, pc98_86_pcm_out, NULL,
                  NULL, dev);
    for (uint16_t p = 0xa466; p <= 0xa46c; p += 2)
        io_sethandler(p, 1, pc98_86_pcm_in, NULL, NULL, pc98_86_pcm_out, NULL,
                      NULL, dev);
    io_sethandler(0xa66e, 1, pc98_86_pcm_in, NULL, NULL, pc98_86_pcm_out, NULL,
                  NULL, dev);

    timer_add(&dev->dac_timer, pc98_86_dac_tick, dev, 0);

    pc98_86_reset(dev);

    pclog("[PC98] 86: YM2608 at %03Xh, IRQ %d%s\n", dev->base, dev->irq,
          dev->has_rom ? ", sound BIOS at CC000h" : "");
    return dev;
}

static void
pc98_86_close(void *priv)
{
    pc98_86_t *dev = (pc98_86_t *) priv;

    if (!dev)
        return;

    timer_disable(&dev->dac_timer);

    io_removehandler(dev->base, 8, pc98_86_opna_in, NULL, NULL,
                     pc98_86_opna_out, NULL, NULL, dev);
    if (dev->base == 0x0288u)
        io_removehandler(0x0188u, 4, pc98_86_joystick_alias_in, NULL, NULL,
                         pc98_86_joystick_alias_out, NULL, NULL, dev);
    io_removehandler(0xa460, 1, pc98_86_pcm_in, NULL, NULL, pc98_86_pcm_out,
                     NULL, NULL, dev);
    for (uint16_t p = 0xa466; p <= 0xa46c; p += 2)
        io_removehandler(p, 1, pc98_86_pcm_in, NULL, NULL, pc98_86_pcm_out,
                         NULL, NULL, dev);
    io_removehandler(0xa66e, 1, pc98_86_pcm_in, NULL, NULL, pc98_86_pcm_out,
                     NULL, NULL, dev);

    sound_source_remove(dev->src_fm);
    sound_source_remove(dev->src_pcm);
    pc98_opn_destroy(dev->opn);
    free(dev->adpcm_rom);
    if (dev->has_rom) {
        /* Registered mapping nodes remain in the memory subsystem until
         * its next reset. Keep the node alive, with no backing storage. */
        mem_mapping_disable(&dev->bios->mapping);
        mem_mapping_set_handler(&dev->bios->mapping, NULL, NULL, NULL, NULL, NULL, NULL);
        mem_mapping_set_exec(&dev->bios->mapping, NULL);
        mem_mapping_set_p(&dev->bios->mapping, NULL);
        free(dev->bios->rom);
        dev->bios->rom = NULL;
    }
    free(dev);
}

static int
pc98_86_available(void)
{
    return 1;
}

static const device_config_t pc98_86_config[] = {
    // clang-format off
    {
        .name           = "base",
        .description    = "Port base (SW1)",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "188H", .value = 0 },
            { .description = "288H", .value = 1 },
            { .description = ""                 }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "irq",
        .description    = "Interrupt level (SW3/SW4)",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "INT5 (IRQ 12)",  .value = 0 },
            { .description = "INT6 (IRQ 13)",  .value = 1 },
            { .description = "INT41 (IRQ 10)", .value = 2 },
            { .description = "INT0 (IRQ 3)",   .value = 3 },
            { .description = ""                           }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "adpcm_ram",
        .description    = "Chibi-oto ADPCM memory (256 KiB)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "rom",
        .description    = "Sound BIOS at CC000H (SW2)",
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

const device_t pc98_86_device = {
    .name          = "NEC PC-9801-86",
    .internal_name = "pc9801_86",
    .flags         = DEVICE_CBUS,
    .local         = 0,
    .init          = pc98_86_init,
    .close         = pc98_86_close,
    .reset         = pc98_86_reset,
    .available     = pc98_86_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pc98_86_config
};
