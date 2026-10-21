static int
opARPL_a16(uint32_t fetchdat)
{
    uint16_t temp_seg;

    NOTRM
    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    temp_seg = geteaw();
    if (cpu_state.abrt)
        return 1;

    flags_rebuild();
    if ((temp_seg & 3) < (cpu_state.regs[cpu_reg].w & 3)) {
        temp_seg = (temp_seg & 0xfffc) | (cpu_state.regs[cpu_reg].w & 3);
        seteaw(temp_seg);
        if (cpu_state.abrt)
            return 1;
        cpu_state.flags |= Z_FLAG;
    } else
        cpu_state.flags &= ~Z_FLAG;

    CLOCK_CYCLES(is486 ? 9 : 20);
    PREFETCH_RUN(is486 ? 9 : 20, 2, rmdat, 1, 0, 1, 0, 0);
    return 0;
}
static int
opARPL_a32(uint32_t fetchdat)
{
    uint16_t temp_seg;

    NOTRM
    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    temp_seg = geteaw();
    if (cpu_state.abrt)
        return 1;

    flags_rebuild();
    if ((temp_seg & 3) < (cpu_state.regs[cpu_reg].w & 3)) {
        temp_seg = (temp_seg & 0xfffc) | (cpu_state.regs[cpu_reg].w & 3);
        seteaw(temp_seg);
        if (cpu_state.abrt)
            return 1;
        cpu_state.flags |= Z_FLAG;
    } else
        cpu_state.flags &= ~Z_FLAG;

    CLOCK_CYCLES(is486 ? 9 : 20);
    PREFETCH_RUN(is486 ? 9 : 20, 2, rmdat, 1, 0, 1, 0, 1);
    return 0;
}

#define opLAR(name, fetch_ea, is32, ea32)                                                                               \
    static int opLAR_##name(uint32_t fetchdat)                                                                          \
    {                                                                                                                   \
        int      valid;                                                                                                 \
        uint32_t rights;                                                                                               \
        uint16_t sel, desc = 0;                                                                                         \
                                                                                                                        \
        NOTRM                                                                                                           \
        fetch_ea(fetchdat);                                                                                             \
        if (cpu_mod != 3)                                                                                               \
            SEG_CHECK_READ(cpu_state.ea_seg);                                                                           \
                                                                                                                        \
        sel = geteaw();                                                                                                 \
        if (cpu_state.abrt)                                                                                             \
            return 1;                                                                                                   \
                                                                                                                        \
        flags_rebuild();                                                                                                \
        if (!(sel & 0xfffc)) {                                                                                          \
            cpu_state.flags &= ~Z_FLAG;                                                                                 \
            return 0;                                                                                                   \
        } /*Null selector*/                                                                                             \
        valid = (sel | 7) <= ((sel & 4) ? ldt.limit : gdt.limit); /*The whole descriptor in the table*/                 \
        if (valid) {                                                                                                    \
            cpl_override = 1;                                                                                           \
            desc         = readmemw(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 4);                             \
            cpl_override = 0;                                                                                           \
            if (cpu_state.abrt)                                                                                         \
                return 1;                                                                                               \
        }                                                                                                               \
        /*System types: TSSs, LDT, call and task gates; not trap or interrupt gates*/                                   \
        if (!(desc & 0x1000) && !(((is386 ? 0x1a3e : 0x003e) >> ((desc >> 8) & 0xf)) & 1)) /*no 386 types on a 286*/  \
            valid = 0;                                                                                                  \
        if ((desc & 0x1c00) < 0x1c00) /*Exclude conforming code segments*/                                              \
        {                                                                                                               \
            int dpl = (desc >> 13) & 3;                                                                                 \
            if (dpl < CPL || dpl < (sel & 3))                                                                           \
                valid = 0;                                                                                              \
        }                                                                                                               \
        if (valid) {                                                                                                    \
            cpl_override = 1;                                                                                           \
            if (is32)                                                                                                   \
                rights = readmeml(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 4) & 0xffff00;                     \
            else                                                                                                        \
                rights = desc & 0xff00;                                                                                \
            cpl_override = 0;                                                                                           \
            if (cpu_state.abrt)                                                                                         \
                return 1;                                                                                               \
            if (is32)                                                                                                   \
                cpu_state.regs[cpu_reg].l = rights;                                                                     \
            else                                                                                                        \
                cpu_state.regs[cpu_reg].w = rights;                                                                     \
            cpu_state.flags |= Z_FLAG;                                                                                  \
        } else                                                                                                          \
            cpu_state.flags &= ~Z_FLAG;                                                                                 \
        CLOCK_CYCLES(11);                                                                                               \
        PREFETCH_RUN(11, 2, rmdat, 2, 0, 0, 0, ea32);                                                                   \
        return cpu_state.abrt;                                                                                          \
    }

opLAR(w_a16, fetch_ea_16, 0, 0)
    opLAR(w_a32, fetch_ea_32, 0, 1)
        opLAR(l_a16, fetch_ea_16, 1, 0)
            opLAR(l_a32, fetch_ea_32, 1, 1)

#define opLSL(name, fetch_ea, is32, ea32)                                                                                   \
    static int opLSL_##name(uint32_t fetchdat)                                                                              \
    {                                                                                                                       \
        int      valid;                                                                                                     \
        uint32_t limit;                                                                                                     \
        uint8_t  limit_high;                                                                                                \
        uint16_t sel, desc = 0;                                                                                             \
                                                                                                                            \
        NOTRM                                                                                                               \
        fetch_ea(fetchdat);                                                                                                 \
        if (cpu_mod != 3)                                                                                                   \
            SEG_CHECK_READ(cpu_state.ea_seg);                                                                               \
                                                                                                                            \
        sel = geteaw();                                                                                                     \
        if (cpu_state.abrt)                                                                                                 \
            return 1;                                                                                                       \
        flags_rebuild();                                                                                                    \
        if (!(sel & 0xfffc)) {                                                                                              \
            cpu_state.flags &= ~Z_FLAG;                                                                                     \
            return 0; /*Null selector*/                                                                                     \
        }                                                                                                                   \
        valid = (sel | 7) <= ((sel & 4) ? ldt.limit : gdt.limit); /*The whole descriptor in the table*/                     \
        if (valid) {                                                                                                        \
            cpl_override = 1;                                                                                               \
            desc         = readmemw(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 4);                                 \
            cpl_override = 0;                                                                                               \
            if (cpu_state.abrt)                                                                                             \
                return 1;                                                                                                   \
        }                                                                                                                   \
        /*System types: TSSs and LDT only, no gates*/                                                                       \
        if (!(desc & 0x1000) && !(((is386 ? 0x0a0e : 0x000e) >> ((desc >> 8) & 0xf)) & 1)) /*no 386 types on a 286*/      \
            valid = 0;                                                                                                      \
        if ((desc & 0x1c00) != 0x1c00) /*Exclude conforming code segments*/                                                 \
        {                                                                                                                   \
            int rpl = (desc >> 13) & 3;                                                                                     \
            if (rpl < CPL || rpl < (sel & 3))                                                                               \
                valid = 0;                                                                                                  \
        }                                                                                                                   \
        if (valid) {                                                                                                        \
            cpl_override = 1;                                                                                               \
            limit = readmemw(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7));                                            \
            if (cpu_state.abrt) {                                                                                            \
                cpl_override = 0;                                                                                           \
                return 1;                                                                                                   \
            }                                                                                                               \
            limit_high = readmemb(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 6);                                    \
            cpl_override = 0;                                                                                               \
            if (cpu_state.abrt)                                                                                             \
                return 1;                                                                                                   \
            if (!is386)                                                                                                     \
                limit_high = 0; /*the descriptor's last word is reserved on the 80286*/                                    \
            limit |= (limit_high & 0xF) << 16;                                                                              \
            if (limit_high & 0x80)                                                                                          \
                limit = (limit << 12) | 0xFFF; /*Byte granular, the 16-bit form too*/                                       \
            if (is32)                                                                                                       \
                cpu_state.regs[cpu_reg].l = limit;                                                                          \
            else                                                                                                            \
                cpu_state.regs[cpu_reg].w = limit;                                                                          \
            cpu_state.flags |= Z_FLAG;                                                                                      \
        } else                                                                                                              \
            cpu_state.flags &= ~Z_FLAG;                                                                                     \
        CLOCK_CYCLES(10);                                                                                                   \
        PREFETCH_RUN(10, 2, rmdat, 4, 0, 0, 0, ea32);                                                                       \
        return cpu_state.abrt;                                                                                              \
    }

opLSL(w_a16, fetch_ea_16, 0, 0)
opLSL(w_a32, fetch_ea_32, 0, 1)
opLSL(l_a16, fetch_ea_16, 1, 0)
opLSL(l_a32, fetch_ea_32, 1, 1)

static int op0F00_common(uint32_t fetchdat, UNUSED(int ea32))
{
    int      dpl;
    int      valid;
    int      granularity;
    uint32_t addr;
    uint32_t base;
    uint32_t limit;
    uint16_t desc;
    uint16_t sel;
    uint8_t  access;
    uint8_t  ar_high;

    switch (rmdat & 0x38) {
        case 0x00: /*SLDT*/
            if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
            seteaw(ldt.seg);
            CLOCK_CYCLES(4);
            PREFETCH_RUN(4, 2, rmdat, 0, 0, (cpu_mod == 3) ? 0 : 1, 0, ea32);
            break;
        case 0x08: /*STR*/
            if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
            seteaw(tr.seg);
            CLOCK_CYCLES(4);
            PREFETCH_RUN(4, 2, rmdat, 0, 0, (cpu_mod == 3) ? 0 : 1, 0, ea32);
            break;
        case 0x10: /*LLDT*/
            if ((CPL || cpu_state.eflags & VM_FLAG) && (cr0 & 1)) {
                x86gpf(NULL, 0);
                return 1;
            }
            if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
            sel = geteaw();
            if (cpu_state.abrt)
                return 1;
            if (!(sel & ~3)) {
                /*Null selector: LDTR invalid, every LDT reference faults*/
                ldt.seg     = sel;
                ldt.base    = 0;
                ldt.limit   = 0;
                ldt.access  = 0;
                ldt.ar_high = 0;
                CLOCK_CYCLES(20);
                PREFETCH_RUN(20, 2, rmdat, (cpu_mod == 3) ? 0 : 1, 2, 0, 0, ea32);
                break;
            }
            if ((sel & 4) || ((sel | 7) > gdt.limit)) {
                x86gpf(NULL, sel & 0xfffc);
                return 1;
            }
            addr        = (sel & ~7) + gdt.base;
            limit       = readmemw(0, addr) + ((readmemb(0, addr + 6) & 0xf) << 16);
            base        = (readmemw(0, addr + 2)) | (readmemb(0, addr + 4) << 16) | (readmemb(0, addr + 7) << 24);
            access      = readmemb(0, addr + 5);
            ar_high     = readmemb(0, addr + 6);
            granularity = readmemb(0, addr + 6) & 0x80;
            if (cpu_state.abrt)
                return 1;
            if (!is386) {
                /* The descriptor's last word is reserved on the 80286 */
                limit &= 0xffff;
                base &= 0xffffff;
                ar_high     = 0;
                granularity = 0;
            }
            if ((access & 0x1f) != 0x02) {
                x86gpf(NULL, sel & 0xfffc);
                return 1;
            }
            if (!(access & 0x80)) {
                x86np("LLDT not present", sel & 0xfffc);
                return 1;
            }
            ldt.limit   = limit;
            ldt.access  = access;
            ldt.ar_high = ar_high;
            if (granularity) {
                ldt.limit <<= 12;
                ldt.limit |= 0xfff;
            }
            ldt.base = base;
            ldt.seg  = sel;
            CLOCK_CYCLES(20);
            PREFETCH_RUN(20, 2, rmdat, (cpu_mod == 3) ? 0 : 1, 2, 0, 0, ea32);
            break;
        case 0x18: /*LTR*/
            if ((CPL || cpu_state.eflags & VM_FLAG) && (cr0 & 1)) {
                x86gpf(NULL, 0);
                break;
            }
            if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
            sel = geteaw();
            if (cpu_state.abrt)
                return 1;
            /* A null selector, one in the LDT, or one past the GDT's limit:
               #GP (Pentium Vol. 3, LTR). */
            if (!(sel & 0xfffc)) {
                x86gpf(NULL, 0);
                return 1;
            }
            if ((sel & 4) || ((uint32_t) (sel | 7) > gdt.limit)) {
                x86gpf(NULL, sel & 0xfffc);
                return 1;
            }
            addr        = (sel & ~7) + gdt.base;
            limit       = readmemw(0, addr) + ((readmemb(0, addr + 6) & 0xf) << 16);
            base        = (readmemw(0, addr + 2)) | (readmemb(0, addr + 4) << 16) | (readmemb(0, addr + 7) << 24);
            access      = readmemb(0, addr + 5);
            ar_high     = readmemb(0, addr + 6);
            granularity = readmemb(0, addr + 6) & 0x80;
            if (cpu_state.abrt)
                return 1;
            if (!is386) {
                /* The descriptor's last word is reserved on the 80286 */
                limit &= 0xffff;
                base &= 0xffffff;
                ar_high     = 0;
                granularity = 0;
            }
            /* "#GP(selector) if the object named by the source selector is
               not a TSS ...; #NP(selector) if the TSS is marked 'not
               present'": a system descriptor of type 1, 3, 9 or 11 (the
               busy bit is tested below, under LOCK#), before anything is
               written to it. A 286 has no 386 TSS (types 9 and 11). */
            if (((access & 0x15) != 0x01) || (!is386 && (access & 0x08))) {
                x86gpf(NULL, sel & 0xfffc);
                return 1;
            }
            if (!(access & 0x80)) {
                x86np("LTR: TSS not present", sel & 0xfffc);
                return 1;
            }
            /* A TSS already busy is the #GP LTR gives for one that is not
               available (Pentium Vol. 3, LTR). */
            if (access & 2) {
                x86gpf(NULL, sel & 0xfffc);
                return 1;
            }
            writememb(0, addr + 5, access | 2);
            if (cpu_state.abrt)
                return 1;
            access |= 2;
            tr.seg     = sel;
            tr.limit   = limit;
            tr.access  = access;
            tr.ar_high = ar_high;
            if (granularity) {
                tr.limit <<= 12;
                tr.limit |= 0xFFF;
            }
            tr.base = base;
            CLOCK_CYCLES(20);
            PREFETCH_RUN(20, 2, rmdat, (cpu_mod == 3) ? 0 : 1, 2, 0, 0, ea32);
            break;
        case 0x20: /*VERR*/
            if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
            sel = geteaw();
            if (cpu_state.abrt)
                return 1;
            flags_rebuild();
            if (!(sel & 0xfffc)) {
                cpu_state.flags &= ~Z_FLAG;
                return 0; /*Null selector*/
            }
            valid = (sel | 7) <= ((sel & 4) ? ldt.limit : gdt.limit);
            if (!valid) {
                /* Past the table limit: ZF=0, no descriptor read */
                cpu_state.flags &= ~Z_FLAG;
                CLOCK_CYCLES(20);
                PREFETCH_RUN(20, 2, rmdat, (cpu_mod == 3) ? 1 : 2, 0, 0, 0, ea32);
                break;
            }
            cpl_override = 1;
            desc         = readmemw(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 4);
            cpl_override = 0;
            if (cpu_state.abrt)
                return 1;
            if (!(desc & 0x1000))
                valid = 0;
            if ((desc & 0xC00) != 0xC00) /*Exclude conforming code segments*/
            {
                dpl = (desc >> 13) & 3; /*Check permissions*/
                if (dpl < CPL || dpl < (sel & 3))
                    valid = 0;
            }
            if ((desc & 0x0800) && !(desc & 0x0200))
                valid = 0; /*Non-readable code*/
            if (valid)
                cpu_state.flags |= Z_FLAG;
            else
                cpu_state.flags &= ~Z_FLAG;
            CLOCK_CYCLES(20);
            PREFETCH_RUN(20, 2, rmdat, (cpu_mod == 3) ? 1 : 2, 0, 0, 0, ea32);
            break;
        case 0x28: /*VERW*/
            if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
            sel = geteaw();
            if (cpu_state.abrt)
                return 1;
            flags_rebuild();
            if (!(sel & 0xfffc)) {
                cpu_state.flags &= ~Z_FLAG;
                return 0; /*Null selector*/
            }
            valid = (sel | 7) <= ((sel & 4) ? ldt.limit : gdt.limit);
            if (!valid) {
                /* Past the table limit: ZF=0, no descriptor read */
                cpu_state.flags &= ~Z_FLAG;
                CLOCK_CYCLES(20);
                PREFETCH_RUN(20, 2, rmdat, (cpu_mod == 3) ? 1 : 2, 0, 0, 0, ea32);
                break;
            }
            cpl_override = 1;
            desc         = readmemw(0, ((sel & 4) ? ldt.base : gdt.base) + (sel & ~7) + 4);
            cpl_override = 0;
            if (cpu_state.abrt)
                return 1;
            if (!(desc & 0x1000))
                valid = 0;
            dpl = (desc >> 13) & 3; /*Check permissions*/
            if (dpl < CPL || dpl < (sel & 3))
                valid = 0;
            if (desc & 0x0800)
                valid = 0; /*Code*/
            if (!(desc & 0x0200))
                valid = 0; /*Read-only data*/
            if (valid)
                cpu_state.flags |= Z_FLAG;
            else
                cpu_state.flags &= ~Z_FLAG;
            CLOCK_CYCLES(20);
            PREFETCH_RUN(20, 2, rmdat, (cpu_mod == 3) ? 1 : 2, 0, 0, 0, ea32);
            break;

        default:
            cpu_state.pc -= 3;
            x86illegal();
            break;
    }
    return cpu_state.abrt;
}

static int
op0F00_a16(uint32_t fetchdat)
{
    NOTRM

    fetch_ea_16(fetchdat);

    return op0F00_common(fetchdat, 0);
}
static int
op0F00_a32(uint32_t fetchdat)
{
    NOTRM

    fetch_ea_32(fetchdat);

    return op0F00_common(fetchdat, 1);
}

static int
op0F01_common(UNUSED(uint32_t fetchdat), int is32, int is286, UNUSED(int ea32))
{
    uint32_t base;
    uint16_t limit;
    uint16_t tempw;

    switch (rmdat & 0x38) {
        case 0x00: /*SGDT*/
            ILLEGAL_ON(cpu_mod == 3);
            SEG_CHECK_WRITE(cpu_state.ea_seg);
            CHECK_WRITE_EA(6); /* the limit and the base */
            seteaw(gdt.limit);
            base = gdt.base; // is32 ? gdt.base : (gdt.base & 0xffffff);
            if (is286)
                base |= 0xff000000;
            writememl(easeg, cpu_state.eaaddr + 2, base);
            CLOCK_CYCLES(7);
            PREFETCH_RUN(7, 2, rmdat, 0, 0, 1, 1, ea32);
            break;
        case 0x08: /*SIDT*/
            ILLEGAL_ON(cpu_mod == 3);
            SEG_CHECK_WRITE(cpu_state.ea_seg);
            CHECK_WRITE_EA(6); /* the limit and the base */
            seteaw(idt.limit);
            base = idt.base;
            if (is286)
                base |= 0xff000000;
            writememl(easeg, cpu_state.eaaddr + 2, base);
            CLOCK_CYCLES(7);
            PREFETCH_RUN(7, 2, rmdat, 0, 0, 1, 1, ea32);
            break;
        case 0x10: /*LGDT*/
            ILLEGAL_ON(cpu_mod == 3);
            if ((CPL || cpu_state.eflags & VM_FLAG) && (cr0 & 1)) {
                x86gpf(NULL, 0);
                break;
            }
            if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
            limit = geteaw();
            base  = readmeml(0, easeg + cpu_state.eaaddr + 2);
            if (cpu_state.abrt)
                return 1;
            gdt.limit = limit;
            gdt.base  = base;
            if (!is32)
                gdt.base &= 0xffffff;
            CLOCK_CYCLES(11);
            PREFETCH_RUN(11, 2, rmdat, 1, 1, 0, 0, ea32);
            break;
        case 0x18: /*LIDT*/
            ILLEGAL_ON(cpu_mod == 3);
            if ((CPL || cpu_state.eflags & VM_FLAG) && (cr0 & 1)) {
                x86gpf(NULL, 0);
                break;
            }
            if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
            limit = geteaw();
            base  = readmeml(0, easeg + cpu_state.eaaddr + 2);
            if (cpu_state.abrt)
                return 1;
            idt.limit = limit;
            idt.base  = base;
            if (!is32)
                idt.base &= 0xffffff;
            CLOCK_CYCLES(11);
            PREFETCH_RUN(11, 2, rmdat, 1, 1, 0, 0, ea32);
            break;

        case 0x20: /*SMSW*/
            if (cpu_mod != 3)
                SEG_CHECK_WRITE(cpu_state.ea_seg);
            if (is386 && is32 && (cpu_mod == 3)) {
                if (is486 || isibm486)
                    seteaw(cr0 | 0x10); /* ET is hardwired to 1 from the 486 on */
                else if (is386 && !cpu_16bitbus)
                    seteaw(cr0 | /* 0x7FFFFF00 */ 0x7FFFFFE0);
                else
                    seteaw(cr0 | 0x7FFFFFF0);
            } else {
                if (is486 || isibm486)
                    seteaw(msw | 0x10); /* ET is hardwired to 1 from the 486 on */
                else if (is386 && !cpu_16bitbus)
                    seteaw(msw | /* 0xFF00 */ 0xFFE0);
                else
                    seteaw(msw | 0xFFF0);
            }
            CLOCK_CYCLES(2);
            PREFETCH_RUN(2, 2, rmdat, 0, 0, (cpu_mod == 3) ? 0 : 1, 0, ea32);
            break;
        case 0x30: /*LMSW*/
            if ((CPL || cpu_state.eflags & VM_FLAG) && (msw & 1)) {
                x86gpf(NULL, 0);
                break;
            }
            if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
            tempw = geteaw();
            if (cpu_state.abrt)
                return 1;
            if (msw & 1)
                tempw |= 1;
            /* Only PE, MP, EM and TS load; PE can't be cleared, and every
               other bit, ET and NE included, stays */
            msw = (msw & ~0xF) | (tempw & 0xF);
            if (msw & 1)
                cpu_cur_status |= CPU_STATUS_PMODE;
            else
                cpu_cur_status &= ~CPU_STATUS_PMODE;
            PREFETCH_RUN(2, 2, rmdat, 0, 0, (cpu_mod == 3) ? 0 : 1, 0, ea32);
            break;

        case 0x38: /*INVLPG*/
            if (is486 || isibm486) {
                ILLEGAL_ON(cpu_mod == 3);
                if ((CPL || cpu_state.eflags & VM_FLAG) && (cr0 & 1)) {
                    x86gpf(NULL, 0);
                    break;
                }
                /* The fetch cache too: it may hold the page being invalidated.
                   Under the recompiler it isn't the page of this INVLPG, which
                   runs from a block; it is whatever page the interpreter last
                   fetched from, so it can be a page the guest has since mapped
                   elsewhere. */
                flushmmucache_pc();
                flushmmucache_nopc();
                CLOCK_CYCLES(12);
                PREFETCH_RUN(12, 2, rmdat, 0, 0, 0, 0, ea32);
                break;
            }

        default:
            cpu_state.pc -= 3;
            x86illegal();
            break;
    }
    return cpu_state.abrt;
}

static int
op0F01_w_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);

    return op0F01_common(fetchdat, 0, 0, 0);
}
static int
op0F01_w_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);

    return op0F01_common(fetchdat, 0, 0, 1);
}
static int
op0F01_l_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);

    return op0F01_common(fetchdat, 1, 0, 0);
}
static int
op0F01_l_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);

    return op0F01_common(fetchdat, 1, 0, 1);
}

static int
op0F01_286(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);

    return op0F01_common(fetchdat, 0, 1, 0);
}
