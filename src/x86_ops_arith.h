#define OP_ARITH(name, operation, setflags, flagops, gettempc)   \
        static int op ## name ## _b_rmw_a16(uint32_t fetchdat)                                         \
        {                                                                                       \
                uint8_t dst;                                                                    \
                uint8_t src;                                                                    \
                if (gettempc) tempc = CF_SET() ? 1 : 0;                                         \
                fetch_ea_16(fetchdat);                                                          \
                if (mod == 3)                                                                   \
                {                                                                               \
                        dst = getr8(rm);                                                        \
                        src = getr8(reg);                                                       \
                        setflags ## 8 flagops;                                                  \
                        setr8(rm, operation);                                                   \
                        CLOCK_CYCLES(timing_rr);                                                \
                }                                                                               \
                else                                                                            \
                {                                                                               \
                        dst = geteab();                         if (abrt) return 1;             \
                        src = getr8(reg);                                                       \
                        seteab(operation);                              if (abrt) return 1;     \
                        setflags ## 8 flagops;                                                  \
                        CLOCK_CYCLES(timing_mr);                                                \
                }                                                                               \
                return 0;                                                                       \
        }                                                                                       \
        static int op ## name ## _b_rmw_a32(uint32_t fetchdat)                                         \
        {                                                                                       \
                uint8_t dst;                                                                    \
                uint8_t src;                                                                    \
                if (gettempc) tempc = CF_SET() ? 1 : 0;                                         \
                fetch_ea_32(fetchdat);                                                          \
                if (mod == 3)                                                                   \
                {                                                                               \
                        dst = getr8(rm);                                                        \
                        src = getr8(reg);                                                       \
                        setflags ## 8 flagops;                                                  \
                        setr8(rm, operation);                                                   \
                        CLOCK_CYCLES(timing_rr);                                                \
                }                                                                               \
                else                                                                            \
                {                                                                               \
                        dst = geteab();                         if (abrt) return 1;             \
                        src = getr8(reg);                                                       \
                        seteab(operation);                              if (abrt) return 1;     \
                        setflags ## 8 flagops;                                                  \
                        CLOCK_CYCLES(timing_mr);                                                \
                }                                                                               \
                return 0;                                                                       \
        }                                                                                       \
                                                                                                \
        static int op ## name ## _w_rmw_a16(uint32_t fetchdat)                                         \
        {                                                                                       \
                uint16_t dst;                                                                   \
                uint16_t src;                                                                   \
                if (gettempc) tempc = CF_SET() ? 1 : 0;                                         \
                fetch_ea_16(fetchdat);                                                          \
                if (mod == 3)                                                                   \
                {                                                                               \
                        dst = cpu_state.regs[rm].w;                                             \
                        src = cpu_state.regs[reg].w;                                            \
                        setflags ## 16 flagops;                                                 \
                        cpu_state.regs[rm].w = operation;                                       \
                        CLOCK_CYCLES(timing_rr);                                                \
                }                                                                               \
                else                                                                            \
                {                                                                               \
                        dst = geteaw();                        if (abrt) return 1;              \
                        src = cpu_state.regs[reg].w;                                            \
                        seteaw(operation);                              if (abrt) return 1;     \
                        setflags ## 16 flagops;                                                 \
                        CLOCK_CYCLES(timing_mr);                                                \
                }                                                                               \
                return 0;                                                                       \
        }                                                                                       \
        static int op ## name ## _w_rmw_a32(uint32_t fetchdat)                                  \
        {                                                                                       \
                uint16_t dst;                                                                   \
                uint16_t src;                                                                   \
                if (gettempc) tempc = CF_SET() ? 1 : 0;                                         \
                fetch_ea_32(fetchdat);                                                          \
                if (mod == 3)                                                                   \
                {                                                                               \
                        dst = cpu_state.regs[rm].w;                                             \
                        src = cpu_state.regs[reg].w;                                            \
                        setflags ## 16 flagops;                                                 \
                        cpu_state.regs[rm].w = operation;                                       \
                        CLOCK_CYCLES(timing_rr);                                                \
                }                                                                               \
                else                                                                            \
                {                                                                               \
                        dst = geteaw();                        if (abrt) return 1;              \
                        src = cpu_state.regs[reg].w;                                            \
                        seteaw(operation);                              if (abrt) return 1;     \
                        setflags ## 16 flagops;                                                 \
                        CLOCK_CYCLES(timing_mr);                                                \
                }                                                                               \
                return 0;                                                                       \
        }                                                                                       \
                                                                                                \
        static int op ## name ## _l_rmw_a16(uint32_t fetchdat)                                         \
        {                                                                                       \
                uint32_t dst;                                                                   \
                uint32_t src;                                                                   \
                if (gettempc) tempc = CF_SET() ? 1 : 0;                                         \
                fetch_ea_16(fetchdat);                                                          \
                if (mod == 3)                                                                   \
                {                                                                               \
                        dst = cpu_state.regs[rm].l;                                             \
                        src = cpu_state.regs[reg].l;                                            \
                        setflags ## 32 flagops;                                                 \
                        cpu_state.regs[rm].l = operation;                                       \
                        CLOCK_CYCLES(timing_rr);                                                \
                }                                                                               \
                else                                                                            \
                {                                                                               \
                        dst = geteal();                        if (abrt) return 1;              \
                        src = cpu_state.regs[reg].l;                                            \
                        seteal(operation);                              if (abrt) return 1;     \
                        setflags ## 32 flagops;                                                 \
                        CLOCK_CYCLES(timing_mrl);                                               \
                }                                                                               \
                return 0;                                                                       \
        }                                                                                       \
        static int op ## name ## _l_rmw_a32(uint32_t fetchdat)                                         \
        {                                                                                       \
                uint32_t dst;                                                                   \
                uint32_t src;                                                                   \
                if (gettempc) tempc = CF_SET() ? 1 : 0;                                         \
                fetch_ea_32(fetchdat);                                                          \
                if (mod == 3)                                                                   \
                {                                                                               \
                        dst = cpu_state.regs[rm].l;                                             \
                        src = cpu_state.regs[reg].l;                                            \
                        setflags ## 32 flagops;                                                 \
                        cpu_state.regs[rm].l = operation;                                       \
                        CLOCK_CYCLES(timing_rr);                                                \
                }                                                                               \
                else                                                                            \
                {                                                                               \
                        dst = geteal();                        if (abrt) return 1;              \
                        src = cpu_state.regs[reg].l;                                            \
                        seteal(operation);                              if (abrt) return 1;     \
                        setflags ## 32 flagops;                                                 \
                        CLOCK_CYCLES(timing_mrl);                                               \
                }                                                                               \
                return 0;                                                                       \
        }                                                                                       \
                                                                                                \
        static int op ## name ## _b_rm_a16(uint32_t fetchdat)                                          \
        {                                                                                       \
                uint8_t dst, src;                                                               \
                if (gettempc) tempc = CF_SET() ? 1 : 0;                                         \
                fetch_ea_16(fetchdat);                                                          \
                dst = getr8(reg);                                                               \
                src = geteab();                                         if (abrt) return 1;     \
                setflags ## 8 flagops;                                                          \
                setr8(reg, operation);                                                          \
                CLOCK_CYCLES((mod == 3) ? timing_rr : timing_rm);                               \
                return 0;                                                                       \
        }                                                                                       \
        static int op ## name ## _b_rm_a32(uint32_t fetchdat)                                          \
        {                                                                                       \
                uint8_t dst, src;                                                               \
                if (gettempc) tempc = CF_SET() ? 1 : 0;                                         \
                fetch_ea_32(fetchdat);                                                          \
                dst = getr8(reg);                                                               \
                src = geteab();                                         if (abrt) return 1;     \
                setflags ## 8 flagops;                                                          \
                setr8(reg, operation);                                                          \
                CLOCK_CYCLES((mod == 3) ? timing_rr : timing_rm);                               \
                return 0;                                                                       \
        }                                                                                       \
                                                                                                \
        static int op ## name ## _w_rm_a16(uint32_t fetchdat)                                          \
        {                                                                                       \
                uint16_t dst, src;                                                              \
                if (gettempc) tempc = CF_SET() ? 1 : 0;                                         \
                fetch_ea_16(fetchdat);                                                          \
                dst = cpu_state.regs[reg].w;                                                    \
                src = geteaw();                                 if (abrt) return 1;             \
                setflags ## 16 flagops;                                                         \
                cpu_state.regs[reg].w = operation;                                              \
                CLOCK_CYCLES((mod == 3) ? timing_rr : timing_rm);                               \
                return 0;                                                                       \
        }                                                                                       \
        static int op ## name ## _w_rm_a32(uint32_t fetchdat)                                          \
        {                                                                                       \
                uint16_t dst, src;                                                              \
                if (gettempc) tempc = CF_SET() ? 1 : 0;                                         \
                fetch_ea_32(fetchdat);                                                          \
                dst = cpu_state.regs[reg].w;                                                    \
                src = geteaw();                                 if (abrt) return 1;             \
                setflags ## 16 flagops;                                                         \
                cpu_state.regs[reg].w = operation;                                              \
                CLOCK_CYCLES((mod == 3) ? timing_rr : timing_rm);                               \
                return 0;                                                                       \
        }                                                                                       \
                                                                                                \
        static int op ## name ## _l_rm_a16(uint32_t fetchdat)                                          \
        {                                                                                       \
                uint32_t dst, src;                                                              \
                if (gettempc) tempc = CF_SET() ? 1 : 0;                                         \
                fetch_ea_16(fetchdat);                                                          \
                dst = cpu_state.regs[reg].l;                                                    \
                src = geteal();                                 if (abrt) return 1;             \
                setflags ## 32 flagops;                                                         \
                cpu_state.regs[reg].l = operation;                                              \
                CLOCK_CYCLES((mod == 3) ? timing_rr : timing_rml);                              \
                return 0;                                                                       \
        }                                                                                       \
        static int op ## name ## _l_rm_a32(uint32_t fetchdat)                                          \
        {                                                                                       \
                uint32_t dst, src;                                                              \
                if (gettempc) tempc = CF_SET() ? 1 : 0;                                         \
                fetch_ea_32(fetchdat);                                                          \
                dst = cpu_state.regs[reg].l;                                                    \
                src = geteal();                                 if (abrt) return 1;             \
                setflags ## 32 flagops;                                                         \
                cpu_state.regs[reg].l = operation;                                              \
                CLOCK_CYCLES((mod == 3) ? timing_rr : timing_rml);                              \
                return 0;                                                                       \
        }                                                                                       \
                                                                                                \
        static int op ## name ## _AL_imm(uint32_t fetchdat)                                            \
        {                                                                                       \
                uint8_t dst = AL;                                                               \
                uint8_t src = getbytef();                                                       \
                if (gettempc) tempc = CF_SET() ? 1 : 0;                                         \
                setflags ## 8 flagops;                                                          \
                AL = operation;                                                                 \
                CLOCK_CYCLES(timing_rr);                                                        \
                return 0;                                                                       \
        }                                                                                       \
                                                                                                \
        static int op ## name ## _AX_imm(uint32_t fetchdat)                                            \
        {                                                                                       \
                uint16_t dst = AX;                                                              \
                uint16_t src = getwordf();                                                      \
                if (gettempc) tempc = CF_SET() ? 1 : 0;                                         \
                setflags ## 16 flagops;                                                         \
                AX = operation;                                                                 \
                CLOCK_CYCLES(timing_rr);                                                        \
                return 0;                                                                       \
        }                                                                                       \
                                                                                                \
        static int op ## name ## _EAX_imm(uint32_t fetchdat)                                           \
        {                                                                                       \
                uint32_t dst = EAX;                                                             \
                uint32_t src = getlong(); if (abrt) return 1;                                   \
                if (gettempc) tempc = CF_SET() ? 1 : 0;                                         \
                setflags ## 32 flagops;                                                         \
                EAX = operation;                                                                \
                CLOCK_CYCLES(timing_rr);                                                        \
                return 0;                                                                       \
        }

OP_ARITH(ADD, dst + src,           setadd, (dst, src), 0)
OP_ARITH(ADC, dst + src + tempc,   setadc, (dst, src), 1)
OP_ARITH(SUB, dst - src,           setsub, (dst, src), 0)
OP_ARITH(SBB, dst - (src + tempc), setsbc, (dst, src), 1)
OP_ARITH(OR,  dst | src,           setznp, (dst | src), 0)
OP_ARITH(AND, dst & src,           setznp, (dst & src), 0)
OP_ARITH(XOR, dst ^ src,           setznp, (dst ^ src), 0)

static int opCMP_b_rmw_a16(uint32_t fetchdat)
{
        uint8_t dst;
        fetch_ea_16(fetchdat);
        dst = geteab();                                         if (abrt) return 1;
        setsub8(dst, getr8(reg));
        if (is486) CLOCK_CYCLES((mod == 3) ? 1 : 2);
        else       CLOCK_CYCLES((mod == 3) ? 2 : 5);
        return 0;
}
static int opCMP_b_rmw_a32(uint32_t fetchdat)                                         
{                                                                                       
        uint8_t dst;
        fetch_ea_32(fetchdat);
        dst = geteab();                                         if (abrt) return 1;
        setsub8(dst, getr8(reg));
        if (is486) CLOCK_CYCLES((mod == 3) ? 1 : 2);
        else       CLOCK_CYCLES((mod == 3) ? 2 : 5);
        return 0;
}                                                                                       
                                                                                                
static int opCMP_w_rmw_a16(uint32_t fetchdat)                                         
{                                                                                       
        uint16_t dst;
        fetch_ea_16(fetchdat);
        dst = geteaw();                                         if (abrt) return 1;
        setsub16(dst, cpu_state.regs[reg].w);
        if (is486) CLOCK_CYCLES((mod == 3) ? 1 : 2);
        else       CLOCK_CYCLES((mod == 3) ? 2 : 5);
        return 0;
}                                                                                       
static int opCMP_w_rmw_a32(uint32_t fetchdat)                                         
{                                                                                       
        uint16_t dst;
        fetch_ea_32(fetchdat);
        dst = geteaw();                                         if (abrt) return 1;
        setsub16(dst, cpu_state.regs[reg].w);
        if (is486) CLOCK_CYCLES((mod == 3) ? 1 : 2);
        else       CLOCK_CYCLES((mod == 3) ? 2 : 5);
        return 0;
}                                                                                       
                                                                                                
static int opCMP_l_rmw_a16(uint32_t fetchdat)                                         
{                                                                                       
        uint32_t dst;
        fetch_ea_16(fetchdat);
        dst = geteal();                                         if (abrt) return 1;
        setsub32(dst, cpu_state.regs[reg].l);
        if (is486) CLOCK_CYCLES((mod == 3) ? 1 : 2);
        else       CLOCK_CYCLES((mod == 3) ? 2 : 5);
        return 0;
}                                                                                       
static int opCMP_l_rmw_a32(uint32_t fetchdat)                                         
{                                                                                       
        uint32_t dst;
        fetch_ea_32(fetchdat);
        dst = geteal();                                         if (abrt) return 1;
        setsub32(dst, cpu_state.regs[reg].l);
        if (is486) CLOCK_CYCLES((mod == 3) ? 1 : 2);
        else       CLOCK_CYCLES((mod == 3) ? 2 : 5);
        return 0;
}                                                                                       
                                                                                                
static int opCMP_b_rm_a16(uint32_t fetchdat)                                          
{                                                                                       
        uint8_t src;                                                               
        fetch_ea_16(fetchdat);                                                          
        src = geteab();                                         if (abrt) return 1;     
        setsub8(getr8(reg), src);
        CLOCK_CYCLES((mod == 3) ? timing_rr : timing_rm);
        return 0;                                                                       
}                                                                                       
static int opCMP_b_rm_a32(uint32_t fetchdat)                                          
{                                                                                       
        uint8_t src;                                                               
        fetch_ea_32(fetchdat);                                                          
        src = geteab();                                         if (abrt) return 1;     
        setsub8(getr8(reg), src);
        CLOCK_CYCLES((mod == 3) ? timing_rr : timing_rm);
        return 0;                                                                       
}                                                                                       
                                                                                                
static int opCMP_w_rm_a16(uint32_t fetchdat)                                          
{                                                                                       
        uint16_t src;                                                              
        fetch_ea_16(fetchdat);                                                          
        src = geteaw();                                 if (abrt) return 1;             
        setsub16(cpu_state.regs[reg].w, src);
        CLOCK_CYCLES((mod == 3) ? timing_rr : timing_rm);
        return 0;                                                                       
}                                                                                       
static int opCMP_w_rm_a32(uint32_t fetchdat)                                          
{                                                                                       
        uint16_t src;                                                              
        fetch_ea_32(fetchdat);                                                          
        src = geteaw();                                 if (abrt) return 1;             
        setsub16(cpu_state.regs[reg].w, src);
        CLOCK_CYCLES((mod == 3) ? timing_rr : timing_rm);
        return 0;                                                                       
}                                                                                       
                                                                                                
static int opCMP_l_rm_a16(uint32_t fetchdat)                                          
{                                                                                       
        uint32_t src;                                                              
        fetch_ea_16(fetchdat);                                                          
        src = geteal();                                 if (abrt) return 1;             
        setsub32(cpu_state.regs[reg].l, src);
        CLOCK_CYCLES((mod == 3) ? timing_rr : timing_rml);
        return 0;                                                                       
}                                                                                       
static int opCMP_l_rm_a32(uint32_t fetchdat)                                          
{                                                                                       
        uint32_t src;
        fetch_ea_32(fetchdat);                                                          
        src = geteal();                                 if (abrt) return 1;             
        setsub32(cpu_state.regs[reg].l, src);
        CLOCK_CYCLES((mod == 3) ? timing_rr : timing_rml);
        return 0;                                                                       
}                                                                                       
                                                                                                
static int opCMP_AL_imm(uint32_t fetchdat)                                            
{                                                                                       
        uint8_t src = getbytef();                                                       
        setsub8(AL, src);
        CLOCK_CYCLES(timing_rr);
        return 0;                                                                       
}                                                                                       
                                                                                                
static int opCMP_AX_imm(uint32_t fetchdat)                                            
{                                                                                       
        uint16_t src = getwordf();                                                      
        setsub16(AX, src);
        CLOCK_CYCLES(timing_rr);
        return 0;                                                                       
}                                                                                       
                                                                                                
static int opCMP_EAX_imm(uint32_t fetchdat)                                           
{                                                                                       
        uint32_t src = getlong(); if (abrt) return 1;
        setsub32(EAX, src);
        CLOCK_CYCLES(timing_rr);
        return 0;                                                                       
}

static int opTEST_b_a16(uint32_t fetchdat)
{
        uint8_t temp, temp2;
        fetch_ea_16(fetchdat);
        temp = geteab();                                if (abrt) return 1;
        temp2 = getr8(reg);
        setznp8(temp & temp2);
        if (is486) CLOCK_CYCLES((mod == 3) ? 1 : 2);
        else       CLOCK_CYCLES((mod == 3) ? 2 : 5);
        return 0;
}
static int opTEST_b_a32(uint32_t fetchdat)
{
        uint8_t temp, temp2;
        fetch_ea_32(fetchdat);
        temp = geteab();                                if (abrt) return 1;
        temp2 = getr8(reg);
        setznp8(temp & temp2);
        if (is486) CLOCK_CYCLES((mod == 3) ? 1 : 2);
        else       CLOCK_CYCLES((mod == 3) ? 2 : 5);
        return 0;
}

static int opTEST_w_a16(uint32_t fetchdat)
{
        uint16_t temp, temp2;
        fetch_ea_16(fetchdat);
        temp = geteaw();                                if (abrt) return 1;
        temp2 = cpu_state.regs[reg].w;
        setznp16(temp & temp2);
        if (is486) CLOCK_CYCLES((mod == 3) ? 1 : 2);
        else       CLOCK_CYCLES((mod == 3) ? 2 : 5);
        return 0;
}
static int opTEST_w_a32(uint32_t fetchdat)
{
        uint16_t temp, temp2;
        fetch_ea_32(fetchdat);
        temp = geteaw();                                if (abrt) return 1;
        temp2 = cpu_state.regs[reg].w;
        setznp16(temp & temp2);
        if (is486) CLOCK_CYCLES((mod == 3) ? 1 : 2);
        else       CLOCK_CYCLES((mod == 3) ? 2 : 5);
        return 0;
}

static int opTEST_l_a16(uint32_t fetchdat)
{
        uint32_t temp, temp2;
        fetch_ea_16(fetchdat);
        temp = geteal();                                if (abrt) return 1;
        temp2 = cpu_state.regs[reg].l;
        setznp32(temp & temp2);
        if (is486) CLOCK_CYCLES((mod == 3) ? 1 : 2);
        else       CLOCK_CYCLES((mod == 3) ? 2 : 5);
        return 0;
}
static int opTEST_l_a32(uint32_t fetchdat)
{
        uint32_t temp, temp2;
        fetch_ea_32(fetchdat);
        temp = geteal();                                if (abrt) return 1;
        temp2 = cpu_state.regs[reg].l;
        setznp32(temp & temp2);
        if (is486) CLOCK_CYCLES((mod == 3) ? 1 : 2);
        else       CLOCK_CYCLES((mod == 3) ? 2 : 5);
        return 0;
}

static int opTEST_AL(uint32_t fetchdat)
{
        uint8_t temp = getbytef();
        setznp8(AL & temp);
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opTEST_AX(uint32_t fetchdat)
{
        uint16_t temp = getwordf();
        setznp16(AX & temp);
        CLOCK_CYCLES(timing_rr);
        return 0;
}
static int opTEST_EAX(uint32_t fetchdat)
{
        uint32_t temp = getlong();                      if (abrt) return 1;
        setznp32(EAX & temp);
        CLOCK_CYCLES(timing_rr);
        return 0;
}


#define ARITH_MULTI(ea_width, flag_width)                                       \
        dst = getea ## ea_width();                      if (abrt) return 1;     \
        switch (rmdat&0x38)                                                     \
        {                                                                       \
                case 0x00: /*ADD ea, #*/                                        \
                setea ## ea_width(dst + src);           if (abrt) return 1;     \
                setadd ## flag_width(dst, src);                                 \
                CLOCK_CYCLES((mod == 3) ? timing_rr : timing_mr);               \
                break;                                                          \
                case 0x08: /*OR ea, #*/                                         \
                dst |= src;                                                     \
                setea ## ea_width(dst);                 if (abrt) return 1;     \
                setznp ## flag_width(dst);                                      \
                CLOCK_CYCLES((mod == 3) ? timing_rr : timing_mr);               \
                break;                                                          \
                case 0x10: /*ADC ea, #*/                                        \
                tempc = CF_SET() ? 1 : 0;                                       \
                setea ## ea_width(dst + src + tempc);   if (abrt) return 1;     \
                setadc ## flag_width(dst, src);                                 \
                CLOCK_CYCLES((mod == 3) ? timing_rr : timing_mr);               \
                break;                                                          \
                case 0x18: /*SBB ea, #*/                                        \
                tempc = CF_SET() ? 1 : 0;                                       \
                setea ## ea_width(dst - (src + tempc)); if (abrt) return 1;     \
                setsbc ## flag_width(dst, src);                                 \
                CLOCK_CYCLES((mod == 3) ? timing_rr : timing_mr);               \
                break;                                                          \
                case 0x20: /*AND ea, #*/                                        \
                dst &= src;                                                     \
                setea ## ea_width(dst);                 if (abrt) return 1;     \
                setznp ## flag_width(dst);                                      \
                CLOCK_CYCLES((mod == 3) ? timing_rr : timing_mr);               \
                break;                                                          \
                case 0x28: /*SUB ea, #*/                                        \
                setea ## ea_width(dst - src);           if (abrt) return 1;     \
                setsub ## flag_width(dst, src);                                 \
                CLOCK_CYCLES((mod == 3) ? timing_rr : timing_mr);               \
                break;                                                          \
                case 0x30: /*XOR ea, #*/                                        \
                dst ^= src;                                                     \
                setea ## ea_width(dst);                 if (abrt) return 1;     \
                setznp ## flag_width(dst);                                      \
                CLOCK_CYCLES((mod == 3) ? timing_rr : timing_mr);               \
                break;                                                          \
                case 0x38: /*CMP ea, #*/                                        \
                setsub ## flag_width(dst, src);                                 \
                if (is486) CLOCK_CYCLES((mod == 3) ? 1 : 2);                    \
                else       CLOCK_CYCLES((mod == 3) ? 2 : 7);                    \
                break;                                                          \
        }


static int op80_a16(uint32_t fetchdat)
{
        uint8_t src, dst;
        
        fetch_ea_16(fetchdat);
        src = getbyte();                        if (abrt) return 1;
        ARITH_MULTI(b, 8);
        
        return 0;
}
static int op80_a32(uint32_t fetchdat)
{
        uint8_t src, dst;
        
        fetch_ea_32(fetchdat);
        src = getbyte();                        if (abrt) return 1;
        ARITH_MULTI(b, 8);
        
        return 0;
}
static int op81_w_a16(uint32_t fetchdat)
{
        uint16_t src, dst;
        
        fetch_ea_16(fetchdat);
        src = getword();                        if (abrt) return 1;
        ARITH_MULTI(w, 16);
        
        return 0;
}
static int op81_w_a32(uint32_t fetchdat)
{
        uint16_t src, dst;
        
        fetch_ea_32(fetchdat);
        src = getword();                        if (abrt) return 1;
        ARITH_MULTI(w, 16);
        
        return 0;
}
static int op81_l_a16(uint32_t fetchdat)
{
        uint32_t src, dst;
        
        fetch_ea_16(fetchdat);
        src = getlong();                        if (abrt) return 1;
        ARITH_MULTI(l, 32);
        
        return 0;
}
static int op81_l_a32(uint32_t fetchdat)
{
        uint32_t src, dst;
        
        fetch_ea_32(fetchdat);
        src = getlong();                        if (abrt) return 1;
        ARITH_MULTI(l, 32);
        
        return 0;
}

static int op83_w_a16(uint32_t fetchdat)
{
        uint16_t src, dst;
        
        fetch_ea_16(fetchdat);
        src = getbyte();                        if (abrt) return 1;
        if (src & 0x80) src |= 0xff00;
        ARITH_MULTI(w, 16);
        
        return 0;
}
static int op83_w_a32(uint32_t fetchdat)
{
        uint16_t src, dst;
        
        fetch_ea_32(fetchdat);
        src = getbyte();                        if (abrt) return 1;
        if (src & 0x80) src |= 0xff00;
        ARITH_MULTI(w, 16);
        
        return 0;
}

static int op83_l_a16(uint32_t fetchdat)
{
        uint32_t src, dst;
        
        fetch_ea_16(fetchdat);
        src = getbyte();                        if (abrt) return 1;
        if (src & 0x80) src |= 0xffffff00;
        ARITH_MULTI(l, 32);
        
        return 0;
}
static int op83_l_a32(uint32_t fetchdat)
{
        uint32_t src, dst;
        
        fetch_ea_32(fetchdat);
        src = getbyte();                        if (abrt) return 1;
        if (src & 0x80) src |= 0xffffff00;
        ARITH_MULTI(l, 32);
        
        return 0;
}

