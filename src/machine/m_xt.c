/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Standard PC/AT implementation.
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2017-2020 Fred N. van Kempen.
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2025      Jasmine Iwanek.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/nmi.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/hdc.h>
#include <86box/gameport.h>
#include <86box/ibm_5161.h>
#include <86box/keyboard.h>
#include <86box/rom.h>
#include <86box/machine.h>
#include <86box/chipset.h>
#include <86box/port_6x.h>
#include <86box/video.h>

extern const device_t vendex_xt_rtc_onboard_device;

static void
machine_xt_common_init(const machine_t *model, int fixed_floppy)
{
    if ((fdc_current[0] == FDC_INTERNAL) || fixed_floppy)
        device_add(&fdc_xt_device);

    machine_common_init(model);

    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_xt);

    nmi_init();
    standalone_gameport_type = &gameport_device;
}

static const device_config_t ibmpc_config[] = {
    // clang-format off
    {
        .name = "bios",
        .description = "BIOS Version",
        .type = CONFIG_BIOS,
        .default_string = "ibm5150_5700671",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .bios = {
            { .name = "5700671 (10/19/81)", .internal_name = "ibm5150_5700671", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 40960, .files = { "roms/machines/ibmpc/BIOS_IBM5150_19OCT81_5700671_U33.BIN", "" } },
            { .name = "5700051 (04/24/81)", .internal_name = "ibm5150_5700051", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 40960, .files = { "roms/machines/ibmpc/BIOS_IBM5150_24APR81_5700051_U33.BIN", "" } },

            // GlaBIOS for IBM PC
            { .name = "GlaBIOS 0.2.5 (8088)", .internal_name = "glabios_025_8088", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 40960, .files = { "roms/machines/glabios/GLABIOS_0.2.5_8P.ROM", "" } },
            { .name = "GlaBIOS 0.2.5 (V20)", .internal_name = "glabios_025_v20", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 40960, .files = { "roms/machines/glabios/GLABIOS_0.2.5_VP.ROM", "" } },

            // The following are Diagnostic ROMs.
            { .name = "Supersoft Diagnostics", .internal_name = "diag_supersoft", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 40960, .files = { "roms/machines/diagnostic/Supersoft_PCXT_8KB.bin", "" } },
            { .name = "Ruud's Diagnostic Rom", .internal_name = "diag_ruuds", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 40960, .files = { "roms/machines/diagnostic/ruuds_diagnostic_rom_v5.3_8kb.bin", "" } },
            { .name = "XT RAM Test", .internal_name = "diag_xtramtest", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 40960, .files = { "roms/machines/diagnostic/xtramtest_8k.bin", "" } },
            { .files_no = 0 }
        },
    },
    {
        .name = "enable_5161",
        .description = "IBM 5161 Expansion Unit",
        .type = CONFIG_BINARY,
        .default_int = 0
    },
    {
        .name = "enable_basic",
        .description = "IBM Cassette Basic",
        .type = CONFIG_BINARY,
        .default_int = 1
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ibmpc_device = {
    .name          = "IBM PC (1981) Device",
    .internal_name = "ibmpc_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ibmpc_config
};

int
machine_pc_init(const machine_t *model)
{
    int         ret = 0;
    int         ret2;
    uint8_t     enable_5161;
    uint8_t     enable_basic;
    const char *fn;

    /* No ROMs available. */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    enable_5161  = machine_get_config_int("enable_5161");
    enable_basic = machine_get_config_int("enable_basic");
    fn           = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    ret          = bios_load_linear(fn, 0x000fe000, 40960, 0);
    device_context_restore();

    if (enable_basic && ret) {
        ret2 = bios_load_aux_linear("roms/machines/ibmpc/ibm-basic-1.00.rom",
                                    0x000f6000, 32768, 0);
        if (!ret2) {
            bios_load_aux_linear("roms/machines/ibmpc/IBM 5150 - Cassette BASIC version C1.00 - U29 - 5700019.bin",
                                 0x000f6000, 8192, 0);
            bios_load_aux_linear("roms/machines/ibmpc/IBM 5150 - Cassette BASIC version C1.00 - U30 - 5700027.bin",
                                 0x000f8000, 8192, 0);
            bios_load_aux_linear("roms/machines/ibmpc/IBM 5150 - Cassette BASIC version C1.00 - U31 - 5700035.bin",
                                 0x000fa000, 8192, 0);
            bios_load_aux_linear("roms/machines/ibmpc/IBM 5150 - Cassette BASIC version C1.00 - U32 - 5700043.bin",
                                 0x000fc000, 8192, 0);
        }
    }

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_pc_device);

    machine_xt_common_init(model, 0);

    if (enable_5161)
        device_add(&ibm_5161_device);

    return ret;
}

static const device_config_t ibmpc82_config[] = {
    // clang-format off
    {
        .name = "bios",
        .description = "BIOS Version",
        .type = CONFIG_BIOS,
        .default_string = "ibm5150_1501476",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .bios = {
            { .name = "1501476 (10/27/82)", .internal_name = "ibm5150_1501476", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 40960, .files = { "roms/machines/ibmpc82/BIOS_5150_27OCT82_1501476_U33.BIN", "" } },
            { .name = "5000024 (08/16/82)", .internal_name = "ibm5150_5000024", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 40960, .files = { "roms/machines/ibmpc82/BIOS_5150_16AUG82_5000024_U33.BIN", "" } },

            // GlaBIOS for IBM PC
            { .name = "GlaBIOS 0.2.5 (8088)", .internal_name = "glabios_025_8088", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 40960, .files = { "roms/machines/glabios/GLABIOS_0.2.5_8P.ROM", "" } },
            { .name = "GlaBIOS 0.2.5 (V20)", .internal_name = "glabios_025_v20", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 40960, .files = { "roms/machines/glabios/GLABIOS_0.2.5_VP.ROM", "" } },

            // The following are Diagnostic ROMs.
            { .name = "Supersoft Diagnostics", .internal_name = "diag_supersoft", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 40960, .files = { "roms/machines/diagnostic/Supersoft_PCXT_8KB.bin", "" } },
            { .name = "Ruud's Diagnostic Rom", .internal_name = "diag_ruuds", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 40960, .files = { "roms/machines/diagnostic/ruuds_diagnostic_rom_v5.3_8kb.bin", "" } },
            { .name = "XT RAM Test", .internal_name = "diag_xtramtest", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 40960, .files = { "roms/machines/diagnostic/xtramtest_8k.bin", "" } },
            { .files_no = 0 }
        },
    },
    {
        .name = "enable_5161",
        .description = "IBM 5161 Expansion Unit",
        .type = CONFIG_BINARY,
        .default_int = 1
    },
    {
        .name = "enable_basic",
        .description = "IBM Cassette Basic",
        .type = CONFIG_BINARY,
        .default_int = 1
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ibmpc82_device = {
    .name          = "IBM PC (1982) Devices",
    .internal_name = "ibmpc82_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ibmpc82_config
};

int
machine_pc82_init(const machine_t *model)
{
    int         ret = 0;
    int         ret2;
    uint8_t     enable_5161;
    uint8_t     enable_basic;
    const char *fn;

    /* No ROMs available. */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    enable_5161  = machine_get_config_int("enable_5161");
    enable_basic = machine_get_config_int("enable_basic");
    fn           = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    ret          = bios_load_linear(fn, 0x000fe000, 40960, 0);
    device_context_restore();

    if (enable_basic && ret) {
        ret2 = bios_load_aux_linear("roms/machines/ibmpc82/ibm-basic-1.10.rom",
                                    0x000f6000, 32768, 0);
        if (!ret2) {
            bios_load_aux_linear("roms/machines/ibmpc82/IBM 5150 - Cassette BASIC version C1.10 - U29 - 5000019.bin",
                                 0x000f6000, 8192, 0);
            bios_load_aux_linear("roms/machines/ibmpc82/IBM 5150 - Cassette BASIC version C1.10 - U30 - 5000021.bin",
                                 0x000f8000, 8192, 0);
            bios_load_aux_linear("roms/machines/ibmpc82/IBM 5150 - Cassette BASIC version C1.10 - U31 - 5000022.bin",
                                 0x000fa000, 8192, 0);
            bios_load_aux_linear("roms/machines/ibmpc82/IBM 5150 - Cassette BASIC version C1.10 - U32 - 5000023.bin",
                                 0x000fc000, 8192, 0);
        }
    }

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_pc82_device);

    machine_xt_common_init(model, 0);

    if (enable_5161)
        device_add(&ibm_5161_device);

    return ret;
}

static const device_config_t ibmxt_config[] = {
    // clang-format off
    {
        .name = "bios",
        .description = "BIOS Version",
        .type = CONFIG_BIOS,
        .default_string = "ibm5160_1501512_5000027",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .bios = {
            { .name = "1501512 (11/08/82)", .internal_name = "ibm5160_1501512_5000027", .bios_type = BIOS_NORMAL,
              .files_no = 2, .local = 0, .size = 65536, .files = { "roms/machines/ibmxt/BIOS_5160_08NOV82_U18_1501512.BIN", "roms/machines/ibmxt/BIOS_5160_08NOV82_U19_5000027.BIN", "" } },
            { .name = "1501512 (11/08/82) (Alt)", .internal_name = "ibm5160_1501512_6359116", .bios_type = BIOS_NORMAL,
              .files_no = 2, .local = 0, .size = 65536, .files = { "roms/machines/ibmxt/BIOS_5160_08NOV82_U18_1501512.BIN", "roms/machines/ibmxt/BIOS_5160_08NOV82_U19_6359116.BIN", "" } },
            { .name = "5000026 (08/16/82)", .internal_name = "ibm5160_5000026_5000027", .bios_type = BIOS_NORMAL,
              .files_no = 2, .local = 0, .size = 65536, .files = { "roms/machines/ibmxt/BIOS_5160_16AUG82_U18_5000026.BIN", "roms/machines/ibmxt/BIOS_5160_16AUG82_U19_5000027.BIN", "" } },
#if 0
            // GlaBIOS for IBM XT
            { .name = "GlaBIOS 0.2.5 (8088)", .internal_name = "glabios_025_8088", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 40960, .files = { "roms/machines/glabios/GLABIOS_0.2.5_8X.ROM", "" } },
            { .name = "GlaBIOS 0.2.5 (V20)", .internal_name = "glabios_025_v20", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 40960, .files = { "roms/machines/glabios/GLABIOS_0.2.5_VX.ROM", "" } },

            // The following are Diagnostic ROMs.
            { .name = "Supersoft Diagnostics", .internal_name = "diag_supersoft", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 65536, .files = { "roms/machines/diagnostic/Supersoft_PCXT_8KB.bin", "" } },
            { .name = "Ruud's Diagnostic Rom", .internal_name = "diag_ruuds", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 65536, .files = { "roms/machines/diagnostic/ruuds_diagnostic_rom_v5.3_8kb.bin", "" } },
            { .name = "XT RAM Test", .internal_name = "diag_xtramtest", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 65536, .files = { "roms/machines/diagnostic/xtramtest_8k.bin", "" } },
#endif
            { .files_no = 0 }
        },
    },
    {
        .name = "enable_5161",
        .description = "IBM 5161 Expansion Unit",
        .type = CONFIG_BINARY,
        .default_int = 1
    },
    {
        .name = "enable_basic",
        .description = "IBM Cassette Basic",
        .type = CONFIG_BINARY,
        .default_int = 1
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ibmxt_device = {
    .name          = "IBM XT (1982) Device",
    .internal_name = "ibmxt_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ibmxt_config
};

int
machine_xt_init(const machine_t *model)
{
    int         ret = 0;
    uint8_t     enable_5161;
    uint8_t     enable_basic;
    const char *fn;

    /* No ROMs available. */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    enable_5161  = machine_get_config_int("enable_5161");
    enable_basic = machine_get_config_int("enable_basic");
    fn           = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    ret          = bios_load_linear(fn, 0x000fe000, 65536, 0x6000);

    if (enable_basic && ret) {
        fn = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
        (void) bios_load_aux_linear(fn, 0x000f8000, 24576, 0);
        fn = device_get_bios_file(model->device, device_get_config_bios("bios"), 1);
        (void) bios_load_aux_linear(fn, 0x000f0000, 32768, 0);
    }
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_xt_device);

    machine_xt_common_init(model, 0);

    if (enable_5161)
        device_add(&ibm_5161_device);

    return ret;
}

int
machine_genxt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/genxt/pcxt.rom",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_xt_device);

    machine_xt_common_init(model, 0);

    return ret;
}

static const device_config_t ibmxt86_config[] = {
    // clang-format off
    {
        .name = "bios",
        .description = "BIOS Version",
        .type = CONFIG_BIOS,
        .default_string = "ibm5160_050986",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .bios = {
            { .name = "1501512 (05/09/86)", .internal_name = "ibm5160_050986", .bios_type = BIOS_NORMAL,
              .files_no = 2, .local = 0, .size = 65536, .files = { "roms/machines/ibmxt86/BIOS_5160_09MAY86_U18_59X7268_62X0890_27256_F800.BIN", "roms/machines/ibmxt86/BIOS_5160_09MAY86_U19_62X0819_68X4370_27256_F000.BIN", "" } },
            { .name = "5000026 (01/10/86)", .internal_name = "ibm5160_011086", .bios_type = BIOS_NORMAL,
              .files_no = 2, .local = 0, .size = 65536, .files = { "roms/machines/ibmxt86/BIOS_5160_10JAN86_U18_62X0851_27256_F800.BIN", "roms/machines/ibmxt86/BIOS_5160_10JAN86_U19_62X0854_27256_F000.BIN", "" } },
            { .name = "1501512 (01/10/86) (Alt)", .internal_name = "ibm5160_011086_alt", .bios_type = BIOS_NORMAL,
              .files_no = 2, .local = 0, .size = 65536, .files = { "roms/machines/ibmxt86/BIOS_5160_10JAN86_U18_62X0852_27256_F800.BIN", "roms/machines/ibmxt86/BIOS_5160_10JAN86_U19_62X0853_27256_F000.BIN", "" } },
#if 0
            // GlaBIOS for IBM XT
            { .name = "GlaBIOS 0.2.5 (8088)", .internal_name = "glabios_025_8088", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 40960, .files = { "roms/machines/glabios/GLABIOS_0.2.5_8X.ROM", "" } },
            { .name = "GlaBIOS 0.2.5 (V20)", .internal_name = "glabios_025_v20", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 40960, .files = { "roms/machines/glabios/GLABIOS_0.2.5_VX.ROM", "" } },

            // The following are Diagnostic ROMs.
            { .name = "Supersoft Diagnostics", .internal_name = "diag_supersoft", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 65536, .files = { "roms/machines/diagnostic/Supersoft_PCXT_8KB.bin", "" } },
            { .name = "Ruud's Diagnostic Rom", .internal_name = "diag_ruuds", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 65536, .files = { "roms/machines/diagnostic/ruuds_diagnostic_rom_v5.3_8kb.bin", "" } },
            { .name = "XT RAM Test", .internal_name = "diag_xtramtest", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 65536, .files = { "roms/machines/diagnostic/xtramtest_8k.bin", "" } },
#endif
            { .files_no = 0 }
        },
    },
    {
        .name = "enable_5161",
        .description = "IBM 5161 Expansion Unit",
        .type = CONFIG_BINARY,
        .default_int = 1
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ibmxt86_device = {
    .name          = "IBM XT (1986) Device",
    .internal_name = "ibmxt86_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ibmxt86_config
};

int
machine_xt86_init(const machine_t *model)
{
    int         ret = 0;
    uint8_t     enable_5161;
    const char *fn;

    /* No ROMs available. */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    enable_5161  = machine_get_config_int("enable_5161");
    fn           = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    ret          = bios_load_linear(fn, 0x000fe000, 65536, 0x6000);

    if (ret) {
        fn = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
        (void) bios_load_aux_linear(fn, 0x000f8000, 24576, 0);
        fn = device_get_bios_file(model->device, device_get_config_bios("bios"), 1);
        (void) bios_load_aux_linear(fn, 0x000f0000, 32768, 0);
    }
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_xt86_device);

    machine_xt_common_init(model, 0);

    if (enable_5161)
        device_add(&ibm_5161_device);

    return ret;
}

static void
machine_xt_clone_init(const machine_t *model, int fixed_floppy)
{
    device_add(&keyboard_xtclone_device);

    machine_xt_common_init(model, fixed_floppy);
}

int
machine_xt_americxt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/americxt/AMERICXT.ROM",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model, 0);

    return ret;
}

int
machine_xt_amixt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/amixt/ami_8088_bios_31jan89.bin",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model, 0);

    return ret;
}

// TODO
// Onboard EGA Graphics (NSI Logic EVC315-S on early boards STMicroelectronics EGA on later revisions)
// RTC
// Adaptec ACB-2072 RLL Controller Card (Optional)
// Atari PCM1 Mouse Support
int
machine_xt_ataripc3_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ataripc3/AWARD_ATARI_PC_BIOS_3.08.BIN",
                           0x000f8000, 32768, 0);
#if 0
    ret = bios_load_linear("roms/machines/ataripc3/c101701-004 308.u61",
                           0x000f8000, 0x8000, 0);
#endif

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model, 0);

    return ret;
}

int
machine_xt_znic_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/znic/ibmzen.rom",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model, 0);

    return ret;
}

int
machine_xt_dtk_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/dtk/dtk_erso_2.42_2764.bin",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model, 0);

    return ret;
}

static const device_config_t jukopc_config[] = {
    // clang-format off
    {
        .name = "bios",
        .description = "BIOS Version",
        .type = CONFIG_BIOS,
        .default_string = "jukost",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .bios = {
            { .name = "Bios 2.30", .internal_name = "jukost", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 8192, .files = { "roms/machines/jukopc/000o001.bin", "" } },
            // GlaBIOS for Juko ST
            { .name = "GlaBIOS 0.2.5 (8088)", .internal_name = "glabios_025_8088", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 8192, .files = { "roms/machines/glabios/GLABIOS_0.2.5_8S_2.ROM", "" } },
            { .name = "GlaBIOS 0.2.5 (V20)", .internal_name = "glabios_025_v20", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 8192, .files = { "roms/machines/glabios/GLABIOS_0.2.5_VS_2.ROM", "" } },
            { .files_no = 0 }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t jukopc_device = {
    .name          = "Juko ST Devices",
    .internal_name = "jukopc_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = jukopc_config
};

int
machine_xt_jukopc_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available. */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn           = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    ret          = bios_load_linear(fn, 0x000fe000, 8192, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model, 0);

    return ret;
}

int
machine_xt_openxt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/openxt/pcxt31.bin",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model, 0);

    return ret;
}

int
machine_xt_pcxt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pcxt/u18.rom",
                           0x000f8000, 65536, 0);
    if (ret) {
        bios_load_aux_linear("roms/machines/pcxt/u19.rom",
                             0x000f0000, 32768, 0);
    }

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model, 0);

    return ret;
}

int
machine_xt_pxxt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pxxt/000p001.bin",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_xt_device);

    machine_xt_common_init(model, 0);

    return ret;
}

int
machine_xt_iskra3104_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/iskra3104/198.bin",
                                "roms/machines/iskra3104/199.bin",
                                0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model, 0);

    return ret;
}

int
machine_xt_maz1016_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/maz1016/e1.bin",
                                "roms/machines/maz1016/e4.bin",
                                0x000fc000, 49152, 0);

    if (ret) {
        bios_load_aux_interleaved("roms/machines/maz1016/e2.bin",
                                  "roms/machines/maz1016/e5.bin",
                                  0x000f8000, 16384, 0);

        bios_load_aux_interleaved("roms/machines/maz1016/e3.bin",
                                  "roms/machines/maz1016/e6b.bin",
                                  0x000f4000, 16384, 0);
    }

    if (bios_only || !ret)
        return ret;

    loadfont("roms/machines/maz1016/crt-8.bin", 0);

    machine_xt_clone_init(model, 0);

    return ret;
}

int
machine_xt_pravetz16_imko4_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pravetz16/BIOS_IMKO4_FE00.bin",
                           0x000fe000, 65536, 0);
    if (ret) {
        bios_load_aux_linear("roms/machines/pravetz16/BIOS_IMKO4_F400.BIN",
                             0x000f4000, 8192, 0);

        bios_load_aux_linear("roms/machines/pravetz16/BIOS_IMKO4_F600.BIN",
                             0x000f6000, 8192, 0);

        bios_load_aux_linear("roms/machines/pravetz16/BIOS_IMKO4_FA00.BIN",
                             0x000fa000, 8192, 0);

        bios_load_aux_linear("roms/machines/pravetz16/BIOS_IMKO4_F800.BIN",
                             0x000f8000, 8192, 0);

        bios_load_aux_linear("roms/machines/pravetz16/BIOS_IMKO4_FC00.BIN",
                             0x000fc000, 8192, 0);
    }

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_pravetz_device);

    machine_xt_common_init(model, 0);

    return ret;
}

int
machine_xt_pravetz16s_cpu12p_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pravetz16s/PR16S.BIN",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_xt_device);

    machine_xt_common_init(model, 0);

    return ret;
}

int
machine_xt_micoms_xl7turbo_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/mxl7t/XL7_TURBO.BIN",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_xt_device);

    machine_xt_common_init(model, 0);

    return ret;
}

int
machine_xt_pc4i_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pc4i/NCR_PC4i_BIOSROM_1985.BIN",
                           0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model, 0);

    return ret;
}

int
machine_xt_mpc1600_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/mpc1600/mpc4.34_merged.bin",
                           0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_pc82_device);

    machine_xt_common_init(model, 0);

    return ret;
}

int
machine_xt_pcspirit_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pcspirit/u1101.bin",
                           0x000fe000, 16384, 0);

    if (ret) {
        bios_load_aux_linear("roms/machines/pcspirit/u1103.bin",
                             0x000fc000, 8192, 0);
    }

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_pc82_device);

    machine_xt_common_init(model, 0);

    return ret;
}

int
machine_xt_pc700_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pc700/multitech pc-700 3.1.bin",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_pc_device);

    machine_xt_common_init(model, 0);

    return ret;
}

int
machine_xt_pc500_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pc500/rom404.bin",
                           0x000f8000, 32768, 0);

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_pc_device);

    machine_xt_common_init(model, 0);

    return ret;
}

static const device_config_t vendex_config[] = {
    // clang-format off
    {
        .name = "bios",
        .description = "BIOS Version",
        .type = CONFIG_BIOS,
        .default_string = "vendex",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .bios = {
            { .name = "Bios 2.03C", .internal_name = "vendex", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 16384, .files = { "roms/machines/vendex/Vendex Turbo 888 XT - ROM BIOS - VER 2.03C.bin", "" } },
            // GlaBIOS for Juko ST
            { .name = "GlaBIOS 0.2.5 (8088)", .internal_name = "glabios_025_8088", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 16384, .files = { "roms/machines/glabios/GLABIOS_0.2.5_8TV.ROM", "" } },
            { .name = "GlaBIOS 0.2.5 (V20)", .internal_name = "glabios_025_v20", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 16384, .files = { "roms/machines/glabios/GLABIOS_0.2.5_VTV.ROM", "" } },
            { .files_no = 0 }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t vendex_device = {
    .name          = "Vendex 888T Devices",
    .internal_name = "vendex_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = vendex_config
};

int
machine_xt_vendex_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available. */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn           = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    ret          = bios_load_linear(fn, 0x000fc000, 16384, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model, 1);

    device_add(&vendex_xt_rtc_onboard_device);

    return ret;
}

static void
machine_xt_hyundai_common_init(const machine_t *model, int fixed_floppy)
{
    device_add(&keyboard_xt_hyundai_device);

    machine_xt_common_init(model, fixed_floppy);
}

int
machine_xt_super16t_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/super16t/Hyundai SUPER-16T - System BIOS HEA v1.12Ta (16k)(MBM27128)(1986).BIN",
                           0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    /* On-board FDC cannot be disabled */
    machine_xt_hyundai_common_init(model, 1);

    return ret;
}

int
machine_xt_super16te_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/super16te/Hyundai SUPER-16TE - System BIOS v2.00Id (16k)(D27128A)(1989).BIN",
                           0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    /* On-board FDC cannot be disabled */
    machine_xt_hyundai_common_init(model, 1);

    return ret;
}

int
machine_xt_top88_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/top88/Hyosung Topstar 88T - BIOS version 3.0.bin",
                           0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    /* On-board FDC cannot be disabled */
    machine_xt_clone_init(model, 1);

    return ret;
}

int
machine_xt_kaypropc_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/kaypropc/Kaypro_v2.03K.bin",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model, 0);

    return ret;
}

int
machine_xt_sansx16_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/sansx16/tmm27128ad.bin.bin",
                           0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    /* On-board FDC cannot be disabled */
    machine_xt_clone_init(model, 1);

    return ret;
}

int
machine_xt_bw230_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/bw230/bondwell.bin",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model, 0);

    return ret;
}

int
machine_xt_v20xt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/v20xt/V20XTBios.bin",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model, 0);

    return ret;
}

int
machine_xt_pb8810_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pb8810/pb8088-8810-633acc631aba0345517682.bin",
                           0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model, 0);

    return ret;
}

int
machine_xt_glabios_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/glabios/GLABIOS_0.2.6_8X_012324.ROM",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_xt_device);

    machine_xt_common_init(model, 0);

    return ret;
}
