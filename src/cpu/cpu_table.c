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
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 leilei.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 *		Copyright 2020 RichardG.
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


cpu_family_t cpu_families[] = {
    {
	.package = CPU_PKG_8088, /* pcjr = only 4.77 */
	.manufacturer = "Intel",
	.name = "8088",
	.internal_name = "8088",
	.cpus = {
		{"4.77",    CPU_8088, fpus_8088,  4772728,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"7.16",    CPU_8088, fpus_8088,  7159092,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"8",       CPU_8088, fpus_8088,  8000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
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
	.cpus = {
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
	.cpus = {
		{"7.16",    CPU_8086, fpus_8088,   7159092,    1, 5000, 0, 0, 0, CPU_ALTERNATE_XTAL, 0,0,0,0, 1},
		{"8",       CPU_8086, fpus_8088,   8000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"9.54",    CPU_8086, fpus_8088,   9545456,    1, 5000, 0, 0, 0, CPU_ALTERNATE_XTAL, 0,0,0,0, 1},
		{"10",      CPU_8086, fpus_8088,  10000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"12",      CPU_8086, fpus_8088,  12000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 1},
		{"16",      CPU_8086, fpus_8088,  16000000,    1, 5000, 0, 0, 0, 0, 0,0,0,0, 2},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_286,
	.manufacturer = "Intel",
	.name = "80286",
	.internal_name = "286",
	.cpus = {
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
	.cpus = {
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
	.cpus = {
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
	.cpus = {
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
	.cpus = {
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
	.cpus = {
		{"25",   CPU_386DX, fpus_80386,     25000000, 1, 5000, 0x0308, 0, 0, 0, 4,4,3,3, 3},
		{"33",   CPU_386DX, fpus_80386,     33333333, 1, 5000, 0x0308, 0, 0, 0, 6,6,3,3, 4},
		{"40",   CPU_386DX, fpus_80386,     40000000, 1, 5000, 0x0308, 0, 0, 0, 7,7,3,3, 5},
		{"", 0}
	}
    },
#if defined(DEV_BRANCH) && defined(USE_M6117)
    {
	.package = CPU_PKG_M6117,
	.manufacturer = "ALi",
	.name = "M6117",
	.internal_name = "m6117",
	.cpus = { /* All timings and edx_reset values assumed. */
		{"33",    CPU_386DX,      fpus_80386,  33333333, 1, 5000, 0x2308, 0, 0, 0, 6,6,3,3, 4},
		{"40",    CPU_386DX,      fpus_80386,  40000000, 1, 5000, 0x2308, 0, 0, 0, 7,7,3,3, 5},
		{"", 0}
	}
    },
#endif
    {
	.package = CPU_PKG_386SLC_IBM,
	.manufacturer = "IBM",
	.name = "386SLC",
	.internal_name = "ibm386slc",
	.cpus = {
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
	.cpus = {
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
	.cpus = {
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
	.cpus = {
		{"33",   CPU_IBM486SLC, fpus_80386, 33333333,  1, 5000, 0xA401, 0, 0, 0, 6,6,3,3,    4},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_486SLC_IBM,
	.manufacturer = "IBM",
	.name = "486SLC2",
	.internal_name = "ibm486slc2",
	.cpus = {
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
	.cpus = {
		{"60",  CPU_IBM486SLC, fpus_80386, 60000000,  3, 5000, 0xA439, 0, 0, 0, 12,12,9,9,  7},
		{"75",  CPU_IBM486SLC, fpus_80386, 75000000,  3, 5000, 0xA439, 0, 0, 0, 12,12,9,9,  9},
		{"100", CPU_IBM486SLC, fpus_80386, 100000000, 3, 5000, 0xA439, 0, 0, 0, 18,18,9,9, 12},	
		{"", 0}
	}
    }, {
	.package = CPU_PKG_486BL,
	.manufacturer = "IBM",
	.name = "486BL2",
	.internal_name = "486bl2",
	.cpus = {
		{"50",  CPU_IBM486BL, fpus_80386, 50000000,  2, 5000, 0xA439, 0, 0, 0, 8,8,6,6,    6},
		{"66",  CPU_IBM486BL, fpus_80386, 66666666,  2, 5000, 0xA439, 0, 0, 0, 12,12,6,6,  8},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_486BL,
	.manufacturer = "IBM",
	.name = "486BL3",
	.internal_name = "486bl3",
	.cpus = {
		{"75",  CPU_IBM486BL, fpus_80386, 75000000,  3, 5000, 0xA439, 0, 0, 0, 12,12,9,9,  9},
		{"100", CPU_IBM486BL, fpus_80386, 100000000, 3, 5000, 0xA439, 0, 0, 0, 18,18,9,9, 12},	
		{"", 0}
	}
    }, {
	.package = CPU_PKG_386DX,
	.manufacturer = "Cyrix",
	.name = "Cx486DLC",
	.internal_name = "cx486dlc",
	.cpus = {
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
	.cpus = {
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
	.cpus = {
		{"16",            CPU_i486SX,     fpus_486sx,  16000000, 1,  5000, 0x420,        0, 0, CPU_SUPPORTS_DYNAREC,  3, 3,3,3,  2},
		{"20",            CPU_i486SX,     fpus_486sx,  20000000, 1,  5000, 0x420,        0, 0, CPU_SUPPORTS_DYNAREC,  4, 4,3,3,  3},
		{"25",            CPU_i486SX,     fpus_486sx,  25000000, 1,  5000, 0x422,        0, 0, CPU_SUPPORTS_DYNAREC,  4, 4,3,3,  3},
		{"33",            CPU_i486SX,     fpus_486sx,  33333333, 1,  5000, 0x42a,        0, 0, CPU_SUPPORTS_DYNAREC,  6, 6,3,3,  4},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET1,
	.manufacturer = "Intel",
	.name = "i486SX2",
	.internal_name = "i486sx2",
	.cpus = {
		{"50",           CPU_i486SX2,    fpus_486sx,  50000000, 2,  5000, 0x45b,        0, 0, CPU_SUPPORTS_DYNAREC,  8, 8,6,6,  6},
		{"66 (Q0569)",   CPU_i486SX2,    fpus_486sx,  66666666, 2,  5000, 0x45b,        0, 0, CPU_SUPPORTS_DYNAREC,  8, 8,6,6,  8},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET1,
	.manufacturer = "Intel",
	.name = "i486DX",
	.internal_name = "i486dx",
	.cpus = {
		{"25",            CPU_i486DX,  fpus_internal,  25000000, 1,  5000, 0x404,        0, 0, CPU_SUPPORTS_DYNAREC,  4, 4,3,3,  3},
		{"33",            CPU_i486DX,  fpus_internal,  33333333, 1,  5000, 0x414,        0, 0, CPU_SUPPORTS_DYNAREC,  6, 6,3,3,  4},
		{"50",            CPU_i486DX,  fpus_internal,  50000000, 1,  5000, 0x411,        0, 0, CPU_SUPPORTS_DYNAREC,  8, 8,4,4,  6},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET1,
	.manufacturer = "Intel",
	.name = "i486DX2",
	.internal_name = "i486dx2",
	.cpus = {
		{"40",           CPU_i486DX2, fpus_internal,  40000000, 2,  5000, 0x430,    0x430, 0, CPU_SUPPORTS_DYNAREC,  7, 7,6,6,  5},
		{"50",           CPU_i486DX2, fpus_internal,  50000000, 2,  5000, 0x433,    0x433, 0, CPU_SUPPORTS_DYNAREC,  8, 8,6,6,  6},
		{"66",           CPU_i486DX2, fpus_internal,  66666666, 2,  5000, 0x435,    0x435, 0, CPU_SUPPORTS_DYNAREC, 12,12,6,6,  8},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET3,
	.manufacturer = "Intel",
	.name = "iDX4",
	.internal_name = "idx4",
	.cpus = {
		{"75",              CPU_iDX4,    fpus_internal,  75000000, 3.0,  5000,  0x480,  0x480, 0x0000, CPU_SUPPORTS_DYNAREC, 12,12, 9, 9,  9}, /*CPUID available on DX4, >= 75 MHz*/
		{"100",             CPU_iDX4,    fpus_internal, 100000000, 3.0,  5000,  0x483,  0x483, 0x0000, CPU_SUPPORTS_DYNAREC, 18,18, 9, 9, 12}, /*Is on some real Intel DX2s, limit here is pretty arbitary*/
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET1,
	.manufacturer = "Intel",
	.name = "iDX4 OverDrive",
	.internal_name = "idx4_od",
	.cpus = {
		{"75",    CPU_iDX4,    fpus_internal,  75000000, 3,  5000, 0x1480,  0x1480, 0, CPU_SUPPORTS_DYNAREC, 12,12,9,9,  9}, /*Only added the DX4 OverDrive as the others would be redundant*/
		{"100",   CPU_iDX4,    fpus_internal, 100000000, 3,  5000, 0x1480,  0x1480, 0, CPU_SUPPORTS_DYNAREC, 18,18,9,9, 12}, 
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET3, /* PC330 = only DX2/50, DX2/66, DX4/75, DX4/100, POD63 and POD83 */
	.manufacturer = "Intel",
	.name = "Pentium OverDrive",
	.internal_name = "pentium_od_s3",
	.cpus = {
		{"63", CPU_P24T,    fpus_internal,  62500000, 2.5, 5000, 0x1531, 0x1531, 0x0000, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10,7,7, 15/2},
		{"83", CPU_P24T,    fpus_internal,  83333333, 2.5, 5000, 0x1532, 0x1532, 0x0000, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15,8,8, 10},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET1,
	.manufacturer = "AMD",
	.name = "Am486SX",
	.internal_name = "am486sx",
	.cpus = {
		{"33",   CPU_Am486SX,     fpus_486sx, 33333333, 1, 5000, 0x42a,     0, 0, CPU_SUPPORTS_DYNAREC,  6, 6, 3, 3, 4},
		{"40",   CPU_Am486SX,     fpus_486sx, 40000000, 1, 5000, 0x42a,     0, 0, CPU_SUPPORTS_DYNAREC,  7, 7, 3, 3, 5}, 
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET1,
	.manufacturer = "AMD",
	.name = "Am486SX2",
	.internal_name = "am486sx2",
	.cpus = {
		{"50",  CPU_Am486SX2,    fpus_486sx, 50000000, 2, 5000, 0x45b, 0x45b, 0, CPU_SUPPORTS_DYNAREC,  8, 8, 6, 6, 6}, /*CPUID available on SX2, DX2, DX4, 5x86, >= 50 MHz*/
		{"66",  CPU_Am486SX2,    fpus_486sx, 66666666, 2, 5000, 0x45b, 0x45b, 0, CPU_SUPPORTS_DYNAREC, 12,12, 6, 6, 8}, /*Isn't on all real AMD SX2s and DX2s, availability here is pretty arbitary (and distinguishes them from the Intel chips)*/
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET1,
	.manufacturer = "AMD",
	.name = "Am486DX",
	.internal_name = "am486dx",
	.cpus = {
		{"33",   CPU_Am486DX,  fpus_internal, 33333333, 1, 5000, 0x430,     0, 0, CPU_SUPPORTS_DYNAREC,  6, 6, 3, 3, 4},
		{"40",   CPU_Am486DX,  fpus_internal, 40000000, 1, 5000, 0x430,     0, 0, CPU_SUPPORTS_DYNAREC,  7, 7, 3, 3, 5},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET1,
	.manufacturer = "AMD",
	.name = "Am486DX2",
	.internal_name = "am486dx2",
	.cpus = {
		{"50",  CPU_Am486DX2, fpus_internal, 50000000, 2, 5000, 0x470, 0x470, 0, CPU_SUPPORTS_DYNAREC,  8, 8, 6, 6, 6},
		{"66",  CPU_Am486DX2, fpus_internal, 66666666, 2, 5000, 0x470, 0x470, 0, CPU_SUPPORTS_DYNAREC, 12,12, 6, 6, 8},
		{"80",  CPU_Am486DX2, fpus_internal, 80000000, 2, 5000, 0x470, 0x470, 0, CPU_SUPPORTS_DYNAREC, 14,14, 6, 6, 10},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET3,
	.manufacturer = "AMD",
	.name = "Am486DX4",
	.internal_name = "am486dx4",
	.cpus = {
		{"75",  CPU_Am486DX4, fpus_internal,   75000000, 3.0, 5000, 0x482, 0x482, 0, CPU_SUPPORTS_DYNAREC, 12,12, 9, 9, 9},
		{"90",  CPU_Am486DX4, fpus_internal,   90000000, 3.0, 5000, 0x482, 0x482, 0, CPU_SUPPORTS_DYNAREC, 15,15, 9, 9, 12},
		{"100", CPU_Am486DX4, fpus_internal,  100000000, 3.0, 5000, 0x482, 0x482, 0, CPU_SUPPORTS_DYNAREC, 15,15, 9, 9, 12},
		{"120", CPU_Am486DX4, fpus_internal,  120000000, 3.0, 5000, 0x482, 0x482, 0, CPU_SUPPORTS_DYNAREC, 21,21, 9, 9, 15},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET3,
	.manufacturer = "AMD",
	.name = "Am5x86",
	.internal_name = "am5x86",
	.cpus = {
		{"P75",   CPU_Am5x86,   fpus_internal,  133333333, 4.0, 5000, 0x4e0, 0x4e0, 0, CPU_SUPPORTS_DYNAREC, 24,24,12,12, 16},
		{"P75+",  CPU_Am5x86,   fpus_internal,  150000000, 3.0, 5000, 0x482, 0x482, 0, CPU_SUPPORTS_DYNAREC, 28,28,12,12, 20},/*The rare P75+ was indeed a triple-clocked 150 MHz according to research*/
		{"P90",   CPU_Am5x86,   fpus_internal,  160000000, 4.0, 5000, 0x4e0, 0x4e0, 0, CPU_SUPPORTS_DYNAREC, 28,28,12,12, 20},/*160 MHz on a 40 MHz bus was a common overclock and "5x86/P90" was used by a number of BIOSes to refer to that configuration*/ 
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET1,
	.manufacturer = "Cyrix",
	.name = "Cx486S",
	.internal_name = "cx486s",
	.cpus = {
		{"25",            CPU_Cx486S,    fpus_486sx,   25000000, 1.0, 5000,  0x420,      0, 0x0010, CPU_SUPPORTS_DYNAREC,  4, 4, 3, 3,  3},
		{"33",            CPU_Cx486S,    fpus_486sx,   33333333, 1.0, 5000,  0x420,      0, 0x0010, CPU_SUPPORTS_DYNAREC,  6, 6, 3, 3,  4},
		{"40",            CPU_Cx486S,    fpus_486sx,   40000000, 1.0, 5000,  0x420,      0, 0x0010, CPU_SUPPORTS_DYNAREC,  7, 7, 3, 3,  5},
	}
    }, {
	.package = CPU_PKG_SOCKET1,
	.manufacturer = "Cyrix",
	.name = "Cx486DX",
	.internal_name = "cx486dx",
	.cpus = {
		{"33",          CPU_Cx486DX, fpus_internal,   33333333, 1.0, 5000,  0x430,      0, 0x051a, CPU_SUPPORTS_DYNAREC,  6, 6, 3, 3,  4},
		{"40",          CPU_Cx486DX, fpus_internal,   40000000, 1.0, 5000,  0x430,      0, 0x051a, CPU_SUPPORTS_DYNAREC,  7, 7, 3, 3,  5},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET1,
	.manufacturer = "Cyrix",
	.name = "Cx486DX2",
	.internal_name = "cx486dx2",
	.cpus = {
		{"50",        CPU_Cx486DX2, fpus_internal,   50000000, 2.0, 5000,  0x430,      0, 0x081b, CPU_SUPPORTS_DYNAREC,  8, 8, 6, 6,  6},
		{"66",        CPU_Cx486DX2, fpus_internal,   66666666, 2.0, 5000,  0x430,      0, 0x0b1b, CPU_SUPPORTS_DYNAREC, 12,12, 6, 6,  8},
		{"80",        CPU_Cx486DX2, fpus_internal,   80000000, 2.0, 5000,  0x430,      0, 0x311b, CPU_SUPPORTS_DYNAREC, 14,14, 6, 6, 10},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET3,
	.manufacturer = "Cyrix",
	.name = "Cx486DX4",
	.internal_name = "cx486dx4",
	.cpus = {
		{"75",  CPU_Cx486DX4, fpus_internal,  75000000, 3.0, 5000, 0x480, 0, 0x361f, CPU_SUPPORTS_DYNAREC, 12,12, 9, 9,  9},
		{"100", CPU_Cx486DX4, fpus_internal, 100000000, 3.0, 5000, 0x480, 0, 0x361f, CPU_SUPPORTS_DYNAREC, 15,15, 9, 9, 12},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET3,
	.manufacturer = "Cyrix",
	.name = "Cx5x86",
	.internal_name = "cx5x86",
	.cpus = {
		{"80",    CPU_Cx5x86,   fpus_internal,  80000000, 2.0, 5000, 0x480, 0, 0x002f, CPU_SUPPORTS_DYNAREC, 14,14, 6, 6, 10}, /*If we're including the Pentium 50, might as well include this*/
		{"100",   CPU_Cx5x86,   fpus_internal, 100000000, 3.0, 5000, 0x480, 0, 0x002f, CPU_SUPPORTS_DYNAREC, 15,15, 9, 9, 12},
		{"120",   CPU_Cx5x86,   fpus_internal, 120000000, 3.0, 5000, 0x480, 0, 0x002f, CPU_SUPPORTS_DYNAREC, 21,21, 9, 9, 15},
		{"133",   CPU_Cx5x86,   fpus_internal, 133333333, 4.0, 5000, 0x480, 0, 0x002f, CPU_SUPPORTS_DYNAREC, 24,24,12,12, 16},
		{"", 0}
	}
    },
#if defined(DEV_BRANCH) && defined(USE_STPC)
    {
	.package = CPU_PKG_STPC,
	.manufacturer = "ST",
	.name = "STPC-DX",
	.internal_name = "stpc_dx",
	.cpus = {
		{"66",     CPU_Cx486DX,  fpus_internal,  66666666, 1.0, 3300, 0x430, 0, 0x051a, CPU_SUPPORTS_DYNAREC, 7, 7, 3, 3,  5},
		{"75",     CPU_Cx486DX,  fpus_internal,  75000000, 1.0, 3300, 0x430, 0, 0x051a, CPU_SUPPORTS_DYNAREC, 7, 7, 3, 3,  5},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_STPC,
	.manufacturer = "ST",
	.name = "STPC-DX2",
	.internal_name = "stpc_dx2",
	.cpus = {
		{"133",    CPU_Cx486DX2, fpus_internal, 133333333, 2.0, 3300, 0x430, 0, 0x0b1b, CPU_SUPPORTS_DYNAREC, 14,14, 6, 6, 10},
		{"", 0}
	}
    },
#endif
    {
	.package = CPU_PKG_SOCKET4,
	.manufacturer = "Intel",
	.name = "Pentium",
	.internal_name = "pentium_p5",
	.cpus = {
		{"50 (Q0399)",    CPU_PENTIUM, fpus_internal,  50000000, 1, 5000, 0x513, 0x513, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  4, 4,3,3,  6},
		{"60",            CPU_PENTIUM, fpus_internal,  60000000, 1, 5000, 0x517, 0x517, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6,3,3,  7},
		{"66",            CPU_PENTIUM, fpus_internal,  66666666, 1, 5000, 0x517, 0x517, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6,3,3,  8},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET4,
	.manufacturer = "Intel",
	.name = "Pentium OverDrive",
	.internal_name = "pentium_od_s4",
	.cpus = {
		{"100", CPU_PENTIUM, fpus_internal, 100000000, 2, 5000, 0x51A, 0x51A, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  8, 8,6,6, 12},
		{"120", CPU_PENTIUM, fpus_internal, 120000000, 2, 5000, 0x51A, 0x51A, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12,6,6, 14},
		{"133", CPU_PENTIUM, fpus_internal, 133333333, 2, 5000, 0x51A, 0x51A, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12,6,6, 16},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET5_7,
	.manufacturer = "Intel",
	.name = "Pentium",
	.internal_name = "pentium_p54c",
	.cpus = {
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
	.cpus = {
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
	.cpus = {
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
	.internal_name = "pentium_od_s5",
	.cpus = {
		{"125",        CPU_PENTIUM,    fpus_internal, 125000000, 3.0, 3520,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12,7,7, 16},
		{"150",        CPU_PENTIUM,    fpus_internal, 150000000, 2.5, 3520,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15,7,7, 35/2},
		{"166",        CPU_PENTIUM,    fpus_internal, 166666666, 2.5, 3520,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15,7,7, 40},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET5_7,
	.manufacturer = "Intel",
	.name = "Pentium OverDrive MMX",
	.internal_name = "pentium_od_p55c",
	.cpus = {
		{"75",     CPU_PENTIUMMMX, fpus_internal,  75000000, 1.5, 3520, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7,4,4,  9},
		{"125",    CPU_PENTIUMMMX, fpus_internal, 125000000, 2.5, 3520, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12,7,7, 15},
		{"150/60", CPU_PENTIUMMMX, fpus_internal, 150000000, 2.5, 3520, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15,7,7, 35/2},
		{"166",    CPU_PENTIUMMMX, fpus_internal, 166000000, 2.5, 3520, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15,7,7, 20},
		{"180",    CPU_PENTIUMMMX, fpus_internal, 180000000, 3.0, 3520, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18,9,9, 21},
		{"200",    CPU_PENTIUMMMX, fpus_internal, 200000000, 3.0, 3520, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18,9,9, 24},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET5_7,
	.manufacturer = "IDT",
	.name = "WinChip",
	.internal_name = "winchip",
	.cpus = {
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
	.cpus = {
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
	.cpus = {
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
	.cpus = {
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
	.cpus = {
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
	.cpus = {
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
	.cpus = {
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
	.cpus = {
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
	.cpus = {
		{"450",               CPU_K6_2P, fpus_internal,  450000000, 4.5, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    41,   41,  14,  14, 54},
		{"475",               CPU_K6_2P, fpus_internal,  475000000, 5.0, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    43,   43,  15,  15, 57},
		{"500",               CPU_K6_2P, fpus_internal,  500000000, 5.0, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    45,   45,  15,  15, 60},
		{"533",               CPU_K6_2P, fpus_internal,  533333333, 5.5, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    48,   48,  17,  17, 64},
		{"550",               CPU_K6_2P, fpus_internal,  550000000, 5.5, 2000, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    50,   50,  17,  17, 66},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET5_7,
	.manufacturer = "AMD",
	.name = "K6-III",
	.internal_name = "k6_3",
	.cpus = {
		{"400",              CPU_K6_3,  fpus_internal,  400000000, 4.0, 2200, 0x591, 0x591, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    36,   36,  12,  12, 48},
		{"450",              CPU_K6_3,  fpus_internal,  450000000, 4.5, 2200, 0x591, 0x591, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    41,   41,  14,  14, 54},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET5_7,
	.manufacturer = "AMD",
	.name = "K6-III+",
	.internal_name = "k6_3p",
	.cpus = {
		{"100",             CPU_K6_3P, fpus_internal,  100000000, 1.0, 2000, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,     7,    7,   4,   4,  9},
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
	.cpus = {
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
	.cpus = {
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
	.cpus = {
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
	.cpus = {
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
	.cpus = {
		{"50",              CPU_PENTIUMPRO, fpus_internal,  50000000, 1.0, 3100,  0x612,  0x612, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  4, 4, 3, 3, 6},
		{"60",              CPU_PENTIUMPRO, fpus_internal,  60000000, 1.0, 3100,  0x612,  0x612, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 7},
		{"66",              CPU_PENTIUMPRO, fpus_internal,  66666666, 1.0, 3100,  0x612,  0x612, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 8},
		{"75",              CPU_PENTIUMPRO, fpus_internal,  75000000, 1.5, 3100,  0x612,  0x612, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7, 4, 4, 9},
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
	.cpus = {
		{"50",     CPU_PENTIUM2D,  fpus_internal,  50000000, 1.0, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  4, 4, 3, 3, 6},
		{"60",     CPU_PENTIUM2D,  fpus_internal,  60000000, 1.0, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 7},
		{"66",     CPU_PENTIUM2D,  fpus_internal,  66666666, 1.0, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 8},
		{"75",     CPU_PENTIUM2D,  fpus_internal,  75000000, 1.5, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7, 4, 4, 9},
		{"210",    CPU_PENTIUM2D,  fpus_internal, 210000000, 3.5, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 17,17, 7, 7, 25},
		{"233",    CPU_PENTIUM2D,  fpus_internal, 233333333, 3.5, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,10,10, 28},
		{"240",    CPU_PENTIUM2D,  fpus_internal, 240000000, 4.0, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 24,24,12,12, 29},
		{"266",    CPU_PENTIUM2D,  fpus_internal, 266666666, 4.0, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 24,24,12,12, 32},
		{"270",    CPU_PENTIUM2D,  fpus_internal, 270000000, 4.5, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 25,25,12,12, 33},
		{"300/66", CPU_PENTIUM2D,  fpus_internal, 300000000, 4.5, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 25,25,12,12, 36},
		{"300/60", CPU_PENTIUM2D,  fpus_internal, 300000000, 5.0, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27,13,13, 36},
		{"333",    CPU_PENTIUM2D,  fpus_internal, 333333333, 5.0, 3300, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27,13,13, 40},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SLOT1,
	.manufacturer = "Intel",
	.name = "Pentium II (Klamath)",
	.internal_name = "pentium2_klamath",
	.cpus = {
		{"50",        CPU_PENTIUM2,  fpus_internal,  50000000, 1.0, 2800,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  4, 4, 3, 3, 6},
		{"60",        CPU_PENTIUM2,  fpus_internal,  60000000, 1.0, 2800,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 7},
		{"66",        CPU_PENTIUM2,  fpus_internal,  66666666, 1.0, 2800,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 8},
		{"75",        CPU_PENTIUM2,  fpus_internal,  75000000, 1.5, 2800,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7, 4, 4, 9},
		{"233",       CPU_PENTIUM2,  fpus_internal, 233333333, 3.5, 2800,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,10,10, 28},
		{"266",       CPU_PENTIUM2,  fpus_internal, 266666666, 4.0, 2800,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 24,24,12,12, 32},
		{"300/66",    CPU_PENTIUM2,  fpus_internal, 300000000, 4.5, 2800,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 25,25,12,12, 36},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SLOT1,
	.manufacturer = "Intel",
	.name = "Pentium II (Deschutes)",
	.internal_name = "pentium2_deschutes",
	.cpus = {
		{"50",     CPU_PENTIUM2D,  fpus_internal,  50000000, 1.0, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  4, 4, 3, 3, 6},
		{"60",     CPU_PENTIUM2D,  fpus_internal,  60000000, 1.0, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 7},
		{"66",     CPU_PENTIUM2D,  fpus_internal,  66666666, 1.0, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 8},
		{"75",     CPU_PENTIUM2D,  fpus_internal,  75000000, 1.5, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7, 4, 4, 9},
		{"266",    CPU_PENTIUM2D,  fpus_internal, 266666666, 4.0, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 24,24,12,12, 32},
		{"300/66", CPU_PENTIUM2D,  fpus_internal, 300000000, 4.5, 2050,  0x651,  0x651, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 25,25,12,12, 36},
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
	.cpus = {
		{"75",     CPU_PENTIUM2D,  fpus_internal,  75000000, 1.5, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7, 4, 4, 9},
		{"100",    CPU_PENTIUM2D,  fpus_internal, 100000000, 1.5, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10, 6, 6, 12},
		{"133",    CPU_PENTIUM2D,  fpus_internal, 133333333, 2.0, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 16},
		{"166",    CPU_PENTIUM2D,  fpus_internal, 166666666, 2.5, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20},
		{"400",    CPU_PENTIUM2D,  fpus_internal, 400000000, 4.0, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 36,36,12,12, 48},
		{"450",    CPU_PENTIUM2D,  fpus_internal, 450000000, 4.5, 2050,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 41,41,14,14, 54},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET370,
	.manufacturer = "Intel",
	.name = "Celeron (Mendocino)",
	.internal_name = "celeron_mendocino",
	.cpus = {
		{"66",	      CPU_PENTIUM2D,  fpus_internal,  66666666, 1.0, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 8},
		{"100",       CPU_PENTIUM2D,  fpus_internal, 100000000, 1.5, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10, 6, 6, 12},
		{"133",       CPU_PENTIUM2D,  fpus_internal, 133333333, 2.0, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 16},
		{"166",       CPU_PENTIUM2D,  fpus_internal, 166666666, 2.5, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20},
		{"300/66",    CPU_PENTIUM2D,  fpus_internal, 300000000, 4.5, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 25,25,12,12, 36},
		{"333",       CPU_PENTIUM2D,  fpus_internal, 333333333, 5.0, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27,13,13, 40},
		{"366",       CPU_PENTIUM2D,  fpus_internal, 366666666, 5.5, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 33,33,17,17, 44},
		{"400",       CPU_PENTIUM2D,  fpus_internal, 400000000, 6.0, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 36,36,12,12, 48},
		{"433",       CPU_PENTIUM2D,  fpus_internal, 433333333, 6.5, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 39,39,13,13, 51},
		{"466",       CPU_PENTIUM2D,  fpus_internal, 466666666, 7.0, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 42,42,14,14, 56},
		{"500",       CPU_PENTIUM2D,  fpus_internal, 500000000, 7.5, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 45,45,15,15, 60},
		{"533",       CPU_PENTIUM2D,  fpus_internal, 533333333, 8.0, 2050,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 48,48,17,17, 64},
		{"", 0}
	}
    }, {
	.package = CPU_PKG_SOCKET370,
	.manufacturer = "VIA",
	.name = "Cyrix III",
	.internal_name = "c3_samuel",
	.cpus = {
		{"66",  CPU_CYRIX3S, fpus_internal,  66666666, 1.0, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC,  6,  6,  3,  3,  8}, /*66 MHz version*/
		{"233", CPU_CYRIX3S, fpus_internal, 233333333, 3.5, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC, 21, 21,  9,  9, 28},
		{"266", CPU_CYRIX3S, fpus_internal, 266666666, 4.0, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC, 24, 24, 12, 12, 32},
		{"300", CPU_CYRIX3S, fpus_internal, 300000000, 4.5, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC, 27, 27, 13, 13, 36},
		{"333", CPU_CYRIX3S, fpus_internal, 333333333, 5.0, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC, 30, 30, 15, 15, 40},
		{"350", CPU_CYRIX3S, fpus_internal, 350000000, 3.5, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC, 32, 32, 11, 11, 42},
		{"400", CPU_CYRIX3S, fpus_internal, 400000000, 4.0, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC, 36, 36, 12, 12, 48},
		{"450", CPU_CYRIX3S, fpus_internal, 450000000, 4.5, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC, 41, 41, 14, 14, 54}, /*^ is lower P2 speeds to allow emulation below 466 mhz*/
		{"500", CPU_CYRIX3S, fpus_internal, 500000000, 5.0, 2050,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC, 45, 45, 15, 15, 60},
		{"550", CPU_CYRIX3S, fpus_internal, 550000000, 5.5, 2050,   0x662, 0x662, 0, CPU_SUPPORTS_DYNAREC, 50, 50, 17, 17, 66},
		{"600", CPU_CYRIX3S, fpus_internal, 600000000, 6.0, 2050,   0x662, 0x662, 0, CPU_SUPPORTS_DYNAREC, 54, 54, 18, 18, 72},
		{"650", CPU_CYRIX3S, fpus_internal, 650000000, 6.5, 2050,   0x662, 0x662, 0, CPU_SUPPORTS_DYNAREC, 58, 58, 20, 20, 78},
		{"700", CPU_CYRIX3S, fpus_internal, 700000000, 7.0, 2050,   0x662, 0x662, 0, CPU_SUPPORTS_DYNAREC, 62, 62, 21, 21, 84},
		{"", 0}
	}
    }, {
	.package = 0,
    }
};

static cpu_legacy_table_t cpus_8088[] = {{"8088", 0, 0}};
#define cpus_pcjr cpus_8088
static cpu_legacy_table_t cpus_europc[] = {{"8088_europc", 0, 0}};
static cpu_legacy_table_t cpus_8086[] = {{"8086", 0, 0}};
static cpu_legacy_table_t cpus_pc1512[] = {{"8086", 0, 1}};
static cpu_legacy_table_t cpus_286[] = {{"286", 0, 0}};
#define cpus_ibmat cpus_286
#define cpus_ibmxt286 cpus_286
static cpu_legacy_table_t cpus_ps1_m2011[] = {{"286", 0, 2}};
#define cpus_ps2_m30_286 cpus_ps1_m2011
static cpu_legacy_table_t cpus_i386SX[] = {{"i386sx", 0, 0}};
static cpu_legacy_table_t cpus_i386DX[] = {{"rapidcad", 5, 0}, {"i386dx", 0, 0}};
static cpu_legacy_table_t cpus_Am386SX[] = {{"am386sx", 0, 0}};
static cpu_legacy_table_t cpus_Am386DX[] = {{"am386dx", 0, 0}};
static cpu_legacy_table_t cpus_ALiM6117[] = {{"m6117", 0, 0}};
static cpu_legacy_table_t cpus_486SLC[] = {{"cx486srx2", 3, 0}, {"cx486slc", 0, 0}};
static cpu_legacy_table_t cpus_IBM486SLC[] = {{"ibm486slc3", 4, 0}, {"ibm486slc2", 1, 0}, {"ibm386slc", 0, 0}};
static cpu_legacy_table_t cpus_IBM486BL[] = {{"ibm486bl3", 2, 0}, {"ibm486bl2", 0, 0}};
static cpu_legacy_table_t cpus_486DLC[] = {{"cx486drx2", 3, 0}, {"cx486dlc", 0, 0}};
static cpu_legacy_table_t cpus_i486S1[] = {{"idx4_od", 12, 0}, {"i486dx2", 9, 0}, {"i486dx", 6, 0}, {"i486sx2", 4, 0}, {"i486sx", 0, 0}};
static cpu_legacy_table_t cpus_Am486S1[] = {{"am486dx2", 6, 0}, {"am486dx", 4, 0}, {"am486sx2", 2, 0}, {"am486sx", 0, 0}};
static cpu_legacy_table_t cpus_Cx486S1[] = {{"cx486dx2", 5, 0}, {"cx486dx", 3, 0}, {"cx486s", 0, 0}};
static cpu_legacy_table_t cpus_i486[] = {{"pentium_od_s3", 16, 0}, {"idx4_od", 14, 0}, {"idx4", 12, 0}, {"i486dx2", 9, 0}, {"i486dx", 6, 0}, {"i486sx2", 4, 0}, {"i486sx", 0, 0}};
static cpu_legacy_table_t cpus_i486_PC330[] = {{"pentium_od_s3", 4, 0}, {"idx4", 2, 0}, {"i486dx2", 0, 1}};
static cpu_legacy_table_t cpus_Am486[] = {{"am5x86", 13, 0}, {"am486dx4", 9, 0}, {"am486dx2", 6, 0}, {"am486dx", 4, 0}, {"am486sx2", 2, 0}, {"am486sx", 0, 0}};
static cpu_legacy_table_t cpus_Cx486[] = {{"cx5x86", 10, 0}, {"cx486dx4", 8, 0}, {"cx486dx2", 5, 0}, {"cx486dx", 3, 0}, {"cx486s", 0, 0}};
static cpu_legacy_table_t cpus_STPCDX[] = {{"stpc_dx", 0, 0}};
static cpu_legacy_table_t cpus_STPCDX2[] = {{"stpc_dx2", 0, 0}};
static cpu_legacy_table_t cpus_6x863V[] = {{"cx6x86", 0, 0}};
static cpu_legacy_table_t cpus_6x86[] = {{"mii", 14, 0}, {"cx6x86mx", 10, 0}, {"cx6x86l", 6, 0}, {"cx6x86", 0, 0}};
#define cpus_6x86SS7 cpus_6x86
static cpu_legacy_table_t cpus_WinChip[] = {{"winchip2a", 15, 0}, {"winchip2", 11, 0}, {"winchip", 0, 0}};
#define cpus_WinChip_SS7 cpus_WinChip
static cpu_legacy_table_t cpus_Pentium5V[] = {{"pentium_od_s4", 2, 1}, {"pentium_p5", 0, 1}};
static cpu_legacy_table_t cpus_PentiumS5[] = {{"pentium_od_p55c", 10, 1}, {"pentium_od_s5", 7, 0}, {"pentium_p54c", 2, 1}, {"pentium_od_p55c", 1, 0}, {"pentium_p54c", 0, 0}};
static cpu_legacy_table_t cpus_Pentium3V[] = {{"pentium_od_p55c", 13, 1}, {"pentium_od_s5", 10, 0}, {"pentium_p54c", 2, 1}, {"pentium_od_p55c", 1, 0}, {"pentium_p54c", 0, 0}};
static cpu_legacy_table_t cpus_Pentium[] = {{"pentium_od_p55c", 24, 1}, {"pentium_od_s5", 21, 0}, {"pentium_tillamook", 13, 0}, {"pentium_p55c", 10, 0}, {"pentium_p54c", 2, 1}, {"pentium_od_p55c", 1, 0}, {"pentium_p54c", 0, 0}};
#define K5_ENTRIES {"k5_5k86", 6, 3}, {"k5_ssa5", 5, 2}, {"k5_5k86", 4, 2}, {"k5_ssa5", 3, 1}, {"k5_5k86", 2, 1}, {"k5_ssa5", 1, 0}, {"k5_5k86", 0, 0}
static cpu_legacy_table_t cpus_K5[] = {K5_ENTRIES};
#define K56_ENTRIES {"k6_2", 18, 0}, {"k6_m7", 14, 0}, {"k6_m6", 11, 0}, K5_ENTRIES
static cpu_legacy_table_t cpus_K56[] = {K56_ENTRIES};
static cpu_legacy_table_t cpus_K56_SS7[] = {{"k6_3p", 38, 0}, {"k6_3", 36, 0}, {"k6_2p", 31, 0}, K56_ENTRIES};
static cpu_legacy_table_t cpus_PentiumPro[] = {{"pentium2_od", 8, 0}, {"pentiumpro", 0, 0}};
static cpu_legacy_table_t cpus_PentiumII66[] = {{"pentium2_deschutes", 7, 0}, {"pentium2_klamath", 0, 0}};
#define cpus_PentiumII cpus_PentiumII66
static cpu_legacy_table_t cpus_Xeon[] = {{"pentium2_xeon", 0, 0}};
static cpu_legacy_table_t cpus_Celeron[] = {{"celeron_mendocino", 0, 0}};
static cpu_legacy_table_t cpus_PentiumIID[] = {{"pentium2_deschutes", 0, 0}};
static cpu_legacy_table_t cpus_Cyrix3[] = {{"c3_samuel", 0, 0}};

cpu_legacy_machine_t cpu_legacy_table[] = {
    {"ibmpc",			{cpus_8088}},
    {"ibmpc82",			{cpus_8088}},
    {"ibmpcjr",			{cpus_pcjr}},
    {"ibmxt",			{cpus_8088}},
    {"ibmxt86",			{cpus_8088}},
    {"americxt",		{cpus_8088}},
    {"amixt",			{cpus_8088}},
    {"portable",		{cpus_8088}},
    {"dtk",			{cpus_8088}},
    {"genxt",			{cpus_8088}},
    {"jukopc",			{cpus_8088}},
    {"open_xt",			{cpus_8088}},
    {"pxxt",			{cpus_8088}},
    {"europc",			{cpus_europc}},
    {"tandy",			{cpus_europc}},
    {"tandy1000hx",		{cpus_europc}},
    {"t1000",			{cpus_8088}},
    {"ltxt",			{cpus_8088}},
    {"xi8088",			{cpus_8088}},
    {"zdsupers",		{cpus_8088}},
    {"pc1512",			{cpus_pc1512}},
    {"pc1640",			{cpus_8086}},
    {"pc2086",			{cpus_8086}},
    {"pc3086",			{cpus_8086}},
    {"pc200",			{cpus_8086}},
    {"ppc512",			{cpus_8086}},
    {"deskpro",			{cpus_8086}},
    {"olivetti_m24",		{cpus_8086}},
    {"iskra3104",		{cpus_8086}},
    {"tandy1000sl2",		{cpus_8086}},
    {"t1200",			{cpus_8086}},
    {"lxt3",			{cpus_8086}},
    {"hed919",			{cpus_286}},
    {"ibmat",			{cpus_ibmat}},
    {"ibmps1es",		{cpus_ps1_m2011}},
    {"ibmps2_m30_286",		{cpus_ps2_m30_286, cpus_IBM486SLC}},
    {"ibmxt286",		{cpus_ibmxt286}},
    {"ibmatami",		{cpus_ibmat}},
    {"cmdpc30",			{cpus_286}},
    {"portableii",		{cpus_286}},
    {"portableiii",		{cpus_286}},
    {"mr286",			{cpus_286}},
    {"open_at",			{cpus_286}},
    {"ibmatpx",			{cpus_ibmat}},
    {"ibmatquadtel",		{cpus_ibmat}},
    {"siemens",			{cpus_286}},
    {"t3100e",			{cpus_286}},
    {"quadt286",		{cpus_286}},
    {"tg286m",			{cpus_286}},
    {"ami286",			{cpus_286}},
    {"px286",			{cpus_286}},
    {"award286",		{cpus_286}},
    {"gw286ct",			{cpus_286}},
    {"gdc212m",			{cpus_286}},
    {"super286tr",		{cpus_286}},
    {"spc4200p",		{cpus_286}},
    {"spc4216p",		{cpus_286}},
    {"deskmaster286",		{cpus_286}},
    {"ibmps2_m50",		{cpus_ps2_m30_286, cpus_IBM486SLC}},
    {"ibmps1_2121",		{cpus_i386SX, cpus_Am386SX, cpus_486SLC}},
    {"ibmps1_2121_isa",		{cpus_i386SX, cpus_Am386SX, cpus_486SLC}},
    {"arb1375",			{cpus_ALiM6117}},
    {"pja511m",			{cpus_ALiM6117}},
    {"ama932j",			{cpus_i386SX, cpus_Am386SX, cpus_486SLC}},
    {"adi386sx",		{cpus_i386SX, cpus_Am386SX, cpus_486SLC}},
    {"shuttle386sx",		{cpus_i386SX, cpus_Am386SX, cpus_486SLC}},
    {"dtk386",			{cpus_i386SX, cpus_Am386SX, cpus_486SLC}},
    {"awardsx",			{cpus_i386SX, cpus_Am386SX, cpus_486SLC}},
    {"cbm_sl386sx25",		{cpus_i386SX, cpus_Am386SX, cpus_486SLC}},
    {"kmxc02",			{cpus_i386SX, cpus_Am386SX, cpus_486SLC}},
    {"megapc",			{cpus_i386SX, cpus_Am386SX, cpus_486SLC}},
    {"ibmps2_m55sx",		{cpus_i386SX, cpus_Am386SX, cpus_486SLC, cpus_IBM486SLC}},
    {"acc386",			{cpus_i386DX, cpus_Am386DX, cpus_486DLC}},
    {"ecs386",			{cpus_i386DX, cpus_Am386DX, cpus_486DLC}},
    {"portableiii386",		{cpus_i386DX, cpus_Am386DX, cpus_486DLC}},
    {"micronics386",		{cpus_i386DX, cpus_Am386DX, cpus_486DLC}},
    {"asus386",			{cpus_i386DX, cpus_Am386DX, cpus_486DLC}},
    {"ustechnologies386",	{cpus_i386DX, cpus_Am386DX, cpus_486DLC}},
    {"award386dx",		{cpus_i386DX, cpus_Am386DX, cpus_486DLC}},
    {"ami386dx",		{cpus_i386DX, cpus_Am386DX, cpus_486DLC}},
    {"mr386dx",			{cpus_i386DX, cpus_Am386DX, cpus_486DLC}},
    {"ibmps2_m70_type3",	{cpus_i386DX, cpus_Am386DX, cpus_486DLC, cpus_IBM486BL}},
    {"ibmps2_m80",		{cpus_i386DX, cpus_Am386DX, cpus_486DLC, cpus_IBM486BL}},
    {"pb410a",			{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"acera1g",			{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"win486",			{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"ali1429",			{cpus_i486S1, cpus_Am486S1, cpus_Cx486S1}},
    {"cs4031",			{cpus_i486S1, cpus_Am486S1, cpus_Cx486S1}},
    {"rycleopardlx",		{cpus_IBM486SLC}},
    {"award486",		{cpus_i486S1, cpus_Am486S1, cpus_Cx486S1}},
    {"ami486",			{cpus_i486S1, cpus_Am486S1, cpus_Cx486S1}},
    {"mr486",			{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"pc330_6571",		{cpus_i486_PC330}},
    {"403tg",			{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"sis401",			{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"valuepoint433",		{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"ami471",			{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"win471",			{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"vi15g",			{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"vli486sv2g",		{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"dtk486",			{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"px471",			{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"486vchd",			{cpus_i486S1, cpus_Am486S1, cpus_Cx486S1}},
    {"ibmps1_2133",		{cpus_i486S1, cpus_Am486S1, cpus_Cx486S1}},
    {"vect486vl",		{cpus_i486S1, cpus_Am486S1, cpus_Cx486S1}},
    {"ibmps2_m70_type4",	{cpus_i486S1, cpus_Am486S1, cpus_Cx486S1}},
    {"abpb4",			{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"486ap4",			{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"486sp3g",			{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"alfredo",			{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"ls486e",			{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"m4li",			{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"r418",			{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"4sa2",			{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"4dps",			{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"itoxstar",		{cpus_STPCDX}},
    {"arb1479",			{cpus_STPCDX2}},
    {"pcm9340",			{cpus_STPCDX2}},
    {"pcm5330",			{cpus_STPCDX2}},
    {"486vipio2",		{cpus_i486, cpus_Am486, cpus_Cx486}},
    {"p5mp3",			{cpus_Pentium5V}},
    {"dellxp60",		{cpus_Pentium5V}},
    {"opti560l",		{cpus_Pentium5V}},
    {"ambradp60",		{cpus_Pentium5V}},
    {"valuepointp60",		{cpus_Pentium5V}},
    {"revenge",			{cpus_Pentium5V}},
    {"586mc1",			{cpus_Pentium5V}},
    {"pb520r",			{cpus_Pentium5V}},
    {"excalibur",		{cpus_Pentium5V}},
    {"plato",			{cpus_PentiumS5, cpus_WinChip, cpus_K5}},
    {"ambradp90",		{cpus_PentiumS5, cpus_WinChip, cpus_K5}},
    {"430nx",			{cpus_PentiumS5, cpus_WinChip, cpus_K5}},
    {"acerv30",			{cpus_PentiumS5, cpus_WinChip, cpus_K5}},
    {"apollo",			{cpus_PentiumS5, cpus_WinChip, cpus_K5}},
    {"vectra54",		{cpus_PentiumS5, cpus_WinChip, cpus_K5}},
    {"zappa",			{cpus_PentiumS5, cpus_WinChip, cpus_K5}},
    {"powermate_v",		{cpus_PentiumS5, cpus_WinChip, cpus_K5}},
    {"mb500n",			{cpus_PentiumS5, cpus_WinChip, cpus_K5}},
    {"p54tp4xe",		{cpus_Pentium3V, cpus_WinChip, cpus_K5, cpus_6x863V}},
    {"mr586",			{cpus_Pentium3V, cpus_WinChip, cpus_K5, cpus_6x863V}},
    {"gw2katx",			{cpus_Pentium3V, cpus_WinChip, cpus_K5, cpus_6x863V}},
    {"thor",			{cpus_Pentium3V, cpus_WinChip, cpus_K5, cpus_6x863V}},
    {"mrthor",			{cpus_Pentium3V, cpus_WinChip, cpus_K5, cpus_6x863V}},
    {"endeavor",		{cpus_Pentium3V, cpus_WinChip, cpus_K5, cpus_6x863V}},
    {"pb640",			{cpus_Pentium3V, cpus_WinChip, cpus_K5, cpus_6x863V}},
    {"chariot",			{cpus_Pentium3V, cpus_K5}},
    {"acerm3a",			{cpus_Pentium3V, cpus_WinChip, cpus_K5, cpus_6x863V}},
    {"ap53",			{cpus_Pentium3V, cpus_WinChip, cpus_K5, cpus_6x863V}},
    {"8500tuc",			{cpus_Pentium3V, cpus_WinChip, cpus_K5, cpus_6x863V}},
    {"p55t2s",			{cpus_Pentium3V, cpus_WinChip, cpus_K5, cpus_6x863V}},
    {"acerv35n",		{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"p55t2p4",			{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"m7shi",			{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"tc430hx",			{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"equium5200",		{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"pcv240",			{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"p65up5_cp55t2d",		{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"p55tvp4",			{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"8500tvxa",		{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"presario4500",		{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"p55va",			{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"gw2kte",			{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"brio80xx",		{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"pb680",			{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"430vx",			{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"nupro592",		{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"tx97",			{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"an430tx",			{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"ym430tx",			{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"mb540n",			{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"p5mms98",			{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"ficva502",		{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"ficpa2012",		{cpus_Pentium, cpus_WinChip, cpus_K56, cpus_6x86}},
    {"ax59pro",			{cpus_Pentium, cpus_WinChip_SS7, cpus_K56_SS7, cpus_6x86SS7}},
    {"ficva503p",		{cpus_Pentium, cpus_WinChip_SS7, cpus_K56_SS7, cpus_6x86SS7}},
    {"ficva503a",		{cpus_Pentium, cpus_WinChip_SS7, cpus_K56_SS7, cpus_6x86SS7}},
    {"v60n",			{cpus_PentiumPro}},
    {"p65up5_cp6nd",		{cpus_PentiumPro}},
    {"8600ttc",			{cpus_PentiumPro}},
    {"686nx",			{cpus_PentiumPro}},
    {"ap440fx",			{cpus_PentiumPro}},
    {"vs440fx",			{cpus_PentiumPro}},
    {"m6mi",			{cpus_PentiumPro}},
    {"mb600n",			{cpus_PentiumPro}},
    {"p65up5_cpknd",		{cpus_PentiumII66}},
    {"kn97",			{cpus_PentiumII66}},
    {"lx6",			{cpus_PentiumII66}},
    {"spitfire",		{cpus_PentiumII66}},
    {"p6i440e2",		{cpus_PentiumII66}},
    {"p2bls",			{cpus_PentiumII, cpus_Celeron, cpus_Cyrix3}},
    {"p3bf",			{cpus_PentiumII, cpus_Celeron, cpus_Cyrix3}},
    {"bf6",			{cpus_PentiumII, cpus_Celeron, cpus_Cyrix3}},
    {"ax6bc",			{cpus_PentiumII, cpus_Celeron, cpus_Cyrix3}},
    {"atc6310bxii",		{cpus_PentiumII, cpus_Celeron, cpus_Cyrix3}},
    {"ga686bx",			{cpus_PentiumII, cpus_Celeron, cpus_Cyrix3}},
    {"tsunamiatx",		{cpus_PentiumII, cpus_Celeron, cpus_Cyrix3}},
    {"p6sba",			{cpus_PentiumII, cpus_Celeron, cpus_Cyrix3}},
    {"ergox365",		{cpus_PentiumII, cpus_Celeron, cpus_Cyrix3}},
    {"fw6400gx_s1",		{cpus_PentiumII, cpus_Celeron, cpus_Cyrix3}},
    {"ficka6130",		{cpus_PentiumII, cpus_Celeron, cpus_Cyrix3}},
    {"6gxu",			{cpus_Xeon}},
    {"fw6400gx",		{cpus_Xeon}},
    {"s2dge",			{cpus_Xeon}},
    {"s370slm",			{cpus_Celeron, cpus_Cyrix3}},
    {"awo671r",			{cpus_Celeron, cpus_Cyrix3}},
    {"cubx",			{cpus_Celeron, cpus_Cyrix3}},
    {"atc7020bxii",		{cpus_Celeron, cpus_Cyrix3}},
    {"ambx133",			{cpus_Celeron, cpus_Cyrix3}},
    {"trinity371",		{cpus_Celeron}},
    {"63a",			{cpus_Celeron, cpus_Cyrix3}},
    {"apas3",			{cpus_Celeron, cpus_Cyrix3}},
    {"wcf681",			{cpus_Celeron, cpus_Cyrix3}},
    {"6via85x",			{cpus_Celeron, cpus_Cyrix3}},
    {"p6bap",			{cpus_Celeron, cpus_Cyrix3}},
    {"603tcf",			{cpus_Celeron, cpus_Cyrix3}},
    {"vpc2007",			{cpus_PentiumIID, cpus_Celeron}},
    {NULL, {NULL}}
};
