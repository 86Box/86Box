/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opXSTORE_common_a16(void)
{
    int      count;
    uint32_t bytes  = 0;
    uint32_t bytes2 = 0;

    switch (DX & 0x3) {
        case 0:
        default:
            bytes  = (random_generate() | (random_generate() << 8) | (random_generate() << 16) | (random_generate() << 24));
            bytes2 = (random_generate() | (random_generate() << 8) | (random_generate() << 16) | (random_generate() << 24));
            count  = 8;
            CLOCK_CYCLES(55);
            break;
        case 1:
            bytes = (random_generate() | (random_generate() << 8) | (random_generate() << 16) | (random_generate() << 24));
            count = 4;
            CLOCK_CYCLES(310);
            break;
        case 2:
            bytes |= (uint32_t) (random_generate() | (random_generate() << 8));
            count  = 2;
            CLOCK_CYCLES(200);
            break;
        case 3:
            bytes |= (uint32_t) random_generate();
            count  = 1;
            CLOCK_CYCLES(120);
            break;
    }

    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, DI, DI + 3UL);
    writememl(es, DI, bytes);
    if (cpu_state.abrt)
        return -1;
    if (bytes2) {
        CHECK_WRITE(&cpu_state.seg_es, DI + 4UL, DI + 7UL);
        writememl(es, DI + 4, bytes2);
        if (cpu_state.abrt)
            return -1;
    }

    DI  += count;
    EAX = ((msr.padlock_rng & 0xfffffff0) | (count & 0xf));
    return count;
}

static int
opXSTORE_common_a32(void)
{
    int      count;
    uint32_t bytes  = 0;
    uint32_t bytes2 = 0;

    switch (EDX & 0x3) {
        case 0:
        default:
            bytes  = (random_generate() | (random_generate() << 8) | (random_generate() << 16) | (random_generate() << 24));
            bytes2 = (random_generate() | (random_generate() << 8) | (random_generate() << 16) | (random_generate() << 24));
            count  = 8;
            CLOCK_CYCLES(55);
            break;
        case 1:
            bytes = (random_generate() | (random_generate() << 8) | (random_generate() << 16) | (random_generate() << 24));
            count = 4;
            CLOCK_CYCLES(310);
            break;
        case 2:
            bytes  = (uint32_t) (random_generate() | (random_generate() << 8));
            count  = 2;
            CLOCK_CYCLES(200);
            break;
        case 3:
            bytes  = (uint32_t) random_generate();
            count  = 1;
            CLOCK_CYCLES(120);
            break;
    }

    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, EDI, EDI + 3UL);
    writememl(es, EDI, bytes);
    if (cpu_state.abrt)
        return -1;
    if (bytes2) {
        CHECK_WRITE(&cpu_state.seg_es, EDI + 4UL, EDI + 7UL);
        writememl(es, EDI + 4, bytes2);
        if (cpu_state.abrt)
            return -1;
    }

    EDI += count;
    EAX = ((msr.padlock_rng & 0xfffffff0) | (count & 0xf));
    return count;
}

static int
opPADLOCK_a16(uint32_t fetchdat)
{
    int temp;

    if (((fetchdat & 0xff) == 0xc0) && (msr.padlock_rng & 0x40)) { /* XSTORE */
        cpu_state.pc++;
        temp = opXSTORE_common_a16();
        if (temp == -1)
            return 1;
    } else
        x86illegal();

    return 0;
}

static int
opPADLOCK_a32(uint32_t fetchdat)
{
    int temp;

    if (((fetchdat & 0xff) == 0xc0) && (msr.padlock_rng & 0x40)) { /* XSTORE */
        cpu_state.pc++;
        temp = opXSTORE_common_a32();
        if (temp == -1)
            return 1;
    } else
        x86illegal();

    return 0;
}

static int
opREP_PADLOCK_a16(uint32_t fetchdat)
{
    int count;
    int temp;

    if (((fetchdat & 0xff) == 0xc0) && (msr.padlock_rng & 0x40)) { /* XSTORE */
        cpu_state.pc++;
        count = CX;
        while (count > 0) {
            temp = opXSTORE_common_a16();
            if (temp == -1)
                return 1;
            count -= temp;
            CX = (count < 0 ? 0 : count);
        }
        CLOCK_CYCLES(7);
    } else
        x86illegal();

    return 0;
}

static int
opREP_PADLOCK_a32(uint32_t fetchdat)
{
    int count;
    int temp;

    if (((fetchdat & 0xff) == 0xc0) && (msr.padlock_rng & 0x40)) { /* XSTORE */
        cpu_state.pc++;
        count = ECX;
        while (count > 0) {
            temp = opXSTORE_common_a32();
            if (temp == -1)
                return 1;
            count -= temp;
            ECX = (count < 0 ? 0 : count);
        }
        CLOCK_CYCLES(7);
    } else
        x86illegal();

    return 0;
}
