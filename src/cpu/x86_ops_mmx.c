#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#ifndef INFINITY
#    define INFINITY (__builtin_inff())
#endif
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include "x86.h"
#include "x87_sf.h"
#include "x87.h"
#include <86box/nmi.h>
#include <86box/mem.h>
#include <86box/smram.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/keyboard.h>
#include <86box/timer.h>
#include "x86seg_common.h"
#include "x86seg.h"
#include "386_common.h"
#include "x86_flags.h"

MMX_REG  *MMP[8];
uint16_t *MMEP[8];

static uint16_t MME[8];

#define MMX_GETREGP(r) fpu_softfloat ? ((MMX_REG *) &fpu_state.st_space[r].signif) : &(cpu_state.MM[r])
void
mmx_init(void)
{
    memset(MME, 0xff, sizeof(MME));

    for (uint8_t i = 0; i < 8; i++) {
        if (fpu_softfloat) {
            MMP[i]  = (MMX_REG *) &fpu_state.st_space[i].signif;
            MMEP[i] = (uint16_t *) &fpu_state.st_space[i].signExp;
        } else {
            MMP[i]  = &(cpu_state.MM[i]);
            MMEP[i] = &(MME[i]);
        }
    }
}
