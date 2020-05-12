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
 *		Available cpuspeeds:
 *
 *		 0 = 16 MHz
 *		 1 = 20 MHz
 *		 2 = 25 MHz
 *		 3 = 33 MHz
 *		 4 = 40 MHz
 *		 5 = 50 MHz
 *		 6 = 66 MHz
 *		 7 = 75 MHz
 *		 8 = 80 MHz
 *		 9 = 90 MHz
 *		10 = 100 MHz
 *		11 = 120 MHz
 *		12 = 133 MHz
 *		13 = 150 MHz
 *		14 = 160 MHz
 *		15 = 166 MHz
 *		16 = 180 MHz
 *		17 = 200 MHz
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		leilei,
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 leilei.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/machine.h>


CPU cpus_8088[] = {
    /*8088 standard*/
    {"8088/4.77",    CPU_8088,  4772728,    1, 0, 0, 0, 0, 0,0,0,0, 1},
    {"8088/7.16",    CPU_8088,  7159092,    1, 0, 0, 0, 0, 0,0,0,0, 1},
    {"8088/8",       CPU_8088,  8000000,    1, 0, 0, 0, 0, 0,0,0,0, 1},
    {"8088/10",      CPU_8088, 10000000,    1, 0, 0, 0, 0, 0,0,0,0, 1},
    {"8088/12",      CPU_8088, 12000000,    1, 0, 0, 0, 0, 0,0,0,0, 1},
    {"8088/16",      CPU_8088, 16000000,    1, 0, 0, 0, 0, 0,0,0,0, 1},
    {"",             -1,              0,    0, 0, 0, 0, 0, 0,0,0,0, 0}
};

CPU cpus_pcjr[] = {
    /*8088 PCjr*/
    {"8088/4.77",    CPU_8088,  4772728,    1, 0, 0, 0, 0, 0,0,0,0, 1},
    {"",             -1,              0,    0, 0, 0, 0, 0, 0,0,0,0, 0}
};

CPU cpus_europc[] = {
    /*8088 EuroPC*/
    {"8088/4.77",    CPU_8088,  4772728,    1, 0, 0, 0, CPU_ALTERNATE_XTAL, 0,0,0,0, 1},
    {"8088/7.16",    CPU_8088,  7159092,    1, 0, 0, 0, CPU_ALTERNATE_XTAL, 0,0,0,0, 1},
    {"8088/9.54",    CPU_8088,  9545456,    1, 0, 0, 0, 0, 0,0,0,0, 1},
    {"",             -1,              0,    0, 0, 0, 0, 0, 0,0,0,0, 0}
};

CPU cpus_8086[] = {
    /*8086 standard*/
    {"8086/7.16",    CPU_8086,   7159092,    1, 0, 0, 0, CPU_ALTERNATE_XTAL, 0,0,0,0, 1},
    {"8086/8",       CPU_8086,   8000000,    1, 0, 0, 0, 0, 0,0,0,0, 1},
    {"8086/9.54",    CPU_8086,   9545456,    1, 0, 0, 0, CPU_ALTERNATE_XTAL, 0,0,0,0, 1},
    {"8086/10",      CPU_8086,  10000000,    1, 0, 0, 0, 0, 0,0,0,0, 1},
    {"8086/12",      CPU_8086,  12000000,    1, 0, 0, 0, 0, 0,0,0,0, 1},
    {"8086/16",      CPU_8086,  16000000,    1, 0, 0, 0, 0, 0,0,0,0, 2},
    {"",             -1,               0,    0, 0, 0, 0, 0, 0,0,0,0, 0}
};

CPU cpus_pc1512[] = {
    /*8086 Amstrad*/
    {"8086/8",       CPU_8086,   8000000,    1, 0, 0, 0, 0, 0,0,0,0, 1},
    {"",             -1,               0,    0, 0, 0, 0, 0, 0,0,0,0, 0}
};

CPU cpus_286[] = {
    /*286*/
    {"286/6",        CPU_286,    6000000,    1, 0, 0, 0, 0, 2,2,2,2, 1},
    {"286/8",        CPU_286,    8000000,    1, 0, 0, 0, 0, 2,2,2,2, 1},
    {"286/10",       CPU_286,   10000000,    1, 0, 0, 0, 0, 2,2,2,2, 1},
    {"286/12",       CPU_286,   12500000,    1, 0, 0, 0, 0, 3,3,3,3, 2},
    {"286/16",       CPU_286,   16000000,    1, 0, 0, 0, 0, 3,3,3,3, 2},
    {"286/20",       CPU_286,   20000000,    1, 0, 0, 0, 0, 4,4,4,4, 3},
    {"286/25",       CPU_286,   25000000,    1, 0, 0, 0, 0, 4,4,4,4, 3},
    {"",             -1,               0,    0, 0, 0, 0, 0, 0,0,0,0, 0}
};

CPU cpus_ibmat[] = {
    /*286*/
    {"286/6",        CPU_286,    6000000,    1, 0, 0, 0, 0, 3,3,3,3, 1},
    {"286/8",        CPU_286,    8000000,    1, 0, 0, 0, 0, 3,3,3,3, 1},
    {"",             -1,               0,    0, 0, 0, 0, 0, 0,0,0,0, 0}
};

CPU cpus_ibmxt286[] = {
    /*286*/
    {"286/6",        CPU_286,    6000000,    1, 0, 0, 0, 0, 2,2,2,2, 1},
    {"",             -1,               0,    0, 0, 0, 0, 0, 0,0,0,0, 0}
};

CPU cpus_ps1_m2011[] = {
    /*286*/
    {"286/10",       CPU_286,   10000000,    1, 0, 0, 0, 0, 2,2,2,2, 1},
    {"",             -1,               0,    0, 0, 0, 0, 0, 0,0,0,0, 9}
};

CPU cpus_ps2_m30_286[] = {
    /*286*/
    {"286/10",       CPU_286,   10000000,    1, 0, 0, 0, 0, 2,2,2,2, 1},
    {"286/12",       CPU_286,   12500000,    1, 0, 0, 0, 0, 3,3,3,3, 2},
    {"286/16",       CPU_286,   16000000,    1, 0, 0, 0, 0, 3,3,3,3, 2},
    {"286/20",       CPU_286,   20000000,    1, 0, 0, 0, 0, 4,4,4,4, 3},
    {"286/25",       CPU_286,   25000000,    1, 0, 0, 0, 0, 4,4,4,4, 3},
    {"",             -1,               0,    0, 0, 0, 0, 0, 0,0,0,0, 0}
};

CPU cpus_i386SX[] = {
    /*i386SX*/
    {"i386SX/16",    CPU_386SX, 16000000,    1, 0x2308, 0, 0, 0, 3,3,3,3, 2},
    {"i386SX/20",    CPU_386SX, 20000000,    1, 0x2308, 0, 0, 0, 4,4,3,3, 3},
    {"i386SX/25",    CPU_386SX, 25000000,    1, 0x2308, 0, 0, 0, 4,4,3,3, 3},
    {"i386SX/33",    CPU_386SX, 33333333,    1, 0x2308, 0, 0, 0, 6,6,3,3, 4},
    {"i386SX/40",    CPU_386SX, 40000000,    1, 0x2308, 0, 0, 0, 7,7,3,3, 5},
    {"",             -1,               0,    0,      0, 0, 0, 0, 0,0,0,0, 0}
};

CPU cpus_i386DX[] = {
    /*i386DX/RapidCAD*/
    {"i386DX/16",    CPU_386DX,     16000000, 1, 0x0308, 0, 0, 0, 3,3,3,3, 2},
    {"i386DX/20",    CPU_386DX,     20000000, 1, 0x0308, 0, 0, 0, 4,4,3,3, 3},
    {"i386DX/25",    CPU_386DX,     25000000, 1, 0x0308, 0, 0, 0, 4,4,3,3, 3},
    {"i386DX/33",    CPU_386DX,     33333333, 1, 0x0308, 0, 0, 0, 6,6,3,3, 4},
    {"i386DX/40",    CPU_386DX,     40000000, 1, 0x0308, 0, 0, 0, 7,7,3,3, 5},
    {"RapidCAD/25",  CPU_RAPIDCAD,  25000000, 1, 0x0340, 0, 0, CPU_SUPPORTS_DYNAREC, 4,4,3,3, 3},
    {"RapidCAD/33",  CPU_RAPIDCAD,  33333333, 1, 0x0340, 0, 0, CPU_SUPPORTS_DYNAREC, 6,6,3,3, 4},
    {"RapidCAD/40",  CPU_RAPIDCAD,  40000000, 1, 0x0340, 0, 0, CPU_SUPPORTS_DYNAREC, 7,7,3,3, 5},
    {"",             -1,                   0, 0,      0, 0, 0, 0, 0,0,0,0, 0}
};

CPU cpus_Am386SX[] = {
    /*Am386SX*/
    {"Am386SX/16",   CPU_386SX,     16000000, 1, 0x2308, 0, 0, 0, 3,3,3,3, 2},
    {"Am386SX/20",   CPU_386SX,     20000000, 1, 0x2308, 0, 0, 0, 4,4,3,3, 3},
    {"Am386SX/25",   CPU_386SX,     25000000, 1, 0x2308, 0, 0, 0, 4,4,3,3, 3},
    {"Am386SX/33",   CPU_386SX,     33333333, 1, 0x2308, 0, 0, 0, 6,6,3,3, 4},
    {"Am386SX/40",   CPU_386SX,     40000000, 1, 0x2308, 0, 0, 0, 7,7,3,3, 5},
    {"",             -1,            0,           0,      0, 0, 0, 0, 0,0,0,0, 0}
};

CPU cpus_Am386DX[] = {
    /*Am386DX*/
    {"Am386DX/25",   CPU_386DX,     25000000, 1, 0x0308, 0, 0, 0, 4,4,3,3, 3},
    {"Am386DX/33",   CPU_386DX,     33333333, 1, 0x0308, 0, 0, 0, 6,6,3,3, 4},
    {"Am386DX/40",   CPU_386DX,     40000000, 1, 0x0308, 0, 0, 0, 7,7,3,3, 5},
    {"",             -1,                   0, 0,      0, 0, 0, 0, 0,0,0,0, 0}
};

CPU cpus_486SLC[] = {
    /*Cx486SLC*/
    {"Cx486SLC/20",  CPU_486SLC, 20000000, 1, 0x400, 0, 0x0000, 0, 4,4,3,3, 3},
    {"Cx486SLC/25",  CPU_486SLC, 25000000, 1, 0x400, 0, 0x0000, 0, 4,4,3,3, 3},
    {"Cx486SLC/33",  CPU_486SLC, 33333333, 1, 0x400, 0, 0x0000, 0, 6,6,3,3, 4},
    {"Cx486SRx2/32", CPU_486SLC, 32000000, 2, 0x406, 0, 0x0006, 0, 6,6,6,6, 4},
    {"Cx486SRx2/40", CPU_486SLC, 40000000, 2, 0x406, 0, 0x0006, 0, 8,8,6,6, 6},
    {"Cx486SRx2/50", CPU_486SLC, 50000000, 2, 0x406, 0, 0x0006, 0, 8,8,6,6, 6},
    {"",             -1,                0, 0,     0, 0,      0, 0, 0,0,0,0, 0}
};

CPU cpus_IBM386SLC[] = {
    /*IBM 386SLC*/	
    {"386SLC/16",  CPU_IBM386SLC, 16000000, 1, 0xA301, 0, 0, 0, 3,3,3,3, 2},
    {"386SLC/20",  CPU_IBM386SLC, 20000000, 1, 0xA301, 0, 0, 0, 4,4,3,3, 3},
    {"386SLC/25",  CPU_IBM386SLC, 25000000, 1, 0xA301, 0, 0, 0, 4,4,3,3, 3},	
    {"",           -1,            0,        0,     0, 0, 0, 0, 0,0,0,0, 0}
};

CPU cpus_IBM486SLC[] = {
    /*IBM 486SLC*/
    {"486SLC/33",   CPU_IBM486SLC, 33333333,  1, 0xA401, 0, 0, 0, 6,6,3,3,    4},
    {"486SLC2/40",  CPU_IBM486SLC, 40000000,  2, 0xA421, 0, 0, 0, 7,7,6,6,    5},
    {"486SLC2/50",  CPU_IBM486SLC, 50000000,  2, 0xA421, 0, 0, 0, 8,8,6,6,    6},
    {"486SLC2/66",  CPU_IBM486SLC, 66666666,  2, 0xA421, 0, 0, 0, 12,12,6,6,  8},
    {"486SLC3/60",  CPU_IBM486SLC, 60000000,  3, 0xA439, 0, 0, 0, 12,12,9,9,  7},
    {"486SLC3/75",  CPU_IBM486SLC, 75000000,  3, 0xA439, 0, 0, 0, 12,12,9,9,  9},
    {"486SLC3/100", CPU_IBM486SLC, 100000000, 3, 0xA439, 0, 0, 0, 18,18,9,9, 12},	
    {"",            -1,            0,         0, 0,     0, 0, 0, 0,0,0,0,    0}
};

CPU cpus_IBM486BL[] = {
    /*IBM Blue Lightning*/	
    {"486BL2/50",  CPU_IBM486BL, 50000000,  2, 0xA439, 0, 0, 0, 8,8,6,6,    6},
    {"486BL2/66",  CPU_IBM486BL, 66666666,  2, 0xA439, 0, 0, 0, 12,12,6,6,  8},
    {"486BL3/75",  CPU_IBM486BL, 75000000,  3, 0xA439, 0, 0, 0, 12,12,9,9,  9},
    {"486BL3/100", CPU_IBM486BL, 100000000, 3, 0xA439, 0, 0, 0, 18,18,9,9, 12},	
    {"",           -1,           0,         0,     0, 0, 0, 0, 0,0,0,0,    0}
};

CPU cpus_486DLC[] = {
    /*Cx486DLC*/
    {"Cx486DLC/25",  CPU_486DLC, 25000000, 1, 0x401, 0, 0x0001, 0,  4, 4,3,3, 3},
    {"Cx486DLC/33",  CPU_486DLC, 33333333, 1, 0x401, 0, 0x0001, 0,  6, 6,3,3, 4},
    {"Cx486DLC/40",  CPU_486DLC, 40000000, 1, 0x401, 0, 0x0001, 0,  7, 7,3,3, 5},
    {"Cx486DRx2/32", CPU_486DLC, 32000000, 2, 0x407, 0, 0x0007, 0,  6, 6,6,6, 4},
    {"Cx486DRx2/40", CPU_486DLC, 40000000, 2, 0x407, 0, 0x0007, 0,  8, 8,6,6, 6},
    {"Cx486DRx2/50", CPU_486DLC, 50000000, 2, 0x407, 0, 0x0007, 0,  8, 8,6,6, 6},
    {"Cx486DRx2/66", CPU_486DLC, 66666666, 2, 0x407, 0, 0x0007, 0, 12,12,6,6, 8},
    {"",             -1,         0,        0,     0, 0,      0, 0,  0, 0,0,0, 0}
};

CPU cpus_i486S1[] = {
    /*i486*/
    {"i486SX/16",            CPU_i486SX,   16000000, 1,  0x420,        0, 0, CPU_SUPPORTS_DYNAREC,  3, 3,3,3,  2},
    {"i486SX/20",            CPU_i486SX,   20000000, 1,  0x420,        0, 0, CPU_SUPPORTS_DYNAREC,  4, 4,3,3,  3},
    {"i486SX/25",            CPU_i486SX,   25000000, 1,  0x422,        0, 0, CPU_SUPPORTS_DYNAREC,  4, 4,3,3,  3},
    {"i486SX/33",            CPU_i486SX,   33333333, 1,  0x42a,        0, 0, CPU_SUPPORTS_DYNAREC,  6, 6,3,3,  4},
    {"i486SX2/50",           CPU_i486SX2,  50000000, 2,  0x45b,        0, 0, CPU_SUPPORTS_DYNAREC,  8, 8,6,6,  6},
    {"i486SX2/66 (Q0569)",   CPU_i486SX2,  66666666, 2,  0x45b,        0, 0, CPU_SUPPORTS_DYNAREC,  8, 8,6,6,  8},
    {"i486DX/25",            CPU_i486DX,   25000000, 1,  0x404,        0, 0, CPU_SUPPORTS_DYNAREC,  4, 4,3,3,  3},
    {"i486DX/33",            CPU_i486DX,   33333333, 1,  0x414,        0, 0, CPU_SUPPORTS_DYNAREC,  6, 6,3,3,  4},
    {"i486DX/50",            CPU_i486DX,   50000000, 1,  0x411,        0, 0, CPU_SUPPORTS_DYNAREC,  8, 8,4,4,  6},
    {"i486DX2/40",           CPU_i486DX2,  40000000, 2,  0x430,    0x430, 0, CPU_SUPPORTS_DYNAREC,  7, 7,6,6,  5},
    {"i486DX2/50",           CPU_i486DX2,  50000000, 2,  0x433,    0x433, 0, CPU_SUPPORTS_DYNAREC,  8, 8,6,6,  6},
    {"i486DX2/66",           CPU_i486DX2,  66666666, 2,  0x435,    0x435, 0, CPU_SUPPORTS_DYNAREC, 12,12,6,6,  8},
    {"iDX4 OverDrive 75",    CPU_iDX4,     75000000, 3,  0x1480,  0x1480, 0, CPU_SUPPORTS_DYNAREC, 12,12,9,9,  9}, /*Only added the DX4 OverDrive as the others would be redundant*/
    {"iDX4 OverDrive 100",   CPU_iDX4,    100000000, 3,  0x1480,  0x1480, 0, CPU_SUPPORTS_DYNAREC, 18,18,9,9, 12}, 
    {"",                     -1,                  0, 0,       0,       0, 0,                    0,  0, 0,0,0,  0}
};
CPU cpus_Am486S1[] = {
    /*Am486*/
    {"Am486SX/33",   CPU_Am486SX,  33333333, 1, 0x42a,     0, 0, CPU_SUPPORTS_DYNAREC,  6, 6, 3, 3, 4},
    {"Am486SX/40",   CPU_Am486SX,  40000000, 1, 0x42a,     0, 0, CPU_SUPPORTS_DYNAREC,  7, 7, 3, 3, 5}, 
    {"Am486SX2/50",  CPU_Am486SX2, 50000000, 2, 0x45b, 0x45b, 0, CPU_SUPPORTS_DYNAREC,  8, 8, 6, 6, 6}, /*CPUID available on SX2, DX2, DX4, 5x86, >= 50 MHz*/
    {"Am486SX2/66",  CPU_Am486SX2, 66666666, 2, 0x45b, 0x45b, 0, CPU_SUPPORTS_DYNAREC, 12,12, 6, 6, 8}, /*Isn't on all real AMD SX2s and DX2s, availability here is pretty arbitary (and distinguishes them from the Intel chips)*/
    {"Am486DX/33",   CPU_Am486DX,  33333333, 1, 0x430,     0, 0, CPU_SUPPORTS_DYNAREC,  6, 6, 3, 3, 4},
    {"Am486DX/40",   CPU_Am486DX,  40000000, 1, 0x430,     0, 0, CPU_SUPPORTS_DYNAREC,  7, 7, 3, 3, 5},
    {"Am486DX2/50",  CPU_Am486DX2, 50000000, 2, 0x470, 0x470, 0, CPU_SUPPORTS_DYNAREC,  8, 8, 6, 6, 6},
    {"Am486DX2/66",  CPU_Am486DX2, 66666666, 2, 0x470, 0x470, 0, CPU_SUPPORTS_DYNAREC, 12,12, 6, 6, 8},
    {"Am486DX2/80",  CPU_Am486DX2, 80000000, 2, 0x470, 0x470, 0, CPU_SUPPORTS_DYNAREC, 14,14, 6, 6, 10},
    {"",             -1,                  0, 0,     0,     0, 0,                    0,  0, 0, 0, 0,  0}
};
CPU cpus_Cx486S1[] = {
    /*Cyrix 486*/
    {"Cx486S/25",            CPU_Cx486S,   25000000, 1.0,  0x420,      0, 0x0010, CPU_SUPPORTS_DYNAREC,  4, 4, 3, 3,  3},
    {"Cx486S/33",            CPU_Cx486S,   33333333, 1.0,  0x420,      0, 0x0010, CPU_SUPPORTS_DYNAREC,  6, 6, 3, 3,  4},
    {"Cx486S/40",            CPU_Cx486S,   40000000, 1.0,  0x420,      0, 0x0010, CPU_SUPPORTS_DYNAREC,  7, 7, 3, 3,  5},
    {"Cx486DX/33",          CPU_Cx486DX,   33333333, 1.0,  0x430,      0, 0x051a, CPU_SUPPORTS_DYNAREC,  6, 6, 3, 3,  4},
    {"Cx486DX/40",          CPU_Cx486DX,   40000000, 1.0,  0x430,      0, 0x051a, CPU_SUPPORTS_DYNAREC,  7, 7, 3, 3,  5},
    {"Cx486DX2/50",        CPU_Cx486DX2,   50000000, 2.0,  0x430,      0, 0x081b, CPU_SUPPORTS_DYNAREC,  8, 8, 6, 6,  6},
    {"Cx486DX2/66",        CPU_Cx486DX2,   66666666, 2.0,  0x430,      0, 0x0b1b, CPU_SUPPORTS_DYNAREC, 12,12, 6, 6,  8},
    {"Cx486DX2/80",        CPU_Cx486DX2,   80000000, 2.0,  0x430,      0, 0x311b, CPU_SUPPORTS_DYNAREC, 14,14, 6, 6, 10},
    {"",                             -1,          0, 0.0,      0,      0, 0x0000,                    0,  0, 0, 0, 0,  0}
};

CPU cpus_i486[] = {
    /*i486/P24T*/
    {"i486SX/16",            CPU_i486SX,   16000000, 1.0,  0x420,      0, 0x0000, CPU_SUPPORTS_DYNAREC,  3, 3, 3, 3,  2},
    {"i486SX/20",            CPU_i486SX,   20000000, 1.0,  0x420,      0, 0x0000, CPU_SUPPORTS_DYNAREC,  4, 4, 3, 3,  3},
    {"i486SX/25",            CPU_i486SX,   25000000, 1.0,  0x422,      0, 0x0000, CPU_SUPPORTS_DYNAREC,  4, 4, 3, 3,  3},
    {"i486SX/33",            CPU_i486SX,   33333333, 1.0,  0x42a,      0, 0x0000, CPU_SUPPORTS_DYNAREC,  6, 6, 3, 3,  4},
    {"i486SX2/50",           CPU_i486SX2,  50000000, 2.0,  0x45b,      0, 0x0000, CPU_SUPPORTS_DYNAREC,  8, 8, 6, 6,  6},
    {"i486SX2/66 (Q0569)",   CPU_i486SX2,  66666666, 2.0,  0x45b,      0, 0x0000, CPU_SUPPORTS_DYNAREC,  8, 8, 6, 6,  8},
    {"i486DX/25",            CPU_i486DX,   25000000, 1.0,  0x404,      0, 0x0000, CPU_SUPPORTS_DYNAREC,  4, 4, 3, 3,  3},
    {"i486DX/33",            CPU_i486DX,   33333333, 1.0,  0x414,      0, 0x0000, CPU_SUPPORTS_DYNAREC,  6, 6, 3, 3,  4},
    {"i486DX/50",            CPU_i486DX,   50000000, 1.0,  0x411,      0, 0x0000, CPU_SUPPORTS_DYNAREC,  8, 8, 4, 4,  6},
    {"i486DX2/40",           CPU_i486DX2,  40000000, 2.0,  0x430,  0x430, 0x0000, CPU_SUPPORTS_DYNAREC,  7, 7, 6, 6,  5},
    {"i486DX2/50",           CPU_i486DX2,  50000000, 2.0,  0x433,  0x433, 0x0000, CPU_SUPPORTS_DYNAREC,  8, 8, 6, 6,  6},
    {"i486DX2/66",           CPU_i486DX2,  66666666, 2.0,  0x435,  0x435, 0x0000, CPU_SUPPORTS_DYNAREC, 12,12, 6, 6,  8},
    {"iDX4/75",              CPU_iDX4,     75000000, 3.0,  0x480,  0x480, 0x0000, CPU_SUPPORTS_DYNAREC, 12,12, 9, 9,  9}, /*CPUID available on DX4, >= 75 MHz*/
    {"iDX4/100",             CPU_iDX4,    100000000, 3.0,  0x483,  0x483, 0x0000, CPU_SUPPORTS_DYNAREC, 18,18, 9, 9, 12}, /*Is on some real Intel DX2s, limit here is pretty arbitary*/
    {"iDX4 OverDrive 75",    CPU_iDX4,     75000000, 3.0, 0x1480, 0x1480, 0x0000, CPU_SUPPORTS_DYNAREC, 12,12, 9, 9,  9},
    {"iDX4 OverDrive 100",   CPU_iDX4,    100000000, 3.0, 0x1480, 0x1480, 0x0000, CPU_SUPPORTS_DYNAREC, 18,18, 9, 9, 12}, 
    {"Pentium OverDrive 63", CPU_PENTIUM,  62500000, 2.5, 0x1531, 0x1531, 0x0000, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10,7,7, 15/2},
    {"Pentium OverDrive 83", CPU_PENTIUM,  83333333, 2.5, 0x1532, 0x1532, 0x0000, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15,8,8, 10},
    {"",                     -1,                  0, 0,        0,      0, 0x0000,                    0,  0, 0, 0, 0,  0}
};

CPU cpus_Am486[] = {
    /*Am486/5x86*/
    {"Am486SX/33",   CPU_Am486SX,   33333333, 1.0, 0x42a,     0, 0, CPU_SUPPORTS_DYNAREC,  6, 6, 3, 3, 4},
    {"Am486SX/40",   CPU_Am486SX,   40000000, 1.0, 0x42a,     0, 0, CPU_SUPPORTS_DYNAREC,  7, 7, 3, 3, 5}, 
    {"Am486SX2/50",  CPU_Am486SX2,  50000000, 2.0, 0x45b, 0x45b, 0, CPU_SUPPORTS_DYNAREC,  8, 8, 6, 6, 6}, /*CPUID available on SX2, DX2, DX4, 5x86, >= 50 MHz*/
    {"Am486SX2/66",  CPU_Am486SX2,  66666666, 2.0, 0x45b, 0x45b, 0, CPU_SUPPORTS_DYNAREC, 12,12, 6, 6, 8},
    {"Am486DX/33",   CPU_Am486DX,   33333333, 1.0, 0x430,     0, 0, CPU_SUPPORTS_DYNAREC,  6, 6, 3, 3, 4},
    {"Am486DX/40",   CPU_Am486DX,   40000000, 1.0, 0x430,     0, 0, CPU_SUPPORTS_DYNAREC,  7, 7, 3, 3, 5},
    {"Am486DX2/50",  CPU_Am486DX2,  50000000, 2.0, 0x470, 0x470, 0, CPU_SUPPORTS_DYNAREC,  8, 8, 6, 6, 6},
    {"Am486DX2/66",  CPU_Am486DX2,  66666666, 2.0, 0x470, 0x470, 0, CPU_SUPPORTS_DYNAREC, 12,12, 6, 6, 8},
    {"Am486DX2/80",  CPU_Am486DX2,  80000000, 2.0, 0x470, 0x470, 0, CPU_SUPPORTS_DYNAREC, 14,14, 6, 6, 10},
    {"Am486DX4/75",  CPU_Am486DX4,  75000000, 3.0, 0x482, 0x482, 0, CPU_SUPPORTS_DYNAREC, 12,12, 9, 9, 9},
    {"Am486DX4/90",  CPU_Am486DX4,  90000000, 3.0, 0x482, 0x482, 0, CPU_SUPPORTS_DYNAREC, 15,15, 9, 9, 12},
    {"Am486DX4/100", CPU_Am486DX4, 100000000, 3.0, 0x482, 0x482, 0, CPU_SUPPORTS_DYNAREC, 15,15, 9, 9, 12},
    {"Am486DX4/120", CPU_Am486DX4, 120000000, 3.0, 0x482, 0x482, 0, CPU_SUPPORTS_DYNAREC, 21,21, 9, 9, 15},
    {"Am5x86/P75",   CPU_Am5x86,   133333333, 4.0, 0x4e0, 0x4e0, 0, CPU_SUPPORTS_DYNAREC, 24,24,12,12, 16},
    {"Am5x86/P75+",  CPU_Am5x86,   150000000, 3.0, 0x482, 0x482, 0, CPU_SUPPORTS_DYNAREC, 28,28,12,12, 20},/*The rare P75+ was indeed a triple-clocked 150 MHz according to research*/
    {"Am5x86/P90",   CPU_Am5x86,   160000000, 4.0, 0x4e0, 0x4e0, 0, CPU_SUPPORTS_DYNAREC, 28,28,12,12, 20},/*160 MHz on a 40 MHz bus was a common overclock and "5x86/P90" was used by a number of BIOSes to refer to that configuration*/ 
    {"",             -1,                   0, 0,     0,     0, 0,                    0,  0, 0, 0, 0,  0}
};

CPU cpus_Cx486[] = {
    /*Cyrix 486*/
    {"Cx486S/25",    CPU_Cx486S,    25000000, 1.0, 0x420, 0, 0x0010, CPU_SUPPORTS_DYNAREC,  4, 4, 3, 3,  3},
    {"Cx486S/33",    CPU_Cx486S,    33333333, 1.0, 0x420, 0, 0x0010, CPU_SUPPORTS_DYNAREC,  6, 6, 3, 3,  4},
    {"Cx486S/40",    CPU_Cx486S,    40000000, 1.0, 0x420, 0, 0x0010, CPU_SUPPORTS_DYNAREC,  7, 7, 3, 3,  5},
    {"Cx486DX/33",   CPU_Cx486DX,   33333333, 1.0, 0x430, 0, 0x051a, CPU_SUPPORTS_DYNAREC,  6, 6, 3, 3,  4},
    {"Cx486DX/40",   CPU_Cx486DX,   40000000, 1.0, 0x430, 0, 0x051a, CPU_SUPPORTS_DYNAREC,  7, 7, 3, 3,  5},
    {"Cx486DX2/50",  CPU_Cx486DX2,  50000000, 2.0, 0x430, 0, 0x081b, CPU_SUPPORTS_DYNAREC,  8, 8, 6, 6,  6},
    {"Cx486DX2/66",  CPU_Cx486DX2,  66666666, 2.0, 0x430, 0, 0x0b1b, CPU_SUPPORTS_DYNAREC, 12,12, 6, 6,  8},
    {"Cx486DX2/80",  CPU_Cx486DX2,  80000000, 2.0, 0x430, 0, 0x311b, CPU_SUPPORTS_DYNAREC, 14,14, 6, 6, 10},
    {"Cx486DX4/75",  CPU_Cx486DX4,  75000000, 3.0, 0x480, 0, 0x361f, CPU_SUPPORTS_DYNAREC, 12,12, 9, 9,  9},
    {"Cx486DX4/100", CPU_Cx486DX4, 100000000, 3.0, 0x480, 0, 0x361f, CPU_SUPPORTS_DYNAREC, 15,15, 9, 9, 12},

    /*Cyrix 5x86*/
    {"Cx5x86/80",    CPU_Cx5x86,    80000000, 2.0, 0x480, 0, 0x002f, CPU_SUPPORTS_DYNAREC, 14,14, 6, 6, 10}, /*If we're including the Pentium 50, might as well include this*/
    {"Cx5x86/100",   CPU_Cx5x86,   100000000, 3.0, 0x480, 0, 0x002f, CPU_SUPPORTS_DYNAREC, 15,15, 9, 9, 12},
    {"Cx5x86/120",   CPU_Cx5x86,   120000000, 3.0, 0x480, 0, 0x002f, CPU_SUPPORTS_DYNAREC, 21,21, 9, 9, 15},
    {"Cx5x86/133",   CPU_Cx5x86,   133333333, 4.0, 0x480, 0, 0x002f, CPU_SUPPORTS_DYNAREC, 24,24,12,12, 16},
    {"",             -1,                   0, 0,     0, 0,      0,                    0,  0, 0, 0, 0,  0}
};

#if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
CPU cpus_6x863V[] = {
    /*Cyrix 6x86*/
    {"Cx6x86/P90",     CPU_Cx6x86,    80000000, 2.0, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  8, 8, 6, 6, 10},
    {"Cx6x86/PR120+",  CPU_Cx6x86,   100000000, 2.0, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10, 6, 6, 12},
    {"Cx6x86/PR133+",  CPU_Cx6x86,   110000000, 2.0, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10, 6, 6, 14},
    {"Cx6x86/PR150+",  CPU_Cx6x86,   120000000, 2.0, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 14},
    {"Cx6x86/PR166+",  CPU_Cx6x86,   133333333, 2.0, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 16},
    {"Cx6x86/PR200+",  CPU_Cx6x86,   150000000, 2.0, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 18},
    {"",               -1,                   0, 0, 0,     0,     0,      0,  0, 0, 0, 0,  0}
};

CPU cpus_6x86[] = {
    /*Cyrix 6x86*/
    {"Cx6x86/P90",     CPU_Cx6x86,    80000000, 2.0, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  8, 8, 6, 6, 10},
    {"Cx6x86/PR120+",  CPU_Cx6x86,   100000000, 2.0, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10, 6, 6, 12},
    {"Cx6x86/PR133+",  CPU_Cx6x86,   110000000, 2.0, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10, 6, 6, 14},
    {"Cx6x86/PR150+",  CPU_Cx6x86,   120000000, 2.0, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 14},
    {"Cx6x86/PR166+",  CPU_Cx6x86,   133333333, 2.0, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 16},
    {"Cx6x86/PR200+",  CPU_Cx6x86,   150000000, 2.0, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 18},

    /*Cyrix 6x86L*/
    {"Cx6x86L/PR133+", CPU_Cx6x86L,  110000000, 2.0, 0x540, 0x540, 0x2231, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10, 6, 6, 14},
    {"Cx6x86L/PR150+", CPU_Cx6x86L,  120000000, 2.0, 0x540, 0x540, 0x2231, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 14},
    {"Cx6x86L/PR166+", CPU_Cx6x86L,  133333333, 2.0, 0x540, 0x540, 0x2231, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 16},
    {"Cx6x86L/PR200+", CPU_Cx6x86L,  150000000, 2.0, 0x540, 0x540, 0x2231, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 18},

    /*Cyrix 6x86MX/MII*/
    {"Cx6x86MX/PR166", CPU_Cx6x86MX, 133333333, 2.0, 0x600, 0x600, 0x0451, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6,   16},
    {"Cx6x86MX/PR200", CPU_Cx6x86MX, 166666666, 2.5, 0x600, 0x600, 0x0452, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7,   20},
    {"Cx6x86MX/PR233", CPU_Cx6x86MX, 187500000, 2.5, 0x600, 0x600, 0x0452, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 45/2},
    {"Cx6x86MX/PR266", CPU_Cx6x86MX, 208333333, 2.5, 0x600, 0x600, 0x0452, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 17,17, 7, 7,   25},
    {"MII/PR300",      CPU_Cx6x86MX, 233333333, 3.5, 0x601, 0x601, 0x0852, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,11,11,   28},
    {"MII/PR333",      CPU_Cx6x86MX, 250000000, 3.0, 0x601, 0x601, 0x0853, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 23,23, 9, 9,   30},
    {"",               -1,                   0, 0,   0,     0,     0,      0,  0, 0, 0, 0,  0}
 };

 CPU cpus_6x86SS7[] = {
    /*Cyrix 6x86*/
    {"Cx6x86/P90",     CPU_Cx6x86,    80000000, 2.0, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  8, 8, 6, 6, 10},
    {"Cx6x86/PR120+",  CPU_Cx6x86,   100000000, 2.0, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10, 6, 6, 12},
    {"Cx6x86/PR133+",  CPU_Cx6x86,   110000000, 2.0, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10, 6, 6, 14},
    {"Cx6x86/PR150+",  CPU_Cx6x86,   120000000, 2.0, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 14},
    {"Cx6x86/PR166+",  CPU_Cx6x86,   133333333, 2.0, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 16},
    {"Cx6x86/PR200+",  CPU_Cx6x86,   150000000, 2.0, 0x520, 0x520, 0x1731, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 18},

    /*Cyrix 6x86L*/
    {"Cx6x86L/PR133+", CPU_Cx6x86L,  110000000, 2.0, 0x540, 0x540, 0x2231, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10, 6, 6, 14},
    {"Cx6x86L/PR150+", CPU_Cx6x86L,  120000000, 2.0, 0x540, 0x540, 0x2231, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 14},
    {"Cx6x86L/PR166+", CPU_Cx6x86L,  133333333, 2.0, 0x540, 0x540, 0x2231, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 16},
    {"Cx6x86L/PR200+", CPU_Cx6x86L,  150000000, 2.0, 0x540, 0x540, 0x2231, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 18},

    /*Cyrix 6x86MX/MII*/
    {"Cx6x86MX/PR166", CPU_Cx6x86MX, 133333333, 2.0, 0x600, 0x600, 0x0451, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6,   16},
    {"Cx6x86MX/PR200", CPU_Cx6x86MX, 166666666, 2.5, 0x600, 0x600, 0x0452, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7,   20},
    {"Cx6x86MX/PR233", CPU_Cx6x86MX, 187500000, 2.5, 0x600, 0x600, 0x0452, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 45/2},
    {"Cx6x86MX/PR266", CPU_Cx6x86MX, 208333333, 2.5, 0x600, 0x600, 0x0452, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 17,17, 7, 7,   25},
    {"MII/PR300",      CPU_Cx6x86MX, 233333333, 3.5, 0x601, 0x601, 0x0852, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,11,11,   28},
    {"MII/PR333",      CPU_Cx6x86MX, 250000000, 3.0, 0x601, 0x601, 0x0853, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 23,23, 9, 9,   30},
    {"MII/PR366",      CPU_Cx6x86MX, 250000000, 2.5, 0x601, 0x601, 0x0853, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 23,23, 7, 7,   30},
    {"MII/PR400",      CPU_Cx6x86MX, 285000000, 3.0, 0x601, 0x601, 0x0853, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27, 9, 9,   34},
    {"MII/PR433",      CPU_Cx6x86MX, 300000000, 3.0, 0x601, 0x601, 0x0853, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27, 9, 9,   36},
    {"",               -1,                   0, 0,   0,     0,     0,      0,  0, 0, 0, 0,  0}
 };
#endif

CPU cpus_WinChip[] = {
    /*IDT WinChip*/
    {"WinChip 75",     CPU_WINCHIP,   75000000, 1.5, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC,  8,  8,  4,  4,  9},
    {"WinChip 90",     CPU_WINCHIP,   90000000, 1.5, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC,  9,  9,  4,  4, 21/2},
    {"WinChip 100",    CPU_WINCHIP,  100000000, 1.5, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC,  9,  9,  4,  4, 12},
    {"WinChip 120",    CPU_WINCHIP,  120000000, 2.0, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 12, 12,  6,  6, 14},
    {"WinChip 133",    CPU_WINCHIP,  133333333, 2.0, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 12, 12,  6,  6, 16},
    {"WinChip 150",    CPU_WINCHIP,  150000000, 2.5, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 15, 15,  7,  7, 35/2},
    {"WinChip 166",    CPU_WINCHIP,  166666666, 2.5, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 15, 15,  7,  7, 40},
    {"WinChip 180",    CPU_WINCHIP,  180000000, 3.0, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 18, 18,  9,  9, 21},
    {"WinChip 200",    CPU_WINCHIP,  200000000, 3.0, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 18, 18,  9,  9, 24},
    {"WinChip 225",    CPU_WINCHIP,  225000000, 3.0, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 18, 18,  9,  9, 27},
    {"WinChip 240",    CPU_WINCHIP,  240000000, 4.0, 0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 24, 24, 12, 12, 28},
    {"WinChip 2/200",  CPU_WINCHIP2, 200000000, 3.0, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC, 18, 18,  9,  9, 24},
    {"WinChip 2/225",  CPU_WINCHIP2, 225000000, 3.0, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC, 18, 18,  9,  9, 27},
    {"WinChip 2/240",  CPU_WINCHIP2, 240000000, 4.0, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC, 24, 24, 12, 12, 30},
    {"WinChip 2/250",  CPU_WINCHIP2, 250000000, 3.0, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC, 24, 24, 12, 12, 30},
    {"WinChip 2A/200", CPU_WINCHIP2, 200000000, 3.0, 0x587, 0x587, 0, CPU_SUPPORTS_DYNAREC, 18, 18,  9,  9, 24},
    {"WinChip 2A/233", CPU_WINCHIP2, 233333333, 3.5, 0x587, 0x587, 0, CPU_SUPPORTS_DYNAREC, 18, 18,  9,  9, (7*8)/2},
    {"",               -1,                   0, 0,   0,     0,     0, 0,                     0,  0,  0,  0,  0}
};

CPU cpus_WinChip_SS7[] = {
    /*IDT WinChip*/
    {"WinChip 75",     CPU_WINCHIP,   75000000, 1.5,     0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC,  8,  8,  4,  4,  9},
    {"WinChip 90",     CPU_WINCHIP,   90000000, 1.5,     0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC,  9,  9,  4,  4, 21/2},
    {"WinChip 100",    CPU_WINCHIP,  100000000, 1.5,     0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC,  9,  9,  4,  4, 12},
    {"WinChip 120",    CPU_WINCHIP,  120000000, 2.0,     0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 12, 12,  6,  6, 14},
    {"WinChip 133",    CPU_WINCHIP,  133333333, 2.0,     0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 12, 12,  6,  6, 16},
    {"WinChip 150",    CPU_WINCHIP,  150000000, 2.5,     0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 15, 15,  7,  7, 35/2},
    {"WinChip 166",    CPU_WINCHIP,  166666666, 2.5,     0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 15, 15,  7,  7, 40},
    {"WinChip 180",    CPU_WINCHIP,  180000000, 3.0,     0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 18, 18,  9,  9, 21},
    {"WinChip 200",    CPU_WINCHIP,  200000000, 3.0,     0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 18, 18,  9,  9, 24},
    {"WinChip 225",    CPU_WINCHIP,  225000000, 3.0,     0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 18, 18,  9,  9, 27},
    {"WinChip 240",    CPU_WINCHIP,  240000000, 4.0,     0x540, 0x540, 0, CPU_SUPPORTS_DYNAREC, 24, 24, 12, 12, 28},
    {"WinChip 2/200",  CPU_WINCHIP2, 200000000, 3.0,     0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC, 18, 18,  9,  9, 3*8},
    {"WinChip 2/225",  CPU_WINCHIP2, 225000000, 3.0,     0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC, 18, 18,  9,  9, 3*9},
    {"WinChip 2/240",  CPU_WINCHIP2, 240000000, 4.0,     0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC, 24, 24, 12, 12, 30},
    {"WinChip 2/250",  CPU_WINCHIP2, 250000000, 3.0,     0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC, 24, 24, 12, 12, 30},
    {"WinChip 2A/200", CPU_WINCHIP2, 200000000, 3.0,     0x587, 0x587, 0, CPU_SUPPORTS_DYNAREC, 18, 18,  9,  9, 3*8},
    {"WinChip 2A/233", CPU_WINCHIP2, 233333333, 3.5,     0x587, 0x587, 0, CPU_SUPPORTS_DYNAREC, 21, 21,  9,  9, (7*8)/2},
    {"WinChip 2A/266", CPU_WINCHIP2, 233333333, 7.0/3.0, 0x587, 0x587, 0, CPU_SUPPORTS_DYNAREC, 21, 21,  7,  7, 28},
    {"WinChip 2A/300", CPU_WINCHIP2, 250000000, 2.5,     0x587, 0x587, 0, CPU_SUPPORTS_DYNAREC, 24, 24,  8,  8, 30},
    {"",               -1,                   0, 0,           0,     0, 0, 0,                     0,  0,  0,  0,  0}
};

CPU cpus_Pentium5V[] = {
    /*Intel Pentium (5V, socket 4)*/
    {"Pentium 60",            CPU_PENTIUM,  60000000, 1, 0x517, 0x517, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6,3,3,  7},
    {"Pentium 66",            CPU_PENTIUM,  66666666, 1, 0x517, 0x517, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6,3,3,  8},
    {"Pentium OverDrive 120", CPU_PENTIUM, 120000000, 2, 0x51A, 0x51A, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12,6,6, 14},
    {"Pentium OverDrive 133", CPU_PENTIUM, 133333333, 2, 0x51A, 0x51A, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12,6,6, 16},
    {"",                               -1,         0, 0,     0,     0, 0, 0,  0, 0, 0, 0,  0}
};

CPU cpus_Pentium5V50[] = {
    /*Intel Pentium (5V, socket 4, including 50 MHz FSB)*/
    {"Pentium 50 (Q0399)",    CPU_PENTIUM,  50000000, 1, 0x513, 0x513, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  4, 4,3,3,  6},
    {"Pentium 60",            CPU_PENTIUM,  60000000, 1, 0x517, 0x517, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6,3,3,  7},
    {"Pentium 66",            CPU_PENTIUM,  66666666, 1, 0x517, 0x517, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6,3,3,  8},
    {"Pentium OverDrive 100", CPU_PENTIUM, 100000000, 2, 0x51A, 0x51A, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  8, 8,6,6, 12},
    {"Pentium OverDrive 120", CPU_PENTIUM, 120000000, 2, 0x51A, 0x51A, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12,6,6, 14},
    {"Pentium OverDrive 133", CPU_PENTIUM, 133333333, 2, 0x51A, 0x51A, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12,6,6, 16},
    {"",                               -1,         0, 0,     0,     0, 0, 0,  0, 0, 0, 0,  0}
};

CPU cpus_PentiumS5[] = {
    /*Intel Pentium (Socket 5)*/
    {"Pentium 75",                   CPU_PENTIUM,     75000000, 1.5,  0x522,  0x522, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7,4,4,  9},
    {"Pentium OverDrive MMX 75",     CPU_PENTIUMMMX,  75000000, 1.5, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7,4,4,  9},
    {"Pentium 90",                   CPU_PENTIUM,     90000000, 1.5,  0x524,  0x524, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9,4,4, 21/2},
    {"Pentium 100/50",               CPU_PENTIUM,    100000000, 2.0,  0x524,  0x524, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10,6,6, 12},
    {"Pentium 100/66",               CPU_PENTIUM,    100000000, 1.5,  0x526,  0x526, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9,4,4, 12},
    {"Pentium 120",                  CPU_PENTIUM,    120000000, 2.0,  0x526,  0x526, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12,6,6, 14},

    /*Intel Pentium OverDrive*/
    {"Pentium OverDrive 125",        CPU_PENTIUM,    125000000, 3.0,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12,7,7, 16},
    {"Pentium OverDrive 150",        CPU_PENTIUM,    150000000, 2.5,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15,7,7, 35/2},
    {"Pentium OverDrive 166",        CPU_PENTIUM,    166666666, 2.5,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15,7,7, 40},
    {"Pentium OverDrive MMX 125",    CPU_PENTIUMMMX, 125000000, 2.5, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12,7,7, 15},
    {"Pentium OverDrive MMX 150/60", CPU_PENTIUMMMX, 150000000, 2.5, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15,7,7, 35/2},
    {"Pentium OverDrive MMX 166",    CPU_PENTIUMMMX, 166000000, 2.5, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15,7,7, 20},
    {"Pentium OverDrive MMX 180",    CPU_PENTIUMMMX, 180000000, 3.0, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18,9,9, 21},
    {"Pentium OverDrive MMX 200",    CPU_PENTIUMMMX, 200000000, 3.0, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18,9,9, 24},
    {"",                             -1,                     0, 0.0,      0,      0, 0, 0,  0, 0, 0, 0,  0}
};

CPU cpus_Pentium3V[] = {
    /*Intel Pentium*/
    {"Pentium 75",                   CPU_PENTIUM,     75000000, 1.5,  0x524,  0x524, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7, 4, 4,  9},
    {"Pentium OverDrive MMX 75",     CPU_PENTIUMMMX,  75000000, 1.5, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7, 4, 4,  9},
    {"Pentium 90",                   CPU_PENTIUM,     90000000, 1.5,  0x524,  0x524, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9, 4, 4, 21/2},
    {"Pentium 100/50",               CPU_PENTIUM,    100000000, 2.0,  0x524,  0x524, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10, 6, 6, 12},
    {"Pentium 100/66",               CPU_PENTIUM,    100000000, 1.5,  0x526,  0x526, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9, 4, 4, 12},
    {"Pentium 120",                  CPU_PENTIUM,    120000000, 2.0,  0x526,  0x526, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 14},
    {"Pentium 133",                  CPU_PENTIUM,    133333333, 2.0,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 16},
    {"Pentium 150",                  CPU_PENTIUM,    150000000, 2.5,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 35/2},
    {"Pentium 166",                  CPU_PENTIUM,    166666666, 2.5,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20},
    {"Pentium 200",                  CPU_PENTIUM,    200000000, 3.0,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 24},

	/*Intel Pentium OverDrive*/
    {"Pentium OverDrive 125",        CPU_PENTIUM,    125000000, 2.5,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 7, 7, 15},
    {"Pentium OverDrive 150",        CPU_PENTIUM,    150000000, 2.5,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 35/2},
    {"Pentium OverDrive 166",        CPU_PENTIUM,    166666666, 2.5,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20},
    {"Pentium OverDrive MMX 125",    CPU_PENTIUMMMX, 125000000, 2.5, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 7, 7, 15},
    {"Pentium OverDrive MMX 150/60", CPU_PENTIUMMMX, 150000000, 2.5, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 35/2},
    {"Pentium OverDrive MMX 166",    CPU_PENTIUMMMX, 166000000, 2.5, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20},
    {"Pentium OverDrive MMX 180",    CPU_PENTIUMMMX, 180000000, 3.0, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 21},
    {"Pentium OverDrive MMX 200",    CPU_PENTIUMMMX, 200000000, 3.0, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 24},
    {"",                                         -1,         0, 0,         0,     0, 0, 0,  0, 0, 0, 0,  0}
};

CPU cpus_Pentium[] = {
    /*Intel Pentium*/
    {"Pentium 75",                   CPU_PENTIUM,     75000000, 1.5,  0x524,  0x524, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7, 4, 4,  9},
    {"Pentium OverDrive MMX 75",     CPU_PENTIUMMMX,  75000000, 1.5, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7, 4, 4,  9},
    {"Pentium 90",                   CPU_PENTIUM,     90000000, 1.5,  0x524,  0x524, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9, 4, 4, 21/2},
    {"Pentium 100/50",               CPU_PENTIUM,    100000000, 2.0,  0x524,  0x524, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10, 6, 6, 12},
    {"Pentium 100/66",               CPU_PENTIUM,    100000000, 1.5,  0x526,  0x526, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9, 4, 4, 12},
    {"Pentium 120",                  CPU_PENTIUM,    120000000, 2.0,  0x526,  0x526, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 14},
    {"Pentium 133",                  CPU_PENTIUM,    133333333, 2.0,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 16},
    {"Pentium 150",                  CPU_PENTIUM,    150000000, 2.5,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 35/2},
    {"Pentium 166",                  CPU_PENTIUM,    166666666, 2.5,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20},
    {"Pentium 200",                  CPU_PENTIUM,    200000000, 3.0,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 24},

	/*Intel Pentium MMX*/
    {"Pentium MMX 166",              CPU_PENTIUMMMX, 166666666, 2.5,  0x543,  0x543, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20},
    {"Pentium MMX 200",              CPU_PENTIUMMMX, 200000000, 3.0,  0x543,  0x543, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 24},
    {"Pentium MMX 233",              CPU_PENTIUMMMX, 233333333, 3.5,  0x543,  0x543, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,10,10, 28},

	/*Mobile Pentium*/
    {"Mobile Pentium MMX 120",       CPU_PENTIUMMMX, 120000000, 2.0,  0x543,  0x543, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 14},
    {"Mobile Pentium MMX 133",       CPU_PENTIUMMMX, 133333333, 2.0,  0x543,  0x543, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 16},
    {"Mobile Pentium MMX 150",       CPU_PENTIUMMMX, 150000000, 2.5,  0x544,  0x544, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 35/2},
    {"Mobile Pentium MMX 166",       CPU_PENTIUMMMX, 166666666, 2.5,  0x544,  0x544, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20},
    {"Mobile Pentium MMX 200",       CPU_PENTIUMMMX, 200000000, 3.0,  0x581,  0x581, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 24},
    {"Mobile Pentium MMX 233",       CPU_PENTIUMMMX, 233333333, 3.5,  0x581,  0x581, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,10,10, 28},
    {"Mobile Pentium MMX 266",       CPU_PENTIUMMMX, 266666666, 4.0,  0x582,  0x582, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 24,24,12,12, 32},
    {"Mobile Pentium MMX 300",       CPU_PENTIUMMMX, 300000000, 4.5,  0x582,  0x582, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27,13,13, 36},

	/*Intel Pentium OverDrive*/
    {"Pentium OverDrive 125",        CPU_PENTIUM,    125000000, 2.5,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 7, 7, 15},
    {"Pentium OverDrive 150",        CPU_PENTIUM,    150000000, 2.5,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 35/2},
    {"Pentium OverDrive 166",        CPU_PENTIUM,    166666666, 2.5,  0x52c,  0x52c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20},
    {"Pentium OverDrive MMX 125",    CPU_PENTIUMMMX, 125000000, 2.5, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 7, 7, 15},
    {"Pentium OverDrive MMX 150/60", CPU_PENTIUMMMX, 150000000, 2.5, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 35/2},
    {"Pentium OverDrive MMX 166",    CPU_PENTIUMMMX, 166000000, 2.5, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20},
    {"Pentium OverDrive MMX 180",    CPU_PENTIUMMMX, 180000000, 3.0, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 21},
    {"Pentium OverDrive MMX 200",    CPU_PENTIUMMMX, 200000000, 3.0, 0x1542, 0x1542, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 24},
    {"",                                         -1,         0, 0.0,      0,     0, 0, 0,  0, 0, 0, 0,  0}
};

#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
CPU cpus_K5[] = {
    /*AMD K5 (Socket 5)*/
    {"K5 (5k86) 75 (P75)",      CPU_K5,    75000000, 1.5, 0x500, 0x500, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7,4,4,  9},
    {"K5 (SSA/5) 75 (PR75)",    CPU_K5,    75000000, 1.5, 0x501, 0x501, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7,4,4,  9},
    {"K5 (5k86) 90 (P90)",      CPU_K5,    90000000, 1.5, 0x500, 0x500, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9,4,4, 21/2},
    {"K5 (SSA/5) 90 (PR90)",    CPU_K5,    90000000, 1.5, 0x501, 0x501, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9,4,4, 21/2},
    {"K5 (5k86) 100 (P100)",    CPU_K5,   100000000, 1.5, 0x500, 0x500, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9,4,4, 12},
    {"K5 (SSA/5) 100 (PR100)",  CPU_K5,   100000000, 1.5, 0x501, 0x501, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9,4,4, 12},
    {"K5 (5k86) 90 (PR120)",    CPU_5K86, 120000000, 2.0, 0x511, 0x511, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12,6,6, 14},
    {"K5 (5k86) 100 (PR133)",   CPU_5K86, 133333333, 2.0, 0x514, 0x514, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12,6,6, 16},
    {"K5 (5k86) 105 (PR150)",   CPU_5K86, 150000000, 2.5, 0x524, 0x524, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15,7,7, 35/2},
    {"K5 (5k86) 116.5 (PR166)", CPU_5K86, 166666666, 2.5, 0x524, 0x524, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15,7,7, 20},
    {"K5 (5k86) 133 (PR200)",   CPU_5K86, 200000000, 3.0, 0x534, 0x534, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18,9,9, 24},
    {"",                        -1,               0, 0.0, 0,     0,     0, 0,  0, 0, 0, 0,  0}
};
#endif

CPU cpus_K56[] = {
#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
    /*AMD K5 (Socket 7)*/
    {"K5 (5k86) 75 (P75)",      CPU_K5,    75000000, 1.5, 0x500, 0x500, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7, 4, 4,  9},
    {"K5 (SSA/5) 75 (PR75)",    CPU_K5,    75000000, 1.5, 0x501, 0x501, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7, 4, 4,  9},
    {"K5 (5k86) 90 (P90)",      CPU_K5,    90000000, 1.5, 0x500, 0x500, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9, 4, 4, 21/2},
    {"K5 (SSA/5) 90 (PR90)",    CPU_K5,    90000000, 1.5, 0x501, 0x501, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9, 4, 4, 21/2},
    {"K5 (5k86) 100 (P100)",    CPU_K5,   100000000, 1.5, 0x500, 0x500, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9, 4, 4, 12},
    {"K5 (SSA/5) 100 (PR100)",  CPU_K5,   100000000, 1.5, 0x501, 0x501, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9, 4, 4, 12},
    {"K5 (5k86) 90 (PR120)",    CPU_5K86, 120000000, 2.0, 0x511, 0x511, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 14},
    {"K5 (5k86) 100 (PR133)",   CPU_5K86, 133333333, 2.0, 0x514, 0x514, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6, 16},
    {"K5 (5k86) 105 (PR150)",   CPU_5K86, 150000000, 2.5, 0x524, 0x524, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 35/2},
    {"K5 (5k86) 116.5 (PR166)", CPU_5K86, 166666666, 2.5, 0x524, 0x524, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20},
    {"K5 (5k86) 133 (PR200)",   CPU_5K86, 200000000, 3.0, 0x534, 0x534, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 24},
#endif

    /*AMD K6 (Socket 7*/
    {"K6 (Model 6) 166",        CPU_K6,   166666666, 2.5, 0x561, 0x561, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15,   7,   7, 20},
    {"K6 (Model 6) 200",        CPU_K6,   200000000, 3.0, 0x561, 0x561, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18,   9,   9, 24},
    {"K6 (Model 6) 233",        CPU_K6,   233333333, 3.5, 0x561, 0x561, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,  10,  10, 28},
    {"K6 (Model 7) 200",        CPU_K6,   200000000, 3.0, 0x570, 0x570, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18,   9,   9, 24},
    {"K6 (Model 7) 233",        CPU_K6,   233333333, 3.5, 0x570, 0x570, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,  10,  10, 28},
    {"K6 (Model 7) 266",        CPU_K6,   266666666, 4.0, 0x570, 0x570, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 24,24,  12,  12, 32},
    {"K6 (Model 7) 300",        CPU_K6,   300000000, 4.5, 0x570, 0x570, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27,  13,  13, 36},

    /*AMD K6-2 (Socket 7)*/
    {"K6-2/233",                CPU_K6_2, 233333333, 3.5, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,  10,  10, 28},
    {"K6-2/266",                CPU_K6_2, 266666666, 4.0, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 24,24,  12,  12, 32},
    {"K6-2/300 AFR-66",         CPU_K6_2, 300000000, 4.5, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27,  13,  13, 36},
    {"K6-2/366",                CPU_K6_2, 366666666, 5.5, 0x58c, 0x58c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 33,33,  17,  17, 44},
    {"",                              -1,         0, 0,    0,     0,     0, 0,  0, 0, 0, 0,  0}
};

CPU cpus_K56_SS7[] = {
#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
    /*AMD K5 (Socket 7)*/
    {"K5 (5k86) 75 (P75)",      CPU_K5,     75000000, 1.5, 0x500, 0x500, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7, 4, 4,    9},
    {"K5 (SSA/5) 75 (PR75)",    CPU_K5,     75000000, 1.5, 0x501, 0x501, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7, 4, 4,    9},
    {"K5 (5k86) 90 (P90)",      CPU_K5,     90000000, 1.5, 0x500, 0x500, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9, 4, 4, 21/2},
    {"K5 (SSA/5) 90 (PR90)",    CPU_K5,     90000000, 1.5, 0x501, 0x501, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9, 4, 4, 21/2},
    {"K5 (5k86) 100 (P100)",    CPU_K5,    100000000, 1.5, 0x500, 0x500, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9, 4, 4,   12},
    {"K5 (SSA/5) 100 (PR100)",  CPU_K5,    100000000, 1.5, 0x501, 0x501, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  9, 9, 4, 4,   12},
    {"K5 (5k86) 90 (PR120)",    CPU_5K86,  120000000, 2.0, 0x511, 0x511, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6,   14},
    {"K5 (5k86) 100 (PR133)",   CPU_5K86,  133333333, 2.0, 0x514, 0x514, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 12,12, 6, 6,   16},
    {"K5 (5k86) 105 (PR150)",   CPU_5K86,  150000000, 2.5, 0x524, 0x524, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 35/2},
    {"K5 (5k86) 116.5 (PR166)", CPU_5K86,  166666666, 2.5, 0x524, 0x524, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7,   20},
    {"K5 (5k86) 133 (PR200)",   CPU_5K86,  200000000, 3.0, 0x534, 0x534, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9,   24},
#endif

    /*AMD K6 (Socket 7)*/
    {"K6 (Model 6) 166",        CPU_K6,    166666666, 2.5, 0x561, 0x561, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20},
    {"K6 (Model 6) 200",        CPU_K6,    200000000, 3.0, 0x561, 0x561, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 24},
    {"K6 (Model 6) 233",        CPU_K6,    233333333, 3.5, 0x561, 0x561, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,10,10, 28},
    {"K6 (Model 7) 200",        CPU_K6,    200000000, 3.0, 0x570, 0x570, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 24},
    {"K6 (Model 7) 233",        CPU_K6,    233333333, 3.5, 0x570, 0x570, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,10,10, 28},
    {"K6 (Model 7) 266",        CPU_K6,    266666666, 4.0, 0x570, 0x570, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 24,24,12,12, 32},
    {"K6 (Model 7) 300",        CPU_K6,    300000000, 4.5, 0x570, 0x570, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27,13,13, 36},

    /*AMD K6-2 (Socket 7/Super Socket 7)*/
    {"K6-2/233",                CPU_K6_2,  233333333, 3.5, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    21,   21,  10,  10, 28},
    {"K6-2/266",                CPU_K6_2,  266666666, 4.0, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    24,   24,  12,  12, 32},
    {"K6-2/300",                CPU_K6_2,  300000000, 3.0, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    27,   27,   9,   9, 36},
    {"K6-2/333",                CPU_K6_2,  332500000, 3.5, 0x580, 0x580, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    30,   30,  11,  11, 40},
    {"K6-2/350",                CPU_K6_2C, 350000000, 3.5, 0x58c, 0x58c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    32,   32,  11,  11, 42},
    {"K6-2/366",                CPU_K6_2C, 366666666, 5.5, 0x58c, 0x58c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    33,   33,  17,  17, 44},
    {"K6-2/380",                CPU_K6_2C, 380000000, 4.0, 0x58c, 0x58c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    34,   34,  12,  12, 46},
    {"K6-2/400",                CPU_K6_2C, 400000000, 4.0, 0x58c, 0x58c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    36,   36,  12,  12, 48},
    {"K6-2/450",                CPU_K6_2C, 450000000, 4.5, 0x58c, 0x58c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    41,   41,  14,  14, 54},
    {"K6-2/475",                CPU_K6_2C, 475000000, 5.0, 0x58c, 0x58c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    43,   43,  15,  15, 57},
    {"K6-2/500",                CPU_K6_2C, 500000000, 5.0, 0x58c, 0x58c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    45,   45,  15,  15, 60},
    {"K6-2/533",                CPU_K6_2C, 533333333, 5.5, 0x58c, 0x58c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    48,   48,  17,  17, 64},
    {"K6-2/550",                CPU_K6_2C, 550000000, 5.5, 0x58c, 0x58c, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    50,   50,  17,  17, 66},

    /*AMD K6-2+/K6-3/K6-3+ (Super Socket 7)*/
    {"K6-2+/450",               CPU_K6_2P, 450000000, 4.5, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    41,   41,  14,  14, 54},
    {"K6-2+/475",               CPU_K6_2P, 475000000, 5.0, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    43,   43,  15,  15, 57},
    {"K6-2+/500",               CPU_K6_2P, 500000000, 5.0, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    45,   45,  15,  15, 60},
    {"K6-2+/533",               CPU_K6_2P, 533333333, 5.5, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    48,   48,  17,  17, 64},
    {"K6-2+/550",               CPU_K6_2P, 550000000, 5.5, 0x5d4, 0x5d4, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    50,   50,  17,  17, 66},
    {"K6-III/400",              CPU_K6_3,  400000000, 4.0, 0x591, 0x591, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    36,   36,  12,  12, 48},
    {"K6-III/450",              CPU_K6_3,  450000000, 4.5, 0x591, 0x591, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    41,   41,  14,  14, 54},
    {"K6-III+/75",              CPU_K6_3P,  75000000, 1.5, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,     7,    7,   4,   4,  9},
    {"K6-III+/400",             CPU_K6_3P, 400000000, 4.0, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    36,   36,  12,  12, 48},
    {"K6-III+/450",             CPU_K6_3P, 450000000, 4.5, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    41,   41,  14,  14, 54},
    {"K6-III+/475",             CPU_K6_3P, 475000000, 5.0, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    43,   43,  15,  15, 57},
    {"K6-III+/500",             CPU_K6_3P, 500000000, 5.0, 0x5d0, 0x5d0, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,    45,   45,  15,  15, 60},
    {"",                               -1,         0, 0,        0,     0, 0, 0,  0, 0, 0, 0,  0}
};

CPU cpus_PentiumPro[] = {
    /*Intel Pentium Pro*/
    {"Pentium Pro 50",              CPU_PENTIUMPRO,  50000000, 1.0,  0x612,  0x612, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  4, 4, 3, 3, 6},
    {"Pentium Pro 60" ,             CPU_PENTIUMPRO,  60000000, 1.0,  0x612,  0x612, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 7},
    {"Pentium Pro 66" ,             CPU_PENTIUMPRO,  66666666, 1.0,  0x612,  0x612, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 8},
    {"Pentium Pro 75",              CPU_PENTIUMPRO,  75000000, 1.5,  0x612,  0x612, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7, 4, 4, 9},
    {"Pentium Pro 150",             CPU_PENTIUMPRO, 150000000, 2.5,  0x612,  0x612, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 35/2},
    {"Pentium Pro 166",             CPU_PENTIUMPRO, 166666666, 2.5,  0x617,  0x617, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 15,15, 7, 7, 20},
    {"Pentium Pro 180",             CPU_PENTIUMPRO, 180000000, 3.0,  0x617,  0x617, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 21},
    {"Pentium Pro 200",             CPU_PENTIUMPRO, 200000000, 3.0,  0x617,  0x617, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 18,18, 9, 9, 24},

    /*Intel Pentium II OverDrive*/
    {"Pentium II Overdrive 50",     CPU_PENTIUM2D,   50000000, 1.0, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  4, 4, 3, 3, 6},
    {"Pentium II Overdrive 60",     CPU_PENTIUM2D,   60000000, 1.0, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 7},
    {"Pentium II Overdrive 66",     CPU_PENTIUM2D,   66666666, 1.0, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 8},
    {"Pentium II Overdrive 75",     CPU_PENTIUM2D,   75000000, 1.5, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7, 4, 4, 9},
    {"Pentium II Overdrive 210",    CPU_PENTIUM2D,  210000000, 3.5, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 17,17, 7, 7, 25},
    {"Pentium II Overdrive 233",    CPU_PENTIUM2D,  233333333, 3.5, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,10,10, 28},
    {"Pentium II Overdrive 240",    CPU_PENTIUM2D,  240000000, 4.0, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 24,24,12,12, 29},
    {"Pentium II Overdrive 266",    CPU_PENTIUM2D,  266666666, 4.0, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 24,24,12,12, 32},
    {"Pentium II Overdrive 270",    CPU_PENTIUM2D,  270000000, 4.5, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 25,25,12,12, 33},
    {"Pentium II Overdrive 300/66", CPU_PENTIUM2D,  300000000, 4.5, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 25,25,12,12, 36},
    {"Pentium II Overdrive 300/60", CPU_PENTIUM2D,  300000000, 5.0, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27,13,13, 36},
    {"Pentium II Overdrive 333",    CPU_PENTIUM2D,  333333333, 5.0, 0x1632, 0x1632, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27,13,13, 40},
    {"",                                       -1,          0, 0,   0,      0,      0, 0,  0, 0, 0, 0,  0}
};
CPU cpus_PentiumII_28v[] = {
    /*Intel Pentium II Klamath*/
    {"Pentium II Klamath 50",        CPU_PENTIUM2,   50000000, 1.0,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  4, 4, 3, 3, 6},
    {"Pentium II Klamath 60",        CPU_PENTIUM2,   60000000, 1.0,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 7},
    {"Pentium II Klamath 66",        CPU_PENTIUM2,   66666666, 1.0,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 8},
    {"Pentium II Klamath 75",        CPU_PENTIUM2,   75000000, 1.5,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7, 4, 4, 9},
    {"Pentium II Klamath 233",       CPU_PENTIUM2,  233333333, 3.5,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,10,10, 28},
    {"Pentium II Klamath 266",       CPU_PENTIUM2,  266666666, 4.0,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 24,24,12,12, 32},
    {"Pentium II Klamath 300/66",    CPU_PENTIUM2,  300000000, 4.5,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 25,25,12,12, 36},
    {"",                                       -1,          0, 0,        0,      0, 0, 0,  0, 0, 0, 0,  0}

};

CPU cpus_PentiumII[] = {
    /*Intel Pentium II Klamath*/
    {"Pentium II Klamath 50",        CPU_PENTIUM2,   50000000, 1.0,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  4, 4, 3, 3, 6},
    {"Pentium II Klamath 60",        CPU_PENTIUM2,   60000000, 1.0,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 7},
    {"Pentium II Klamath 66",        CPU_PENTIUM2,   66666666, 1.0,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 8},
    {"Pentium II Klamath 75",        CPU_PENTIUM2,   75000000, 1.5,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7, 4, 4, 9},
    {"Pentium II Klamath 233",       CPU_PENTIUM2,  233333333, 3.5,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 21,21,10,10, 28},
    {"Pentium II Klamath 266",       CPU_PENTIUM2,  266666666, 4.0,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 24,24,12,12, 32},
    {"Pentium II Klamath 300/66",    CPU_PENTIUM2,  300000000, 4.5,  0x634,  0x634, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 25,25,12,12, 36},
    
    /*Intel Celeron Covington*/
    {"Celeron Covington 66",        CPU_PENTIUM2D,   66666666, 1.0,  0x651,  0x651, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 8},
    {"Celeron Covington 75",        CPU_PENTIUM2D,   75000000, 1.5,  0x651,  0x651, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7, 4, 4, 9},
    {"Celeron Covington 266",       CPU_PENTIUM2D,  266666666, 4.0,  0x651,  0x651, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 24,24,12,12, 32},
    {"Celeron Covington 300/66",    CPU_PENTIUM2D,  300000000, 4.5,  0x650,  0x650, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 25,25,12,12, 36},

    /*Intel Pentium II Deschutes*/
    {"Pentium II Deschutes 50",     CPU_PENTIUM2D,   50000000, 1.0,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  4, 4, 3, 3, 6},
    {"Pentium II Deschutes 60",     CPU_PENTIUM2D,   60000000, 1.0,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 7},
    {"Pentium II Deschutes 66",     CPU_PENTIUM2D,   66666666, 1.0,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 8},
    {"Pentium II Deschutes 75",     CPU_PENTIUM2D,   75000000, 1.5,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  7, 7, 4, 4, 9},
    {"Pentium II Deschutes 266",    CPU_PENTIUM2D,  266666666, 4.0,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 24,24,12,12, 32},
    {"Pentium II Deschutes 300/66", CPU_PENTIUM2D,  300000000, 4.5,  0x651,  0x651, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 25,25,12,12, 36},
    {"Pentium II Deschutes 333",    CPU_PENTIUM2D,  333333333, 5.0,  0x651,  0x651, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27,13,13, 40},
    {"Pentium II Deschutes 350",    CPU_PENTIUM2D,  350000000, 3.5,  0x651,  0x651, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 32,32,11,11, 42},
    {"Pentium II Deschutes 400",    CPU_PENTIUM2D,  400000000, 4.0,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 36,36,12,12, 48},
    {"Pentium II Deschutes 450",    CPU_PENTIUM2D,  450000000, 4.5,  0x652,  0x652, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 41,41,14,14, 54},
    {"",                                       -1,          0, 0,        0,      0, 0, 0,  0, 0, 0, 0,  0}

};

CPU cpus_Xeon[] = {
	/* Slot 2 Xeons. Literal P2D's with more cache */
    {"Pentium II Xeon 100",    CPU_PENTIUM2D,  100000000, 1.5,  0x653,  0x653, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10, 6, 6, 12},
    {"Pentium II Xeon 400",    CPU_PENTIUM2D,  400000000, 4.0,  0x653,  0x653, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 36,36,12,12, 48},
    {"Pentium II Xeon 450",    CPU_PENTIUM2D,  450000000, 4.5,  0x653,  0x653, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 41,41,14,14, 54},
    {"",                                       -1,          0,   0,      0,      0, 0, 0,  0, 0, 0, 0,  0}	
};

CPU cpus_Celeron[] = {
    /* Mendocino Celerons. Exact architecture as the P2D series with their L2 cache on-dye.
       Intended for the PGA370 boards but they were capable to fit on a PGA 370 to Slot 1
       adaptor card so they work on Slot 1 and Slot 2 motherboards too!.

       The 66Mhz & 100Mhz Mendocinos are only meant to not cause any struggle
       to the recompiler. */
    {"Celeron Mendocino 66",        CPU_PENTIUM2D,   66666666, 1.0,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC,  6, 6, 3, 3, 8},
    {"Celeron Mendocino 100",       CPU_PENTIUM2D,  100000000, 1.5,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 10,10, 6, 6, 12},
    {"Celeron Mendocino 300/66",    CPU_PENTIUM2D,  300000000, 4.5,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 25,25,12,12, 36},
    {"Celeron Mendocino 333",       CPU_PENTIUM2D,  333333333, 5.0,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 27,27,13,13, 40},
    {"Celeron Mendocino 366",       CPU_PENTIUM2D,  366666666, 5.5,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 33,33,17,17, 44},
    {"Celeron Mendocino 400",       CPU_PENTIUM2D,  400000000, 6.0,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 36,36,12,12, 48},
    {"Celeron Mendocino 433",       CPU_PENTIUM2D,  433333333, 6.5,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 39,39,13,13, 51},
    {"Celeron Mendocino 466",       CPU_PENTIUM2D,  466666666, 7.0,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 43,43,15,15, 57},
    {"Celeron Mendocino 500",       CPU_PENTIUM2D,  500000000, 7.5,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 45,45,15,15, 60},
    {"Celeron Mendocino 533",       CPU_PENTIUM2D,  533333333, 8.0,  0x665,  0x665, 0, CPU_SUPPORTS_DYNAREC | CPU_REQUIRES_DYNAREC, 48,48,17,17, 64},
    {"",                                       -1,          0,   0,      0,      0, 0, 0,  0, 0, 0, 0,  0}	
};

CPU cpus_Cyrix3[] = {
    /*VIA Cyrix III (Samuel)*/
    {"Cyrix III 66",  CPU_CYRIX3S,  66666666, 1.0,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC,  6,  6,  3,  3,  8}, /*66 MHz version*/
    {"Cyrix III 233", CPU_CYRIX3S, 233333333, 3.5,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC, 21, 21,  9,  9, 28},
    {"Cyrix III 266", CPU_CYRIX3S, 266666666, 4.0,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC, 24, 24, 12, 12, 32},
    {"Cyrix III 300", CPU_CYRIX3S, 300000000, 4.5,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC, 27, 27, 13, 13, 36},
    {"Cyrix III 333", CPU_CYRIX3S, 333333333, 5.0,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC, 30, 30, 15, 15, 40},
    {"Cyrix III 350", CPU_CYRIX3S, 350000000, 3.5,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC, 32, 32, 11, 11, 42},
    {"Cyrix III 400", CPU_CYRIX3S, 400000000, 4.0,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC, 36, 36, 12, 12, 48},
    {"Cyrix III 450", CPU_CYRIX3S, 450000000, 4.5,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC, 41, 41, 14, 14, 54}, /*^ is lower P2 speeds to allow emulation below 466 mhz*/
    {"Cyrix III 500", CPU_CYRIX3S, 500000000, 5.0,   0x660, 0x660, 0, CPU_SUPPORTS_DYNAREC, 45, 45, 15, 15, 60},
    {"Cyrix III 550", CPU_CYRIX3S, 550000000, 5.5,   0x662, 0x662, 0, CPU_SUPPORTS_DYNAREC, 50, 50, 17, 17, 66},
    {"Cyrix III 600", CPU_CYRIX3S, 600000000, 6.0,   0x662, 0x662, 0, CPU_SUPPORTS_DYNAREC, 54, 54, 18, 18, 72},
    {"Cyrix III 650", CPU_CYRIX3S, 650000000, 6.5,   0x662, 0x662, 0, CPU_SUPPORTS_DYNAREC, 58, 58, 20, 20, 78},
    {"Cyrix III 700", CPU_CYRIX3S, 700000000, 7.0,   0x662, 0x662, 0, CPU_SUPPORTS_DYNAREC, 62, 62, 21, 21, 84},
    {"",                       -1,         0, 0.0,       0,     0, 0, 0,                    0,   0, 0,  0,  0}
};
