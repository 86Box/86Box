#define OP_SHIFT_b(c, ea32)                                                             \
        {                                                                               \
                uint8_t temp_orig = temp;                                               \
                if (!c) return 0;                                                       \
                flags_rebuild();                                                        \
                switch (rmdat & 0x38)                                                   \
                {                                                                       \
                        case 0x00: /*ROL b, c*/                                         \
                        while (c > 0)                                                   \
                        {                                                               \
                                temp2 = (temp & 0x80) ? 1 : 0;                          \
                                temp = (temp << 1) | temp2;                             \
                                c--;                                                    \
                        }                                                               \
                        seteab(temp);           if (cpu_state.abrt) return 1;                     \
                        flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) flags |= C_FLAG;                                     \
                        if ((flags & C_FLAG) ^ (temp >> 7)) flags |= V_FLAG;            \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x08: /*ROR b,CL*/                                         \
                        while (c > 0)                                                   \
                        {                                                               \
                                temp2 = temp & 1;                                       \
                                temp >>= 1;                                             \
                                if (temp2) temp |= 0x80;                                \
                                c--;                                                    \
                        }                                                               \
                        seteab(temp);           if (cpu_state.abrt) return 1;                     \
                        flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) flags |= C_FLAG;                                     \
                        if ((temp ^ (temp >> 1)) & 0x40) flags |= V_FLAG;               \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x10: /*RCL b,CL*/                                         \
                        temp2 = flags & C_FLAG;                                         \
                        if (is486) CLOCK_CYCLES_ALWAYS(c);                              \
                        while (c > 0)                                                   \
                        {                                                               \
                                tempc = temp2 ? 1 : 0;                                  \
                                temp2 = temp & 0x80;                                    \
                                temp = (temp << 1) | tempc;                             \
                                c--;                                                    \
                        }                                                               \
                        seteab(temp);           if (cpu_state.abrt) return 1;                     \
                        flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) flags |= C_FLAG;                                     \
                        if ((flags & C_FLAG) ^ (temp >> 7)) flags |= V_FLAG;            \
                        CLOCK_CYCLES((cpu_mod == 3) ? 9 : 10);                                \
                        PREFETCH_RUN((cpu_mod == 3) ? 9 : 10, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x18: /*RCR b,CL*/                                         \
                        temp2 = flags & C_FLAG;                                         \
                        if (is486) CLOCK_CYCLES_ALWAYS(c);                              \
                        while (c > 0)                                                   \
                        {                                                               \
                                tempc = temp2 ? 0x80 : 0;                               \
                                temp2 = temp & 1;                                       \
                                temp = (temp >> 1) | tempc;                             \
                                c--;                                                    \
                        }                                                               \
                        seteab(temp);           if (cpu_state.abrt) return 1;                     \
                        flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) flags |= C_FLAG;                                     \
                        if ((temp ^ (temp >> 1)) & 0x40) flags |= V_FLAG;               \
                        CLOCK_CYCLES((cpu_mod == 3) ? 9 : 10);                                \
                        PREFETCH_RUN((cpu_mod == 3) ? 9 : 10, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x20: case 0x30: /*SHL b,CL*/                              \
                        seteab(temp << c);      if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SHL8, temp_orig, c, (temp << c) & 0xff);  \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x28: /*SHR b,CL*/                                         \
                        seteab(temp >> c);      if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SHR8, temp_orig, c, temp >> c);           \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x38: /*SAR b,CL*/                                         \
                        temp = (int8_t)temp >> c;                                       \
                        seteab(temp);           if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SAR8, temp_orig, c, temp);                \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                }                                                                       \
        }

#define OP_SHIFT_w(c, ea32)                                                             \
        {                                                                               \
                uint16_t temp_orig = temp;                                              \
                if (!c) return 0;                                                       \
                flags_rebuild();                                                        \
                switch (rmdat & 0x38)                                                   \
                {                                                                       \
                        case 0x00: /*ROL w, c*/                                         \
                        while (c > 0)                                                   \
                        {                                                               \
                                temp2 = (temp & 0x8000) ? 1 : 0;                        \
                                temp = (temp << 1) | temp2;                             \
                                c--;                                                    \
                        }                                                               \
                        seteaw(temp);           if (cpu_state.abrt) return 1;                     \
                        flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) flags |= C_FLAG;                                     \
                        if ((flags & C_FLAG) ^ (temp >> 15)) flags |= V_FLAG;           \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x08: /*ROR w, c*/                                         \
                        while (c > 0)                                                   \
                        {                                                               \
                                temp2 = temp & 1;                                       \
                                temp >>= 1;                                             \
                                if (temp2) temp |= 0x8000;                              \
                                c--;                                                    \
                        }                                                               \
                        seteaw(temp);           if (cpu_state.abrt) return 1;                     \
                        flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) flags |= C_FLAG;                                     \
                        if ((temp ^ (temp >> 1)) & 0x4000) flags |= V_FLAG;             \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x10: /*RCL w, c*/                                         \
                        temp2 = flags & C_FLAG;                                         \
                        if (is486) CLOCK_CYCLES_ALWAYS(c);                              \
                        while (c > 0)                                                   \
                        {                                                               \
                                tempc = temp2 ? 1 : 0;                                  \
                                temp2 = temp & 0x8000;                                  \
                                temp = (temp << 1) | tempc;                             \
                                c--;                                                    \
                        }                                                               \
                        seteaw(temp);           if (cpu_state.abrt) return 1;                     \
                        flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) flags |= C_FLAG;                                     \
                        if ((flags & C_FLAG) ^ (temp >> 15)) flags |= V_FLAG;           \
                        CLOCK_CYCLES((cpu_mod == 3) ? 9 : 10);                                \
                        PREFETCH_RUN((cpu_mod == 3) ? 9 : 10, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x18: /*RCR w, c*/                                         \
                        temp2 = flags & C_FLAG;                                         \
                        if (is486) CLOCK_CYCLES_ALWAYS(c);                              \
                        while (c > 0)                                                   \
                        {                                                               \
                                tempc = temp2 ? 0x8000 : 0;                             \
                                temp2 = temp & 1;                                       \
                                temp = (temp >> 1) | tempc;                             \
                                c--;                                                    \
                        }                                                               \
                        seteaw(temp);           if (cpu_state.abrt) return 1;                     \
                        flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) flags |= C_FLAG;                                     \
                        if ((temp ^ (temp >> 1)) & 0x4000) flags |= V_FLAG;             \
                        CLOCK_CYCLES((cpu_mod == 3) ? 9 : 10);                                \
                        PREFETCH_RUN((cpu_mod == 3) ? 9 : 10, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x20: case 0x30: /*SHL w, c*/                              \
                        seteaw(temp << c);      if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SHL16, temp_orig, c, (temp << c) & 0xffff); \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x28: /*SHR w, c*/                                         \
                        seteaw(temp >> c);      if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SHR16, temp_orig, c, temp >> c);          \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                        case 0x38: /*SAR w, c*/                                         \
                        temp = (int16_t)temp >> c;                                      \
                        seteaw(temp);           if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SAR16, temp_orig, c, temp);               \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, ea32); \
                        break;                                                          \
                }                                                                       \
        }

#define OP_SHIFT_l(c, ea32)                                                             \
        {                                                                               \
                uint32_t temp_orig = temp;                                              \
                if (!c) return 0;                                                       \
                flags_rebuild();                                                        \
                switch (rmdat & 0x38)                                                   \
                {                                                                       \
                        case 0x00: /*ROL l, c*/                                         \
                        while (c > 0)                                                   \
                        {                                                               \
                                temp2 = (temp & 0x80000000) ? 1 : 0;                    \
                                temp = (temp << 1) | temp2;                             \
                                c--;                                                    \
                        }                                                               \
                        seteal(temp);           if (cpu_state.abrt) return 1;                     \
                        flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) flags |= C_FLAG;                                     \
                        if ((flags & C_FLAG) ^ (temp >> 31)) flags |= V_FLAG;           \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, ea32); \
                        break;                                                          \
                        case 0x08: /*ROR l, c*/                                         \
                        while (c > 0)                                                   \
                        {                                                               \
                                temp2 = temp & 1;                                       \
                                temp >>= 1;                                             \
                                if (temp2) temp |= 0x80000000;                          \
                                c--;                                                    \
                        }                                                               \
                        seteal(temp);           if (cpu_state.abrt) return 1;                     \
                        flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) flags |= C_FLAG;                                     \
                        if ((temp ^ (temp >> 1)) & 0x40000000) flags |= V_FLAG;         \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, ea32); \
                        break;                                                          \
                        case 0x10: /*RCL l, c*/                                         \
                        temp2 = CF_SET();                                               \
                        if (is486) CLOCK_CYCLES_ALWAYS(c);                              \
                        while (c > 0)                                                   \
                        {                                                               \
                                tempc = temp2 ? 1 : 0;                                  \
                                temp2 = temp & 0x80000000;                              \
                                temp = (temp << 1) | tempc;                             \
                                c--;                                                    \
                        }                                                               \
                        seteal(temp);           if (cpu_state.abrt) return 1;                     \
                        flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) flags |= C_FLAG;                                     \
                        if ((flags & C_FLAG) ^ (temp >> 31)) flags |= V_FLAG;           \
                        CLOCK_CYCLES((cpu_mod == 3) ? 9 : 10);                                \
                        PREFETCH_RUN((cpu_mod == 3) ? 9 : 10, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, ea32); \
                        break;                                                          \
                        case 0x18: /*RCR l, c*/                                         \
                        temp2 = flags & C_FLAG;                                         \
                        if (is486) CLOCK_CYCLES_ALWAYS(c);                              \
                        while (c > 0)                                                   \
                        {                                                               \
                                tempc = temp2 ? 0x80000000 : 0;                         \
                                temp2 = temp & 1;                                       \
                                temp = (temp >> 1) | tempc;                             \
                                c--;                                                    \
                        }                                                               \
                        seteal(temp);           if (cpu_state.abrt) return 1;                     \
                        flags &= ~(C_FLAG | V_FLAG);                                    \
                        if (temp2) flags |= C_FLAG;                                     \
                        if ((temp ^ (temp >> 1)) & 0x40000000) flags |= V_FLAG;         \
                        CLOCK_CYCLES((cpu_mod == 3) ? 9 : 10);                                \
                        PREFETCH_RUN((cpu_mod == 3) ? 9 : 10, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, ea32); \
                        break;                                                          \
                        case 0x20: case 0x30: /*SHL l, c*/                              \
                        seteal(temp << c);      if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SHL32, temp_orig, c, temp << c);          \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, ea32); \
                        break;                                                          \
                        case 0x28: /*SHR l, c*/                                         \
                        seteal(temp >> c);      if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SHR32, temp_orig, c, temp >> c);          \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, ea32); \
                        break;                                                          \
                        case 0x38: /*SAR l, c*/                                         \
                        temp = (int32_t)temp >> c;                                      \
                        seteal(temp);           if (cpu_state.abrt) return 1;                     \
                        set_flags_shift(FLAGS_SAR32, temp_orig, c, temp);               \
                        CLOCK_CYCLES((cpu_mod == 3) ? 3 : 7);                                 \
                        PREFETCH_RUN((cpu_mod == 3) ? 3 : 7, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, ea32); \
                        break;                                                          \
                }                                                                       \
        }

static int opC0_a16(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint8_t temp, temp2;
        
        fetch_ea_16(fetchdat);
        c = readmemb(cs, cpu_state.pc) & 31; cpu_state.pc++;
        PREFETCH_PREFIX();
        temp = geteab();                if (cpu_state.abrt) return 1;
        OP_SHIFT_b(c, 0);
        return 0;
}
static int opC0_a32(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint8_t temp, temp2;
        
        fetch_ea_32(fetchdat);
        c = readmemb(cs, cpu_state.pc) & 31; cpu_state.pc++;
        PREFETCH_PREFIX();
        temp = geteab();                if (cpu_state.abrt) return 1;
        OP_SHIFT_b(c, 1);
        return 0;
}
static int opC1_w_a16(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint16_t temp, temp2;
        
        fetch_ea_16(fetchdat);
        c = readmemb(cs, cpu_state.pc) & 31; cpu_state.pc++;
        PREFETCH_PREFIX();
        temp = geteaw();                if (cpu_state.abrt) return 1;
        OP_SHIFT_w(c, 0);
        return 0;
}
static int opC1_w_a32(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint16_t temp, temp2;
        
        fetch_ea_32(fetchdat);
        c = readmemb(cs, cpu_state.pc) & 31; cpu_state.pc++;
        PREFETCH_PREFIX();
        temp = geteaw();                if (cpu_state.abrt) return 1;
        OP_SHIFT_w(c, 1);
        return 0;
}
static int opC1_l_a16(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint32_t temp, temp2;
        
        fetch_ea_16(fetchdat);
        c = readmemb(cs, cpu_state.pc) & 31; cpu_state.pc++;
        PREFETCH_PREFIX();
        temp = geteal();                if (cpu_state.abrt) return 1;
        OP_SHIFT_l(c, 0);
        return 0;
}
static int opC1_l_a32(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint32_t temp, temp2;
        
        fetch_ea_32(fetchdat);
        c = readmemb(cs, cpu_state.pc) & 31; cpu_state.pc++;
        PREFETCH_PREFIX();
        temp = geteal();                if (cpu_state.abrt) return 1;
        OP_SHIFT_l(c, 1);
        return 0;
}

static int opD0_a16(uint32_t fetchdat)
{
        int c = 1;
        int tempc;
        uint8_t temp, temp2;
        
        fetch_ea_16(fetchdat);
        temp = geteab();                if (cpu_state.abrt) return 1;
        OP_SHIFT_b(c, 0);
        return 0;
}
static int opD0_a32(uint32_t fetchdat)
{
        int c = 1;
        int tempc;
        uint8_t temp, temp2;
        
        fetch_ea_32(fetchdat);
        temp = geteab();                if (cpu_state.abrt) return 1;
        OP_SHIFT_b(c, 1);
        return 0;
}
static int opD1_w_a16(uint32_t fetchdat)
{
        int c = 1;
        int tempc;
        uint16_t temp, temp2;
        
        fetch_ea_16(fetchdat);
        temp = geteaw();                if (cpu_state.abrt) return 1;
        OP_SHIFT_w(c, 0);
        return 0;
}
static int opD1_w_a32(uint32_t fetchdat)
{
        int c = 1;
        int tempc;
        uint16_t temp, temp2;
        
        fetch_ea_32(fetchdat);
        temp = geteaw();                if (cpu_state.abrt) return 1;
        OP_SHIFT_w(c, 1);
        return 0;
}
static int opD1_l_a16(uint32_t fetchdat)
{
        int c = 1;
        int tempc;
        uint32_t temp, temp2;
        
        fetch_ea_16(fetchdat);
        temp = geteal();                if (cpu_state.abrt) return 1;
        OP_SHIFT_l(c, 0);
        return 0;
}
static int opD1_l_a32(uint32_t fetchdat)
{
        int c = 1;
        int tempc;
        uint32_t temp, temp2;
        
        fetch_ea_32(fetchdat);
        temp = geteal();                if (cpu_state.abrt) return 1;
        OP_SHIFT_l(c, 1);
        return 0;
}

static int opD2_a16(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint8_t temp, temp2;
        
        fetch_ea_16(fetchdat);
        c = CL & 31;
        temp = geteab();                if (cpu_state.abrt) return 1;
        OP_SHIFT_b(c, 0);
        return 0;
}
static int opD2_a32(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint8_t temp, temp2;
        
        fetch_ea_32(fetchdat);
        c = CL & 31;
        temp = geteab();                if (cpu_state.abrt) return 1;
        OP_SHIFT_b(c, 1);
        return 0;
}
static int opD3_w_a16(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint16_t temp, temp2;
        
        fetch_ea_16(fetchdat);
        c = CL & 31;
        temp = geteaw();                if (cpu_state.abrt) return 1;
        OP_SHIFT_w(c, 0);
        return 0;
}
static int opD3_w_a32(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint16_t temp, temp2;
        
        fetch_ea_32(fetchdat);
        c = CL & 31;
        temp = geteaw();                if (cpu_state.abrt) return 1;
        OP_SHIFT_w(c, 1);
        return 0;
}
static int opD3_l_a16(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint32_t temp, temp2;
        
        fetch_ea_16(fetchdat);
        c = CL & 31;
        temp = geteal();                if (cpu_state.abrt) return 1;
        OP_SHIFT_l(c, 0);
        return 0;
}
static int opD3_l_a32(uint32_t fetchdat)
{
        int c;
        int tempc;
        uint32_t temp, temp2;
        
        fetch_ea_32(fetchdat);
        c = CL & 31;
        temp = geteal();                if (cpu_state.abrt) return 1;
        OP_SHIFT_l(c, 1);
        return 0;
}


#define SHLD_w()                                                                \
        if (count)                                                              \
        {                                                                       \
                uint16_t tempw = geteaw();      if (cpu_state.abrt) return 1;             \
                int tempc = ((tempw << (count - 1)) & (1 << 15)) ? 1 : 0;       \
                uint32_t templ = (tempw << 16) | cpu_state.regs[cpu_reg].w;         \
                if (count <= 16) tempw =  templ >> (16 - count);                \
                else             tempw = (templ << count) >> 16;                \
                seteaw(tempw);                  if (cpu_state.abrt) return 1;             \
                setznp16(tempw);                                                \
                flags_rebuild();                                                \
                if (tempc) flags |= C_FLAG;                                     \
        }

#define SHLD_l()                                                                \
        if (count)                                                              \
        {                                                                       \
                uint32_t templ = geteal();      if (cpu_state.abrt) return 1;             \
                int tempc = ((templ << (count - 1)) & (1 << 31)) ? 1 : 0;       \
                templ = (templ << count) | (cpu_state.regs[cpu_reg].l >> (32 - count)); \
                seteal(templ);                  if (cpu_state.abrt) return 1;             \
                setznp32(templ);                                                \
                flags_rebuild();                                                \
                if (tempc) flags |= C_FLAG;                                     \
        }


#define SHRD_w()                                                                \
        if (count)                                                              \
        {                                                                       \
                uint16_t tempw = geteaw();      if (cpu_state.abrt) return 1;             \
                int tempc = (tempw >> (count - 1)) & 1;                         \
                uint32_t templ = tempw | (cpu_state.regs[cpu_reg].w << 16);         \
                tempw = templ >> count;                                         \
                seteaw(tempw);                  if (cpu_state.abrt) return 1;             \
                setznp16(tempw);                                                \
                flags_rebuild();                                                \
                if (tempc) flags |= C_FLAG;                                     \
        }

#define SHRD_l()                                                                \
        if (count)                                                              \
        {                                                                       \
                uint32_t templ = geteal();      if (cpu_state.abrt) return 1;             \
                int tempc = (templ >> (count - 1)) & 1;                         \
                templ = (templ >> count) | (cpu_state.regs[cpu_reg].l << (32 - count)); \
                seteal(templ);                  if (cpu_state.abrt) return 1;             \
                setznp32(templ);                                                \
                flags_rebuild();                                                \
                if (tempc) flags |= C_FLAG;                                     \
        }

#define opSHxD(operation)                                                       \
        static int op ## operation ## _i_a16(uint32_t fetchdat)                 \
        {                                                                       \
                int count;                                                      \
                                                                                \
                fetch_ea_16(fetchdat);                                          \
                count = getbyte() & 31;                                         \
                operation();                                                    \
                                                                                \
                CLOCK_CYCLES(3);                                                \
                PREFETCH_RUN(3, 3, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, 0); \
                return 0;                                                       \
        }                                                                       \
        static int op ## operation ## _CL_a16(uint32_t fetchdat)                \
        {                                                                       \
                int count;                                                      \
                                                                                \
                fetch_ea_16(fetchdat);                                          \
                count = CL & 31;                                                \
                operation();                                                    \
                                                                                \
                CLOCK_CYCLES(3);                                                \
                PREFETCH_RUN(3, 3, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, 0); \
                return 0;                                                       \
        }                                                                       \
        static int op ## operation ## _i_a32(uint32_t fetchdat)                 \
        {                                                                       \
                int count;                                                      \
                                                                                \
                fetch_ea_32(fetchdat);                                          \
                count = getbyte() & 31;                                         \
                operation();                                                    \
                                                                                \
                CLOCK_CYCLES(3);                                                \
                PREFETCH_RUN(3, 3, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, 1); \
                return 0;                                                       \
        }                                                                       \
        static int op ## operation ## _CL_a32(uint32_t fetchdat)                \
        {                                                                       \
                int count;                                                      \
                                                                                \
                fetch_ea_32(fetchdat);                                          \
                count = CL & 31;                                                \
                operation();                                                    \
                                                                                \
                CLOCK_CYCLES(3);                                                \
                PREFETCH_RUN(3, 3, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, 1); \
                return 0;                                                       \
        }
              
opSHxD(SHLD_w)
opSHxD(SHLD_l)
opSHxD(SHRD_w)
opSHxD(SHRD_l)
