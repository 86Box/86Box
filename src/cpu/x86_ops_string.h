static int
opMOVSB_a16_ex(UNUSED(uint32_t fetchdat))
{
    uint8_t temp;

    addr64 = addr64_2 = 0x00000000;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, SI, SI);
    high_page = 0;
    do_mmut_rb(cpu_state.ea_seg->base, SI, &addr64);
    if (cpu_state.abrt)
        return 2;

    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, DI, DI);
    high_page = 0;
    do_mmut_wb(es, DI, &addr64_2);
    if (cpu_state.abrt)
        return 1;

    temp = readmemb_n(cpu_state.ea_seg->base, SI, addr64);
    writememb_n(es, DI, addr64_2, temp);

    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 1, 0, 1, 0, 0);
    return 0;
}
static int
opMOVSB_a16(uint32_t fetchdat)
{
    int ret = opMOVSB_a16_ex(fetchdat);

    if (!is386 || (ret == 0)) {
        if (cpu_state.flags & D_FLAG) {
            SI--;
            if (ret < 2)
                DI--;
        } else {
            SI++;
            if (ret < 2)
                DI++;
        }
    }

    return !!ret;
}
static int
opMOVSB_a32(UNUSED(uint32_t fetchdat))
{
    uint8_t temp;

    addr64 = addr64_2 = 0x00000000;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI);
    high_page = 0;
    do_mmut_rb(cpu_state.ea_seg->base, ESI, &addr64);
    if (cpu_state.abrt)
        return 1;

    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, EDI, EDI);
    high_page = 0;
    do_mmut_wb(es, EDI, &addr64_2);
    if (cpu_state.abrt)
        return 1;

    temp = readmemb_n(cpu_state.ea_seg->base, ESI, addr64);
    writememb_n(es, EDI, addr64_2, temp);

    if (cpu_state.flags & D_FLAG) {
        EDI--;
        ESI--;
    } else {
        EDI++;
        ESI++;
    }
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 1, 0, 1, 0, 1);
    return 0;
}

static int
opMOVSW_a16_ex(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;

    addr64a[0] = addr64a[1] = 0x00000000;
    addr64a_2[0] = addr64a_2[1] = 0x00000000;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, SI, SI + 1UL);
    high_page = 0;
    do_mmut_rw(cpu_state.ea_seg->base, SI, addr64a);
    if (cpu_state.abrt)
        return 2;

    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, DI, DI + 1UL);
    high_page = 0;
    do_mmut_ww(es, DI, addr64a_2);
    if (cpu_state.abrt)
        return 1;

    temp = readmemw_n(cpu_state.ea_seg->base, SI, addr64a);
    writememw_n(es, DI, addr64a_2, temp);

    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 1, 0, 1, 0, 0);
    return 0;
}
static int
opMOVSW_a16(uint32_t fetchdat)
{
    int ret = opMOVSW_a16_ex(fetchdat);

    if (!is386 || (ret == 0)) {
        if (cpu_state.flags & D_FLAG) {
            SI -= 2;
            if (ret < 2)
                DI -= 2;
        } else {
            SI += 2;
            if (ret < 2)
                DI += 2;
        }
    }

    return ret;
}
static int
opMOVSW_a32(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;

    addr64a[0] = addr64a[1] = 0x00000000;
    addr64a_2[0] = addr64a_2[1] = 0x00000000;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI + 1UL);
    high_page = 0;
    do_mmut_rw(cpu_state.ea_seg->base, ESI, addr64a);
    if (cpu_state.abrt)
        return 1;

    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, EDI, EDI + 1UL);
    high_page = 0;
    do_mmut_ww(es, EDI, addr64a_2);
    if (cpu_state.abrt)
        return 1;

    temp = readmemw_n(cpu_state.ea_seg->base, ESI, addr64a);
    writememw_n(es, EDI, addr64a_2, temp);

    if (cpu_state.flags & D_FLAG) {
        EDI -= 2;
        ESI -= 2;
    } else {
        EDI += 2;
        ESI += 2;
    }
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 1, 0, 1, 0, 1);
    return 0;
}

static int
opMOVSL_a16(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;

    addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;
    addr64a_2[0] = addr64a_2[1] = addr64a_2[2] = addr64a_2[3] = 0x00000000;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, SI, SI + 3UL);
    high_page = 0;
    do_mmut_rl(cpu_state.ea_seg->base, SI, addr64a);
    if (cpu_state.abrt)
        return 1;

    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, DI, DI + 3UL);
    high_page = 0;
    do_mmut_wl(es, DI, addr64a_2);
    if (cpu_state.abrt)
        return 1;

    temp = readmeml_n(cpu_state.ea_seg->base, SI, addr64a);
    writememl_n(es, DI, addr64a_2, temp);

    if (cpu_state.flags & D_FLAG) {
        DI -= 4;
        SI -= 4;
    } else {
        DI += 4;
        SI += 4;
    }
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 0, 1, 0, 1, 0);
    return 0;
}
static int
opMOVSL_a32(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;

    addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;
    addr64a_2[0] = addr64a_2[1] = addr64a_2[2] = addr64a_2[3] = 0x00000000;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI + 3UL);
    high_page = 0;
    do_mmut_rl(cpu_state.ea_seg->base, ESI, addr64a);
    if (cpu_state.abrt)
        return 1;

    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, EDI, EDI + 3UL);
    high_page = 0;
    do_mmut_wl(es, EDI, addr64a_2);
    if (cpu_state.abrt)
        return 1;

    temp = readmeml_n(cpu_state.ea_seg->base, ESI, addr64a);
    writememl_n(es, EDI, addr64a_2, temp);

    if (cpu_state.flags & D_FLAG) {
        EDI -= 4;
        ESI -= 4;
    } else {
        EDI += 4;
        ESI += 4;
    }
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 0, 1, 0, 1, 1);
    return 0;
}

static int
opCMPSB_a16_ex(UNUSED(uint32_t fetchdat))
{
    uint8_t src;
    uint8_t dst;

    addr64 = addr64_2 = 0x00000000;

    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(&cpu_state.seg_es, DI, DI);
    high_page = 0;
    do_mmut_rb(es, DI, &addr64);
    if (cpu_state.abrt)
        return 2;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, SI, SI);
    high_page = 0;
    do_mmut_rb2(cpu_state.ea_seg->base, SI, &addr64_2);
    if (cpu_state.abrt)
        return 1;

    src = readmemb_n(es, DI, addr64);
    is_compare = 1;
    dst = readmemb_n2(cpu_state.ea_seg->base, SI, addr64_2);
    is_compare = 0;

    setsub8(dst, src);
    CLOCK_CYCLES((is486) ? 8 : 10);
    PREFETCH_RUN((is486) ? 8 : 10, 1, -1, 2, 0, 0, 0, 0);
    return 0;
}
static int
opCMPSB_a16(uint32_t fetchdat)
{
    int ret = opCMPSB_a16_ex(fetchdat);

    if (!is386 || (ret == 0)) {
        if (cpu_state.flags & D_FLAG) {
            DI--;
            if (ret < 2)
                SI--;
        } else {
            DI++;
            if (ret < 2)
                SI++;
        }
    }

    return ret;
}
static int
opCMPSB_a32(UNUSED(uint32_t fetchdat))
{
    uint8_t src;
    uint8_t dst;

    addr64 = addr64_2 = 0x00000000;

    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(&cpu_state.seg_es, EDI, EDI);
    high_page = 0;
    do_mmut_rb(es, EDI, &addr64);
    if (cpu_state.abrt)
        return 1;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI);
    high_page = 0;
    do_mmut_rb2(cpu_state.ea_seg->base, ESI, &addr64_2);
    if (cpu_state.abrt)
        return 1;

    src = readmemb_n(es, EDI, addr64);
    is_compare = 1;
    dst = readmemb_n2(cpu_state.ea_seg->base, ESI, addr64_2);
    is_compare = 0;

    setsub8(dst, src);

    if (cpu_state.flags & D_FLAG) {
        EDI--;
        ESI--;
    } else {
        EDI++;
        ESI++;
    }
    CLOCK_CYCLES((is486) ? 8 : 10);
    PREFETCH_RUN((is486) ? 8 : 10, 1, -1, 2, 0, 0, 0, 1);
    return 0;
}

static int
opCMPSW_a16_ex(UNUSED(uint32_t fetchdat))
{
    uint16_t src;
    uint16_t dst;

    addr64a[0] = addr64a[1] = 0x00000000;
    addr64a_2[0] = addr64a_2[1] = 0x00000000;

    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(&cpu_state.seg_es, DI, DI + 1UL);
    high_page = 0;
    do_mmut_rw(es, DI, addr64a);
    if (cpu_state.abrt)
        return 2;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, SI, SI + 1UL);
    high_page = 0;
    do_mmut_rw2(cpu_state.ea_seg->base, SI, addr64a_2);
    if (cpu_state.abrt)
        return 1;

    src = readmemw_n(es, DI, addr64a);
    is_compare = 1;
    dst = readmemw_n2(cpu_state.ea_seg->base, SI, addr64a_2);
    is_compare = 0;

    setsub16(dst, src);
    CLOCK_CYCLES((is486) ? 8 : 10);
    PREFETCH_RUN((is486) ? 8 : 10, 1, -1, 2, 0, 0, 0, 0);
    return 0;
}
static int
opCMPSW_a16(uint32_t fetchdat)
{
    int ret = opCMPSW_a16_ex(fetchdat);

    if (!is386 || (ret == 0)) {
        if (cpu_state.flags & D_FLAG) {
            DI -= 2;
            if (ret < 2)
                SI -= 2;
        } else {
            DI += 2;
            if (ret < 2)
                SI += 2;
        }
    }

    return ret;
}
static int
opCMPSW_a32(UNUSED(uint32_t fetchdat))
{
    uint16_t src;
    uint16_t dst;

    addr64a[0] = addr64a[1] = 0x00000000;
    addr64a_2[0] = addr64a_2[1] = 0x00000000;

    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(&cpu_state.seg_es, EDI, EDI + 1UL);
    high_page = 0;
    do_mmut_rw(es, EDI, addr64a);
    if (cpu_state.abrt)
        return 1;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI + 1UL);
    high_page = 0;
    do_mmut_rw2(cpu_state.ea_seg->base, ESI, addr64a_2);
    if (cpu_state.abrt)
        return 1;

    src = readmemw_n(es, EDI, addr64a);
    is_compare = 1;
    dst = readmemw_n2(cpu_state.ea_seg->base, ESI, addr64a_2);
    is_compare = 0;

    setsub16(dst, src);
    if (cpu_state.flags & D_FLAG) {
        EDI -= 2;
        ESI -= 2;
    } else {
        EDI += 2;
        ESI += 2;
    }
    CLOCK_CYCLES((is486) ? 8 : 10);
    PREFETCH_RUN((is486) ? 8 : 10, 1, -1, 2, 0, 0, 0, 1);
    return 0;
}

static int
opCMPSL_a16(UNUSED(uint32_t fetchdat))
{
    uint32_t src;
    uint32_t dst;

    addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;
    addr64a_2[0] = addr64a_2[1] = addr64a_2[2] = addr64a_2[3] = 0x00000000;

    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(&cpu_state.seg_es, DI, DI + 3UL);
    high_page = 0;
    do_mmut_rl(es, DI, addr64a);
    if (cpu_state.abrt)
        return 1;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, SI, SI + 3UL);
    high_page = 0;
    do_mmut_rl2(cpu_state.ea_seg->base, SI, addr64a_2);
    if (cpu_state.abrt)
        return 1;

    src = readmeml_n(es, DI, addr64a);
    is_compare = 1;
    dst = readmeml_n2(cpu_state.ea_seg->base, SI, addr64a_2);
    is_compare = 0;

    setsub32(dst, src);
    if (cpu_state.flags & D_FLAG) {
        DI -= 4;
        SI -= 4;
    } else {
        DI += 4;
        SI += 4;
    }
    CLOCK_CYCLES((is486) ? 8 : 10);
    PREFETCH_RUN((is486) ? 8 : 10, 1, -1, 0, 2, 0, 0, 0);
    return 0;
}
static int
opCMPSL_a32(UNUSED(uint32_t fetchdat))
{
    uint32_t src;
    uint32_t dst;

    addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;
    addr64a_2[0] = addr64a_2[1] = addr64a_2[2] = addr64a_2[3] = 0x00000000;

    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(&cpu_state.seg_es, EDI, EDI + 3UL);
    high_page = 0;
    do_mmut_rl(es, EDI, addr64a);
    if (cpu_state.abrt)
        return 1;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI + 3UL);
    high_page = 0;
    do_mmut_rl2(cpu_state.ea_seg->base, ESI, addr64a_2);
    if (cpu_state.abrt)
        return 1;

    src = readmeml_n(es, EDI, addr64a);
    is_compare = 1;
    dst = readmeml_n2(cpu_state.ea_seg->base, ESI, addr64a_2);
    is_compare = 0;

    setsub32(dst, src);
    if (cpu_state.flags & D_FLAG) {
        EDI -= 4;
        ESI -= 4;
    } else {
        EDI += 4;
        ESI += 4;
    }
    CLOCK_CYCLES((is486) ? 8 : 10);
    PREFETCH_RUN((is486) ? 8 : 10, 1, -1, 0, 2, 0, 0, 1);
    return 0;
}

static int
opSTOSB_a16_ex(UNUSED(uint32_t fetchdat))
{
    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, DI, DI);
    writememb(es, DI, AL);
    if (cpu_state.abrt)
        return 1;

    CLOCK_CYCLES(4);
    PREFETCH_RUN(4, 1, -1, 0, 0, 1, 0, 0);
    return 0;
}
static int
opSTOSB_a16(uint32_t fetchdat)
{
    int ret = opSTOSB_a16_ex(fetchdat);

    if (!is386 || (ret == 0)) {
        if (cpu_state.flags & D_FLAG)
            DI--;
        else
            DI++;
    }

    return ret;
}
static int
opSTOSB_a32(UNUSED(uint32_t fetchdat))
{
    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, EDI, EDI);
    writememb(es, EDI, AL);
    if (cpu_state.abrt)
        return 1;

    if (cpu_state.flags & D_FLAG)
        EDI--;
    else
        EDI++;
    CLOCK_CYCLES(4);
    PREFETCH_RUN(4, 1, -1, 0, 0, 1, 0, 1);
    return 0;
}

static int
opSTOSW_a16_ex(UNUSED(uint32_t fetchdat))
{
    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, DI, DI + 1UL);
    writememw(es, DI, AX);
    if (cpu_state.abrt)
        return 1;

    CLOCK_CYCLES(4);
    PREFETCH_RUN(4, 1, -1, 0, 0, 1, 0, 0);
    return 0;
}
static int
opSTOSW_a16(uint32_t fetchdat)
{
    int ret = opSTOSW_a16_ex(fetchdat);

    if (!is386 || (ret == 0)) {
        if (cpu_state.flags & D_FLAG)
            DI -= 2;
        else
            DI += 2;
    }

    return ret;
}
static int
opSTOSW_a32(UNUSED(uint32_t fetchdat))
{
    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, EDI, EDI + 1UL);
    writememw(es, EDI, AX);
    if (cpu_state.abrt)
        return 1;

    if (cpu_state.flags & D_FLAG)
        EDI -= 2;
    else
        EDI += 2;
    CLOCK_CYCLES(4);
    PREFETCH_RUN(4, 1, -1, 0, 0, 1, 0, 1);
    return 0;
}

static int
opSTOSL_a16(UNUSED(uint32_t fetchdat))
{
    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, DI, DI + 3UL);
    writememl(es, DI, EAX);
    if (cpu_state.abrt)
        return 1;

    if (cpu_state.flags & D_FLAG)
        DI -= 4;
    else
        DI += 4;
    CLOCK_CYCLES(4);
    PREFETCH_RUN(4, 1, -1, 0, 0, 0, 1, 0);
    return 0;
}
static int
opSTOSL_a32(UNUSED(uint32_t fetchdat))
{
    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, EDI, EDI + 3UL);
    writememl(es, EDI, EAX);
    if (cpu_state.abrt)
        return 1;
    if (cpu_state.flags & D_FLAG)
        EDI -= 4;
    else
        EDI += 4;
    CLOCK_CYCLES(4);
    PREFETCH_RUN(4, 1, -1, 0, 0, 0, 1, 1);
    return 0;
}

static int
opLODSB_a16_ex(UNUSED(uint32_t fetchdat))
{
    uint8_t temp;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, SI, SI);
    temp = readmemb(cpu_state.ea_seg->base, SI);
    if (cpu_state.abrt)
        return 1;

    AL = temp;
    CLOCK_CYCLES(5);
    PREFETCH_RUN(5, 1, -1, 1, 0, 0, 0, 0);
    return 0;
}
static int
opLODSB_a16(uint32_t fetchdat)
{
    int ret = opLODSB_a16_ex(fetchdat);

    if (!is386 || (ret == 0)) {
        if (cpu_state.flags & D_FLAG)
            SI--;
        else
            SI++;
    }

    return ret;
}
static int
opLODSB_a32(UNUSED(uint32_t fetchdat))
{
    uint8_t temp;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI);
    temp = readmemb(cpu_state.ea_seg->base, ESI);
    if (cpu_state.abrt)
        return 1;

    AL = temp;
    if (cpu_state.flags & D_FLAG)
        ESI--;
    else
        ESI++;
    CLOCK_CYCLES(5);
    PREFETCH_RUN(5, 1, -1, 1, 0, 0, 0, 1);
    return 0;
}

static int
opLODSW_a16_ex(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, SI, SI + 1UL);
    temp = readmemw(cpu_state.ea_seg->base, SI);
    if (cpu_state.abrt)
        return 1;

    AX = temp;
    CLOCK_CYCLES(5);
    PREFETCH_RUN(5, 1, -1, 1, 0, 0, 0, 0);
    return 0;
}
static int
opLODSW_a16(uint32_t fetchdat)
{
    int ret = opLODSW_a16_ex(fetchdat);

    if (!is386 || (ret == 0)) {
        if (cpu_state.flags & D_FLAG)
            SI -= 2;
        else
            SI += 2;
    }

    return ret;
}
static int
opLODSW_a32(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI + 1UL);
    temp = readmemw(cpu_state.ea_seg->base, ESI);
    if (cpu_state.abrt)
        return 1;

    AX = temp;
    if (cpu_state.flags & D_FLAG)
        ESI -= 2;
    else
        ESI += 2;
    CLOCK_CYCLES(5);
    PREFETCH_RUN(5, 1, -1, 1, 0, 0, 0, 1);
    return 0;
}

static int
opLODSL_a16(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, SI, SI + 3UL);
    temp = readmeml(cpu_state.ea_seg->base, SI);
    if (cpu_state.abrt)
        return 1;

    EAX = temp;
    if (cpu_state.flags & D_FLAG)
        SI -= 4;
    else
        SI += 4;
    CLOCK_CYCLES(5);
    PREFETCH_RUN(5, 1, -1, 0, 1, 0, 0, 0);
    return 0;
}
static int
opLODSL_a32(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI + 3UL);
    temp = readmeml(cpu_state.ea_seg->base, ESI);
    if (cpu_state.abrt)
        return 1;

    EAX = temp;
    if (cpu_state.flags & D_FLAG)
        ESI -= 4;
    else
        ESI += 4;
    CLOCK_CYCLES(5);
    PREFETCH_RUN(5, 1, -1, 0, 1, 0, 0, 1);
    return 0;
}

static int
opSCASB_a16_ex(UNUSED(uint32_t fetchdat))
{
    uint8_t temp;

    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(&cpu_state.seg_es, DI, DI);
    temp = readmemb(es, DI);
    if (cpu_state.abrt)
        return 1;

    setsub8(AL, temp);
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 1, 0, 0, 0, 0);
    return 0;
}
static int
opSCASB_a16(uint32_t fetchdat)
{
    int ret = opSCASB_a16_ex(fetchdat);

    if (!is386 || (ret == 0)) {
        if (cpu_state.flags & D_FLAG)
            DI--;
        else
            DI++;
    }

    return ret;
}
static int
opSCASB_a32(UNUSED(uint32_t fetchdat))
{
    uint8_t temp;

    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(&cpu_state.seg_es, EDI, EDI);
    temp = readmemb(es, EDI);
    if (cpu_state.abrt)
        return 1;

    setsub8(AL, temp);
    if (cpu_state.flags & D_FLAG)
        EDI--;
    else
        EDI++;
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 1, 0, 0, 0, 1);
    return 0;
}

static int
opSCASW_a16_ex(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;

    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(&cpu_state.seg_es, DI, DI + 1UL);
    temp = readmemw(es, DI);
    if (cpu_state.abrt)
        return 1;

    setsub16(AX, temp);
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 1, 0, 0, 0, 0);
    return 0;
}
static int
opSCASW_a16(uint32_t fetchdat)
{
    int ret = opSCASW_a16_ex(fetchdat);

    if (!is386 || (ret == 0)) {
        if (cpu_state.flags & D_FLAG)
            DI -= 2;
        else
            DI += 2;
    }

    return ret;
}
static int
opSCASW_a32(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;

    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(&cpu_state.seg_es, EDI, EDI + 1UL);
    temp = readmemw(es, EDI);
    if (cpu_state.abrt)
        return 1;

    setsub16(AX, temp);
    if (cpu_state.flags & D_FLAG)
        EDI -= 2;
    else
        EDI += 2;
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 1, 0, 0, 0, 1);
    return 0;
}

static int
opSCASL_a16(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;

    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(&cpu_state.seg_es, DI, DI + 3UL);
    temp = readmeml(es, DI);
    if (cpu_state.abrt)
        return 1;

    setsub32(EAX, temp);
    if (cpu_state.flags & D_FLAG)
        DI -= 4;
    else
        DI += 4;
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 0, 1, 0, 0, 0);
    return 0;
}
static int
opSCASL_a32(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;

    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(&cpu_state.seg_es, EDI, EDI + 3UL);
    temp = readmeml(es, EDI);
    if (cpu_state.abrt)
        return 1;

    setsub32(EAX, temp);
    if (cpu_state.flags & D_FLAG)
        EDI -= 4;
    else
        EDI += 4;
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 0, 1, 0, 0, 1);
    return 0;
}

static int
opINSB_a16_ex(UNUSED(uint32_t fetchdat))
{
    uint8_t temp;

    addr64 = 0x00000000;

    check_io_perm(DX, 1);

    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, DI, DI);
    high_page = 0;
    do_mmut_wb(es, DI, &addr64);
    if (cpu_state.abrt)
        return 1;

    temp = inb(DX);
    writememb_n(es, DI, addr64, temp);

    CLOCK_CYCLES(15);
    PREFETCH_RUN(15, 1, -1, 1, 0, 1, 0, 0);
    return 0;
}
static int
opINSB_a16(uint32_t fetchdat)
{
    int ret = opINSB_a16_ex(fetchdat);

    if (!is386 || (ret == 0)) {
        if (cpu_state.flags & D_FLAG)
            DI--;
        else
            DI++;
    }

    return ret;
}
static int
opINSB_a32(UNUSED(uint32_t fetchdat))
{
    uint8_t temp;

    addr64 = 0x00000000;

    check_io_perm(DX, 1);

    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, EDI, EDI);
    high_page = 0;
    do_mmut_wb(es, EDI, &addr64);
    if (cpu_state.abrt)
        return 1;

    temp = inb(DX);
    writememb_n(es, EDI, addr64, temp);

    if (cpu_state.flags & D_FLAG)
        EDI--;
    else
        EDI++;
    CLOCK_CYCLES(15);
    PREFETCH_RUN(15, 1, -1, 1, 0, 1, 0, 1);
    return 0;
}

static int
opINSW_a16_ex(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;

    addr64a[0] = addr64a[1] = 0x00000000;

    check_io_perm(DX, 2);

    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, DI, DI + 1UL);
    high_page = 0;
    do_mmut_ww(es, DI, addr64a);
    if (cpu_state.abrt)
        return 1;

    temp = inw(DX);
    writememw_n(es, DI, addr64a, temp);

    CLOCK_CYCLES(15);
    PREFETCH_RUN(15, 1, -1, 1, 0, 1, 0, 0);
    return 0;
}
static int
opINSW_a16(uint32_t fetchdat)
{
    int ret = opINSW_a16_ex(fetchdat);

    if (!is386 || (ret == 0)) {
        if (cpu_state.flags & D_FLAG)
            DI -= 2;
        else
            DI += 2;
    }

    return ret;
}
static int
opINSW_a32(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;

    addr64a[0] = addr64a[1] = 0x00000000;

    check_io_perm(DX, 2);

    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, EDI, EDI + 1UL);
    high_page = 0;
    do_mmut_ww(es, EDI, addr64a);
    if (cpu_state.abrt)
        return 1;

    temp = inw(DX);
    writememw_n(es, EDI, addr64a, temp);

    if (cpu_state.flags & D_FLAG)
        EDI -= 2;
    else
        EDI += 2;
    CLOCK_CYCLES(15);
    PREFETCH_RUN(15, 1, -1, 1, 0, 1, 0, 1);
    return 0;
}

static int
opINSL_a16(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;

    addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;

    check_io_perm(DX, 4);

    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, DI, DI + 3UL);
    high_page = 0;
    do_mmut_wl(es, DI, addr64a);
    if (cpu_state.abrt)
        return 1;

    temp = inl(DX);
    writememl_n(es, DI, addr64a, temp);

    if (cpu_state.flags & D_FLAG)
        DI -= 4;
    else
        DI += 4;
    CLOCK_CYCLES(15);
    PREFETCH_RUN(15, 1, -1, 0, 1, 0, 1, 0);
    return 0;
}
static int
opINSL_a32(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;

    addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;

    check_io_perm(DX, 4);

    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, EDI, EDI + 3UL);
    high_page = 0;
    do_mmut_wl(es, EDI, addr64a);
    if (cpu_state.abrt)
        return 1;

    temp = inl(DX);
    writememl_n(es, EDI, addr64a, temp);

    if (cpu_state.flags & D_FLAG)
        EDI -= 4;
    else
        EDI += 4;
    CLOCK_CYCLES(15);
    PREFETCH_RUN(15, 1, -1, 0, 1, 0, 1, 1);
    return 0;
}

static int
opOUTSB_a16_ex(UNUSED(uint32_t fetchdat))
{
    uint8_t temp;

    addr64 = 0x00000000;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, SI, SI);
    do_mmut_rb(cpu_state.ea_seg->base, SI, &addr64);
    if (cpu_state.abrt)
        return 1;
    check_io_perm(DX, 1);

    temp = readmemb_n(cpu_state.ea_seg->base, SI, addr64);
    outb(DX, temp);

    CLOCK_CYCLES(14);
    PREFETCH_RUN(14, 1, -1, 1, 0, 1, 0, 0);
    return 0;
}
static int
opOUTSB_a16(uint32_t fetchdat)
{
    int ret = opOUTSB_a16_ex(fetchdat);

    if (!is386 || (ret == 0)) {
        if (cpu_state.flags & D_FLAG)
            SI--;
        else
            SI++;
    }

    return ret;
}
static int
opOUTSB_a32(UNUSED(uint32_t fetchdat))
{
    uint8_t temp;

    addr64 = 0x00000000;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI);
    do_mmut_rb(cpu_state.ea_seg->base, ESI, &addr64);
    if (cpu_state.abrt)
        return 1;
    check_io_perm(DX, 1);

    temp = readmemb_n(cpu_state.ea_seg->base, ESI, addr64);
    outb(DX, temp);

    if (cpu_state.flags & D_FLAG)
        ESI--;
    else
        ESI++;
    CLOCK_CYCLES(14);
    PREFETCH_RUN(14, 1, -1, 1, 0, 1, 0, 1);
    return 0;
}

static int
opOUTSW_a16_ex(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;

    addr64a[0] = addr64a[1] = 0x00000000;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, SI, SI + 1UL);
    do_mmut_rw(cpu_state.ea_seg->base, SI, addr64a);
    if (cpu_state.abrt)
        return 1;
    check_io_perm(DX, 2);

    temp = readmemw_n(cpu_state.ea_seg->base, SI, addr64a);
    outw(DX, temp);
    CLOCK_CYCLES(14);
    PREFETCH_RUN(14, 1, -1, 1, 0, 1, 0, 0);
    return 0;
}
static int
opOUTSW_a16(uint32_t fetchdat)
{
    int ret = opOUTSW_a16_ex(fetchdat);

    if (!is386 || (ret == 0)) {
        if (cpu_state.flags & D_FLAG)
            SI -= 2;
        else
            SI += 2;
    }

    return ret;
}
static int
opOUTSW_a32(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;

    addr64a[0] = addr64a[1] = 0x00000000;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI + 1UL);
    do_mmut_rw(cpu_state.ea_seg->base, ESI, addr64a);
    if (cpu_state.abrt)
        return 1;
    check_io_perm(DX, 2);

    temp = readmemw_n(cpu_state.ea_seg->base, ESI, addr64a);
    outw(DX, temp);

    if (cpu_state.flags & D_FLAG)
        ESI -= 2;
    else
        ESI += 2;

    CLOCK_CYCLES(14);
    PREFETCH_RUN(14, 1, -1, 1, 0, 1, 0, 1);
    return 0;
}

static int
opOUTSL_a16(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;

    addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, SI, SI + 3UL);
    do_mmut_rl(cpu_state.ea_seg->base, SI, addr64a);
    if (cpu_state.abrt)
        return 1;
    check_io_perm(DX, 4);

    temp = readmeml_n(cpu_state.ea_seg->base, SI, addr64a);
    outl(DX, temp);

    if (cpu_state.flags & D_FLAG)
        SI -= 4;
    else
        SI += 4;
    CLOCK_CYCLES(14);
    PREFETCH_RUN(14, 1, -1, 0, 1, 0, 1, 0);
    return 0;
}
static int
opOUTSL_a32(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;

    addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI + 3UL);
    do_mmut_rl(cpu_state.ea_seg->base, ESI, addr64a);
    if (cpu_state.abrt)
        return 1;
    check_io_perm(DX, 4);

    temp = readmeml_n(cpu_state.ea_seg->base, ESI, addr64a);
    outl(DX, temp);

    if (cpu_state.flags & D_FLAG)
        ESI -= 4;
    else
        ESI += 4;
    CLOCK_CYCLES(14);
    PREFETCH_RUN(14, 1, -1, 0, 1, 0, 1, 1);
    return 0;
}
