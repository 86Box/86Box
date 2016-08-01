#define PUSH_W_OP(reg)                                                                          \
        static int opPUSH_ ## reg (uint32_t fetchdat)                                                  \
        {                                                                                       \
                PUSH_W(reg);                                                                    \
                CLOCK_CYCLES((is486) ? 1 : 2);                                                  \
                return abrt;                                                                    \
        }

#define PUSH_L_OP(reg)                                                                          \
        static int opPUSH_ ## reg (uint32_t fetchdat)                                                  \
        {                                                                                       \
                PUSH_L(reg);                                                                    \
                CLOCK_CYCLES((is486) ? 1 : 2);                                                  \
                return abrt;                                                                    \
        }

#define POP_W_OP(reg)                                                                           \
        static int opPOP_ ## reg (uint32_t fetchdat)                                                   \
        {                                                                                       \
                reg = POP_W();                                                                  \
                CLOCK_CYCLES((is486) ? 1 : 4);                                                  \
                return abrt;                                                                    \
        }

#define POP_L_OP(reg)                                                                           \
        static int opPOP_ ## reg (uint32_t fetchdat)                                                   \
        {                                                                                       \
                reg = POP_L();                                                                  \
                CLOCK_CYCLES((is486) ? 1 : 4);                                                  \
                return abrt;                                                                    \
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


static int opPUSHA_w(uint32_t fetchdat)
{
        if (stack32)
        {
                writememw(ss, ESP -  2, AX);
                writememw(ss, ESP -  4, CX);
                writememw(ss, ESP -  6, DX);
                writememw(ss, ESP -  8, BX);
                writememw(ss, ESP - 10, SP);
                writememw(ss, ESP - 12, BP);
                writememw(ss, ESP - 14, SI);
                writememw(ss, ESP - 16, DI);
                if (!abrt) ESP -= 16;
        }
        else
        {
                writememw(ss, ((SP -  2) & 0xFFFF), AX);
                writememw(ss, ((SP -  4) & 0xFFFF), CX);
                writememw(ss, ((SP -  6) & 0xFFFF), DX);
                writememw(ss, ((SP -  8) & 0xFFFF), BX);
                writememw(ss, ((SP - 10) & 0xFFFF), SP);
                writememw(ss, ((SP - 12) & 0xFFFF), BP);
                writememw(ss, ((SP - 14) & 0xFFFF), SI);
                writememw(ss, ((SP - 16) & 0xFFFF), DI);
                if (!abrt) SP -= 16;
        }
        CLOCK_CYCLES((is486) ? 11 : 18);
        return abrt;
}
static int opPUSHA_l(uint32_t fetchdat)
{
        if (stack32)
        {
                writememl(ss, ESP -  4, EAX);
                writememl(ss, ESP -  8, ECX);
                writememl(ss, ESP - 12, EDX);
                writememl(ss, ESP - 16, EBX);
                writememl(ss, ESP - 20, ESP);
                writememl(ss, ESP - 24, EBP);
                writememl(ss, ESP - 28, ESI);
                writememl(ss, ESP - 32, EDI);
                if (!abrt) ESP -= 32;
        }
        else
        {
                writememl(ss, ((SP -  4) & 0xFFFF), EAX);
                writememl(ss, ((SP -  8) & 0xFFFF), ECX);
                writememl(ss, ((SP - 12) & 0xFFFF), EDX);
                writememl(ss, ((SP - 16) & 0xFFFF), EBX);
                writememl(ss, ((SP - 20) & 0xFFFF), ESP);
                writememl(ss, ((SP - 24) & 0xFFFF), EBP);
                writememl(ss, ((SP - 28) & 0xFFFF), ESI);
                writememl(ss, ((SP - 32) & 0xFFFF), EDI);
                if (!abrt) SP -= 32;
        }
        CLOCK_CYCLES((is486) ? 11 : 18);
        return abrt;
}

static int opPOPA_w(uint32_t fetchdat)
{
        if (stack32)
        {
                DI = readmemw(ss, ESP);                         if (abrt) return 1;
                SI = readmemw(ss, ESP +  2);                    if (abrt) return 1;
                BP = readmemw(ss, ESP +  4);                    if (abrt) return 1;
                BX = readmemw(ss, ESP +  8);                    if (abrt) return 1;
                DX = readmemw(ss, ESP + 10);                    if (abrt) return 1;
                CX = readmemw(ss, ESP + 12);                    if (abrt) return 1;
                AX = readmemw(ss, ESP + 14);                    if (abrt) return 1;
                ESP += 16;
        }
        else
        {
                DI = readmemw(ss, ((SP)      & 0xFFFF));        if (abrt) return 1;
                SI = readmemw(ss, ((SP +  2) & 0xFFFF));        if (abrt) return 1;
                BP = readmemw(ss, ((SP +  4) & 0xFFFF));        if (abrt) return 1;
                BX = readmemw(ss, ((SP +  8) & 0xFFFF));        if (abrt) return 1;
                DX = readmemw(ss, ((SP + 10) & 0xFFFF));        if (abrt) return 1;
                CX = readmemw(ss, ((SP + 12) & 0xFFFF));        if (abrt) return 1;
                AX = readmemw(ss, ((SP + 14) & 0xFFFF));        if (abrt) return 1;
                SP += 16;
        }
        CLOCK_CYCLES((is486) ? 9 : 24);
        return 0;
}
static int opPOPA_l(uint32_t fetchdat)
{
        if (stack32)
        {
                EDI = readmeml(ss, ESP);                        if (abrt) return 1;
                ESI = readmeml(ss, ESP +  4);                   if (abrt) return 1;
                EBP = readmeml(ss, ESP +  8);                   if (abrt) return 1;
                EBX = readmeml(ss, ESP + 16);                   if (abrt) return 1;
                EDX = readmeml(ss, ESP + 20);                   if (abrt) return 1;
                ECX = readmeml(ss, ESP + 24);                   if (abrt) return 1;
                EAX = readmeml(ss, ESP + 28);                   if (abrt) return 1;
                ESP += 32;
        }
        else
        {
                EDI = readmeml(ss, ((SP)      & 0xFFFF));       if (abrt) return 1;
                ESI = readmeml(ss, ((SP +  4) & 0xFFFF));       if (abrt) return 1;
                EBP = readmeml(ss, ((SP +  8) & 0xFFFF));       if (abrt) return 1;
                EBX = readmeml(ss, ((SP + 16) & 0xFFFF));       if (abrt) return 1;
                EDX = readmeml(ss, ((SP + 20) & 0xFFFF));       if (abrt) return 1;
                ECX = readmeml(ss, ((SP + 24) & 0xFFFF));       if (abrt) return 1;
                EAX = readmeml(ss, ((SP + 28) & 0xFFFF));       if (abrt) return 1;
                SP += 32;
        }
        CLOCK_CYCLES((is486) ? 9 : 24);
        return 0;
}

static int opPUSH_imm_w(uint32_t fetchdat)
{
        uint16_t val = getwordf(); 
        PUSH_W(val);
        CLOCK_CYCLES(2);
        return abrt;
}
static int opPUSH_imm_l(uint32_t fetchdat)
{
        uint32_t val = getlong();              if (abrt) return 1;
        PUSH_L(val);
        CLOCK_CYCLES(2);
        return abrt;
}

static int opPUSH_imm_bw(uint32_t fetchdat)
{
        uint16_t tempw = getbytef();

        if (tempw & 0x80) tempw |= 0xFF00;
        PUSH_W(tempw);
        
        CLOCK_CYCLES(2);
        return abrt;
}
static int opPUSH_imm_bl(uint32_t fetchdat)
{
        uint32_t templ = getbytef();

        if (templ & 0x80) templ |= 0xFFFFFF00;
        PUSH_L(templ);
        
        CLOCK_CYCLES(2);
        return abrt;
}

static int opPOPW_a16(uint32_t fetchdat)
{
        uint16_t temp;
        
        temp = POP_W();                                 if (abrt) return 1;

        fetch_ea_16(fetchdat);
        seteaw(temp);
        if (abrt)
        {
                if (stack32) ESP -= 2;
                else         SP -= 2;
        }
                        
        if (is486) CLOCK_CYCLES((mod == 3) ? 1 : 6);
        else       CLOCK_CYCLES((mod == 3) ? 4 : 5);
        return abrt;
}
static int opPOPW_a32(uint32_t fetchdat)
{
        uint16_t temp;
                
        temp = POP_W();                                 if (abrt) return 1;
        
        fetch_ea_32(fetchdat);
        seteaw(temp);
        if (abrt)
        {
                if (stack32) ESP -= 2;
                else         SP -= 2;
        }
                        
        if (is486) CLOCK_CYCLES((mod == 3) ? 1 : 6);
        else       CLOCK_CYCLES((mod == 3) ? 4 : 5);
        return abrt;
}

static int opPOPL_a16(uint32_t fetchdat)
{
        uint32_t temp;

        temp = POP_L();                                 if (abrt) return 1;

        fetch_ea_16(fetchdat);        
        seteal(temp);
        if (abrt)
        {
                if (stack32) ESP -= 4;
                else         SP -= 4;
        }
                        
        if (is486) CLOCK_CYCLES((mod == 3) ? 1 : 6);
        else       CLOCK_CYCLES((mod == 3) ? 4 : 5);
        return abrt;
}
static int opPOPL_a32(uint32_t fetchdat)
{
        uint32_t temp;

        temp = POP_L();                                 if (abrt) return 1;

        fetch_ea_32(fetchdat);
        seteal(temp);
        if (abrt)
        {
                if (stack32) ESP -= 4;
                else         SP -= 4;
        }

        if (is486) CLOCK_CYCLES((mod == 3) ? 1 : 6);
        else       CLOCK_CYCLES((mod == 3) ? 4 : 5);
        return abrt;
}


static int opENTER_w(uint32_t fetchdat)
{
        uint16_t offset;
        int count;
        uint32_t tempEBP, tempESP, frame_ptr;
	offset = getwordf();
	count = (fetchdat >> 16) & 0xff;
	tempEBP = EBP, tempESP = ESP;
	cpu_state.pc++;
        
        PUSH_W(BP); if (abrt) return 1;
        frame_ptr = ESP;
        
        if (count > 0)
        {
                while (--count)
                {
                        uint16_t tempw;
                        
                        BP -= 2;
                        tempw = readmemw(ss, BP);
                        if (abrt) { ESP = tempESP; EBP = tempEBP; return 1; }
                        PUSH_W(tempw);
                        if (abrt) { ESP = tempESP; EBP = tempEBP; return 1; }
                        CLOCK_CYCLES((is486) ? 3 : 4);
                }
                PUSH_W(frame_ptr);
                if (abrt) { ESP = tempESP; EBP = tempEBP; return 1; }
                CLOCK_CYCLES((is486) ? 3 : 5);
        }
        BP = frame_ptr;
        
        if (stack32) ESP -= offset;
        else          SP -= offset;
        CLOCK_CYCLES((is486) ? 14 : 10);
        return 0;
}
static int opENTER_l(uint32_t fetchdat)
{
        uint16_t offset;
        int count;
        uint32_t tempEBP, tempESP, frame_ptr;
        offset = getwordf();
        count = (fetchdat >> 16) & 0xff;
        tempEBP = EBP, tempESP = ESP;
	cpu_state.pc++;
        
        PUSH_L(EBP); if (abrt) return 1;
        frame_ptr = ESP;
        
        if (count > 0)
        {
                while (--count)
                {
                        uint32_t templ;
                        
                        EBP -= 4;
                        templ = readmeml(ss, EBP);
                        if (abrt) { ESP = tempESP; EBP = tempEBP; return 1; }
                        PUSH_L(templ);
                        if (abrt) { ESP = tempESP; EBP = tempEBP; return 1; }
                        CLOCK_CYCLES((is486) ? 3 : 4);
                }
                PUSH_L(frame_ptr);
                if (abrt) { ESP = tempESP; EBP = tempEBP; return 1; }
                CLOCK_CYCLES((is486) ? 3 : 5);
        }
        EBP = frame_ptr;
        
        if (stack32) ESP -= offset;
        else          SP -= offset;
        CLOCK_CYCLES((is486) ? 14 : 10);
        return 0;
}


static int opLEAVE_w(uint32_t fetchdat)
{
        uint32_t tempESP = ESP;
        uint16_t temp;

        SP = BP;       
        temp = POP_W();
        if (abrt) { ESP = tempESP; return 1; }
        BP = temp;
        
        CLOCK_CYCLES(4);
        return 0;
}
static int opLEAVE_l(uint32_t fetchdat)
{
        uint32_t tempESP = ESP;
        uint32_t temp;

        ESP = EBP;       
        temp = POP_L();
        if (abrt) { ESP = tempESP; return 1; }
        EBP = temp;
        
        CLOCK_CYCLES(4);        
        return 0;
}


#define PUSH_SEG_OPS(seg)                                                       \
        static int opPUSH_ ## seg ## _w(uint32_t fetchdat)                      \
        {                                                                       \
                PUSH_W(seg);                                                    \
                CLOCK_CYCLES(2);                                                \
                return abrt;                                                    \
        }                                                                       \
        static int opPUSH_ ## seg ## _l(uint32_t fetchdat)                      \
        {                                                                       \
                PUSH_L(seg);                                                    \
                CLOCK_CYCLES(2);                                                \
                return abrt;                                                    \
        }
        
#define POP_SEG_OPS(seg, realseg)                                               \
        static int opPOP_ ## seg ## _w(uint32_t fetchdat)                       \
        {                                                                       \
                uint16_t temp_seg;                                              \
                uint32_t temp_esp = ESP;                                        \
                temp_seg = POP_W();                     if (abrt) return 1;     \
                loadseg(temp_seg, realseg);             if (abrt) ESP = temp_esp; \
                CLOCK_CYCLES(is486 ? 3 : 7);                                    \
                return abrt;                                                    \
        }                                                                       \
        static int opPOP_ ## seg ## _l(uint32_t fetchdat)                       \
        {                                                                       \
                uint32_t temp_seg;                                              \
                uint32_t temp_esp = ESP;                                        \
                temp_seg = POP_L();                     if (abrt) return 1;     \
                loadseg(temp_seg & 0xffff, realseg);    if (abrt) ESP = temp_esp; \
                CLOCK_CYCLES(is486 ? 3 : 7);                                    \
                return abrt;                                                    \
        }

                
PUSH_SEG_OPS(CS);
PUSH_SEG_OPS(DS);
PUSH_SEG_OPS(ES);
PUSH_SEG_OPS(FS);
PUSH_SEG_OPS(GS);
PUSH_SEG_OPS(SS);

POP_SEG_OPS(DS, &_ds);
POP_SEG_OPS(ES, &_es);
POP_SEG_OPS(FS, &_fs);
POP_SEG_OPS(GS, &_gs);


static int opPOP_SS_w(uint32_t fetchdat)
{
        uint16_t temp_seg;
        uint32_t temp_esp = ESP;
        temp_seg = POP_W();                     if (abrt) return 1;
        loadseg(temp_seg, &_ss);                if (abrt) { ESP = temp_esp; return 1; }
        CLOCK_CYCLES(is486 ? 3 : 7);
        
        oldpc = cpu_state.pc;
        op32 = use32;
        ssegs = 0;
        ea_seg = &_ds;
        fetchdat = fastreadl(cs + cpu_state.pc);
        cpu_state.pc++;
        if (abrt) return 1;
        x86_opcodes[(fetchdat & 0xff) | op32](fetchdat >> 8);

        return 1;
}
static int opPOP_SS_l(uint32_t fetchdat)
{
        uint32_t temp_seg;
        uint32_t temp_esp = ESP;
        temp_seg = POP_L();                     if (abrt) return 1;
        loadseg(temp_seg & 0xffff, &_ss);       if (abrt) { ESP = temp_esp; return 1; }
        CLOCK_CYCLES(is486 ? 3 : 7);

        oldpc = cpu_state.pc;
        op32 = use32;
        ssegs = 0;
        ea_seg = &_ds;
        fetchdat = fastreadl(cs + cpu_state.pc);
        cpu_state.pc++;
        if (abrt) return 1;
        x86_opcodes[(fetchdat & 0xff) | op32](fetchdat >> 8);

        return 1;
}
