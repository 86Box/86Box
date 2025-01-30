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
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2020 Miran Grca.
 *
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

int
machine_at_acerv35n_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/acerv35n/v35nd1s1.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add(&fdc37c932fr_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

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
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&fdc37c665_device);
    device_add(&ncr53c810_onboard_pci_device);
    device_add(&intel_flash_bxt_device);

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
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83877f_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_m7shi_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/m7shi/m7shi2n.rom",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add(&fdc37c935_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

/* The Sony VAIO is an AG430HX, I'm assuming it has the same configuration bits
   as the TC430HX, hence the #define. */
#define machine_at_ag430hx_gpio_init machine_at_tc430hx_gpio_init

/* The PB680 is a NV430VX, I'm assuming it has the same configuration bits as
   the TC430HX, hence the #define. */
#define machine_at_nv430vx_gpio_init machine_at_tc430hx_gpio_init

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

    machine_set_gpio_default(gpio);
}

int
machine_at_tc430hx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined2("roms/machines/tc430hx/1007DH0_.BIO",
                                     "roms/machines/tc430hx/1007DH0_.BI1",
                                     "roms/machines/tc430hx/1007DH0_.BI2",
                                     "roms/machines/tc430hx/1007DH0_.BI3",
                                     "roms/machines/tc430hx/1007DH0_.RCV",
                                     0x3a000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);
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

    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

int
machine_at_infinia7200_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined2("roms/machines/infinia7200/1008DH08.BIO",
                                     "roms/machines/infinia7200/1008DH08.BI1",
                                     "roms/machines/infinia7200/1008DH08.BI2",
                                     "roms/machines/infinia7200/1008DH08.BI3",
                                     "roms/machines/infinia7200/1008DH08.RCV",
                                     0x3a000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);
    machine_at_tc430hx_gpio_init();

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO, 4, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

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

static void
machine_at_cu430hx_common_init(const machine_t *model)
{
    machine_at_common_init_ex(model, 2);
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
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_ami_device);
}

int
machine_at_cu430hx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined2("roms/machines/cu430hx/1006DK0_.BIO",
                                     "roms/machines/cu430hx/1006DK0_.BI1",
                                     "roms/machines/cu430hx/1006DK0_.BI2",
                                     "roms/machines/cu430hx/1006DK0_.BI3",
                                     "roms/machines/cu430hx/1006DK0_.RCV",
                                     0x3a000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_cu430hx_common_init(model);

    return ret;
}

int
machine_at_equium5200_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined2("roms/machines/equium5200/1003DK08.BIO",
                                     "roms/machines/equium5200/1003DK08.BI1",
                                     "roms/machines/equium5200/1003DK08.BI2",
                                     "roms/machines/equium5200/1003DK08.BI3",
                                     "roms/machines/equium5200/1003DK08.RCV",
                                     0x3a000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_cu430hx_common_init(model);

    return ret;
}

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

    machine_at_common_init_ex(model, 2);
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
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
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

int
machine_at_epc2102_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/epc2102/P5000HX.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);
    device_add_params(&at_nvr_device, (void *) 0x20);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_intel_ami_pci_device);
    device_add(&i82091aa_device);
    device_add(&sst_flash_39sf010_device);

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
    device_add(&keyboard_ps2_ami_pci_device); // It uses the AMIKEY KBC
    device_add(&w83877f_device);
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
    device_add(&keyboard_ps2_pci_device);
    device_add(&prime3c_device);
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
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&um8669f_device);
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

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_NO_BRIDGES);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x14, PCI_CARD_VIDEO,       3, 0, 0, 0);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      1, 2, 3, 4);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&s3_trio64v2_dx_onboard_pci_device);

    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add(&fdc37c932qf_device);
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

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_NO_BRIDGES);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x14, PCI_CARD_VIDEO,       3, 0, 0, 0);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      1, 2, 3, 4);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&s3_trio64v2_dx_onboard_pci_device);

    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add(&fdc37c931apm_compaq_device);
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

    machine_at_common_init_ex(model, 2);

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
    device_add(&fdc37c932fr_device);
    device_add(&intel_flash_bxt_ami_device);

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

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add(&fdc37c932fr_device);
    device_add(&intel_flash_bxt_device);

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

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x14, PCI_CARD_VIDEO,       4, 0, 0, 0);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add(&fdc37c935_370_device);
    device_add(&sst_flash_29ee020_device);

    return ret;
}

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

    machine_at_common_init_ex(model, 2);
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
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&pc87306_device);
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

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 2, 1, 3, 4);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 1, 3, 2, 4);
    pci_register_slot(0x0b, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    if (sound_card_current[0] == SOUND_INTERNAL)
        device_add(&cs4237b_device);

    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add(&fdc37c935_370_device);
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
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&fdc37c669_device);
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
    device_add(&keyboard_ps2_pci_device);
    device_add(&um8669f_device);
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

    machine_at_common_init_ex(model, 2);

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
    device_add(&fdc37c932fr_device);
    device_add(&intel_flash_bxt_ami_device);

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

    machine_at_common_init_ex(model, 2);

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
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&fdc37c67x_device);
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    return ret;
}

int
machine_at_nupro592_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/nupro592/np590b10.bin",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

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
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&w83977ef_device);
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

    machine_at_common_init_ex(model, 2);

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
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&w83877tf_acorp_device);
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

#ifdef USE_AN430TX
int
machine_at_an430tx_init(const machine_t *model)
{
    int ret;

#    if 1
    ret = bios_load_linear_combined2("roms/machines/an430tx/P10-0095.BIO",
                                     "roms/machines/an430tx/P10-0095.BI1",
                                     "roms/machines/an430tx/P10-0095.BI2",
                                     "roms/machines/an430tx/P10-0095.BI3",
                                     "roms/machines/an430tx/P10-0095.RCV",
                                     0x3a000, 160);
#    else
    ret = bios_load_linear_combined2("roms/machines/an430tx/P06-0062.BIO",
                                     "roms/machines/an430tx/P06-0062.BI1",
                                     "roms/machines/an430tx/P06-0062.BI2",
                                     "roms/machines/an430tx/P06-0062.BI3",
                                     "roms/machines/an430tx/P10-0095.RCV",
                                     0x3a000, 160);
#    endif

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4); /* PIIX4 */
    // pci_register_slot(0x08, PCI_CARD_VIDEO,       4, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      4, 1, 2, 3);
    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&pc87307_both_device);
    device_add(&intel_flash_bxt_ami_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 128);

    return ret;
}
#endif /* USE_AN430TX */

int
machine_at_ym430tx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ym430tx/YM430TX.003",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

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
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83977tf_device);
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 128);

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

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4); /* PIIX4 */
    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&um8669f_device);
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

    machine_at_common_init_ex(model, 2);

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
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83877f_device);
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

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4); /* PIIX4 */
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      4, 1, 2, 3);
    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&w83977tf_device);
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

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4); /* PIIX4 */
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      4, 1, 2, 3);
    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&it8671f_device);
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 128);
    device_add(&lm78_device);      /* fans: Thermal, CPU, Chassis; temperature: unused */
    device_add(&lm75_1_4a_device); /* temperature: CPU */

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

    machine_at_common_init_ex(model, 2);

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
    device_add(&keyboard_ps2_intel_ami_pci_device);
    device_add(&fdc37c67x_device);
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
machine_at_ficva502_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ficva502/VA502bp.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    device_add(&via_vpx_device);
    device_add(&via_vt82c586b_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&fdc37c669_370_device);
    device_add(&sst_flash_29ee010_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 256);

    return ret;
}

int
machine_at_ficpa2012_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ficpa2012/113jb16.awd",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

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
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83877f_device);
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

    machine_at_common_init_ex(model, 2);

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
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&fdc37c669_device);
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 512);

    return ret;
}

int
machine_at_r534f_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/r534f/r534f008.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&sis_5571_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&w83877f_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_ms5146_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ms5146/A546MS11.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&sis_5571_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&w83877f_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_cb52xsi_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/cb52xsi/CD5205S.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&sis_5571_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&fdc37c669_370_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_sp97xv_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/sp97xv/0109XVJ2.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x13, PCI_CARD_VIDEO,       1, 2, 3, 4);    /* On-chip SiS graphics, absent here. */
    device_add(&sis_5581_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&w83877f_device);
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

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      1, 2, 3, 4);
    device_add(&sis_5581_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&w83877tf_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_ms5172_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ms5172/A572MS15.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x02, PCI_CARD_AGPBRIDGE,   0, 0, 0, 0);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      3, 4, 1, 2);
    device_add(&sis_5591_1997_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&w83877tf_device);
    device_add(&sst_flash_29ee010_device);

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

    machine_at_common_init_ex(model, 2);

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

int
machine_at_ms5164_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ms5164/W564MS43.005",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

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
machine_at_thunderbolt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/thunderbolt/tbolt-01.rom",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 1, 2, 3); /* PIIX4 */
    pci_register_slot(0x11, PCI_CARD_NORMAL,      0, 1, 2, 3);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      1, 2, 3, 0);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 0, 1);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      3, 0, 1, 2);
    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add(&fdc37c935_device);
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 128);

    return ret;
}
