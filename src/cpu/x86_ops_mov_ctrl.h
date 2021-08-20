static int opMOV_r_CRx_a16(uint32_t fetchdat)
{
        if ((CPL || (cpu_state.eflags&VM_FLAG)) && (cr0&1))
        {
                x86gpf(NULL, 0);
                return 1;
        }
        fetch_ea_16(fetchdat);
        switch (cpu_reg)
        {
                case 0:
                cpu_state.regs[cpu_rm].l = cr0;
                if (is486 || isibm486)
                        cpu_state.regs[cpu_rm].l |= 0x10; /*ET hardwired on 486*/
		else {
			if (is386)
				cpu_state.regs[cpu_rm].l |=0x7fffffe0;
			else
				cpu_state.regs[cpu_rm].l |=0x7ffffff0;
		}
                break;
                case 2:
                cpu_state.regs[cpu_rm].l = cr2;
                break;
                case 3:
                cpu_state.regs[cpu_rm].l = cr3;
                break;
                case 4:
                if (cpu_has_feature(CPU_FEATURE_CR4))
                {
                        cpu_state.regs[cpu_rm].l = cr4;
                        break;
                }
                default:
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                break;
        }
        CLOCK_CYCLES(6);
        PREFETCH_RUN(6, 2, rmdat, 0,0,0,0, 0);
        return 0;
}
static int opMOV_r_CRx_a32(uint32_t fetchdat)
{
        if ((CPL || (cpu_state.eflags&VM_FLAG)) && (cr0&1))
        {
                x86gpf(NULL, 0);
                return 1;
        }
        fetch_ea_32(fetchdat);
        switch (cpu_reg)
        {
                case 0:
                cpu_state.regs[cpu_rm].l = cr0;
                if (is486 || isibm486)
                        cpu_state.regs[cpu_rm].l |= 0x10; /*ET hardwired on 486*/
		else {
			if (is386)
				cpu_state.regs[cpu_rm].l |=0x7fffffe0;
			else
				cpu_state.regs[cpu_rm].l |=0x7ffffff0;
		}
                break;
                case 2:
                cpu_state.regs[cpu_rm].l = cr2;
                break;
                case 3:
                cpu_state.regs[cpu_rm].l = cr3;
                break;
                case 4:
                if (cpu_has_feature(CPU_FEATURE_CR4))
                {
                        cpu_state.regs[cpu_rm].l = cr4;
                        break;
                }
                default:
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                break;
        }
        CLOCK_CYCLES(6);
        PREFETCH_RUN(6, 2, rmdat, 0,0,0,0, 1);
        return 0;
}

static int opMOV_r_DRx_a16(uint32_t fetchdat)
{
        if ((CPL || (cpu_state.eflags&VM_FLAG)) && (cr0&1))
        {
                x86gpf(NULL, 0);
                return 1;
        }
        fetch_ea_16(fetchdat);
        cpu_state.regs[cpu_rm].l = dr[cpu_reg];
        CLOCK_CYCLES(6);
        PREFETCH_RUN(6, 2, rmdat, 0,0,0,0, 0);
        return 0;
}
static int opMOV_r_DRx_a32(uint32_t fetchdat)
{
        if ((CPL || (cpu_state.eflags&VM_FLAG)) && (cr0&1))
        {
                x86gpf(NULL, 0);
                return 1;
        }
        fetch_ea_32(fetchdat);
        cpu_state.regs[cpu_rm].l = dr[cpu_reg];
        CLOCK_CYCLES(6);
        PREFETCH_RUN(6, 2, rmdat, 0,0,0,0, 1);
        return 0;
}

static int opMOV_CRx_r_a16(uint32_t fetchdat)
{
        uint32_t old_cr0 = cr0;
        
        if ((CPL || (cpu_state.eflags&VM_FLAG)) && (cr0&1))
        {
                x86gpf(NULL,0);
                return 1;
        }
        fetch_ea_16(fetchdat);
        switch (cpu_reg)
        {
                case 0:
                if ((cpu_state.regs[cpu_rm].l ^ cr0) & 0x80000001)
                        flushmmucache();
		/* Make sure CPL = 0 when switching from real mode to protected mode. */
		if ((cpu_state.regs[cpu_rm].l & 0x01) && !(cr0 & 0x01))
			cpu_state.seg_cs.access &= 0x9f;
                cr0 = cpu_state.regs[cpu_rm].l;
                if (cpu_16bitbus)
                        cr0 |= 0x10;
                if (!(cr0 & 0x80000000))
                        mmu_perm=4;
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
                if (cpu_has_feature(CPU_FEATURE_CR4))
                {
	                if (((cpu_state.regs[cpu_rm].l ^ cr4) & cpu_CR4_mask) & CR4_PAE)
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
        PREFETCH_RUN(10, 2, rmdat, 0,0,0,0, 0);
        return 0;
}
static int opMOV_CRx_r_a32(uint32_t fetchdat)
{
        uint32_t old_cr0 = cr0;
        
        if ((CPL || (cpu_state.eflags&VM_FLAG)) && (cr0&1))
        {
                x86gpf(NULL,0);
                return 1;
        }
        fetch_ea_32(fetchdat);
        switch (cpu_reg)
        {
                case 0:
                if ((cpu_state.regs[cpu_rm].l ^ cr0) & 0x80000001)
                        flushmmucache();
		/* Make sure CPL = 0 when switching from real mode to protected mode. */
		if ((cpu_state.regs[cpu_rm].l & 0x01) && !(cr0 & 0x01))
			cpu_state.seg_cs.access &= 0x9f;
                cr0 = cpu_state.regs[cpu_rm].l;
                if (cpu_16bitbus)
                        cr0 |= 0x10;
                if (!(cr0 & 0x80000000))
                        mmu_perm=4;
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
                if (cpu_has_feature(CPU_FEATURE_CR4))
                {
	                if (((cpu_state.regs[cpu_rm].l ^ cr4) & cpu_CR4_mask) & CR4_PAE)
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
        PREFETCH_RUN(10, 2, rmdat, 0,0,0,0, 1);
        return 0;
}

static int opMOV_DRx_r_a16(uint32_t fetchdat)
{
        if ((CPL || (cpu_state.eflags&VM_FLAG)) && (cr0&1))
        {
                x86gpf(NULL, 0);
                return 1;
        }
        fetch_ea_16(fetchdat);
        dr[cpu_reg] = cpu_state.regs[cpu_rm].l;
        CLOCK_CYCLES(6);
        PREFETCH_RUN(6, 2, rmdat, 0,0,0,0, 0);
        return 0;
}
static int opMOV_DRx_r_a32(uint32_t fetchdat)
{
        if ((CPL || (cpu_state.eflags&VM_FLAG)) && (cr0&1))
        {
                x86gpf(NULL, 0);
                return 1;
        }
        fetch_ea_16(fetchdat);
        dr[cpu_reg] = cpu_state.regs[cpu_rm].l;
        CLOCK_CYCLES(6);
        PREFETCH_RUN(6, 2, rmdat, 0,0,0,0, 1);
        return 0;
}

static int opMOV_r_TRx_a16(uint32_t fetchdat)
{
        if ((CPL || (cpu_state.eflags&VM_FLAG)) && (cr0&1))
        {
                x86gpf(NULL, 0);
                return 1;
        }
        fetch_ea_16(fetchdat);
        cpu_state.regs[cpu_rm].l = 0;
        CLOCK_CYCLES(6);
        PREFETCH_RUN(6, 2, rmdat, 0,0,0,0, 0);
        return 0;
}
static int opMOV_r_TRx_a32(uint32_t fetchdat)
{
        if ((CPL || (cpu_state.eflags&VM_FLAG)) && (cr0&1))
        {
                x86gpf(NULL, 0);
                return 1;
        }
        fetch_ea_32(fetchdat);
        cpu_state.regs[cpu_rm].l = 0;
        CLOCK_CYCLES(6);
        PREFETCH_RUN(6, 2, rmdat, 0,0,0,0, 1);
        return 0;
}

static int opMOV_TRx_r_a16(uint32_t fetchdat)
{
        if ((CPL || (cpu_state.eflags&VM_FLAG)) && (cr0&1))
        {
                x86gpf(NULL, 0);
                return 1;
        }
        fetch_ea_16(fetchdat);
        CLOCK_CYCLES(6);
        PREFETCH_RUN(6, 2, rmdat, 0,0,0,0, 0);
        return 0;
}
static int opMOV_TRx_r_a32(uint32_t fetchdat)
{
        if ((CPL || (cpu_state.eflags&VM_FLAG)) && (cr0&1))
        {
                x86gpf(NULL, 0);
                return 1;
        }
        fetch_ea_16(fetchdat);
        CLOCK_CYCLES(6);
        PREFETCH_RUN(6, 2, rmdat, 0,0,0,0, 1);
        return 0;
}

