/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of 286 machines.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          EngiNerd <webmaster.crrc@yahoo.it>
 *
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2020-2025 EngiNerd.
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
#include <86box/rom.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/hdc.h>
#include <86box/nvr.h>
#include <86box/port_6x.h>
#define USE_SIO_DETECT
#include <86box/sio.h>
#include <86box/serial.h>
#include <86box/video.h>
#include <86box/vid_cga.h>
#include <86box/flash.h>
#include <86box/machine.h>

/* ISA */
int
machine_at_mr286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/mr286/V000B200-1",
                                "roms/machines/mr286/V000B200-2",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);
    device_add(&kbc_at_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/*
 * Current bugs:
 * - ctrl-alt-del produces an 8042 error
 */
int
machine_at_pc8_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/pc8/ncr_35117_u127_vers.4-2.bin",
                                "roms/machines/pc8/ncr_35116_u113_vers.4-2.bin",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add(&kbc_at_ncr_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

int
machine_at_m290_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/m290/m290_pep3_1.25.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 6);
    device_add(&amstrad_megapc_nvr_device);

    device_add(&olivetti_eva_device);
    device_add(&port_6x_olivetti_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&kbc_at_olivetti_device);

    return ret;
}

/* C&T PC/AT */
static void
machine_at_ctat_common_init(const machine_t *model)
{
    machine_at_common_init(model);

    device_add(&cs8220_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&kbc_at_phoenix_device);
}

int
machine_at_dells200_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/dells200/dellL200256_LO_@DIP28.BIN",
                                "roms/machines/dells200/Dell200256_HI_@DIP28.BIN",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ctat_common_init(model);

    return ret;
}

int
machine_at_super286c_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/super286c/hyundai_award286.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&kbc_at_ami_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&cs8220_device);

    return ret;
}

int
machine_at_at122_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/at122/FINAL.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ctat_common_init(model);

    return ret;
}

int
machine_at_tuliptc7_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleavedr("roms/machines/tuliptc7/tc7be.bin",
                                 "roms/machines/tuliptc7/tc7bo.bin",
                                 0x000f8000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ctat_common_init(model);

    return ret;
}

int
machine_at_wellamerastar_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/wellamerastar/W_3.031_L.BIN",
                                "roms/machines/wellamerastar/W_3.031_H.BIN",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ctat_common_init(model);

    return ret;
}

/* GC103 */
int
machine_at_quadt286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/quadt286/QUADT89L.ROM",
                                "roms/machines/quadt286/QUADT89H.ROM",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add(&kbc_at_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&headland_gc10x_device);

    return ret;
}

void
machine_at_headland_common_init(const machine_t *model, int type)
{
    device_add(&kbc_at_ami_device);

    if ((type != 2) && (fdc_current[0] == FDC_INTERNAL))
        device_add(&fdc_at_device);

    if (type == 2)
        device_add(&headland_ht18b_device);
    else if (type == 1)
        device_add(&headland_gc113_device);
    else
        device_add(&headland_gc10x_device);
}

int
machine_at_tg286m_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/tg286m/ami.bin",
                           0x000f0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);

    machine_at_headland_common_init(model, 1);

    return ret;
}

// TODO
// Onboard Paradise PVGA1A-JK VGA Graphics
// Data Technology Corporation DTC7187 RLL Controller (Optional)
int
machine_at_ataripc4_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/ataripc4/AMI_PC4X_1.7_EVEN.BIN",
                                "roms/machines/ataripc4/AMI_PC4X_1.7_ODD.BIN",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&neat_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&kbc_at_ami_device);

    return ret;
}

int
machine_at_neat_ami_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ami286/AMIC206.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&neat_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&kbc_at_ami_device);

    return ret;
}

int
machine_at_3302_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/3302/f000-flex_drive_test.bin",
                           0x000f0000, 65536, 0);

    if (ret) {
        ret &= bios_load_aux_linear("roms/machines/3302/f800-setup_ncr3.5-013190.bin",
                                    0x000f8000, 32768, 0);
    }

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);
    device_add(&neat_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    device_add(&kbc_at_ncr_device);

    return ret;
}

int
machine_at_px286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/px286/KENITEC.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add(&kbc_at_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&neat_device);

    return ret;
}

/* SCAT */
static void
machine_at_scat_init(const machine_t *model, int is_v4, int is_ami)
{
    machine_at_common_init(model);

    if (machines[machine].bus_flags & MACHINE_BUS_PS2) {
        if (is_ami)
            device_add(&kbc_ps2_ami_device);
        else
            device_add(&kbc_ps2_device);
    } else {
        if (is_ami)
            device_add(&kbc_at_ami_device);
        else
            device_add(&kbc_at_device);
    }

    if (is_v4)
        device_add(&scat_4_device);
    else
        device_add(&scat_device);
}

int
machine_at_gw286ct_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/gw286ct/2ctc001.bin",
                           0x000f0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    device_add(&f82c710_device);

    machine_at_scat_init(model, 1, 0);

    device_add(&ide_isa_device);

    return ret;
}

int
machine_at_gdc212m_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/gdc212m/gdc212m_72h.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_scat_init(model, 0, 1);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&ide_isa_device);

    return ret;
}

int
machine_at_award286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/award286/award.bin",
                           0x000f0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_scat_init(model, 0, 1);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&ide_isa_device);

    return ret;
}

int
machine_at_super286tr_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/super286tr/hyundai_award286.bin",
                           0x000f0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_scat_init(model, 0, 1);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

int
machine_at_drsm35286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/drsm35286/syab04-665821fb81363428830424.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;
	
    device_add(&ide_isa_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C651 | FDC37C6XX_IDE_PRI));
   
    machine_at_scat_init(model, 1, 0);
	
    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}

int
machine_at_deskmaster286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/deskmaster286/SAMSUNG-DESKMASTER-28612-ROM.BIN",
                           0x000f0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_scat_init(model, 0, 1);

    device_add(&f82c710_device);
        
    device_add(&ide_isa_device);

    return ret;
}

int
machine_at_spc4200p_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/spc4200p/u8.01",
                           0x000f0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_scat_init(model, 0, 1);

    device_add(&f82c710_device);

    device_add(&ide_isa_device);

    return ret;
}

int
machine_at_spc4216p_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/spc4216p/7101.U8",
                                "roms/machines/spc4216p/AC64.U10",
                                0x000f0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_scat_init(model, 1, 1);

    device_add(&f82c710_device);

    return ret;
}

int
machine_at_spc4620p_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/spc4620p/31005h.u8",
                                "roms/machines/spc4620p/31005h.u10",
                                0x000f0000, 131072, 0x8000);

    if (bios_only || !ret)
        return ret;

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&ati28800k_spc4620p_device);

    machine_at_scat_init(model, 1, 1);

    device_add(&f82c710_device);

    device_add(&ide_isa_device);

    return ret;
}

int
machine_at_senor_scat286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/senor286/AMI-DSC2-1115-061390-K8.rom",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_scat_init(model, 0, 1);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}
