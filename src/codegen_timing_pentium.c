/*Elements taken into account :
        - U/V integer pairing
        - FPU/FXCH pairing
        - Prefix decode delay (including shadowing)
  Elements not taken into account :
        - Branch prediction (beyond most simplistic approximation)
        - PMMX decode queue
        - FPU latencies
        - MMX latencies
*/

#include "ibm.h"
#include "cpu.h"
#include "x86.h"
#include "x86_ops.h"
#include "x87.h"
#include "mem.h"
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
#define CYCLES_REG (0 << 0)
#define CYCLES_RM  (1 << 0)
#define CYCLES_RMW (2 << 0)
#define CYCLES_BRANCH (3 << 0)

#define CYCLES_MASK ((1 << 7) - 1)

/*Instruction is MMX shift or pack/unpack instruction*/
#define MMX_SHIFTPACK (1 << 7)
/*Instruction is MMX multiply instruction*/
#define MMX_MULTIPLY  (1 << 8)

/*Instruction does not pair*/
#define PAIR_NP (0 << 29)
/*Instruction pairs in U pipe only*/
#define PAIR_U  (1 << 29)
/*Instruction pairs in V pipe only*/
#define PAIR_V  (2 << 29)
/*Instruction pairs in both U and V pipes*/
#define PAIR_UV (3 << 29)
/*Instruction pairs in U pipe only and only with FXCH*/
#define PAIR_FX (5 << 29)
/*Instruction is FXCH and only pairs in V pipe with FX pairable instruction*/
#define PAIR_FXCH (6 << 29)

#define PAIR_MASK (7 << 29)

/*Instruction has input dependency on register in REG field*/
#define SRCDEP_REG (1 << 9)
/*Instruction has input dependency on register in R/M field*/
#define SRCDEP_RM  (1 << 10)
/*Instruction modifies register in REG field*/
#define DSTDEP_REG (1 << 11)
/*Instruction modifies register in R/M field*/
#define DSTDEP_RM  (1 << 12)

/*Instruction has input dependency on given register*/
#define SRCDEP_EAX (1 << 13)
#define SRCDEP_ECX (1 << 14)
#define SRCDEP_EDX (1 << 15)
#define SRCDEP_EBX (1 << 16)
#define SRCDEP_ESP (1 << 17)
#define SRCDEP_EBP (1 << 18)
#define SRCDEP_ESI (1 << 19)
#define SRCDEP_EDI (1 << 20)

/*Instruction modifies given register*/
#define DSTDEP_EAX (1 << 21)
#define DSTDEP_ECX (1 << 22)
#define DSTDEP_EDX (1 << 23)
#define DSTDEP_EBX (1 << 24)
#define DSTDEP_ESP (1 << 25)
#define DSTDEP_EBP (1 << 26)
#define DSTDEP_ESI (1 << 27)
#define DSTDEP_EDI (1 << 28)

#define INVALID 0

static int u_pipe_full;
static uint32_t u_pipe_opcode;
static uint32_t *u_pipe_timings;
static uint32_t u_pipe_op_32;
static uint32_t u_pipe_regmask;

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
static uint32_t opcode_timings[256] =
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

static uint32_t opcode_timings_mod3[256] =
{
/*      ADD                                                             ADD                                                             ADD                                                             ADD*/
/*00*/  PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,
/*      ADD                                                             ADD                                                             PUSH ES                                                         POP ES*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX | SRCDEP_RM | DSTDEP_EAX,     PAIR_UV | CYCLES_REG | SRCDEP_EAX | SRCDEP_RM | DSTDEP_EAX,     PAIR_NP | CYCLES(1),                                            PAIR_NP | CYCLES(3),
/*      OR                                                              OR                                                              OR                                                              OR*/
        PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,
/*      OR                                                              OR                                                              PUSH CS                                                         */
        PAIR_UV | CYCLES_REG | SRCDEP_EAX | SRCDEP_RM | DSTDEP_EAX,     PAIR_UV | CYCLES_REG | SRCDEP_EAX | SRCDEP_RM | DSTDEP_EAX,     PAIR_NP | CYCLES(1),                                            INVALID,

/*      ADC                                                             ADC                                                             ADC                                                             ADC*/
/*10*/  PAIR_U  | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_U  | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_U  | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_U  | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,
/*      ADC                                                             ADC                                                             PUSH SS                                                         POP SS*/
        PAIR_U  | CYCLES_REG | SRCDEP_EAX | SRCDEP_RM | DSTDEP_EAX,     PAIR_U  | CYCLES_REG | SRCDEP_EAX | SRCDEP_RM | DSTDEP_EAX,     PAIR_NP | CYCLES(1),                                            PAIR_NP | CYCLES(3),
/*      SBB                                                             SBB                                                             SBB                                                             SBB*/        
        PAIR_U  | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_U  | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_U  | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_U  | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,
/*      SBB                                                             SBB                                                             PUSH DS                                                         POP DS*/
        PAIR_U  | CYCLES_REG | SRCDEP_EAX | SRCDEP_RM | DSTDEP_EAX,     PAIR_U  | CYCLES_REG | SRCDEP_EAX | SRCDEP_RM | DSTDEP_EAX,     PAIR_NP | CYCLES(1),                                            PAIR_NP | CYCLES(3),

/*      AND                                                             AND                                                             AND                                                             AND*/
/*20*/  PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,
/*      AND                                                             AND                                                                                                                             DAA*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX | SRCDEP_RM | DSTDEP_EAX,     PAIR_UV | CYCLES_REG | SRCDEP_EAX | SRCDEP_RM | DSTDEP_EAX,     INVALID,                                                        PAIR_NP | CYCLES(3),
/*      SUB                                                             SUB                                                             SUB                                                             SUB*/
        PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,
/*      SUB                                                             SUB                                                                                                                             DAS*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX | SRCDEP_RM | DSTDEP_EAX,     PAIR_UV | CYCLES_REG | SRCDEP_EAX | SRCDEP_RM | DSTDEP_EAX,     INVALID,                                                        PAIR_NP | CYCLES(3),
        
/*      XOR                                                             XOR                                                             XOR                                                             XOR*/
/*30*/  PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,     PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM | DSTDEP_REG,
/*      XOR                                                             XOR                                                                                                                             AAA*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX | SRCDEP_RM | DSTDEP_EAX,     PAIR_UV | CYCLES_REG | SRCDEP_EAX | SRCDEP_RM | DSTDEP_EAX,     INVALID,                                                        PAIR_NP | CYCLES(3),
/*      CMP                                                             CMP                                                             CMP                                                             CMP*/
        PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM,                  PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM,                  PAIR_UV | CYCLES_REG | SRCDEP_REG | SRCDEP_RM,                  PAIR_UV | CYCLES_REG | SRCDEP_RM | SRCDEP_REG,
/*      CMP                                                             CMP                                                                                                                             AAS*/
        PAIR_UV | CYCLES_REG | SRCDEP_EAX | SRCDEP_RM,                  PAIR_UV | CYCLES_REG | SRCDEP_EAX,                              INVALID,                                                        PAIR_NP | CYCLES(3),

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

static uint32_t opcode_timings_0f[256] =
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
static uint32_t opcode_timings_0f_mod3[256] =
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

static uint32_t opcode_timings_shift[8] =
{
        PAIR_U | CYCLES_RMW,    PAIR_U | CYCLES_RMW,    PAIR_U | CYCLES_RMW,    PAIR_U | CYCLES_RMW,
        PAIR_U | CYCLES_RMW,    PAIR_U | CYCLES_RMW,    PAIR_U | CYCLES_RMW,    PAIR_U | CYCLES_RMW,
};
static uint32_t opcode_timings_shift_mod3[8] =
{
        PAIR_U | CYCLES_REG | DSTDEP_RM,    PAIR_U | CYCLES_REG | DSTDEP_RM,    PAIR_U | CYCLES_REG | DSTDEP_RM,    PAIR_U | CYCLES_REG | DSTDEP_RM,
        PAIR_U | CYCLES_REG | DSTDEP_RM,    PAIR_U | CYCLES_REG | DSTDEP_RM,    PAIR_U | CYCLES_REG | DSTDEP_RM,    PAIR_U | CYCLES_REG | DSTDEP_RM,
};

static uint32_t opcode_timings_f6[8] =
{
/*      TST                                             NOT                     NEG*/
        PAIR_UV | CYCLES_RM,    INVALID,                PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(3),
/*      MUL                     IMUL                    DIV                     IDIV*/
        PAIR_NP | CYCLES(11),   PAIR_NP | CYCLES(11),   PAIR_NP | CYCLES(17),   PAIR_NP | CYCLES(22)
};
static uint32_t opcode_timings_f6_mod3[8] =
{
/*      TST                                             NOT                     NEG*/
        PAIR_UV | CYCLES_REG | SRCDEP_RM,               INVALID,                PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),
/*      MUL                     IMUL                    DIV                     IDIV*/
        PAIR_NP | CYCLES(11),   PAIR_NP | CYCLES(11),   PAIR_NP | CYCLES(17),   PAIR_NP | CYCLES(22)
};
static uint32_t opcode_timings_f7[8] =
{
/*      TST                                                                     NOT                             NEG*/
        PAIR_UV | CYCLES_RM,                    INVALID,                        PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),
/*      MUL                                     IMUL                            DIV                             IDIV*/
        PAIR_NP | CYCLES_MULTI(11,10),          PAIR_NP | CYCLES_MULTI(11,10),  PAIR_NP | CYCLES_MULTI(25,41),  PAIR_NP | CYCLES_MULTI(30,46)
};
static uint32_t opcode_timings_f7_mod3[8] =
{
/*      TST                                                                     NOT                             NEG*/
        PAIR_UV | CYCLES_REG | SRCDEP_RM,       INVALID,                        PAIR_NP | CYCLES(3),            PAIR_NP | CYCLES(3),
/*      MUL                                     IMUL                            DIV                             IDIV*/
        PAIR_NP | CYCLES_MULTI(11,10),          PAIR_NP | CYCLES_MULTI(11,10),  PAIR_NP | CYCLES_MULTI(25,41),  PAIR_NP | CYCLES_MULTI(30,46)
};
static uint32_t opcode_timings_ff[8] =
{
/*      INC                     DEC                     CALL                    CALL far*/
        PAIR_UV | CYCLES_RMW,   PAIR_UV | CYCLES_RMW,   PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(0),
/*      JMP                     JMP far                 PUSH*/
        PAIR_NP | CYCLES(2),    PAIR_NP | CYCLES(0),    PAIR_NP | CYCLES(2),    INVALID
};
static uint32_t opcode_timings_ff_mod3[8] =
{
/*      INC                                             DEC                                             CALL                    CALL far*/
        PAIR_UV | CYCLES_REG | SRCDEP_RM | DSTDEP_RM,   PAIR_UV | CYCLES_REG | SRCDEP_RM | DSTDEP_RM,   PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(0),
/*      JMP                     JMP far                 PUSH*/
        PAIR_NP | CYCLES(2),    PAIR_NP | CYCLES(0),    PAIR_NP | CYCLES(2),    INVALID
};

static uint32_t opcode_timings_d8[8] =
{
/*      FADDs                   FMULs                   FCOMs                   FCOMPs*/
        PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),
/*      FSUBs                   FSUBRs                  FDIVs                   FDIVRs*/
        PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(39),   PAIR_FX | CYCLES(39)
};
static uint32_t opcode_timings_d8_mod3[8] =
{
/*      FADD                    FMUL                    FCOM                    FCOMP*/
        PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),
/*      FSUB                    FSUBR                   FDIV                    FDIVR*/
        PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(39),   PAIR_FX | CYCLES(39)
};

static uint32_t opcode_timings_d9[8] =
{
/*      FLDs                                            FSTs                      FSTPs*/
        PAIR_FX | CYCLES(1),      INVALID,              PAIR_NP | CYCLES(2),      PAIR_NP | CYCLES(2),
/*      FLDENV                    FLDCW                 FSTENV                    FSTCW*/
        PAIR_NP | CYCLES(32),     PAIR_NP | CYCLES(7),  PAIR_NP | CYCLES(48),     PAIR_NP | CYCLES(2)
};
static uint32_t opcode_timings_d9_mod3[64] =
{
        /*FLD*/
        PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),
        PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),
        /*FXCH*/
        PAIR_V | CYCLES(0),     PAIR_V | CYCLES(0),     PAIR_V | CYCLES(0),     PAIR_V | CYCLES(0),
        PAIR_V | CYCLES(0),     PAIR_V | CYCLES(0),     PAIR_V | CYCLES(0),     PAIR_V | CYCLES(0),
        /*FNOP*/
        PAIR_NP | CYCLES(3),    INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,        
        /*FSTP*/
        PAIR_NP | CYCLES(1),    PAIR_NP | CYCLES(1),    PAIR_NP | CYCLES(1),    PAIR_NP | CYCLES(1),
        PAIR_NP | CYCLES(1),    PAIR_NP | CYCLES(1),    PAIR_NP | CYCLES(1),    PAIR_NP | CYCLES(1),        
/*      opFCHS                  opFABS*/
        PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(3),    INVALID,                INVALID,
/*      opFTST                  opFXAM*/
        PAIR_NP | CYCLES(1),    PAIR_NP | CYCLES(21),   INVALID,                INVALID,
/*      opFLD1                  opFLDL2T                opFLDL2E                opFLDPI*/
        PAIR_NP | CYCLES(2),    CYCLES(3),              PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(3),
/*      opFLDEG2                opFLDLN2                opFLDZ*/
        PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(2),    INVALID,
/*      opF2XM1                 opFYL2X                 opFPTAN                 opFPATAN*/
        PAIR_NP | CYCLES(13),   PAIR_NP | CYCLES(22),   PAIR_NP | CYCLES(100),  PAIR_NP | CYCLES(100),
/*                                                      opFDECSTP               opFINCSTP,*/
        INVALID,                INVALID,                PAIR_NP | CYCLES(1),    PAIR_NP | CYCLES(1),
/*      opFPREM                                         opFSQRT                 opFSINCOS*/
        PAIR_NP | CYCLES(70),   INVALID,                PAIR_NP | CYCLES(70),   PAIR_NP | CYCLES(50),
/*      opFRNDINT               opFSCALE                opFSIN                  opFCOS*/
        PAIR_NP | CYCLES(9),    PAIR_NP | CYCLES(20),   PAIR_NP | CYCLES(50),   PAIR_NP | CYCLES(50)
};

static uint32_t opcode_timings_da[8] =
{
/*      FIADDl                  FIMULl                  FICOMl                  FICOMPl*/
        PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(4),
/*      FISUBl                  FISUBRl                 FIDIVl                  FIDIVRl*/
        PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(42),   PAIR_NP | CYCLES(42)
};
static uint32_t opcode_timings_da_mod3[8] =
{
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                PAIR_NP | CYCLES(5),    INVALID,                INVALID
};


static uint32_t opcode_timings_db[8] =
{
/*      FLDil                                           FSTil                   FSTPil*/
        PAIR_NP | CYCLES(1),    INVALID,                PAIR_NP | CYCLES(6),    PAIR_NP | CYCLES(6),
/*                              FLDe                                            FSTPe*/
        INVALID,                PAIR_NP | CYCLES(3),    INVALID,                PAIR_NP | CYCLES(3)
};
static uint32_t opcode_timings_db_mod3[64] =
{
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        
/*                              opFNOP                  opFCLEX                 opFINIT*/
        INVALID,                PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(7),    PAIR_NP | CYCLES(17),
/*      opFNOP                  opFNOP*/
        PAIR_NP | CYCLES(3),    PAIR_NP | CYCLES(3),    INVALID,                INVALID,
        
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
};

static uint32_t opcode_timings_dc[8] =
{
/*      FADDd                   FMULd                   FCOMd                   FCOMPd*/
        PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),
/*      FSUBd                   FSUBRd                  FDIVd                   FDIVRd*/
        PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(39),   PAIR_FX | CYCLES(39)
};
static uint32_t opcode_timings_dc_mod3[8] =
{
/*      opFADDr                 opFMULr*/
        PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),    INVALID,                INVALID,
/*      opFSUBRr                opFSUBr                 opFDIVRr                opFDIVr*/
        PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(39),   PAIR_FX | CYCLES(39)
};

static uint32_t opcode_timings_dd[8] =
{
/*      FLDd                                            FSTd                    FSTPd*/
        PAIR_FX | CYCLES(1),    INVALID,                PAIR_NP | CYCLES(2),    PAIR_NP | CYCLES(2),
/*      FRSTOR                                          FSAVE                   FSTSW*/
        PAIR_NP | CYCLES(70),   INVALID,                PAIR_NP | CYCLES(127),  PAIR_NP | CYCLES(2)
};
static uint32_t opcode_timings_dd_mod3[8] =
{
/*      FFFREE                                          FST                     FSTP*/
        PAIR_NP | CYCLES(3),    INVALID,                PAIR_NP | CYCLES(2),    PAIR_NP | CYCLES(2),
/*      FUCOM                   FUCOMP*/
        PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(4),    INVALID,                INVALID
};

static uint32_t opcode_timings_de[8] =
{
/*      FIADDw                  FIMULw                  FICOMw                  FICOMPw*/
        PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(4),
/*      FISUBw                  FISUBRw                 FIDIVw                  FIDIVRw*/
        PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(42),   PAIR_NP | CYCLES(42)
};
static uint32_t opcode_timings_de_mod3[8] =
{
/*      FADD                    FMUL                                            FCOMPP*/
        PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),    INVALID,                PAIR_FX | CYCLES(1),
/*      FSUB                    FSUBR                   FDIV                    FDIVR*/
        PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(1),    PAIR_FX | CYCLES(39),   PAIR_FX | CYCLES(39)
};

static uint32_t opcode_timings_df[8] =
{
/*      FILDiw                                          FISTiw                  FISTPiw*/
        PAIR_NP | CYCLES(1),    INVALID,                PAIR_NP | CYCLES(6),    PAIR_NP | CYCLES(6),
/*                              FILDiq                  FBSTP                   FISTPiq*/
        INVALID,                PAIR_NP | CYCLES(1),    PAIR_NP | CYCLES(148),  PAIR_NP | CYCLES(6)
};
static uint32_t opcode_timings_df_mod3[8] =
{
        INVALID,                INVALID,                INVALID,                INVALID,
/*      FSTSW AX*/
        PAIR_NP | CYCLES(2),    INVALID,                INVALID,                INVALID
};

static uint32_t opcode_timings_8x[8] =
{
        PAIR_UV | CYCLES_RMW | SRCDEP_REG,      PAIR_UV | CYCLES_RMW | SRCDEP_REG,      PAIR_UV | CYCLES_RMW | SRCDEP_REG,      PAIR_UV | CYCLES_RMW | SRCDEP_REG,
        PAIR_UV | CYCLES_RMW | SRCDEP_REG,      PAIR_UV | CYCLES_RMW | SRCDEP_REG,      PAIR_UV | CYCLES_RMW | SRCDEP_REG,      PAIR_UV | CYCLES_RM | SRCDEP_REG
};

static int decode_delay;
static uint8_t last_prefix;

static __inline int COUNT(uint32_t c, int op_32)
{
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
        
        fatal("Illegal COUNT %08x\n", c);
                
        return c;
}

void codegen_timing_pentium_block_start()
{
        u_pipe_full = decode_delay = 0;
}

void codegen_timing_pentium_start()
{
        last_prefix = 0;
}

void codegen_timing_pentium_prefix(uint8_t prefix, uint32_t fetchdat)
{
        if (cpu_hasMMX && prefix == 0x0f)
        {
                /*On Pentium MMX 0fh prefix is 'free'*/
                last_prefix = prefix;
                return;
        }
        if (cpu_hasMMX && (prefix == 0x66 || prefix == 0x67))
        {
                /*On Pentium MMX 66h and 67h prefixes take 2 clocks*/
                decode_delay += 2;
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
        decode_delay++;
        last_prefix = prefix;
}

void codegen_timing_pentium_opcode(uint8_t opcode, uint32_t fetchdat, int op_32)
{
        int *timings;
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
                        case 0x80: case 0x81: case 0x82: case 0x83:
                        timings = mod3 ? opcode_timings_mod3 : opcode_timings_8x;
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
        
        if (decode_delay < 0)
                decode_delay = 0;

        if (u_pipe_full)
        {
                uint8_t regmask = get_srcdep_mask(timings[opcode], fetchdat, bit8);
                
                if ((u_pipe_timings[u_pipe_opcode] & PAIR_MASK) == PAIR_FX &&
                    (timings[opcode] & PAIR_MASK) != PAIR_FXCH)
                        goto nopair;

                if ((timings[opcode] & PAIR_MASK) == PAIR_FXCH &&
                    (u_pipe_timings[u_pipe_opcode] & PAIR_MASK) != PAIR_FX)
                        goto nopair;
                
                if ((timings[opcode] & PAIR_V) && !(u_pipe_regmask & regmask) && !decode_delay)
                {
                        int t1 = u_pipe_timings[u_pipe_opcode] & CYCLES_MASK;
                        int t2 = timings[opcode] & CYCLES_MASK;
                        int t_pair;
                        
                        if (t1 < 0 || t2 < 0 || t1 > CYCLES_BRANCH || t2 > CYCLES_BRANCH)
                                fatal("Pair out of range\n");
                        
                        t_pair = pair_timings[t1][t2];
                        if (t_pair < 1)
                                fatal("Illegal pair timings : t1=%i t2=%i u_opcode=%02x v_opcode=%02x\n", t1, t2, u_pipe_opcode, opcode);

                        codegen_block_cycles += t_pair;
                        decode_delay = (-t_pair) + 1;
                        
                        /*Instruction can pair with previous*/
                        u_pipe_full = 0;
                        return;
                }
nopair:
                /*Instruction can not pair with previous*/
                /*Run previous now*/
                codegen_block_cycles += COUNT(u_pipe_timings[u_pipe_opcode], u_pipe_op_32) + decode_delay;
                decode_delay = (-COUNT(u_pipe_timings[u_pipe_opcode], u_pipe_op_32)) + 1;
                u_pipe_full = 0;
        }

        if ((timings[opcode] & PAIR_U) && !decode_delay)
        {
                /*Instruction might pair with next*/
                u_pipe_full = 1;
                u_pipe_opcode = opcode;
                u_pipe_timings = timings;
                u_pipe_op_32 = op_32;
                u_pipe_regmask = get_dstdep_mask(timings[opcode], fetchdat, bit8);
                return;
        }
        /*Instruction can not pair and must run now*/

        codegen_block_cycles += COUNT(timings[opcode], op_32) + decode_delay;
        decode_delay = (-COUNT(timings[opcode], op_32)) + 1;
}

void codegen_timing_pentium_block_end()
{
        if (u_pipe_full)
        {
                /*Run previous now*/
                codegen_block_cycles += COUNT(u_pipe_timings[u_pipe_opcode], u_pipe_op_32) + decode_delay;
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
