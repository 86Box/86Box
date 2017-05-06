static int opINT3(uint32_t fetchdat)
{
        int cycles_old = cycles;
        if ((cr0 & 1) && (eflags & VM_FLAG) && (IOPL != 3))
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
        int cycles_old = cycles;
        if ((cr0 & 1) && (eflags & VM_FLAG) && (IOPL != 3))
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
        int cycles_old = cycles;
        uint8_t temp;
        
        /*if (msw&1) pclog("INT %i %i %i\n",cr0&1,eflags&VM_FLAG,IOPL);*/
        if ((cr0 & 1) && (eflags & VM_FLAG) && (IOPL != 3))
        {
                x86gpf(NULL,0);
                return 1;
        }
        temp = getbytef();

        x86_int_sw(temp);
        PREFETCH_RUN(cycles_old-cycles, 2, -1, 0,0,0,0, 0);
        return 1;
}

static int opINTO(uint32_t fetchdat)
{
        int cycles_old = cycles;
        
        if ((cr0 & 1) && (eflags & VM_FLAG) && (IOPL != 3))
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

