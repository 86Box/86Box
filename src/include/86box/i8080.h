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
        uint8_t a, flags;
    };
    union
    {
        uint16_t bc;
        uint8_t b, c;
    };
    union
    {
        uint16_t de;
        uint8_t d, e;
    };
    union
    {
        uint16_t hl;
        uint8_t h, l;
    };
    uint16_t pc, sp;
} i8080;