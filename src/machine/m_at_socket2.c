/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Socket 2 machines.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2025 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/pci.h>
#include <86box/dma.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/gameport.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/rom.h>
#include <86box/sio.h>
#include <86box/hdc.h>
#include <86box/port_6x.h>
#include <86box/port_92.h>
#include <86box/video.h>
#include <86box/flash.h>
#include <86box/scsi_ncr53c8xx.h>
#include <86box/hwm.h>
#include <86box/machine.h>
#include <86box/plat_unused.h>
#include <86box/sound.h>

/* ACC 2168 */
int
machine_at_pb410a_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pb410a/pb410a.080337.4abf.u25.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ibm_common_ide_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&acc3221_device);
    device_add(&acc2168_device);

    device_add(&phoenix_486_jumper_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}

/* ALi M1429G */
int
machine_at_acera1g_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/acera1g/4alo001.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add(&ali1429g_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add_params(&pc87310_device, (void *) (PC87310_ALI));
    device_add(&ide_ali5213_device);

    return ret;
}

static void
machine_at_ali1429_common_init(const machine_t *model, int is_green)
{
    machine_at_common_init(model);

    if (is_green)
        device_add(&ali1429g_device);
    else
        device_add(&ali1429_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);
}

int
machine_at_winbios1429_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/win486/ali1429g.amw",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ali1429_common_init(model, 1);

    return ret;
}

int
machine_at_ali1429_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ali1429/ami486.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ali1429_common_init(model, 0);

    return ret;
}

/* i420TX */
int
machine_at_pci400ca_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pci400ca/486-AA008851.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_2 | PCI_NO_IRQ_STEERING);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SCSI,        1, 2, 3, 4);
    pci_register_slot(0x03, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x04, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x05, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&sio_device);

    device_add(&intel_flash_bxt_ami_device);

    device_add(&i420tx_device);
    device_add(&ncr53c810_onboard_pci_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* IMS 8848 */
int
machine_at_g486ip_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/g486ip/G486IP.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);
    device_add(&ami_1992_nvr_device);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2); /* 03 = Slot 1 */
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 3, 4, 1); /* 04 = Slot 2 */
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4); /* 05 = Slot 3 */

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&ims8848_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* OPTi 499 */
int
machine_at_cobalt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/cobalt/Cobalt_2.3.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&opti499_device);
    device_add(&ide_opti611_vlb_device);
    device_add(&ide_isa_sec_device);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}

int
machine_at_cougar_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/cougar/COUGRMRB.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add(&ide_vlb_device);

    device_add(&opti499_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_PRI));

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* SiS 461 */
int
machine_at_decpclpv_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/decpclpv/bios.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&sis_85c461_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&ide_isa_2ch_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C663 | FDC37C6XX_IDE_PRI));

    return ret;
}

int
machine_at_dell466np_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/dell466np/466np.bin",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add(&sis_85c461_device);

    video_reset(gfxcard[0]);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));
    else {
        for (uint16_t i = 0; i < 32768; i++)
            rom[i] = mem_readb_phys(0x000c0000 + i);
    }
    mem_mapping_set_addr(&bios_mapping, 0x0c0000, 0x40000);
    mem_mapping_set_exec(&bios_mapping, rom);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&ide_isa_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C661 | FDC37C6XX_IDE_PRI));

    return ret;
}

int
machine_at_valuepoint433_init(const machine_t *model) // hangs without the PS/2 mouse
{
    int ret;

    ret = bios_load_linear("roms/machines/valuepoint433/$IMAGEP.FLH",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    memcpy(&rom[0x00020000], rom, 131072);

    machine_at_common_ide_init(model);
    device_add(&sis_85c461_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&et4000w32_onboard_device);

    device_add_params(&fdc37c6xx_device, (void *) (FDC37C661 | FDC37C6XX_IDE_PRI));

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    video_reset(gfxcard[0]);

    if (gfxcard[0] != VID_INTERNAL) {
        for (uint16_t i = 0; i < 32768; i++)
            rom[i] = mem_readb_phys(0x000c0000 + i);
    }
    mem_mapping_set_addr(&bios_mapping, 0x0c0000, 0x40000);
    mem_mapping_set_exec(&bios_mapping, rom);

    return ret;
}

/* VLSI 82C480 */
int
machine_at_monsoon_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/monsoon/1009AC0_.BIO",
                                    "roms/machines/monsoon/1009AC0_.BI1", 0x1c000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    device_add(&vl82c480_device);
    device_add(&vl82c113_device);

    device_add(&ide_vlb_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C651 | FDC37C6XX_IDE_PRI));

    device_add(&intel_flash_bxt_device);
    device_add(&phoenix_486_jumper_monsoon_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}

int
machine_at_martin_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/martin/NONSCSI.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    device_add(&vl82c480_device);
    device_add(&vl82c113_device);

    device_add(&ide_vlb_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C651 | FDC37C6XX_IDE_PRI));

    device_add(&intel_flash_bxt_device);

    return ret;
}

/* VLSI 82C486 */
int
machine_at_sensation2_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/sensation2/TANDY_SENSATION_2_011004_10051993.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);

    device_add(&vl82c486_device);
    device_add(&vl82c113_device);

    device_add_params(&fdc37c6xx_device, (void *) (FDC37C651 | FDC37C6XX_IDE_PRI));

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}
