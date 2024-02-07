/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Define all known processor types.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          leilei,
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          RichardG, <richardg867@gmail.com>
 *          dob205,
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 leilei.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2017-2020 Fred N. van Kempen.
 *          Copyright 2020      RichardG.
 *          Copyright 2021      dob205.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/machine.h>

FPU fpus_none[] = {
    { .name = "None", .internal_name = "none", .type = FPU_NONE },
    { .name = NULL,   .internal_name = NULL,   .type = 0        }
};
FPU fpus_8088[] = {
    { .name = "None", .internal_name = "none", .type = FPU_NONE },
    { .name = "8087", .internal_name = "8087", .type = FPU_8087 },
    { .name = NULL,   .internal_name = NULL,   .type = 0        }
};
FPU fpus_80186[] = {
    { .name = "None",  .internal_name = "none",  .type = FPU_NONE  },
    { .name = "8087",  .internal_name = "8087",  .type = FPU_8087  },
    { .name = "80187", .internal_name = "80187", .type = FPU_80187 },
    { .name = NULL,    .internal_name = NULL,    .type = 0         }
};
FPU fpus_80286[] = {
    { .name = "None",  .internal_name = "none",  .type = FPU_NONE  },
    { .name = "287",   .internal_name = "287",   .type = FPU_287   },
    { .name = "287XL", .internal_name = "287xl", .type = FPU_287XL },
    { .name = NULL,    .internal_name = NULL,    .type = 0         }
};
FPU fpus_80386[] = {
    { .name = "None", .internal_name = "none", .type = FPU_NONE },
    { .name = "387",  .internal_name = "387",  .type = FPU_387  },
    { .name = NULL,   .internal_name = NULL,   .type = 0        }
};
FPU fpus_486sx[] = {
    { .name = "None",  .internal_name = "none",  .type = FPU_NONE  },
    { .name = "487SX", .internal_name = "487sx", .type = FPU_487SX },
    { .name = NULL,    .internal_name = NULL,    .type = 0         }
};
FPU fpus_internal[] = {
    { .name = "Internal", .internal_name = "internal", .type = FPU_INTERNAL },
    { .name = NULL,       .internal_name = NULL,       .type = 0            }
};

const cpu_family_t cpu_families[] = {
  // clang-format off
    {
    .package = CPU_PKG_8088,
    .manufacturer = "Intel",
    .name = "8088",
    .internal_name = "8088",
    .cpus = (const CPU[]) {
        {
            .name = "4.77",
            .cpu_type = CPU_8088,
            .fpus = fpus_8088,
            .rspeed =  4772728,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "7.16",
            .cpu_type = CPU_8088,
            .fpus = fpus_8088,
            .rspeed =  7159092,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "8",
            .cpu_type = CPU_8088,
            .fpus = fpus_8088,
            .rspeed =  8000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
#if 0
        {
            .name = "9.54",
            .cpu_type = CPU_8088,
            .fpus = fpus_8088,
            .rspeed =  9545456,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
#endif
        {
            .name = "10",
            .cpu_type = CPU_8088,
            .fpus = fpus_8088,
            .rspeed = 10000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "12",
            .cpu_type = CPU_8088,
            .fpus = fpus_8088,
            .rspeed = 12000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "16",
            .cpu_type = CPU_8088,
            .fpus = fpus_8088,
            .rspeed = 16000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        { .name = "", 0 }
    }
    }, {
    .package = CPU_PKG_8088_EUROPC,
    .manufacturer = "Intel",
    .name = "8088",
    .internal_name = "8088_europc",
    .cpus = (const CPU[]) {
        {
            .name = "4.77",
            .cpu_type = CPU_8088,
            .fpus = fpus_8088,
            .rspeed = 4772728,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = CPU_ALTERNATE_XTAL,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "7.16",
            .cpu_type = CPU_8088,
            .fpus = fpus_8088,
            .rspeed = 7159092,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = CPU_ALTERNATE_XTAL,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "9.54",
            .cpu_type = CPU_8088,
            .fpus = fpus_8088,
            .rspeed = 9545456,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        { .name = "", 0 }
    }
    }, {
    .package = CPU_PKG_8086,
    .manufacturer = "Intel",
    .name = "8086",
    .internal_name = "8086",
    .cpus = (const CPU[]) {
        {
            .name = "7.16",
            .cpu_type = CPU_8086,
            .fpus = fpus_8088,
            .rspeed = 7159092,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = CPU_ALTERNATE_XTAL,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "8",
            .cpu_type = CPU_8086,
            .fpus = fpus_8088,
            .rspeed = 8000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "9.54",
            .cpu_type = CPU_8086,
            .fpus = fpus_8088,
            .rspeed = 9545456,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = CPU_ALTERNATE_XTAL,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "10",
            .cpu_type = CPU_8086,
            .fpus = fpus_8088,
            .rspeed = 10000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "12",
            .cpu_type = CPU_8086,
            .fpus = fpus_8088,
            .rspeed = 12000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "16",
            .cpu_type = CPU_8086,
            .fpus = fpus_8088,
            .rspeed = 16000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 2
        },
        { .name = "", 0 }
    }
    }, {
    .package = CPU_PKG_188,
    .manufacturer = "Intel",
    .name = "80188",
    .internal_name = "80188",
    .cpus = (const CPU[]) {
        {
            .name = "6",
            .cpu_type = CPU_188,
            .fpus = fpus_8088,
            .rspeed = 6000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "7.16",
            .cpu_type = CPU_188,
            .fpus = fpus_8088,
            .rspeed = 7159092,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = CPU_ALTERNATE_XTAL,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "8",
            .cpu_type = CPU_188,
            .fpus = fpus_8088,
            .rspeed = 8000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "9.54",
            .cpu_type = CPU_188,
            .fpus = fpus_8088,
            .rspeed = 9545456,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = CPU_ALTERNATE_XTAL,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "10",
            .cpu_type = CPU_188,
            .fpus = fpus_8088,
            .rspeed = 10000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "12",
            .cpu_type = CPU_188,
            .fpus = fpus_8088,
            .rspeed = 12000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "16",
            .cpu_type = CPU_188,
            .fpus = fpus_8088,
            .rspeed = 16000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 2
        },
        {
            .name = "20",
            .cpu_type = CPU_188,
            .fpus = fpus_8088,
            .rspeed = 20000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 3
        },
        {
            .name = "25",
            .cpu_type = CPU_188,
            .fpus = fpus_8088,
            .rspeed = 25000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 3
        },
        { .name = "", 0 }
    }
    }, {
    .package = CPU_PKG_8088,
    .manufacturer = "NEC",
    .name = "V20",
    .internal_name = "necv20",
    .cpus = (const CPU[]) {
        {
            .name = "4.77",
            .cpu_type = CPU_V20,
            .fpus = fpus_8088,
            .rspeed = 4772728,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "7.16",
            .cpu_type = CPU_V20,
            .fpus = fpus_8088,
            .rspeed = 7159092,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "10",
            .cpu_type = CPU_V20,
            .fpus = fpus_8088,
            .rspeed = 10000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "12",
            .cpu_type = CPU_V20,
            .fpus = fpus_8088,
            .rspeed = 12000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "16",
            .cpu_type = CPU_V20,
            .fpus = fpus_8088,
            .rspeed = 16000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 2
        },
        { .name = "", 0 }
    }
    }, {
    .package = CPU_PKG_186,
    .manufacturer = "Intel",
    .name = "80186",
    .internal_name = "80186",
    .cpus = (const CPU[]) {
        {
            .name = "6",
            .cpu_type = CPU_186,
            .fpus = fpus_80186,
            .rspeed = 6000000,
            .multi = 1,
            .voltage = 0,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "7.16",
            .cpu_type = CPU_186,
            .fpus = fpus_80186,
            .rspeed = 7159092,
            .multi = 1,
            .voltage = 0,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = CPU_ALTERNATE_XTAL,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "8",
            .cpu_type = CPU_186,
            .fpus = fpus_80186,
            .rspeed = 8000000,
            .multi = 1,
            .voltage = 0,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "9.54",
            .cpu_type = CPU_186,
            .fpus = fpus_80186,
            .rspeed = 9545456,
            .multi = 1,
            .voltage = 0,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = CPU_ALTERNATE_XTAL,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "10",
            .cpu_type = CPU_186,
            .fpus = fpus_80186,
            .rspeed = 10000000,
            .multi = 1,
            .voltage = 0,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "12",
            .cpu_type = CPU_186,
            .fpus = fpus_80186,
            .rspeed = 12000000,
            .multi = 1,
            .voltage = 0,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "16",
            .cpu_type = CPU_186,
            .fpus = fpus_80186,
            .rspeed = 16000000,
            .multi = 1,
            .voltage = 0,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 2
        },
        {
            .name = "20",
            .cpu_type = CPU_186,
            .fpus = fpus_80186,
            .rspeed = 20000000,
            .multi = 1,
            .voltage = 0,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 3
        },
        {
            .name = "25",
            .cpu_type = CPU_186,
            .fpus = fpus_80186,
            .rspeed = 25000000,
            .multi = 1,
            .voltage = 0,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 3
        },
        { .name = "", 0 }
    }
    }, {
    .package = CPU_PKG_8086,
    .manufacturer = "NEC",
    .name = "V30",
    .internal_name = "necv30",
    .cpus = (const CPU[]) {
        {
            .name = "5",
            .cpu_type = CPU_V30,
            .fpus = fpus_80186,
            .rspeed = 5000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "8",
            .cpu_type = CPU_V30,
            .fpus = fpus_80186,
            .rspeed = 8000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "10",
            .cpu_type = CPU_V30,
            .fpus = fpus_80186,
            .rspeed = 10000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "12",
            .cpu_type = CPU_V30,
            .fpus = fpus_80186,
            .rspeed = 12000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 1
        },
        {
            .name = "16",
            .cpu_type = CPU_V30,
            .fpus = fpus_80186,
            .rspeed = 16000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 0,
            .mem_write_cycles = 0,
            .cache_read_cycles = 0,
            .cache_write_cycles = 0,
            .atclk_div = 2
        },
        { .name = "", 0 }
    }
    }, {
    .package = CPU_PKG_286,
    .manufacturer = "Intel",
    .name = "80286",
    .internal_name = "286",
    .cpus = (const CPU[]) {
        {
            .name = "6",
            .cpu_type = CPU_286,
            .fpus = fpus_80286,
            .rspeed = 6000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 2,
            .mem_write_cycles = 2,
            .cache_read_cycles = 2,
            .cache_write_cycles = 2,
            .atclk_div = 1
        },
        {
            .name = "8",
            .cpu_type = CPU_286,
            .fpus = fpus_80286,
            .rspeed = 8000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 2,
            .mem_write_cycles = 2,
            .cache_read_cycles = 2,
            .cache_write_cycles = 2,
            .atclk_div = 1
        },
        {
            .name = "10",
            .cpu_type = CPU_286,
            .fpus = fpus_80286,
            .rspeed = 10000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 2,
            .mem_write_cycles = 2,
            .cache_read_cycles = 2,
            .cache_write_cycles = 2,
            .atclk_div = 1
        },
        {
            .name = "12",
            .cpu_type = CPU_286,
            .fpus = fpus_80286,
            .rspeed = 12500000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 3,
            .mem_write_cycles = 3,
            .cache_read_cycles = 3,
            .cache_write_cycles = 3,
            .atclk_div = 2
        },
        {
            .name = "16",
            .cpu_type = CPU_286,
            .fpus = fpus_80286,
            .rspeed = 16000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 3,
            .mem_write_cycles = 3,
            .cache_read_cycles = 3,
            .cache_write_cycles = 3,
            .atclk_div = 2
        },
        {
            .name = "20",
            .cpu_type = CPU_286,
            .fpus = fpus_80286,
            .rspeed = 20000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 4,
            .mem_write_cycles = 4,
            .cache_read_cycles = 4,
            .cache_write_cycles = 4,
            .atclk_div = 3
        },
        {
            .name = "25",
            .cpu_type = CPU_286,
            .fpus = fpus_80286,
            .rspeed = 25000000,
            .multi = 1,
            .voltage = 5000,
            .edx_reset = 0,
            .cpuid_model = 0,
            .cyrix_id = 0,
            .cpu_flags = 0,
            .mem_read_cycles = 4,
            .mem_write_cycles = 4,
            .cache_read_cycles = 4,
            .cache_write_cycles = 4,
            .atclk_div = 3
        },
        { .name = "", 0 }
    }
    }, {
    .package = CPU_PKG_386SX,
    .manufacturer = "Intel",
    .name = "i386SX",
    .internal_name = "i386sx",
    .cpus = (const CPU[]) {
        {"16",    CPU_386SX, fpus_80386, 16000000,    1, 5000, 0x2308, 0, 0, 0, 3,3,3,3, 2},
        {"20",    CPU_386SX, fpus_80386, 20000000,    1, 5000, 0x2308, 0, 0, 0, 4,4,3,3, 3},
        {"25",    CPU_386SX, fpus_80386, 25000000,    1, 5000, 0x2308, 0, 0, 0, 4,4,3,3, 3},
        {"33",    CPU_386SX, fpus_80386, 33333333,    1, 5000, 0x2308, 0, 0, 0, 6,6,3,3, 4},
        {"40",    CPU_386SX, fpus_80386, 40000000,    1, 5000, 0x2308, 0, 0, 0, 7,7,3,3, 5},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_386SX,
    .manufacturer = "AMD",
    .name = "Am386SX",
    .internal_name = "am386sx",
    .cpus = (const CPU[]) {
        {"16",   CPU_386SX, fpus_80386,     16000000, 1, 5000, 0x2308, 0, 0, 0, 3,3,3,3, 2},
        {"20",   CPU_386SX, fpus_80386,     20000000, 1, 5000, 0x2308, 0, 0, 0, 4,4,3,3, 3},
        {"25",   CPU_386SX, fpus_80386,     25000000, 1, 5000, 0x2308, 0, 0, 0, 4,4,3,3, 3},
        {"33",   CPU_386SX, fpus_80386,     33333333, 1, 5000, 0x2308, 0, 0, 0, 6,6,3,3, 4},
        {"40",   CPU_386SX, fpus_80386,     40000000, 1, 5000, 0x2308, 0, 0, 0, 7,7,3,3, 5},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_386DX,
    .manufacturer = "Intel",
    .name = "i386DX",
    .internal_name = "i386dx",
    .cpus = (const CPU[]) {
        {"16",    CPU_386DX,      fpus_80386,  16000000, 1, 5000, 0x0308, 0, 0, 0, 3,3,3,3, 2},
        {"20",    CPU_386DX,      fpus_80386,  20000000, 1, 5000, 0x0308, 0, 0, 0, 4,4,3,3, 3},
        {"25",    CPU_386DX,      fpus_80386,  25000000, 1, 5000, 0x0308, 0, 0, 0, 4,4,3,3, 3},
        {"33",    CPU_386DX,      fpus_80386,  33333333, 1, 5000, 0x0308, 0, 0, 0, 6,6,3,3, 4},
        {"40",    CPU_386DX,      fpus_80386,  40000000, 1, 5000, 0x0308, 0, 0, 0, 7,7,3,3, 5},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_386DX_DESKPRO386,
    .manufacturer = "Intel",
    .name = "i386DX",
    .internal_name = "i386dx_deskpro386",
    .cpus = (const CPU[]) {
        {"16",    CPU_386DX,      fpus_80286,  16000000, 1, 5000, 0x0308, 0, 0, 0, 3,3,3,3, 2},
        {"20",    CPU_386DX,      fpus_80386,  20000000, 1, 5000, 0x0308, 0, 0, 0, 4,4,3,3, 3},
        {"25",    CPU_386DX,      fpus_80386,  25000000, 1, 5000, 0x0308, 0, 0, 0, 4,4,3,3, 3},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_386DX,
    .manufacturer = "Intel",
    .name = "RapidCAD",
    .internal_name = "rapidcad",
    .cpus = (const CPU[]) {
        {"25",  CPU_RAPIDCAD, fpus_internal,  25000000, 1, 5000, 0x0340, 0, 0, CPU_SUPPORTS_DYNAREC, 4,4,3,3, 3},
        {"33",  CPU_RAPIDCAD, fpus_internal,  33333333, 1, 5000, 0x0340, 0, 0, CPU_SUPPORTS_DYNAREC, 6,6,3,3, 4},
        {"40",  CPU_RAPIDCAD, fpus_internal,  40000000, 1, 5000, 0x0340, 0, 0, CPU_SUPPORTS_DYNAREC, 7,7,3,3, 5},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_386DX,
    .manufacturer = "AMD",
    .name = "Am386DX",
    .internal_name = "am386dx",
    .cpus = (const CPU[]) {
        {"25",   CPU_386DX, fpus_80386,     25000000, 1, 5000, 0x0308, 0, 0, 0, 4,4,3,3, 3},
        {"33",   CPU_386DX, fpus_80386,     33333333, 1, 5000, 0x0308, 0, 0, 0, 6,6,3,3, 4},
        {"40",   CPU_386DX, fpus_80386,     40000000, 1, 5000, 0x0308, 0, 0, 0, 7,7,3,3, 5},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_M6117,
    .manufacturer = "ALi",
    .name = "M6117",
    .internal_name = "m6117",
    .cpus = (const CPU[]) { /* All timings and edx_reset values assumed. */
        {"33",    CPU_386SX,      fpus_none,  33333333, 1, 5000, 0x2309, 0, 0, 0, 6,6,3,3, 4},
        {"40",    CPU_386SX,      fpus_none,  40000000, 1, 5000, 0x2309, 0, 0, 0, 7,7,3,3, 5},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_386SLC_IBM,
    .manufacturer = "IBM",
    .name = "386SLC",
    .internal_name = "ibm386slc",
    .cpus = (const CPU[]) {
        {"16",  CPU_IBM386SLC, fpus_80386, 16000000, 1, 5000, 0xA301, 0, 0, 0, 3,3,3,3, 2},
        {"20",  CPU_IBM386SLC, fpus_80386, 20000000, 1, 5000, 0xA301, 0, 0, 0, 4,4,3,3, 3},
        {"25",  CPU_IBM386SLC, fpus_80386, 25000000, 1, 5000, 0xA301, 0, 0, 0, 4,4,3,3, 3},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_386SX,
    .manufacturer = "Cyrix",
    .name = "Cx486SLC",
    .internal_name = "cx486slc",
    .cpus = (const CPU[]) {
        {"20",  CPU_486SLC, fpus_80386, 20000000, 1, 5000, 0x400, 0, 0x0000, 0, 4,4,3,3, 3},
        {"25",  CPU_486SLC, fpus_80386, 25000000, 1, 5000, 0x400, 0, 0x0000, 0, 4,4,3,3, 3},
        {"33",  CPU_486SLC, fpus_80386, 33333333, 1, 5000, 0x400, 0, 0x0000, 0, 6,6,3,3, 4},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_386SX,
    .manufacturer = "Cyrix",
    .name = "Cx486SRx2",
    .internal_name = "cx486srx2",
    .cpus = (const CPU[]) {
        {"32", CPU_486SLC, fpus_80386, 32000000, 2, 5000, 0x406, 0, 0x0006, 0, 6,6,6,6, 4},
        {"40", CPU_486SLC, fpus_80386, 40000000, 2, 5000, 0x406, 0, 0x0006, 0, 8,8,6,6, 6},
        {"50", CPU_486SLC, fpus_80386, 50000000, 2, 5000, 0x406, 0, 0x0006, 0, 8,8,6,6, 6},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_486SLC_IBM,
    .manufacturer = "IBM",
    .name = "486SLC",
    .internal_name = "ibm486slc",
    .cpus = (const CPU[]) {
        {"33",   CPU_IBM486SLC, fpus_80386, 33333333,  1, 5000, 0xA401, 0, 0, 0, 6,6,3,3,    4},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_486SLC_IBM,
    .manufacturer = "IBM",
    .name = "486SLC2",
    .internal_name = "ibm486slc2",
    .cpus = (const CPU[]) {
        {"40",  CPU_IBM486SLC, fpus_80386, 40000000,  2, 5000, 0xA421, 0, 0, 0, 7,7,6,6,    5},
        {"50",  CPU_IBM486SLC, fpus_80386, 50000000,  2, 5000, 0xA421, 0, 0, 0, 8,8,6,6,    6},
        {"66",  CPU_IBM486SLC, fpus_80386, 66666666,  2, 5000, 0xA421, 0, 0, 0, 12,12,6,6,  8},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_486SLC_IBM,
    .manufacturer = "IBM",
    .name = "486SLC3",
    .internal_name = "ibm486slc3",
    .cpus = (const CPU[]) {
        {"60",  CPU_IBM486SLC, fpus_80386, 60000000,  3, 5000, 0xA439, 0, 0, 0, 12,12,9,9,  7},
        {"75",  CPU_IBM486SLC, fpus_80386, 75000000,  3, 5000, 0xA439, 0, 0, 0, 12,12,9,9,  9},
        {"100", CPU_IBM486SLC, fpus_80386, 100000000, 3, 5000, 0xA439, 0, 0, 0, 18,18,9,9, 12},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_486BL,
    .manufacturer = "IBM",
    .name = "486BL2",
    .internal_name = "ibm486bl2",
    .cpus = (const CPU[]) {
        {"50",  CPU_IBM486BL, fpus_80386, 50000000,  2, 5000, 0xA439, 0, 0, 0, 8,8,6,6,    6},
        {"66",  CPU_IBM486BL, fpus_80386, 66666666,  2, 5000, 0xA439, 0, 0, 0, 12,12,6,6,  8},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_486BL,
    .manufacturer = "IBM",
    .name = "486BL3",
    .internal_name = "ibm486bl3",
    .cpus = (const CPU[]) {
        {"75",  CPU_IBM486BL, fpus_80386, 75000000,  3, 5000, 0xA439, 0, 0, 0, 12,12,9,9,  9},
        {"100", CPU_IBM486BL, fpus_80386, 100000000, 3, 5000, 0xA439, 0, 0, 0, 18,18,9,9, 12},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_386DX,
    .manufacturer = "Cyrix",
    .name = "Cx486DLC",
    .internal_name = "cx486dlc",
    .cpus = (const CPU[]) {
        {"25",  CPU_486DLC, fpus_80386, 25000000, 1, 5000, 0x401, 0, 0x0001, 0,  4, 4,3,3, 3},
        {"33",  CPU_486DLC, fpus_80386, 33333333, 1, 5000, 0x401, 0, 0x0001, 0,  6, 6,3,3, 4},
        {"40",  CPU_486DLC, fpus_80386, 40000000, 1, 5000, 0x401, 0, 0x0001, 0,  7, 7,3,3, 5},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_386DX,
    .manufacturer = "Cyrix",
    .name = "Cx486DRx2",
    .internal_name = "cx486drx2",
    .cpus = (const CPU[]) {
        {"32", CPU_486DLC, fpus_80386, 32000000, 2, 5000, 0x407, 0, 0x0007, 0,  6, 6,6,6, 4},
        {"40", CPU_486DLC, fpus_80386, 40000000, 2, 5000, 0x407, 0, 0x0007, 0,  8, 8,6,6, 6},
        {"50", CPU_486DLC, fpus_80386, 50000000, 2, 5000, 0x407, 0, 0x0007, 0,  8, 8,6,6, 6},
        {"66", CPU_486DLC, fpus_80386, 66666666, 2, 5000, 0x407, 0, 0x0007, 0, 12,12,6,6, 8},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET1,
    .manufacturer = "Intel",
    .name = "i486SX",
    .internal_name = "i486sx",
    .cpus = (const CPU[]) {
        {"16",            CPU_i486SX,     fpus_486sx,  16000000, 1,  5000, 0x420,        0, 0, CPU_SUPPORTS_DYNAREC,  3, 3,3,3,  2},
        {"20",            CPU_i486SX,     fpus_486sx,  20000000, 1,  5000, 0x420,        0, 0, CPU_SUPPORTS_DYNAREC,  4, 4,3,3,  3},
        {"25",            CPU_i486SX,     fpus_486sx,  25000000, 1,  5000, 0x422,        0, 0, CPU_SUPPORTS_DYNAREC,  4, 4,3,3,  3},
        {"33",            CPU_i486SX,     fpus_486sx,  33333333, 1,  5000, 0x422,        0, 0, CPU_SUPPORTS_DYNAREC,  6, 6,3,3,  4},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET1,
    .manufacturer = "Intel",
    .name = "i486SX-S",
    .internal_name = "i486sx_slenh",
    .cpus = (const CPU[]) {
        {"25",            CPU_i486SX_SLENH,     fpus_486sx,  25000000, 1,  5000, 0x423,    0x423, 0, CPU_SUPPORTS_DYNAREC,  4, 4,3,3,  3},
        {"33",            CPU_i486SX_SLENH,     fpus_486sx,  33333333, 1,  5000, 0x42a,    0x42a, 0, CPU_SUPPORTS_DYNAREC,  6, 6,3,3,  4},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET1,
    .manufacturer = "Intel",
    .name = "i486SX2",
    .internal_name = "i486sx2",
    .cpus = (const CPU[]) {
        {"50",           CPU_i486SX_SLENH,    fpus_486sx,  50000000, 2,  5000, 0x45b,    0x45b, 0, CPU_SUPPORTS_DYNAREC,  8, 8,6,6,  6},
        {"66 (Q0569)",   CPU_i486SX_SLENH,    fpus_486sx,  66666666, 2,  5000, 0x45b,    0x45b, 0, CPU_SUPPORTS_DYNAREC,  8, 8,6,6,  8},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET1,
    .manufacturer = "Intel",
    .name = "i486DX",
    .internal_name = "i486dx",
    .cpus = (const CPU[]) {
        {"25",            CPU_i486DX,  fpus_internal,  25000000, 1,  5000, 0x404,        0, 0, CPU_SUPPORTS_DYNAREC,  4, 4,3,3,  3},
        {"33",            CPU_i486DX,  fpus_internal,  33333333, 1,  5000, 0x404,        0, 0, CPU_SUPPORTS_DYNAREC,  6, 6,3,3,  4},
        {"50",            CPU_i486DX,  fpus_internal,  50000000, 1,  5000, 0x411,        0, 0, CPU_SUPPORTS_DYNAREC,  8, 8,4,4,  6},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET1,
    .manufacturer = "Intel",
    .name = "i486DX-S",
    .internal_name = "i486dx_slenh",
    .cpus = (const CPU[]) {
        {"33",            CPU_i486DX_SLENH,  fpus_internal,  33333333, 1,  5000, 0x414,    0x414, 0, CPU_SUPPORTS_DYNAREC,  6, 6,3,3,  4},
        {"50",            CPU_i486DX_SLENH,  fpus_internal,  50000000, 1,  5000, 0x414,    0x414, 0, CPU_SUPPORTS_DYNAREC,  8, 8,4,4,  6},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET1,
    .manufacturer = "Intel",
    .name = "i486DX2",
    .internal_name = "i486dx2",
    .cpus = (const CPU[]) {
        {"40",           CPU_i486DX, fpus_internal,  40000000, 2,  5000, 0x430,        0, 0, CPU_SUPPORTS_DYNAREC,  7, 7,6,6,  5},
        {"50",           CPU_i486DX, fpus_internal,  50000000, 2,  5000, 0x433,        0, 0, CPU_SUPPORTS_DYNAREC,  8, 8,6,6,  6},
        {"66",           CPU_i486DX, fpus_internal,  66666666, 2,  5000, 0x433,        0, 0, CPU_SUPPORTS_DYNAREC, 12,12,6,6,  8},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET1,
    .manufacturer = "Intel",
    .name = "i486DX2-S",
    .internal_name = "i486dx2_slenh",
    .cpus = (const CPU[]) {
        {"40",           CPU_i486DX_SLENH, fpus_internal,  40000000, 2,  5000, 0x435,    0x435, 0, CPU_SUPPORTS_DYNAREC,  7, 7,6,6,  5},
        {"50",           CPU_i486DX_SLENH, fpus_internal,  50000000, 2,  5000, 0x435,    0x435, 0, CPU_SUPPORTS_DYNAREC,  8, 8,6,6,  6},
        {"66",           CPU_i486DX_SLENH, fpus_internal,  66666666, 2,  5000, 0x435,    0x435, 0, CPU_SUPPORTS_DYNAREC, 12,12,6,6,  8},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET1 | CPU_PKG_SOCKET3_PC330,
    .manufacturer = "Intel",
    .name = "i486DX2 WB",
    .internal_name = "i486dx2_pc330",
    .cpus = (const CPU[]) {
        {"50",           CPU_i486DX_SLENH, fpus_internal,  50000000, 2,  5000, 0x436,    0x436, 0, CPU_SUPPORTS_DYNAREC,  8, 8,6,6,  6},
        {"66",           CPU_i486DX_SLENH, fpus_internal,  66666666, 2,  5000, 0x436,    0x436, 0, CPU_SUPPORTS_DYNAREC, 12,12,6,6,  8},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET1 | CPU_PKG_SOCKET3_PC330, /*OEM versions are 3.3V, Retail versions are 3.3V with a 5V regulator for installation in older boards. They are functionally identical*/
    .manufacturer = "Intel",
    .name = "iDX4",
    .internal_name = "idx4",
    .cpus = (const CPU[]) {
        {"75",              CPU_i486DX_SLENH,    fpus_internal,  75000000, 3.0, 5000,  0x480,  0x480, 0x0000, CPU_SUPPORTS_DYNAREC, 12,12, 9, 9,  9},
        {"100",             CPU_i486DX_SLENH,    fpus_internal, 100000000, 3.0, 5000,  0x483,  0x483, 0x0000, CPU_SUPPORTS_DYNAREC, 18,18, 9, 9, 12},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET3 | CPU_PKG_SOCKET3_PC330,
    .manufacturer = "Intel",
    .name = "Pentium OverDrive",
    .internal_name = "pentium_p24t",
    .cpus = (const CPU[]) {
        {"63", CPU_P24T,    fpus_internal,  62500000, 2.5, 5000, 0x1531, 0x1531, 0x0000, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10,7,7, 15/2},
        {"83", CPU_P24T,    fpus_internal,  83333333, 2.5, 5000, 0x1532, 0x1532, 0x0000, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15,8,8, 10},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET1,
    .manufacturer = "AMD",
    .name = "Am486SX",
    .internal_name = "am486sx",
    .cpus = (const CPU[]) {
        {"33",   CPU_Am486SX,     fpus_486sx, 33333333, 1, 5000, 0x422,     0, 0, CPU_SUPPORTS_DYNAREC,  6, 6, 3, 3, 4},
        {"40",   CPU_Am486SX,     fpus_486sx, 40000000, 1, 5000, 0x422,     0, 0, CPU_SUPPORTS_DYNAREC,  7, 7, 3, 3, 5},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET1,
    .manufacturer = "AMD",
    .name = "Am486SX2",
    .internal_name = "am486sx2",
    .cpus = (const CPU[]) {
        {"50",  CPU_Am486SX,    fpus_486sx, 50000000, 2, 5000, 0x45b,     0, 0, CPU_SUPPORTS_DYNAREC,  8, 8, 6, 6, 6},
        {"66",  CPU_Am486SX,    fpus_486sx, 66666666, 2, 5000, 0x45b,     0, 0, CPU_SUPPORTS_DYNAREC, 12,12, 6, 6, 8},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET1,
    .manufacturer = "AMD",
    .name = "Am486DX",
    .internal_name = "am486dx",
    .cpus = (const CPU[]) {
        {"33",   CPU_Am486DX,  fpus_internal, 33333333, 1, 5000, 0x412,     0, 0, CPU_SUPPORTS_DYNAREC,  6, 6, 3, 3, 4},
        {"40",   CPU_Am486DX,  fpus_internal, 40000000, 1, 5000, 0x412,     0, 0, CPU_SUPPORTS_DYNAREC,  7, 7, 3, 3, 5},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET1,
    .manufacturer = "AMD",
    .name = "Am486DX2",
    .internal_name = "am486dx2",
    .cpus = (const CPU[]) {
        {"50",  CPU_Am486DX, fpus_internal, 50000000, 2, 5000, 0x432,     0, 0, CPU_SUPPORTS_DYNAREC,  8, 8, 6, 6, 6},
        {"66",  CPU_Am486DX, fpus_internal, 66666666, 2, 5000, 0x432,     0, 0, CPU_SUPPORTS_DYNAREC, 12,12, 6, 6, 8},
        {"80",  CPU_Am486DX, fpus_internal, 80000000, 2, 5000, 0x432,     0, 0, CPU_SUPPORTS_DYNAREC, 14,14, 6, 6, 10},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET1,
    .manufacturer = "AMD",
    .name = "Am486DXL",
    .internal_name = "am486dxl",
    .cpus = (const CPU[]) {
        {"33",   CPU_Am486DXL,  fpus_internal, 33333333, 1, 5000, 0x422,     0, 0, CPU_SUPPORTS_DYNAREC,  6, 6, 3, 3, 4},
        {"40",   CPU_Am486DXL,  fpus_internal, 40000000, 1, 5000, 0x422,     0, 0, CPU_SUPPORTS_DYNAREC,  7, 7, 3, 3, 5},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET1,
    .manufacturer = "AMD",
    .name = "Am486DXL2",
    .internal_name = "am486dxl2",
    .cpus = (const CPU[]) {
        {"50",  CPU_Am486DXL, fpus_internal, 50000000, 2, 5000, 0x432,     0, 0, CPU_SUPPORTS_DYNAREC,  8, 8, 6, 6, 6},
        {"66",  CPU_Am486DXL, fpus_internal, 66666666, 2, 5000, 0x432,     0, 0, CPU_SUPPORTS_DYNAREC, 12,12, 6, 6, 8},
        {"80",  CPU_Am486DXL, fpus_internal, 80000000, 2, 5000, 0x432,     0, 0, CPU_SUPPORTS_DYNAREC, 14,14, 6, 6, 10},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET3,
    .manufacturer = "AMD",
    .name = "Am486DX4",
    .internal_name = "am486dx4",
    .cpus = (const CPU[]) {
        {"75",  CPU_Am486DX, fpus_internal,   75000000, 3.0, 5000, 0x432,     0, 0, CPU_SUPPORTS_DYNAREC, 12,12, 9, 9, 9},
        {"90",  CPU_Am486DX, fpus_internal,   90000000, 3.0, 5000, 0x432,     0, 0, CPU_SUPPORTS_DYNAREC, 15,15, 9, 9, 12},
        {"100", CPU_Am486DX, fpus_internal,  100000000, 3.0, 5000, 0x432,     0, 0, CPU_SUPPORTS_DYNAREC, 15,15, 9, 9, 12},
        {"120", CPU_Am486DX, fpus_internal,  120000000, 3.0, 5000, 0x432,     0, 0, CPU_SUPPORTS_DYNAREC, 21,21, 9, 9, 15},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET3,
    .manufacturer = "AMD",
    .name = "Am486DX2 (Enhanced)",
    .internal_name = "am486dx2_slenh",
    .cpus = (const CPU[]) {
        {"66",  CPU_ENH_Am486DX, fpus_internal, 66666666, 2, 5000, 0x435, 0x435, 0, CPU_SUPPORTS_DYNAREC, 12,12, 6, 6, 8},
        {"80",  CPU_ENH_Am486DX, fpus_internal, 80000000, 2, 5000, 0x435, 0x435, 0, CPU_SUPPORTS_DYNAREC, 14,14, 6, 6, 10},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET3,
    .manufacturer = "AMD",
    .name = "Am486DX4 (Enhanced)",
    .internal_name = "am486dx4_slenh",
    .cpus = (const CPU[]) {
        {"75",  CPU_ENH_Am486DX, fpus_internal,   75000000, 3.0, 5000, 0x482, 0x482, 0, CPU_SUPPORTS_DYNAREC, 12,12, 9, 9, 9},
        {"100", CPU_ENH_Am486DX, fpus_internal,  100000000, 3.0, 5000, 0x482, 0x482, 0, CPU_SUPPORTS_DYNAREC, 15,15, 9, 9, 12},
        {"120", CPU_ENH_Am486DX, fpus_internal,  120000000, 3.0, 5000, 0x482, 0x482, 0, CPU_SUPPORTS_DYNAREC, 21,21, 9, 9, 15},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET3,
    .manufacturer = "AMD",
    .name = "Am5x86",
    .internal_name = "am5x86",
    .cpus = (const CPU[]) {
        {"133 (P75)",  CPU_ENH_Am486DX,   fpus_internal,  133333333, 4.0, 5000, 0x4e0, 0x4e0, 0, CPU_SUPPORTS_DYNAREC, 24,24,12,12, 16},
        {"150 (P75+)", CPU_ENH_Am486DX,   fpus_internal,  150000000, 3.0, 5000, 0x482, 0x482, 0, CPU_SUPPORTS_DYNAREC, 28,28,12,12, 20},/*The rare P75+ was indeed a triple-clocked 150 MHz according to research*/
        {"160 (P90)",  CPU_ENH_Am486DX,   fpus_internal,  160000000, 4.0, 5000, 0x4e0, 0x4e0, 0, CPU_SUPPORTS_DYNAREC, 28,28,12,12, 20},/*160 MHz on a 40 MHz bus was a common overclock and "5x86/P90" was used by a number of BIOSes to refer to that configuration*/
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET1,
    .manufacturer = "Cyrix",
    .name = "Cx486S",
    .internal_name = "cx486s",
    .cpus = (const CPU[]) {
        {"25",            CPU_Cx486S,    fpus_486sx,   25000000, 1.0, 5000,  0x420,      0, 0x0010, CPU_SUPPORTS_DYNAREC,  4, 4, 3, 3,  3},
        {"33",            CPU_Cx486S,    fpus_486sx,   33333333, 1.0, 5000,  0x420,      0, 0x0010, CPU_SUPPORTS_DYNAREC,  6, 6, 3, 3,  4},
        {"40",            CPU_Cx486S,    fpus_486sx,   40000000, 1.0, 5000,  0x420,      0, 0x0010, CPU_SUPPORTS_DYNAREC,  7, 7, 3, 3,  5},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET1,
    .manufacturer = "Cyrix",
    .name = "Cx486DX",
    .internal_name = "cx486dx",
    .cpus = (const CPU[]) {
        {"33",          CPU_Cx486DX, fpus_internal,   33333333, 1.0, 5000,  0x430,      0, 0x051a, CPU_SUPPORTS_DYNAREC,  6, 6, 3, 3,  4},
        {"40",          CPU_Cx486DX, fpus_internal,   40000000, 1.0, 5000,  0x430,      0, 0x051a, CPU_SUPPORTS_DYNAREC,  7, 7, 3, 3,  5},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET1,
    .manufacturer = "Cyrix",
    .name = "Cx486DX2",
    .internal_name = "cx486dx2",
    .cpus = (const CPU[]) {
        {"50",        CPU_Cx486DX, fpus_internal,   50000000, 2.0, 5000,  0x430,      0, 0x081b, CPU_SUPPORTS_DYNAREC,  8, 8, 6, 6,  6},
        {"66",        CPU_Cx486DX, fpus_internal,   66666666, 2.0, 5000,  0x430,      0, 0x0b1b, CPU_SUPPORTS_DYNAREC, 12,12, 6, 6,  8},
        {"80",        CPU_Cx486DX, fpus_internal,   80000000, 2.0, 5000,  0x430,      0, 0x311b, CPU_SUPPORTS_DYNAREC, 14,14, 6, 6, 10},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET3,
    .manufacturer = "Cyrix",
    .name = "Cx486DX4",
    .internal_name = "cx486dx4",
    .cpus = (const CPU[]) {
        {"75",  CPU_Cx486DX, fpus_internal,  75000000, 3.0, 5000, 0x480, 0, 0x361f, CPU_SUPPORTS_DYNAREC, 12,12, 9, 9,  9},
        {"100", CPU_Cx486DX, fpus_internal, 100000000, 3.0, 5000, 0x480, 0, 0x361f, CPU_SUPPORTS_DYNAREC, 15,15, 9, 9, 12},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET3,
    .manufacturer = "Cyrix",
    .name = "Cx5x86",
    .internal_name = "cx5x86",
    .cpus = (const CPU[]) {
        {"80",    CPU_Cx5x86,   fpus_internal,  80000000, 2.0, 5000, 0x480, 0, 0x002f, CPU_SUPPORTS_DYNAREC, 14,14, 6, 6, 10}, /*If we're including the Pentium 50, might as well include this*/
        {"100",   CPU_Cx5x86,   fpus_internal, 100000000, 3.0, 5000, 0x480, 0, 0x002f, CPU_SUPPORTS_DYNAREC, 15,15, 9, 9, 12},
        {"120",   CPU_Cx5x86,   fpus_internal, 120000000, 3.0, 5000, 0x480, 0, 0x002f, CPU_SUPPORTS_DYNAREC, 21,21, 9, 9, 15},
        {"133",   CPU_Cx5x86,   fpus_internal, 133333333, 4.0, 5000, 0x480, 0, 0x002f, CPU_SUPPORTS_DYNAREC, 24,24,12,12, 16},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_STPC,
    .manufacturer = "ST",
    .name = "STPC-DX",
    .internal_name = "stpc_dx",
    .cpus = (const CPU[]) {
        {"66",     CPU_STPC,    fpus_internal,  66666666, 1.0, 3300, 0x430, 0, 0x051a, CPU_SUPPORTS_DYNAREC, 7, 7, 3, 3,  5},
        {"75",     CPU_STPC,    fpus_internal,  75000000, 1.0, 3300, 0x430, 0, 0x051a, CPU_SUPPORTS_DYNAREC, 7, 7, 3, 3,  5},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_STPC,
    .manufacturer = "ST",
    .name = "STPC-DX2",
    .internal_name = "stpc_dx2",
    .cpus = (const CPU[]) {
        {"133",    CPU_STPC,     fpus_internal, 133333333, 2.0, 3300, 0x430, 0, 0x0b1b, CPU_SUPPORTS_DYNAREC, 14,14, 6, 6, 10},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET4,
    .manufacturer = "Intel",
    .name = "Pentium",
    .internal_name = "pentium_p5",
    .cpus = (const CPU[]) {
        {"50 (Q0399)",    CPU_PENTIUM, fpus_internal,  50000000, 1, 5000, 0x513, 0x513, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER,  4, 4,3,3,  6},
        {"60",            CPU_PENTIUM, fpus_internal,  60000000, 1, 5000, 0x517, 0x517, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER,  6, 6,3,3,  7},
        {"66",            CPU_PENTIUM, fpus_internal,  66666666, 1, 5000, 0x517, 0x517, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER,  6, 6,3,3,  8},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET4,
    .manufacturer = "Intel",
    .name = "Pentium OverDrive",
    .internal_name = "pentium_p54c_od5v",
    .cpus = (const CPU[]) {
        {"100", CPU_PENTIUM, fpus_internal, 100000000, 2, 5000, 0x51A, 0x51A, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER,  8, 8,6,6, 12},
        {"120", CPU_PENTIUM, fpus_internal, 120000000, 2, 5000, 0x51A, 0x51A, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 12,12,6,6, 14},
        {"133", CPU_PENTIUM, fpus_internal, 133333333, 2, 5000, 0x51A, 0x51A, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 12,12,6,6, 16},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET5_7,
    .manufacturer = "Intel",
    .name = "Pentium",
    .internal_name = "pentium_p54c",
    .cpus = (const CPU[]) {
        {"75",                   CPU_PENTIUM,    fpus_internal,  75000000, 1.5, 3520,  0x522,  0x522, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7,4,4,  9},
        {"90",                   CPU_PENTIUM,    fpus_internal,  90000000, 1.5, 3520,  0x524,  0x524, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9,4,4, 21/2},
        {"100/50",               CPU_PENTIUM,    fpus_internal, 100000000, 2.0, 3520,  0x524,  0x524, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10,6,6, 12},
        {"100/66",               CPU_PENTIUM,    fpus_internal, 100000000, 1.5, 3520,  0x526,  0x526, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9,4,4, 12},
        {"120",                  CPU_PENTIUM,    fpus_internal, 120000000, 2.0, 3520,  0x526,  0x526, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12,6,6, 14},
        {"133",                  CPU_PENTIUM,    fpus_internal, 133333333, 2.0, 3520,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12,6,6, 16},
        {"150",                  CPU_PENTIUM,    fpus_internal, 150000000, 2.5, 3520,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 35/2},
        {"166",                  CPU_PENTIUM,    fpus_internal, 166666666, 2.5, 3520,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20},
        {"200",                  CPU_PENTIUM,    fpus_internal, 200000000, 3.0, 3520,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 24},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET5_7,
    .manufacturer = "Intel",
    .name = "Pentium MMX",
    .internal_name = "pentium_p55c",
    .cpus = (const CPU[]) {
        {"166",              CPU_PENTIUMMMX, fpus_internal, 166666666, 2.5, 2800,  0x543,  0x543, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20},
        {"200",              CPU_PENTIUMMMX, fpus_internal, 200000000, 3.0, 2800,  0x543,  0x543, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 24},
        {"233",              CPU_PENTIUMMMX, fpus_internal, 233333333, 3.5, 2800,  0x543,  0x543, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,10,10, 28},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET5_7,
    .manufacturer = "Intel",
    .name = "Mobile Pentium MMX",
    .internal_name = "pentium_tillamook",
    .cpus = (const CPU[]) {
        {"120",       CPU_PENTIUMMMX, fpus_internal, 120000000, 2.0, 2800,  0x543,  0x543, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 14},
        {"133",       CPU_PENTIUMMMX, fpus_internal, 133333333, 2.0, 2800,  0x543,  0x543, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 16},
        {"150",       CPU_PENTIUMMMX, fpus_internal, 150000000, 2.5, 2800,  0x544,  0x544, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 35/2},
        {"166",       CPU_PENTIUMMMX, fpus_internal, 166666666, 2.5, 2800,  0x544,  0x544, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20},
        {"200",       CPU_PENTIUMMMX, fpus_internal, 200000000, 3.0, 2800,  0x581,  0x581, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 24},
        {"233",       CPU_PENTIUMMMX, fpus_internal, 233333333, 3.5, 2800,  0x581,  0x581, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,10,10, 28},
        {"266",       CPU_PENTIUMMMX, fpus_internal, 266666666, 4.0, 2800,  0x582,  0x582, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 24,24,12,12, 32},
        {"300",       CPU_PENTIUMMMX, fpus_internal, 300000000, 4.5, 2800,  0x582,  0x582, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27,13,13, 36},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET5_7,
    .manufacturer = "Intel",
    .name = "Pentium OverDrive",
    .internal_name = "pentium_p54c_od3v",
    .cpus = (const CPU[]) {
        {"125",        CPU_PENTIUM,    fpus_internal, 125000000, 3.0, 3520,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 12,12,7,7, 15},
        {"150",        CPU_PENTIUM,    fpus_internal, 150000000, 2.5, 3520,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 15,15,7,7, 35/2},
        {"166",        CPU_PENTIUM,    fpus_internal, 166666666, 2.5, 3520,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 15,15,7,7, 20},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET5_7,
    .manufacturer = "Intel",
    .name = "Pentium OverDrive MMX",
    .internal_name = "pentium_p55c_od",
    .cpus = (const CPU[]) {
        {"75",     CPU_PENTIUMMMX, fpus_internal,  75000000, 1.5, 3520, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER,  7, 7,4,4,  9},
        {"125",    CPU_PENTIUMMMX, fpus_internal, 125000000, 2.5, 3520, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 12,12,7,7, 15},
        {"150/60", CPU_PENTIUMMMX, fpus_internal, 150000000, 2.5, 3520, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 15,15,7,7, 35/2},
        {"166",    CPU_PENTIUMMMX, fpus_internal, 166000000, 2.5, 3520, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 15,15,7,7, 20},
        {"180",    CPU_PENTIUMMMX, fpus_internal, 180000000, 3.0, 3520, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 18,18,9,9, 21},
        {"200",    CPU_PENTIUMMMX, fpus_internal, 200000000, 3.0, 3520, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 18,18,9,9, 24},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET5_7,
    .manufacturer = "IDT",
    .name = "WinChip",
    .internal_name = "winchip",
    .cpus = (const CPU[]) {
        {"75",     CPU_WINCHIP,  fpus_internal,  75000000, 1.5,     3520, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC,  8,  8,  4,  4,  9},
        {"90",     CPU_WINCHIP,  fpus_internal,  90000000, 1.5,     3520, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC,  9,  9,  4,  4, 21/2},
        {"100",    CPU_WINCHIP,  fpus_internal, 100000000, 1.5,     3520, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC,  9,  9,  4,  4, 12},
        {"120",    CPU_WINCHIP,  fpus_internal, 120000000, 2.0,     3520, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 12, 12,  6,  6, 14},
        {"133",    CPU_WINCHIP,  fpus_internal, 133333333, 2.0,     3520, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 12, 12,  6,  6, 16},
        {"150",    CPU_WINCHIP,  fpus_internal, 150000000, 2.5,     3520, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 15, 15,  7,  7, 35/2},
        {"166",    CPU_WINCHIP,  fpus_internal, 166666666, 2.5,     3520, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 15, 15,  7,  7, 40},
        {"180",    CPU_WINCHIP,  fpus_internal, 180000000, 3.0,     3520, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 18, 18,  9,  9, 21},
        {"200",    CPU_WINCHIP,  fpus_internal, 200000000, 3.0,     3520, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 18, 18,  9,  9, 24},
        {"225",    CPU_WINCHIP,  fpus_internal, 225000000, 3.0,     3520, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 18, 18,  9,  9, 27},
        {"240",    CPU_WINCHIP,  fpus_internal, 240000000, 4.0,     3520, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 24, 24, 12, 12, 28},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET5_7,
    .manufacturer = "IDT",
    .name = "WinChip 2",
    .internal_name = "winchip2",
    .cpus = (const CPU[]) {
        {"200",  CPU_WINCHIP2, fpus_internal, 200000000, 3.0,     3520, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC, 18, 18,  9,  9, 3*8},
        {"225",  CPU_WINCHIP2, fpus_internal, 225000000, 3.0,     3520, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC, 18, 18,  9,  9, 3*9},
        {"240",  CPU_WINCHIP2, fpus_internal, 240000000, 4.0,     3520, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC, 24, 24, 12, 12, 30},
        {"250",  CPU_WINCHIP2, fpus_internal, 250000000, 3.0,     3520, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC, 24, 24, 12, 12, 30},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET5_7,
    .manufacturer = "IDT",
    .name = "WinChip 2A",
    .internal_name = "winchip2a",
    .cpus = (const CPU[]) {
        {"200", CPU_WINCHIP2, fpus_internal, 200000000, 3.0,     3520, 0x587, 0x587, 0, CPU_SUPPORTS_DYNAREC, 18, 18,  9,  9, 3*8},
        {"233", CPU_WINCHIP2, fpus_internal, 233333333, 3.5,     3520, 0x587, 0x587, 0, CPU_SUPPORTS_DYNAREC, 21, 21,  9,  9, (7*8)/2},
        {"266", CPU_WINCHIP2, fpus_internal, 233333333, 7.0/3.0, 3520, 0x587, 0x587, 0, CPU_SUPPORTS_DYNAREC, 21, 21,  7,  7, 28},
        {"300", CPU_WINCHIP2, fpus_internal, 250000000, 2.5,     3520, 0x587, 0x587, 0, CPU_SUPPORTS_DYNAREC, 24, 24,  8,  8, 30},
        {"", 0}
    }
    },
#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
    {
    .package = CPU_PKG_SOCKET5_7,
    .manufacturer = "AMD",
    .name = "K5 (Model 0)",
    .internal_name = "k5_ssa5",
    .cpus = (const CPU[]) {
        {"75 (PR75)",    CPU_K5,   fpus_internal,  75000000, 1.5, 3520, 0x501, 0x501, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7,4,4,  9},
        {"90 (PR90)",    CPU_K5,   fpus_internal,  90000000, 1.5, 3520, 0x501, 0x501, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9,4,4, 21/2},
        {"100 (PR100)",  CPU_K5,   fpus_internal, 100000000, 1.5, 3520, 0x501, 0x501, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9,4,4, 12},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET5_7,
    .manufacturer = "AMD",
    .name = "K5 (Model 1/2/3)",
    .internal_name = "k5_5k86",
    .cpus = (const CPU[]) {
        {"90 (PR120)",    CPU_5K86, fpus_internal, 120000000, 2.0, 3520, 0x511, 0x511, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12,6,6, 14},
        {"100 (PR133)",   CPU_5K86, fpus_internal, 133333333, 2.0, 3520, 0x514, 0x514, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12,6,6, 16},
        {"105 (PR150)",   CPU_5K86, fpus_internal, 150000000, 2.5, 3520, 0x524, 0x524, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15,7,7, 35/2},
        {"116.7 (PR166)", CPU_5K86, fpus_internal, 166666666, 2.5, 3520, 0x524, 0x524, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15,7,7, 20},
        {"133 (PR200)",   CPU_5K86, fpus_internal, 200000000, 3.0, 3520, 0x534, 0x534, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18,9,9, 24},
        {"", 0}
    }
    },
#endif
    {
    .package = CPU_PKG_SOCKET5_7,
    .manufacturer = "AMD",
    .name = "K6 (Model 6)",
    .internal_name = "k6_m6",
    .cpus = (const CPU[]) {
        {"66",         CPU_K6,    fpus_internal,   66666666, 1.0, 2900, 0x561, 0x561, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 6, 6, 3, 3, 8},     /* out of spec */
        {"100",        CPU_K6,    fpus_internal,  100000000, 1.5, 2900, 0x561, 0x561, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 9, 9, 4, 4, 12},   /* out of spec */
        {"133",        CPU_K6,    fpus_internal,  133333333, 2.0, 2900, 0x561, 0x561, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 16}, /* out of spec */
        {"166",        CPU_K6,    fpus_internal,  166666666, 2.5, 2900, 0x561, 0x561, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20},
        {"200",        CPU_K6,    fpus_internal,  200000000, 3.0, 2900, 0x561, 0x561, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 24},
        {"233",        CPU_K6,    fpus_internal,  233333333, 3.5, 3200, 0x561, 0x561, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,10,10, 28},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET5_7,
    .manufacturer = "AMD",
    .name = "K6 (Model 7)",
    .internal_name = "k6_m7",
    .cpus = (const CPU[]) {
        {"100",        CPU_K6,    fpus_internal,  100000000, 1.5, 2200, 0x570, 0x570, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 9, 9, 4, 4, 12}, /* out of spec */
        {"133",        CPU_K6,    fpus_internal,  133333333, 2.0, 2200, 0x570, 0x570, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 16}, /* out of spec */
        {"166",        CPU_K6,    fpus_internal,  166666666, 2.5, 2200, 0x570, 0x570, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20}, /* out of spec */
        {"200",        CPU_K6,    fpus_internal,  200000000, 3.0, 2200, 0x570, 0x570, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 24},
        {"233",        CPU_K6,    fpus_internal,  233333333, 3.5, 2200, 0x570, 0x570, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,10,10, 28},
        {"266",        CPU_K6,    fpus_internal,  266666666, 4.0, 2200, 0x570, 0x570, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 24,24,12,12, 32},
        {"300",        CPU_K6,    fpus_internal,  300000000, 4.5, 2200, 0x570, 0x570, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27,13,13, 36},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET5_7,
    .manufacturer = "AMD",
    .name = "K6-2",
    .internal_name = "k6_2",
    .cpus = (const CPU[]) {
        {"100",                CPU_K6_2,  fpus_internal,  100000000, 1.5, 2200, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    9,     9,   4,   4, 12}, /* out of spec */
        {"133",                CPU_K6_2,  fpus_internal,  133333333, 2.0, 2200, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    12,   12,   6,   6, 16}, /* out of spec */
        {"166",                CPU_K6_2,  fpus_internal,  166666666, 2.5, 2200, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    15,   15,   7,   7, 20}, /* out of spec */
        {"200",                CPU_K6_2,  fpus_internal,  200000000, 3.0, 2200, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    18,   18,   9,   9, 24}, /* out of spec */
        {"233",                CPU_K6_2,  fpus_internal,  233333333, 3.5, 2200, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    21,   21,  10,  10, 28},
        {"266",                CPU_K6_2,  fpus_internal,  266666666, 4.0, 2200, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    24,   24,  12,  12, 32},
        {"300",                CPU_K6_2,  fpus_internal,  300000000, 3.0, 2200, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    27,   27,   9,   9, 36},
        {"333",                CPU_K6_2,  fpus_internal,  332500000, 3.5, 2200, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    30,   30,  11,  11, 40},
        {"350",                CPU_K6_2C, fpus_internal,  350000000, 3.5, 2200, 0x58c, 0x58c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    32,   32,  11,  11, 42},
        {"366",                CPU_K6_2C, fpus_internal,  366666666, 5.5, 2200, 0x58c, 0x58c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    33,   33,  17,  17, 44},
        {"380",                CPU_K6_2C, fpus_internal,  380000000, 4.0, 2200, 0x58c, 0x58c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    34,   34,  12,  12, 46},
        {"400/66",             CPU_K6_2C, fpus_internal,  400000000, 6.0, 2200, 0x58c, 0x58c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    36,   36,  12,  12, 48},
        {"400/100",            CPU_K6_2C, fpus_internal,  400000000, 4.0, 2200, 0x58c, 0x58c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    36,   36,  12,  12, 48},
        {"450",                CPU_K6_2C, fpus_internal,  450000000, 4.5, 2200, 0x58c, 0x58c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    41,   41,  14,  14, 54},
        {"475",                CPU_K6_2C, fpus_internal,  475000000, 5.0, 2400, 0x58c, 0x58c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    43,   43,  15,  15, 57},
        {"500",                CPU_K6_2C, fpus_internal,  500000000, 5.0, 2400, 0x58c, 0x58c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    45,   45,  15,  15, 60},
        {"533",                CPU_K6_2C, fpus_internal,  533333333, 5.5, 2200, 0x58c, 0x58c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    48,   48,  17,  17, 64},
        {"550",                CPU_K6_2C, fpus_internal,  550000000, 5.5, 2300, 0x58c, 0x58c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    50,   50,  17,  17, 66},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET5_7,
    .manufacturer = "AMD",
    .name = "K6-2+",
    .internal_name = "k6_2p",
    .cpus = (const CPU[]) {
        {"100",               CPU_K6_2P,  fpus_internal,  100000000, 1.5, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    9,     9,   4,   4, 12}, /* out of spec */
        {"133",               CPU_K6_2P,  fpus_internal,  133333333, 2.0, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    12,   12,   6,   6, 16}, /* out of spec */
        {"166",               CPU_K6_2P,  fpus_internal,  166666666, 2.5, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    15,   15,   7,   7, 20}, /* out of spec */
        {"200",               CPU_K6_2P,  fpus_internal,  200000000, 3.0, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    18,   18,   9,   9, 24}, /* out of spec */
        {"233",               CPU_K6_2P,  fpus_internal,  233333333, 3.5, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    21,   21,  10,  10, 28}, /* out of spec */
        {"266",               CPU_K6_2P,  fpus_internal,  266666666, 4.0, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    24,   24,  12,  12, 32}, /* out of spec */
        {"300",               CPU_K6_2P,  fpus_internal,  300000000, 3.0, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    27,   27,   9,   9, 36}, /* out of spec */
        {"333",               CPU_K6_2P,  fpus_internal,  332500000, 3.5, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    30,   30,  11,  11, 40}, /* out of spec */
        {"350",               CPU_K6_2P,  fpus_internal,  350000000, 3.5, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    32,   32,  11,  11, 42}, /* out of spec */
        {"366",               CPU_K6_2P,  fpus_internal,  366666666, 5.5, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    33,   33,  17,  17, 44}, /* out of spec */
        {"380",               CPU_K6_2P,  fpus_internal,  380000000, 4.0, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    34,   34,  12,  12, 46}, /* out of spec */
        {"400/66",            CPU_K6_2P,  fpus_internal,  400000000, 6.0, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    36,   36,  12,  12, 48}, /* out of spec */
        {"400/100",           CPU_K6_2P,  fpus_internal,  400000000, 4.0, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    36,   36,  12,  12, 48}, /* out of spec */
        {"450",               CPU_K6_2P,  fpus_internal,  450000000, 4.5, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    41,   41,  14,  14, 54},
        {"475",               CPU_K6_2P,  fpus_internal,  475000000, 5.0, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    43,   43,  15,  15, 57},
        {"500",               CPU_K6_2P,  fpus_internal,  500000000, 5.0, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    45,   45,  15,  15, 60},
        {"533",               CPU_K6_2P,  fpus_internal,  533333333, 5.5, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    48,   48,  17,  17, 64},
        {"550",               CPU_K6_2P,  fpus_internal,  550000000, 5.5, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    50,   50,  17,  17, 66},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET5_7,
    .manufacturer = "AMD",
    .name = "K6-III",
    .internal_name = "k6_3",
    .cpus = (const CPU[]) {
        {"100",              CPU_K6_3,  fpus_internal,  100000000, 1.5, 2200, 0x591, 0x591, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    9,     9,   4,   4, 12}, /* out of spec */
        {"133",              CPU_K6_3,  fpus_internal,  133333333, 2.0, 2200, 0x591, 0x591, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    12,   12,   6,   6, 16}, /* out of spec */
        {"166",              CPU_K6_3,  fpus_internal,  166666666, 2.5, 2200, 0x591, 0x591, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    15,   15,   7,   7, 20}, /* out of spec */
        {"200",              CPU_K6_3,  fpus_internal,  200000000, 3.0, 2200, 0x591, 0x591, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    18,   18,   9,   9, 24}, /* out of spec */
        {"233",              CPU_K6_3,  fpus_internal,  233333333, 3.5, 2200, 0x591, 0x591, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    21,   21,  10,  10, 28}, /* out of spec */
        {"266",              CPU_K6_3,  fpus_internal,  266666666, 4.0, 2200, 0x591, 0x591, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    24,   24,  12,  12, 32}, /* out of spec */
        {"300",              CPU_K6_3,  fpus_internal,  300000000, 3.0, 2200, 0x591, 0x591, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    27,   27,   9,   9, 36}, /* out of spec */
        {"333",              CPU_K6_3,  fpus_internal,  332500000, 3.5, 2200, 0x591, 0x591, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    30,   30,  11,  11, 40}, /* out of spec */
        {"350",              CPU_K6_3,  fpus_internal,  350000000, 3.5, 2200, 0x591, 0x591, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    32,   32,  11,  11, 42}, /* out of spec */
        {"366",              CPU_K6_3,  fpus_internal,  366666666, 5.5, 2200, 0x591, 0x591, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    33,   33,  17,  17, 44}, /* out of spec */
        {"380",              CPU_K6_3,  fpus_internal,  380000000, 4.0, 2200, 0x591, 0x591, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    34,   34,  12,  12, 46}, /* out of spec */
        {"400",              CPU_K6_3,  fpus_internal,  400000000, 4.0, 2200, 0x591, 0x591, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    36,   36,  12,  12, 48},
        {"450",              CPU_K6_3,  fpus_internal,  450000000, 4.5, 2200, 0x591, 0x591, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    41,   41,  14,  14, 54},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET5_7,
    .manufacturer = "AMD",
    .name = "K6-III+",
    .internal_name = "k6_3p",
    .cpus = (const CPU[]) {
        {"100",             CPU_K6_3P, fpus_internal,  100000000, 1.5, 2000, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,     7,    7,   4,   4,  9}, /* out of spec */
        {"133",             CPU_K6_3P, fpus_internal,  133333333, 2.0, 2000, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    12,   12,   6,   6, 16}, /* out of spec */
        {"166",             CPU_K6_3P, fpus_internal,  166666666, 2.5, 2000, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    15,   15,   7,   7, 20}, /* out of spec */
        {"200",             CPU_K6_3P, fpus_internal,  200000000, 3.0, 2000, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    18,   18,   9,   9, 24}, /* out of spec */
        {"233",             CPU_K6_3P, fpus_internal,  233333333, 3.5, 2000, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    21,   21,  10,  10, 28}, /* out of spec */
        {"266",             CPU_K6_3P, fpus_internal,  266666666, 4.0, 2000, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    24,   24,  12,  12, 32}, /* out of spec */
        {"300",             CPU_K6_3P, fpus_internal,  300000000, 3.0, 2000, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    27,   27,   9,   9, 36}, /* out of spec */
        {"333",             CPU_K6_3P, fpus_internal,  332500000, 3.5, 2000, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    30,   30,  11,  11, 40}, /* out of spec */
        {"350",             CPU_K6_3P, fpus_internal,  350000000, 3.5, 2000, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    32,   32,  11,  11, 42}, /* out of spec */
        {"366",             CPU_K6_3P, fpus_internal,  366666666, 5.5, 2000, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    33,   33,  17,  17, 44}, /* out of spec */
        {"380",             CPU_K6_3P, fpus_internal,  380000000, 4.0, 2000, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    34,   34,  12,  12, 46}, /* out of spec */
        {"400",             CPU_K6_3P, fpus_internal,  400000000, 4.0, 2000, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    36,   36,  12,  12, 48},
        {"450",             CPU_K6_3P, fpus_internal,  450000000, 4.5, 2000, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    41,   41,  14,  14, 54},
        {"475",             CPU_K6_3P, fpus_internal,  475000000, 5.0, 2000, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    43,   43,  15,  15, 57},
        {"500",             CPU_K6_3P, fpus_internal,  500000000, 5.0, 2000, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    45,   45,  15,  15, 60},
        {"", 0}
    }
    },
#if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
    {
    .package = CPU_PKG_SOCKET5_7,
    .manufacturer = "Cyrix",
    .name = "Cx6x86",
    .internal_name = "cx6x86",
    .cpus = (const CPU[]) {
        {"80 (PR90+)",    CPU_Cx6x86, fpus_internal,    80000000, 2.0, 3520, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  8, 8, 6, 6, 10},
        {"100 (PR120+)",  CPU_Cx6x86, fpus_internal,   100000000, 2.0, 3520, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10, 6, 6, 12},
        {"110 (PR133+)",  CPU_Cx6x86, fpus_internal,   110000000, 2.0, 3520, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10, 6, 6, 14},
        {"120 (PR150+)",  CPU_Cx6x86, fpus_internal,   120000000, 2.0, 3520, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 14},
        {"133 (PR166+)",  CPU_Cx6x86, fpus_internal,   133333333, 2.0, 3520, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 16},
        {"150 (PR200+)",  CPU_Cx6x86, fpus_internal,   150000000, 2.0, 3520, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 18},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET5_7,
    .manufacturer = "Cyrix",
    .name = "Cx6x86L",
    .internal_name = "cx6x86l",
    .cpus = (const CPU[]) {
        {"110 (PR133+)", CPU_Cx6x86L,  fpus_internal, 110000000, 2.0, 2800, 0x540, 0x540, 0x2231, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10, 6, 6, 14},
        {"120 (PR150+)", CPU_Cx6x86L,  fpus_internal, 120000000, 2.0, 2800, 0x540, 0x540, 0x2231, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 14},
        {"133 (PR166+)", CPU_Cx6x86L,  fpus_internal, 133333333, 2.0, 2800, 0x540, 0x540, 0x2231, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 16},
        {"150 (PR200+)", CPU_Cx6x86L,  fpus_internal, 150000000, 2.0, 2800, 0x540, 0x540, 0x2231, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 18},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET5_7,
    .manufacturer = "Cyrix",
    .name = "Cx6x86MX",
    .internal_name = "cx6x86mx",
    .cpus = (const CPU[]) {
        {"133 (PR166)",   CPU_Cx6x86MX, fpus_internal, 133333333, 2.0, 2900, 0x600, 0x600, 0x0451, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6,   16},
        {"166 (PR200)",   CPU_Cx6x86MX, fpus_internal, 166666666, 2.5, 2900, 0x600, 0x600, 0x0452, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7,   20},
        {"187.5 (PR233)", CPU_Cx6x86MX, fpus_internal, 187500000, 2.5, 2900, 0x600, 0x600, 0x0452, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 45/2},
        {"208.3 (PR266)", CPU_Cx6x86MX, fpus_internal, 208333333, 2.5, 2700, 0x600, 0x600, 0x0452, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 17,17, 7, 7,   25},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET5_7,
    .manufacturer = "Cyrix",
    .name = "MII",
    .internal_name = "mii",
    .cpus = (const CPU[]) {
        {"233 (PR300)",     CPU_Cx6x86MX, fpus_internal, 233333333, 3.5, 2900, 0x601, 0x601, 0x0852, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,11,11,   28},
        {"250/83 (PR333)",  CPU_Cx6x86MX, fpus_internal, 250000000, 3.0, 2900, 0x601, 0x601, 0x0853, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 23,23, 9, 9,   30},
        {"250/100 (PR366)", CPU_Cx6x86MX, fpus_internal, 250000000, 2.5, 2900, 0x601, 0x601, 0x0853, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 23,23, 7, 7,   30},
        {"285 (PR400)",     CPU_Cx6x86MX, fpus_internal, 285000000, 3.0, 2900, 0x601, 0x601, 0x0853, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27, 9, 9,   34},
        {"300 (PR433)",     CPU_Cx6x86MX, fpus_internal, 300000000, 3.0, 2900, 0x601, 0x601, 0x0853, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27, 9, 9,   36},
        {"", 0}
    }
    },
#endif
    {
    .package = CPU_PKG_SOCKET8,
    .manufacturer = "Intel",
    .name = "Pentium Pro",
    .internal_name = "pentiumpro",
    .cpus = (const CPU[]) {
        {"60",              CPU_PENTIUMPRO, fpus_internal,  60000000, 1.0, 3100,  0x612,  0x612, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 1, 1, 7},  /* out of spec */
        {"66",              CPU_PENTIUMPRO, fpus_internal,  66666666, 1.0, 3300,  0x617,  0x617, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 1, 1, 8},  /* out of spec */
        {"90",              CPU_PENTIUMPRO, fpus_internal,  90000000, 1.5, 3100,  0x612,  0x612, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9, 3, 3, 11}, /* out of spec */
        {"100",             CPU_PENTIUMPRO, fpus_internal, 100000000, 1.5, 3300,  0x617,  0x617, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9, 3, 3, 12}, /* out of spec */
        {"120",             CPU_PENTIUMPRO, fpus_internal, 120000000, 2.0, 3100,  0x612,  0x612, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 5, 5, 14}, /* out of spec */
        {"133",             CPU_PENTIUMPRO, fpus_internal, 133333333, 2.0, 3300,  0x617,  0x617, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 5, 5, 16}, /* out of spec */
        {"150",             CPU_PENTIUMPRO, fpus_internal, 150000000, 2.5, 3100,  0x612,  0x612, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 35/2},
        {"166",             CPU_PENTIUMPRO, fpus_internal, 166666666, 2.5, 3300,  0x617,  0x617, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20},
        {"180",             CPU_PENTIUMPRO, fpus_internal, 180000000, 3.0, 3300,  0x617,  0x617, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 21},
        {"200",             CPU_PENTIUMPRO, fpus_internal, 200000000, 3.0, 3300,  0x617,  0x617, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 24},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET8,
    .manufacturer = "Intel",
    .name = "Pentium II OverDrive",
    .internal_name = "pentium2_od",
    .cpus = (const CPU[]) {
        {"66",     CPU_PENTIUM2D,  fpus_internal,  66666666, 1.0, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER,  6, 6, 3, 3, 8},  /* out of spec */
        {"100",    CPU_PENTIUM2D,  fpus_internal, 100000000, 1.5, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER,  9, 9, 4, 4, 12}, /* out of spec */
        {"133",    CPU_PENTIUM2D,  fpus_internal, 133333333, 2.0, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 12,12, 6, 6, 16}, /* out of spec */
        {"166",    CPU_PENTIUM2D,  fpus_internal, 166666666, 2.5, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 15,15, 7, 7, 20}, /* out of spec */
        {"200",    CPU_PENTIUM2D,  fpus_internal, 200000000, 3.0, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 18,18, 9, 9, 24}, /* out of spec */
        {"233",    CPU_PENTIUM2D,  fpus_internal, 233333333, 3.5, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 21,21,10,10, 28}, /* out of spec */
        {"266",    CPU_PENTIUM2D,  fpus_internal, 266666666, 4.0, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 24,24,12,12, 32}, /* out of spec */
        {"300",    CPU_PENTIUM2D,  fpus_internal, 300000000, 5.0, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 27,27,13,13, 36},
        {"333",    CPU_PENTIUM2D,  fpus_internal, 333333333, 5.0, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 27,27,13,13, 40},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SLOT1,
    .manufacturer = "Intel",
    .name = "Pentium II (Klamath)",
    .internal_name = "pentium2_klamath",
    .cpus = (const CPU[]) {
        {"66",        CPU_PENTIUM2,  fpus_internal,  66666666, 1.0, 2800,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 8},  /* out of spec */
        {"100",       CPU_PENTIUM2,  fpus_internal, 100000000, 1.5, 2800,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9, 4, 4, 12}, /* out of spec */
        {"133",       CPU_PENTIUM2,  fpus_internal, 133333333, 2.0, 2800,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 16}, /* out of spec */
        {"166",       CPU_PENTIUM2,  fpus_internal, 166666666, 2.5, 2800,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20}, /* out of spec */
        {"200",       CPU_PENTIUM2,  fpus_internal, 200000000, 3.0, 2800,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 24}, /* out of spec */
        {"233",       CPU_PENTIUM2,  fpus_internal, 233333333, 3.5, 2800,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,10,10, 28},
        {"266",       CPU_PENTIUM2,  fpus_internal, 266666666, 4.0, 2800,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 24,24,12,12, 32},
        {"300",       CPU_PENTIUM2,  fpus_internal, 300000000, 4.5, 2800,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 25,25,12,12, 36},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SLOT1,
    .manufacturer = "Intel",
    .name = "Pentium II (Deschutes)",
    .internal_name = "pentium2_deschutes",
    .cpus = (const CPU[]) {
        {"66",     CPU_PENTIUM2D,  fpus_internal,  66666666, 1.0, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 8},  /* out of spec */
        {"100",    CPU_PENTIUM2D,  fpus_internal, 100000000, 1.5, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9, 5, 5, 12}, /* out of spec */
        {"133",    CPU_PENTIUM2D,  fpus_internal, 133333333, 2.0, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 16}, /* out of spec */
        {"166",    CPU_PENTIUM2D,  fpus_internal, 166666666, 2.5, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20}, /* out of spec */
        {"200",    CPU_PENTIUM2D,  fpus_internal, 200000000, 3.0, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 24}, /* out of spec */
        {"233",    CPU_PENTIUM2D,  fpus_internal, 233333333, 3.5, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,11,11, 28}, /* out of spec */
        {"266",    CPU_PENTIUM2D,  fpus_internal, 266666666, 4.0, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 24,24,12,12, 32},
        {"300",    CPU_PENTIUM2D,  fpus_internal, 300000000, 4.5, 2050,  0x651,  0x651, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 25,25,12,12, 36},
        {"333",    CPU_PENTIUM2D,  fpus_internal, 333333333, 5.0, 2050,  0x651,  0x651, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27,13,13, 40},
        {"350",    CPU_PENTIUM2D,  fpus_internal, 350000000, 3.5, 2050,  0x651,  0x651, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 32,32,11,11, 42},
        {"400",    CPU_PENTIUM2D,  fpus_internal, 400000000, 4.0, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 36,36,12,12, 48},
        {"450",    CPU_PENTIUM2D,  fpus_internal, 450000000, 4.5, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 41,41,14,14, 54},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SLOT1,
    .manufacturer = "Intel",
    .name = "Celeron (Covington)",
    .internal_name = "celeron_covington",
    .cpus = (const CPU[]) {
        {"66",     CPU_PENTIUM2D,  fpus_internal,  66666666, 1.0, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 6, 6, 8},  /* out of spec */
        {"100",    CPU_PENTIUM2D,  fpus_internal, 100000000, 1.5, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9, 9, 9, 12}, /* out of spec */
        {"133",    CPU_PENTIUM2D,  fpus_internal, 133333333, 2.0, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12,12,12, 16}, /* out of spec */
        {"166",    CPU_PENTIUM2D,  fpus_internal, 166666666, 2.5, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15,15,15, 20}, /* out of spec */
        {"200",    CPU_PENTIUM2D,  fpus_internal, 200000000, 3.0, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18,18,18, 24}, /* out of spec */
        {"233",    CPU_PENTIUM2D,  fpus_internal, 233333333, 3.5, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,21,21, 28}, /* out of spec */
        {"266",    CPU_PENTIUM2D,  fpus_internal, 266666666, 4.0, 2050,  0x650,  0x650, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 24,24,24,24, 32},
        {"300",    CPU_PENTIUM2D,  fpus_internal, 300000000, 4.5, 2050,  0x651,  0x651, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 25,25,25,25, 36},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SLOT2,
    .manufacturer = "Intel",
    .name = "Pentium II Xeon",
    .internal_name = "pentium2_xeon",
    .cpus = (const CPU[]) {
        {"100",    CPU_PENTIUM2D,  fpus_internal, 100000000, 1.0, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9, 3, 3, 12}, /* out of spec */
        {"150",    CPU_PENTIUM2D,  fpus_internal, 150000000, 1.5, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 14,14, 4, 4, 18}, /* out of spec */
        {"200",    CPU_PENTIUM2D,  fpus_internal, 200000000, 2.0, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 6, 6, 24}, /* out of spec */
        {"250",    CPU_PENTIUM2D,  fpus_internal, 250000000, 2.5, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 22,22, 7, 7, 30}, /* out of spec */
        {"300",    CPU_PENTIUM2D,  fpus_internal, 300000000, 3.0, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27, 9, 9, 36}, /* out of spec */
        {"350",    CPU_PENTIUM2D,  fpus_internal, 350000000, 3.5, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 32,32,10,10, 42}, /* out of spec */
        {"400",    CPU_PENTIUM2D,  fpus_internal, 400000000, 4.0, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 36,36,12,12, 48},
        {"450",    CPU_PENTIUM2D,  fpus_internal, 450000000, 4.5, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 41,41,14,14, 54},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET370,
    .manufacturer = "Intel",
    .name = "Celeron (Mendocino)",
    .internal_name = "celeron_mendocino",
    .cpus = (const CPU[]) {
        {"66",        CPU_PENTIUM2D,  fpus_internal,  66666666, 1.0, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER,  6, 6, 3, 3, 8},  /* out of spec */
        {"100",       CPU_PENTIUM2D,  fpus_internal, 100000000, 1.5, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER,  8, 8, 4, 4, 12}, /* out of spec */
        {"133",       CPU_PENTIUM2D,  fpus_internal, 133333333, 2.0, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 11,11, 5, 5, 16}, /* out of spec */
        {"166",       CPU_PENTIUM2D,  fpus_internal, 166666666, 2.5, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 14,14, 7, 7, 20}, /* out of spec */
        {"200",       CPU_PENTIUM2D,  fpus_internal, 200000000, 3.0, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 17,17, 8, 8, 24}, /* out of spec */
        {"233",       CPU_PENTIUM2D,  fpus_internal, 233333333, 3.5, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 19,19, 9, 9, 28}, /* out of spec */
        {"266",       CPU_PENTIUM2D,  fpus_internal, 266666666, 4.0, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 22,22,11,11, 32}, /* out of spec */
        {"300A",      CPU_PENTIUM2D,  fpus_internal, 300000000, 4.5, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 25,25,12,12, 36},
        {"333",       CPU_PENTIUM2D,  fpus_internal, 333333333, 5.0, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 27,27,13,13, 40},
        {"366",       CPU_PENTIUM2D,  fpus_internal, 366666666, 5.5, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 33,33,17,17, 44},
        {"400",       CPU_PENTIUM2D,  fpus_internal, 400000000, 6.0, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 36,36,12,12, 48},
        {"433",       CPU_PENTIUM2D,  fpus_internal, 433333333, 6.5, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 39,39,13,13, 51},
        {"466",       CPU_PENTIUM2D,  fpus_internal, 466666666, 7.0, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 42,42,14,14, 56},
        {"500",       CPU_PENTIUM2D,  fpus_internal, 500000000, 7.5, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 45,45,15,15, 60},
        {"533",       CPU_PENTIUM2D,  fpus_internal, 533333333, 8.0, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC | CPU_FIXED_MULTIPLIER, 48,48,17,17, 64},
        {"", 0}
    }
    }, {
    .package = CPU_PKG_SOCKET370,
    .manufacturer = "VIA",
    .name = "Cyrix III",
    .internal_name = "c3_samuel",
    .cpus = (const CPU[]) {
        {"66",      CPU_CYRIX3S, fpus_internal,  66666666, 1.0, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER,  6,  6,  3,  3,  8}, /* out of multiplier range */
        {"100",     CPU_CYRIX3S, fpus_internal, 100000000, 1.5, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER,  9,  9,  4,  4, 12}, /* out of multiplier range */
        {"133",     CPU_CYRIX3S, fpus_internal, 133333333, 2.0, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 12, 12,  6,  6, 16}, /* out of multiplier range */
        {"166",     CPU_CYRIX3S, fpus_internal, 166666666, 2.5, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 15, 15,  7,  7, 20}, /* out of multiplier range */
        {"200",     CPU_CYRIX3S, fpus_internal, 200000000, 3.0, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 18, 18,  8,  8, 24}, /* out of multiplier range */
        {"233",     CPU_CYRIX3S, fpus_internal, 233333333, 3.5, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 21, 21,  9,  9, 28}, /* out of multiplier range */
        {"266",     CPU_CYRIX3S, fpus_internal, 266666666, 4.0, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 24, 24, 12, 12, 32}, /* out of multiplier range */
        {"300",     CPU_CYRIX3S, fpus_internal, 300000000, 4.5, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 27, 27, 13, 13, 36}, /* out of spec */
        {"333",     CPU_CYRIX3S, fpus_internal, 333333333, 5.0, 2050,   0x662, 0x662, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 30, 30, 15, 15, 40}, /* out of spec */
        {"366",     CPU_CYRIX3S, fpus_internal, 366666666, 5.5, 2050,   0x662, 0x662, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 33, 33, 16, 16, 44}, /* out of spec */
        {"400",     CPU_CYRIX3S, fpus_internal, 400000000, 6.0, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 36, 36, 17, 17, 48},
        {"433",     CPU_CYRIX3S, fpus_internal, 433333333, 6.5, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 39, 39, 18, 18, 52}, /* out of spec */
        {"450",     CPU_CYRIX3S, fpus_internal, 450000000, 4.5, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 41, 41, 14, 14, 54},
        {"466",     CPU_CYRIX3S, fpus_internal, 466666666, 6.5, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 42, 42, 14, 14, 56}, /* out of spec */
        {"500",     CPU_CYRIX3S, fpus_internal, 500000000, 5.0, 2050,   0x662, 0x662, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 45, 45, 15, 15, 60},
        {"533",     CPU_CYRIX3S, fpus_internal, 533333333, 8.0, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 48, 48, 15, 15, 64}, /* out of spec */
        {"550",     CPU_CYRIX3S, fpus_internal, 550000000, 5.5, 2050,   0x662, 0x662, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 50, 50, 17, 17, 66},
        {"600/100", CPU_CYRIX3S, fpus_internal, 600000000, 6.0, 2050,   0x662, 0x662, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 54, 54, 18, 18, 72},
        {"600/133", CPU_CYRIX3S, fpus_internal, 600000000, 4.5, 2050,   0x663, 0x663, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 54, 54, 13, 13, 72},
        {"650",     CPU_CYRIX3S, fpus_internal, 650000000, 6.5, 2050,   0x663, 0x663, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 58, 58, 20, 20, 78},
        {"667",     CPU_CYRIX3S, fpus_internal, 666666667, 5.0, 2050,   0x663, 0x663, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 60, 60, 16, 16, 80},
        {"700",     CPU_CYRIX3S, fpus_internal, 700000000, 7.0, 2050,   0x663, 0x663, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 63, 63, 21, 21, 84},
        {"733",     CPU_CYRIX3S, fpus_internal, 733333333, 5.5, 2050,   0x663, 0x663, 0, CPU_SUPPORTS_DYNAREC | CPU_FIXED_MULTIPLIER, 66, 66, 18, 18, 88},
        {"", 0}
    }
    }, {
    .package = 0,
    }
  // clang-format on
};
