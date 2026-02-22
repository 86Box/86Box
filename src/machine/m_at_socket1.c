/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Socket 168 and 1 machines.
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

/* CS4031 */
int
machine_at_cs4031_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/cs4031/CHIPS_1.AMI",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&cs4031_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* OPTi 381 */
int
machine_at_ga486l_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ga486l/ga-486l_bios.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&opti381_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* OPTi 493 */
int
machine_at_svc486wb_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/svc486wb/svc486wb-AM27C512DIP28.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&opti493_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&ide_isa_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* OPTi 498 */
int
machine_at_mvi486_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/mvi486/MVI627.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&opti498_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&ide_isa_device);
    device_add_params(&pc873xx_device, (void *) (PCX73XX_IDE_PRI | PCX730X_398));

    return ret;
}

/* SiS 401 */
static void
machine_at_sis401_common_init(const machine_t *model)
{
    machine_at_common_init(model);

    device_add(&sis_85c401_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);
}

int
machine_at_isa486_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/isa486/ISA-486.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_sis401_common_init(model);

    return ret;
}

int
machine_at_sis401_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/sis401/SIS401-2.AMI",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_sis401_common_init(model);

    return ret;
}

/* SiS 460 */
int
machine_at_av4_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/av4/amibios_486dx_isa_bios_aa4025963.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&sis_85c460_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* SiS 471 */
int
machine_at_advantage40xxd_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/advantage40xxd/AST101.09A",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add(&sis_85c471_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&um866x_device, (void *) (UM82C863F | UM866X_IDE_PRI));

    device_add(&intel_flash_bxt_device);

    return ret;
}

/* Symphony SL42C460 */
int
machine_at_dtk461_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/dtk461/DTK.BIO",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add(&sl82c461_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* VIA VT82C495 */
int
machine_at_486vchd_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/486vchd/486-4386-VC-HD.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&via_vt82c49x_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* VLSI 82C480 */
int
machine_at_vect486vl_init(const machine_t *model) // has HDC problems
{
    int ret;

    ret = bios_load_linear("roms/machines/vect486vl/aa0500.ami",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    memcpy(&rom[0x00020000], rom, 131072);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    machine_at_common_init_ex(model, 2);

    device_add(&vl82c480_device);

    device_add(&vl82c113_device);

    device_add(&ide_isa_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C651 | FDC37C6XX_IDE_PRI));

    video_reset(gfxcard[0]);

    if (gfxcard[0] != VID_INTERNAL) {
        for (uint16_t i = 0; i < 32768; i++)
            rom[i] = mem_readb_phys(0x000c0000 + i);
    }
    mem_mapping_set_addr(&bios_mapping, 0x0c0000, 0x40000);
    mem_mapping_set_exec(&bios_mapping, rom);

    return ret;
}

/* VLSI 82C481 */
int
machine_at_d824_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/d824/fts-biosupdated824noflashbiosepromv320-320334-160.bin",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    memcpy(&rom[0x00020000], rom, 131072);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    machine_at_common_init_ex(model, 2);

    device_add(&vl82c480_device);

    /*
       Technically, it should be the VL82C114 but we do not have
       a proper datasheet of it that tells us the registers.
     */
    device_add(&vl82c113_device);

    device_add(&ide_isa_device);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C651);

    video_reset(gfxcard[0]);

    if (gfxcard[0] != VID_INTERNAL) {
        for (uint16_t i = 0; i < 32768; i++)
            rom[i] = mem_readb_phys(0x000c0000 + i);
    }
    mem_mapping_set_addr(&bios_mapping, 0x0c0000, 0x40000);
    mem_mapping_set_exec(&bios_mapping, rom);

    return ret;
}

/* VLSI 82C486 */
int
machine_at_pcs44c_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pcs44c/V032004G.25",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    device_add(&vl82c486_device);
    device_add(&tulip_jumper_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&oti077_pcs44c_device);

    device_add(&vl82c113_device);

    device_add(&ide_isa_device);
    device_add_params(&pc873xx_device, (void *) (PCX73XX_IDE_PRI | PCX730X_398));

    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_sensation1_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/sensation1/P1033PCD_01.10.01_11-11-92_E687_Sensation_1_BIOS.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);

    device_add(&vl82c486_device);
    device_add(&vl82c113_device);

    device_add(&pssj_1e0_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_nsc_dp8473_device);

    /* TODO: Add onboard WD90C31 once it's implemented */

    if (sound_card_current[0] == SOUND_INTERNAL)
        machine_snd = device_add(machine_get_snd_device(machine));

    return ret;
}

int
machine_at_tuliptc38_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/tuliptc38/TULIP1.BIN",
                           0x000f0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    device_add(&vl82c486_device);
    device_add(&tulip_jumper_device);

    device_add(&vl82c113_device);

    device_add(&ide_isa_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C651 | FDC37C6XX_IDE_PRI));

    video_reset(gfxcard[0]);

    if (gfxcard[0] == VID_INTERNAL) {
        bios_load_aux_linear("roms/machines/tuliptc38/VBIOS.BIN",
                             0x000c0000, 32768, 0);

        device_add(machine_get_vid_device(machine));
    } else
        for (uint16_t i = 0; i < 32768; i++)
            rom[i] = mem_readb_phys(0x000c0000 + i);

    mem_mapping_set_addr(&bios_mapping, 0x0c0000, 0x40000);
    mem_mapping_set_exec(&bios_mapping, rom);

    return ret;
}

/* ZyMOS Poach */
int
machine_at_isa486c_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/isa486c/asus-isa-486c-401a0-040591-657e2c17a0218417632602.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&isa486c_device);
    device_add(&port_92_key_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

int
machine_at_genoa486_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/genoa486/AMI486.BIO",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&compaq_genoa_device);
    device_add(&port_92_key_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}
