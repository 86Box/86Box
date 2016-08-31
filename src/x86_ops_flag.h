static int opCMC(uint32_t fetchdat)
{
        flags_rebuild();
        flags ^= C_FLAG;
        CLOCK_CYCLES(2);
        return 0;
}


static int opCLC(uint32_t fetchdat)
{
        flags_rebuild();
        flags &= ~C_FLAG;
        CLOCK_CYCLES(2);
        return 0;
}
static int opCLD(uint32_t fetchdat)
{
        flags &= ~D_FLAG;
        CLOCK_CYCLES(2);
        return 0;
}
static int opCLI(uint32_t fetchdat)
{
        if (!IOPLp)
        {
                x86gpf(NULL,0);
                return 1;
        }
        else
                flags &= ~I_FLAG;
         
        CPU_BLOCK_END();
                       
        CLOCK_CYCLES(3);
        return 0;
}

static int opSTC(uint32_t fetchdat)
{
        flags_rebuild();
        flags |= C_FLAG;
        CLOCK_CYCLES(2);
        return 0;
}
static int opSTD(uint32_t fetchdat)
{
        flags |= D_FLAG;
        CLOCK_CYCLES(2);
        return 0;
}
static int opSTI(uint32_t fetchdat)
{
        if (!IOPLp)
        {
                x86gpf(NULL,0);
                return 1;
        }
        else
                flags |= I_FLAG;

        CPU_BLOCK_END();
                                
        CLOCK_CYCLES(2);
        return 0;
}

static int opSAHF(uint32_t fetchdat)
{
        flags_rebuild();
        flags = (flags & 0xff00) | (AH & 0xd5) | 2;
        CLOCK_CYCLES(3);

        codegen_flags_changed = 0;

        return 0;
}
static int opLAHF(uint32_t fetchdat)
{
        flags_rebuild();
        AH = flags & 0xff;
        CLOCK_CYCLES(3);
        return 0;
}

static int opPUSHF(uint32_t fetchdat)
{
        if ((eflags & VM_FLAG) && (IOPL < 3))
        {
                x86gpf(NULL,0);
                return 1;
        }
        flags_rebuild();
        PUSH_W(flags);
        CLOCK_CYCLES(4);
        return cpu_state.abrt;
}
static int opPUSHFD(uint32_t fetchdat)
{
        uint16_t tempw;
        if ((eflags & VM_FLAG) && (IOPL < 3))
        {
                x86gpf(NULL, 0);
                return 1;
        }
        if (CPUID) tempw = eflags & 0x24;
        else       tempw = eflags & 4;
        flags_rebuild();
        PUSH_L(flags | (tempw << 16));
        CLOCK_CYCLES(4);
        return cpu_state.abrt;
}

static int opPOPF_286(uint32_t fetchdat)
{
        uint16_t tempw;
        
        if ((eflags & VM_FLAG) && (IOPL < 3))
        {
                x86gpf(NULL, 0);
                return 1;
        }
        
        tempw = POP_W();                if (cpu_state.abrt) return 1;

        if (!(msw & 1))           flags = (flags & 0x7000) | (tempw & 0x0fd5) | 2;
        else if (!(CPL))          flags = (tempw & 0x7fd5) | 2;
        else if (IOPLp)           flags = (flags & 0x3000) | (tempw & 0x4fd5) | 2;
        else                      flags = (flags & 0x3200) | (tempw & 0x4dd5) | 2;
        flags_extract();

        CLOCK_CYCLES(5);

        codegen_flags_changed = 0;

        return 0;
}
static int opPOPF(uint32_t fetchdat)
{
        uint16_t tempw;
        
        if ((eflags & VM_FLAG) && (IOPL < 3))
        {
                x86gpf(NULL, 0);
                return 1;
        }
        
        tempw = POP_W();                if (cpu_state.abrt) return 1;

        if (!(CPL) || !(msw & 1)) flags = (tempw & 0x7fd5) | 2;
        else if (IOPLp)           flags = (flags & 0x3000) | (tempw & 0x4fd5) | 2;
        else                      flags = (flags & 0x3200) | (tempw & 0x4dd5) | 2;
        flags_extract();

        CLOCK_CYCLES(5);

        codegen_flags_changed = 0;

        return 0;
}
static int opPOPFD(uint32_t fetchdat)
{
        uint32_t templ;
        
        if ((eflags & VM_FLAG) && (IOPL < 3))
        {
                x86gpf(NULL, 0);
                return 1;
        }
        
        templ = POP_L();                if (cpu_state.abrt) return 1;

        if (!(CPL) || !(msw & 1)) flags = (templ & 0x7fd5) | 2;
        else if (IOPLp)           flags = (flags & 0x3000) | (templ & 0x4fd5) | 2;
        else                      flags = (flags & 0x3200) | (templ & 0x4dd5) | 2;
        
        templ &= is486 ? 0x240000 : 0;
        templ |= ((eflags&3) << 16);
        if (CPUID)      eflags = (templ >> 16) & 0x27;
        else if (is486) eflags = (templ >> 16) & 7;
        else            eflags = (templ >> 16) & 3;
        
        flags_extract();

        CLOCK_CYCLES(5);

        codegen_flags_changed = 0;

        return 0;
}
