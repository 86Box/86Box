#define MMX_GETSHIFT()                                                  \
        if (cpu_mod == 3)                                                   \
        {                                                               \
                shift = cpu_state.MM[cpu_rm].b[0];                                    \
                CLOCK_CYCLES(1);                                        \
        }                                                               \
        else                                                            \
        {                                                               \
                SEG_CHECK_READ(cpu_state.ea_seg);                       \
                shift = readmemb(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 0;    \
                CLOCK_CYCLES(2);                                        \
        }

static int opPSxxW_imm(uint32_t fetchdat)
{
        int reg = fetchdat & 7;
        int op = fetchdat & 0x38;
        int shift = (fetchdat >> 8) & 0xff;

        cpu_state.pc += 2;
        MMX_ENTER();

        switch (op)
        {
                case 0x10: /*PSRLW*/
                if (shift > 15)
                        cpu_state.MM[reg].q = 0;
                else
                {
                        cpu_state.MM[reg].w[0] >>= shift;
                        cpu_state.MM[reg].w[1] >>= shift;
                        cpu_state.MM[reg].w[2] >>= shift;
                        cpu_state.MM[reg].w[3] >>= shift;
                }
                break;
                case 0x20: /*PSRAW*/
                if (shift > 15)
                        shift = 15;
                cpu_state.MM[reg].sw[0] >>= shift;
                cpu_state.MM[reg].sw[1] >>= shift;
                cpu_state.MM[reg].sw[2] >>= shift;
                cpu_state.MM[reg].sw[3] >>= shift;
                break;
                case 0x30: /*PSLLW*/
                if (shift > 15)
                        cpu_state.MM[reg].q = 0;
                else
                {
                        cpu_state.MM[reg].w[0] <<= shift;
                        cpu_state.MM[reg].w[1] <<= shift;
                        cpu_state.MM[reg].w[2] <<= shift;
                        cpu_state.MM[reg].w[3] <<= shift;
                }
                break;
                default:
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                return 0;
        }

        CLOCK_CYCLES(1);
        return 0;
}

static int opPSLLW_a16(uint32_t fetchdat)
{
        int shift;

        MMX_ENTER();

        fetch_ea_16(fetchdat);
        MMX_GETSHIFT();

        if (shift > 15)
                cpu_state.MM[cpu_reg].q = 0;
        else
        {
                cpu_state.MM[cpu_reg].w[0] <<= shift;
                cpu_state.MM[cpu_reg].w[1] <<= shift;
                cpu_state.MM[cpu_reg].w[2] <<= shift;
                cpu_state.MM[cpu_reg].w[3] <<= shift;
        }

        return 0;
}
static int opPSLLW_a32(uint32_t fetchdat)
{
        int shift;

        MMX_ENTER();

        fetch_ea_32(fetchdat);
        MMX_GETSHIFT();

        if (shift > 15)
                cpu_state.MM[cpu_reg].q = 0;
        else
        {
                cpu_state.MM[cpu_reg].w[0] <<= shift;
                cpu_state.MM[cpu_reg].w[1] <<= shift;
                cpu_state.MM[cpu_reg].w[2] <<= shift;
                cpu_state.MM[cpu_reg].w[3] <<= shift;
        }

        return 0;
}

static int opPSRLW_a16(uint32_t fetchdat)
{
        int shift;

        MMX_ENTER();

        fetch_ea_16(fetchdat);
        MMX_GETSHIFT();

        if (shift > 15)
                cpu_state.MM[cpu_reg].q = 0;
        else
        {
                cpu_state.MM[cpu_reg].w[0] >>= shift;
                cpu_state.MM[cpu_reg].w[1] >>= shift;
                cpu_state.MM[cpu_reg].w[2] >>= shift;
                cpu_state.MM[cpu_reg].w[3] >>= shift;
        }

        return 0;
}
static int opPSRLW_a32(uint32_t fetchdat)
{
        int shift;

        MMX_ENTER();

        fetch_ea_32(fetchdat);
        MMX_GETSHIFT();

        if (shift > 15)
                cpu_state.MM[cpu_reg].q = 0;
        else
        {
                cpu_state.MM[cpu_reg].w[0] >>= shift;
                cpu_state.MM[cpu_reg].w[1] >>= shift;
                cpu_state.MM[cpu_reg].w[2] >>= shift;
                cpu_state.MM[cpu_reg].w[3] >>= shift;
        }

        return 0;
}

static int opPSRAW_a16(uint32_t fetchdat)
{
        int shift;

        MMX_ENTER();

        fetch_ea_16(fetchdat);
        MMX_GETSHIFT();

        if (shift > 15)
                shift = 15;

        cpu_state.MM[cpu_reg].sw[0] >>= shift;
        cpu_state.MM[cpu_reg].sw[1] >>= shift;
        cpu_state.MM[cpu_reg].sw[2] >>= shift;
        cpu_state.MM[cpu_reg].sw[3] >>= shift;

        return 0;
}
static int opPSRAW_a32(uint32_t fetchdat)
{
        int shift;

        MMX_ENTER();

        fetch_ea_32(fetchdat);
        MMX_GETSHIFT();

        if (shift > 15)
                shift = 15;

        cpu_state.MM[cpu_reg].sw[0] >>= shift;
        cpu_state.MM[cpu_reg].sw[1] >>= shift;
        cpu_state.MM[cpu_reg].sw[2] >>= shift;
        cpu_state.MM[cpu_reg].sw[3] >>= shift;

        return 0;
}

static int opPSxxD_imm(uint32_t fetchdat)
{
        int reg = fetchdat & 7;
        int op = fetchdat & 0x38;
        int shift = (fetchdat >> 8) & 0xff;

        cpu_state.pc += 2;
        MMX_ENTER();

        switch (op)
        {
                case 0x10: /*PSRLD*/
                if (shift > 31)
                        cpu_state.MM[reg].q = 0;
                else
                {
                        cpu_state.MM[reg].l[0] >>= shift;
                        cpu_state.MM[reg].l[1] >>= shift;
                }
                break;
                case 0x20: /*PSRAD*/
                if (shift > 31)
                        shift = 31;
                cpu_state.MM[reg].sl[0] >>= shift;
                cpu_state.MM[reg].sl[1] >>= shift;
                break;
                case 0x30: /*PSLLD*/
                if (shift > 31)
                        cpu_state.MM[reg].q = 0;
                else
                {
                        cpu_state.MM[reg].l[0] <<= shift;
                        cpu_state.MM[reg].l[1] <<= shift;
                }
                break;
                default:
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                return 0;
        }

        CLOCK_CYCLES(1);
        return 0;
}

static int opPSLLD_a16(uint32_t fetchdat)
{
        int shift;

        MMX_ENTER();

        fetch_ea_16(fetchdat);
        MMX_GETSHIFT();

        if (shift > 31)
                cpu_state.MM[cpu_reg].q = 0;
        else
        {
                cpu_state.MM[cpu_reg].l[0] <<= shift;
                cpu_state.MM[cpu_reg].l[1] <<= shift;
        }

        return 0;
}
static int opPSLLD_a32(uint32_t fetchdat)
{
        int shift;

        MMX_ENTER();

        fetch_ea_32(fetchdat);
        MMX_GETSHIFT();

        if (shift > 31)
                cpu_state.MM[cpu_reg].q = 0;
        else
        {
                cpu_state.MM[cpu_reg].l[0] <<= shift;
                cpu_state.MM[cpu_reg].l[1] <<= shift;
        }

        return 0;
}

static int opPSRLD_a16(uint32_t fetchdat)
{
        int shift;

        MMX_ENTER();

        fetch_ea_16(fetchdat);
        MMX_GETSHIFT();

        if (shift > 31)
                cpu_state.MM[cpu_reg].q = 0;
        else
        {
                cpu_state.MM[cpu_reg].l[0] >>= shift;
                cpu_state.MM[cpu_reg].l[1] >>= shift;
        }

        return 0;
}
static int opPSRLD_a32(uint32_t fetchdat)
{
        int shift;

        MMX_ENTER();

        fetch_ea_32(fetchdat);
        MMX_GETSHIFT();

        if (shift > 31)
                cpu_state.MM[cpu_reg].q = 0;
        else
        {
                cpu_state.MM[cpu_reg].l[0] >>= shift;
                cpu_state.MM[cpu_reg].l[1] >>= shift;
        }

        return 0;
}

static int opPSRAD_a16(uint32_t fetchdat)
{
        int shift;

        MMX_ENTER();

        fetch_ea_16(fetchdat);
        MMX_GETSHIFT();

        if (shift > 31)
                shift = 31;

        cpu_state.MM[cpu_reg].sl[0] >>= shift;
        cpu_state.MM[cpu_reg].sl[1] >>= shift;

        return 0;
}
static int opPSRAD_a32(uint32_t fetchdat)
{
        int shift;

        MMX_ENTER();

        fetch_ea_32(fetchdat);
        MMX_GETSHIFT();

        if (shift > 31)
                shift = 31;

        cpu_state.MM[cpu_reg].sl[0] >>= shift;
        cpu_state.MM[cpu_reg].sl[1] >>= shift;

        return 0;
}

static int opPSxxQ_imm(uint32_t fetchdat)
{
        int reg = fetchdat & 7;
        int op = fetchdat & 0x38;
        int shift = (fetchdat >> 8) & 0xff;

        cpu_state.pc += 2;
        MMX_ENTER();

        switch (op)
        {
                case 0x10: /*PSRLW*/
                if (shift > 63)
                        cpu_state.MM[reg].q = 0;
                else
                        cpu_state.MM[reg].q >>= shift;
                break;
                case 0x20: /*PSRAW*/
                if (shift > 63)
                        shift = 63;
                cpu_state.MM[reg].sq >>= shift;
                break;
                case 0x30: /*PSLLW*/
                if (shift > 63)
                        cpu_state.MM[reg].q = 0;
                else
                        cpu_state.MM[reg].q <<= shift;
                break;
                default:
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                return 0;
        }

        CLOCK_CYCLES(1);
        return 0;
}

static int opPSLLQ_a16(uint32_t fetchdat)
{
        int shift;

        MMX_ENTER();

        fetch_ea_16(fetchdat);
        MMX_GETSHIFT();

        if (shift > 63)
                cpu_state.MM[cpu_reg].q = 0;
        else
                cpu_state.MM[cpu_reg].q <<= shift;

        return 0;
}
static int opPSLLQ_a32(uint32_t fetchdat)
{
        int shift;

        MMX_ENTER();

        fetch_ea_32(fetchdat);
        MMX_GETSHIFT();

        if (shift > 63)
                cpu_state.MM[cpu_reg].q = 0;
        else
                cpu_state.MM[cpu_reg].q <<= shift;

        return 0;
}

static int opPSRLQ_a16(uint32_t fetchdat)
{
        int shift;

        MMX_ENTER();

        fetch_ea_16(fetchdat);
        MMX_GETSHIFT();

        if (shift > 63)
                cpu_state.MM[cpu_reg].q = 0;
        else
                cpu_state.MM[cpu_reg].q >>= shift;

        return 0;
}
static int opPSRLQ_a32(uint32_t fetchdat)
{
        int shift;

        MMX_ENTER();

        fetch_ea_32(fetchdat);
        MMX_GETSHIFT();

        if (shift > 63)
                cpu_state.MM[cpu_reg].q = 0;
        else
                cpu_state.MM[cpu_reg].q >>= shift;

        return 0;
}
