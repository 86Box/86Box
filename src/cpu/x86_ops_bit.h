static int opBT_w_r_a16(uint32_t fetchdat)
{
        uint16_t temp;

        fetch_ea_16(fetchdat);
        SEG_CHECK_READ(cpu_state.ea_seg);
        cpu_state.eaaddr += ((cpu_state.regs[cpu_reg].w / 16) * 2);     eal_r = 0;
        temp = geteaw();                        if (cpu_state.abrt) return 1;
        flags_rebuild();
        if (temp & (1 << (cpu_state.regs[cpu_reg].w & 15))) cpu_state.flags |=  C_FLAG;
        else                                                cpu_state.flags &= ~C_FLAG;

        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 2, rmdat, 1,0,0,0, 0);
        return 0;
}
static int opBT_w_r_a32(uint32_t fetchdat)
{
        uint16_t temp;

        fetch_ea_32(fetchdat);
        SEG_CHECK_READ(cpu_state.ea_seg);
        cpu_state.eaaddr += ((cpu_state.regs[cpu_reg].w / 16) * 2);     eal_r = 0;
        temp = geteaw();                        if (cpu_state.abrt) return 1;
        flags_rebuild();
        if (temp & (1 << (cpu_state.regs[cpu_reg].w & 15))) cpu_state.flags |=  C_FLAG;
        else                                                cpu_state.flags &= ~C_FLAG;

        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 2, rmdat, 1,0,0,0, 1);
        return 0;
}
static int opBT_l_r_a16(uint32_t fetchdat)
{
        uint32_t temp;

        fetch_ea_16(fetchdat);
        SEG_CHECK_READ(cpu_state.ea_seg);
        cpu_state.eaaddr += ((cpu_state.regs[cpu_reg].l / 32) * 4);     eal_r = 0;
        temp = geteal();                        if (cpu_state.abrt) return 1;
        flags_rebuild();
        if (temp & (1 << (cpu_state.regs[cpu_reg].l & 31))) cpu_state.flags |=  C_FLAG;
        else                                                cpu_state.flags &= ~C_FLAG;

        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 2, rmdat, 0,1,0,0, 0);
        return 0;
}
static int opBT_l_r_a32(uint32_t fetchdat)
{
        uint32_t temp;

        fetch_ea_32(fetchdat);
        SEG_CHECK_READ(cpu_state.ea_seg);
        cpu_state.eaaddr += ((cpu_state.regs[cpu_reg].l / 32) * 4);     eal_r = 0;
        temp = geteal();                        if (cpu_state.abrt) return 1;
        flags_rebuild();
        if (temp & (1 << (cpu_state.regs[cpu_reg].l & 31))) cpu_state.flags |=  C_FLAG;
        else                                                cpu_state.flags &= ~C_FLAG;

        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 2, rmdat, 0,1,0,0, 1);
        return 0;
}

#define opBT(name, operation)                                                   \
        static int opBT ## name ## _w_r_a16(uint32_t fetchdat)                  \
        {                                                                       \
                int tempc;                                                      \
                uint16_t temp;                                                  \
                                                                                \
                fetch_ea_16(fetchdat);                                          \
                if (cpu_mod != 3)                                               \
                        SEG_CHECK_WRITE(cpu_state.ea_seg);                      \
                cpu_state.eaaddr += ((cpu_state.regs[cpu_reg].w / 16) * 2);     eal_r = eal_w = 0;      \
                temp = geteaw();                        if (cpu_state.abrt) return 1;     \
                tempc = (temp & (1 << (cpu_state.regs[cpu_reg].w & 15))) ? 1 : 0;   \
                temp operation (1 << (cpu_state.regs[cpu_reg].w & 15));             \
                seteaw(temp);                           if (cpu_state.abrt) return 1;     \
                flags_rebuild();                                                \
                if (tempc) cpu_state.flags |=  C_FLAG;                                    \
                else       cpu_state.flags &= ~C_FLAG;                                    \
                                                                                \
                CLOCK_CYCLES(6);                                                \
                PREFETCH_RUN(6, 2, rmdat, 1,0,1,0, 0);                          \
                return 0;                                                       \
        }                                                                       \
        static int opBT ## name ## _w_r_a32(uint32_t fetchdat)                  \
        {                                                                       \
                int tempc;                                                      \
                uint16_t temp;                                                  \
                                                                                \
                fetch_ea_32(fetchdat);                                          \
                if (cpu_mod != 3)                                               \
                        SEG_CHECK_WRITE(cpu_state.ea_seg);                      \
                cpu_state.eaaddr += ((cpu_state.regs[cpu_reg].w / 16) * 2);     eal_r = eal_w = 0;      \
                temp = geteaw();                        if (cpu_state.abrt) return 1;     \
                tempc = (temp & (1 << (cpu_state.regs[cpu_reg].w & 15))) ? 1 : 0;   \
                temp operation (1 << (cpu_state.regs[cpu_reg].w & 15));             \
                seteaw(temp);                           if (cpu_state.abrt) return 1;     \
                flags_rebuild();                                                \
                if (tempc) cpu_state.flags |=  C_FLAG;                                    \
                else       cpu_state.flags &= ~C_FLAG;                                    \
                                                                                \
                CLOCK_CYCLES(6);                                                \
                PREFETCH_RUN(6, 2, rmdat, 1,0,1,0, 1);                          \
                return 0;                                                       \
        }                                                                       \
        static int opBT ## name ## _l_r_a16(uint32_t fetchdat)                  \
        {                                                                       \
                int tempc;                                                      \
                uint32_t temp;                                                  \
                                                                                \
                fetch_ea_16(fetchdat);                                          \
                if (cpu_mod != 3)                                               \
                        SEG_CHECK_WRITE(cpu_state.ea_seg);                      \
                cpu_state.eaaddr += ((cpu_state.regs[cpu_reg].l / 32) * 4);     eal_r = eal_w = 0;      \
                temp = geteal();                        if (cpu_state.abrt) return 1;     \
                tempc = (temp & (1 << (cpu_state.regs[cpu_reg].l & 31))) ? 1 : 0;   \
                temp operation (1 << (cpu_state.regs[cpu_reg].l & 31));             \
                seteal(temp);                           if (cpu_state.abrt) return 1;     \
                flags_rebuild();                                                \
                if (tempc) cpu_state.flags |=  C_FLAG;                                    \
                else       cpu_state.flags &= ~C_FLAG;                                    \
                                                                                \
                CLOCK_CYCLES(6);                                                \
                PREFETCH_RUN(6, 2, rmdat, 0,1,0,1, 0);                          \
                return 0;                                                       \
        }                                                                       \
        static int opBT ## name ## _l_r_a32(uint32_t fetchdat)                  \
        {                                                                       \
                int tempc;                                                      \
                uint32_t temp;                                                  \
                                                                                \
                fetch_ea_32(fetchdat);                                          \
                if (cpu_mod != 3)                                               \
                        SEG_CHECK_WRITE(cpu_state.ea_seg);                      \
                cpu_state.eaaddr += ((cpu_state.regs[cpu_reg].l / 32) * 4);     eal_r = eal_w = 0;      \
                temp = geteal();                        if (cpu_state.abrt) return 1;     \
                tempc = (temp & (1 << (cpu_state.regs[cpu_reg].l & 31))) ? 1 : 0;   \
                temp operation (1 << (cpu_state.regs[cpu_reg].l & 31));             \
                seteal(temp);                           if (cpu_state.abrt) return 1;     \
                flags_rebuild();                                                \
                if (tempc) cpu_state.flags |=  C_FLAG;                                    \
                else       cpu_state.flags &= ~C_FLAG;                                    \
                                                                                \
                CLOCK_CYCLES(6);                                                \
                PREFETCH_RUN(6, 2, rmdat, 0,1,0,1, 1);                          \
                return 0;                                                       \
        }

opBT(C, ^=)
opBT(R, &=~)
opBT(S, |=)

static int opBA_w_a16(uint32_t fetchdat)
{
        int tempc, count;
        uint16_t temp;

        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);

        temp = geteaw();
        count = getbyte();                      if (cpu_state.abrt) return 1;
        tempc = temp & (1 << count);
        flags_rebuild();
        switch (rmdat & 0x38)
        {
                case 0x20: /*BT w,imm*/
                if (tempc) cpu_state.flags |=  C_FLAG;
                else       cpu_state.flags &= ~C_FLAG;
                CLOCK_CYCLES(3);
                PREFETCH_RUN(3, 3, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
                return 0;
                case 0x28: /*BTS w,imm*/
                temp |=  (1 << count);
                break;
                case 0x30: /*BTR w,imm*/
                temp &= ~(1 << count);
                break;
                case 0x38: /*BTC w,imm*/
                temp ^=  (1 << count);
                break;

                default:
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                break;
        }
        seteaw(temp);                           if (cpu_state.abrt) return 1;
        if (tempc) cpu_state.flags |=  C_FLAG;
        else       cpu_state.flags &= ~C_FLAG;
        CLOCK_CYCLES(6);
        PREFETCH_RUN(6, 3, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, 0);
        return 0;
}
static int opBA_w_a32(uint32_t fetchdat)
{
        int tempc, count;
        uint16_t temp;

        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);

        temp = geteaw();
        count = getbyte();                      if (cpu_state.abrt) return 1;
        tempc = temp & (1 << count);
        flags_rebuild();
        switch (rmdat & 0x38)
        {
                case 0x20: /*BT w,imm*/
                if (tempc) cpu_state.flags |=  C_FLAG;
                else       cpu_state.flags &= ~C_FLAG;
                CLOCK_CYCLES(3);
                PREFETCH_RUN(3, 3, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 1);
                return 0;
                case 0x28: /*BTS w,imm*/
                temp |=  (1 << count);
                break;
                case 0x30: /*BTR w,imm*/
                temp &= ~(1 << count);
                break;
                case 0x38: /*BTC w,imm*/
                temp ^=  (1 << count);
                break;

                default:
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                break;
        }
        seteaw(temp);                           if (cpu_state.abrt) return 1;
        if (tempc) cpu_state.flags |=  C_FLAG;
        else       cpu_state.flags &= ~C_FLAG;
        CLOCK_CYCLES(6);
        PREFETCH_RUN(6, 3, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, 0);
        return 0;
}

static int opBA_l_a16(uint32_t fetchdat)
{
        int tempc, count;
        uint32_t temp;

        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);

        temp = geteal();
        count = getbyte();                      if (cpu_state.abrt) return 1;
        tempc = temp & (1 << count);
        flags_rebuild();
        switch (rmdat & 0x38)
        {
                case 0x20: /*BT w,imm*/
                if (tempc) cpu_state.flags |=  C_FLAG;
                else       cpu_state.flags &= ~C_FLAG;
                CLOCK_CYCLES(3);
                PREFETCH_RUN(3, 3, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 0);
                return 0;
                case 0x28: /*BTS w,imm*/
                temp |=  (1 << count);
                break;
                case 0x30: /*BTR w,imm*/
                temp &= ~(1 << count);
                break;
                case 0x38: /*BTC w,imm*/
                temp ^=  (1 << count);
                break;

                default:
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                break;
        }
        seteal(temp);                           if (cpu_state.abrt) return 1;
        if (tempc) cpu_state.flags |=  C_FLAG;
        else       cpu_state.flags &= ~C_FLAG;
        CLOCK_CYCLES(6);
        PREFETCH_RUN(6, 3, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, 0);
        return 0;
}
static int opBA_l_a32(uint32_t fetchdat)
{
        int tempc, count;
        uint32_t temp;

        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);

        temp = geteal();
        count = getbyte();                      if (cpu_state.abrt) return 1;
        tempc = temp & (1 << count);
        flags_rebuild();
        switch (rmdat & 0x38)
        {
                case 0x20: /*BT w,imm*/
                if (tempc) cpu_state.flags |=  C_FLAG;
                else       cpu_state.flags &= ~C_FLAG;
                CLOCK_CYCLES(3);
                PREFETCH_RUN(3, 3, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 1);
                return 0;
                case 0x28: /*BTS w,imm*/
                temp |=  (1 << count);
                break;
                case 0x30: /*BTR w,imm*/
                temp &= ~(1 << count);
                break;
                case 0x38: /*BTC w,imm*/
                temp ^=  (1 << count);
                break;

                default:
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                break;
        }
        seteal(temp);                           if (cpu_state.abrt) return 1;
        if (tempc) cpu_state.flags |=  C_FLAG;
        else       cpu_state.flags &= ~C_FLAG;
        CLOCK_CYCLES(6);
        PREFETCH_RUN(6, 3, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, 1);
        return 0;
}
