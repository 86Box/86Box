static int opRDTSC(uint32_t fetchdat)
{
        if (!cpu_has_feature(CPU_FEATURE_RDTSC))
        {
                cpu_state.pc = cpu_state.oldpc;
                x86illegal();
                return 1;
        }
        if ((cr4 & CR4_TSD) && CPL)
        {
                x86gpf("RDTSC when TSD set and CPL != 0", 0);
                return 1;
        }
        EAX = tsc & 0xffffffff;
        EDX = tsc >> 32;
        CLOCK_CYCLES(1);
        return 0;
}

static int opRDPMC(uint32_t fetchdat)
{
        if (ECX > 1 || (!(cr4 & CR4_PCE) && (cr0 & 1) && CPL))
        {
                x86gpf("RDPMC not allowed", 0);
                return 1;
        }
        EAX = EDX = 0;
        CLOCK_CYCLES(1);
        return 0;
}
