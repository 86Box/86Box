static int opMOV_r_CRx_a16(uint32_t fetchdat)
{
        if ((CPL || (eflags&VM_FLAG)) && (cr0&1))
        {
                pclog("Can't load from CRx\n");
                x86gpf(NULL, 0);
                return 1;
        }
        fetch_ea_16(fetchdat);
        switch (cpu_reg)
        {
                case 0:
                cpu_state.regs[cpu_rm].l = cr0;
                if (is486)
                        cpu_state.regs[cpu_rm].l |= 0x10; /*ET hardwired on 486*/
                break;
                case 2:
                cpu_state.regs[cpu_rm].l = cr2;
                break;
                case 3:
                cpu_state.regs[cpu_rm].l = cr3;
                break;
                case 4:
                if (cpu_hasCR4)
                {
                        cpu_state.regs[cpu_rm].l = cr4;
                        break;
                }
                default:
                pclog("Bad read of CR%i %i\n",rmdat&7,cpu_reg);
                cpu_state.pc = oldpc;
                x86illegal();
                break;
        }
        CLOCK_CYCLES(6);
        return 0;
}
static int opMOV_r_CRx_a32(uint32_t fetchdat)
{
        if ((CPL || (eflags&VM_FLAG)) && (cr0&1))
        {
                pclog("Can't load from CRx\n");
                x86gpf(NULL, 0);
                return 1;
        }
        fetch_ea_32(fetchdat);
        switch (cpu_reg)
        {
                case 0:
                cpu_state.regs[cpu_rm].l = cr0;
                if (is486)
                        cpu_state.regs[cpu_rm].l |= 0x10; /*ET hardwired on 486*/
                break;
                case 2:
                cpu_state.regs[cpu_rm].l = cr2;
                break;
                case 3:
                cpu_state.regs[cpu_rm].l = cr3;
                break;
                case 4:
                if (cpu_hasCR4)
                {
                        cpu_state.regs[cpu_rm].l = cr4;
                        break;
                }
                default:
                pclog("Bad read of CR%i %i\n",rmdat&7,cpu_reg);
                cpu_state.pc = oldpc;
                x86illegal();
                break;
        }
        CLOCK_CYCLES(6);
        return 0;
}

static int opMOV_r_DRx_a16(uint32_t fetchdat)
{
        if ((CPL || (eflags&VM_FLAG)) && (cr0&1))
        {
                pclog("Can't load from DRx\n");
                x86gpf(NULL, 0);
                return 1;
        }
        fetch_ea_16(fetchdat);
        cpu_state.regs[cpu_rm].l = dr[cpu_reg];
        CLOCK_CYCLES(6);
        return 0;
}
static int opMOV_r_DRx_a32(uint32_t fetchdat)
{
        if ((CPL || (eflags&VM_FLAG)) && (cr0&1))
        {
                pclog("Can't load from DRx\n");
                x86gpf(NULL, 0);
                return 1;
        }
        fetch_ea_32(fetchdat);
        cpu_state.regs[cpu_rm].l = dr[cpu_reg];
        CLOCK_CYCLES(6);
        return 0;
}

static int opMOV_CRx_r_a16(uint32_t fetchdat)
{
        if ((CPL || (eflags&VM_FLAG)) && (cr0&1))
        {
                pclog("Can't load CRx\n");
                x86gpf(NULL,0);
                return 1;
        }
        fetch_ea_16(fetchdat);
        switch (cpu_reg)
        {
                case 0:
                if ((cpu_state.regs[cpu_rm].l ^ cr0) & 0x80000001)
                        flushmmucache();
                cr0 = cpu_state.regs[cpu_rm].l;
                if (cpu_16bitbus)
                        cr0 |= 0x10;
                if (!(cr0 & 0x80000000))
                        mmu_perm=4;
                break;
                case 2:
                cr2 = cpu_state.regs[cpu_rm].l;
                break;
                case 3:
                cr3 = cpu_state.regs[cpu_rm].l;
                flushmmucache();
                break;
                case 4:
                if (cpu_hasCR4)
                {
                        cr4 = cpu_state.regs[cpu_rm].l & cpu_CR4_mask;
                        break;
                }

                default:
                pclog("Bad load CR%i\n", cpu_reg);
                cpu_state.pc = oldpc;
                x86illegal();
                break;
        }
        CLOCK_CYCLES(10);
        return 0;
}
static int opMOV_CRx_r_a32(uint32_t fetchdat)
{
        if ((CPL || (eflags&VM_FLAG)) && (cr0&1))
        {
                pclog("Can't load CRx\n");
                x86gpf(NULL,0);
                return 1;
        }
        fetch_ea_32(fetchdat);
        switch (cpu_reg)
        {
                case 0:
                if ((cpu_state.regs[cpu_rm].l ^ cr0) & 0x80000001)
                        flushmmucache();
                cr0 = cpu_state.regs[cpu_rm].l;
                if (cpu_16bitbus)
                        cr0 |= 0x10;
                if (!(cr0 & 0x80000000))
                        mmu_perm=4;
                break;
                case 2:
                cr2 = cpu_state.regs[cpu_rm].l;
                break;
                case 3:
                cr3 = cpu_state.regs[cpu_rm].l;
                flushmmucache();
                break;
                case 4:
                if (cpu_hasCR4)
                {
                        cr4 = cpu_state.regs[cpu_rm].l & cpu_CR4_mask;
                        break;
                }

                default:
                pclog("Bad load CR%i\n", cpu_reg);
                cpu_state.pc = oldpc;
                x86illegal();
                break;
        }
        CLOCK_CYCLES(10);
        return 0;
}

static int opMOV_DRx_r_a16(uint32_t fetchdat)
{
        if ((CPL || (eflags&VM_FLAG)) && (cr0&1))
        {
                pclog("Can't load DRx\n");
                x86gpf(NULL, 0);
                return 1;
        }
        fetch_ea_16(fetchdat);
        dr[cpu_reg] = cpu_state.regs[cpu_rm].l;
        CLOCK_CYCLES(6);
        return 0;
}
static int opMOV_DRx_r_a32(uint32_t fetchdat)
{
        if ((CPL || (eflags&VM_FLAG)) && (cr0&1))
        {
                pclog("Can't load DRx\n");
                x86gpf(NULL, 0);
                return 1;
        }
        fetch_ea_16(fetchdat);
        dr[cpu_reg] = cpu_state.regs[cpu_rm].l;
        CLOCK_CYCLES(6);
        return 0;
}

static int opMOV_r_TRx_a16(uint32_t fetchdat)
{
        if ((CPL || (eflags&VM_FLAG)) && (cr0&1))
        {
                pclog("Can't load from TRx\n");
                x86gpf(NULL, 0);
                return 1;
        }
        fetch_ea_16(fetchdat);
        cpu_state.regs[cpu_rm].l = 0;
        CLOCK_CYCLES(6);
        return 0;
}
static int opMOV_r_TRx_a32(uint32_t fetchdat)
{
        if ((CPL || (eflags&VM_FLAG)) && (cr0&1))
        {
                pclog("Can't load from TRx\n");
                x86gpf(NULL, 0);
                return 1;
        }
        fetch_ea_32(fetchdat);
        cpu_state.regs[cpu_rm].l = 0;
        CLOCK_CYCLES(6);
        return 0;
}

static int opMOV_TRx_r_a16(uint32_t fetchdat)
{
        if ((CPL || (eflags&VM_FLAG)) && (cr0&1))
        {
                pclog("Can't load TRx\n");
                x86gpf(NULL, 0);
                return 1;
        }
        fetch_ea_16(fetchdat);
        CLOCK_CYCLES(6);
        return 0;
}
static int opMOV_TRx_r_a32(uint32_t fetchdat)
{
        if ((CPL || (eflags&VM_FLAG)) && (cr0&1))
        {
                pclog("Can't load TRx\n");
                x86gpf(NULL, 0);
                return 1;
        }
        fetch_ea_16(fetchdat);
        CLOCK_CYCLES(6);
        return 0;
}

