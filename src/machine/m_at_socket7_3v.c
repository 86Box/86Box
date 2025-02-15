/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Socket 7 (Single Voltage) machines.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2020 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/rom.h>
#include <86box/pci.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/keyboard.h>
#include <86box/flash.h>
#include <86box/sio.h>
#include <86box/hwm.h>
#include <86box/video.h>
#include <86box/spd.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/nvr.h>
#include <86box/plat_unused.h>
#include <86box/sound.h>

static void
machine_at_thor_gpio_init(void)
{
    uint32_t gpio = 0xffffe1cf;

    /* Register 0x0078 (Undocumented): */
    /* Bit 5: 0 = Multiplier. */
    /* Bit 4: 0 = Multiplier. */
    /*        1.5: 0, 0. */
    /*        3.0: 0, 1. */
    /*        2.0: 1, 0. */
    /*        2.5: 1, 1. */
    /* Bit 1: 0 = Error beep, 1 = No error. */
    if (cpu_dmulti <= 1.5)
        gpio |= 0xffff0000;
    else if ((cpu_dmulti > 1.5) && (cpu_dmulti <= 2.0))
        gpio |= 0xffff0020;
    else if ((cpu_dmulti > 2.0) && (cpu_dmulti <= 2.5))
        gpio |= 0xffff0030;
    else if (cpu_dmulti > 2.5)
        gpio |= 0xffff0010;

    /* Register 0x0079: */
    /* Bit 7: 0 = Clear password, 1 = Keep password. */
    /* Bit 6: 0 = NVRAM cleared by jumper, 1 = NVRAM normal. */
    /* Bit 5: 0 = CMOS Setup disabled, 1 = CMOS Setup enabled. */
    /* Bit 4: External CPU clock (Switch 8). */
    /* Bit 3: External CPU clock (Switch 7). */
    /*        50 MHz: Switch 7 = Off, Switch 8 = Off. */
    /*        60 MHz: Switch 7 = On, Switch 8 = Off. */
    /*        66 MHz: Switch 7 = Off, Switch 8 = On. */
    /* Bit 2: 0 = On-board audio absent, 1 = On-board audio present. */
    /* Bit 1: 0 = Soft-off capable power supply present, 1 = Soft-off capable power supply absent. */
    /* Bit 0: 0 = Reserved. */
    /* NOTE: A bit is read as 1 if switch is off, and as 0 if switch is on. */
    if (cpu_busspeed <= 50000000)
        gpio |= 0xffff0000;
    else if ((cpu_busspeed > 50000000) && (cpu_busspeed <= 60000000))
        gpio |= 0xffff0800;
    else if (cpu_busspeed > 60000000)
        gpio |= 0xffff1000;

    machine_set_gpio_default(gpio);
}

static void
machine_at_thor_common_init(const machine_t *model, int has_video)
{
    machine_at_common_init_ex(model, 2);
    machine_at_thor_gpio_init();

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       4, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 2, 1);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      4, 3, 2, 1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    if (has_video && (gfxcard[0] == VID_INTERNAL))
        device_add(machine_get_vid_device(machine));

    device_add(&keyboard_ps2_intel_ami_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_ami_device);
}

static void
machine_at_p54tp4xe_common_init(const machine_t *model)
{
    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&fdc37c665_device);
    device_add(&intel_flash_bxt_device);
}

int
machine_at_p54tp4xe_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p54tp4xe/t15i0302.awd",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_p54tp4xe_common_init(model);

    return ret;
}

int
machine_at_p54tp4xe_mr_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p54tp4xe/TRITON.BIO",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_p54tp4xe_common_init(model);

    return ret;
}

int
machine_at_exp8551_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/exp8551/AMI20.BIO",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x14, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&w83787f_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_gw2katx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/gw2katx/1003CN0T.BIO",
                                    "roms/machines/gw2katx/1003CN0T.BI1",
                                    0x20000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_thor_common_init(model, 0);

    return ret;
}

int
machine_at_thor_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/thor/1006cn0_.bio",
                                    "roms/machines/thor/1006cn0_.bi1",
                                    0x20000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_thor_common_init(model, 1);

    return ret;
}

int
machine_at_mrthor_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/mrthor/mr_atx.bio",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_thor_common_init(model, 0);

    return ret;
}

static void
machine_at_endeavor_gpio_init(void)
{
    uint32_t gpio = 0xffffe0cf;
    uint16_t addr;

    /* Register 0x0078 (Undocumented): */
    /* Bit 5,4: Vibra 16S base address: 0 = 220h, 1 = 260h, 2 = 240h, 3 = 280h. */
    device_context(machine_get_snd_device(machine));
    addr = device_get_config_hex16("base");
    switch (addr) {
        case 0x0220:
            gpio |= 0xffff00cf;
            break;
        case 0x0240:
            gpio |= 0xffff00ef;
            break;
        case 0x0260:
            gpio |= 0xffff00df;
            break;
        case 0x0280:
            gpio |= 0xffff00ff;
            break;
    }
    device_context_restore();

    /* Register 0x0079: */
    /* Bit 7: 0 = Clear password, 1 = Keep password. */
    /* Bit 6: 0 = NVRAM cleared by jumper, 1 = NVRAM normal. */
    /* Bit 5: 0 = CMOS Setup disabled, 1 = CMOS Setup enabled. */
    /* Bit 4: External CPU clock (Switch 8). */
    /* Bit 3: External CPU clock (Switch 7). */
    /*        50 MHz: Switch 7 = Off, Switch 8 = Off. */
    /*        60 MHz: Switch 7 = On, Switch 8 = Off. */
    /*        66 MHz: Switch 7 = Off, Switch 8 = On. */
    /* Bit 2: 0 = On-board audio absent, 1 = On-board audio present. */
    /* Bit 1: 0 = Soft-off capable power supply present, 1 = Soft-off capable power supply absent. */
    /* Bit 0: 0 = 2x multiplier, 1 = 1.5x multiplier (Switch 6). */
    /* NOTE: A bit is read as 1 if switch is off, and as 0 if switch is on. */
    if (cpu_busspeed <= 50000000)
        gpio |= 0xffff0000;
    else if ((cpu_busspeed > 50000000) && (cpu_busspeed <= 60000000))
        gpio |= 0xffff0800;
    else if (cpu_busspeed > 60000000)
        gpio |= 0xffff1000;

    if (sound_card_current[0] == SOUND_INTERNAL)
        gpio |= 0xffff0400;

    if (cpu_dmulti <= 1.5)
        gpio |= 0xffff0100;
    else
        gpio |= 0xffff0000;

    machine_set_gpio_default(gpio);
}

uint32_t
machine_at_endeavor_gpio_handler(uint8_t write, uint32_t val)
{
    uint32_t ret = machine_get_gpio_default();

    if (write) {
        ret &= ((val & 0xffffffcf) | 0xffff0000);
        ret |= (val & 0x00000030);
        if (machine_snd != NULL)  switch ((val >> 4) & 0x03) {
            case 0x00:
                sb_vibra16s_onboard_relocate_base(0x0220, machine_snd);
                break;
            case 0x01:
                sb_vibra16s_onboard_relocate_base(0x0260, machine_snd);
                break;
            case 0x02:
                sb_vibra16s_onboard_relocate_base(0x0240, machine_snd);
                break;
            case 0x03:
                sb_vibra16s_onboard_relocate_base(0x0280, machine_snd);
                break;
        }
        machine_set_gpio(ret);
    } else
        ret = machine_get_gpio();

    return ret;
}

int
machine_at_endeavor_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/endeavor/1006cb0_.bio",
                                    "roms/machines/endeavor/1006cb0_.bi1",
                                    0x1d000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);
    machine_at_endeavor_gpio_init();

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       4, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    if (sound_card_current[0] == SOUND_INTERNAL)
        machine_snd = device_add(machine_get_snd_device(machine));

    device_add(&keyboard_ps2_intel_ami_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

int
machine_at_ms5119_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ms5119/A37EB.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0d, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0e, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0f, PCI_CARD_NORMAL,      3, 4, 1, 2);

    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&w83787f_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

static void
machine_at_pb640_gpio_init(void)
{
    uint32_t gpio = 0xffffe6ff;

    /* Register 0x0079: */
    /* Bit 7: 0 = Clear password, 1 = Keep password. */
    /* Bit 6: 0 = NVRAM cleared by jumper, 1 = NVRAM normal. */
    /* Bit 5: 0 = CMOS Setup disabled, 1 = CMOS Setup enabled. */
    /* Bit 4: External CPU clock (Switch 8). */
    /* Bit 3: External CPU clock (Switch 7). */
    /*        50 MHz: Switch 7 = Off, Switch 8 = Off. */
    /*        60 MHz: Switch 7 = On, Switch 8 = Off. */
    /*        66 MHz: Switch 7 = Off, Switch 8 = On. */
    /* Bit 2: No Connect. */
    /* Bit 1: No Connect. */
    /* Bit 0: 2x multiplier, 1 = 1.5x multiplier (Switch 6). */
    /* NOTE: A bit is read as 1 if switch is off, and as 0 if switch is on. */
    if (cpu_busspeed <= 50000000)
        gpio |= 0xffff00ff;
    else if ((cpu_busspeed > 50000000) && (cpu_busspeed <= 60000000))
        gpio |= 0xffff08ff;
    else if (cpu_busspeed > 60000000)
        gpio |= 0xffff10ff;

    if (cpu_dmulti <= 1.5)
        gpio |= 0xffff01ff;
    else
        gpio |= 0xffff00ff;

    machine_set_gpio_default(gpio);
}

int
machine_at_pb640_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/pb640/1007CP0R.BIO",
                                    "roms/machines/pb640/1007CP0R.BI1", 0x1d000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);
    machine_at_pb640_gpio_init();

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       4, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 1, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      3, 2, 1, 4);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i430fx_rev02_device);
    device_add(&piix_rev02_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&gd5440_onboard_pci_device);

    device_add(&keyboard_ps2_intel_ami_pci_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

int
machine_at_mb500n_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/mb500n/031396s.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x14, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&keyboard_ps2_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_no_mirq_device);
    device_add(&fdc37c665_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_fmb_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/fmb/P5IV183.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 2, 1);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      4, 3, 2, 1);

    device_add(&i430fx_device);
    device_add(&piix_no_mirq_device);
    device_add(&keyboard_at_ami_device);
    device_add(&w83787f_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_acerm3a_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/acerm3a/r01-b3.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x10, PCI_CARD_VIDEO,       4, 0, 0, 0);
    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add(&fdc37c935_device);

    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_ap53_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ap53/ap53r2c0.rom",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_VIDEO,       1, 2, 3, 4);
    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&fdc37c669_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_8500tuc_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/8500tuc/Tuc0221b.rom",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&um8669f_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_p55t2s_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p55t2s/s6y08t.rom",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_p5vxb_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p5vxb/P5VXB10.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x05, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x06, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 4);
    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&w83877f_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_gw2kma_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined2("roms/machines/gw2kma/1007DQ0T.BIO",
                                     "roms/machines/gw2kma/1007DQ0T.BI1",
                                     "roms/machines/gw2kma/1007DQ0T.BI2",
                                     "roms/machines/gw2kma/1007DQ0T.BI3",
                                     "roms/machines/gw2kma/1007DQ0T.RCV",
                                     0x3a000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 4);
    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add(&fdc37c932fr_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

int
machine_at_ap5s_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ap5s/AP5S150.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&sis_5511_device);
    device_add(&keyboard_ps2_ami_device);
    device_add(&fdc37c665_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_ms5124_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ms5124/AG77.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0xFE, 0xFF, 0, 0);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);

    device_add(&sis_5511_device);
    device_add(&keyboard_ps2_ami_device);
    device_add(&w83787f_88h_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_amis727_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/amis727/S727p.rom",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0xFE, 0xFF, 0, 0);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&sis_5511_device);
    device_add(&keyboard_ps2_intel_ami_pci_device);
    device_add(&fdc37c665_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_vectra54_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/vectra54/GT0724.22",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0F, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_VIDEO,       0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      3, 4, 1, 2);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&s3_phoenix_trio64_onboard_pci_device);

    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&fdc37c932_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_5sbm2_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/5sbm2/5SBM0717.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      3, 4, 1, 2);

    device_add(&keyboard_at_ami_device);
    device_add(&sis_550x_device);
    device_add(&um8663af_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_pc140_6260_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pc140_6260/LYKT32A.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x14, PCI_CARD_VIDEO,       0, 0, 0, 0); /* Onboard video */

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&gd5436_onboard_pci_device);

    device_add(&sis_5511_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&fdc37c669_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}
