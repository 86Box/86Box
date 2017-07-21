/*Elements taken into account :
        - U/V integer pairing
        - FPU/FXCH pairing
        - Prefix decode delay (including shadowing)
        - FPU latencies
  Elements not taken into account :
        - Branch prediction (beyond most simplistic approximation)
        - PMMX decode queue
        - MMX latencies
*/

#include "../ibm.h"
#include "cpu.h"
#include "x86.h"
#include "x86_ops.h"
#include "x87.h"
#include "../mem.h"
#include "codegen.h"



/*Instruction has different execution time for 16 and 32 bit data. Does not pair */
#define CYCLES_HAS_MULTI (1 << 28)

#define CYCLES_MULTI(c16, c32) (CYCLES_HAS_MULTI | c16 | (c32 << 8))

/*Instruction lasts given number of cycles. Does not pair*/
#define CYCLES(c) (c | PAIR_NP)


static int pair_timings[4][4] =
{
/*              Reg     RM      RMW     Branch*/
/*Reg*/         {1,      2,      3,      2},
/*RM*/          {2,      2,      3,      3},
/*RMW*/         {3,      4,      5,      4},
/*Branch*/      {-1,     -1,     -1,     -1}
};

/*Instruction follows either register timing, read-modify, or read-modify-write.
  May be pairable*/
#define CYCLES_REG (0ull << 0)
#define CYCLES_RM  (1ull << 0)
#define CYCLES_RMW (2ull << 0)
#define CYCLES_BRANCH (3ull << 0)

/*Instruction has immediate data. Can only be used with PAIR_U/PAIR_V/PAIR_UV*/
#define CYCLES_HASIMM (3ull << 2)
#define CYCLES_IMM8    (1ull << 2)
#define CYCLES_IMM1632 (2ull << 2)

#define CYCLES_MASK ((1ull << 7) - 1)

/*Instruction is MMX shift or pack/unpack instruction*/
#define MMX_SHIFTPACK (1ull << 7)
/*Instruction is MMX multiply instruction*/
#define MMX_MULTIPLY  (1ull << 8)

/*Instruction does not pair*/
#define PAIR_NP (0ull << 29)
/*Instruction pairs in U pipe only*/
#define PAIR_U  (1ull << 29)
/*Instruction pairs in V pipe only*/
#define PAIR_V  (2ull << 29)
/*Instruction pairs in both U and V pipes*/
#define PAIR_UV (3ull << 29)
/*Instruction pairs in U pipe only and only with FXCH*/
#define PAIR_FX (5ull << 29)
/*Instruction is FXCH and only pairs in V pipe with FX pairable instruction*/
#define PAIR_FXCH (6ull << 29)

#define PAIR_FPU (4ull << 29)

#define PAIR_MASK (7ull << 29)

/*Instruction has input dependency on register in REG field*/
#define SRCDEP_REG (1ull << 9)
/*Instruction has input dependency on register in R/M field*/
#define SRCDEP_RM  (1ull << 10)
/*Instruction modifies register in REG field*/
#define DSTDEP_REG (1ull << 11)
/*Instruction modifies register in R/M field*/
#define DSTDEP_RM  (1ull << 12)

/*Instruction has input dependency on given register*/
#define SRCDEP_EAX (1ull << 13)
#define SRCDEP_ECX (1ull << 14)
#define SRCDEP_EDX (1ull << 15)
#define SRCDEP_EBX (1ull << 16)
#define SRCDEP_ESP (1ull << 17)
#define SRCDEP_EBP (1ull << 18)
#define SRCDEP_ESI (1ull << 19)
#define SRCDEP_EDI (1ull << 20)

/*Instruction modifies given register*/
#define DSTDEP_EAX (1ull << 21)
#define DSTDEP_ECX (1ull << 22)
#define DSTDEP_EDX (1ull << 23)
#define DSTDEP_EBX (1ull << 24)
#define DSTDEP_ESP (1ull << 25)
#define DSTDEP_EBP (1ull << 26)
#define DSTDEP_ESI (1ull << 27)
#define DSTDEP_EDI (1ull << 28)

/*Instruction pops the FPU stack*/
#define FPU_POP         (1ull << 32)
/*Instruction pops the FPU stack twice*/
#define FPU_POP2        (1ull << 33)
/*Instruction pushes onto the FPU stack*/
#define FPU_PUSH        (1ull << 34)

/*Instruction writes to ST(0)*/
#define FPU_WRITE_ST0   (1ull << 35)
/*Instruction reads from ST(0)*/
#define FPU_READ_ST0    (1ull << 36)
/*Instruction reads from and writes to ST(0)*/
#define FPU_RW_ST0      (3ull << 35)

/*Instruction reads from ST(1)*/
#define FPU_READ_ST1    (1ull << 37)
/*Instruction writes to ST(1)*/
#define FPU_WRITE_ST1   (1ull << 38)
/*Instruction reads from and writes to ST(1)*/
#define FPU_RW_ST1      (3ull << 37)

/*Instruction reads from ST(reg)*/
#define FPU_READ_STREG  (1ull << 39)
/*Instruction writes to ST(reg)*/
#define FPU_WRITE_STREG (1ull << 40)
/*Instruction reads from and writes to ST(reg)*/
#define FPU_RW_STREG    (3ull << 39)

/*comp_time = cycles until instruction complete
  i_overlap = cycles that overlap with integer
  f_overlap = cycles that overlap with subsequent FPU*/
#define FPU_CYCLES(comp_time, i_overlap, f_overlap) ((uint64_t)comp_time) | ((uint64_t)i_overlap << 41) | ((uint64_t)f_overlap << 49) | PAIR_FPU

#define FPU_COMP_TIME(timing) (timing & 0xff)
#define FPU_I_OVERLAP(timing) ((timing >> 41) & 0xff)
#define FPU_F_OVERLAP(timing) ((timing >> 49) & 0xff)

#define FPU_I_LATENCY(timing) (FPU_COMP_TIME(timing) - FPU_I_OVERLAP(timing))

#define FPU_F_LATENCY(timing) (FPU_I_OVERLAP(timing) - FPU_F_OVERLAP(timing))

#define FPU_RESULT_LATENCY(timing) ((timing >> 41) & 0xff)

#define FPU_FXCH (1ull << 56)

#define INVALID 0

static int u_pipe_full;
static uint32_t u_pipe_opcode;
static uint64_t *u_pipe_timings;
static uint32_t u_pipe_op_32;
static uint32_t u_pipe_regmask;
static uint32_t u_pipe_fetchdat;
static int u_pipe_decode_delay_offset;

static int fpu_latency;
static int fpu_st_latency[8];

#define REGMASK_SHIFTPACK (1 << 8)
#define REGMASK_MULTIPLY  (1 << 8)

static uint32_t get_srcdep_mask(uint32_t data, uint32_t fetchdat, int bit8)
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
        mask |= ((data >> 16) & 0xff);
        if (data & MMX_SHIFTPACK)
                mask |= REGMASK_SHIFTPACK;
        if (data & MMX_MULTIPLY)
                mask |= REGMASK_MULTIPLY;
        
        return mask;
}

static uint32_t get_dstdep_mask(uint32_t data, uint32_t fetchdat, int bit8)
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
        mask |= ((data >> 24) & 0xff);
        if (data & MMX_SHIFTPACK)
                mask |= REGMASK_SHIFTPACK;
        if (data & MMX_MULTIPLY)
                mask |= REGMASK_MULTIPLY;
        
        return mask;
}
static uint64_t opcode_timings[256] =
{
/*      ADD                                             ADD                                             ADD                                            ADD*/
/*00*/  PAIR_UV | CYCLES_RMW | SRCDEP_REG,              PAIR_UV | CYCLES_RMW | SRCDEP_REG,              PAIR_UV | CYCLES_RM | SRCDEP_REG | DSTDEP_REG, PAIR_UV | CYCLES_RM | SRCDEP_REG | DSTDEP_REG,
/*      ADD                                             ADD                                             PUSH ES                                        POP ES*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, PAIR_NP | CYCLES(1),                           PAIR_NP | CYCLES(3),
/*      OR                                              OR                                              OR                                             OR*/
        PAIR_UV | CYCLES_RMW | SRCDEP_REG,              PAIR_UV | CYCLES_RMW | SRCDEP_REG,              PAIR_UV | CYCLES_RM | SRCDEP_REG | DSTDEP_REG, PAIR_UV | CYCLES_RM | SRCDEP_REG | DSTDEP_REG,
/*      OR                                              OR                                              PUSH CS                                        */
        PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, PAIR_NP | CYCLES(1),                           INVALID,

/*      ADC                                             ADC                                             ADC                                            ADC*/
/*10*/  PAIR_U  | CYCLES_RMW | SRCDEP_REG,              PAIR_U  | CYCLES_RMW | SRCDEP_REG,              PAIR_U  | CYCLES_RM | SRCDEP_REG | DSTDEP_REG, PAIR_U  | CYCLES_RM | SRCDEP_REG | DSTDEP_REG,
/*      ADC                                             ADC                                             PUSH SS                                        POP SS*/
        PAIR_U  | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, PAIR_U  | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, PAIR_NP | CYCLES(1),                           PAIR_NP | CYCLES(3),
/*      SBB                                             SBB                                             SBB                                            SBB*/        
        PAIR_U  | CYCLES_RMW | SRCDEP_REG,              PAIR_U  | CYCLES_RMW | SRCDEP_REG,              PAIR_U  | CYCLES_RM | SRCDEP_REG | DSTDEP_REG, PAIR_U  | CYCLES_RM | SRCDEP_REG | DSTDEP_REG,
/*      SBB                                             SBB                                             PUSH DS                                        POP DS*/
        PAIR_U  | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, PAIR_U  | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, PAIR_NP | CYCLES(1),                           PAIR_NP | CYCLES(3),

/*      AND                                             AND                                             AND                                            AND*/
/*20*/  PAIR_UV | CYCLES_RMW | SRCDEP_REG,              PAIR_UV | CYCLES_RMW | SRCDEP_REG,              PAIR_UV | CYCLES_RM | SRCDEP_REG | DSTDEP_REG, PAIR_UV | CYCLES_RM | SRCDEP_REG | DSTDEP_REG,
/*      AND                                             AND                                                                                            DAA*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, INVALID,                                       PAIR_NP | CYCLES(3),
/*      SUB                                             SUB                                             SUB                                            SUB*/
        PAIR_UV | CYCLES_RMW | SRCDEP_REG,              PAIR_UV | CYCLES_RMW | SRCDEP_REG,              PAIR_UV | CYCLES_RM | SRCDEP_REG | DSTDEP_REG, PAIR_UV | CYCLES_RM | SRCDEP_REG | DSTDEP_REG,
/*      SUB                                             SUB                                                                                            DAS*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, INVALID,                                       PAIR_NP | CYCLES(3),
        
/*      XOR                                             XOR                                             XOR                                            XOR*/
/*30*/  PAIR_UV | CYCLES_RMW | SRCDEP_REG,              PAIR_UV | CYCLES_RMW | SRCDEP_REG,              PAIR_UV | CYCLES_RM | SRCDEP_REG | DSTDEP_REG, PAIR_UV | CYCLES_RM | SRCDEP_REG | DSTDEP_REG,
/*      XOR                                             XOR                                                                                            AAA*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, INVALID,                                       PAIR_NP | CYCLES(3),
/*      CMP                                             CMP                                             CMP                                            CMP*/
        PAIR_UV | CYCLES_RM | SRCDEP_REG,               PAIR_UV | CYCLES_RM | SRCDEP_REG,               PAIR_UV | CYCLES_RM | SRCDEP_REG,              PAIR_UV | CYCLES_RM | SRCDEP_REG,
/*      CMP                                             CMP                                                                                            AAS*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX,              PAIR_UV | CYCLES_REG | SRCDEP_EAX,              INVALID,                                       PAIR_NP | CYCLES(3),

/*      INC EAX                                         INC ECX                                         INC EDX                                         INC EBX*/
/*40*/  PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, PAIR_UV | CYCLES_REG | SRCDEP_ECX | DSTDEP_ECX, PAIR_UV | CYCLES_REG | SRCDEP_EDX | DSTDEP_EDX, PAIR_UV | CYCLES_REG | SRCDEP_EBX | DSTDEP_EBX,
/*      INC ESP                                         INC EBP                                         INC ESI                                         INC EDI*/
        PAIR_UV | CYCLES_REG | SRCDEP_ESP | DSTDEP_ESP, PAIR_UV | CYCLES_REG | SRCDEP_EBP | DSTDEP_EBP, PAIR_UV | CYCLES_REG | SRCDEP_ESI | DSTDEP_ESI, PAIR_UV | CYCLES_REG | SRCDEP_EDI | DSTDEP_EDI,
/*      DEC EAX                                         DEC ECX                                         DEC EDX                                         DEC EBX*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, PAIR_UV | CYCLES_REG | SRCDEP_ECX | DSTDEP_ECX, PAIR_UV | CYCLES_REG | SRCDEP_EDX | DSTDEP_EDX, PAIR_UV | CYCLES_REG | SRCDEP_EBX | DSTDEP_EBX,
/*      DEC ESP                                         DEC EBP                                         DEC ESI                                         DEC EDI*/
        PAIR_UV | CYCLES_REG | SRCDEP_ESP | DSTDEP_ESP, PAIR_UV | CYCLES_REG | SRCDEP_EBP | DSTDEP_EBP, PAIR_UV | CYCLES_REG | SRCDEP_ESI | DSTDEP_ESI, PAIR_UV | CYCLES_REG | SRCDEP_EDI | DSTDEP_EDI,
        
/*      PUSH EAX                                        PUSH ECX                                        PUSH EDX                                        PUSH EBX*/
/*50*/  PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, PAIR_UV | CYCLES_REG | SRCDEP_ECX | DSTDEP_ECX, PAIR_UV | CYCLES_REG | SRCDEP_EDX | DSTDEP_EDX, PAIR_UV | CYCLES_REG | SRCDEP_EBX | DSTDEP_EBX,
/*      PUSH ESP                                        PUSH EBP                                        PUSH ESI                                        PUSH EDI*/
        PAIR_UV | CYCLES_REG | SRCDEP_ESP | DSTDEP_ESP, PAIR_UV | CYCLES_REG | SRCDEP_EBP | DSTDEP_EBP, PAIR_UV | CYCLES_REG | SRCDEP_ESI | DSTDEP_ESI, PAIR_UV | CYCLES_REG | SRCDEP_EDI | DSTDEP_EDI,
/*      POP EAX                                         POP ECX                                         POP EDX                                         POP EBX*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, PAIR_UV | CYCLES_REG | SRCDEP_ECX | DSTDEP_ECX, PAIR_UV | CYCLES_REG | SRCDEP_EDX | DSTDEP_EDX, PAIR_UV | CYCLES_REG | SRCDEP_EBX | DSTDEP_EBX,
/*      POP ESP                                         POP EBP                                         POP ESI                                         POP EDI*/
        PAIR_UV | CYCLES_REG | SRCDEP_ESP | DSTDEP_ESP, PAIR_UV | CYCLES_REG | SRCDEP_EBP | DSTDEP_EBP, PAIR_UV | CYCLES_REG | SRCDEP_ESI | DSTDEP_ESI, PAIR_UV | CYCLES_REG | SRCDEP_EDI | DSTDEP_EDI,

/*      PUSHA                                           POPA                                            BOUND                                           ARPL*/
/*60*/  PAIR_NP | CYCLES(5),                            PAIR_NP | CYCLES(5),                            PAIR_NP | CYCLES(8),                            PAIR_NP | CYCLES(7),
        INVALID,                                        INVALID,                                        INVALID,                                        INVALID,
/*      PUSH imm                                        IMUL                                            PUSH imm                                        IMUL*/
        PAIR_UV | CYCLES_REG,                           PAIR_NP | CYCLES(10),                           PAIR_UV | CYCLES_REG,                           PAIR_NP | CYCLES(10),
/*      INSB                                            INSW                                            OUTSB                                           OUTSW*/
        PAIR_NP | CYCLES(9),                            PAIR_NP | CYCLES(9),                            PAIR_NP | CYCLES(13),                           PAIR_NP | CYCLES(13),
        
/*      Jxx*/        
/*70*/  PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,
        PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,
        PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,
        PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,

/*80*/  INVALID,                                        INVALID,                                        INVALID,                                        INVALID,
/*      TEST                                            TEST                                            XCHG                                            XCHG*/
        PAIR_UV | CYCLES_RM | SRCDEP_REG,               PAIR_UV | CYCLES_RM | SRCDEP_REG,               PAIR_NP | CYCLES(3),                            PAIR_NP | CYCLES(3),
/*      MOV                                             MOV                                             MOV                                             MOV*/
        PAIR_UV | CYCLES_REG | SRCDEP_REG,              PAIR_UV | CYCLES_REG | SRCDEP_REG,              PAIR_UV | CYCLES_REG | DSTDEP_REG,              PAIR_UV | CYCLES_REG | DSTDEP_REG,
/*      MOV from seg                                    LEA                                             MOV to seg                                      POP*/
        PAIR_NP | CYCLES(1),                            PAIR_UV | CYCLES_REG | DSTDEP_REG,              CYCLES(3),                                      PAIR_NP | CYCLES(3),
        
/*      NOP                                             XCHG                                            XCHG                                            XCHG*/
/*90*/  PAIR_UV | CYCLES_REG,                           PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(2),
/*      XCHG                                            XCHG                                            XCHG                                            XCHG*/
        PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(2),
/*      CBW                                             CWD                                             CALL far                                        WAIT*/
        PAIR_NP | CYCLES(3),                            PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(0),                            PAIR_NP | CYCLES(1),
/*      PUSHF                                           POPF                                            SAHF                                            LAHF*/
        PAIR_NP | CYCLES(3),                            PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(2),

/*      MOV                                             MOV                                             MOV                                             MOV*/        
/*a0*/  PAIR_UV | CYCLES_REG | DSTDEP_EAX,              PAIR_UV | CYCLES_REG | DSTDEP_EAX,              PAIR_UV | CYCLES_REG | SRCDEP_EAX,              PAIR_UV | CYCLES_REG | SRCDEP_EAX,
/*      MOVSB                                           MOVSW                                           CMPSB                                           CMPSW*/
        PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(5),                            PAIR_NP | CYCLES(5),
/*      TEST                                            TEST                                            STOSB                                           STOSW*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX,              PAIR_UV | CYCLES_REG | SRCDEP_EAX,              PAIR_NP | CYCLES(3),                            PAIR_NP | CYCLES(3),
/*      LODSB                                           LODSW                                           SCASB                                           SCASW*/
        PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(4),

/*      MOV*/
/*b0*/  PAIR_UV | CYCLES_REG | DSTDEP_EAX,              PAIR_UV | CYCLES_REG | DSTDEP_ECX,              PAIR_UV | CYCLES_REG | DSTDEP_EDX,              PAIR_UV | CYCLES_REG | DSTDEP_EBX,
        PAIR_UV | CYCLES_REG | DSTDEP_EAX,              PAIR_UV | CYCLES_REG | DSTDEP_ECX,              PAIR_UV | CYCLES_REG | DSTDEP_EDX,              PAIR_UV | CYCLES_REG | DSTDEP_EBX,
        PAIR_UV | CYCLES_REG | DSTDEP_EAX,              PAIR_UV | CYCLES_REG | DSTDEP_ECX,              PAIR_UV | CYCLES_REG | DSTDEP_EDX,              PAIR_UV | CYCLES_REG | DSTDEP_EBX,
        PAIR_UV | CYCLES_REG | DSTDEP_ESP,              PAIR_UV | CYCLES_REG | DSTDEP_EBP,              PAIR_UV | CYCLES_REG | DSTDEP_ESI,              PAIR_UV | CYCLES_REG | DSTDEP_EDI,

/*                                                                                                      RET imm                                         RET*/
/*c0*/  INVALID,                                        INVALID,                                        PAIR_NP | CYCLES(3),                            PAIR_NP | CYCLES(2),
/*      LES                                             LDS                                             MOV                                             MOV*/
        PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(4),                            PAIR_UV | CYCLES_REG,                           PAIR_UV | CYCLES_REG,
/*      ENTER                                           LEAVE                                           RETF                                            RETF*/
        PAIR_NP | CYCLES(15),                           PAIR_NP | CYCLES(3),                            PAIR_NP | CYCLES(0),                            PAIR_NP | CYCLES(0),
/*      INT3                                            INT                                             INTO                                            IRET*/
        PAIR_NP | CYCLES(5),                            PAIR_NP | CYCLES(6),                            PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(0),


/*d0*/  INVALID,                                        INVALID,                                        INVALID,                                        INVALID,
/*      AAM                                             AAD                                             SETALC                                          XLAT*/
        PAIR_NP | CYCLES(18),                           PAIR_NP | CYCLES(10),                           PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(4),
        INVALID,                                        INVALID,                                        INVALID,                                        INVALID,        
        INVALID,                                        INVALID,                                        INVALID,                                        INVALID,
/*      LOOPNE                                          LOOPE                                           LOOP                                            JCXZ*/
/*e0*/  PAIR_NP | CYCLES(7),                            PAIR_NP | CYCLES(7),                            PAIR_NP | CYCLES(5),                            PAIR_NP | CYCLES(5),
/*      IN AL                                           IN AX                                           OUT_AL                                          OUT_AX*/
        PAIR_NP | CYCLES(7),                            PAIR_NP | CYCLES(7),                            PAIR_NP | CYCLES(12),                           PAIR_NP | CYCLES(12),
/*      CALL                                            JMP                                             JMP                                             JMP*/
        PAIR_V | CYCLES_REG,                            PAIR_V | CYCLES_REG,                            PAIR_NP | CYCLES(0),                            PAIR_V | CYCLES_REG,
/*      IN AL                                           IN AX                                           OUT_AL                                          OUT_AX*/
        PAIR_NP | CYCLES(7),                            PAIR_NP | CYCLES(7),                            PAIR_NP | CYCLES(12),                           PAIR_NP | CYCLES(12),

/*                                                                                                      REPNE                                           REPE*/
/*f0*/  INVALID,                                        INVALID,                                        PAIR_NP | CYCLES(0),                            PAIR_NP | CYCLES(0),
/*      HLT                                             CMC*/
        PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(2),                            INVALID,                                        INVALID,
/*      CLC                                             STC                                             CLI                                             STI*/
        PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(7),                            PAIR_NP | CYCLES(7),
/*      CLD                                             STD                                             INCDEC*/
        PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(2),                            PAIR_UV | CYCLES_RMW,                           INVALID
};

static uint64_t opcode_timings_mod3[256] =
{
/*      ADD                                                             ADD                                                             ADD                                                             ADD*/
/*00*/  PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,
/*      ADD                                                             ADD                                                             PUSH ES                                                         POP ES*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX,                 PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX,                 PAIR_NP | CYCLES(1),                                            PAIR_NP | CYCLES(3),
/*      OR                                                              OR                                                              OR                                                              OR*/
        PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,
/*      OR                                                              OR                                                              PUSH CS                                                         */
        PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX,                 PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX,                 PAIR_NP | CYCLES(1),                                            INVALID,

/*      ADC                                                             ADC                                                             ADC                                                             ADC*/
/*10*/  PAIR_U  | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_U  | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_U  | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_U  | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,
/*      ADC                                                             ADC                                                             PUSH SS                                                         POP SS*/
        PAIR_U  | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX,                 PAIR_U  | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX,                 PAIR_NP | CYCLES(1),                                            PAIR_NP | CYCLES(3),
/*      SBB                                                             SBB                                                             SBB                                                             SBB*/        
        PAIR_U  | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_U  | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_U  | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_U  | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,
/*      SBB                                                             SBB                                                             PUSH DS                                                         POP DS*/
        PAIR_U  | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX,                 PAIR_U  | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX,                 PAIR_NP | CYCLES(1),                                            PAIR_NP | CYCLES(3),

/*      AND                                                             AND                                                             AND                                                             AND*/
/*20*/  PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,
/*      AND                                                             AND                                                                                                                             DAA*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX,                 PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX,                 INVALID,                                                        PAIR_NP | CYCLES(3),
/*      SUB                                                             SUB                                                             SUB                                                             SUB*/
        PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,
/*      SUB                                                             SUB                                                                                                                             DAS*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX,                 PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX,                 INVALID,                                                        PAIR_NP | CYCLES(3),
        
/*      XOR                                                             XOR                                                             XOR                                                             XOR*/
/*30*/  PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,
/*      XOR                                                             XOR                                                                                                                             AAA*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX,                 PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX,                 INVALID,                                                        PAIR_NP | CYCLES(3),
/*      CMP                                                             CMP                                                             CMP                                                             CMP*/
        PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM,                  PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM,                  PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM,                  PAIR_UV | CYCLES_REG | SRCDEP_RM | SRCDEP_REG,
/*      CMP                                                             CMP                                                                                                                             AAS*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX,                              PAIR_UV | CYCLES_REG | SRCDEP_EAX,                              INVALID,                                                        PAIR_NP | CYCLES(3),

/*      INC EAX                                         INC ECX                                         INC EDX                                         INC EBX*/
/*40*/  PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, PAIR_UV | CYCLES_REG | SRCDEP_ECX | DSTDEP_ECX, PAIR_UV | CYCLES_REG | SRCDEP_EDX | DSTDEP_EDX, PAIR_UV | CYCLES_REG | SRCDEP_EBX | DSTDEP_EBX,
/*      INC ESP                                         INC EBP                                         INC ESI                                         INC EDI*/
        PAIR_UV | CYCLES_REG | SRCDEP_ESP | DSTDEP_ESP, PAIR_UV | CYCLES_REG | SRCDEP_EBP | DSTDEP_EBP, PAIR_UV | CYCLES_REG | SRCDEP_ESI | DSTDEP_ESI, PAIR_UV | CYCLES_REG | SRCDEP_EDI | DSTDEP_EDI,
/*      DEC EAX                                         DEC ECX                                         DEC EDX                                         DEC EBX*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, PAIR_UV | CYCLES_REG | SRCDEP_ECX | DSTDEP_ECX, PAIR_UV | CYCLES_REG | SRCDEP_EDX | DSTDEP_EDX, PAIR_UV | CYCLES_REG | SRCDEP_EBX | DSTDEP_EBX,
/*      DEC ESP                                         DEC EBP                                         DEC ESI                                         DEC EDI*/
        PAIR_UV | CYCLES_REG | SRCDEP_ESP | DSTDEP_ESP, PAIR_UV | CYCLES_REG | SRCDEP_EBP | DSTDEP_EBP, PAIR_UV | CYCLES_REG | SRCDEP_ESI | DSTDEP_ESI, PAIR_UV | CYCLES_REG | SRCDEP_EDI | DSTDEP_EDI,
        
/*      PUSH EAX                                        PUSH ECX                                        PUSH EDX                                        PUSH EBX*/
/*50*/  PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, PAIR_UV | CYCLES_REG | SRCDEP_ECX | DSTDEP_ECX, PAIR_UV | CYCLES_REG | SRCDEP_EDX | DSTDEP_EDX, PAIR_UV | CYCLES_REG | SRCDEP_EBX | DSTDEP_EBX,
/*      PUSH ESP                                        PUSH EBP                                        PUSH ESI                                        PUSH EDI*/
        PAIR_UV | CYCLES_REG | SRCDEP_ESP | DSTDEP_ESP, PAIR_UV | CYCLES_REG | SRCDEP_EBP | DSTDEP_EBP, PAIR_UV | CYCLES_REG | SRCDEP_ESI | DSTDEP_ESI, PAIR_UV | CYCLES_REG | SRCDEP_EDI | DSTDEP_EDI,
/*      POP EAX                                         POP ECX                                         POP EDX                                         POP EBX*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX | DSTDEP_EAX, PAIR_UV | CYCLES_REG | SRCDEP_ECX | DSTDEP_ECX, PAIR_UV | CYCLES_REG | SRCDEP_EDX | DSTDEP_EDX, PAIR_UV | CYCLES_REG | SRCDEP_EBX | DSTDEP_EBX,
/*      POP ESP                                         POP EBP                                         POP ESI                                         POP EDI*/
        PAIR_UV | CYCLES_REG | SRCDEP_ESP | DSTDEP_ESP, PAIR_UV | CYCLES_REG | SRCDEP_EBP | DSTDEP_EBP, PAIR_UV | CYCLES_REG | SRCDEP_ESI | DSTDEP_ESI, PAIR_UV | CYCLES_REG | SRCDEP_EDI | DSTDEP_EDI,

/*      PUSHA                                           POPA                                            BOUND                                           ARPL*/
/*60*/  PAIR_NP | CYCLES(5),                            PAIR_NP | CYCLES(5),                            PAIR_NP | CYCLES(8),                            PAIR_NP | CYCLES(7),
        INVALID,                                        INVALID,                                        INVALID,                                        INVALID,
/*      PUSH imm                                        IMUL                                            PUSH imm                                        IMUL*/
        PAIR_UV | CYCLES_REG,                           PAIR_NP | CYCLES(10),                           PAIR_UV | CYCLES_REG,                           PAIR_NP | CYCLES(10),
/*      INSB                                            INSW                                            OUTSB                                           OUTSW*/
        PAIR_NP | CYCLES(9),                            PAIR_NP | CYCLES(9),                            PAIR_NP | CYCLES(13),                           PAIR_NP | CYCLES(13),
        
/*      Jxx*/        
/*70*/  PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,
        PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,
        PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,
        PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,                         PAIR_V | CYCLES_BRANCH,

/*80*/  PAIR_UV | CYCLES_REG | SRCDEP_RM | DSTDEP_RM,   PAIR_UV | CYCLES_REG | SRCDEP_RM | DSTDEP_RM,   PAIR_UV | CYCLES_REG | SRCDEP_RM | DSTDEP_RM,   PAIR_UV | CYCLES_REG | SRCDEP_RM | DSTDEP_RM,
/*      TEST                                            TEST                                            XCHG                                            XCHG*/
        PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM,  PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM,  PAIR_NP | CYCLES(3),                            PAIR_NP | CYCLES(3),
/*      MOV                                             MOV                                             MOV                                             MOV*/
        PAIR_UV | CYCLES_REG | SRCDEP_REG | DSTDEP_RM,  PAIR_UV | CYCLES_REG | SRCDEP_REG | DSTDEP_RM,  PAIR_UV | CYCLES_REG | SRCDEP_RM | DSTDEP_REG,  PAIR_UV | CYCLES_REG | SRCDEP_RM | DSTDEP_REG,
/*      MOV from seg                                    LEA                                             MOV to seg                                      POP*/
        PAIR_NP | CYCLES(1),                            PAIR_UV | CYCLES_REG | DSTDEP_REG,              CYCLES(3),                                      PAIR_NP | CYCLES(3),
        
/*      NOP                                             XCHG                                            XCHG                                            XCHG*/
/*90*/  PAIR_UV | CYCLES_REG,                           PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(2),
/*      XCHG                                            XCHG                                            XCHG                                            XCHG*/
        PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(2),
/*      CBW                                             CWD                                             CALL far                                        WAIT*/
        PAIR_NP | CYCLES(3),                            PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(0),                            PAIR_NP | CYCLES(1),
/*      PUSHF                                           POPF                                            SAHF                                            LAHF*/
        PAIR_NP | CYCLES(3),                            PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(2),

/*      MOV                                             MOV                                             MOV                                             MOV*/        
/*a0*/  PAIR_UV | CYCLES_REG | DSTDEP_EAX,              PAIR_UV | CYCLES_REG | DSTDEP_EAX,              PAIR_UV | CYCLES_REG | SRCDEP_EAX,              PAIR_UV | CYCLES_REG | SRCDEP_EAX,
/*      MOVSB                                           MOVSW                                           CMPSB                                           CMPSW*/
        PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(5),                            PAIR_NP | CYCLES(5),
/*      TEST                                            TEST                                            STOSB                                           STOSW*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX,              PAIR_UV | CYCLES_REG | SRCDEP_EAX,              PAIR_NP | CYCLES(3),                            PAIR_NP | CYCLES(3),
/*      LODSB                                           LODSW                                           SCASB                                           SCASW*/
        PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(4),

/*      MOV*/
/*b0*/  PAIR_UV | CYCLES_REG | DSTDEP_EAX,              PAIR_UV | CYCLES_REG | DSTDEP_ECX,              PAIR_UV | CYCLES_REG | DSTDEP_EDX,              PAIR_UV | CYCLES_REG | DSTDEP_EBX,
        PAIR_UV | CYCLES_REG | DSTDEP_EAX,              PAIR_UV | CYCLES_REG | DSTDEP_ECX,              PAIR_UV | CYCLES_REG | DSTDEP_EDX,              PAIR_UV | CYCLES_REG | DSTDEP_EBX,
        PAIR_UV | CYCLES_REG | DSTDEP_EAX,              PAIR_UV | CYCLES_REG | DSTDEP_ECX,              PAIR_UV | CYCLES_REG | DSTDEP_EDX,              PAIR_UV | CYCLES_REG | DSTDEP_EBX,
        PAIR_UV | CYCLES_REG | DSTDEP_ESP,              PAIR_UV | CYCLES_REG | DSTDEP_EBP,              PAIR_UV | CYCLES_REG | DSTDEP_ESI,              PAIR_UV | CYCLES_REG | DSTDEP_EDI,

/*                                                                                                      RET imm                                         RET*/
/*c0*/  INVALID,                                        INVALID,                                        PAIR_NP | CYCLES(3),                            PAIR_NP | CYCLES(2),
/*      LES                                             LDS                                             MOV                                             MOV*/
        PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(4),                            PAIR_UV | CYCLES_REG,                           PAIR_UV | CYCLES_REG,
/*      ENTER                                           LEAVE                                           RETF                                            RETF*/
        PAIR_NP | CYCLES(15),                           PAIR_NP | CYCLES(3),                            PAIR_NP | CYCLES(0),                            PAIR_NP | CYCLES(0),
/*      INT3                                            INT                                             INTO                                            IRET*/
        PAIR_NP | CYCLES(5),                            PAIR_NP | CYCLES(6),                            PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(0),


/*d0*/  INVALID,                                        INVALID,                                        INVALID,                                        INVALID,
/*      AAM                                             AAD                                             SETALC                                          XLAT*/
        PAIR_NP | CYCLES(18),                           PAIR_NP | CYCLES(10),                           PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(4),
        INVALID,                                        INVALID,                                        INVALID,                                        INVALID,        
        INVALID,                                        INVALID,                                        INVALID,                                        INVALID,

/*      LOOPNE                                          LOOPE                                           LOOP                                            JCXZ*/
/*e0*/  PAIR_NP | CYCLES(7),                            PAIR_NP | CYCLES(7),                            PAIR_NP | CYCLES(5),                            PAIR_NP | CYCLES(5),
/*      IN AL                                           IN AX                                           OUT_AL                                          OUT_AX*/
        PAIR_NP | CYCLES(7),                            PAIR_NP | CYCLES(7),                            PAIR_NP | CYCLES(12),                           PAIR_NP | CYCLES(12),
/*      CALL                                            JMP                                             JMP                                             JMP*/
        PAIR_V | CYCLES_REG,                            PAIR_V | CYCLES_REG,                            PAIR_NP | CYCLES(0),                            PAIR_V | CYCLES_REG,
/*      IN AL                                           IN AX                                           OUT_AL                                          OUT_AX*/
        PAIR_NP | CYCLES(7),                            PAIR_NP | CYCLES(7),                            PAIR_NP | CYCLES(12),                           PAIR_NP | CYCLES(12),

/*                                                                                                      REPNE                                           REPE*/
/*f0*/  INVALID,                                        INVALID,                                        PAIR_NP | CYCLES(0),                            PAIR_NP | CYCLES(0),
/*      HLT                                             CMC*/
        PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(2),                            INVALID,                                        INVALID,
/*      CLC                                             STC                                             CLI                                             STI*/
        PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(7),                            PAIR_NP | CYCLES(7),
/*      CLD                                             STD                                             INCDEC*/
        PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(2),                            PAIR_UV | CYCLES_REG,                           INVALID
};

static uint64_t opcode_timings_0f[256] =
{
/*00*/  PAIR_NP | CYCLES(20),   PAIR_NP | CYCLES(11),           PAIR_NP | CYCLES(11),           PAIR_NP | CYCLES(10),
        INVALID,                PAIR_NP | CYCLES(195),          PAIR_NP | CYCLES(7),            INVALID,
        PAIR_NP | CYCLES(1000), PAIR_NP | CYCLES(10000),        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,

/*10*/  INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        
/*20*/  PAIR_NP | CYCLES(6),    PAIR_NP | CYCLES(6),            PAIR_NP | CYCLES(6),            PAIR_NP | CYCLES(6),
        PAIR_NP | CYCLES(6),    PAIR_NP | CYCLES(6),            INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        
/*30*/  PAIR_NP | CYCLES(9),    CYCLES(1),                      PAIR_NP | CYCLES(9),            INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,

/*40*/  INVALID,                INVALID,                        INVALID,                        INVALID,        
        INVALID,                INVALID,                        INVALID,                        INVALID,        
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        
/*50*/  INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        
/*60*/  PAIR_U | MMX_SHIFTPACK | CYCLES_RM,     PAIR_U | MMX_SHIFTPACK | CYCLES_RM,     PAIR_U | MMX_SHIFTPACK | CYCLES_RM,     PAIR_U | MMX_SHIFTPACK | CYCLES_RM,
        PAIR_U | CYCLES_RM,                     PAIR_U | CYCLES_RM,                     PAIR_U | CYCLES_RM,                     PAIR_U | CYCLES_RM,
        PAIR_U | MMX_SHIFTPACK | CYCLES_RM,     PAIR_U | MMX_SHIFTPACK | CYCLES_RM,     PAIR_U | MMX_SHIFTPACK | CYCLES_RM,     PAIR_U | MMX_SHIFTPACK | CYCLES_RM,
        INVALID,                                INVALID,                                PAIR_U | CYCLES_RM,                     PAIR_U | CYCLES_RM,
        
/*70*/  INVALID,                                PAIR_U | MMX_SHIFTPACK | CYCLES_RM,     PAIR_U | MMX_SHIFTPACK | CYCLES_RM,     PAIR_U | MMX_SHIFTPACK | CYCLES_RM,
        PAIR_U | CYCLES_RM,                     PAIR_U | CYCLES_RM,                     PAIR_U | CYCLES_RM,                     PAIR_NP | CYCLES(100),
        INVALID,                                INVALID,                                INVALID,                                INVALID,
        INVALID,                                INVALID,                                PAIR_U | CYCLES_RM,                     PAIR_U | CYCLES_RM,

/*80*/  PAIR_NP | CYCLES(2),    PAIR_NP | CYCLES(2),            PAIR_NP | CYCLES(2),            PAIR_NP | CYCLES(2),
        PAIR_NP | CYCLES(2),    PAIR_NP | CYCLES(2),            PAIR_NP | CYCLES(2),            PAIR_NP | CYCLES(2),
        PAIR_NP | CYCLES(2),    PAIR_NP | CYCLES(2),            PAIR_NP | CYCLES(2),            PAIR_NP | CYCLES(2),
        PAIR_NP | CYCLES(2),    PAIR_NP | CYCLES(2),            PAIR_NP | CYCLES(2),            PAIR_NP | CYCLES(2),
        
/*90*/  PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),
        PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),
        PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),
        PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),
        
/*a0*/  PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(14),           PAIR_NP | CYCLES(8),
        PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(4),            INVALID,                        INVALID,
        PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(3),            INVALID,                        PAIR_NP | CYCLES(13),
        PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(3),            INVALID,                        PAIR_NP | CYCLES(10),
        
/*b0*/  PAIR_NP | CYCLES(10),   PAIR_NP | CYCLES(10),           PAIR_NP | CYCLES(6),            PAIR_NP | CYCLES(13),
        PAIR_NP | CYCLES(6),    PAIR_NP | CYCLES(6),            PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),
        INVALID,                INVALID,                        PAIR_NP | CYCLES(6),            PAIR_NP | CYCLES(13),
        PAIR_NP | CYCLES(7),    PAIR_NP | CYCLES(7),            PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),

/*c0*/  PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(4),            INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        PAIR_NP | CYCLES(1),    PAIR_NP | CYCLES(1),            PAIR_NP | CYCLES(1),            PAIR_NP | CYCLES(1),
        PAIR_NP | CYCLES(1),    PAIR_NP | CYCLES(1),            PAIR_NP | CYCLES(1),            PAIR_NP | CYCLES(1),

/*d0*/  INVALID,                PAIR_U | MMX_SHIFTPACK | CYCLES_RM,     PAIR_U | MMX_SHIFTPACK | CYCLES_RM,     PAIR_U | MMX_SHIFTPACK | CYCLES_RM,
        INVALID,                PAIR_U | CYCLES_RM,                     INVALID,                                INVALID,
        PAIR_U | CYCLES_RM,     PAIR_U | CYCLES_RM,                     INVALID,                                PAIR_U | CYCLES_RM,
        PAIR_U | CYCLES_RM,     PAIR_U | CYCLES_RM,                     INVALID,                                PAIR_U | CYCLES_RM,
        
/*e0*/  INVALID,                PAIR_U | MMX_SHIFTPACK | CYCLES_RM,     PAIR_U | MMX_SHIFTPACK | CYCLES_RM,     INVALID,
        INVALID,                PAIR_U | CYCLES_RM,                     INVALID,                                INVALID,
        PAIR_U | CYCLES_RM,     PAIR_U | CYCLES_RM,                     INVALID,                                PAIR_U | CYCLES_RM,
        PAIR_U | CYCLES_RM,     PAIR_U | CYCLES_RM,                     INVALID,                                PAIR_U | CYCLES_RM,
        
/*f0*/  INVALID,                PAIR_U | MMX_SHIFTPACK | CYCLES_RM,     PAIR_U | MMX_SHIFTPACK | CYCLES_RM,     PAIR_U | MMX_SHIFTPACK | CYCLES_RM,
        INVALID,                PAIR_U | CYCLES_RM,                     INVALID,                                INVALID,
        PAIR_U | CYCLES_RM,     PAIR_U | CYCLES_RM,                     PAIR_U | CYCLES_RM,                     INVALID,
        PAIR_U | CYCLES_RM,     PAIR_U | CYCLES_RM,                     PAIR_U | CYCLES_RM,                     INVALID,
};
static uint64_t opcode_timings_0f_mod3[256] =
{
/*00*/  PAIR_NP | CYCLES(20),   PAIR_NP | CYCLES(11),           PAIR_NP | CYCLES(11),           PAIR_NP | CYCLES(10),
        INVALID,                PAIR_NP | CYCLES(195),          PAIR_NP | CYCLES(7),            INVALID,
        PAIR_NP | CYCLES(1000), PAIR_NP | CYCLES(10000),        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        
/*10*/  INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,

/*20*/  PAIR_NP | CYCLES(6),    PAIR_NP | CYCLES(6),            PAIR_NP | CYCLES(6),            PAIR_NP | CYCLES(6),
        PAIR_NP | CYCLES(6),    PAIR_NP | CYCLES(6),            INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        
/*30*/  PAIR_NP | CYCLES(9),    PAIR_NP | CYCLES(1),            PAIR_NP | CYCLES(9),            INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,

/*40*/  INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        
/*50*/  INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        
/*60*/  PAIR_UV | MMX_SHIFTPACK | CYCLES_REG,   PAIR_UV | MMX_SHIFTPACK | CYCLES_REG,   PAIR_UV | MMX_SHIFTPACK | CYCLES_REG,   PAIR_UV | MMX_SHIFTPACK | CYCLES_REG,
        PAIR_UV | CYCLES_REG,                   PAIR_UV | CYCLES_REG,                   PAIR_UV | CYCLES_REG,                   PAIR_UV | CYCLES_REG,
        PAIR_UV | MMX_SHIFTPACK | CYCLES_REG,   PAIR_UV | MMX_SHIFTPACK | CYCLES_REG,   PAIR_UV | MMX_SHIFTPACK | CYCLES_REG,   PAIR_UV | MMX_SHIFTPACK | CYCLES_REG,
        INVALID,                                INVALID,                                PAIR_UV | CYCLES_REG,                   PAIR_UV | CYCLES_REG,
        
/*70*/  INVALID,                PAIR_UV | MMX_SHIFTPACK | CYCLES_REG,   PAIR_UV | MMX_SHIFTPACK | CYCLES_REG,   PAIR_UV | MMX_SHIFTPACK | CYCLES_REG,
        PAIR_UV | CYCLES_REG,   PAIR_UV | CYCLES_REG,                   PAIR_UV | CYCLES_REG,                   PAIR_NP | CYCLES(100),
        INVALID,                INVALID,                                INVALID,                                INVALID,
        INVALID,                INVALID,                                PAIR_UV | CYCLES_REG,                   PAIR_UV | CYCLES_REG,

/*80*/  PAIR_NP | CYCLES(2),    PAIR_NP | CYCLES(2),            PAIR_NP | CYCLES(2),            PAIR_NP | CYCLES(2),
        PAIR_NP | CYCLES(2),    PAIR_NP | CYCLES(2),            PAIR_NP | CYCLES(2),            PAIR_NP | CYCLES(2),
        PAIR_NP | CYCLES(2),    PAIR_NP | CYCLES(2),            PAIR_NP | CYCLES(2),            PAIR_NP | CYCLES(2),
        PAIR_NP | CYCLES(2),    PAIR_NP | CYCLES(2),            PAIR_NP | CYCLES(2),            PAIR_NP | CYCLES(2),

/*90*/  PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),
        PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),
        PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),
        PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),
        
/*a0*/  PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(14),           PAIR_NP | CYCLES(8),
        PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(4),            INVALID,                        INVALID,
        PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(3),            INVALID,                        PAIR_NP | CYCLES(13),
        PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(3),            INVALID,                        PAIR_NP | CYCLES(10),
        
/*b0*/  PAIR_NP | CYCLES(10),   PAIR_NP | CYCLES(10),           PAIR_NP | CYCLES(6),            PAIR_NP | CYCLES(13),
        PAIR_NP | CYCLES(6),    PAIR_NP | CYCLES(6),            PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),
        INVALID,                INVALID,                        PAIR_NP | CYCLES(6),            PAIR_NP | CYCLES(13),
        PAIR_NP | CYCLES(7),    PAIR_NP | CYCLES(7),            PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),

/*c0*/  PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(4),            INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        PAIR_NP | CYCLES(1),    PAIR_NP | CYCLES(1),            PAIR_NP | CYCLES(1),            PAIR_NP | CYCLES(1),
        PAIR_NP | CYCLES(1),    PAIR_NP | CYCLES(1),            PAIR_NP | CYCLES(1),            PAIR_NP | CYCLES(1),
        
/*d0*/  INVALID,                PAIR_UV | MMX_SHIFTPACK | CYCLES_REG,   PAIR_UV | MMX_SHIFTPACK | CYCLES_REG,   PAIR_UV | MMX_SHIFTPACK | CYCLES_REG,
        INVALID,                PAIR_UV | MMX_MULTIPLY | CYCLES_REG,    INVALID,                                INVALID,
        PAIR_UV | CYCLES_REG,   PAIR_UV | CYCLES_REG,                   INVALID,                                PAIR_UV | CYCLES_REG,
        PAIR_UV | CYCLES_REG,   PAIR_UV | CYCLES_REG,                   INVALID,                                PAIR_UV | CYCLES_REG,
        
/*e0*/  INVALID,                PAIR_UV | MMX_SHIFTPACK | CYCLES_REG,   PAIR_UV | MMX_SHIFTPACK | CYCLES_REG,   INVALID,
        INVALID,                PAIR_UV | MMX_MULTIPLY | CYCLES_REG,    INVALID,                                INVALID,
        PAIR_UV | CYCLES_REG,   PAIR_UV | CYCLES_REG,                   INVALID,                                PAIR_UV | CYCLES_REG,
        PAIR_UV | CYCLES_REG,   PAIR_UV | CYCLES_REG,                   INVALID,                                PAIR_UV | CYCLES_REG,   
        
/*f0*/  INVALID,                PAIR_UV | MMX_SHIFTPACK | CYCLES_REG,   PAIR_UV | MMX_SHIFTPACK | CYCLES_REG,   PAIR_UV | MMX_SHIFTPACK | CYCLES_REG,
        INVALID,                PAIR_UV | MMX_MULTIPLY | CYCLES_REG,    INVALID,                                INVALID,
        PAIR_UV | CYCLES_REG,   PAIR_UV | CYCLES_REG,                   PAIR_UV | CYCLES_REG,                   INVALID,
        PAIR_UV | CYCLES_REG,   PAIR_UV | CYCLES_REG,                   PAIR_UV | CYCLES_REG,                   INVALID,
};

static uint64_t opcode_timings_shift[8] =
{
        PAIR_U | CYCLES_RMW,    PAIR_U | CYCLES_RMW,    PAIR_U | CYCLES_RMW,    PAIR_U | CYCLES_RMW,
        PAIR_U | CYCLES_RMW,    PAIR_U | CYCLES_RMW,    PAIR_U | CYCLES_RMW,    PAIR_U | CYCLES_RMW,
};
static uint64_t opcode_timings_shift_mod3[8] =
{
        PAIR_U | CYCLES_REG | DSTDEP_RM,    PAIR_U | CYCLES_REG | DSTDEP_RM,    PAIR_U | CYCLES_REG | DSTDEP_RM,    PAIR_U | CYCLES_REG | DSTDEP_RM,
        PAIR_U | CYCLES_REG | DSTDEP_RM,    PAIR_U | CYCLES_REG | DSTDEP_RM,    PAIR_U | CYCLES_REG | DSTDEP_RM,    PAIR_U | CYCLES_REG | DSTDEP_RM,
};

static uint64_t opcode_timings_f6[8] =
{
/*      TST                                             NOT                     NEG*/
        PAIR_UV | CYCLES_RM,    INVALID,                PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(3),
/*      MUL                     IMUL                    DIV                     IDIV*/
        PAIR_NP | CYCLES(11),   PAIR_NP | CYCLES(11),   PAIR_NP | CYCLES(17),   PAIR_NP | CYCLES(22)
};
static uint64_t opcode_timings_f6_mod3[8] =
{
/*      TST                                             NOT                     NEG*/
        PAIR_UV | CYCLES_REG | SRCDEP_RM,               INVALID,                PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),
/*      MUL                     IMUL                    DIV                     IDIV*/
        PAIR_NP | CYCLES(11),   PAIR_NP | CYCLES(11),   PAIR_NP | CYCLES(17),   PAIR_NP | CYCLES(22)
};
static uint64_t opcode_timings_f7[8] =
{
/*      TST                                                                     NOT                             NEG*/
        PAIR_UV | CYCLES_RM,                    INVALID,                        PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),
/*      MUL                                     IMUL                            DIV                             IDIV*/
        PAIR_NP | CYCLES_MULTI(11,10),          PAIR_NP | CYCLES_MULTI(11,10),  PAIR_NP | CYCLES_MULTI(25,41),  PAIR_NP | CYCLES_MULTI(30,46)
};
static uint64_t opcode_timings_f7_mod3[8] =
{
/*      TST                                                                     NOT                             NEG*/
        PAIR_UV | CYCLES_REG | SRCDEP_RM,       INVALID,                        PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),
/*      MUL                                     IMUL                            DIV                             IDIV*/
        PAIR_NP | CYCLES_MULTI(11,10),          PAIR_NP | CYCLES_MULTI(11,10),  PAIR_NP | CYCLES_MULTI(25,41),  PAIR_NP | CYCLES_MULTI(30,46)
};
static uint64_t opcode_timings_ff[8] =
{
/*      INC                     DEC                     CALL                    CALL far*/
        PAIR_UV | CYCLES_RMW,   PAIR_UV | CYCLES_RMW,   PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(0),
/*      JMP                     JMP far                 PUSH*/
        PAIR_NP | CYCLES(2),    PAIR_NP | CYCLES(0),    PAIR_NP | CYCLES(2),    INVALID
};
static uint64_t opcode_timings_ff_mod3[8] =
{
/*      INC                                             DEC                                             CALL                    CALL far*/
        PAIR_UV | CYCLES_REG | SRCDEP_RM | DSTDEP_RM,   PAIR_UV | CYCLES_REG | SRCDEP_RM | DSTDEP_RM,   PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(0),
/*      JMP                     JMP far                 PUSH*/
        PAIR_NP | CYCLES(2),    PAIR_NP | CYCLES(0),    PAIR_NP | CYCLES(2),    INVALID
};

static uint64_t opcode_timings_d8[8] =
{
/*      FADDs                                        FMULs                                      FCOMs                                        FCOMPs*/
        PAIR_FX | FPU_RW_ST0 | FPU_CYCLES(3,2,2),    PAIR_FX | FPU_RW_ST0 | FPU_CYCLES(3,2,2),  PAIR_FX | FPU_READ_ST0 | FPU_CYCLES(1,0,0),  PAIR_FX | FPU_POP | FPU_READ_ST0 | FPU_CYCLES(1,0,0),
/*      FSUBs                                        FSUBRs                                     FDIVs                                        FDIVRs*/
        PAIR_FX | FPU_RW_ST0 | FPU_CYCLES(3,2,2),    PAIR_FX | FPU_RW_ST0 | FPU_CYCLES(3,2,2),  PAIR_FX | FPU_RW_ST0 | FPU_CYCLES(39,38,2),  PAIR_FX | FPU_RW_ST0 | FPU_CYCLES(39,38,2)
};
static uint64_t opcode_timings_d8_mod3[8] =
{
/*      FADD                                                          FMUL                                                          FCOM                                                          FCOMP*/
        PAIR_FX | FPU_RW_ST0 | FPU_READ_STREG | FPU_CYCLES(3,2,2),    PAIR_FX | FPU_RW_ST0 | FPU_READ_STREG | FPU_CYCLES(3,2,2),    PAIR_FX | FPU_READ_ST0 | FPU_READ_STREG | FPU_CYCLES(1,0,0),  PAIR_FX | FPU_POP | FPU_READ_ST0 | FPU_READ_STREG | FPU_CYCLES(1,0,0),
/*      FSUB                                                          FSUBR                                                         FDIV                                                          FDIVR*/
        PAIR_FX | FPU_RW_ST0 | FPU_READ_STREG | FPU_CYCLES(3,2,2),    PAIR_FX | FPU_RW_ST0 | FPU_READ_STREG | FPU_CYCLES(3,2,2),    PAIR_FX | FPU_RW_ST0 | FPU_READ_STREG | FPU_CYCLES(39,38,2),  PAIR_FX | FPU_RW_ST0 | FPU_READ_STREG | FPU_CYCLES(39,38,2)
};

static uint64_t opcode_timings_d9[8] =
{
/*      FLDs                                                                  FSTs                                        FSTPs*/
        PAIR_FX | FPU_PUSH | FPU_CYCLES(1,0,0), INVALID,                      PAIR_NP | FPU_READ_ST0 | FPU_CYCLES(2,0,0), PAIR_NP | FPU_READ_ST0 | FPU_POP | FPU_CYCLES(2,0,0),
/*      FLDENV                                  FLDCW                         FSTENV                                      FSTCW*/
        PAIR_NP | FPU_CYCLES(32,0,0),           PAIR_NP | FPU_CYCLES(8,0,0),  PAIR_NP | FPU_CYCLES(48,0,0),               PAIR_NP | FPU_CYCLES(2,0,0)
};
static uint64_t opcode_timings_d9_mod3[64] =
{
        /*FLD*/
        PAIR_FX | FPU_PUSH | FPU_READ_STREG | FPU_CYCLES(1,0,0),  PAIR_FX | FPU_PUSH | FPU_READ_STREG | FPU_CYCLES(1,0,0),  PAIR_FX | FPU_PUSH | FPU_READ_STREG | FPU_CYCLES(1,0,0),  PAIR_FX | FPU_PUSH | FPU_READ_STREG | FPU_CYCLES(1,0,0),
        PAIR_FX | FPU_PUSH | FPU_READ_STREG | FPU_CYCLES(1,0,0),  PAIR_FX | FPU_PUSH | FPU_READ_STREG | FPU_CYCLES(1,0,0),  PAIR_FX | FPU_PUSH | FPU_READ_STREG | FPU_CYCLES(1,0,0),  PAIR_FX | FPU_PUSH | FPU_READ_STREG | FPU_CYCLES(1,0,0),
        /*FXCH*/
        PAIR_FXCH | FPU_FXCH | CYCLES(0),                 PAIR_FXCH | FPU_FXCH | CYCLES(0),                 PAIR_FXCH | FPU_FXCH | CYCLES(0),                 PAIR_FXCH | FPU_FXCH | CYCLES(0),
        PAIR_FXCH | FPU_FXCH | CYCLES(0),                 PAIR_FXCH | FPU_FXCH | CYCLES(0),                 PAIR_FXCH | FPU_FXCH | CYCLES(0),                 PAIR_FXCH | FPU_FXCH | CYCLES(0),
        /*FNOP*/
        PAIR_NP | FPU_CYCLES(3,0,0),                      INVALID,                                          INVALID,                                          INVALID,
        INVALID,                                          INVALID,                                          INVALID,                                          INVALID,        
        /*FSTP*/
        PAIR_NP | FPU_READ_ST0 | FPU_WRITE_STREG | FPU_POP | FPU_CYCLES(1,0,0),  PAIR_NP | FPU_READ_ST0 | FPU_WRITE_STREG | FPU_POP | FPU_CYCLES(1,0,0),  PAIR_NP | FPU_READ_ST0 | FPU_WRITE_STREG | FPU_POP | FPU_CYCLES(1,0,0),  PAIR_NP | FPU_READ_ST0 | FPU_WRITE_STREG | FPU_POP | FPU_CYCLES(1,0,0),
        PAIR_NP | FPU_READ_ST0 | FPU_WRITE_STREG | FPU_POP | FPU_CYCLES(1,0,0),  PAIR_NP | FPU_READ_ST0 | FPU_WRITE_STREG | FPU_POP | FPU_CYCLES(1,0,0),  PAIR_NP | FPU_READ_ST0 | FPU_WRITE_STREG | FPU_POP | FPU_CYCLES(1,0,0),  PAIR_NP | FPU_READ_ST0 | FPU_WRITE_STREG | FPU_POP | FPU_CYCLES(1,0,0),
/*      opFCHS                                            opFABS*/
        PAIR_FX | FPU_CYCLES(1,0,0),                      PAIR_FX | FPU_CYCLES(1,0,0),                      INVALID,                                          INVALID,
/*      opFTST                                            opFXAM*/
        PAIR_NP | FPU_CYCLES(1,0,0),                      PAIR_NP | FPU_CYCLES(21,4,0),                     INVALID,                                          INVALID,
/*      opFLD1                                            opFLDL2T                                          opFLDL2E                                          opFLDPI*/
        PAIR_NP | FPU_PUSH | FPU_CYCLES(2,0,0),           PAIR_NP | FPU_PUSH | FPU_CYCLES(5,2,2),           PAIR_NP | FPU_PUSH | FPU_CYCLES(5,2,2),           PAIR_NP | FPU_PUSH | FPU_CYCLES(5,2,2),
/*      opFLDEG2                                          opFLDLN2                                          opFLDZ*/
        PAIR_NP | FPU_PUSH | FPU_CYCLES(5,2,2),           PAIR_NP | FPU_PUSH | FPU_CYCLES(5,2,2),           PAIR_NP | FPU_PUSH | FPU_CYCLES(2,0,0),           INVALID,
/*      opF2XM1                                           opFYL2X                                           opFPTAN                                           opFPATAN*/
        PAIR_NP | FPU_CYCLES(53,2,2),                     PAIR_NP | FPU_CYCLES(103,2,2),                    PAIR_NP | FPU_CYCLES(120,36,0),                   PAIR_NP | FPU_CYCLES(112,2,2),
/*                                                                                                          opFDECSTP                                         opFINCSTP,*/
        INVALID,                                          INVALID,                                          PAIR_NP | FPU_CYCLES(2,0,0),                      PAIR_NP | FPU_CYCLES(2,0,0),
/*      opFPREM                                                                                             opFSQRT                                           opFSINCOS*/
        PAIR_NP | FPU_CYCLES(64,2,2),                     INVALID,                                          PAIR_NP | FPU_CYCLES(70,69,2),                    PAIR_NP | FPU_CYCLES(89,2,2),
/*      opFRNDINT                                         opFSCALE                                          opFSIN                                            opFCOS*/
        PAIR_NP | FPU_CYCLES(9,0,0),                      PAIR_NP | FPU_CYCLES(20,5,0),                     PAIR_NP | FPU_CYCLES(65,2,2),                     PAIR_NP | FPU_CYCLES(65,2,2)
};

static uint64_t opcode_timings_da[8] =
{
/*      FIADDl                                       FIMULl                                       FICOMl                                        FICOMPl*/
        PAIR_NP | FPU_RW_ST0 | FPU_CYCLES(6,2,2),    PAIR_NP | FPU_RW_ST0 | FPU_CYCLES(6,2,2),    PAIR_NP | FPU_READ_ST0 | FPU_CYCLES(4,0,0),   PAIR_NP | FPU_READ_ST0 | FPU_POP | FPU_CYCLES(4,0,0),
/*      FISUBl                                       FISUBRl                                      FIDIVl                                        FIDIVRl*/
        PAIR_NP | FPU_RW_ST0 | FPU_CYCLES(6,2,2),    PAIR_NP | FPU_RW_ST0 | FPU_CYCLES(6,2,2),    PAIR_NP | FPU_RW_ST0 | FPU_CYCLES(42,38,2),   PAIR_NP | FPU_RW_ST0 | FPU_CYCLES(42,38,2)
};
static uint64_t opcode_timings_da_mod3[8] =
{
        INVALID,                INVALID,                                   INVALID,                INVALID,
/*                              FCOMPP*/
        INVALID,                PAIR_NP | FPU_POP2 | FPU_CYCLES(1,0,0),    INVALID,                INVALID
};


static uint64_t opcode_timings_db[8] =
{
/*      FLDil                                                                                 FSTil                                          FSTPil*/
        PAIR_NP | FPU_PUSH | FPU_CYCLES(3,2,2),    INVALID,                                   PAIR_NP | FPU_READ_ST0 | FPU_CYCLES(6,0,0),    PAIR_NP | FPU_READ_ST0 | FPU_POP | FPU_CYCLES(6,0,0),
/*                                                 FLDe                                                                                      FSTPe*/
        INVALID,                                   PAIR_NP | FPU_PUSH | FPU_CYCLES(3,0,0),    INVALID,                                       PAIR_NP | FPU_READ_ST0 | FPU_POP | FPU_CYCLES(3,0,0)
};
static uint64_t opcode_timings_db_mod3[64] =
{
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        
/*                                      opFNOP                          opFCLEX                         opFINIT*/
        INVALID,                        PAIR_NP | FPU_CYCLES(1,0,0),    PAIR_NP | FPU_CYCLES(7,0,0),    PAIR_NP | FPU_CYCLES(17,0,0),
/*      opFNOP                          opFNOP*/
        PAIR_NP | FPU_CYCLES(1,0,0),    PAIR_NP | FPU_CYCLES(1,0,0),    INVALID,                        INVALID,
        
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
};

static uint64_t opcode_timings_dc[8] =
{
/*      FADDd                                        FMULd                                        FCOMd                                          FCOMPd*/
        PAIR_FX | FPU_RW_ST0 | FPU_CYCLES(3,2,2),    PAIR_FX | FPU_RW_ST0 | FPU_CYCLES(3,2,2),    PAIR_FX | FPU_READ_ST0 | FPU_CYCLES(1,0,0),    PAIR_FX | FPU_READ_ST0 | FPU_POP | FPU_CYCLES(1,0,0),
/*      FSUBd                                        FSUBRd                                       FDIVd                                          FDIVRd*/
        PAIR_FX | FPU_RW_ST0 | FPU_CYCLES(3,2,2),    PAIR_FX | FPU_RW_ST0 | FPU_CYCLES(3,2,2),    PAIR_FX | FPU_RW_ST0 | FPU_CYCLES(39,38,2),    PAIR_FX | FPU_RW_ST0 | FPU_CYCLES(39,38,2)
};
static uint64_t opcode_timings_dc_mod3[8] =
{
/*      opFADDr                                                       opFMULr*/
        PAIR_FX | FPU_READ_ST0 | FPU_RW_STREG | FPU_CYCLES(3,2,2),    PAIR_FX | FPU_READ_ST0 | FPU_RW_STREG | FPU_CYCLES(3,2,2),    INVALID,                                                       INVALID,
/*      opFSUBRr                                                      opFSUBr                                                       opFDIVRr                                                       opFDIVr*/
        PAIR_FX | FPU_READ_ST0 | FPU_RW_STREG | FPU_CYCLES(3,2,2),    PAIR_FX | FPU_READ_ST0 | FPU_RW_STREG | FPU_CYCLES(3,2,2),    PAIR_FX | FPU_READ_ST0 | FPU_RW_STREG | FPU_CYCLES(39,38,2),   PAIR_FX | FPU_READ_ST0 | FPU_RW_STREG | FPU_CYCLES(39,38,2)
};

static uint64_t opcode_timings_dd[8] =
{
/*      FLDd                                                               FSTd                                           FSTPd*/
        PAIR_FX | FPU_PUSH | FPU_CYCLES(1,0,0),    INVALID,                PAIR_NP | FPU_READ_ST0 | FPU_CYCLES(2,0,0),    PAIR_NP | FPU_READ_ST0 | FPU_POP | FPU_CYCLES(2,0,0),
/*      FRSTOR                                                             FSAVE                                          FSTSW*/
        PAIR_NP | FPU_CYCLES(70,0,0),              INVALID,                PAIR_NP | FPU_CYCLES(127,0,0),                 PAIR_NP | FPU_CYCLES(6,0,0)
};
static uint64_t opcode_timings_dd_mod3[8] =
{
/*      FFFREE                                                                                                                                  FST                                                              FSTP*/
        PAIR_NP | FPU_CYCLES(2,0,0),                                    INVALID,                                                                PAIR_NP | FPU_READ_ST0 | FPU_WRITE_STREG | FPU_CYCLES(1,0,0),    PAIR_NP | FPU_READ_ST0 | FPU_WRITE_STREG | FPU_POP | FPU_CYCLES(1,0,0),
/*      FUCOM                                                           FUCOMP*/
        PAIR_NP | FPU_READ_ST0 | FPU_READ_STREG | FPU_CYCLES(1,0,0),    PAIR_NP | FPU_READ_ST0 | FPU_READ_STREG | FPU_POP | FPU_CYCLES(1,0,0),  INVALID,                                                         INVALID
};

static uint64_t opcode_timings_de[8] =
{
/*      FIADDw                                       FIMULw                                       FICOMw                                        FICOMPw*/
        PAIR_NP | FPU_RW_ST0 | FPU_CYCLES(6,2,2),    PAIR_NP | FPU_RW_ST0 | FPU_CYCLES(6,2,2),    PAIR_NP | FPU_READ_ST0 | FPU_CYCLES(4,0,0),   PAIR_NP | FPU_READ_ST0 | FPU_POP | FPU_CYCLES(4,0,0),
/*      FISUBw                                       FISUBRw                                      FIDIVw                                        FIDIVRw*/
        PAIR_NP | FPU_RW_ST0 | FPU_CYCLES(6,2,2),    PAIR_NP | FPU_RW_ST0 | FPU_CYCLES(6,2,2),    PAIR_NP | FPU_RW_ST0 | FPU_CYCLES(42,38,2),   PAIR_NP | FPU_RW_ST0 | FPU_CYCLES(42,38,2)
};
static uint64_t opcode_timings_de_mod3[8] =
{
/*      FADDP                                                                 FMULP                                                                                                                                          FCOMPP*/
        PAIR_FX | FPU_READ_ST0 | FPU_RW_STREG | FPU_POP | FPU_CYCLES(3,2,2),  PAIR_FX | FPU_READ_ST0 | FPU_RW_STREG | FPU_POP | FPU_CYCLES(3,2,2),   INVALID,                                                                PAIR_FX | FPU_READ_ST0 | FPU_READ_ST1 | FPU_POP2 | FPU_CYCLES(1,0,0),
/*      FSUBP                                                                 FSUBRP                                                                 FDIVP                                                                   FDIVRP*/
        PAIR_FX | FPU_READ_ST0 | FPU_RW_STREG | FPU_POP | FPU_CYCLES(3,2,2),  PAIR_FX | FPU_READ_ST0 | FPU_RW_STREG | FPU_POP | FPU_CYCLES(3,2,2),   PAIR_FX | FPU_READ_ST0 | FPU_RW_STREG | FPU_POP | FPU_CYCLES(39,38,2),  PAIR_FX | FPU_READ_ST0 | FPU_RW_STREG | FPU_POP | FPU_CYCLES(39,38,2)
};

static uint64_t opcode_timings_df[8] =
{
/*      FILDiw                                                                                FISTiw                                                   FISTPiw*/
        PAIR_NP | FPU_PUSH | FPU_CYCLES(3,2,2),    INVALID,                                   PAIR_NP | FPU_READ_ST0 | FPU_CYCLES(6,0,0),              PAIR_NP | FPU_READ_ST0 | FPU_POP | FPU_CYCLES(6,0,0),
/*                                                 FILDiq                                     FBSTP                                                    FISTPiq*/
        INVALID,                                   PAIR_NP | FPU_PUSH | FPU_CYCLES(3,2,2),    PAIR_NP | FPU_READ_ST0 | FPU_POP | FPU_CYCLES(148,0,0),  PAIR_NP | FPU_READ_ST0 | FPU_POP | FPU_CYCLES(6,0,0)
};
static uint64_t opcode_timings_df_mod3[8] =
{
        INVALID,                        INVALID,                INVALID,                INVALID,
/*      FSTSW AX*/
        PAIR_NP | FPU_CYCLES(6,0,0),    INVALID,                INVALID,                INVALID
};

static uint64_t opcode_timings_81[8] =
{
        PAIR_UV | CYCLES_RMW | SRCDEP_REG | CYCLES_IMM1632,     PAIR_UV | CYCLES_RMW | SRCDEP_REG | CYCLES_IMM1632,     PAIR_UV | CYCLES_RMW | SRCDEP_REG | CYCLES_IMM1632,     PAIR_UV | CYCLES_RMW | SRCDEP_REG | CYCLES_IMM1632,
        PAIR_UV | CYCLES_RMW | SRCDEP_REG | CYCLES_IMM1632,     PAIR_UV | CYCLES_RMW | SRCDEP_REG | CYCLES_IMM1632,     PAIR_UV | CYCLES_RMW | SRCDEP_REG | CYCLES_IMM1632,     PAIR_UV | CYCLES_RM | SRCDEP_REG | CYCLES_IMM1632
};
static uint64_t opcode_timings_8x[8] =
{
        PAIR_UV | CYCLES_RMW | SRCDEP_REG | CYCLES_IMM8,        PAIR_UV | CYCLES_RMW | SRCDEP_REG | CYCLES_IMM8,        PAIR_UV | CYCLES_RMW | SRCDEP_REG | CYCLES_IMM8,        PAIR_UV | CYCLES_RMW | SRCDEP_REG | CYCLES_IMM8,
        PAIR_UV | CYCLES_RMW | SRCDEP_REG | CYCLES_IMM8,        PAIR_UV | CYCLES_RMW | SRCDEP_REG | CYCLES_IMM8,        PAIR_UV | CYCLES_RMW | SRCDEP_REG | CYCLES_IMM8,        PAIR_UV | CYCLES_RM | SRCDEP_REG | CYCLES_IMM8
};

static int decode_delay, decode_delay_offset;
static uint8_t last_prefix;
static int prefixes;

static inline int COUNT(uint64_t c, int op_32)
{
        if ((c & PAIR_FPU) && !(c & FPU_FXCH))
                return FPU_I_LATENCY(c);
        if (c & CYCLES_HAS_MULTI)
        {
                if (op_32 & 0x100)
                        return ((uintptr_t)c >> 8) & 0xff;
                return (uintptr_t)c & 0xff;
        }
        if (!(c & PAIR_MASK))
                return c & 0xffff;
        if ((c & PAIR_MASK) == PAIR_FX)
                return c & 0xffff;        
        if ((c & PAIR_MASK) == PAIR_FXCH)
                return c & 0xffff;        
        if ((c & PAIR_UV) && !(c & PAIR_FPU))
                c &= 3;
        switch (c & CYCLES_MASK)
        {
                case CYCLES_REG:
                return 1;
                case CYCLES_RM:
                return 2;
                case CYCLES_RMW:
                return 3;
                case CYCLES_BRANCH:
                return cpu_hasMMX ? 1 : 2;
        }
        
        fatal("Illegal COUNT %016llx\n", c);
                
        return c;
}

static int codegen_fpu_latencies(uint64_t timings, int reg)
{
        int latency = fpu_latency;
        
        if ((timings & FPU_RW_ST0) && fpu_st_latency[0] && fpu_st_latency[0] > latency)
                latency = fpu_st_latency[0];
        if ((timings & FPU_RW_ST1) && fpu_st_latency[1] && fpu_st_latency[1] > latency)
                latency = fpu_st_latency[1];
        if ((timings & FPU_RW_STREG) && fpu_st_latency[reg] && fpu_st_latency[reg] > latency)
                latency = fpu_st_latency[reg];
        
        return latency;
}

#define SUB_AND_CLAMP(latency, count)  \
        latency -= count;              \
        if (latency < 0)               \
                latency = 0
                
static void codegen_fpu_latency_clock(int count)
{
        SUB_AND_CLAMP(fpu_latency, count);
        SUB_AND_CLAMP(fpu_st_latency[0], count);
        SUB_AND_CLAMP(fpu_st_latency[1], count);
        SUB_AND_CLAMP(fpu_st_latency[2], count);
        SUB_AND_CLAMP(fpu_st_latency[3], count);
        SUB_AND_CLAMP(fpu_st_latency[4], count);
        SUB_AND_CLAMP(fpu_st_latency[5], count);
        SUB_AND_CLAMP(fpu_st_latency[6], count);
        SUB_AND_CLAMP(fpu_st_latency[7], count);
}

static inline int codegen_timing_has_displacement(uint32_t fetchdat, int op_32)
{
        if (op_32 & 0x200)
        {
                if ((fetchdat & 7) == 4 && (fetchdat & 0xc0) != 0xc0)
                {
                        /*Has SIB*/
                        if ((fetchdat & 0xc0) == 0x40 || (fetchdat & 0xc0) == 0x80 || (fetchdat & 0x700) == 0x500)
                                return 1;
                }
                else
                {
                        if ((fetchdat & 0xc0) == 0x40 || (fetchdat & 0xc0) == 0x80 || (fetchdat & 0xc7) == 0x05)
                                return 1;
                }
        }
        else
        {
                if ((fetchdat & 0xc0) == 0x40 || (fetchdat & 0xc0) == 0x80 || (fetchdat & 0xc7) == 0x06)
                        return 1;
        }
        return 0;
}                

/*The instruction is only of interest here if it's longer than 7 bytes, as that's the
  limit on Pentium MMX parallel decoding*/
static inline int codegen_timing_instr_length(uint64_t timing, uint32_t fetchdat, int op_32)
{
        int len = prefixes;
        if ((timing & CYCLES_MASK) == CYCLES_RM || (timing & CYCLES_MASK) == CYCLES_RMW)
        {
                len += 2; /*Opcode + ModR/M*/
                if ((timing & CYCLES_HASIMM) == CYCLES_IMM8)
                        len++;
                if ((timing & CYCLES_HASIMM) == CYCLES_IMM1632)
                        len += (op_32 & 0x100) ? 4 : 2;

                if (op_32 & 0x200)
                {
                        if ((fetchdat & 7) == 4 && (fetchdat & 0xc0) != 0xc0)
                        {
                                /* Has SIB*/
                                len++;
                                if ((fetchdat & 0xc0) == 0x40)
                                        len++;
                                else if ((fetchdat & 0xc0) == 0x80)
                                        len += 4;
                                else if ((fetchdat & 0x700) == 0x500)
                                        len += 4;
                        }
                        else
                        {
                                if ((fetchdat & 0xc0) == 0x40)
                                        len++;
                                else if ((fetchdat & 0xc0) == 0x80)
                                        len += 4;
                                else if ((fetchdat & 0xc7) == 0x05)
                                        len += 4;
                        }
                }
                else
                {
                        if ((fetchdat & 0xc0) == 0x40)
                                len++;
                        else if ((fetchdat & 0xc0) == 0x80)
                                len += 2;
                        else if ((fetchdat & 0xc7) == 0x06)
                                len += 2;
                }
        }
        
        return len;
}

void codegen_timing_pentium_block_start()
{
        u_pipe_full = decode_delay = decode_delay_offset = 0;
}

void codegen_timing_pentium_start()
{
        last_prefix = 0;
        prefixes = 0;
}

void codegen_timing_pentium_prefix(uint8_t prefix, uint32_t fetchdat)
{
        prefixes++;
        if ((prefix & 0xf8) == 0xd8)
        {
                last_prefix = prefix;
                return;
        }
        if (cpu_hasMMX && prefix == 0x0f)
        {
                /*On Pentium MMX 0fh prefix is 'free'*/
                last_prefix = prefix;
                return;
        }
        if (cpu_hasMMX && (prefix == 0x66 || prefix == 0x67))
        {
                /*On Pentium MMX 66h and 67h prefixes take 2 clocks*/
                decode_delay_offset += 2;
                last_prefix = prefix;
                return;
        }
        if (prefix == 0x0f && (opcode & 0xf0) == 0x80)
        {
                /*On Pentium 0fh prefix is 'free' when used on conditional jumps*/
                last_prefix = prefix;
                return;
        }
        /*On Pentium all prefixes take 1 cycle to decode. Decode may be shadowed
          by execution of previous instructions*/
        decode_delay_offset++;
        last_prefix = prefix;
}

static void codegen_instruction(uint64_t *timings, uint8_t opcode, uint32_t fetchdat, int decode_delay_offset, int op_32)
{
        int instr_cycles, latency = 0;

        if ((timings[opcode] & PAIR_FPU) && !(timings[opcode] & FPU_FXCH))
                instr_cycles = latency = codegen_fpu_latencies(timings[opcode], fetchdat & 7);
        else
        {
                instr_cycles = 0;
        }

        if ((decode_delay + decode_delay_offset) > 0)
                codegen_fpu_latency_clock(decode_delay + decode_delay_offset + instr_cycles);
        else
                codegen_fpu_latency_clock(instr_cycles);
        instr_cycles += COUNT(timings[opcode], op_32);
        if ((decode_delay + decode_delay_offset) > 0)
                codegen_block_cycles += instr_cycles + decode_delay + decode_delay_offset;
        else
                codegen_block_cycles += instr_cycles;
        decode_delay = (-instr_cycles) + 1;
        
        if (timings[opcode] & FPU_POP)
        {
                int c;
                
                for (c = 0; c < 7; c++)
                        fpu_st_latency[c] = fpu_st_latency[c+1];
                fpu_st_latency[7] = 0;
        }
        if (timings[opcode] & FPU_POP2)
        {
                int c;
                
                for (c = 0; c < 6; c++)
                        fpu_st_latency[c] = fpu_st_latency[c+2];
                fpu_st_latency[6] = fpu_st_latency[7] = 0;
        }
        if ((timings[opcode] & PAIR_FPU) && !(timings[opcode] & FPU_FXCH))
        {
                fpu_latency = FPU_F_LATENCY(timings[opcode]);
        }

        if (timings[opcode] & FPU_PUSH)
        {
                int c;
                
                for (c = 0; c < 7; c++)
                        fpu_st_latency[c+1] = fpu_st_latency[c];
                fpu_st_latency[0] = 0;
        }
        if (timings[opcode] & FPU_WRITE_ST0)
        {
                fpu_st_latency[0] = FPU_RESULT_LATENCY(timings[opcode]);
        }
        if (timings[opcode] & FPU_WRITE_ST1)
        {
                fpu_st_latency[1] = FPU_RESULT_LATENCY(timings[opcode]);
        }
        if (timings[opcode] & FPU_WRITE_STREG)
        {
                int reg = fetchdat & 7;
                if (timings[opcode] & FPU_POP)
                        reg--;
                if (reg >= 0 &&
                        !(reg == 0 && (timings[opcode] & FPU_WRITE_ST0)) &&
                        !(reg == 1 && (timings[opcode] & FPU_WRITE_ST1)))
                {
                        fpu_st_latency[reg] = FPU_RESULT_LATENCY(timings[opcode]);
                }
        }
}

void codegen_timing_pentium_opcode(uint8_t opcode, uint32_t fetchdat, int op_32)
{
        uint64_t *timings;
        int mod3 = ((fetchdat & 0xc0) == 0xc0);
        int bit8 = !(opcode & 1);

        switch (last_prefix)
        {
                case 0x0f:
                timings = mod3 ? opcode_timings_0f_mod3 : opcode_timings_0f;
                break;
                
                case 0xd8:
                timings = mod3 ? opcode_timings_d8_mod3 : opcode_timings_d8;
                opcode = (opcode >> 3) & 7;
                break;
                case 0xd9:
                timings = mod3 ? opcode_timings_d9_mod3 : opcode_timings_d9;
                opcode = mod3 ? opcode & 0x3f : (opcode >> 3) & 7;
                break;
                case 0xda:
                timings = mod3 ? opcode_timings_da_mod3 : opcode_timings_da;
                opcode = (opcode >> 3) & 7;
                break;
                case 0xdb:
                timings = mod3 ? opcode_timings_db_mod3 : opcode_timings_db;
                opcode = mod3 ? opcode & 0x3f : (opcode >> 3) & 7;
                break;
                case 0xdc:
                timings = mod3 ? opcode_timings_dc_mod3 : opcode_timings_dc;
                opcode = (opcode >> 3) & 7;
                break;
                case 0xdd:
                timings = mod3 ? opcode_timings_dd_mod3 : opcode_timings_dd;
                opcode = (opcode >> 3) & 7;
                break;
                case 0xde:
                timings = mod3 ? opcode_timings_de_mod3 : opcode_timings_de;
                opcode = (opcode >> 3) & 7;
                break;
                case 0xdf:
                timings = mod3 ? opcode_timings_df_mod3 : opcode_timings_df;
                opcode = (opcode >> 3) & 7;
                break;

                default:
                switch (opcode)
                {
                        case 0x80: case 0x82: case 0x83:
                        timings = mod3 ? opcode_timings_mod3 : opcode_timings_8x;
                        if (!mod3)
                                opcode = (fetchdat >> 3) & 7;
                        break;
                        case 0x81:
                        timings = mod3 ? opcode_timings_mod3 : opcode_timings_81;
                        if (!mod3)
                                opcode = (fetchdat >> 3) & 7;
                        break;
                                
                        case 0xc0: case 0xc1: case 0xd0: case 0xd1: case 0xd2: case 0xd3:
                        timings = mod3 ? opcode_timings_shift_mod3 : opcode_timings_shift;
                        opcode = (fetchdat >> 3) & 7;
                        break;
                        
                        case 0xf6:
                        timings = mod3 ? opcode_timings_f6_mod3 : opcode_timings_f6;
                        opcode = (fetchdat >> 3) & 7;
                        break;
                        case 0xf7:
                        timings = mod3 ? opcode_timings_f7_mod3 : opcode_timings_f7;
                        opcode = (fetchdat >> 3) & 7;
                        break;
                        case 0xff:
                        timings = mod3 ? opcode_timings_ff_mod3 : opcode_timings_ff;
                        opcode = (fetchdat >> 3) & 7;
                        break;

                        default:
                        timings = mod3 ? opcode_timings_mod3 : opcode_timings;
                        break;
                }
        }
        
        if (u_pipe_full)
        {
                uint8_t regmask = get_srcdep_mask(timings[opcode], fetchdat, bit8);
                
                if ((u_pipe_timings[u_pipe_opcode] & PAIR_MASK) == PAIR_FX &&
                    (timings[opcode] & PAIR_MASK) != PAIR_FXCH)
                        goto nopair;

                if ((timings[opcode] & PAIR_MASK) == PAIR_FXCH &&
                    (u_pipe_timings[u_pipe_opcode] & PAIR_MASK) != PAIR_FX)
                        goto nopair;

                if ((u_pipe_timings[u_pipe_opcode] & PAIR_MASK) == PAIR_FX &&
                    (timings[opcode] & PAIR_MASK) == PAIR_FXCH)                    
                {
                        int temp;

                        codegen_instruction(u_pipe_timings, u_pipe_opcode, u_pipe_fetchdat, u_pipe_decode_delay_offset, u_pipe_op_32);

                        temp = fpu_st_latency[fetchdat & 7];
                        fpu_st_latency[fetchdat & 7] = fpu_st_latency[0];
                        fpu_st_latency[0] = temp;

                        u_pipe_full = 0;
                        decode_delay_offset = 0;
                        return;
                }
                
                if ((timings[opcode] & PAIR_V) && !(u_pipe_regmask & regmask) && (decode_delay+decode_delay_offset+u_pipe_decode_delay_offset) <= 0)
                {
                        int has_displacement;
                        
                        if (timings[opcode] & CYCLES_HASIMM)
                                has_displacement = codegen_timing_has_displacement(fetchdat, op_32);
                        else
                                has_displacement = 0;
                                
                        if (!has_displacement && (!cpu_hasMMX || codegen_timing_instr_length(timings[opcode], fetchdat, op_32) <= 7))
                        {
                                int t1 = u_pipe_timings[u_pipe_opcode] & CYCLES_MASK;
                                int t2 = timings[opcode] & CYCLES_MASK;
                                int t_pair;
                                uint64_t temp_timing;
                        
                                if (!(u_pipe_timings[u_pipe_opcode] & PAIR_FPU))
                                        t1 &= 3;
                                if (!(timings[opcode] & PAIR_FPU))
                                        t2 &= 3;                        
                        
                                if (t1 < 0 || t2 < 0 || t1 > CYCLES_BRANCH || t2 > CYCLES_BRANCH)
                                        fatal("Pair out of range\n");
                        
                                t_pair = pair_timings[t1][t2];
                                if (t_pair < 1)
                                        fatal("Illegal pair timings : t1=%i t2=%i u_opcode=%02x v_opcode=%02x\n", t1, t2, u_pipe_opcode, opcode);
                                                        
                                /*Instruction can pair with previous*/
                                temp_timing = t_pair;
                                codegen_instruction(&temp_timing, 0, 0, 0, 0);
                                u_pipe_full = 0;
                                decode_delay_offset = 0;
                                return;
                        }
                }
nopair:
                /*Instruction can not pair with previous*/
                /*Run previous now*/
                codegen_instruction(u_pipe_timings, u_pipe_opcode, u_pipe_fetchdat, u_pipe_decode_delay_offset, u_pipe_op_32);
                u_pipe_full = 0;
        }

        if ((timings[opcode] & PAIR_U) && (decode_delay + decode_delay_offset) <= 0)
        {
                int has_displacement;
                
                if (timings[opcode] & CYCLES_HASIMM)
                        has_displacement = codegen_timing_has_displacement(fetchdat, op_32);
                else
                        has_displacement = 0;
                
                if ((!has_displacement || cpu_hasMMX) && (!cpu_hasMMX || codegen_timing_instr_length(timings[opcode], fetchdat, op_32) <= 7))
                {                
                        /*Instruction might pair with next*/
                        u_pipe_full = 1;
                        u_pipe_opcode = opcode;
                        u_pipe_timings = timings;
                        u_pipe_op_32 = op_32;
                        u_pipe_regmask = get_dstdep_mask(timings[opcode], fetchdat, bit8);
                        u_pipe_fetchdat = fetchdat;
                        u_pipe_decode_delay_offset = decode_delay_offset;
                        decode_delay_offset = 0;
                        return;
                }
        }
        /*Instruction can not pair and must run now*/
        codegen_instruction(timings, opcode, fetchdat, decode_delay_offset, op_32);
        decode_delay_offset = 0;
}

void codegen_timing_pentium_block_end()
{
        if (u_pipe_full)
        {
                /*Run previous now*/
                codegen_block_cycles += COUNT(u_pipe_timings[u_pipe_opcode], u_pipe_op_32) + decode_delay + decode_delay_offset;
                u_pipe_full = 0;
        }
}

codegen_timing_t codegen_timing_pentium =
{
        codegen_timing_pentium_start,
        codegen_timing_pentium_prefix,
        codegen_timing_pentium_opcode,
        codegen_timing_pentium_block_start,
        codegen_timing_pentium_block_end
};
