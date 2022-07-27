static int opAAA(uint32_t fetchdat)
{
        flags_rebuild();
        if ((cpu_state.flags & A_FLAG) || ((AL & 0xF) > 9))
        {
		/* On 286, it's indeed AX - behavior difference from 808x. */
                AX += 6;
                AH++;
                cpu_state.flags |= (A_FLAG | C_FLAG);
        }
        else
                cpu_state.flags &= ~(A_FLAG | C_FLAG);
        AL &= 0xF;
        CLOCK_CYCLES(is486 ? 3 : 4);
        PREFETCH_RUN(is486 ? 3 : 4, 1, -1, 0,0,0,0, 0);
        return 0;
}

static int opAAD(uint32_t fetchdat)
{
        int base = getbytef();
        if (!cpu_isintel) base = 10;
        AL = (AH * base) + AL;
        AH = 0;
        setznp16(AX);
        CLOCK_CYCLES((is486) ? 14 : 19);
        PREFETCH_RUN(is486 ? 14 : 19, 2, -1, 0,0,0,0, 0);
        return 0;
}

static int opAAM(uint32_t fetchdat)
{
        int base = getbytef();
        if (!base || !cpu_isintel) base = 10;
        AH = AL / base;
        AL %= base;
        setznp16(AX);
        CLOCK_CYCLES((is486) ? 15 : 17);
        PREFETCH_RUN(is486 ? 15 : 17, 2, -1, 0,0,0,0, 0);
        return 0;
}

static int opAAS(uint32_t fetchdat)
{
        flags_rebuild();
        if ((cpu_state.flags & A_FLAG) || ((AL & 0xF) > 9))
        {
		/* On 286, it's indeed AX - behavior difference from 808x. */
                AX -= 6;
                AH--;
                cpu_state.flags |= (A_FLAG | C_FLAG);
        }
        else
                cpu_state.flags &= ~(A_FLAG | C_FLAG);
        AL &= 0xF;
        CLOCK_CYCLES(is486 ? 3 : 4);
        PREFETCH_RUN(is486 ? 3 : 4, 1, -1, 0,0,0,0, 0);
        return 0;
}

static int opDAA(uint32_t fetchdat)
{
        uint16_t tempw, old_AL, old_CF;

        flags_rebuild();
	old_AL = AL;
	old_CF = cpu_state.flags & C_FLAG;
	cpu_state.flags &= ~C_FLAG;

	if (((AL & 0xf) > 9) || (cpu_state.flags & A_FLAG)) {
                int tempi = ((uint16_t)AL) + 6;
                AL += 6;
                if (old_CF || (tempi & 0x100))
			cpu_state.flags |= C_FLAG;
                cpu_state.flags |= A_FLAG;
        } else
		cpu_state.flags &= ~A_FLAG;

        if ((old_AL > 0x99) || old_CF)
        {
                AL += 0x60;
                cpu_state.flags |= C_FLAG;
        } else
		cpu_state.flags &= ~C_FLAG;

        tempw = cpu_state.flags & (C_FLAG | A_FLAG);
        setznp8(AL);
        flags_rebuild();
        cpu_state.flags = (cpu_state.flags & ~(C_FLAG | A_FLAG)) | tempw;
        CLOCK_CYCLES(4);
        PREFETCH_RUN(4, 1, -1, 0,0,0,0, 0);

        return 0;
}

static int opDAS(uint32_t fetchdat)
{
        uint16_t tempw, old_AL, old_CF;

        flags_rebuild();
	old_AL = AL;
	old_CF = cpu_state.flags & C_FLAG;
	cpu_state.flags &= ~C_FLAG;

        if (((AL & 0xf) > 9) || (cpu_state.flags & A_FLAG))
        {
                int tempi = ((uint16_t)AL) - 6;
                AL -= 6;
                if (old_CF || (tempi & 0x100))
			cpu_state.flags |= C_FLAG;
                cpu_state.flags |= A_FLAG;
        } else
                cpu_state.flags &= ~A_FLAG;

        if ((old_AL > 0x99) || old_CF)
        {
                AL -= 0x60;
                cpu_state.flags |= C_FLAG;
        }

        tempw = cpu_state.flags & (C_FLAG | A_FLAG);
        setznp8(AL);
        flags_rebuild();
        cpu_state.flags = (cpu_state.flags & ~(C_FLAG | A_FLAG)) | tempw;
        CLOCK_CYCLES(4);
        PREFETCH_RUN(4, 1, -1, 0,0,0,0, 0);

        return 0;
}
