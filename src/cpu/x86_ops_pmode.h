static int opARPL_a16(uint32_t fetchdat)
{
        uint16_t temp_seg;

        NOTRM
        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp_seg = geteaw();            if (cpu_state.abrt) return 1;

        flags_rebuild();
        if ((temp_seg & 3) < (cpu_state.regs[cpu_reg].w & 3))
        {
                temp_seg = (temp_seg & 0xfffc) | (cpu_state.regs[cpu_reg].w & 3);
                seteaw(temp_seg);       if (cpu_state.abrt) return 1;
                cpu_state.flags |= Z_FLAG;
        }
        else
                cpu_state.flags &= ~Z_FLAG;

        CLOCK_CYCLES(is486 ? 9 : 20);
        PREFETCH_RUN(is486 ? 9 : 20, 2, rmdat, 1,0,1,0, 0);
        return 0;
}
static int opARPL_a32(uint32_t fetchdat)
{
        uint16_t temp_seg;

        NOTRM
        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
        temp_seg = geteaw();            if (cpu_state.abrt) return 1;

        flags_rebuild();
        if ((temp_seg & 3) < (cpu_state.regs[cpu_reg].w & 3))
        {
                temp_seg = (temp_seg & 0xfffc) | (cpu_state.regs[cpu_reg].w & 3);
                seteaw(temp_seg);       if (cpu_state.abrt) return 1;
                cpu_state.flags |= Z_FLAG;
        }
        else
                cpu_state.flags &= ~Z_FLAG;

        CLOCK_CYCLES(is486 ? 9 : 20);
        PREFETCH_RUN(is486 ? 9 : 20, 2, rmdat, 1,0,1,0, 1);
        return 0;
}

#define opLAR(name, fetch_ea, is32, ea32)                                                                             \
        static int opLAR_ ## name(uint32_t fetchdat)                                                            \
        {                                                                                                       \
                int valid;                                                                                      \
                uint16_t sel, desc = 0;                                                                         \
                                                                                                                \
                NOTRM                                                                                           \
                fetch_ea(fetchdat);                                                                             \
                if (cpu_mod != 3)                                                                               \
                        SEG_CHECK_READ(cpu_state.ea_seg);                                                       \
                                                                                                                \
                sel = geteaw();                 if (cpu_state.abrt) return 1;                                             \
                                                                                                                \
                flags_rebuild();                                                                                \
                if (!(sel & 0xfffc)) { cpu_state.flags &= ~Z_FLAG; return 0; } /*Null selector*/                          \
                valid = (sel & ~7) < ((sel & 4) ? ldt.limit : gdt.limit);                                       \
                if (valid)                                                                                      \
                {                                                                                               \
                        cpl_override = 1;                                                                       \
                        desc = readmemw(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 4);                 \
                        cpl_override = 0;       if (cpu_state.abrt) return 1;                                             \
                }                                                                                               \
                cpu_state.flags &= ~Z_FLAG;                                                                               \
                if ((desc & 0x1f00) == 0x000) valid = 0;                                                        \
                if ((desc & 0x1f00) == 0x800) valid = 0;                                                        \
                if ((desc & 0x1f00) == 0xa00) valid = 0;                                                        \
                if ((desc & 0x1f00) == 0xd00) valid = 0;                                                        \
                if ((desc & 0x1c00) < 0x1c00) /*Exclude conforming code segments*/                              \
                {                                                                                               \
                        int dpl = (desc >> 13) & 3;                                                             \
                        if (dpl < CPL || dpl < (sel & 3)) valid = 0;                                            \
                }                                                                                               \
                if (valid)                                                                                      \
                {                                                                                               \
                        cpu_state.flags |= Z_FLAG;                                                                        \
                        cpl_override = 1;                                                                       \
                        if (is32)                                                                               \
                                cpu_state.regs[cpu_reg].l = readmeml(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 4) & 0xffff00;       \
                        else                                                                                    \
                                cpu_state.regs[cpu_reg].w = readmemw(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 4) & 0xff00;         \
                        cpl_override = 0;                                                                       \
                }                                                                                               \
                CLOCK_CYCLES(11);                                                                               \
                PREFETCH_RUN(11, 2, rmdat, 2,0,0,0, ea32); \
                return cpu_state.abrt;                                                                                    \
        }

opLAR(w_a16, fetch_ea_16, 0, 0)
opLAR(w_a32, fetch_ea_32, 0, 1)
opLAR(l_a16, fetch_ea_16, 1, 0)
opLAR(l_a32, fetch_ea_32, 1, 1)

#define opLSL(name, fetch_ea, is32, ea32)                                                                             \
        static int opLSL_ ## name(uint32_t fetchdat)                                                            \
        {                                                                                                       \
                int valid;                                                                                      \
                uint16_t sel, desc = 0;                                                                         \
                                                                                                                \
                NOTRM                                                                                           \
                fetch_ea(fetchdat);                                                                             \
                if (cpu_mod != 3)                                                                               \
                        SEG_CHECK_READ(cpu_state.ea_seg);                                                       \
                                                                                                                \
                sel = geteaw();                 if (cpu_state.abrt) return 1;                                             \
                flags_rebuild();                                                                                \
                cpu_state.flags &= ~Z_FLAG;                                                                               \
                if (!(sel & 0xfffc)) return 0; /*Null selector*/                                                \
                valid = (sel & ~7) < ((sel & 4) ? ldt.limit : gdt.limit);                                       \
                if (valid)                                                                                      \
                {                                                                                               \
                        cpl_override = 1;                                                                       \
                        desc = readmemw(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 4);                 \
                        cpl_override = 0;       if (cpu_state.abrt) return 1;                                             \
                }                                                                                               \
                if ((desc & 0x1400) ==  0x400) valid = 0; /*Interrupt or trap or call gate*/                    \
                if ((desc & 0x1f00) ==  0x000) valid = 0; /*Invalid*/                                           \
                if ((desc & 0x1f00) ==  0xa00) valid = 0; /*Invalid*/                                           \
                if ((desc & 0x1c00) != 0x1c00) /*Exclude conforming code segments*/                             \
                {                                                                                               \
                        int rpl = (desc >> 13) & 3;                                                             \
                        if (rpl < CPL || rpl < (sel & 3)) valid = 0;                                            \
                }                                                                                               \
                if (valid)                                                                                      \
                {                                                                                               \
                        cpu_state.flags |= Z_FLAG;                                                                        \
                        cpl_override = 1;                                                                       \
                        if (is32)                                                                               \
                        {                                                                                       \
                                cpu_state.regs[cpu_reg].l = readmemw(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7));      \
                                cpu_state.regs[cpu_reg].l |= (readmemb(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 6) & 0xF) << 16;   \
                                if (readmemb(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 6) & 0x80)     \
                                {                                                                               \
                                        cpu_state.regs[cpu_reg].l <<= 12;                                           \
                                        cpu_state.regs[cpu_reg].l |= 0xFFF;                                         \
                                }                                                                               \
                        }                                                                                       \
                        else                                                                                    \
                                cpu_state.regs[cpu_reg].w = readmemw(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7));      \
                        cpl_override = 0;                                                                       \
                }                                                                                               \
                CLOCK_CYCLES(10);                                                                               \
                PREFETCH_RUN(10, 2, rmdat, 4,0,0,0, ea32); \
                return cpu_state.abrt;                                                                                    \
        }

opLSL(w_a16, fetch_ea_16, 0, 0)
opLSL(w_a32, fetch_ea_32, 0, 1)
opLSL(l_a16, fetch_ea_16, 1, 0)
opLSL(l_a32, fetch_ea_32, 1, 1)


static int op0F00_common(uint32_t fetchdat, int ea32)
{
        int dpl, valid, granularity;
        uint32_t addr, base, limit;
        uint16_t desc, sel;
        uint8_t access, ar_high;

        switch (rmdat & 0x38)
        {
                case 0x00: /*SLDT*/
                if (cpu_mod != 3)
                        SEG_CHECK_WRITE(cpu_state.ea_seg);
                seteaw(ldt.seg);
                CLOCK_CYCLES(4);
                PREFETCH_RUN(4, 2, rmdat, 0,0,(cpu_mod == 3) ? 0:1,0, ea32);
                break;
                case 0x08: /*STR*/
                if (cpu_mod != 3)
                        SEG_CHECK_WRITE(cpu_state.ea_seg);
                seteaw(tr.seg);
                CLOCK_CYCLES(4);
                PREFETCH_RUN(4, 2, rmdat, 0,0,(cpu_mod == 3) ? 0:1,0, ea32);
                break;
                case 0x10: /*LLDT*/
                if ((CPL || cpu_state.eflags&VM_FLAG) && (cr0&1))
                {
                        x86gpf(NULL,0);
                        return 1;
                }
                if (cpu_mod != 3)
                        SEG_CHECK_READ(cpu_state.ea_seg);
                sel = geteaw(); if (cpu_state.abrt) return 1;
                addr = (sel & ~7) + gdt.base;
                limit = readmemw(0, addr) + ((readmemb(0, addr + 6) & 0xf) << 16);
                base = (readmemw(0, addr + 2)) | (readmemb(0, addr + 4) << 16) | (readmemb(0, addr + 7) << 24);
                access = readmemb(0, addr + 5);
                ar_high = readmemb(0, addr + 6);
                granularity = readmemb(0, addr + 6) & 0x80;
                if (cpu_state.abrt) return 1;
                ldt.limit = limit;
                ldt.access = access;
                ldt.ar_high = ar_high;
                if (granularity)
                {
                        ldt.limit <<= 12;
                        ldt.limit |= 0xfff;
                }
                ldt.base = base;
                ldt.seg = sel;
                CLOCK_CYCLES(20);
                PREFETCH_RUN(20, 2, rmdat, (cpu_mod == 3) ? 0:1,2,0,0, ea32);
                break;
                case 0x18: /*LTR*/
                if ((CPL || cpu_state.eflags&VM_FLAG) && (cr0&1))
                {
                        x86gpf(NULL,0);
                        break;
                }
                if (cpu_mod != 3)
                        SEG_CHECK_READ(cpu_state.ea_seg);
                sel = geteaw(); if (cpu_state.abrt) return 1;
                addr = (sel & ~7) + gdt.base;
                limit = readmemw(0, addr) + ((readmemb(0, addr + 6) & 0xf) << 16);
                base = (readmemw(0, addr + 2)) | (readmemb(0, addr + 4) << 16) | (readmemb(0, addr + 7) << 24);
                access = readmemb(0, addr + 5);
                ar_high = readmemb(0, addr + 6);
                granularity = readmemb(0, addr + 6) & 0x80;
                if (cpu_state.abrt) return 1;
                access |= 2;
                writememb(0, addr + 5, access);
                if (cpu_state.abrt) return 1;
                tr.seg = sel;
                tr.limit = limit;
                tr.access = access;
                tr.ar_high = ar_high;
                if (granularity)
                {
                        tr.limit <<= 12;
                        tr.limit |= 0xFFF;
                }
                tr.base = base;
                CLOCK_CYCLES(20);
                PREFETCH_RUN(20, 2, rmdat, (cpu_mod == 3) ? 0:1,2,0,0, ea32);
                break;
                case 0x20: /*VERR*/
                if (cpu_mod != 3)
                        SEG_CHECK_READ(cpu_state.ea_seg);
                sel = geteaw();                 if (cpu_state.abrt) return 1;
                flags_rebuild();
                cpu_state.flags &= ~Z_FLAG;
                if (!(sel & 0xfffc)) return 0; /*Null selector*/
                cpl_override = 1;
                valid = (sel & ~7) < ((sel & 4) ? ldt.limit : gdt.limit);
                desc = readmemw(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 4);
                cpl_override = 0;               if (cpu_state.abrt) return 1;
                if (!(desc & 0x1000)) valid = 0;
                if ((desc & 0xC00) != 0xC00) /*Exclude conforming code segments*/
                {
                        dpl = (desc >> 13) & 3; /*Check permissions*/
                        if (dpl < CPL || dpl < (sel & 3)) valid = 0;
                }
                if ((desc & 0x0800) && !(desc & 0x0200)) valid = 0; /*Non-readable code*/
                if (valid) cpu_state.flags |= Z_FLAG;
                CLOCK_CYCLES(20);
                PREFETCH_RUN(20, 2, rmdat, (cpu_mod == 3) ? 1:2,0,0,0, ea32);
                break;
                case 0x28: /*VERW*/
                if (cpu_mod != 3)
                        SEG_CHECK_READ(cpu_state.ea_seg);
                sel = geteaw();                 if (cpu_state.abrt) return 1;
                flags_rebuild();
                cpu_state.flags &= ~Z_FLAG;
                if (!(sel & 0xfffc)) return 0; /*Null selector*/
                cpl_override = 1;
                valid  = (sel & ~7) < ((sel & 4) ? ldt.limit : gdt.limit);
                desc = readmemw(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 4);
                cpl_override = 0;               if (cpu_state.abrt) return 1;
                if (!(desc & 0x1000)) valid = 0;
                dpl = (desc >> 13) & 3; /*Check permissions*/
                if (dpl < CPL || dpl < (sel & 3)) valid = 0;
                if (desc & 0x0800) valid = 0; /*Code*/
                if (!(desc & 0x0200)) valid = 0; /*Read-only data*/
                if (valid) cpu_state.flags |= Z_FLAG;
                CLOCK_CYCLES(20);
                PREFETCH_RUN(20, 2, rmdat, (cpu_mod == 3) ? 1:2,0,0,0, ea32);
                break;

                default:
                cpu_state.pc -= 3;
                x86illegal();
                break;
        }
        return cpu_state.abrt;
}

static int op0F00_a16(uint32_t fetchdat)
{
        NOTRM

        fetch_ea_16(fetchdat);

        return op0F00_common(fetchdat, 0);
}
static int op0F00_a32(uint32_t fetchdat)
{
        NOTRM

        fetch_ea_32(fetchdat);

        return op0F00_common(fetchdat, 1);
}

static int op0F01_common(uint32_t fetchdat, int is32, int is286, int ea32)
{
        uint32_t base;
        uint16_t limit, tempw;
        switch (rmdat & 0x38)
        {
                case 0x00: /*SGDT*/
                if (cpu_mod != 3)
                        SEG_CHECK_WRITE(cpu_state.ea_seg);
                seteaw(gdt.limit);
                base = gdt.base; //is32 ? gdt.base : (gdt.base & 0xffffff);
                if (is286)
                        base |= 0xff000000;
                writememl(easeg, cpu_state.eaaddr + 2, base);
                CLOCK_CYCLES(7);
                PREFETCH_RUN(7, 2, rmdat, 0,0,1,1, ea32);
                break;
                case 0x08: /*SIDT*/
                if (cpu_mod != 3)
                        SEG_CHECK_WRITE(cpu_state.ea_seg);
                seteaw(idt.limit);
                base = idt.base;
                if (is286)
                        base |= 0xff000000;
                writememl(easeg, cpu_state.eaaddr + 2, base);
                CLOCK_CYCLES(7);
                PREFETCH_RUN(7, 2, rmdat, 0,0,1,1, ea32);
                break;
                case 0x10: /*LGDT*/
                if ((CPL || cpu_state.eflags&VM_FLAG) && (cr0&1))
                {
                        x86gpf(NULL,0);
                        break;
                }
                if (cpu_mod != 3)
                        SEG_CHECK_READ(cpu_state.ea_seg);
                limit = geteaw();
                base = readmeml(0, easeg + cpu_state.eaaddr + 2);         if (cpu_state.abrt) return 1;
                gdt.limit = limit;
                gdt.base = base;
                if (!is32) gdt.base &= 0xffffff;
                CLOCK_CYCLES(11);
                PREFETCH_RUN(11, 2, rmdat, 1,1,0,0, ea32);
                break;
                case 0x18: /*LIDT*/
                if ((CPL || cpu_state.eflags&VM_FLAG) && (cr0&1))
                {
                        x86gpf(NULL,0);
                        break;
                }
                if (cpu_mod != 3)
                        SEG_CHECK_READ(cpu_state.ea_seg);
                limit = geteaw();
                base = readmeml(0, easeg + cpu_state.eaaddr + 2);         if (cpu_state.abrt) return 1;
                idt.limit = limit;
                idt.base = base;
                if (!is32) idt.base &= 0xffffff;
                CLOCK_CYCLES(11);
                PREFETCH_RUN(11, 2, rmdat, 1,1,0,0, ea32);
                break;

                case 0x20: /*SMSW*/
                if (cpu_mod != 3)
                        SEG_CHECK_WRITE(cpu_state.ea_seg);
                if (is486 || isibm486)      seteaw(msw);
                else if (is386) seteaw(msw | /* 0xFF00 */ 0xFFE0);
                else            seteaw(msw | 0xFFF0);
                CLOCK_CYCLES(2);
                PREFETCH_RUN(2, 2, rmdat, 0,0,(cpu_mod == 3) ? 0:1,0, ea32);
                break;
                case 0x30: /*LMSW*/
                if ((CPL || cpu_state.eflags&VM_FLAG) && (msw&1))
                {
                        x86gpf(NULL, 0);
                        break;
                }
                if (cpu_mod != 3)
                        SEG_CHECK_READ(cpu_state.ea_seg);
                tempw = geteaw();                                       if (cpu_state.abrt) return 1;
                if (msw & 1) tempw |= 1;
                if (is386)
                {
                        tempw &= ~0x10;
                        tempw |= (msw & 0x10);
                }
                else tempw &= 0xF;
                msw = tempw;
                if (msw & 1)
                        cpu_cur_status |= CPU_STATUS_PMODE;
                else
                        cpu_cur_status &= ~CPU_STATUS_PMODE;
                PREFETCH_RUN(2, 2, rmdat, 0,0,(cpu_mod == 3) ? 0:1,0, ea32);
                break;

                case 0x38: /*INVLPG*/
                if (is486 || isibm486)
                {
                        if ((CPL || cpu_state.eflags&VM_FLAG) && (cr0&1))
                        {
                                x86gpf(NULL, 0);
                                break;
                        }
                        SEG_CHECK_READ(cpu_state.ea_seg);
                        mmu_invalidate(ds + cpu_state.eaaddr);
                        CLOCK_CYCLES(12);
                        PREFETCH_RUN(12, 2, rmdat, 0,0,0,0, ea32);
                        break;
                }

                default:
                cpu_state.pc -= 3;
                x86illegal();
                break;
        }
        return cpu_state.abrt;
}

static int op0F01_w_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);

        return op0F01_common(fetchdat, 0, 0, 0);
}
static int op0F01_w_a32(uint32_t fetchdat)
{
        fetch_ea_32(fetchdat);

        return op0F01_common(fetchdat, 0, 0, 1);
}
static int op0F01_l_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);

        return op0F01_common(fetchdat, 1, 0, 0);
}
static int op0F01_l_a32(uint32_t fetchdat)
{
        fetch_ea_32(fetchdat);

        return op0F01_common(fetchdat, 1, 0, 1);
}

static int op0F01_286(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);

        return op0F01_common(fetchdat, 0, 1, 0);
}
