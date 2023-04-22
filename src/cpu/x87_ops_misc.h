static int
fpu_save_environment(void)
{
    int tag;
    unsigned offset;

    /* read all registers in stack order and update x87 tag word */
    for (int n = 0; n < 8; n++) {
        // update tag only if it is not empty
        if (!IS_TAG_EMPTY(n)) {
            tag = SF_FPU_tagof(x87_read_reg_ext(n));
            x87_set_tag_ext(tag, n, 0);
        }
    }

    switch ((cr0 & 1) | (cpu_state.op32 & 0x100)) {
        case 0x000: { /*16-bit real mode*/
            uint16_t tmp;
            uint32_t fp_ip, fp_dp;

            fp_ip = (x87_pc_seg << 4) | x87_pc_off;
            fp_dp = (x87_op_seg << 4) | x87_op_off;

            tmp = cpu_state.npxc;
            writememw(easeg, cpu_state.eaaddr + 0x00, tmp);
            tmp = ((cpu_state.npxs & ~FPU_SW_Top & 0xFFFF) | ((cpu_state.TOP << 11) & FPU_SW_Top));
            writememw(easeg, cpu_state.eaaddr + 0x02, tmp);
            tmp = fpu_state.tag;
            writememw(easeg, cpu_state.eaaddr + 0x04, tmp);
            tmp = fp_ip & 0xffff;
            writememw(easeg, cpu_state.eaaddr + 0x06, tmp);
            tmp = (uint16_t)((fp_ip & 0xf0000) >> 4) | x87_opcode;
            writememw(easeg, cpu_state.eaaddr + 0x08, tmp);
            tmp = fp_dp & 0xffff;
            writememw(easeg, cpu_state.eaaddr + 0x0a, tmp);
            tmp = (uint16_t)((fp_dp & 0xf0000) >> 4);
            writememw(easeg, cpu_state.eaaddr + 0x0c, tmp);
            offset = 0x0e;
        }
        break;
        case 0x001: {/*16-bit protected mode*/
            uint16_t tmp;
            tmp = cpu_state.npxc;
            writememw(easeg, cpu_state.eaaddr + 0x00, tmp);
            tmp = ((cpu_state.npxs & ~FPU_SW_Top & 0xFFFF) | ((cpu_state.TOP << 11) & FPU_SW_Top));
            writememw(easeg, cpu_state.eaaddr + 0x02, tmp);
            tmp = fpu_state.tag;
            writememw(easeg, cpu_state.eaaddr + 0x04, tmp);
            tmp = (uint16_t)(x87_pc_off) & 0xffff;
            writememw(easeg, cpu_state.eaaddr + 0x06, tmp);
            tmp = x87_pc_seg;
            writememw(easeg, cpu_state.eaaddr + 0x08, tmp);
            tmp = (uint16_t)(x87_op_off) & 0xffff;
            writememw(easeg, cpu_state.eaaddr + 0x0a, tmp);
            tmp = x87_op_seg;
            writememw(easeg, cpu_state.eaaddr + 0x0c, tmp);
            offset = 0x0e;
        }
        break;
        case 0x100: { /*32-bit real mode*/
            uint32_t tmp, fp_ip, fp_dp;

            fp_ip = (x87_pc_seg << 4) | x87_pc_off;
            fp_dp = (x87_op_seg << 4) | x87_op_off;

            tmp = 0xffff0000 | cpu_state.npxc;
            writememl(easeg, cpu_state.eaaddr + 0x00, tmp);
            tmp = 0xffff0000 | ((cpu_state.npxs & ~FPU_SW_Top & 0xFFFF) | ((cpu_state.TOP << 11) & FPU_SW_Top));
            writememl(easeg, cpu_state.eaaddr + 0x04, tmp);
            tmp = 0xffff0000 | fpu_state.tag;
            writememl(easeg, cpu_state.eaaddr + 0x08, tmp);
            tmp = 0xffff0000 | (fp_ip & 0xffff);
            writememl(easeg, cpu_state.eaaddr + 0x0c, tmp);
            tmp = ((fp_ip & 0xffff0000) >> 4) | x87_opcode;
            writememl(easeg, cpu_state.eaaddr + 0x10, tmp);
            tmp = 0xffff0000 | (fp_dp & 0xffff);
            writememl(easeg, cpu_state.eaaddr + 0x14, tmp);
            tmp = (fp_dp & 0xffff0000) >> 4;
            writememl(easeg, cpu_state.eaaddr + 0x18, tmp);
            offset = 0x1c;
        }
        break;
        case 0x101: { /*32-bit protected mode*/
            uint32_t tmp;
            tmp = 0xffff0000 | cpu_state.npxc;
            writememl(easeg, cpu_state.eaaddr + 0x00, tmp);
            tmp = 0xffff0000 | ((cpu_state.npxs & ~FPU_SW_Top & 0xFFFF) | ((cpu_state.TOP << 11) & FPU_SW_Top));
            writememl(easeg, cpu_state.eaaddr + 0x04, tmp);
            tmp = 0xffff0000 | fpu_state.tag;
            writememl(easeg, cpu_state.eaaddr + 0x08, tmp);
            tmp = (uint32_t)(x87_pc_off);
            writememl(easeg, cpu_state.eaaddr + 0x0c, tmp);
            tmp = x87_pc_seg | (((uint32_t)(x87_opcode)) << 16);
            writememl(easeg, cpu_state.eaaddr + 0x10, tmp);
            tmp = (uint32_t)(x87_op_off);
            writememl(easeg, cpu_state.eaaddr + 0x14, tmp);
            tmp = 0xffff0000 | x87_op_seg;
            writememl(easeg, cpu_state.eaaddr + 0x18, tmp);
            offset = 0x1c;
        }
        break;
    }

    return (cpu_state.eaaddr + offset);
}


static int
fpu_load_environment(void)
{
    unsigned offset;

    switch ((cr0 & 1) | (cpu_state.op32 & 0x100)) {
        case 0x000: { /*16-bit real mode*/
            uint16_t tmp;
            uint32_t fp_ip, fp_dp;
            tmp = readmemw(easeg, cpu_state.eaaddr + 0x0c);
            fp_dp = (tmp & 0xf000) << 4;
            tmp = readmemw(easeg, cpu_state.eaaddr + 0x0a);
            x87_op_off = fp_dp | tmp;
            x87_op_seg = 0;
            tmp = readmemw(easeg, cpu_state.eaaddr + 0x08);
            fp_ip = (tmp & 0xf000) << 4;
            tmp = readmemw(easeg, cpu_state.eaaddr + 0x06);
            x87_pc_off = fp_ip | tmp;
            x87_pc_seg = 0;
            tmp = readmemw(easeg, cpu_state.eaaddr + 0x04);
            fpu_state.tag = tmp;
            tmp = readmemw(easeg, cpu_state.eaaddr + 0x02);
            cpu_state.npxs = tmp;
            cpu_state.TOP = (tmp >> 11) & 7;
            tmp = readmemw(easeg, cpu_state.eaaddr + 0x00);
            cpu_state.npxc = tmp;
            offset = 0x0e;
        }
        break;
        case 0x001: {/*16-bit protected mode*/
            uint16_t tmp;
            tmp = readmemw(easeg, cpu_state.eaaddr + 0x0c);
            x87_op_seg = tmp;
            tmp = readmemw(easeg, cpu_state.eaaddr + 0x0a);
            x87_op_off = tmp;
            tmp = readmemw(easeg, cpu_state.eaaddr + 0x08);
            x87_pc_seg = tmp;
            tmp = readmemw(easeg, cpu_state.eaaddr + 0x06);
            x87_pc_off = tmp;
            tmp = readmemw(easeg, cpu_state.eaaddr + 0x04);
            fpu_state.tag = tmp;
            tmp = readmemw(easeg, cpu_state.eaaddr + 0x02);
            cpu_state.npxs = tmp;
            cpu_state.TOP = (tmp >> 11) & 7;
            tmp = readmemw(easeg, cpu_state.eaaddr + 0x00);
            cpu_state.npxc = tmp;
            offset = 0x0e;
        }
        break;
        case 0x100: { /*32-bit real mode*/
            uint32_t tmp, fp_ip, fp_dp;
            tmp = readmeml(easeg, cpu_state.eaaddr + 0x18);
            fp_dp = (tmp & 0x0ffff000) << 4;
            tmp = readmeml(easeg, cpu_state.eaaddr + 0x14);
            fp_dp |= (tmp & 0xffff);
            x87_op_off = fp_dp;
            x87_op_seg = 0;
            tmp = readmeml(easeg, cpu_state.eaaddr + 0x10);
            x87_opcode = tmp & 0x07ff;
            fp_ip = (tmp & 0x0ffff000) << 4;
            tmp = readmeml(easeg, cpu_state.eaaddr + 0x0c);
            fp_ip |= (tmp & 0xffff);
            x87_pc_off = fp_ip;
            x87_pc_seg = 0;
            tmp = readmeml(easeg, cpu_state.eaaddr + 0x08);
            fpu_state.tag = tmp & 0xffff;
            tmp = readmeml(easeg, cpu_state.eaaddr + 0x04);
            cpu_state.npxs = tmp & 0xffff;
            cpu_state.TOP = (tmp >> 11) & 7;
            tmp = readmeml(easeg, cpu_state.eaaddr + 0x00);
            cpu_state.npxc = tmp & 0xffff;
            offset = 0x1c;
        }
        break;
        case 0x101: { /*32-bit protected mode*/
            uint32_t tmp;
            tmp = readmeml(easeg, cpu_state.eaaddr + 0x18);
            x87_op_seg = tmp & 0xffff;
            tmp = readmeml(easeg, cpu_state.eaaddr + 0x14);
            x87_op_off = tmp;
            tmp = readmeml(easeg, cpu_state.eaaddr + 0x10);
            x87_pc_seg = tmp & 0xffff;
            x87_opcode = (tmp >> 16) & 0x07ff;
            tmp = readmeml(easeg, cpu_state.eaaddr + 0x0c);
            x87_pc_off = tmp;
            tmp = readmeml(easeg, cpu_state.eaaddr + 0x08);
            fpu_state.tag = tmp & 0xffff;
            tmp = readmeml(easeg, cpu_state.eaaddr + 0x04);
            cpu_state.npxs = tmp & 0xffff;
            cpu_state.TOP = (tmp >> 11) & 7;
            tmp = readmeml(easeg, cpu_state.eaaddr + 0x00);
            cpu_state.npxc = tmp & 0xffff;
            offset = 0x1c;
        }
        break;
    }

    /* always set bit 6 as '1 */
    cpu_state.npxc = (cpu_state.npxc & ~FPU_CW_Reserved_Bits) | 0x0040;

    /* check for unmasked exceptions */
    if (cpu_state.npxs & ~cpu_state.npxc & FPU_CW_Exceptions_Mask) {
        /* set the B and ES bits in the status-word */
        cpu_state.npxs |= (FPU_SW_Summary | FPU_SW_Backward);
    } else {
        /* clear the B and ES bits in the status-word */
        cpu_state.npxs &= ~(FPU_SW_Summary | FPU_SW_Backward);
    }

    return (cpu_state.eaaddr + offset);
}

#ifdef FPU_8087
static int
opFI(uint32_t fetchdat)
{
    FP_ENTER();
    cpu_state.pc++;
    cpu_state.npxc &= ~0x80;
    if (rmdat == 0xe1)
        cpu_state.npxc |= 0x80;
    wait(3, 0);
    return 0;
}
#else
static int
opFSTSW_AX(uint32_t fetchdat)
{
    //pclog("FSTSW_AX.\n");
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec)
        AX = cpu_state.npxs;
    else
        AX = ((cpu_state.npxs & ~FPU_SW_Top & 0xFFFF) | ((cpu_state.TOP << 11) & FPU_SW_Top));
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstcw_sw) : (x87_timings.fstcw_sw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstcw_sw) : (x87_concurrency.fstcw_sw * cpu_multi));
    return 0;
}
#endif

static int
opFNOP(uint32_t fetchdat)
{
    //pclog("FNOP.\n");
    FP_ENTER();
    cpu_state.pc++;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fnop) : (x87_timings.fnop * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fnop) : (x87_concurrency.fnop * cpu_multi));
    return 0;
}

static int
opFCLEX(uint32_t fetchdat)
{
    pclog("FCLEX.\n");
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec)
        cpu_state.npxs &= 0xff00;
    else
        cpu_state.npxs &= ~(FPU_SW_Backward | FPU_SW_Summary | FPU_SW_Stack_Fault | FPU_SW_Precision |
                   FPU_SW_Underflow | FPU_SW_Overflow | FPU_SW_Zero_Div | FPU_SW_Denormal_Op |
                   FPU_SW_Invalid);

    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fnop) : (x87_timings.fnop * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fnop) : (x87_concurrency.fnop * cpu_multi));
    return 0;
}

static int
opFINIT(uint32_t fetchdat)
{
    //pclog("FINIT.\n");
    uint64_t *p;
    FP_ENTER();
    cpu_state.pc++;
#ifdef FPU_8087
    cpu_state.npxc = 0x3FF;
#else
    cpu_state.npxc = 0x37F;
#endif
    if (cpu_use_dynarec)
        codegen_set_rounding_mode(X87_ROUNDING_NEAREST);

    cpu_state.npxs = 0;
    cpu_state.TOP   = 0;

    if (cpu_use_dynarec) {
        p              = (uint64_t *) cpu_state.tag;
#ifdef USE_NEW_DYNAREC
        *p = 0;
#else
        *p                                            = 0x0303030303030303ll;
#endif
    } else {
        fpu_state.tag = 0xffff;
        x87_opcode = 0;
        x87_op_seg = 0;
        x87_op_off = 0;
        x87_pc_seg = 0;
        x87_pc_off = 0;
    }

    cpu_state.ismmx = 0;
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.finit) : (x87_timings.finit * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.finit) : (x87_concurrency.finit * cpu_multi));
    CPU_BLOCK_END();
    return 0;
}

static int
opFFREE(uint32_t fetchdat)
{
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
#ifdef USE_NEW_DYNAREC
        cpu_state.tag[(cpu_state.TOP + fetchdat) & 7] = TAG_EMPTY;
#else
        cpu_state.tag[(cpu_state.TOP + fetchdat) & 7] = 3;
#endif
    } else {
        //pclog("FFREE.\n");
        clear_C1();
        x87_set_tag_ext(X87_TAG_EMPTY, fetchdat & 7, 0);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.ffree) : (x87_timings.ffree * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.ffree) : (x87_concurrency.ffree * cpu_multi));
    return 0;
}

static int
opFFREEP(uint32_t fetchdat)
{
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
#ifdef USE_NEW_DYNAREC
        cpu_state.tag[(cpu_state.TOP + fetchdat) & 7] = TAG_EMPTY;
#else
        cpu_state.tag[(cpu_state.TOP + fetchdat) & 7] = 3;
#endif
        if (cpu_state.abrt)
            return 1;
        x87_pop();
    } else {
        //pclog("FFREEP.\n");
        clear_C1();
        x87_set_tag_ext(X87_TAG_EMPTY, fetchdat & 7, 0);
        if (cpu_state.abrt)
            return 1;
        x87_pop_ext();
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.ffree) : (x87_timings.ffree * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.ffree) : (x87_concurrency.ffree * cpu_multi));
    return 0;
}

static int
opFST(uint32_t fetchdat)
{
    floatx80 st0_reg;

    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(fetchdat & 7)                              = ST(0);
        cpu_state.tag[(cpu_state.TOP + fetchdat) & 7] = cpu_state.tag[cpu_state.TOP & 7];
    } else {
        //pclog("FST_sti.\n");
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_stack_underflow(fetchdat & 7, 0);
        } else {
            st0_reg = x87_read_reg_ext(0);
            x87_save_reg_ext(st0_reg, 0, fetchdat & 7, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst) : (x87_timings.fst * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst) : (x87_concurrency.fst * cpu_multi));
    return 0;
}

static int
opFSTP(uint32_t fetchdat)
{
    floatx80 st0_reg;

    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(fetchdat & 7)                              = ST(0);
        cpu_state.tag[(cpu_state.TOP + fetchdat) & 7] = cpu_state.tag[cpu_state.TOP & 7];
        x87_pop();
    } else {
        //pclog("FSTP.\n");
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_stack_underflow(fetchdat & 7, 1);
        } else {
            st0_reg = x87_read_reg_ext(0);
            x87_save_reg_ext(st0_reg, 0, fetchdat & 7, 0);
            x87_pop_ext();
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fst) : (x87_timings.fst * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fst) : (x87_concurrency.fst * cpu_multi));
    return 0;
}

static int
FSTOR(void)
{
    uint64_t *p;
    FP_ENTER();
    switch ((cr0 & 1) | (cpu_state.op32 & 0x100)) {
        case 0x000: /*16-bit real mode*/
        case 0x001: /*16-bit protected mode*/
            cpu_state.npxc = readmemw(easeg, cpu_state.eaaddr);
            codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
            cpu_state.npxs = readmemw(easeg, cpu_state.eaaddr + 2);
            x87_settag(readmemw(easeg, cpu_state.eaaddr + 4));
            cpu_state.TOP = (cpu_state.npxs >> 11) & 7;
            cpu_state.eaaddr += 14;
            break;
        case 0x100: /*32-bit real mode*/
        case 0x101: /*32-bit protected mode*/
            cpu_state.npxc = readmemw(easeg, cpu_state.eaaddr);
            codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
            cpu_state.npxs = readmemw(easeg, cpu_state.eaaddr + 4);
            x87_settag(readmemw(easeg, cpu_state.eaaddr + 8));
            cpu_state.TOP = (cpu_state.npxs >> 11) & 7;
            cpu_state.eaaddr += 28;
            break;
    }
    x87_ld_frstor(0);
    cpu_state.eaaddr += 10;
    x87_ld_frstor(1);
    cpu_state.eaaddr += 10;
    x87_ld_frstor(2);
    cpu_state.eaaddr += 10;
    x87_ld_frstor(3);
    cpu_state.eaaddr += 10;
    x87_ld_frstor(4);
    cpu_state.eaaddr += 10;
    x87_ld_frstor(5);
    cpu_state.eaaddr += 10;
    x87_ld_frstor(6);
    cpu_state.eaaddr += 10;
    x87_ld_frstor(7);

    cpu_state.ismmx = 0;
    /*Horrible hack, but as 86Box doesn't keep the FPU stack in 80-bit precision at all times
      something like this is needed*/
    p = (uint64_t *) cpu_state.tag;
#ifdef USE_NEW_DYNAREC
    if (cpu_state.MM_w4[0] == 0xffff && cpu_state.MM_w4[1] == 0xffff && cpu_state.MM_w4[2] == 0xffff && cpu_state.MM_w4[3] == 0xffff && cpu_state.MM_w4[4] == 0xffff && cpu_state.MM_w4[5] == 0xffff && cpu_state.MM_w4[6] == 0xffff && cpu_state.MM_w4[7] == 0xffff && !cpu_state.TOP && (*p == 0x0101010101010101ull))
#else
    if (cpu_state.MM_w4[0] == 0xffff && cpu_state.MM_w4[1] == 0xffff && cpu_state.MM_w4[2] == 0xffff && cpu_state.MM_w4[3] == 0xffff && cpu_state.MM_w4[4] == 0xffff && cpu_state.MM_w4[5] == 0xffff && cpu_state.MM_w4[6] == 0xffff && cpu_state.MM_w4[7] == 0xffff && !cpu_state.TOP && !(*p))
#endif
        cpu_state.ismmx = 1;

    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.frstor) : (x87_timings.frstor * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.frstor) : (x87_concurrency.frstor * cpu_multi));
    return cpu_state.abrt;
}
static int
opFSTOR_a16(uint32_t fetchdat)
{
    floatx80 tmp;
    int offset;

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        FSTOR();
    } else {
        //pclog("FRSTOR_a16.\n");
        offset = fpu_load_environment();
        for (int n = 0; n < 8; n++) {
            tmp.fraction = readmemq(easeg, offset + (n * 10));
            tmp.exp = readmemw(easeg, offset + (n * 10) + 8);
            x87_save_reg_ext(tmp, IS_TAG_EMPTY(n) ? X87_TAG_EMPTY : SF_FPU_tagof(tmp), n, 1);
        }
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.frstor) : (x87_timings.frstor * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.frstor) : (x87_concurrency.frstor * cpu_multi));
    }
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
opFSTOR_a32(uint32_t fetchdat)
{
    pclog("FSTOR_a32.\n");
    floatx80 tmp;
    int offset;

    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        FSTOR();
    } else {
        offset = fpu_load_environment();
        for (int n = 0; n < 8; n++) {
            tmp.fraction = readmemq(easeg, offset + (n * 10));
            tmp.exp = readmemw(easeg, offset + (n * 10) + 8);
            x87_save_reg_ext(tmp, IS_TAG_EMPTY(n) ? X87_TAG_EMPTY : SF_FPU_tagof(tmp), n, 1);
        }
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.frstor) : (x87_timings.frstor * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.frstor) : (x87_concurrency.frstor * cpu_multi));
    }
    return cpu_state.abrt;
}
#endif

static int
FSAVE(void)
{
    uint64_t *p;

    FP_ENTER();
    cpu_state.npxs = (cpu_state.npxs & ~(7 << 11)) | ((cpu_state.TOP & 7) << 11);

    switch ((cr0 & 1) | (cpu_state.op32 & 0x100)) {
        case 0x000: /*16-bit real mode*/
            writememw(easeg, cpu_state.eaaddr, cpu_state.npxc);
            writememw(easeg, cpu_state.eaaddr + 2, cpu_state.npxs);
            writememw(easeg, cpu_state.eaaddr + 4, x87_gettag());
            writememw(easeg, cpu_state.eaaddr + 6, x87_pc_off);
            writememw(easeg, cpu_state.eaaddr + 10, x87_op_off);
            cpu_state.eaaddr += 14;
            if (cpu_state.ismmx) {
                x87_stmmx(cpu_state.MM[0]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[1]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[2]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[3]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[4]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[5]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[6]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[7]);
            } else {
                x87_st_fsave(0);
                cpu_state.eaaddr += 10;
                x87_st_fsave(1);
                cpu_state.eaaddr += 10;
                x87_st_fsave(2);
                cpu_state.eaaddr += 10;
                x87_st_fsave(3);
                cpu_state.eaaddr += 10;
                x87_st_fsave(4);
                cpu_state.eaaddr += 10;
                x87_st_fsave(5);
                cpu_state.eaaddr += 10;
                x87_st_fsave(6);
                cpu_state.eaaddr += 10;
                x87_st_fsave(7);
            }
            break;
        case 0x001: /*16-bit protected mode*/
            writememw(easeg, cpu_state.eaaddr, cpu_state.npxc);
            writememw(easeg, cpu_state.eaaddr + 2, cpu_state.npxs);
            writememw(easeg, cpu_state.eaaddr + 4, x87_gettag());
            writememw(easeg, cpu_state.eaaddr + 6, x87_pc_off);
            writememw(easeg, cpu_state.eaaddr + 8, x87_pc_seg);
            writememw(easeg, cpu_state.eaaddr + 10, x87_op_off);
            writememw(easeg, cpu_state.eaaddr + 12, x87_op_seg);
            cpu_state.eaaddr += 14;
            if (cpu_state.ismmx) {
                x87_stmmx(cpu_state.MM[0]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[1]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[2]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[3]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[4]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[5]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[6]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[7]);
            } else {
                x87_st_fsave(0);
                cpu_state.eaaddr += 10;
                x87_st_fsave(1);
                cpu_state.eaaddr += 10;
                x87_st_fsave(2);
                cpu_state.eaaddr += 10;
                x87_st_fsave(3);
                cpu_state.eaaddr += 10;
                x87_st_fsave(4);
                cpu_state.eaaddr += 10;
                x87_st_fsave(5);
                cpu_state.eaaddr += 10;
                x87_st_fsave(6);
                cpu_state.eaaddr += 10;
                x87_st_fsave(7);
            }
            break;
        case 0x100: /*32-bit real mode*/
            writememw(easeg, cpu_state.eaaddr, cpu_state.npxc);
            writememw(easeg, cpu_state.eaaddr + 4, cpu_state.npxs);
            writememw(easeg, cpu_state.eaaddr + 8, x87_gettag());
            writememw(easeg, cpu_state.eaaddr + 12, x87_pc_off);
            writememw(easeg, cpu_state.eaaddr + 20, x87_op_off);
            writememl(easeg, cpu_state.eaaddr + 24, (x87_op_off >> 16) << 12);
            cpu_state.eaaddr += 28;
            if (cpu_state.ismmx) {
                x87_stmmx(cpu_state.MM[0]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[1]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[2]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[3]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[4]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[5]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[6]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[7]);
            } else {
                x87_st_fsave(0);
                cpu_state.eaaddr += 10;
                x87_st_fsave(1);
                cpu_state.eaaddr += 10;
                x87_st_fsave(2);
                cpu_state.eaaddr += 10;
                x87_st_fsave(3);
                cpu_state.eaaddr += 10;
                x87_st_fsave(4);
                cpu_state.eaaddr += 10;
                x87_st_fsave(5);
                cpu_state.eaaddr += 10;
                x87_st_fsave(6);
                cpu_state.eaaddr += 10;
                x87_st_fsave(7);
            }
            break;
        case 0x101: /*32-bit protected mode*/
            writememw(easeg, cpu_state.eaaddr, cpu_state.npxc);
            writememw(easeg, cpu_state.eaaddr + 4, cpu_state.npxs);
            writememw(easeg, cpu_state.eaaddr + 8, x87_gettag());
            writememl(easeg, cpu_state.eaaddr + 12, x87_pc_off);
            writememl(easeg, cpu_state.eaaddr + 16, x87_pc_seg);
            writememl(easeg, cpu_state.eaaddr + 20, x87_op_off);
            writememl(easeg, cpu_state.eaaddr + 24, x87_op_seg);
            cpu_state.eaaddr += 28;
            if (cpu_state.ismmx) {
                x87_stmmx(cpu_state.MM[0]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[1]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[2]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[3]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[4]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[5]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[6]);
                cpu_state.eaaddr += 10;
                x87_stmmx(cpu_state.MM[7]);
            } else {
                x87_st_fsave(0);
                cpu_state.eaaddr += 10;
                x87_st_fsave(1);
                cpu_state.eaaddr += 10;
                x87_st_fsave(2);
                cpu_state.eaaddr += 10;
                x87_st_fsave(3);
                cpu_state.eaaddr += 10;
                x87_st_fsave(4);
                cpu_state.eaaddr += 10;
                x87_st_fsave(5);
                cpu_state.eaaddr += 10;
                x87_st_fsave(6);
                cpu_state.eaaddr += 10;
                x87_st_fsave(7);
            }
            break;
    }

    cpu_state.npxc = 0x37F;
    codegen_set_rounding_mode(X87_ROUNDING_NEAREST);
    cpu_state.npxs = 0;
    p              = (uint64_t *) cpu_state.tag;
#ifdef USE_NEW_DYNAREC
    *p = 0;
#else
    *p = 0x0303030303030303ll;
#endif
    cpu_state.TOP   = 0;
    cpu_state.ismmx = 0;

    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fsave) : (x87_timings.fsave * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fsave) : (x87_concurrency.fsave * cpu_multi));
    return cpu_state.abrt;
}
static int
opFSAVE_a16(uint32_t fetchdat)
{
    floatx80 stn;
    int offset;

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        FSAVE();
    } else {
        //pclog("FSAVE_a16.\n");
        offset = fpu_save_environment();
        /* save all registers in stack order. */
        for (int m = 0; m < 8; m++) {
            stn = x87_read_reg_ext(m);
            writememq(easeg, offset + (m * 10), stn.fraction);
            writememw(easeg, offset + (m * 10) + 8, stn.exp);
        }

#ifdef FPU_8087
        cpu_state.npxc = 0x3FF;
#else
        cpu_state.npxc = 0x37F;
#endif
        cpu_state.npxs = 0;
        cpu_state.TOP = 0;
        fpu_state.tag = 0xffff;
        cpu_state.ismmx = 0;
        x87_opcode = 0;
        x87_op_seg = 0;
        x87_op_off = 0;
        x87_pc_seg = 0;
        x87_pc_off = 0;
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fsave) : (x87_timings.fsave * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fsave) : (x87_concurrency.fsave * cpu_multi));
    }
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
opFSAVE_a32(uint32_t fetchdat)
{
    floatx80 stn;
    int offset;

    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        FSAVE();
    } else {
        //pclog("FSAVE_a32.\n");
        offset = fpu_save_environment();
        /* save all registers in stack order. */
        for (int m = 0; m < 8; m++) {
            stn = x87_read_reg_ext(m);
            writememq(easeg, offset + (m * 10), stn.fraction);
            writememw(easeg, offset + (m * 10) + 8, stn.exp);
        }

#ifdef FPU_8087
        cpu_state.npxc = 0x3FF;
#else
        cpu_state.npxc = 0x37F;
#endif
        cpu_state.npxs = 0;
        cpu_state.TOP = 0;
        fpu_state.tag = 0xffff;
        cpu_state.ismmx = 0;
        x87_opcode = 0;
        x87_op_seg = 0;
        x87_op_off = 0;
        x87_pc_seg = 0;
        x87_pc_off = 0;
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fsave) : (x87_timings.fsave * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fsave) : (x87_concurrency.fsave * cpu_multi));
    }
    return cpu_state.abrt;
}
#endif

static int
opFSTSW_a16(uint32_t fetchdat)
{
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec)
        seteaw((cpu_state.npxs & 0xC7FF) | ((cpu_state.TOP & 7) << 11));
    else {
        //pclog("FSTSW_a16.\n");
        seteaw((cpu_state.npxs & ~FPU_SW_Top & 0xFFFF) | ((cpu_state.TOP << 11) & FPU_SW_Top));
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstcw_sw) : (x87_timings.fstcw_sw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstcw_sw) : (x87_concurrency.fstcw_sw * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
opFSTSW_a32(uint32_t fetchdat)
{
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec)
        seteaw((cpu_state.npxs & 0xC7FF) | ((cpu_state.TOP & 7) << 11));
    else {
        //pclog("FSTSW_a32.\n");
        seteaw((cpu_state.npxs & ~FPU_SW_Top & 0xFFFF) | ((cpu_state.TOP << 11) & FPU_SW_Top));
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstcw_sw) : (x87_timings.fstcw_sw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstcw_sw) : (x87_concurrency.fstcw_sw * cpu_multi));
    return cpu_state.abrt;
}
#endif

static int
opFLD(uint32_t fetchdat)
{
    const floatx80 floatx80_default_nan = packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);
    floatx80 sti_reg;
    int      old_tag;
    uint64_t old_i64;

    pclog("FLD_sti.\n");
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        old_tag = cpu_state.tag[(cpu_state.TOP + fetchdat) & 7];
        old_i64 = cpu_state.MM[(cpu_state.TOP + fetchdat) & 7].q;
        x87_push(ST(fetchdat & 7));
        cpu_state.tag[cpu_state.TOP & 7]  = old_tag;
        cpu_state.MM[cpu_state.TOP & 7].q = old_i64;
    } else {
        clear_C1();
        if (!IS_TAG_EMPTY(-1)) {
            x87_stack_overflow();
            return 1;
        }
        sti_reg = floatx80_default_nan;
        if (IS_TAG_EMPTY(fetchdat & 7)) {
            x87_checkexceptions(FPU_EX_Stack_Underflow, 0);
            if (is_IA_masked())
                return 1;
        } else {
            sti_reg = x87_read_reg_ext(fetchdat & 7);
            x87_push_ext();
            x87_save_reg_ext(sti_reg, -1, 0, 0);
        }
    }

//next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld) : (x87_timings.fld * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld) : (x87_concurrency.fld * cpu_multi));
    return 0;
}

static int
opFXCH(uint32_t fetchdat)
{
    const floatx80 floatx80_default_nan = packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);
    floatx80 st0_reg, sti_reg;
    int st0_tag, sti_tag;
    double   td;
    uint8_t  old_tag;
    uint64_t old_i64;

    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        td                                             = ST(0);
        ST(0)                                          = ST(fetchdat & 7);
        ST(fetchdat & 7)                               = td;
        old_tag                                        = cpu_state.tag[cpu_state.TOP & 7];
        cpu_state.tag[cpu_state.TOP & 7]               = cpu_state.tag[(cpu_state.TOP + fetchdat) & 7];
        cpu_state.tag[(cpu_state.TOP + fetchdat) & 7]  = old_tag;
        old_i64                                        = cpu_state.MM[cpu_state.TOP & 7].q;
        cpu_state.MM[cpu_state.TOP & 7].q              = cpu_state.MM[(cpu_state.TOP + fetchdat) & 7].q;
        cpu_state.MM[(cpu_state.TOP + fetchdat) & 7].q = old_i64;
    } else {
        //pclog("FXCH_sti.\n");
        st0_tag = x87_get_tag_ext(0);
        sti_tag = x87_get_tag_ext(fetchdat & 7);
        st0_reg = x87_read_reg_ext(0);
        sti_reg = x87_read_reg_ext(fetchdat & 7);

        clear_C1();
        if ((st0_tag == X87_TAG_EMPTY) || (sti_tag == X87_TAG_EMPTY)) {
            x87_checkexceptions(FPU_EX_Stack_Underflow, 0);
            if (cpu_state.npxc & FPU_CW_Invalid) {
                /* Masked response */
                if (st0_tag == X87_TAG_EMPTY)
                    st0_reg = floatx80_default_nan;
                if (sti_tag == X87_TAG_EMPTY)
                    sti_reg = floatx80_default_nan;
            } else
                goto next_ins;
        }
        x87_save_reg_ext(st0_reg, -1, fetchdat & 7, 0);
        x87_save_reg_ext(sti_reg, -1, 0, 0);
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fxch) : (x87_timings.fxch * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fxch) : (x87_concurrency.fxch * cpu_multi));
    return 0;
}

static int
opFCHS(uint32_t fetchdat)
{
    floatx80 st0_reg;

    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(0) = -ST(0);
        FP_TAG_VALID;
    } else {
        //pclog("FCHS.\n");
        if (IS_TAG_EMPTY(0))
            x87_stack_underflow(0, 0);
        else {
            clear_C1();
            st0_reg = x87_read_reg_ext(0);
            x87_save_reg_ext(floatx80_chs(st0_reg), -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fchs) : (x87_timings.fchs * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fchs) : (x87_concurrency.fchs * cpu_multi));
    return 0;
}

static int
opFABS(uint32_t fetchdat)
{
    floatx80 st0_reg;

    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(0) = fabs(ST(0));
        FP_TAG_VALID;
    } else {
        //pclog("FABS.\n");
        if (IS_TAG_EMPTY(0))
            x87_stack_underflow(0, 0);
        else {
            clear_C1();
            st0_reg = x87_read_reg_ext(0);
            x87_save_reg_ext(floatx80_abs(st0_reg), -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fabs) : (x87_timings.fabs * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fabs) : (x87_concurrency.fabs * cpu_multi));
    return 0;
}

static int
opFTST(uint32_t fetchdat)
{
    const floatx80 Const_Z = packFloatx80(0, 0x0000, 0);
    int rc;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        cpu_state.npxs &= ~(C0 | C2 | C3);
        if (ST(0) == 0.0)
            cpu_state.npxs |= C3;
        else if (ST(0) < 0.0)
            cpu_state.npxs |= C0;
    } else {
        //pclog("FTST.\n");
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_checkexceptions(FPU_EX_Stack_Underflow, 0);
            setcc(C0 | C2 | C3);
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            rc = floatx80_compare_two(x87_read_reg_ext(0), Const_Z, &status);
            setcc(x87_status_word_flags_fpu_compare(rc));
            x87_checkexceptions(status.float_exception_flags, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.ftst) : (x87_timings.ftst * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.ftst) : (x87_concurrency.ftst * cpu_multi));
    return 0;
}

static int
opFXAM(uint32_t fetchdat)
{
    floatx80 reg;
    int sign;
    float_class_t aClass;
    pclog("FXAM.\n");

    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        cpu_state.npxs &= ~(C0 | C1 | C2 | C3);
#ifdef USE_NEW_DYNAREC
        if (cpu_state.tag[cpu_state.TOP & 7] == TAG_EMPTY)
            cpu_state.npxs |= (C0 | C3);
#else
        if (cpu_state.tag[cpu_state.TOP & 7] == 3)
            cpu_state.npxs |= (C0 | C3);
#endif
        else if (ST(0) == 0.0)
            cpu_state.npxs |= C3;
        else
            cpu_state.npxs |= C2;
        if (ST(0) < 0.0)
            cpu_state.npxs |= C1;
    } else {
        //pclog("FXAM.\n");
        reg = x87_read_reg_ext(0);
        sign = floatx80_sign(reg);
      /*
       * Examine the contents of the ST(0) register and sets the condition
       * code flags C0, C2 and C3 in the FPU status word to indicate the
       * class of value or number in the register.
       */
        if (IS_TAG_EMPTY(0)) {
            setcc(C3 | C1 | C0);
        } else {
            aClass = floatx80_class(reg);
            switch (aClass) {
                case float_zero:
                    setcc(C3 | C1);
                    break;
                case float_SNaN:
                case float_QNaN:
                    // unsupported handled as NaNs
                    if (floatx80_is_unsupported(reg)) {
                        setcc(C1);
                    } else {
                        setcc(C1 | C0);
                    }
                    break;
                case float_negative_inf:
                case float_positive_inf:
                    setcc(C2 | C1 | C0);
                    break;
                case float_denormal:
                    setcc(C3 | C2 | C1);
                    break;
                case float_normalized:
                    setcc(C2 | C1);
                    break;
            }
        }
      /*
       * The C1 flag is set to the sign of the value in ST(0), regardless
       * of whether the register is empty or full.
       */
       if (!sign)
        clear_C1();
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fxam) : (x87_timings.fxam * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fxam) : (x87_concurrency.fxam * cpu_multi));
    return 0;
}

static int
opFLD1(uint32_t fetchdat)
{
    const floatx80 Const_1    = packFloatx80(0, 0x3fff, BX_CONST64(0x8000000000000000));

    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec)
        x87_push(1.0);
    else {
        //pclog("FLD1.\n");
        clear_C1();
        if (!IS_TAG_EMPTY(-1))
            x87_stack_overflow();
        else {
            x87_push_ext();
            x87_save_reg_ext(Const_1, -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_z1) : (x87_timings.fld_z1 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_z1) : (x87_concurrency.fld_z1 * cpu_multi));
    return 0;
}

static int
opFLDL2T(uint32_t fetchdat)
{
    const floatx80 Const_L2T  = packFloatx80(0, 0x4000, BX_CONST64(0xd49a784bcd1b8afe));

    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec)
        x87_push(3.3219280948873623);
    else {
            //pclog("FLDL2T.\n");
        clear_C1();
        if (!IS_TAG_EMPTY(-1))
            x87_stack_overflow();
        else {
            x87_push_ext();
            x87_save_reg_ext(x87_round_const(Const_L2T, (cpu_state.npxc & FPU_CW_RC) == X87_ROUNDING_UP), -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_const) : (x87_timings.fld_const * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_const) : (x87_concurrency.fld_const * cpu_multi));
    return 0;
}

static int
opFLDL2E(uint32_t fetchdat)
{
    const floatx80 Const_L2E  = packFloatx80(0, 0x3fff, BX_CONST64(0xb8aa3b295c17f0bc));

    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec)
        x87_push(1.4426950408889634);
    else {
        //pclog("FLDL2E.\n");
        clear_C1();
        if (!IS_TAG_EMPTY(-1))
            x87_stack_overflow();
        else {
            x87_push_ext();
            x87_save_reg_ext(x87_round_const(Const_L2E, DOWN_OR_CHOP() ? -1 : 0), -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_const) : (x87_timings.fld_const * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_const) : (x87_concurrency.fld_const * cpu_multi));
    return 0;
}

static int
opFLDPI(uint32_t fetchdat)
{
    const floatx80 Const_PI   = packFloatx80(0, 0x4000, BX_CONST64(0xc90fdaa22168c235));

    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec)
        x87_push(3.141592653589793);
    else {
        //pclog("FLDPI.\n");
        clear_C1();
        if (!IS_TAG_EMPTY(-1))
            x87_stack_overflow();
        else {
            x87_push_ext();
            x87_save_reg_ext(x87_round_const(Const_PI, DOWN_OR_CHOP() ? -1 : 0), -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_const) : (x87_timings.fld_const * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_const) : (x87_concurrency.fld_const * cpu_multi));
    return 0;
}

static int
opFLDEG2(uint32_t fetchdat)
{
    const floatx80 Const_LG2  = packFloatx80(0, 0x3ffd, BX_CONST64(0x9a209a84fbcff799));

    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec)
        x87_push(0.3010299956639812);
    else {
        //pclog("FLDEG2.\n");
        clear_C1();
        if (!IS_TAG_EMPTY(-1))
            x87_stack_overflow();
        else {
            x87_push_ext();
            x87_save_reg_ext(x87_round_const(Const_LG2, DOWN_OR_CHOP() ? -1 : 0), -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_const) : (x87_timings.fld_const * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_const) : (x87_concurrency.fld_const * cpu_multi));
    return 0;
}

static int
opFLDLN2(uint32_t fetchdat)
{
    const floatx80 Const_LN2  = packFloatx80(0, 0x3ffe, BX_CONST64(0xb17217f7d1cf79ac));

    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec)
        x87_push_u64(0x3fe62e42fefa39f0ull);
    else {
        //pclog("FLDL2N.\n");
        clear_C1();
        if (!IS_TAG_EMPTY(-1))
            x87_stack_overflow();
        else {
            x87_push_ext();
            x87_save_reg_ext(x87_round_const(Const_LN2, DOWN_OR_CHOP() ? -1 : 0), -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_const) : (x87_timings.fld_const * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_const) : (x87_concurrency.fld_const * cpu_multi));
    return 0;
}

static int
opFLDZ(uint32_t fetchdat)
{
    const floatx80 Const_Z = packFloatx80(0, 0x0000, 0);

    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        x87_push(0.0);
        FP_TAG_VALID;
    } else {
        //pclog("FLDZ.\n");
        clear_C1();
        if (!IS_TAG_EMPTY(-1))
            x87_stack_overflow();
        else {
            x87_push_ext();
            x87_save_reg_ext(Const_Z, -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fld_z1) : (x87_timings.fld_z1 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fld_z1) : (x87_concurrency.fld_z1 * cpu_multi));
    return 0;
}

static int
opF2XM1(uint32_t fetchdat)
{
    floatx80 result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(0) = pow(2.0, ST(0)) - 1.0;
        FP_TAG_VALID;
    } else {
        //pclog("F2XM1.\n");
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_stack_underflow(0, 0);
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word() | FPU_PR_80_BITS);
            result = f2xm1(x87_read_reg_ext(0), &status);
            if (!x87_checkexceptions(status.float_exception_flags, 0))
                x87_save_reg_ext(result, -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.f2xm1) : (x87_timings.f2xm1 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.f2xm1) : (x87_concurrency.f2xm1 * cpu_multi));
    return 0;
}

static int
opFYL2X(uint32_t fetchdat)
{
    floatx80 result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(1) = ST(1) * (log(ST(0)) / log(2.0));
        FP_TAG_VALID_N;
        x87_pop();
    } else {
        //pclog("FYL2X.\n");
        clear_C1();
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(1)) {
            x87_stack_underflow(1, 1);
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word() | FPU_PR_80_BITS);
            result = fyl2x(x87_read_reg_ext(0), x87_read_reg_ext(1), &status);
            if (!x87_checkexceptions(status.float_exception_flags, 0)) {
                x87_pop_ext();
                x87_save_reg_ext(result, -1, 0, 0);
            }
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fyl2x) : (x87_timings.fyl2x * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fyl2x) : (x87_concurrency.fyl2x * cpu_multi));
    return 0;
}

static int
opFYL2XP1(uint32_t fetchdat)
{
    floatx80 result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(1) = ST(1) * (log1p(ST(0)) / log(2.0));
        FP_TAG_VALID_N;
        x87_pop();
    } else {
        //pclog("FYL2XP1.\n");
        clear_C1();
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(1)) {
            x87_stack_underflow(1, 1);
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word() | FPU_PR_80_BITS);
            result = fyl2xp1(x87_read_reg_ext(0), x87_read_reg_ext(1), &status);
            if (!x87_checkexceptions(status.float_exception_flags, 0)) {
                x87_pop_ext();
                x87_save_reg_ext(result, -1, 0, 0);
            }
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fyl2xp1) : (x87_timings.fyl2xp1 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fyl2xp1) : (x87_concurrency.fyl2xp1 * cpu_multi));
    return 0;
}

static int
opFPTAN(uint32_t fetchdat)
{
    const floatx80 Const_1    = packFloatx80(0, 0x3fff, BX_CONST64(0x8000000000000000));
    const floatx80 floatx80_default_nan = packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);

    floatx80 y;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(0) = tan(ST(0));
        FP_TAG_VALID;
        x87_push(1.0);
        cpu_state.npxs &= ~C2;
    } else {
        //pclog("FPTAN.\n");
        clear_C1();
        clear_C2();
        if (IS_TAG_EMPTY(0) || !IS_TAG_EMPTY(-1)) {
            if (IS_TAG_EMPTY(0))
                x87_checkexceptions(FPU_EX_Stack_Underflow, 0);
            else
                x87_checkexceptions(FPU_EX_Stack_Overflow, 0);

            /* The masked response */
            if (is_IA_masked()) {
                x87_save_reg_ext(floatx80_default_nan, -1, 0, 0);
                x87_push_ext();
                x87_save_reg_ext(floatx80_default_nan, -1, 0, 0);
            }
            goto next_ins;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word() | FPU_PR_80_BITS);
        y = x87_read_reg_ext(0);
        if (ftan(&y, &status) == -1) {
            cpu_state.npxs |= C2;
            goto next_ins;
        }

        if (floatx80_is_nan(y)) {
            if (!x87_checkexceptions(status.float_exception_flags, 0)) {
                x87_save_reg_ext(y, -1, 0, 0);
                x87_push_ext();
                x87_save_reg_ext(y, -1, 0, 0);
            }
            goto next_ins;
        }

        if (!x87_checkexceptions(status.float_exception_flags, 0)) {
            x87_save_reg_ext(y, -1, 0, 0);
            x87_push_ext();
            x87_save_reg_ext(Const_1, -1, 0, 0);
        }
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fptan) : (x87_timings.fptan * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fptan) : (x87_concurrency.fptan * cpu_multi));
    return 0;
}

static int
opFPATAN(uint32_t fetchdat)
{
    floatx80 result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(1) = atan2(ST(1), ST(0));
        FP_TAG_VALID_N;
        x87_pop();
    } else {
        //pclog("FPATAN.\n");
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(1)) {
            x87_stack_underflow(1, 1);
            goto next_ins;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word() | FPU_PR_80_BITS);
        result = fpatan(x87_read_reg_ext(0), x87_read_reg_ext(1), &status);
        if (!x87_checkexceptions(status.float_exception_flags, 0)) {
            x87_pop_ext();
            x87_save_reg_ext(result, -1, 0, 0);
        }
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fpatan) : (x87_timings.fpatan * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fpatan) : (x87_concurrency.fpatan * cpu_multi));
    return 0;
}

static int
opFDECSTP(uint32_t fetchdat)
{
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
#ifdef USE_NEW_DYNAREC
        cpu_state.TOP--;
#else
        cpu_state.TOP = (cpu_state.TOP - 1) & 7;
#endif
    } else {
        //pclog("FDECSTP.\n");
        clear_C1();
        cpu_state.TOP = (cpu_state.TOP - 1) & 7;
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fincdecstp) : (x87_timings.fincdecstp * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fincdecstp) : (x87_concurrency.fincdecstp * cpu_multi));
    return 0;
}

static int
opFINCSTP(uint32_t fetchdat)
{
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
#ifdef USE_NEW_DYNAREC
        cpu_state.TOP++;
#else
        cpu_state.TOP = (cpu_state.TOP + 1) & 7;
#endif
    } else {
        //pclog("FINCSTP.\n");
        clear_C1();
        cpu_state.TOP = (cpu_state.TOP + 1) & 7;
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fincdecstp) : (x87_timings.fincdecstp * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fincdecstp) : (x87_concurrency.fincdecstp * cpu_multi));
    return 0;
}

static int
opFPREM(uint32_t fetchdat)
{
    floatx80 a, b, result;
    float_status_t status;
    uint64_t quotient = 0;
    int64_t temp64;
    int flags, cc;

    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        temp64 = (int64_t) (ST(0) / ST(1));
        ST(0)  = ST(0) - (ST(1) * (double) temp64);
        FP_TAG_VALID;
        cpu_state.npxs &= ~(C0 | C1 | C2 | C3);
        if (temp64 & 4)
            cpu_state.npxs |= C0;
        if (temp64 & 2)
            cpu_state.npxs |= C3;
        if (temp64 & 1)
            cpu_state.npxs |= C1;
    } else {
        //pclog("FPREM.\n");
        clear_C1();
        clear_C2();
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(1)) {
            x87_stack_underflow(0, 0);
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            a = x87_read_reg_ext(0);
            b = x87_read_reg_ext(1);
            // handle unsupported extended double-precision floating encodings
            flags = floatx80_remainder(a, b, &result, &quotient, &status);
            if (!x87_checkexceptions(status.float_exception_flags, 0)) {
                if (flags >= 0) {
                    cc = 0;
                    if (flags)
                        cc = C2;
                    else {
                        if (quotient & 1)
                            cc |= C1;
                        if (quotient & 2)
                            cc |= C3;
                        if (quotient & 4)
                            cc |= C0;
                    }
                    setcc(cc);
                }
                x87_save_reg_ext(result, -1, 0, 0);
            }
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fprem) : (x87_timings.fprem * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fprem) : (x87_concurrency.fprem * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFPREM1(uint32_t fetchdat)
{
    floatx80 a, b, result;
    float_status_t status;
    uint64_t quotient = 0;
    int64_t temp64;
    int flags, cc;

    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        temp64 = (int64_t) (ST(0) / ST(1));
        ST(0)  = ST(0) - (ST(1) * (double) temp64);
        FP_TAG_VALID;
        cpu_state.npxs &= ~(C0 | C1 | C2 | C3);
        if (temp64 & 4)
            cpu_state.npxs |= C0;
        if (temp64 & 2)
            cpu_state.npxs |= C3;
        if (temp64 & 1)
            cpu_state.npxs |= C1;
    } else {
        //pclog("FPREM1.\n");
        clear_C1();
        clear_C2();
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(1)) {
            x87_stack_underflow(0, 0);
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            a = x87_read_reg_ext(0);
            b = x87_read_reg_ext(1);
            flags = floatx80_ieee754_remainder(a, b, &result, &quotient, &status);
            if (!x87_checkexceptions(status.float_exception_flags, 0)) {
                if (flags >= 0) {
                    cc = 0;
                    if (flags)
                        cc = C2;
                    else {
                        if (quotient & 1)
                            cc |= C1;
                        if (quotient & 2)
                            cc |= C3;
                        if (quotient & 4)
                            cc |= C0;
                    }
                    setcc(cc);
                }
                x87_save_reg_ext(result, -1, 0, 0);
            }
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fprem1) : (x87_timings.fprem1 * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fprem1) : (x87_concurrency.fprem1 * cpu_multi));
    return 0;
}
#endif

static int
opFSQRT(uint32_t fetchdat)
{
    floatx80 result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(0) = sqrt(ST(0));
        FP_TAG_VALID;
    } else {
        //pclog("FSQRT.\n");
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_stack_underflow(0, 0);
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            result = floatx80_sqrt(x87_read_reg_ext(0), &status);
            if (!x87_checkexceptions(status.float_exception_flags, 0))
                x87_save_reg_ext(result, -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fsqrt) : (x87_timings.fsqrt * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fsqrt) : (x87_concurrency.fsqrt * cpu_multi));
    return 0;
}

#ifndef FPU_8087
static int
opFSINCOS(uint32_t fetchdat)
{
    const floatx80 floatx80_default_nan = packFloatx80(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);
    float_status_t status;
    floatx80 y, sin_y, cos_y;
    double td;

    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        td    = ST(0);
        ST(0) = sin(td);
        FP_TAG_VALID;
        x87_push(cos(td));
        cpu_state.npxs &= ~C2;
    } else {
        //pclog("FSINCOS.\n");
        clear_C1();
        clear_C2();
        if (IS_TAG_EMPTY(0) || !IS_TAG_EMPTY(-1)) {
            if (IS_TAG_EMPTY(0))
                x87_checkexceptions(FPU_EX_Stack_Underflow, 0);
            else
                x87_checkexceptions(FPU_EX_Stack_Overflow, 0);

            /* The masked response */
            if (cpu_state.npxc & FPU_CW_Invalid) {
                x87_save_reg_ext(floatx80_default_nan, -1, 0, 0);
                x87_push_ext();
                x87_save_reg_ext(floatx80_default_nan, -1, 0, 0);
            }
            goto next_ins;
        }
        i387cw_to_softfloat_status_word(&status, i387_get_control_word() | FPU_PR_80_BITS);
        y = x87_read_reg_ext(0);
        if (fsincos(y, &sin_y, &cos_y, &status) == -1)
            cpu_state.npxs |= C2;
        else {
            if (!x87_checkexceptions(status.float_exception_flags, 0)) {
                x87_save_reg_ext(sin_y, -1, 0, 0);
                x87_push_ext();
                x87_save_reg_ext(cos_y, -1, 0, 0);
            }
        }
    }

next_ins:
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fsincos) : (x87_timings.fsincos * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fsincos) : (x87_concurrency.fsincos * cpu_multi));
    return 0;
}
#endif

static int
opFRNDINT(uint32_t fetchdat)
{
    pclog("FRNDINT.\n");
    floatx80 result;
    float_status_t status;
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(0) = (double) x87_fround(ST(0));
        FP_TAG_VALID;
    } else {
        clear_C1();
        if (IS_TAG_EMPTY(0)) {
            x87_stack_underflow(0, 0);
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            result = floatx80_round_to_int(x87_read_reg_ext(0), &status);
            if (!x87_checkexceptions(status.float_exception_flags, 0))
                x87_save_reg_ext(result, -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.frndint) : (x87_timings.frndint * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.frndint) : (x87_concurrency.frndint * cpu_multi));
    return 0;
}

static int
opFSCALE(uint32_t fetchdat)
{
    floatx80 result;
    float_status_t status;
    int64_t temp64;

    pclog("FSCALE.\n");
    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        temp64 = (int64_t) ST(1);
        if (ST(0) != 0.0)
            ST(0) = ST(0) * pow(2.0, (double) temp64);
        FP_TAG_VALID;
    } else {
        clear_C1();
        if (IS_TAG_EMPTY(0) || IS_TAG_EMPTY(1)) {
            x87_stack_underflow(0, 0);
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word());
            result = floatx80_scale(x87_read_reg_ext(0), x87_read_reg_ext(1), &status);
            if (!x87_checkexceptions(status.float_exception_flags, 0))
                x87_save_reg_ext(result, -1, 0, 0);
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fscale) : (x87_timings.fscale * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fscale) : (x87_concurrency.fscale * cpu_multi));
    return 0;
}

#ifndef FPU_8087
static int
opFSIN(uint32_t fetchdat)
{
    floatx80 y;
    float_status_t status;

    pclog("FSIN.\n");

    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(0) = sin(ST(0));
        FP_TAG_VALID;
        cpu_state.npxs &= ~C2;
    } else {
        clear_C1();
        clear_C2();
        if (IS_TAG_EMPTY(0)) {
            x87_stack_underflow(0, 0);
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word() | FPU_PR_80_BITS);
            y = x87_read_reg_ext(0);
            if (fsin(&y, &status) == -1)
                cpu_state.npxs |= C2;
            else {
                if (!x87_checkexceptions(status.float_exception_flags, 0))
                    x87_save_reg_ext(y, -1, 0, 0);
            }
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fsin_cos) : (x87_timings.fsin_cos * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fsin_cos) : (x87_concurrency.fsin_cos * cpu_multi));
    return 0;
}

static int
opFCOS(uint32_t fetchdat)
{
    floatx80 y;
    float_status_t status;
    pclog("FCOS.\n");

    FP_ENTER();
    cpu_state.pc++;
    if (cpu_use_dynarec) {
        ST(0) = cos(ST(0));
        FP_TAG_VALID;
        cpu_state.npxs &= ~C2;
    } else {
        clear_C1();
        clear_C2();
        if (IS_TAG_EMPTY(0)) {
            x87_stack_underflow(0, 0);
        } else {
            i387cw_to_softfloat_status_word(&status, i387_get_control_word() | FPU_PR_80_BITS);
            y = x87_read_reg_ext(0);
            if (fcos(&y, &status) == -1)
                cpu_state.npxs |= C2;
            else {
                if (!x87_checkexceptions(status.float_exception_flags, 0))
                    x87_save_reg_ext(y, -1, 0, 0);
            }
        }
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fsin_cos) : (x87_timings.fsin_cos * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fsin_cos) : (x87_concurrency.fsin_cos * cpu_multi));
    return 0;
}
#endif

static int
FLDENV(void)
{
    FP_ENTER();
    switch ((cr0 & 1) | (cpu_state.op32 & 0x100)) {
        case 0x000: /*16-bit real mode*/
        case 0x001: /*16-bit protected mode*/
            cpu_state.npxc = readmemw(easeg, cpu_state.eaaddr);
            codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
            cpu_state.npxs = readmemw(easeg, cpu_state.eaaddr + 2);
            x87_settag(readmemw(easeg, cpu_state.eaaddr + 4));
            cpu_state.TOP = (cpu_state.npxs >> 11) & 7;
            break;
        case 0x100: /*32-bit real mode*/
        case 0x101: /*32-bit protected mode*/
            cpu_state.npxc = readmemw(easeg, cpu_state.eaaddr);
            codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
            cpu_state.npxs = readmemw(easeg, cpu_state.eaaddr + 4);
            x87_settag(readmemw(easeg, cpu_state.eaaddr + 8));
            cpu_state.TOP = (cpu_state.npxs >> 11) & 7;
            break;
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fldenv) : (x87_timings.fldenv * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fldenv) : (x87_concurrency.fldenv * cpu_multi));
    return cpu_state.abrt;
}

static int
opFLDENV_a16(uint32_t fetchdat)
{
    pclog("FLDENV_a16.\n");
    int tag;

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    if (cpu_use_dynarec)
        FLDENV();
    else {
        fpu_load_environment();

        /* read all registers in stack order and update x87 tag word */
        for (int n = 0; n < 8; n++) {
            // update tag only if it is not empty
            if (!IS_TAG_EMPTY(n)) {
                tag = SF_FPU_tagof(x87_read_reg_ext(n));
                x87_set_tag_ext(tag, n, 0);
            }
        }
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fldenv) : (x87_timings.fldenv * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fldenv) : (x87_concurrency.fldenv * cpu_multi));
    }
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
opFLDENV_a32(uint32_t fetchdat)
{
    pclog("FLDENV_a32.\n");
    int tag;

    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    if (cpu_use_dynarec)
        FLDENV();
    else {
        fpu_load_environment();

        /* read all registers in stack order and update x87 tag word */
        for (int n = 0; n < 8; n++) {
            // update tag only if it is not empty
            if (!IS_TAG_EMPTY(n)) {
                tag = SF_FPU_tagof(x87_read_reg_ext(n));
                x87_set_tag_ext(tag, n, 0);
            }
        }
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fldenv) : (x87_timings.fldenv * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fldenv) : (x87_concurrency.fldenv * cpu_multi));
    }
    return cpu_state.abrt;
}
#endif

static int
opFLDCW_a16(uint32_t fetchdat)
{
    pclog("FLDCW_a16.\n");

    uint16_t tempw;
    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        tempw = geteaw();
        if (cpu_state.abrt)
            return 1;
        cpu_state.npxc = tempw;
        codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
    } else {
        tempw = readmemw(easeg, cpu_state.eaaddr);

        cpu_state.npxc = (tempw & ~FPU_CW_Reserved_Bits) | 0x0040; // bit 6 is reserved as '1
        /* check for unmasked exceptions */
        if (cpu_state.npxs & ~cpu_state.npxc & FPU_CW_Exceptions_Mask) {
            /* set the B and ES bits in the status-word */
            cpu_state.npxs |= FPU_SW_Summary | FPU_SW_Backward;
        } else {
            /* clear the B and ES bits in the status-word */
            cpu_state.npxs &= ~(FPU_SW_Summary | FPU_SW_Backward);
        }
        pclog("Tempw = %04x, NPXC = %04x.\n", tempw, cpu_state.npxc);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fldcw) : (x87_timings.fldcw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fldcw) : (x87_concurrency.fldcw * cpu_multi));
    return 0;
}
#ifndef FPU_8087
static int
opFLDCW_a32(uint32_t fetchdat)
{
    pclog("FLDCW_a32.\n");

    uint16_t tempw;
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_READ(cpu_state.ea_seg);
    if (cpu_use_dynarec) {
        tempw = geteaw();
        if (cpu_state.abrt)
            return 1;
        cpu_state.npxc = tempw;
        codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
    } else {
        tempw = readmemw(easeg, cpu_state.eaaddr);

        cpu_state.npxc = (tempw & ~FPU_CW_Reserved_Bits) | 0x0040; // bit 6 is reserved as '1
        /* check for unmasked exceptions */
        if (cpu_state.npxs & ~cpu_state.npxc & FPU_CW_Exceptions_Mask) {
            /* set the B and ES bits in the status-word */
            cpu_state.npxs |= FPU_SW_Summary | FPU_SW_Backward;
            //pclog("B and ES bits set.\n");
        } else {
            /* clear the B and ES bits in the status-word */
            cpu_state.npxs &= ~(FPU_SW_Summary | FPU_SW_Backward);
            //pclog("B and ES bits cleared.\n");
        }
        pclog("Tempw = %04x, NPXC = %04x.\n", tempw, cpu_state.npxc);
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fldcw) : (x87_timings.fldcw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fldcw) : (x87_concurrency.fldcw * cpu_multi));
    return 0;
}
#endif

static int
FSTENV(void)
{
    FP_ENTER();
    cpu_state.npxs = (cpu_state.npxs & ~(7 << 11)) | ((cpu_state.TOP & 7) << 11);

    switch ((cr0 & 1) | (cpu_state.op32 & 0x100)) {
        case 0x000: /*16-bit real mode*/
            writememw(easeg, cpu_state.eaaddr, cpu_state.npxc);
            writememw(easeg, cpu_state.eaaddr + 2, cpu_state.npxs);
            writememw(easeg, cpu_state.eaaddr + 4, x87_gettag());
            writememw(easeg, cpu_state.eaaddr + 6, x87_pc_off);
            writememw(easeg, cpu_state.eaaddr + 10, x87_op_off);
            break;
        case 0x001: /*16-bit protected mode*/
            writememw(easeg, cpu_state.eaaddr, cpu_state.npxc);
            writememw(easeg, cpu_state.eaaddr + 2, cpu_state.npxs);
            writememw(easeg, cpu_state.eaaddr + 4, x87_gettag());
            writememw(easeg, cpu_state.eaaddr + 6, x87_pc_off);
            writememw(easeg, cpu_state.eaaddr + 8, x87_pc_seg);
            writememw(easeg, cpu_state.eaaddr + 10, x87_op_off);
            writememw(easeg, cpu_state.eaaddr + 12, x87_op_seg);
            break;
        case 0x100: /*32-bit real mode*/
            writememw(easeg, cpu_state.eaaddr, cpu_state.npxc);
            writememw(easeg, cpu_state.eaaddr + 4, cpu_state.npxs);
            writememw(easeg, cpu_state.eaaddr + 8, x87_gettag());
            writememw(easeg, cpu_state.eaaddr + 12, x87_pc_off);
            writememw(easeg, cpu_state.eaaddr + 20, x87_op_off);
            writememl(easeg, cpu_state.eaaddr + 24, (x87_op_off >> 16) << 12);
            break;
        case 0x101: /*32-bit protected mode*/
            writememw(easeg, cpu_state.eaaddr, cpu_state.npxc);
            writememw(easeg, cpu_state.eaaddr + 4, cpu_state.npxs);
            writememw(easeg, cpu_state.eaaddr + 8, x87_gettag());
            writememl(easeg, cpu_state.eaaddr + 12, x87_pc_off);
            writememl(easeg, cpu_state.eaaddr + 16, x87_pc_seg);
            writememl(easeg, cpu_state.eaaddr + 20, x87_op_off);
            writememl(easeg, cpu_state.eaaddr + 24, x87_op_seg);
            break;
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstenv) : (x87_timings.fstenv * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstenv) : (x87_concurrency.fstenv * cpu_multi));
    return cpu_state.abrt;
}

static int
opFSTENV_a16(uint32_t fetchdat)
{
    pclog("FSTENV_a16.\n");

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec)
        FSTENV();
    else {
        fpu_save_environment();
        /* mask all floating point exceptions */
        cpu_state.npxc |= FPU_CW_Exceptions_Mask;
        /* clear the B and ES bits in the status word */
        cpu_state.npxs &= ~(FPU_SW_Backward|FPU_SW_Summary);
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstenv) : (x87_timings.fstenv * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstenv) : (x87_concurrency.fstenv * cpu_multi));
    }
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
opFSTENV_a32(uint32_t fetchdat)
{
    pclog("FSTENV_a32.\n");
    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec)
        FSTENV();
    else {
        fpu_save_environment();
        /* mask all floating point exceptions */
        cpu_state.npxc |= FPU_CW_Exceptions_Mask;
        /* clear the B and ES bits in the status word */
        cpu_state.npxs &= ~(FPU_SW_Backward|FPU_SW_Summary);
        CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstenv) : (x87_timings.fstenv * cpu_multi));
        CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstenv) : (x87_concurrency.fstenv * cpu_multi));
    }
    return cpu_state.abrt;
}
#endif

static int
opFSTCW_a16(uint32_t fetchdat)
{
    pclog("FSTCW_a16.\n");

    FP_ENTER();
    fetch_ea_16(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec)
        seteaw(cpu_state.npxc);
    else {
        writememw(easeg, cpu_state.eaaddr, i387_get_control_word());
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstcw_sw) : (x87_timings.fstcw_sw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstenv) : (x87_concurrency.fstenv * cpu_multi));
    return cpu_state.abrt;
}
#ifndef FPU_8087
static int
opFSTCW_a32(uint32_t fetchdat)
{
    pclog("FSTCW_a32.\n");

    FP_ENTER();
    fetch_ea_32(fetchdat);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_use_dynarec)
        seteaw(cpu_state.npxc);
    else {
        writememw(easeg, cpu_state.eaaddr, i387_get_control_word());
    }
    CLOCK_CYCLES_FPU((fpu_type >= FPU_487SX) ? (x87_timings.fstcw_sw) : (x87_timings.fstcw_sw * cpu_multi));
    CONCURRENCY_CYCLES((fpu_type >= FPU_487SX) ? (x87_concurrency.fstcw_sw) : (x87_concurrency.fstcw_sw * cpu_multi));
    return cpu_state.abrt;
}
#endif

#ifndef FPU_8087
#    define opFCMOV(condition)                                                                      \
        static int opFCMOV##condition(uint32_t fetchdat)                                            \
        {                                                                                           \
            FP_ENTER();                                                                             \
            cpu_state.pc++;                                                                         \
            if (cond_##condition) {                                                                 \
                cpu_state.tag[cpu_state.TOP & 7]  = cpu_state.tag[(cpu_state.TOP + fetchdat) & 7];  \
                cpu_state.MM[cpu_state.TOP & 7].q = cpu_state.MM[(cpu_state.TOP + fetchdat) & 7].q; \
                ST(0)                             = ST(fetchdat & 7);                               \
            }                                                                                       \
            CLOCK_CYCLES_FPU(4);                                                                    \
            return 0;                                                                               \
        }

#    define cond_U  (PF_SET())
#    define cond_NU (!PF_SET())

// clang-format off
opFCMOV(B)
opFCMOV(E)
opFCMOV(BE)
opFCMOV(U)
opFCMOV(NB)
opFCMOV(NE)
opFCMOV(NBE)
opFCMOV(NU)
// clang-format on
#endif
