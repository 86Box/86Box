/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Socket 370 (PGA370) machines.
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
#include <86box/pci.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/ioapic_routes.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/keyboard.h>
#include <86box/flash.h>
#include <86box/sio.h>
#include <86box/hwm.h>
#include <86box/spd.h>
#include <86box/video.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/clock.h>
#include <86box/sound.h>
#include <86box/snd_ac97.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/network.h>

/* i440LX */
int
machine_at_s370slm_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/s370slm/3LM1202.rom",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440lx_device);
    device_add(&piix4e_device);
    device_add_params(&w83977_device, (void *) (W83977TF | W83977_AMI | W83977_NO_NVR));
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);
    device_add(&w83781d_device);    /* fans: CPU, Fan 2, Chassis; temperatures: unused, CPU, unused */
    hwm_values.temperatures[0] = 0; /* unused */
    hwm_values.temperatures[2] = 0; /* unused */

    return ret;
}

/* i440BX */
int
machine_at_awo671r_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/awo671r/a08139c.bin",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0C, PCI_CARD_SOUND,       4, 1, 2, 3);
    pci_register_slot(0x0D, PCI_CARD_VIDEO,       2, 3, 4, 1);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add_inst_params(&w83977_device, 1, (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add_inst_params(&w83977_device, 2, (void *) (W83977EF | W83977_NO_NVR));
    device_add(&sst_flash_39sf020_device);
    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));
    spd_register(SPD_TYPE_SDRAM, 0x3, 256);

    if (sound_card_current[0] == SOUND_INTERNAL)
        machine_snd = device_add(machine_get_snd_device(machine));

    return ret;
}

int
machine_at_ambx133_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ambx133/mkbx2vg2.bin",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add_params(&w83977_device, (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);
    device_add(&gl518sm_2d_device); /* fans: CPUFAN1, CPUFAN2; temperature: CPU */
    hwm_values.fans[1] += 500;
    hwm_values.temperatures[0] += 4;                         /* CPU offset */
    hwm_values.voltages[1] = RESISTOR_DIVIDER(12000, 10, 2); /* different 12V divider in BIOS (10K/2K?) */

    return ret;
}

int
machine_at_cubx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/cubx/1008cu.004",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x04, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_IDE,         2, 3, 4, 1);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add_params(&w83977_device, (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add(machine_get_ide_device(machine));
    device_add(ics9xxx_get(ICS9250_08));
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0xF, 256);
    device_add(&as99127f_device); /* fans: Chassis, CPU, Power; temperatures: MB, JTPWR, CPU */

    return ret;
}

static const device_config_t em440_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "em440",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "PhoenixBIOS 4.0 Release 6.0 - Revision 1.03B",
                .internal_name = "em440_103b",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 524288,
                .files         = { "roms/machines/em440/BIOS512K.ROM", "" }
            },
            {
                .name          = "PhoenixBIOS 4.0 Release 6.0 - Revision 1.04A",
                .internal_name = "em440",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/em440/BIOS256K.ROM", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t em440_device = {
    .name          = "RadiSys Endura EM440",
    .internal_name = "em440",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = em440_config
};

int
machine_at_em440_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    int size = device_get_bios_file_size(machine_get_device(machine), device_get_config_bios("bios"));
    ret = bios_load_linear(fn, 0x00100000 - size, size, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       1, 2, 3, 4); /* not yet implemented */
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add_params(&pc87309_device, (void *) (PCX730X_15C | PCX730X_AMI | PC87309_PC87309));
    device_add(&micron_flash_x00_t_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    return ret;
}

/* i440ZX */
int
machine_at_acerv66ma_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/acerv66ma/acerv66ma.bin",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_SOUND,       3, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 1, 2);

    device_add(&i440zx_device);
    device_add(&piix4e_device);
    device_add_params(&fdc37mx0x_device, (void *) (FDC37M70X | FDC37XXX7 | FDC37C93X_NO_NVR));
    device_add_params(&winbond_flash_w29c020_device, (void *) SST_FORCE_128);
    spd_register(SPD_TYPE_SDRAM, 0x3, 128);

    if (sound_card_current[0] == SOUND_INTERNAL)
        machine_snd = device_add(machine_get_snd_device(machine));

    return ret;
}

int
machine_at_bl440zx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_intel("roms/machines/bl440zx/P05-0012.BIO",
                          "roms/machines/bl440zx/P05-0012.BBO",
                          262144, 0);

    if (bios_only || !ret)
        return ret;
    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    /* The official Intel riser only has a single PCI slot, although the PCI IRQ table still defines
       three more slots, possibly intended for allowing the use of risers with more slots if Intel
       would have ever decided to make them. */
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x06, PCI_CARD_NETWORK,     4, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_SOUND,       2, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440zx_device);
    device_add(&piix4e_device);
    device_add_params(&fdc37mx0x_device, (void *) (FDC37B80X | FDC37XXX7 | FDC37C93X_NO_NVR | FDC37XXXX_370));
    /* TODO: Implement Intel E28F200 flash */
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 256);

    if ((net_cards_conf[0].device_num == NET_INTERNAL) && machine_get_net_device(machine))
        device_add(machine_get_net_device(machine));

    if (sound_card_current[0] == SOUND_INTERNAL) {
        device_add(machine_get_snd_device(machine));
        device_add(&cs4297a_device);
    }

    return ret;
}

int
machine_at_63a1_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/63a1/63a-q3.bin",
                           0x000c0000, 262144, 0);

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
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4); /* Integrated Sound? */
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440zx_device);
    device_add(&piix4e_device);
    device_add_params(&w83977_device, (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 256);

    return ret;
}

/* SiS 600 */
int
machine_at_7sbb_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/7sbb/sbb12aa2.bin",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x02, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&sis_5600_device);
    device_add(&it8661f_device);
    device_add(&sst_flash_29ee020_device); /* assumed */

    return ret;
}

/* SiS 620 */
static const device_config_t me99_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "me99",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AwardBIOS v6.0 - Revision 1004",
                .internal_name = "me99_1004",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/me99/ME9I1004.AWD", "" }
            },
            {
                .name          = "AwardBIOS v6.0 - Revision 1004.A",
                .internal_name = "me99_1004a",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/me99/me91004a.awd", "" }
            },
            {
                .name          = "AwardBIOS v6.0 - Revision 1007 Beta 002",
                .internal_name = "me99",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/me99/1007.002", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t me99_device = {
    .name          = "ASUS ME-99",
    .internal_name = "me99",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = me99_config
};

int
machine_at_me99_init(const machine_t *model)
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

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_SOUND,       4, 1, 2, 3);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x02, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&sis_620_device);
    device_add_params(&w83877_device, (void *) (W83877TF | W83877_3F0));
    device_add(&sst_flash_29ee020_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 512);

    /* Voltage values for the SiS 5595 DAM */
    hwm_values.voltages[0] = 1168; /* Vcore */
    hwm_values.voltages[1] = 1952; /* 12V */
    hwm_values.voltages[2] = 1952; /* 3.3V, BIOS displays it as 3.54V */
    hwm_values.voltages[3] = 1984; /* 5V */
    hwm_values.voltages[4] = 2784; /* CPU temperature */

    if ((gfxcard[0] == VID_INTERNAL) && machine_get_vid_device(machine))
        device_add(machine_get_vid_device(machine));

    if (sound_card_current[0] == SOUND_INTERNAL)
        device_add(machine_get_snd_device(machine));

    return ret;
}

/* SMSC VictoryBX-66 */
int
machine_at_atc7020bxii_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/atc7020bxii/7020s102.bin",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440bx_device);
    device_add(&slc90e66_device);
    device_add_params(&w83977_device, (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0xF, 256);

    return ret;
}

int
machine_at_m773_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/m773/010504s.rom",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_SOUND,       4, 3, 0, 0);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440bx_device);
    device_add(&slc90e66_device);
    device_add(&it8671f_device);
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 256);
    device_add(&gl520sm_2d_device);  /* fans: CPU, Chassis; temperature: System */
    hwm_values.temperatures[0] += 2; /* System offset */
    hwm_values.temperatures[1] += 2; /* CPU offset */
    hwm_values.voltages[0] = 3300;   /* Vcore and 3.3V are swapped */
    hwm_values.voltages[2] = hwm_get_vcore();

    if (sound_card_current[0] == SOUND_INTERNAL)
        device_add(machine_get_snd_device(machine));

    return ret;
}

/* VIA Apollo Pro */
int
machine_at_vim863s_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/vim863s/V0218SAG.BIN",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 0, 0);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    if ((sound_card_current[0] == SOUND_INTERNAL) && machine_get_snd_device(machine)->available())
        machine_snd = device_add(machine_get_snd_device(machine));

    device_add(&via_apro_device);
    device_add(&via_vt82c586b_device);
    device_add_params(&fdc37c669_device, (void *) 0);
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    return ret;
}

/* VIA Apollo Pro 133 */
int
machine_at_p6bap_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p6bap/bapa14a.BIN",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0a, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0b, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0c, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0d, PCI_CARD_SOUND,       4, 3, 2, 1); /* assumed */
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&via_apro133a_device);  /* Rebranded as ET82C693A */
    device_add_params(&via_vt82c596b_device, (void *) VIA_PIPC_NO_KBC); /* Rebranded as ET82C696B */
    device_add_params(&w83977_device, (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);
    device_add(&gl520sm_2d_device);  /* fans: CPU, Chassis; temperature: System */
    hwm_values.temperatures[0] += 2; /* System offset */
    hwm_values.temperatures[1] += 2; /* CPU offset */
    hwm_values.voltages[0] = 3300;   /* Vcore and 3.3V are swapped */
    hwm_values.voltages[2] = hwm_get_vcore();

    if (sound_card_current[0] == SOUND_INTERNAL)
        device_add(machine_get_snd_device(machine));

    return ret;
}

/* VIA Apollo Pro 133A */
int
machine_at_6via90ap_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/6via90ap/90ap10.bin",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&via_apro133a_device);
    device_add(&via_vt82c686b_device); /* fans: CPU1, CPU2; temperatures: CPU, System, unused */
    device_add(ics9xxx_get(ICS9250_18));
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 1024);
    hwm_values.temperatures[0] += 2; /* CPU offset */
    hwm_values.temperatures[1] += 2; /* System offset */
    hwm_values.temperatures[2] = 0;  /* unused */

    if (sound_card_current[0] == SOUND_INTERNAL)
        device_add(machine_get_snd_device(machine)); /* ALC100P identified on similar Acorp boards (694TA, 6VIA90A1) */

    return ret;
}

int
machine_at_cuv4xls_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/cuv4xls/1005LS.001",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x04, PCI_CARD_SOUTHBRIDGE, 4, 1, 2, 3);
    pci_register_slot(0x05, PCI_CARD_SOUND,       3, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_NORMAL,      2, 3, 0, 0);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      4, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&via_apro133a_device);
    device_add(&via_vt82c686b_device);
    device_add(ics9xxx_get(ICS9250_18));
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0xF, 1024);
    device_add(&as99127f_device); /* fans: Chassis, CPU, Power; temperatures: MB, JTPWR, CPU */

    if (sound_card_current[0] == SOUND_INTERNAL)
        device_add(machine_get_snd_device(machine));

    return ret;
}

static const device_config_t cairo5_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ms6309",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AMIBIOS 6 (071595) - Revision 1.5",
                .internal_name = "ms6309_15",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/cairo5/a6309vms.150", "" }
            },
            {
                .name          = "AMIBIOS 6 (071595) - Revision 1.9",
                .internal_name = "ms6309",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/cairo5/a6309vms.190", "" }
            },
            {
                .name          = "PhoenixBIOS 4.0 Release 6.0 - Revision 08BV (Samsung)",
                .internal_name = "cairo5",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/cairo5/08BV.BIN", "" }
            },
            { .files_no = 0 }
        }
    },
    {
        .name           = "disable_apic",
        .description    = "Disable APIC",
        .type           = CONFIG_BINARY,
        .default_int    = 0,
        .default_string = NULL,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t cairo5_device = {
    .name          = "MSI MS-6309",
    .internal_name = "cairo5",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = cairo5_config
};

int
machine_at_cairo5_init(const machine_t *model)
{
    /* The 686A's I/O APIC has the ISA IRQs on their own inputs and PIRQA-D#
       on 16-19. */
    static const ioapic_legacy_irq_routing_t cairo5_irq_routing = {
        .irq_to_intin  = IOAPIC_LEGACY_IRQ_ROUTE_ISA,
        .pirq_to_intin = IOAPIC_PIRQ_ROUTE_82093AA
    };
    int         ret = 0;
    const char *fn;
    void *pipc;
    void *ioapic;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000c0000, 262144, 0);
    device_context_restore();

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      1, 2, 3, 0);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 0, 1);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      3, 0, 1, 2);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      0, 1, 2, 3);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&via_apro133a_device);
    pipc = device_add(&via_vt82c686a_device);
    device_add(&winbond_flash_w29c020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 1024);

    /* The 686A's internal I/O APIC. The board's BIOSes build no MP table
       and turn the APIC off through Rx77[4] late in POST. */
    ioapic = device_add_params(&ioapic_validated_up_device,
                               (void *) (IOAPIC_TOPOLOGY_UP_APIC_DEFAULT | IOAPIC_VARIANT_VIA));
    ioapic_set_legacy_irq_routing(ioapic, &cairo5_irq_routing);
    via_pipc_set_ioapic(pipc, ioapic);

    return ret;
}

static const device_config_t ms6318_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ms6318",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AwardBIOS v6.00PG - Revision 1.1",
                .internal_name = "ms6318_110",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6318/w6318vms.110", "" }
            },
            {
                .name          = "AwardBIOS v6.00PG - Revision 1.2",
                .internal_name = "ms6318",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6318/w6318vms.120", "" }
            },
            {
                .name          = "AwardBIOS v6.00PG - Revision 7.1B5E (Elonex OEM)",
                .internal_name = "ms6318_715",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6318/w6318ve1.715", "" }
            },
            {
                .name          = "AwardBIOS v6.00PG - Revision 1.0B9 (Fujitsu-Siemens OEM)",
                .internal_name = "ms6318_109",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6318/ms-6318-ver5.bin", "" }
            },
            {
                .name          = "AwardBIOS v6.00PG - Revision 1.8 (HP Pavilion A7xx)",
                .internal_name = "ms6318_180",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6318/med2000v2.bin", "" }
            },
            {
                .name          = "AwardBIOS v6.00PG - Revision 1.9 (HP Pavilion A8xx)",
                .internal_name = "ms6318_190",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6318/med2000.bin", "" }
            },
            {
                .name          = "AwardBIOS v6.00PG - Revision 2.02 (HP Medion 2000A)",
                .internal_name = "ms6318_202",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6318/ms6318hp.bin", "" }
            },
            {
                .name          = "AwardBIOS v6.00PG - Revision 1.3 (Medion MED 2000)",
                .internal_name = "ms6318_130",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6318/ms6318.bin", "" }
            },
            {
                .name          = "AwardBIOS v6.00PG - Revision 7.51 (Medion MD6318)",
                .internal_name = "ms6318_751",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6318/bios.rom", "" }
            },
            { .files_no = 0 }
        }
    },
    {
        .name           = "disable_apic",
        .description    = "Disable APIC",
        .type           = CONFIG_BINARY,
        .default_int    = 0,
        .default_string = NULL,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ms6318_device = {
    .name          = "MSI MS-6318",
    .internal_name = "ms6318",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ms6318_config
};

int
machine_at_ms6318_init(const machine_t *model)
{
    /* The 686B's I/O APIC has the ISA IRQs on their own inputs and PIRQA-D#
       on 16-19. */
    static const ioapic_legacy_irq_routing_t ms6318_irq_routing = {
        .irq_to_intin  = IOAPIC_LEGACY_IRQ_ROUTE_ISA,
        .pirq_to_intin = IOAPIC_PIRQ_ROUTE_82093AA
    };
    int         ret = 0;
    const char *fn;
    void       *pipc;
    void       *ioapic;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000c0000, 262144, 0);
    device_context_restore();

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_SOUND,       3, 4, 1, 2);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&via_apro133a_device);
    pipc = device_add(&via_vt82c686b_device); /* fans: CPU1, CPU2; temperatures: CPU, System, unused */
    device_add(&sst_flash_39sf020_device); /* assumed */
    spd_register(SPD_TYPE_SDRAM, 0x7, 1024);

    /* The 686B's internal I/O APIC, which the Elonex, Fujitsu-Siemens and
       Medion 7.51 BIOSes describe in their MP table. */
    ioapic = device_add_params(&ioapic_validated_up_device,
                               (void *) (IOAPIC_TOPOLOGY_UP_APIC_DEFAULT | IOAPIC_VARIANT_VIA));
    ioapic_set_legacy_irq_routing(ioapic, &ms6318_irq_routing);
    via_pipc_set_ioapic(pipc, ioapic);

    hwm_values.temperatures[0] += 2; /* CPU offset */
    hwm_values.temperatures[1] += 2; /* System offset */
    hwm_values.temperatures[2] = 0;  /* unused */

    if (sound_card_current[0] == SOUND_INTERNAL)
        device_add(machine_get_snd_device(machine));
    device_add(&vt1611a_device); /* for both VIA and CT5880 options */

    return ret;
}

int
machine_at_m790mr_init(const machine_t *model)
{
    /* The 686A's I/O APIC has the ISA IRQs on their own inputs. The BIOS's
       MP table puts each PCI interrupt on the input of the IRQ it steered
       that PIRQ to (00:0A INTA# on INTIN5 with PIRQC# at IRQ5), level and
       active low, so the steered PIRQs reach the I/O APIC there. */
    static const ioapic_legacy_irq_routing_t m790mr_irq_routing = {
        .irq_to_intin = IOAPIC_LEGACY_IRQ_ROUTE_ISA
    };
    void *pipc;
    void *ioapic;
    int   ret;

    ret = bios_load_linear("roms/machines/m790mr/010914s.rom",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&via_apro133a_device);
    pipc = device_add(&via_vt82c686a_device);
    device_add(&sst_flash_39sf020_device); /* assumed */
    spd_register(SPD_TYPE_SDRAM, 0x7, 1024);

    /* The 686A's internal I/O APIC. The BIOS is AMI, so the MP table is in
       place by POST code 00h (IOAPIC_POST_TRIGGER_AMI). */
    ioapic = device_add_params(&ioapic_validated_up_device,
                               (void *) (IOAPIC_TOPOLOGY_UP_APIC_DEFAULT | IOAPIC_POST_TRIGGER_AMI | IOAPIC_VARIANT_VIA));
    ioapic_set_legacy_irq_routing(ioapic, &m790mr_irq_routing);
    via_pipc_set_ioapic(pipc, ioapic);

    hwm_values.temperatures[0] += 2; /* CPU offset */
    hwm_values.temperatures[1] += 2; /* System offset */
    hwm_values.temperatures[2] = 0;  /* unused */

    if (sound_card_current[0] == SOUND_INTERNAL)
        device_add(machine_get_snd_device(machine));

    return ret;
}
