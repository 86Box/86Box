#define cond_O   ( VF_SET())
#define cond_NO  (!VF_SET())
#define cond_B   ( CF_SET())
#define cond_NB  (!CF_SET())
#define cond_E   ( ZF_SET())
#define cond_NE  (!ZF_SET())
#define cond_BE  ( CF_SET() ||  ZF_SET())
#define cond_NBE (!CF_SET() && !ZF_SET())
#define cond_S   ( NF_SET())
#define cond_NS  (!NF_SET())
#define cond_P   ( PF_SET())
#define cond_NP  (!PF_SET())
#define cond_L   (((NF_SET()) ? 1 : 0) != ((VF_SET()) ? 1 : 0))
#define cond_NL  (((NF_SET()) ? 1 : 0) == ((VF_SET()) ? 1 : 0))
#define cond_LE  (((NF_SET()) ? 1 : 0) != ((VF_SET()) ? 1 : 0) ||  (ZF_SET()))
#define cond_NLE (((NF_SET()) ? 1 : 0) == ((VF_SET()) ? 1 : 0) && (!ZF_SET()))

#define opJ(condition)                                  \
        static int opJ ## condition(uint32_t fetchdat)  \
        {                                               \
                int8_t offset = (int8_t)getbytef();     \
                CLOCK_CYCLES(timing_bnt);               \
                if (cond_ ## condition)                 \
                {                                       \
                        cpu_state.pc += offset;         \
                        CLOCK_CYCLES_ALWAYS(timing_bt); \
                        CPU_BLOCK_END();                \
                        return 1;                       \
                }                                       \
                return 0;                               \
        }                                               \
                                                        \
        static int opJ ## condition ## _w(uint32_t fetchdat)  \
        {                                               \
                int16_t offset = (int16_t)getwordf();   \
                CLOCK_CYCLES(timing_bnt);               \
                if (cond_ ## condition)                 \
                {                                       \
                        cpu_state.pc += offset;         \
                        CLOCK_CYCLES_ALWAYS(timing_bt); \
                        CPU_BLOCK_END();                \
                        return 1;                       \
                }                                       \
                return 0;                               \
        }                                               \
                                                        \
        static int opJ ## condition ## _l(uint32_t fetchdat)  \
        {                                               \
                uint32_t offset = getlong(); if (abrt) return 1; \
                CLOCK_CYCLES(timing_bnt);               \
                if (cond_ ## condition)                 \
                {                                       \
                        cpu_state.pc += offset;         \
                        CLOCK_CYCLES_ALWAYS(timing_bt); \
                        CPU_BLOCK_END();                \
                        return 1;                       \
                }                                       \
                return 0;                               \
        }                                               \
        
opJ(O)
opJ(NO)
opJ(B)
opJ(NB)
opJ(E)
opJ(NE)
opJ(BE)
opJ(NBE)
opJ(S)
opJ(NS)
opJ(P)
opJ(NP)
opJ(L)
opJ(NL)
opJ(LE)
opJ(NLE)



static int opLOOPNE_w(uint32_t fetchdat)
{
        int8_t offset = (int8_t)getbytef();
        CX--;
        CLOCK_CYCLES((is486) ? 7 : 11);
        if (CX && !ZF_SET())
        {
                cpu_state.pc += offset;
                CPU_BLOCK_END();
                return 1;
        }
        return 0;
}
static int opLOOPNE_l(uint32_t fetchdat)
{
        int8_t offset = (int8_t)getbytef();
        ECX--;
        CLOCK_CYCLES((is486) ? 7 : 11);
        if (ECX && !ZF_SET()) 
        {
                cpu_state.pc += offset;
                CPU_BLOCK_END();
                return 1;
        }
        return 0;
}

static int opLOOPE_w(uint32_t fetchdat)
{
        int8_t offset = (int8_t)getbytef();
        CX--;
        CLOCK_CYCLES((is486) ? 7 : 11);
        if (CX && ZF_SET())
        {
                cpu_state.pc += offset;
                CPU_BLOCK_END();
                return 1;
        }
        return 0;
}
static int opLOOPE_l(uint32_t fetchdat)
{
        int8_t offset = (int8_t)getbytef();
        ECX--;
        CLOCK_CYCLES((is486) ? 7 : 11);
        if (ECX && ZF_SET())
        {
                cpu_state.pc += offset;
                CPU_BLOCK_END();
                return 1;
        }
        return 0;
}

static int opLOOP_w(uint32_t fetchdat)
{
        int8_t offset = (int8_t)getbytef();
        CX--;
        CLOCK_CYCLES((is486) ? 7 : 11);
        if (CX)
        {
                cpu_state.pc += offset;
                CPU_BLOCK_END();
                return 1;
        }
        return 0;
}
static int opLOOP_l(uint32_t fetchdat)
{
        int8_t offset = (int8_t)getbytef();
        ECX--;
        CLOCK_CYCLES((is486) ? 7 : 11);
        if (ECX)
        {
                cpu_state.pc += offset;
                CPU_BLOCK_END();
                return 1;
        }
        return 0;
}

static int opJCXZ(uint32_t fetchdat)
{
        int8_t offset = (int8_t)getbytef();
        CLOCK_CYCLES(5);
        if (!CX)
        {
                cpu_state.pc += offset;
                CLOCK_CYCLES(4);
                CPU_BLOCK_END();
                return 1;
        }
        return 0;
}
static int opJECXZ(uint32_t fetchdat)
{
        int8_t offset = (int8_t)getbytef();
        CLOCK_CYCLES(5);
        if (!ECX)
        {
                cpu_state.pc += offset;
                CLOCK_CYCLES(4);
                CPU_BLOCK_END();
                return 1;
        }
        return 0;
}


static int opJMP_r8(uint32_t fetchdat)
{
        int8_t offset = (int8_t)getbytef();
        cpu_state.pc += offset;
        CPU_BLOCK_END();
        CLOCK_CYCLES((is486) ? 3 : 7);
        return 0;
}
static int opJMP_r16(uint32_t fetchdat)
{
        int16_t offset = (int16_t)getwordf();
        cpu_state.pc += offset;
        CPU_BLOCK_END();
        CLOCK_CYCLES((is486) ? 3 : 7);
        return 0;
}
static int opJMP_r32(uint32_t fetchdat)
{
        int32_t offset = (int32_t)getlong();            if (abrt) return 1;
        cpu_state.pc += offset;
        CPU_BLOCK_END();
        CLOCK_CYCLES((is486) ? 3 : 7);
        return 0;
}

static int opJMP_far_a16(uint32_t fetchdat)
{
        uint16_t addr = getwordf();
        uint16_t seg = getword();                       if (abrt) return 1;
        uint32_t oxpc = cpu_state.pc;
        cpu_state.pc = addr;
        loadcsjmp(seg, oxpc);
        CPU_BLOCK_END();
        return 0;
}
static int opJMP_far_a32(uint32_t fetchdat)
{
        uint32_t addr = getlong();
        uint16_t seg = getword();                       if (abrt) return 1;
        uint32_t oxpc = cpu_state.pc;
        cpu_state.pc = addr;
        loadcsjmp(seg, oxpc);
        CPU_BLOCK_END();
        return 0;
}

static int opCALL_r16(uint32_t fetchdat)
{
        int16_t addr = (int16_t)getwordf();
        PUSH_W(cpu_state.pc);
        cpu_state.pc += addr;
        CPU_BLOCK_END();
        CLOCK_CYCLES((is486) ? 3 : 7);
        return 0;
}
static int opCALL_r32(uint32_t fetchdat)
{
        int32_t addr = getlong();                       if (abrt) return 1;       
        PUSH_L(cpu_state.pc);
        cpu_state.pc += addr;
        CPU_BLOCK_END();
        CLOCK_CYCLES((is486) ? 3 : 7);
        return 0;
}

static int opRET_w(uint32_t fetchdat)
{
        uint16_t ret;
        
        ret = POP_W();                          if (abrt) return 1;
        cpu_state.pc = ret;
        CPU_BLOCK_END();
        
        CLOCK_CYCLES((is486) ? 5 : 10);
        return 0;
}
static int opRET_l(uint32_t fetchdat)
{
        uint32_t ret;

        ret = POP_L();                          if (abrt) return 1;
        cpu_state.pc = ret;
        CPU_BLOCK_END();
        
        CLOCK_CYCLES((is486) ? 5 : 10);
        return 0;
}

static int opRET_w_imm(uint32_t fetchdat)
{
        uint16_t offset = getwordf();
        uint16_t ret;

        ret = POP_W();                          if (abrt) return 1;
        if (stack32) ESP += offset;
        else          SP += offset;       
        cpu_state.pc = ret;
        CPU_BLOCK_END();
        
        CLOCK_CYCLES((is486) ? 5 : 10);
        return 0;
}
static int opRET_l_imm(uint32_t fetchdat)
{
        uint16_t offset = getwordf();
        uint32_t ret;

        ret = POP_L();                          if (abrt) return 1;
        if (stack32) ESP += offset;
        else          SP += offset;       
        cpu_state.pc = ret;
        CPU_BLOCK_END();
        
        CLOCK_CYCLES((is486) ? 5 : 10);
        return 0;
}

