static int opMOVSB_a16(uint32_t fetchdat)
{
        uint8_t temp;
        uint64_t addr64r;
        uint64_t addr64w;

        SEG_CHECK_READ(cpu_state.ea_seg);
        do_mmut_rb(cpu_state.ea_seg->base, SI, &addr64r);
        if (cpu_state.abrt) return 1;
        SEG_CHECK_WRITE(&cpu_state.seg_es);
        do_mmut_wb(es, DI, &addr64w);
        if (cpu_state.abrt) return 1;
        temp = readmemb_n(cpu_state.ea_seg->base, SI, addr64r); if (cpu_state.abrt) return 1;
        writememb_n(es, DI, addr64w, temp);                if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) { DI--; SI--; }
        else                          { DI++; SI++; }
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 1, -1, 1,0,1,0, 0);
        return 0;
}
static int opMOVSB_a32(uint32_t fetchdat)
{
        uint8_t temp;
        uint64_t addr64r;
        uint64_t addr64w;

        SEG_CHECK_READ(cpu_state.ea_seg);
        do_mmut_rb(cpu_state.ea_seg->base, ESI, &addr64r);
        if (cpu_state.abrt) return 1;
        SEG_CHECK_WRITE(&cpu_state.seg_es);
        do_mmut_wb(es, EDI, &addr64w);
        if (cpu_state.abrt) return 1;
        temp = readmemb_n(cpu_state.ea_seg->base, ESI, addr64r); if (cpu_state.abrt) return 1;
        writememb_n(es, EDI, addr64w, temp);               if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) { EDI--; ESI--; }
        else                          { EDI++; ESI++; }
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 1, -1, 1,0,1,0, 1);
        return 0;
}

static int opMOVSW_a16(uint32_t fetchdat)
{
        uint16_t temp;
        uint64_t addr64r[2];
        uint64_t addr64w[2];

        SEG_CHECK_READ(cpu_state.ea_seg);
        do_mmut_rw(cpu_state.ea_seg->base, SI, addr64r);
        if (cpu_state.abrt) return 1;
        SEG_CHECK_WRITE(&cpu_state.seg_es);
        do_mmut_ww(es, DI, addr64w);
        if (cpu_state.abrt) return 1;
        temp = readmemw_n(cpu_state.ea_seg->base, SI, addr64r); if (cpu_state.abrt) return 1;
        writememw_n(es, DI, addr64w, temp);                if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) { DI -= 2; SI -= 2; }
        else                          { DI += 2; SI += 2; }
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 1, -1, 1,0,1,0, 0);
        return 0;
}
static int opMOVSW_a32(uint32_t fetchdat)
{
        uint16_t temp;
        uint64_t addr64r[2];
        uint64_t addr64w[2];

        SEG_CHECK_READ(cpu_state.ea_seg);
        do_mmut_rw(cpu_state.ea_seg->base, ESI, addr64r);
        if (cpu_state.abrt) return 1;
        SEG_CHECK_WRITE(&cpu_state.seg_es);
        do_mmut_ww(es, EDI, addr64w);
        if (cpu_state.abrt) return 1;
        temp = readmemw_n(cpu_state.ea_seg->base, ESI, addr64r); if (cpu_state.abrt) return 1;
        writememw_n(es, EDI, addr64w, temp);               if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) { EDI -= 2; ESI -= 2; }
        else                          { EDI += 2; ESI += 2; }
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 1, -1, 1,0,1,0, 1);
        return 0;
}

static int opMOVSL_a16(uint32_t fetchdat)
{
        uint32_t temp;
        uint64_t addr64r[4];
        uint64_t addr64w[4];

        SEG_CHECK_READ(cpu_state.ea_seg);
        do_mmut_rl(cpu_state.ea_seg->base, SI, addr64r);
        if (cpu_state.abrt) return 1;
        SEG_CHECK_WRITE(&cpu_state.seg_es);
        do_mmut_wl(es, DI, addr64w);
        if (cpu_state.abrt) return 1;
        temp = readmeml_n(cpu_state.ea_seg->base, SI, addr64r); if (cpu_state.abrt) return 1;
        writememl_n(es, DI, addr64w, temp);                if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) { DI -= 4; SI -= 4; }
        else                          { DI += 4; SI += 4; }
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 1, -1, 0,1,0,1, 0);
        return 0;
}
static int opMOVSL_a32(uint32_t fetchdat)
{
        uint32_t temp;
        uint64_t addr64r[4];
        uint64_t addr64w[4];

        SEG_CHECK_READ(cpu_state.ea_seg);
        do_mmut_rl(cpu_state.ea_seg->base, ESI, addr64r);
        if (cpu_state.abrt) return 1;
        SEG_CHECK_WRITE(&cpu_state.seg_es);
        do_mmut_wl(es, EDI, addr64w);
        if (cpu_state.abrt) return 1;
        temp = readmeml_n(cpu_state.ea_seg->base, ESI, addr64r); if (cpu_state.abrt) return 1;
        writememl_n(es, EDI, addr64w, temp);               if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) { EDI -= 4; ESI -= 4; }
        else                          { EDI += 4; ESI += 4; }
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 1, -1, 0,1,0,1, 1);
        return 0;
}


static int opCMPSB_a16(uint32_t fetchdat)
{
        uint8_t src, dst;
        uint64_t addr64;
        uint64_t addr642;

        SEG_CHECK_READ(cpu_state.ea_seg);
        do_mmut_rb(cpu_state.ea_seg->base, SI, &addr64);
        if (cpu_state.abrt) return 1;
        SEG_CHECK_READ(&cpu_state.seg_es);
        do_mmut_rb(es, DI, &addr642);
        if (cpu_state.abrt) return 1;
        src = readmemb_n(cpu_state.ea_seg->base, SI, addr64);
        dst = readmemb_n(es, DI, addr642);         if (cpu_state.abrt) return 1;
        setsub8(src, dst);
        if (cpu_state.flags & D_FLAG) { DI--; SI--; }
        else                          { DI++; SI++; }
        CLOCK_CYCLES((is486) ? 8 : 10);
        PREFETCH_RUN((is486) ? 8 : 10, 1, -1, 2,0,0,0, 0);
        return 0;
}
static int opCMPSB_a32(uint32_t fetchdat)
{
        uint8_t src, dst;
        uint64_t addr64;
        uint64_t addr642;

        SEG_CHECK_READ(cpu_state.ea_seg);
        do_mmut_rb(cpu_state.ea_seg->base, ESI, &addr64);
        if (cpu_state.abrt) return 1;
        SEG_CHECK_READ(&cpu_state.seg_es);
        do_mmut_rb(es, EDI, &addr642);
        if (cpu_state.abrt) return 1;
        src = readmemb_n(cpu_state.ea_seg->base, ESI, addr64);
        dst = readmemb_n(es, EDI, addr642);        if (cpu_state.abrt) return 1;
        setsub8(src, dst);
        if (cpu_state.flags & D_FLAG) { EDI--; ESI--; }
        else                          { EDI++; ESI++; }
        CLOCK_CYCLES((is486) ? 8 : 10);
        PREFETCH_RUN((is486) ? 8 : 10, 1, -1, 2,0,0,0, 1);
        return 0;
}

static int opCMPSW_a16(uint32_t fetchdat)
{
        uint16_t src, dst;
        uint64_t addr64[2];
        uint64_t addr642[2];

        SEG_CHECK_READ(cpu_state.ea_seg);
        do_mmut_rw(cpu_state.ea_seg->base, SI, addr64);
        if (cpu_state.abrt) return 1;
        SEG_CHECK_READ(&cpu_state.seg_es);
        do_mmut_rw(es, DI, addr642);
        if (cpu_state.abrt) return 1;
        src = readmemw_n(cpu_state.ea_seg->base, SI, addr64);
        dst = readmemw_n(es, DI, addr642);        if (cpu_state.abrt) return 1;
        setsub16(src, dst);
        if (cpu_state.flags & D_FLAG) { DI -= 2; SI -= 2; }
        else                          { DI += 2; SI += 2; }
        CLOCK_CYCLES((is486) ? 8 : 10);
        PREFETCH_RUN((is486) ? 8 : 10, 1, -1, 2,0,0,0, 0);
        return 0;
}
static int opCMPSW_a32(uint32_t fetchdat)
{
        uint16_t src, dst;
        uint64_t addr64[2];
        uint64_t addr642[2];

        SEG_CHECK_READ(cpu_state.ea_seg);
        do_mmut_rw(cpu_state.ea_seg->base, ESI, addr64);
        if (cpu_state.abrt) return 1;
        SEG_CHECK_READ(&cpu_state.seg_es);
        do_mmut_rw(es, EDI, addr642);
        if (cpu_state.abrt) return 1;
        src = readmemw_n(cpu_state.ea_seg->base, ESI, addr64);
        dst = readmemw_n(es, EDI, addr642);        if (cpu_state.abrt) return 1;
        setsub16(src, dst);
        if (cpu_state.flags & D_FLAG) { EDI -= 2; ESI -= 2; }
        else                          { EDI += 2; ESI += 2; }
        CLOCK_CYCLES((is486) ? 8 : 10);
        PREFETCH_RUN((is486) ? 8 : 10, 1, -1, 2,0,0,0, 1);
        return 0;
}

static int opCMPSL_a16(uint32_t fetchdat)
{
        uint32_t src, dst;
        uint64_t addr64[4];
        uint64_t addr642[4];

        SEG_CHECK_READ(cpu_state.ea_seg);
        do_mmut_rl(cpu_state.ea_seg->base, SI, addr64);
        if (cpu_state.abrt) return 1;
        SEG_CHECK_READ(&cpu_state.seg_es);
        do_mmut_rl(es, DI, addr642);
        if (cpu_state.abrt) return 1;
        src = readmeml_n(cpu_state.ea_seg->base, SI, addr64);
        dst = readmeml_n(es, DI, addr642);        if (cpu_state.abrt) return 1;
        setsub32(src, dst);
        if (cpu_state.flags & D_FLAG) { DI -= 4; SI -= 4; }
        else                          { DI += 4; SI += 4; }
        CLOCK_CYCLES((is486) ? 8 : 10);
        PREFETCH_RUN((is486) ? 8 : 10, 1, -1, 0,2,0,0, 0);
        return 0;
}
static int opCMPSL_a32(uint32_t fetchdat)
{
        uint32_t src, dst;
        uint64_t addr64[4];
        uint64_t addr642[4];

        SEG_CHECK_READ(cpu_state.ea_seg);
        do_mmut_rl(cpu_state.ea_seg->base, ESI, addr64);
        if (cpu_state.abrt) return 1;
        SEG_CHECK_READ(&cpu_state.seg_es);
        do_mmut_rl(es, EDI, addr642);
        if (cpu_state.abrt) return 1;
        src = readmeml_n(cpu_state.ea_seg->base, ESI, addr64);
        dst = readmeml_n(es, EDI, addr642);        if (cpu_state.abrt) return 1;
        setsub32(src, dst);
        if (cpu_state.flags & D_FLAG) { EDI -= 4; ESI -= 4; }
        else                          { EDI += 4; ESI += 4; }
        CLOCK_CYCLES((is486) ? 8 : 10);
        PREFETCH_RUN((is486) ? 8 : 10, 1, -1, 0,2,0,0, 1);
        return 0;
}

static int opSTOSB_a16(uint32_t fetchdat)
{
        SEG_CHECK_WRITE(&cpu_state.seg_es);
        writememb(es, DI, AL);                  if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) DI--;
        else                          DI++;
        CLOCK_CYCLES(4);
        PREFETCH_RUN(4, 1, -1, 0,0,1,0, 0);
        return 0;
}
static int opSTOSB_a32(uint32_t fetchdat)
{
        SEG_CHECK_WRITE(&cpu_state.seg_es);
        writememb(es, EDI, AL);                 if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) EDI--;
        else                          EDI++;
        CLOCK_CYCLES(4);
        PREFETCH_RUN(4, 1, -1, 0,0,1,0, 1);
        return 0;
}

static int opSTOSW_a16(uint32_t fetchdat)
{
        SEG_CHECK_WRITE(&cpu_state.seg_es);
        writememw(es, DI, AX);                  if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) DI -= 2;
        else                          DI += 2;
        CLOCK_CYCLES(4);
        PREFETCH_RUN(4, 1, -1, 0,0,1,0, 0);
        return 0;
}
static int opSTOSW_a32(uint32_t fetchdat)
{
        SEG_CHECK_WRITE(&cpu_state.seg_es);
        writememw(es, EDI, AX);                 if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) EDI -= 2;
        else                          EDI += 2;
        CLOCK_CYCLES(4);
        PREFETCH_RUN(4, 1, -1, 0,0,1,0, 1);
        return 0;
}

static int opSTOSL_a16(uint32_t fetchdat)
{
        SEG_CHECK_WRITE(&cpu_state.seg_es);
        writememl(es, DI, EAX);                 if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) DI -= 4;
        else                          DI += 4;
        CLOCK_CYCLES(4);
        PREFETCH_RUN(4, 1, -1, 0,0,0,1, 0);
        return 0;
}
static int opSTOSL_a32(uint32_t fetchdat)
{
        SEG_CHECK_WRITE(&cpu_state.seg_es);
        writememl(es, EDI, EAX);                if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) EDI -= 4;
        else                          EDI += 4;
        CLOCK_CYCLES(4);
        PREFETCH_RUN(4, 1, -1, 0,0,0,1, 1);
        return 0;
}


static int opLODSB_a16(uint32_t fetchdat)
{
        uint8_t temp;

        SEG_CHECK_READ(cpu_state.ea_seg);
        temp = readmemb(cpu_state.ea_seg->base, SI); if (cpu_state.abrt) return 1;
        AL = temp;
        if (cpu_state.flags & D_FLAG) SI--;
        else                          SI++;
        CLOCK_CYCLES(5);
        PREFETCH_RUN(5, 1, -1, 1,0,0,0, 0);
        return 0;
}
static int opLODSB_a32(uint32_t fetchdat)
{
        uint8_t temp;

        SEG_CHECK_READ(cpu_state.ea_seg);
        temp = readmemb(cpu_state.ea_seg->base, ESI); if (cpu_state.abrt) return 1;
        AL = temp;
        if (cpu_state.flags & D_FLAG) ESI--;
        else                          ESI++;
        CLOCK_CYCLES(5);
        PREFETCH_RUN(5, 1, -1, 1,0,0,0, 1);
        return 0;
}

static int opLODSW_a16(uint32_t fetchdat)
{
        uint16_t temp;

        SEG_CHECK_READ(cpu_state.ea_seg);
        temp = readmemw(cpu_state.ea_seg->base, SI); if (cpu_state.abrt) return 1;
        AX = temp;
        if (cpu_state.flags & D_FLAG) SI -= 2;
        else                          SI += 2;
        CLOCK_CYCLES(5);
        PREFETCH_RUN(5, 1, -1, 1,0,0,0, 0);
        return 0;
}
static int opLODSW_a32(uint32_t fetchdat)
{
        uint16_t temp;

        SEG_CHECK_READ(cpu_state.ea_seg);
        temp = readmemw(cpu_state.ea_seg->base, ESI); if (cpu_state.abrt) return 1;
        AX = temp;
        if (cpu_state.flags & D_FLAG) ESI -= 2;
        else                          ESI += 2;
        CLOCK_CYCLES(5);
        PREFETCH_RUN(5, 1, -1, 1,0,0,0, 1);
        return 0;
}

static int opLODSL_a16(uint32_t fetchdat)
{
        uint32_t temp;

        SEG_CHECK_READ(cpu_state.ea_seg);
        temp = readmeml(cpu_state.ea_seg->base, SI); if (cpu_state.abrt) return 1;
        EAX = temp;
        if (cpu_state.flags & D_FLAG) SI -= 4;
        else                          SI += 4;
        CLOCK_CYCLES(5);
        PREFETCH_RUN(5, 1, -1, 0,1,0,0, 0);
        return 0;
}
static int opLODSL_a32(uint32_t fetchdat)
{
        uint32_t temp;

        SEG_CHECK_READ(cpu_state.ea_seg);
        temp = readmeml(cpu_state.ea_seg->base, ESI); if (cpu_state.abrt) return 1;
        EAX = temp;
        if (cpu_state.flags & D_FLAG) ESI -= 4;
        else                          ESI += 4;
        CLOCK_CYCLES(5);
        PREFETCH_RUN(5, 1, -1, 0,1,0,0, 1);
        return 0;
}


static int opSCASB_a16(uint32_t fetchdat)
{
        uint8_t temp;

        SEG_CHECK_READ(&cpu_state.seg_es);
        temp = readmemb(es, DI);        if (cpu_state.abrt) return 1;
        setsub8(AL, temp);
        if (cpu_state.flags & D_FLAG) DI--;
        else                          DI++;
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 1, -1, 1,0,0,0, 0);
        return 0;
}
static int opSCASB_a32(uint32_t fetchdat)
{
        uint8_t temp;

        SEG_CHECK_READ(&cpu_state.seg_es);
        temp = readmemb(es, EDI);       if (cpu_state.abrt) return 1;
        setsub8(AL, temp);
        if (cpu_state.flags & D_FLAG) EDI--;
        else                          EDI++;
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 1, -1, 1,0,0,0, 1);
        return 0;
}

static int opSCASW_a16(uint32_t fetchdat)
{
        uint16_t temp;

        SEG_CHECK_READ(&cpu_state.seg_es);
        temp = readmemw(es, DI);       if (cpu_state.abrt) return 1;
        setsub16(AX, temp);
        if (cpu_state.flags & D_FLAG) DI -= 2;
        else                          DI += 2;
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 1, -1, 1,0,0,0, 0);
        return 0;
}
static int opSCASW_a32(uint32_t fetchdat)
{
        uint16_t temp;

        SEG_CHECK_READ(&cpu_state.seg_es);
        temp = readmemw(es, EDI);      if (cpu_state.abrt) return 1;
        setsub16(AX, temp);
        if (cpu_state.flags & D_FLAG) EDI -= 2;
        else                          EDI += 2;
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 1, -1, 1,0,0,0, 1);
        return 0;
}

static int opSCASL_a16(uint32_t fetchdat)
{
        uint32_t temp;

        SEG_CHECK_READ(&cpu_state.seg_es);
        temp = readmeml(es, DI);       if (cpu_state.abrt) return 1;
        setsub32(EAX, temp);
        if (cpu_state.flags & D_FLAG) DI -= 4;
        else                          DI += 4;
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 1, -1, 0,1,0,0, 0);
        return 0;
}
static int opSCASL_a32(uint32_t fetchdat)
{
        uint32_t temp;

        SEG_CHECK_READ(&cpu_state.seg_es);
        temp = readmeml(es, EDI);      if (cpu_state.abrt) return 1;
        setsub32(EAX, temp);
        if (cpu_state.flags & D_FLAG) EDI -= 4;
        else                          EDI += 4;
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 1, -1, 0,1,0,0, 1);
        return 0;
}

static int opINSB_a16(uint32_t fetchdat)
{
        uint8_t temp;
        uint64_t addr64 = 0x0000000000000000ULL;

        SEG_CHECK_WRITE(&cpu_state.seg_es);
        check_io_perm(DX);
	do_mmut_wb(es, DI, &addr64);                      if (cpu_state.abrt) return 1;
        temp = inb(DX);
        writememb_n(es, DI, addr64, temp);                if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) DI--;
        else                          DI++;
        CLOCK_CYCLES(15);
        PREFETCH_RUN(15, 1, -1, 1,0,1,0, 0);
        return 0;
}
static int opINSB_a32(uint32_t fetchdat)
{
        uint8_t temp;
        uint64_t addr64 = 0x0000000000000000ULL;

        SEG_CHECK_WRITE(&cpu_state.seg_es);
        check_io_perm(DX);
	do_mmut_wb(es, EDI, &addr64);                     if (cpu_state.abrt) return 1;
        temp = inb(DX);
        writememb_n(es, EDI, addr64, temp);               if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) EDI--;
        else                          EDI++;
        CLOCK_CYCLES(15);
        PREFETCH_RUN(15, 1, -1, 1,0,1,0, 1);
        return 0;
}

static int opINSW_a16(uint32_t fetchdat)
{
        uint16_t temp;
        uint64_t addr64[2];

        SEG_CHECK_WRITE(&cpu_state.seg_es);
        check_io_perm(DX);
        check_io_perm(DX + 1);
	do_mmut_ww(es, DI, addr64);                       if (cpu_state.abrt) return 1;
        temp = inw(DX);
        writememw_n(es, DI, addr64, temp);                if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) DI -= 2;
        else                          DI += 2;
        CLOCK_CYCLES(15);
        PREFETCH_RUN(15, 1, -1, 1,0,1,0, 0);
        return 0;
}
static int opINSW_a32(uint32_t fetchdat)
{
        uint16_t temp;
        uint64_t addr64[2];

        SEG_CHECK_WRITE(&cpu_state.seg_es);
        check_io_perm(DX);
        check_io_perm(DX + 1);
	do_mmut_ww(es, EDI, addr64);                      if (cpu_state.abrt) return 1;
        temp = inw(DX);
        writememw_n(es, EDI, addr64, temp);               if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) EDI -= 2;
        else                          EDI += 2;
        CLOCK_CYCLES(15);
        PREFETCH_RUN(15, 1, -1, 1,0,1,0, 1);
        return 0;
}

static int opINSL_a16(uint32_t fetchdat)
{
        uint32_t temp;
        uint64_t addr64[4];

        SEG_CHECK_WRITE(&cpu_state.seg_es);
        check_io_perm(DX);
        check_io_perm(DX + 1);
        check_io_perm(DX + 2);
        check_io_perm(DX + 3);
	do_mmut_wl(es, DI, addr64);                       if (cpu_state.abrt) return 1;
        temp = inl(DX);
        writememl_n(es, DI, addr64, temp);                if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) DI -= 4;
        else                          DI += 4;
        CLOCK_CYCLES(15);
        PREFETCH_RUN(15, 1, -1, 0,1,0,1, 0);
        return 0;
}
static int opINSL_a32(uint32_t fetchdat)
{
        uint32_t temp;
        uint64_t addr64[4];

        SEG_CHECK_WRITE(&cpu_state.seg_es);
        check_io_perm(DX);
        check_io_perm(DX + 1);
        check_io_perm(DX + 2);
        check_io_perm(DX + 3);
	do_mmut_wl(es, DI, addr64);                       if (cpu_state.abrt) return 1;
        temp = inl(DX);
        writememl_n(es, EDI, addr64, temp);               if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) EDI -= 4;
        else                          EDI += 4;
        CLOCK_CYCLES(15);
        PREFETCH_RUN(15, 1, -1, 0,1,0,1, 1);
        return 0;
}

static int opOUTSB_a16(uint32_t fetchdat)
{
        uint8_t temp;

        SEG_CHECK_READ(cpu_state.ea_seg);
        temp = readmemb(cpu_state.ea_seg->base, SI); if (cpu_state.abrt) return 1;
        check_io_perm(DX);
        if (cpu_state.flags & D_FLAG) SI--;
        else                          SI++;
        outb(DX, temp);
        CLOCK_CYCLES(14);
        PREFETCH_RUN(14, 1, -1, 1,0,1,0, 0);
        return 0;
}
static int opOUTSB_a32(uint32_t fetchdat)
{
        uint8_t temp;

        SEG_CHECK_READ(cpu_state.ea_seg);
        temp = readmemb(cpu_state.ea_seg->base, ESI); if (cpu_state.abrt) return 1;
        check_io_perm(DX);
        if (cpu_state.flags & D_FLAG) ESI--;
        else                          ESI++;
        outb(DX, temp);
        CLOCK_CYCLES(14);
        PREFETCH_RUN(14, 1, -1, 1,0,1,0, 1);
        return 0;
}

static int opOUTSW_a16(uint32_t fetchdat)
{
        uint16_t temp;

        SEG_CHECK_READ(cpu_state.ea_seg);
        temp = readmemw(cpu_state.ea_seg->base, SI); if (cpu_state.abrt) return 1;
        check_io_perm(DX);
        check_io_perm(DX + 1);
        if (cpu_state.flags & D_FLAG) SI -= 2;
        else                          SI += 2;
        outw(DX, temp);
        CLOCK_CYCLES(14);
        PREFETCH_RUN(14, 1, -1, 1,0,1,0, 0);
        return 0;
}
static int opOUTSW_a32(uint32_t fetchdat)
{
        uint16_t temp;

        SEG_CHECK_READ(cpu_state.ea_seg);
        temp = readmemw(cpu_state.ea_seg->base, ESI); if (cpu_state.abrt) return 1;
        check_io_perm(DX);
        check_io_perm(DX + 1);
        if (cpu_state.flags & D_FLAG) ESI -= 2;
        else                          ESI += 2;
        outw(DX, temp);
        CLOCK_CYCLES(14);
        PREFETCH_RUN(14, 1, -1, 1,0,1,0, 1);
        return 0;
}

static int opOUTSL_a16(uint32_t fetchdat)
{
        uint32_t temp;

        SEG_CHECK_READ(cpu_state.ea_seg);
        temp = readmeml(cpu_state.ea_seg->base, SI); if (cpu_state.abrt) return 1;
        check_io_perm(DX);
        check_io_perm(DX + 1);
        check_io_perm(DX + 2);
        check_io_perm(DX + 3);
        if (cpu_state.flags & D_FLAG) SI -= 4;
        else                          SI += 4;
        outl(EDX, temp);
        CLOCK_CYCLES(14);
        PREFETCH_RUN(14, 1, -1, 0,1,0,1, 0);
        return 0;
}
static int opOUTSL_a32(uint32_t fetchdat)
{
        uint32_t temp;

        SEG_CHECK_READ(cpu_state.ea_seg);
        temp = readmeml(cpu_state.ea_seg->base, ESI); if (cpu_state.abrt) return 1;
        check_io_perm(DX);
        check_io_perm(DX + 1);
        check_io_perm(DX + 2);
        check_io_perm(DX + 3);
        if (cpu_state.flags & D_FLAG) ESI -= 4;
        else                          ESI += 4;
        outl(EDX, temp);
        CLOCK_CYCLES(14);
        PREFETCH_RUN(14, 1, -1, 0,1,0,1, 1);
        return 0;
}
