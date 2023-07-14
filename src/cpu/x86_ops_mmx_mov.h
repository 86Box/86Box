static int
opMOVD_l_mm_a16(uint32_t fetchdat)
{
    uint32_t dst;
    MMX_REG op;
    MMX_ENTER();

    fetch_ea_16(fetchdat);
    if (fpu_softfloat) {
        if (cpu_mod == 3) {
            fpu_state.tag = 0;
            fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
            op.l[0] = cpu_state.regs[cpu_rm].l;
            op.l[1] = 0;
            fpu_state.st_space[cpu_reg].fraction = op.q;
            fpu_state.st_space[cpu_reg].exp = 0xffff;
            CLOCK_CYCLES(1);
        } else {
            SEG_CHECK_READ(cpu_state.ea_seg);
            dst = readmeml(easeg, cpu_state.eaaddr);
            if (cpu_state.abrt)
                return 1;
            fpu_state.tag = 0;
            fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
            op.l[0] = dst;
            op.l[1] = 0;
            fpu_state.st_space[cpu_reg].fraction = op.q;
            fpu_state.st_space[cpu_reg].exp = 0xffff;
            CLOCK_CYCLES(2);
        }
    } else {
        if (cpu_mod == 3) {
            cpu_state.MM[cpu_reg].l[0] = cpu_state.regs[cpu_rm].l;
            cpu_state.MM[cpu_reg].l[1] = 0;
            CLOCK_CYCLES(1);
        } else {
            SEG_CHECK_READ(cpu_state.ea_seg);
            dst = readmeml(easeg, cpu_state.eaaddr);
            if (cpu_state.abrt)
                return 1;
            cpu_state.MM[cpu_reg].l[0] = dst;
            cpu_state.MM[cpu_reg].l[1] = 0;
            CLOCK_CYCLES(2);
        }
    }
    return 0;
}
static int
opMOVD_l_mm_a32(uint32_t fetchdat)
{
    uint32_t dst;
    MMX_REG op;
    MMX_ENTER();

    fetch_ea_32(fetchdat);
    if (fpu_softfloat) {
        if (cpu_mod == 3) {
            fpu_state.tag = 0;
            fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
            op.l[0] = cpu_state.regs[cpu_rm].l;
            op.l[1] = 0;
            fpu_state.st_space[cpu_reg].fraction = op.q;
            fpu_state.st_space[cpu_reg].exp = 0xffff;
            CLOCK_CYCLES(1);
        } else {
            SEG_CHECK_READ(cpu_state.ea_seg);
            dst = readmeml(easeg, cpu_state.eaaddr);
            if (cpu_state.abrt)
                return 1;
            fpu_state.tag = 0;
            fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
            op.l[0] = dst;
            op.l[1] = 0;
            fpu_state.st_space[cpu_reg].fraction = op.q;
            fpu_state.st_space[cpu_reg].exp = 0xffff;
            CLOCK_CYCLES(2);
        }
    } else {
        if (cpu_mod == 3) {
            cpu_state.MM[cpu_reg].l[0] = cpu_state.regs[cpu_rm].l;
            cpu_state.MM[cpu_reg].l[1] = 0;
            CLOCK_CYCLES(1);
        } else {
            SEG_CHECK_READ(cpu_state.ea_seg);
            dst = readmeml(easeg, cpu_state.eaaddr);
            if (cpu_state.abrt)
                return 1;
            cpu_state.MM[cpu_reg].l[0] = dst;
            cpu_state.MM[cpu_reg].l[1] = 0;
            CLOCK_CYCLES(2);
        }
    }
    return 0;
}

static int
opMOVD_mm_l_a16(uint32_t fetchdat)
{
    MMX_REG op;
    MMX_ENTER();

    fetch_ea_16(fetchdat);
    if (fpu_softfloat) {
        if (cpu_mod == 3) {
            fpu_state.tag = 0;
            fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
            op = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;
            cpu_state.regs[cpu_rm].l = op.l[0];
            CLOCK_CYCLES(1);
        } else {
            SEG_CHECK_WRITE(cpu_state.ea_seg);
            CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3);
            op = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;
            writememl(easeg, cpu_state.eaaddr, op.l[0]);
            if (cpu_state.abrt)
                return 1;
            fpu_state.tag = 0;
            fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
            CLOCK_CYCLES(2);
        }
    } else {
        if (cpu_mod == 3) {
            cpu_state.regs[cpu_rm].l = cpu_state.MM[cpu_reg].l[0];
            CLOCK_CYCLES(1);
        } else {
            SEG_CHECK_WRITE(cpu_state.ea_seg);
            CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3);
            writememl(easeg, cpu_state.eaaddr, cpu_state.MM[cpu_reg].l[0]);
            if (cpu_state.abrt)
                return 1;
            CLOCK_CYCLES(2);
        }
    }
    return 0;
}
static int
opMOVD_mm_l_a32(uint32_t fetchdat)
{
    MMX_REG op;
    MMX_ENTER();

    fetch_ea_32(fetchdat);
    if (fpu_softfloat) {
        if (cpu_mod == 3) {
            fpu_state.tag = 0;
            fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
            op = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;
            cpu_state.regs[cpu_rm].l = op.l[0];
            CLOCK_CYCLES(1);
        } else {
            SEG_CHECK_WRITE(cpu_state.ea_seg);
            CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3);
            op = *(MMX_REG *)&fpu_state.st_space[cpu_reg].fraction;
            writememl(easeg, cpu_state.eaaddr, op.l[0]);
            if (cpu_state.abrt)
                return 1;
            fpu_state.tag = 0;
            fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
            CLOCK_CYCLES(2);
        }
    } else {
        if (cpu_mod == 3) {
            cpu_state.regs[cpu_rm].l = cpu_state.MM[cpu_reg].l[0];
            CLOCK_CYCLES(1);
        } else {
            SEG_CHECK_WRITE(cpu_state.ea_seg);
            CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3);
            writememl(easeg, cpu_state.eaaddr, cpu_state.MM[cpu_reg].l[0]);
            if (cpu_state.abrt)
                return 1;
            CLOCK_CYCLES(2);
        }
    }
    return 0;
}

#if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
/*Cyrix maps both MOVD and SMINT to the same opcode*/
static int
opMOVD_mm_l_a16_cx(uint32_t fetchdat)
{
    if (in_smm)
        return opSMINT(fetchdat);

    MMX_ENTER();

    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.regs[cpu_rm].l = cpu_state.MM[cpu_reg].l[0];
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3);
        writememl(easeg, cpu_state.eaaddr, cpu_state.MM[cpu_reg].l[0]);
        if (cpu_state.abrt)
            return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}
static int
opMOVD_mm_l_a32_cx(uint32_t fetchdat)
{
    if (in_smm)
        return opSMINT(fetchdat);

    MMX_ENTER();

    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.regs[cpu_rm].l = cpu_state.MM[cpu_reg].l[0];
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3);
        writememl(easeg, cpu_state.eaaddr, cpu_state.MM[cpu_reg].l[0]);
        if (cpu_state.abrt)
            return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}
#endif

static int
opMOVQ_q_mm_a16(uint32_t fetchdat)
{
    uint64_t dst;
    MMX_REG src, op;
    MMX_ENTER();

    fetch_ea_16(fetchdat);
    if (fpu_softfloat) {
        if (cpu_mod == 3) {
            fpu_state.tag = 0;
            fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
            src = *(MMX_REG *)&fpu_state.st_space[cpu_rm].fraction;
            op.q = src.q;
            fpu_state.st_space[cpu_reg].fraction = op.q;
            fpu_state.st_space[cpu_reg].exp = 0xffff;
            CLOCK_CYCLES(1);
        } else {
            SEG_CHECK_READ(cpu_state.ea_seg);
            dst = readmemq(easeg, cpu_state.eaaddr);
            if (cpu_state.abrt)
                return 1;
            fpu_state.tag = 0;
            fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
            op.q = dst;
            fpu_state.st_space[cpu_reg].fraction = op.q;
            fpu_state.st_space[cpu_reg].exp = 0xffff;
            CLOCK_CYCLES(2);
        }
    } else {
        if (cpu_mod == 3) {
            cpu_state.MM[cpu_reg].q = cpu_state.MM[cpu_rm].q;
            CLOCK_CYCLES(1);
        } else {
            SEG_CHECK_READ(cpu_state.ea_seg);
            dst = readmemq(easeg, cpu_state.eaaddr);
            if (cpu_state.abrt)
                return 1;
            cpu_state.MM[cpu_reg].q = dst;
            CLOCK_CYCLES(2);
        }
    }
    return 0;
}
static int
opMOVQ_q_mm_a32(uint32_t fetchdat)
{
    uint64_t dst;
    MMX_REG src, op;
    MMX_ENTER();

    fetch_ea_32(fetchdat);
    if (fpu_softfloat) {
        if (cpu_mod == 3) {
            fpu_state.tag = 0;
            fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
            src = *(MMX_REG *)&fpu_state.st_space[cpu_rm].fraction;
            op.q = src.q;
            fpu_state.st_space[cpu_reg].fraction = op.q;
            fpu_state.st_space[cpu_reg].exp = 0xffff;
            CLOCK_CYCLES(1);
        } else {
            SEG_CHECK_READ(cpu_state.ea_seg);
            dst = readmemq(easeg, cpu_state.eaaddr);
            if (cpu_state.abrt)
                return 1;
            fpu_state.tag = 0;
            fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
            op.q = dst;
            fpu_state.st_space[cpu_reg].fraction = op.q;
            fpu_state.st_space[cpu_reg].exp = 0xffff;
            CLOCK_CYCLES(2);
        }
    } else {
        if (cpu_mod == 3) {
            cpu_state.MM[cpu_reg].q = cpu_state.MM[cpu_rm].q;
            CLOCK_CYCLES(1);
        } else {
            SEG_CHECK_READ(cpu_state.ea_seg);
            dst = readmemq(easeg, cpu_state.eaaddr);
            if (cpu_state.abrt)
                return 1;
            cpu_state.MM[cpu_reg].q = dst;
            CLOCK_CYCLES(2);
        }
    }
    return 0;
}

static int
opMOVQ_mm_q_a16(uint32_t fetchdat)
{
    MMX_ENTER();

    fetch_ea_16(fetchdat);
    if (fpu_softfloat) {
        if (cpu_mod == 3) {
            fpu_state.tag = 0;
            fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
            fpu_state.st_space[cpu_rm].fraction = fpu_state.st_space[cpu_reg].fraction;
            CLOCK_CYCLES(1);
        } else {
            SEG_CHECK_WRITE(cpu_state.ea_seg);
            CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
            writememq(easeg, cpu_state.eaaddr, fpu_state.st_space[cpu_reg].fraction);
            if (cpu_state.abrt)
                return 1;
            fpu_state.tag = 0;
            fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
            CLOCK_CYCLES(2);
        }
    } else {
        if (cpu_mod == 3) {
            cpu_state.MM[cpu_rm].q = cpu_state.MM[cpu_reg].q;
            CLOCK_CYCLES(1);
        } else {
            SEG_CHECK_WRITE(cpu_state.ea_seg);
            CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
            writememq(easeg, cpu_state.eaaddr, cpu_state.MM[cpu_reg].q);
            if (cpu_state.abrt)
                return 1;
            CLOCK_CYCLES(2);
        }
    }
    return 0;
}
static int
opMOVQ_mm_q_a32(uint32_t fetchdat)
{
    MMX_ENTER();

    fetch_ea_32(fetchdat);
    if (fpu_softfloat) {
        if (cpu_mod == 3) {
            fpu_state.tag = 0;
            fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
            fpu_state.st_space[cpu_rm].fraction = fpu_state.st_space[cpu_reg].fraction;
            CLOCK_CYCLES(1);
        } else {
            SEG_CHECK_WRITE(cpu_state.ea_seg);
            CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
            writememq(easeg, cpu_state.eaaddr, fpu_state.st_space[cpu_reg].fraction);
            if (cpu_state.abrt)
                return 1;
            fpu_state.tag = 0;
            fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
            CLOCK_CYCLES(2);
        }
    } else {
        if (cpu_mod == 3) {
            cpu_state.MM[cpu_rm].q = cpu_state.MM[cpu_reg].q;
            CLOCK_CYCLES(1);
        } else {
            SEG_CHECK_WRITE(cpu_state.ea_seg);
            CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
            writememq(easeg, cpu_state.eaaddr, cpu_state.MM[cpu_reg].q);
            if (cpu_state.abrt)
                return 1;
            CLOCK_CYCLES(2);
        }
    }
    return 0;
}
