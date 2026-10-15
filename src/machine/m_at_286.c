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
#include <86box/rom.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/hdc.h>
#include <86box/ibm_5161.h>
#include <86box/nvr.h>
#include <86box/port_6x.h>
#define USE_SIO_DETECT
#include <86box/sio.h>
#include <86box/serial.h>
#include <86box/video.h>
#include <86box/vid_cga.h>
#include <86box/vid_cga_comp.h>
#include <86box/flash.h>
#include <86box/machine.h>
#include <86box/port_92.h>
#include <86box/pic.h>
#include <86box/sound.h>

/* ISA */
static const device_config_t ibmat_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ibm5170_111585",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "IBM BIOS - Revision 62X082x (11/15/85)",
                .internal_name = "ibm5170_111585",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/ibmat/BIOS_5170_15NOV85_U27.BIN", "roms/machines/ibmat/BIOS_5170_15NOV85_U47.BIN", "" }
            },
            {
                .name          = "IBM BIOS - Revision 61X9266 (11/15/85, alternate)",
                .internal_name = "ibm5170_111585_alt",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/ibmat/BIOS_5170_15NOV85_U27_61X9266.BIN", "roms/machines/ibmat/BIOS_5170_15NOV85_U47_61X9265.BIN", "" }
            },
            {
                .name          = "IBM BIOS - Revision 648009x (06/10/85)",
                .internal_name = "ibm5170_061085",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/ibmat/BIOS_5170_10JUN85_U27.BIN", "roms/machines/ibmat/BIOS_5170_10JUN85_U47.BIN", "" }
            },
            {
                .name          = "IBM BIOS - Revision 618102x (01/10/84)",
                .internal_name = "ibm5170_011084",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/ibmat/BIOS_5170_10JAN84_U27.BIN", "roms/machines/ibmat/BIOS_5170_10JAN84_U47.BIN", "" }
            },
            // The following are Diagnostic ROMs.
            {
                .name          = "Supersoft Diagnostics",
                .internal_name = "diag_supersoft",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 2,
                .size          = 65536,
                .files         = { "roms/machines/diagnostic/5170_EVEN_LOW_U27_27256.bin", "roms/machines/diagnostic/5170_ODD_HIGH_U47_27256.bin", "" }
            },

            { .files_no = 0 }
        },
    },
    {
        .name           = "enable_5161",
        .description    = "IBM 5161 Expansion Unit",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ibmat_device = {
    .name          = "IBM AT",
    .internal_name = "ibmat",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ibmat_config
};

static void
machine_at_ibm_common_init(const machine_t *model)
{
    machine_at_common_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    mem_remap_top(384);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);
}

int
machine_at_ibmat_init(const machine_t *model)
{
    int         ret = 0;
    uint8_t     enable_5161;
    const char *fn[2];

    /* No ROMs available. */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    enable_5161 = machine_get_config_int("enable_5161");
    fn[0]       = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    fn[1]       = device_get_bios_file(model->device, device_get_config_bios("bios"), 1);
    ret         = bios_load_interleaved(fn[0], fn[1], 0x000f0000, 65536, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_at_ibm_common_init(model);

    if (enable_5161)
        device_add(&ibm_5161_device);

    return ret;
}

static const device_config_t ibmxt286_config[] = {
    // clang-format off
    {
        .name           = "enable_5161",
        .description    = "IBM 5161 Expansion Unit",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ibmxt286_device = {
    .name          = "IBM XT model 286",
    .internal_name = "ibmxt286",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ibmxt286_config
};

int
machine_at_ibmxt286_init(const machine_t *model)
{
    int     ret;
    uint8_t enable_5161;

    device_context(model->device);
    enable_5161 = machine_get_config_int("enable_5161");
    device_context_restore();

    ret = bios_load_interleaved("roms/machines/ibmxt286/bios_5162_21apr86_u34_78x7460_27256.bin",
                                "roms/machines/ibmxt286/bios_5162_21apr86_u35_78x7461_27256.bin",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ibm_common_init(model);

    if (enable_5161)
        device_add(&ibm_5161_device);

    return ret;
}

int
machine_at_cmdpc_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/cmdpc30/commodore pc 30 iii even.bin",
                                "roms/machines/cmdpc30/commodore pc 30 iii odd.bin",
                                0x000f8000, 32768, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_init(model);

    mem_remap_top(384);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&cbm_io_device);

    return ret;
}

int
machine_at_portableii_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleavedr("roms/machines/portableii/109740-001.rom",
                                 "roms/machines/portableii/109739-001.rom",
                                 0x000f8000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    video_reset(gfxcard[0]);

    device_add(&compaq_device);

    machine_at_common_init(model);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_portableiii_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linearr("roms/machines/portableiii/K Combined.bin",
                            0x000f8000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    video_reset(gfxcard[0]);

    if (hdc_current[0] == HDC_INTERNAL)
        device_add(&ide_isa_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&compaq_plasma_device);

    device_add(&compaq_device);

    machine_at_common_init(model);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_ft286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/ft286/286-Access methods-ROM2.BIN",
                                "roms/machines/ft286/286-Access methods-ROM4.BIN",
                                0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ibm_common_init(model);

    return ret;
}

int
machine_at_grid1520_init(const machine_t *model)
{
    int ret = 0;

    ret = bios_load_linear("roms/machines/grid1520/grid1520_891025.rom",
                           0x000f8000, 0x8000, 0);
    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);
    mem_remap_top(384);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    // for now just select CGA with amber monitor
    // device_add(&cga_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&grid1520_device);

    return ret;
}

static const device_config_t pc900_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "pc900",
        .default_int    = 0,
        .file_filter    = "",
        .spinner        = { 0 },
        .bios           = {
            {
                .name           = "AwardBIOS - Revision V2.07A (Commodore)",
                .internal_name  = "pc900",
                .bios_type      = BIOS_NORMAL,
                .files_no       = 1,
                .local          = 0,
                .size           = 32768,
                .files          = { "roms/machines/pc900/mpf_pc900_v207a.bin", "" }
            },
            {
                .name           = "AwardBIOS - Revision V2.07A.XC (Commodore)",
                .internal_name  = "pc900_v207a_xc",
                .bios_type      = BIOS_NORMAL,
                .files_no       = 1,
                .local          = 0,
                .size           = 32768,
                .files          = { "roms/machines/pc900/cbm_pc40_v207a_xc.bin", "" }
            },
            {
                .name           = "AwardBIOS - Revision V2.07B",
                .internal_name  = "pc900_v207b",
                .bios_type      = BIOS_NORMAL,
                .files_no       = 1,
                .local          = 0,
                .size           = 32768,
                .files          = { "roms/machines/pc900/mpf_pc900_v207b.bin", "" }
            },
            {
                .name           = "AwardBIOS - Revision V3.01B",
                .internal_name  = "pc900_v301b",
                .bios_type      = BIOS_NORMAL,
                .files_no       = 1,
                .local          = 0,
                .size           = 32768,
                .files          = { "roms/machines/pc900/cbm_pc40_v301b.bin", "" }
            },
            { .files_no = 0 }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t pc900_device = {
    .name           = "Multitech PC-900",
    .internal_name  = "pc900",
    .flags          = 0,
    .local          = 0,
    .init           = NULL,
    .close          = NULL,
    .reset          = NULL,
    .available      = NULL,
    .speed_changed  = NULL,
    .force_redraw   = NULL,
    .config         = pc900_config
};

int
machine_at_pc900_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available. */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000f8000, 32768, 0);
    device_context_restore();

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
machine_at_mr286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/mr286/V000B200-1",
                                "roms/machines/mr286/V000B200-2",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

uint8_t
machine_ncr_p1_handler(void)
{
    /* switch settings
     * bit 7: keyboard disable
     * bit 6: display type (0 color, 1 mono)
     * bit 5: power-on default speed (0 high, 1 low)
     * bit 4: sense RAM size (0 unsupported, 1 512k on system board)
     * bit 3: coprocessor detect
     * bit 2: unused
     * bit 1: high/auto speed
     * bit 0: dma mode
     */
    /* (B0 or F0) | 0x04 | (display on bit 6) | (fpu on bit 3) */
    return (video_is_mda() ? 0x40 : 0x00) | (hasfpu ? 0x08 : 0x00) | 0x90;
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
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

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

    machine_at_common_init(model);

    device_add(&olivetti_eva_device);
    device_add(&port_6x_olivetti_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_pxat_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/ibmatpx/BIOS ROM - PhoenixBIOS A286 - Version 1.01 - Even.bin",
                                "roms/machines/ibmatpx/BIOS ROM - PhoenixBIOS A286 - Version 1.01 - Odd.bin",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ibm_common_init(model);

    return ret;
}

int
machine_at_quadtat_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/ibmatquadtel/BIOS_30MAR90_U27_QUADTEL_ENH_286_BIOS_3.05.01_27256.BIN",
                                "roms/machines/ibmatquadtel/BIOS_30MAR90_U47_QUADTEL_ENH_286_BIOS_3.05.01_27256.BIN",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ibm_common_init(model);

    return ret;
}

int
machine_at_pb286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/pb286/LB_V332P.BIN",
                                "roms/machines/pb286/HB_V332P.BIN",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ibm_common_init(model);

    return ret;
}

int
machine_at_mbc17_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/mbc17/SAT200C_U45EVEN_FB3H2.bin",
                                "roms/machines/mbc17/SAT200C_U44ODD_FB3J2.bin",
                                0x000f8000, 32768, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);
    device_add(&sanyo_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

int
machine_at_ax286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/ax286/AM27C512@DIP28_even.BIN",
                                "roms/machines/ax286/AM27C512@DIP28_odd.BIN",
                                0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

int
machine_at_siemens_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/siemens/286BIOS.BIN",
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
machine_at_tbunk286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/ibmatami/BIOS_5170_30APR89_U27_AMI_27256.BIN",
                                "roms/machines/ibmatami/BIOS_5170_30APR89_U47_AMI_27256.BIN",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ibm_common_init(model);

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

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
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
machine_at_ftbaby286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/ftbaby286/AMI286_u17.bin",
                                "roms/machines/ftbaby286/AMI286_u18.bin",
                                0x000f8000, 32768, 0);

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

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

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
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&headland_gc10x_device);

    return ret;
}

void
machine_at_headland_common_init(const machine_t *model, int type)
{
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

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

int
machine_at_px286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/px286/KENITEC.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&neat_device);

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

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

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

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

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

static const device_t tvis_device = {
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
    .config        = NULL
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

    extern const device_t tvis_vid_device;
    device_add(&tvis_vid_device);

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

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_n8810m30_init(const machine_t *model) /* Onboard SCSI not yet emulated */
{
    int ret;

    ret = bios_load_linear("roms/machines/n8810m30/at286bios_53889.00.0.17jr.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&neat_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

/* SCAMP */
int
machine_at_pc7286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pc7286/PC7286 BIOS (AM27C010@DIP32).BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    device_add_params(&dw90c50_device, (void *) DW90C50_IDE);
    device_add(&vl82c113_device); /* The keyboard controller is part of the VL82c113. */

    device_add(&vlsi_scamp_device);

    return ret;
}

/* SCAT */
static void
machine_at_scat_init(const machine_t *model, int is_v4, int is_ami)
{
    machine_at_common_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (is_v4)
        device_add(&scat_4_device);
    else
        device_add(&scat_device);
}

int
machine_at_pc5286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pc5286/PC5286",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    /* Patch the checksum to avoid checksum error. */
    if (rom[0xffff] == 0x2c)
        rom[0xffff] = 0x2b;

    machine_at_scat_init(model, 1, 0);

    device_add(&f82c710_device);

    device_add(&ide_isa_device);

    return ret;
}

int
machine_at_gw286ct_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/gw286ct/2ctc001.bin",
                           0x000f0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_scat_init(model, 1, 0);

    device_add(&f82c710_device);

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
        device_add(machine_get_vid_device(machine));

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
