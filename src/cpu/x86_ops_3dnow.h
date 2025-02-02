#include <math.h>

static int
opPREFETCH_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);

    CLOCK_CYCLES(1);
    return 0;
}
static int
opPREFETCH_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);

    CLOCK_CYCLES(1);
    return 0;
}

static int
opFEMMS(UNUSED(uint32_t fetchdat))
{
    ILLEGAL_ON(!cpu_has_feature(CPU_FEATURE_MMX));
    if (cr0 & 0xc) {
        x86_int(7);
        return 1;
    }
    x87_emms();
    CLOCK_CYCLES(1);
    return 0;
}

static int
opPAVGUSB(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->b[0] = (dst->b[0] + src.b[0] + 1) >> 1;
    dst->b[1] = (dst->b[1] + src.b[1] + 1) >> 1;
    dst->b[2] = (dst->b[2] + src.b[2] + 1) >> 1;
    dst->b[3] = (dst->b[3] + src.b[3] + 1) >> 1;
    dst->b[4] = (dst->b[4] + src.b[4] + 1) >> 1;
    dst->b[5] = (dst->b[5] + src.b[5] + 1) >> 1;
    dst->b[6] = (dst->b[6] + src.b[6] + 1) >> 1;
    dst->b[7] = (dst->b[7] + src.b[7] + 1) >> 1;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPF2ID(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->sl[0] = (int32_t) src.f[0];
    dst->sl[1] = (int32_t) src.f[1];

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPF2IW(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->sw[0] = (int32_t) src.f[0];
    dst->sw[1] = (int32_t) src.f[1];

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPFACC(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);
    float    tempf;

    MMX_GETSRC();

    tempf     = dst->f[0] + dst->f[1];
    dst->f[1] = src.f[0] + src.f[1];
    dst->f[0] = tempf;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPFNACC(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);
    float    tempf;

    MMX_GETSRC();

    tempf     = dst->f[0] - dst->f[1];
    dst->f[1] = src.f[0] - src.f[1];
    dst->f[0] = tempf;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPFPNACC(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);
    float    tempf;

    MMX_GETSRC();

    tempf     = dst->f[0] - dst->f[1];
    dst->f[1] = src.f[0] + src.f[1];
    dst->f[0] = tempf;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPSWAPD(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);
    float    tempf;
    float    tempf2;

    MMX_GETSRC();

    /* We have to do this in case source and destination overlap. */
    tempf     = src.f[0];
    tempf2    = src.f[1];
    dst->f[1] = tempf;
    dst->f[0] = tempf2;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPFADD(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->f[0] += src.f[0];
    dst->f[1] += src.f[1];

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPFCMPEQ(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->l[0] = (dst->f[0] == src.f[0]) ? 0xffffffff : 0;
    dst->l[1] = (dst->f[1] == src.f[1]) ? 0xffffffff : 0;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPFCMPGE(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->l[0] = (dst->f[0] >= src.f[0]) ? 0xffffffff : 0;
    dst->l[1] = (dst->f[1] >= src.f[1]) ? 0xffffffff : 0;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPFCMPGT(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->l[0] = (dst->f[0] > src.f[0]) ? 0xffffffff : 0;
    dst->l[1] = (dst->f[1] > src.f[1]) ? 0xffffffff : 0;

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPFMAX(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    if (src.f[0] > dst->f[0])
        dst->f[0] = src.f[0];
    if (src.f[1] > dst->f[1])
        dst->f[1] = src.f[1];

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPFMIN(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    if (src.f[0] < dst->f[0])
        dst->f[0] = src.f[0];
    if (src.f[1] < dst->f[1])
        dst->f[1] = src.f[1];

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPFMUL(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->f[0] *= src.f[0];
    dst->f[1] *= src.f[1];

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPFRCP(UNUSED(uint32_t fetchdat))
{
    MMX_REG *dst = MMX_GETREGP(cpu_reg);

    union {
        uint32_t i;
        float    f;
    } src;

    if (cpu_mod == 3) {
        src.f = (MMX_GETREG(cpu_rm)).f[0];
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_READ(cpu_state.ea_seg);
        src.i = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        CLOCK_CYCLES(2);
    }

    dst->f[0] = 1.0 / src.f;
    dst->f[1] = dst->f[0];

    MMX_SETEXP(cpu_reg);

    return 0;
}
/*Since opPFRCP() calculates a full precision reciprocal, treat the followup iterations as MOVs*/
static int
opPFRCPIT1(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->f[0] = src.f[0];
    dst->f[1] = src.f[1];

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPFRCPIT2(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->f[0] = src.f[0];
    dst->f[1] = src.f[1];

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPFRSQRT(UNUSED(uint32_t fetchdat))
{
    MMX_REG *dst = MMX_GETREGP(cpu_reg);

    union {
        uint32_t i;
        float    f;
    } src;

    if (cpu_mod == 3) {
        src.f = (MMX_GETREG(cpu_rm)).f[0];
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_READ(cpu_state.ea_seg);
        src.i = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        CLOCK_CYCLES(2);
    }

    dst->f[0] = 1.0 / sqrt(src.f);
    dst->f[1] = dst->f[0];

    MMX_SETEXP(cpu_reg);

    return 0;
}
/*Since opPFRSQRT() calculates a full precision inverse square root, treat the followup iteration as a NOP*/
static int
opPFRSQIT1(UNUSED(uint32_t fetchdat))
{
    MMX_REG src;

    MMX_GETSRC();
    UN_USED(src);

    return 0;
}
static int
opPFSUB(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->f[0] -= src.f[0];
    dst->f[1] -= src.f[1];

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPFSUBR(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->f[0] = src.f[0] - dst->f[0];
    dst->f[1] = src.f[1] - dst->f[1];

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPI2FD(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->f[0] = (float) src.sl[0];
    dst->f[1] = (float) src.sl[1];

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPI2FW(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->f[0] = (float) src.sw[0];
    dst->f[1] = (float) src.sw[1];

    MMX_SETEXP(cpu_reg);

    return 0;
}
static int
opPMULHRW(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst = MMX_GETREGP(cpu_reg);

    if (cpu_mod == 3) {
        src = MMX_GETREG(cpu_rm);

        dst->w[0] = (((int32_t) dst->sw[0] * (int32_t) src.sw[0]) + 0x8000) >> 16;
        dst->w[1] = (((int32_t) dst->sw[1] * (int32_t) src.sw[1]) + 0x8000) >> 16;
        dst->w[2] = (((int32_t) dst->sw[2] * (int32_t) src.sw[2]) + 0x8000) >> 16;
        dst->w[3] = (((int32_t) dst->sw[3] * (int32_t) src.sw[3]) + 0x8000) >> 16;
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_READ(cpu_state.ea_seg);
        src.l[0] = readmeml(easeg, cpu_state.eaaddr);
        src.l[1] = readmeml(easeg, cpu_state.eaaddr + 4);
        if (cpu_state.abrt)
            return 0;
        dst->w[0] = ((int32_t) (dst->sw[0] * (int32_t) src.sw[0]) + 0x8000) >> 16;
        dst->w[1] = ((int32_t) (dst->sw[1] * (int32_t) src.sw[1]) + 0x8000) >> 16;
        dst->w[2] = ((int32_t) (dst->sw[2] * (int32_t) src.sw[2]) + 0x8000) >> 16;
        dst->w[3] = ((int32_t) (dst->sw[3] * (int32_t) src.sw[3]) + 0x8000) >> 16;
        CLOCK_CYCLES(2);
    }

    MMX_SETEXP(cpu_reg);

    return 0;
}

const OpFn OP_TABLE(3DNOW)[256] = {
    // clang-format off
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        opPI2FD,        ILLEGAL,        ILLEGAL,
/*10*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        opPF2ID,        ILLEGAL,        ILLEGAL,
/*20*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,
/*30*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,

/*40*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,
/*50*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,
/*60*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,
/*70*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,

/*80*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,
/*90*/  opPFCMPGE,      ILLEGAL,        ILLEGAL,        ILLEGAL,        opPFMIN,        ILLEGAL,        opPFRCP,        opPFRSQRT,      ILLEGAL,        ILLEGAL,        opPFSUB,        ILLEGAL,        ILLEGAL,        ILLEGAL,        opPFADD,        ILLEGAL,
/*a0*/  opPFCMPGT,      ILLEGAL,        ILLEGAL,        ILLEGAL,        opPFMAX,        ILLEGAL,        opPFRCPIT1,     opPFRSQIT1,     ILLEGAL,        ILLEGAL,        opPFSUBR,       ILLEGAL,        ILLEGAL,        ILLEGAL,        opPFACC,        ILLEGAL,
/*b0*/  opPFCMPEQ,      ILLEGAL,        ILLEGAL,        ILLEGAL,        opPFMUL,        ILLEGAL,        opPFRCPIT2,     opPMULHRW,      ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        opPAVGUSB,

/*c0*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,
/*d0*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,
/*e0*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,
/*f0*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,
    // clang-format on
};

const OpFn OP_TABLE(3DNOWE)[256] = {
    // clang-format off
/*      00              01              02              03              04              05              06              07              08              09              0a              0b              0c              0d              0e              0f*/
/*00*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        opPI2FW,        opPI2FD,        ILLEGAL,        ILLEGAL,
/*10*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        opPF2IW,        opPF2ID,        ILLEGAL,        ILLEGAL,
/*20*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,
/*30*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,

/*40*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,
/*50*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,
/*60*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,
/*70*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,

/*80*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        opPFNACC,       ILLEGAL,        ILLEGAL,        ILLEGAL,        opPFPNACC,      ILLEGAL,
/*90*/  opPFCMPGE,      ILLEGAL,        ILLEGAL,        ILLEGAL,        opPFMIN,        ILLEGAL,        opPFRCP,        opPFRSQRT,      ILLEGAL,        ILLEGAL,        opPFSUB,        ILLEGAL,        ILLEGAL,        ILLEGAL,        opPFADD,        ILLEGAL,
/*a0*/  opPFCMPGT,      ILLEGAL,        ILLEGAL,        ILLEGAL,        opPFMAX,        ILLEGAL,        opPFRCPIT1,     opPFRSQIT1,     ILLEGAL,        ILLEGAL,        opPFSUBR,       ILLEGAL,        ILLEGAL,        ILLEGAL,        opPFACC,        ILLEGAL,
/*b0*/  opPFCMPEQ,      ILLEGAL,        ILLEGAL,        ILLEGAL,        opPFMUL,        ILLEGAL,        opPFRCPIT2,     opPMULHRW,      ILLEGAL,        ILLEGAL,        ILLEGAL,        opPSWAPD,       ILLEGAL,        ILLEGAL,        ILLEGAL,        opPAVGUSB,

/*c0*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,
/*d0*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,
/*e0*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,
/*f0*/  ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,        ILLEGAL,
    // clang-format on
};

static int
op3DNOW_a16(uint32_t fetchdat)
{
    uint8_t opcode;

    MMX_ENTER();

    fetch_ea_16(fetchdat);
    opcode = fastreadb(cs + cpu_state.pc);
    if (cpu_state.abrt)
        return 1;
    cpu_state.pc++;

    return x86_opcodes_3DNOW[opcode](0);
}
static int
op3DNOW_a32(uint32_t fetchdat)
{
    uint8_t opcode;

    MMX_ENTER();

    fetch_ea_32(fetchdat);
    opcode = fastreadb(cs + cpu_state.pc);
    if (cpu_state.abrt)
        return 1;
    cpu_state.pc++;

    return x86_opcodes_3DNOW[opcode](0);
}
