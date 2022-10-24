/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		8080 CPU emulation (header).
 *
 * Authors:	Cacodemon345
 *
 *		Copyright 2022 Cacodemon345
 */

#include <stdint.h>

typedef struct i8080
{
    union { 
        uint16_t af; /* Intended in case we also go for μPD9002 emulation, which also has a Z80 emulation mode. */
        struct { uint8_t a, flags; };
    };
    union
    {
        uint16_t bc;
        struct { uint8_t b, c; };
    };
    union
    {
        uint16_t de;
        struct { uint8_t d, e; };
    };
    union
    {
        uint16_t hl;
        struct { uint8_t h, l; };
    };
    uint16_t pc, sp;
    uint16_t oldpc, ei;
    uint32_t pmembase, dmembase; /* Base from where i8080 starts. */
    uint8_t emulated; /* 0 = not emulated, use separate registers, 1 = emulated, use x86 registers. */
    uint16_t* cpu_flags;
    void (*writemembyte)(uint32_t, uint8_t);
    uint8_t (*readmembyte)(uint32_t);
    void (*startclock)();
    void (*endclock)();
    void (*checkinterrupts)();
    uint8_t (*fetchinstruction)();
} i8080;

#define C_FLAG_I8080 (1 << 0)
#define P_FLAG_I8080 (1 << 2)
#define AC_FLAG_I8080 (1 << 4)
#define Z_FLAG_I8080 (1 << 6)
#define S_FLAG_I8080 (1 << 7)
