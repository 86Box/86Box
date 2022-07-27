#include <math.h>

static int opPREFETCH_a16(uint32_t fetchdat)
{
        fetch_ea_16(fetchdat);
        ILLEGAL_ON(cpu_mod == 3);

        CLOCK_CYCLES(1);
        return 0;
}
static int opPREFETCH_a32(uint32_t fetchdat)
{
        fetch_ea_32(fetchdat);
        ILLEGAL_ON(cpu_mod == 3);

        CLOCK_CYCLES(1);
        return 0;
}

static int opFEMMS(uint32_t fetchdat)
{
        ILLEGAL_ON(!cpu_has_feature(CPU_FEATURE_MMX));
        if (cr0 & 0xc)
        {
                x86_int(7);
                return 1;
        }
        x87_emms();
        CLOCK_CYCLES(1);
        return 0;
}

static int opPAVGUSB(uint32_t fetchdat)
{
        MMX_REG src;

        MMX_GETSRC();

        cpu_state.MM[cpu_reg].b[0] = (cpu_state.MM[cpu_reg].b[0] + src.b[0] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[1] = (cpu_state.MM[cpu_reg].b[1] + src.b[1] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[2] = (cpu_state.MM[cpu_reg].b[2] + src.b[2] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[3] = (cpu_state.MM[cpu_reg].b[3] + src.b[3] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[4] = (cpu_state.MM[cpu_reg].b[4] + src.b[4] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[5] = (cpu_state.MM[cpu_reg].b[5] + src.b[5] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[6] = (cpu_state.MM[cpu_reg].b[6] + src.b[6] + 1) >> 1;
        cpu_state.MM[cpu_reg].b[7] = (cpu_state.MM[cpu_reg].b[7] + src.b[7] + 1) >> 1;

        return 0;
}
static int opPF2ID(uint32_t fetchdat)
{
        MMX_REG src;

        MMX_GETSRC();

        cpu_state.MM[cpu_reg].sl[0] = (int32_t)src.f[0];
        cpu_state.MM[cpu_reg].sl[1] = (int32_t)src.f[1];

        return 0;
}
static int opPFACC(uint32_t fetchdat)
{
        MMX_REG src;
        float tempf;

        MMX_GETSRC();

        tempf = cpu_state.MM[cpu_reg].f[0] + cpu_state.MM[cpu_reg].f[1];
        cpu_state.MM[cpu_reg].f[1] = src.f[0] + src.f[1];
        cpu_state.MM[cpu_reg].f[0] = tempf;

        return 0;
}
static int opPFADD(uint32_t fetchdat)
{
        MMX_REG src;

        MMX_GETSRC();

        cpu_state.MM[cpu_reg].f[0] += src.f[0];
        cpu_state.MM[cpu_reg].f[1] += src.f[1];

        return 0;
}
static int opPFCMPEQ(uint32_t fetchdat)
{
        MMX_REG src;

        MMX_GETSRC();

        cpu_state.MM[cpu_reg].l[0] = (cpu_state.MM[cpu_reg].f[0] == src.f[0]) ? 0xffffffff : 0;
        cpu_state.MM[cpu_reg].l[1] = (cpu_state.MM[cpu_reg].f[1] == src.f[1]) ? 0xffffffff : 0;

        return 0;
}
static int opPFCMPGE(uint32_t fetchdat)
{
        MMX_REG src;

        MMX_GETSRC();

        cpu_state.MM[cpu_reg].l[0] = (cpu_state.MM[cpu_reg].f[0] >= src.f[0]) ? 0xffffffff : 0;
        cpu_state.MM[cpu_reg].l[1] = (cpu_state.MM[cpu_reg].f[1] >= src.f[1]) ? 0xffffffff : 0;

        return 0;
}
static int opPFCMPGT(uint32_t fetchdat)
{
        MMX_REG src;

        MMX_GETSRC();

        cpu_state.MM[cpu_reg].l[0] = (cpu_state.MM[cpu_reg].f[0] > src.f[0]) ? 0xffffffff : 0;
        cpu_state.MM[cpu_reg].l[1] = (cpu_state.MM[cpu_reg].f[1] > src.f[1]) ? 0xffffffff : 0;

        return 0;
}
static int opPFMAX(uint32_t fetchdat)
{
        MMX_REG src;

        MMX_GETSRC();

        if (src.f[0] > cpu_state.MM[cpu_reg].f[0])
                cpu_state.MM[cpu_reg].f[0] = src.f[0];
        if (src.f[1] > cpu_state.MM[cpu_reg].f[1])
                cpu_state.MM[cpu_reg].f[1] = src.f[1];

        return 0;
}
static int opPFMIN(uint32_t fetchdat)
{
        MMX_REG src;

        MMX_GETSRC();

        if (src.f[0] < cpu_state.MM[cpu_reg].f[0])
                cpu_state.MM[cpu_reg].f[0] = src.f[0];
        if (src.f[1] < cpu_state.MM[cpu_reg].f[1])
                cpu_state.MM[cpu_reg].f[1] = src.f[1];

        return 0;
}
static int opPFMUL(uint32_t fetchdat)
{
        MMX_REG src;

        MMX_GETSRC();

        cpu_state.MM[cpu_reg].f[0] *= src.f[0];
        cpu_state.MM[cpu_reg].f[1] *= src.f[1];

        return 0;
}
static int opPFRCP(uint32_t fetchdat)
{
        union
        {
                uint32_t i;
                float f;
        } src;

        if (cpu_mod == 3)
        {
                src.f = cpu_state.MM[cpu_rm].f[0];
                CLOCK_CYCLES(1);
        }
        else
        {
                SEG_CHECK_READ(cpu_state.ea_seg);
                src.i = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
                CLOCK_CYCLES(2);
        }

        cpu_state.MM[cpu_reg].f[0] = 1.0/src.f;
        cpu_state.MM[cpu_reg].f[1] = cpu_state.MM[cpu_reg].f[0];

        return 0;
}
/*Since opPFRCP() calculates a full precision reciprocal, treat the followup iterations as MOVs*/
static int opPFRCPIT1(uint32_t fetchdat)
{
        MMX_REG src;

        MMX_GETSRC();

        cpu_state.MM[cpu_reg].f[0] = src.f[0];
        cpu_state.MM[cpu_reg].f[1] = src.f[1];

        return 0;
}
static int opPFRCPIT2(uint32_t fetchdat)
{
        MMX_REG src;

        MMX_GETSRC();

        cpu_state.MM[cpu_reg].f[0] = src.f[0];
        cpu_state.MM[cpu_reg].f[1] = src.f[1];

        return 0;
}
static int opPFRSQRT(uint32_t fetchdat)
{
        union
        {
                uint32_t i;
                float f;
        } src;

        if (cpu_mod == 3)
        {
                src.f = cpu_state.MM[cpu_rm].f[0];
                CLOCK_CYCLES(1);
        }
        else
        {
                SEG_CHECK_READ(cpu_state.ea_seg);
                src.i = readmeml(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
                CLOCK_CYCLES(2);
        }

        cpu_state.MM[cpu_reg].f[0] = 1.0/sqrt(src.f);
        cpu_state.MM[cpu_reg].f[1] = cpu_state.MM[cpu_reg].f[0];

        return 0;
}
/*Since opPFRSQRT() calculates a full precision inverse square root, treat the followup iteration as a NOP*/
static int opPFRSQIT1(uint32_t fetchdat)
{
        MMX_REG src;

        MMX_GETSRC();
        UN_USED(src);

        return 0;
}
static int opPFSUB(uint32_t fetchdat)
{
        MMX_REG src;

        MMX_GETSRC();

        cpu_state.MM[cpu_reg].f[0] -= src.f[0];
        cpu_state.MM[cpu_reg].f[1] -= src.f[1];

        return 0;
}
static int opPFSUBR(uint32_t fetchdat)
{
        MMX_REG src;

        MMX_GETSRC();

        cpu_state.MM[cpu_reg].f[0] = src.f[0] - cpu_state.MM[cpu_reg].f[0];
        cpu_state.MM[cpu_reg].f[1] = src.f[1] - cpu_state.MM[cpu_reg].f[1];

        return 0;
}
static int opPI2FD(uint32_t fetchdat)
{
        MMX_REG src;

        MMX_GETSRC();

        cpu_state.MM[cpu_reg].f[0] = (float)src.sl[0];
        cpu_state.MM[cpu_reg].f[1] = (float)src.sl[1];

        return 0;
}
static int opPMULHRW(uint32_t fetchdat)
{
        if (cpu_mod == 3)
        {
                cpu_state.MM[cpu_reg].w[0] = (((int32_t)cpu_state.MM[cpu_reg].sw[0] * (int32_t)cpu_state.MM[cpu_rm].sw[0]) + 0x8000) >> 16;
                cpu_state.MM[cpu_reg].w[1] = (((int32_t)cpu_state.MM[cpu_reg].sw[1] * (int32_t)cpu_state.MM[cpu_rm].sw[1]) + 0x8000) >> 16;
                cpu_state.MM[cpu_reg].w[2] = (((int32_t)cpu_state.MM[cpu_reg].sw[2] * (int32_t)cpu_state.MM[cpu_rm].sw[2]) + 0x8000) >> 16;
                cpu_state.MM[cpu_reg].w[3] = (((int32_t)cpu_state.MM[cpu_reg].sw[3] * (int32_t)cpu_state.MM[cpu_rm].sw[3]) + 0x8000) >> 16;
                CLOCK_CYCLES(1);
        }
        else
        {
                MMX_REG src;

                SEG_CHECK_READ(cpu_state.ea_seg);
                src.l[0] = readmeml(easeg, cpu_state.eaaddr);
                src.l[1] = readmeml(easeg, cpu_state.eaaddr + 4); if (cpu_state.abrt) return 0;
                cpu_state.MM[cpu_reg].w[0] = ((int32_t)(cpu_state.MM[cpu_reg].sw[0] * (int32_t)src.sw[0]) + 0x8000) >> 16;
                cpu_state.MM[cpu_reg].w[1] = ((int32_t)(cpu_state.MM[cpu_reg].sw[1] * (int32_t)src.sw[1]) + 0x8000) >> 16;
                cpu_state.MM[cpu_reg].w[2] = ((int32_t)(cpu_state.MM[cpu_reg].sw[2] * (int32_t)src.sw[2]) + 0x8000) >> 16;
                cpu_state.MM[cpu_reg].w[3] = ((int32_t)(cpu_state.MM[cpu_reg].sw[3] * (int32_t)src.sw[3]) + 0x8000) >> 16;
                CLOCK_CYCLES(2);
        }
        return 0;
}

const OpFn OP_TABLE(3DNOW)[256] =
{
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
};

static int op3DNOW_a16(uint32_t fetchdat)
{
        uint8_t opcode;

        MMX_ENTER();

        fetch_ea_16(fetchdat);
        opcode = fastreadb(cs + cpu_state.pc);
        if (cpu_state.abrt) return 1;
        cpu_state.pc++;

        return x86_opcodes_3DNOW[opcode](0);
}
static int op3DNOW_a32(uint32_t fetchdat)
{
        uint8_t opcode;

        MMX_ENTER();

        fetch_ea_32(fetchdat);
        opcode = fastreadb(cs + cpu_state.pc);
        if (cpu_state.abrt) return 1;
        cpu_state.pc++;

        return x86_opcodes_3DNOW[opcode](0);
}
