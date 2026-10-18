static int
opCMPXCHG_b_a16(uint32_t fetchdat)
{
    uint8_t temp;
    uint8_t temp2 = AL;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3) {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr);
    }
    temp = geteab();
    if (cpu_state.abrt)
        return 1;
    /* The destination is written either way: the old value goes back on a
       mismatch (SDM 1999, CMPXCHG), so a write fault comes first */
    if (AL == temp) {
        seteab(getr8(cpu_reg));
    } else {
        seteab(temp);
        if (!cpu_state.abrt)
            AL = temp;
    }
    if (cpu_state.abrt)
        return 1;
    setsub8(temp2, temp);
    CLOCK_CYCLES((cpu_mod == 3) ? 6 : 10);
    return 0;
}
static int
opCMPXCHG_b_a32(uint32_t fetchdat)
{
    uint8_t temp;
    uint8_t temp2 = AL;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3) {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr);
    }
    temp = geteab();
    if (cpu_state.abrt)
        return 1;
    /* The destination is written either way: the old value goes back on a
       mismatch (SDM 1999, CMPXCHG), so a write fault comes first */
    if (AL == temp) {
        seteab(getr8(cpu_reg));
    } else {
        seteab(temp);
        if (!cpu_state.abrt)
            AL = temp;
    }
    if (cpu_state.abrt)
        return 1;
    setsub8(temp2, temp);
    CLOCK_CYCLES((cpu_mod == 3) ? 6 : 10);
    return 0;
}

static int
opCMPXCHG_w_a16(uint32_t fetchdat)
{
    uint16_t temp;
    uint16_t temp2 = AX;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3) {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1UL);
    }
    temp = geteaw();
    if (cpu_state.abrt)
        return 1;
    /* The destination is written either way: the old value goes back on a
       mismatch (SDM 1999, CMPXCHG), so a write fault comes first */
    if (AX == temp) {
        seteaw(cpu_state.regs[cpu_reg].w);
    } else {
        seteaw(temp);
        if (!cpu_state.abrt)
            AX = temp;
    }
    if (cpu_state.abrt)
        return 1;
    setsub16(temp2, temp);
    CLOCK_CYCLES((cpu_mod == 3) ? 6 : 10);
    return 0;
}
static int
opCMPXCHG_w_a32(uint32_t fetchdat)
{
    uint16_t temp;
    uint16_t temp2 = AX;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3) {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1UL);
    }
    temp = geteaw();
    if (cpu_state.abrt)
        return 1;
    /* The destination is written either way: the old value goes back on a
       mismatch (SDM 1999, CMPXCHG), so a write fault comes first */
    if (AX == temp) {
        seteaw(cpu_state.regs[cpu_reg].w);
    } else {
        seteaw(temp);
        if (!cpu_state.abrt)
            AX = temp;
    }
    if (cpu_state.abrt)
        return 1;
    setsub16(temp2, temp);
    CLOCK_CYCLES((cpu_mod == 3) ? 6 : 10);
    return 0;
}

static int
opCMPXCHG_l_a16(uint32_t fetchdat)
{
    uint32_t temp;
    uint32_t temp2 = EAX;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3) {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
    }
    temp = geteal();
    if (cpu_state.abrt)
        return 1;
    /* The destination is written either way: the old value goes back on a
       mismatch (SDM 1999, CMPXCHG), so a write fault comes first */
    if (EAX == temp) {
        seteal(cpu_state.regs[cpu_reg].l);
    } else {
        seteal(temp);
        if (!cpu_state.abrt)
            EAX = temp;
    }
    if (cpu_state.abrt)
        return 1;
    setsub32(temp2, temp);
    CLOCK_CYCLES((cpu_mod == 3) ? 6 : 10);
    return 0;
}
static int
opCMPXCHG_l_a32(uint32_t fetchdat)
{
    uint32_t temp;
    uint32_t temp2 = EAX;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3) {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
    }
    temp = geteal();
    if (cpu_state.abrt)
        return 1;
    /* The destination is written either way: the old value goes back on a
       mismatch (SDM 1999, CMPXCHG), so a write fault comes first */
    if (EAX == temp) {
        seteal(cpu_state.regs[cpu_reg].l);
    } else {
        seteal(temp);
        if (!cpu_state.abrt)
            EAX = temp;
    }
    if (cpu_state.abrt)
        return 1;
    setsub32(temp2, temp);
    CLOCK_CYCLES((cpu_mod == 3) ? 6 : 10);
    return 0;
}

#ifndef OPS_286_386
static int
opCMPXCHG8B_a16(uint32_t fetchdat)
{
    uint32_t temp;
    uint32_t temp_hi;
    uint32_t temp2    = EAX;
    uint32_t temp2_hi = EDX;

    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        /* The register form of CMPXCHG8B is illegal on real CPUs. The Microsoft
           Virtual PC 2007 BIOS abuses this encoding as a hypervisor call during
           its removable-media boot path; treat it as a no-op there so the guest
           does not get stuck in an endless #UD loop. Everywhere else, raise #UD
           (required e.g. by the Windows 7 DEC Tulip driver). */
        if (is_vpc) {
            cycles -= 6;
            return 0;
        }
        return ILLEGAL(fetchdat);
    }
    if ((fetchdat & 0x38) != 0x08)
        return ILLEGAL(fetchdat); /* Only 0F C7 /1 is CMPXCHG8B. */
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, (uint64_t) cpu_state.eaaddr + 7);
    temp    = geteal();
    if (cpu_state.abrt)
        return 0;
    temp_hi = readmeml(easeg, cpu_state.eaaddr + 4);
    if (cpu_state.abrt)
        return 0;
    /* Validate both pages before either write. Probing with a write of
       the old value would cause an extra write to a memory-mapped device. */
    if (cr0 & 0x80000000) {
        mmutranslatereal(easeg + cpu_state.eaaddr, 1);
        if (cpu_state.abrt)
            return 0;
        mmutranslatereal(easeg + cpu_state.eaaddr + 7, 1);
        if (cpu_state.abrt)
            return 0;
    }
    if (EAX == temp && EDX == temp_hi) {
        seteal(EBX);
        if (!cpu_state.abrt) {
            writememl(easeg, cpu_state.eaaddr + 4, ECX);
        }
    } else {
        /* CMPXCHG8B writes back the complete old value on a mismatch. */
        seteal(temp);
        if (!cpu_state.abrt) {
            writememl(easeg, cpu_state.eaaddr + 4, temp_hi);
        }
        if (!cpu_state.abrt) {
            EAX = temp;
            EDX = temp_hi;
        }
    }
    if (cpu_state.abrt)
        return 0;
    flags_rebuild();
    if (temp == temp2 && temp_hi == temp2_hi)
        cpu_state.flags |= Z_FLAG;
    else
        cpu_state.flags &= ~Z_FLAG;
    cycles -= (cpu_mod == 3) ? 6 : 10;
    return 0;
}
static int
opCMPXCHG8B_a32(uint32_t fetchdat)
{
    uint32_t temp;
    uint32_t temp_hi;
    uint32_t temp2    = EAX;
    uint32_t temp2_hi = EDX;

    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        /* See the a16 variant: allow the Virtual PC 2007 BIOS hypervisor-call
           encoding, otherwise raise #UD as a real CPU would. */
        if (is_vpc) {
            cycles -= 6;
            return 0;
        }
        return ILLEGAL(fetchdat);
    }
    if ((fetchdat & 0x38) != 0x08)
        return ILLEGAL(fetchdat); /* Only 0F C7 /1 is CMPXCHG8B. */
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, (uint64_t) cpu_state.eaaddr + 7);
    temp    = geteal();
    if (cpu_state.abrt)
        return 0;
    temp_hi = readmeml(easeg, cpu_state.eaaddr + 4);
    if (cpu_state.abrt)
        return 0;
    /* Validate both pages before either write. Probing with a write of
       the old value would cause an extra write to a memory-mapped device. */
    if (cr0 & 0x80000000) {
        mmutranslatereal(easeg + cpu_state.eaaddr, 1);
        if (cpu_state.abrt)
            return 0;
        mmutranslatereal(easeg + cpu_state.eaaddr + 7, 1);
        if (cpu_state.abrt)
            return 0;
    }
    if (EAX == temp && EDX == temp_hi) {
        seteal(EBX);
        if (!cpu_state.abrt) {
            writememl(easeg, cpu_state.eaaddr + 4, ECX);
        }
    } else {
        /* CMPXCHG8B writes back the complete old value on a mismatch. */
        seteal(temp);
        if (!cpu_state.abrt) {
            writememl(easeg, cpu_state.eaaddr + 4, temp_hi);
        }
        if (!cpu_state.abrt) {
            EAX = temp;
            EDX = temp_hi;
        }
    }
    if (cpu_state.abrt)
        return 0;
    flags_rebuild();
    if (temp == temp2 && temp_hi == temp2_hi)
        cpu_state.flags |= Z_FLAG;
    else
        cpu_state.flags &= ~Z_FLAG;
    cycles -= (cpu_mod == 3) ? 6 : 10;
    return 0;
}
#endif

/* Write the source after a successful destination write. With two distinct
   registers it receives the old destination; with the same register the sum
   must remain. A faulting memory destination must leave the source unchanged. */
/* dest = eab, src = r8 */
static int
opXADD_b_a16(uint32_t fetchdat)
{
    uint8_t src;
    uint8_t dest;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    src  = getr8(cpu_reg);
    dest = geteab();
    if (cpu_state.abrt)
        return 1;
    seteab(src + dest);
    if (cpu_state.abrt)
        return 1;
    setadd8(src, dest);
    /* With one register as both operands, leave the sum in place. */
    if ((cpu_mod != 3) || (cpu_reg != cpu_rm)) {
        setr8(cpu_reg, dest);
    }
    CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
    return 0;
}
static int
opXADD_b_a32(uint32_t fetchdat)
{
    uint8_t src;
    uint8_t dest;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    src  = getr8(cpu_reg);
    dest = geteab();
    if (cpu_state.abrt)
        return 1;
    seteab(src + dest);
    if (cpu_state.abrt)
        return 1;
    setadd8(src, dest);
    /* With one register as both operands, leave the sum in place. */
    if ((cpu_mod != 3) || (cpu_reg != cpu_rm)) {
        setr8(cpu_reg, dest);
    }
    CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
    return 0;
}

static int
opXADD_w_a16(uint32_t fetchdat)
{
    uint16_t src;
    uint16_t dest;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    src  = cpu_state.regs[cpu_reg].w;
    dest = geteaw();
    if (cpu_state.abrt)
        return 1;
    seteaw(src + dest);
    if (cpu_state.abrt)
        return 1;
    setadd16(src, dest);
    /* With one register as both operands, leave the sum in place. */
    if ((cpu_mod != 3) || (cpu_reg != cpu_rm))
        cpu_state.regs[cpu_reg].w = dest;
    CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
    return 0;
}
static int
opXADD_w_a32(uint32_t fetchdat)
{
    uint16_t src;
    uint16_t dest;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    src  = cpu_state.regs[cpu_reg].w;
    dest = geteaw();
    if (cpu_state.abrt)
        return 1;
    seteaw(src + dest);
    if (cpu_state.abrt)
        return 1;
    setadd16(src, dest);
    /* With one register as both operands, leave the sum in place. */
    if ((cpu_mod != 3) || (cpu_reg != cpu_rm))
        cpu_state.regs[cpu_reg].w = dest;
    CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
    return 0;
}

static int
opXADD_l_a16(uint32_t fetchdat)
{
    uint32_t src;
    uint32_t dest;

    fetch_ea_16(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    src  = cpu_state.regs[cpu_reg].l;
    dest = geteal();
    if (cpu_state.abrt)
        return 1;
    seteal(src + dest);
    if (cpu_state.abrt)
        return 1;
    setadd32(src, dest);
    /* With one register as both operands, leave the sum in place. */
    if ((cpu_mod != 3) || (cpu_reg != cpu_rm))
        cpu_state.regs[cpu_reg].l = dest;
    CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
    return 0;
}
static int
opXADD_l_a32(uint32_t fetchdat)
{
    uint32_t src;
    uint32_t dest;

    fetch_ea_32(fetchdat);
    if (cpu_mod != 3)
        SEG_CHECK_WRITE(cpu_state.ea_seg);
    src  = cpu_state.regs[cpu_reg].l;
    dest = geteal();
    if (cpu_state.abrt)
        return 1;
    seteal(src + dest);
    if (cpu_state.abrt)
        return 1;
    setadd32(src, dest);
    /* With one register as both operands, leave the sum in place. */
    if ((cpu_mod != 3) || (cpu_reg != cpu_rm))
        cpu_state.regs[cpu_reg].l = dest;
    CLOCK_CYCLES((cpu_mod == 3) ? 3 : 4);
    return 0;
}
