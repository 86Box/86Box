/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of 386SX machines.
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
#include <86box/sound.h>

/* ISA */
/*
 * Current bugs:
 * - soft-reboot after saving CMOS settings/pressing ctrl-alt-del produces an 8042 error
 */
int
machine_at_pc916sx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/pc916sx/ncr_386sx_u46-17_7.3.bin",
                                "roms/machines/pc916sx/ncr_386sx_u12-19_7.3.bin",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    mem_remap_top(384);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

int
machine_at_quadt386sx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/quadt386sx/QTC-SXM-EVEN-U3-05-07.BIN",
                                "roms/machines/quadt386sx/QTC-SXM-ODD-U3-05-07.BIN",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&headland_gc10x_device);

    return ret;
}

/* ACC2036 */
static const device_config_t pbl300sx_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "pbl300sx",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Phoenix ROM BIOS PLUS 1.10 - Revision 19910723091302",
                .internal_name = "pbl300sx_1991",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/pbl300sx/V1.10_1113_910723.bin", "" }
            },
            {
                .name          = "Phoenix ROM BIOS PLUS 1.10 - Revision 19920910",
                .internal_name = "pbl300sx",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/pbl300sx/pb_l300sx_1992.bin", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t pbl300sx_device = {
    .name          = "Packard Bell Legend 300SX",
    .internal_name = "pbl300sx_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pbl300sx_config
};

int
machine_at_pbl300sx_init(const machine_t *model)
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

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add(&acc2036_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&um866x_device, (void *) (UM82C862F | UM866X_IDE_PRI));

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}

/* ALi M1217 */
int
machine_at_sbc350a_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/sbc350a/350a.rom",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&ali1217_device);
    device_add(&ide_isa_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_PRI));

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_arb1374_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/arb1374/1374s.rom",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&ali1217_device);
    device_add(&ide_isa_device);
    device_add_params(&w83877_device, (void *) (W83877F | W83877_3F0 | W83XX7_IDE_PRI));

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_flytech386_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/flytech386/FLYTECH.BIO",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&ali1217_device);
    device_add(&ide_isa_device);
    device_add_params(&w837x7_device, (void *) (W83787F | W837X7_KEY_89 | W83XX7_IDE_PRI | W837X7_IDE_START));

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&tvga8900d_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

static const device_config_t c325ax_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "325ax",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios = {
            {
                .name          = "AMIBIOS 070791",
                .internal_name = "325ax",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/325ax/M27C512.BIN", "" }
            },
            {
                .name          = "MR BIOS V1.41",
                .internal_name = "mr1217",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/325ax/mrbios.BIN", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t c325ax_device = {
    .name          = "Chaintech 3xxAX/AXB",
    .internal_name = "325ax_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = c325ax_config
};

int
machine_at_325ax_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000f0000, 65536, 0);

    machine_at_common_init(model);

    device_add(&ali1217_device);
    device_add(&fdc_at_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

/* ALi M1409 */
int
machine_at_acer100t_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/acer100t/acer386.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ps2_ide_init(model);

    device_add(&ali1409_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&oti077_acer100t_device);

    device_add_params(&pc87310_device, (void *) (PC87310_ALI));

    return ret;
}

/* HT18 */
int
machine_at_ama932j_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ama932j/ami.bin",
                           0x000f0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&oti067_ama932j_device);

    machine_at_headland_common_init(model, 2);

    device_add_params(&pc87310_device, (void *) (PC87310_ALI));

    return ret;
}

int
machine_at_tandy1000rsx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/tandy1000rsx/tandy-1000rsx-1-10.00.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);

    device_add(&headland_ht18c_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&pssj_1e0_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}

/* Intel 82335 */
int
machine_at_adi386sx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/adi386sx/3iip001l.bin",
                                "roms/machines/adi386sx/3iip001h.bin",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&intel_82335_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

int
machine_at_shuttle386sx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/shuttle386sx/386-Shuttle386SX-Even.BIN",
                                "roms/machines/shuttle386sx/386-Shuttle386SX-Odd.BIN",
                                0x000f0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&intel_82335_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* NEAT */
int
machine_at_cmdsl386sx16_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/cmdsl386sx16/cbm-sl386sx-bios-lo-v1.04-390914-04.bin",
                                "roms/machines/cmdsl386sx16/cbm-sl386sx-bios-hi-v1.04-390915-04.bin",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&neat_device);
    device_add(&ide_isa_device);
    /* Two serial ports - on the real hardware SL386SX-16, they are on the single UMC UM82C452. */
    device_add_inst(&ns16450_device, 1);
    device_add_inst(&ns16450_device, 2);

    return ret;
}

int
machine_at_neat_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/dtk386/3cto001.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_init(model);

    device_add(&neat_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

int
machine_at_p3345_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/p3345/BIOS_EVEN.BIN",
                                "roms/machines/p3345/BIOS_ODD.BIN",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&neat_device);
    device_add(&ide_isa_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* NEATsx */
int
machine_at_if386sx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/if386sx/OKI_IF386SX_odd.bin",
                                "roms/machines/if386sx/OKI_IF386SX_even.bin",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&neat_sx_device);

    device_add(&if386jega_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    /*
       One serial port - on the real hardware IF386AX, it is on the VL 16C451,
       alognside the bidirectional parallel port.
     */
    device_add_inst(&ns16450_device, 1);

    return ret;
}

/* OPTi 283 */
int
machine_at_svc386sxp1_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/svc386sxp1/svc-386sx-am27c512dip28-6468c04f09d89320349795.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&opti283_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* OPTi 291 */
int
machine_at_awardsx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/awardsx/Unknown 386SX OPTi291 - Award (original).BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_init(model);

    device_add(&opti291_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* SCAMP */
int
machine_at_cmdsl386sx25_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/cmdsl386sx25/f000.rom",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&gd5402_onboard_commodore_device);

    machine_at_common_init(model);

    device_add(&ide_isa_device);

    device_add_params(&pc87310_device, (void *) (PC87310_ALI));
    device_add(&vl82c113_device); /* The keyboard controller is part of the VL82c113. */

    device_add(&vlsi_scamp_device);

    return ret;
}

static void
machine_at_scamp_common_init(const machine_t *model, int is_ps2)
{
    machine_at_common_ide_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&vlsi_scamp_device);
}

int
machine_at_dataexpert386sx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/dataexpert386sx/5e9f20e5ef967717086346.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_scamp_common_init(model, 0);

    return ret;
}

static const device_config_t dells333sl_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "dells333sl",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .bios           = {
            {
                .name          = "Phoenix ROM BIOS PLUS 1.10 - Revision J01 (Jostens Learning Corporation OEM)",
                .internal_name = "dells333sl_j01",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/dells333sl/DELL386.BIN", "" }
            },
            {
                .name          = "Phoenix ROM BIOS PLUS 1.10 - Revision A02",
                .internal_name = "dells333sl",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/dells333sl/Dell_386SX_30807_UBIOS_B400_VLSI_VL82C311_Cirrus_Logic_GD5420.bin", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t dells333sl_device = {
    .name          = "Dell System 333s/L",
    .internal_name = "dells333sl_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = dells333sl_config
};

int
machine_at_dells333sl_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000e0000, 262144, 0);
    memcpy(rom, &(rom[0x00020000]), 131072);
    mem_mapping_set_addr(&bios_mapping, 0x0c0000, 0x40000);
    mem_mapping_set_exec(&bios_mapping, rom);
    device_context_restore();

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    machine_at_common_init(model);

    device_add(&ide_isa_device);

    device_add_params(&pc873xx_device, (void *) (PCX73XX_IDE_PRI | PCX730X_26E));
    device_add(&vl82c113_device); /* The keyboard controller is part of the VL82c113. */

    device_add(&vlsi_scamp_device);

    return ret;
}

int
machine_at_spc6033p_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/spc6033p/phoenix.BIN",
                           0x000f0000, 65536, 0x10000);

    if (bios_only || !ret)
        return ret;

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&ati28800k_spc6033p_device);

    machine_at_scamp_common_init(model, 1);

    return ret;
}

/* SCATsx */
static void
machine_at_scatsx_init(const machine_t *model)
{
    machine_at_common_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&scat_sx_device);
}

int
machine_at_kmxc02_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/kmxc02/3ctm005.bin",
                           0x000f0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_scatsx_init(model);

    return ret;
}

/* WD76C10 */
int
machine_at_wd76c10_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/megapc/41651-bios lo.u18",
                                "roms/machines/megapc/211253-bios hi.u19",
                                0x000f0000, 65536, 0x08000);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&wd76c10_device);

    return ret;
}
