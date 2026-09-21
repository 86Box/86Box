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
 *          A YM2608 (OPNA) with its ADPCM RAM, plus the board's own 16-bit
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
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/plat_unused.h>
#include <86box/pc98_rom.h>
#include <86box/rom.h>
#include <86box/snd_pc98_opn.h>
#include <86box/sound.h>
#include <86box/timer.h>

#define PC98_86_CLOCK 7987000u /* YM2608B, 7.987 MHz */

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
    rom_t       bios;
    int         has_rom;

    uint8_t *adpcm_rom;

    uint16_t base;
    int      irq;
    int      irq_state;
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
    int        initialised;
    double     dac_period_us;
} pc98_86_t;

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
    const int level = (dev->fm_irq || dev->pcm_irq) ? 1 : 0;

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

static uint8_t
pc98_86_fifo_pop(pc98_86_t *dev)
{
    uint8_t v;

    if (!dev->count)
        return 0u;

    v          = dev->fifo[dev->tail];
    dev->tail  = (dev->tail + 1u) % PC98_86_FIFO_SIZE;
    dev->count--;
    return v;
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
            dev->dac_l = (int16_t) (pc98_86_fifo_pop(dev) << 8);
            dev->dac_r = (int16_t) (pc98_86_fifo_pop(dev) << 8);
            break;
        case 0x60u: /* 8-bit left */
            dev->dac_l = (int16_t) (pc98_86_fifo_pop(dev) << 8);
            break;
        case 0x50u: /* 8-bit right */
            dev->dac_r = (int16_t) (pc98_86_fifo_pop(dev) << 8);
            break;
        case 0x30u: { /* 16-bit stereo, high byte first */
            int16_t l = (int16_t) (pc98_86_fifo_pop(dev) << 8);
            l |= (int16_t) pc98_86_fifo_pop(dev);
            int16_t r = (int16_t) (pc98_86_fifo_pop(dev) << 8);
            r |= (int16_t) pc98_86_fifo_pop(dev);
            dev->dac_l = l;
            dev->dac_r = r;
            break;
        }
        case 0x20u: { /* 16-bit left */
            int16_t l = (int16_t) (pc98_86_fifo_pop(dev) << 8);
            l |= (int16_t) pc98_86_fifo_pop(dev);
            dev->dac_l = l;
            break;
        }
        case 0x10u: { /* 16-bit right */
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

    /* Free-running: re-arm from the period the control register set, not
     * from the timer's own state, so a rate change mid-stream takes effect
     * on the next tick rather than the next reset. */
    timer_on_auto(&dev->dac_timer, dev->dac_period_us);

    if ((dev->pcm_ctrl & PC98_86_CTL_PAUS) || !(dev->pcm_ctrl & PC98_86_CTL_RUN))
        return;

    if (!dev->count)
        return;

    pc98_86_dac_transfer(dev);

    /* Drained below the programmed level: ask for more.  The interrupt is
     * edge-triggered - Windows drivers rely on that. */
    if ((dev->count < dev->irq_rate) && (dev->pcm_ctrl & PC98_86_CTL_IRQE)) {
        dev->pcm_irq = 0;
        pc98_86_update_irq(dev);
        dev->pcm_irq = 1;
        pc98_86_update_irq(dev);
    }
}

static void
pc98_86_set_rate(pc98_86_t *dev, uint8_t ctrl)
{
    const double hz = PC98_86_DAC_BASE_HZ /
                      (double) pc98_86_dac_div[ctrl & PC98_86_CTL_RATE];

    dev->dac_period_us = 1000000.0 / hz;
    timer_stop(&dev->dac_timer);
    timer_on_auto(&dev->dac_timer, dev->dac_period_us);
}

/* ------------------------------------------------------------------ */
/* I/O                                                                 */
/* ------------------------------------------------------------------ */

static uint8_t
pc98_86_opna_in(uint16_t port, void *priv)
{
    pc98_86_t *dev = (pc98_86_t *) priv;

    if (port & 1u)
        return 0xffu;
    return pc98_opn_read(dev->opn, (uint32_t) ((port - dev->base) >> 1));
}

static void
pc98_86_opna_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_86_t *dev = (pc98_86_t *) priv;

    if (port & 1u)
        return;
    pc98_opn_write(dev->opn, (uint32_t) ((port - dev->base) >> 1), val);
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
            return (uint8_t) (0x40u | ((dev->base == 0x0288u) ? 0x10u : 0x00u) |
                              dev->id_mask);

        case 0xa466u: {
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
            dev->id_mask = (uint8_t) (val & 3u);
            break;

        case 0xa466u:
            /* Volume, one nibble per line, the line in bits 7:5. */
            dev->vol[(val >> 5) & 7u] = (uint8_t) (val & 0x0fu);
            break;

        case 0xa468u:
            if (((val & PC98_86_CTL_RATE) != (dev->pcm_ctrl & PC98_86_CTL_RATE)) ||
                !dev->initialised)
                pc98_86_set_rate(dev, val);

            if (val & PC98_86_CTL_RST)
                dev->head = dev->tail = dev->count = 0u;

            if (!(val & PC98_86_CTL_IRQC)) {
                if (!(dev->count < dev->irq_rate) || !(val & PC98_86_CTL_RUN)) {
                    dev->pcm_irq = 0;
                    pc98_86_update_irq(dev);
                }
            }

            dev->initialised = 1;
            dev->pcm_ctrl    = (uint8_t) (val & ~PC98_86_CTL_IRQC);
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
            /*
             * A full FIFO is not an error the board reports; it simply
             * cannot take the byte.  Step the DAC once so a driver that has
             * over-filled still makes progress rather than wedging.
             */
            if (dev->count >= PC98_86_FIFO_SIZE)
                pc98_86_dac_transfer(dev);

            if (dev->count < PC98_86_FIFO_SIZE) {
                dev->fifo[dev->head] = val;
                dev->head            = (dev->head + 1u) % PC98_86_FIFO_SIZE;
                dev->count++;
            }
            break;

        case 0xa66eu:
            dev->pcm_mute = val;
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

    /* The PCM DACs hold their last sample between ticks, so the mixer sees
     * a step-held signal rather than an interpolated one - which is what
     * the hardware does too. */
    if (dev->pcm_mute & 1u)
        return;

    for (uint16_t i = 0; i < len; i++) {
        buffer[i * 2]     += dev->dac_l;
        buffer[i * 2 + 1] += dev->dac_r;
    }
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

    pc98_opn_reset(dev->opn);

    dev->head = dev->tail = dev->count = 0u;
    dev->pcm_ctrl = dev->pcm_mode = 0u;
    dev->pcm_clk                  = 0u;
    dev->pcm_mute                 = 0x01u;
    dev->irq_rate                 = 0u;
    dev->dac_l = dev->dac_r = 0;
    dev->initialised        = 0;
    dev->id_mask            = 0u;
    dev->fm_irq = dev->pcm_irq = 0;
    memset(dev->vol, 0x0f, sizeof(dev->vol));

    pc98_86_update_irq(dev);
}

static void *
pc98_86_init(UNUSED(const device_t *info))
{
    (void) info; /* the stand-ins in tests/stub define UNUSED() as a no-op */

    pc98_86_t *dev = (pc98_86_t *) calloc(1, sizeof(pc98_86_t));

    if (!dev)
        return NULL;

    dev->base = device_get_config_int("base") ? 0x0288u : 0x0188u;
    dev->irq  = pc98_86_int_irq[device_get_config_int("irq") & 3];

    dev->opn = pc98_opn_create(PC98_OPN_YM2608, PC98_86_CLOCK, pc98_86_fm_irq,
                               dev);
    if (!dev->opn) {
        free(dev);
        return NULL;
    }

    pc98_86_load_adpcm_rom(dev);

    if (device_get_config_int("rom")) {
        char bios_path[PC98_ROM_PATH_MAX];

        if (pc98_rom_find_quiet(pc98_86_rom_bios, bios_path,
                                sizeof(bios_path))) {
            dev->has_rom = !rom_init(&dev->bios, bios_path, PC98_86_ROM_ADDR,
                                     PC98_86_ROM_SIZE, PC98_86_ROM_SIZE - 1, 0,
                                     MEM_MAPPING_EXTERNAL);
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

    sound_add_handler(pc98_86_get_buffer, dev);

    pc98_86_reset(dev);

    /* Free-run the DAC clock: software polls A466h bit 0 before it ever
     * writes a control byte. */
    pc98_86_set_rate(dev, 0u);

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
    io_removehandler(0xa460, 1, pc98_86_pcm_in, NULL, NULL, pc98_86_pcm_out,
                     NULL, NULL, dev);
    for (uint16_t p = 0xa466; p <= 0xa46c; p += 2)
        io_removehandler(p, 1, pc98_86_pcm_in, NULL, NULL, pc98_86_pcm_out,
                         NULL, NULL, dev);
    io_removehandler(0xa66e, 1, pc98_86_pcm_in, NULL, NULL, pc98_86_pcm_out,
                     NULL, NULL, dev);

    pc98_opn_destroy(dev->opn);
    free(dev->adpcm_rom);
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
