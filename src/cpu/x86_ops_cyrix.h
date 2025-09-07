/*Cyrix-only instructions*/
/*System Management Mode*/
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
    uint8_t ins_check = ((ccr1 & (CCR1_USE_SMI | CCR1_SM3)) ==
                         (CCR1_USE_SMI | CCR1_SM3)) &&
                        ((ccr1 & CCR1_SMAC) || in_smm) &&
                        (cyrix.arr[3].size > 0) &&
                        (CPL == 0);

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
    uint8_t ins_check = ((ccr1 & (CCR1_USE_SMI | CCR1_SM3)) ==
                         (CCR1_USE_SMI | CCR1_SM3)) &&
                        ((ccr1 & CCR1_SMAC) || in_smm) &&
                        (cyrix.arr[3].size > 0) &&
                        (CPL == 0);

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
    uint8_t ins_check = ((ccr1 & (CCR1_USE_SMI | CCR1_SM3)) ==
                         (CCR1_USE_SMI | CCR1_SM3)) &&
                        ((ccr1 & CCR1_SMAC) || in_smm) &&
                        (cyrix.arr[3].size > 0) &&
                        (CPL == 0);

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
    uint8_t ins_check = ((ccr1 & (CCR1_USE_SMI | CCR1_SM3)) ==
                         (CCR1_USE_SMI | CCR1_SM3)) &&
                        ((ccr1 & CCR1_SMAC) || in_smm) &&
                        (cyrix.arr[3].size > 0) &&
                        (CPL == 0);

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
    uint8_t ins_check = ((ccr1 & (CCR1_USE_SMI | CCR1_SM3)) ==
                         (CCR1_USE_SMI | CCR1_SM3)) &&
                        ((ccr1 & CCR1_SMAC) || in_smm) &&
                        (cyrix.arr[3].size > 0) &&
                        (CPL == 0);

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
    uint8_t ins_check = ((ccr1 & (CCR1_USE_SMI | CCR1_SM3)) ==
                         (CCR1_USE_SMI | CCR1_SM3)) &&
                        ((ccr1 & CCR1_SMAC) || in_smm) &&
                        (cyrix.arr[3].size > 0) &&
                        (CPL == 0);

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
    uint8_t ins_check = ((ccr1 & (CCR1_USE_SMI | CCR1_SM3)) ==
                         (CCR1_USE_SMI | CCR1_SM3)) &&
                        ((ccr1 & CCR1_SMAC) || in_smm) &&
                        (cyrix.arr[3].size > 0) &&
                        (CPL == 0);

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
    uint8_t ins_check = ((ccr1 & (CCR1_USE_SMI | CCR1_SM3)) ==
                         (CCR1_USE_SMI | CCR1_SM3)) &&
                        ((ccr1 & CCR1_SMAC) || in_smm) &&
                        (cyrix.arr[3].size > 0) &&
                        (CPL == 0);

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
    uint8_t ins_check = ((ccr1 & (CCR1_USE_SMI | CCR1_SM3)) ==
                         (CCR1_USE_SMI | CCR1_SM3)) &&
                        ((ccr1 & CCR1_SMAC) || in_smm) &&
                        (cyrix.arr[3].size > 0) &&
                        (CPL == 0);

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
    uint8_t ins_check = ((ccr1 & (CCR1_USE_SMI | CCR1_SM3)) ==
                         (CCR1_USE_SMI | CCR1_SM3)) &&
                        ((ccr1 & CCR1_SMAC) || in_smm) &&
                        (cyrix.arr[3].size > 0) &&
                        (CPL == 0);

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
    uint8_t ins_check = ((ccr1 & (CCR1_USE_SMI | CCR1_SM3)) ==
                         (CCR1_USE_SMI | CCR1_SM3)) &&
                        ((ccr1 & CCR1_SMAC) || in_smm) &&
                        (cyrix.arr[3].size > 0) &&
                        (CPL == 0);

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
    uint8_t ins_check = ((ccr1 & (CCR1_USE_SMI | CCR1_SM3)) ==
                         (CCR1_USE_SMI | CCR1_SM3)) &&
                        ((ccr1 & CCR1_SMAC) || in_smm) &&
                        (cyrix.arr[3].size > 0) &&
                        (CPL == 0);

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
    uint8_t ccr1_check = ((ccr1 & (CCR1_USE_SMI | CCR1_SMAC | CCR1_SM3)) ==
                          (CCR1_USE_SMI | CCR1_SMAC | CCR1_SM3)) &&
                          (cyrix.arr[3].size > 0);

    if (in_smm)
        fatal("opSMINT\n");
    else if (ccr1_check) {
        is_smint = 1;
        enter_smm(0);
    }

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
