/* Speak Board and Spark Board C-BUS sound cards.
 *
 * Speak Board: YM2608 with 256 KiB ADPCM-B RAM, six FM channels enabled
 * from reset. Spark Board adds a YM3438 at the first chip's base + 400h.
 * Port decoding and shared status wiring follow NP2kai cbus/boardspb.c:
 * https://github.com/AZO234/NP2kai/blob/master/cbus/boardspb.c
 * These cards have no PC-9801-86 PCM FIFO or A460h mode register.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/pc98_rom.h>
#include <86box/rom.h>
#include <86box/snd_pc98_opn.h>
#include <86box/sound.h>
#include "snd_pc98_joystick.h"

#define PC98_SPB_CLOCK    7987200u
#define PC98_SPB_ROM_SIZE 0x4000u

static const char *const pc98_spb_bios_paths[] = {
    "roms/sound/speakboard/sound_spb.rom",
    "roms/machines/pc9801/sound_spb.rom",
    "roms/speakboard/sound_spb.rom",
    NULL
};
static const char *const pc98_spb_rhythm_paths[] = {
    "roms/sound/yamaha/ym2608_adpcm_rom.bin",
    "roms/machines/pc9801/ym2608_adpcm_rom.bin",
    "roms/sound/speakboard/ym2608_adpcm_rom.bin",
    "roms/sound/pc9801_86/ym2608_adpcm_rom.bin",
    "roms/sound/ym2608/ym2608_adpcm_rom.bin",
    NULL
};
static const int      pc98_spb_irqs[4]      = { 3, 13, 10, 12 };
static const uint32_t pc98_spb_rom_addrs[5] = {
    0xc8000u, 0xcc000u, 0xd0000u, 0xd4000u, 0u
};

typedef struct pc98_spb_t {
    pc98_opn_t *opna;
    pc98_opn_t *extra_fm;
    void       *src;
    rom_t      *bios;
    uint8_t    *rhythm;
    uint16_t    base;
    uint8_t     address;
    uint8_t     irq_select;
    int         irq_state;
} pc98_spb_t;

static void
pc98_spb_irq(void *priv, int level)
{
    pc98_spb_t *dev = priv;
    if (level == dev->irq_state)
        return;
    dev->irq_state = level;
    if (level)
        picint(1u << pc98_spb_irqs[dev->irq_select]);
    else
        picintc(1u << pc98_spb_irqs[dev->irq_select]);
}

static uint8_t
pc98_spb_in(uint16_t port, void *priv)
{
    pc98_spb_t    *dev    = priv;
    const uint16_t offset = port - dev->base;
    if (offset & 1u)
        return 0xffu;
    const unsigned index = (offset & 7u) >> 1;
    if (offset >= 0x400u) {
        /* Spark's status ports are wired to the first chip's timers. */
        if (!(index & 1u))
            return pc98_opn_read(dev->opna, 0u) & (index ? 3u : 0xffu);
        return pc98_opn_read(dev->extra_fm, index);
    }
    /* Speak's two status ports both expose the OPNA extended status. */
    if (!(index & 1u))
        return pc98_opn_read(dev->opna, 2u);
    if ((index == 1u) && (dev->address == 0x0eu))
        pc98_opn_set_port_a(dev->opna,
                            pc98_sound_joystick(pc98_opn_get_port_b(dev->opna), dev->irq_select << 6));
    return pc98_opn_read(dev->opna, index);
}

static void
pc98_spb_out(uint16_t port, uint8_t value, void *priv)
{
    pc98_spb_t    *dev    = priv;
    const uint16_t offset = port - dev->base;
    if (offset & 1u)
        return;
    const unsigned index = (offset & 7u) >> 1;
    if (offset >= 0x400u)
        pc98_opn_write(dev->extra_fm, index, value);
    else {
        if (index == 0u)
            dev->address = value;
        pc98_opn_write(dev->opna, index, value);
    }
}

static void
pc98_spb_mix(int32_t *buffer, uint16_t len, void *priv)
{
    pc98_spb_t *dev = priv;
    pc98_opn_generate(dev->opna, buffer, len);
    if (dev->extra_fm)
        pc98_opn_generate(dev->extra_fm, buffer, len);
}

static void
pc98_spb_reset(void *priv)
{
    pc98_spb_t *dev = priv;
    pc98_opn_reset(dev->opna);
    pc98_opn_set_extended(dev->opna, 1);
    if (dev->extra_fm)
        pc98_opn_reset(dev->extra_fm);
    dev->address = 0;
    pc98_spb_irq(dev, 0);
}

static void *
pc98_spb_init(const device_t *info)
{
    pc98_spb_t *dev = calloc(1, sizeof(*dev));
    if (!dev)
        return NULL;
    dev->base       = device_get_config_int("base") ? 0x188u : 0x88u;
    dev->irq_select = device_get_config_int("irq") & 3;
    dev->opna       = pc98_opn_create(PC98_OPN_YM2608, PC98_SPB_CLOCK, pc98_spb_irq, dev);
    if (!dev->opna || !pc98_opn_set_adpcm_ram(dev->opna, 1))
        goto fail;
    if (info->local) {
        dev->extra_fm = pc98_opn_create(PC98_OPN_YM3438, PC98_SPB_CLOCK, NULL, NULL);
        if (!dev->extra_fm)
            goto fail;
    }
    dev->src = sound_source_add_stream(I_NORMAL, pc98_spb_mix, dev);
    pc98_opn_set_clock(dev->opna, sound_source_get_pos_ptr(dev->src));
    pc98_opn_set_clock(dev->extra_fm, sound_source_get_pos_ptr(dev->src));

    char path[PC98_ROM_PATH_MAX];
    if (pc98_rom_find_quiet(pc98_spb_rhythm_paths, path, sizeof(path))) {
        dev->rhythm = malloc(0x2000u);
        if (dev->rhythm && rom_load_linear(path, 0, 0x2000u, 0, dev->rhythm))
            pc98_opn_set_adpcm_rom(dev->opna, dev->rhythm, 0x2000u);
        else {
            free(dev->rhythm);
            dev->rhythm = NULL;
        }
    }
    if (!dev->rhythm)
        pclog("[PC98] %s: YM2608 rhythm ROM missing; rhythm is silent\n", info->name);

    unsigned rom_select = device_get_config_int("rom_addr");
    if (rom_select < 4 && pc98_rom_find_quiet(pc98_spb_bios_paths, path, sizeof(path))) {
        dev->bios = calloc(1, sizeof(*dev->bios));
        if (dev->bios && rom_init(dev->bios, path, pc98_spb_rom_addrs[rom_select], PC98_SPB_ROM_SIZE, PC98_SPB_ROM_SIZE - 1, 0, MEM_MAPPING_EXTERNAL)) {
            free(dev->bios);
            dev->bios = NULL;
        }
    }
    if (rom_select < 4 && !dev->bios)
        pclog("[PC98] %s: sound_spb.rom missing; direct chip access remains available\n", info->name);

    io_sethandler(dev->base, 8, pc98_spb_in, NULL, NULL, pc98_spb_out, NULL, NULL, dev);
    if (dev->extra_fm)
        io_sethandler(dev->base + 0x400u, 8, pc98_spb_in, NULL, NULL, pc98_spb_out, NULL, NULL, dev);
    pc98_spb_reset(dev);
    return dev;
fail:
    sound_source_remove(dev->src);
    pc98_opn_destroy(dev->extra_fm);
    pc98_opn_destroy(dev->opna);
    free(dev);
    return NULL;
}

static void
pc98_spb_close(void *priv)
{
    pc98_spb_t *dev = priv;
    if (!dev)
        return;
    io_removehandler(dev->base, 8, pc98_spb_in, NULL, NULL, pc98_spb_out, NULL, NULL, dev);
    if (dev->extra_fm)
        io_removehandler(dev->base + 0x400u, 8, pc98_spb_in, NULL, NULL, pc98_spb_out, NULL, NULL, dev);
    pc98_spb_irq(dev, 0);
    sound_source_remove(dev->src);
    pc98_opn_destroy(dev->opna);
    pc98_opn_destroy(dev->extra_fm);
    free(dev->rhythm);
    if (dev->bios) {
        /* Memory retains registered mapping nodes until its next reset. */
        mem_mapping_disable(&dev->bios->mapping);
        mem_mapping_set_handler(&dev->bios->mapping, NULL, NULL, NULL, NULL, NULL, NULL);
        mem_mapping_set_exec(&dev->bios->mapping, NULL);
        mem_mapping_set_p(&dev->bios->mapping, NULL);
        free(dev->bios->rom);
        dev->bios->rom = NULL;
    }
    free(dev);
}

static const device_config_t pc98_spb_config[] = {
    { .name = "base", .description = "Port base", .type = CONFIG_SELECTION, .default_int = 1, .selection = { { "188H", 1 }, { "088H", 0 }, { "" } } },
    { .name = "irq", .description = "Interrupt level", .type = CONFIG_SELECTION, .default_int = 3, .selection = { { "INT5 (IRQ 12)", 3 }, { "INT0 (IRQ 3)", 0 }, { "INT41 (IRQ 10)", 2 }, { "INT6 (IRQ 13)", 1 }, { "" } } },
    { .name = "rom_addr", .description = "Sound BIOS address", .type = CONFIG_SELECTION, .default_int = 1, .selection = { { "CC000H", 1 }, { "C8000H", 0 }, { "D0000H", 2 }, { "D4000H", 3 }, { "Disabled", 4 }, { "" } } },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t pc98_speakboard_device = {
    .name = "Speak Board", .internal_name = "pc98_speakboard", .flags = DEVICE_CBUS, .local = 0, .init = pc98_spb_init, .close = pc98_spb_close, .reset = pc98_spb_reset, .config = pc98_spb_config
};
const device_t pc98_sparkboard_device = {
    .name = "Spark Board", .internal_name = "pc98_sparkboard", .flags = DEVICE_CBUS, .local = 1, .init = pc98_spb_init, .close = pc98_spb_close, .reset = pc98_spb_reset, .config = pc98_spb_config
};
