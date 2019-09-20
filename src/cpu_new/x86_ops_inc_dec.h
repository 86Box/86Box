#define INC_DEC_OP(name, reg, inc, setflags) \
        static int op ## name (uint32_t fetchdat)       \
        {                                               \
                setflags(reg, 1);                       \
                reg += inc;                             \
                CLOCK_CYCLES(timing_rr);                \
                PREFETCH_RUN(timing_rr, 1, -1, 0,0,0,0, 0); \
                return 0;                               \
        }

INC_DEC_OP(INC_AX, AX, 1, setadd16nc)
INC_DEC_OP(INC_BX, BX, 1, setadd16nc)
INC_DEC_OP(INC_CX, CX, 1, setadd16nc)
INC_DEC_OP(INC_DX, DX, 1, setadd16nc)
INC_DEC_OP(INC_SI, SI, 1, setadd16nc)
INC_DEC_OP(INC_DI, DI, 1, setadd16nc)
INC_DEC_OP(INC_BP, BP, 1, setadd16nc)
INC_DEC_OP(INC_SP, SP, 1, setadd16nc)

INC_DEC_OP(INC_EAX, EAX, 1, setadd32nc)
INC_DEC_OP(INC_EBX, EBX, 1, setadd32nc)
INC_DEC_OP(INC_ECX, ECX, 1, setadd32nc)
INC_DEC_OP(INC_EDX, EDX, 1, setadd32nc)
INC_DEC_OP(INC_ESI, ESI, 1, setadd32nc)
INC_DEC_OP(INC_EDI, EDI, 1, setadd32nc)
INC_DEC_OP(INC_EBP, EBP, 1, setadd32nc)
INC_DEC_OP(INC_ESP, ESP, 1, setadd32nc)

INC_DEC_OP(DEC_AX, AX, -1, setsub16nc)
INC_DEC_OP(DEC_BX, BX, -1, setsub16nc)
INC_DEC_OP(DEC_CX, CX, -1, setsub16nc)
INC_DEC_OP(DEC_DX, DX, -1, setsub16nc)
INC_DEC_OP(DEC_SI, SI, -1, setsub16nc)
INC_DEC_OP(DEC_DI, DI, -1, setsub16nc)
INC_DEC_OP(DEC_BP, BP, -1, setsub16nc)
INC_DEC_OP(DEC_SP, SP, -1, setsub16nc)

INC_DEC_OP(DEC_EAX, EAX, -1, setsub32nc)
INC_DEC_OP(DEC_EBX, EBX, -1, setsub32nc)
INC_DEC_OP(DEC_ECX, ECX, -1, setsub32nc)
INC_DEC_OP(DEC_EDX, EDX, -1, setsub32nc)
INC_DEC_OP(DEC_ESI, ESI, -1, setsub32nc)
INC_DEC_OP(DEC_EDI, EDI, -1, setsub32nc)
INC_DEC_OP(DEC_EBP, EBP, -1, setsub32nc)
INC_DEC_OP(DEC_ESP, ESP, -1, setsub32nc)


static int opINCDEC_b_a16(uint32_t fetchdat)
{
        uint8_t temp;
        
        fetch_ea_16(fetchdat);       
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp=geteab();                  if (cpu_state.abrt) return 1;

        if (rmdat&0x38)
        {
                seteab(temp - 1);       if (cpu_state.abrt) return 1;
                setsub8nc(temp, 1);
        }
        else
        {
                seteab(temp + 1);       if (cpu_state.abrt) return 1;
                setadd8nc(temp, 1);
        }
        CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, 0);
        return 0;
}
static int opINCDEC_b_a32(uint32_t fetchdat)
{
        uint8_t temp;
        
        fetch_ea_32(fetchdat);       
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp=geteab();                  if (cpu_state.abrt) return 1;

        if (rmdat&0x38)
        {
                seteab(temp - 1);       if (cpu_state.abrt) return 1;
                setsub8nc(temp, 1);
        }
        else
        {
                seteab(temp + 1);       if (cpu_state.abrt) return 1;
                setadd8nc(temp, 1);
        }
        CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
        PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, 1);
        return 0;
}
