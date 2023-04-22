/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          x87 FPU instructions core.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 */

#define swap_values16u(a, b) { uint16_t tmp = a; a = b; b = tmp; }

static int
opFILDiw_a16(uint32_t fetchdat)
{
    floatx80 result;
    int16_t temp;

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        temp = geteaw();
        if (cpu_state.abrt)
            return 1;
        x87_push((double) temp);
    } else {
        temp = (int16_t)readmemw(easeg, cpu_state.eaaddr);
        clear_C1();
        //pclog("FILDiw_a16.\n");
        if (!IS_TAG_EMPTY(-1)) {
            x87_stack_overflow();
        } else {
            result = int32_to_floatx80(temp);
            x87_push_ext();
            x87_save_reg_ext(result, -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_16) : (x87_timings.fild_16 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_16) : (x87_concurrency.fild_16 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFILDiw_a32(uint32_t fetchdat)
{
    floatx80 result;
    int16_t temp;

    pclog("FILDiw_a32.\n");
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        temp = geteaw();
        if (cpu_state.abrt)
            return 1;
        x87_push((double) temp);
    } else {
        temp = (int16_t)readmemw(easeg, cpu_state.eaaddr);
        clear_C1();
        if (!IS_TAG_EMPTY(-1)) {
            x87_stack_overflow();
        } else {
            result = int32_to_floatx80(temp);
            x87_push_ext();
            x87_save_reg_ext(result, -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_16) : (x87_timings.fild_16 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_16) : (x87_concurrency.fild_16 * cpu_multi));
    return 0;
}
#endif

static int
opFISTiw_a16(uint32_t fetchdat)
{
    float_status_t status;
    uint16_t sw = cpu_state.npxs;
    int16_t save_reg = int16_indefinite;

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec)
        seteaw(x87_fround16(ST(0)));
    else {
        //pclog("FISTiw_a16.\n");
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_CW_Underflow, 0);
            if (!is_IA_masked())
                return 1;
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            save_reg = floatx80_to_int16(x87_read_reg_ext(0), &status);
            if (x87_checkexceptions(status.float_exception_flags, 1))
                return 1;
        }
        // store to the memory might generate an exception, in this case origial FPU_SW must be kept
        swap_values16u(sw, cpu_state.npxs);
        writememw(easeg, cpu_state.eaaddr, (uint16_t)save_reg);
        cpu_state.npxs = sw;
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_16) : (x87_timings.fist_16 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_16) : (x87_concurrency.fist_16 * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
opFISTiw_a32(uint32_t fetchdat)
{
    float_status_t status;
    uint16_t sw = cpu_state.npxs;
    int16_t save_reg = int16_indefinite;

    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec)
        seteaw(x87_fround16(ST(0)));
    else {
        //pclog("FILDiw_a32.\n");
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_CW_Underflow, 0);
            if (!is_IA_masked())
                return 1;
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            save_reg = floatx80_to_int16(x87_read_reg_ext(0), &status);
            if (x87_checkexceptions(status.float_exception_flags, 1))
                return 1;
        }
        // store to the memory might generate an exception, in this case origial FPU_SW must be kept
        swap_values16u(sw, cpu_state.npxs);
        writememw(easeg, cpu_state.eaaddr, (uint16_t)save_reg);
        cpu_state.npxs = sw;
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_16) : (x87_timings.fist_16 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_16) : (x87_concurrency.fist_16 * cpu_multi));
    return cpu_state.abrt;
}
#endif

static int
opFISTPiw_a16(uint32_t fetchdat)
{
    float_status_t status;
    uint16_t sw = cpu_state.npxs;
    int16_t save_reg = int16_indefinite;

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        seteaw(x87_fround16(ST(0)));
        if (cpu_state.abrt)
            return 1;
        x87_pop();
    } else {
        //pclog("FISTPiw_a16.\n");
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_CW_Underflow, 0);
            if (!is_IA_masked())
                return 1;
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            save_reg = floatx80_to_int16(x87_read_reg_ext(0), &status);
            if (x87_checkexceptions(status.float_exception_flags, 1))
                return 1;
        }
        // store to the memory might generate an exception, in this case original FPU_SW must be kept
        swap_values16u(sw, cpu_state.npxs);
        writememw(easeg, cpu_state.eaaddr, (uint16_t)save_reg);
        cpu_state.npxs = sw;
        x87_pop_ext();
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_16) : (x87_timings.fist_16 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_16) : (x87_concurrency.fist_16 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFISTPiw_a32(uint32_t fetchdat)
{
    float_status_t status;
    uint16_t sw = cpu_state.npxs;
    int16_t save_reg = int16_indefinite;

    pclog("FISTPiw_a32.\n");
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        seteaw(x87_fround16(ST(0)));
        if (cpu_state.abrt)
            return 1;
        x87_pop();
    } else {
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_CW_Underflow, 0);
            if (!is_IA_masked())
                return 1;
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            save_reg = floatx80_to_int16(x87_read_reg_ext(0), &status);
            if (x87_checkexceptions(status.float_exception_flags, 1))
                return 1;
        }
        // store to the memory might generate an exception, in this case original FPU_SW must be kept
        swap_values16u(sw, cpu_state.npxs);
        writememw(easeg, cpu_state.eaaddr, (uint16_t)save_reg);
        cpu_state.npxs = sw;
        x87_pop_ext();
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_16) : (x87_timings.fist_16 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_16) : (x87_concurrency.fist_16 * cpu_multi));
    return 0;
}
#endif

static int
opFILDiq_a16(uint32_t fetchdat)
{
    floatx80 result;
    int64_t temp64;

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        temp64 = geteaq();
        if (cpu_state.abrt)
            return 1;
        x87_push((double) temp64);
        cpu_state.MM[cpu_state.TOP & 7].q = temp64;
        FP_TAG_DEFAULT;
    } else {
        //pclog("FILDiq_a16.\n");
        temp64 = (int64_t)readmemq(easeg, cpu_state.eaaddr);
        clear_C1();
        if (!IS_TAG_EMPTY(-1)) {
            x87_stack_overflow();
        } else {
            result = int64_to_floatx80(temp64);
            x87_push_ext();
            x87_save_reg_ext(result, -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_64) : (x87_timings.fild_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_64) : (x87_concurrency.fild_64 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFILDiq_a32(uint32_t fetchdat)
{
    floatx80 result;
    int64_t temp64;

    pclog("FILDiq_a32.\n");
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        temp64 = geteaq();
        if (cpu_state.abrt)
            return 1;
        x87_push((double) temp64);
        cpu_state.MM[cpu_state.TOP & 7].q = temp64;
        FP_TAG_DEFAULT;
    } else {
        temp64 = (int64_t)readmemq(easeg, cpu_state.eaaddr);
        clear_C1();
        if (!IS_TAG_EMPTY(-1)) {
            x87_stack_overflow();
        } else {
            result = int64_to_floatx80(temp64);
            x87_push_ext();
            x87_save_reg_ext(result, -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_64) : (x87_timings.fild_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_64) : (x87_concurrency.fild_64 * cpu_multi));
    return 0;
}
#endif

static int
FBSTP_a16(uint32_t fetchdat)
{
    float_status_t status;
    uint16_t sw = cpu_state.npxs;
    uint16_t save_reg_hi = 0xffff;
    uint64_t save_reg_lo = BX_CONST64(0xC000000000000000);
    floatx80 reg;
    int64_t save_val;
    int sign;
    double tempd;
    int    c;

    pclog("FBSTP_a16.\n");
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        tempd = ST(0);
        if (tempd < 0.0)
            tempd = -tempd;
        for (c = 0; c < 9; c++) {
            uint8_t tempc = (uint8_t) floor(fmod(tempd, 10.0));
            tempd -= floor(fmod(tempd, 10.0));
            tempd /= 10.0;
            tempc |= ((uint8_t) floor(fmod(tempd, 10.0))) << 4;
            tempd -= floor(fmod(tempd, 10.0));
            tempd /= 10.0;
            writememb(easeg, cpu_state.eaaddr + c, tempc);
        }
        tempc = (uint8_t) floor(fmod(tempd, 10.0));
        if (ST(0) < 0.0)
            tempc |= 0x80;
        writememb(easeg, cpu_state.eaaddr + 9, tempc);
        if (cpu_state.abrt)
            return 1;
        x87_pop();
    } else {
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_EX_Stack_Underflow, 0);
            if (!is_IA_masked())
                return 1;
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            reg = x87_read_reg_ext(0);
            save_val = floatx80_to_int64(reg, &status);
            sign = (reg.exp & 0x8000) != 0;
            if (sign)
                save_val = -save_val;

            if (save_val > BX_CONST64(999999999999999999)) {
                status.float_exception_flags = float_flag_invalid; // throw away other flags
                pclog("save_val invalid.\n");
            }

            if (!(status.float_exception_flags & float_flag_invalid)) {
                save_reg_hi = sign ? 0x8000 : 0;
                save_reg_lo = 0;
                for (int i = 0; i < 16; i++) {
                    save_reg_lo += ((uint64_t)(save_val % 10)) << (4 * i);
                    save_val /= 10;
                }
                save_reg_hi += (uint16_t)(save_val % 10);
                save_val /= 10;
                save_reg_hi += (uint16_t)(save_val % 10) << 4;
            }
            /* check for fpu arithmetic exceptions */
            if (x87_checkexceptions(status.float_exception_flags, 1))
                return 1;
        }
        // store to the memory might generate an exception, in this case original FPU_SW must be kept
        swap_values16u(sw, cpu_state.npxs);

        // write packed bcd to memory
        writememq(easeg, cpu_state.eaaddr, save_reg_lo);
        writememw(easeg, cpu_state.eaaddr + 8, save_reg_hi);
        cpu_state.npxs = sw;
        x87_pop_ext();
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fbstp) : (x87_timings.fbstp * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fbstp) : (x87_concurrency.fbstp * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
FBSTP_a32(uint32_t fetchdat)
{
    float_status_t status;
    uint16_t sw = cpu_state.npxs;
    uint16_t save_reg_hi = 0xffff;
    uint64_t save_reg_lo = BX_CONST64(0xC000000000000000);
    floatx80 reg;
    int64_t save_val;
    int sign;
    double tempd;
    int    c;

    pclog("FBSTP_a32.\n");
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        tempd = ST(0);
        if (tempd < 0.0)
            tempd = -tempd;
        for (c = 0; c < 9; c++) {
            uint8_t tempc = (uint8_t) floor(fmod(tempd, 10.0));
            tempd -= floor(fmod(tempd, 10.0));
            tempd /= 10.0;
            tempc |= ((uint8_t) floor(fmod(tempd, 10.0))) << 4;
            tempd -= floor(fmod(tempd, 10.0));
            tempd /= 10.0;
            writememb(easeg, cpu_state.eaaddr + c, tempc);
        }
        tempc = (uint8_t) floor(fmod(tempd, 10.0));
        if (ST(0) < 0.0)
            tempc |= 0x80;
        writememb(easeg, cpu_state.eaaddr + 9, tempc);
        if (cpu_state.abrt)
            return 1;
        x87_pop();
    } else {
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_EX_Stack_Underflow, 0);
            if (!is_IA_masked())
                return 1;
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            reg = x87_read_reg_ext(0);
            save_val = floatx80_to_int64(reg, &status);
            sign = (reg.exp & 0x8000) != 0;
            if (sign)
                save_val = -save_val;

            if (save_val > BX_CONST64(999999999999999999))
                status.float_exception_flags = float_flag_invalid; // throw away other flags

            if (!(status.float_exception_flags & float_flag_invalid)) {
                save_reg_hi = sign ? 0x8000 : 0;
                save_reg_lo = 0;
                for (int i = 0; i < 16; i++) {
                    save_reg_lo += ((uint64_t)(save_val % 10)) << (4 * i);
                    save_val /= 10;
                }
                save_reg_hi += (uint16_t)(save_val % 10);
                save_val /= 10;
                save_reg_hi += (uint16_t)(save_val % 10) << 4;
            }
            /* check for fpu arithmetic exceptions */
            if (x87_checkexceptions(status.float_exception_flags, 1))
                return 1;
        }
        // store to the memory might generate an exception, in this case original FPU_SW must be kept
        swap_values16u(sw, cpu_state.npxs);

        // write packed bcd to memory
        writememq(easeg, cpu_state.eaaddr, save_reg_lo);
        writememw(easeg, cpu_state.eaaddr + 8, save_reg_hi);
        cpu_state.npxs = sw;
        x87_pop_ext();
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fbstp) : (x87_timings.fbstp * cpu_multi));
    return 0;
}
#endif

static int
FISTPiq_a16(uint32_t fetchdat)
{
    float_status_t status;
    uint16_t sw = cpu_state.npxs;
    int64_t save_reg = int64_indefinite;
    int64_t temp64;

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        if (cpu_state.tag[cpu_state.TOP & 7] & TAG_UINT64)
            temp64 = cpu_state.MM[cpu_state.TOP & 7].q;
        else
            temp64 = x87_fround(ST(0));
        seteaq(temp64);
        if (cpu_state.abrt)
            return 1;
        x87_pop();
    } else {
        //pclog("FISTPiq_a16.\n");
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_CW_Underflow, 0);
            if (!is_IA_masked())
                return 1;
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            save_reg = floatx80_to_int64(x87_read_reg_ext(0), &status);
            if (x87_checkexceptions(status.float_exception_flags, 1))
                return 1;
        }
        // store to the memory might generate an exception, in this case origial FPU_SW must be kept
        swap_values16u(sw, cpu_state.npxs);
        writememq(easeg, cpu_state.eaaddr, (uint64_t)save_reg);
        cpu_state.npxs = sw;
        x87_pop_ext();
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_64) : (x87_timings.fist_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_64) : (x87_concurrency.fist_64 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
FISTPiq_a32(uint32_t fetchdat)
{
    float_status_t status;
    uint16_t sw = cpu_state.npxs;
    int64_t save_reg = int64_indefinite;
    int64_t temp64;

    //pclog("FISTPiq_a32.\n");
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        if (cpu_state.tag[cpu_state.TOP & 7] & TAG_UINT64)
            temp64 = cpu_state.MM[cpu_state.TOP & 7].q;
        else
            temp64 = x87_fround(ST(0));
        seteaq(temp64);
        if (cpu_state.abrt)
            return 1;
        x87_pop();
    } else {
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_CW_Underflow, 0);
            if (!is_IA_masked())
                return 1;
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            save_reg = floatx80_to_int64(x87_read_reg_ext(0), &status);
            if (x87_checkexceptions(status.float_exception_flags, 1))
                return 1;
        }
        // store to the memory might generate an exception, in this case origial FPU_SW must be kept
        swap_values16u(sw, cpu_state.npxs);
        pclog("FISTP_q result = %08x.\n", save_reg);
        writememq(easeg, cpu_state.eaaddr, (uint64_t)save_reg);
        cpu_state.npxs = sw;
        x87_pop_ext();
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_64) : (x87_timings.fist_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_64) : (x87_concurrency.fist_64 * cpu_multi));
    return 0;
}
#endif

static int
opFILDil_a16(uint32_t fetchdat)
{
    floatx80 result;
    int32_t templ;

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        templ = geteal();
        if (cpu_state.abrt)
            return 1;
        x87_push((double) templ);
    } else {
        templ = (int32_t)readmeml(easeg, cpu_state.eaaddr);
        //pclog("FILDil_a16.\n");
        clear_C1();
        if (!IS_TAG_EMPTY(-1)) {
            x87_stack_overflow();
        } else {
            result = int32_to_floatx80(templ);
            x87_push_ext();
            x87_save_reg_ext(result, -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_32) : (x87_timings.fild_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_32) : (x87_concurrency.fild_32 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFILDil_a32(uint32_t fetchdat)
{
    floatx80 result;
    int32_t templ;

    pclog("FILDil_a32.\n");
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        templ = geteal();
        if (cpu_state.abrt)
            return 1;
        x87_push((double) templ);
    } else {
        templ = (int32_t)readmeml(easeg, cpu_state.eaaddr);
        //pclog("FILDil_a16.\n");
        clear_C1();
        if (!IS_TAG_EMPTY(-1)) {
            x87_stack_overflow();
        } else {
            result = int32_to_floatx80(templ);
            x87_push_ext();
            x87_save_reg_ext(result, -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fild_32) : (x87_timings.fild_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fild_32) : (x87_concurrency.fild_32 * cpu_multi));
    return 0;
}
#endif

static int
opFISTil_a16(uint32_t fetchdat)
{
    float_status_t status;
    uint16_t sw = cpu_state.npxs;
    int32_t save_reg = int32_indefinite;

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec)
        seteal(x87_fround32(ST(0)));
    else {
        //pclog("FISTil_a16.\n");
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_CW_Underflow, 0);
            if (!is_IA_masked())
                return 1;
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            save_reg = floatx80_to_int32(x87_read_reg_ext(0), &status);
            if (x87_checkexceptions(status.float_exception_flags, 1))
                return 1;
        }
        // store to the memory might generate an exception, in this case original FPU_SW must be kept
        swap_values16u(sw, cpu_state.npxs);
        writememl(easeg, cpu_state.eaaddr, (uint32_t)save_reg);
        cpu_state.npxs = sw;
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_32) : (x87_timings.fist_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_32) : (x87_concurrency.fist_32 * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
opFISTil_a32(uint32_t fetchdat)
{
    float_status_t status;
    uint16_t sw = cpu_state.npxs;
    int32_t save_reg = int32_indefinite;

    pclog("FISTil_a32.\n");
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec)
        seteal(x87_fround32(ST(0)));
    else {
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_CW_Underflow, 0);
            if (!is_IA_masked())
                return 1;
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            save_reg = floatx80_to_int32(x87_read_reg_ext(0), &status);
            if (x87_checkexceptions(status.float_exception_flags, 1))
                return 1;
        }
        // store to the memory might generate an exception, in this case original FPU_SW must be kept
        swap_values16u(sw, cpu_state.npxs);
        writememl(easeg, cpu_state.eaaddr, (uint32_t)save_reg);
        cpu_state.npxs = sw;
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_32) : (x87_timings.fist_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_32) : (x87_concurrency.fist_32 * cpu_multi));
    return cpu_state.abrt;
}
#endif

static int
opFISTPil_a16(uint32_t fetchdat)
{
    float_status_t status;
    uint16_t sw = cpu_state.npxs;
    int32_t save_reg = int32_indefinite;

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        seteal(x87_fround32(ST(0)));
        if (cpu_state.abrt)
            return 1;
        x87_pop();
    } else {
        //pclog("FISTPil_a16.\n");
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_CW_Underflow, 0);
            if (!is_IA_masked())
                return 1;
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            save_reg = floatx80_to_int32(x87_read_reg_ext(0), &status);
            if (x87_checkexceptions(status.float_exception_flags, 1))
                return 1;
        }
        // store to the memory might generate an exception, in this case origial FPU_SW must be kept
        swap_values16u(sw, cpu_state.npxs);
        writememl(easeg, cpu_state.eaaddr, (uint32_t)save_reg);
        cpu_state.npxs = sw;
        x87_pop_ext();
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_32) : (x87_timings.fist_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_32) : (x87_concurrency.fist_32 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFISTPil_a32(uint32_t fetchdat)
{
    float_status_t status;
    uint16_t sw = cpu_state.npxs;
    int32_t save_reg = int32_indefinite;

    pclog("FISTPil_a32.\n");
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        seteal(x87_fround32(ST(0)));
        if (cpu_state.abrt)
            return 1;
        x87_pop();
    } else {
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_CW_Underflow, 0);
            if (!is_IA_masked())
                return 1;
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            save_reg = floatx80_to_int32(x87_read_reg_ext(0), &status);
            if (x87_checkexceptions(status.float_exception_flags, 1))
                return 1;
        }
        // store to the memory might generate an exception, in this case origial FPU_SW must be kept
        swap_values16u(sw, cpu_state.npxs);
        writememl(easeg, cpu_state.eaaddr, (uint32_t)save_reg);
        cpu_state.npxs = sw;
        x87_pop_ext();
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fist_32) : (x87_timings.fist_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fist_32) : (x87_concurrency.fist_32 * cpu_multi));
    return 0;
}
#endif

static int
opFLDe_a16(uint32_t fetchdat)
{
    floatx80 result;
    double t;

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        t = x87_ld80();
        if (cpu_state.abrt)
            return 1;
        x87_push(t);
    } else {
        //pclog("FLDe_a16.\n");
        result.fraction = readmemq(easeg, cpu_state.eaaddr);
        result.exp = readmemw(easeg, cpu_state.eaaddr + 8);
        clear_C1();
        if (!IS_TAG_EMPTY(-1)) {
            x87_stack_overflow();
        } else {
            x87_push_ext();
            x87_save_reg_ext(result, -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_80) : (x87_timings.fld_80 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_80) : (x87_concurrency.fld_80 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFLDe_a32(uint32_t fetchdat)
{
    floatx80 result;
    double t;

    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        t = x87_ld80();
        if (cpu_state.abrt)
            return 1;
        x87_push(t);
    } else {
        result.fraction = readmemq(easeg, cpu_state.eaaddr);
        result.exp = readmemw(easeg, cpu_state.eaaddr + 8);
        //pclog("FLDe_a32.\n");
        clear_C1();
        if (!IS_TAG_EMPTY(-1)) {
            x87_stack_overflow();
        } else {
            x87_push_ext();
            x87_save_reg_ext(result, -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_80) : (x87_timings.fld_80 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_80) : (x87_concurrency.fld_80 * cpu_multi));
    return 0;
}
#endif

static int
opFSTPe_a16(uint32_t fetchdat)
{
    const floatx80 floatx80_default_nan = packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);
    floatx80 save_reg;

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        x87_st80(ST(0));
        if (cpu_state.abrt)
            return 1;
        x87_pop();
    } else {
        //pclog("FSTPe_a16.\n");
        save_reg = floatx80_default_nan; /* The masked response */
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_EX_Stack_Underflow, 0);
            if (!is_IA_masked())
                return 1;
        } else {
            save_reg = x87_read_reg_ext(0);
        }
        writememq(easeg, cpu_state.eaaddr, save_reg.fraction);
        writememw(easeg, cpu_state.eaaddr + 8, save_reg.exp);
        x87_pop_ext();
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_80) : (x87_timings.fld_80 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_80) : (x87_concurrency.fld_80 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFSTPe_a32(uint32_t fetchdat)
{
    const floatx80 floatx80_default_nan = packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);
    floatx80 save_reg;

    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        x87_st80(ST(0));
        if (cpu_state.abrt)
            return 1;
        x87_pop();
    } else {
        //pclog("FSTPe_a32.\n");
        save_reg = floatx80_default_nan; /* The masked response */
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_EX_Stack_Underflow, 0);
            if (!is_IA_masked())
                return 1;
        } else {
            save_reg = x87_read_reg_ext(0);
        }
        writememq(easeg, cpu_state.eaaddr, save_reg.fraction);
        writememw(easeg, cpu_state.eaaddr + 8, save_reg.exp);
        x87_pop_ext();
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_80) : (x87_timings.fld_80 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_80) : (x87_concurrency.fld_80 * cpu_multi));
    return 0;
}
#endif

static int
opFLDd_a16(uint32_t fetchdat)
{
    float_status_t status;
    floatx80 result;
    float64 load_reg;
    x87_td t;
    unsigned unmasked;

    //pclog("FLDd_a16.\n");
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        t.i = geteaq();
        if (cpu_state.abrt)
            return 1;
        x87_push(t.d);
    } else {
        load_reg = readmemq(easeg, cpu_state.eaaddr);
        clear_C1();
        if (!IS_TAG_EMPTY(-1)) {
            x87_stack_overflow();
            pclog("Overflow.\n");
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        result = float64_to_floatx80(load_reg, &status);
        pclog("FLD_d result = %08x.\n", result);
        unmasked = x87_checkexceptions(status.float_exception_flags, 0);
        if (!(unmasked & FPU_CW_Invalid)) {
            pclog("FLD successful.\n");
            x87_push_ext();
            x87_save_reg_ext(result, -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_64) : (x87_timings.fld_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_64) : (x87_concurrency.fld_64 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFLDd_a32(uint32_t fetchdat)
{
    float_status_t status;
    floatx80 result;
    float64 load_reg;
    x87_td t;
    unsigned unmasked;

    //pclog("FLDd_a32.\n");
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        t.i = geteaq();
        if (cpu_state.abrt)
            return 1;
        x87_push(t.d);
    } else {
        load_reg = readmemq(easeg, cpu_state.eaaddr);
        clear_C1();
        if (!IS_TAG_EMPTY(-1)) {
            x87_stack_overflow();
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        result = float64_to_floatx80(load_reg, &status);
        pclog("FLD_d result = %08x.\n", result);
        unmasked = x87_checkexceptions(status.float_exception_flags, 0);
        if (!(unmasked & FPU_CW_Invalid)) {
            x87_push_ext();
            x87_save_reg_ext(result, -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_64) : (x87_timings.fld_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_64) : (x87_concurrency.fld_64 * cpu_multi));
    return 0;
}
#endif

static int
opFSTd_a16(uint32_t fetchdat)
{
    float_status_t status;
    uint16_t sw = cpu_state.npxs;
    float64 save_reg = float64_default_nan;
    x87_td t;
    //pclog("FSTd_a16.\n");

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        t.d = ST(0);
        seteaq(t.i);
    } else {
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_CW_Underflow, 0);
            if (!is_IA_masked())
                return 1;
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            save_reg = floatx80_to_float64(x87_read_reg_ext(0), &status);
            if (x87_checkexceptions(status.float_exception_flags, 1))
                return 1;
        }
        // store to the memory might generate an exception, in this case original FPU_SW must be kept
        swap_values16u(sw, cpu_state.npxs);
        //pclog("Double val = %08x.\n", t.i);
        writememq(easeg, cpu_state.eaaddr, save_reg);
        cpu_state.npxs = sw;
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_64) : (x87_timings.fst_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_64) : (x87_concurrency.fst_64 * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
opFSTd_a32(uint32_t fetchdat)
{
    float_status_t status;
    uint16_t sw = cpu_state.npxs;
    float64 save_reg = float64_default_nan;
    x87_td t;

    pclog("FSTd_a32.\n");
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        t.d = ST(0);
        seteaq(t.i);
    } else {
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_CW_Underflow, 0);
            if (!is_IA_masked())
                return 1;
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            save_reg = floatx80_to_float64(x87_read_reg_ext(0), &status);
            if (x87_checkexceptions(status.float_exception_flags, 1))
                return 1;
        }
        // store to the memory might generate an exception, in this case original FPU_SW must be kept
        swap_values16u(sw, cpu_state.npxs);
        //pclog("Double val = %08x.\n", t.i);
        writememq(easeg, cpu_state.eaaddr, save_reg);
        cpu_state.npxs = sw;
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_64) : (x87_timings.fst_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_64) : (x87_concurrency.fst_64 * cpu_multi));
    return cpu_state.abrt;
}
#endif

static int
opFSTPd_a16(uint32_t fetchdat)
{
    float_status_t status;
    uint16_t sw = cpu_state.npxs;
    float64 save_reg = float64_default_nan;
    x87_td t;

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        t.d = ST(0);
        seteaq(t.i);
        if (cpu_state.abrt)
            return 1;
        x87_pop();
    } else {
        //pclog("FSTPd_a16.\n");
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_CW_Underflow, 0);
            if (!is_IA_masked())
                return 1;
        } else {
            i387cw_to_softfloat_status_word(&status, cpu_state.npxc);
            save_reg = floatx80_to_float64(x87_read_reg_ext(0), &status);
            if (x87_checkexceptions(status.float_exception_flags, 1))
                return 1;
        }
        // store to the memory might generate an exception, in this case original FPU_SW must be kept
        swap_values16u(sw, cpu_state.npxs);
        //pclog("Double val = %08x.\n", t.i);
        writememq(easeg, cpu_state.eaaddr, save_reg);
        cpu_state.npxs = sw;
        x87_pop_ext();
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_64) : (x87_timings.fst_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_64) : (x87_concurrency.fst_64 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFSTPd_a32(uint32_t fetchdat)
{
    float_status_t status;
    uint16_t sw = cpu_state.npxs;
    uint16_t tmp;
    uint64_t save_reg = float64_default_nan;
    x87_td t;

    pclog("FSTPd_a32.\n");
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        t.d = ST(0);
        seteaq(t.i);
        if (cpu_state.abrt)
            return 1;
        x87_pop();
    } else {
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_CW_Underflow, 0);
            if (!is_IA_masked())
                return 1;
        } else {
            i387cw_to_softfloat_status_word(&status, cpu_state.npxc);
            save_reg = floatx80_to_float64(x87_read_reg_ext(0), &status);
            if (x87_checkexceptions(status.float_exception_flags, 1))
                return 1;
        }
        // store to the memory might generate an exception, in this case original FPU_SW must be kept
        swap_values16u(sw, cpu_state.npxs);
        //pclog("Double val = %08x.\n", t.i);
        writememq(easeg, cpu_state.eaaddr, save_reg);
        cpu_state.npxs = sw;
        x87_pop_ext();
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_64) : (x87_timings.fst_64 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_64) : (x87_concurrency.fst_64 * cpu_multi));
    return 0;
}
#endif

static int
opFLDs_a16(uint32_t fetchdat)
{
    float_status_t status;
    floatx80 result;
    float32 load_reg;
    x87_ts ts;
    unsigned unmasked;

    pclog("FLDs_a16.\n");
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        ts.i = geteal();
        if (cpu_state.abrt)
            return 1;
        x87_push((double) ts.s);
    } else {
        load_reg = readmeml(easeg, cpu_state.eaaddr);
        clear_C1();
        if (!IS_TAG_EMPTY(-1)) {
            x87_stack_overflow();
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        result = float32_to_floatx80(load_reg, &status);
        unmasked = x87_checkexceptions(status.float_exception_flags, 0);
        if (!(unmasked & FPU_CW_Invalid)) {
            x87_push_ext();
            x87_save_reg_ext(result, -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFLDs_a32(uint32_t fetchdat)
{
    float_status_t status;
    floatx80 result;
    float32 load_reg;
    x87_ts ts;
    unsigned unmasked;

    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        ts.i = geteal();
        if (cpu_state.abrt)
            return 1;
        x87_push((double) ts.s);
    } else {
        //pclog("FLDs_a32.\n");
        load_reg = readmeml(easeg, cpu_state.eaaddr);
        clear_C1();
        if (!IS_TAG_EMPTY(-1)) {
            x87_stack_overflow();
            return 1;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word());
        result = float32_to_floatx80(load_reg, &status);
        unmasked = x87_checkexceptions(status.float_exception_flags, 0);
        if (!(unmasked & FPU_CW_Invalid)) {
            x87_push_ext();
            x87_save_reg_ext(result, -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
    return 0;
}
#endif

static int
opFSTs_a16(uint32_t fetchdat)
{
    float_status_t status;
    uint16_t sw = cpu_state.npxs;
    uint16_t tmp;
    uint32_t save_reg;
    x87_ts ts;

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        ts.s = (float) ST(0);
        seteal(ts.i);
    } else {
        //pclog("FSTs_a16.\n");
        clear_C1();
        save_reg = 0xffc00000;
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_CW_Underflow, 0);
            if (!is_IA_masked())
                goto next_ins;
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            save_reg = floatx80_to_float32(x87_read_reg_ext(0), &status);
            if (x87_checkexceptions(status.float_exception_flags, 1))
                goto next_ins;
        }
        // store to the memory might generate an exception, in this case original FPU_SW must be kept
        tmp = sw;
        sw = cpu_state.npxs;
        cpu_state.npxs = tmp;
        seteal(save_reg);
        cpu_state.npxs = sw;
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
opFSTs_a32(uint32_t fetchdat)
{
    float_status_t status;
    uint16_t sw = cpu_state.npxs;
    uint16_t tmp;
    uint32_t save_reg;
    x87_ts ts;

    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        ts.s = (float) ST(0);
        seteal(ts.i);
    } else {
        //pclog("FSTs_a32.\n");
        clear_C1();
        save_reg = 0xffc00000;
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_CW_Underflow, 0);
            if (!is_IA_masked())
                goto next_ins;
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            save_reg = floatx80_to_float32(x87_read_reg_ext(0), &status);
            if (x87_checkexceptions(status.float_exception_flags, 1))
                goto next_ins;
        }
        // store to the memory might generate an exception, in this case original FPU_SW must be kept
        tmp = sw;
        sw = cpu_state.npxs;
        cpu_state.npxs = tmp;
        seteal(save_reg);
        cpu_state.npxs = sw;
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
    return cpu_state.abrt;
}
#endif

static int
opFSTPs_a16(uint32_t fetchdat)
{
    float_status_t status;
    uint16_t sw = cpu_state.npxs;
    uint16_t tmp;
    uint32_t save_reg;
    x87_ts ts;

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        ts.s = (float) ST(0);
        seteal(ts.i);
        if (cpu_state.abrt)
            return 1;
        x87_pop();
    } else {
        //pclog("FSTPs_a16.\n");
        clear_C1();
        save_reg = 0xffc00000;
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_CW_Underflow, 0);
            if (!is_IA_masked())
                goto next_ins;
        } else {
            i387cw_to_softfloat_status_word(&status, cpu_state.npxc);
            save_reg = floatx80_to_float32(x87_read_reg_ext(0), &status);
            if (x87_checkexceptions(status.float_exception_flags, 1))
                goto next_ins;
        }
        // store to the memory might generate an exception, in this case original FPU_SW must be kept
        tmp = sw;
        sw = cpu_state.npxs;
        cpu_state.npxs = tmp;
        seteal(save_reg);
        cpu_state.npxs = sw;
        if (cpu_state.abrt)
            return 1;
        x87_pop_ext();
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFSTPs_a32(uint32_t fetchdat)
{
    float_status_t status;
    uint16_t sw = cpu_state.npxs;
    uint16_t tmp;
    uint32_t save_reg;
    x87_ts ts;

    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        ts.s = (float) ST(0);
        seteal(ts.i);
        if (cpu_state.abrt)
            return 1;
        x87_pop();
    } else {
        //pclog("FSTPs_a32.\n");
        clear_C1();
        save_reg = 0xffc00000;
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_CW_Underflow, 0);
            if (!is_IA_masked())
                goto next_ins;
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            save_reg = floatx80_to_float32(x87_read_reg_ext(0), &status);
            if (x87_checkexceptions(status.float_exception_flags, 1))
                goto next_ins;
        }
        // store to the memory might generate an exception, in this case original FPU_SW must be kept
        tmp = sw;
        sw = cpu_state.npxs;
        cpu_state.npxs = tmp;
        seteal(save_reg);
        cpu_state.npxs = sw;
        if (cpu_state.abrt)
            return 1;
        x87_pop_ext();
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst_32) : (x87_timings.fst_32 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst_32) : (x87_concurrency.fst_32 * cpu_multi));
    return 0;
}
#endif
