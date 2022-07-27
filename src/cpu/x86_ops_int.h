static int opINT3(uint32_t fetchdat)
{
        int cycles_old = cycles; UN_USED(cycles_old);
#ifdef USE_GDBSTUB
        if (gdbstub_int3())
                return 1;
#endif
        if ((cr0 & 1) && (cpu_state.eflags & VM_FLAG) && (IOPL != 3))
        {
                x86gpf(NULL,0);
                return 1;
        }
        x86_int_sw(3);
        CLOCK_CYCLES((is486) ? 44 : 59);
        PREFETCH_RUN(cycles_old-cycles, 1, -1, 0,0,0,0, 0);
        return 1;
}

static int opINT1(uint32_t fetchdat)
{
        int cycles_old = cycles; UN_USED(cycles_old);
        if ((cr0 & 1) && (cpu_state.eflags & VM_FLAG) && (IOPL != 3))
        {
                x86gpf(NULL,0);
                return 1;
        }
        x86_int_sw(1);
        CLOCK_CYCLES((is486) ? 44 : 59);
        PREFETCH_RUN(cycles_old-cycles, 1, -1, 0,0,0,0, 0);
        return 1;
}

static int opINT(uint32_t fetchdat)
{
        int cycles_old = cycles; UN_USED(cycles_old);
        uint8_t temp = getbytef();

        if ((cr0 & 1) && (cpu_state.eflags & VM_FLAG) && (IOPL != 3))
        {
                if (cr4 & CR4_VME)
                {
                        uint16_t t;
                        uint8_t d;

                        cpl_override = 1;
                        t = readmemw(tr.base, 0x66) - 32;
                        cpl_override = 0;
                        if (cpu_state.abrt) return 1;

                        t += (temp >> 3);
                        if (t <= tr.limit)
                        {
                                cpl_override = 1;
                                d = readmemb(tr.base, t);// + (temp >> 3));
                                cpl_override = 0;
                                if (cpu_state.abrt) return 1;

                                if (!(d & (1 << (temp & 7))))
                                {
                                        x86_int_sw_rm(temp);
                                        PREFETCH_RUN(cycles_old-cycles, 2, -1, 0,0,0,0, 0);
                                        return 1;
                                }
                        }
                }
                x86gpf_expected(NULL,0);
                return 1;
        }

        x86_int_sw(temp);
        PREFETCH_RUN(cycles_old-cycles, 2, -1, 0,0,0,0, 0);
        return 1;
}

static int opINTO(uint32_t fetchdat)
{
        int cycles_old = cycles; UN_USED(cycles_old);

        if ((cr0 & 1) && (cpu_state.eflags & VM_FLAG) && (IOPL != 3))
        {
                x86gpf(NULL,0);
                return 1;
        }
        if (VF_SET())
        {
                cpu_state.oldpc = cpu_state.pc;
                x86_int_sw(4);
                PREFETCH_RUN(cycles_old-cycles, 1, -1, 0,0,0,0, 0);
                return 1;
        }
        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 1, -1, 0,0,0,0, 0);
        return 0;
}
