static int
opRDTSC(UNUSED(uint32_t fetchdat))
{
    if (!cpu_has_feature(CPU_FEATURE_RDTSC)) {
        cpu_state.pc = cpu_state.oldpc;
        x86illegal();
        return 1;
    }
    if ((cr4 & CR4_TSD) && CPL) {
        x86gpf("RDTSC when TSD set and CPL != 0", 0);
        return 1;
    }
    EAX = tsc & 0xffffffff;
    EDX = tsc >> 32;
    CLOCK_CYCLES(1);
#ifdef USE_DYNAREC
    /*
       A due timer run here can reset the CPU (the 8042 pulse); the nonzero
       return ends the recompiled block, as the OUT ops do.
     */
    if (cpu_use_dynarec) {
        update_tsc();
        return x86_was_reset;
    }
#endif
    return 0;
}

static int
opRDPMC(UNUSED(uint32_t fetchdat))
{
    if (ECX > 1 || (!(cr4 & CR4_PCE) && (cr0 & 1) && CPL)) {
        x86gpf("RDPMC not allowed", 0);
        return 1;
    }
    /* The 40-bit counter: PerfCtr0/1 (MSR C1h/C2h) on the P6 family and
       the VIA Cyrix III (whose counter 0 is the TSC), CTR0/1 (MSR
       12h/13h) on the others */
    if (cpu_s->cpu_type == CPU_CYRIX3S) {
        uint64_t v = ECX ? msr.perfctr[1] : tsc;
        EAX = v & 0xffffffff;
        EDX = (v >> 32) & 0xff;
    } else if (is_p6) {
        EAX = msr.perfctr[ECX] & 0xffffffff;
        EDX = (msr.perfctr[ECX] >> 32) & 0xff;
    } else {
        EAX = msr.pmc[ECX] & 0xffffffff;
        EDX = (msr.pmc[ECX] >> 32) & 0xff;
    }
    CLOCK_CYCLES(1);
    return 0;
}
