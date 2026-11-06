/*Cyrix-only instructions*/
/*System Management Mode*/

/* SVDC, RSDC, SVLDT, RSLDT, SVTS and RSTS: with SMM set up (USE_SMI, SM3
   on the 6x86, an SMM region), at CPL 0, in SMM or with SMAC set (MediaGX
   data book, 3.11.6). The MediaGX's SMM code has them whatever CCR1 holds:
   VSA clears USE_SMI as its handler starts, and goes on with RSDC. */
static int
cyrix_smm_insn_check(void)
{
    if (is_gxm && in_smm)
        return 1;

    if (is_cx6x86)
        return ((ccr1 & (CCR1_USE_SMI | CCR1_SM3)) == (CCR1_USE_SMI | CCR1_SM3)) &&
               ((ccr1 & CCR1_SMAC) || in_smm) && (cyrix.arr[3].size > 0) && (CPL == 0);

    return (ccr1 & CCR1_USE_SMI) && ((ccr1 & CCR1_SMAC) || in_smm) && (cyrix.arr[3].size > 0) && (CPL == 0);
}
static void
opSVDC_common(uint32_t fetchdat)
{
    switch (rmdat & 0x38) {
        case 0x00: /*ES*/
            cyrix_write_seg_descriptor(easeg + cpu_state.eaaddr, &cpu_state.seg_es);
            writememw(0, easeg + cpu_state.eaaddr + 8, ES);
            break;
        case 0x08: /*CS*/
            cyrix_write_seg_descriptor(easeg + cpu_state.eaaddr, &cpu_state.seg_cs);
            writememw(0, easeg + cpu_state.eaaddr + 8, CS);
            break;
        case 0x18: /*DS*/
            cyrix_write_seg_descriptor(easeg + cpu_state.eaaddr, &cpu_state.seg_ds);
            writememw(0, easeg + cpu_state.eaaddr + 8, DS);
            break;
        case 0x10: /*SS*/
            cyrix_write_seg_descriptor(easeg + cpu_state.eaaddr, &cpu_state.seg_ss);
            writememw(0, easeg + cpu_state.eaaddr + 8, SS);
            break;
        case 0x20: /*FS*/
            cyrix_write_seg_descriptor(easeg + cpu_state.eaaddr, &cpu_state.seg_fs);
            writememw(0, easeg + cpu_state.eaaddr + 8, FS);
            break;
        case 0x28: /*GS*/
            cyrix_write_seg_descriptor(easeg + cpu_state.eaaddr, &cpu_state.seg_gs);
            writememw(0, easeg + cpu_state.eaaddr + 8, GS);
            break;
        default:
            x86illegal();
    }
}
static int
opSVDC_a16(uint32_t fetchdat)
{
    uint8_t ins_check;

    ins_check = cyrix_smm_insn_check();

    if (ins_check) {
        fetch_ea_16(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        opSVDC_common(fetchdat);
    } else
        x86illegal();

    return cpu_state.abrt;
}
static int
opSVDC_a32(uint32_t fetchdat)
{
    uint8_t ins_check;

    ins_check = cyrix_smm_insn_check();

    if (ins_check) {
        fetch_ea_32(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        opSVDC_common(fetchdat);
    } else
        x86illegal();

    return cpu_state.abrt;
}

static void
opRSDC_common(uint32_t fetchdat)
{
    switch (rmdat & 0x38) {
        case 0x00: /*ES*/
            cyrix_load_seg_descriptor(easeg + cpu_state.eaaddr, &cpu_state.seg_es);
            ES = readmemw(0, easeg + cpu_state.eaaddr + 8);
            break;
        case 0x18: /*DS*/
            cyrix_load_seg_descriptor(easeg + cpu_state.eaaddr, &cpu_state.seg_ds);
            DS = readmemw(0, easeg + cpu_state.eaaddr + 8);
            break;
        case 0x10: /*SS*/
            cyrix_load_seg_descriptor(easeg + cpu_state.eaaddr, &cpu_state.seg_ss);
            SS = readmemw(0, easeg + cpu_state.eaaddr + 8);
            break;
        case 0x20: /*FS*/
            cyrix_load_seg_descriptor(easeg + cpu_state.eaaddr, &cpu_state.seg_fs);
            FS = readmemw(0, easeg + cpu_state.eaaddr + 8);
            break;
        case 0x28: /*GS*/
            cyrix_load_seg_descriptor(easeg + cpu_state.eaaddr, &cpu_state.seg_gs);
            GS = readmemw(0, easeg + cpu_state.eaaddr + 8);
            break;
        default:
            x86illegal();
    }
}
static int
opRSDC_a16(uint32_t fetchdat)
{
    uint8_t ins_check;

    ins_check = cyrix_smm_insn_check();

    if (ins_check) {
        fetch_ea_16(fetchdat);
        SEG_CHECK_READ(cpu_state.ea_seg);
        opRSDC_common(fetchdat);
    } else
        x86illegal();

    return cpu_state.abrt;
}
static int
opRSDC_a32(uint32_t fetchdat)
{
    uint8_t ins_check;

    ins_check = cyrix_smm_insn_check();

    if (ins_check) {
        fetch_ea_32(fetchdat);
        SEG_CHECK_READ(cpu_state.ea_seg);
        opRSDC_common(fetchdat);
    } else
        x86illegal();

    return cpu_state.abrt;
}

static int
opSVLDT_a16(uint32_t fetchdat)
{
    uint8_t ins_check;

    ins_check = cyrix_smm_insn_check();

    if (ins_check) {
        fetch_ea_16(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        cyrix_write_seg_descriptor(easeg + cpu_state.eaaddr, &ldt);
        writememw(0, easeg + cpu_state.eaaddr + 8, ldt.seg);
    } else
        x86illegal();

    return cpu_state.abrt;
}
static int
opSVLDT_a32(uint32_t fetchdat)
{
    uint8_t ins_check;

    ins_check = cyrix_smm_insn_check();

    if (ins_check) {
        fetch_ea_32(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        cyrix_write_seg_descriptor(easeg + cpu_state.eaaddr, &ldt);
        writememw(0, easeg + cpu_state.eaaddr + 8, ldt.seg);
    } else
        x86illegal();

    return cpu_state.abrt;
}

static int
opRSLDT_a16(uint32_t fetchdat)
{
    uint8_t ins_check;

    ins_check = cyrix_smm_insn_check();

    if (ins_check) {
        fetch_ea_16(fetchdat);
        SEG_CHECK_READ(cpu_state.ea_seg);
        cyrix_load_seg_descriptor(easeg + cpu_state.eaaddr, &ldt);
    } else
        x86illegal();

    return cpu_state.abrt;
}
static int
opRSLDT_a32(uint32_t fetchdat)
{
    uint8_t ins_check;

    ins_check = cyrix_smm_insn_check();

    if (ins_check) {
        fetch_ea_32(fetchdat);
        SEG_CHECK_READ(cpu_state.ea_seg);
        cyrix_load_seg_descriptor(easeg + cpu_state.eaaddr, &ldt);
    } else
        x86illegal();

    return cpu_state.abrt;
}

static int
opSVTS_a16(uint32_t fetchdat)
{
    uint8_t ins_check;

    ins_check = cyrix_smm_insn_check();

    if (ins_check) {
        fetch_ea_16(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        cyrix_write_seg_descriptor(easeg + cpu_state.eaaddr, &tr);
        writememw(0, easeg + cpu_state.eaaddr + 8, tr.seg);
    } else
        x86illegal();

    return cpu_state.abrt;
}
static int
opSVTS_a32(uint32_t fetchdat)
{
    uint8_t ins_check;

    ins_check = cyrix_smm_insn_check();

    if (ins_check) {
        fetch_ea_32(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        cyrix_write_seg_descriptor(easeg + cpu_state.eaaddr, &tr);
        writememw(0, easeg + cpu_state.eaaddr + 8, tr.seg);
    } else
        x86illegal();

    return cpu_state.abrt;
}

static int
opRSTS_a16(uint32_t fetchdat)
{
    uint8_t ins_check;

    ins_check = cyrix_smm_insn_check();

    if (ins_check) {
        fetch_ea_16(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        cyrix_write_seg_descriptor(easeg + cpu_state.eaaddr, &tr);
        writememw(0, easeg + cpu_state.eaaddr + 8, tr.seg);
    } else
        x86illegal();

    return cpu_state.abrt;
}
static int
opRSTS_a32(uint32_t fetchdat)
{
    uint8_t ins_check;

    ins_check = cyrix_smm_insn_check();

    if (ins_check) {
        fetch_ea_32(fetchdat);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        cyrix_write_seg_descriptor(easeg + cpu_state.eaaddr, &tr);
        writememw(0, easeg + cpu_state.eaaddr + 8, tr.seg);
    } else
        x86illegal();

    return cpu_state.abrt;
}

static int
opSMINT(UNUSED(uint32_t fetchdat))
{
    uint8_t ccr1_check;

    if (is_cx6x86)
        ccr1_check = ((ccr1 & (CCR1_USE_SMI | CCR1_SMAC | CCR1_SM3)) ==
                      (CCR1_USE_SMI | CCR1_SMAC | CCR1_SM3)) &&
                      (cyrix.arr[3].size > 0);
    else
        ccr1_check = ((ccr1 & (CCR1_USE_SMI | CCR1_SMAC)) ==
                      (CCR1_USE_SMI | CCR1_SMAC)) &&
                      (cyrix.arr[3].size > 0);

    /* Invalid opcode unless SMM is set up for it, at CPL 0, outside SMM
       (Cyrix SMM Programmer's Guide 2.5 and table C-1) */
    ILLEGAL_ON(in_smm || CPL || !ccr1_check);

    is_smint = 1;
    enter_smm(0);

    return 1;
}

static int
opRDSHR_a16(UNUSED(uint32_t fetchdat))
{
    uint8_t ins_check = ((ccr1 & (CCR1_USE_SMI | CCR1_SM3)) ==
                         (CCR1_USE_SMI | CCR1_SM3)) &&
                        ((ccr1 & CCR1_SMAC) || in_smm) &&
                        (cyrix.arr[3].size > 0) &&
                        (CPL == 0);

    if (ins_check) {
        fetch_ea_16(fetchdat);
        if (cpu_mod == 3) {
            cpu_state.regs[cpu_rm].l = cyrix.smhr;
            CLOCK_CYCLES(timing_rr);
            PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 0);
        } else {
            SEG_CHECK_WRITE(cpu_state.ea_seg);
            seteal(cyrix.smhr);
            CLOCK_CYCLES(is486 ? 1 : 2);
            PREFETCH_RUN(2, 2, rmdat, 0, 0, 0, 1, 0);
        }
        return cpu_state.abrt;
    } else
        x86illegal();

    return 1;
}
static int
opRDSHR_a32(UNUSED(uint32_t fetchdat))
{
    uint8_t ins_check = ((ccr1 & (CCR1_USE_SMI | CCR1_SM3)) ==
                         (CCR1_USE_SMI | CCR1_SM3)) &&
                        ((ccr1 & CCR1_SMAC) || in_smm) &&
                        (cyrix.arr[3].size > 0) &&
                        (CPL == 0);

    if (ins_check) {
        fetch_ea_32(fetchdat);
        if (cpu_mod == 3) {
            cpu_state.regs[cpu_rm].l = cyrix.smhr;
            CLOCK_CYCLES(timing_rr);
            PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 1);
        } else {
            SEG_CHECK_WRITE(cpu_state.ea_seg);
            seteal(cyrix.smhr);
            CLOCK_CYCLES(is486 ? 1 : 2);
            PREFETCH_RUN(2, 2, rmdat, 0, 0, 0, 1, 1);
        }
        return cpu_state.abrt;
    } else
        x86illegal();

    return 1;
}

static int
opWRSHR_a16(uint32_t fetchdat)
{
    uint8_t ins_check = ((ccr1 & (CCR1_USE_SMI | CCR1_SM3)) ==
                         (CCR1_USE_SMI | CCR1_SM3)) &&
                        ((ccr1 & CCR1_SMAC) || in_smm) &&
                        (cyrix.arr[3].size > 0) &&
                        (CPL == 0);

    if (ins_check) {
        fetch_ea_16(fetchdat);
        if (cpu_mod == 3) {
            cyrix.smhr = cpu_state.regs[cpu_rm].l;
            CLOCK_CYCLES(timing_rr);
            PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 0);
        } else {
            uint32_t temp;
            SEG_CHECK_READ(cpu_state.ea_seg);
            CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3);
            temp = geteal();
            if (cpu_state.abrt)
                return 1;
            cyrix.smhr = temp;
            CLOCK_CYCLES(is486 ? 1 : 4);
            PREFETCH_RUN(4, 2, rmdat, 0, 1, 0, 0, 0);
        }
        return 0;
    } else
        x86illegal();

    return 1;
}
static int
opWRSHR_a32(uint32_t fetchdat)
{
    uint8_t ins_check = ((ccr1 & (CCR1_USE_SMI | CCR1_SM3)) ==
                         (CCR1_USE_SMI | CCR1_SM3)) &&
                        ((ccr1 & CCR1_SMAC) || in_smm) &&
                        (cyrix.arr[3].size > 0) &&
                        (CPL == 0);

    if (ins_check) {
        fetch_ea_32(fetchdat);
        if (cpu_mod == 3) {
            cyrix.smhr = cpu_state.regs[cpu_rm].l;
            CLOCK_CYCLES(timing_rr);
            PREFETCH_RUN(timing_rr, 2, rmdat, 0, 0, 0, 0, 1);
        } else {
            uint32_t temp;
            SEG_CHECK_READ(cpu_state.ea_seg);
            CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3);
            temp = geteal();
            if (cpu_state.abrt)
                return 1;
            cyrix.smhr = temp;
            CLOCK_CYCLES(is486 ? 1 : 4);
            PREFETCH_RUN(4, 2, rmdat, 0, 1, 0, 0, 1);
        }
        return 0;
    } else
        x86illegal();

    return 1;
}

/*MediaGX display driver instructions (MediaGX MMX data book, 4.1.5 and 4.1.6),
  #UD while no part of the cache is scratchpad (GCR[3:2] = 00b). The data book's
  instruction table has CPU_READ and CPU_WRITE the other way round; its 4.1.5
  agrees with VSA, which writes with 0F 3C and reads with 0F 3D.*/
static uint32_t *
gxm_cpu_reg(uint32_t addr)
{
    switch (addr) {
        case 0xffffff0c:
            return &gxm.bb_base[0];
        case 0xffffff1c:
            return &gxm.bb_base[1];
        case 0xffffff2c:
            return &gxm.bb_ptr[0];
        case 0xffffff3c:
            return &gxm.bb_ptr[1];
        case 0xffffff6c:
            return &gxm.pm_base;
        case 0xffffff7c:
            return &gxm.pm_mask;

        default:
            return NULL;
    }
}

static int
opBB0_RESET(UNUSED(uint32_t fetchdat))
{
    ILLEGAL_ON(!(gxm.gcr & GCR_SP_SIZE));

    gxm.bb_ptr[0] = gxm.bb_base[0] & 0x0ff0;
    CLOCK_CYCLES(2);
    return 0;
}

static int
opBB1_RESET(UNUSED(uint32_t fetchdat))
{
    ILLEGAL_ON(!(gxm.gcr & GCR_SP_SIZE));

    gxm.bb_ptr[1] = gxm.bb_base[1] & 0x0ff0;
    CLOCK_CYCLES(2);
    return 0;
}

static int
opCPU_WRITE(UNUSED(uint32_t fetchdat))
{
    uint32_t *reg;

    ILLEGAL_ON(!(gxm.gcr & GCR_SP_SIZE));

    reg = gxm_cpu_reg(EBX);
    switch (EBX) {
        case 0xffffff0c:
        case 0xffffff1c:
            *reg = EAX & 0x0fff;
            break;
        case 0xffffff2c:
        case 0xffffff3c:
            *reg = EAX & 0x0ff0;
            break;
        case 0xffffff6c:
            *reg = EAX & 0x0ffffffc;
            break;
        case 0xffffff7c:
            *reg = EAX & 0x0fffffff;
            break;

        default:
            break;
    }
    CLOCK_CYCLES(1);
    return 0;
}

static int
opCPU_READ(UNUSED(uint32_t fetchdat))
{
    const uint32_t *reg;

    ILLEGAL_ON(!(gxm.gcr & GCR_SP_SIZE));

    reg = gxm_cpu_reg(EBX);
    EAX = reg ? *reg : 0x00000000;
    CLOCK_CYCLES(1);
    return 0;
}
