/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Tandy VIS (Video Information System).
 *
 * Authors: Cacodemon345
 *          Carl
 *
 *          Copyright 2026 Cacodemon345.
 *          Copyright Carl
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/machine.h>
#include <86box/port_92.h>
#include <86box/pic.h>
#include <86box/sound.h>

extern int cpu_waitstates;
extern void cpu_update_waitstates(void);
extern const device_t tvis_vid_device;

static rom_t tandy_vis_rom_struct;
static rom_t tandy_vis_rom_struct_2;

static rom_t tandy_vis_rom_struct_3;
static rom_t tandy_vis_rom_struct_4;

static uint8_t
tvis_unk2_read(uint16_t addr, void* priv)
{
    return 0x40;
}

static uint8_t tvis_chipset_regs[0x100];
static uint8_t tvis_chipset_index;

static uint8_t
tvis_unk_read(uint16_t addr, void* priv)
{
    return (addr & 1) ? tvis_chipset_regs[tvis_chipset_index] : 0;
}

static void
tvis_unk_write(uint16_t addr, uint8_t val, void* priv)
{
    if (addr & 1)
        tvis_chipset_regs[tvis_chipset_index] = val;
    else
        tvis_chipset_index = val & 0xf;
}

static uint8_t
tvis_unk1_read(uint16_t addr, void* priv)
{
    return ((addr & 3) == 2) ? 0xde : 0x00;
}

static void
tvis_unk1_write(uint16_t addr, uint8_t val, void* priv)
{
    if ((addr & 3) == 1) {
        if (val == 0x10)
            picintc(1 << 9);
        else if (val == 0x16)
            picint(1 << 9);
    }
}

/* Handheld controller ("pad") interface at 0x268-0x26B, IRQ 3.

   The VIS has no keyboard controller that the firmware uses: KEYBOARD.DRV in
   ROM hooks IRQ 3, reads the button state word from 0x268 and turns every
   changed button into a keystroke (controller 1 = VK_F1-VK_F10, controller 2 =
   VK_F11-VK_F20). Reading 0x268 alternates between controller 1 (bit 10 clear)
   and controller 2 (bit 10 set); 0x26A bit 7 means new data is pending. The
   BIOS pad driver probes for the receiver by expecting bit 10 to toggle.

   Host keys are mapped onto controller 1. */
#define TVIS_PAD_DOWN  0x001
#define TVIS_PAD_1     0x002
#define TVIS_PAD_A     0x004
#define TVIS_PAD_2     0x008
#define TVIS_PAD_4     0x010
#define TVIS_PAD_B     0x020
#define TVIS_PAD_3     0x040
#define TVIS_PAD_LEFT  0x080
#define TVIS_PAD_UP    0x100
#define TVIS_PAD_RIGHT 0x200
#define TVIS_PAD_SEL2  0x400

typedef struct tvis_pad_t {
    uint16_t buttons;
    uint8_t  key_down[512];
    uint8_t  stat;
    uint8_t  ctl;
    uint8_t  sel;
    uint16_t last;
} tvis_pad_t;

static uint16_t
tvis_pad_key_to_button(uint16_t scan)
{
    switch (scan) {
        case 0x148: /* Up */
        case 0x048: /* Keypad 8 */
            return TVIS_PAD_UP;
        case 0x150: /* Down */
        case 0x050: /* Keypad 2 */
            return TVIS_PAD_DOWN;
        case 0x14b: /* Left */
        case 0x04b: /* Keypad 4 */
            return TVIS_PAD_LEFT;
        case 0x14d: /* Right */
        case 0x04d: /* Keypad 6 */
            return TVIS_PAD_RIGHT;
        case 0x01c: /* Enter */
        case 0x11c: /* Keypad Enter */
        case 0x039: /* Space */
        case 0x02c: /* Z */
            return TVIS_PAD_A;
        case 0x001: /* Esc */
        case 0x00e: /* Backspace */
        case 0x02d: /* X */
            return TVIS_PAD_B;
        case 0x002: /* 1 */
            return TVIS_PAD_1;
        case 0x003: /* 2 */
            return TVIS_PAD_2;
        case 0x004: /* 3 */
            return TVIS_PAD_3;
        case 0x005: /* 4 */
            return TVIS_PAD_4;
        default:
            return 0;
    }
}

static void
tvis_pad_key(uint16_t scan, int down, void *priv)
{
    tvis_pad_t *dev = (tvis_pad_t *) priv;
    uint16_t    buttons = 0;

    if (!tvis_pad_key_to_button(scan & 0x1ff))
        return;

    dev->key_down[scan & 0x1ff] = !!down;

    /* Several host keys can map to the same button. */
    for (int i = 0; i < 512; i++) {
        if (dev->key_down[i])
            buttons |= tvis_pad_key_to_button(i);
    }

    if (buttons != dev->buttons) {
        dev->buttons = buttons;
        dev->stat    = 0x80;
        dev->sel     = 0;
        picint(1 << 3);
    }
}

static uint16_t
tvis_pad_readw(uint16_t addr, void *priv)
{
    tvis_pad_t *dev = (tvis_pad_t *) priv;
    uint16_t    ret = 0x0000;

    switch (addr & 3) {
        case 0:
            if (!dev->sel) {
                ret       = dev->buttons;
                dev->stat = 0x00;
                dev->sel  = 1;
            } else {
                /* Controller 2, not connected. */
                ret      = TVIS_PAD_SEL2;
                dev->sel = 0;
            }
            dev->last = ret;
            picintc(1 << 3);
            break;
        case 2:
            ret = dev->stat;
            break;
        default:
            break;
    }

    return ret;
}

static uint8_t
tvis_pad_read(uint16_t addr, void *priv)
{
    tvis_pad_t *dev = (tvis_pad_t *) priv;

    switch (addr & 3) {
        case 0:
            return tvis_pad_readw(addr, priv) & 0xff;
        case 1:
            return dev->last >> 8;
        case 2:
            return dev->stat;
        default:
            return 0x00;
    }
}

static void
tvis_pad_write(uint16_t addr, uint8_t val, void *priv)
{
    tvis_pad_t *dev = (tvis_pad_t *) priv;

    if ((addr & 3) == 2)
        dev->ctl = val;
}

static void
tvis_pad_writew(uint16_t addr, uint16_t val, void *priv)
{
    tvis_pad_write(addr, val & 0xff, priv);
}

static void *
tvis_init(const device_t *info)
{
    tvis_pad_t *dev = (tvis_pad_t *) calloc(1, sizeof(tvis_pad_t));

    io_sethandler(0x268, 4, tvis_pad_read, tvis_pad_readw, NULL, tvis_pad_write, tvis_pad_writew, NULL, dev);

    keyboard_scan = 1;
    keyboard_set_input_handler(tvis_pad_key, dev);

    return dev;
}

static void
tvis_close(void *priv)
{
    free(tandy_vis_rom_struct.rom);
    free(tandy_vis_rom_struct_2.rom);
    free(tandy_vis_rom_struct_3.rom);
    free(tandy_vis_rom_struct_4.rom);
    memset(&tandy_vis_rom_struct, 0, sizeof(tandy_vis_rom_struct));
    memset(&tandy_vis_rom_struct_2, 0, sizeof(tandy_vis_rom_struct));
    memset(&tandy_vis_rom_struct_3, 0, sizeof(tandy_vis_rom_struct));
    memset(&tandy_vis_rom_struct_4, 0, sizeof(tandy_vis_rom_struct));
    keyboard_set_input_handler(NULL, NULL);
    free(priv);
}

static void
tvis_reset(void *priv)
{
    tvis_pad_t *dev = (tvis_pad_t *) priv;

    memset(tvis_chipset_regs, 0, sizeof(tvis_chipset_regs));

    dev->buttons = 0;
    dev->stat    = 0;
    dev->ctl     = 0;
    dev->sel     = 0;
    dev->last    = 0;
    memset(dev->key_down, 0, sizeof(dev->key_down));
    picintc(1 << 3);
}

static const device_config_t tvis_config[] = {
    // clang-format off
    {
        .name           = "memcard",
        .description    = "Memory card inserted",
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

const device_t tvis_device = {
    .name          = "Tandy VIS",
    .internal_name = "tvis",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = tvis_init,
    .close         = tvis_close,
    .reset         = tvis_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = tvis_config
};

int
machine_at_tandy_vis_init(const machine_t *model)
{
    int ret;

    if (bios_only)
        ret = bios_load_linear("roms/machines/tvis/p513bk0b.bin", 0x300000, 0x80000, 0) && bios_load_aux_linear("roms/machines/tvis/p513bk1b.bin", 0x380000, 0x80000, 0);
    else {
        ret = !rom_init(&tandy_vis_rom_struct, "roms/machines/tvis/p513bk0b.bin", 0x300000, 0x80000, 0x7ffff, 0, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS);
        ret = ret && !rom_init(&tandy_vis_rom_struct_2, "roms/machines/tvis/p513bk1b.bin", 0x380000, 0x80000, 0x7ffff, 0, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS);

        bios_load_linear("roms/machines/tvis/p513bk1b.bin", 0x380000, 0x80000, 0);

        ret = ret && !rom_init(&tandy_vis_rom_struct_3, "roms/machines/tvis/p513bk1b.bin", 0xd8000, 0x28000, 0x7ffff, 0xd8000 - 0x80000, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS);
        ret = ret && !rom_init(&tandy_vis_rom_struct_4, "roms/machines/tvis/p513bk1b.bin", 0xff0000, 0x10000, 0xffff, 0x70000, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS);

        mem_set_mem_state_both(0xff0000, 0x10000, MEM_READ_ROMCS | MEM_WRITE_ROMCS);
        mem_set_mem_state_both(0x300000, 0x100000, MEM_READ_ROMCS | MEM_WRITE_ROMCS);
        mem_set_mem_state_both(0xd8000, 0x28000, MEM_READ_ROMCS | MEM_WRITE_ROMCS);
    }

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&port_92_device);
    device_add_params(&sensationaud_device, (void *) SENSATION_TANDY_VIS);

    memset(tvis_chipset_regs, 0, sizeof(tvis_chipset_regs));
    io_sethandler(0x26, 2, tvis_unk_read, NULL, NULL, tvis_unk_write, NULL, NULL, (void*)1);
    io_sethandler(0x6a, 1, tvis_unk2_read, NULL, NULL, NULL, NULL, NULL, (void*)1);
    io_sethandler(0x23c, 4, tvis_unk1_read, NULL, NULL, tvis_unk1_write, NULL, NULL, (void*)1);
    device_add(&tvis_device);
    device_add(&tvis_memcard_device);

    device_add(&tvis_vid_device);

    cpu_waitstates = 1;
    cpu_update_waitstates();

    return ret;
}
