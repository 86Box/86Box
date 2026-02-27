/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of PC and XT machines.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2008-2025 Sarah Walker.
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2017-2025 Fred N. van Kempen.
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
#include <86box/lpt.h>
#include <86box/hdc.h>
#include <86box/gameport.h>
#include <86box/serial.h>
#include <86box/sio.h>
#include <86box/ibm_5161.h>
#include <86box/isartc.h>
#include <86box/keyboard.h>
#include <86box/rom.h>
#include <86box/machine.h>
#include <86box/nvr.h>
#include <86box/chipset.h>
#include <86box/port_6x.h>
#include <86box/video.h>

/* 8088 */
static void
machine_xt_common_init(const machine_t *model, int fixed_floppy)
{
    if ((fdc_current[0] == FDC_INTERNAL) || fixed_floppy)
        device_add(&fdc_xt_device);

    machine_common_init(model);

    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_xt);

    nmi_init();
    standalone_gameport_type = &gameport_200_device;
}

static const device_config_t ibmpc_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ibm5150_5700671",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "5700671 (10/19/81)",
                .internal_name = "ibm5150_5700671",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 40960,
                .files         = { "roms/machines/ibmpc/BIOS_IBM5150_19OCT81_5700671_U33.BIN", "" }
            },
            {
                .name          = "5700051 (04/24/81)",
                .internal_name = "ibm5150_5700051",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 40960,
                .files         = { "roms/machines/ibmpc/BIOS_IBM5150_24APR81_5700051_U33.BIN", "" }
            },

            // GLaBIOS for IBM PC
            {
                .name          = "GLaBIOS 0.4.0 (8088)",
                .internal_name = "glabios_040_8088",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 40960,
                .files         = { "roms/machines/glabios/GLABIOS_0.4.0_8P.ROM", "" }
            },
            {
                .name          = "GLaBIOS 0.4.0 (V20)",
                .internal_name = "glabios_040_v20",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 40960,
                .files         = { "roms/machines/glabios/GLABIOS_0.4.0_VP.ROM", "" }
            },

            // The following are Diagnostic ROMs.
            {
                .name          = "Supersoft Diagnostics",
                .internal_name = "diag_supersoft",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 40960,
                .files         = { "roms/machines/diagnostic/Supersoft_PCXT_8KB.bin", "" }
            },
            {
                .name          = "Ruud's Diagnostic Rom",
                .internal_name = "diag_ruuds",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 40960,
                .files         = { "roms/machines/diagnostic/ruuds_diagnostic_rom_v5.4_8kb.bin", "" }
            },
            {
                .name          = "XT RAM Test",
                .internal_name = "diag_xtramtest",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 40960,
                .files         = { "roms/machines/diagnostic/xtramtest_8k.bin", "" }
            },
            { .files_no = 0 }
        }
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
    {
        .name           = "enable_basic",
        .description    = "IBM Cassette Basic",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ibmpc_device = {
    .name          = "IBM PC (1981)",
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
machine_ibmpc_init(const machine_t *model)
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

    device_add(&kbc_pc_device);

    machine_xt_common_init(model, 0);

    if (enable_5161)
        device_add(&ibm_5161_device);

    return ret;
}

static const device_config_t ibmpc82_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ibm5150_1501476",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "1501476 (10/27/82)",
                .internal_name = "ibm5150_1501476",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 40960,
                .files         = { "roms/machines/ibmpc82/BIOS_5150_27OCT82_1501476_U33.BIN", "" }
            },
            {
                .name          = "5000024 (08/16/82)",
                .internal_name = "ibm5150_5000024",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 40960,
                .files         = { "roms/machines/ibmpc82/BIOS_5150_16AUG82_5000024_U33.BIN", "" }
            },

            // GLaBIOS for IBM PC
            {
                .name          = "GLaBIOS 0.4.0 (8088)",
                .internal_name = "glabios_040_8088",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 40960,
                .files         = { "roms/machines/glabios/GLABIOS_0.4.0_8P.ROM", "" }
            },
            {
                .name          = "GLaBIOS 0.4.0 (V20)",
                .internal_name = "glabios_040_v20",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 40960,
                .files         = { "roms/machines/glabios/GLABIOS_0.4.0_VP.ROM", "" }
            },

            // The following are Diagnostic ROMs.
            {
                .name          = "Supersoft Diagnostics",
                .internal_name = "diag_supersoft",
                .bios_type = BIOS_NORMAL,
                .files_no = 1,
                .local = 0,
                .size = 40960,
                .files = { "roms/machines/diagnostic/Supersoft_PCXT_8KB.bin", "" }
            },
            {
                .name          = "Ruud's Diagnostic Rom",
                .internal_name = "diag_ruuds",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 40960,
                .files         = { "roms/machines/diagnostic/ruuds_diagnostic_rom_v5.4_8kb.bin", "" }
            },
            {
                .name          = "XT RAM Test",
                .internal_name = "diag_xtramtest",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 40960,
                .files         = { "roms/machines/diagnostic/xtramtest_8k.bin", "" }
            },
            { .files_no = 0 }
        }
    },
    {
        .name           = "enable_5161",
        .description    = "IBM 5161 Expansion Unit",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "enable_basic",
        .description    = "IBM Cassette Basic",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ibmpc82_device = {
    .name          = "IBM PC (1982)",
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
machine_ibmpc82_init(const machine_t *model)
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

    device_add(&kbc_pc82_device);

    machine_xt_common_init(model, 0);

    if (enable_5161)
        device_add(&ibm_5161_device);

    return ret;
}

static const device_config_t ibmxt_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ibm5160_1501512_5000027",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "1501512 (11/08/82)",
                .internal_name = "ibm5160_1501512_5000027",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/ibmxt/BIOS_5160_08NOV82_U18_1501512.BIN",
                                   "roms/machines/ibmxt/BIOS_5160_08NOV82_U19_5000027.BIN", "" }
            },
            {
                .name          = "1501512 (11/08/82) (Alt)",
                .internal_name = "ibm5160_1501512_6359116",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/ibmxt/BIOS_5160_08NOV82_U18_1501512.BIN",
                                   "roms/machines/ibmxt/BIOS_5160_08NOV82_U19_6359116.BIN", "" }
            },
            {
                .name          = "5000026 (08/16/82)",
                .internal_name = "ibm5160_5000026_5000027",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/ibmxt/BIOS_5160_16AUG82_U18_5000026.BIN",
                                   "roms/machines/ibmxt/BIOS_5160_16AUG82_U19_5000027.BIN", "" }
            },

            // GLaBIOS for IBM XT
            {
                .name          = "GLaBIOS 0.4.0 (8088)",
                .internal_name = "glabios_040_8088",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 1,
                .size          = 40960,
                .files         = { "roms/machines/glabios/GLABIOS_0.4.0_8X.ROM",
                                   "roms/machines/ibmxt/BIOS_5160_08NOV82_U19_5000027.BIN", "" }
            },
            {
                .name          = "GLaBIOS 0.4.0 (V20)",
                .internal_name = "glabios_040_v20",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 1,
                .size          = 40960,
                .files         = { "roms/machines/glabios/GLABIOS_0.4.0_VX.ROM",
                                   "roms/machines/ibmxt/BIOS_5160_08NOV82_U19_5000027.BIN", "" }
            },

            // The following are Diagnostic ROMs.
            {
                .name          = "Supersoft Diagnostics",
                .internal_name = "diag_supersoft",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 2,
                .size          = 65536,
                .files         = { "roms/machines/diagnostic/Supersoft_PCXT_32KB.bin",
                                   "roms/machines/ibmxt/BIOS_5160_08NOV82_U19_5000027.BIN", "" }
            },
            {
                .name          = "Ruud's Diagnostic Rom",
                .internal_name = "diag_ruuds",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 2,
                .size          = 65536,
                .files         = { "roms/machines/diagnostic/ruuds_diagnostic_rom_v5.4_32kb.bin",
                                   "roms/machines/ibmxt/BIOS_5160_08NOV82_U19_5000027.BIN", "" }
            },
            {
                .name          = "XT RAM Test",
                .internal_name = "diag_xtramtest",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 2,
                .size          = 65536,
                .files         = { "roms/machines/diagnostic/xtramtest_32k.bin",
                                   "roms/machines/ibmxt/BIOS_5160_08NOV82_U19_5000027.BIN", "" }
            },
            { .files_no = 0 }
        }
    },
    {
        .name           = "enable_5161",
        .description    = "IBM 5161 Expansion Unit",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "enable_basic",
        .description    = "IBM Cassette Basic",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ibmxt_device = {
    .name          = "IBM XT (1982)",
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
machine_ibmxt_init(const machine_t *model)
{
    int         ret = 0;
    uint8_t     enable_5161;
    uint8_t     enable_basic;
    const char *fn;
    uint16_t    offset = 0;
    uint32_t    local  = 0;

    /* No ROMs available. */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    enable_5161  = machine_get_config_int("enable_5161");
    enable_basic = machine_get_config_int("enable_basic");
    fn           = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    local        = device_get_bios_local(model->device, device_get_config_bios("bios"));

    if (local == 0) // Offset for stock roms
        offset = 0x6000;
    ret = bios_load_linear(fn, 0x000fe000, 65536, offset);

    if (enable_basic && ret) {
        if (local == 0) { // needed for stock roms
            fn = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
            (void) bios_load_aux_linear(fn, 0x000f8000, 24576, 0);
        }
        fn = device_get_bios_file(model->device, device_get_config_bios("bios"), 1);
        /* On the real machine, the BASIC is repeated. */
        (void) bios_load_aux_linear(fn, 0x000f0000, 8192, 0);
        (void) bios_load_aux_linear(fn, 0x000f2000, 8192, 0);
        (void) bios_load_aux_linear(fn, 0x000f4000, 8192, 0);
        (void) bios_load_aux_linear(fn, 0x000f6000, 8192, 0);
    }
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    device_add(&kbc_xt_device);

    machine_xt_common_init(model, 0);

    if (enable_5161)
        device_add(&ibm_5161_device);

    return ret;
}

static const device_config_t ibmxt86_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ibm5160_050986",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "1501512 (05/09/86)",
                .internal_name = "ibm5160_050986",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/ibmxt86/BIOS_5160_09MAY86_U18_59X7268_62X0890_27256_F800.BIN",
                                   "roms/machines/ibmxt86/BIOS_5160_09MAY86_U19_62X0819_68X4370_27256_F000.BIN", "" }
            },
            {
                .name          = "5000026 (01/10/86)",
                .internal_name = "ibm5160_011086",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/ibmxt86/BIOS_5160_10JAN86_U18_62X0851_27256_F800.BIN",
                                   "roms/machines/ibmxt86/BIOS_5160_10JAN86_U19_62X0854_27256_F000.BIN", "" }
            },
            {
                .name          = "1501512 (01/10/86) (Alt)",
                .internal_name = "ibm5160_011086_alt",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/ibmxt86/BIOS_5160_10JAN86_U18_62X0852_27256_F800.BIN",
                                   "roms/machines/ibmxt86/BIOS_5160_10JAN86_U19_62X0853_27256_F000.BIN", "" }
            },

            // GLaBIOS for IBM XT
            {
                .name          = "GLaBIOS 0.4.0 (8088)",
                .internal_name = "glabios_040_8088",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 1,
                .size          = 65536,
                .files         = { "roms/machines/glabios/GLABIOS_0.4.0_8X.ROM",
                                   "roms/machines/ibmxt86/BIOS_5160_09MAY86_U19_62X0819_68X4370_27256_F000.BIN", "" }
            },
            {
                .name          = "GLaBIOS 0.4.0 (V20)",
                .internal_name = "glabios_040_v20",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 1,
                .size          = 65536,
                .files         = { "roms/machines/glabios/GLABIOS_0.4.0_VX.ROM",
                                   "roms/machines/ibmxt86/BIOS_5160_09MAY86_U19_62X0819_68X4370_27256_F000.BIN", "" }
            },

            // The following are Diagnostic ROMs.
            {
                .name          = "Supersoft Diagnostics",
                .internal_name = "diag_supersoft",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 2,
                .size          = 65536,
                .files         = { "roms/machines/diagnostic/Supersoft_PCXT_32KB.bin",
                                   "roms/machines/ibmxt86/BIOS_5160_09MAY86_U19_62X0819_68X4370_27256_F000.BIN", "" }
            },
            {
                .name          = "Ruud's Diagnostic Rom",
                .internal_name = "diag_ruuds",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 2,
                .size          = 65536,
                .files         = { "roms/machines/diagnostic/ruuds_diagnostic_rom_v5.4_32kb.bin", "roms/machines/ibmxt86/BIOS_5160_09MAY86_U19_62X0819_68X4370_27256_F000.BIN", "" }
            },
            {
                .name          = "XT RAM Test",
                .internal_name = "diag_xtramtest",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 2,
                .size          = 65536,
                .files         = { "roms/machines/diagnostic/xtramtest_32k.bin", "roms/machines/ibmxt86/BIOS_5160_09MAY86_U19_62X0819_68X4370_27256_F000.BIN", "" }
            },

            { .files_no = 0 }
        },
    },
    {
        .name           = "enable_5161",
        .description    = "IBM 5161 Expansion Unit",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ibmxt86_device = {
    .name          = "IBM XT (1986)",
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
machine_ibmxt86_init(const machine_t *model)
{
    int         ret = 0;
    uint8_t     enable_5161;
    const char *fn;
    uint16_t    offset = 0;
    uint32_t    local  = 0;

    /* No ROMs available. */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    enable_5161 = machine_get_config_int("enable_5161");
    fn          = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    local       = device_get_bios_local(model->device, device_get_config_bios("bios"));

    if (local == 0) // Offset for stock roms
        offset = 0x6000;
    ret = bios_load_linear(fn, 0x000fe000, 65536, offset);

    if (ret) {
        if (local == 0) { // needed for stock roms
            fn = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
            (void) bios_load_aux_linear(fn, 0x000f8000, 24576, 0);
        }
        fn = device_get_bios_file(model->device, device_get_config_bios("bios"), 1);
        (void) bios_load_aux_linear(fn, 0x000f0000, 32768, 0);
    }
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    device_add(&kbc_xt86_device);

    machine_xt_common_init(model, 0);

    if (enable_5161)
        device_add(&ibm_5161_device);

    return ret;
}

static void
machine_xt_clone_init(const machine_t *model, int fixed_floppy)
{
    device_add(&kbc_xtclone_device);

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

/*
   TODO:
   - Onboard EGA Graphics (NSI Logic EVC315-S on early boards
                           STMicroelectronics EGA on later revisions);
   - RTC;
   - Adaptec ACB-2072 RLL Controller Card (Optional);
   - Atari PCM1 Mouse Support.
 */
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
machine_xt_mpc1600_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/mpc1600/mpc4.34_merged.bin",
                           0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    device_add(&kbc_pc82_device);

    machine_xt_common_init(model, 0);

    return ret;
}

int
machine_xt_compaq_portable_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/portable/compaq portable plus 100666-001 rev c u47.bin",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_common_init(model);

    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_xt);

    device_add(&kbc_xt_compaq_device);
    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_xt_device);
    nmi_init();
    if (joystick_type[0])
        device_add(&gameport_200_device);

    lpt_t *lpt = device_add_inst(&lpt_port_device, 1);
    lpt_port_setup(lpt, LPT_MDA_ADDR);
    lpt_set_3bc_used(1);

    return ret;
}

static const device_config_t dtk_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "dtk",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios = {
            {
                .name          = "2.39",
                .internal_name = "dtk_239",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 8192,
                .files         = { "roms/machines/dtk/PIM-TB10-Z.BIN", ""}
            },
            {
                .name          = "2.42",
                .internal_name = "dtk",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 8192,
                .files         = { "roms/machines/dtk/dtk_erso_2.42_2764.bin", ""}
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t dtk_device = {
    .name          = "DTK PIM-TB10-Z",
    .internal_name = "dtk_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = dtk_config
};

int
machine_xt_dtk_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available. */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000fe000, 8192, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model, 0);

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

    device_add(&kbc_pc82_device);

    machine_xt_common_init(model, 0);

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

    device_add(&kbc_xt_device);

    machine_xt_common_init(model, 0);

    return ret;
}

int
machine_xt_glabios_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/glabios/GLABIOS_0.4.0_8X.ROM",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    device_add(&kbc_xt_device);

    machine_xt_common_init(model, 0);

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

static void
machine_xt_hyundai_common_init(const machine_t *model, int fixed_floppy)
{
    device_add(&kbc_xt_hyundai_device);

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

static const device_config_t jukopc_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "jukost",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Bios 2.30",
                .internal_name = "jukost",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 8192,
                .files         = { "roms/machines/jukopc/000o001.bin", "" }
            },

            // GLaBIOS for Juko ST
            {
                .name          = "GLaBIOS 0.4.0 (8088)",
                .internal_name = "glabios_040_8088",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 8192,
                .files         = { "roms/machines/glabios/GLABIOS_0.4.0_8S.ROM", "" }
            },
            {
                .name          = "GLaBIOS 0.4.0 (V20)",
                .internal_name = "glabios_040_v20",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 8192,
                .files         = { "roms/machines/glabios/GLABIOS_0.4.0_VS.ROM", "" }
            },

            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t jukopc_device = {
    .name          = "Juko ST",
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
    fn  = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000fe000, 8192, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model, 0);

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
machine_xt_micoms_xl7turbo_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/mxl7t/XL7_TURBO.BIN",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    device_add(&kbc_xt_device);

    machine_xt_common_init(model, 0);

    return ret;
}

static const device_config_t pc500_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "pc500_330",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "3.30",
                .internal_name = "pc500_330",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 8192,
                .files         = { "roms/machines/pc500/rom330.bin", "" }
            },
            {
                .name          = "3.1",
                .internal_name = "pc500_310",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 8192,
                .files         = { "roms/machines/pc500/rom310.bin", "" }
            },
            { .files_no = 0 }
        }
    },
    {
        .name           = "rtc_irq",
        .description    = "RTC IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = -1 },
            { .description = "IRQ 2",    .value =  2 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "rtc_port",
        .description    = "RTC Port Address",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value =     0 },
            { .description = "2C0H",     .value = 0x2c0 },
            { .description = "300H",     .value = 0x300 },
            { .description = ""                         }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t pc500_device = {
    .name          = "Multitech PC-500 / Franklin PC 8000",
    .internal_name = "pc500_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pc500_config
};

int
machine_xt_pc500_init(const machine_t *model)
{
    int         ret      = 0;
    int         rtc_port = 0;
    const char *fn;

    /* No ROMs available. */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    rtc_port = machine_get_config_int("rtc_port");
    fn       = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    ret      = bios_load_linear(fn, 0x000fe000, 8192, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    device_add(&kbc_pc82_device);

    machine_xt_common_init(model, 0);

    if (rtc_port != 0)
        device_add(&rtc58167_device);

    return ret;
}

static const device_config_t pc500plus_config[] = {
    // clang-format off
    {
        .name       = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "pc500plus",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .bios           = {
            {
                .name          = "4.06",
                .internal_name = "pc500plus_406",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 16384,
                .files         = { "roms/machines/pc500/rom406.bin", "" }
            },
            {
                .name          = "4.04",
                .internal_name = "pc500plus",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 16384,
                .files         = { "roms/machines/pc500/rom404.bin", "" }
            },
            {
                .name          = "4.03",
                .internal_name = "pc500plus_403",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 16384,
                .files         = { "roms/machines/pc500/rom403.bin", "" }
            },
            { .files_no = 0 }
        },
    },
    {
        .name           = "rtc_irq",
        .description    = "RTC IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = -1 },
            { .description = "IRQ 2",    .value =  2 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "rtc_port",
        .description    = "Onboard RTC",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value =     0 },
            { .description = "Enabled",  .value = 0x2c0 },
            { .description = ""                         }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t pc500plus_device = {
    .name          = "Multitech PC-500+",
    .internal_name = "pc500plus_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pc500plus_config
};

int
machine_xt_pc500plus_init(const machine_t *model)
{
    int         ret      = 0;
    int         rtc_port = 0;
    const char *fn;

    /* No ROMs available. */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    rtc_port = machine_get_config_int("rtc_port");
    fn       = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    ret      = bios_load_linear(fn, 0x000fc000, 16384, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model, 0);

    if (rtc_port != 0)
        device_add(&rtc58167_device);

    return ret;
}

static const device_config_t pc700_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "pc700",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .bios           = {
            {
                .name           = "3.30",
                .internal_name  = "pc700",
                .bios_type      = BIOS_NORMAL,
                .files_no       = 1,
                .local          = 0,
                .size           = 8192,
                .files          = { "roms/machines/pc700/multitech pc-700 3.30.bin", "" }
            },
            {
                .name           = "3.1",
                .internal_name  = "pc700_31",
                .bios_type      = BIOS_NORMAL,
                .files_no       = 1,
                .local          = 0,
                .size           = 8192,
                .files          = { "roms/machines/pc700/multitech pc-700 3.1.bin", "" }
            },
            { .files_no = 0 }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t pc700_device = {
    .name           = "Multitech PC-700 / Siemens SICOMP PC 16 05",
    .internal_name  = "pc700_device",
    .flags          = 0,
    .local          = 0,
    .init           = NULL,
    .close          = NULL,
    .reset          = NULL,
    .available      = NULL,
    .speed_changed  = NULL,
    .force_redraw   = NULL,
    .config         = pc700_config
};

int
machine_xt_pc700_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available. */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000fe000, 8192, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    device_add(&kbc_pc82_device);

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

static void
machine_xt_philips_common_init(const machine_t *model)
{
    machine_common_init(model);

    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_xt);

    nmi_init();

    standalone_gameport_type = &gameport_200_device;

    device_add(&kbc_pc_device);

    device_add(&philips_device);

    device_add(&xta_hd20_device);
}

int
machine_xt_p3105_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p3105/philipsnms9100.bin",
                           0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_philips_common_init(model);

    /* On-board FDC cannot be disabled */
    device_add(&fdc_xt_device);

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

    device_add(&kbc_xt_device);

    machine_xt_common_init(model, 0);

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

    device_add(&kbc_pravetz_device);

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

    device_add(&kbc_xt_device);

    machine_xt_common_init(model, 0);

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

static const device_config_t to16_config[] = {
    // clang-format off
    {
        .name           = "rtc_port",
        .description    = "Onboard RTC",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Not installed", .value =     0 },
            { .description = "RTC0",          .value = 0x300 },
            { .description = "RTC1",          .value = 0x2c0 },
            { .description = ""                              }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "rtc_irq",
        .description    = "RTC IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = -1 },
            { .description = "IRQ 2",    .value =  2 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t to16_device = {
    .name          = "Thomson TO16",
    .internal_name = "to16_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = to16_config
};

int
machine_xt_to16_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/to16/TO16_103.bin", 0x000f8000, 32768, 0);

    if (bios_only || !ret)
        return ret;

    device_context(model->device);
    int rtc_port = machine_get_config_int("rtc_port");
    device_context_restore();

    machine_xt_clone_init(model, 0);

    if (rtc_port != 0)
        device_add(&rtc58167_device);

    return ret;
}

static const device_config_t vendex_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "vendex",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Bios 2.03C",
                .internal_name = "vendex",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 16384,
                .files         = { "roms/machines/vendex/Vendex Turbo 888 XT - ROM BIOS - VER 2.03C.bin", "" }
            },

            // GLaBIOS for Vendex
            {
                .name          = "GLaBIOS 0.4.0 (8088)",
                .internal_name = "glabios_040_8088",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 16384,
                .files         = { "roms/machines/glabios/GLABIOS_0.4.0_8TV.ROM", "" }
            },
            {
                .name          = "GLaBIOS 0.4.0 (V20)",
                .internal_name = "glabios_040_v20",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 16384,
                .files         = { "roms/machines/glabios/GLABIOS_0.4.0_VTV.ROM", "" }
            },

            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t vendex_device = {
    .name          = "Vendex 888T",
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
    fn  = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000fc000, 16384, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model, 1);

    device_add(&vendex_xt_rtc_onboard_device);

    return ret;
}

static void
machine_xt_laserxt_common_init(const machine_t *model, int is_lxt3)
{
    machine_common_init(model);

    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_xt);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_xt_device);

    nmi_init();
    standalone_gameport_type = &gameport_200_device;

    device_add(is_lxt3 ? &lxt3_device : &laserxt_device);

    device_add(&kbc_xt_lxt3_device);
}

int
machine_xt_laserxt_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available. */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000fe000, 8192, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_xt_laserxt_common_init(model, 0);

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

static void
machine_zenith_common_init(const machine_t *model)
{
    machine_common_init(model);

    device_add(&zenith_scratchpad_device);

    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_xt);

    device_add(&kbc_xt_zenith_device);

    nmi_init();
}

int
machine_xt_z151_init(const machine_t *model)
{
    int ret;
    ret = bios_load_linear("roms/machines/zdsz151/444-229-18.bin",
                           0x000fc000, 32768, 0);
    if (ret) {
        bios_load_aux_linear("roms/machines/zdsz151/444-260-18.bin",
                             0x000f8000, 16384, 0);
    }

    if (bios_only || !ret)
        return ret;

    machine_zenith_common_init(model);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_xt_tandy_device);

    return ret;
}

/*
 * Current bugs and limitations:
 * - Memory board support for EMS currently missing
 */
int
machine_xt_z159_init(const machine_t *model)
{
    lpt_t *lpt = NULL;
    int    ret;

    ret = bios_load_linear("roms/machines/zdsz159/z159m v2.9e.10d",
                           0x000f8000, 32768, 0);

    if (bios_only || !ret)
        return ret;

    machine_zenith_common_init(model);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_xt_tandy_device);

    /* parallel port is on the memory board */
    lpt = device_add_inst(&lpt_port_device, 1);
    lpt_port_remove(lpt);
    lpt_port_setup(lpt, LPT2_ADDR);
    lpt_set_next_inst(255);

    return ret;
}

/*
 * Current bugs and limitations:
 * - missing NVRAM implementation
 */
int
machine_xt_z184_init(const machine_t *model)
{
    lpt_t *lpt = NULL;
    int    ret;

    ret = bios_load_linear("roms/machines/zdsupers/z184m v3.1d.10d",
                           0x000f8000, 32768, 0);

    if (bios_only || !ret)
        return ret;

    machine_zenith_common_init(model);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_xt_device);

    lpt = device_add_inst(&lpt_port_device, 1);
    lpt_port_remove(lpt);
    lpt_port_setup(lpt, LPT2_ADDR);
    lpt_set_next_inst(255);

    device_add(&ns8250_device);
    /* So that serial_standalone_init() won't do anything. */
    serial_set_next_inst(SERIAL_MAX - 1);

    device_add(&v6355d_device);

    return ret;
}

/* GC100A */
int
machine_xt_p3120_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p3120/philips_p3120.bin",
                           0x000f8000, 32768, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_philips_common_init(model);

    device_add(&gc100a_device);

    device_add(&fdc_at_device);

    return ret;
}

/* V20 */
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
machine_xt_tuliptc8_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/tuliptc8/tulip-bios_xt_compact_2.bin",
                           0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    device_add(&kbc_xt_fe2010_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    machine_common_init(model);

    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_xt);

    nmi_init();
    standalone_gameport_type = &gameport_200_device;

    return ret;
}

/* 8086 */
int
machine_xt_compaq_deskpro_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/deskpro/Compaq - BIOS - Revision J - 106265-002.bin",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_common_init(model);

    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_xt);

    device_add(&kbc_xt_compaq_device);
    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_xt_device);
    nmi_init();
    standalone_gameport_type = &gameport_200_device;

    lpt_t *lpt = device_add_inst(&lpt_port_device, 1);
    lpt_port_setup(lpt, LPT_MDA_ADDR);
    lpt_set_3bc_used(1);

    return ret;
}

int
machine_xt_pc5086_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pc5086/sys_rom.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_common_init(model);

    device_add(&ct_82c100_device);
    device_add(&f82c710_pc5086_device);

    device_add(&kbc_xt_device);

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

    video_load_font("roms/machines/maz1016/crt-8.bin", FONT_FORMAT_MDA, LOAD_FONT_NO_OFFSET);

    machine_xt_clone_init(model, 0);

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
machine_xt_lxt3_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/lxt3/27c64d.bin",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_laserxt_common_init(model, 1);

    return ret;
}
