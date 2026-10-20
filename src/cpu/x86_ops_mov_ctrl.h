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
            cpu_state.regs[cpu_rm].l = cr0 | ((is486 || isibm486) ? 0x10 : 0); /* ET hardwired from the 486 on */
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
            cpu_state.regs[cpu_rm].l = cr0 | ((is486 || isibm486) ? 0x10 : 0); /* ET hardwired from the 486 on */
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
    /* General detect: a #DB fault before the instruction, with DR6.BD set */
    if (cpu_debug_regs_486 && (dr[7] & 0x2000)) {
        dr[6] |= 0x2000;
        x86gen();
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
    /* General detect: a #DB fault before the instruction, with DR6.BD set */
    if (cpu_debug_regs_486 && (dr[7] & 0x2000)) {
        dr[6] |= 0x2000;
        x86gen();
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
            if (cpu_cr0_invalid(cpu_state.regs[cpu_rm].l)) {
                x86gpf(NULL, 0);
                return 1;
            }
            /* Setting PG with PAE on loads the PDPTEs */
            if ((cpu_state.regs[cpu_rm].l & 0x80000000) && !(cr0 & 0x80000000) && (cr4 & CR4_PAE) && pae_check_load(cr3)) {
                x86gpf(NULL, 0);
                return 1;
            }
            if ((cpu_state.regs[cpu_rm].l ^ cr0) & 0x00000001)
                flushmmucache();
            else if ((cpu_state.regs[cpu_rm].l ^ cr0) & 0x80000000) {
                if (is_p6)
                    flushmmucache();
                else {
                    flushmmucache_nopc();
                    cpu_flush_pending = 1;
                    /* The recompiler runs the next instruction through the
                       interpreter, which still fetches it with the old
                       paging state. */
                    if (cpu_use_dynarec)
                        CPU_BLOCK_END();
                }
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
            if ((cr0 ^ old_cr0) & (1 << 30))
                cpu_car_update();
            if (cr0 & 1)
                cpu_cur_status |= CPU_STATUS_PMODE;
            else
                cpu_cur_status &= ~CPU_STATUS_PMODE;
            break;
        case 2:
            cr2 = cpu_state.regs[cpu_rm].l;
            break;
        case 3:
            /* With PAE paging, the PDPTEs load with CR3 */
            if ((cr4 & CR4_PAE) && (cr0 & 0x80000000) && pae_check_load(cpu_state.regs[cpu_rm].l)) {
                x86gpf(NULL, 0);
                return 1;
            }
            cr3 = cpu_state.regs[cpu_rm].l;
            flushmmucache();
            break;
        case 4:
            if (cpu_has_feature(CPU_FEATURE_CR4)) {
                /* A bit this model doesn't have is #GP(0), not dropped. */
                if (cpu_state.regs[cpu_rm].l & ~(cpu_CR4_mask | cpu_CR4_ignore)) {
                    x86gpf(NULL, 0);
                    return 1;
                }
                /* Setting PAE with paging on loads the PDPTEs */
                if ((cpu_state.regs[cpu_rm].l & CR4_PAE) && !(cr4 & CR4_PAE) && (cr0 & 0x80000000) && pae_check_load(cr3)) {
                    x86gpf(NULL, 0);
                    return 1;
                }
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
            if (cpu_cr0_invalid(cpu_state.regs[cpu_rm].l)) {
                x86gpf(NULL, 0);
                return 1;
            }
            /* Setting PG with PAE on loads the PDPTEs */
            if ((cpu_state.regs[cpu_rm].l & 0x80000000) && !(cr0 & 0x80000000) && (cr4 & CR4_PAE) && pae_check_load(cr3)) {
                x86gpf(NULL, 0);
                return 1;
            }
            if ((cpu_state.regs[cpu_rm].l ^ cr0) & 0x00000001)
                flushmmucache();
            else if ((cpu_state.regs[cpu_rm].l ^ cr0) & 0x80000000) {
                if (is_p6)
                    flushmmucache();
                else {
                    flushmmucache_nopc();
                    cpu_flush_pending = 1;
                    /* The recompiler runs the next instruction through the
                       interpreter, which still fetches it with the old
                       paging state. */
                    if (cpu_use_dynarec)
                        CPU_BLOCK_END();
                }
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
            if ((cr0 ^ old_cr0) & (1 << 30))
                cpu_car_update();
            if (cr0 & 1)
                cpu_cur_status |= CPU_STATUS_PMODE;
            else
                cpu_cur_status &= ~CPU_STATUS_PMODE;
            break;
        case 2:
            cr2 = cpu_state.regs[cpu_rm].l;
            break;
        case 3:
            /* With PAE paging, the PDPTEs load with CR3 */
            if ((cr4 & CR4_PAE) && (cr0 & 0x80000000) && pae_check_load(cpu_state.regs[cpu_rm].l)) {
                x86gpf(NULL, 0);
                return 1;
            }
            cr3 = cpu_state.regs[cpu_rm].l;
            flushmmucache();
            break;
        case 4:
            if (cpu_has_feature(CPU_FEATURE_CR4)) {
                /* A bit this model doesn't have is #GP(0), not dropped. */
                if (cpu_state.regs[cpu_rm].l & ~(cpu_CR4_mask | cpu_CR4_ignore)) {
                    x86gpf(NULL, 0);
                    return 1;
                }
                /* Setting PAE with paging on loads the PDPTEs */
                if ((cpu_state.regs[cpu_rm].l & CR4_PAE) && !(cr4 & CR4_PAE) && (cr0 & 0x80000000) && pae_check_load(cr3)) {
                    x86gpf(NULL, 0);
                    return 1;
                }
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
    /* General detect: a #DB fault before the instruction, with DR6.BD set */
    if (cpu_debug_regs_486 && (dr[7] & 0x2000)) {
        dr[6] |= 0x2000;
        x86gen();
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
            dr[6] = (dr[6] & 0xffff0ff0) | (cpu_state.regs[cpu_rm].l & (is586 ? 0x0000e00f : 0x0000f00f)); /* bit 12 is 0 from the Pentium on */
            break;
        case 5:
            if (cr4 & 0x8) {
                x86illegal();
                return 1;
            }
            fallthrough;
        case 7:
            dr[7] = (cpu_state.regs[cpu_rm].l | 0x00000400) & (is586 ? ~0x0000d800 : ~0);
            cpu_dr7_update();
            break;
        default:
            x86illegal();
            return 1;
    }
    CLOCK_CYCLES(6);
    PREFETCH_RUN(6, 2, rmdat, 0, 0, 0, 0, 0);
    if (cpu_debug_regs_486)
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
    /* General detect: a #DB fault before the instruction, with DR6.BD set */
    if (cpu_debug_regs_486 && (dr[7] & 0x2000)) {
        dr[6] |= 0x2000;
        x86gen();
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
            dr[6] = (dr[6] & 0xffff0ff0) | (cpu_state.regs[cpu_rm].l & (is586 ? 0x0000e00f : 0x0000f00f)); /* bit 12 is 0 from the Pentium on */
            break;
        case 5:
            if (cr4 & 0x8) {
                x86illegal();
                return 1;
            }
            fallthrough;
        case 7:
            dr[7] = (cpu_state.regs[cpu_rm].l | 0x00000400) & (is586 ? ~0x0000d800 : ~0);
            cpu_dr7_update();
            break;
        default:
            x86illegal();
            return 1;
    }
    CLOCK_CYCLES(6);
    PREFETCH_RUN(6, 2, rmdat, 0, 0, 0, 0, 1);
    if (cpu_debug_regs_486)
        CPU_BLOCK_END();
    return 0;
}

static void
opMOV_r_TRx(void)
{
    cpu_state.regs[cpu_rm].l = cpu_tr_read(cpu_reg);
    CLOCK_CYCLES(6);
}
static int
opMOV_r_TRx_a16(uint32_t fetchdat)
{
    if ((CPL || (cpu_state.eflags & VM_FLAG)) && (cr0 & 1)) {
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
    if ((CPL || (cpu_state.eflags & VM_FLAG)) && (cr0 & 1)) {
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
    cpu_tr_write(cpu_reg, cpu_state.regs[cpu_rm].l);
    CLOCK_CYCLES(6);
}
static int
opMOV_TRx_r_a16(uint32_t fetchdat)
{
    if ((CPL || (cpu_state.eflags & VM_FLAG)) && (cr0 & 1)) {
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
    if ((CPL || (cpu_state.eflags & VM_FLAG)) && (cr0 & 1)) {
        x86gpf(NULL, 0);
        return 1;
    }
    fetch_ea_32(fetchdat);
    opMOV_TRx_r();
    PREFETCH_RUN(6, 2, rmdat, 0, 0, 0, 0, 1);
    return 0;
}
