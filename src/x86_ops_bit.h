static int opBT_w_r_a16(uint32_t fetchdat)
{
        int tempc;
        uint16_t temp;
        
        fetch_ea_16(fetchdat);
        cpu_state.eaaddr += ((cpu_state.regs[cpu_reg].w / 16) * 2);     eal_r = 0;
        temp = geteaw();                        if (abrt) return 1;
        flags_rebuild();
        if (temp & (1 << (cpu_state.regs[cpu_reg].w & 15))) flags |=  C_FLAG;
        else                                            flags &= ~C_FLAG;
        
        CLOCK_CYCLES(3);
        return 0;
}
static int opBT_w_r_a32(uint32_t fetchdat)
{
        uint16_t temp;
        
        fetch_ea_32(fetchdat);
        cpu_state.eaaddr += ((cpu_state.regs[cpu_reg].w / 16) * 2);     eal_r = 0;
        temp = geteaw();                        if (abrt) return 1;
        flags_rebuild();
        if (temp & (1 << (cpu_state.regs[cpu_reg].w & 15))) flags |=  C_FLAG;
        else                                            flags &= ~C_FLAG;
        
        CLOCK_CYCLES(3);
        return 0;
}
static int opBT_l_r_a16(uint32_t fetchdat)
{
        uint32_t temp;
        
        fetch_ea_16(fetchdat);
        cpu_state.eaaddr += ((cpu_state.regs[cpu_reg].l / 32) * 4);     eal_r = 0;
        temp = geteal();                        if (abrt) return 1;
        flags_rebuild();
        if (temp & (1 << (cpu_state.regs[cpu_reg].l & 31))) flags |=  C_FLAG;
        else                                            flags &= ~C_FLAG;
        
        CLOCK_CYCLES(3);
        return 0;
}
static int opBT_l_r_a32(uint32_t fetchdat)
{
        uint32_t temp;
        
        fetch_ea_32(fetchdat);
        cpu_state.eaaddr += ((cpu_state.regs[cpu_reg].l / 32) * 4);     eal_r = 0;
        temp = geteal();                        if (abrt) return 1;
        flags_rebuild();
        if (temp & (1 << (cpu_state.regs[cpu_reg].l & 31))) flags |=  C_FLAG;
        else                                            flags &= ~C_FLAG;
        
        CLOCK_CYCLES(3);
        return 0;
}

#define opBT(name, operation)                                                   \
        static int opBT ## name ## _w_r_a16(uint32_t fetchdat)                  \
        {                                                                       \
                int tempc;                                                      \
                uint16_t temp;                                                  \
                                                                                \
                fetch_ea_16(fetchdat);                                          \
                cpu_state.eaaddr += ((cpu_state.regs[cpu_reg].w / 16) * 2);     eal_r = eal_w = 0;      \
                temp = geteaw();                        if (abrt) return 1;     \
                tempc = (temp & (1 << (cpu_state.regs[cpu_reg].w & 15))) ? 1 : 0;   \
                temp operation (1 << (cpu_state.regs[cpu_reg].w & 15));             \
                seteaw(temp);                           if (abrt) return 1;     \
                flags_rebuild();                                                \
                if (tempc) flags |=  C_FLAG;                                    \
                else       flags &= ~C_FLAG;                                    \
                                                                                \
                CLOCK_CYCLES(6);                                                \
                return 0;                                                       \
        }                                                                       \
        static int opBT ## name ## _w_r_a32(uint32_t fetchdat)                  \
        {                                                                       \
                int tempc;                                                      \
                uint16_t temp;                                                  \
                                                                                \
                fetch_ea_32(fetchdat);                                          \
                cpu_state.eaaddr += ((cpu_state.regs[cpu_reg].w / 16) * 2);     eal_r = eal_w = 0;      \
                temp = geteaw();                        if (abrt) return 1;     \
                tempc = (temp & (1 << (cpu_state.regs[cpu_reg].w & 15))) ? 1 : 0;   \
                temp operation (1 << (cpu_state.regs[cpu_reg].w & 15));             \
                seteaw(temp);                           if (abrt) return 1;     \
                flags_rebuild();                                                \
                if (tempc) flags |=  C_FLAG;                                    \
                else       flags &= ~C_FLAG;                                    \
                                                                                \
                CLOCK_CYCLES(6);                                                \
                return 0;                                                       \
        }                                                                       \
        static int opBT ## name ## _l_r_a16(uint32_t fetchdat)                  \
        {                                                                       \
                int tempc;                                                      \
                uint32_t temp;                                                  \
                                                                                \
                fetch_ea_16(fetchdat);                                          \
                cpu_state.eaaddr += ((cpu_state.regs[cpu_reg].l / 32) * 4);     eal_r = eal_w = 0;      \
                temp = geteal();                        if (abrt) return 1;     \
                tempc = (temp & (1 << (cpu_state.regs[cpu_reg].l & 31))) ? 1 : 0;   \
                temp operation (1 << (cpu_state.regs[cpu_reg].l & 31));             \
                seteal(temp);                           if (abrt) return 1;     \
                flags_rebuild();                                                \
                if (tempc) flags |=  C_FLAG;                                    \
                else       flags &= ~C_FLAG;                                    \
                                                                                \
                CLOCK_CYCLES(6);                                                \
                return 0;                                                       \
        }                                                                       \
        static int opBT ## name ## _l_r_a32(uint32_t fetchdat)                  \
        {                                                                       \
                int tempc;                                                      \
                uint32_t temp;                                                  \
                                                                                \
                fetch_ea_32(fetchdat);                                          \
                cpu_state.eaaddr += ((cpu_state.regs[cpu_reg].l / 32) * 4);     eal_r = eal_w = 0;      \
                temp = geteal();                        if (abrt) return 1;     \
                tempc = (temp & (1 << (cpu_state.regs[cpu_reg].l & 31))) ? 1 : 0;   \
                temp operation (1 << (cpu_state.regs[cpu_reg].l & 31));             \
                seteal(temp);                           if (abrt) return 1;     \
                flags_rebuild();                                                \
                if (tempc) flags |=  C_FLAG;                                    \
                else       flags &= ~C_FLAG;                                    \
                                                                                \
                CLOCK_CYCLES(6);                                                \
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
        
        temp = geteaw();
        count = getbyte();                      if (abrt) return 1;
        tempc = temp & (1 << count);
        flags_rebuild();
        switch (rmdat & 0x38)
        {
                case 0x20: /*BT w,imm*/
                if (tempc) flags |=  C_FLAG;
                else       flags &= ~C_FLAG;
                CLOCK_CYCLES(3);
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
                pclog("Bad 0F BA opcode %02X\n", rmdat & 0x38);
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                break;
        }
        seteaw(temp);                           if (abrt) return 1;
        if (tempc) flags |=  C_FLAG;
        else       flags &= ~C_FLAG;
        CLOCK_CYCLES(6);
        return 0;
}
static int opBA_w_a32(uint32_t fetchdat)
{
        int tempc, count;
        uint16_t temp;

        fetch_ea_32(fetchdat);
        
        temp = geteaw();
        count = getbyte();                      if (abrt) return 1;
        tempc = temp & (1 << count);
        flags_rebuild();
        switch (rmdat & 0x38)
        {
                case 0x20: /*BT w,imm*/
                if (tempc) flags |=  C_FLAG;
                else       flags &= ~C_FLAG;
                CLOCK_CYCLES(3);
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
                pclog("Bad 0F BA opcode %02X\n", rmdat & 0x38);
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                break;
        }
        seteaw(temp);                           if (abrt) return 1;
        if (tempc) flags |=  C_FLAG;
        else       flags &= ~C_FLAG;
        CLOCK_CYCLES(6);
        return 0;
}

static int opBA_l_a16(uint32_t fetchdat)
{
        int tempc, count;
        uint32_t temp;

        fetch_ea_16(fetchdat);
        
        temp = geteal();
        count = getbyte();                      if (abrt) return 1;
        tempc = temp & (1 << count);
        flags_rebuild();
        switch (rmdat & 0x38)
        {
                case 0x20: /*BT w,imm*/
                if (tempc) flags |=  C_FLAG;
                else       flags &= ~C_FLAG;
                CLOCK_CYCLES(3);
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
                pclog("Bad 0F BA opcode %02X\n", rmdat & 0x38);
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                break;
        }
        seteal(temp);                           if (abrt) return 1;
        if (tempc) flags |=  C_FLAG;
        else       flags &= ~C_FLAG;
        CLOCK_CYCLES(6);
        return 0;
}
static int opBA_l_a32(uint32_t fetchdat)
{
        int tempc, count;
        uint32_t temp;

        fetch_ea_32(fetchdat);
        
        temp = geteal();
        count = getbyte();                      if (abrt) return 1;
        tempc = temp & (1 << count);
        flags_rebuild();
        switch (rmdat & 0x38)
        {
                case 0x20: /*BT w,imm*/
                if (tempc) flags |=  C_FLAG;
                else       flags &= ~C_FLAG;
                CLOCK_CYCLES(3);
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
                pclog("Bad 0F BA opcode %02X\n", rmdat & 0x38);
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                break;
        }
        seteal(temp);                           if (abrt) return 1;
        if (tempc) flags |=  C_FLAG;
        else       flags &= ~C_FLAG;
        CLOCK_CYCLES(6);
        return 0;
}
