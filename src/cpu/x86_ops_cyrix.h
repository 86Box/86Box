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
    if (in_smm) {
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
    if (in_smm) {
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
            break;
        case 0x18: /*DS*/
            cyrix_load_seg_descriptor(easeg + cpu_state.eaaddr, &cpu_state.seg_ds);
            break;
        case 0x10: /*SS*/
            cyrix_load_seg_descriptor(easeg + cpu_state.eaaddr, &cpu_state.seg_ss);
            break;
        case 0x20: /*FS*/
            cyrix_load_seg_descriptor(easeg + cpu_state.eaaddr, &cpu_state.seg_fs);
            break;
        case 0x28: /*GS*/
            cyrix_load_seg_descriptor(easeg + cpu_state.eaaddr, &cpu_state.seg_gs);
            break;
        default:
            x86illegal();
    }
}
static int
opRSDC_a16(uint32_t fetchdat)
{
    if (in_smm) {
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
    if (in_smm) {
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
    if (in_smm) {
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
    if (in_smm) {
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
    if (in_smm) {
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
    if (in_smm) {
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
    if (in_smm) {
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
    if (in_smm) {
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
    if (in_smm) {
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
    if (in_smm) {
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
    if (in_smm)
        fatal("opSMINT\n");
    else
        x86illegal();

    return 1;
}

static int
opRDSHR_a16(UNUSED(uint32_t fetchdat))
{
    if (in_smm)
        fatal("opRDSHR_a16\n");
    else
        x86illegal();

    return 1;
}
static int
opRDSHR_a32(UNUSED(uint32_t fetchdat))
{
    if (in_smm)
        fatal("opRDSHR_a32\n");
    else
        x86illegal();

    return 1;
}

static int
opWRSHR_a16(UNUSED(uint32_t fetchdat))
{
    if (in_smm)
        fatal("opWRSHR_a16\n");
    else
        x86illegal();

    return 1;
}
static int
opWRSHR_a32(UNUSED(uint32_t fetchdat))
{
    if (in_smm)
        fatal("opWRSHR_a32\n");
    else
        x86illegal();

    return 1;
}
