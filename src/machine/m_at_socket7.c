/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Socket 7 (Dual Voltage) machines.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2025 Miran Grca.
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
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/keyboard.h>
#include <86box/flash.h>
#include <86box/sio.h>
#include <86box/sound.h>
#include <86box/hwm.h>
#include <86box/video.h>
#include <86box/spd.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/nvr.h>
#include <86box/scsi_ncr53c8xx.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/pci.h>

/* i430HX */
int
machine_at_acerm3a_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/acerm3a/r01-b3.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

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
    device_add_params(&fdc37c93x_device, (void *) (FDC37XXX5 | FDC37C93X_NORMAL));

    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_p55t2p4_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p55t2p4/0207_j2.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&w83877_device, (void *) (W83877F | W83877_3F0));
    device_add(&intel_flash_bxt_device);

    return ret;
}

void
machine_at_p65up5_common_init(const machine_t *model, const device_t *northbridge)
{
    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(northbridge);
    device_add(&piix3_ioapic_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&w83877_device, (void *) (W83877F | W83877_3F0));
    device_add(&sst_flash_29ee010_device);
    device_add(&ioapic_device);
}

int
machine_at_p65up5_cp55t2d_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p65up5/TD5I0201.AWD",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_p65up5_common_init(model, &i430hx_device);

    return ret;
}

static void
machine_at_rubyusb_gpio_init(void)
{
    uint32_t gpio = 0xffffe3ff;

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
        gpio |= 0xffff10ff;
    else if ((cpu_busspeed > 50000000) && (cpu_busspeed <= 60000000))
        gpio |= 0xffff18ff;
    else if (cpu_busspeed > 60000000)
        gpio |= 0xffff00ff;

    if (sound_card_current[0] == SOUND_INTERNAL)
        gpio |= 0xffff04ff;

    machine_set_gpio_default(gpio);
}

int
machine_at_rubyusb_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined2("roms/machines/rubyusb/1005DL0L.BIO",
                                     "roms/machines/rubyusb/1005DL0L.BI1",
                                     "roms/machines/rubyusb/1005DL0L.BI2",
                                     "roms/machines/rubyusb/1005DL0L.BI3",
                                     "roms/machines/rubyusb/1005DL0L.RCV",
                                     /*NULL,*/
                                     0x3a000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    machine_at_rubyusb_gpio_init();

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       4, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    if (sound_card_current[0] == SOUND_INTERNAL)
        machine_snd = device_add(machine_get_snd_device(machine));

    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add_params(&pc87306_device, (void *) PCX730X_AMI);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

static const device_config_t cu430hx_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "cu430hx",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Intel AMIBIOS - Revision 1.00.03.DK08 (Toshiba Equium 5xx0D)",
                .internal_name = "equium5200",
                .bios_type     = BIOS_NORMAL, 
                .files_no      = 5,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/cu430hx/1003DK08.BIO", "roms/machines/cu430hx/1003DK08.BI1",
                                   "roms/machines/cu430hx/1003DK08.BI2", "roms/machines/cu430hx/1003DK08.BI3",
                                   "roms/machines/cu430hx/1003DK08.RCV", "" } },
            {
                .name          = "Intel AMIBIOS - Revision 1.00.04.DK0K (NEC PowerMate V2xxx/P2xxx)",
                .internal_name = "powermatev2p2",
                .bios_type     = BIOS_NORMAL, 
                .files_no      = 5,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/cu430hx/1004DK0K.BIO", "roms/machines/cu430hx/1004DK0K.BI1",
                                   "roms/machines/cu430hx/1004DK0K.BI2", "roms/machines/cu430hx/1004DK0K.BI3",
                                   "roms/machines/cu430hx/1004DK0K.RCV", "" }
            },
            {
                .name          = "Intel AMIBIOS - Revision 1.00.06.DK0",
                .internal_name = "cu430hx",
                .bios_type     = BIOS_NORMAL, 
                .files_no      = 5,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/cu430hx/1006DK0_.BIO", "roms/machines/cu430hx/1006DK0_.BI1",
                                   "roms/machines/cu430hx/1006DK0_.BI2", "roms/machines/cu430hx/1006DK0_.BI3",
                                   "roms/machines/cu430hx/1006DK0_.RCV", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t cu430hx_device = {
    .name          = "Intel CU430HX (Cumberland)",
    .internal_name = "cu430hx_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = cu430hx_config
};

static void
machine_at_cu430hx_gpio_init(void)
{
    uint32_t gpio = 0xffffe1ff;

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
        gpio |= 0xffff10ff;
    else if ((cpu_busspeed > 50000000) && (cpu_busspeed <= 60000000))
        gpio |= 0xffff18ff;
    else if (cpu_busspeed > 60000000)
        gpio |= 0xffff00ff;

    if ((sound_card_current[0] == SOUND_INTERNAL) && machine_get_snd_device(machine)->available())
        gpio |= 0xffff04ff;

    machine_set_gpio_default(gpio);
}

int
machine_at_cu430hx_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn[5];

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    for (int i = 0; i < 5; i++)
        fn[i] = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), i);
    ret = bios_load_linear_combined2(fn[0], fn[1], fn[2], fn[3], fn[4], 0x3a000, 128);
    device_context_restore();

    machine_at_common_init(model);
    machine_at_cu430hx_gpio_init();

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       4, 0, 0, 0); // ATI VGA Graphics
    pci_register_slot(0x0C, PCI_CARD_NETWORK,     4, 0, 0, 0); // Intel 82557 Ethernet Network
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 0, 0, 0); // riser

    if ((sound_card_current[0] == SOUND_INTERNAL) && machine_get_snd_device(machine)->available())
        machine_snd = device_add(machine_get_snd_device(machine));

    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add_params(&pc87306_device, (void *) PCX730X_AMI);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

static const device_config_t tc430hx_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "tc430hx",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Intel AMIBIOS - Revision 1.00.07.DH0",
                .internal_name = "tc430hx",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 5,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/tc430hx/1007DH0_.BIO", "roms/machines/tc430hx/1007DH0_.BI1",
                                   "roms/machines/tc430hx/1007DH0_.BI2", "roms/machines/tc430hx/1007DH0_.BI3",
                                   "roms/machines/tc430hx/1007DH0_.RCV", "" }
            },
            {
                .name          = "Intel AMIBIOS - Revision 1.00.08.DH08 (Toshiba Infinia 7xx1)",
                .internal_name = "infinia7200",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 5,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/tc430hx/1008DH08.BIO", "roms/machines/tc430hx/1008DH08.BI1",
                                   "roms/machines/tc430hx/1008DH08.BI2", "roms/machines/tc430hx/1008DH08.BI3",
                                   "roms/machines/tc430hx/1008DH08.RCV", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t tc430hx_device = {
    .name          = "Intel TC430HX (Tucson)",
    .internal_name = "tc430hx_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = tc430hx_config
};

static void
machine_at_tc430hx_gpio_init(void)
{
    uint32_t gpio = 0xffffe1ff;

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
        gpio |= 0xffff10ff;
    else if ((cpu_busspeed > 50000000) && (cpu_busspeed <= 60000000))
        gpio |= 0xffff18ff;
    else if (cpu_busspeed > 60000000)
        gpio |= 0xffff00ff;

    if (sound_card_current[0] == SOUND_INTERNAL)
        gpio |= 0xffff04ff;

    machine_set_gpio_default(gpio);
}

int
machine_at_tc430hx_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn[5];

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    for (int i = 0; i < 5; i++)
        fn[i] = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), i);
    ret = bios_load_linear_combined2(fn[0], fn[1], fn[2], fn[3], fn[4], 0x3a000, 128);
    device_context_restore();

    machine_at_common_init(model);
    machine_at_tc430hx_gpio_init();

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

    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add_params(&pc87306_device, (void *) PCX730X_AMI);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

static const device_config_t m7shi_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "m7shi",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "PhoenixBIOS 4.0 Release 6.0 - Revision 05/20/97",
                .internal_name = "m7shi_97",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/m7shi/m7shi2n.rom", "" }
            },
            {
                .name          = "PhoenixBIOS 4.0 Release 6.0 - Revision 01/21/98",
                .internal_name = "m7shi",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/m7shi/M7ns04.rom", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t m7shi_device = {
    .name          = "Micronics M7S-Hi",
    .internal_name = "m7shi_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = m7shi_config
};

int
machine_at_m7shi_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000c0000, 262144, 0);
    device_context_restore();

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);

    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add_params(&fdc37c93x_device, (void *) (FDC37XXX5 | FDC37C93X_NORMAL));
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_epc2102_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/epc2102/P5000HX.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&i82091aa_device, (void *) I82091AA_022);
    device_add(&radisys_config_device);
    device_add(&sst_flash_39sf010_device);

    return ret;
}

/* The Sony VAIO is an AG430HX, I'm assuming it has the same configuration bits
   as the TC430HX, hence the #define. */
#define machine_at_ag430hx_gpio_init machine_at_tc430hx_gpio_init

int
machine_at_pcv90_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined2("roms/machines/pcv90/1010DD04.BIO",
                                     "roms/machines/pcv90/1010DD04.BI1",
                                     "roms/machines/pcv90/1010DD04.BI2",
                                     "roms/machines/pcv90/1010DD04.BI3",
                                     "roms/machines/pcv90/1010DD04.RCV",
                                     0x3a000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    machine_at_ag430hx_gpio_init();

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       4, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add_params(&pc87306_device, (void *) PCX730X_AMI);
    device_add(&intel_flash_bxt_ami_device);

    if (sound_card_current[0] == SOUND_INTERNAL)
        machine_snd = device_add(machine_get_snd_device(machine));

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

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add_params(&pc87306_device, (void *) PCX730X_AMI);
    device_add(&intel_flash_bxt_device);

    return ret;
}

/* i430VX */
int
machine_at_ap5vm_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ap5vm/AP5V270.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    /* It seems there were plans for an on-board NCR 53C810 according to some clues
       left in the manual, but were latter scrapped. The BIOS still support that
       PCI device, though, so why not. */
    pci_register_slot(0x06, PCI_CARD_SCSI,        1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&ncr53c810_onboard_pci_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_p55tvp4_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p55tvp4/0204_128.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&w83877_device, (void *) (W83877F | W83877_3F0));
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_5ivg_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/5ivg/5IVG.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&gm82c803c_device, (void *) 0);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_8500tvxa_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/8500tvxa/tvx0619b.rom",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 2, 1);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 3, 2, 1);

    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&um8669f_device, (void *) 0);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_presario2240_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/presario2240/B0184008.ROM",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_NO_BRIDGES);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x14, PCI_CARD_VIDEO,       3, 0, 0, 0);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      1, 2, 3, 4);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&s3_trio64v2_dx_onboard_pci_device);

    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add_params(&fdc37c93x_device, (void *) (FDC37XXX2 | FDC37C93X_NORMAL));
    device_add(&sst_flash_29ee020_device);

    return ret;
}

int
machine_at_presario4500_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/presario4500/B013300I.ROM",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_NO_BRIDGES);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x14, PCI_CARD_VIDEO,       3, 0, 0, 0);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      1, 2, 3, 4);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&s3_trio64v2_dx_onboard_pci_device);

    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add_params(&fdc37c93x_device, (void *) (FDC37XXX1 | FDC37C93X_APM));
    device_add(&sst_flash_29ee020_device);

    return ret;
}

int
machine_at_dellhannibalp_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined2("roms/machines/dellhannibalp/1003DY0J.BIO",
                                     "roms/machines/dellhannibalp/1003DY0J.BI1",
                                     "roms/machines/dellhannibalp/1003DY0J.BI2",
                                     "roms/machines/dellhannibalp/1003DY0J.BI3",
                                     "roms/machines/dellhannibalp/1003DY0J.RCV",
                                     0x3a000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       4, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 4);

    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add_params(&fdc37c93x_device, (void *) (FDC37XXX2 | FDC37C93X_FR));
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

static const device_config_t p5vxb_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "p5vxb",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Award Modular BIOS v4.50PG - Revision 1.0",
                .internal_name = "p5vxb",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/p5vxb/P5VXB10.BIN", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 1.5c",
                .internal_name = "p5vxb_451pg",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/p5vxb/P5VXB15C.BIN", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t p5vxb_device = {
    .name          = "ECS P5VX-B",
    .internal_name = "p5vxb_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = p5vxb_config
};

int
machine_at_p5vxb_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    device_context_restore();

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
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&w83877_device, (void *) (W83877F | W83877_3F0));
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_p55va_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p55va/va021297.bin",
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
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add_params(&fdc37c93x_device, (void *) (FDC37XXX2 | FDC37C93X_FR));
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_gw2kte_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined2("roms/machines/gw2kte/1008CY1T.BIO",
                                     "roms/machines/gw2kte/1008CY1T.BI1",
                                     "roms/machines/gw2kte/1008CY1T.BI2",
                                     "roms/machines/gw2kte/1008CY1T.BI3",
                                     "roms/machines/gw2kte/1008CY1T.RCV",
                                     0x3a000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       4, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 4);

    if ((sound_card_current[0] == SOUND_INTERNAL) && machine_get_snd_device(machine)->available())
        machine_snd = device_add(machine_get_snd_device(machine));

    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add_params(&fdc37c93x_device, (void *) (FDC37XXX2 | FDC37C93X_FR));
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

int
machine_at_brio80xx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/brio80xx/Hf0705.rom",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x14, PCI_CARD_VIDEO,       4, 0, 0, 0);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);

    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add_params(&fdc37c93x_device, (void *) (FDC37XXX5 | FDC37C93X_NORMAL | FDC37XXXX_370));
    device_add(&sst_flash_29ee020_device);

    return ret;
}

static const device_config_t lgibmx52_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "lgibmx52",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "PhoenixBIOS 4.05 - Revision 08/21/97",
                .internal_name = "lgibmx52_082197",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/lgibmx52/BIOS.ROM", "" }
            },
            {
                .name          = "PhoenixBIOS 4.05 - Revision 03/26/99",
                .internal_name = "lgibmx52",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/lgibmx52/MS5136 LG IBM OEM.ROM", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t lgibmx52_device = {
    .name          = "LG IBM Multinet x52 (MSI MS-5136)",
    .internal_name = "lgibmx52_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = lgibmx52_config
};

int
machine_at_lgibmx52_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    device_context_restore();

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&w83877_device, (void *) (W83877F | W83877_3F0));
    device_add(&winbond_flash_w29c010_device);

    return ret;
}

/* The PB680 is a NV430VX, I'm assuming it has the same configuration bits as
   the TC430HX, hence the #define. */
#define machine_at_nv430vx_gpio_init machine_at_tc430hx_gpio_init

int
machine_at_pb680_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined2("roms/machines/pb680/1012DN0R.BIO",
                                     "roms/machines/pb680/1012DN0R.BI1",
                                     "roms/machines/pb680/1012DN0R.BI2",
                                     "roms/machines/pb680/1012DN0R.BI3",
                                     "roms/machines/pb680/1012DN0R.RCV",
                                     0x3a000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    machine_at_nv430vx_gpio_init();

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       4, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add_params(&pc87306_device, (void *) PCX730X_AMI);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

int
machine_at_pb810_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pb810/G400125I.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 1, 3, 4);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 3, 2, 4);
    pci_register_slot(0x0b, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    if (sound_card_current[0] == SOUND_INTERNAL)
        machine_snd = device_add(machine_get_snd_device(machine));

    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add_params(&fdc37c93x_device, (void *) (FDC37XXX5 | FDC37C93X_NORMAL | FDC37XXXX_370));
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_mb520n_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/mb520n/520n503s.rom",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c669_device, (void *) 0);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_i430vx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/430vx/55XWUQ0E.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&um8669f_device, (void *) 0);
    device_add(&intel_flash_bxt_device);

    return ret;
}

/* i430TX */
int
machine_at_nupro592_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/nupro592/np590b10.bin",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0B, PCI_CARD_VIDEO,       3, 4, 1, 2); /* C&T B69000 */
    pci_register_slot(0x0C, PCI_CARD_NETWORK,     4, 1, 2, 3); /* Intel 82559 */
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 4); /* PIIX4 */

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add_params(&w83977_device, (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 128);
    device_add(&w83781d_device);    /* fans: CPU1, unused, unused; temperatures: System, CPU1, unused */
    hwm_values.temperatures[2] = 0; /* unused */
    hwm_values.fans[1]         = 0; /* unused */
    hwm_values.fans[2]         = 0; /* unused */
    /* -5V is not reported by the BIOS, but leave it set */

    return ret;
}

int
machine_at_tx97_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/tx97/0112.001",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4); /* PIIX4 */
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);

    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&w83877_device, (void *) (W83877TF | W83877_3F0));
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 128);
    device_add(&w83781d_device);    /* fans: Chassis, CPU, Power; temperatures: MB, unused, CPU */
    hwm_values.temperatures[1] = 0; /* unused */
    /* CPU offset */
    if (hwm_values.temperatures[2] < 32) /* prevent underflow */
        hwm_values.temperatures[2] = 0;
    else
        hwm_values.temperatures[2] -= 32;

    return ret;
}

void
machine_at_optiplex_21152_init(void)
{
    uint8_t bus_index = pci_bridge_get_bus_index(device_add(&dec21152_device));

    pci_register_bus_slot(bus_index, 0x09, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_bus_slot(bus_index, 0x0a, PCI_CARD_NORMAL, 4, 2, 1, 3);
    pci_register_bus_slot(bus_index, 0x0b, PCI_CARD_NORMAL, 1, 3, 4, 2);
}

int
machine_at_optiplexgn_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/optiplexgn/DELL.ROM",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 2, 1);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      2, 1, 3, 4);
    pci_register_slot(0x10, PCI_CARD_VIDEO,       4, 0, 0, 0); /* Trio64V2/GX, temporarily Trio64V2/DX is given */
    pci_register_slot(0x11, PCI_CARD_NETWORK,     4, 0, 0, 0); /* 3C905, not yet emulated */
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 4);
    pci_register_slot(0x0F, PCI_CARD_BRIDGE,      0, 0, 0, 0);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    if ((sound_card_current[0] == SOUND_INTERNAL) && machine_get_snd_device(machine)->available())
        machine_snd = device_add(machine_get_snd_device(machine));

    device_add(&i430tx_device);
    device_add(&piix4_device);
    machine_at_optiplex_21152_init();
    device_add_params(&pc87307_device, (void *) (PCX730X_PHOENIX_42 | PCX7307_PC87307));
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 128);

    return ret;
}

int
machine_at_tomahawk_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/tomahawk/0AAGT046.ROM",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0F, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4); /* PIIX4 */
    pci_register_slot(0x0D, PCI_CARD_VIDEO,       3, 0, 0, 0);
    pci_register_slot(0x0E, PCI_CARD_NETWORK,     4, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      3, 4, 1, 2);

    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add_params(&fdc37c67x_device, (void *) (FDC37XXX2 | FDC37XXXX_370));
    device_add(&amd_flash_29f020a_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 128);
    device_add(&lm78_device);      /* fans: Thermal, CPU, Chassis; temperature: unused */
    device_add(&lm75_1_4a_device); /* temperature: CPU */

    if ((gfxcard[0] == VID_INTERNAL) && machine_get_vid_device(machine))
        device_add(machine_get_vid_device(machine));

    if ((sound_card_current[0] == SOUND_INTERNAL) && machine_get_snd_device(machine))
        device_add(machine_get_snd_device(machine));

    if ((net_cards_conf[0].device_num == NET_INTERNAL) && machine_get_net_device(machine))
        device_add(machine_get_net_device(machine));

    return ret;
}

int
machine_at_ym430tx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ym430tx/YM430TX.003",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4); /* PIIX4 */
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);

    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add_params(&w83977_device, (void *) (W83977TF | W83977_AMI | W83977_NO_NVR));
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 128);

    return ret;
}

int
machine_at_tx97xv_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/tx97xv/Bios.rom",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4); /* PIIX4 */
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_VIDEO,       1, 0, 0, 0);

    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add_params(&pc87307_device, (void *) (PCX730X_AMI | PCX7307_PC87307 | PCX730X_02E));
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 128);

    if ((gfxcard[0] == VID_INTERNAL) && machine_get_vid_device(machine))
        device_add(machine_get_vid_device(machine));

    return ret;
}

int
machine_at_thunderbolt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/thunderbolt/tbolt-01.rom",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 1, 2, 3); /* PIIX4 */
    pci_register_slot(0x11, PCI_CARD_NORMAL,      0, 1, 2, 3);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      1, 2, 3, 0);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 0, 1);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      3, 0, 1, 2);

    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add_params(&fdc37c93x_device, (void *) (FDC37XXX5 | FDC37C93X_NORMAL | FDC37C93X_NO_NVR));
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 128);

    return ret;
}

static const device_config_t ms5156_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ms5156w",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AMIBIOS 6 (071595) - Revision 1.0",
                .internal_name = "ms5156a",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ms5156/A556MS10.ROM", "" }
            },
            {
                .name          = "AMIBIOS 6 (071595) - Revision 1.0 (Japanese)",
                .internal_name = "ms5156aj",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ms5156/A556J110.ROM", "" }
            },
            {
                .name          = "AMIBIOS 6 (071595) - Revision 1.0 (Traditional Chinese)",
                .internal_name = "ms5156atc",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ms5156/A556C410.ROM", "" }
            },
            {
                .name          = "AMIBIOS 6 (071595) - Revision 1.3 (Simplified Chinese)",
                .internal_name = "ms5156asc",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ms5156/A556C313.ROM", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 1.5",
                .internal_name = "ms5156w",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ms5156/W556MS15.BIN", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 1.6B1 (ACPI Beta)",
                .internal_name = "ms5156wab",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ms5156/W556MS16.001", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ms5156_device = {
    .name          = "MSI MS-5156",
    .internal_name = "ms5156",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ms5156_config
};

int
machine_at_ms5156_init(const machine_t *model)
{
    int ret;
    const char *fn;

    device_context(model->device);
    int is_english = !strcmp(device_get_config_bios("bios"), "ms5156a");
    int is_award   = !strcmp(device_get_config_bios("bios"), "ms5156w") || !strcmp(device_get_config_bios("bios"), "ms5156wab");
    fn             = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    device_context_restore();

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 4); /* PIIX4 */

    device_add(&i430tx_device);
    if (is_award)
        device_add(&piix4_device);
    else if (is_english)
        device_add_params(&piix4_device, (void *) PIIX4_NVR_AMI_1995);
    else
        device_add_params(&piix4_device, (void *) PIIX4_NVR_AMI_1995J);
    device_add_params(&w83977_device, (void *) (W83977TF | W83977_AMI | W83977_NO_NVR));
    device_add(&sst_flash_29ee010_device); /* assumed */
    spd_register(SPD_TYPE_SDRAM, 0x3, 128);

    return ret;
}

int
machine_at_ma23c_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ma23c/BIOS.ROM",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_VIDEO,       3, 4, 1, 2);

    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add(&nec_mate_unk_device);
    device_add_params(&fdc37c67x_device, (void *) (FDC37XXX2 | FDC37XXXX_370));
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    return ret;
}

static const device_config_t an430tx_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "an430tx",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "PhoenixBIOS 4.0 Release 6.0 - Revision P02-0011 (Sony Vaio PCV-130/150)",
                .internal_name = "pcv150",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 5,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/an430tx/P02-0011.BIO", "roms/machines/an430tx/P02-0011.BI1",
                                   "roms/machines/an430tx/P02-0011.BI2", "roms/machines/an430tx/P02-0011.BI3",
                                   "roms/machines/an430tx/P02-0011.RCV", "" }
            },
            {
                .name          = "PhoenixBIOS 4.0 Release 6.0 - Revision P09-0006 (Packard Bell PB79x)",
                .internal_name = "an430tx",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 5,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/an430tx/ANP0911A.BIO", "roms/machines/an430tx/ANP0911A.BI1",
                                   "roms/machines/an430tx/ANP0911A.BI2", "roms/machines/an430tx/ANP0911A.BI3",
                                   "roms/machines/an430tx/ANP0911A.RCV", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t an430tx_device = {
    .name          = "Intel AN430TX (Anchorage)",
    .internal_name = "an430tx_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = an430tx_config
};

int
machine_at_an430tx_init(const machine_t *model)
{
    int ret = 0;
    const char* fn[5];

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    for (int i = 0; i < 5; i++)
        fn[i] = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), i);
    ret = bios_load_linear_combined2(fn[0], fn[1], fn[2], fn[3], fn[4], 0x3a000, 160);
    device_context_restore();

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 4); /* PIIX4 */
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       4, 0, 0, 0);

    device_add(&i430tx_device);
    device_add(&piix4_device);
#ifdef FOLLOW_THE_SPECIFICATION
    device_add_params(&pc87307_device, (void *) (PCX730X_PHOENIX_42I | PCX7307_PC97307));
#else
    /* The technical specification says Phoenix, a real machnine HWINFO dump says AMI '5'. */
    device_add_params(&pc87307_device, (void *) (PCX730X_AMI | PCX7307_PC97307));
#endif
    device_add(&intel_flash_bxt_ami_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 128);

    if (sound_card_current[0] == SOUND_INTERNAL)
        machine_snd = device_add(machine_get_snd_device(machine));

    return ret;
}

int
machine_at_mb540n_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/mb540n/Tx0720ug.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4); /* PIIX4 */

    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&um8669f_device, (void *) 0);
    device_add(&sst_flash_29ee010_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 128);

    return ret;
}

int
machine_at_56a5_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/56a5/54p5b6b.bin",
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
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4); /* PIIX4 */
    pci_register_slot(0x10, PCI_CARD_NORMAL,      1, 2, 3, 4);

    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&w83877_device, (void *) (W83877F | W83877_3F0));
    device_add(&sst_flash_29ee010_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 128);

    return ret;
}

int
machine_at_p5mms98_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p5mms98/s981182.rom",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4); /* PIIX4 */
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&i430tx_device);
    device_add(&piix4_device);
    /* This actually has the Winbond W83967AF, for which I can not find any datasheet at all. */
    device_add_params(&w83977_device, (void *) (W83977F | W83977_AMI | W83977_NO_NVR));
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 128);
    device_add(&lm78_device);      /* fans: Thermal, CPU, Chassis; temperature: unused */
    device_add(&lm75_1_4a_device); /* temperature: CPU */

    return ret;
}

int
machine_at_richmond_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/richmond/RICHMOND.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4); /* PIIX4 */
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&it8671f_device);
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 128);
    device_add(&lm78_device);      /* fans: Thermal, CPU, Chassis; temperature: unused */
    device_add(&lm75_1_4a_device); /* temperature: CPU */

    return ret;
}

/* VIA VPX */
int
machine_at_ficva502_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ficva502/VA502bp.BIN",
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

    device_add(&via_vpx_device);
    device_add(&via_vt82c586b_device);
    device_add_params(&fdc37c669_device, (void *) FDC37C6XX_370);
    device_add(&sst_flash_29ee010_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 256);

    return ret;
}

/* VIA VP3 */
int
machine_at_ficpa2012_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ficpa2012/113jb16.awd",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&via_vp3_device);
    device_add(&via_vt82c586b_device);
    device_add_params(&w83877_device, (void *) (W83877F | W83877_3F0));
    device_add(&sst_flash_29ee010_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 512);

    return ret;
}

int
machine_at_via809ds_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/via809ds/v30422sg.rom",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 1, 2, 3); /* assumed */
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&via_vp3_device);
    device_add(&via_vt82c586b_device);
    device_add_params(&fdc37c669_device, (void *) 0);
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 512);

    return ret;
}

/* SiS 5571 */
int
machine_at_cb52xsi_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/cb52xsi/CD5205S.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&sis_5571_device);
    device_add_params(&fdc37c669_device, (void *) FDC37C6XX_370);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

static const device_config_t ms5146_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ms5146",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AMIBIOS 6 (071595) - Revision 1.1",
                .internal_name = "ms5146",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ms5146/A546MS11.ROM", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 2.1",
                .internal_name = "ms5146_451pg",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ms5146/W546MS21.BIN", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ms5146_device = {
    .name          = "MSI MS-5146",
    .internal_name = "ms5146_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ms5146_config
};

int
machine_at_ms5146_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    device_context_restore();

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&sis_5571_device);
    device_add_params(&w83877_device, (void *) (W83877F | W83877_3F0));
    device_add(&sst_flash_29ee010_device);

    return ret;
}

static const device_config_t r534f_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "r534f_1998",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 06/12/1998",
                .internal_name = "r534f_1998",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/r534f/r534f008-1998.bin", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 03/13/2000 (by Unicore Software)",
                .internal_name = "r534f",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/r534f/r534f008.bin", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t r534f_device = {
    .name          = "Rise R534F",
    .internal_name = "r534f_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = r534f_config
};

int
machine_at_r534f_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    device_context_restore();

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&sis_5571_device);
    device_add_params(&w83877_device, (void *) (W83877F | W83877_3F0));
    device_add(&sst_flash_29ee010_device);

    return ret;
}

/* SiS 5581 */
int
machine_at_sp97xv_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/sp97xv/0109XVJ2.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x13, PCI_CARD_VIDEO,       1, 2, 3, 4);    /* On-chip SiS graphics, absent here. */

    device_add(&sis_5581_device);
    device_add_params(&w83877_device, (void *) (W83877F | W83877_3F0));
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_sq578_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/sq578/578b03.rom",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      1, 2, 3, 4);

    device_add(&sis_5581_device);
    device_add_params(&w83877_device, (void *) (W83877TF | W83877_3F0));
    device_add(&sst_flash_29ee010_device);

    return ret;
}

/* SiS 5591 */
int
machine_at_ms5172_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ms5172/A572MS15.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x02, PCI_CARD_AGPBRIDGE,   0, 0, 0, 0);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      3, 4, 1, 2);

    device_add(&sis_5591_1997_device);
    device_add_params(&w83877_device, (void *) (W83877TF | W83877_3F0));
    device_add(&sst_flash_29ee010_device);

    return ret;
}

/* ALi ALADDiN IV+ */
static const device_config_t m5ata_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "m5ata",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 12/23/97",
                .internal_name = "m5ata_1223",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/m5ata/ATA1223.BIN", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 05/27/98",
                .internal_name = "m5ata",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/m5ata/ATA0527B.BIN", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t m5ata_device = {
    .name          = "Biostar M5ATA",
    .internal_name = "m5ata_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = m5ata_config
};

int
machine_at_m5ata_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    device_context_restore();

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE,     1, 2, 3, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE,     1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_SOUTHBRIDGE_IDE, 1, 2, 3, 4);
    pci_register_slot(0x03, PCI_CARD_NORMAL,          1, 2, 3, 4);
    pci_register_slot(0x04, PCI_CARD_NORMAL,          2, 3, 4, 1);
    pci_register_slot(0x05, PCI_CARD_NORMAL,          3, 4, 1, 2);
    pci_register_slot(0x06, PCI_CARD_NORMAL,          4, 1, 2, 3);

    device_add(&ali1531_device);
    device_add(&ali1543_device); /* -5 */
    spd_register(SPD_TYPE_SDRAM, 0x3, 64);

    return ret;
}

int
machine_at_ms5164_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ms5164/W564MS43.005",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE,     0, 0, 0, 0);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE,     1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_SOUTHBRIDGE_IDE, 5, 6, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_SOUTHBRIDGE_PMU, 1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_SOUTHBRIDGE_USB, 1, 2, 3, 4);
    pci_register_slot(0x03, PCI_CARD_NORMAL,          1, 2, 3, 4);
    pci_register_slot(0x04, PCI_CARD_NORMAL,          2, 3, 4, 1);
    pci_register_slot(0x05, PCI_CARD_NORMAL,          3, 4, 1, 2);
    pci_register_slot(0x06, PCI_CARD_NORMAL,          4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_NORMAL,          1, 2, 3, 4);

    device_add(&ali1531_device);
    device_add(&ali1543_device); /* -5 */
    device_add(&sst_flash_29ee010_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 256);

    return ret;
}

int
machine_at_m560_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/m560/5600410s.ami",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE,     0, 0, 0, 0);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE,     1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_SOUTHBRIDGE_IDE, 1, 2, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_SOUTHBRIDGE_PMU, 1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_SOUTHBRIDGE_USB, 1, 2, 3, 4);
    pci_register_slot(0x03, PCI_CARD_NORMAL,          1, 2, 3, 4);
    pci_register_slot(0x04, PCI_CARD_NORMAL,          2, 3, 4, 1);
    pci_register_slot(0x05, PCI_CARD_NORMAL,          3, 4, 1, 2);
    pci_register_slot(0x06, PCI_CARD_NORMAL,          4, 1, 2, 3);

    device_add(&ali1531_device);
    device_add(&ali1543_device); /* -5 */
    device_add(&sst_flash_29ee010_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 256);

    return ret;
}
