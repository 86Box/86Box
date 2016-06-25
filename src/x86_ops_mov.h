static int opMOV_AL_imm(uint32_t fetchdat)
{
        AL = getbytef();
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_AH_imm(uint32_t fetchdat)
{
        AH = getbytef();
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_BL_imm(uint32_t fetchdat)
{
        BL = getbytef();
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_BH_imm(uint32_t fetchdat)
{
        BH = getbytef();
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_CL_imm(uint32_t fetchdat)
{
        CL = getbytef();
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_CH_imm(uint32_t fetchdat)
{
        CH = getbytef();
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_DL_imm(uint32_t fetchdat)
{
        DL = getbytef();
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_DH_imm(uint32_t fetchdat)
{
        DH = getbytef();
        CLOCK_CYCLES(timing_rr);
        return 0;
}

static int opMOV_AX_imm(uint32_t fetchdat)
{
        AX = getwordf();
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_BX_imm(uint32_t fetchdat)
{
        BX = getwordf();
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_CX_imm(uint32_t fetchdat)
{
        CX = getwordf();
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_DX_imm(uint32_t fetchdat)
{
        DX = getwordf();
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_SI_imm(uint32_t fetchdat)
{
        SI = getwordf();
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_DI_imm(uint32_t fetchdat)
{
        DI = getwordf();
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_BP_imm(uint32_t fetchdat)
{
        BP = getwordf();
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_SP_imm(uint32_t fetchdat)
{
        SP = getwordf();
        CLOCK_CYCLES(timing_rr);
        return 0;
}

static int opMOV_EAX_imm(uint32_t fetchdat)
{
        uint32_t templ = getlong();     if (abrt) return 1;
        EAX = templ;
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_EBX_imm(uint32_t fetchdat)
{
        uint32_t templ = getlong();     if (abrt) return 1;
        EBX = templ;
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_ECX_imm(uint32_t fetchdat)
{
        uint32_t templ = getlong();     if (abrt) return 1;
        ECX = templ;
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_EDX_imm(uint32_t fetchdat)
{
        uint32_t templ = getlong();     if (abrt) return 1;
        EDX = templ;
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_ESI_imm(uint32_t fetchdat)
{
        uint32_t templ = getlong();     if (abrt) return 1;
        ESI = templ;
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_EDI_imm(uint32_t fetchdat)
{
        uint32_t templ = getlong();     if (abrt) return 1;
        EDI = templ;
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_EBP_imm(uint32_t fetchdat)
{
        uint32_t templ = getlong();     if (abrt) return 1;
        EBP = templ;
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opMOV_ESP_imm(uint32_t fetchdat)
{
        uint32_t templ = getlong();     if (abrt) return 1;
        ESP = templ;
        CLOCK_CYCLES(timing_rr);
        return 0;
}

static int opMOV_b_imm_a16(uint32_t fetchdat)
{
        uint8_t temp;
        fetch_ea_16(fetchdat);
        temp = readmemb(cs,cpu_state.pc); cpu_state.pc++;               if (abrt) return 1;
        CHECK_WRITE(ea_seg, eaaddr, eaaddr);
        seteab(temp);
        CLOCK_CYCLES(timing_rr);
        return abrt;
}
static int opMOV_b_imm_a32(uint32_t fetchdat)
{
        uint8_t temp;
        fetch_ea_32(fetchdat);
        temp = getbyte();               if (abrt) return 1;
        seteab(temp);
        CLOCK_CYCLES(timing_rr);
        return abrt;
}

static int opMOV_w_imm_a16(uint32_t fetchdat)
{
        uint16_t temp;
        fetch_ea_16(fetchdat);
        temp = getword();               if (abrt) return 1;
        seteaw(temp);
        CLOCK_CYCLES(timing_rr);
        return abrt;
}
static int opMOV_w_imm_a32(uint32_t fetchdat)
{
        uint16_t temp;
        fetch_ea_32(fetchdat);
        temp = getword();               if (abrt) return 1;
        seteaw(temp);
        CLOCK_CYCLES(timing_rr);
        return abrt;
}
static int opMOV_l_imm_a16(uint32_t fetchdat)
{
        uint32_t temp;
        fetch_ea_16(fetchdat);
        temp = getlong();               if (abrt) return 1;
        seteal(temp);
        CLOCK_CYCLES(timing_rr);
        return abrt;
}
static int opMOV_l_imm_a32(uint32_t fetchdat)
{
        uint32_t temp;
        fetch_ea_32(fetchdat);
        temp = getlong();               if (abrt) return 1;
        seteal(temp);
        CLOCK_CYCLES(timing_rr);
        return abrt;
}


static int opMOV_AL_a16(uint32_t fetchdat)
{
        uint16_t addr = getwordf();
        uint8_t temp = readmemb(ea_seg->base, addr);      if (abrt) return 1;
        AL = temp;
        CLOCK_CYCLES((is486) ? 1 : 4);
        return 0;        
}
static int opMOV_AL_a32(uint32_t fetchdat)
{
        uint32_t addr = getlong();
        uint8_t temp = readmemb(ea_seg->base, addr);      if (abrt) return 1;
        AL = temp;
        CLOCK_CYCLES((is486) ? 1 : 4);
        return 0;        
}
static int opMOV_AX_a16(uint32_t fetchdat)
{
        uint16_t addr = getwordf();
        uint16_t temp = readmemw(ea_seg->base, addr);     if (abrt) return 1;
        AX = temp;
        CLOCK_CYCLES((is486) ? 1 : 4);
        return 0;        
}
static int opMOV_AX_a32(uint32_t fetchdat)
{
        uint32_t addr = getlong();
        uint16_t temp = readmemw(ea_seg->base, addr);     if (abrt) return 1;
        AX = temp;
        CLOCK_CYCLES((is486) ? 1 : 4);
        return 0;        
}
static int opMOV_EAX_a16(uint32_t fetchdat)
{
        uint16_t addr = getwordf();
        uint32_t temp = readmeml(ea_seg->base, addr);     if (abrt) return 1;
        EAX = temp;
        CLOCK_CYCLES((is486) ? 1 : 4);
        return 0;        
}
static int opMOV_EAX_a32(uint32_t fetchdat)
{
        uint32_t addr = getlong();
        uint32_t temp = readmeml(ea_seg->base, addr);     if (abrt) return 1;
        EAX = temp;
        CLOCK_CYCLES((is486) ? 1 : 4);
        return 0;        
}

static int opMOV_a16_AL(uint32_t fetchdat)
{
        uint16_t addr = getwordf();
        writememb(ea_seg->base, addr, AL);
        CLOCK_CYCLES((is486) ? 1 : 2);
        return abrt;
}
static int opMOV_a32_AL(uint32_t fetchdat)
{
        uint32_t addr = getlong();
        writememb(ea_seg->base, addr, AL);
        CLOCK_CYCLES((is486) ? 1 : 2);
        return abrt;
}
static int opMOV_a16_AX(uint32_t fetchdat)
{
        uint16_t addr = getwordf();
        writememw(ea_seg->base, addr, AX);
        CLOCK_CYCLES((is486) ? 1 : 2);
        return abrt;
}
static int opMOV_a32_AX(uint32_t fetchdat)
{
        uint32_t addr = getlong();             if (abrt) return 1;
        writememw(ea_seg->base, addr, AX);
        CLOCK_CYCLES((is486) ? 1 : 2);
        return abrt;
}
static int opMOV_a16_EAX(uint32_t fetchdat)
{
        uint16_t addr = getwordf();
        writememl(ea_seg->base, addr, EAX);
        CLOCK_CYCLES((is486) ? 1 : 2);
        return abrt;
}
static int opMOV_a32_EAX(uint32_t fetchdat)
{
        uint32_t addr = getlong();             if (abrt) return 1;
        writememl(ea_seg->base, addr, EAX);
        CLOCK_CYCLES((is486) ? 1 : 2);
        return abrt;
}


static int opLEA_w_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);
        ILLEGAL_ON(mod == 3);
        cpu_state.regs[reg].w = eaaddr;
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opLEA_w_a32(uint32_t fetchdat)
{
        fetch_ea_32(fetchdat);
        ILLEGAL_ON(mod == 3);
        cpu_state.regs[reg].w = eaaddr;
        CLOCK_CYCLES(timing_rr);
        return 0;
}

static int opLEA_l_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);
        ILLEGAL_ON(mod == 3);
        cpu_state.regs[reg].l = eaaddr & 0xffff;
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opLEA_l_a32(uint32_t fetchdat)
{
        fetch_ea_32(fetchdat);
        ILLEGAL_ON(mod == 3);
        cpu_state.regs[reg].l = eaaddr;
        CLOCK_CYCLES(timing_rr);
        return 0;
}



static int opXLAT_a16(uint32_t fetchdat)
{
        uint32_t addr = (BX + AL)&0xFFFF;
        uint8_t temp = readmemb(ea_seg->base, addr); if (abrt) return 1;
        AL = temp;
        CLOCK_CYCLES(5);
        return 0;
}
static int opXLAT_a32(uint32_t fetchdat)
{
        uint32_t addr = EBX + AL;
        uint8_t temp = readmemb(ea_seg->base, addr); if (abrt) return 1;
        AL = temp;
        CLOCK_CYCLES(5);
        return 0;
}

static int opMOV_b_r_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);
        if (mod == 3)
        {
                setr8(rm, getr8(reg));
                CLOCK_CYCLES(timing_rr);
        }
        else
        {
                CHECK_WRITE(ea_seg, eaaddr, eaaddr);
                seteab(getr8(reg));
                CLOCK_CYCLES(is486 ? 1 : 2);
        }
        return abrt;
}
static int opMOV_b_r_a32(uint32_t fetchdat)
{
        fetch_ea_32(fetchdat);
        if (mod == 3)
        {
                setr8(rm, getr8(reg));
                CLOCK_CYCLES(timing_rr);
        }
        else
        {
                CHECK_WRITE(ea_seg, eaaddr, eaaddr);
                seteab(getr8(reg));
                CLOCK_CYCLES(is486 ? 1 : 2);
        }
        return abrt;
}
static int opMOV_w_r_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);
        if (mod == 3)
        {
                cpu_state.regs[rm].w = cpu_state.regs[reg].w;
                CLOCK_CYCLES(timing_rr);
        }
        else
        { 
                CHECK_WRITE(ea_seg, eaaddr, eaaddr+1);
                seteaw(cpu_state.regs[reg].w);
                CLOCK_CYCLES(is486 ? 1 : 2);
        }
        return abrt;
}
static int opMOV_w_r_a32(uint32_t fetchdat)
{
        fetch_ea_32(fetchdat);
        if (mod == 3)
        {
                cpu_state.regs[rm].w = cpu_state.regs[reg].w;
                CLOCK_CYCLES(timing_rr);
        }
        else
        { 
                CHECK_WRITE(ea_seg, eaaddr, eaaddr+1);
                seteaw(cpu_state.regs[reg].w);
                CLOCK_CYCLES(is486 ? 1 : 2);
        }
        return abrt;
}
static int opMOV_l_r_a16(uint32_t fetchdat)
{                       
        fetch_ea_16(fetchdat);
        if (mod == 3)
        {
                cpu_state.regs[rm].l = cpu_state.regs[reg].l;
                CLOCK_CYCLES(timing_rr);
        }
        else
        {
                CHECK_WRITE(ea_seg, eaaddr, eaaddr+3);
                seteal(cpu_state.regs[reg].l);
                CLOCK_CYCLES(is486 ? 1 : 2);
        }
        return abrt;
}
static int opMOV_l_r_a32(uint32_t fetchdat)
{                       
        fetch_ea_32(fetchdat);
        if (mod == 3)
        {
                cpu_state.regs[rm].l = cpu_state.regs[reg].l;
                CLOCK_CYCLES(timing_rr);
        }
        else
        {
                CHECK_WRITE(ea_seg, eaaddr, eaaddr+3);
                seteal(cpu_state.regs[reg].l);
                CLOCK_CYCLES(is486 ? 1 : 2);
        }
        return abrt;
}

static int opMOV_r_b_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);
        if (mod == 3)
        {
                setr8(reg, getr8(rm));
                CLOCK_CYCLES(timing_rr);
        }
        else
        {
                uint8_t temp;
                CHECK_READ(ea_seg, eaaddr, eaaddr);
                temp = geteab();                if (abrt) return 1;
                setr8(reg, temp);
                CLOCK_CYCLES(is486 ? 1 : 4);
        }
        return 0;
}
static int opMOV_r_b_a32(uint32_t fetchdat)
{
        fetch_ea_32(fetchdat);
        if (mod == 3)
        {
                setr8(reg, getr8(rm));
                CLOCK_CYCLES(timing_rr);
        }
        else
        {
                uint8_t temp;
                CHECK_READ(ea_seg, eaaddr, eaaddr);
                temp = geteab();                if (abrt) return 1;
                setr8(reg, temp);
                CLOCK_CYCLES(is486 ? 1 : 4);
        }
        return 0;
}
static int opMOV_r_w_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);
        if (mod == 3)
        {
                cpu_state.regs[reg].w = cpu_state.regs[rm].w;
                CLOCK_CYCLES(timing_rr);
        }
        else
        {
                uint16_t temp;
                CHECK_READ(ea_seg, eaaddr, eaaddr+1);
                temp = geteaw();                if (abrt) return 1;
                cpu_state.regs[reg].w = temp;
                CLOCK_CYCLES((is486) ? 1 : 4);
        }
        return 0;
}
static int opMOV_r_w_a32(uint32_t fetchdat)
{
        fetch_ea_32(fetchdat);
        if (mod == 3)
        {
                cpu_state.regs[reg].w = cpu_state.regs[rm].w;
                CLOCK_CYCLES(timing_rr);
        }
        else
        {
                uint16_t temp;
                CHECK_READ(ea_seg, eaaddr, eaaddr+1);
                temp = geteaw();                if (abrt) return 1;
                cpu_state.regs[reg].w = temp;
                CLOCK_CYCLES((is486) ? 1 : 4);
        }
        return 0;
}
static int opMOV_r_l_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);
        if (mod == 3)
        {
                cpu_state.regs[reg].l = cpu_state.regs[rm].l;
                CLOCK_CYCLES(timing_rr);
        }
        else
        {
                uint32_t temp;
                CHECK_READ(ea_seg, eaaddr, eaaddr+3);
                temp = geteal();                if (abrt) return 1;
                cpu_state.regs[reg].l = temp;
                CLOCK_CYCLES(is486 ? 1 : 4);
        }
        return 0;
}
static int opMOV_r_l_a32(uint32_t fetchdat)
{
        fetch_ea_32(fetchdat);
        if (mod == 3)
        {
                cpu_state.regs[reg].l = cpu_state.regs[rm].l;
                CLOCK_CYCLES(timing_rr);
        }
        else
        {
                uint32_t temp;
                CHECK_READ(ea_seg, eaaddr, eaaddr+3);
                temp = geteal();                if (abrt) return 1;
                cpu_state.regs[reg].l = temp;
                CLOCK_CYCLES(is486 ? 1 : 4);
        }
        return 0;
}

#define opCMOV(condition)                                                               \
        static int opCMOV ## condition ## _w_a16(uint32_t fetchdat)                     \
        {                                                                               \
                fetch_ea_16(fetchdat);                                                  \
                if (cond_ ## condition)                                                 \
                {                                                                       \
                        if (mod == 3)                                                   \
                                cpu_state.regs[reg].w = cpu_state.regs[rm].w;           \
                        else                                                            \
                        {                                                               \
                                uint16_t temp;                                          \
                                CHECK_READ(ea_seg, eaaddr, eaaddr+1);                   \
                                temp = geteaw();                if (abrt) return 1;     \
                                cpu_state.regs[reg].w = temp;                           \
                        }                                                               \
                }                                                                       \
                CLOCK_CYCLES(1);                                                        \
                return 0;                                                               \
        }                                                                               \
        static int opCMOV ## condition ## _w_a32(uint32_t fetchdat)                     \
        {                                                                               \
                fetch_ea_32(fetchdat);                                                  \
                if (cond_ ## condition)                                                 \
                {                                                                       \
                        if (mod == 3)                                                   \
                                cpu_state.regs[reg].w = cpu_state.regs[rm].w;           \
                        else                                                            \
                        {                                                               \
                                uint16_t temp;                                          \
                                CHECK_READ(ea_seg, eaaddr, eaaddr+1);                   \
                                temp = geteaw();                if (abrt) return 1;     \
                                cpu_state.regs[reg].w = temp;                           \
                        }                                                               \
                }                                                                       \
                CLOCK_CYCLES(1);                                                        \
                return 0;                                                               \
        }                                                                               \
        static int opCMOV ## condition ## _l_a16(uint32_t fetchdat)                     \
        {                                                                               \
                fetch_ea_16(fetchdat);                                                  \
                if (cond_ ## condition)                                                 \
                {                                                                       \
                        if (mod == 3)                                                   \
                                cpu_state.regs[reg].l = cpu_state.regs[rm].l;           \
                        else                                                            \
                        {                                                               \
                                uint32_t temp;                                          \
                                CHECK_READ(ea_seg, eaaddr, eaaddr+3);                   \
                                temp = geteal();                if (abrt) return 1;     \
                                cpu_state.regs[reg].l = temp;                           \
                        }                                                               \
                }                                                                       \
                CLOCK_CYCLES(1);                                                        \
                return 0;                                                               \
        }                                                                               \
        static int opCMOV ## condition ## _l_a32(uint32_t fetchdat)                     \
        {                                                                               \
                fetch_ea_32(fetchdat);                                                  \
                if (cond_ ## condition)                                                 \
                {                                                                       \
                        if (mod == 3)                                                   \
                                cpu_state.regs[reg].l = cpu_state.regs[rm].l;           \
                        else                                                            \
                        {                                                               \
                                uint32_t temp;                                          \
                                CHECK_READ(ea_seg, eaaddr, eaaddr+3);                   \
                                temp = geteal();                if (abrt) return 1;     \
                                cpu_state.regs[reg].l = temp;                           \
                        }                                                               \
                }                                                                       \
                CLOCK_CYCLES(1);                                                        \
                return 0;                                                               \
        }

opCMOV(O)
opCMOV(NO)
opCMOV(B)
opCMOV(NB)
opCMOV(E)
opCMOV(NE)
opCMOV(BE)
opCMOV(NBE)
opCMOV(S)
opCMOV(NS)
opCMOV(P)
opCMOV(NP)
opCMOV(L)
opCMOV(NL)
opCMOV(LE)
opCMOV(NLE)
