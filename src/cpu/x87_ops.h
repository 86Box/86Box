/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		x87 FPU instructions core.
 *
 * Version:	@(#)x87_ops.h	1.0.8	2019/06/11
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *		leilei,
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 leilei.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2018,2019 Fred N. van Kempen.
 */
#include <math.h>
#include <fenv.h>
#include "x87_timings.h"
#ifdef _MSC_VER
# include <intrin.h>
#endif

#ifdef ENABLE_FPU_LOG
extern void	fpu_log(const char *fmt, ...);
#else
#ifndef fpu_log
#define fpu_log(fmt, ...)
#endif
#endif

static int rounding_modes[4] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};

#define ST(x) cpu_state.ST[((cpu_state.TOP+(x))&7)]

#define C0 (1<<8)
#define C1 (1<<9)
#define C2 (1<<10)
#define C3 (1<<14)

#define STATUS_ZERODIVIDE 4

#if defined(_MSC_VER) && !defined(__clang__)
# if defined i386 || defined __i386 || defined __i386__ || defined _X86_ || defined _M_IX86
#  define X87_INLINE_ASM
# endif
#else
# if defined i386 || defined __i386 || defined __i386__ || defined _X86_ || defined _M_IX86 || defined _M_X64 || defined __amd64__
#  define X87_INLINE_ASM
# endif
#endif

#ifdef FPU_8087
#define x87_div(dst, src1, src2) do                             \
        {                                                       \
                if (((double)src2) == 0.0)                      \
                {                                               \
                        cpu_state.npxs |= STATUS_ZERODIVIDE;    \
                        if (cpu_state.npxc & STATUS_ZERODIVIDE) \
                                dst = src1 / (double)src2;      \
                        else                                    \
                        {                                       \
                                fpu_log("FPU : divide by zero\n"); \
				if (!(cpu_state.npxc & 0x80)) {	\
					cpu_state.npxs |= 0x80;	\
					nmi = 1;		\
				}				\
                                return 1;                       \
                        }                                       \
                }                                               \
                else                                            \
                        dst = src1 / (double)src2;              \
        } while (0)
#else
#define x87_div(dst, src1, src2) do                             \
        {                                                       \
                if (((double)src2) == 0.0)                      \
                {                                               \
                        cpu_state.npxs |= STATUS_ZERODIVIDE;    \
                        if (cpu_state.npxc & STATUS_ZERODIVIDE) \
                                dst = src1 / (double)src2;      \
                        else                                    \
                        {                                       \
                                fpu_log("FPU : divide by zero\n"); \
                                picint(1 << 13);        	\
                                return 1;                       \
                        }                                       \
                }                                               \
                else                                            \
                        dst = src1 / (double)src2;              \
        } while (0)
#endif

static __inline void x87_checkexceptions()
{
}

static __inline void x87_push(double i)
{
#ifdef USE_NEW_DYNAREC
        cpu_state.TOP--;
#else
        cpu_state.TOP=(cpu_state.TOP-1)&7;
#endif
        cpu_state.ST[cpu_state.TOP&7] = i;
#ifdef USE_NEW_DYNAREC
        cpu_state.tag[cpu_state.TOP&7] = TAG_VALID;
#else
        cpu_state.tag[cpu_state.TOP&7] = (i == 0.0) ? TAG_VALID : 0;
#endif
}

static __inline void x87_push_u64(uint64_t i)
{
        union
        {
                double d;
                uint64_t ll;
        } td;

        td.ll = i;

#ifdef USE_NEW_DYNAREC
        cpu_state.TOP--;
#else
        cpu_state.TOP=(cpu_state.TOP-1)&7;
#endif
        cpu_state.ST[cpu_state.TOP&7] = td.d;
#ifdef USE_NEW_DYNAREC
        cpu_state.tag[cpu_state.TOP&7] = TAG_VALID;
#else
        cpu_state.tag[cpu_state.TOP&7] = (td.d == 0.0) ? TAG_VALID : 0;
#endif
}

static __inline double x87_pop()
{
        double t = cpu_state.ST[cpu_state.TOP&7];
        cpu_state.tag[cpu_state.TOP&7] = TAG_EMPTY;
#ifdef USE_NEW_DYNAREC
        cpu_state.TOP++;
#else
        cpu_state.tag[cpu_state.TOP&7] |= TAG_UINT64;
        cpu_state.TOP=(cpu_state.TOP+1)&7;
#endif
        return t;
}

static __inline int16_t x87_fround16(double b)
{
        int16_t a, c;

        switch ((cpu_state.npxc >> 10) & 3)
        {
                case 0: /*Nearest*/
                a = (int16_t)floor(b);
                c = (int16_t)floor(b + 1.0);
                if ((b - a) < (c - b))
                        return a;
                else if ((b - a) > (c - b))
                        return c;
                else
                        return (a & 1) ? c : a;
                case 1: /*Down*/
                return (int16_t)floor(b);
                case 2: /*Up*/
                return (int16_t)ceil(b);
                case 3: /*Chop*/
                return (int16_t)b;
        }

        return 0;
}

static __inline int64_t x87_fround16_64(double b)
{
    return (int64_t) x87_fround16(b);
}

static __inline int32_t x87_fround32(double b)
{
        int32_t a, c;

        switch ((cpu_state.npxc >> 10) & 3)
        {
                case 0: /*Nearest*/
                a = (int32_t)floor(b);
                c = (int32_t)floor(b + 1.0);
                if ((b - a) < (c - b))
                        return a;
                else if ((b - a) > (c - b))
                        return c;
                else
                        return (a & 1) ? c : a;
                case 1: /*Down*/
                return (int32_t)floor(b);
                case 2: /*Up*/
                return (int32_t)ceil(b);
                case 3: /*Chop*/
                return (int32_t)b;
        }

        return 0;
}

static __inline int64_t x87_fround32_64(double b)
{
    return (int64_t) x87_fround32(b);
}

static __inline int64_t x87_fround(double b)
{
        int64_t a, c;

        switch ((cpu_state.npxc >> 10) & 3)
        {
                case 0: /*Nearest*/
                a = (int64_t)floor(b);
                c = (int64_t)floor(b + 1.0);
                if ((b - a) < (c - b))
                        return a;
                else if ((b - a) > (c - b))
                        return c;
                else
                        return (a & 1) ? c : a;
                case 1: /*Down*/
                return (int64_t)floor(b);
                case 2: /*Up*/
                return (int64_t)ceil(b);
                case 3: /*Chop*/
                return (int64_t)b;
        }

        return 0LL;
}

#include "x87_ops_conv.h"

static __inline double x87_ld80()
{
        x87_conv_t test;
        test.eind.ll = readmeml(easeg,cpu_state.eaaddr);
        test.eind.ll |= (uint64_t)readmeml(easeg,cpu_state.eaaddr+4)<<32;
        test.begin = readmemw(easeg,cpu_state.eaaddr+8);
        return x87_from80(&test);
}

static __inline void x87_st80(double d)
{
        x87_conv_t test;
        x87_to80(d, &test);
        writememl(easeg,cpu_state.eaaddr,test.eind.ll & 0xffffffff);
        writememl(easeg,cpu_state.eaaddr+4,test.eind.ll>>32);
        writememw(easeg,cpu_state.eaaddr+8,test.begin);
}

static __inline void x87_st_fsave(int reg)
{
        reg = (cpu_state.TOP + reg) & 7;

        if (cpu_state.tag[reg] & TAG_UINT64)
        {
        	writememl(easeg, cpu_state.eaaddr, cpu_state.MM[reg].q & 0xffffffff);
        	writememl(easeg, cpu_state.eaaddr + 4, cpu_state.MM[reg].q >> 32);
        	writememw(easeg, cpu_state.eaaddr + 8, 0x5555);
        }
        else
                x87_st80(cpu_state.ST[reg]);
}

static __inline void x87_ld_frstor(int reg)
{
        reg = (cpu_state.TOP + reg) & 7;

        cpu_state.MM[reg].q = readmemq(easeg, cpu_state.eaaddr);
        cpu_state.MM_w4[reg] = readmemw(easeg, cpu_state.eaaddr + 8);

#ifdef USE_NEW_DYNAREC
        if ((cpu_state.MM_w4[reg] == 0x5555) && (cpu_state.tag[reg] & TAG_UINT64))
#else
        if ((cpu_state.MM_w4[reg] == 0x5555) && (cpu_state.tag[reg] == 2))
#endif
        {
#ifndef USE_NEW_DYNAREC
		cpu_state.tag[reg] = TAG_UINT64;
#endif
                cpu_state.ST[reg] = (double)cpu_state.MM[reg].q;
        }
        else
        {
#ifdef USE_NEW_DYNAREC
                cpu_state.tag[reg] &= ~TAG_UINT64;
#endif
                cpu_state.ST[reg] = x87_ld80();
        }
}

static __inline void x87_ldmmx(MMX_REG *r, uint16_t *w4)
{
        r->l[0] = readmeml(easeg, cpu_state.eaaddr);
        r->l[1] = readmeml(easeg, cpu_state.eaaddr + 4);
        *w4 = readmemw(easeg, cpu_state.eaaddr + 8);
}

static __inline void x87_stmmx(MMX_REG r)
{
        writememl(easeg, cpu_state.eaaddr,     r.l[0]);
        writememl(easeg, cpu_state.eaaddr + 4, r.l[1]);
        writememw(easeg, cpu_state.eaaddr + 8, 0xffff);
}

#include <inttypes.h>

static __inline uint16_t x87_compare(double a, double b)
{
#ifdef X87_INLINE_ASM
        uint32_t result;
	double ea = a, eb = b;
	const uint64_t ia = 0x3fec1a6ff866a936ull;
	const uint64_t ib = 0x3fec1a6ff866a938ull;

	/* Hack to make CHKCOP happy. */
	if (!memcmp(&ea, &ia, 8) && !memcmp(&eb, &ib, 8))
		return C3;

	if ((fpu_type < FPU_287XL) && !(cpu_state.npxc & 0x1000) &&
	    ((a == INFINITY) || (a == -INFINITY)) && ((b == INFINITY) || (b == -INFINITY)))
		eb = ea;

#if !defined(_MSC_VER) || defined(__clang__)
        /* Memory barrier, to force GCC to write to the input parameters
         * before the compare rather than after */
        __asm volatile ("" : : : "memory");

        __asm(
                "fldl %2\n"
                "fldl %1\n"
                "fclex\n"
                "fcompp\n"
                "fnstsw %0\n"
                : "=m" (result)
                : "m" (ea), "m" (eb)
        );
#else
        _ReadWriteBarrier();
        _asm
        {
                fld eb
                fld ea
                fclex
                fcompp
                fnstsw result
        }
#endif

        return result & (C0|C2|C3);
#else
        /* Generic C version is known to give incorrect results in some
         * situations, eg comparison of infinity (Unreal) */
        uint32_t result = 0;
	double ea = a, eb = b;

	if ((fpu_type < FPU_287XL) && !(cpu_state.npxc & 0x1000) &&
	    ((a == INFINITY) || (a == -INFINITY)) && ((b == INFINITY) || (b == -INFINITY)))
		eb = ea;

        if (ea == eb)
                result |= C3;
        else if (ea < eb)
                result |= C0;

        return result;
#endif
}

static __inline uint16_t x87_ucompare(double a, double b)
{
#ifdef X87_INLINE_ASM
        uint32_t result;

#if !defined(_MSC_VER) || defined(__clang__)
        /* Memory barrier, to force GCC to write to the input parameters
         * before the compare rather than after */
        asm volatile ("" : : : "memory");

        asm(
                "fldl %2\n"
                "fldl %1\n"
                "fclex\n"
                "fucompp\n"
                "fnstsw %0\n"
                : "=m" (result)
                : "m" (a), "m" (b)
        );
#else
        _ReadWriteBarrier();
        _asm
        {
                fld b
                fld a
                fclex
                fcompp
                fnstsw result
        }
#endif

        return result & (C0|C2|C3);
#else
        /* Generic C version is known to give incorrect results in some
         * situations, eg comparison of infinity (Unreal) */
        uint32_t result = 0;

        if (a == b)
                result |= C3;
        else if (a < b)
                result |= C0;

        return result;
#endif
}

typedef union
{
        float s;
        uint32_t i;
} x87_ts;

typedef union
{
        double d;
        uint64_t i;
} x87_td;

#ifdef FPU_8087
#define FP_ENTER() {			\
	}
#else
#define FP_ENTER() do                   \
        {                               \
                if (cr0 & 0xc)          \
                {                       \
                        x86_int(7);     \
                        return 1;       \
                }                       \
        } while (0)
#endif

#ifdef USE_NEW_DYNAREC
# define FP_TAG_VALID	cpu_state.tag[cpu_state.TOP&7] = TAG_VALID
# define FP_TAG_VALID_F	cpu_state.tag[(cpu_state.TOP + fetchdat) & 7] = TAG_VALID
# define FP_TAG_DEFAULT	cpu_state.tag[cpu_state.TOP&7] = TAG_VALID | TAG_UINT64
# define FP_TAG_VALID_N	cpu_state.tag[(cpu_state.TOP + 1) & 7] = TAG_VALID
#else
# define FP_TAG_VALID	cpu_state.tag[cpu_state.TOP] &= ~TAG_UINT64
# define FP_TAG_VALID_F	cpu_state.tag[(cpu_state.TOP + fetchdat) & 7] &= ~TAG_UINT64
# define FP_TAG_DEFAULT	cpu_state.tag[cpu_state.TOP] |= TAG_UINT64;
# define FP_TAG_VALID_N	cpu_state.tag[(cpu_state.TOP + 1) & 7] &= ~TAG_UINT64
#endif

#include "x87_ops_arith.h"
#include "x87_ops_misc.h"
#include "x87_ops_loadstore.h"

#ifndef FPU_8087
static int op_nofpu_a16(uint32_t fetchdat)
{
        if (cr0 & 0xc)
        {
                x86_int(7);
                return 1;
        }
        else
	{
                fetch_ea_16(fetchdat);
		return 0;
	}
}
static int op_nofpu_a32(uint32_t fetchdat)
{
        if (cr0 & 0xc)
        {
                x86_int(7);
                return 1;
        }
        else
	{
                fetch_ea_32(fetchdat);
		return 0;
	}
}
#endif

#ifdef FPU_8087
static int FPU_ILLEGAL_a16(uint32_t fetchdat)
{
	geteaw();
        wait(timing_rr, 0);
        return 0;
}
#else
static int FPU_ILLEGAL_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);
        CLOCK_CYCLES(timing_rr);
        PREFETCH_RUN(timing_rr, 2, rmdat, 0,0,0,0, 0);
        return 0;
}

static int FPU_ILLEGAL_a32(uint32_t fetchdat)
{
        fetch_ea_32(fetchdat);
        CLOCK_CYCLES(timing_rr);
        PREFETCH_RUN(timing_rr, 2, rmdat, 0,0,0,0, 0);
        return 0;
}
#endif

#define ILLEGAL_a16 FPU_ILLEGAL_a16

#ifdef FPU_8087
const OpFn OP_TABLE(fpu_8087_d8)[32] =
{
        opFADDs_a16, opFMULs_a16, opFCOMs_a16, opFCOMPs_a16, opFSUBs_a16, opFSUBRs_a16, opFDIVs_a16, opFDIVRs_a16,
        opFADDs_a16, opFMULs_a16, opFCOMs_a16, opFCOMPs_a16, opFSUBs_a16, opFSUBRs_a16, opFDIVs_a16, opFDIVRs_a16,
        opFADDs_a16, opFMULs_a16, opFCOMs_a16, opFCOMPs_a16, opFSUBs_a16, opFSUBRs_a16, opFDIVs_a16, opFDIVRs_a16,
        opFADD,      opFMUL,      opFCOM,      opFCOMP,      opFSUB,      opFSUBR,      opFDIV,      opFDIVR
};

const OpFn OP_TABLE(fpu_8087_d9)[256] =
{
        opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,
        opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,
        opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16,
        opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,
        opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16,
        opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,

        opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,
        opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,
        opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16,
        opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,
        opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16,
        opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,

        opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,
        opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,
        opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16,
        opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,
        opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16,
        opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,

        opFLD,        opFLD,        opFLD,        opFLD,        opFLD,        opFLD,        opFLD,        opFLD,
        opFXCH,       opFXCH,       opFXCH,       opFXCH,       opFXCH,       opFXCH,       opFXCH,       opFXCH,
        opFNOP,       ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16, /*Invalid*/
        opFCHS,       opFABS,       ILLEGAL_a16,  ILLEGAL_a16,  opFTST,       opFXAM,       ILLEGAL_a16,  ILLEGAL_a16,
        opFLD1,       opFLDL2T,     opFLDL2E,     opFLDPI,      opFLDEG2,     opFLDLN2,     opFLDZ,       ILLEGAL_a16,
        opF2XM1,      opFYL2X,      opFPTAN,      opFPATAN,     ILLEGAL_a16,  ILLEGAL_a16,  opFDECSTP,    opFINCSTP,
        opFPREM,      opFYL2XP1,    opFSQRT,      ILLEGAL_a16,   opFRNDINT,   opFSCALE,     ILLEGAL_a16,  ILLEGAL_a16
};

const OpFn OP_TABLE(fpu_8087_da)[256] =
{
        opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,
        opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,
        opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,
        opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16,
        opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,
        opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16,
        opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,
        opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16,

        opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,
        opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,
        opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,
        opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16,
        opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,
        opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16,
        opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,
        opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16,

        opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,
        opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,
        opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,
        opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16,
        opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,
        opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16,
        opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,
        opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16,

        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
};

const OpFn OP_TABLE(fpu_8087_db)[256] =
{
        opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,
        opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,

        opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,
        opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,

        opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,
        opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,

        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFI,         opFI,         opFCLEX,      opFINIT,      ILLEGAL_a16,  opFNOP,       ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
};

const OpFn OP_TABLE(fpu_8087_dc)[32] =
{
        opFADDd_a16, opFMULd_a16, opFCOMd_a16, opFCOMPd_a16, opFSUBd_a16, opFSUBRd_a16, opFDIVd_a16, opFDIVRd_a16,
        opFADDd_a16, opFMULd_a16, opFCOMd_a16, opFCOMPd_a16, opFSUBd_a16, opFSUBRd_a16, opFDIVd_a16, opFDIVRd_a16,
        opFADDd_a16, opFMULd_a16, opFCOMd_a16, opFCOMPd_a16, opFSUBd_a16, opFSUBRd_a16, opFDIVd_a16, opFDIVRd_a16,
        opFADDr,     opFMULr,     ILLEGAL_a16, ILLEGAL_a16,  opFSUBRr,    opFSUBr,      opFDIVRr,    opFDIVr
};

const OpFn OP_TABLE(fpu_8087_dd)[256] =
{
        opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,
        opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,
        opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,
        opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,

        opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,
        opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,
        opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,
        opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,

        opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,
        opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,
        opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,
        opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,

        opFFREE,       opFFREE,       opFFREE,       opFFREE,       opFFREE,       opFFREE,       opFFREE,       opFFREE,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFST,         opFST,         opFST,         opFST,         opFST,         opFST,         opFST,         opFST,
        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
};

const OpFn OP_TABLE(fpu_8087_de)[256] =
{
        opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,
        opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,
        opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,
        opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16,
        opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,
        opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16,
        opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,
        opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16,

        opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,
        opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,
        opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,
        opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16,
        opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,
        opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16,
        opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,
        opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16,

        opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,
        opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,
        opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,
        opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16,
        opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,
        opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16,
        opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,
        opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16,

        opFADDP,       opFADDP,       opFADDP,       opFADDP,       opFADDP,       opFADDP,       opFADDP,       opFADDP,
        opFMULP,       opFMULP,       opFMULP,       opFMULP,       opFMULP,       opFMULP,       opFMULP,       opFMULP,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  opFCOMPP,     ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,
        opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,
        opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,
        opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,
};

const OpFn OP_TABLE(fpu_8087_df)[256] =
{
        opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,
        opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,
        FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,
        FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,

        opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,
        opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,
        FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,
        FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,

        opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,
        opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,
        FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,
        FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,

        opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
};
#else
#define ILLEGAL_a32 FPU_ILLEGAL_a32

const OpFn OP_TABLE(fpu_d8_a16)[32] =
{
        opFADDs_a16, opFMULs_a16, opFCOMs_a16, opFCOMPs_a16, opFSUBs_a16, opFSUBRs_a16, opFDIVs_a16, opFDIVRs_a16,
        opFADDs_a16, opFMULs_a16, opFCOMs_a16, opFCOMPs_a16, opFSUBs_a16, opFSUBRs_a16, opFDIVs_a16, opFDIVRs_a16,
        opFADDs_a16, opFMULs_a16, opFCOMs_a16, opFCOMPs_a16, opFSUBs_a16, opFSUBRs_a16, opFDIVs_a16, opFDIVRs_a16,
        opFADD,      opFMUL,      opFCOM,      opFCOMP,      opFSUB,      opFSUBR,      opFDIV,      opFDIVR
};
const OpFn OP_TABLE(fpu_d8_a32)[32] =
{
        opFADDs_a32, opFMULs_a32, opFCOMs_a32, opFCOMPs_a32, opFSUBs_a32, opFSUBRs_a32, opFDIVs_a32, opFDIVRs_a32,
        opFADDs_a32, opFMULs_a32, opFCOMs_a32, opFCOMPs_a32, opFSUBs_a32, opFSUBRs_a32, opFDIVs_a32, opFDIVRs_a32,
        opFADDs_a32, opFMULs_a32, opFCOMs_a32, opFCOMPs_a32, opFSUBs_a32, opFSUBRs_a32, opFDIVs_a32, opFDIVRs_a32,
        opFADD,      opFMUL,      opFCOM,      opFCOMP,      opFSUB,      opFSUBR,      opFDIV,      opFDIVR
};

const OpFn OP_TABLE(fpu_287_d9_a16)[256] =
{
        opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,
        opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,
        opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16,
        opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,
        opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16,
        opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,

        opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,
        opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,
        opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16,
        opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,
        opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16,
        opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,

        opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,
        opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,
        opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16,
        opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,
        opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16,
        opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,

        opFLD,        opFLD,        opFLD,        opFLD,        opFLD,        opFLD,        opFLD,        opFLD,
        opFXCH,       opFXCH,       opFXCH,       opFXCH,       opFXCH,       opFXCH,       opFXCH,       opFXCH,
        opFNOP,       ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16, /*Invalid*/
        opFCHS,       opFABS,       ILLEGAL_a16,  ILLEGAL_a16,  opFTST,       opFXAM,       ILLEGAL_a16,  ILLEGAL_a16,
        opFLD1,       opFLDL2T,     opFLDL2E,     opFLDPI,      opFLDEG2,     opFLDLN2,     opFLDZ,       ILLEGAL_a16,
        opF2XM1,      opFYL2X,      opFPTAN,      opFPATAN,     ILLEGAL_a16,  opFPREM1,     opFDECSTP,    opFINCSTP,
        opFPREM,      opFYL2XP1,    opFSQRT,      opFSINCOS,    opFRNDINT,    opFSCALE,     opFSIN,       opFCOS
};

const OpFn OP_TABLE(fpu_287_d9_a32)[256] =
{
        opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,
        opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,
        opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32,
        opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,
        opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32,
        opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,

        opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,
        opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,
        opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32,
        opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,
        opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32,
        opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,

        opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,
        opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,
        opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32,
        opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,
        opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32,
        opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,

        opFLD,        opFLD,        opFLD,        opFLD,        opFLD,        opFLD,        opFLD,        opFLD,
        opFXCH,       opFXCH,       opFXCH,       opFXCH,       opFXCH,       opFXCH,       opFXCH,       opFXCH,
        opFNOP,       ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32, /*Invalid*/
        opFCHS,       opFABS,       ILLEGAL_a32,  ILLEGAL_a32,  opFTST,       opFXAM,       ILLEGAL_a32,  ILLEGAL_a32,
        opFLD1,       opFLDL2T,     opFLDL2E,     opFLDPI,      opFLDEG2,     opFLDLN2,     opFLDZ,       ILLEGAL_a32,
        opF2XM1,      opFYL2X,      opFPTAN,      opFPATAN,     ILLEGAL_a32,  opFPREM1,     opFDECSTP,    opFINCSTP,
        opFPREM,      opFYL2XP1,    opFSQRT,      opFSINCOS,    opFRNDINT,    opFSCALE,     opFSIN,       opFCOS
};

const OpFn OP_TABLE(fpu_d9_a16)[256] =
{
        opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,
        opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,
        opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16,
        opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,
        opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16,
        opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,

        opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,
        opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,
        opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16,
        opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,
        opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16,
        opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,

        opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,   opFLDs_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,   opFSTs_a16,
        opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,  opFSTPs_a16,
        opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16, opFLDENV_a16,
        opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,  opFLDCW_a16,
        opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16, opFSTENV_a16,
        opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,  opFSTCW_a16,

        opFLD,        opFLD,        opFLD,        opFLD,        opFLD,        opFLD,        opFLD,        opFLD,
        opFXCH,       opFXCH,       opFXCH,       opFXCH,       opFXCH,       opFXCH,       opFXCH,       opFXCH,
        opFNOP,       ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTP,       opFSTP,       opFSTP,       opFSTP,       opFSTP,       opFSTP,       opFSTP,       opFSTP,  /*Invalid*/
        opFCHS,       opFABS,       ILLEGAL_a16,  ILLEGAL_a16,  opFTST,       opFXAM,       ILLEGAL_a16,  ILLEGAL_a16,
        opFLD1,       opFLDL2T,     opFLDL2E,     opFLDPI,      opFLDEG2,     opFLDLN2,     opFLDZ,       ILLEGAL_a16,
        opF2XM1,      opFYL2X,      opFPTAN,      opFPATAN,     ILLEGAL_a16,  opFPREM1,     opFDECSTP,    opFINCSTP,
        opFPREM,      opFYL2XP1,    opFSQRT,      opFSINCOS,    opFRNDINT,    opFSCALE,     opFSIN,       opFCOS
};

const OpFn OP_TABLE(fpu_d9_a32)[256] =
{
        opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,
        opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,
        opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32,
        opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,
        opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32,
        opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,

        opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,
        opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,
        opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32,
        opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,
        opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32,
        opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,

        opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,   opFLDs_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,   opFSTs_a32,
        opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,  opFSTPs_a32,
        opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32, opFLDENV_a32,
        opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,  opFLDCW_a32,
        opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32, opFSTENV_a32,
        opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,  opFSTCW_a32,

        opFLD,        opFLD,        opFLD,        opFLD,        opFLD,        opFLD,        opFLD,        opFLD,
        opFXCH,       opFXCH,       opFXCH,       opFXCH,       opFXCH,       opFXCH,       opFXCH,       opFXCH,
        opFNOP,       ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTP,       opFSTP,       opFSTP,       opFSTP,       opFSTP,       opFSTP,       opFSTP,       opFSTP,  /*Invalid*/
        opFCHS,       opFABS,       ILLEGAL_a32,  ILLEGAL_a32,  opFTST,       opFXAM,       ILLEGAL_a32,  ILLEGAL_a32,
        opFLD1,       opFLDL2T,     opFLDL2E,     opFLDPI,      opFLDEG2,     opFLDLN2,     opFLDZ,       ILLEGAL_a32,
        opF2XM1,      opFYL2X,      opFPTAN,      opFPATAN,     ILLEGAL_a32,  opFPREM1,     opFDECSTP,    opFINCSTP,
        opFPREM,      opFYL2XP1,    opFSQRT,      opFSINCOS,    opFRNDINT,    opFSCALE,     opFSIN,       opFCOS
};

const OpFn OP_TABLE(fpu_287_da_a16)[256] =
{
        opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,
        opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,
        opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,
        opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16,
        opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,
        opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16,
        opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,
        opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16,

        opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,
        opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,
        opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,
        opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16,
        opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,
        opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16,
        opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,
        opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16,

        opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,
        opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,
        opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,
        opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16,
        opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,
        opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16,
        opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,
        opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16,

        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
};
const OpFn OP_TABLE(fpu_287_da_a32)[256] =
{
        opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,
        opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,
        opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,
        opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32,
        opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,
        opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32,
        opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,
        opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32,

        opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,
        opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,
        opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,
        opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32,
        opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,
        opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32,
        opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,
        opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32,

        opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,
        opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,
        opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,
        opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32,
        opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,
        opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32,
        opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,
        opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32,

        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
};

const OpFn OP_TABLE(fpu_da_a16)[256] =
{
        opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,
        opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,
        opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,
        opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16,
        opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,
        opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16,
        opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,
        opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16,

        opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,
        opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,
        opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,
        opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16,
        opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,
        opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16,
        opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,
        opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16,

        opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,
        opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,
        opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,
        opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16,
        opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,
        opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16,
        opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,
        opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16,

        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  opFUCOMPP,    ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
};
const OpFn OP_TABLE(fpu_da_a32)[256] =
{
        opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,
        opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,
        opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,
        opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32,
        opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,
        opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32,
        opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,
        opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32,

        opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,
        opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,
        opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,
        opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32,
        opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,
        opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32,
        opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,
        opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32,

        opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,
        opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,
        opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,
        opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32,
        opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,
        opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32,
        opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,
        opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32,

        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  opFUCOMPP,    ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
};

const OpFn OP_TABLE(fpu_686_da_a16)[256] =
{
        opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,
        opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,
        opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,
        opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16,
        opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,
        opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16,
        opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,
        opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16,

        opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,
        opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,
        opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,
        opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16,
        opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,
        opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16,
        opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,
        opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16,

        opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,  opFADDil_a16,
        opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,  opFMULil_a16,
        opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,  opFCOMil_a16,
        opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16, opFCOMPil_a16,
        opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,  opFSUBil_a16,
        opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16, opFSUBRil_a16,
        opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,  opFDIVil_a16,
        opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16, opFDIVRil_a16,

        opFCMOVB,      opFCMOVB,      opFCMOVB,      opFCMOVB,      opFCMOVB,      opFCMOVB,      opFCMOVB,      opFCMOVB,
        opFCMOVE,      opFCMOVE,      opFCMOVE,      opFCMOVE,      opFCMOVE,      opFCMOVE,      opFCMOVE,      opFCMOVE,
        opFCMOVBE,     opFCMOVBE,     opFCMOVBE,     opFCMOVBE,     opFCMOVBE,     opFCMOVBE,     opFCMOVBE,     opFCMOVBE,
        opFCMOVU,      opFCMOVU,      opFCMOVU,      opFCMOVU,      opFCMOVU,      opFCMOVU,      opFCMOVU,      opFCMOVU,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  opFUCOMPP,    ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
};
const OpFn OP_TABLE(fpu_686_da_a32)[256] =
{
        opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,
        opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,
        opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,
        opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32,
        opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,
        opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32,
        opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,
        opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32,

        opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,
        opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,
        opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,
        opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32,
        opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,
        opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32,
        opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,
        opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32,

        opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,  opFADDil_a32,
        opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,  opFMULil_a32,
        opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,  opFCOMil_a32,
        opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32, opFCOMPil_a32,
        opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,  opFSUBil_a32,
        opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32, opFSUBRil_a32,
        opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,  opFDIVil_a32,
        opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32, opFDIVRil_a32,

        opFCMOVB,      opFCMOVB,      opFCMOVB,      opFCMOVB,      opFCMOVB,      opFCMOVB,      opFCMOVB,      opFCMOVB,
        opFCMOVE,      opFCMOVE,      opFCMOVE,      opFCMOVE,      opFCMOVE,      opFCMOVE,      opFCMOVE,      opFCMOVE,
        opFCMOVBE,     opFCMOVBE,     opFCMOVBE,     opFCMOVBE,     opFCMOVBE,     opFCMOVBE,     opFCMOVBE,     opFCMOVBE,
        opFCMOVU,      opFCMOVU,      opFCMOVU,      opFCMOVU,      opFCMOVU,      opFCMOVU,      opFCMOVU,      opFCMOVU,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  opFUCOMPP,    ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
};

const OpFn OP_TABLE(fpu_287_db_a16)[256] =
{
        opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,
        opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,

        opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,
        opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,

        opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,
        opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,

        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFNOP,        opFNOP,        opFCLEX,       opFINIT,       opFNOP,        opFNOP,        ILLEGAL_a16,       ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
};
const OpFn OP_TABLE(fpu_287_db_a32)[256] =
{
        opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,
        opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,

        opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,
        opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,

        opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,
        opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,

        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFNOP,        opFNOP,        opFCLEX,       opFINIT,       opFNOP,        opFNOP,        ILLEGAL_a32,       ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
};

const OpFn OP_TABLE(fpu_db_a16)[256] =
{
        opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,
        opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,

        opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,
        opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,

        opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,
        opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,

        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFNOP,        opFNOP,        opFCLEX,       opFINIT,       opFNOP,        opFNOP,        ILLEGAL_a16,       ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
};
const OpFn OP_TABLE(fpu_db_a32)[256] =
{
        opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,
        opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,

        opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,
        opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,

        opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,
        opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,

        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFNOP,        opFNOP,        opFCLEX,       opFINIT,       opFNOP,        opFNOP,        ILLEGAL_a32,       ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
};

const OpFn OP_TABLE(fpu_686_db_a16)[256] =
{
        opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,
        opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,

        opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,
        opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,

        opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,  opFILDil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,  opFISTil_a16,
        opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16, opFISTPil_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,    opFLDe_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,   opFSTPe_a16,

        opFCMOVNB,     opFCMOVNB,     opFCMOVNB,     opFCMOVNB,     opFCMOVNB,     opFCMOVNB,     opFCMOVNB,     opFCMOVNB,
        opFCMOVNE,     opFCMOVNE,     opFCMOVNE,     opFCMOVNE,     opFCMOVNE,     opFCMOVNE,     opFCMOVNE,     opFCMOVNE,
        opFCMOVNBE,    opFCMOVNBE,    opFCMOVNBE,    opFCMOVNBE,    opFCMOVNBE,    opFCMOVNBE,    opFCMOVNBE,    opFCMOVNBE,
        opFCMOVNU,     opFCMOVNU,     opFCMOVNU,     opFCMOVNU,     opFCMOVNU,     opFCMOVNU,     opFCMOVNU,     opFCMOVNU,
        opFNOP,        opFNOP,        opFCLEX,       opFINIT,       opFNOP,        opFNOP,        ILLEGAL_a16,       ILLEGAL_a16,
        opFUCOMI,      opFUCOMI,      opFUCOMI,      opFUCOMI,      opFUCOMI,      opFUCOMI,      opFUCOMI,      opFUCOMI,
        opFCOMI,       opFCOMI,       opFCOMI,       opFCOMI,       opFCOMI,       opFCOMI,       opFCOMI,       opFCOMI,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
};
const OpFn OP_TABLE(fpu_686_db_a32)[256] =
{
        opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,
        opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,

        opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,
        opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,

        opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,  opFILDil_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,  opFISTil_a32,
        opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32, opFISTPil_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,    opFLDe_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,   opFSTPe_a32,

        opFCMOVNB,     opFCMOVNB,     opFCMOVNB,     opFCMOVNB,     opFCMOVNB,     opFCMOVNB,     opFCMOVNB,     opFCMOVNB,
        opFCMOVNE,     opFCMOVNE,     opFCMOVNE,     opFCMOVNE,     opFCMOVNE,     opFCMOVNE,     opFCMOVNE,     opFCMOVNE,
        opFCMOVNBE,    opFCMOVNBE,    opFCMOVNBE,    opFCMOVNBE,    opFCMOVNBE,    opFCMOVNBE,    opFCMOVNBE,    opFCMOVNBE,
        opFCMOVNU,     opFCMOVNU,     opFCMOVNU,     opFCMOVNU,     opFCMOVNU,     opFCMOVNU,     opFCMOVNU,     opFCMOVNU,
        opFNOP,        opFNOP,        opFCLEX,       opFINIT,       opFNOP,        opFNOP,        ILLEGAL_a32,       ILLEGAL_a32,
        opFUCOMI,      opFUCOMI,      opFUCOMI,      opFUCOMI,      opFUCOMI,      opFUCOMI,      opFUCOMI,      opFUCOMI,
        opFCOMI,       opFCOMI,       opFCOMI,       opFCOMI,       opFCOMI,       opFCOMI,       opFCOMI,       opFCOMI,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
};

const OpFn OP_TABLE(fpu_287_dc_a16)[32] =
{
        opFADDd_a16, opFMULd_a16, opFCOMd_a16, opFCOMPd_a16, opFSUBd_a16, opFSUBRd_a16, opFDIVd_a16, opFDIVRd_a16,
        opFADDd_a16, opFMULd_a16, opFCOMd_a16, opFCOMPd_a16, opFSUBd_a16, opFSUBRd_a16, opFDIVd_a16, opFDIVRd_a16,
        opFADDd_a16, opFMULd_a16, opFCOMd_a16, opFCOMPd_a16, opFSUBd_a16, opFSUBRd_a16, opFDIVd_a16, opFDIVRd_a16,
        opFADDr,     opFMULr,     ILLEGAL_a16, ILLEGAL_a16,  opFSUBRr,    opFSUBr,      opFDIVRr,    opFDIVr
};
const OpFn OP_TABLE(fpu_287_dc_a32)[32] =
{
        opFADDd_a32, opFMULd_a32, opFCOMd_a32, opFCOMPd_a32, opFSUBd_a32, opFSUBRd_a32, opFDIVd_a32, opFDIVRd_a32,
        opFADDd_a32, opFMULd_a32, opFCOMd_a32, opFCOMPd_a32, opFSUBd_a32, opFSUBRd_a32, opFDIVd_a32, opFDIVRd_a32,
        opFADDd_a32, opFMULd_a32, opFCOMd_a32, opFCOMPd_a32, opFSUBd_a32, opFSUBRd_a32, opFDIVd_a32, opFDIVRd_a32,
        opFADDr,     opFMULr,     ILLEGAL_a32, ILLEGAL_a32,  opFSUBRr,    opFSUBr,      opFDIVRr,    opFDIVr
};

const OpFn OP_TABLE(fpu_dc_a16)[32] =
{
        opFADDd_a16, opFMULd_a16, opFCOMd_a16, opFCOMPd_a16, opFSUBd_a16, opFSUBRd_a16, opFDIVd_a16, opFDIVRd_a16,
        opFADDd_a16, opFMULd_a16, opFCOMd_a16, opFCOMPd_a16, opFSUBd_a16, opFSUBRd_a16, opFDIVd_a16, opFDIVRd_a16,
        opFADDd_a16, opFMULd_a16, opFCOMd_a16, opFCOMPd_a16, opFSUBd_a16, opFSUBRd_a16, opFDIVd_a16, opFDIVRd_a16,
        opFADDr,     opFMULr,     opFCOM,      opFCOMP,      opFSUBRr,    opFSUBr,      opFDIVRr,    opFDIVr
};
const OpFn OP_TABLE(fpu_dc_a32)[32] =
{
        opFADDd_a32, opFMULd_a32, opFCOMd_a32, opFCOMPd_a32, opFSUBd_a32, opFSUBRd_a32, opFDIVd_a32, opFDIVRd_a32,
        opFADDd_a32, opFMULd_a32, opFCOMd_a32, opFCOMPd_a32, opFSUBd_a32, opFSUBRd_a32, opFDIVd_a32, opFDIVRd_a32,
        opFADDd_a32, opFMULd_a32, opFCOMd_a32, opFCOMPd_a32, opFSUBd_a32, opFSUBRd_a32, opFDIVd_a32, opFDIVRd_a32,
        opFADDr,     opFMULr,     opFCOM,      opFCOMP,      opFSUBRr,    opFSUBr,      opFDIVRr,    opFDIVr
};

const OpFn OP_TABLE(fpu_287_dd_a16)[256] =
{
        opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,
        opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,
        opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,
        opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,

        opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,
        opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,
        opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,
        opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,

        opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,
        opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,
        opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,
        opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,

        opFFREE,       opFFREE,       opFFREE,       opFFREE,       opFFREE,       opFFREE,       opFFREE,       opFFREE,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFST,         opFST,         opFST,         opFST,         opFST,         opFST,         opFST,         opFST,
        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
};
const OpFn OP_TABLE(fpu_287_dd_a32)[256] =
{
        opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,
        opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,
        opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,
        opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,

        opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,
        opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,
        opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,
        opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,

        opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,
        opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,
        opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,
        opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,

        opFFREE,       opFFREE,       opFFREE,       opFFREE,       opFFREE,       opFFREE,       opFFREE,       opFFREE,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFST,         opFST,         opFST,         opFST,         opFST,         opFST,         opFST,         opFST,
        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
};

const OpFn OP_TABLE(fpu_dd_a16)[256] =
{
        opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,
        opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,
        opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,
        opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,

        opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,
        opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,
        opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,
        opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,

        opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,    opFLDd_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,    opFSTd_a16,
        opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,   opFSTPd_a16,
        opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,   opFSTOR_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,   opFSAVE_a16,
        opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,   opFSTSW_a16,

        opFFREE,       opFFREE,       opFFREE,       opFFREE,       opFFREE,       opFFREE,       opFFREE,       opFFREE,
        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,
        opFST,         opFST,         opFST,         opFST,         opFST,         opFST,         opFST,         opFST,
        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,
        opFUCOM,       opFUCOM,       opFUCOM,       opFUCOM,       opFUCOM,       opFUCOM,       opFUCOM,       opFUCOM,
        opFUCOMP,      opFUCOMP,      opFUCOMP,      opFUCOMP,      opFUCOMP,      opFUCOMP,      opFUCOMP,      opFUCOMP,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
};
const OpFn OP_TABLE(fpu_dd_a32)[256] =
{
        opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,
        opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,
        opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,
        opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,

        opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,
        opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,
        opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,
        opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,

        opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,    opFLDd_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,    opFSTd_a32,
        opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,   opFSTPd_a32,
        opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,   opFSTOR_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,   opFSAVE_a32,
        opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,   opFSTSW_a32,

        opFFREE,       opFFREE,       opFFREE,       opFFREE,       opFFREE,       opFFREE,       opFFREE,       opFFREE,
        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,
        opFST,         opFST,         opFST,         opFST,         opFST,         opFST,         opFST,         opFST,
        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,
        opFUCOM,       opFUCOM,       opFUCOM,       opFUCOM,       opFUCOM,       opFUCOM,       opFUCOM,       opFUCOM,
        opFUCOMP,      opFUCOMP,      opFUCOMP,      opFUCOMP,      opFUCOMP,      opFUCOMP,      opFUCOMP,      opFUCOMP,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
};

const OpFn OP_TABLE(fpu_287_de_a16)[256] =
{
        opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,
        opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,
        opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,
        opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16,
        opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,
        opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16,
        opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,
        opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16,

        opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,
        opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,
        opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,
        opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16,
        opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,
        opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16,
        opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,
        opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16,

        opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,
        opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,
        opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,
        opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16,
        opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,
        opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16,
        opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,
        opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16,

        opFADDP,       opFADDP,       opFADDP,       opFADDP,       opFADDP,       opFADDP,       opFADDP,       opFADDP,
        opFMULP,       opFMULP,       opFMULP,       opFMULP,       opFMULP,       opFMULP,       opFMULP,       opFMULP,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  opFCOMPP,     ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,
        opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,
        opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,
        opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,
};

const OpFn OP_TABLE(fpu_287_de_a32)[256] =
{
        opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,
        opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,
        opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,
        opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32,
        opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,
        opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32,
        opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,
        opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32,

        opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,
        opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,
        opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,
        opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32,
        opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,
        opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32,
        opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,
        opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32,

        opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,
        opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,
        opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,
        opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32,
        opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,
        opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32,
        opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,
        opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32,

        opFADDP,       opFADDP,       opFADDP,       opFADDP,       opFADDP,       opFADDP,       opFADDP,       opFADDP,
        opFMULP,       opFMULP,       opFMULP,       opFMULP,       opFMULP,       opFMULP,       opFMULP,       opFMULP,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  opFCOMPP,     ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,
        opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,
        opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,
        opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,
};

const OpFn OP_TABLE(fpu_de_a16)[256] =
{
        opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,
        opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,
        opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,
        opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16,
        opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,
        opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16,
        opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,
        opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16,

        opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,
        opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,
        opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,
        opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16,
        opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,
        opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16,
        opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,
        opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16,

        opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,  opFADDiw_a16,
        opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,  opFMULiw_a16,
        opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,  opFCOMiw_a16,
        opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16, opFCOMPiw_a16,
        opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,  opFSUBiw_a16,
        opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16, opFSUBRiw_a16,
        opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,  opFDIViw_a16,
        opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16, opFDIVRiw_a16,

        opFADDP,       opFADDP,       opFADDP,       opFADDP,       opFADDP,       opFADDP,       opFADDP,       opFADDP,
        opFMULP,       opFMULP,       opFMULP,       opFMULP,       opFMULP,       opFMULP,       opFMULP,       opFMULP,
        opFCOMP,       opFCOMP,       opFCOMP,       opFCOMP,       opFCOMP,       opFCOMP,       opFCOMP,       opFCOMP,
        ILLEGAL_a16,  opFCOMPP,     ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,
        opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,
        opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,
        opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,
};

const OpFn OP_TABLE(fpu_de_a32)[256] =
{
        opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,
        opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,
        opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,
        opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32,
        opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,
        opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32,
        opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,
        opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32,

        opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,
        opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,
        opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,
        opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32,
        opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,
        opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32,
        opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,
        opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32,

        opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,  opFADDiw_a32,
        opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,  opFMULiw_a32,
        opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,  opFCOMiw_a32,
        opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32, opFCOMPiw_a32,
        opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,  opFSUBiw_a32,
        opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32, opFSUBRiw_a32,
        opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,  opFDIViw_a32,
        opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32, opFDIVRiw_a32,

        opFADDP,       opFADDP,       opFADDP,       opFADDP,       opFADDP,       opFADDP,       opFADDP,       opFADDP,
        opFMULP,       opFMULP,       opFMULP,       opFMULP,       opFMULP,       opFMULP,       opFMULP,       opFMULP,
        opFCOMP,       opFCOMP,       opFCOMP,       opFCOMP,       opFCOMP,       opFCOMP,       opFCOMP,       opFCOMP,
        ILLEGAL_a32,  opFCOMPP,     ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,      opFSUBRP,
        opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,       opFSUBP,
        opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,      opFDIVRP,
        opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,       opFDIVP,
};

const OpFn OP_TABLE(fpu_287_df_a16)[256] =
{
        opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,
        opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,
        FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,
        FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,

        opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,
        opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,
        FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,
        FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,

        opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,
        opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,
        FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,
        FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,

        opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFSTSW_AX,   ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
};
const OpFn OP_TABLE(fpu_287_df_a32)[256] =
{
        opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,
        opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,
        FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,
        FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,

        opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,
        opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,
        FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,
        FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,

        opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,
        opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,
        FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,
        FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,

        opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFSTSW_AX,   ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
};

const OpFn OP_TABLE(fpu_df_a16)[256] =
{
        opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,
        opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,
        FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,
        FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,

        opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,
        opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,
        FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,
        FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,

        opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,
        opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,
        FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,
        FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,

        opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,
        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,
        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,
        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,
        opFSTSW_AX,   ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
};
const OpFn OP_TABLE(fpu_df_a32)[256] =
{
        opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,
        opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,
        FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,
        FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,

        opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,
        opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,
        FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,
        FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,

        opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,
        opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,
        FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,
        FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,

        opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,
        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,
        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,
        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,
        opFSTSW_AX,   ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
};

const OpFn OP_TABLE(fpu_686_df_a16)[256] =
{
        opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,
        opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,
        FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,
        FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,

        opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,
        opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,
        FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,
        FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,

        opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,  opFILDiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,  opFISTiw_a16,
        opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16, opFISTPiw_a16,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,  opFILDiq_a16,
        FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,     FBSTP_a16,
        FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,   FISTPiq_a16,

        opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,
        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,
        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,
        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,
        opFSTSW_AX,   ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
        opFUCOMIP,     opFUCOMIP,     opFUCOMIP,     opFUCOMIP,     opFUCOMIP,     opFUCOMIP,     opFUCOMIP,     opFUCOMIP,
        opFCOMIP,      opFCOMIP,      opFCOMIP,      opFCOMIP,      opFCOMIP,      opFCOMIP,      opFCOMIP,      opFCOMIP,
        ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,  ILLEGAL_a16,
};
const OpFn OP_TABLE(fpu_686_df_a32)[256] =
{
        opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,
        opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,
        FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,
        FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,

        opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,
        opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,
        FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,
        FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,

        opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,  opFILDiw_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,  opFISTiw_a32,
        opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32, opFISTPiw_a32,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,  opFILDiq_a32,
        FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,     FBSTP_a32,
        FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,   FISTPiq_a32,

        opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,      opFFREEP,
        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,        opFXCH,
        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,
        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,        opFSTP,
        opFSTSW_AX,   ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
        opFUCOMIP,     opFUCOMIP,     opFUCOMIP,     opFUCOMIP,     opFUCOMIP,     opFUCOMIP,     opFUCOMIP,     opFUCOMIP,
        opFCOMIP,      opFCOMIP,      opFCOMIP,      opFCOMIP,      opFCOMIP,      opFCOMIP,      opFCOMIP,      opFCOMIP,
        ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,  ILLEGAL_a32,
};

const OpFn OP_TABLE(nofpu_a16)[256] =
{
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,

        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,

        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,

        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
        op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16, op_nofpu_a16,
};
const OpFn OP_TABLE(nofpu_a32)[256] =
{
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,

        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,

        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,

        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
        op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32, op_nofpu_a32,
};
#endif

#undef ILLEGAL
