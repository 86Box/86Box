#include "codegen_ops.h"

/*Instruction has input dependency on register in REG field*/
#define SRCDEP_REG (1ull << 0)
/*Instruction has input dependency on register in R/M field*/
#define SRCDEP_RM  (1ull << 1)
/*Instruction modifies register in REG field*/
#define DSTDEP_REG (1ull << 2)
/*Instruction modifies register in R/M field*/
#define DSTDEP_RM  (1ull << 3)

#define SRCDEP_SHIFT 4
#define DSTDEP_SHIFT 12

/*Instruction has input dependency on given register*/
#define SRCDEP_EAX (1ull << 4)
#define SRCDEP_ECX (1ull << 5)
#define SRCDEP_EDX (1ull << 6)
#define SRCDEP_EBX (1ull << 7)
#define SRCDEP_ESP (1ull << 8)
#define SRCDEP_EBP (1ull << 9)
#define SRCDEP_ESI (1ull << 10)
#define SRCDEP_EDI (1ull << 11)

/*Instruction modifies given register*/
#define DSTDEP_EAX (1ull << 12)
#define DSTDEP_ECX (1ull << 13)
#define DSTDEP_EDX (1ull << 14)
#define DSTDEP_EBX (1ull << 15)
#define DSTDEP_ESP (1ull << 16)
#define DSTDEP_EBP (1ull << 17)
#define DSTDEP_ESI (1ull << 18)
#define DSTDEP_EDI (1ull << 19)

/*Instruction has ModR/M byte*/
#define MODRM (1ull << 20)
/*Instruction implicitly uses ESP*/
#define IMPL_ESP (1ull << 21)

/*Instruction is MMX shift or pack/unpack instruction*/
#define MMX_SHIFTPACK (1ull << 22)
/*Instruction is MMX multiply instruction*/
#define MMX_MULTIPLY  (1ull << 23)

/*Instruction pops the FPU stack*/
#define FPU_POP         (1ull << 24)
/*Instruction pops the FPU stack twice*/
#define FPU_POP2        (1ull << 25)
/*Instruction pushes onto the FPU stack*/
#define FPU_PUSH        (1ull << 26)

/*Instruction writes to ST(0)*/
#define FPU_WRITE_ST0   (1ull << 27)
/*Instruction reads from ST(0)*/
#define FPU_READ_ST0    (1ull << 28)
/*Instruction reads from and writes to ST(0)*/
#define FPU_RW_ST0      (3ull << 27)

/*Instruction reads from ST(1)*/
#define FPU_READ_ST1    (1ull << 29)
/*Instruction writes to ST(1)*/
#define FPU_WRITE_ST1   (1ull << 30)
/*Instruction reads from and writes to ST(1)*/
#define FPU_RW_ST1      (3ull << 29)

/*Instruction reads from ST(reg)*/
#define FPU_READ_STREG  (1ull << 31)
/*Instruction writes to ST(reg)*/
#define FPU_WRITE_STREG (1ull << 32)
/*Instruction reads from and writes to ST(reg)*/
#define FPU_RW_STREG    (3ull << 30)

#define FPU_FXCH (1ull << 33)

#define HAS_IMM8    (1ull << 34)
#define HAS_IMM1632 (1ull << 35)


#define REGMASK_IMPL_ESP (1 << 8)
#define REGMASK_SHIFTPACK (1 << 9)
#define REGMASK_MULTIPLY  (1 << 9)


extern uint64_t opcode_deps[256];
extern uint64_t opcode_deps_mod3[256];
extern uint64_t opcode_deps_0f[256];
extern uint64_t opcode_deps_0f_mod3[256];
extern uint64_t opcode_deps_0f0f[256];
extern uint64_t opcode_deps_0f0f_mod3[256];
extern uint64_t opcode_deps_shift[8];
extern uint64_t opcode_deps_shift_mod3[8];
extern uint64_t opcode_deps_shift_cl[8];
extern uint64_t opcode_deps_shift_cl_mod3[8];
extern uint64_t opcode_deps_f6[8];
extern uint64_t opcode_deps_f6_mod3[8];
extern uint64_t opcode_deps_f7[8];
extern uint64_t opcode_deps_f7_mod3[8];
extern uint64_t opcode_deps_ff[8];
extern uint64_t opcode_deps_ff_mod3[8];
extern uint64_t opcode_deps_d8[8];
extern uint64_t opcode_deps_d8_mod3[8];
extern uint64_t opcode_deps_d9[8];
extern uint64_t opcode_deps_d9_mod3[64];
extern uint64_t opcode_deps_da[8];
extern uint64_t opcode_deps_da_mod3[8];
extern uint64_t opcode_deps_db[8];
extern uint64_t opcode_deps_db_mod3[64];
extern uint64_t opcode_deps_dc[8];
extern uint64_t opcode_deps_dc_mod3[8];
extern uint64_t opcode_deps_dd[8];
extern uint64_t opcode_deps_dd_mod3[8];
extern uint64_t opcode_deps_de[8];
extern uint64_t opcode_deps_de_mod3[8];
extern uint64_t opcode_deps_df[8];
extern uint64_t opcode_deps_df_mod3[8];
extern uint64_t opcode_deps_81[8];
extern uint64_t opcode_deps_81_mod3[8];
extern uint64_t opcode_deps_8x[8];
extern uint64_t opcode_deps_8x_mod3[8];



static inline uint32_t get_addr_regmask(uint64_t data, uint32_t fetchdat, int op_32)
{
        uint32_t addr_regmask = 0;

        if (data & MODRM)
        {
                uint8_t modrm = fetchdat & 0xff;

                if ((modrm & 0xc0) != 0xc0)
                {
                        if (op_32 & 0x200)
                        {
                                if ((modrm & 0x7) == 4)
                                {
                                        uint8_t sib = (fetchdat >> 8) & 0xff;

                                        if ((modrm & 0xc0) != 0xc0 && (sib & 7) != 5)
                                        {
                                                addr_regmask = 1 << (sib & 7);
                                                if ((sib & 0x38) != 0x20)
                                                        addr_regmask |= 1 << ((sib >> 3) & 7);
                                        }
                                }
                                else if ((modrm & 0xc7) != 5)
                                {
                                        addr_regmask = 1 << (modrm & 7);
                                }
                        }
                        else
                        {
                                if ((modrm & 0xc7) != 0x06)
                                {
                                        switch (modrm & 7)
                                        {
                                                case 0: addr_regmask = REG_BX | REG_SI; break;
                                                case 1: addr_regmask = REG_BX | REG_DI; break;
                                                case 2: addr_regmask = REG_BP | REG_SI; break;
                                                case 3: addr_regmask = REG_BP | REG_DI; break;
                                                case 4: addr_regmask = REG_SI; break;
                                                case 5: addr_regmask = REG_DI; break;
                                                case 6: addr_regmask = REG_BP; break;
                                                case 7: addr_regmask = REG_BX; break;
                                        }
                                }
                        }
                }
        }

        if (data & IMPL_ESP)
                addr_regmask |= REGMASK_IMPL_ESP;
        
        return addr_regmask;
}

static inline uint32_t get_srcdep_mask(uint64_t data, uint32_t fetchdat, int bit8, int op_32)
{
        uint32_t mask = 0;
        if (data & SRCDEP_REG)
        {
                int reg = (fetchdat >> 3) & 7;
                if (bit8)
                        reg &= 3;
                mask |= (1 << reg);
        }
        if (data & SRCDEP_RM)
        {
                int reg = fetchdat & 7;
                if (bit8)
                        reg &= 3;
                mask |= (1 << reg);
        }
        mask |= ((data >> SRCDEP_SHIFT) & 0xff);
        if (data & MMX_SHIFTPACK)
                mask |= REGMASK_SHIFTPACK;
        if (data & MMX_MULTIPLY)
                mask |= REGMASK_MULTIPLY;

        mask |= get_addr_regmask(data, fetchdat, op_32);

        return mask;
}

static inline uint32_t get_dstdep_mask(uint64_t data, uint32_t fetchdat, int bit8)
{
        uint32_t mask = 0;
        if (data & DSTDEP_REG)
        {
                int reg = (fetchdat >> 3) & 7;
                if (bit8)
                        reg &= 3;
                mask |= (1 << reg);
        }
        if (data & DSTDEP_RM)
        {
                int reg = fetchdat & 7;
                if (bit8)
                        reg &= 3;
                mask |= (1 << reg);
        }
        mask |= ((data >> DSTDEP_SHIFT) & 0xff);
        if (data & MMX_SHIFTPACK)
                mask |= REGMASK_SHIFTPACK;
        if (data & MMX_MULTIPLY)
                mask |= REGMASK_MULTIPLY;
        if (data & IMPL_ESP)
                mask |= REGMASK_IMPL_ESP | (1 << REG_ESP);

        return mask;
}
