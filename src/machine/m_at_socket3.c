/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Socket 3 machines.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2025 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
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

/* ALi M1429G */
int
machine_at_atc1762_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/atc1762/atc1762.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add(&ali1429g_device);
    device_add(&kbc_ps2_ami_pci_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

int
machine_at_ecsal486_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ecsal486/ECS_AL486.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&ali1429g_device);
    device_add(&kbc_ps2_ami_pci_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

int
machine_at_ap4100aa_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ap4100aa/M27C512DIP28.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    device_add(&ami_1994_nvr_device);
    device_add(&ali1429g_device);
    device_add(&kbc_ps2_ami_pci_device);
    device_add(&ide_vlb_device);
    device_add_params(&um866x_device, (void *) UM8663BF);

    return ret;
}

/* Contaq 82C596A */
int
machine_at_4gpv5_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/4gpv5/4GPV5.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&contaq_82c596a_device);

    device_add(&kbc_at_device);

    return ret;
}

/* Contaq 82C597 */
int
machine_at_greenb_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/greenb/4gpv31-ami-1993-8273517.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&contaq_82c597_device);

    device_add(&kbc_at_ami_device);

    return ret;
}

/* OPTi 895 */
static void
machine_at_403tg_common_init(const machine_t *model, int nvr_hack)
{
    if (nvr_hack) {
        machine_at_common_init_ex(model, 2);
        device_add(&ami_1994_nvr_device);
    } else
        machine_at_common_init(model);

    device_add(&opti895_device);

    device_add(&kbc_at_ami_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);
}

int
machine_at_403tg_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/403tg/403TG.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_403tg_common_init(model, 0);

    return ret;
}

int
machine_at_403tg_d_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/403tg_d/J403TGRevD.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_403tg_common_init(model, 1);

    return ret;
}

int
machine_at_403tg_d_mr_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/403tg_d/MRBiosOPT895.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_403tg_common_init(model, 0);

    return ret;
}

/* SiS 461 */
int
machine_at_acerv10_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/acerv10/ALL.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&sis_85c461_device);
    device_add(&kbc_ps2_acer_pci_device);
    device_add(&ide_isa_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* SiS 471 */
static void
machine_at_sis_85c471_common_init(const machine_t *model)
{
    machine_at_common_init(model);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&sis_85c471_device);
}

int
machine_at_win471_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/win471/486-SiS_AC0360136.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_sis_85c471_common_init(model);
    device_add(&kbc_at_ami_device);

    return ret;
}

int
machine_at_vi15g_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/vi15g/vi15gr23.rom",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_sis_85c471_common_init(model);
    device_add(&kbc_at_ami_device);

    return ret;
}

int
machine_at_vli486sv2g_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/vli486sv2g/0402.001",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_sis_85c471_common_init(model);
    device_add(&kbc_ps2_ami_device);

    return ret;
}

int
machine_at_dvent4xx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/dvent4xx/Venturis466_BIOS.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add(&sis_85c471_device);
    device_add(&ide_cmd640_vlb_pri_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_SEC));
    device_add(&kbc_ps2_phoenix_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}

int
machine_at_dtk486_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/dtk486/4siw005.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_sis_85c471_common_init(model);
    device_add(&kbc_at_device);

    return ret;
}

int
machine_at_ami471_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ami471/SIS471BE.AMI",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_sis_85c471_common_init(model);
    device_add(&kbc_at_ami_device);

    return ret;
}

int
machine_at_px471_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/px471/SIS471A1.PHO",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_sis_85c471_common_init(model);
    device_add(&ide_vlb_device);
    device_add(&kbc_at_device);

    return ret;
}

int
machine_at_tg486g_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/tg486g/tg486g.bin",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);
    device_add(&amstrad_megapc_nvr_device);
    device_add(&sis_85c471_device);
    device_add(&ide_isa_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C651 | FDC37C6XX_IDE_PRI));
    device_add(&kbc_ps2_tg_ami_pci_device);

    if (gfxcard[0] != VID_INTERNAL) {
        for (uint16_t i = 0; i < 32768; i++)
            rom[i] = mem_readb_phys(0x000c0000 + i);
    }
    mem_mapping_set_addr(&bios_mapping, 0x0c0000, 0x40000);
    mem_mapping_set_exec(&bios_mapping, rom);

    return ret;
}
