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
 * Version:	@(#)x87_ops_loadstore.h	1.0.2	2019/06/11
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
static int opFILDiw_a16(uint32_t fetchdat)
{
        int16_t temp;
        FP_ENTER();
        fetch_ea_16(fetchdat);
	SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteaw(); if (cpu_state.abrt) return 1;
        x87_push((double)temp);
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_16) : (x87_timings.fild_16 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_16) : (x87_concurrency.fild_16 * cpu_multi));
        return 0;
}
#ifndef FPU_8087
static int opFILDiw_a32(uint32_t fetchdat)
{
        int16_t temp;
        FP_ENTER();
        fetch_ea_32(fetchdat);
	SEG_CHECK_READ(cpu_state.ea_seg);
        temp = geteaw(); if (cpu_state.abrt) return 1;
        x87_push((double)temp);
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_16) : (x87_timings.fild_16 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_16) : (x87_concurrency.fild_16 * cpu_multi));
        return 0;
}
#endif

static int opFISTiw_a16(uint32_t fetchdat)
{
        FP_ENTER();
        fetch_ea_16(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        seteaw(x87_fround16(ST(0)));
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_16) : (x87_timings.fist_16 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_16) : (x87_concurrency.fist_16 * cpu_multi));
        return cpu_state.abrt;
}
#ifndef FPU_8087
static int opFISTiw_a32(uint32_t fetchdat)
{
        FP_ENTER();
        fetch_ea_32(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        seteaw(x87_fround16(ST(0)));
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_16) : (x87_timings.fist_16 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_16) : (x87_concurrency.fist_16 * cpu_multi));
        return cpu_state.abrt;
}
#endif

static int opFISTPiw_a16(uint32_t fetchdat)
{
        FP_ENTER();
        fetch_ea_16(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        seteaw(x87_fround16(ST(0))); if (cpu_state.abrt) return 1;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_16) : (x87_timings.fist_16 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_16) : (x87_concurrency.fist_16 * cpu_multi));
        return 0;
}
#ifndef FPU_8087
static int opFISTPiw_a32(uint32_t fetchdat)
{
        FP_ENTER();
        fetch_ea_32(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        seteaw(x87_fround16(ST(0))); if (cpu_state.abrt) return 1;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_16) : (x87_timings.fist_16 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_16) : (x87_concurrency.fist_16 * cpu_multi));
        return 0;
}
#endif

static int opFILDiq_a16(uint32_t fetchdat)
{
        int64_t temp64;
        FP_ENTER();
        fetch_ea_16(fetchdat);
	SEG_CHECK_READ(cpu_state.ea_seg);
        temp64 = geteaq(); if (cpu_state.abrt) return 1;
        x87_push((double)temp64);
        cpu_state.MM[cpu_state.TOP&7].q = temp64;
	FP_TAG_DEFAULT;

        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_64) : (x87_timings.fild_64 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_64) : (x87_concurrency.fild_64 * cpu_multi));
        return 0;
}
#ifndef FPU_8087
static int opFILDiq_a32(uint32_t fetchdat)
{
        int64_t temp64;
        FP_ENTER();
        fetch_ea_32(fetchdat);
	SEG_CHECK_READ(cpu_state.ea_seg);
        temp64 = geteaq(); if (cpu_state.abrt) return 1;
        x87_push((double)temp64);
        cpu_state.MM[cpu_state.TOP&7].q = temp64;
	FP_TAG_DEFAULT;

        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_64) : (x87_timings.fild_64 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_64) : (x87_concurrency.fild_64 * cpu_multi));
        return 0;
}
#endif

static int FBSTP_a16(uint32_t fetchdat)
{
        double tempd;
        int c;
        FP_ENTER();
        fetch_ea_16(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        tempd = ST(0);
        if (tempd < 0.0)
                tempd = -tempd;
        for (c = 0; c < 9; c++)
        {
                uint8_t tempc = (uint8_t)floor(fmod(tempd, 10.0));
                tempd -= floor(fmod(tempd, 10.0));
                tempd /= 10.0;
                tempc |= ((uint8_t)floor(fmod(tempd, 10.0))) << 4;
                tempd -= floor(fmod(tempd, 10.0));
                tempd /= 10.0;
                writememb(easeg, cpu_state.eaaddr + c, tempc);
        }
        tempc = (uint8_t)floor(fmod(tempd, 10.0));
        if (ST(0) < 0.0) tempc |= 0x80;
        writememb(easeg, cpu_state.eaaddr + 9, tempc); if (cpu_state.abrt) return 1;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fbstp) : (x87_timings.fbstp * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fbstp) : (x87_concurrency.fbstp * cpu_multi));
        return 0;
}
#ifndef FPU_8087
static int FBSTP_a32(uint32_t fetchdat)
{
        double tempd;
        int c;
        FP_ENTER();
        fetch_ea_32(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        tempd = ST(0);
        if (tempd < 0.0)
                tempd = -tempd;
        for (c = 0; c < 9; c++)
        {
                uint8_t tempc = (uint8_t)floor(fmod(tempd, 10.0));
                tempd -= floor(fmod(tempd, 10.0));
                tempd /= 10.0;
                tempc |= ((uint8_t)floor(fmod(tempd, 10.0))) << 4;
                tempd -= floor(fmod(tempd, 10.0));
                tempd /= 10.0;
                writememb(easeg, cpu_state.eaaddr + c, tempc);
        }
        tempc = (uint8_t)floor(fmod(tempd, 10.0));
        if (ST(0) < 0.0) tempc |= 0x80;
        writememb(easeg, cpu_state.eaaddr + 9, tempc); if (cpu_state.abrt) return 1;
        x87_pop();
		CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fbstp) : (x87_timings.fbstp * cpu_multi));
        return 0;
}
#endif

static int FISTPiq_a16(uint32_t fetchdat)
{
        int64_t temp64;
        FP_ENTER();
        fetch_ea_16(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        if (cpu_state.tag[cpu_state.TOP&7] & TAG_UINT64)
                temp64 = cpu_state.MM[cpu_state.TOP&7].q;
        else
                temp64 = x87_fround(ST(0));
        seteaq(temp64); if (cpu_state.abrt) return 1;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_64) : (x87_timings.fist_64 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_64) : (x87_concurrency.fist_64 * cpu_multi));
        return 0;
}
#ifndef FPU_8087
static int FISTPiq_a32(uint32_t fetchdat)
{
        int64_t temp64;
        FP_ENTER();
        fetch_ea_32(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        if (cpu_state.tag[cpu_state.TOP&7] & TAG_UINT64)
                temp64 = cpu_state.MM[cpu_state.TOP&7].q;
        else
                temp64 = x87_fround(ST(0));
        seteaq(temp64); if (cpu_state.abrt) return 1;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_64) : (x87_timings.fist_64 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_64) : (x87_concurrency.fist_64 * cpu_multi));
        return 0;
}
#endif

static int opFILDil_a16(uint32_t fetchdat)
{
        int32_t templ;
        FP_ENTER();
        fetch_ea_16(fetchdat);
	SEG_CHECK_READ(cpu_state.ea_seg);
        templ = geteal(); if (cpu_state.abrt) return 1;
        x87_push((double)templ);
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_32) : (x87_timings.fild_32 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_32) : (x87_concurrency.fild_32 * cpu_multi));
        return 0;
}
#ifndef FPU_8087
static int opFILDil_a32(uint32_t fetchdat)
{
        int32_t templ;
        FP_ENTER();
        fetch_ea_32(fetchdat);
	SEG_CHECK_READ(cpu_state.ea_seg);
        templ = geteal(); if (cpu_state.abrt) return 1;
        x87_push((double)templ);
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_32) : (x87_timings.fild_32 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_32) : (x87_concurrency.fild_32 * cpu_multi));
        return 0;
}
#endif

static int opFISTil_a16(uint32_t fetchdat)
{
        FP_ENTER();
        fetch_ea_16(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        seteal(x87_fround32(ST(0)));
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_32) : (x87_timings.fist_32 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_32) : (x87_concurrency.fist_32 * cpu_multi));
        return cpu_state.abrt;
}
#ifndef FPU_8087
static int opFISTil_a32(uint32_t fetchdat)
{
        FP_ENTER();
        fetch_ea_32(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        seteal(x87_fround32(ST(0)));
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_32) : (x87_timings.fist_32 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_32) : (x87_concurrency.fist_32 * cpu_multi));
        return cpu_state.abrt;
}
#endif

static int opFISTPil_a16(uint32_t fetchdat)
{
        FP_ENTER();
        fetch_ea_16(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        seteal(x87_fround32(ST(0))); if (cpu_state.abrt) return 1;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_32) : (x87_timings.fist_32 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_32) : (x87_concurrency.fist_32 * cpu_multi));
        return 0;
}
#ifndef FPU_8087
static int opFISTPil_a32(uint32_t fetchdat)
{
        FP_ENTER();
        fetch_ea_32(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        seteal(x87_fround32(ST(0))); if (cpu_state.abrt) return 1;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_32) : (x87_timings.fist_32 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_32) : (x87_concurrency.fist_32 * cpu_multi));
        return 0;
}
#endif

static int opFLDe_a16(uint32_t fetchdat)
{
        double t;
        FP_ENTER();
        fetch_ea_16(fetchdat);
	SEG_CHECK_READ(cpu_state.ea_seg);
        t=x87_ld80(); if (cpu_state.abrt) return 1;
        x87_push(t);
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_80) : (x87_timings.fld_80 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_80) : (x87_concurrency.fld_80 * cpu_multi));
        return 0;
}
#ifndef FPU_8087
static int opFLDe_a32(uint32_t fetchdat)
{
        double t;
        FP_ENTER();
        fetch_ea_32(fetchdat);
	SEG_CHECK_READ(cpu_state.ea_seg);
        t=x87_ld80(); if (cpu_state.abrt) return 1;
        x87_push(t);
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_80) : (x87_timings.fld_80 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_80) : (x87_concurrency.fld_80 * cpu_multi));
        return 0;
}
#endif

static int opFSTPe_a16(uint32_t fetchdat)
{
        FP_ENTER();
        fetch_ea_16(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        x87_st80(ST(0)); if (cpu_state.abrt) return 1;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_80) : (x87_timings.fld_80 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_80) : (x87_concurrency.fld_80 * cpu_multi));
        return 0;
}
#ifndef FPU_8087
static int opFSTPe_a32(uint32_t fetchdat)
{
        FP_ENTER();
        fetch_ea_32(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        x87_st80(ST(0)); if (cpu_state.abrt) return 1;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_80) : (x87_timings.fld_80 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_80) : (x87_concurrency.fld_80 * cpu_multi));
        return 0;
}
#endif

static int opFLDd_a16(uint32_t fetchdat)
{
        x87_td t;
        FP_ENTER();
        fetch_ea_16(fetchdat);
	SEG_CHECK_READ(cpu_state.ea_seg);
        t.i = geteaq(); if (cpu_state.abrt) return 1;
        x87_push(t.d);
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_64) : (x87_timings.fld_64 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_64) : (x87_concurrency.fld_64 * cpu_multi));
        return 0;
}
#ifndef FPU_8087
static int opFLDd_a32(uint32_t fetchdat)
{
        x87_td t;
        FP_ENTER();
        fetch_ea_32(fetchdat);
	SEG_CHECK_READ(cpu_state.ea_seg);
        t.i = geteaq(); if (cpu_state.abrt) return 1;
        x87_push(t.d);
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_64) : (x87_timings.fld_64 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_64) : (x87_concurrency.fld_64 * cpu_multi));
        return 0;
}
#endif

static int opFSTd_a16(uint32_t fetchdat)
{
        x87_td t;
        FP_ENTER();
        fetch_ea_16(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        t.d = ST(0);
        seteaq(t.i);
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_64) : (x87_timings.fst_64 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_64) : (x87_concurrency.fst_64 * cpu_multi));
        return cpu_state.abrt;
}
#ifndef FPU_8087
static int opFSTd_a32(uint32_t fetchdat)
{
        x87_td t;
        FP_ENTER();
        fetch_ea_32(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        t.d = ST(0);
        seteaq(t.i);
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_64) : (x87_timings.fst_64 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_64) : (x87_concurrency.fst_64 * cpu_multi));
        return cpu_state.abrt;
}
#endif

static int opFSTPd_a16(uint32_t fetchdat)
{
        x87_td t;
        FP_ENTER();
        fetch_ea_16(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        t.d = ST(0);
        seteaq(t.i); if (cpu_state.abrt) return 1;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_64) : (x87_timings.fst_64 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_64) : (x87_concurrency.fst_64 * cpu_multi));
        return 0;
}
#ifndef FPU_8087
static int opFSTPd_a32(uint32_t fetchdat)
{
        x87_td t;
        FP_ENTER();
        fetch_ea_32(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        t.d = ST(0);
        seteaq(t.i); if (cpu_state.abrt) return 1;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_64) : (x87_timings.fst_64 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_64) : (x87_concurrency.fst_64 * cpu_multi));
        return 0;
}
#endif

static int opFLDs_a16(uint32_t fetchdat)
{
        x87_ts ts;
        FP_ENTER();
        fetch_ea_16(fetchdat);
	SEG_CHECK_READ(cpu_state.ea_seg);
        ts.i = geteal(); if (cpu_state.abrt) return 1;
        x87_push((double)ts.s);
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
        return 0;
}
#ifndef FPU_8087
static int opFLDs_a32(uint32_t fetchdat)
{
        x87_ts ts;
        FP_ENTER();
        fetch_ea_32(fetchdat);
	SEG_CHECK_READ(cpu_state.ea_seg);
        ts.i = geteal(); if (cpu_state.abrt) return 1;
        x87_push((double)ts.s);
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
        return 0;
}
#endif

static int opFSTs_a16(uint32_t fetchdat)
{
        x87_ts ts;
        FP_ENTER();
        fetch_ea_16(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        ts.s = (float)ST(0);
        seteal(ts.i);
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
        return cpu_state.abrt;
}
#ifndef FPU_8087
static int opFSTs_a32(uint32_t fetchdat)
{
        x87_ts ts;
        FP_ENTER();
        fetch_ea_32(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        ts.s = (float)ST(0);
        seteal(ts.i);
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
        return cpu_state.abrt;
}
#endif

static int opFSTPs_a16(uint32_t fetchdat)
{
        x87_ts ts;
        FP_ENTER();
        fetch_ea_16(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        ts.s = (float)ST(0);
        seteal(ts.i); if (cpu_state.abrt) return 1;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
        return 0;
}
#ifndef FPU_8087
static int opFSTPs_a32(uint32_t fetchdat)
{
        x87_ts ts;
        FP_ENTER();
        fetch_ea_32(fetchdat);
	SEG_CHECK_WRITE(cpu_state.ea_seg);
        ts.s = (float)ST(0);
        seteal(ts.i); if (cpu_state.abrt) return 1;
        x87_pop();
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
        return 0;
}
#endif
