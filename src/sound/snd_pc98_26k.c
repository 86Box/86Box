/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NEC PC-9801-26K sound board.
 *
 *          A single YM2203 (OPN) on a C-BUS card, with a 16 KiB sound BIOS
 *          ROM and three jumper banks: port base, ROM address and interrupt
 *          level.  The defaults here are the ones the board shipped with
 *          and the ones PC-98 software expects - port base 188h, ROM at
 *          CC000h, INT5.
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
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/plat_unused.h>
#include <86box/pc98_rom.h>
#include <86box/rom.h>
#include <86box/snd_pc98_opn.h>
#include <86box/sound.h>
#include <86box/timer.h>
#include "snd_pc98_joystick.h"

/* 15.9744 MHz / 4.  The -26 drew this from the C-BUS clock instead, which
 * is the only documented difference between the two revisions. */
#define PC98_26K_CLOCK 3993600u

#define PC98_26K_ROM_SIZE 0x4000

/* Each list is tried under every ROM directory 86Box is configured with. */
static const char *const pc98_26k_rom_linear[] = {
    "roms/machines/pc9801/sound.rom",
    "roms/machines/pc9801_26/sound.rom",
    "roms/sound/pc9801_26/sound.rom",
    "roms/sound/nec/sound.rom",
    "roms/pc9801_26/sound.rom",
    NULL
};

static const char *const pc98_26k_rom_9821[] = {
    "roms/machines/pc9801/sound_9821.rom",
    "roms/machines/pc9801_26/sound_9821.rom",
    "roms/sound/pc9801_26/sound_9821.rom",
    "roms/pc9801_26/sound_9821.rom",
    NULL
};

static const char *const pc98_26k_rom_even[] = {
    "roms/machines/pc9801/26k_wyka01_00.bin",
    "roms/machines/pc9801_26/26k_wyka01_00.bin",
    "roms/sound/pc9801_26/26k_wyka01_00.bin",
    "roms/pc9801_26/26k_wyka01_00.bin",
    NULL
};

static const char *const pc98_26k_rom_odd[] = {
    "roms/machines/pc9801/26k_wyka02_00.bin",
    "roms/machines/pc9801_26/26k_wyka02_00.bin",
    "roms/sound/pc9801_26/26k_wyka02_00.bin",
    "roms/pc9801_26/26k_wyka02_00.bin",
    NULL
};
/*
 * YM2203 PSG Port A bits 7:6 reflect PC-9801-26K SW3/SW4.
 *
 * Config ordering:
 *   0 = INT0
 *   1 = INT6
 *   2 = INT41
 *   3 = INT5
 *
 * PSG encoding:
 *   00 = INT0
 *   01 = INT6
 *   10 = INT41
 *   11 = INT5
 */
static const uint8_t pc98_26k_irq_pa_bits[4] = {
    0x00, /* INT0  */
    0x40, /* INT6  */
    0x80, /* INT41 */
    0xc0  /* INT5  */
};
/*
 * PC-98 C-BUS interrupt levels, in 86Box's flattened IRQ numbering.  The
 * board's JP6A1/JP6A3 pair selects one of these four.
 */
static const int pc98_26k_int_irq[4] = {
    3,  /* INT0  */
    13, /* INT6  */
    10, /* INT41 */
    12  /* INT5  */
};

static const uint32_t pc98_26k_rom_addr[8] = {
    0x000c8000u, 0x000cc000u, 0x000d0000u, 0x000d4000u, 0u, 0u, 0u, 0u
};

typedef struct pc98_26k_t {
    pc98_opn_t *opn;
    rom_t      *bios;
    int         has_rom;
    uint16_t    base;
    int         irq;
    uint8_t     opn_addr;
    int         irq_state;
    uint8_t     int_level;
} pc98_26k_t;

static void
pc98_26k_irq(void *priv, int level)
{
    pc98_26k_t *dev = (pc98_26k_t *) priv;

    if (level == dev->irq_state)
        return;
    dev->irq_state = level;

    if (level)
        picint(1u << dev->irq);
    else
        picintc(1u << dev->irq);
}

static uint8_t
pc98_26k_in(uint16_t port, void *priv)
{
    pc98_26k_t *dev = (pc98_26k_t *) priv;
    const uint32_t index = (uint32_t) ((port - dev->base) >> 1);
    uint8_t val = 0xFF;
    /* Only the even byte lane is decoded: 188h and 18Ah. */
    if (port & 1u)
        return 0xffu;

    if ((index == 1) && (dev->opn_addr == 0x0e))
        pc98_opn_set_port_a(dev->opn,
            pc98_sound_joystick(pc98_opn_get_port_b(dev->opn),
                                pc98_26k_irq_pa_bits[dev->int_level]));
    val = pc98_opn_read(dev->opn, index);
    if ((index == 1) && (dev->opn_addr == 0x0e)) {
        /* The same jumpers drive Port A and the physical interrupt line. */
        val &= 0x3f;
        val |= pc98_26k_irq_pa_bits[dev->int_level];
    }
    return val;
}

static void
pc98_26k_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_26k_t *dev = (pc98_26k_t *) priv;
    const uint32_t index = (uint32_t) ((port - dev->base) >> 1);
    if (port & 1u)
        return;
    if (index == 0)
        dev->opn_addr = val;
    pc98_opn_write(dev->opn, index, val);
}

static void
pc98_26k_get_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    pc98_26k_t *dev = (pc98_26k_t *) priv;

    pc98_opn_generate(dev->opn, buffer, len);
}

static int
pc98_26k_load_rom(pc98_26k_t *dev, uint32_t addr)
{
    if (!addr) {
        pclog("[PC98] 26K: sound ROM disabled by jumper\n");
        return 0;
    }

    /*
     * The board's ROM window overlaps the SASI card's when the jumper is
     * set to D0000h, which is a real hardware conflict rather than
     * something to paper over - say so and carry on, as the machine would.
     */
    if (addr == 0x000d0000u)
        pclog("[PC98] 26K: sound ROM jumpered to D0000h, which is also "
              "where a PC-9801-27 SASI board maps its own BIOS\n");

    char p1[PC98_ROM_PATH_MAX];
    char p2[PC98_ROM_PATH_MAX];

    dev->bios = (rom_t *) calloc(1, sizeof(rom_t));
    if (!dev->bios)
        return 0;

    if (pc98_rom_find_quiet(pc98_26k_rom_linear, p1, sizeof(p1))) {
        if (!rom_init(dev->bios, p1, addr, PC98_26K_ROM_SIZE,
                      PC98_26K_ROM_SIZE - 1, 0, MEM_MAPPING_EXTERNAL)) {
            pclog("[PC98] 26K: %s mapped at %05X\n", p1, (unsigned) addr);
            return 1;
        }
    }

    if (pc98_rom_find_quiet(pc98_26k_rom_9821, p1, sizeof(p1))) {
        if (!rom_init(dev->bios, p1, addr, PC98_26K_ROM_SIZE,
                      PC98_26K_ROM_SIZE - 1, 0, MEM_MAPPING_EXTERNAL)) {
            pclog("[PC98] 26K: %s mapped at %05X\n", p1, (unsigned) addr);
            return 1;
        }
    }

    /* The physical -26K pair: 01 carries the even bytes, 02 the odd ones. */
    if (pc98_rom_find_quiet(pc98_26k_rom_even, p1, sizeof(p1)) &&
        pc98_rom_find_quiet(pc98_26k_rom_odd, p2, sizeof(p2))) {
        if (!rom_init_interleaved(dev->bios, p1, p2, addr,
                                  PC98_26K_ROM_SIZE, PC98_26K_ROM_SIZE - 1, 0,
                                  MEM_MAPPING_EXTERNAL)) {
            pclog("[PC98] 26K: %s + %s mapped at %05X\n", p1, p2,
                  (unsigned) addr);
            return 1;
        }
    }

    free(dev->bios);
    dev->bios = NULL;

    /*
     * The FM hardware works without it; only the BIOS calls that software
     * reaches through INT DCh do not.  Most PC-98 games drive the YM2203
     * directly, so this is a warning and not a failure.
     */
    pclog("[PC98] 26K: no sound BIOS ROM found - the board still answers "
          "%03Xh, but software that calls the sound BIOS will not find it.  "
          "Looked for sound.rom, sound_9821.rom or the 26k_wyka01/02 pair "
          "under every configured ROM directory\n",
          dev->base);
    return 0;
}

static void
pc98_26k_reset(void *priv)
{
    pc98_26k_t *dev = (pc98_26k_t *) priv;

    pc98_opn_reset(dev->opn);
    dev->opn_addr = 0u;
    pc98_26k_irq(dev, 0);
}

static void *
pc98_26k_init(UNUSED(const device_t *info))
{
    (void) info; /* the stand-ins in tests/stub define UNUSED() as a no-op */

    pc98_26k_t *dev = (pc98_26k_t *) calloc(1, sizeof(pc98_26k_t));

    if (!dev)
        return NULL;

    dev->base      = device_get_config_int("base") ? 0x0188u : 0x0088u;
    dev->int_level = (uint8_t) (device_get_config_int("irq") & 3);
    dev->irq       = pc98_26k_int_irq[dev->int_level];

    dev->opn = pc98_opn_create(PC98_OPN_YM2203, PC98_26K_CLOCK, pc98_26k_irq,
                               dev);
    if (!dev->opn) {
        free(dev);
        return NULL;
    }

    dev->has_rom = pc98_26k_load_rom(
        dev, pc98_26k_rom_addr[device_get_config_int("rom_addr") & 7]);

    /* 188h and 18Ah; the odd addresses in between are not decoded. */
    io_sethandler(dev->base, 4, pc98_26k_in, NULL, NULL, pc98_26k_out, NULL,
                  NULL, dev);

    sound_add_handler(pc98_26k_get_buffer, dev);

    pclog("[PC98] 26K: YM2203 at %03Xh, INT%d (IRQ %d)%s\n", dev->base,
          dev->int_level, dev->irq, dev->has_rom ? ", sound BIOS mapped" : "");

    pc98_26k_reset(dev);
    return dev;
}

static void
pc98_26k_close(void *priv)
{
    pc98_26k_t *dev = (pc98_26k_t *) priv;

    if (!dev)
        return;

    io_removehandler(dev->base, 4, pc98_26k_in, NULL, NULL, pc98_26k_out, NULL,
                     NULL, dev);
    pc98_opn_destroy(dev->opn);
    if (dev->has_rom) {
        /* Mapping nodes are retained by the memory subsystem until reset. */
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
pc98_26k_available(void)
{
    /* The chip is the board; the BIOS ROM is optional. */
    return 1;
}

static const device_config_t pc98_26k_config[] = {
  // clang-format off
    {
        .name           = "base",
        .description    = "Port base (JP6A4)",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "188H", .value = 1 },
            { .description = "088H", .value = 0 },
            { .description = ""                 }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "irq",
        .description    = "Interrupt level (JP6A1/JP6A3)",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 3,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "INT5 (IRQ 12)",  .value = 3 },
            { .description = "INT0 (IRQ 3)",   .value = 0 },
            { .description = "INT41 (IRQ 10)", .value = 2 },
            { .description = "INT6 (IRQ 13)",  .value = 1 },
            { .description = ""                           }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "rom_addr",
        .description    = "Sound BIOS address (JP6A2)",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "CC000H",  .value = 1 },
            { .description = "C8000H",  .value = 0 },
            { .description = "D0000H",  .value = 2 },
            { .description = "D4000H",  .value = 3 },
            { .description = "Disabled", .value = 4 },
            { .description = ""                     }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t pc98_26k_device = {
    .name          = "NEC PC-9801-26K",
    .internal_name = "pc9801_26k",
    .flags         = DEVICE_CBUS,
    .local         = 0,
    .init          = pc98_26k_init,
    .close         = pc98_26k_close,
    .reset         = pc98_26k_reset,
    .available     = pc98_26k_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pc98_26k_config
};
