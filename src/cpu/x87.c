#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define fplog 0
#include <math.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/pic.h>
#include "x86.h"
#include "x86_flags.h"
#include "x86_ops.h"
#include "x87.h"
#include "386_common.h"


uint32_t x87_pc_off,x87_op_off;
uint16_t x87_pc_seg,x87_op_seg;


#ifdef ENABLE_FPU_LOG
int fpu_do_log = ENABLE_FPU_LOG;


void
fpu_log(const char *fmt, ...)
{
    va_list ap;

    if (fpu_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define fpu_log(fmt, ...)
#endif


#define X87_TAG_VALID   0
#define X87_TAG_ZERO    1
#define X87_TAG_INVALID 2
#define X87_TAG_EMPTY   3

#ifdef USE_NEW_DYNAREC
uint16_t x87_gettag()
{
        uint16_t ret = 0;
        int c;

        for (c = 0; c < 8; c++)
        {
                if (cpu_state.tag[c] == TAG_EMPTY)
                        ret |= X87_TAG_EMPTY << (c * 2);
                else if (cpu_state.tag[c] & TAG_UINT64)
                        ret |= 2 << (c*2);
                else if (cpu_state.ST[c] == 0.0 && !cpu_state.ismmx)
                        ret |= X87_TAG_ZERO << (c * 2);
                else
                        ret |= X87_TAG_VALID << (c * 2);
        }

        return ret;
}

void x87_settag(uint16_t new_tag)
{
        int c;

        for (c = 0; c < 8; c++)
        {
                int tag = (new_tag >> (c * 2)) & 3;

                if (tag == X87_TAG_EMPTY)
                        cpu_state.tag[c] = TAG_EMPTY;
                else if (tag == 2)
                        cpu_state.tag[c] = TAG_VALID | TAG_UINT64;
                else
                        cpu_state.tag[c] = TAG_VALID;
        }
}
#else
uint16_t x87_gettag()
{
        uint16_t ret = 0;
        int c;

        for (c = 0; c < 8; c++)
        {
                if (cpu_state.tag[c] & TAG_UINT64)
                        ret |= 2 << (c*2);
                else
                        ret |= (cpu_state.tag[c] << (c*2));
        }

        return ret;
}

void x87_settag(uint16_t new_tag)
{
        cpu_state.tag[0] = new_tag & 3;
        cpu_state.tag[1] = (new_tag >> 2) & 3;
        cpu_state.tag[2] = (new_tag >> 4) & 3;
        cpu_state.tag[3] = (new_tag >> 6) & 3;
        cpu_state.tag[4] = (new_tag >> 8) & 3;
        cpu_state.tag[5] = (new_tag >> 10) & 3;
        cpu_state.tag[6] = (new_tag >> 12) & 3;
        cpu_state.tag[7] = (new_tag >> 14) & 3;
}
#endif


#ifdef ENABLE_808X_LOG
void x87_dumpregs()
{
        if (cpu_state.ismmx)
        {
                fpu_log("MM0=%016llX\tMM1=%016llX\tMM2=%016llX\tMM3=%016llX\n", cpu_state.MM[0].q, cpu_state.MM[1].q, cpu_state.MM[2].q, cpu_state.MM[3].q);
                fpu_log("MM4=%016llX\tMM5=%016llX\tMM6=%016llX\tMM7=%016llX\n", cpu_state.MM[4].q, cpu_state.MM[5].q, cpu_state.MM[6].q, cpu_state.MM[7].q);
        }
        else
        {
                fpu_log("ST(0)=%f\tST(1)=%f\tST(2)=%f\tST(3)=%f\t\n",cpu_state.ST[cpu_state.TOP],cpu_state.ST[(cpu_state.TOP+1)&7],cpu_state.ST[(cpu_state.TOP+2)&7],cpu_state.ST[(cpu_state.TOP+3)&7]);
                fpu_log("ST(4)=%f\tST(5)=%f\tST(6)=%f\tST(7)=%f\t\n",cpu_state.ST[(cpu_state.TOP+4)&7],cpu_state.ST[(cpu_state.TOP+5)&7],cpu_state.ST[(cpu_state.TOP+6)&7],cpu_state.ST[(cpu_state.TOP+7)&7]);
        }
        fpu_log("Status = %04X  Control = %04X  Tag = %04X\n", cpu_state.npxs, cpu_state.npxc, x87_gettag());
}
#endif
