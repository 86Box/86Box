static int
opMOV_r_CRx_a16(uint32_t fetchdat)
{
    if ((CPL || (cpu_state.eflags & VM_FLAG)) && (cr0 & 1)) {
        x86gpf(NULL, 0);
        return 1;
    }
    fetch_ea_16(fetchdat);
    switch (cpu_reg) {
        case 0:
            cpu_state.regs[cpu_rm].l = cr0;
            if (cpu_flush_pending)
                cpu_state.regs[cpu_rm].l ^= 0x80000000;
            if (is486 || isibm486)
                cpu_state.regs[cpu_rm].l |= 0x10; /*ET hardwired on 486*/
            else {
                if (is386)
                    cpu_state.regs[cpu_rm].l |= 0x7fffffe0;
                else
                    cpu_state.regs[cpu_rm].l |= 0x7ffffff0;
            }
            break;
        case 2:
            cpu_state.regs[cpu_rm].l = cr2;
            break;
        case 3:
            cpu_state.regs[cpu_rm].l = cr3;
            break;
        case 4:
            if (cpu_has_feature(CPU_FEATURE_CR4)) {
                cpu_state.regs[cpu_rm].l = cr4;
                break;
            }
        default:
            cpu_state.pc = cpu_state.oldpc;
            x86illegal();
            break;
    }
    CLOCK_CYCLES(6);
    PREFETCH_RUN(6, 2, rmdat, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_r_CRx_a32(uint32_t fetchdat)
{
    if ((CPL || (cpu_state.eflags & VM_FLAG)) && (cr0 & 1)) {
        x86gpf(NULL, 0);
        return 1;
    }
    fetch_ea_32(fetchdat);
    switch (cpu_reg) {
        case 0:
            cpu_state.regs[cpu_rm].l = cr0;
            if (cpu_flush_pending)
                cpu_state.regs[cpu_rm].l ^= 0x80000000;
            if (is486 || isibm486)
                cpu_state.regs[cpu_rm].l |= 0x10; /*ET hardwired on 486*/
            else {
                if (is386)
                    cpu_state.regs[cpu_rm].l |= 0x7fffffe0;
                else
                    cpu_state.regs[cpu_rm].l |= 0x7ffffff0;
            }
            break;
        case 2:
            cpu_state.regs[cpu_rm].l = cr2;
            break;
        case 3:
            cpu_state.regs[cpu_rm].l = cr3;
            break;
        case 4:
            if (cpu_has_feature(CPU_FEATURE_CR4)) {
                cpu_state.regs[cpu_rm].l = cr4;
                break;
            }
        default:
            cpu_state.pc = cpu_state.oldpc;
            x86illegal();
            break;
    }
    CLOCK_CYCLES(6);
    PREFETCH_RUN(6, 2, rmdat, 0, 0, 0, 0, 1);
    return 0;
}

static int
opMOV_r_DRx_a16(uint32_t fetchdat)
{
    if ((CPL > 0) && (cr0 & 1)) {
        x86gpf(NULL, 0);
        return 1;
    }
    if ((dr[7] & 0x2000) && !(cpu_state.eflags & RF_FLAG)) {
        trap |= 1;
        return 1;
    }
    fetch_ea_16(fetchdat);
    switch (cpu_reg) {
        case 0 ... 3:
            cpu_state.regs[cpu_rm].l = dr[cpu_reg];
            break;
        case 4:
            if (cr4 & 0x8) {
                x86illegal();
                return 1;
            }
            fallthrough;
        case 6:
            cpu_state.regs[cpu_rm].l = dr[6];
            break;
        case 5:
            if (cr4 & 0x8) {
                x86illegal();
                return 1;
            }
            fallthrough;
        case 7:
            cpu_state.regs[cpu_rm].l = dr[7];
            break;
        default:
            x86illegal();
            return 1;
    }
    CLOCK_CYCLES(6);
    PREFETCH_RUN(6, 2, rmdat, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_r_DRx_a32(uint32_t fetchdat)
{
    if ((CPL > 0) && (cr0 & 1)) {
        x86gpf(NULL, 0);
        return 1;
    }
    if ((dr[7] & 0x2000) && !(cpu_state.eflags & RF_FLAG)) {
        trap |= 1;
        return 1;
    }
    fetch_ea_32(fetchdat);
    switch (cpu_reg) {
        case 0 ... 3:
            cpu_state.regs[cpu_rm].l = dr[cpu_reg];
            break;
        case 4:
            if (cr4 & 0x8) {
                x86illegal();
                return 1;
            }
            fallthrough;
        case 6:
            cpu_state.regs[cpu_rm].l = dr[6];
            break;
        case 5:
            if (cr4 & 0x8) {
                x86illegal();
                return 1;
            }
            fallthrough;
        case 7:
            cpu_state.regs[cpu_rm].l = dr[7];
            break;
        default:
            x86illegal();
            return 1;
    }
    CLOCK_CYCLES(6);
    PREFETCH_RUN(6, 2, rmdat, 0, 0, 0, 0, 1);
    return 0;
}

static int
opMOV_CRx_r_a16(uint32_t fetchdat)
{
    uint32_t old_cr0 = cr0;

    if ((CPL || (cpu_state.eflags & VM_FLAG)) && (cr0 & 1)) {
        x86gpf(NULL, 0);
        return 1;
    }
    fetch_ea_16(fetchdat);
    switch (cpu_reg) {
        case 0:
            if ((cpu_state.regs[cpu_rm].l ^ cr0) & 0x00000001)
                flushmmucache();
            else if ((cpu_state.regs[cpu_rm].l ^ cr0) & 0x80000000) {
                flushmmucache_nopc();
                cpu_flush_pending = 1;
            } else if ((cpu_state.regs[cpu_rm].l ^ cr0) & WP_FLAG)
                flushmmucache_write();
            /* Make sure CPL = 0 when switching from real mode to protected mode. */
            if ((cpu_state.regs[cpu_rm].l & 0x01) && !(cr0 & 0x01))
                cpu_state.seg_cs.access &= 0x9f;
            cr0 = cpu_state.regs[cpu_rm].l;
            if (cpu_16bitbus)
                cr0 |= 0x10;
            if (hascache && !(cr0 & (1 << 30)))
                cpu_cache_int_enabled = 1;
            else
                cpu_cache_int_enabled = 0;
            if (hascache && ((cr0 ^ old_cr0) & (1 << 30)))
                cpu_update_waitstates();
            if (cr0 & 1)
                cpu_cur_status |= CPU_STATUS_PMODE;
            else
                cpu_cur_status &= ~CPU_STATUS_PMODE;
            break;
        case 2:
            cr2 = cpu_state.regs[cpu_rm].l;
            break;
        case 3:
            cr3 = cpu_state.regs[cpu_rm].l;
            flushmmucache();
            break;
        case 4:
            if (cpu_has_feature(CPU_FEATURE_CR4)) {
                if (((cpu_state.regs[cpu_rm].l ^ cr4) & cpu_CR4_mask) & (CR4_PSE | CR4_PAE | CR4_PGE))
                    flushmmucache();
                cr4 = cpu_state.regs[cpu_rm].l & cpu_CR4_mask;
                break;
            }

        default:
            cpu_state.pc = cpu_state.oldpc;
            x86illegal();
            break;
    }
    CLOCK_CYCLES(10);
    PREFETCH_RUN(10, 2, rmdat, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_CRx_r_a32(uint32_t fetchdat)
{
    uint32_t old_cr0 = cr0;

    if ((CPL || (cpu_state.eflags & VM_FLAG)) && (cr0 & 1)) {
        x86gpf(NULL, 0);
        return 1;
    }
    fetch_ea_32(fetchdat);
    switch (cpu_reg) {
        case 0:
            if ((cpu_state.regs[cpu_rm].l ^ cr0) & 0x00000001)
                flushmmucache();
            else if ((cpu_state.regs[cpu_rm].l ^ cr0) & 0x80000000) {
                flushmmucache_nopc();
                cpu_flush_pending = 1;
            } else if ((cpu_state.regs[cpu_rm].l ^ cr0) & WP_FLAG)
                flushmmucache_write();
            /* Make sure CPL = 0 when switching from real mode to protected mode. */
            if ((cpu_state.regs[cpu_rm].l & 0x01) && !(cr0 & 0x01))
                cpu_state.seg_cs.access &= 0x9f;
            cr0 = cpu_state.regs[cpu_rm].l;
            if (cpu_16bitbus)
                cr0 |= 0x10;
            if (hascache && !(cr0 & (1 << 30)))
                cpu_cache_int_enabled = 1;
            else
                cpu_cache_int_enabled = 0;
            if (hascache && ((cr0 ^ old_cr0) & (1 << 30)))
                cpu_update_waitstates();
            if (cr0 & 1)
                cpu_cur_status |= CPU_STATUS_PMODE;
            else
                cpu_cur_status &= ~CPU_STATUS_PMODE;
            break;
        case 2:
            cr2 = cpu_state.regs[cpu_rm].l;
            break;
        case 3:
            cr3 = cpu_state.regs[cpu_rm].l;
            flushmmucache();
            break;
        case 4:
            if (cpu_has_feature(CPU_FEATURE_CR4)) {
                if (((cpu_state.regs[cpu_rm].l ^ cr4) & cpu_CR4_mask) & (CR4_PSE | CR4_PAE | CR4_PGE))
                    flushmmucache();
                cr4 = cpu_state.regs[cpu_rm].l & cpu_CR4_mask;
                break;
            }

        default:
            cpu_state.pc = cpu_state.oldpc;
            x86illegal();
            break;
    }
    CLOCK_CYCLES(10);
    PREFETCH_RUN(10, 2, rmdat, 0, 0, 0, 0, 1);
    return 0;
}

static int
opMOV_DRx_r_a16(uint32_t fetchdat)
{
    if ((CPL > 0) && (cr0 & 1)) {
        x86gpf(NULL, 0);
        return 1;
    }
    if ((dr[7] & 0x2000) && !(cpu_state.eflags & RF_FLAG)) {
        trap |= 1;
        return 1;
    }
    fetch_ea_16(fetchdat);
    switch (cpu_reg) {
        case 0 ... 3:
            dr[cpu_reg] = cpu_state.regs[cpu_rm].l;
            break;
        case 4:
            if (cr4 & 0x8) {
                x86illegal();
                return 1;
            }
            fallthrough;
        case 6:
            dr[6] = (dr[6] & 0xffff0ff0) | (cpu_state.regs[cpu_rm].l & 0x0000f00f);
            break;
        case 5:
            if (cr4 & 0x8) {
                x86illegal();
                return 1;
            }
            fallthrough;
        case 7:
            dr[7] = cpu_state.regs[cpu_rm].l | 0x00000400;
            break;
        default:
            x86illegal();
            return 1;
    }
    CLOCK_CYCLES(6);
    PREFETCH_RUN(6, 2, rmdat, 0, 0, 0, 0, 0);
    CPU_BLOCK_END();
    return 0;
}
static int
opMOV_DRx_r_a32(uint32_t fetchdat)
{
    if ((CPL > 0) && (cr0 & 1)) {
        x86gpf(NULL, 0);
        return 1;
    }
    if ((dr[7] & 0x2000) && !(cpu_state.eflags & RF_FLAG)) {
        trap |= 1;
        return 1;
    }
    fetch_ea_32(fetchdat);
    switch (cpu_reg) {
        case 0 ... 3:
            dr[cpu_reg] = cpu_state.regs[cpu_rm].l;
            break;
        case 4:
            if (cr4 & 0x8) {
                x86illegal();
                return 1;
            }
            fallthrough;
        case 6:
            dr[6] = (dr[6] & 0xffff0ff0) | (cpu_state.regs[cpu_rm].l & 0x0000f00f);
            break;
        case 5:
            if (cr4 & 0x8) {
                x86illegal();
                return 1;
            }
            fallthrough;
        case 7:
            dr[7] = cpu_state.regs[cpu_rm].l | 0x00000400;
            break;
        default:
            x86illegal();
            return 1;
    }
    CLOCK_CYCLES(6);
    PREFETCH_RUN(6, 2, rmdat, 0, 0, 0, 0, 1);
    CPU_BLOCK_END();
    return 0;
}

static void
opMOV_r_TRx(void)
{
#if 0
    uint32_t base;

    base = _tr[4] & 0xfffff800;
#endif

    switch (cpu_reg) {
        case 3:
#if 0
            pclog("[R] %08X cache = %08X\n", base + cache_index, _tr[3]);
#endif
            _tr[3]      = *(uint32_t *) &(_cache[cache_index]);
            cache_index = (cache_index + 4) & 0xf;
            break;
    }
    cpu_state.regs[cpu_rm].l = _tr[cpu_reg];
    CLOCK_CYCLES(6);
}
static int
opMOV_r_TRx_a16(uint32_t fetchdat)
{
    if ((cpu_s->cpu_type == CPU_PENTIUM) || ((CPL || (cpu_state.eflags & VM_FLAG)) && (cr0 & 1))) {
        x86gpf(NULL, 0);
        return 1;
    }
    fetch_ea_16(fetchdat);
    opMOV_r_TRx();
    PREFETCH_RUN(6, 2, rmdat, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_r_TRx_a32(uint32_t fetchdat)
{
    if ((cpu_s->cpu_type == CPU_PENTIUM) || ((CPL || (cpu_state.eflags & VM_FLAG)) && (cr0 & 1))) {
        x86gpf(NULL, 0);
        return 1;
    }
    fetch_ea_32(fetchdat);
    opMOV_r_TRx();
    PREFETCH_RUN(6, 2, rmdat, 0, 0, 0, 0, 1);
    return 0;
}

static void
opMOV_TRx_r(void)
{
    uint32_t base;
    int      i;
    int      ctl;

    _tr[cpu_reg] = cpu_state.regs[cpu_rm].l;
    base         = _tr[4] & 0xfffff800;
    ctl          = _tr[5] & 3;
    switch (cpu_reg) {
        case 3:
#if 0
            pclog("[W] %08X cache = %08X\n", base + cache_index, _tr[3]);
#endif
            *(uint32_t *) &(_cache[cache_index]) = _tr[3];
            cache_index                          = (cache_index + 4) & 0xf;
            break;
        case 4:
#if 0
            if (!(cr0 & 1) && !(_tr[5] & (1 << 19)))
                pclog("TAG = %08X, DEST = %08X\n", base, base + cache_index - 16);
#endif
            break;
        case 5:
#if 0
            pclog("[16] EXT = %i (%i), SET = %04X\n", !!(_tr[5] & (1 << 19)), _tr[5] & 0x03, _tr[5] & 0x7f0);
#endif
            if (!(_tr[5] & (1 << 19))) {
                switch (ctl) {
                    case 0:
#if 0
                        pclog("    Cache fill or read...\n", base);
#endif
                        break;
                    case 1:
                        base += (_tr[5] & 0x7f0);
#if 0
                        pclog("    Writing 16 bytes to   %08X...\n", base);
#endif
                        for (i = 0; i < 16; i += 4)
                            mem_writel_phys(base + i, *(uint32_t *) &(_cache[i]));
                        break;
                    case 2:
                        base += (_tr[5] & 0x7f0);
#if 0
                        pclog("    Reading 16 bytes from %08X...\n", base);
#endif
                        for (i = 0; i < 16; i += 4)
                            *(uint32_t *) &(_cache[i]) = mem_readl_phys(base + i);
                        break;
                    case 3:
#if 0
                        pclog("    Cache invalidate/flush...\n", base);
#endif
                        break;
                }
            }
            break;
    }
    CLOCK_CYCLES(6);
}
static int
opMOV_TRx_r_a16(uint32_t fetchdat)
{
    if ((cpu_s->cpu_type == CPU_PENTIUM) || ((CPL || (cpu_state.eflags & VM_FLAG)) && (cr0 & 1))) {
        x86gpf(NULL, 0);
        return 1;
    }
    fetch_ea_16(fetchdat);
    opMOV_TRx_r();
    PREFETCH_RUN(6, 2, rmdat, 0, 0, 0, 0, 0);
    return 0;
}
static int
opMOV_TRx_r_a32(uint32_t fetchdat)
{
    if ((cpu_s->cpu_type == CPU_PENTIUM) || ((CPL || (cpu_state.eflags & VM_FLAG)) && (cr0 & 1))) {
        x86gpf(NULL, 0);
        return 1;
    }
    fetch_ea_32(fetchdat);
    opMOV_TRx_r();
    PREFETCH_RUN(6, 2, rmdat, 0, 0, 0, 0, 1);
    return 0;
}
