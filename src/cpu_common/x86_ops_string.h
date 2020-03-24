static int opMOVSB_a16(uint32_t fetchdat)
{
        uint8_t temp;

        SEG_CHECK_READ(cpu_state.ea_seg);
        SEG_CHECK_WRITE(&cpu_state.seg_es);
        temp = readmemb(cpu_state.ea_seg->base, SI); if (cpu_state.abrt) return 1;
        writememb(es, DI, temp);                if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) { DI--; SI--; }
        else                          { DI++; SI++; }
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 1, -1, 1,0,1,0, 0);
        return 0;
}
static int opMOVSB_a32(uint32_t fetchdat)
{
        uint8_t temp;

        SEG_CHECK_READ(cpu_state.ea_seg);
        SEG_CHECK_WRITE(&cpu_state.seg_es);
        temp = readmemb(cpu_state.ea_seg->base, ESI); if (cpu_state.abrt) return 1;
        writememb(es, EDI, temp);               if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) { EDI--; ESI--; }
        else                          { EDI++; ESI++; }
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 1, -1, 1,0,1,0, 1);
        return 0;
}

static int opMOVSW_a16(uint32_t fetchdat)
{
        uint16_t temp;

        SEG_CHECK_READ(cpu_state.ea_seg);
        SEG_CHECK_WRITE(&cpu_state.seg_es);
        temp = readmemw(cpu_state.ea_seg->base, SI); if (cpu_state.abrt) return 1;
        writememw(es, DI, temp);                if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) { DI -= 2; SI -= 2; }
        else                          { DI += 2; SI += 2; }
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 1, -1, 1,0,1,0, 0);
        return 0;
}
static int opMOVSW_a32(uint32_t fetchdat)
{
        uint16_t temp;

        SEG_CHECK_READ(cpu_state.ea_seg);
        SEG_CHECK_WRITE(&cpu_state.seg_es);
        temp = readmemw(cpu_state.ea_seg->base, ESI); if (cpu_state.abrt) return 1;
        writememw(es, EDI, temp);               if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) { EDI -= 2; ESI -= 2; }
        else                          { EDI += 2; ESI += 2; }
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 1, -1, 1,0,1,0, 1);
        return 0;
}

static int opMOVSL_a16(uint32_t fetchdat)
{
        uint32_t temp;

        SEG_CHECK_READ(cpu_state.ea_seg);
        SEG_CHECK_WRITE(&cpu_state.seg_es);
        temp = readmeml(cpu_state.ea_seg->base, SI); if (cpu_state.abrt) return 1;
        writememl(es, DI, temp);                if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) { DI -= 4; SI -= 4; }
        else                          { DI += 4; SI += 4; }
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 1, -1, 0,1,0,1, 0);
        return 0;
}
static int opMOVSL_a32(uint32_t fetchdat)
{
        uint32_t temp;

        SEG_CHECK_READ(cpu_state.ea_seg);
        SEG_CHECK_WRITE(&cpu_state.seg_es);
        temp = readmeml(cpu_state.ea_seg->base, ESI); if (cpu_state.abrt) return 1;
        writememl(es, EDI, temp);               if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) { EDI -= 4; ESI -= 4; }
        else                          { EDI += 4; ESI += 4; }
        CLOCK_CYCLES(7);
        PREFETCH_RUN(7, 1, -1, 0,1,0,1, 1);
        return 0;
}


static int opCMPSB_a16(uint32_t fetchdat)
{
        uint8_t src, dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        SEG_CHECK_READ(&cpu_state.seg_es);
        src = readmemb(cpu_state.ea_seg->base, SI);
        dst = readmemb(es, DI);         if (cpu_state.abrt) return 1;
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

        SEG_CHECK_READ(cpu_state.ea_seg);
        SEG_CHECK_READ(&cpu_state.seg_es);
        src = readmemb(cpu_state.ea_seg->base, ESI);
        dst = readmemb(es, EDI);        if (cpu_state.abrt) return 1;
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

        SEG_CHECK_READ(cpu_state.ea_seg);
        SEG_CHECK_READ(&cpu_state.seg_es);
        src = readmemw(cpu_state.ea_seg->base, SI);
        dst = readmemw(es, DI);        if (cpu_state.abrt) return 1;
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

        SEG_CHECK_READ(cpu_state.ea_seg);
        SEG_CHECK_READ(&cpu_state.seg_es);
        src = readmemw(cpu_state.ea_seg->base, ESI);
        dst = readmemw(es, EDI);        if (cpu_state.abrt) return 1;
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

        SEG_CHECK_READ(cpu_state.ea_seg);
        SEG_CHECK_READ(&cpu_state.seg_es);
        src = readmeml(cpu_state.ea_seg->base, SI);
        dst = readmeml(es, DI);        if (cpu_state.abrt) return 1;
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

        SEG_CHECK_READ(cpu_state.ea_seg);
        SEG_CHECK_READ(&cpu_state.seg_es);
        src = readmeml(cpu_state.ea_seg->base, ESI);
        dst = readmeml(es, EDI);        if (cpu_state.abrt) return 1;
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

        SEG_CHECK_WRITE(&cpu_state.seg_es);
        check_io_perm(DX);
        temp = inb(DX);
        writememb(es, DI, temp);                if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) DI--;
        else                          DI++;
        CLOCK_CYCLES(15);
        PREFETCH_RUN(15, 1, -1, 1,0,1,0, 0);
        return 0;
}
static int opINSB_a32(uint32_t fetchdat)
{
        uint8_t temp;

        SEG_CHECK_WRITE(&cpu_state.seg_es);
        check_io_perm(DX);
        temp = inb(DX);
        writememb(es, EDI, temp);               if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) EDI--;
        else                          EDI++;
        CLOCK_CYCLES(15);
        PREFETCH_RUN(15, 1, -1, 1,0,1,0, 1);
        return 0;
}

static int opINSW_a16(uint32_t fetchdat)
{
        uint16_t temp;

        SEG_CHECK_WRITE(&cpu_state.seg_es);
        check_io_perm(DX);
        check_io_perm(DX + 1);
        temp = inw(DX);
        writememw(es, DI, temp);                if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) DI -= 2;
        else                          DI += 2;
        CLOCK_CYCLES(15);
        PREFETCH_RUN(15, 1, -1, 1,0,1,0, 0);
        return 0;
}
static int opINSW_a32(uint32_t fetchdat)
{
        uint16_t temp;

        SEG_CHECK_WRITE(&cpu_state.seg_es);
        check_io_perm(DX);
        check_io_perm(DX + 1);
        temp = inw(DX);
        writememw(es, EDI, temp);               if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) EDI -= 2;
        else                          EDI += 2;
        CLOCK_CYCLES(15);
        PREFETCH_RUN(15, 1, -1, 1,0,1,0, 1);
        return 0;
}

static int opINSL_a16(uint32_t fetchdat)
{
        uint32_t temp;

        SEG_CHECK_WRITE(&cpu_state.seg_es);
        check_io_perm(DX);
        check_io_perm(DX + 1);
        check_io_perm(DX + 2);
        check_io_perm(DX + 3);
        temp = inl(DX);
        writememl(es, DI, temp);                if (cpu_state.abrt) return 1;
        if (cpu_state.flags & D_FLAG) DI -= 4;
        else                          DI += 4;
        CLOCK_CYCLES(15);
        PREFETCH_RUN(15, 1, -1, 0,1,0,1, 0);
        return 0;
}
static int opINSL_a32(uint32_t fetchdat)
{
        uint32_t temp;

        SEG_CHECK_WRITE(&cpu_state.seg_es);
        check_io_perm(DX);
        check_io_perm(DX + 1);
        check_io_perm(DX + 2);
        check_io_perm(DX + 3);
        temp = inl(DX);
        writememl(es, EDI, temp);               if (cpu_state.abrt) return 1;
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
