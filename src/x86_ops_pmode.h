static int opARPL_a16(uint32_t fetchdat)
{
        uint16_t temp_seg;
        
        NOTRM
        fetch_ea_16(fetchdat);
        pclog("ARPL_a16\n");
        temp_seg = geteaw();            if (abrt) return 1;
        
        flags_rebuild();
        if ((temp_seg & 3) < (cpu_state.regs[reg].w & 3))
        {
                temp_seg = (temp_seg & 0xfffc) | (cpu_state.regs[reg].w & 3);
                seteaw(temp_seg);       if (abrt) return 1;
                flags |= Z_FLAG;
        }
        else
                flags &= ~Z_FLAG;
        
        CLOCK_CYCLES(is486 ? 9 : 20);
        return 0;
}
static int opARPL_a32(uint32_t fetchdat)
{
        uint16_t temp_seg;
        
        NOTRM
        fetch_ea_32(fetchdat);
        pclog("ARPL_a32\n");        
        temp_seg = geteaw();            if (abrt) return 1;
        
        flags_rebuild();
        if ((temp_seg & 3) < (cpu_state.regs[reg].w & 3))
        {
                temp_seg = (temp_seg & 0xfffc) | (cpu_state.regs[reg].w & 3);
                seteaw(temp_seg);       if (abrt) return 1;
                flags |= Z_FLAG;
        }
        else
                flags &= ~Z_FLAG;
        
        CLOCK_CYCLES(is486 ? 9 : 20);
        return 0;
}

#define opLAR(name, fetch_ea, is32)                                                                             \
        static int opLAR_ ## name(uint32_t fetchdat)                                                            \
        {                                                                                                       \
                int valid;                                                                                      \
                uint16_t sel, desc;                                                                             \
                                                                                                                \
                NOTRM                                                                                           \
                fetch_ea(fetchdat);                                                                             \
                                                                                                                \
                sel = geteaw();                 if (abrt) return 1;                                             \
                                                                                                                \
                flags_rebuild();                                                                                \
                if (!(sel & 0xfffc)) { flags &= ~Z_FLAG; return 0; } /*Null selector*/                          \
                valid = (sel & ~7) < ((sel & 4) ? ldt.limit : gdt.limit);                                       \
                if (valid)                                                                                      \
                {                                                                                               \
                        cpl_override = 1;                                                                       \
                        desc = readmemw(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 4);                 \
                        cpl_override = 0;       if (abrt) return 1;                                             \
                }                                                                                               \
                flags &= ~Z_FLAG;                                                                               \
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
                        flags |= Z_FLAG;                                                                        \
                        cpl_override = 1;                                                                       \
                        if (is32)                                                                               \
                                cpu_state.regs[reg].l = readmeml(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 4) & 0xffff00;       \
                        else                                                                                    \
                                cpu_state.regs[reg].w = readmemw(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 4) & 0xff00;         \
                        cpl_override = 0;                                                                       \
                }                                                                                               \
                CLOCK_CYCLES(11);                                                                               \
                return abrt;                                                                                    \
        }

opLAR(w_a16, fetch_ea_16, 0)
opLAR(w_a32, fetch_ea_32, 0)
opLAR(l_a16, fetch_ea_16, 1)
opLAR(l_a32, fetch_ea_32, 1)

#define opLSL(name, fetch_ea, is32)                                                                             \
        static int opLSL_ ## name(uint32_t fetchdat)                                                            \
        {                                                                                                       \
                int valid;                                                                                      \
                uint16_t sel, desc;                                                                             \
                                                                                                                \
                NOTRM                                                                                           \
                fetch_ea(fetchdat);                                                                             \
                                                                                                                \
                sel = geteaw();                 if (abrt) return 1;                                             \
                flags_rebuild();                                                                                \
                flags &= ~Z_FLAG;                                                                               \
                if (!(sel & 0xfffc)) return 0; /*Null selector*/                                                \
                valid = (sel & ~7) < ((sel & 4) ? ldt.limit : gdt.limit);                                       \
                if (valid)                                                                                      \
                {                                                                                               \
                        cpl_override = 1;                                                                       \
                        desc = readmemw(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 4);                 \
                        cpl_override = 0;       if (abrt) return 1;                                             \
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
                        flags |= Z_FLAG;                                                                        \
                        cpl_override = 1;                                                                       \
                        if (is32)                                                                               \
                        {                                                                                       \
                                cpu_state.regs[reg].l = readmemw(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7));      \
                                cpu_state.regs[reg].l |= (readmemb(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 6) & 0xF) << 16;   \
                                if (readmemb(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 6) & 0x80)     \
                                {                                                                               \
                                        cpu_state.regs[reg].l <<= 12;                                           \
                                        cpu_state.regs[reg].l |= 0xFFF;                                         \
                                }                                                                               \
                        }                                                                                       \
                        else                                                                                    \
                                cpu_state.regs[reg].w = readmemw(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7));      \
                        cpl_override = 0;                                                                       \
                }                                                                                               \
                CLOCK_CYCLES(10);                                                                               \
                return abrt;                                                                                    \
        }

opLSL(w_a16, fetch_ea_16, 0)
opLSL(w_a32, fetch_ea_32, 0)
opLSL(l_a16, fetch_ea_16, 1)
opLSL(l_a32, fetch_ea_32, 1)


static int op0F00_common(uint32_t fetchdat)
{
        int dpl, valid, granularity;
        uint32_t addr, base, limit;
        uint16_t desc, sel;
        uint8_t access;

//        pclog("op0F00 %02X %04X:%04X\n", rmdat & 0x38, CS, pc);        
        switch (rmdat & 0x38)
        {
                case 0x00: /*SLDT*/
                seteaw(ldt.seg);
                CLOCK_CYCLES(4);
                break;
                case 0x08: /*STR*/
                seteaw(tr.seg);
                CLOCK_CYCLES(4);
                break;
                case 0x10: /*LLDT*/
                if ((CPL || eflags&VM_FLAG) && (cr0&1))
                {
                        pclog("Invalid LLDT!\n");
                        x86gpf(NULL,0);
                        return 1;
                }
                sel = geteaw(); if (abrt) return 1;
                addr = (sel & ~7) + gdt.base;
                limit = readmemw(0, addr) + ((readmemb(0, addr + 6) & 0xf) << 16);
                base = (readmemw(0, addr + 2)) | (readmemb(0, addr + 4) << 16) | (readmemb(0, addr + 7) << 24);
                access = readmemb(0, addr + 5);
                granularity = readmemb(0, addr + 6) & 0x80;
                if (abrt) return 1;
                ldt.limit = limit;
                ldt.access = access;
                if (granularity)
                {
                        ldt.limit <<= 12;
                        ldt.limit |= 0xfff;
                }
                ldt.base = base;
                ldt.seg = sel;
                CLOCK_CYCLES(20);
                break;
                case 0x18: /*LTR*/
                if ((CPL || eflags&VM_FLAG) && (cr0&1))
                {
                        pclog("Invalid LTR!\n");
                        x86gpf(NULL,0);
                        break;
                }
                sel = geteaw(); if (abrt) return 1;
                addr = (sel & ~7) + gdt.base;
                limit = readmemw(0, addr) + ((readmemb(0, addr + 6) & 0xf) << 16);
                base = (readmemw(0, addr + 2)) | (readmemb(0, addr + 4) << 16) | (readmemb(0, addr + 7) << 24);
                access = readmemb(0, addr + 5);
                granularity = readmemb(0, addr + 6) & 0x80;
                if (abrt) return 1;
                tr.seg = sel;
                tr.limit = limit;
                tr.access = access;
                if (granularity)
                {
                        tr.limit <<= 12;
                        tr.limit |= 0xFFF;
                }
                tr.base = base;
                CLOCK_CYCLES(20);
                break;
                case 0x20: /*VERR*/
                sel = geteaw();                 if (abrt) return 1;
                flags_rebuild();
                flags &= ~Z_FLAG;
                if (!(sel & 0xfffc)) return 0; /*Null selector*/
                cpl_override = 1;
                valid = (sel & ~7) < ((sel & 4) ? ldt.limit : gdt.limit);
                desc = readmemw(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 4);
                cpl_override = 0;               if (abrt) return 1;
                if (!(desc & 0x1000)) valid = 0;
                if ((desc & 0xC00) != 0xC00) /*Exclude conforming code segments*/
                {
                        dpl = (desc >> 13) & 3; /*Check permissions*/
                        if (dpl < CPL || dpl < (sel & 3)) valid = 0;
                }
                if ((desc & 0x0800) && !(desc & 0x0200)) valid = 0; /*Non-readable code*/
                if (valid) flags |= Z_FLAG;
                CLOCK_CYCLES(20);
                break;
                case 0x28: /*VERW*/
                sel = geteaw();                 if (abrt) return 1;
                flags_rebuild();
                flags &= ~Z_FLAG;
                if (!(sel & 0xfffc)) return 0; /*Null selector*/
                cpl_override = 1;
                valid  = (sel & ~7) < ((sel & 4) ? ldt.limit : gdt.limit);
                desc = readmemw(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 4);
                cpl_override = 0;               if (abrt) return 1;
                if (!(desc & 0x1000)) valid = 0;
                dpl = (desc >> 13) & 3; /*Check permissions*/
                if (dpl < CPL || dpl < (sel & 3)) valid = 0;
                if (desc & 0x0800) valid = 0; /*Code*/
                if (!(desc & 0x0200)) valid = 0; /*Read-only data*/
                if (valid) flags |= Z_FLAG;
                CLOCK_CYCLES(20);
                break;

                default:
                pclog("Bad 0F 00 opcode %02X\n", rmdat & 0x38);
                cpu_state.pc -= 3;
                x86illegal();
                break;
        }
        return abrt;
}

static int op0F00_a16(uint32_t fetchdat)
{
        NOTRM

        fetch_ea_16(fetchdat);
        
        return op0F00_common(fetchdat);
}
static int op0F00_a32(uint32_t fetchdat)
{
        NOTRM

        fetch_ea_32(fetchdat);
        
        return op0F00_common(fetchdat);
}

static int op0F01_common(uint32_t fetchdat, int is32, int is286)
{
        uint32_t base;
        uint16_t limit, tempw;
//        pclog("op0F01 %02X %04X:%04X\n", rmdat & 0x38, CS, pc);
        switch (rmdat & 0x38)
        {
                case 0x00: /*SGDT*/
                seteaw(gdt.limit);
                base = gdt.base; //is32 ? gdt.base : (gdt.base & 0xffffff);
                if (is286)
                        base |= 0xff000000;
                writememl(easeg, eaaddr + 2, base);
                CLOCK_CYCLES(7);
                break;
                case 0x08: /*SIDT*/
                seteaw(idt.limit);
                base = idt.base;
                if (is286)
                        base |= 0xff000000;
                writememl(easeg, eaaddr + 2, base);
                CLOCK_CYCLES(7);
                break;
                case 0x10: /*LGDT*/
                if ((CPL || eflags&VM_FLAG) && (cr0&1))
                {
                        pclog("Invalid LGDT!\n");
                        x86gpf(NULL,0);
                        break;
                }
//                pclog("LGDT %08X:%08X\n", easeg, eaaddr);
                limit = geteaw();
                base = readmeml(0, easeg + eaaddr + 2);         if (abrt) return 1;
//                pclog("     %08X %04X\n", base, limit);
                gdt.limit = limit;
                gdt.base = base;
                if (!is32) gdt.base &= 0xffffff;
                CLOCK_CYCLES(11);
                break;
                case 0x18: /*LIDT*/
                if ((CPL || eflags&VM_FLAG) && (cr0&1))
                {
                        pclog("Invalid LIDT!\n");
                        x86gpf(NULL,0);
                        break;
                }
//                pclog("LIDT %08X:%08X\n", easeg, eaaddr);
                limit = geteaw();
                base = readmeml(0, easeg + eaaddr + 2);         if (abrt) return 1;
//                pclog("     %08X %04X\n", base, limit);
                idt.limit = limit;
                idt.base = base;
                if (!is32) idt.base &= 0xffffff;
                CLOCK_CYCLES(11);
                break;

                case 0x20: /*SMSW*/
                if (is486) seteaw(msw);
                else       seteaw(msw | 0xFF00);
                CLOCK_CYCLES(2);
                break;
                case 0x30: /*LMSW*/
                if ((CPL || eflags&VM_FLAG) && (msw&1))
                {
                        pclog("LMSW - ring not zero!\n");
                        x86gpf(NULL, 0);
                        break;
                }
                tempw = geteaw();                                       if (abrt) return 1;
                if (msw & 1) tempw |= 1;
                msw = tempw;
                break;

                case 0x38: /*INVLPG*/
                if (is486)
                {
                        if ((CPL || eflags&VM_FLAG) && (cr0&1))
                        {
                                pclog("Invalid INVLPG!\n");
                                x86gpf(NULL, 0);
                                break;
                        }
                        mmu_invalidate(ds + eaaddr);
                        CLOCK_CYCLES(12);
                        break;
                }

                default:
                pclog("Bad 0F 01 opcode %02X\n", rmdat & 0x38);
                cpu_state.pc -= 3;
                x86illegal();
                break;
        }
        return abrt;
}

static int op0F01_w_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);
        
        return op0F01_common(fetchdat, 0, 0);
}
static int op0F01_w_a32(uint32_t fetchdat)
{
        fetch_ea_32(fetchdat);
        
        return op0F01_common(fetchdat, 0, 0);
}
static int op0F01_l_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);
        
        return op0F01_common(fetchdat, 1, 0);
}
static int op0F01_l_a32(uint32_t fetchdat)
{
        fetch_ea_32(fetchdat);
        
        return op0F01_common(fetchdat, 1, 0);
}

static int op0F01_286(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);
        
        return op0F01_common(fetchdat, 0, 1);
}
