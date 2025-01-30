/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          x86 i686 (Pentium Pro/Pentium II) CPU Instructions.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Copyright 2016-2020 Miran Grca.
 */
static int
opSYSENTER(uint32_t fetchdat)
{
    int ret = sysenter(fetchdat);

    if (ret <= 1)
        CPU_BLOCK_END();

    return ret;
}

static int
opSYSEXIT(uint32_t fetchdat)
{
    int ret = sysexit(fetchdat);

    if (ret <= 1)
        CPU_BLOCK_END();

    return ret;
}

static int
sf_fx_save_stor_common(uint32_t fetchdat, int bits)
{
    uint8_t  fxinst = 0;
    uint32_t tag_byte;
    unsigned index;
    floatx80 reg;

    if (CPUID < 0x650)
        return ILLEGAL(fetchdat);

    FP_ENTER();

    if (bits == 32) {
        fetch_ea_32(fetchdat);
    } else {
        fetch_ea_16(fetchdat);
    }

    if (cpu_state.eaaddr & 0xf) {
        x386_dynarec_log("Effective address %08X not on 16-byte boundary\n", cpu_state.eaaddr);
        x86gpf(NULL, 0);
        return cpu_state.abrt;
    }

    fxinst = (rmdat >> 3) & 7;

    if ((fxinst > 1) || (cpu_mod == 3)) {
        x86illegal();
        return cpu_state.abrt;
    }

    FP_ENTER();

    if (fxinst == 1) {
        /* FXRSTOR */
        fpu_state.cwd = readmemw(easeg, cpu_state.eaaddr);
        fpu_state.swd = readmemw(easeg, cpu_state.eaaddr + 2);
        fpu_state.tos = (fpu_state.swd >> 11) & 7;

        /* always set bit 6 as '1 */
        fpu_state.cwd = (fpu_state.cwd & ~FPU_CW_Reserved_Bits) | 0x0040;

        /* Restore x87 FPU Opcode */
        /* The lower 11 bits contain the FPU opcode, upper 5 bits are reserved */
        fpu_state.foo = readmemw(easeg, cpu_state.eaaddr + 6) & 0x7FF;

        fpu_state.fip = readmeml(easeg, cpu_state.eaaddr + 8);
        fpu_state.fcs = readmemw(easeg, cpu_state.eaaddr + 12);

        tag_byte = readmemb(easeg, cpu_state.eaaddr + 4);

        fpu_state.fdp = readmeml(easeg, cpu_state.eaaddr + 16);
        fpu_state.fds = readmemw(easeg, cpu_state.eaaddr + 20);

        /* load i387 register file */
        for (index = 0; index < 8; index++) {
            reg.signif  = readmemq(easeg, cpu_state.eaaddr + (index * 16) + 32);
            reg.signExp = readmemw(easeg, cpu_state.eaaddr + (index * 16) + 40);

            // update tag only if it is not empty
            FPU_save_regi_tag(reg, IS_TAG_EMPTY(index) ? X87_TAG_EMPTY : FPU_tagof(reg), index);
        }

        fpu_state.tag = unpack_FPU_TW(tag_byte);

        /* check for unmasked exceptions */
        if (fpu_state.swd & ~fpu_state.cwd & FPU_CW_Exceptions_Mask) {
            /* set the B and ES bits in the status-word */
            fpu_state.swd |= (FPU_SW_Summary | FPU_SW_Backward);
        } else {
            /* clear the B and ES bits in the status-word */
            fpu_state.swd &= ~(FPU_SW_Summary | FPU_SW_Backward);
        }

        // CLOCK_CYCLES((cr0 & 1) ? 34 : 44);
        CLOCK_CYCLES(1);
    } else {
        /* FXSAVE */
        writememw(easeg, cpu_state.eaaddr, i387_get_control_word());
        writememw(easeg, cpu_state.eaaddr + 2, i387_get_status_word());
        writememw(easeg, cpu_state.eaaddr + 4, pack_FPU_TW(fpu_state.tag));

        /* x87 FPU Opcode (16 bits) */
        /* The lower 11 bits contain the FPU opcode, upper 5 bits are reserved */
        writememw(easeg, cpu_state.eaaddr + 6, fpu_state.foo);

        /*
         * x87 FPU IP Offset (32/64 bits)
         * The contents of this field differ depending on the current
         * addressing mode (16/32/64 bit) when the FXSAVE instruction was executed:
         *   + 64-bit mode - 64-bit IP offset
         *   + 32-bit mode - 32-bit IP offset
         *   + 16-bit mode - low 16 bits are IP offset; high 16 bits are reserved.
         * x87 CS FPU IP Selector
         *   + 16 bit, in 16/32 bit mode only
         */
        writememl(easeg, cpu_state.eaaddr + 8, fpu_state.fip);
        writememl(easeg, cpu_state.eaaddr + 12, fpu_state.fcs);

        /*
         * x87 FPU Instruction Operand (Data) Pointer Offset (32/64 bits)
         * The contents of this field differ depending on the current
         * addressing mode (16/32 bit) when the FXSAVE instruction was executed:
         *   + 64-bit mode - 64-bit offset
         *   + 32-bit mode - 32-bit offset
         *   + 16-bit mode - low 16 bits are offset; high 16 bits are reserved.
         * x87 DS FPU Instruction Operand (Data) Pointer Selector
         *   + 16 bit, in 16/32 bit mode only
         */
        writememl(easeg, cpu_state.eaaddr + 16, fpu_state.fdp);
        writememl(easeg, cpu_state.eaaddr + 20, fpu_state.fds);

        /* store i387 register file */
        for (index = 0; index < 8; index++) {
            const floatx80 fp = FPU_read_regi(index);

            writememq(easeg, cpu_state.eaaddr + (index * 16) + 32, fp.signif);
            writememw(easeg, cpu_state.eaaddr + (index * 16) + 40, fp.signExp);
        }

        CLOCK_CYCLES(1);
    }

    return cpu_state.abrt;
}

static int
fx_save_stor_common(uint32_t fetchdat, int bits)
{
    uint8_t  fxinst     = 0;
    uint16_t twd        = x87_gettag();
    uint32_t old_eaaddr = 0;
    uint8_t  ftwb       = 0;
    uint16_t rec_ftw    = 0;
    uint16_t fpus       = 0;
    int      i;
    int      mmx_tags = 0;
    uint16_t exp      = 0x0000;
    uint64_t mant     = 0x0000000000000000ULL;
    uint64_t fraction;
    uint8_t  jm;
    uint8_t  valid;
    /* Exp_all_1 Exp_all_0 Frac_all_0 J M FTW_Valid  |  Ent
       ----------------------------------------------+------ */
    uint8_t ftw_table_idx;
    uint8_t ftw_table[48] = { 0x03,   /* 0         0         0          0 0 0          |  0x00 */
                              0x02,   /* 0         0         0          0 0 1          |  0x01 */
                              0x03,   /* 0         0         0          0 0 0          |  0x02 */
                              0x02,   /* 0         0         0          0 1 1          |  0x03 */
                              0x03,   /* 0         0         0          1 0 0          |  0x04 */
                              0x00,   /* 0         0         0          1 0 1          |  0x05 */
                              0x03,   /* 0         0         0          1 1 0          |  0x06 */
                              0x00,   /* 0         0         0          1 1 1          |  0x07 */
                              0x03,   /* 0         0         1          0 0 0          |  0x08 */
                              0x02,   /* 0         0         1          0 0 1          |  0x09 */
                              0x03,   /* 0         0         1          0 1 0          |  0x0a - Impossible */
                              0x03,   /* 0         0         1          0 1 1          |  0x0b - Impossible */
                              0x03,   /* 0         0         1          1 0 0          |  0x0c */
                              0x02,   /* 0         0         1          1 0 1          |  0x0d */
                              0x03,   /* 0         0         1          1 1 0          |  0x0e - Impossible */
                              0x03,   /* 0         0         1          1 1 1          |  0x0f - Impossible */
                              0x03,   /* 0         1         0          0 0 0          |  0x10 */
                              0x02,   /* 0         1         0          0 0 1          |  0x11 */
                              0x03,   /* 0         1         0          0 1 0          |  0x12 */
                              0x02,   /* 0         1         0          0 1 1          |  0x13 */
                              0x03,   /* 0         1         0          1 0 0          |  0x14 */
                              0x02,   /* 0         1         0          1 0 1          |  0x15 */
                              0x03,   /* 0         1         0          1 1 0          |  0x16 */
                              0x02,   /* 0         1         0          1 1 1          |  0x17 */
                              0x03,   /* 0         1         1          0 0 0          |  0x18 */
                              0x01,   /* 0         1         1          0 0 1          |  0x19 */
                              0x03,   /* 0         1         1          0 1 0          |  0x1a - Impossible */
                              0x03,   /* 0         1         1          0 1 1          |  0x1b - Impossible */
                              0x03,   /* 0         1         1          1 0 0          |  0x1c */
                              0x01,   /* 0         1         1          1 0 1          |  0x1d */
                              0x03,   /* 0         1         1          1 1 0          |  0x1e - Impossible */
                              0x03,   /* 0         1         1          1 1 1          |  0x1f - Impossible */
                              0x03,   /* 1         0         0          0 0 0          |  0x20 */
                              0x02,   /* 1         0         0          0 0 1          |  0x21 */
                              0x03,   /* 1         0         0          0 1 0          |  0x22 */
                              0x02,   /* 1         0         0          0 1 1          |  0x23 */
                              0x03,   /* 1         0         0          1 0 0          |  0x24 */
                              0x02,   /* 1         0         0          1 0 1          |  0x25 */
                              0x03,   /* 1         0         0          1 1 0          |  0x26 */
                              0x02,   /* 1         0         0          1 1 1          |  0x27 */
                              0x03,   /* 1         0         1          0 0 0          |  0x28 */
                              0x02,   /* 1         0         1          0 0 1          |  0x29 */
                              0x03,   /* 1         0         1          0 1 0          |  0x2a - Impossible */
                              0x03,   /* 1         0         1          0 1 1          |  0x2b - Impossible */
                              0x03,   /* 1         0         1          1 0 0          |  0x2c */
                              0x02,   /* 1         0         1          1 0 1          |  0x2d */
                              0x03,   /* 1         0         1          1 1 0          |  0x2e - Impossible */
                              0x03 }; /* 1         0         1          1 1 1          |  0x2f - Impossible */
                                      /* M is the most significant bit of the franction, so it is impossible
                                         for M to o be 1 when the fraction is all 0's. */

    if (CPUID < 0x650)
        return ILLEGAL(fetchdat);

    FP_ENTER();

    if (bits == 32) {
        fetch_ea_32(fetchdat);
    } else {
        fetch_ea_16(fetchdat);
    }

    if (cpu_state.eaaddr & 0xf) {
        x386_dynarec_log("Effective address %08X not on 16-byte boundary\n", cpu_state.eaaddr);
        x86gpf(NULL, 0);
        return cpu_state.abrt;
    }

    fxinst = (rmdat >> 3) & 7;

    if ((fxinst > 1) || (cpu_mod == 3)) {
        x86illegal();
        return cpu_state.abrt;
    }

    FP_ENTER();

    old_eaaddr = cpu_state.eaaddr;

    if (fxinst == 1) {
        /* FXRSTOR */
        cpu_state.npxc = readmemw(easeg, cpu_state.eaaddr);
        fpus           = readmemw(easeg, cpu_state.eaaddr + 2);
        cpu_state.npxc = (cpu_state.npxc & ~FPU_CW_Reserved_Bits) | 0x0040;
        codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
        cpu_state.TOP = (fpus >> 11) & 7;
        cpu_state.npxs &= fpus & ~0x3800;

        x87_pc_off = readmeml(easeg, cpu_state.eaaddr + 8);
        x87_pc_seg = readmemw(easeg, cpu_state.eaaddr + 12);

        ftwb = readmemb(easeg, cpu_state.eaaddr + 4);

        x87_op_off = readmeml(easeg, cpu_state.eaaddr + 16);
        x87_op_off |= (readmemw(easeg, cpu_state.eaaddr + 6) >> 12) << 16;
        x87_op_seg = readmemw(easeg, cpu_state.eaaddr + 20);

        for (i = 0; i <= 7; i++) {
            cpu_state.eaaddr = old_eaaddr + 32 + (i << 4);
            mant             = readmemq(easeg, cpu_state.eaaddr);
            fraction         = mant & 0x7fffffffffffffffULL;
            exp              = readmemw(easeg, cpu_state.eaaddr + 8);
            jm               = (mant >> 62) & 0x03;
            valid            = !(ftwb & (1 << i));

            ftw_table_idx = (!!(exp == 0x1111)) << 5;
            ftw_table_idx |= (!!(exp == 0x0000)) << 4;
            ftw_table_idx |= (!!(fraction == 0x0000000000000000ULL)) << 3;
            ftw_table_idx |= (jm << 1);
            ftw_table_idx |= valid;

            rec_ftw |= (ftw_table[ftw_table_idx] << (i << 1));

            if (exp == 0xffff)
                mmx_tags++;
        }

        cpu_state.ismmx = 0;
        /* Determine, whether or not the saved state is x87 or MMX based on a heuristic,
           because we do not keep the internal state in 64-bit precision.

           TODO: Is there no way to unify the whole lot? */
        if ((mmx_tags == 8) && !cpu_state.TOP)
            cpu_state.ismmx = 1;

        x87_settag(rec_ftw);

        if (cpu_state.ismmx) {
            for (i = 0; i <= 7; i++) {
                cpu_state.eaaddr = old_eaaddr + 32 + (i << 4);
                x87_ldmmx(&(cpu_state.MM[i]), &(cpu_state.MM_w4[i]));
            }
        } else {
            for (i = 0; i <= 7; i++) {
                cpu_state.eaaddr = old_eaaddr + 32 + (i << 4);
                x87_ld_frstor(i);
            }
        }

        // CLOCK_CYCLES((cr0 & 1) ? 34 : 44);
        CLOCK_CYCLES(1);
    } else {
        /* FXSAVE */
        if ((twd & 0x0003) != 0x0003)
            ftwb |= 0x01;
        if ((twd & 0x000c) != 0x000c)
            ftwb |= 0x02;
        if ((twd & 0x0030) != 0x0030)
            ftwb |= 0x04;
        if ((twd & 0x00c0) != 0x00c0)
            ftwb |= 0x08;
        if ((twd & 0x0300) != 0x0300)
            ftwb |= 0x10;
        if ((twd & 0x0c00) != 0x0c00)
            ftwb |= 0x20;
        if ((twd & 0x3000) != 0x3000)
            ftwb |= 0x40;
        if ((twd & 0xc000) != 0xc000)
            ftwb |= 0x80;

        writememw(easeg, cpu_state.eaaddr, cpu_state.npxc);
        writememw(easeg, cpu_state.eaaddr + 2, cpu_state.npxs);
        writememb(easeg, cpu_state.eaaddr + 4, ftwb);

        writememw(easeg, cpu_state.eaaddr + 6, (x87_op_off >> 16) << 12);
        writememl(easeg, cpu_state.eaaddr + 8, x87_pc_off);
        writememw(easeg, cpu_state.eaaddr + 12, x87_pc_seg);

        writememl(easeg, cpu_state.eaaddr + 16, x87_op_off);
        writememw(easeg, cpu_state.eaaddr + 20, x87_op_seg);

        if (cpu_state.ismmx) {
            for (i = 0; i <= 7; i++) {
                cpu_state.eaaddr = old_eaaddr + 32 + (i << 4);
                x87_stmmx(cpu_state.MM[i]);
            }
        } else {
            for (i = 0; i <= 7; i++) {
                cpu_state.eaaddr = old_eaaddr + 32 + (i << 4);
                x87_st_fsave(i);
            }
        }

        cpu_state.eaaddr = old_eaaddr;

        CLOCK_CYCLES(1);
    }

    return cpu_state.abrt;
}

static int
opFXSAVESTOR_a16(uint32_t fetchdat)
{
    if (fpu_softfloat)
        return sf_fx_save_stor_common(fetchdat, 16);

    return fx_save_stor_common(fetchdat, 16);
}

static int
opFXSAVESTOR_a32(uint32_t fetchdat)
{
    if (fpu_softfloat)
        return sf_fx_save_stor_common(fetchdat, 32);

    return fx_save_stor_common(fetchdat, 32);
}

static int
opHINT_NOP_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    CLOCK_CYCLES(1);
    return 0;
}

static int
opHINT_NOP_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    CLOCK_CYCLES(1);
    return 0;
}
