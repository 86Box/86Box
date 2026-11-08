static int
opCMC(UNUSED(uint32_t fetchdat))
{
    flags_rebuild();
    cpu_state.flags ^= C_FLAG;
    CLOCK_CYCLES(2);
    PREFETCH_RUN(2, 1, -1, 0, 0, 0, 0, 0);
    return 0;
}

static int
opCLC(UNUSED(uint32_t fetchdat))
{
    flags_rebuild();
    cpu_state.flags &= ~C_FLAG;
    CLOCK_CYCLES(2);
    PREFETCH_RUN(2, 1, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opCLD(UNUSED(uint32_t fetchdat))
{
    cpu_state.flags &= ~D_FLAG;
    CLOCK_CYCLES(2);
    PREFETCH_RUN(2, 1, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opCLI(UNUSED(uint32_t fetchdat))
{
    if (!IOPLp) {
        if ((!(cpu_state.eflags & VM_FLAG) && (cr4 & CR4_PVI)) || ((cpu_state.eflags & VM_FLAG) && (cr4 & CR4_VME))) {
            cpu_state.eflags &= ~VIF_FLAG;
        } else {
            x86gpf(NULL, 0);
            return 1;
        }
    } else
        cpu_state.flags &= ~I_FLAG;

    CLOCK_CYCLES(3);
    PREFETCH_RUN(3, 1, -1, 0, 0, 0, 0, 0);
    return 0;
}

static int
opSTC(UNUSED(uint32_t fetchdat))
{
    flags_rebuild();
    cpu_state.flags |= C_FLAG;
    CLOCK_CYCLES(2);
    PREFETCH_RUN(2, 1, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opSTD(UNUSED(uint32_t fetchdat))
{
    cpu_state.flags |= D_FLAG;
    CLOCK_CYCLES(2);
    PREFETCH_RUN(2, 1, -1, 0, 0, 0, 0, 0);
    return 0;
}
static int
opSTI(UNUSED(uint32_t fetchdat))
{
    if (!IOPLp) {
        if ((!(cpu_state.eflags & VM_FLAG) && (cr4 & CR4_PVI)) || ((cpu_state.eflags & VM_FLAG) && (cr4 & CR4_VME))) {
            if (cpu_state.eflags & VIP_FLAG) {
                x86gpf(NULL, 0);
                return 1;
            } else
                cpu_state.eflags |= VIF_FLAG;
        } else {
            x86gpf(NULL, 0);
            return 1;
        }
    } else
        cpu_state.flags |= I_FLAG;

    /*First instruction after STI will always execute, regardless of whether
      there is a pending interrupt*/
    cpu_end_block_after_ins = 2;

    CLOCK_CYCLES(2);
    PREFETCH_RUN(2, 1, -1, 0, 0, 0, 0, 0);
    return 0;
}

static int
opSAHF(UNUSED(uint32_t fetchdat))
{
    flags_rebuild();
    cpu_state.flags = (cpu_state.flags & 0xff00) | (AH & 0xd5) | 2;
    CLOCK_CYCLES(3);
    PREFETCH_RUN(3, 1, -1, 0, 0, 0, 0, 0);

#if (defined(USE_DYNAREC))
    codegen_flags_changed = 0;
#endif

    return 0;
}
static int
opLAHF(UNUSED(uint32_t fetchdat))
{
    flags_rebuild();
    AH = cpu_state.flags & 0xff;
    CLOCK_CYCLES(3);
    PREFETCH_RUN(3, 1, -1, 0, 0, 0, 0, 0);
    return 0;
}

static int
opPUSHF(UNUSED(uint32_t fetchdat))
{
    if ((cpu_state.eflags & VM_FLAG) && (IOPL < 3)) {
        if (cr4 & CR4_VME) {
            uint16_t temp;

            flags_rebuild();
            temp = (cpu_state.flags & ~I_FLAG) | 0x3000;
            if (cpu_state.eflags & VIF_FLAG)
                temp |= I_FLAG;
            temp = (temp & 0x7fd5) | 2;
            PUSH_W(temp);
        } else {
            x86gpf(NULL, 0);
            return 1;
        }
    } else {
        flags_rebuild();
        cpu_state.flags = (cpu_state.flags & 0x7fd5) | 2;
        PUSH_W(cpu_state.flags);
    }
    CLOCK_CYCLES(4);
    PREFETCH_RUN(4, 1, -1, 0, 0, 1, 0, 0);
    return cpu_state.abrt;
}
static int
opPUSHFD(UNUSED(uint32_t fetchdat))
{
    uint16_t tempw;
    if ((cpu_state.eflags & VM_FLAG) && (IOPL < 3)) {
        x86gpf(NULL, 0);
        return 1;
    }
    if (cpu_CR4_mask & CR4_VME)
        tempw = cpu_state.eflags & 0x3c;
    else if (CPUID)
        tempw = cpu_state.eflags & 0x24;
    else
        tempw = cpu_state.eflags & 4;
    flags_rebuild();
    cpu_state.flags = (cpu_state.flags & 0x7fd5) | 2;
    PUSH_L(cpu_state.flags | (tempw << 16));
    CLOCK_CYCLES(4);
    PREFETCH_RUN(4, 1, -1, 0, 0, 0, 1, 0);
    return cpu_state.abrt;
}

static int
opPOPF_186(UNUSED(uint32_t fetchdat))
{
    uint16_t popf_old = cpu_state.flags;
    uint16_t tempw;

    tempw = POP_W();
    if (cpu_state.abrt)
        return 1;

    cpu_state.flags = (cpu_state.flags & 0x7000) | (tempw & 0x0fd5) | 2;
    flags_extract();
    rf_flag_no_clear = 1;

    CLOCK_CYCLES(5);
    PREFETCH_RUN(5, 1, -1, 1, 0, 0, 0, 0);

#if (defined(USE_DYNAREC))
    codegen_flags_changed = 0;
#endif

    /* TF turned on: the trap comes after the instruction that follows
       (Pentium Vol. 3, 17.3.1.4). A compiled block ends here (a nonzero
       answer leaves it, the PC past POPF), and the loop, which steps
       single instructions while TF is set, runs that one; the interpreter
       ignores the answer. */
    if ((cpu_state.flags & ~popf_old) & T_FLAG)
        return 1;
    return 0;
}
static int
opPOPF_286(UNUSED(uint32_t fetchdat))
{
    uint16_t popf_old = cpu_state.flags;
    uint16_t tempw;

    if ((cpu_state.eflags & VM_FLAG) && (IOPL < 3)) {
        x86gpf(NULL, 0);
        return 1;
    }

    tempw = POP_W();
    if (cpu_state.abrt)
        return 1;

    if (!(msw & 1))
        cpu_state.flags = (cpu_state.flags & 0x7000) | (tempw & 0x0fd5) | 2;
    else if (!(CPL))
        cpu_state.flags = (tempw & 0x7fd5) | 2;
    else if (IOPLp)
        cpu_state.flags = (cpu_state.flags & 0x3000) | (tempw & 0x4fd5) | 2;
    else
        cpu_state.flags = (cpu_state.flags & 0x3200) | (tempw & 0x4dd5) | 2;
    flags_extract();
    rf_flag_no_clear = 1;

    CLOCK_CYCLES(5);
    PREFETCH_RUN(5, 1, -1, 1, 0, 0, 0, 0);

#if (defined(USE_DYNAREC))
    codegen_flags_changed = 0;
#endif

    /* TF turned on: the trap comes after the instruction that follows
       (Pentium Vol. 3, 17.3.1.4). A compiled block ends here (a nonzero
       answer leaves it, the PC past POPF), and the loop, which steps
       single instructions while TF is set, runs that one; the interpreter
       ignores the answer. */
    if ((cpu_state.flags & ~popf_old) & T_FLAG)
        return 1;
    return 0;
}
static int
opPOPF(UNUSED(uint32_t fetchdat))
{
    uint16_t popf_old = cpu_state.flags;
    uint16_t tempw;

    if ((cpu_state.eflags & VM_FLAG) && (IOPL < 3)) {
        if (cr4 & CR4_VME) {
            uint32_t old_esp = ESP;

            tempw = POP_W();
            if (cpu_state.abrt) {

                ESP = old_esp;
                return 1;
            }

            if ((tempw & T_FLAG) || ((tempw & I_FLAG) && (cpu_state.eflags & VIP_FLAG))) {
                ESP = old_esp;
                x86gpf(NULL, 0);
                return 1;
            }
            if (tempw & I_FLAG)
                cpu_state.eflags |= VIF_FLAG;
            else
                cpu_state.eflags &= ~VIF_FLAG;
            cpu_state.flags = (cpu_state.flags & 0x3200) | (tempw & 0x4dd5) | 2;
        } else {
            x86gpf(NULL, 0);
            return 1;
        }
    } else {
        tempw = POP_W();
        if (cpu_state.abrt)
            return 1;

        if (!(CPL) || !(msw & 1))
            cpu_state.flags = (tempw & 0x7fd5) | 2;
        else if (IOPLp)
            cpu_state.flags = (cpu_state.flags & 0x3000) | (tempw & 0x4fd5) | 2;
        else
            cpu_state.flags = (cpu_state.flags & 0x3200) | (tempw & 0x4dd5) | 2;
    }
    flags_extract();
    rf_flag_no_clear = 1;

    CLOCK_CYCLES(5);
    PREFETCH_RUN(5, 1, -1, 1, 0, 0, 0, 0);

#if (defined(USE_DYNAREC))
    codegen_flags_changed = 0;
#endif

    /* TF turned on: the trap comes after the instruction that follows
       (Pentium Vol. 3, 17.3.1.4). A compiled block ends here (a nonzero
       answer leaves it, the PC past POPF), and the loop, which steps
       single instructions while TF is set, runs that one; the interpreter
       ignores the answer. */
    if ((cpu_state.flags & ~popf_old) & T_FLAG)
        return 1;
    return 0;
}
static int
opPOPFD(UNUSED(uint32_t fetchdat))
{
    uint16_t popf_old = cpu_state.flags;
    uint32_t templ;

    if ((cpu_state.eflags & VM_FLAG) && (IOPL < 3)) {
        x86gpf(NULL, 0);
        return 1;
    }

    templ = POP_L();
    if (cpu_state.abrt)
        return 1;

    if (!(CPL) || !(msw & 1))
        cpu_state.flags = (templ & 0x7fd5) | 2;
    else if (IOPLp)
        cpu_state.flags = (cpu_state.flags & 0x3000) | (templ & 0x4fd5) | 2;
    else
        cpu_state.flags = (cpu_state.flags & 0x3200) | (templ & 0x4dd5) | 2;

    /* AC and ID where the CPU has them; VM, RF, VIF and VIP keep their values
       (Pentium Developer's Manual vol. 3, POPF/POPFD) */
    cpu_state.eflags = (cpu_state.eflags & (VM_FLAG | RF_FLAG | VIF_FLAG | VIP_FLAG)) |
                       ((templ >> 16) & (AC_FLAG | VID_FLAG) & cpu_eflags_implemented());

    flags_extract();
    rf_flag_no_clear = 1;

    CLOCK_CYCLES(5);
    PREFETCH_RUN(5, 1, -1, 0, 1, 0, 0, 0);

#if (defined(USE_DYNAREC))
    codegen_flags_changed = 0;
#endif

    /* TF turned on: the trap comes after the instruction that follows
       (Pentium Vol. 3, 17.3.1.4). A compiled block ends here (a nonzero
       answer leaves it, the PC past POPF), and the loop, which steps
       single instructions while TF is set, runs that one; the interpreter
       ignores the answer. */
    if ((cpu_state.flags & ~popf_old) & T_FLAG)
        return 1;
    return 0;
}
