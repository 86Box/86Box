/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          808x CPU emulation, mostly ported from reenigne's XTCE, which
 *          is cycle-accurate.
 *
 * Authors: Andrew Jenner, <https://www.reenigne.org>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2015-2020 Andrew Jenner.
 *          Copyright 2016-2020 Miran Grca.
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "i8080.h"

#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include <86box/machine.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/ppi.h>
#include <86box/timer.h>
#include <86box/gdbstub.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>
#include "vx0_biu.h"

#define do_cycle()           wait(1)
#define do_cycle_no_modrm()  if (!nx)       \
                                 do_cycle()
#define do_cycle_i()         do_cycle()
#define do_cycles(c)         wait(c)
#define do_cycles_i(c)       do_cycles(c)
#define do_cycle_nx()        nx = 1
#define do_cycle_nx_i()      nx = 1
#define do_cycles_nx(c)      nx = 1;    \
                             if (c > 1) \
                                 do_cycles(c - 1)
#define do_cycles_nx_i(c)    nx = 1;    \
                             if (c > 1) \
                                 do_cycles(c - 1)
#define addr_mode_match()    cpu_mod == 3
#define math_op(o)           cpu_alu_op = o; \
                             alu_op(bits)

/* Various things needed for 8087. */
#define OP_TABLE(name) ops_##name

#define CPU_BLOCK_END()
#define SEG_CHECK_READ(seg)
#define SEG_CHECK_WRITE(seg)
#define CHECK_READ(a, b, c)
#define CHECK_WRITE(a, b, c)
#define UN_USED(x) (void) (x)
#define fetch_ea_16(val)
#define fetch_ea_32(val)
#define PREFETCH_RUN(a, b, c, d, e, f, g, h)

#define CYCLES(val)     \
    {                   \
        do_cycles(val); \
    }

#define CLOCK_CYCLES_ALWAYS(val) \
    {                            \
        do_cycles(val);          \
    }

#define CLOCK_CYCLES_FPU(val) \
        {                     \
            do_cycles(val);   \
        }

# define CLOCK_CYCLES(val)            \
        {                             \
            if (fpu_cycles > 0) {     \
                fpu_cycles -= (val);  \
                if (fpu_cycles < 0) { \
                    do_cycles(val);   \
                }                     \
            } else {                  \
                do_cycles(val);       \
            }                         \
        }

#define CONCURRENCY_CYCLES(c) fpu_cycles = (c)

#define OP_MRM        1
#define OP_EA         2
#define OP_MEA       (OP_MRM | OP_EA)
#define OP_GRP        4
#define OP_DELAY      8
#define OP_PRE       16

#define readmemb     readmemb_vx0
#define readmemw     readmemw_vx0
#define readmem      readmem_vx0
#define readmeml     readmeml_vx0
#define readmemq     readmemq_vx0
#define writememb    writememb_vx0
#define writememw    writememw_vx0
#define writemem     writemem_vx0
#define writememl    writememl_vx0
#define writememq    writememq_vx0

typedef int (*OpFn)(uint32_t fetchdat);

static void    farret(int far);

const uint8_t opf[256]     = { OP_MEA, OP_MEA, OP_MEA, OP_MEA,      0,      0,      0,      0,   /* 00 */
                               OP_MEA, OP_MEA, OP_MEA, OP_MEA,      0,      0,      0,      0,   /* 08 */
                               OP_MEA, OP_MEA, OP_MEA, OP_MEA,      0,      0,      0,      0,   /* 10 */
                               OP_MEA, OP_MEA, OP_MEA, OP_MEA,      0,      0,      0,      0,   /* 18 */
                               OP_MEA, OP_MEA, OP_MEA, OP_MEA,      0,      0, OP_PRE,      0,   /* 20 */
                               OP_MEA, OP_MEA, OP_MEA, OP_MEA,      0,      0, OP_PRE,      0,   /* 28 */
                               OP_MEA, OP_MEA, OP_MEA, OP_MEA,      0,      0, OP_PRE,      0,   /* 30 */
                               OP_MEA, OP_MEA, OP_MEA, OP_MEA,      0,      0, OP_PRE,      0,   /* 38 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 40 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 48 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 50 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 58 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 60 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 68 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 70 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 78 */
                               OP_GRP, OP_GRP, OP_GRP, OP_GRP, OP_MEA, OP_MEA, OP_MEA, OP_MEA,   /* 80 */
                               OP_MRM, OP_MRM, OP_MEA, OP_MEA, OP_MRM, OP_MRM, OP_MEA, OP_MRM,   /* 88 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 90 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 98 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* A0 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* A8 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* B0 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* B8 */
                                    0,      0,      0,      0, OP_MEA, OP_MEA, OP_MRM, OP_MRM,   /* C0 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* C8 */
                               OP_GRP, OP_GRP, OP_GRP, OP_GRP,      0,      0,      0,      0,   /* D0 */
                               OP_MRM, OP_MRM, OP_MRM, OP_MRM, OP_MRM, OP_MRM, OP_MRM, OP_MRM,   /* D8 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* E0 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* E8 */
                               OP_PRE, OP_PRE,      OP_PRE, OP_PRE, 0,      0, OP_GRP, OP_GRP,   /* F0 */
                                    0,      0,      0,      0,      0,      0, OP_GRP, OP_GRP }; /* F8 */

const uint8_t opf_nec[256] = { OP_MEA, OP_MEA, OP_MEA, OP_MEA,      0,      0,      0,      0,   /* 00 */
                               OP_MEA, OP_MEA, OP_MEA, OP_MEA,      0,      0,      0, OP_PRE,   /* 08 */
                               OP_MEA, OP_MEA, OP_MEA, OP_MEA,      0,      0,      0,      0,   /* 10 */
                               OP_MEA, OP_MEA, OP_MEA, OP_MEA,      0,      0,      0,      0,   /* 18 */
                               OP_MEA, OP_MEA, OP_MEA, OP_MEA,      0,      0, OP_PRE,      0,   /* 20 */
                               OP_MEA, OP_MEA, OP_MEA, OP_MEA,      0,      0, OP_PRE,      0,   /* 28 */
                               OP_MEA, OP_MEA, OP_MEA, OP_MEA,      0,      0, OP_PRE,      0,   /* 30 */
                               OP_MEA, OP_MEA, OP_MEA, OP_MEA,      0,      0, OP_PRE,      0,   /* 38 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 40 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 48 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 50 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 58 */
                                    0,      0, OP_MRM, OP_MRM, OP_PRE, OP_PRE, OP_MRM, OP_MRM,   /* 60 */
                                    0, OP_MRM,      0, OP_MEA,      0,      0,      0,      0,   /* 68 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 70 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 78 */
                               OP_GRP, OP_GRP, OP_GRP, OP_GRP, OP_MEA, OP_MEA, OP_MEA, OP_MEA,   /* 80 */
                               OP_MRM, OP_MRM, OP_MEA, OP_MEA, OP_MRM, OP_MRM, OP_MEA, OP_MRM,   /* 88 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 90 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 98 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* A0 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* A8 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* B0 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* B8 */
                               OP_GRP, OP_GRP,      0,      0, OP_MEA, OP_MEA, OP_MRM, OP_MRM,   /* C0 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* C8 */
                               OP_GRP, OP_GRP, OP_GRP, OP_GRP,      0,      0,      0,      0,   /* D0 */
                               OP_MRM, OP_MRM, OP_MRM, OP_MRM, OP_MRM, OP_MRM, OP_MRM, OP_MRM,   /* D8 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* E0 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* E8 */
                               OP_PRE, OP_PRE,      OP_PRE, OP_PRE, 0,      0, OP_GRP, OP_GRP,   /* F0 */
                                    0,      0,      0,      0,      0,      0, OP_GRP, OP_GRP }; /* F8 */

const uint8_t opf_0f[256]  = {      0,      0,      0,      0,      0,      0,      0,      0,   /* 00 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 08 */
                               OP_MRM, OP_MRM, OP_MRM, OP_MRM, OP_MRM, OP_MRM, OP_MRM, OP_MRM,   /* 10 */
                               OP_MRM, OP_MRM, OP_MRM, OP_MRM, OP_MRM, OP_MRM, OP_MRM, OP_MRM,   /* 18 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 20 */
                               OP_MRM,      0, OP_MRM,      0,      0,      0,      0,      0,   /* 28 */
                               OP_MRM,      0, OP_MRM,      0,      0,      0,      0,      0,   /* 30 */
                               OP_MRM,      0, OP_MRM,      0,      0,      0,      0,      0,   /* 38 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 40 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 48 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 50 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 58 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 60 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 68 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 70 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 78 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 80 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 88 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 90 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* 98 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* A0 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* A8 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* B0 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* B8 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* C0 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* C8 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* D0 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* D8 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* E0 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* E8 */
                                    0,      0,      0,      0,      0,      0,      0,      0,   /* F0 */
                                    0,      0,      0,      0,      0,      0,      0,      0 }; /* F8 */

uint8_t            use_custom_nmi_vector = 0;

uint32_t           custom_nmi_vector     = 0x00000000;

/* Is the CPU 8088 or 8086. */
int                is8086                = 0;
int                nx                    = 0;

static uint32_t    cpu_src               = 0;
static uint32_t    cpu_dest              = 0;

static uint32_t    cpu_data              = 0;

static int         oldc;
static int         cpu_alu_op;
static int         completed             = 1;
static int         in_rep                = 0;
static int         repeating             = 0;
static int         rep_c_flag            = 0;
static int         clear_lock            = 0;
static int         noint                 = 0;
static int         tempc_fpu             = 0;
static int         started               = 0;
static int         group_delay           = 0;
static int         modrm_loaded          = 0;
static int         in_0f                 = 0;
static int         in_hlt                = 0;
static int         retem                 = 0;
static int         halted                = 0;

static uint32_t *  ovr_seg               = NULL;

/* Pointer tables needed for segment overrides. */
static uint32_t *  opseg[4];

static x86seg   *  _opseg[4];

enum {
    MODRM_ADDR_BX_SI        = 0x00,
    MODRM_ADDR_BX_DI,
    MODRM_ADDR_BP_SI,
    MODRM_ADDR_BP_DI,
    MODRM_ADDR_SI,
    MODRM_ADDR_DI,
    MODRM_ADDR_DISP16,
    MODRM_ADDR_BX,

    MODRM_ADDR_BX_SI_DISP8  = 0x40,
    MODRM_ADDR_BX_DI_DISP8,
    MODRM_ADDR_BP_SI_DISP8,
    MODRM_ADDR_BP_DI_DISP8,
    MODRM_ADDR_SI_DISP8,
    MODRM_ADDR_DI_DISP8,
    MODRM_ADDR_BP_DISP8,
    MODRM_ADDR_BX_DISP8,

    MODRM_ADDR_BX_SI_DISP16 = 0x80,
    MODRM_ADDR_BX_DI_DISP16,
    MODRM_ADDR_BP_SI_DISP16,
    MODRM_ADDR_BP_DI_DISP16,
    MODRM_ADDR_SI_DISP16,
    MODRM_ADDR_DI_DISP16,
    MODRM_ADDR_BP_DISP16,
    MODRM_ADDR_BX_DISP16
};

static uint8_t     modrm_cycs_pre[256]   = { [MODRM_ADDR_BX_SI]        = 4,
                                             [MODRM_ADDR_BX_DI]        = 5,
                                             [MODRM_ADDR_BP_SI]        = 5,
                                             [MODRM_ADDR_BP_DI]        = 4,
                                             [MODRM_ADDR_SI]           = 2,
                                             [MODRM_ADDR_DI]           = 2,
                                             [MODRM_ADDR_DISP16]       = 0,
                                             [MODRM_ADDR_BX]           = 2,
                                             [0x08 ... 0x3f]           = 0,
                                             [MODRM_ADDR_BX_SI_DISP8]  = 4,
                                             [MODRM_ADDR_BX_DI_DISP8]  = 5,
                                             [MODRM_ADDR_BP_SI_DISP8]  = 5,
                                             [MODRM_ADDR_BP_DI_DISP8]  = 4,
                                             [MODRM_ADDR_SI_DISP8]     = 2,
                                             [MODRM_ADDR_DI_DISP8]     = 2,
                                             [MODRM_ADDR_BP_DISP8]     = 2,
                                             [MODRM_ADDR_BX_DISP8]     = 2,
                                             [0x48 ... 0x7f]           = 0,
                                             [MODRM_ADDR_BX_SI_DISP16] = 4,
                                             [MODRM_ADDR_BX_DI_DISP16] = 5,
                                             [MODRM_ADDR_BP_SI_DISP16] = 5,
                                             [MODRM_ADDR_BP_DI_DISP16] = 4,
                                             [MODRM_ADDR_SI_DISP16]    = 2,
                                             [MODRM_ADDR_DI_DISP16]    = 2,
                                             [MODRM_ADDR_BP_DISP16]    = 2,
                                             [MODRM_ADDR_BX_DISP16]    = 2,
                                             [0x88 ... 0xff]           = 0 };

static uint8_t     modrm_cycs_post[256]  = { [MODRM_ADDR_BX_SI]        = 0,
                                             [MODRM_ADDR_BX_DI]        = 0,
                                             [MODRM_ADDR_BP_SI]        = 0,
                                             [MODRM_ADDR_BP_DI]        = 0,
                                             [MODRM_ADDR_SI]           = 0,
                                             [MODRM_ADDR_DI]           = 0,
                                             [MODRM_ADDR_DISP16]       = 1,
                                             [MODRM_ADDR_BX]           = 0,
                                             [0x08 ... 0x3f]           = 0,
                                             [MODRM_ADDR_BX_SI_DISP8]  = 3,
                                             [MODRM_ADDR_BX_DI_DISP8]  = 3,
                                             [MODRM_ADDR_BP_SI_DISP8]  = 3,
                                             [MODRM_ADDR_BP_DI_DISP8]  = 3,
                                             [MODRM_ADDR_SI_DISP8]     = 3,
                                             [MODRM_ADDR_DI_DISP8]     = 3,
                                             [MODRM_ADDR_BP_DISP8]     = 3,
                                             [MODRM_ADDR_BX_DISP8]     = 3,
                                             [0x48 ... 0x7f]           = 0,
                                             [MODRM_ADDR_BX_SI_DISP16] = 2,
                                             [MODRM_ADDR_BX_DI_DISP16] = 2,
                                             [MODRM_ADDR_BP_SI_DISP16] = 2,
                                             [MODRM_ADDR_BP_DI_DISP16] = 2,
                                             [MODRM_ADDR_SI_DISP16]    = 2,
                                             [MODRM_ADDR_DI_DISP16]    = 2,
                                             [MODRM_ADDR_BP_DISP16]    = 2,
                                             [MODRM_ADDR_BX_DISP16]    = 2,
                                             [0x88 ... 0xff]           = 0 };

#ifdef ENABLE_808X_LOG
#if 0
void dumpregs(int);
#endif
int x808x_do_log = ENABLE_808X_LOG;

static void
x808x_log(const char *fmt, ...)
{
    va_list ap;

    if (x808x_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define x808x_log(fmt, ...)
#endif

static i8080 emulated_processor;
static bool cpu_md_write_disable = 1;

static void
set_if(int cond)
{
    cpu_state.flags = (cpu_state.flags & ~I_FLAG) | (cond ? I_FLAG : 0);
}

void
sync_from_i8080(void)
{
    AL = emulated_processor.a;
    BH = emulated_processor.h;
    BL = emulated_processor.l;
    CH = emulated_processor.b;
    CL = emulated_processor.c;
    DH = emulated_processor.d;
    DL = emulated_processor.e;
    BP = emulated_processor.sp;
    
    cpu_state.pc = emulated_processor.pc;
    cpu_state.flags &= 0xFF00;
    cpu_state.flags |= emulated_processor.sf << 7;
    cpu_state.flags |= emulated_processor.zf << 6;
    cpu_state.flags |= emulated_processor.hf << 4;
    cpu_state.flags |= emulated_processor.pf << 2;
    cpu_state.flags |= 1 << 1;
    cpu_state.flags |= emulated_processor.cf << 0;
    set_if(emulated_processor.iff);
}

void
sync_to_i8080(void)
{
    if (!is_nec)
        return;
    emulated_processor.a  = AL;
    emulated_processor.h  = BH;
    emulated_processor.l  = BL;
    emulated_processor.b  = CH;
    emulated_processor.c  = CL;
    emulated_processor.d  = DH;
    emulated_processor.e  = DL;
    emulated_processor.sp = BP;
    emulated_processor.pc = cpu_state.pc;
    emulated_processor.iff = !!(cpu_state.flags & I_FLAG);

    emulated_processor.sf = (cpu_state.flags >> 7) & 1;
    emulated_processor.zf = (cpu_state.flags >> 6) & 1;
    emulated_processor.hf = (cpu_state.flags >> 4) & 1;
    emulated_processor.pf = (cpu_state.flags >> 2) & 1;
    emulated_processor.cf = (cpu_state.flags >> 0) & 1;

    emulated_processor.interrupt_delay = noint;
}

uint16_t
get_last_addr(void)
{
    return last_addr;
}

static void
set_ip(uint16_t new_ip)
{
    cpu_state.pc = new_ip;
}

static void
startx86(void)
{
    /* Reset takes 6 cycles before first fetch. */
    do_cycle();
    biu_suspend_fetch();
    do_cycles_i(2);
    biu_queue_flush();
    do_cycles_i(3);
}

static void
load_cs(uint16_t seg)
{
    cpu_state.seg_cs.base = seg << 4;
    cpu_state.seg_cs.seg  = seg & 0xffff;
}

static void
load_seg(uint16_t seg, x86seg *s)
{
    s->base = seg << 4;
    s->seg  = seg & 0xffff;
}

static uint8_t
fetch_i8080_opcode(UNUSED(void* priv), uint16_t addr)
{
    return readmemb(cs, addr);
}

static uint8_t
fetch_i8080_data(UNUSED(void* priv), uint16_t addr)
{
    return readmemb(ds, addr);
}

static void
put_i8080_data(UNUSED(void* priv), uint16_t addr, uint8_t val)
{
    writememb(ds, addr, val);
}

static
uint8_t i8080_port_in(UNUSED(void* priv), uint8_t port)
{
    cpu_data = port;
    cpu_state.eaaddr = cpu_data;
    cpu_io_vx0(8, 0, cpu_state.eaaddr);
    return AL;
}

static
void i8080_port_out(UNUSED(void* priv), uint8_t port, uint8_t val)
{
    cpu_data = DX;
    AL = val;
    do_cycle_i();

    cpu_state.eaaddr = cpu_data;
    cpu_data = AL;
    cpu_io_vx0(8, 1, port);
}

void
reset_vx0(int hard)
{
    halted     = 0;
    in_hlt     = 0;
    in_0f      = 0;
    in_rep     = 0;
    in_lock    = 0;
    completed  = 1;
    repeating  = 0;
    clear_lock = 0;
    ovr_seg    = NULL;

    if (hard) {
        opseg[0]  = &es;
        opseg[1]  = &cs;
        opseg[2]  = &ss;
        opseg[3]  = &ds;
        _opseg[0] = &cpu_state.seg_es;
        _opseg[1] = &cpu_state.seg_cs;
        _opseg[2] = &cpu_state.seg_ss;
        _opseg[3] = &cpu_state.seg_ds;
    }

    load_cs(0xFFFF);
    cpu_state.pc = 0;

    if (is_nec)
        cpu_state.flags |= MD_FLAG;
    rammask = 0xfffff;

    cpu_alu_op            = 0;

    nx                    = 0;

    use_custom_nmi_vector = 0x00;
    custom_nmi_vector     = 0x00000000;

    biu_reset();

    started               = 1;
    modrm_loaded          = 0;

    cpu_md_write_disable = 1;
    i8080_init(&emulated_processor);
    emulated_processor.write_byte    = put_i8080_data;
    emulated_processor.read_byte     = fetch_i8080_data;
    emulated_processor.read_byte_seg = fetch_i8080_opcode;
    emulated_processor.port_in       = i8080_port_in;
    emulated_processor.port_out      = i8080_port_out;
}

static uint16_t
get_accum(int bits)
{
    return (bits == 16) ? AX : AL;
}

static void
set_accum(int bits, uint16_t val)
{
    if (bits == 16)
        AX = val;
    else
        AL = val;
}

static uint16_t
sign_extend(uint8_t data)
{
    return data + (data < 0x80 ? 0 : 0xff00);
}

#undef getr8
#define getr8(r) ((r & 4) ? cpu_state.regs[r & 3].b.h : cpu_state.regs[r & 3].b.l)

#undef setr8
#define setr8(r, v)                    \
    if (r & 4)                         \
        cpu_state.regs[r & 3].b.h = v; \
    else                               \
        cpu_state.regs[r & 3].b.l = v;

/* Reads a byte from the effective address. */
static uint8_t
geteab(void)
{
    uint8_t ret;

    if (cpu_mod == 3)
        ret = (getr8(cpu_rm));
    else 
        ret = readmemb(easeg, cpu_state.eaaddr);

    return ret;
}

/* Reads a word from the effective address. */
static uint16_t
geteaw(void)
{
    uint16_t ret;

    if (cpu_mod == 3)
        ret = cpu_state.regs[cpu_rm].w;
    else
        ret = readmemw(easeg, cpu_state.eaaddr);

    return ret;
}

/* Neede for 8087 - memory only. */
static uint32_t
geteal(void)
{
    uint32_t ret;

    if (cpu_mod == 3) {
        fatal("808x register geteal()\n");
        ret = 0xffffffff;
    } else
        ret = readmeml(easeg, cpu_state.eaaddr);

    return ret;
}

/* Neede for 8087 - memory only. */
static uint64_t
geteaq(void)
{
    uint32_t ret;

    if (cpu_mod == 3) {
        fatal("808x register geteaq()\n");
        ret = 0xffffffff;
    } else
        ret = readmemq(easeg, cpu_state.eaaddr);

    return ret;
}

static void
read_ea(int memory_only, int bits)
{
    if (cpu_mod != 3) {
        if (bits == 16)
            cpu_data = readmemw(easeg, cpu_state.eaaddr);
        else
            cpu_data = readmemb(easeg, cpu_state.eaaddr);
        return;
    }
    if (!memory_only) {
        if (bits == 8) {
            cpu_data = getr8(cpu_rm);
        } else
            cpu_data = cpu_state.regs[cpu_rm].w;
    }
}

static void
read_ea_8to16(void)
{
    cpu_data = cpu_state.regs[cpu_rm & 3].w;
}

static void
read_ea2(int bits)
{
    cpu_state.eaaddr = (cpu_state.eaaddr + 2) & 0xffff;
    if (bits == 16)
        cpu_data = readmemw(easeg, cpu_state.eaaddr);
    else
        cpu_data = readmemb(easeg, cpu_state.eaaddr);
}

/* Writes a byte to the effective address. */
static void
seteab(uint8_t val)
{
    if (cpu_mod == 3) {
        setr8(cpu_rm, val);
    } else {
        do_cycle();
        writememb(easeg, cpu_state.eaaddr, val);
    }
}

/* Writes a word to the effective address. */
static void
seteaw(uint16_t val)
{
    if (cpu_mod == 3)
        cpu_state.regs[cpu_rm].w = val;
    else {
        do_cycle();
        writememw(easeg, cpu_state.eaaddr, val);
    }
}

static void
seteal(uint32_t val)
{
    if (cpu_mod == 3) {
        fatal("808x register seteal()\n");
        return;
    } else
        writememl(easeg, cpu_state.eaaddr, val);
}

static void
seteaq(uint64_t val)
{
    if (cpu_mod == 3) {
        fatal("808x register seteaq()\n");
        return;
    } else
        writememq(easeg, cpu_state.eaaddr, val);
}

/* Leave out the 686 stuff as it's not needed and
   complicates compiling. */
#define FPU_8087
#define FPU_NEC
#define tempc tempc_fpu
#include "x87_sf.h"
#include "x87.h"
#include "x87_ops.h"
#undef tempc
#undef FPU_8087

static void
set_cf(int cond)
{
    cpu_state.flags = (cpu_state.flags & ~C_FLAG) | (cond ? C_FLAG : 0);
}

static void
set_df(int cond)
{
    cpu_state.flags = (cpu_state.flags & ~D_FLAG) | (cond ? D_FLAG : 0);
}

static void
set_of(int of)
{
    cpu_state.flags = (cpu_state.flags & ~0x800) | (of ? 0x800 : 0);
}

static int
top_bit(uint16_t w, int bits)
{
    return (w & (1 << (bits - 1)));
}

static void
set_of_add(int bits)
{
    set_of(top_bit((cpu_data ^ cpu_src) & (cpu_data ^ cpu_dest), bits));
}

static void
set_of_sub(int bits)
{
    set_of(top_bit((cpu_dest ^ cpu_src) & (cpu_data ^ cpu_dest), bits));
}

static void
set_af(int af)
{
    cpu_state.flags = (cpu_state.flags & ~0x10) | (af ? 0x10 : 0);
}

static void
do_af(void)
{
    set_af(((cpu_data ^ cpu_src ^ cpu_dest) & 0x10) != 0);
}

static void
set_sf(int bits)
{
    cpu_state.flags = (cpu_state.flags & ~0x80) | (top_bit(cpu_data, bits) ? 0x80 : 0);
}

static void
set_pf(void)
{
    cpu_state.flags = (cpu_state.flags & ~4) | (!__builtin_parity(cpu_data & 0xFF) << 2);
}

static void
set_of_rotate(int bits)
{
    set_of(top_bit(cpu_data ^ cpu_dest, bits));
}

static void
set_zf_ex(int zf)
{
    cpu_state.flags = (cpu_state.flags & ~0x40) | (zf ? 0x40 : 0);
}

static void
set_zf(int bits)
{
    int size_mask = (1 << bits) - 1;

    set_zf_ex((cpu_data & size_mask) == 0);
}

static void
set_pzs(int bits)
{
    set_pf();
    set_zf(bits);
    set_sf(bits);
}

static void
set_apzs(int bits)
{
    set_pzs(bits);
    do_af();
}

static void
add(int bits)
{
    int size_mask = (1 << bits) - 1;

    cpu_data = cpu_dest + cpu_src;
    set_apzs(bits);
    set_of_add(bits);

    /* Anything - FF with carry on is basically anything + 0x100: value stays
       unchanged but carry goes on. */
    if ((cpu_alu_op == 2) && !(cpu_src & size_mask) && (cpu_state.flags & C_FLAG))
        cpu_state.flags |= C_FLAG;
    else
        set_cf((cpu_src & size_mask) > (cpu_data & size_mask));
}

static void
sub(int bits)
{
    int size_mask = (1 << bits) - 1;

    cpu_data = cpu_dest - cpu_src;
    set_apzs(bits);
    set_of_sub(bits);

    /* Anything - FF with carry on is basically anything - 0x100: value stays
       unchanged but carry goes on. */
    if ((cpu_alu_op == 3) && !(cpu_src & size_mask) && (cpu_state.flags & C_FLAG))
        cpu_state.flags |= C_FLAG;
    else
        set_cf((cpu_src & size_mask) > (cpu_dest & size_mask));
}

static void
bitwise(int bits, uint16_t data)
{
    cpu_data = data;
    cpu_state.flags &= ~(C_FLAG | A_FLAG | V_FLAG);
    set_pzs(bits);
}

static void
test(int bits, uint16_t dest, uint16_t src)
{
    cpu_dest = dest;
    cpu_src  = src;
    bitwise(bits, (cpu_dest & cpu_src));
}

static void
alu_op(int bits)
{
    switch (cpu_alu_op) {
        case 1:
            bitwise(bits, (cpu_dest | cpu_src));
            break;
        case 2:
            if (cpu_state.flags & C_FLAG)
                cpu_src++;
            fallthrough;
        case 0:
            add(bits);
            break;
        case 3:
            if (cpu_state.flags & C_FLAG)
                cpu_src++;
            fallthrough;
        case 5:
        case 7:
            sub(bits);
            break;
        case 4:
            test(bits, cpu_dest, cpu_src);
            break;
        case 6:
            bitwise(bits, (cpu_dest ^ cpu_src));
            break;

        default:
            break;
    }
}

static void
mul(uint16_t a, uint16_t b)
{
    int      negate    = 0;
    int      bit_count = 8;
    int      carry;
    uint16_t high_bit = 0x80;
    uint16_t size_mask;
    uint16_t c;
    uint16_t r;

    size_mask = (1 << bit_count) - 1;

    if (opcode != 0xd5) {
        if (opcode & 1) {
            bit_count = 16;
            high_bit  = 0x8000;
        } else
            do_cycles(8);

        size_mask = (1 << bit_count) - 1;

        if ((rmdat & 0x38) == 0x28) {
            if (!top_bit(a, bit_count)) {
                if (top_bit(b, bit_count)) {
                    do_cycle();
                    if ((b & size_mask) != ((opcode & 1) ? 0x8000 : 0x80))
                        do_cycle();
                    b      = ~b + 1;
                    negate = 1;
                }
            } else {
                do_cycle();
                a      = ~a + 1;
                negate = 1;
                if (top_bit(b, bit_count)) {
                    b      = ~b + 1;
                    negate = 0;
                } else
                    do_cycles(4);
            }
            do_cycles(10);
        }
        do_cycles(3);
    }

    c = 0;
    a &= size_mask;
    carry = (a & 1) != 0;
    a >>= 1;
    for (int i = 0; i < bit_count; ++i) {
        do_cycles(7);
        if (carry) {
            cpu_src  = c;
            cpu_dest = b;
            add(bit_count);
            c = cpu_data & size_mask;
            do_cycles(1);
            carry = !!(cpu_state.flags & C_FLAG);
        }
        r     = (c >> 1) + (carry ? high_bit : 0);
        carry = (c & 1) != 0;
        c     = r;
        r     = (a >> 1) + (carry ? high_bit : 0);
        carry = (a & 1) != 0;
        a     = r;
    }
    if (negate) {
        c = ~c;
        a = (~a + 1) & size_mask;
        if (a == 0)
            ++c;
        do_cycles(9);
    }
    cpu_data = a;
    cpu_dest = c;

    set_sf(bit_count);
    set_pf();
    set_af(0);
}

static void
set_co_mul(UNUSED(int bits), int carry)
{
    set_cf(carry);
    set_of(carry);
    set_zf_ex(!carry);
    if (!carry)
        do_cycle();
}

/* Pushes a word to the stack. */
static void
push(uint16_t *val)
{
    if ((is186 && !is_nec) && (SP == 1)) {
        writememw(ss - 1, 0, *val);
        SP = cpu_state.eaaddr = 0xFFFF;
        return;
    }
    SP -= 2;
    cpu_state.eaaddr = (SP & 0xffff);
    writememw(ss, cpu_state.eaaddr, *val);
}

/* Pops a word from the stack. */
static uint16_t
pop(void)
{
    cpu_state.eaaddr = (SP & 0xffff);
    SP += 2;
    return readmemw(ss, cpu_state.eaaddr);
}

static void
nearcall(uint16_t new_ip)
{
    uint16_t ret_ip = cpu_state.pc & 0xffff;

    do_cycle_i();
    set_ip(new_ip);
    biu_queue_flush();
    do_cycles_i(3);
    push(&ret_ip);
}

static void
farcall2(uint16_t new_cs, uint16_t new_ip)
{
    do_cycles_i(3);
    push(&CS);
    load_cs(new_cs);
    do_cycles_i(2);
    nearcall(new_ip);
}

/* The INTR microcode routine. */
static void
intr_routine(uint16_t intr, int skip_first)
{
    uint16_t vector = intr * 4;
    uint16_t tempf = cpu_state.flags & (is_nec ? 0x8fd7 : 0x0fd7);
    uint16_t new_cs;
    uint16_t new_ip;

    if (!(cpu_state.flags & MD_FLAG) && is_nec) {
        sync_from_i8080();
        x808x_log("CALLN/INT#/NMI#\n");
    }

    if (!skip_first)
        do_cycle_i();
    do_cycles_i(2);

    cpu_state.eaaddr = vector & 0xffff;
    new_ip           = readmemw(0, cpu_state.eaaddr);
    do_cycle_i();
    cpu_state.eaaddr = (cpu_state.eaaddr + 2) & 0xffff;
    new_cs           = readmemw(0, cpu_state.eaaddr);

    biu_suspend_fetch();
    do_cycles_i(2);
    push(&tempf);
    cpu_state.flags &= ~(I_FLAG | T_FLAG);
    if (is_nec)
        cpu_state.flags |= MD_FLAG;
    do_cycle_i();

    farcall2(new_cs, new_ip);
}

/* Was div(), renamed to avoid conflicts with stdlib div(). */
static int
x86_div(uint16_t l, uint16_t h)
{
    int      bit_count         = 8;
    int      negative          = 0;
    int      dividend_negative = 0;
    int      size_mask;
    int      carry;
    uint16_t r;

    if (opcode & 1) {
        l         = AX;
        h         = DX;
        bit_count = 16;
    }

    size_mask = (1 << bit_count) - 1;

    if (opcode != 0xd4) {
        if ((rmdat & 0x38) == 0x38) {
            if (top_bit(h, bit_count)) {
                h = ~h;
                l = (~l + 1) & size_mask;
                if (l == 0)
                    ++h;
                h &= size_mask;
                negative          = 1;
                dividend_negative = 1;
                do_cycles(4);
            }
            if (top_bit(cpu_src, bit_count)) {
                cpu_src  = ~cpu_src + 1;
                negative = !negative;
            } else
                do_cycle();
            do_cycles(9);
        }
        do_cycles(3);
    }
    do_cycles(8);
    cpu_src &= size_mask;
    if (h >= cpu_src) {
        if (opcode != 0xd4)
            do_cycle();
        intr_routine(0, 0);
        return 0;
    }
    if (opcode != 0xd4)
        do_cycle();
    do_cycles(2);
    carry = 1;
    for (int b = 0; b < bit_count; ++b) {
        r     = (l << 1) + (carry ? 1 : 0);
        carry = top_bit(l, bit_count);
        l     = r;
        r     = (h << 1) + (carry ? 1 : 0);
        carry = top_bit(h, bit_count);
        h     = r;
        do_cycles(8);
        if (carry) {
            carry = 0;
            h -= cpu_src;
            if (b == bit_count - 1)
                do_cycles(2);
        } else {
            carry = cpu_src > h;
            if (!carry) {
                h -= cpu_src;
                do_cycle();
                if (b == bit_count - 1)
                    do_cycles(2);
            }
        }
    }
    l = ~((l << 1) + (carry ? 1 : 0));
    if (opcode != 0xd4 && (rmdat & 0x38) == 0x38) {
        do_cycles(4);
        if (top_bit(l, bit_count)) {
            if (cpu_mod == 3)
                do_cycle();
            intr_routine(0, 0);
            return 0;
        }
        do_cycles(7);
        if (negative)
            l = ~l + 1;
        if (dividend_negative)
            h = ~h + 1;
    }
    if (opcode == 0xd4) {
        AL = h & 0xff;
        AH = l & 0xff;
    } else {
        AH = h & 0xff;
        AL = l & 0xff;
        if (opcode & 1) {
            DX = h;
            AX = l;
        }
    }
    return 1;
}

static uint16_t
string_increment(int bits)
{
    int d = bits >> 3;
    if (cpu_state.flags & D_FLAG)
        cpu_state.eaaddr -= d;
    else
        cpu_state.eaaddr += d;
    cpu_state.eaaddr &= 0xffff;
    return cpu_state.eaaddr;
}

static void
lods(int bits)
{
    cpu_state.eaaddr = SI;
    if (bits == 16)
        cpu_data = readmemw((ovr_seg ? *ovr_seg : ds), cpu_state.eaaddr);
    else
        cpu_data = readmemb((ovr_seg ? *ovr_seg : ds), cpu_state.eaaddr);
    SI = string_increment(bits);
}

static void
lods_di(int bits)
{
    cpu_state.eaaddr = DI;
    if (bits == 16)
        cpu_data = readmemw(es, cpu_state.eaaddr);
    else
        cpu_data = readmemb(es, cpu_state.eaaddr);
    DI = string_increment(bits);
}

static void
stos(int bits)
{
    cpu_state.eaaddr = DI;
    if (bits == 16)
        writememw(es, cpu_state.eaaddr, cpu_data);
    else
        writememb(es, cpu_state.eaaddr, (uint8_t) (cpu_data & 0xff));
    DI = string_increment(bits);
}

static uint16_t
get_ea(void)
{
    if (opcode & 1)
        return geteaw();
    else
        return (uint16_t) geteab();
}

static uint16_t
get_reg(uint8_t reg)
{
    if (opcode & 1)
        return cpu_state.regs[reg].w;
    else
        return (uint16_t) getr8(reg);
}

static void
set_ea(uint16_t val)
{
    if (opcode & 1)
        seteaw(val);
    else
        seteab((uint8_t) (val & 0xff));
}

static void
set_reg(uint8_t reg, uint16_t val)
{
    if (opcode & 1)
        cpu_state.regs[reg].w = val;
    else
        setr8(reg, (uint8_t) (val & 0xff));
}

static void
cpu_data_opff_rm(void)
{
    if (!(opcode & 1)) {
        if (cpu_mod != 3)
            cpu_data |= 0xff00;
        else
            cpu_data = cpu_state.regs[cpu_rm].w;
    }
}

static void
farcall(uint16_t new_cs, uint16_t new_ip, int jump)
{
    if (jump)
        do_cycle_i();
    biu_suspend_fetch();
    do_cycles_i(2);
    push(&CS);
    load_cs(new_cs);
    do_cycles_i(2);
    nearcall(new_ip);
}

/* Calls an interrupt. */
static void
sw_int(uint16_t intr)
{
    uint16_t vector = intr * 4;
    uint16_t tempf = cpu_state.flags & (is_nec ? 0x8fd7 : 0x0fd7);
    uint16_t new_cs;
    uint16_t new_ip;
    uint16_t old_ip;

    if (!(cpu_state.flags & MD_FLAG) && is_nec) {
        sync_from_i8080();
        x808x_log("CALLN/INT#/NMI#\n");
    }

    do_cycles_i(3);
    cpu_state.eaaddr = vector & 0xffff;
    new_ip           = readmemw(0, cpu_state.eaaddr);
    do_cycle_i();
    cpu_state.eaaddr = (cpu_state.eaaddr + 2) & 0xffff;
    new_cs           = readmemw(0, cpu_state.eaaddr);

    biu_suspend_fetch();
    do_cycles_i(2);
    push(&tempf);
    cpu_state.flags &= ~(I_FLAG | T_FLAG);
    if (is_nec)
        cpu_state.flags |= MD_FLAG;

    /* FARCALL2 */
    do_cycles_i(4);
    push(&CS);
    load_cs(new_cs);
    do_cycle_i();

    /* NEARCALL */
    old_ip = cpu_state.pc & 0xffff;
    do_cycles_i(2);
    set_ip(new_ip);
    biu_queue_flush();
    do_cycles_i(3);
    push(&old_ip);
}

static void
int3(void)
{
    do_cycles_i(4);
    intr_routine(3, 0);
}

/* Ditto, but for breaking into emulation mode. */
static void
interrupt_brkem(uint16_t addr)
{
    uint16_t tempf = cpu_state.flags & (is_nec ? 0x8fd7 : 0x0fd7);
    uint16_t new_cs;
    uint16_t new_ip;
    uint16_t old_ip;

    do_cycles_i(3);
    cpu_state.eaaddr = addr << 2;
    new_ip           = readmemw(0, cpu_state.eaaddr);
    do_cycle_i();
    cpu_state.eaaddr = (cpu_state.eaaddr + 2) & 0xffff;
    new_cs           = readmemw(0, cpu_state.eaaddr);

    biu_suspend_fetch();
    do_cycles_i(2);
    push(&tempf);
    cpu_state.flags      &= ~(MD_FLAG);
    cpu_md_write_disable  = 0;

    /* FARCALL2 */
    do_cycles_i(4);
    push(&CS);
    load_cs(new_cs);
    do_cycle_i();

    /* NEARCALL */
    old_ip = cpu_state.pc & 0xffff;
    do_cycles_i(2);
    set_ip(new_ip);
    biu_queue_flush();
    do_cycles_i(3);
    push(&old_ip);

    sync_to_i8080();
    x808x_log("BRKEM mode\n");
}

void
retem_i8080(void)
{
    sync_from_i8080();

    do_cycle_i();
    farret(1);
    /* pop_flags() */
    cpu_state.flags = pop();
    do_cycle_i();

    noint      = 1;
    nmi_enable = 1;

    emulated_processor.iff = !!(cpu_state.flags & I_FLAG);

    cpu_md_write_disable = 1;

    retem = 1;

    x808x_log("RETEM mode\n");
}

void
interrupt_808x(uint16_t addr)
{
    biu_suspend_fetch();
    do_cycles_i(2);

    intr_routine(addr, 0);
}

static void
custom_nmi(void)
{
    uint16_t tempf = cpu_state.flags & (is_nec ? 0x8fd7 : 0x0fd7);
    uint16_t new_cs;
    uint16_t new_ip;

    if (!(cpu_state.flags & MD_FLAG) && is_nec) {
        sync_from_i8080();
        x808x_log("CALLN/INT#/NMI#\n");
    }

    do_cycle_i();
    do_cycles_i(2);

    cpu_state.eaaddr = 0x0002;
    (void) readmemw(0, cpu_state.eaaddr);
    new_ip = custom_nmi_vector & 0xffff;
    do_cycle_i();
    cpu_state.eaaddr = (cpu_state.eaaddr + 2) & 0xffff;
    (void) readmemw(0, cpu_state.eaaddr);
    new_cs = custom_nmi_vector >> 16;

    biu_suspend_fetch();
    do_cycles_i(2);
    push(&tempf);
    cpu_state.flags &= ~(I_FLAG | T_FLAG);
    if (is_nec)
        cpu_state.flags |= MD_FLAG;
    do_cycle_i();

    farcall2(new_cs, new_ip);
}

static int
irq_pending(void)
{
    uint8_t temp;

    temp = (nmi && nmi_enable && nmi_mask) || ((cpu_state.flags & T_FLAG) && !noint) ||
           ((in_hlt || (cpu_state.flags & I_FLAG)) && pic.int_pending && !noint);

    return temp;
}

static int
bus_pic_ack(void)
{
    int old_in_lock = in_lock;

    in_lock          = 1;
    bus_request_type = BUS_PIC;
    biu_begin_eu();
    biu_wait_for_read_finish();
    in_lock = old_in_lock;
    return pic_data;
}

static void
hw_int(uint16_t vector)
{
    biu_suspend_fetch();
    do_cycles_i(2);

    intr_routine(vector, 0);
}

static void
int1(void)
{
    do_cycles_i(2);
    intr_routine(1, 1);
}

static void
int2(void)
{
    do_cycles_i(2);
    intr_routine(2, 1);
}

static void
check_interrupts(void)
{
    int temp;

    if (irq_pending()) {
        if ((cpu_state.flags & T_FLAG) && !noint) {
            int1();
            return;
        }
        if (nmi && nmi_enable && nmi_mask) {
            nmi_enable = 0;
            if (use_custom_nmi_vector) {
                do_cycles(2);
                custom_nmi();
            } else
                int2();
            return;
        }
        if ((in_hlt || (cpu_state.flags & I_FLAG)) && pic.int_pending && !noint) {
            repeating = 0;
            completed = 1;
            ovr_seg   = NULL;
            do_cycles(4);
            /* ACK to PIC */
            biu_begin_eu();
            temp = bus_pic_ack();
            do_cycle();
            /* ACK to PIC */
            temp = bus_pic_ack();
            in_lock    = 0;
            clear_lock = 0;
            /* Here is where temp should be filled, but we cheat. */
            opcode = 0x00;
            hw_int(temp);
        }
    }
}

/* The FARRET microcode routine. */
static void
farret(int far)
{
    uint8_t  far2 = !!(opcode & 0x08);

    do_cycle_i();
    set_ip(pop());
    biu_suspend_fetch();
    do_cycles_i(2);

    if ((!!far) != far2)
        fatal("Far call distance mismatch (%i = %i)\n", !!far, far2);

    if (far) {
        do_cycle_i();
        load_cs(pop());

        biu_queue_flush();
        do_cycles_i(2);
    } else {
        biu_queue_flush();
        do_cycles_i(2);
    }

    do_cycles_i(2);
}

static void
iret_routine(void)
{
    do_cycle_i();
    farret(1);
    /* pop_flags() */
    if (is_nec && cpu_md_write_disable)
        cpu_state.flags = pop() | 0x8002;
    else
        cpu_state.flags = pop() | 0x0002;
    do_cycle_i();
    noint      = 1;
    nmi_enable = 1;
}

static void
rep_end(void)
{
    repeating = 0;
    in_rep = 0;
    completed = 1;
}

static int
rep_start(void)
{
    if (!repeating) {
        if (in_rep != 0) {
            if (CX == 0) {
                do_cycles_i(is_nec ? 1 : 4);
                rep_end();
                return 0;
            } else
                do_cycles_i(is_nec ? 1 : 7);
        }
    }

    completed = 1;
    return 1;
}

static void
rep_interrupt(void)
{
    biu_suspend_fetch();
    do_cycles_i(4);
    biu_queue_flush();

    if (is_nec && (ovr_seg != NULL))
        set_ip((cpu_state.pc - 3) & 0xffff);
    else
        set_ip((cpu_state.pc - 2) & 0xffff);

    rep_end();
}

static void
sign_extend_al(void)
{
    if ((AL & 0x80) != 0)
        AH = 0xff;
    else
        AH = 0x00;
}

static void
sign_extend_ax(void)
{
    do_cycles(3);
    if ((AX & 0x8000) == 0)
        DX = 0x0000;
    else {
        do_cycle();
        DX = 0xffff;
    }
}

static void
reljmp(uint16_t new_ip, int jump)
{
    if (!is_nec && jump)
        do_cycle_i();

    biu_suspend_fetch();
    if (!is_nec)
        do_cycles_i(3);
    set_ip(new_ip);
    biu_queue_flush();
    do_cycle_i();
}

static void
daa(void)
{
    uint16_t old_cf   = cpu_state.flags & C_FLAG;
    uint16_t old_af   = cpu_state.flags & A_FLAG;
    uint8_t  old_al   = AL;
    uint8_t  al_check;

    cpu_state.flags &= ~C_FLAG;

    al_check = (old_af ? 0x9f : 0x99);

    cpu_state.flags &= ~V_FLAG;
    if (old_cf) {
        if ((AL >= 0x1a) && (AL <= 0x7f))
            cpu_state.flags |= V_FLAG;
    } else if ((AL >= 0x7a) && (AL <= 0x7f))
        cpu_state.flags |= V_FLAG;

    if (((AL & 0x0f) > 9) || (cpu_state.flags & A_FLAG)) {
        AL += 6;
        cpu_state.flags |= A_FLAG;
    } else
        cpu_state.flags &= ~A_FLAG;

    if ((old_al > al_check) || old_cf) {
        AL += 0x60;
        cpu_state.flags |= C_FLAG;
    } else
        cpu_state.flags &= ~C_FLAG;

    set_pzs(8);
}

static void
das(void)
{
    uint8_t  old_al   = AL;
    uint16_t old_af   = cpu_state.flags & A_FLAG;
    uint16_t old_cf   = cpu_state.flags & C_FLAG;
    uint8_t  al_check = (old_af ? 0x9f : 0x99);

    cpu_state.flags &= ~V_FLAG;

    if (!old_af && !old_cf) {
        if ((AL >= 0x9a) && (AL <= 0xdf))
            cpu_state.flags |= V_FLAG;
    } else if (old_af && !old_cf) {
        if (((AL >= 0x80) && (AL <= 0x85)) || ((AL >= 0xa0) && (AL <= 0xe5)))
            cpu_state.flags |= V_FLAG;
    } else if (!old_af && old_cf) {
        if ((AL >= 0x80) && (AL <= 0xdf))
            cpu_state.flags |= V_FLAG;
    } else if (old_af && old_cf) {
        if ((AL >= 0x80) && (AL <= 0xe5))
            cpu_state.flags |= V_FLAG;
    }

    cpu_state.flags &= ~C_FLAG;
    if (((AL & 0x0f) > 9) || (cpu_state.flags & A_FLAG)) {
        AL -= 6;
        cpu_state.flags |= A_FLAG;
    } else
        cpu_state.flags &= ~A_FLAG;

    if ((old_al > al_check) || old_cf) {
        AL -= 0x60;
        cpu_state.flags |= C_FLAG;
    } else
        cpu_state.flags &= ~C_FLAG;

    set_pzs(8);
}

static void
aaa(void)
{
    uint8_t old_al = AL;
    uint8_t new_al;

    if (((AL & 0x0f) > 9) || (cpu_state.flags & A_FLAG)) {
        AH += 1;
        new_al = AL + 6;
        AL = new_al & 0x0f;
        cpu_state.flags |= A_FLAG;
        cpu_state.flags |= C_FLAG;
    } else {
        new_al = AL;
        AL &= 0x0f;
        cpu_state.flags &= ~A_FLAG;
        cpu_state.flags &= ~C_FLAG;
        do_cycle_i();
    }

    cpu_state.flags &= ~(V_FLAG | Z_FLAG | N_FLAG);
    if (new_al == 0x00)
        cpu_state.flags |= Z_FLAG;
    if ((old_al >= 0x7a) && (old_al <= 0x7f))
        cpu_state.flags |= V_FLAG;
    if ((old_al >= 0x7a) && (old_al <= 0xf9))
        cpu_state.flags |= N_FLAG;

    cpu_data = new_al;
    set_pf();
}

static void
aas(void)
{
    uint8_t  old_al = AL;
    uint16_t old_af = cpu_state.flags & A_FLAG;
    uint8_t  new_al;

    do_cycles_i(6);

    if (((AL &  0x0f) > 9) || old_af) {
        new_al = AL - 6;
        AH++;
        AL = new_al & 0x0f;
        cpu_state.flags |= (A_FLAG | C_FLAG);
    } else {
        new_al = AL;
        AL &= 0x0f;
        cpu_state.flags &= ~(C_FLAG | A_FLAG);
        do_cycle_i();
    }

    cpu_state.flags &= ~(V_FLAG | Z_FLAG | N_FLAG);
    if (new_al == 0x00)
        cpu_state.flags |= Z_FLAG;
    if (old_af && (old_al >= 0x80) && (old_al <= 0x85))
        cpu_state.flags |= V_FLAG;
    if (!old_af && (old_al >= 0x80))
        cpu_state.flags |= N_FLAG;
    if (old_af && ((old_al <= 0x05) || (old_al >= 0x86)))
        cpu_state.flags |= N_FLAG;

    cpu_data = new_al;
    set_pf();
}

static void
finalize(void)
{
    in_0f      = 0;
    repeating  = 0;
    ovr_seg    = NULL;
    in_rep     = 0;
    rep_c_flag = 0;
    if (in_lock)
        clear_lock = 1;
    cpu_alu_op = 0;

    if (pfq_pos == 0) {
        do {
            if (nx)
                nx = 0;
            do_cycle();
        } while (pfq_pos == 0);
        biu_preload_byte = biu_pfq_read();
        biu_queue_preload = 1;
        do_cycle();
    } else {
        biu_queue_preload = 1;
        biu_preload_byte = biu_pfq_read();

        biu_resume_on_queue_read();

        do_cycle();
    }

    if (irq_pending())
        cpu_state.pc--;
}

/* Fetches the effective address from the prefetch queue according to MOD and R/M. */
static void
do_mod_rm(void)
{
    rmdat   = biu_pfq_fetchb();
    cpu_reg = (rmdat >> 3) & 7;
    cpu_mod = (rmdat >> 6) & 3;
    cpu_rm  = rmdat & 7;

    if (cpu_mod != 3) {
        do_cycle();
        if (is_nec)
           do_cycle();
        else if (modrm_cycs_pre[rmdat & 0xc7])
            do_cycles(modrm_cycs_pre[rmdat & 0xc7]);

        if ((rmdat & 0xc7) == 0x06) {
            cpu_state.eaaddr = biu_pfq_fetchw();
            easeg            = ovr_seg ? *ovr_seg : ds;
        } else {
            cpu_state.eaaddr = (*mod1add[0][cpu_rm]) + (*mod1add[1][cpu_rm]);
            easeg            = ovr_seg ? *ovr_seg : *mod1seg[cpu_rm];
            switch (rmdat & 0xc0) {
                default:
                    break;
                case 0x40:
                    cpu_state.eaaddr += sign_extend(biu_pfq_fetchb());
                    break;
                case 0x80:
                    cpu_state.eaaddr += biu_pfq_fetchw();
                    break;
            }
            cpu_state.eaaddr &= 0xffff;
        }

        if (!is_nec && modrm_cycs_post[rmdat & 0xc7])
            do_cycles(modrm_cycs_post[rmdat & 0xc7]);
    }
}

static void
decode(void)
{
    uint8_t op_f;
    uint8_t prefix = 0;

    if (halted)
        opcode  = 0xf4;
    else
        opcode  = biu_pfq_fetchb_common();

    modrm_loaded = 0;

    while (1) {
        prefix = 0;

        switch (opcode) {
            case 0x0f: /* NEC/186 */
                if (is_nec) {
                    in_0f      = 1;
                    prefix     = 1;
                }
                break;
            case 0x26: /* ES: */
            case 0x2e: /* CS: */
            case 0x36: /* SS: */
            case 0x3e: /* DS: */
                ovr_seg   = opseg[(opcode >> 3) & 0x03];
                prefix = 1;
                break;
            case 0x64: /* REPNC */
            case 0x65: /* REPC */
                if (is_nec) {
                    in_rep     = (opcode == 0x64 ? 1 : 2);
                    rep_c_flag = 1;
                    prefix     = 1;
                }
                break;
            case 0xf0:
            case 0xf1: /* LOCK - F1 is alias */
                in_lock    = 1;
                prefix     = 1;
                break;
            case 0xf2: /* REPNE */
            case 0xf3: /* REPE */
                in_rep     = (opcode == 0xf2 ? 1 : 2);
                rep_c_flag = 0;
                prefix     = 1;
                break;
            default:
                break;
        }

        if (prefix == 0)
            break;

        do_cycle();

        opcode  = biu_pfq_fetchb_common();
    }

    if (is_nec) {
        if (in_0f)
            op_f = (uint8_t) opf_0f[opcode];
        else
            op_f = (uint8_t) opf_nec[opcode];
    } else
        op_f = (uint8_t) opf[opcode];

    if (op_f & OP_GRP) {
        do_mod_rm();
        modrm_loaded = 1;

        op_f |= (OP_MRM | OP_EA);

        if (opcode >= 0xf0) {
            op_f |= OP_DELAY;
            group_delay = 1;
        }
    }

    if (!modrm_loaded && (op_f & OP_MRM)) {
        do_mod_rm();
        modrm_loaded = 1;
    }

    if (modrm_loaded && !(op_f & OP_EA)) {
        if (is_nec)
            do_cycle();
        else {
            if (opcode == 0x8f) {
                if (cpu_mod == 3)
                   do_cycles_i(2);
            } else
                do_cycles_i(2);
        }
    }
}

static void
string_op(int bits)
{
    uint16_t      tmpa;
    uint16_t      old_ax;

    if ((opcode & 0xf0) == 0x60)  switch (opcode & 0x0e) {
        case 0x0c:
            old_ax           = AX;
            cpu_data         = DX;
            cpu_state.eaaddr = cpu_data;
            cpu_io_vx0(bits, 0, cpu_state.eaaddr);
            cpu_data         = AX;
            stos(bits);
            AX               = old_ax;
            break;
        case 0x0e:
            old_ax           = AX;
            lods(bits);
            set_accum(bits, cpu_data);
            cpu_data         = DX;
            do_cycle_i();
            /* biu_io_write_u16() */
            cpu_state.eaaddr = cpu_data;
            cpu_io_vx0(bits, 1, cpu_state.eaaddr);
            AX               = old_ax;
            break;
    } else  switch (opcode & 0x0e) {
        case 0x04:
            lods(bits);
            do_cycle_i();
            stos(bits);
            break;
        case 0x06:
            do_cycle_i();
            if (is_nec) {
                if (in_rep) {
                    lods_di(bits);
                    tmpa = cpu_data;
                    lods(bits);
                } else {
                    lods(bits);
                    tmpa = cpu_data;
                    lods_di(bits);
                }
            } else {
                lods(bits);
                tmpa = cpu_data;
                do_cycles_i(2);
                lods_di(bits);
                do_cycles_i(3);
            }

            cpu_src  = cpu_data;
            cpu_dest = tmpa;
            sub(bits);
            break;
        case 0x0e:
            tmpa = AX;
            do_cycles_i(2);
            lods_di(bits);
            do_cycles_i(3);

            cpu_src  = cpu_data;
            cpu_dest = tmpa;
            sub(bits);
            break;
        case 0x0a:
            cpu_data = AX;
            stos(bits);
            break;
        case 0x0c:
            lods(bits);
            set_accum(bits, cpu_data);
            break;
    }
}

static int do_print = 1;

static void
execvx0_0f(void)
{
    uint8_t  bit;
    uint8_t  odd;
    uint8_t  nibbles_count;
    uint8_t  destcmp;
    uint8_t  destbyte;
    uint8_t  srcbyte;
    uint8_t  nibble_result;
    uint8_t  temp_val;
    uint8_t  temp_al;
    uint8_t  bit_length;
    uint8_t  bit_offset;
    int8_t   nibble_result_s;
    int      bits;
    uint32_t i;
    uint32_t carry;
    uint32_t nibble;
    uint32_t srcseg;
    uint32_t byteaddr;

    switch (opcode) {
        case 0x10:    /* TEST1 r8/m8, CL*/
        case 0x11:    /* TEST1 r16/m16, CL*/
        case 0x18:    /* TEST1 r8/m8, imm3 */
        case 0x19:    /* TEST1 r16/m16, imm4 */
            bits = 8 << (opcode & 0x1);
            do_cycles(2);

            bit = (opcode & 0x8) ? biu_pfq_fetchb() : CL;
            bit &= ((1 << (3 + (opcode & 0x1))) - 1);
            read_ea(0, bits);

            set_zf_ex(!(cpu_data & (1 << bit)));
            cpu_state.flags &= ~(V_FLAG | C_FLAG);
            break;

        case 0x12:    /* CLR1 r8/m8, CL*/
        case 0x13:    /* CLR1 r16/m16, CL*/
        case 0x1a:    /* CLR1 r8/m8, imm3 */
        case 0x1b:    /* CLR1 r16/m16, imm4 */
            bits = 8 << (opcode & 0x1);
            do_cycles(2);

            bit = (opcode & 0x8) ? biu_pfq_fetchb() : CL;
            bit &= ((1 << (3 + (opcode & 0x1))) - 1);
            read_ea(0, bits);

            if (bits == 8)
                seteab((cpu_data & 0xff) & ~(1 << bit));
            else
                seteaw((cpu_data & 0xffff) & ~(1 << bit));
            break;

        case 0x14:    /* SET1 r8/m8, CL*/
        case 0x15:    /* SET1 r16/m16, CL*/
        case 0x1c:    /* SET1 r8/m8, imm3 */
        case 0x1d:    /* SET1 r16/m16, imm4 */
            bits = 8 << (opcode & 0x1);
            do_cycles(2);

            bit = (opcode & 0x8) ? biu_pfq_fetchb() : CL;
            bit &= ((1 << (3 + (opcode & 0x1))) - 1);
            read_ea(0, bits);

            if (bits == 8)
                seteab((cpu_data & 0xff) | (1 << bit));
            else
                seteaw((cpu_data & 0xffff) | (1 << bit));
            break;

        case 0x16:    /* NOT1 r8/m8, CL*/
        case 0x17:    /* NOT1 r16/m16, CL*/
        case 0x1e:    /* NOT1 r8/m8, imm3 */
        case 0x1f:    /* NOT1 r16/m16, imm4 */
            bits = 8 << (opcode & 0x1);
            do_cycles(2);

            bit = (opcode & 0x8) ? (biu_pfq_fetchb()) : (CL);
            bit &= ((1 << (3 + (opcode & 0x1))) - 1);
            read_ea(0, bits);

            if (bits == 8)
                seteab((cpu_data & 0xff) ^ (1 << bit));
            else
                seteaw((cpu_data & 0xffff) ^ (1 << bit));
            break;

        case 0x20:    /* ADD4S */
            odd           = !!(CL % 2);
            zero          = 1;
            nibbles_count = CL - odd;
            i             = 0;
            carry         = 0;
            nibble        = 0;
            srcseg        = ovr_seg ? *ovr_seg : ds;

            do_cycles(4);

            for (i = 0; i < ((nibbles_count / 2) + odd); i++) {
                do_cycles(19);
                destcmp = read_mem_b((es) + DI + i);

                for (nibble = 0; nibble < 2; nibble++) {
                    destbyte = destcmp >> (nibble ? 4 : 0);
                    srcbyte  = read_mem_b(srcseg + SI + i) >> (nibble ? 4 : 0);
                    destbyte &= 0xF;
                    srcbyte &= 0xF;
                    nibble_result = (i == (nibbles_count / 2) && nibble == 1) ? (destbyte + carry) :
                                    ((uint8_t) (destbyte)) + ((uint8_t) (srcbyte)) + ((uint32_t) carry);
                    carry         = 0;

                    while (nibble_result >= 10) {
                        nibble_result -= 10;
                        carry++;
                    }

                    if (zero != 0 || (i == (nibbles_count / 2) && nibble == 1))
                        zero = (nibble_result == 0);

                    destcmp = ((destcmp & (nibble ? 0x0F : 0xF0)) | (nibble_result << (4 * nibble)));
                }

                write_mem_b(es + DI + i, destcmp);
            }

            set_cf(!!carry);
            set_zf(!!zero);
            break;

        case 0x22:    /* SUB4S */
            odd           = !!(CL % 2);
            zero          = 1;
            nibbles_count = CL - odd;
            i             = 0;
            carry         = 0;
            nibble        = 0;
            srcseg        = ovr_seg ? *ovr_seg : ds;

            do_cycles(4);

            for (i = 0; i < ((nibbles_count / 2) + odd); i++) {
                do_cycles(19);
                destcmp = read_mem_b((es) + DI + i);

                for (nibble = 0; nibble < 2; nibble++) {
                    destbyte = destcmp >> (nibble ? 4 : 0);
                    srcbyte  = read_mem_b(srcseg + SI + i) >> (nibble ? 4 : 0);
                    destbyte &= 0xF;
                    srcbyte &= 0xF;
                    nibble_result_s = (i == (nibbles_count / 2) && nibble == 1) ? ((int8_t) destbyte - (int8_t) carry) :
                                      ((int8_t) (destbyte)) - ((int8_t) (srcbyte)) - ((int8_t) carry);
                    carry           = 0;

                    while (nibble_result_s < 0) {
                        nibble_result_s += 10;
                        carry++;
                    }

                    if (zero != 0 || (i == (nibbles_count / 2) && nibble == 1))
                        zero = (nibble_result_s == 0);

                    destcmp = ((destcmp & (nibble ? 0x0F : 0xF0)) | (nibble_result_s << (4 * nibble)));
                }

                write_mem_b(es + DI + i, destcmp);
            }

            set_cf(!!carry);
            set_zf(!!zero);
            break;

        case 0x26:    /* CMP4S */
            odd           = !!(CL % 2);
            zero          = 1;
            nibbles_count = CL - odd;
            i             = 0;
            carry         = 0;
            nibble        = 0;
            srcseg        = ovr_seg ? *ovr_seg : ds;

            do_cycles(4);

            for (i = 0; i < ((nibbles_count / 2) + odd); i++) {
                do_cycles(19);
                destcmp = read_mem_b((es) + DI + i);

                for (nibble = 0; nibble < 2; nibble++) {
                    destbyte = destcmp >> (nibble ? 4 : 0);
                    srcbyte  = read_mem_b(srcseg + SI + i) >> (nibble ? 4 : 0);
                    destbyte &= 0xF;
                    srcbyte &= 0xF;
                    nibble_result_s = ((int8_t) (destbyte)) - ((int8_t) (srcbyte)) - ((int8_t) carry);
                    carry           = 0;

                    while (nibble_result_s < 0) {
                        nibble_result_s += 10;
                        carry++;
                    }

                    if (zero != 0 || (i == (nibbles_count / 2) && nibble == 1))
                        zero = (nibble_result_s == 0);

                    destcmp = ((destcmp & (nibble ? 0x0F : 0xF0)) | (nibble_result_s << (4 * nibble)));
                }
            }

            set_cf(!!carry);
            set_zf(!!zero);
            break;

        case 0x28:    /* ROL4 r/m */
            do_cycles(20);

            temp_val = geteab();
            temp_al  = AL;

            temp_al &= 0x0f;
            temp_al |= (temp_val & 0xf0);
            temp_val = (temp_al & 0x0f) | ((temp_val & 0x0f) << 4);
            temp_al >>= 4;
            temp_al &= 0x0f;
            seteab(temp_val);
            AL = temp_al;
            break;

        case 0x2a:    /* ROR4 r/m */
            do_cycles(20);

            temp_val = geteab();
            temp_al  = AL;

            AL       = temp_val & 0x0f;
            temp_val = (temp_val >> 4) | ((temp_al & 0x0f) << 4);

            seteab(temp_val);
            break;

        case 0x31:    /* INS reg1, reg2 */
        case 0x39:    /* INS reg8, imm4 */
            bit_length = ((opcode & 0x8) ? (biu_pfq_fetchb() & 0x0f) : (getr8(cpu_reg) & 0x0f)) + 1;
            bit_offset = getr8(cpu_rm) & 0x0f;
            byteaddr   = (es) + DI;
            i          = 0;

            if (bit_offset >= 8) {
                DI++;
                byteaddr++;
                bit_offset -= 8;
            }

            for (i = 0; i < bit_length; i++) {
                byteaddr = (es) + DI;
                writememb(es, DI, (read_mem_b(byteaddr) & ~(1 << (bit_offset))) | ((!!(AX & (1 << i))) << bit_offset));
                bit_offset++;

                if (bit_offset == 8) {
                    DI++;
                    bit_offset = 0;
                }
            }

            setr8(cpu_rm, bit_offset);
            break;

        case 0x33:    /* EXT reg1, reg2 */
        case 0x3b:    /* EXT reg8, imm4 */
            bit_length = ((opcode & 0x8) ? (biu_pfq_fetchb() & 0x0f) : (getr8(cpu_reg) & 0x0f)) + 1;
            bit_offset = getr8(cpu_rm) & 0x0f;
            byteaddr   = (ds) + SI;
            i          = 0;

            if (bit_offset >= 8) {
                SI++;
                byteaddr++;
                bit_offset -= 8;
            }

            AX = 0;

            for (i = 0; i < bit_length; i++) {
                byteaddr = (ds) + SI;
                AX |= (!!(readmemb(ds, SI) & (1 << bit_offset))) << i;
                bit_offset++;

                if (bit_offset == 8) {
                    SI++;
                    bit_offset = 0;
                }
            }

            setr8(cpu_rm, bit_offset);
            break;

        case 0xff:    /* BRKEM */
            interrupt_brkem(biu_pfq_fetchb());
            break;

        default:
            do_cycles_nx_i(2);    /* Guess, based on NOP. */
            break;
    }
}

static void
execvx0_6x(uint16_t *jump)
{
    uint16_t lowbound;
    uint16_t highbound;
    uint16_t regval;
    uint16_t wordtopush;
    uint16_t immediate;
    uint16_t tempw;
    int      bits;
    int32_t  templ;

    switch (opcode) {
        case 0x60:    /* PUSHA/PUSH R */
            writememw(ss, ((SP -  2) & 0xffff), AX);
            biu_state_set_eu();
            writememw(ss, ((SP -  4) & 0xffff), CX);
            biu_state_set_eu();
            writememw(ss, ((SP -  6) & 0xffff), DX);
            biu_state_set_eu();
            writememw(ss, ((SP -  8) & 0xffff), BX);
            biu_state_set_eu();
            writememw(ss, ((SP - 10) & 0xffff), SP);
            biu_state_set_eu();
            writememw(ss, ((SP - 12) & 0xffff), BP);
            biu_state_set_eu();
            writememw(ss, ((SP - 14) & 0xffff), SI);
            biu_state_set_eu();
            writememw(ss, ((SP - 16) & 0xffff), DI);
            SP -= 16;
            break;

        case 0x61:    /* POPA/POP R */
            DI = readmemw(ss, ((SP) & 0xffff));
            biu_state_set_eu();
            SI = readmemw(ss, ((SP + 2) & 0xffff));
            biu_state_set_eu();
            BP = readmemw(ss, ((SP + 4) & 0xffff));
            biu_state_set_eu();
            BX = readmemw(ss, ((SP + 8) & 0xffff));
            biu_state_set_eu();
            DX = readmemw(ss, ((SP + 10) & 0xffff));
            biu_state_set_eu();
            CX = readmemw(ss, ((SP + 12) & 0xffff));
            biu_state_set_eu();
            AX = readmemw(ss, ((SP + 14) & 0xffff));
            SP += 16;
            break;

        case 0x62:    /* BOUND r/m */
            lowbound  = 0;
            highbound = 0;
            regval    = 0;

            lowbound  = readmemw(easeg, cpu_state.eaaddr);
            highbound = readmemw(easeg, cpu_state.eaaddr + 2);
            regval    = get_reg(cpu_reg);

            if ((lowbound > regval) || (highbound < regval)) {
                sw_int(5);
                *jump = 1;
            }
            break;

        case 0x63:
            if (is_nec) {
                /* read_operand16() */
                if (cpu_mod != 3)
                    do_cycles_i(2);    /* load_operand() */
                tempw = cpu_state.pc;
                geteaw();
                do_cycles(60);
            }
            break;

        case 0x64:
        case 0x65:
            if (!is_nec) {
                do_cycles_nx_i(2);    /* Guess, based on NOP. */
            }
            break;

        case 0x66 ... 0x67:    /* FPO2 - NEC FPU instructions. */
            /* read_operand16() */
            if (cpu_mod != 3)
                do_cycles_i(2);    /* load_operand() */
            tempw = cpu_state.pc;
            geteaw();
            /* fpu_op() */
            cpu_state.pc = tempw; /* Do this as the x87 code advances it, which is needed on
                                     the 286+ core, but not here. */
            break;

        case 0x68:
            wordtopush = biu_pfq_fetchw();
            push(&wordtopush);
            break;

        case 0x69:
            bits      = 16;
            read_ea(0, 16);
            immediate = biu_pfq_fetchw();

            templ = ((int) cpu_data) * ((int) immediate);
            if ((templ >> 15) != 0 && (templ >> 15) != -1)
                cpu_state.flags |= C_FLAG | V_FLAG;
            else
                cpu_state.flags &= ~(C_FLAG | V_FLAG);
            set_reg(cpu_reg, templ & 0xffff);
            do_cycles((cpu_mod == 3) ? 20 : 26);
            break;

        case 0x6a:
            wordtopush = sign_extend(biu_pfq_fetchb());
            push(&wordtopush);
            break;

        case 0x6b:    /* IMUL reg16,reg16/mem16,imm8 */
            read_ea(0, 16);
            immediate = biu_pfq_fetchb();
            immediate = geteaw();
            if (immediate & 0x80)
                immediate |= 0xff00;

            templ = ((int) cpu_data) * ((int) immediate);
            if ((templ >> 15) != 0 && (templ >> 15) != -1)
                cpu_state.flags |= C_FLAG | V_FLAG;
            else
                cpu_state.flags &= ~(C_FLAG | V_FLAG);
            set_reg(cpu_reg, templ & 0xffff);
            do_cycles((cpu_mod == 3) ? 24 : 30);
            break;

        case 0x6c:
        case 0x6d: /* INM dst, DW/INS dst, DX */
            bits = 8 << (opcode & 1);
            if (rep_start()) {
                string_op(bits);
                do_cycle_i();

                if (in_rep != 0) {
                    completed = 0;
                    repeating = 1;

                    do_cycle_i();
                    if (irq_pending()) {
                        do_cycle_i();
                        rep_interrupt();
                    }

                    do_cycle_i();
                    /* decrement_register16() */
                    CX--;
                    if (CX == 0)
                        rep_end();
                    else
                        do_cycle_i();
                } else
                    do_cycle_i();
            }
            break;

        case 0x6e:
        case 0x6f: /* OUTM DW, src/OUTS DX, src */
            bits = 8 << (opcode & 1);
            if (rep_start()) {
                string_op(bits);
                do_cycles_i(3);

                if (in_rep != 0) {
                    completed = 0;
                    repeating = 1;

                    do_cycle_i();
                    /* decrement_register16() */
                    CX--;

                    if (irq_pending()) {
                        do_cycles_i(2);
                        rep_interrupt();
                    } else {
                        do_cycles_i(2);

                        if (CX == 0)
                            rep_end();
                        else
                            do_cycle_i();
                    }
                }
            }
            break;

        default:
            do_cycles_nx_i(2);    /* Guess, based on NOP. */
            break;
    }
}

/* Executes a single instruction. */
void
execute_instruction(void)
{
    uint8_t       temp = 0;
    uint8_t       tempb;
    uint8_t       nests;

    int8_t        rel8;
    int8_t        temps;

    uint16_t      addr;
    uint16_t      jump = 0;
    uint16_t      new_cs;
    uint16_t      new_ip;
    uint16_t      tempw;
    uint16_t      zero_cond;
    uint16_t      old_cs;
    uint16_t      old_ip;
    uint16_t      old_flags;
    uint16_t      prod16;
    uint16_t      tempw_int;
    uint16_t      size;
    uint16_t      tempbp;
    uint16_t      src16;

    int16_t       rel16;
    int16_t       temps16;

    uint32_t      prod32;
    uint32_t      templ;
    uint32_t      templ2 = 0;

    int           bits;
    int           negate;
    int           tempws  = 0;
    int           tempws2 = 0;

    completed = 1;

    if (nx) {
        do_cycle();
        nx = 0;
    } else if (!modrm_loaded)
        do_cycle();

    if (group_delay) {
        group_delay = 0;
        do_cycle();
        nx = 0;
    }

    if (!is_nec && (opcode >= 0xc0) && (opcode <= 0xc1))
        opcode |= 0x02;

    if (is_nec && !(cpu_state.flags & MD_FLAG)) {
        i8080_step(&emulated_processor);
        set_if(emulated_processor.iff);
        if (retem)
            jump = 1;
        retem = 0;
        do_cycles(emulated_processor.cyc);
        emulated_processor.cyc = 0;
    } else if (is_nec && in_0f)
        execvx0_0f();
    else if (is_nec && ((opcode & 0xf0) == 0x60))
        execvx0_6x(&jump);
    else  switch (opcode) {
        case 0x00: /* ADD r/m8, r8; r8, r/m8; al, imm8 */
        case 0x02:
        case 0x04:
        case 0x08: /* OR  r/m8, r8; r8, r/m8; al, imm8 */
        case 0x0a:
        case 0x0c:
        case 0x10: /* ADC r/m8, r8; r8, r/m8; al, imm8 */
        case 0x12:
        case 0x14:
        case 0x18: /* SBB r/m8, r8; r8, r/m8; al, imm8 */
        case 0x1a:
        case 0x1c:
        case 0x20: /* AND r/m8, r8; r8, r/m8; al, imm8 */
        case 0x22:
        case 0x24:
        case 0x28: /* SUB r/m8, r8; r8, r/m8; al, imm8 */
        case 0x2a:
        case 0x2c:
        case 0x30: /* XOR r/m8, r8; r8, r/m8; al, imm8 */
        case 0x32:
        case 0x34:
            bits = 8;
            /* read_operand8() */
            if (opcode & 0x04) {
                cpu_data   = biu_pfq_fetch();
                cpu_dest   = get_accum(bits); /* AX/AL */
                cpu_src    = cpu_data;
            } else {
                if (cpu_mod != 3)
                    do_cycles_i(2);    /* load_operand() */
                tempw      = get_ea();
                if (opcode & 2) {
                    cpu_dest = get_reg(cpu_reg);
                    cpu_src  = tempw;
                } else {
                    cpu_dest = tempw;
                    cpu_src  = get_reg(cpu_reg);
                }
            }
            do_cycles_nx_i(2);
            if (!(opcode & 0x06) && (cpu_mod != 3))
                do_cycles_i(2);

            /* math_op8() */
            math_op((opcode >> 3) & 7);
            /* write_operand8() */
            if (opcode & 0x04)
                set_accum(bits, cpu_data);
            else {
                if (opcode & 2)
                    set_reg(cpu_reg, cpu_data);
                else
                    set_ea(cpu_data);
            }
            break;

        case 0x01: /* ADD r/m16, r16; r16, r/m16; ax, imm16 */
        case 0x03:
        case 0x05:
        case 0x09: /* OR  r/m16, r16; r16, r/m16; ax, imm16 */
        case 0x0b:
        case 0x0d:
        case 0x11: /* ADC r/m16, r16; r16, r/m16; ax, imm16 */
        case 0x13:
        case 0x15:
        case 0x19: /* SBB r/m16, r16; r16, r/m16; ax, imm16 */
        case 0x1b:
        case 0x1d:
        case 0x21: /* AND r/m16, r16; r16, r/m16; ax, imm16 */
        case 0x23:
        case 0x25:
        case 0x29: /* SUB r/m16, r16; r16, r/m16; ax, imm16 */
        case 0x2b:
        case 0x2d:
        case 0x31: /* XOR r/m16, r16; r16, r/m16; ax, imm16 */
        case 0x33:
        case 0x35:
            bits = 16;
            nx = 1;
            /* read_operand16() */
            if (opcode & 0x04) {
                cpu_data   = biu_pfq_fetch();
                cpu_dest   = get_accum(bits); /* AX/AL */
                cpu_src    = cpu_data;
            } else {
                if (cpu_mod != 3)
                    do_cycles_i(is_nec ? 1 : 2);    /* load_operand() */
                tempw      = get_ea();
                if (opcode & 2) {
                    cpu_dest = get_reg(cpu_reg);
                    cpu_src  = tempw;
                } else {
                    cpu_dest = tempw;
                    cpu_src  = get_reg(cpu_reg);
                }
            }
            do_cycles_nx_i(2);
            if (!(opcode & 0x06) && (cpu_mod != 3))
                do_cycles_i(2);

            /* math_op16() */
            math_op((opcode >> 3) & 7);
            /* write_operand16() */
            if (opcode & 0x04)
                set_accum(bits, cpu_data);
            else {
                if (opcode & 0x02)
                    set_reg(cpu_reg, cpu_data);
                else
                    set_ea(cpu_data);
            }
            break;

        case 0x06:
        case 0x0e:
        case 0x16:
        case 0x1e: /* PUSH seg */
            do_cycles_i(3);
            push(&(_opseg[(opcode >> 3) & 0x03]->seg));
            break;

        case 0x07:
        case 0x17:
        case 0x1f: /* POP seg */
            load_seg(pop(), _opseg[(opcode >> 3) & 0x03]);
            /* All POP segment instructions suppress interrupts for one instruction. */
            noint = 1;
            break;

        case 0x0f:
            if (!is_nec) {
                load_cs(pop());
                /* All POP segment instructions suppress interrupts for one instruction. */
                noint = 1;
            }
            break;

        case 0x26: /* ES: */
        case 0x2e: /* CS: */
        case 0x36: /* SS: */
        case 0x3e: /* DS: */
            break;

        case 0x27: /* DAA */
            do_cycles_nx_i(3);
            daa();
            break;

        case 0x2f: /* DAS */
            do_cycles_nx_i(3);
            das();
            break;

        case 0x37: /* AAA */
            aaa();
            break;

        case 0x38: /* CMP r/m8, r8; r8, r/m8; al, imm8 */
        case 0x3a:
        case 0x3c:
            bits = 8;
            /* read_operand8() */
            if (opcode & 0x04) {
                cpu_data   = biu_pfq_fetch();
                cpu_dest   = get_accum(bits); /* AX/AL */
                cpu_src    = cpu_data;
            } else {
                if (cpu_mod != 3)
                    do_cycles_i(2);    /* load_operand() */
                tempw      = get_ea();
                if (opcode & 2) {
                    cpu_dest = get_reg(cpu_reg);
                    cpu_src  = tempw;
                } else {
                    cpu_dest = tempw;
                    cpu_src  = get_reg(cpu_reg);
                }
            }

            do_cycles_nx_i(2);

            /* math_op8() */
            math_op(7);
            break;

        case 0x39: /* CMP r/m16, r16; r16, r/m16; ax, imm16 */
        case 0x3b:
        case 0x3d:
            bits = 16;
            /* read_operand16() */
            if (opcode & 0x04) {
                cpu_data   = biu_pfq_fetch();
                cpu_dest   = get_accum(bits); /* AX/AL */
                cpu_src    = cpu_data;
            } else {
                if (cpu_mod != 3)
                    do_cycles_i(2);    /* load_operand() */
                tempw      = get_ea();
                if (opcode & 2) {
                    cpu_dest = get_reg(cpu_reg);
                    cpu_src  = tempw;
                } else {
                    cpu_dest = tempw;
                    cpu_src  = get_reg(cpu_reg);
                }
            }

            do_cycles_nx_i((opcode == 0x3d) ? 1 : 2);

            /* math_op16() */
            math_op(7);
            break;

        case 0x3f: /*AAS*/
            aas();
            break;

        case 0x40 ... 0x47: /* INC r16 */
            /* read_operand16() */
            cpu_dest = cpu_state.regs[opcode & 7].w;
            cpu_src  = 1;
            bits     = 16;
            /* math_op16() */
            cpu_data = cpu_dest + cpu_src;
            set_of_add(bits);
            do_af();
            set_pzs(16);
            /* write_operand16() */
            cpu_state.regs[opcode & 7].w = cpu_data;

            do_cycles_nx(1);
            break;

        case 0x48 ... 0x4f: /* DEC r16 */
            /* read_operand16() */
            cpu_dest = cpu_state.regs[opcode & 7].w;
            cpu_src  = 1;
            bits     = 16;
            /* math_op16() */
            cpu_data = cpu_dest - cpu_src;
            set_of_sub(bits);
            do_af();
            set_pzs(16);
            /* write_operand16() */
            cpu_state.regs[opcode & 7].w = cpu_data;

            do_cycles_nx(1);
            break;

        case 0x50 ... 0x57: /* PUSH r16 */
            do_cycles_i(3);

            push(&(cpu_state.regs[opcode & 0x07].w));
            break;

        case 0x58 ... 0x5f: /* POP r16 */
            cpu_state.regs[opcode & 0x07].w = pop();
            do_cycle_nx_i();
            break;

        case 0x60 ... 0x7f: /* JMP rel8 */
            switch ((opcode >> 1) & 0x07) {
                case 0x00:
                    jump = cpu_state.flags & V_FLAG;
                    break;
                case 0x01:
                    jump = cpu_state.flags & C_FLAG;
                    break;
                case 0x02:
                    jump = cpu_state.flags & Z_FLAG;
                    break;
                case 0x03:
                    jump = cpu_state.flags & (C_FLAG | Z_FLAG);
                    break;
                case 0x04:
                    jump = cpu_state.flags & N_FLAG;
                    break;
                case 0x05:
                    jump = cpu_state.flags & P_FLAG;
                    break;
                case 0x06:
                    jump = (!!(cpu_state.flags & N_FLAG)) != (!!(cpu_state.flags & V_FLAG));
                    break;
                case 0x07:
                    jump = (cpu_state.flags & Z_FLAG) ||
                            ((!!(cpu_state.flags & N_FLAG)) != (!!(cpu_state.flags & V_FLAG)));
                    break;
            }
            if (opcode & 1)
                jump = !jump;

            rel8 = (int8_t) biu_pfq_fetchb();
            new_ip = cpu_state.pc + rel8;
            if (!is_nec)
                do_cycle_i();

            if (jump)
                reljmp(new_ip, 1);
            break;

        case 0x80: /* ADD/OR/ADC/SBB/AND/SUB/XOR/CMP r/m8, imm8 */
        case 0x82:
            bits = 8;
            /* read_opreand8() */
            if (cpu_mod != 3)
                do_cycles_i(2);    /* load_operand() */
            cpu_data = get_ea();
            cpu_dest = cpu_data;
            cpu_src = biu_pfq_fetchb() | 0xff00;

            do_cycle_nx();
            math_op((rmdat & 0x38) >> 3);

            if (cpu_mod != 3)
                do_cycles_i(2);

            /* write_operand8() */
            if (cpu_alu_op != 7)
                set_ea(cpu_data);
            break;

        case 0x81: /* ADD/OR/ADC/SBB/AND/SUB/XOR/CMP r/m16, imm16 */
            bits = 16;
            /* read_opreand16() */
            if (cpu_mod != 3)
                do_cycles_i(2);    /* load_operand() */
            cpu_data = get_ea();
            cpu_dest = cpu_data;
            cpu_src = biu_pfq_fetchw();

            do_cycle_nx();
            math_op((rmdat & 0x38) >> 3);

            if (cpu_mod != 3) {
                if (cpu_alu_op != 7)
                    do_cycles_i(2);
                else {
                    do_cycles_nx_i(2);
                }
            }

            /* write_operand16() */
            if (cpu_alu_op != 7)
                set_ea(cpu_data);
            break;

        case 0x83: /* ADD/OR/ADC/SBB/AND/SUB/XOR/CMP r/m16, imm8 (sign-extended) */
            bits = 16;
            /* read_opreand16() */
            if (cpu_mod != 3)
                do_cycles_i(2);    /* load_operand() */
            cpu_data = get_ea();
            cpu_dest = cpu_data;
            cpu_src = sign_extend(biu_pfq_fetchb());

            do_cycle_nx();
            math_op((rmdat & 0x38) >> 3);

            if (cpu_mod != 3)
                do_cycles_i(2);

            /* write_operand16() */
            if (cpu_alu_op != 7)
                set_ea(cpu_data);
            break;

        case 0x84: /* TEST r/m8, r8 */
            bits = 8;
            /* read_operand8() */
            if (cpu_mod != 3)
                do_cycles_i(2);    /* load_operand() */
            cpu_data = get_ea();

            /* math_op8() */
            test(bits, cpu_data, get_reg(cpu_reg));

            do_cycles_nx_i(2);
            break;

        case 0x85: /* TEST r/m16, r16 */
            bits = 16;
            /* read_operand16() */
            if (cpu_mod != 3)
                do_cycles_i(2);    /* load_operand() */
            cpu_data = get_ea();

            /* math_op16() */
            test(bits, cpu_data, get_reg(cpu_reg));

            do_cycles_nx_i(2);
            break;

        case 0x86: /* XHG r8, r/m8 */
            bits = 8;
            /* read_operand8() */
            if (cpu_mod != 3)
                do_cycles_i(2);    /* load_operand() */
            cpu_data = get_ea();
            cpu_src  = get_reg(cpu_reg);

            do_cycles_nx(3);

            if (cpu_mod != 3)
                do_cycles(2);

            /* write_operand8() */
            set_reg(cpu_reg, cpu_data);
            set_ea(cpu_src);
            break;

        case 0x87: /* XCHG r16, r/m16 */
            bits = 16;
            /* read_operand16() */
            if (cpu_mod != 3)
                do_cycles_i(2);    /* load_operand() */
            cpu_data = get_ea();
            cpu_src  = get_reg(cpu_reg);

            do_cycles_nx(3);

            if (cpu_mod != 3)
                do_cycles(2);

            /* write_operand16() */
            set_reg(cpu_reg, cpu_data);
            set_ea(cpu_src);
            break;

        case 0x88: /* MOV r/m8, r8; MOV r8, r/m8 */
        case 0x8a:
            bits = 8;
            if (cpu_mod != 3)
                do_cycles_i(2);    /* load_operand() */
            do_cycle_nx();
            /* read_operand8() */
            if (opcode == 0x88)
                tempb = get_reg(cpu_reg);
            else
                tempb = get_ea();

            if ((opcode == 0x88) && (cpu_mod != 3))
                do_cycles_i(2);

            /* write_operand8() */
            if (opcode == 0x88)
                set_ea(tempb);
            else
                set_reg(cpu_reg, tempb);
            break;

        case 0x89: /* MOV r/m16, r16; MOV r16, r/m16 */
        case 0x8b:
            bits = 16;
            if (cpu_mod != 3)
                do_cycles_i(2);    /* load_operand() */
            do_cycle_nx();
            /* read_operand16() */
            if (opcode == 0x89)
                tempw = get_reg(cpu_reg);
            else
                tempw = get_ea();

            if ((opcode == 0x89) && (cpu_mod != 3))
                do_cycles_i(2);

            /* write_operand16() */
            if (opcode == 0x89)
                set_ea(tempw);
            else
                set_reg(cpu_reg, tempw);
            break;

        case 0x8c: /* MOV r/m16, SReg; MOV SReg, r/m16 */
        case 0x8e:
            bits = 16;
            if (cpu_mod != 3)
                do_cycles_i(2);    /* load_operand() */
            if ((opcode == 0x8c) && (cpu_mod != 3))
                do_cycle_i();
            /* read_operand16() */
            if (opcode == 0x8c)
                tempw = _opseg[(rmdat & 0x18) >> 3]->seg;
            else
                tempw = geteaw();
            /* write_operand16() */
            if (opcode == 0x8c)
                seteaw(tempw);
            else {
                if ((rmdat & 0x18) == 0x08)
                    load_cs(tempw);
                else
                    load_seg(tempw, _opseg[(rmdat & 0x18) >> 3]);
                if (((rmdat & 0x18) >> 3) == 2)
                    noint = 1;
            }
            break;

        case 0x8d: /* LEA */
            if (cpu_mod != 3)
                do_cycles_i(2);    /* load_operand() */
            cpu_state.regs[cpu_reg].w = cpu_state.eaaddr;
            break;

        case 0x8f: /* POP r/m16 */
            if (cpu_mod != 3)
                do_cycles_i(2);    /* load_operand() */
            do_cycle_i();
            /* pop_u16() */
            cpu_src  = cpu_state.eaaddr;
            cpu_data = pop();
            do_cycle_i();
            if (cpu_mod != 3)
                do_cycles_i(2);
            /* write_operand16() */
            cpu_state.eaaddr = cpu_src;
            seteaw(cpu_data);
            break;

        case 0x90 ... 0x97: /* XCHG AX, r */
            cpu_data = cpu_state.regs[opcode & 7].w;
            do_cycles_nx_i(2);
            cpu_state.regs[opcode & 7].w = AX;
            AX = cpu_data;
            break;

        case 0x98: /* CBW */
            sign_extend_al();
            break;

        case 0x99: /* CWD */
            sign_extend_ax();
            break;

       case 0x9a: /* CALLF */
            /* read_operand_faraddr() */
            new_ip = biu_pfq_fetchw();
            new_cs = biu_pfq_fetchw();
 
            farcall(new_cs, new_ip, 1);

            jump = 1;
            break;

        case 0x9b: /* WAIT */
            do_cycles(3);
            break;

        case 0x9c: /* PUSHF */
            do_cycles(3);
            /* push_flags() */
            if (is_nec)
                tempw = (cpu_state.flags & 0x8fd7) | 0x7000;
            else
                tempw = (cpu_state.flags & 0x0fd7) | 0xf000;
            push(&tempw);
            break;

        case 0x9d: /* POPF */
            /* pop_flags() */
            if (is_nec && cpu_md_write_disable)
                cpu_state.flags = pop() | 0x8002;
            else
                cpu_state.flags = pop() | 0x0002;
            sync_to_i8080();
            break;

        case 0x9e: /* SAHF */
            /* store_flags() */
            cpu_state.flags = (cpu_state.flags & 0xff02) | AH;
            break;

        case 0x9f: /*LAHF*/
            /* load_flags() */
            /* set_register8() */
            AH = cpu_state.flags & 0xd7;
            break;

        case 0xa0: /* MOV al, offset8 */
            bits = 8;
            /* read_operand8() */
            cpu_state.eaaddr = biu_pfq_fetchw();
            tempb = readmem(ovr_seg ? *ovr_seg : ds);
            /* set_register8() */
            set_accum(bits, tempb);
            break;

        case 0xa1: /* MOV al, offset16 */
            bits = 16;
            /* read_operand16() */
            cpu_state.eaaddr = biu_pfq_fetchw();
            tempw = readmem(ovr_seg ? *ovr_seg : ds);
            /* set_register16() */
            set_accum(bits, tempw);
            break;

        case 0xa2: /* MOV offset8, Al */
            bits = 8;
            tempb = get_accum(bits);
            /* write_operand8() */
            cpu_state.eaaddr = biu_pfq_fetchw();
            writemem((ovr_seg ? *ovr_seg : ds), tempb);
            break;

        case 0xa3: /* MOV offset16, AX */
            bits = 16;
            tempw = get_accum(bits);
            /* write_operand16() */
            cpu_state.eaaddr = biu_pfq_fetchw();
            writemem((ovr_seg ? *ovr_seg : ds), tempw);
            break;

        case 0xa4: /* MOVSB & MOVSW */
        case 0xa5:
            bits = 8 << (opcode & 1);

            if (rep_start()) {
                string_op(bits);
                do_cycle_i();

                if (in_rep != 0) {
                    completed = 0;
                    repeating = 1;

                    /* decrement_register16() */
                    CX--;

                    if (irq_pending()) {
                        do_cycles_i(2);
                        rep_interrupt();
                    } else {
                        do_cycles_i(2);

                        if (CX == 0)
                            rep_end();
                        else
                            do_cycle_i();
                    }
                } else
                    do_cycle_i();
            }
            break;

        case 0xa6: /* CMPSB, CMPSW, SCASB, SCASW */
        case 0xa7:
        case 0xae:
        case 0xaf:
            bits = 8 << (opcode & 1);
            if (rep_start()) {
                string_op(bits);

                if (in_rep) {
                    uint8_t end = 0;

                    completed = 0;
                    repeating = 1;

                    do_cycle_i();
                    /* decrement_register16() */
                    CX--;

                    if ((!!(cpu_state.flags & (rep_c_flag ? C_FLAG : Z_FLAG))) == (in_rep == 1)) {
                        rep_end();
                        do_cycle_i();
                        end = 1;
                    }

                    if (!end) {
                        do_cycle_i();

                        if (irq_pending()) {
                            do_cycle_i();
                            rep_interrupt();
                        }

                        do_cycle_i();
                        if (CX == 0)
                            rep_end();
                        else
                            do_cycle_i();
                    } else
                        do_cycle_i();
                }
            }
            break;

        case 0xa8: /* TEST al, imm8 */
            bits = 8;
            /* read_operand8() */
            cpu_data = biu_pfq_fetch();
            /* math_op8() */
            test(bits, get_accum(bits), cpu_data);
            break;

        case 0xa9: /* TEST ax, imm16 */
            bits = 16;
            /* read_operand16() */
            cpu_data = biu_pfq_fetch();
            /* math_op16() */
            test(bits, get_accum(bits), cpu_data);
            break;

        case 0xaa: /* STOSB & STOSW */
        case 0xab:
            bits = 8 << (opcode & 1);
            if (rep_start()) {
                string_op(bits);
                do_cycle_i();

                if (in_rep != 0) {
                    completed = 0;
                    repeating = 1;

                    do_cycle_i();
                    if (irq_pending()) {
                        do_cycle_i();
                        rep_interrupt();
                    }

                    do_cycle_i();
                    /* decrement_register16() */
                    CX--;
                    if (CX == 0)
                        rep_end();
                    else
                        do_cycle_i();
                } else
                    do_cycle_i();
            }
            break;

        case 0xac: /* LODSB * LODSW */
        case 0xad:
            bits = 8 << (opcode & 1);
            if (rep_start()) {
                string_op(bits);
                do_cycles_i(3);

                if (in_rep != 0) {
                    completed = 0;
                    repeating = 1;

                    do_cycle_i();
                    /* decrement_register16() */
                    CX--;

                    if (irq_pending()) {
                        do_cycles_i(2);
                        rep_interrupt();
                    } else {
                        do_cycles_i(2);

                        if (CX == 0)
                            rep_end();
                        else
                            do_cycle_i();
                    }
                }
            }
            break;

        case 0xb0 ... 0xb7: /* MOV r8, imm8 */
            bits = 8;
            /* read_operand8() */
            tempb = biu_pfq_fetchb();
            /* set_register8() */
            if (opcode & 0x04)
                cpu_state.regs[opcode & 0x03].b.h = tempb;
            else
                cpu_state.regs[opcode & 0x03].b.l = tempb;
            do_cycle_i();
            break;

        case 0xb8 ... 0xbf: /* MOV r16, imm16 */
            bits = 16;
            /* read_operand16() */
            tempw = biu_pfq_fetchw();
            /* set_register16() */
            cpu_state.regs[opcode & 0x07].w = tempw;
            break;

        case 0xc0: /* rot imm8 */
        case 0xc1:
            /* rot rm */
            bits = 8 << (opcode & 1);
            if (cpu_mod != 3)
                do_cycles_i(is_nec ? 1 : 2);    /* load_operand() */
            /* read_operand() */
            cpu_data = get_ea();
            cpu_src = biu_pfq_fetchb();
            if (is186 && !is_nec)
                cpu_src &= 0x1F;
            do_cycles_i(6);
            if (cpu_src > 0) {
                for (uint8_t i = 0; i < cpu_src; i++)
                    do_cycles_i(4);
            }
            if (cpu_mod != 3)
                 do_cycle_i();
            /* bitshift_op() */
            while (cpu_src != 0) {
                cpu_dest = cpu_data;
                oldc     = cpu_state.flags & C_FLAG;
                switch (rmdat & 0x38) {
                    case 0x00: /* ROL */
                        set_cf(top_bit(cpu_data, bits));
                        cpu_data <<= 1;
                        cpu_data |= ((cpu_state.flags & C_FLAG) ? 1 : 0);
                        set_of_rotate(bits);
                        set_af(0);
                        break;
                    case 0x08: /* ROR */
                        set_cf((cpu_data & 1) != 0);
                        cpu_data >>= 1;
                        if (cpu_state.flags & C_FLAG)
                            cpu_data |= (!(opcode & 1) ? 0x80 : 0x8000);
                        set_of_rotate(bits);
                        set_af(0);
                        break;
                    case 0x10: /* RCL */
                        set_cf(top_bit(cpu_data, bits));
                        cpu_data = (cpu_data << 1) | (oldc ? 1 : 0);
                        set_of_rotate(bits);
                        set_af(0);
                        break;
                    case 0x18: /* RCR */
                        set_cf((cpu_data & 1) != 0);
                         cpu_data >>= 1;
                        if (oldc)
                            cpu_data |= (!(opcode & 0x01) ? 0x80 : 0x8000);
                        set_cf((cpu_dest & 1) != 0);
                        set_of_rotate(bits);
                        set_af(0);
                        break;
                    case 0x20: /* SHL */
                        set_cf(top_bit(cpu_data, bits));
                        cpu_data <<= 1;
                        set_of_rotate(bits);
                        set_af((cpu_data & 0x10) != 0);
                        set_pzs(bits);
                        break;
                    case 0x28: /* SHR */
                        set_cf((cpu_data & 1) != 0);
                        cpu_data >>= 1;
                        set_of_rotate(bits);
                        set_af(0);
                        set_pzs(bits);
                        break;
                    case 0x30: /* SETMO - undocumented? */
                        bitwise(bits, 0xffff);
                        set_cf(0);
                        set_of_rotate(bits);
                        set_af(0);
                        set_pzs(bits);
                        break;
                    case 0x38: /* SAR */
                        set_cf((cpu_data & 1) != 0);
                        cpu_data >>= 1;
                        if (!(opcode & 1))
                            cpu_data |= (cpu_dest & 0x80);
                        else
                            cpu_data |= (cpu_dest & 0x8000);
                        set_of_rotate(bits);
                        set_af(0);
                        set_pzs(bits);
                        break;

                    default:
                        break;
                }
                --cpu_src;
            }

            if (opcode <= 0xd1) {
                if (cpu_mod != 3)
                    do_cycle_i();
            }

            /* write_operand() */
            set_ea(cpu_data);
            break;

        case 0xc2: /* RETN imm16 */
            bits = 8;
            cpu_src = biu_pfq_fetchw();
            do_cycle_i();
            new_ip = pop();
            biu_suspend_fetch();
            set_ip(new_ip);

            do_cycles_i(2);
            biu_queue_flush();
            do_cycles_i(3);

            /* release() */
            SP += cpu_src;

            jump = 1;
            break;

        case 0xc3: /* RETN */
            bits = 8;
            new_ip = pop();
            biu_suspend_fetch();
            set_ip(new_ip);
            do_cycle_i();
            biu_queue_flush();
            do_cycles_i(2);

            jump = 1;
            break;

        case 0xc4: /* LES */
        case 0xc5: /* LDS */
            bits = 16;
            if (cpu_mod != 3)
                do_cycles_i(2);    /* load_operand() */

            do_cycles_i(2);

            /* read_operand_farptr() */
            read_ea(1, bits);
            cpu_state.regs[cpu_reg].w = cpu_data;
            read_ea2(bits);

            /* write_operand16() */
            load_seg(cpu_data, (opcode & 0x01) ? &cpu_state.seg_ds : &cpu_state.seg_es);
            break;

        case 0xc6: /* MOV r/m8, imm8 */
            bits = 8;
            /* read_operand8() */
            if (cpu_mod != 3)
                do_cycles_i(2);    /* load_operand() */
            cpu_data = biu_pfq_fetch();
            do_cycles(2);
            /* write_operand8() */
            set_ea(cpu_data);
            break;

        case 0xc7: /* MOV r/m16, imm16 */
            bits = 16;
            /* read_operand16() */
            if (cpu_mod != 3)
                do_cycles_i(2);    /* load_operand() */
            cpu_data = biu_pfq_fetch();
            do_cycle_i();
            /* write_operand16() */
            set_ea(cpu_data);
            break;

        case 0xc8: /* RETF imm16 */
            if (is_nec) {
                /* ENTER/PREPARE */
                tempw_int = 0;
                size      = biu_pfq_fetchw();
                nests     = biu_pfq_fetchb();

                push(&BP);
                tempw_int = SP;
                if (nests > 0) {
                     while (--nests) {
                        tempbp = 0;
                        BP -= 2;
                        tempbp = readmemw(ss, BP);
                        push(&tempbp);
                    }
                    push(&tempw_int);
                }
                BP = tempw_int;
                SP -= size;
                break;
            } else
                fallthrough;
        case 0xca:
            bits = 16;
            /* read_operand16() */
            cpu_src = biu_pfq_fetchw();
            farret(1);
            /* release() */
            SP += cpu_src;
            do_cycle_i();
            jump = 1;
            break;

        case 0xc9: /* RETF */
            if (is_nec) {
                /* LEAVE/DISPOSE */
                SP      = BP;
                BP      = pop();
                break;
            } else
                fallthrough;
        case 0xcb:
            bits = 16;
            do_cycle_i();
            farret(1);
            jump = 1;
            break;

        case 0xcc: /* INT 3 */
            do_cycles_i(4);
            int3();
            jump = 1;
            break;

        case 0xcd: /* INT imm8 */
            /* read_operand8() */
            temp = biu_pfq_fetchb();
            do_cycle_i();
            sw_int(temp);
            jump = 1;
            break;

        case 0xce: /* INTO */
            if (cpu_state.flags & V_FLAG) {
                sw_int(4);
                jump = 1;
            }
            break;

        case 0xcf: /* IRET */
            iret_routine();
            if (is_nec && !(cpu_state.flags & MD_FLAG))
                sync_to_i8080();
            jump = 1;
            break;

        case 0xd0: /* ROL, ROR, RCL, RCR, SHL, SHR, SAR:  r/m 8, 0x01 */
        case 0xd1: /* ROL, ROR, RCL, RCR, SHL, SHR, SAR:  r/m 16, 0x01 */
        case 0xd2: /* ROL, ROR, RCL, RCR, SHL, SHR, SAR:  r/m 8, cl */
        case 0xd3: /* ROL, ROR, RCL, RCR, SHL, SHR, SAR:  r/m 16, cl */
            /* rot rm */
            bits = 8 << (opcode & 1);
            if (cpu_mod != 3)
                do_cycles_i(is_nec ? 1 : 2);    /* load_operand() */
            /* read_operand() */
            cpu_data = get_ea();
            if ((opcode & 2) == 0)
                cpu_src = 1;
            else
                cpu_src = CL;
            if (is186 && !is_nec)
                cpu_src &= 0x1F;
            if (opcode >= 0xd2) {
                do_cycles_i(6);
                if (CL > 0) {
                    for (uint8_t i = 0; i < CL; i++)
                        do_cycles_i(4);
                }
                if (cpu_mod != 3)
                    do_cycle_i();
            }
            /* bitshift_op() */
            while (cpu_src != 0) {
                cpu_dest = cpu_data;
                oldc     = cpu_state.flags & C_FLAG;
                if (is_nec && ((rmdat & 0x38) == 0x30))
                    rmdat &= 0xef;    /* Make it 0x20, so it aliases to SHL. */
                switch (rmdat & 0x38) {
                    case 0x00: /* ROL */
                        set_cf(top_bit(cpu_data, bits));
                        cpu_data <<= 1;
                        cpu_data |= ((cpu_state.flags & C_FLAG) ? 1 : 0);
                        set_of_rotate(bits);
                        set_af(0);
                        break;
                    case 0x08: /* ROR */
                        set_cf((cpu_data & 1) != 0);
                        cpu_data >>= 1;
                        if (cpu_state.flags & C_FLAG)
                            cpu_data |= (!(opcode & 1) ? 0x80 : 0x8000);
                        set_of_rotate(bits);
                        set_af(0);
                        break;
                    case 0x10: /* RCL */
                        set_cf(top_bit(cpu_data, bits));
                        cpu_data = (cpu_data << 1) | (oldc ? 1 : 0);
                        set_of_rotate(bits);
                        set_af(0);
                        break;
                    case 0x18: /* RCR */
                        set_cf((cpu_data & 1) != 0);
                         cpu_data >>= 1;
                        if (oldc)
                            cpu_data |= (!(opcode & 0x01) ? 0x80 : 0x8000);
                        set_cf((cpu_dest & 1) != 0);
                        set_of_rotate(bits);
                        set_af(0);
                        break;
                    case 0x20: /* SHL */
                        set_cf(top_bit(cpu_data, bits));
                        cpu_data <<= 1;
                        set_of_rotate(bits);
                        set_af((cpu_data & 0x10) != 0);
                        set_pzs(bits);
                        break;
                    case 0x28: /* SHR */
                        set_cf((cpu_data & 1) != 0);
                        cpu_data >>= 1;
                        set_of_rotate(bits);
                        set_af(0);
                        set_pzs(bits);
                        break;
                    case 0x30: /* SETMO - undocumented? */
                        bitwise(bits, 0xffff);
                        set_cf(0);
                        set_of_rotate(bits);
                        set_af(0);
                        set_pzs(bits);
                        break;
                    case 0x38: /* SAR */
                        set_cf((cpu_data & 1) != 0);
                        cpu_data >>= 1;
                        if (!(opcode & 1))
                            cpu_data |= (cpu_dest & 0x80);
                        else
                            cpu_data |= (cpu_dest & 0x8000);
                        set_of_rotate(bits);
                        set_af(0);
                        set_pzs(bits);
                        break;

                    default:
                        break;
                }
                --cpu_src;
            }

            if (opcode <= 0xd1) {
                if (cpu_mod != 3)
                    do_cycle_i();
            }

            /* write_operand() */
            set_ea(cpu_data);
            break;

        case 0xd4: /* AAM */
            /* read_operand8() */
            cpu_src = biu_pfq_fetchb();

            if (is_nec) {
                if (!cpu_src)
                    cpu_src = 10;
                AH = AL / cpu_src;
                AL %= cpu_src;
                cpu_data = AL;
                set_pzs(8);
                do_cycles(12);
            } else {
                /* Confirmed to be identical on V20/V30 to 808x, per
                   XTIDE working correctly on both (it uses AAM with
                   parameter other than 10. */
                /* aam() */
                if (x86_div(AL, 0)) {
                    cpu_data = AL;
                    set_pzs(8);
                }
            }
            break;

        case 0xd5: /* AAD */
            /* read_operand8() */
            cpu_src = biu_pfq_fetchb();

            if (is_nec) {
                cpu_src = 10;
                AL = (AH * cpu_src) + AL;
                AH = 0;
                cpu_data = AL;
                set_pzs(8);
                do_cycles(4);
            } else {
                if (is_nec)
                    cpu_src = 10;
                /* aad() */
                mul(cpu_src, AH);
                cpu_dest = AL;
                cpu_src  = cpu_data;
                add(8);
                AL = cpu_data;
                AH = 0x00;
                set_pzs(8);
            }
            break;

        case 0xd6: /* SALC */
            if (is_nec) {
                do_cycles(14);
                fallthrough;
            } else {
                AL = (cpu_state.flags & C_FLAG) ? 0xff : 0x00;
                break;
            }
        case 0xd7: /* XLAT */
            do_cycles_i(3);
            /* biu_read_u8() */
            cpu_state.eaaddr = (BX + AL) & 0xffff;
            tempb = readmemb((ovr_seg ? *ovr_seg : ds), cpu_state.eaaddr);
            /* set_register8() */
            AL = tempb;
            break;

        case 0xd8 ... 0xdf: /* ESC - FPU instructions. */
            /* read_operand16() */
            if (cpu_mod != 3)
                do_cycles_i(2);    /* load_operand() */
            tempw = cpu_state.pc;
            geteaw();
            /* fpu_op() */
            if (hasfpu) {
                if (fpu_softfloat) {
                    switch (opcode) {
                        case 0xd8:
                            ops_sf_fpu_8087_d8[(rmdat >> 3) & 0x1f](rmdat);
                            break;
                        case 0xd9:
                            ops_sf_fpu_8087_d9[rmdat & 0xff](rmdat);
                            break;
                        case 0xda:
                            ops_sf_fpu_8087_da[rmdat & 0xff](rmdat);
                            break;
                        case 0xdb:
                            ops_sf_fpu_8087_db[rmdat & 0xff](rmdat);
                            break;
                        case 0xdc:
                            ops_sf_fpu_8087_dc[(rmdat >> 3) & 0x1f](rmdat);
                            break;
                        case 0xdd:
                            ops_sf_fpu_8087_dd[rmdat & 0xff](rmdat);
                            break;
                        case 0xde:
                            ops_sf_fpu_8087_de[rmdat & 0xff](rmdat);
                            break;
                        case 0xdf:
                            ops_sf_fpu_8087_df[rmdat & 0xff](rmdat);
                            break;

                        default:
                             break;
                    }
                } else {
                    switch (opcode) {
                        case 0xd8:
                            ops_fpu_8087_d8[(rmdat >> 3) & 0x1f](rmdat);
                            break;
                        case 0xd9:
                            ops_fpu_8087_d9[rmdat & 0xff](rmdat);
                            break;
                        case 0xdA:
                            ops_fpu_8087_da[rmdat & 0xff](rmdat);
                            break;
                        case 0xdb:
                            ops_fpu_8087_db[rmdat & 0xff](rmdat);
                            break;
                        case 0xdc:
                            ops_fpu_8087_dc[(rmdat >> 3) & 0x1f](rmdat);
                            break;
                        case 0xdd:
                            ops_fpu_8087_dd[rmdat & 0xff](rmdat);
                            break;
                        case 0xde:
                            ops_fpu_8087_de[rmdat & 0xff](rmdat);
                            break;
                        case 0xdf:
                            ops_fpu_8087_df[rmdat & 0xff](rmdat);
                            break;

                        default:
                            break;
                    }
                }
            }
            cpu_state.pc = tempw; /* Do this as the x87 code advances it, which is needed on
                                     the 286+ core, but not here. */
            break;

        case 0xe0: /* LOOPNE & LOOPE */
        case 0xe1:
            /* decrement_register16() */
            --CX;
            do_cycles_i(2);

            zero_cond = !(cpu_state.flags & Z_FLAG);
            if (opcode == 0xe1)
                zero_cond = !zero_cond;

            /* read_operand8() */
            cpu_data = biu_pfq_fetchb();

            if ((CX != 0x0000) && zero_cond) {
                rel8 = (int8_t) cpu_data;
                new_ip = (cpu_state.pc + rel8) & 0xffff;
                reljmp(new_ip, 1);
                jump = 1;
                do_print = 0;
            } else {
                do_cycle_i();
                do_print = 1;
            }
            break;

        case 0xe2: /* LOOP */
            /* decrement_register16() */
            --CX;
            do_cycles_i(2);

            /* read_operand8() */
            cpu_data = biu_pfq_fetchb();

            if (CX != 0x0000) {
                rel8 = (int8_t) cpu_data;
                new_ip = (cpu_state.pc + rel8) & 0xffff;
                reljmp(new_ip, 1);
                jump = 1;
                do_print = 0;
            }
            if (!jump) {
                do_cycle();
                do_print = 1;
            }
            break;

        case 0xe3: /* JCXZ */
            do_cycles_i(2);
            /* read_operand8() */
            cpu_data = biu_pfq_fetchb();

            do_cycle_i();

            if (CX == 0x0000) {
                rel8 = (int8_t) cpu_data;
                new_ip = (cpu_state.pc + rel8) & 0xffff;
                reljmp(new_ip, 1);
                jump = 1;
            } else
                do_cycle_i();
            break;

        case 0xe4: /* IN al, imm8 */
            bits = 8;
            /* read_operand8() */
            cpu_data         = biu_pfq_fetchb();
            do_cycles_i(2);

            /* biu_io_read_u8() */
            cpu_state.eaaddr = cpu_data;
            cpu_io_vx0(bits, 0, cpu_state.eaaddr);
            /* set_register8() */
            break;

        case 0xe5: /* IN ax, imm8 */
            bits = 16;
            /* read_operand16() */
            cpu_data         = biu_pfq_fetchb();
            do_cycles_i(2);

            /* biu_io_read_u16() */
            cpu_state.eaaddr = cpu_data;
            cpu_io_vx0(bits, 0, cpu_state.eaaddr);
            /* set_register16() */
            break;

        case 0xe6: /* OUT imm8, al */
            bits = 8;
            /* read_operand8() */
            cpu_data         = biu_pfq_fetchb();
            /* read_operand8() */
            tempb            = AL;
            do_cycles_i(2);

            /* biu_io_write_u8() */
            cpu_state.eaaddr = cpu_data;
            cpu_data = tempb;
            cpu_io_vx0(bits, 1, cpu_state.eaaddr);
            break;

        case 0xe7: /* OUT imm8, ax */
            bits = 16;
            /* read_operand8() */
            cpu_data         = biu_pfq_fetchb();
            /* read_operand16() */
            tempw            = AX;
            do_cycles_i(2);

            /* biu_io_write_u16() */
            cpu_state.eaaddr = cpu_data;
            cpu_data = tempw;
            cpu_io_vx0(bits, 1, cpu_state.eaaddr);
            break;

        case 0xe8: /* CALL rel16 */
            /* read_operand16() */
            rel16 = (int16_t) biu_pfq_fetchw();

            biu_suspend_fetch();
            do_cycles_i(4);

            old_ip = cpu_state.pc;
            new_ip = cpu_state.pc + rel16;

            set_ip(new_ip);
            biu_queue_flush();
            do_cycles_i(3);

            push(&old_ip);
            jump = 1;
            break;

        case 0xe9: /* JMP rel16 */
            /* read_operand16() */
            rel16 = (int16_t) biu_pfq_fetchw();
            new_ip = (cpu_state.pc + rel16) & 0xffff;

            reljmp(new_ip, 1);
            jump = 1;
            break;

        case 0xea: /* JMP far [addr16:16] */
            /* read_operand_faraddr() */
            addr = biu_pfq_fetchw();
            tempw = biu_pfq_fetchw();
            load_cs(tempw);
            set_ip(addr);

            biu_suspend_fetch();
            do_cycles_i(2);
            biu_queue_flush();
            do_cycle_i();
            jump = 1;
            break;

        case 0xeb: /* JMP rel8 */
            /* read_operand8() */
            rel8 = (int8_t) biu_pfq_fetchb();
            new_ip = (cpu_state.pc + rel8) & 0xffff;

            reljmp(new_ip, 1);
            jump = 1;
            break;

        case 0xec: /* IN al, dx */
            bits = 8;
            /* read_operand8() */
            cpu_data         = DX;
            /* biu_io_read_u8() */
            cpu_state.eaaddr = cpu_data;
            cpu_io_vx0(bits, 0, cpu_state.eaaddr);
            /* set_register8() */
            break;

        case 0xed: /* IN ax, dx */
            bits = 16;
            /* read_operand16() */
            cpu_data         = DX;
            /* biu_io_read_u16() */
            cpu_state.eaaddr = cpu_data;
            cpu_io_vx0(bits, 0, cpu_state.eaaddr);
            /* set_register16() */
            break;

        case 0xee: /* OUT dx, al */
            bits = 8;
            /* read_operand8() */
            cpu_data         = DX;
            /* read_operand8() */
            tempb            = AL;
            do_cycle_i();

            /* biu_io_write_u8() */
            cpu_state.eaaddr = cpu_data;
            cpu_data = tempb;
            cpu_io_vx0(bits, 1, cpu_state.eaaddr);
            break;

        case 0xef: /* OUT dx, ax */
            bits = 16;
            /* read_operand8() */
            cpu_data         = DX;
            /* read_operand16() */
            tempw            = AX;
            do_cycle_i();

            /* biu_io_write_u16() */
            cpu_state.eaaddr = cpu_data;
            cpu_data = tempw;
            cpu_io_vx0(bits, 1, cpu_state.eaaddr);
            break;

        case 0xf0:
        case 0xf1: /* LOCK - F1 is alias */
            break;

        case 0xf2: /* REPNE */
        case 0xf3: /* REPE */
            break;

        case 0xf4: /* HLT */
            if (is_nec)
                in_hlt = 1;

            if (!repeating) {
                biu_suspend_fetch();
                // biu_queue_flush();
                do_cycles(2);
                /* TODO: biu_halt(); */
                    do_cycle();
            }

            do_cycle_i();
            do_cycle_i();
            do_cycle_i();
            if (irq_pending()) {
                halted    = 0;
                check_interrupts();
            } else {
                repeating = 1;
                completed = 0;

                halted    = 1;
            }

            if (is_nec)
                 in_hlt = 0;
            break;

        case 0xf5: /* CMC */
            cpu_state.flags ^= C_FLAG;
            break;

        case 0xf6: /* Miscellaneuous Opcode Extensions, r/m8, imm8 */
            bits = 8;
            if (cpu_mod != 3)
                do_cycles_i(is_nec ? 1 : 2);    /* load_operand() */
            negate = !!in_rep;

            if (is_nec && ((rmdat & 0x38) >= 0x20))  switch (rmdat & 0x38) {
                case 0x20: /* MUL */
                    /* read_operand8() */
                    cpu_data = get_ea();

                    AX = AL * cpu_data;
                    if (AH)
                        cpu_state.flags |= (C_FLAG | V_FLAG);
                    else
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);

                    do_cycles((cpu_mod == 3) ? 24 : 30);
                    break;
                case 0x28: /* IMUL */
                    /* read_operand8() */
                    cpu_data = get_ea();

                    tempws = (int) ((int8_t) AL) * (int) ((int8_t) cpu_data);
                    AX     = tempws & 0xffff;
                    if (((int16_t) AX >> 7) != 0 && ((int16_t) AX >> 7) != -1)
                        cpu_state.flags |= (C_FLAG | V_FLAG);
                    else
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);

                    do_cycles((cpu_mod == 3) ? 13 : 19);
                    break;
                case 0x30: /* DIV */
                    /* read_operand8() */
                    cpu_data = get_ea();

                    src16 = AX;
                    if (cpu_data)
                        tempw = src16 / cpu_data;
                    if (cpu_data && !(tempw & 0xff00)) {
                        AH = src16 % cpu_data;
                        AL = (src16 / cpu_data) & 0xff;
                        cpu_state.flags |= 0x8D5;
                        cpu_state.flags &= ~1;
                    } else {
                        intr_routine(0, 0);
                        break;
                    }

                    do_cycles((cpu_mod == 3) ? 21 : 27);
                    break;
                case 0x38: /* IDIV */
                    /* read_operand8() */
                    cpu_data = get_ea();

                    tempws = (int) (int16_t) AX;
                    if (cpu_data != 0)
                        tempws2 = tempws / (int) ((int8_t) cpu_data);
                    temps = tempws2 & 0xff;
                    if (cpu_data && ((int) temps == tempws2)) {
                        AH = (tempws % (int) ((int8_t) cpu_data)) & 0xff;
                        AL = tempws2 & 0xff;
                        cpu_state.flags |= 0x8D5;
                        cpu_state.flags &= ~1;
                    } else {
                        intr_routine(0, 0);
                        break;
                    }

                    do_cycles((cpu_mod == 3) ? 11 : 17);
                    break;
            } else  switch (rmdat & 0x38) {
                case 0x00: /* TEST */
                case 0x08:
                    /* read_operand8() */
                    cpu_data = get_ea();
                    /* read_operand8() */
                    cpu_src = biu_pfq_fetch();

                    do_cycles_i(is_nec ? 1 : 2);

                    /* math_op8() */
                    test(bits, cpu_data, cpu_src);
                    break;
                case 0x10: /* NOT */
                case 0x18: /* NEG */
                    /* read_operand8() */
                    cpu_data = get_ea();
                    /* math_op8() */
                    if ((rmdat & 0x38) == 0x10)
                        cpu_data = ~cpu_data;
                    else {
                        cpu_src  = cpu_data;
                        cpu_dest = 0;
                        sub(bits);
                    }

                    if (cpu_mod != 3)
                        do_cycles_i(2);

                    /* write_operand8() */
                    set_ea(cpu_data);
                    break;

                case 0x20: /* MUL */
                case 0x28: /* IMUL */
                    /* read_operand8() */
                    cpu_data = get_ea();

                    /* mul8() */
                    old_flags = cpu_state.flags;
                    mul(get_accum(bits), cpu_data);
                    prod16 = ((cpu_dest & 0xff) << 8) | (cpu_data & 0xff);
                    if (negate)
                        prod16 = -prod16;
                    cpu_dest = prod16 >> 8;
                    cpu_data = prod16 & 0xff;
                    AL = (uint8_t) cpu_data;
                    AH = (uint8_t) cpu_dest;
                    set_co_mul(bits, AH != ((AL & 0x80) == 0 ||
                               (rmdat & 0x38) == 0x20 ? 0 : 0xff));
                    if (!is_nec)
                        cpu_data = AH;
                    set_sf(bits);
                    set_pf();
                    /* NOTE: When implementing the V20, care should be taken to not change
                             the zero flag. */
                    if (is_nec)
                        cpu_state.flags = (cpu_state.flags & ~Z_FLAG) | (old_flags & Z_FLAG);
                    break;

                case 0x30: /* DIV */
                case 0x38: /* IDIV */
                    /* read_operand8() */
                    cpu_data = get_ea();

                    cpu_src = cpu_data;
                    if (x86_div(AL, AH)) {
                        if (!is_nec && negate)
                            AL = -AL;
                        do_cycle();
                    }
                    break;
            }
            break;

        case 0xf7: /* Miscellaneuous Opcode Extensions, r/m16, imm16 */
            bits = 16;
            if (cpu_mod != 3)
                do_cycles_i(is_nec ? 1 : 2);    /* load_operand() */
            negate = !!in_rep;

            if (is_nec && ((rmdat & 0x38) >= 0x20))  switch (rmdat & 0x38) {
                case 0x20: /* MUL */
                    /* read_operand16() */
                    cpu_data = get_ea();

                    templ = AX * cpu_data;
                    AX    = templ & 0xFFFF;
                    DX    = templ >> 16;
                    if (DX)
                        cpu_state.flags |= (C_FLAG | V_FLAG);
                    else
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);

                    do_cycles((cpu_mod == 3) ? 29 : 35);
                    break;
                case 0x28: /* IMUL */
                    /* read_operand16() */
                    cpu_data = get_ea();

                    templ = (int) ((int16_t) AX) * (int) ((int16_t) cpu_data);
                    AX    = templ & 0xFFFF;
                    DX    = templ >> 16;
                    if (((int32_t) templ >> 15) != 0 && ((int32_t) templ >> 15) != -1)
                        cpu_state.flags |= (C_FLAG | V_FLAG);
                    else
                        cpu_state.flags &= ~(C_FLAG | V_FLAG);

                    do_cycles((cpu_mod == 3) ? 17 : 27);
                    break;
                case 0x30: /* DIV */
                    /* read_operand16() */
                    cpu_data = get_ea();

                    templ = (DX << 16) | AX;
                    if (cpu_data)
                        templ2 = templ / cpu_data;
                    if (cpu_data && !(templ2 & 0xffff0000)) {
                        DX = templ % cpu_data;
                        AX = (templ / cpu_data) & 0xffff;
                        cpu_data = AX;
                        set_pzs(16);
                    } else {
                        intr_routine(0, 0);
                        break;
                    }

                    do_cycles((cpu_mod == 3) ? 26 : 36);
                    break;
                case 0x38: /* IDIV */
                    /* read_operand16() */
                    cpu_data = get_ea();

                    tempws = (int) ((DX << 16) | AX);
                    if (cpu_data)
                        tempws2 = tempws / (int) ((int16_t) cpu_data);
                    temps16 = tempws2 & 0xffff;
                    if ((cpu_data != 0) && ((int) temps16 == tempws2)) {
                        DX = tempws % (int) ((int16_t) cpu_data);
                        AX = tempws2 & 0xffff;
                        cpu_data = AX;
                        set_pzs(16);
                    } else {
                        intr_routine(0, 0);
                        break;
                    }

                    do_cycles((cpu_mod == 3) ? 13 : 23);
                    break;
            } else  switch (rmdat & 0x38) {
                case 0x00: /* TEST */
                case 0x08:
                    /* read_operand16() */
                    cpu_data = get_ea();
                    /* read_operand16() */
                    cpu_src = biu_pfq_fetch();

                    do_cycle_i();

                    /* math_op16() */
                    test(bits, cpu_data, cpu_src);
                    break;
                case 0x10: /* NOT */
                case 0x18: /* NEG */
                    /* read_operand16() */
                    cpu_data = get_ea();
                    /* math_op16() */
                    if ((rmdat & 0x38) == 0x10)
                        cpu_data = ~cpu_data;
                    else {
                        cpu_src  = cpu_data;
                        cpu_dest = 0;
                        sub(bits);
                    }

                    if (cpu_mod != 3)
                        do_cycles_i(2);

                    /* write_operand16() */
                    set_ea(cpu_data);
                    break;

                case 0x20: /* MUL */
                case 0x28: /* IMUL */
                    /* read_operand16() */
                    cpu_data = get_ea();

                    /* mul8() */
                    old_flags = cpu_state.flags;
                    mul(get_accum(bits), cpu_data);
                    prod32 = (((uint32_t) cpu_dest) << 16) | cpu_data;
                    if (negate)
                        prod32 = -prod32;
                    cpu_dest = prod32 >> 16;
                    cpu_data = prod32 & 0xffff;
                    AX = cpu_data;
                    DX = cpu_dest;
                    set_co_mul(bits, DX != ((AX & 0x8000) == 0 ||
                               (rmdat & 0x38) == 0x20 ? 0 : 0xffff));
                    cpu_data = DX;
                    set_sf(bits);
                    set_pf();
                    /* NOTE: When implementing the V20, care should be taken to not change
                             the zero flag. */
                    if (is_nec)
                        cpu_state.flags = (cpu_state.flags & ~Z_FLAG) | (old_flags & Z_FLAG);
                    break;

                case 0x30: /* DIV */
                case 0x38: /* IDIV */
                    /* read_operand16() */
                    cpu_data = get_ea();

                    cpu_src = cpu_data;
                    if (x86_div(AX, DX)) {
                        if (!is_nec && negate)
                            AX = -AX;
                        do_cycle();
                    }
                    break;
            }
            break;

        case 0xf8: /* CLCSTC */
        case 0xf9:
            set_cf(opcode & 1);
            break;

        case 0xfa: /* CLISTI */
        case 0xfb:
            set_if(opcode & 1);
            break;

        case 0xfc: /* CLDSTD */
        case 0xfd:
            set_df(opcode & 1);
            break;

        case 0xfe:
            bits = 8;
            if (cpu_mod != 3)
                do_cycles_i(is_nec ? 1 : 2);    /* load_operand() */
            read_ea(((rmdat & 0x38) == 0x18) || ((rmdat & 0x38) == 0x28), bits);
            switch (rmdat & 0x38) {
                case 0x00: /* INC rm */
                case 0x08: /* DEC rm */
                    /* read_operand8() */
                    /* math_op8() */
                    cpu_dest = cpu_data;
                    cpu_src  = 1;
                    if ((rmdat & 0x38) == 0x00) {
                        cpu_data = cpu_dest + cpu_src;
                        set_of_add(bits);
                    } else {
                        cpu_data = cpu_dest - cpu_src;
                        set_of_sub(bits);
                    }
                    do_af();
                    set_pzs(bits);

                    if (cpu_mod != 3)
                        do_cycles_i(2);
                    /* write_operand8() */
                    set_ea(cpu_data);
                    break;
                case 0x10: /* CALL rm */
                    /* read_operand8() */
                    cpu_data_opff_rm();

                    cpu_state.oldpc = cpu_state.pc;
                    push((uint16_t *) &(cpu_state.oldpc));

                    biu_suspend_fetch();
                    do_cycles(4);
                    biu_queue_flush();

                    set_ip(cpu_data | 0xff00);
                    break;
                case 0x18: /* CALL rmd */
                    if (cpu_mod == 3) {
                        /* biu_read_u8() */
                        cpu_state.eaaddr = 0x0004;
                        tempb = readmemb((ovr_seg ? *ovr_seg : ds), cpu_state.eaaddr);

                        old_cs = CS & 0x00ff;
                        push(&old_cs);
                        old_ip = cpu_state.pc & 0x00ff;
                        push(&old_ip);

                        biu_suspend_fetch();
                        do_cycles(4);
                        biu_queue_flush();

                        read_ea_8to16();
                        set_ip(cpu_data);
                    } else {
                        /* read_operand8() */
                        new_ip = cpu_data | 0xff00;

                        do_cycles_i(3);

                        /* biu_read_u8() */
                        read_ea2(bits);
                        cpu_data |= 0xff00;
                        new_cs = cpu_data;

                        do_cycle_i();
                        biu_suspend_fetch();
                        do_cycles_i(3);

                        old_cs = CS & 0x00ff;
                        push(&old_cs);
                        old_ip = cpu_state.pc & 0x00ff;

                        load_cs(new_cs);
                        set_ip(new_ip);

                        do_cycles_i(3);
                        biu_queue_flush();
                        do_cycles_i(3);

                        push(&old_ip);
                    }
                    break;
                case 0x20: /* JMP rm */
                    /* read_operand8() */
                    cpu_data_opff_rm();

                    set_ip(cpu_data | 0xff00);

                    biu_suspend_fetch();
                    do_cycles(4);
                    biu_queue_flush();
                    break;
                case 0x28: /* JMP rmd */
                    if (cpu_mod == 3) {
                        /* biu_read_u8() */
                        cpu_state.eaaddr = 0x0004;
                        tempb = readmemb((ovr_seg ? *ovr_seg : ds), cpu_state.eaaddr);

                        biu_suspend_fetch();
                        do_cycles(4);
                        biu_queue_flush();

                        read_ea_8to16();
                        set_ip(cpu_data);
                    } else {
                        /* read_operand8() */
                        new_ip = cpu_data | 0xff00;

                        /* biu_read_u8() */
                        read_ea2(bits);
                        cpu_data |= 0xff00;
                        new_cs = cpu_data;

                        biu_suspend_fetch();
                        do_cycles(4);
                        biu_queue_flush();

                        load_cs(new_cs);
                        set_ip(new_ip);
                    }
                    break;
                case 0x30: /* PUSH rm */
                case 0x38:
                    /* read_operand8() */
                    do_cycles_i(3);
                    cpu_data &= 0x00ff;
                    push((uint16_t *) &cpu_data);
                    break;
            }
            break;

        case 0xff:
            bits = 16;
            if (cpu_mod != 3)
                do_cycles_i(is_nec ? 1 : 2);    /* load_operand() */
            read_ea(((rmdat & 0x38) == 0x18) || ((rmdat & 0x38) == 0x28), bits);
            switch (rmdat & 0x38) {
                case 0x00: /* INC rm */
                case 0x08: /* DEC rm */
                    /* read_operand16() */
                    /* math_op16() */
                    cpu_dest = cpu_data;
                    cpu_src  = 1;
                    if ((rmdat & 0x38) == 0x00) {
                        cpu_data = cpu_dest + cpu_src;
                        set_of_add(bits);
                    } else {
                        cpu_data = cpu_dest - cpu_src;
                        set_of_sub(bits);
                    }
                    do_af();
                    set_pzs(bits);
                    if (cpu_mod != 3)
                        do_cycles_i(2);
                    /* write_operand16() */
                    set_ea(cpu_data);
                    break;
                case 0x10: /* CALL rm */
                    /* read_operand16() */
                    cpu_data_opff_rm();

                    biu_suspend_fetch();
                    do_cycles(4);

                    cpu_state.oldpc = cpu_state.pc;

                    old_ip = cpu_state.pc;
                    set_ip(cpu_data);
                    biu_queue_flush();
                    do_cycles_i(3);

                    push(&old_ip);
                    break;
                case 0x18: /* CALL rmd */
                    if (cpu_mod == 3) {
                        new_ip = cpu_data;

                        /* biu_read_u16() */
                        cpu_state.eaaddr = 0x0004;
                        new_cs = readmemw((ovr_seg ? *ovr_seg : ds), cpu_state.eaaddr);

                        do_cycle_i();
                        biu_suspend_fetch();
                        do_cycles_i(3);

                        push(&CS);
                        old_ip = cpu_state.pc;
                        set_ip(new_ip);

                        load_cs(new_cs);

                        do_cycles_i(3);
                        biu_queue_flush();
                        do_cycles_i(3);

                        push(&old_ip);
                    } else {
                        do_cycle_i();
                        /* read_operand_farptr() */
                        new_ip = cpu_data;
                        read_ea2(bits);
                        new_cs = cpu_data;

                        do_cycle_i();

                        biu_suspend_fetch();
                        do_cycles_i(3);

                        push(&CS);

                        load_cs(new_cs);
                        old_ip = cpu_state.pc;
                        set_ip(new_ip);
                        do_cycles_i(3);
                        biu_queue_flush();
                        do_cycles_i(3);
                        push(&old_ip);
                    }
                    break;
                case 0x20: /* JMP rm */
                    /* read_operand16() */
                    cpu_data_opff_rm();

                    biu_suspend_fetch();
                    do_cycle_i();
                    set_ip(cpu_data);
                    biu_queue_flush();
                    break;
                case 0x28: /* JMP rmd */
                    if (cpu_mod == 3) {
                        new_ip = cpu_data;

                        do_cycle();
                        biu_suspend_fetch();
                        do_cycle();

                        /* biu_read_u16() */
                        cpu_state.eaaddr = 0x0004;
                        new_cs = readmemw((ovr_seg ? *ovr_seg : ds), cpu_state.eaaddr);

                        push(&CS);
                        biu_queue_flush();
                    } else {
                        do_cycle_i();
                        biu_suspend_fetch();
                        do_cycle_i();

                        /* read_operand_farptr() */
                        new_ip = cpu_data;
                        read_ea2(bits);
                        new_cs = cpu_data;

                        load_cs(new_cs);
                        set_ip(new_ip);
                        biu_queue_flush();
                    }
                    break;
                case 0x30: /* PUSH rm */
                case 0x38:
                    /* read_operand16() */
                    do_cycles_i(3);

                    if (cpu_rm == 4)
                        cpu_rm -= 2;
                    push((uint16_t *) &cpu_data);
                    break;
            }
            break;

        default:
            x808x_log("Illegal opcode: %02X\n", opcode);
            biu_pfq_fetchb();
            do_cycles(8);
            break;
    }
}

/* Executes instructions up to the specified number of cycles. */
void
execvx0(int cycs)
{
    cycles += cycs;

    while (cycles > 0) {
        if (started) {
            started = 0;
            startx86();
        }

        if (!repeating) {
            cpu_state.oldpc = cpu_state.pc;

            if (clear_lock) {
                in_lock    = 0;
                clear_lock = 0;
            }

            if (!is_nec || (cpu_state.flags & MD_FLAG))
                decode();

            oldc    = cpu_state.flags & C_FLAG;
        }

        x808x_log("[%04X:%04X] Opcode: %02X\n", CS, cpu_state.pc, opcode);

        execute_instruction();

        if (completed) {
            if (opcode != 0xf4)
                finalize();

            check_interrupts();

            if (noint)
                noint = 0;
        }

#ifdef USE_GDBSTUB
        if (gdbstub_instruction())
            return;
#endif
    }
}
