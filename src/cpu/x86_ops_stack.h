#define PUSH_W_OP(reg)                         \
    static int opPUSH_##reg(UNUSED(uint32_t fetchdat)) \
    {                                          \
        PUSH_W(reg);                           \
        CLOCK_CYCLES((is486) ? 1 : 2);         \
        PREFETCH_RUN(2, 1, -1, 0, 0, 1, 0, 0); \
        return cpu_state.abrt;                 \
    }

#define PUSH_L_OP(reg)                         \
    static int opPUSH_##reg(UNUSED(uint32_t fetchdat)) \
    {                                          \
        PUSH_L(reg);                           \
        CLOCK_CYCLES((is486) ? 1 : 2);         \
        PREFETCH_RUN(2, 1, -1, 0, 0, 0, 1, 0); \
        return cpu_state.abrt;                 \
    }

#define POP_W_OP(reg)                          \
    static int opPOP_##reg(UNUSED(uint32_t fetchdat)) \
    {                                          \
        reg = POP_W();                         \
        CLOCK_CYCLES((is486) ? 1 : 4);         \
        PREFETCH_RUN(4, 1, -1, 1, 0, 0, 0, 0); \
        return cpu_state.abrt;                 \
    }

#define POP_L_OP(reg)                          \
    static int opPOP_##reg(UNUSED(uint32_t fetchdat)) \
    {                                          \
        reg = POP_L();                         \
        CLOCK_CYCLES((is486) ? 1 : 4);         \
        PREFETCH_RUN(4, 1, -1, 0, 1, 0, 0, 0); \
        return cpu_state.abrt;                 \
    }

PUSH_W_OP(AX)
PUSH_W_OP(BX)
PUSH_W_OP(CX)
PUSH_W_OP(DX)
PUSH_W_OP(SI)
PUSH_W_OP(DI)
PUSH_W_OP(BP)
PUSH_W_OP(SP)

PUSH_L_OP(EAX)
PUSH_L_OP(EBX)
PUSH_L_OP(ECX)
PUSH_L_OP(EDX)
PUSH_L_OP(ESI)
PUSH_L_OP(EDI)
PUSH_L_OP(EBP)
PUSH_L_OP(ESP)

POP_W_OP(AX)
POP_W_OP(BX)
POP_W_OP(CX)
POP_W_OP(DX)
POP_W_OP(SI)
POP_W_OP(DI)
POP_W_OP(BP)
POP_W_OP(SP)

POP_L_OP(EAX)
POP_L_OP(EBX)
POP_L_OP(ECX)
POP_L_OP(EDX)
POP_L_OP(ESI)
POP_L_OP(EDI)
POP_L_OP(EBP)
POP_L_OP(ESP)

static int
opPUSHA_w(UNUSED(uint32_t fetchdat))
{
    if (stack32) {
        writememw(ss, ESP - 2, AX);
        writememw(ss, ESP - 4, CX);
        writememw(ss, ESP - 6, DX);
        writememw(ss, ESP - 8, BX);
        writememw(ss, ESP - 10, SP);
        writememw(ss, ESP - 12, BP);
        writememw(ss, ESP - 14, SI);
        writememw(ss, ESP - 16, DI);
        if (!cpu_state.abrt)
            ESP -= 16;
    } else {
        writememw(ss, ((SP - 2) & 0xFFFF), AX);
        writememw(ss, ((SP - 4) & 0xFFFF), CX);
        writememw(ss, ((SP - 6) & 0xFFFF), DX);
        writememw(ss, ((SP - 8) & 0xFFFF), BX);
        writememw(ss, ((SP - 10) & 0xFFFF), SP);
        writememw(ss, ((SP - 12) & 0xFFFF), BP);
        writememw(ss, ((SP - 14) & 0xFFFF), SI);
        writememw(ss, ((SP - 16) & 0xFFFF), DI);
        if (!cpu_state.abrt)
            SP -= 16;
    }
    CLOCK_CYCLES((is486) ? 11 : 18);
    PREFETCH_RUN(18, 1, -1, 0, 0, 8, 0, 0);
    return cpu_state.abrt;
}
static int
opPUSHA_l(UNUSED(uint32_t fetchdat))
{
    if (stack32) {
        writememl(ss, ESP - 4, EAX);
        writememl(ss, ESP - 8, ECX);
        writememl(ss, ESP - 12, EDX);
        writememl(ss, ESP - 16, EBX);
        writememl(ss, ESP - 20, ESP);
        writememl(ss, ESP - 24, EBP);
        writememl(ss, ESP - 28, ESI);
        writememl(ss, ESP - 32, EDI);
        if (!cpu_state.abrt)
            ESP -= 32;
    } else {
        writememl(ss, ((SP - 4) & 0xFFFF), EAX);
        writememl(ss, ((SP - 8) & 0xFFFF), ECX);
        writememl(ss, ((SP - 12) & 0xFFFF), EDX);
        writememl(ss, ((SP - 16) & 0xFFFF), EBX);
        writememl(ss, ((SP - 20) & 0xFFFF), ESP);
        writememl(ss, ((SP - 24) & 0xFFFF), EBP);
        writememl(ss, ((SP - 28) & 0xFFFF), ESI);
        writememl(ss, ((SP - 32) & 0xFFFF), EDI);
        if (!cpu_state.abrt)
            SP -= 32;
    }
    CLOCK_CYCLES((is486) ? 11 : 18);
    PREFETCH_RUN(18, 1, -1, 0, 0, 0, 8, 0);
    return cpu_state.abrt;
}

static int
opPOPA_w(UNUSED(uint32_t fetchdat))
{
    if (stack32) {
        DI = readmemw(ss, ESP);
        if (cpu_state.abrt)
            return 1;
        SI = readmemw(ss, ESP + 2);
        if (cpu_state.abrt)
            return 1;
        BP = readmemw(ss, ESP + 4);
        if (cpu_state.abrt)
            return 1;
        BX = readmemw(ss, ESP + 8);
        if (cpu_state.abrt)
            return 1;
        DX = readmemw(ss, ESP + 10);
        if (cpu_state.abrt)
            return 1;
        CX = readmemw(ss, ESP + 12);
        if (cpu_state.abrt)
            return 1;
        AX = readmemw(ss, ESP + 14);
        if (cpu_state.abrt)
            return 1;
        ESP += 16;
    } else {
        DI = readmemw(ss, ((SP) &0xFFFF));
        if (cpu_state.abrt)
            return 1;
        SI = readmemw(ss, ((SP + 2) & 0xFFFF));
        if (cpu_state.abrt)
            return 1;
        BP = readmemw(ss, ((SP + 4) & 0xFFFF));
        if (cpu_state.abrt)
            return 1;
        BX = readmemw(ss, ((SP + 8) & 0xFFFF));
        if (cpu_state.abrt)
            return 1;
        DX = readmemw(ss, ((SP + 10) & 0xFFFF));
        if (cpu_state.abrt)
            return 1;
        CX = readmemw(ss, ((SP + 12) & 0xFFFF));
        if (cpu_state.abrt)
            return 1;
        AX = readmemw(ss, ((SP + 14) & 0xFFFF));
        if (cpu_state.abrt)
            return 1;
        SP += 16;
    }
    CLOCK_CYCLES((is486) ? 9 : 24);
    PREFETCH_RUN(24, 1, -1, 7, 0, 0, 0, 0);
    return 0;
}
static int
opPOPA_l(UNUSED(uint32_t fetchdat))
{
    if (stack32) {
        EDI = readmeml(ss, ESP);
        if (cpu_state.abrt)
            return 1;
        ESI = readmeml(ss, ESP + 4);
        if (cpu_state.abrt)
            return 1;
        EBP = readmeml(ss, ESP + 8);
        if (cpu_state.abrt)
            return 1;
        EBX = readmeml(ss, ESP + 16);
        if (cpu_state.abrt)
            return 1;
        EDX = readmeml(ss, ESP + 20);
        if (cpu_state.abrt)
            return 1;
        ECX = readmeml(ss, ESP + 24);
        if (cpu_state.abrt)
            return 1;
        EAX = readmeml(ss, ESP + 28);
        if (cpu_state.abrt)
            return 1;
        ESP += 32;
    } else {
        EDI = readmeml(ss, ((SP) &0xFFFF));
        if (cpu_state.abrt)
            return 1;
        ESI = readmeml(ss, ((SP + 4) & 0xFFFF));
        if (cpu_state.abrt)
            return 1;
        EBP = readmeml(ss, ((SP + 8) & 0xFFFF));
        if (cpu_state.abrt)
            return 1;
        EBX = readmeml(ss, ((SP + 16) & 0xFFFF));
        if (cpu_state.abrt)
            return 1;
        EDX = readmeml(ss, ((SP + 20) & 0xFFFF));
        if (cpu_state.abrt)
            return 1;
        ECX = readmeml(ss, ((SP + 24) & 0xFFFF));
        if (cpu_state.abrt)
            return 1;
        EAX = readmeml(ss, ((SP + 28) & 0xFFFF));
        if (cpu_state.abrt)
            return 1;
        SP += 32;
    }
    CLOCK_CYCLES((is486) ? 9 : 24);
    PREFETCH_RUN(24, 1, -1, 0, 7, 0, 0, 0);
    return 0;
}

static int
opPUSH_imm_w(uint32_t fetchdat)
{
    uint16_t val = getwordf();
    PUSH_W(val);
    CLOCK_CYCLES(2);
    PREFETCH_RUN(2, 3, -1, 0, 0, 1, 0, 0);
    return cpu_state.abrt;
}
static int
opPUSH_imm_l(UNUSED(uint32_t fetchdat))
{
    uint32_t val = getlong();
    if (cpu_state.abrt)
        return 1;
    PUSH_L(val);
    CLOCK_CYCLES(2);
    PREFETCH_RUN(2, 3, -1, 0, 0, 0, 1, 0);
    return cpu_state.abrt;
}

static int
opPUSH_imm_bw(uint32_t fetchdat)
{
    uint16_t tempw = getbytef();

    if (tempw & 0x80)
        tempw |= 0xFF00;
    PUSH_W(tempw);

    CLOCK_CYCLES(2);
    PREFETCH_RUN(2, 2, -1, 0, 0, 1, 0, 0);
    return cpu_state.abrt;
}
static int
opPUSH_imm_bl(uint32_t fetchdat)
{
    uint32_t templ = getbytef();

    if (templ & 0x80)
        templ |= 0xFFFFFF00;
    PUSH_L(templ);

    CLOCK_CYCLES(2);
    PREFETCH_RUN(2, 2, -1, 0, 0, 0, 1, 0);
    return cpu_state.abrt;
}

static int
opPOPW_a16(uint32_t fetchdat)
{
    uint16_t temp;

    temp = POP_W();
    if (cpu_state.abrt)
        return 1;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    seteaw(temp);
    if (cpu_state.abrt) {
        if (stack32)
            ESP -= 2;
        else
            SP -= 2;
    }

    if (is486) {
        CLOCK_CYCLES((cpu_mod == 3) ? 1 : 6);
    } else {
        CLOCK_CYCLES((cpu_mod == 3) ? 4 : 5);
    }
    PREFETCH_RUN((cpu_mod == 3) ? 4 : 5, 2, rmdat, 1, 0, (cpu_mod == 3) ? 0 : 1, 0, 0);
    return cpu_state.abrt;
}
static int
opPOPW_a32(uint32_t fetchdat)
{
    uint16_t temp;

    temp = POP_W();
    if (cpu_state.abrt)
        return 1;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    seteaw(temp);
    if (cpu_state.abrt) {
        if (stack32)
            ESP -= 2;
        else
            SP -= 2;
    }

    if (is486) {
        CLOCK_CYCLES((cpu_mod == 3) ? 1 : 6);
    } else {
        CLOCK_CYCLES((cpu_mod == 3) ? 4 : 5);
    }
    PREFETCH_RUN((cpu_mod == 3) ? 4 : 5, 2, rmdat, 1, 0, (cpu_mod == 3) ? 0 : 1, 0, 1);
    return cpu_state.abrt;
}

static int
opPOPL_a16(uint32_t fetchdat)
{
    uint32_t temp;

    temp = POP_L();
    if (cpu_state.abrt)
        return 1;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    seteal(temp);
    if (cpu_state.abrt) {
        if (stack32)
            ESP -= 4;
        else
            SP -= 4;
    }

    if (is486) {
        CLOCK_CYCLES((cpu_mod == 3) ? 1 : 6);
    } else {
        CLOCK_CYCLES((cpu_mod == 3) ? 4 : 5);
    }
    PREFETCH_RUN((cpu_mod == 3) ? 4 : 5, 2, rmdat, 0, 1, 0, (cpu_mod == 3) ? 0 : 1, 0);
    return cpu_state.abrt;
}
static int
opPOPL_a32(uint32_t fetchdat)
{
    uint32_t temp;

    temp = POP_L();
    if (cpu_state.abrt)
        return 1;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    seteal(temp);
    if (cpu_state.abrt) {
        if (stack32)
            ESP -= 4;
        else
            SP -= 4;
    }

    if (is486) {
        CLOCK_CYCLES((cpu_mod == 3) ? 1 : 6);
    } else {
        CLOCK_CYCLES((cpu_mod == 3) ? 4 : 5);
    }
    PREFETCH_RUN((cpu_mod == 3) ? 4 : 5, 2, rmdat, 0, 1, 0, (cpu_mod == 3) ? 0 : 1, 1);
    return cpu_state.abrt;
}

static int
opENTER_w(uint32_t fetchdat)
{
    uint16_t offset;
    int      count;
    uint32_t tempEBP;
    uint32_t tempESP;
    uint32_t frame_ptr;
#ifndef IS_DYNAREC
    int reads        = 0;
    int writes       = 1;
    int instr_cycles = 0;
#endif
    uint16_t tempw;

    offset = getwordf();
    count  = (fetchdat >> 16) & 0xff;
    cpu_state.pc++;
    tempEBP = EBP;
    tempESP = ESP;

    PUSH_W(BP);
    if (cpu_state.abrt)
        return 1;
    frame_ptr = ESP;

    if (count > 0) {
        while (--count) {
            BP -= 2;
            tempw = readmemw(ss, BP);
            if (cpu_state.abrt) {
                ESP = tempESP;
                EBP = tempEBP;
                return 1;
            }
            PUSH_W(tempw);
            if (cpu_state.abrt) {
                ESP = tempESP;
                EBP = tempEBP;
                return 1;
            }
            CLOCK_CYCLES((is486) ? 3 : 4);
#ifndef IS_DYNAREC
            reads++;
            writes++;
            instr_cycles += (is486) ? 3 : 4;
#endif
        }
        PUSH_W(frame_ptr);
        if (cpu_state.abrt) {
            ESP = tempESP;
            EBP = tempEBP;
            return 1;
        }
        CLOCK_CYCLES((is486) ? 3 : 5);
#ifndef IS_DYNAREC
        writes++;
        instr_cycles += (is486) ? 3 : 5;
#endif
    }
    BP = frame_ptr;

    if (stack32)
        ESP -= offset;
    else
        SP -= offset;
    CLOCK_CYCLES((is486) ? 14 : 10);
#ifndef IS_DYNAREC
    instr_cycles += (is486) ? 14 : 10;
    PREFETCH_RUN(instr_cycles, 3, -1, reads, 0, writes, 0, 0);
#endif
    return 0;
}
static int
opENTER_l(uint32_t fetchdat)
{
    uint16_t offset;
    int      count;
    uint32_t tempEBP;
    uint32_t tempESP;
    uint32_t frame_ptr;
#ifndef IS_DYNAREC
    int reads        = 0;
    int writes       = 1;
    int instr_cycles = 0;
#endif
    uint32_t templ;

    offset = getwordf();
    count  = (fetchdat >> 16) & 0xff;
    cpu_state.pc++;
    tempEBP = EBP;
    tempESP = ESP;

    PUSH_L(EBP);
    if (cpu_state.abrt)
        return 1;
    frame_ptr = ESP;

    if (count > 0) {
        while (--count) {
            EBP -= 4;
            templ = readmeml(ss, EBP);
            if (cpu_state.abrt) {
                ESP = tempESP;
                EBP = tempEBP;
                return 1;
            }
            PUSH_L(templ);
            if (cpu_state.abrt) {
                ESP = tempESP;
                EBP = tempEBP;
                return 1;
            }
            CLOCK_CYCLES((is486) ? 3 : 4);
#ifndef IS_DYNAREC
            reads++;
            writes++;
            instr_cycles += (is486) ? 3 : 4;
#endif
        }
        PUSH_L(frame_ptr);
        if (cpu_state.abrt) {
            ESP = tempESP;
            EBP = tempEBP;
            return 1;
        }
        CLOCK_CYCLES((is486) ? 3 : 5);
#ifndef IS_DYNAREC
        writes++;
        instr_cycles += (is486) ? 3 : 5;
#endif
    }
    EBP = frame_ptr;

    if (stack32)
        ESP -= offset;
    else
        SP -= offset;
    CLOCK_CYCLES((is486) ? 14 : 10);
#ifndef IS_DYNAREC
    instr_cycles += (is486) ? 14 : 10;
    PREFETCH_RUN(instr_cycles, 3, -1, reads, 0, writes, 0, 0);
#endif
    return 0;
}

static int
opLEAVE_w(UNUSED(uint32_t fetchdat))
{
    uint32_t tempESP = ESP;
    uint16_t temp;

    SP   = BP;
    temp = POP_W();
    if (cpu_state.abrt) {
        ESP = tempESP;
        return 1;
    }
    BP = temp;

    CLOCK_CYCLES(4);
    PREFETCH_RUN(4, 1, -1, 1, 0, 0, 0, 0);
    return 0;
}
static int
opLEAVE_l(UNUSED(uint32_t fetchdat))
{
    uint32_t tempESP = ESP;
    uint32_t temp;

    ESP  = EBP;
    temp = POP_L();
    if (cpu_state.abrt) {
        ESP = tempESP;
        return 1;
    }
    EBP = temp;

    CLOCK_CYCLES(4);
    PREFETCH_RUN(4, 1, -1, 0, 1, 0, 0, 0);
    return 0;
}

#define PUSH_SEG_OPS(seg)                          \
    static int opPUSH_##seg##_w(UNUSED(uint32_t fetchdat)) \
    {                                              \
        PUSH_W(seg);                               \
        CLOCK_CYCLES(2);                           \
        PREFETCH_RUN(2, 1, -1, 0, 0, 1, 0, 0);     \
        return cpu_state.abrt;                     \
    }                                              \
    static int opPUSH_##seg##_l(UNUSED(uint32_t fetchdat)) \
    {                                              \
        PUSH_L(seg);                               \
        CLOCK_CYCLES(2);                           \
        PREFETCH_RUN(2, 1, -1, 0, 0, 0, 1, 0);     \
        return cpu_state.abrt;                     \
    }

#define POP_SEG_OPS(seg, realseg)                          \
    static int opPOP_##seg##_w(UNUSED(uint32_t fetchdat))  \
    {                                                      \
        uint16_t temp_seg;                                 \
        uint32_t temp_esp = ESP;                           \
        temp_seg          = POP_W();                       \
        if (cpu_state.abrt)                                \
            return 1;                                      \
        op_loadseg(temp_seg, realseg);                     \
        if (cpu_state.abrt)                                \
            ESP = temp_esp;                                \
        CLOCK_CYCLES(is486 ? 3 : 7);                       \
        PREFETCH_RUN(is486 ? 3 : 7, 1, -1, 0, 0, 1, 0, 0); \
        return cpu_state.abrt;                             \
    }                                                      \
    static int opPOP_##seg##_l(UNUSED(uint32_t fetchdat))  \
    {                                                      \
        uint32_t temp_seg;                                 \
        uint32_t temp_esp = ESP;                           \
        temp_seg          = POP_L();                       \
        if (cpu_state.abrt)                                \
            return 1;                                      \
        op_loadseg(temp_seg & 0xffff, realseg);            \
        if (cpu_state.abrt)                                \
            ESP = temp_esp;                                \
        CLOCK_CYCLES(is486 ? 3 : 7);                       \
        PREFETCH_RUN(is486 ? 3 : 7, 1, -1, 0, 0, 1, 0, 0); \
        return cpu_state.abrt;                             \
    }

PUSH_SEG_OPS(CS)
PUSH_SEG_OPS(DS)
PUSH_SEG_OPS(ES)
PUSH_SEG_OPS(FS)
PUSH_SEG_OPS(GS)
PUSH_SEG_OPS(SS)
POP_SEG_OPS(DS, &cpu_state.seg_ds)
POP_SEG_OPS(ES, &cpu_state.seg_es)
POP_SEG_OPS(FS, &cpu_state.seg_fs)
POP_SEG_OPS(GS, &cpu_state.seg_gs)

static int
opPOP_SS_w(uint32_t fetchdat)
{
    uint16_t temp_seg;
    uint32_t temp_esp = ESP;
    temp_seg          = POP_W();
    if (cpu_state.abrt)
        return 1;
    op_loadseg(temp_seg, &cpu_state.seg_ss);
    if (cpu_state.abrt) {
        ESP = temp_esp;
        return 1;
    }
    CLOCK_CYCLES(is486 ? 3 : 7);
    PREFETCH_RUN(is486 ? 3 : 7, 1, -1, 0, 0, 1, 0, 0);

    cpu_state.oldpc  = cpu_state.pc;
    cpu_state.op32   = use32;
    cpu_state.ssegs  = 0;
    cpu_state.ea_seg = &cpu_state.seg_ds;
    fetchdat         = fastreadl(cs + cpu_state.pc);
    cpu_state.pc++;
    if (cpu_state.abrt)
        return 1;
#ifdef OPS_286_386
    x86_2386_opcodes[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
#else
    x86_opcodes[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
#endif

    return 1;
}
static int
opPOP_SS_l(uint32_t fetchdat)
{
    uint32_t temp_seg;
    uint32_t temp_esp = ESP;
    temp_seg          = POP_L();
    if (cpu_state.abrt)
        return 1;
    op_loadseg(temp_seg & 0xffff, &cpu_state.seg_ss);
    if (cpu_state.abrt) {
        ESP = temp_esp;
        return 1;
    }
    CLOCK_CYCLES(is486 ? 3 : 7);
    PREFETCH_RUN(is486 ? 3 : 7, 1, -1, 0, 0, 1, 0, 0);

    cpu_state.oldpc  = cpu_state.pc;
    cpu_state.op32   = use32;
    cpu_state.ssegs  = 0;
    cpu_state.ea_seg = &cpu_state.seg_ds;
    fetchdat         = fastreadl(cs + cpu_state.pc);
    cpu_state.pc++;
    if (cpu_state.abrt)
        return 1;
#ifdef OPS_286_386
    x86_2386_opcodes[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
#else
    x86_opcodes[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
#endif

    return 1;
}
