/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Define all known processor types.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		leilei,
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		RichardG, <richardg867@gmail.com>
 *		dob205,
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 leilei.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2020 Fred N. van Kempen.
 *		Copyright 2020 RichardG.
 *		Copyright 2021 dob205.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/machine.h>

FPU fpus_none[] =
{
        {"None", "none", FPU_NONE},
        {NULL, NULL, 0}
};
FPU fpus_8088[] =
{
        {"None", "none",  FPU_NONE},
        {"8087", "8087",  FPU_8087},
        {NULL, NULL, 0}
};
FPU fpus_80186[] =
{
        {"None", "none",  FPU_NONE},
        {"8087", "8087",  FPU_8087},
        {"80187", "80187",  FPU_80187},
        {NULL, NULL, 0}
};
FPU fpus_80286[] =
{
        {"None", "none",  FPU_NONE},
        {"287",  "287",   FPU_287},
        {"287XL","287xl", FPU_287XL},
        {NULL, NULL, 0}
};
FPU fpus_80386[] =
{
        {"None", "none",  FPU_NONE},
        {"387",  "387",   FPU_387},
        {NULL, NULL, 0}
};
FPU fpus_486sx[] =
{
        {"None", "none",  FPU_NONE},
        {"487SX","487sx", FPU_487SX},
        {NULL, NULL, 0}
};
FPU fpus_internal[] =
{
        {"Internal", "internal", FPU_INTERNAL},
        {NULL, NULL, 0}
};


const cpu_family_t cpu_families[] = {
    {
	.package = CPU_PKG_8088,
	.manufacturer = "Intel",
	.name = "8088",
	.internal_name = "8088",
	.cpus = (const CPU[]) {
		{"4.77",    CPU_8088, fpus_8088,  4772728,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"7.16",    CPU_8088, fpus_8088,  7159092,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"8",       CPU_8088, fpus_8088,  8000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
//		{"9.54",    CPU_8088, fpus_8088,  9545456,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"10",      CPU_8088, fpus_8088, 10000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"12",      CPU_8088, fpus_8088, 12000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"16",      CPU_8088, fpus_8088, 16000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_8088_EUROPC,
	.manufacturer = "Intel",
	.name = "8088",
	.internal_name = "8088_europc",
	.cpus = (const CPU[]) {
		{"4.77",    CPU_8088, fpus_8088,  4772728,    1, 5000, 0, 0, 0, CPU_ALTERNATE_XTAL, 0,0,0,0, 1},
		{"7.16",    CPU_8088, fpus_8088,  7159092,    1, 5000, 0, 0, 0, CPU_ALTERNATE_XTAL, 0,0,0,0, 1},
		{"9.54",    CPU_8088, fpus_8088,  9545456,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_8086,
	.manufacturer = "Intel",
	.name = "8086",
	.internal_name = "8086",
	.cpus = (const CPU[]) {
		{"7.16",    CPU_8086, fpus_8088,   7159092,    1, 5000, 0, 0, 0, CPU_ALTERNATE_XTAL, 0,0,0,0, 1},
		{"8",       CPU_8086, fpus_8088,   8000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"9.54",    CPU_8086, fpus_8088,   9545456,    1, 5000, 0, 0, 0, CPU_ALTERNATE_XTAL, 0,0,0,0, 1},
		{"10",      CPU_8086, fpus_8088,  10000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"12",      CPU_8086, fpus_8088,  12000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"16",      CPU_8086, fpus_8088,  16000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 2},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_188,
	.manufacturer = "Intel",
	.name = "80188",
	.internal_name = "80188",
	.cpus = (const CPU[]) {
		{"6",       CPU_188, fpus_8088,   6000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"7.16",    CPU_188, fpus_8088,   7159092,    1, 5000, 0, 0, 0, CPU_ALTERNATE_XTAL, 0,0,0,0, 1},
		{"8",       CPU_188, fpus_8088,   8000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"9.54",    CPU_188, fpus_8088,   9545456,    1, 5000, 0, 0, 0, CPU_ALTERNATE_XTAL, 0,0,0,0, 1},
		{"10",      CPU_188, fpus_8088,  10000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"12",      CPU_188, fpus_8088,  12000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"16",      CPU_188, fpus_8088,  16000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 2},
		{"20",      CPU_188, fpus_8088,  20000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 3},
		{"25",      CPU_188, fpus_8088,  25000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 3},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_8088,
	.manufacturer = "NEC",
	.name = "V20",
	.internal_name = "necv20",
	.cpus = (const CPU[]) {
		{"5",       CPU_V20, fpus_8088,   5000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"8",       CPU_V20, fpus_8088,   8000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"10",      CPU_V20, fpus_8088,  10000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"12",      CPU_V20, fpus_8088,  12000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"16",      CPU_V20, fpus_8088,  16000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 2},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_186,
	.manufacturer = "Intel",
	.name = "80186",
	.internal_name = "80186",
	.cpus = (const CPU[]) {
		{"6",       CPU_186, fpus_80186,   6000000,    1, 0, 0, 0, 0, 0, 0,0,0,0, 1},
		{"7.16",    CPU_186, fpus_80186,   7159092,    1, 0, 0, 0, 0, CPU_ALTERNATE_XTAL, 0,0,0,0, 1},
		{"8",       CPU_186, fpus_80186,   8000000,    1, 0, 0, 0, 0, 0, 0,0,0,0, 1},
		{"9.54",    CPU_186, fpus_80186,   9545456,    1, 0, 0, 0, 0, CPU_ALTERNATE_XTAL, 0,0,0,0, 1},
		{"10",      CPU_186, fpus_80186,  10000000,    1, 0, 0, 0, 0, 0, 0,0,0,0, 1},
		{"12",      CPU_186, fpus_80186,  12000000,    1, 0, 0, 0, 0, 0, 0,0,0,0, 1},
		{"16",      CPU_186, fpus_80186,  16000000,    1, 0, 0, 0, 0, 0, 0,0,0,0, 2},
		{"20",      CPU_186, fpus_80186,  20000000,    1, 0, 0, 0, 0, 0, 0,0,0,0, 3},
		{"25",      CPU_186, fpus_80186,  25000000,    1, 0, 0, 0, 0, 0, 0,0,0,0, 3},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_8086,
	.manufacturer = "NEC",
	.name = "V30",
	.internal_name = "necv30",
	.cpus = (const CPU[]) {
		{"5",       CPU_V30, fpus_80186,   5000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"8",       CPU_V30, fpus_80186,   8000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"10",      CPU_V30, fpus_80186,  10000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"12",      CPU_V30, fpus_80186,  12000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"16",      CPU_V30, fpus_80186,  16000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 2},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_286,
	.manufacturer = "Intel",
	.name = "80286",
	.internal_name = "286",
	.cpus = (const CPU[]) {
		{"6",        CPU_286, fpus_80286,    6000000,    1, 5000, 0, 0, 0, 0, 2,2,2,2, 1},
		{"8",        CPU_286, fpus_80286,    8000000,    1, 5000, 0, 0, 0, 0, 2,2,2,2, 1},
		{"10",       CPU_286, fpus_80286,   10000000,    1, 5000, 0, 0, 0, 0, 2,2,2,2, 1},
		{"12",       CPU_286, fpus_80286,   12500000,    1, 5000, 0, 0, 0, 0, 3,3,3,3, 2},
		{"16",       CPU_286, fpus_80286,   16000000,    1, 5000, 0, 0, 0, 0, 3,3,3,3, 2},
		{"20",       CPU_286, fpus_80286,   20000000,    1, 5000, 0, 0, 0, 0, 4,4,4,4, 3},
		{"25",       CPU_286, fpus_80286,   25000000,    1, 5000, 0, 0, 0, 0, 4,4,4,4, 3},
		{"", 0}
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
    },
    {
	.package = CPU_PKG_M6117,
	.manufacturer = "ALi",
	.name = "M6117",
	.internal_name = "m6117",
	.cpus = (const CPU[]) { /* All timings and edx_reset values assumed. */
		{"33",    CPU_386SX,      fpus_none,  33333333, 1, 5000, 0x2308, 0, 0, 0, 6,6,3,3, 4},
		{"40",    CPU_386SX,      fpus_none,  40000000, 1, 5000, 0x2308, 0, 0, 0, 7,7,3,3, 5},
		{"", 0}
	}
    },
    {
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
	.name = "i486SX (SL-Enhanced)",
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
	.name = "i486DX (SL-Enhanced)",
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
	.name = "i486DX2 (SL-Enhanced)",
	.internal_name = "i486dx2_slenh",
	.cpus = (const CPU[]) {
		{"40",           CPU_i486DX_SLENH, fpus_internal,  40000000, 2,  5000, 0x435,    0x435, 0, CPU_SUPPORTS_DYNAREC,  7, 7,6,6,  5},
		{"50",           CPU_i486DX_SLENH, fpus_internal,  50000000, 2,  5000, 0x435,    0x435, 0, CPU_SUPPORTS_DYNAREC,  8, 8,6,6,  6},
		{"66",           CPU_i486DX_SLENH, fpus_internal,  66666666, 2,  5000, 0x435,    0x435, 0, CPU_SUPPORTS_DYNAREC, 12,12,6,6,  8},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET3_PC330,
	.manufacturer = "Intel",
	.name = "i486DX2",
	.internal_name = "i486dx2_pc330",
	.cpus = (const CPU[]) {
		{"50",           CPU_i486DX_SLENH, fpus_internal,  50000000, 2,  5000, 0x470,    0x470, 0, CPU_SUPPORTS_DYNAREC,  8, 8,6,6,  6},
		{"66",           CPU_i486DX_SLENH, fpus_internal,  66666666, 2,  5000, 0x470,    0x470, 0, CPU_SUPPORTS_DYNAREC, 12,12,6,6,  8},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET1 | CPU_PKG_SOCKET3_PC330, /*OEM versions are 3.3V, Retail versions are 3.3V with a 5V regulator for installation in older boards. hey are functionally identical*/
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
    },     {
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
    },
     {
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
		{"P75",   CPU_ENH_Am486DX,   fpus_internal,  133333333, 4.0, 5000, 0x4e0, 0x4e0, 0, CPU_SUPPORTS_DYNAREC, 24,24,12,12, 16},
		{"P75+",  CPU_ENH_Am486DX,   fpus_internal,  150000000, 3.0, 5000, 0x482, 0x482, 0, CPU_SUPPORTS_DYNAREC, 28,28,12,12, 20},/*The rare P75+ was indeed a triple-clocked 150 MHz according to research*/
		{"P90",   CPU_ENH_Am486DX,   fpus_internal,  160000000, 4.0, 5000, 0x4e0, 0x4e0, 0, CPU_SUPPORTS_DYNAREC, 28,28,12,12, 20},/*160 MHz on a 40 MHz bus was a common overclock and "5x86/P90" was used by a number of BIOSes to refer to that configuration*/
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
	.name = "K5 (5k86)",
	.internal_name = "k5_5k86",
	.cpus = (const CPU[]) {
		{"75 (P75)",      CPU_K5,   fpus_internal,  75000000, 1.5, 3520, 0x500, 0x500, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7,4,4,  9},
		{"90 (P90)",      CPU_K5,   fpus_internal,  90000000, 1.5, 3520, 0x500, 0x500, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9,4,4, 21/2},
		{"100 (P100)",    CPU_K5,   fpus_internal, 100000000, 1.5, 3520, 0x500, 0x500, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9,4,4, 12},
		{"90 (PR120)",    CPU_5K86, fpus_internal, 120000000, 2.0, 3520, 0x511, 0x511, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12,6,6, 14},
		{"100 (PR133)",   CPU_5K86, fpus_internal, 133333333, 2.0, 3520, 0x514, 0x514, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12,6,6, 16},
		{"105 (PR150)",   CPU_5K86, fpus_internal, 150000000, 2.5, 3520, 0x524, 0x524, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15,7,7, 35/2},
		{"116.5 (PR166)", CPU_5K86, fpus_internal, 166666666, 2.5, 3520, 0x524, 0x524, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15,7,7, 20},
		{"133 (PR200)",   CPU_5K86, fpus_internal, 200000000, 3.0, 3520, 0x534, 0x534, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18,9,9, 24},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET5_7,
	.manufacturer = "AMD",
	.name = "K5 (SSA/5)",
	.internal_name = "k5_ssa5",
	.cpus = (const CPU[]) {
		{"75 (PR75)",    CPU_K5,   fpus_internal,  75000000, 1.5, 3520, 0x501, 0x501, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7,4,4,  9},
		{"90 (PR90)",    CPU_K5,   fpus_internal,  90000000, 1.5, 3520, 0x501, 0x501, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9,4,4, 21/2},
		{"100 (PR100)",  CPU_K5,   fpus_internal, 100000000, 1.5, 3520, 0x501, 0x501, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9,4,4, 12},
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
		{"P90",     CPU_Cx6x86, fpus_internal,    80000000, 2.0, 3520, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  8, 8, 6, 6, 10},
		{"PR120+",  CPU_Cx6x86, fpus_internal,   100000000, 2.0, 3520, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10, 6, 6, 12},
		{"PR133+",  CPU_Cx6x86, fpus_internal,   110000000, 2.0, 3520, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10, 6, 6, 14},
		{"PR150+",  CPU_Cx6x86, fpus_internal,   120000000, 2.0, 3520, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 14},
		{"PR166+",  CPU_Cx6x86, fpus_internal,   133333333, 2.0, 3520, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 16},
		{"PR200+",  CPU_Cx6x86, fpus_internal,   150000000, 2.0, 3520, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 18},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET5_7,
	.manufacturer = "Cyrix",
	.name = "Cx6x86L",
	.internal_name = "cx6x86l",
	.cpus = (const CPU[]) {
		{"PR133+", CPU_Cx6x86L,  fpus_internal, 110000000, 2.0, 2800, 0x540, 0x540, 0x2231, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10, 6, 6, 14},
		{"PR150+", CPU_Cx6x86L,  fpus_internal, 120000000, 2.0, 2800, 0x540, 0x540, 0x2231, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 14},
		{"PR166+", CPU_Cx6x86L,  fpus_internal, 133333333, 2.0, 2800, 0x540, 0x540, 0x2231, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 16},
		{"PR200+", CPU_Cx6x86L,  fpus_internal, 150000000, 2.0, 2800, 0x540, 0x540, 0x2231, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 18},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET5_7,
	.manufacturer = "Cyrix",
	.name = "Cx6x86MX",
	.internal_name = "cx6x86mx",
	.cpus = (const CPU[]) {
		{"PR166", CPU_Cx6x86MX, fpus_internal, 133333333, 2.0, 2900, 0x600, 0x600, 0x0451, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6,   16},
		{"PR200", CPU_Cx6x86MX, fpus_internal, 166666666, 2.5, 2900, 0x600, 0x600, 0x0452, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7,   20},
		{"PR233", CPU_Cx6x86MX, fpus_internal, 187500000, 2.5, 2900, 0x600, 0x600, 0x0452, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 45/2},
		{"PR266", CPU_Cx6x86MX, fpus_internal, 208333333, 2.5, 2700, 0x600, 0x600, 0x0452, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 17,17, 7, 7,   25},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET5_7,
	.manufacturer = "Cyrix",
	.name = "MII",
	.internal_name = "mii",
	.cpus = (const CPU[]) {
		{"PR300",      CPU_Cx6x86MX, fpus_internal, 233333333, 3.5, 2900, 0x601, 0x601, 0x0852, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,11,11,   28},
		{"PR333",      CPU_Cx6x86MX, fpus_internal, 250000000, 3.0, 2900, 0x601, 0x601, 0x0853, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 23,23, 9, 9,   30},
		{"PR366",      CPU_Cx6x86MX, fpus_internal, 250000000, 2.5, 2900, 0x601, 0x601, 0x0853, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 23,23, 7, 7,   30},
		{"PR400",      CPU_Cx6x86MX, fpus_internal, 285000000, 3.0, 2900, 0x601, 0x601, 0x0853, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27, 9, 9,   34},
		{"PR433",      CPU_Cx6x86MX, fpus_internal, 300000000, 3.0, 2900, 0x601, 0x601, 0x0853, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27, 9, 9,   36},
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
};

/* Legacy CPU tables for backwards compatibility. */

static const cpu_legacy_table_t cpus_8088[] = {
    {"8088", 4772728, 1},
    {"8088", 7159092, 1},
    {"8088", 8000000, 1},
    {"8088", 10000000, 1},
    {"8088", 12000000, 1},
    {"8088", 16000000, 1},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_pcjr[] = {
    {"8088", 4772728, 1},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_europc[] = {
    {"8088_europc", 4772728, 1},
    {"8088_europc", 7159092, 1},
    {"8088_europc", 9545456, 1},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_8086[] = {
    {"8086", 7159092, 1},
    {"8086", 8000000, 1},
    {"8086", 9545456, 1},
    {"8086", 10000000, 1},
    {"8086", 12000000, 1},
    {"8086", 16000000, 1},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_pc1512[] = {
    {"8086", 8000000, 1},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_286[] = {
    {"286", 6000000, 1},
    {"286", 8000000, 1},
    {"286", 10000000, 1},
    {"286", 12500000, 1},
    {"286", 16000000, 1},
    {"286", 20000000, 1},
    {"286", 25000000, 1},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_ibmat[] = {
    {"286", 6000000, 1},
    {"286", 8000000, 1},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_ibmxt286[] = {
    {"286", 6000000, 1},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_ps1_m2011[] = {
    {"286", 10000000, 1},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_ps2_m30_286[] = {
    {"286", 10000000, 1},
    {"286", 12500000, 1},
    {"286", 16000000, 1},
    {"286", 20000000, 1},
    {"286", 25000000, 1},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_i386SX[] = {
    {"i386sx", 16000000, 1},
    {"i386sx", 20000000, 1},
    {"i386sx", 25000000, 1},
    {"i386sx", 33333333, 1},
    {"i386sx", 40000000, 1},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_i386DX[] = {
    {"i386dx", 16000000, 1},
    {"i386dx", 20000000, 1},
    {"i386dx", 25000000, 1},
    {"i386dx", 33333333, 1},
    {"i386dx", 40000000, 1},
    {"rapidcad", 25000000, 1},
    {"rapidcad", 33333333, 1},
    {"rapidcad", 40000000, 1},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_Am386SX[] = {
    {"am386sx", 16000000, 1},
    {"am386sx", 20000000, 1},
    {"am386sx", 25000000, 1},
    {"am386sx", 33333333, 1},
    {"am386sx", 40000000, 1},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_Am386DX[] = {
    {"am386dx", 25000000, 1},
    {"am386dx", 33333333, 1},
    {"am386dx", 40000000, 1},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_ALiM6117[] = {
    {"m6117", 33333333, 1},
    {"m6117", 40000000, 1},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_486SLC[] = {
    {"cx486slc", 20000000, 1},
    {"cx486slc", 25000000, 1},
    {"cx486slc", 33333333, 1},
    {"cx486srx2", 32000000, 2},
    {"cx486srx2", 40000000, 2},
    {"cx486srx2", 50000000, 2},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_IBM486SLC[] = {
    {"ibm486slc", 33333333, 1},
    {"ibm486slc2", 40000000, 2},
    {"ibm486slc2", 50000000, 2},
    {"ibm486slc2", 66666666, 2},
    {"ibm486slc3", 60000000, 3},
    {"ibm486slc3", 75000000, 3},
    {"ibm486slc3", 100000000, 3},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_IBM486BL[] = {
    {"ibm486bl2", 50000000, 2},
    {"ibm486bl2", 66666666, 2},
    {"ibm486bl3", 75000000, 3},
    {"ibm486bl3", 100000000, 3},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_486DLC[] = {
    {"cx486dlc", 25000000, 1},
    {"cx486dlc", 33333333, 1},
    {"cx486dlc", 40000000, 1},
    {"cx486drx2", 32000000, 2},
    {"cx486drx2", 40000000, 2},
    {"cx486drx2", 50000000, 2},
    {"cx486drx2", 66666666, 2},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_i486S1[] = {
    {"i486sx", 16000000, 1},
    {"i486sx", 20000000, 1},
    {"i486sx", 25000000, 1},
    {"i486sx", 33333333, 1},
    {"i486sx2", 50000000, 2},
    {"i486sx2", 66666666, 2},
    {"i486dx", 25000000, 1},
    {"i486dx", 33333333, 1},
    {"i486dx", 50000000, 1},
    {"i486dx2", 40000000, 2},
    {"i486dx2", 50000000, 2},
    {"i486dx2", 66666666, 2},
    {"idx4_od", 75000000, 3},
    {"idx4_od", 100000000, 3},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_Am486S1[] = {
    {"am486sx", 33333333, 1},
    {"am486sx", 40000000, 1},
    {"am486sx2", 50000000, 2},
    {"am486sx2", 66666666, 2},
    {"am486dx", 33333333, 1},
    {"am486dx", 40000000, 1},
    {"am486dx2", 50000000, 2},
    {"am486dx2", 66666666, 2},
    {"am486dx2", 80000000, 2},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_Cx486S1[] = {
    {"cx486s", 25000000, 1.0},
    {"cx486s", 33333333, 1.0},
    {"cx486s", 40000000, 1.0},
    {"cx486dx", 33333333, 1.0},
    {"cx486dx", 40000000, 1.0},
    {"cx486dx2", 50000000, 2.0},
    {"cx486dx2", 66666666, 2.0},
    {"cx486dx2", 80000000, 2.0},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_i486[] = {
    {"i486sx", 16000000, 1.0},
    {"i486sx", 20000000, 1.0},
    {"i486sx", 25000000, 1.0},
    {"i486sx", 33333333, 1.0},
    {"i486sx2", 50000000, 2.0},
    {"i486sx2", 66666666, 2.0},
    {"i486dx", 25000000, 1.0},
    {"i486dx", 33333333, 1.0},
    {"i486dx", 50000000, 1.0},
    {"i486dx2", 40000000, 2.0},
    {"i486dx2", 50000000, 2.0},
    {"i486dx2", 66666666, 2.0},
    {"idx4", 75000000, 3.0},
    {"idx4", 100000000, 3.0},
    {"idx4_od", 75000000, 3.0},
    {"idx4_od", 100000000, 3.0},
    {"pentium_p24t", 62500000, 2.5},
    {"pentium_p24t", 83333333, 2.5},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_i486_PC330[] = {
    {"i486dx2", 50000000, 2.0},
    {"i486dx2", 66666666, 2.0},
    {"idx4", 75000000, 3.0},
    {"idx4", 100000000, 3.0},
    {"pentium_p24t", 62500000, 2.5},
    {"pentium_p24t", 83333333, 2.5},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_Am486[] = {
    {"am486sx", 33333333, 1.0},
    {"am486sx", 40000000, 1.0},
    {"am486sx2", 50000000, 2.0},
    {"am486sx2", 66666666, 2.0},
    {"am486dx", 33333333, 1.0},
    {"am486dx", 40000000, 1.0},
    {"am486dx2", 50000000, 2.0},
    {"am486dx2", 66666666, 2.0},
    {"am486dx2", 80000000, 2.0},
    {"am486dx4", 75000000, 3.0},
    {"am486dx4", 90000000, 3.0},
    {"am486dx4", 100000000, 3.0},
    {"am486dx4", 120000000, 3.0},
    {"am5x86", 133333333, 4.0},
    {"am5x86", 150000000, 3.0},
    {"am5x86", 160000000, 4.0},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_Cx486[] = {
    {"cx486s", 25000000, 1.0},
    {"cx486s", 33333333, 1.0},
    {"cx486s", 40000000, 1.0},
    {"cx486dx", 33333333, 1.0},
    {"cx486dx", 40000000, 1.0},
    {"cx486dx2", 50000000, 2.0},
    {"cx486dx2", 66666666, 2.0},
    {"cx486dx2", 80000000, 2.0},
    {"cx486dx4", 75000000, 3.0},
    {"cx486dx4", 100000000, 3.0},
    {"cx5x86", 80000000, 2.0},
    {"cx5x86", 100000000, 3.0},
    {"cx5x86", 120000000, 3.0},
    {"cx5x86", 133333333, 4.0},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_STPCDX[] = {
    {"stpc_dx", 66666666, 1.0},
    {"stpc_dx", 75000000, 1.0},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_STPCDX2[] = {
    {"stpc_dx2", 133333333, 2.0},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_6x863V[] = {
    {"cx6x86", 80000000, 2.0},
    {"cx6x86", 100000000, 2.0},
    {"cx6x86", 110000000, 2.0},
    {"cx6x86", 120000000, 2.0},
    {"cx6x86", 133333333, 2.0},
    {"cx6x86", 150000000, 2.0},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_6x86[] = {
    {"cx6x86", 80000000, 2.0},
    {"cx6x86", 100000000, 2.0},
    {"cx6x86", 110000000, 2.0},
    {"cx6x86", 120000000, 2.0},
    {"cx6x86", 133333333, 2.0},
    {"cx6x86", 150000000, 2.0},
    {"cx6x86l", 110000000, 2.0},
    {"cx6x86l", 120000000, 2.0},
    {"cx6x86l", 133333333, 2.0},
    {"cx6x86l", 150000000, 2.0},
    {"cx6x86mx", 133333333, 2.0},
    {"cx6x86mx", 166666666, 2.5},
    {"cx6x86mx", 187500000, 2.5},
    {"cx6x86mx", 208333333, 2.5},
    {"mii", 233333333, 3.5},
    {"mii", 250000000, 3.0},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_6x86SS7[] = {
    {"cx6x86", 80000000, 2.0},
    {"cx6x86", 100000000, 2.0},
    {"cx6x86", 110000000, 2.0},
    {"cx6x86", 120000000, 2.0},
    {"cx6x86", 133333333, 2.0},
    {"cx6x86", 150000000, 2.0},
    {"cx6x86l", 110000000, 2.0},
    {"cx6x86l", 120000000, 2.0},
    {"cx6x86l", 133333333, 2.0},
    {"cx6x86l", 150000000, 2.0},
    {"cx6x86mx", 133333333, 2.0},
    {"cx6x86mx", 166666666, 2.5},
    {"cx6x86mx", 187500000, 2.5},
    {"cx6x86mx", 208333333, 2.5},
    {"mii", 233333333, 3.5},
    {"mii", 250000000, 3.0},
    {"mii", 250000000, 2.5},
    {"mii", 285000000, 3.0},
    {"mii", 300000000, 3.0},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_WinChip[] = {
    {"winchip", 75000000, 1.5},
    {"winchip", 90000000, 1.5},
    {"winchip", 100000000, 1.5},
    {"winchip", 120000000, 2.0},
    {"winchip", 133333333, 2.0},
    {"winchip", 150000000, 2.5},
    {"winchip", 166666666, 2.5},
    {"winchip", 180000000, 3.0},
    {"winchip", 200000000, 3.0},
    {"winchip", 225000000, 3.0},
    {"winchip", 240000000, 4.0},
    {"winchip2", 200000000, 3.0},
    {"winchip2", 225000000, 3.0},
    {"winchip2", 240000000, 4.0},
    {"winchip2", 250000000, 3.0},
    {"winchip2a", 200000000, 3.0},
    {"winchip2a", 233333333, 3.5},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_WinChip_SS7[] = {
    {"winchip", 75000000, 1.5},
    {"winchip", 90000000, 1.5},
    {"winchip", 100000000, 1.5},
    {"winchip", 120000000, 2.0},
    {"winchip", 133333333, 2.0},
    {"winchip", 150000000, 2.5},
    {"winchip", 166666666, 2.5},
    {"winchip", 180000000, 3.0},
    {"winchip", 200000000, 3.0},
    {"winchip", 225000000, 3.0},
    {"winchip", 240000000, 4.0},
    {"winchip2", 200000000, 3.0},
    {"winchip2", 225000000, 3.0},
    {"winchip2", 240000000, 4.0},
    {"winchip2", 250000000, 3.0},
    {"winchip2a", 200000000, 3.0},
    {"winchip2a", 233333333, 3.5},
    {"winchip2a", 233333333, 7.0},
    {"winchip2a", 250000000, 2.5},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_Pentium5V[] = {
    {"pentium_p5", 60000000, 1},
    {"pentium_p5", 66666666, 1},
    {"pentium_p54c_od5v", 120000000, 2},
    {"pentium_p54c_od5v", 133333333, 2},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_PentiumS5[] = {
    {"pentium_p54c", 75000000, 1.5},
    {"pentium_p55c_od", 75000000, 1.5},
    {"pentium_p54c", 90000000, 1.5},
    {"pentium_p54c", 100000000, 2.0},
    {"pentium_p54c", 100000000, 1.5},
    {"pentium_p54c", 120000000, 2.0},
    {"pentium_p54c", 133333333, 2.0},
    {"pentium_p54c_od3v", 125000000, 3.0},
    {"pentium_p54c_od3v", 150000000, 2.5},
    {"pentium_p54c_od3v", 166666666, 2.5},
    {"pentium_p55c_od", 125000000, 2.5},
    {"pentium_p55c_od", 150000000, 2.5},
    {"pentium_p55c_od", 166000000, 2.5},
    {"pentium_p55c_od", 180000000, 3.0},
    {"pentium_p55c_od", 200000000, 3.0},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_Pentium3V[] = {
    {"pentium_p54c", 75000000, 1.5},
    {"pentium_p55c_od", 75000000, 1.5},
    {"pentium_p54c", 90000000, 1.5},
    {"pentium_p54c", 100000000, 2.0},
    {"pentium_p54c", 100000000, 1.5},
    {"pentium_p54c", 120000000, 2.0},
    {"pentium_p54c", 133333333, 2.0},
    {"pentium_p54c", 150000000, 2.5},
    {"pentium_p54c", 166666666, 2.5},
    {"pentium_p54c", 200000000, 3.0},
    {"pentium_p54c_od3v", 125000000, 2.5},
    {"pentium_p54c_od3v", 150000000, 2.5},
    {"pentium_p54c_od3v", 166666666, 2.5},
    {"pentium_p55c_od", 125000000, 2.5},
    {"pentium_p55c_od", 150000000, 2.5},
    {"pentium_p55c_od", 166000000, 2.5},
    {"pentium_p55c_od", 180000000, 3.0},
    {"pentium_p55c_od", 200000000, 3.0},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_Pentium[] = {
    {"pentium_p54c", 75000000, 1.5},
    {"pentium_p55c_od", 75000000, 1.5},
    {"pentium_p54c", 90000000, 1.5},
    {"pentium_p54c", 100000000, 2.0},
    {"pentium_p54c", 100000000, 1.5},
    {"pentium_p54c", 120000000, 2.0},
    {"pentium_p54c", 133333333, 2.0},
    {"pentium_p54c", 150000000, 2.5},
    {"pentium_p54c", 166666666, 2.5},
    {"pentium_p54c", 200000000, 3.0},
    {"pentium_p55c", 166666666, 2.5},
    {"pentium_p55c", 200000000, 3.0},
    {"pentium_p55c", 233333333, 3.5},
    {"pentium_tillamook", 120000000, 2.0},
    {"pentium_tillamook", 133333333, 2.0},
    {"pentium_tillamook", 150000000, 2.5},
    {"pentium_tillamook", 166666666, 2.5},
    {"pentium_tillamook", 200000000, 3.0},
    {"pentium_tillamook", 233333333, 3.5},
    {"pentium_tillamook", 266666666, 4.0},
    {"pentium_tillamook", 300000000, 4.5},
    {"pentium_p54c_od3v", 125000000, 2.5},
    {"pentium_p54c_od3v", 150000000, 2.5},
    {"pentium_p54c_od3v", 166666666, 2.5},
    {"pentium_p55c_od", 125000000, 2.5},
    {"pentium_p55c_od", 150000000, 2.5},
    {"pentium_p55c_od", 166000000, 2.5},
    {"pentium_p55c_od", 180000000, 3.0},
    {"pentium_p55c_od", 200000000, 3.0},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_K5[] = {
    {"k5_5k86", 75000000, 1.5},
    {"k5_ssa5", 75000000, 1.5},
    {"k5_5k86", 90000000, 1.5},
    {"k5_ssa5", 90000000, 1.5},
    {"k5_5k86", 100000000, 1.5},
    {"k5_ssa5", 100000000, 1.5},
    {"k5_5k86", 120000000, 2.0},
    {"k5_5k86", 133333333, 2.0},
    {"k5_5k86", 150000000, 2.5},
    {"k5_5k86", 166666666, 2.5},
    {"k5_5k86", 200000000, 3.0},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_K56[] = {
    {"k6_m6", 66666666, 1.0},
    {"k6_m6", 100000000, 1.5},
    {"k6_m6", 133333333, 2.0},
    {"k6_m6", 166666666, 2.5},
    {"k6_m6", 200000000, 3.0},
    {"k6_m6", 233333333, 3.5},
    {"k6_m7", 100000000, 1.5},
    {"k6_m7", 133333333, 2.0},
    {"k6_m7", 166666666, 2.5},
	{"k6_m7", 200000000, 3.0},
    {"k6_m7", 233333333, 3.5},
    {"k6_m7", 266666666, 4.0},
    {"k6_m7", 300000000, 4.5},
    {"k6_2", 100000000, 1.5},
    {"k6_2", 133333333, 2.0},
    {"k6_2", 166666666, 2.5},
    {"k6_2", 200000000, 3.0},
	{"k6_2", 233333333, 3.5},
    {"k6_2", 266666666, 4.0},
    {"k6_2", 300000000, 4.5},
    {"k6_2", 366666666, 5.5},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_K56_SS7[] = {
    {"k6_m6", 66666666, 1.0},
    {"k6_m6", 100000000, 1.5},
    {"k6_m6", 133333333, 2.0},
	{"k6_m6", 166666666, 2.5},
    {"k6_m6", 200000000, 3.0},
    {"k6_m6", 233333333, 3.5},
    {"k6_m7", 100000000, 1.5},
    {"k6_m7", 133333333, 2.0},
    {"k6_m7", 166666666, 2.5},
	{"k6_m7", 200000000, 3.0},
    {"k6_m7", 233333333, 3.5},
    {"k6_m7", 266666666, 4.0},
    {"k6_m7", 300000000, 4.5},
    {"k6_2", 100000000, 1.5},
    {"k6_2", 133333333, 2.0},
    {"k6_2", 166666666, 2.5},
	{"k6_2", 200000000, 3.0},
	{"k6_2", 233333333, 3.5},
    {"k6_2", 266666666, 4.0},
    {"k6_2", 300000000, 3.0},
    {"k6_2", 332500000, 3.5},
    {"k6_2", 350000000, 3.5},
    {"k6_2", 366666666, 5.5},
    {"k6_2", 380000000, 4.0},
    {"k6_2", 400000000, 4.0},
    {"k6_2", 450000000, 4.5},
    {"k6_2", 475000000, 5.0},
    {"k6_2", 500000000, 5.0},
    {"k6_2", 533333333, 5.5},
    {"k6_2", 550000000, 5.5},
    {"k6_2p", 100000000, 1.5},
    {"k6_2p", 133333333, 2.0},
    {"k6_2p", 166666666, 2.5},
	{"k6_2p", 200000000, 3.0},
	{"k6_2p", 233333333, 3.5},
    {"k6_2p", 266666666, 4.0},
    {"k6_2p", 300000000, 3.0},
    {"k6_2p", 332500000, 3.5},
    {"k6_2p", 350000000, 3.5},
    {"k6_2p", 366666666, 5.5},
    {"k6_2p", 380000000, 4.0},
    {"k6_2p", 400000000, 4.0},
	{"k6_2p", 450000000, 4.5},
    {"k6_2p", 475000000, 5.0},
    {"k6_2p", 500000000, 5.0},
    {"k6_2p", 533333333, 5.5},
    {"k6_2p", 550000000, 5.5},
    {"k6_3", 100000000, 1.5},
    {"k6_3", 133333333, 2.0},
    {"k6_3", 166666666, 2.5},
	{"k6_3", 200000000, 3.0},
	{"k6_3", 233333333, 3.5},
    {"k6_3", 266666666, 4.0},
    {"k6_3", 300000000, 3.0},
    {"k6_3", 332500000, 3.5},
    {"k6_3", 350000000, 3.5},
    {"k6_3", 366666666, 5.5},
    {"k6_3", 380000000, 4.0},
	{"k6_3", 400000000, 4.0},
    {"k6_3", 450000000, 4.5},
    {"k6_3p", 75000000, 1.5},
	{"k6_3p", 100000000, 1.5},
    {"k6_3p", 133333333, 2.0},
    {"k6_3p", 166666666, 2.5},
	{"k6_3p", 200000000, 3.0},
	{"k6_3p", 233333333, 3.5},
    {"k6_3p", 266666666, 4.0},
    {"k6_3p", 300000000, 3.0},
    {"k6_3p", 332500000, 3.5},
    {"k6_3p", 350000000, 3.5},
    {"k6_3p", 366666666, 5.5},
    {"k6_3p", 380000000, 4.0},
	{"k6_3p", 400000000, 4.0},
    {"k6_3p", 450000000, 4.5},
    {"k6_3p", 475000000, 5.0},
    {"k6_3p", 500000000, 5.0},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_PentiumPro[] = {
    {"pentiumpro", 50000000, 1.0},
    {"pentiumpro", 60000000, 1.0},
    {"pentiumpro", 66666666, 1.0},
    {"pentiumpro", 75000000, 1.5},
    {"pentiumpro", 150000000, 2.5},
    {"pentiumpro", 166666666, 2.5},
    {"pentiumpro", 180000000, 3.0},
    {"pentiumpro", 200000000, 3.0},
    {"pentium2_od", 50000000, 1.0},
    {"pentium2_od", 60000000, 1.0},
    {"pentium2_od", 66666666, 1.0},
    {"pentium2_od", 75000000, 1.5},
    {"pentium2_od", 210000000, 3.5},
    {"pentium2_od", 233333333, 3.5},
    {"pentium2_od", 240000000, 4.0},
    {"pentium2_od", 266666666, 4.0},
    {"pentium2_od", 270000000, 4.5},
    {"pentium2_od", 300000000, 4.5},
    {"pentium2_od", 300000000, 5.0},
    {"pentium2_od", 333333333, 5.0},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_PentiumII66[] = {
    {"pentium2_klamath", 50000000, 1.0},
    {"pentium2_klamath", 60000000, 1.0},
    {"pentium2_klamath", 66666666, 1.0},
    {"pentium2_klamath", 75000000, 1.5},
    {"pentium2_klamath", 233333333, 3.5},
    {"pentium2_klamath", 266666666, 4.0},
    {"pentium2_klamath", 300000000, 4.5},
    {"pentium2_deschutes", 50000000, 1.0},
    {"pentium2_deschutes", 60000000, 1.0},
    {"pentium2_deschutes", 66666666, 1.0},
    {"pentium2_deschutes", 75000000, 1.5},
    {"pentium2_deschutes", 266666666, 4.0},
    {"pentium2_deschutes", 300000000, 4.5},
    {"pentium2_deschutes", 333333333, 5.0},
    {NULL, 0, 0}

};

static const cpu_legacy_table_t cpus_PentiumII[] = {
    {"pentium2_klamath", 50000000, 1.0},
    {"pentium2_klamath", 60000000, 1.0},
    {"pentium2_klamath", 66666666, 1.0},
    {"pentium2_klamath", 75000000, 1.5},
    {"pentium2_klamath", 233333333, 3.5},
    {"pentium2_klamath", 266666666, 4.0},
    {"pentium2_klamath", 300000000, 4.5},
    {"pentium2_deschutes", 50000000, 1.0},
    {"pentium2_deschutes", 60000000, 1.0},
    {"pentium2_deschutes", 66666666, 1.0},
    {"pentium2_deschutes", 75000000, 1.5},
    {"pentium2_deschutes", 266666666, 4.0},
    {"pentium2_deschutes", 300000000, 4.5},
    {"pentium2_deschutes", 333333333, 5.0},
    {"pentium2_deschutes", 350000000, 3.5},
    {"pentium2_deschutes", 400000000, 4.0},
    {"pentium2_deschutes", 450000000, 4.5},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_Xeon[] = {
    {"pentium2_xeon", 75000000, 1.5},
    {"pentium2_xeon", 100000000, 1.5},
    {"pentium2_xeon", 133333333, 2.0},
    {"pentium2_xeon", 166666666, 2.5},
    {"pentium2_xeon", 400000000, 4.0},
    {"pentium2_xeon", 450000000, 4.5},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_Celeron[] = {
    {"celeron_mendocino", 66666666, 1.0},
    {"celeron_mendocino", 100000000, 1.5},
    {"celeron_mendocino", 133333333, 2.0},
    {"celeron_mendocino", 166666666, 2.5},
    {"celeron_mendocino", 300000000, 4.5},
    {"celeron_mendocino", 333333333, 5.0},
    {"celeron_mendocino", 366666666, 5.5},
    {"celeron_mendocino", 400000000, 6.0},
    {"celeron_mendocino", 433333333, 6.5},
    {"celeron_mendocino", 466666666, 7.0},
    {"celeron_mendocino", 500000000, 7.5},
    {"celeron_mendocino", 533333333, 8.0},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_PentiumIID[] = {
    {"pentium2_deschutes", 50000000, 1.0},
    {"pentium2_deschutes", 60000000, 1.0},
    {"pentium2_deschutes", 66666666, 1.0},
    {"pentium2_deschutes", 75000000, 1.5},
    {"pentium2_deschutes", 266666666, 4.0},
    {"pentium2_deschutes", 300000000, 4.5},
    {"pentium2_deschutes", 333333333, 5.0},
    {"pentium2_deschutes", 350000000, 3.5},
    {"pentium2_deschutes", 400000000, 4.0},
    {"pentium2_deschutes", 450000000, 4.5},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t cpus_Cyrix3[] = {
    {"c3_samuel", 66666666, 1.0},
    {"c3_samuel", 233333333, 3.5},
    {"c3_samuel", 266666666, 4.0},
    {"c3_samuel", 300000000, 4.5},
    {"c3_samuel", 333333333, 5.0},
    {"c3_samuel", 350000000, 3.5},
    {"c3_samuel", 400000000, 4.0},
    {"c3_samuel", 450000000, 4.5},
    {"c3_samuel", 500000000, 5.0},
    {"c3_samuel", 550000000, 5.5},
    {"c3_samuel", 600000000, 6.0},
    {"c3_samuel", 650000000, 6.5},
    {"c3_samuel", 700000000, 7.0},
    {NULL, 0, 0}
};

static const cpu_legacy_table_t *cputables_8088[4] = {cpus_8088};
static const cpu_legacy_table_t *cputables_pcjr[4] = {cpus_pcjr};
static const cpu_legacy_table_t *cputables_europc[4] = {cpus_europc};
static const cpu_legacy_table_t *cputables_pc1512[4] = {cpus_pc1512};
static const cpu_legacy_table_t *cputables_8086[4] = {cpus_8086};
static const cpu_legacy_table_t *cputables_286[4] = {cpus_286};
static const cpu_legacy_table_t *cputables_ibmat[4] = {cpus_ibmat};
static const cpu_legacy_table_t *cputables_ps1_m2011[4] = {cpus_ps1_m2011};
static const cpu_legacy_table_t *cputables_ps2_m30_286_IBM486SLC[4] = {cpus_ps2_m30_286, cpus_IBM486SLC};
static const cpu_legacy_table_t *cputables_ibmxt286[4] = {cpus_ibmxt286};
static const cpu_legacy_table_t *cputables_i386SX_Am386SX_486SLC[4] = {cpus_i386SX, cpus_Am386SX, cpus_486SLC};
static const cpu_legacy_table_t *cputables_ALiM6117[4] = {cpus_ALiM6117};
static const cpu_legacy_table_t *cputables_i386SX_Am386SX_486SLC_IBM486SLC[4] = {cpus_i386SX, cpus_Am386SX, cpus_486SLC, cpus_IBM486SLC};
static const cpu_legacy_table_t *cputables_i386DX_Am386DX_486DLC[4] = {cpus_i386DX, cpus_Am386DX, cpus_486DLC};
static const cpu_legacy_table_t *cputables_i386DX_Am386DX_486DLC_IBM486BL[4] = {cpus_i386DX, cpus_Am386DX, cpus_486DLC, cpus_IBM486BL};
static const cpu_legacy_table_t *cputables_i486_Am486_Cx486[4] = {cpus_i486, cpus_Am486, cpus_Cx486};
static const cpu_legacy_table_t *cputables_i486S1_Am486S1_Cx486S1[4] = {cpus_i486S1, cpus_Am486S1, cpus_Cx486S1};
static const cpu_legacy_table_t *cputables_IBM486SLC[4] = {cpus_IBM486SLC};
static const cpu_legacy_table_t *cputables_i486_PC330[4] = {cpus_i486_PC330};
static const cpu_legacy_table_t *cputables_STPCDX[4] = {cpus_STPCDX};
static const cpu_legacy_table_t *cputables_STPCDX2[4] = {cpus_STPCDX2};
static const cpu_legacy_table_t *cputables_Pentium5V[4] = {cpus_Pentium5V};
static const cpu_legacy_table_t *cputables_PentiumS5_WinChip_K5[4] = {cpus_PentiumS5, cpus_WinChip, cpus_K5};
static const cpu_legacy_table_t *cputables_Pentium3V_WinChip_K5_6x863V[4] = {cpus_Pentium3V, cpus_WinChip, cpus_K5, cpus_6x863V};
static const cpu_legacy_table_t *cputables_Pentium3V_K5[4] = {cpus_Pentium3V, cpus_K5};
static const cpu_legacy_table_t *cputables_Pentium_WinChip_K56_6x86[4] = {cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86};
static const cpu_legacy_table_t *cputables_Pentium_WinChip_SS7_K56_SS7_6x86SS7[4] = {cpus_Pentium, cpus_WinChip_SS7, cpus_K56_SS7, cpus_6x86SS7};
static const cpu_legacy_table_t *cputables_PentiumPro[4] = {cpus_PentiumPro};
static const cpu_legacy_table_t *cputables_PentiumII66[4] = {cpus_PentiumII66};
static const cpu_legacy_table_t *cputables_PentiumII_Celeron_Cyrix3[4] = {cpus_PentiumII, cpus_Celeron, cpus_Cyrix3};
static const cpu_legacy_table_t *cputables_Xeon[4] = {cpus_Xeon};
static const cpu_legacy_table_t *cputables_Celeron_Cyrix3[4] = {cpus_Celeron, cpus_Cyrix3};
static const cpu_legacy_table_t *cputables_Celeron[4] = {cpus_Celeron};
static const cpu_legacy_table_t *cputables_PentiumIID_Celeron[4] = {cpus_PentiumIID, cpus_Celeron};

const cpu_legacy_machine_t cpu_legacy_table[] = {
    {"ibmpc",                   cputables_8088},
    {"ibmpc82",                 cputables_8088},
    {"ibmpcjr",                 cputables_pcjr},
    {"ibmxt",                   cputables_8088},
    {"ibmxt86",                 cputables_8088},
    {"americxt",                cputables_8088},
    {"amixt",                   cputables_8088},
    {"portable",                cputables_8088},
    {"dtk",                     cputables_8088},
    {"genxt",                   cputables_8088},
    {"jukopc",                  cputables_8088},
    {"openxt",                  cputables_8088},
    {"pxxt",                    cputables_8088},
    {"europc",                  cputables_europc},
    {"tandy",                   cputables_europc},
    {"tandy1000hx",             cputables_europc},
    {"t1000",                   cputables_8088},
    {"ltxt",                    cputables_8088},
    {"xi8088",                  cputables_8088},
    {"zdsupers",                cputables_8088},
    {"pc1512",                  cputables_pc1512},
    {"pc1640",                  cputables_8086},
    {"pc2086",                  cputables_8086},
    {"pc3086",                  cputables_8086},
    {"pc200",                   cputables_8086},
    {"ppc512",                  cputables_8086},
    {"deskpro",                 cputables_8086},
    {"m24",                     cputables_8086},
    {"iskra3104",               cputables_8086},
    {"tandy1000sl2",            cputables_8086},
    {"t1200",                   cputables_8086},
    {"lxt3",                    cputables_8086},
    {"hed919",                  cputables_286},
    {"ibmat",                   cputables_ibmat},
    {"ibmps1es",                cputables_ps1_m2011},
    {"ibmps2_m30_286",          cputables_ps2_m30_286_IBM486SLC},
    {"ibmxt286",                cputables_ibmxt286},
    {"ibmatami",                cputables_ibmat},
    {"cmdpc30",                 cputables_286},
    {"portableii",              cputables_286},
    {"portableiii",             cputables_286},
    {"mr286",                   cputables_286},
    {"open_at",                 cputables_286},
    {"ibmatpx",                 cputables_ibmat},
    {"ibmatquadtel",            cputables_ibmat},
    {"siemens",                 cputables_286},
    {"t3100e",                  cputables_286},
    {"quadt286",                cputables_286},
    {"tg286m",                  cputables_286},
    {"ami286",                  cputables_286},
    {"px286",                   cputables_286},
    {"award286",                cputables_286},
    {"gw286ct",                 cputables_286},
    {"gdc212m",                 cputables_286},
    {"super286tr",              cputables_286},
    {"spc4200p",                cputables_286},
    {"spc4216p",                cputables_286},
    {"deskmaster286",           cputables_286},
    {"ibmps2_m50",              cputables_ps2_m30_286_IBM486SLC},
    {"ibmps1_2121",             cputables_i386SX_Am386SX_486SLC},
    {"ibmps1_2121_isa",         cputables_i386SX_Am386SX_486SLC},
    {"arb1375",                 cputables_ALiM6117},
    {"pja511m",                 cputables_ALiM6117},
    {"ama932j",                 cputables_i386SX_Am386SX_486SLC},
    {"adi386sx",                cputables_i386SX_Am386SX_486SLC},
    {"shuttle386sx",            cputables_i386SX_Am386SX_486SLC},
    {"dtk386",                  cputables_i386SX_Am386SX_486SLC},
    {"awardsx",                 cputables_i386SX_Am386SX_486SLC},
    {"cmdsl386sx25",            cputables_i386SX_Am386SX_486SLC},
    {"kmxc02",                  cputables_i386SX_Am386SX_486SLC},
    {"megapc",                  cputables_i386SX_Am386SX_486SLC},
    {"ibmps2_m55sx",            cputables_i386SX_Am386SX_486SLC_IBM486SLC},
    {"acc386",                  cputables_i386DX_Am386DX_486DLC},
    {"ecs386",                  cputables_i386DX_Am386DX_486DLC},
    {"portableiii386",          cputables_i386DX_Am386DX_486DLC},
    {"micronics386",            cputables_i386DX_Am386DX_486DLC},
    {"asus386",                 cputables_i386DX_Am386DX_486DLC},
    {"ustechnologies386",       cputables_i386DX_Am386DX_486DLC},
    {"award386dx",              cputables_i386DX_Am386DX_486DLC},
    {"ibmps2_m70_type3",        cputables_i386DX_Am386DX_486DLC_IBM486BL},
    {"ibmps2_m80",              cputables_i386DX_Am386DX_486DLC_IBM486BL},
    {"pb410a",                  cputables_i486_Am486_Cx486},
    {"acera1g",                 cputables_i486_Am486_Cx486},
    {"win486",                  cputables_i486_Am486_Cx486},
    {"ali1429",                 cputables_i486S1_Am486S1_Cx486S1},
    {"cs4031",                  cputables_i486S1_Am486S1_Cx486S1},
    {"rycleopardlx",            cputables_IBM486SLC},
    {"award486",                cputables_i486S1_Am486S1_Cx486S1},
    {"ami486",                  cputables_i486S1_Am486S1_Cx486S1},
    {"mr486",                   cputables_i486_Am486_Cx486},
    {"pc330_6571",              cputables_i486_PC330},
    {"403tg",                   cputables_i486_Am486_Cx486},
    {"sis401",                  cputables_i486_Am486_Cx486},
    {"valuepoint433",           cputables_i486_Am486_Cx486},
    {"ami471",                  cputables_i486_Am486_Cx486},
    {"win471",                  cputables_i486_Am486_Cx486},
    {"vi15g",                   cputables_i486_Am486_Cx486},
    {"vli486sv2g",              cputables_i486_Am486_Cx486},
    {"dtk486",                  cputables_i486_Am486_Cx486},
    {"px471",                   cputables_i486_Am486_Cx486},
    {"486vchd",                 cputables_i486S1_Am486S1_Cx486S1},
    {"ibmps1_2133",             cputables_i486S1_Am486S1_Cx486S1},
    {"vect486vl",               cputables_i486S1_Am486S1_Cx486S1},
    {"ibmps2_m70_type4",        cputables_i486S1_Am486S1_Cx486S1},
    {"abpb4",                   cputables_i486_Am486_Cx486},
    {"486ap4",                  cputables_i486_Am486_Cx486},
    {"486sp3g",                 cputables_i486_Am486_Cx486},
    {"alfredo",                 cputables_i486_Am486_Cx486},
    {"ls486e",                  cputables_i486_Am486_Cx486},
    {"m4li",                    cputables_i486_Am486_Cx486},
    {"r418",                    cputables_i486_Am486_Cx486},
    {"4sa2",                    cputables_i486_Am486_Cx486},
    {"4dps",                    cputables_i486_Am486_Cx486},
    {"itoxstar",                cputables_STPCDX},
    {"arb1479",                 cputables_STPCDX2},
    {"pcm9340",                 cputables_STPCDX2},
    {"pcm5330",                 cputables_STPCDX2},
    {"486vipio2",               cputables_i486_Am486_Cx486},
    {"p5mp3",                   cputables_Pentium5V},
    {"dellxp60",                cputables_Pentium5V},
    {"opti560l",                cputables_Pentium5V},
    {"ambradp60",               cputables_Pentium5V},
    {"valuepointp60",           cputables_Pentium5V},
    {"revenge",                 cputables_Pentium5V},
    {"586mc1",                  cputables_Pentium5V},
    {"pb520r",                  cputables_Pentium5V},
    {"excalibur",               cputables_Pentium5V},
    {"plato",                   cputables_PentiumS5_WinChip_K5},
    {"ambradp90",               cputables_PentiumS5_WinChip_K5},
    {"430nx",                   cputables_PentiumS5_WinChip_K5},
    {"acerv30",                 cputables_PentiumS5_WinChip_K5},
    {"apollo",                  cputables_PentiumS5_WinChip_K5},
    {"vectra54",                cputables_PentiumS5_WinChip_K5},
    {"zappa",                   cputables_PentiumS5_WinChip_K5},
    {"powermate_v",             cputables_PentiumS5_WinChip_K5},
    {"mb500n",                  cputables_PentiumS5_WinChip_K5},
    {"p54tp4xe",                cputables_Pentium3V_WinChip_K5_6x863V},
    {"mr586",                   cputables_Pentium3V_WinChip_K5_6x863V},
    {"gw2katx",                 cputables_Pentium3V_WinChip_K5_6x863V},
    {"thor",                    cputables_Pentium3V_WinChip_K5_6x863V},
    {"mrthor",                  cputables_Pentium3V_WinChip_K5_6x863V},
    {"endeavor",                cputables_Pentium3V_WinChip_K5_6x863V},
    {"pb640",                   cputables_Pentium3V_WinChip_K5_6x863V},
    {"chariot",                 cputables_Pentium3V_K5},
    {"acerm3a",                 cputables_Pentium3V_WinChip_K5_6x863V},
    {"ap53",                    cputables_Pentium3V_WinChip_K5_6x863V},
    {"8500tuc",                 cputables_Pentium3V_WinChip_K5_6x863V},
    {"p55t2s",                  cputables_Pentium3V_WinChip_K5_6x863V},
    {"acerv35n",                cputables_Pentium_WinChip_K56_6x86},
    {"p55t2p4",                 cputables_Pentium_WinChip_K56_6x86},
    {"m7shi",                   cputables_Pentium_WinChip_K56_6x86},
    {"tc430hx",                 cputables_Pentium_WinChip_K56_6x86},
    {"equium5200",              cputables_Pentium_WinChip_K56_6x86},
    {"pcv240",                  cputables_Pentium_WinChip_K56_6x86},
    {"p65up5_cp55t2d",          cputables_Pentium_WinChip_K56_6x86},
    {"p55tvp4",                 cputables_Pentium_WinChip_K56_6x86},
    {"8500tvxa",                cputables_Pentium_WinChip_K56_6x86},
    {"presario4500",            cputables_Pentium_WinChip_K56_6x86},
    {"p55va",                   cputables_Pentium_WinChip_K56_6x86},
    {"gw2kte",                  cputables_Pentium_WinChip_K56_6x86},
    {"brio80xx",                cputables_Pentium_WinChip_K56_6x86},
    {"pb680",                   cputables_Pentium_WinChip_K56_6x86},
    {"430vx",                   cputables_Pentium_WinChip_K56_6x86},
    {"nupro592",                cputables_Pentium_WinChip_K56_6x86},
    {"tx97",                    cputables_Pentium_WinChip_K56_6x86},
    {"an430tx",                 cputables_Pentium_WinChip_K56_6x86},
    {"ym430tx",                 cputables_Pentium_WinChip_K56_6x86},
    {"mb540n",                  cputables_Pentium_WinChip_K56_6x86},
    {"p5mms98",                 cputables_Pentium_WinChip_K56_6x86},
    {"ficva502",                cputables_Pentium_WinChip_K56_6x86},
    {"ficpa2012",               cputables_Pentium_WinChip_K56_6x86},
    {"ax59pro",                 cputables_Pentium_WinChip_SS7_K56_SS7_6x86SS7},
    {"ficva503p",               cputables_Pentium_WinChip_SS7_K56_SS7_6x86SS7},
    {"ficva503a",               cputables_Pentium_WinChip_SS7_K56_SS7_6x86SS7},
    {"v60n",                    cputables_PentiumPro},
    {"p65up5_cp6nd",            cputables_PentiumPro},
    {"8600ttc",                 cputables_PentiumPro},
    {"686nx",                   cputables_PentiumPro},
    {"ap440fx",                 cputables_PentiumPro},
    {"vs440fx",                 cputables_PentiumPro},
    {"m6mi",                    cputables_PentiumPro},
    {"mb600n",                  cputables_PentiumPro},
    {"p65up5_cpknd",            cputables_PentiumII66},
    {"kn97",                    cputables_PentiumII66},
    {"lx6",                     cputables_PentiumII66},
    {"spitfire",                cputables_PentiumII66},
    {"p6i440e2",                cputables_PentiumII66},
    {"p2bls",                   cputables_PentiumII_Celeron_Cyrix3},
    {"p3bf",                    cputables_PentiumII_Celeron_Cyrix3},
    {"bf6",                     cputables_PentiumII_Celeron_Cyrix3},
    {"ax6bc",                   cputables_PentiumII_Celeron_Cyrix3},
    {"atc6310bxii",             cputables_PentiumII_Celeron_Cyrix3},
    {"686bx",                   cputables_PentiumII_Celeron_Cyrix3},
    {"tsunamiatx",              cputables_PentiumII_Celeron_Cyrix3},
    {"p6sba",                   cputables_PentiumII_Celeron_Cyrix3},
    {"ergox365",                cputables_PentiumII_Celeron_Cyrix3},
    {"ficka6130",               cputables_PentiumII_Celeron_Cyrix3},
    {"6gxu",                    cputables_Xeon},
    {"fw6400gx",                cputables_Xeon},
    {"s2dge",                   cputables_Xeon},
    {"s370slm",                 cputables_Celeron_Cyrix3},
    {"awo671r",                 cputables_Celeron_Cyrix3},
    {"cubx",                    cputables_Celeron_Cyrix3},
    {"atc7020bxii",             cputables_Celeron_Cyrix3},
    {"ambx133",                 cputables_Celeron_Cyrix3},
    {"trinity371",              cputables_Celeron},
    {"63a",                     cputables_Celeron_Cyrix3},
    {"apas3",                   cputables_Celeron_Cyrix3},
    {"wcf681",                  cputables_Celeron_Cyrix3},
    {"6via90ap",                cputables_Celeron_Cyrix3},
    {"p6bap",                   cputables_Celeron_Cyrix3},
    {"603tcf",                  cputables_Celeron_Cyrix3},
    {"vpc2007",                 cputables_PentiumIID_Celeron},
    {NULL, NULL}
};
