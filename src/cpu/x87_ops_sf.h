static uint32_t
fpu_save_environment(void)
{
    int      tag;
    unsigned offset = 0;

    /* read all registers in stack order and update x87 tag word */
    for (int n = 0; n < 8; n++) {
        // update tag only if it is not empty
        if (!IS_TAG_EMPTY(n)) {
            tag = FPU_tagof(FPU_read_regi(n));
            FPU_settagi(tag, n);
        }
    }

    fpu_state.swd = (fpu_state.swd & ~(7 << 11)) | ((fpu_state.tos & 7) << 11);

    switch ((cr0 & 1) | (cpu_state.op32 & 0x100)) {
        case 0x000:
            { /*16-bit real mode*/
                uint16_t tmp;
                uint32_t fp_ip;
                uint32_t fp_dp;

                fp_ip = ((uint32_t) (fpu_state.fcs << 4)) | fpu_state.fip;
                fp_dp = ((uint32_t) (fpu_state.fds << 4)) | fpu_state.fdp;

                tmp = i387_get_control_word();
                writememw(easeg, cpu_state.eaaddr + 0x00, tmp);
                tmp = i387_get_status_word();
                writememw(easeg, cpu_state.eaaddr + 0x02, tmp);
                tmp = fpu_state.tag;
                writememw(easeg, cpu_state.eaaddr + 0x04, tmp);
                tmp = fp_ip & 0xffff;
                writememw(easeg, cpu_state.eaaddr + 0x06, tmp);
                tmp = (uint16_t) ((fp_ip & 0xf0000) >> 4) | fpu_state.foo;
                writememw(easeg, cpu_state.eaaddr + 0x08, tmp);
                tmp = fp_dp & 0xffff;
                writememw(easeg, cpu_state.eaaddr + 0x0a, tmp);
                tmp = (uint16_t) ((fp_dp & 0xf0000) >> 4);
                writememw(easeg, cpu_state.eaaddr + 0x0c, tmp);
                offset = 0x0e;
            }
            break;
        case 0x001:
            { /*16-bit protected mode*/
                uint16_t tmp;
                tmp = i387_get_control_word();
                writememw(easeg, cpu_state.eaaddr + 0x00, tmp);
                tmp = i387_get_status_word();
                writememw(easeg, cpu_state.eaaddr + 0x02, tmp);
                tmp = fpu_state.tag;
                writememw(easeg, cpu_state.eaaddr + 0x04, tmp);
                tmp = (uint16_t) (fpu_state.fip) & 0xffff;
                writememw(easeg, cpu_state.eaaddr + 0x06, tmp);
                tmp = fpu_state.fcs;
                writememw(easeg, cpu_state.eaaddr + 0x08, tmp);
                tmp = (uint16_t) (fpu_state.fdp) & 0xffff;
                writememw(easeg, cpu_state.eaaddr + 0x0a, tmp);
                tmp = fpu_state.fds;
                writememw(easeg, cpu_state.eaaddr + 0x0c, tmp);
                offset = 0x0e;
            }
            break;
        case 0x100:
            { /*32-bit real mode*/
                uint32_t tmp;
                uint32_t fp_ip;
                uint32_t fp_dp;

                fp_ip = ((uint32_t) (fpu_state.fcs << 4)) | fpu_state.fip;
                fp_dp = ((uint32_t) (fpu_state.fds << 4)) | fpu_state.fdp;

                tmp = 0xffff0000 | i387_get_control_word();
                writememl(easeg, cpu_state.eaaddr + 0x00, tmp);
                tmp = 0xffff0000 | i387_get_status_word();
                writememl(easeg, cpu_state.eaaddr + 0x04, tmp);
                tmp = 0xffff0000 | fpu_state.tag;
                writememl(easeg, cpu_state.eaaddr + 0x08, tmp);
                tmp = 0xffff0000 | (fp_ip & 0xffff);
                writememl(easeg, cpu_state.eaaddr + 0x0c, tmp);
                tmp = ((fp_ip & 0xffff0000) >> 4) | fpu_state.foo;
                writememl(easeg, cpu_state.eaaddr + 0x10, tmp);
                tmp = 0xffff0000 | (fp_dp & 0xffff);
                writememl(easeg, cpu_state.eaaddr + 0x14, tmp);
                tmp = (fp_dp & 0xffff0000) >> 4;
                writememl(easeg, cpu_state.eaaddr + 0x18, tmp);
                offset = 0x1c;
            }
            break;
        case 0x101:
            { /*32-bit protected mode*/
                uint32_t tmp;
                tmp = 0xffff0000 | i387_get_control_word();
                writememl(easeg, cpu_state.eaaddr + 0x00, tmp);
                tmp = 0xffff0000 | i387_get_status_word();
                writememl(easeg, cpu_state.eaaddr + 0x04, tmp);
                tmp = 0xffff0000 | fpu_state.tag;
                writememl(easeg, cpu_state.eaaddr + 0x08, tmp);
                tmp = (uint32_t) (fpu_state.fip);
                writememl(easeg, cpu_state.eaaddr + 0x0c, tmp);
                tmp = fpu_state.fcs | (((uint32_t) (fpu_state.foo)) << 16);
                writememl(easeg, cpu_state.eaaddr + 0x10, tmp);
                tmp = (uint32_t) (fpu_state.fdp);
                writememl(easeg, cpu_state.eaaddr + 0x14, tmp);
                tmp = 0xffff0000 | fpu_state.fds;
                writememl(easeg, cpu_state.eaaddr + 0x18, tmp);
                offset = 0x1c;
            }
            break;
    }

    return (cpu_state.eaaddr + offset);
}

static uint32_t
fpu_load_environment(void)
{
    unsigned offset = 0;

    switch ((cr0 & 1) | (cpu_state.op32 & 0x100)) {
        case 0x000:
            { /*16-bit real mode*/
                uint16_t tmp;
                uint32_t fp_ip;
                uint32_t fp_dp;

                tmp           = readmemw(easeg, cpu_state.eaaddr + 0x0c);
                fp_dp         = (tmp & 0xf000) << 4;
                tmp           = readmemw(easeg, cpu_state.eaaddr + 0x0a);
                fpu_state.fdp = fp_dp | tmp;
                fpu_state.fds = 0;
                tmp           = readmemw(easeg, cpu_state.eaaddr + 0x08);
                fp_ip         = (tmp & 0xf000) << 4;
                tmp           = readmemw(easeg, cpu_state.eaaddr + 0x06);
                fpu_state.fip = fp_ip | tmp;
                fpu_state.fcs = 0;
                tmp           = readmemw(easeg, cpu_state.eaaddr + 0x04);
                fpu_state.tag = tmp;
                tmp           = readmemw(easeg, cpu_state.eaaddr + 0x02);
                fpu_state.swd = tmp;
                fpu_state.tos = (tmp >> 11) & 7;
                tmp           = readmemw(easeg, cpu_state.eaaddr + 0x00);
                fpu_state.cwd = tmp;
                offset        = 0x0e;
            }
            break;
        case 0x001:
            { /*16-bit protected mode*/
                uint16_t tmp;
                tmp           = readmemw(easeg, cpu_state.eaaddr + 0x0c);
                fpu_state.fds = tmp;
                tmp           = readmemw(easeg, cpu_state.eaaddr + 0x0a);
                fpu_state.fdp = tmp;
                tmp           = readmemw(easeg, cpu_state.eaaddr + 0x08);
                fpu_state.fcs = tmp;
                tmp           = readmemw(easeg, cpu_state.eaaddr + 0x06);
                fpu_state.fip = tmp;
                tmp           = readmemw(easeg, cpu_state.eaaddr + 0x04);
                fpu_state.tag = tmp;
                tmp           = readmemw(easeg, cpu_state.eaaddr + 0x02);
                fpu_state.swd = tmp;
                fpu_state.tos = (tmp >> 11) & 7;
                tmp           = readmemw(easeg, cpu_state.eaaddr + 0x00);
                fpu_state.cwd = tmp;
                offset        = 0x0e;
            }
            break;
        case 0x100:
            { /*32-bit real mode*/
                uint32_t tmp;
                uint32_t fp_ip;
                uint32_t fp_dp;

                tmp   = readmeml(easeg, cpu_state.eaaddr + 0x18);
                fp_dp = (tmp & 0x0ffff000) << 4;
                tmp   = readmeml(easeg, cpu_state.eaaddr + 0x14);
                fp_dp |= (tmp & 0xffff);
                fpu_state.fdp = fp_dp;
                fpu_state.fds = 0;
                tmp           = readmeml(easeg, cpu_state.eaaddr + 0x10);
                fpu_state.foo = tmp & 0x07ff;
                fp_ip         = (tmp & 0x0ffff000) << 4;
                tmp           = readmeml(easeg, cpu_state.eaaddr + 0x0c);
                fp_ip |= (tmp & 0xffff);
                fpu_state.fip = fp_ip;
                fpu_state.fcs = 0;
                tmp           = readmeml(easeg, cpu_state.eaaddr + 0x08);
                fpu_state.tag = tmp & 0xffff;
                tmp           = readmeml(easeg, cpu_state.eaaddr + 0x04);
                fpu_state.swd = tmp & 0xffff;
                fpu_state.tos = (tmp >> 11) & 7;
                tmp           = readmeml(easeg, cpu_state.eaaddr + 0x00);
                fpu_state.cwd = tmp & 0xffff;
                offset        = 0x1c;
            }
            break;
        case 0x101:
            { /*32-bit protected mode*/
                uint32_t tmp;
                tmp           = readmeml(easeg, cpu_state.eaaddr + 0x18);
                fpu_state.fds = tmp & 0xffff;
                tmp           = readmeml(easeg, cpu_state.eaaddr + 0x14);
                fpu_state.fdp = tmp;
                tmp           = readmeml(easeg, cpu_state.eaaddr + 0x10);
                fpu_state.fcs = tmp & 0xffff;
                fpu_state.foo = (tmp >> 16) & 0x07ff;
                tmp           = readmeml(easeg, cpu_state.eaaddr + 0x0c);
                fpu_state.fip = tmp;
                tmp           = readmeml(easeg, cpu_state.eaaddr + 0x08);
                fpu_state.tag = tmp & 0xffff;
                tmp           = readmeml(easeg, cpu_state.eaaddr + 0x04);
                fpu_state.swd = tmp & 0xffff;
                fpu_state.tos = (tmp >> 11) & 7;
                tmp           = readmeml(easeg, cpu_state.eaaddr + 0x00);
                fpu_state.cwd = tmp & 0xffff;
                offset        = 0x1c;
            }
            break;
    }

    /* always set bit 6 as '1 */
    fpu_state.cwd = (fpu_state.cwd & ~FPU_CW_Reserved_Bits) | 0x0040;

    /* check for unmasked exceptions */
    if (fpu_state.swd & ~fpu_state.cwd & FPU_CW_Exceptions_Mask) {
        /* set the B and ES bits in the status-word */
        fpu_state.swd |= (FPU_SW_Summary | FPU_SW_Backward);
    } else {
        /* clear the B and ES bits in the status-word */
        fpu_state.swd &= ~(FPU_SW_Summary | FPU_SW_Backward);
    }

    return (cpu_state.eaaddr + offset);
}

static int
sf_FLDCW_a16(UNUSED(uint32_t fetchdat))
{
    uint16_t tempw;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    tempw = geteaw();
    if (cpu_state.abrt)
        return 1;
    fpu_state.cwd = (tempw & ~FPU_CW_Reserved_Bits) | 0x0040; // bit 6 is reserved as '1
    /* check for unmasked exceptions */
    if (fpu_state.swd & (~fpu_state.cwd & FPU_CW_Exceptions_Mask)) {
        /* set the B and ES bits in the status-word */
        fpu_state.swd |= (FPU_SW_Summary | FPU_SW_Backward);
    } else {
        /* clear the B and ES bits in the status-word */
        fpu_state.swd &= ~(FPU_SW_Summary | FPU_SW_Backward);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fldcw) : (x87_timings.fldcw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fldcw) : (x87_concurrency.fldcw * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
sf_FLDCW_a32(uint32_t fetchdat)
{
    uint16_t tempw;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    tempw = geteaw();
    if (cpu_state.abrt)
        return 1;
    fpu_state.cwd = (tempw & ~FPU_CW_Reserved_Bits) | 0x0040; // bit 6 is reserved as '1
    /* check for unmasked exceptions */
    if (fpu_state.swd & (~fpu_state.cwd & FPU_CW_Exceptions_Mask)) {
        /* set the B and ES bits in the status-word */
        fpu_state.swd |= (FPU_SW_Summary | FPU_SW_Backward);
    } else {
        /* clear the B and ES bits in the status-word */
        fpu_state.swd &= ~(FPU_SW_Summary | FPU_SW_Backward);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fldcw) : (x87_timings.fldcw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fldcw) : (x87_concurrency.fldcw * cpu_multi));
    return 0;
}
#endif

static int
sf_FNSTCW_a16(UNUSED(uint32_t fetchdat))
{
    uint16_t cwd = i387_get_control_word();

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    seteaw(cwd);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstcw_sw) : (x87_timings.fstcw_sw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstenv) : (x87_concurrency.fstenv * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
sf_FNSTCW_a32(uint32_t fetchdat)
{
    uint16_t cwd = i387_get_control_word();

    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    seteaw(cwd);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstcw_sw) : (x87_timings.fstcw_sw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstcw_sw) : (x87_concurrency.fstcw_sw * cpu_multi));
    return cpu_state.abrt;
}
#endif

static int
sf_FNSTSW_a16(UNUSED(uint32_t fetchdat))
{
    uint16_t swd = i387_get_status_word();

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    seteaw(swd);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstcw_sw) : (x87_timings.fstcw_sw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstcw_sw) : (x87_concurrency.fstcw_sw * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
sf_FNSTSW_a32(uint32_t fetchdat)
{
    uint16_t swd = i387_get_status_word();

    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    seteaw(swd);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstcw_sw) : (x87_timings.fstcw_sw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstcw_sw) : (x87_concurrency.fstcw_sw * cpu_multi));
    return cpu_state.abrt;
}
#endif

#ifdef FPU_8087
static int
sf_FI(uint32_t fetchdat)
{
    FP_ENTER();
    cpu_state.pc++;
    fpu_state.cwd &= ~FPU_SW_Summary;
    if (rmdat == 0xe1)
        fpu_state.cwd |= FPU_SW_Summary;
    wait(3, 0);
    return 0;
}
#else
static int
sf_FNSTSW_AX(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    AX = i387_get_status_word();
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstcw_sw) : (x87_timings.fstcw_sw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstcw_sw) : (x87_concurrency.fstcw_sw * cpu_multi));
    return 0;
}
#endif

static int
sf_FRSTOR_a16(UNUSED(uint32_t fetchdat))
{
    floatx80 tmp;
    int      offset;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    offset = fpu_load_environment();
    for (int n = 0; n < 8; n++) {
        tmp.signif  = readmemq(easeg, offset + (n * 10));
        tmp.signExp = readmemw(easeg, offset + (n * 10) + 8);
        FPU_save_regi_tag(tmp, IS_TAG_EMPTY(n) ? X87_TAG_EMPTY : FPU_tagof(tmp), n);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.frstor) : (x87_timings.frstor * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.frstor) : (x87_concurrency.frstor * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
sf_FRSTOR_a32(uint32_t fetchdat)
{
    floatx80 tmp;
    int      offset;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    offset = fpu_load_environment();
    for (int n = 0; n < 8; n++) {
        tmp.signif   = readmemq(easeg, offset + (n * 10));
        tmp.signExp  = readmemw(easeg, offset + (n * 10) + 8);
        FPU_save_regi_tag(tmp, IS_TAG_EMPTY(n) ? X87_TAG_EMPTY : FPU_tagof(tmp), n);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.frstor) : (x87_timings.frstor * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.frstor) : (x87_concurrency.frstor * cpu_multi));
    return cpu_state.abrt;
}
#endif

static int
sf_FNSAVE_a16(UNUSED(uint32_t fetchdat))
{
    floatx80 stn;
    int      offset;

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    offset = fpu_save_environment();
    /* save all registers in stack order. */
    for (int m = 0; m < 8; m++) {
        stn = FPU_read_regi(m);
        writememq(easeg, offset + (m * 10), stn.signif);
        writememw(easeg, offset + (m * 10) + 8, stn.signExp);
    }

#ifdef FPU_8087
    fpu_state.cwd = 0x3FF;
    fpu_state.swd  &= 0x4700;
#else
    fpu_state.cwd = 0x37F;
    fpu_state.swd   = 0;
#endif
    fpu_state.tos   = 0;
    fpu_state.tag   = 0xffff;
    cpu_state.ismmx = 0;
    fpu_state.foo   = 0;
    fpu_state.fds   = 0;
    fpu_state.fdp   = 0;
    fpu_state.fcs   = 0;
    fpu_state.fip   = 0;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fsave) : (x87_timings.fsave * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fsave) : (x87_concurrency.fsave * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
sf_FNSAVE_a32(uint32_t fetchdat)
{
    floatx80 stn;
    int      offset;

    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    offset = fpu_save_environment();
    /* save all registers in stack order. */
    for (int m = 0; m < 8; m++) {
        stn = FPU_read_regi(m);
        writememq(easeg, offset + (m * 10), stn.signif);
        writememw(easeg, offset + (m * 10) + 8, stn.signExp);
    }

#    ifdef FPU_8087
    fpu_state.cwd = 0x3FF;
#    else
    fpu_state.cwd = 0x37F;
#    endif
    fpu_state.swd   = 0;
    fpu_state.tos   = 0;
    fpu_state.tag   = 0xffff;
    cpu_state.ismmx = 0;
    fpu_state.foo   = 0;
    fpu_state.fds   = 0;
    fpu_state.fdp   = 0;
    fpu_state.fcs   = 0;
    fpu_state.fip   = 0;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fsave) : (x87_timings.fsave * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fsave) : (x87_concurrency.fsave * cpu_multi));
    return cpu_state.abrt;
}
#endif

static int
sf_FNCLEX(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
    fpu_state.swd &= ~(FPU_SW_Backward | FPU_SW_Summary | FPU_SW_Stack_Fault | FPU_SW_Precision | FPU_SW_Underflow | FPU_SW_Overflow | FPU_SW_Zero_Div | FPU_SW_Denormal_Op | FPU_SW_Invalid);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fnop) : (x87_timings.fnop * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fnop) : (x87_concurrency.fnop * cpu_multi));
    return 0;
}

static int
sf_FNINIT(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    cpu_state.pc++;
#ifdef FPU_8087
    fpu_state.cwd = 0x3FF;
    fpu_state.swd  &= 0x4700;
#else
    fpu_state.cwd = 0x37F;
    fpu_state.swd   = 0;
#endif
    fpu_state.tos   = 0;
    fpu_state.tag   = 0xffff;
    fpu_state.foo   = 0;
    fpu_state.fds   = 0;
    fpu_state.fdp   = 0;
    fpu_state.fcs   = 0;
    fpu_state.fip   = 0;
    cpu_state.ismmx = 0;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.finit) : (x87_timings.finit * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.finit) : (x87_concurrency.finit * cpu_multi));
    CPU_BLOCK_END();
    return 0;
}

static int
sf_FLDENV_a16(UNUSED(uint32_t fetchdat))
{
    int tag;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    fpu_load_environment();
    /* read all registers in stack order and update x87 tag word */
    for (int n = 0; n < 8; n++) {
        // update tag only if it is not empty
        if (!IS_TAG_EMPTY(n)) {
            tag = FPU_tagof(FPU_read_regi(n));
            FPU_settagi(tag, n);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fldenv) : (x87_timings.fldenv * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fldenv) : (x87_concurrency.fldenv * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
sf_FLDENV_a32(uint32_t fetchdat)
{
    int tag;

    FP_ENTER();
    FPU_check_pending_exceptions();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    fpu_load_environment();
    /* read all registers in stack order and update x87 tag word */
    for (int n = 0; n < 8; n++) {
        // update tag only if it is not empty
        if (!IS_TAG_EMPTY(n)) {
            tag = FPU_tagof(FPU_read_regi(n));
            FPU_settagi(tag, n);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fldenv) : (x87_timings.fldenv * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fldenv) : (x87_concurrency.fldenv * cpu_multi));
    return cpu_state.abrt;
}
#endif

static int
sf_FNSTENV_a16(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    fpu_save_environment();
    /* mask all floating point exceptions */
    fpu_state.cwd |= FPU_CW_Exceptions_Mask;
    /* clear the B and ES bits in the status word */
    fpu_state.swd &= ~(FPU_SW_Backward | FPU_SW_Summary);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstenv) : (x87_timings.fstenv * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstenv) : (x87_concurrency.fstenv * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
sf_FNSTENV_a32(uint32_t fetchdat)
{
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    fpu_save_environment();
    /* mask all floating point exceptions */
    fpu_state.cwd |= FPU_CW_Exceptions_Mask;
    /* clear the B and ES bits in the status word */
    fpu_state.swd &= ~(FPU_SW_Backward | FPU_SW_Summary);
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstenv) : (x87_timings.fstenv * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstenv) : (x87_concurrency.fstenv * cpu_multi));
    return cpu_state.abrt;
}
#endif

static int
sf_FNOP(UNUSED(uint32_t fetchdat))
{
    FP_ENTER();
    FPU_check_pending_exceptions();
    cpu_state.pc++;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fnop) : (x87_timings.fnop * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fnop) : (x87_concurrency.fnop * cpu_multi));
    return 0;
}
