/*Elements taken into account :
        - X/Y pairing
        - FPU/FXCH pairing
        - Prefix decode delay
        - AGI stalls
  Elements not taken into account :
        - Branch prediction (beyond most simplistic approximation)
        - FPU queue
        - Out of order execution (beyond most simplistic approximation)
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/mem.h>
#include "cpu.h"
#include "x86.h"
#include "x86_ops.h"
#include "x87.h"
#include "codegen.h"
#include "codegen_timing_common.h"

/*Instruction has different execution time for 16 and 32 bit data. Does not pair */
#define CYCLES_HAS_MULTI (1 << 31)

#define CYCLES_MULTI(c16, c32) (CYCLES_HAS_MULTI | c16 | (c32 << 8))

/*Instruction lasts given number of cycles. Does not pair*/
#define CYCLES(c) (c)

/*Instruction follows either register timing, read-modify, or read-modify-write.
  May be pairable*/
#define CYCLES_REG (1 << 0)
#define CYCLES_RM  (1 << 0)
#define CYCLES_RMW (1 << 0)
#define CYCLES_BRANCH (1 << 0)

#define CYCLES_MASK ((1 << 7) - 1)

/*Instruction does not pair*/
#define PAIR_NP (0 << 29)
/*Instruction pairs in X pipe only*/
#define PAIR_X  (1 << 29)
/*Instruction pairs in X pipe only, and can not pair with a following instruction*/
#define PAIR_X_BRANCH  (2 << 29)
/*Instruction pairs in both X and Y pipes*/
#define PAIR_XY (3 << 29)

#define PAIR_MASK (3 << 29)

#define INVALID 0

static int prev_full;
static uint32_t prev_opcode;
static uint32_t *prev_timings;
static uint32_t prev_op_32;
static uint32_t prev_regmask;
static uint64_t *prev_deps;
static uint32_t prev_fetchdat;

static uint32_t last_regmask_modified;
static uint32_t regmask_modified;

static uint32_t opcode_timings[256] =
{
/*      ADD                                ADD                                ADD                               ADD*/
/*00*/  PAIR_XY | CYCLES_RMW,              PAIR_XY | CYCLES_RMW,              PAIR_XY | CYCLES_RM,              PAIR_XY | CYCLES_RM,
/*      ADD                                ADD                                PUSH ES                           POP ES*/
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_NP | CYCLES(1),              PAIR_NP | CYCLES(3),
/*      OR                                 OR                                 OR                                OR*/
        PAIR_XY | CYCLES_RMW,              PAIR_XY | CYCLES_RMW,              PAIR_XY | CYCLES_RM,              PAIR_XY | CYCLES_RM,
/*      OR                                 OR                                 PUSH CS                                        */
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_NP | CYCLES(1),              INVALID,

/*      ADC                                ADC                                ADC                               ADC*/
/*10*/  PAIR_XY | CYCLES_RMW,              PAIR_XY | CYCLES_RMW,              PAIR_XY | CYCLES_RM,              PAIR_XY | CYCLES_RM,
/*      ADC                                ADC                                PUSH SS                           POP SS*/
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_NP | CYCLES(1),              PAIR_NP | CYCLES(3),
/*      SBB                                SBB                                SBB                               SBB*/
        PAIR_XY | CYCLES_RMW,              PAIR_XY | CYCLES_RMW,              PAIR_XY | CYCLES_RM,              PAIR_XY | CYCLES_RM,
/*      SBB                                SBB                                PUSH DS                           POP DS*/
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_NP | CYCLES(1),              PAIR_NP | CYCLES(3),

/*      AND                                AND                                AND                               AND*/
/*20*/  PAIR_XY | CYCLES_RMW,              PAIR_XY | CYCLES_RMW,              PAIR_XY | CYCLES_RM,              PAIR_XY | CYCLES_RM,
/*      AND                                AND                                                                  DAA*/
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              INVALID,                          PAIR_NP | CYCLES(7),
/*      SUB                                SUB                                SUB                               SUB*/
        PAIR_XY | CYCLES_RMW,              PAIR_XY | CYCLES_RMW,              PAIR_XY | CYCLES_RM,              PAIR_XY | CYCLES_RM,
/*      SUB                                SUB                                                                  DAS*/
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              INVALID,                          PAIR_NP | CYCLES(7),

/*      XOR                                XOR                                XOR                               XOR*/
/*30*/  PAIR_XY | CYCLES_RMW,              PAIR_XY | CYCLES_RMW,              PAIR_XY | CYCLES_RM,              PAIR_XY | CYCLES_RM,
/*      XOR                                XOR                                                                                            AAA*/
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              INVALID,                          PAIR_NP | CYCLES(7),
/*      CMP                                CMP                                CMP                               CMP*/
        PAIR_XY | CYCLES_RM,               PAIR_XY | CYCLES_RM,               PAIR_XY | CYCLES_RM,              PAIR_XY | CYCLES_RM,
/*      CMP                                CMP                                                                  AAS*/
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              INVALID,                          PAIR_NP | CYCLES(7),

/*      INC EAX                INC ECX                INC EDX                INC EBX*/
/*40*/  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,
/*      INC ESP                INC EBP                INC ESI                INC EDI*/
        PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,
/*      DEC EAX                DEC ECX                DEC EDX                DEC EBX*/
        PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,
/*      DEC ESP                DEC EBP                DEC ESI                DEC EDI*/
        PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,

/*      PUSH EAX               PUSH ECX               PUSH EDX               PUSH EBX*/
/*50*/  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,
/*      PUSH ESP               PUSH EBP               PUSH ESI               PUSH EDI*/
        PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,
/*      POP EAX                POP ECX                POP EDX                POP EBX*/
        PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,
/*      POP ESP                POP EBP                POP ESI                POP EDI*/
        PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,

/*      PUSHA                   POPA                   BOUND                  ARPL*/
/*60*/  PAIR_NP | CYCLES(6),    PAIR_NP | CYCLES(6),   PAIR_NP | CYCLES(11),  PAIR_NP | CYCLES(9),
        INVALID,                INVALID,               INVALID,               INVALID,
/*      PUSH imm                IMUL                   PUSH imm               IMUL*/
        PAIR_XY | CYCLES_REG,   PAIR_NP | CYCLES(10),  PAIR_XY | CYCLES_REG,  PAIR_NP | CYCLES(10),
/*      INSB                    INSW                   OUTSB                  OUTSW*/
        PAIR_NP | CYCLES(14),   PAIR_NP | CYCLES(14),  PAIR_NP | CYCLES(14),  PAIR_NP | CYCLES(14),

/*      Jxx*/
/*70*/  PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,
        PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,
        PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,
        PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,

/*80*/  INVALID,                           INVALID,                           INVALID,                           INVALID,
/*      TEST                               TEST                               XCHG                               XCHG*/
        PAIR_XY | CYCLES_RM,               PAIR_XY | CYCLES_RM,               PAIR_XY | CYCLES(2),               PAIR_XY | CYCLES(2),
/*      MOV                                MOV                                MOV                                MOV*/
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,
/*      MOV from seg                       LEA                                MOV to seg                         POP*/
        PAIR_XY | CYCLES(1),               PAIR_XY | CYCLES_REG,              CYCLES(3),                         PAIR_XY | CYCLES(1),

/*      NOP                    XCHG                  XCHG                  XCHG*/
/*90*/  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES(2),  PAIR_XY | CYCLES(2),  PAIR_XY | CYCLES(2),
/*      XCHG                   XCHG                  XCHG                  XCHG*/
        PAIR_XY | CYCLES(2),   PAIR_XY | CYCLES(2),  PAIR_XY | CYCLES(2),  PAIR_XY | CYCLES(2),
/*      CBW                    CWD                   CALL far              WAIT*/
        PAIR_XY | CYCLES(3),   PAIR_XY | CYCLES(2),  PAIR_NP | CYCLES(4),  PAIR_NP | CYCLES(5),
/*      PUSHF                  POPF                  SAHF                  LAHF*/
        PAIR_XY | CYCLES(2),   PAIR_XY | CYCLES(9),  PAIR_XY | CYCLES(1),  PAIR_XY | CYCLES(2),

/*      MOV                                MOV                                MOV                                MOV*/
/*a0*/  PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,
/*      MOVSB                              MOVSW                              CMPSB                              CMPSW*/
        PAIR_NP | CYCLES(4),               PAIR_NP | CYCLES(4),               PAIR_NP | CYCLES(5),               PAIR_NP | CYCLES(5),
/*      TEST                               TEST                               STOSB                              STOSW*/
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_NP | CYCLES(2),               PAIR_NP | CYCLES(2),
/*      LODSB                              LODSW                              SCASB                              SCASW*/
        PAIR_NP | CYCLES(3),               PAIR_NP | CYCLES(3),               PAIR_NP | CYCLES(2),               PAIR_NP | CYCLES(2),

/*      MOV*/
/*b0*/  PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,

/*                                                                                                      RET imm                                         RET*/
/*c0*/  INVALID,                                        INVALID,                                        PAIR_X_BRANCH | CYCLES(3),                      PAIR_X_BRANCH | CYCLES(2),
/*      LES                                             LDS                                             MOV                                             MOV*/
        PAIR_XY | CYCLES(4),                            PAIR_XY | CYCLES(4),                            PAIR_XY | CYCLES_REG,                           PAIR_XY | CYCLES_REG,
/*      ENTER                                           LEAVE                                           RETF                                            RETF*/
        PAIR_XY | CYCLES(10),                           PAIR_XY | CYCLES(4),                            PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(4),
/*      INT3                                            INT                                             INTO                                            IRET*/
        PAIR_NP | CYCLES(13),                           PAIR_NP | CYCLES(16),                           PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(10),


/*d0*/  INVALID,                                        INVALID,                                        INVALID,                                        INVALID,
/*      AAM                                             AAD                                             SETALC                                          XLAT*/
        PAIR_XY | CYCLES(18),                           PAIR_XY | CYCLES(7),                            PAIR_XY | CYCLES(2),                            PAIR_XY | CYCLES(4),
        INVALID,                                        INVALID,                                        INVALID,                                        INVALID,
        INVALID,                                        INVALID,                                        INVALID,                                        INVALID,
/*      LOOPNE                                          LOOPE                                           LOOP                                            JCXZ*/
/*e0*/  PAIR_X_BRANCH| CYCLES_BRANCH,                   PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,
/*      IN AL                                           IN AX                                           OUT_AL                                          OUT_AX*/
        PAIR_NP | CYCLES(14),                           PAIR_NP | CYCLES(14),                           PAIR_NP | CYCLES(14),                           PAIR_NP | CYCLES(14),
/*      CALL                                            JMP                                             JMP                                             JMP*/
        PAIR_X_BRANCH | CYCLES_REG,                     PAIR_X_BRANCH | CYCLES_REG,                     PAIR_NP | CYCLES(1),                            PAIR_X_BRANCH | CYCLES_REG,
/*      IN AL                                           IN AX                                           OUT_AL                                          OUT_AX*/
        PAIR_NP | CYCLES(14),                           PAIR_NP | CYCLES(14),                           PAIR_NP | CYCLES(14),                           PAIR_NP | CYCLES(14),

/*                                                                                                      REPNE                                           REPE*/
/*f0*/  INVALID,                                        INVALID,                                        PAIR_NP | CYCLES(0),                            PAIR_NP | CYCLES(0),
/*      HLT                                             CMC*/
        PAIR_NP | CYCLES(5),                            PAIR_XY | CYCLES(2),                            INVALID,                                        INVALID,
/*      CLC                                             STC                                             CLI                                             STI*/
        PAIR_XY | CYCLES(1),                            PAIR_XY | CYCLES(1),                            PAIR_XY | CYCLES(7),                            PAIR_XY | CYCLES(7),
/*      CLD                                             STD                                             INCDEC*/
        PAIR_XY | CYCLES(7),                            PAIR_XY | CYCLES(7),                            PAIR_XY | CYCLES_RMW,                           INVALID
};

static uint32_t opcode_timings_mod3[256] =
{
/*      ADD                                ADD                                ADD                               ADD*/
/*00*/  PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,
/*      ADD                                ADD                                PUSH ES                           POP ES*/
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_NP | CYCLES(1),              PAIR_NP | CYCLES(3),
/*      OR                                 OR                                 OR                                OR*/
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,             PAIR_XY | CYCLES_REG,
/*      OR                                 OR                                 PUSH CS                                        */
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_NP | CYCLES(1),              INVALID,

/*      ADC                                ADC                                ADC                               ADC*/
/*10*/  PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,             PAIR_XY | CYCLES_REG,
/*      ADC                                ADC                                PUSH SS                           POP SS*/
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_NP | CYCLES(1),              PAIR_NP | CYCLES(3),
/*      SBB                                SBB                                SBB                               SBB*/
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,             PAIR_XY | CYCLES_REG,
/*      SBB                                SBB                                PUSH DS                           POP DS*/
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_NP | CYCLES(1),              PAIR_NP | CYCLES(3),

/*      AND                                AND                                AND                               AND*/
/*20*/  PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,             PAIR_XY | CYCLES_REG,
/*      AND                                AND                                                                  DAA*/
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              INVALID,                          PAIR_NP | CYCLES(7),
/*      SUB                                SUB                                SUB                               SUB*/
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,             PAIR_XY | CYCLES_REG,
/*      SUB                                SUB                                                                  DAS*/
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              INVALID,                          PAIR_NP | CYCLES(7),

/*      XOR                                XOR                                XOR                               XOR*/
/*30*/  PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,             PAIR_XY | CYCLES_REG,
/*      XOR                                XOR                                                                  AAA*/
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              INVALID,                          PAIR_NP | CYCLES(7),
/*      CMP                                CMP                                CMP                               CMP*/
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,             PAIR_XY | CYCLES_REG,
/*      CMP                                CMP                                                                  AAS*/
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              INVALID,                          PAIR_NP | CYCLES(7),

/*      INC EAX                INC ECX                INC EDX                INC EBX*/
/*40*/  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,
/*      INC ESP                INC EBP                INC ESI                INC EDI*/
        PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,
/*      DEC EAX                DEC ECX                DEC EDX                DEC EBX*/
        PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,
/*      DEC ESP                DEC EBP                DEC ESI                DEC EDI*/
        PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,

/*      PUSH EAX               PUSH ECX               PUSH EDX               PUSH EBX*/
/*50*/  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,
/*      PUSH ESP               PUSH EBP               PUSH ESI               PUSH EDI*/
        PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,
/*      POP EAX                POP ECX                POP EDX                POP EBX*/
        PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,
/*      POP ESP                POP EBP                POP ESI                POP EDI*/
        PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,

/*      PUSHA                  POPA                   BOUND                  ARPL*/
/*60*/  PAIR_NP | CYCLES(6),   PAIR_NP | CYCLES(6),   PAIR_NP | CYCLES(11),  PAIR_NP | CYCLES(9),
        INVALID,               INVALID,               INVALID,               INVALID,
/*      PUSH imm               IMUL                   PUSH imm               IMUL*/
        PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES(10),  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES(10),
/*      INSB                   INSW                   OUTSB                  OUTSW*/
        PAIR_NP | CYCLES(14),  PAIR_NP | CYCLES(14),  PAIR_NP | CYCLES(14),  PAIR_NP | CYCLES(14),

/*      Jxx*/
/*70*/  PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,
        PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,
        PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,
        PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,

/*80*/  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,
/*      TEST                   TEST                   XCHG                   XCHG*/
        PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES(2),   PAIR_XY | CYCLES(2),
/*      MOV                    MOV                    MOV                    MOV*/
        PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,  PAIR_XY | CYCLES_REG,
/*      MOV from seg           LEA                    MOV to seg             POP*/
        PAIR_XY | CYCLES(1),   PAIR_XY | CYCLES_REG,  PAIR_NP | CYCLES(3),   PAIR_XY | CYCLES(1),

/*      NOP                                             XCHG                                            XCHG                                            XCHG*/
/*90*/  PAIR_XY | CYCLES_REG,                           PAIR_XY | CYCLES(2),                            PAIR_XY | CYCLES(2),                            PAIR_XY | CYCLES(2),
/*      XCHG                                            XCHG                                            XCHG                                            XCHG*/
        PAIR_XY | CYCLES(2),                            PAIR_XY | CYCLES(2),                            PAIR_XY | CYCLES(2),                            PAIR_XY | CYCLES(2),
/*      CBW                                             CWD                                             CALL far                                        WAIT*/
        PAIR_XY | CYCLES(3),                            PAIR_XY | CYCLES(2),                            PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(5),
/*      PUSHF                                           POPF                                            SAHF                                            LAHF*/
        PAIR_XY | CYCLES(2),                            PAIR_XY | CYCLES(9),                            PAIR_XY | CYCLES(1),                            PAIR_XY | CYCLES(2),

/*      MOV                                             MOV                                             MOV                                             MOV*/
/*a0*/  PAIR_XY | CYCLES_REG,                           PAIR_XY | CYCLES_REG,                           PAIR_XY | CYCLES_REG,                           PAIR_XY | CYCLES_REG,
/*      MOVSB                                           MOVSW                                           CMPSB                                           CMPSW*/
        PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(5),                            PAIR_NP | CYCLES(5),
/*      TEST                                            TEST                                            STOSB                                           STOSW*/
        PAIR_XY | CYCLES_REG,                           PAIR_XY | CYCLES_REG,                           PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(2),
/*      LODSB                                           LODSW                                           SCASB                                           SCASW*/
        PAIR_NP | CYCLES(3),                            PAIR_NP | CYCLES(3),                            PAIR_NP | CYCLES(2),                            PAIR_NP | CYCLES(2),

/*      MOV*/
/*b0*/  PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,
        PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,              PAIR_XY | CYCLES_REG,

/*                                                                                                      RET imm                                         RET*/
/*c0*/  INVALID,                                        INVALID,                                        PAIR_X_BRANCH | CYCLES(3),                      PAIR_X_BRANCH | CYCLES(2),
/*      LES                                             LDS                                             MOV                                             MOV*/
        PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(4),                            PAIR_XY | CYCLES_REG,                           PAIR_XY | CYCLES_REG,
/*      ENTER                                           LEAVE                                           RETF                                            RETF*/
        PAIR_XY | CYCLES(13),                           PAIR_XY | CYCLES(4),                            PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(4),
/*      INT3                                            INT                                             INTO                                            IRET*/
        PAIR_NP | CYCLES(13),                           PAIR_NP | CYCLES(16),                           PAIR_NP | CYCLES(4),                            PAIR_NP | CYCLES(10),


/*d0*/  INVALID,                                        INVALID,                                        INVALID,                                        INVALID,
/*      AAM                                             AAD                                             SETALC                                          XLAT*/
        PAIR_XY | CYCLES(18),                           PAIR_XY | CYCLES(7),                            PAIR_XY | CYCLES(2),                            PAIR_XY | CYCLES(4),
        INVALID,                                        INVALID,                                        INVALID,                                        INVALID,
        INVALID,                                        INVALID,                                        INVALID,                                        INVALID,

/*      LOOPNE                                          LOOPE                                           LOOP                                            JCXZ*/
/*e0*/  PAIR_X_BRANCH| CYCLES_BRANCH,                   PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,                  PAIR_X_BRANCH | CYCLES_BRANCH,
/*      IN AL                                           IN AX                                           OUT_AL                                          OUT_AX*/
        PAIR_NP | CYCLES(14),                           PAIR_NP | CYCLES(14),                           PAIR_NP | CYCLES(14),                           PAIR_NP | CYCLES(14),
/*      CALL                                            JMP                                             JMP                                             JMP*/
        PAIR_X_BRANCH | CYCLES_REG,                     PAIR_X_BRANCH | CYCLES_REG,                     PAIR_NP | CYCLES(1),                            PAIR_X_BRANCH | CYCLES_REG,
/*      IN AL                                           IN AX                                           OUT_AL                                          OUT_AX*/
        PAIR_NP | CYCLES(14),                           PAIR_NP | CYCLES(14),                           PAIR_NP | CYCLES(14),                           PAIR_NP | CYCLES(14),

/*                                                                                                      REPNE                                           REPE*/
/*f0*/  INVALID,                                        INVALID,                                        PAIR_NP | CYCLES(0),                            PAIR_NP | CYCLES(0),
/*      HLT                                             CMC*/
        PAIR_NP | CYCLES(4),                            PAIR_XY | CYCLES(2),                            INVALID,                                        INVALID,
/*      CLC                                             STC                                             CLI                                             STI*/
        PAIR_XY | CYCLES(1),                            PAIR_XY | CYCLES(1),                            PAIR_XY | CYCLES(7),                            PAIR_XY | CYCLES(7),
/*      CLD                                             STD                                             INCDEC*/
        PAIR_XY | CYCLES(7),                            PAIR_XY | CYCLES(7),                            PAIR_XY | CYCLES_REG,                           INVALID
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

/*40*/  PAIR_XY | CYCLES(1),    PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),
        PAIR_XY | CYCLES(1),    PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),
        PAIR_XY | CYCLES(1),    PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),
        PAIR_XY | CYCLES(1),    PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),

/*50*/  INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,

/*60*/  PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,
        PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,
        PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,
        INVALID,                INVALID,                PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,

/*70*/  INVALID,                PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,
        PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,     PAIR_X | CYCLES(1),
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,

/*80*/  PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH,
        PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH,
        PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH,
        PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH,

/*90*/  PAIR_XY | CYCLES(1),    PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),
        PAIR_XY | CYCLES(1),    PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),
        PAIR_XY | CYCLES(1),    PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),
        PAIR_XY | CYCLES(1),    PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),

/*a0*/  PAIR_XY | CYCLES(3),    PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(12),           PAIR_XY | CYCLES(5),
        PAIR_XY | CYCLES(4),    PAIR_XY | CYCLES(5),            INVALID,                        INVALID,
        PAIR_XY | CYCLES(3),    PAIR_XY | CYCLES(1),            INVALID,                        PAIR_XY | CYCLES(5),
        PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(5),            INVALID,                        PAIR_NP | CYCLES(10),

/*b0*/  PAIR_NP | CYCLES(11),   PAIR_NP | CYCLES(11),           PAIR_NP | CYCLES(4),            PAIR_XY | CYCLES(5),
        PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(4),            PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),
        INVALID,                INVALID,                        PAIR_XY | CYCLES(3),            PAIR_XY | CYCLES(5),
        PAIR_XY | CYCLES(3),    PAIR_XY | CYCLES(3),            PAIR_XY | CYCLES(1),            INVALID,

/*c0*/  PAIR_NP | CYCLES(2),    PAIR_NP | CYCLES(2),            INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        PAIR_XY | CYCLES(4),    PAIR_XY | CYCLES(4),            PAIR_XY | CYCLES(4),            PAIR_XY | CYCLES(4),
        PAIR_XY | CYCLES(4),    PAIR_XY | CYCLES(4),            PAIR_XY | CYCLES(4),            PAIR_XY | CYCLES(4),

/*d0*/  INVALID,                PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,
        INVALID,                PAIR_X | CYCLES_RM,     INVALID,                INVALID,
        PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,     INVALID,                PAIR_X | CYCLES_RM,
        PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,     INVALID,                PAIR_X | CYCLES_RM,

/*e0*/  INVALID,                PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,     INVALID,
        INVALID,                PAIR_X | CYCLES_RM,     INVALID,                INVALID,
        PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,     INVALID,                PAIR_X | CYCLES_RM,
        PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,     INVALID,                PAIR_X | CYCLES_RM,

/*f0*/  INVALID,                PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,
        INVALID,                PAIR_X | CYCLES_RM,     INVALID,                INVALID,
        PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,     INVALID,
        PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,     PAIR_X | CYCLES_RM,     INVALID,
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

/*30*/  PAIR_NP | CYCLES(9),    CYCLES(1),                      PAIR_NP | CYCLES(9),            INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,

/*40*/  PAIR_XY | CYCLES(1),    PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),
        PAIR_XY | CYCLES(1),    PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),
        PAIR_XY | CYCLES(1),    PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),
        PAIR_XY | CYCLES(1),    PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),

/*50*/  INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,

/*60*/  PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,
        PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,
        PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,
        INVALID,                INVALID,                PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,

/*70*/  INVALID,                PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,
        PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,    PAIR_X | CYCLES(1),
        INVALID,                INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,

/*80*/  PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH,
        PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH,
        PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH,
        PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH, PAIR_X_BRANCH | CYCLES_BRANCH,

/*90*/  PAIR_XY | CYCLES(1),    PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),
        PAIR_XY | CYCLES(1),    PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),
        PAIR_XY | CYCLES(1),    PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),
        PAIR_XY | CYCLES(1),    PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),

/*a0*/  PAIR_XY | CYCLES(3),    PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(12),           PAIR_XY | CYCLES(5),
        PAIR_XY | CYCLES(4),    PAIR_XY | CYCLES(5),            INVALID,                        INVALID,
        PAIR_XY | CYCLES(3),    PAIR_XY | CYCLES(1),            INVALID,                        PAIR_XY | CYCLES(5),
        PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(5),            INVALID,                        PAIR_NP | CYCLES(10),

/*b0*/  PAIR_NP | CYCLES(11),   PAIR_NP | CYCLES(11),           PAIR_NP | CYCLES(4),            PAIR_XY | CYCLES(5),
        PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(4),            PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),
        INVALID,                INVALID,                        PAIR_XY | CYCLES(3),            PAIR_XY | CYCLES(5),
        PAIR_XY | CYCLES(3),    PAIR_XY | CYCLES(3),            PAIR_XY | CYCLES(1),            INVALID,
/*c0*/  PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(4),            INVALID,                        INVALID,
        INVALID,                INVALID,                        INVALID,                        INVALID,
        PAIR_NP | CYCLES(1),    PAIR_NP | CYCLES(1),            PAIR_NP | CYCLES(1),            PAIR_NP | CYCLES(1),
        PAIR_NP | CYCLES(1),    PAIR_NP | CYCLES(1),            PAIR_NP | CYCLES(1),            PAIR_NP | CYCLES(1),

/*d0*/  INVALID,                PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,
        INVALID,                PAIR_X | CYCLES_REG,    INVALID,                INVALID,
        PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,    INVALID,                PAIR_X | CYCLES_REG,
        PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,    INVALID,                PAIR_X | CYCLES_REG,

/*e0*/  INVALID,                PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,    INVALID,
        INVALID,                PAIR_X | CYCLES_REG,    INVALID,                INVALID,
        PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,    INVALID,                PAIR_X | CYCLES_REG,
        PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,    INVALID,                PAIR_X | CYCLES_REG,

/*f0*/  INVALID,                PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,
        INVALID,                PAIR_X | CYCLES_REG,    INVALID,                INVALID,
        PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,    INVALID,
        PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,    PAIR_X | CYCLES_REG,    INVALID,
};

static uint32_t opcode_timings_shift[8] =
{
        PAIR_XY | CYCLES_RMW,    PAIR_XY | CYCLES_RMW,    PAIR_XY | CYCLES(3),    PAIR_XY | CYCLES(4),
        PAIR_XY | CYCLES_RMW,    PAIR_XY | CYCLES_RMW,    PAIR_XY | CYCLES_RMW,   PAIR_XY | CYCLES_RMW,
};
static uint32_t opcode_timings_shift_mod3[8] =
{
        PAIR_XY | CYCLES_REG,    PAIR_XY | CYCLES_REG,    PAIR_XY | CYCLES(3),     PAIR_XY | CYCLES(4),
        PAIR_XY | CYCLES_REG,    PAIR_XY | CYCLES_REG,    PAIR_XY | CYCLES_REG,    PAIR_XY | CYCLES_REG,
};
static uint32_t opcode_timings_shift_imm[8] =
{
        PAIR_XY | CYCLES_RMW,    PAIR_XY | CYCLES_RMW,    PAIR_XY | CYCLES(8),    PAIR_XY | CYCLES(9),
        PAIR_XY | CYCLES_RMW,    PAIR_XY | CYCLES_RMW,    PAIR_XY | CYCLES_RMW,   PAIR_XY | CYCLES_RMW,
};
static uint32_t opcode_timings_shift_imm_mod3[8] =
{
        PAIR_XY | CYCLES_REG,    PAIR_XY | CYCLES_REG,    PAIR_XY | CYCLES(3),     PAIR_XY | CYCLES(4),
        PAIR_XY | CYCLES_REG,    PAIR_XY | CYCLES_REG,    PAIR_XY | CYCLES_REG,    PAIR_XY | CYCLES_REG,
};
static uint32_t opcode_timings_shift_cl[8] =
{
        PAIR_XY | CYCLES(2),     PAIR_XY | CYCLES(2),     PAIR_XY | CYCLES(8),    PAIR_XY | CYCLES(9),
        PAIR_XY | CYCLES(2),     PAIR_XY | CYCLES(2),     PAIR_XY | CYCLES(2),    PAIR_XY | CYCLES(2),
};
static uint32_t opcode_timings_shift_cl_mod3[8] =
{
        PAIR_XY | CYCLES(2),    PAIR_XY | CYCLES(2),    PAIR_XY | CYCLES(8),     PAIR_XY | CYCLES(9),
        PAIR_XY | CYCLES(2),    PAIR_XY | CYCLES(2),    PAIR_XY | CYCLES(2),     PAIR_XY | CYCLES(2),
};

static uint32_t opcode_timings_f6[8] =
{
/*      TST                                                                     NOT                     NEG*/
        PAIR_XY | CYCLES_RM,    INVALID,                PAIR_XY | CYCLES(1),    PAIR_XY | CYCLES(1),
/*      MUL                     IMUL                    DIV                     IDIV*/
        PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(18),   PAIR_NP | CYCLES(18)
};
static uint32_t opcode_timings_f6_mod3[8] =
{
/*      TST                                             NOT                     NEG*/
        PAIR_XY | CYCLES_REG,   INVALID,                PAIR_XY | CYCLES(1),    PAIR_XY | CYCLES(1),
/*      MUL                     IMUL                    DIV                     IDIV*/
        PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(4),    PAIR_NP | CYCLES(18),   PAIR_NP | CYCLES(18)
};
static uint32_t opcode_timings_f7[8] =
{
/*      TST                                                                     NOT                             NEG*/
        PAIR_XY | CYCLES_REG,                   INVALID,                        PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),
/*      MUL                                     IMUL                            DIV                             IDIV*/
        PAIR_NP | CYCLES_MULTI(4,10),           PAIR_NP | CYCLES_MULTI(4,10),   PAIR_NP | CYCLES_MULTI(19,27),  PAIR_NP | CYCLES_MULTI(22,30)
};
static uint32_t opcode_timings_f7_mod3[8] =
{
/*      TST                                                                     NOT                             NEG*/
        PAIR_XY | CYCLES_REG,                   INVALID,                        PAIR_XY | CYCLES(1),            PAIR_XY | CYCLES(1),
/*      MUL                                     IMUL                            DIV                             IDIV*/
        PAIR_NP | CYCLES_MULTI(4,10),           PAIR_NP | CYCLES_MULTI(4,10),   PAIR_NP | CYCLES_MULTI(19,27),  PAIR_NP | CYCLES_MULTI(22,30)
};
static uint32_t opcode_timings_ff[8] =
{
/*      INC                        DEC                     CALL                       CALL far*/
        PAIR_XY | CYCLES_RMW,      PAIR_XY | CYCLES_RMW,   PAIR_X_BRANCH | CYCLES(3), PAIR_NP | CYCLES(5),
/*      JMP                        JMP far                 PUSH*/
        PAIR_X_BRANCH | CYCLES(3), PAIR_NP | CYCLES(5),    PAIR_XY | CYCLES(1),       INVALID
};
static uint32_t opcode_timings_ff_mod3[8] =
{
/*      INC                        DEC                    CALL                       CALL far*/
        PAIR_XY | CYCLES_REG,      PAIR_XY | CYCLES_REG,  PAIR_X_BRANCH | CYCLES(1), PAIR_XY | CYCLES(5),
/*      JMP                        JMP far                PUSH*/
        PAIR_X_BRANCH | CYCLES(1), PAIR_XY | CYCLES(5),   PAIR_XY | CYCLES(2),       INVALID
};

static uint32_t opcode_timings_d8[8] =
{
/*      FADDs                   FMULs                   FCOMs                   FCOMPs*/
        PAIR_X | CYCLES(7),     PAIR_X | CYCLES(6),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),
/*      FSUBs                   FSUBRs                  FDIVs                   FDIVRs*/
        PAIR_X | CYCLES(7),     PAIR_X | CYCLES(7),     PAIR_X | CYCLES(34),    PAIR_X | CYCLES(34)
};
static uint32_t opcode_timings_d8_mod3[8] =
{
/*      FADD                    FMUL                    FCOM                    FCOMP*/
        PAIR_X | CYCLES(7),     PAIR_X | CYCLES(6),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),
/*      FSUB                    FSUBR                   FDIV                    FDIVR*/
        PAIR_X | CYCLES(7),     PAIR_X | CYCLES(7),     PAIR_X | CYCLES(34),    PAIR_X | CYCLES(34)
};

static uint32_t opcode_timings_d9[8] =
{
/*      FLDs                                            FSTs                      FSTPs*/
        PAIR_X | CYCLES(2),       INVALID,              PAIR_X | CYCLES(2),       PAIR_X | CYCLES(2),
/*      FLDENV                    FLDCW                 FSTENV                    FSTCW*/
        PAIR_X | CYCLES(30),      PAIR_X | CYCLES(4),   PAIR_X | CYCLES(24),      PAIR_X | CYCLES(5)
};
static uint32_t opcode_timings_d9_mod3[64] =
{
        /*FLD*/
        PAIR_X | CYCLES(2),     PAIR_X | CYCLES(2),     PAIR_X | CYCLES(2),     PAIR_X | CYCLES(2),
        PAIR_X | CYCLES(2),     PAIR_X | CYCLES(2),     PAIR_X | CYCLES(2),     PAIR_X | CYCLES(2),
        /*FXCH*/
        PAIR_X | CYCLES(3),     PAIR_X | CYCLES(3),     PAIR_X | CYCLES(3),     PAIR_X | CYCLES(3),
        PAIR_X | CYCLES(3),     PAIR_X | CYCLES(3),     PAIR_X | CYCLES(3),     PAIR_X | CYCLES(3),
        /*FNOP*/
        PAIR_X | CYCLES(2),     INVALID,                INVALID,                INVALID,
        INVALID,                INVALID,                INVALID,                INVALID,
        /*FSTP*/
        PAIR_X | CYCLES(2),     PAIR_X | CYCLES(2),     PAIR_X | CYCLES(2),     PAIR_X | CYCLES(2),
        PAIR_X | CYCLES(2),     PAIR_X | CYCLES(2),     PAIR_X | CYCLES(2),     PAIR_X | CYCLES(2),
/*      opFCHS                  opFABS*/
        PAIR_X | CYCLES(2),     PAIR_X | CYCLES(2),     INVALID,                INVALID,
/*      opFTST                  opFXAM (oddly low) */
        PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     INVALID,                INVALID,
/*      opFLD1                  opFLDL2T                opFLDL2E                opFLDPI*/
        PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),
/*      opFLDEG2                opFLDLN2                opFLDZ*/
        PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     INVALID,
/*      opF2XM1                 opFYL2X                 opFPTAN                 opFPATAN*/
        PAIR_X | CYCLES(92),    PAIR_X | CYCLES(170),   PAIR_X | CYCLES(129),   PAIR_X | CYCLES(161),
/*                                                      opFDECSTP               opFINCSTP,*/
        INVALID,                INVALID,                PAIR_X | CYCLES(4),     PAIR_X | CYCLES(2),
/*      opFPREM                                         opFSQRT                 opFSINCOS*/
        PAIR_X | CYCLES(91),    INVALID,                PAIR_X | CYCLES(60),    PAIR_X | CYCLES(161),
/*      opFRNDINT               opFSCALE                opFSIN                  opFCOS*/
        PAIR_X | CYCLES(20),    PAIR_X | CYCLES(14),    PAIR_X | CYCLES(140),   PAIR_X | CYCLES(141)
};

static uint32_t opcode_timings_da[8] =
{
/*      FIADDl                  FIMULl                  FICOMl                  FICOMPl*/
        PAIR_X | CYCLES(12),    PAIR_X | CYCLES(11),    PAIR_X | CYCLES(10),    PAIR_X | CYCLES(10),
/*      FISUBl                  FISUBRl                 FIDIVl                  FIDIVRl*/
        PAIR_X | CYCLES(29),    PAIR_X | CYCLES(27),    PAIR_X | CYCLES(38),    PAIR_X | CYCLES(48)
};
static uint32_t opcode_timings_da_mod3[8] =
{
        PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),
        INVALID,                PAIR_X | CYCLES(5),     INVALID,                INVALID
};


static uint32_t opcode_timings_db[8] =
{
/*      FLDil                                           FSTil                   FSTPil*/
        PAIR_X | CYCLES(2),     INVALID,                PAIR_X | CYCLES(2),     PAIR_X | CYCLES(2),
/*                              FLDe                                            FSTPe*/
        INVALID,                PAIR_X | CYCLES(2),     INVALID,                PAIR_X | CYCLES(2)
};
static uint32_t opcode_timings_db_mod3[64] =
{
        PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),
        PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),

        PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),
        PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),

        PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),
        PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),

        PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),
        PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),

/*                              opFNOP                  opFCLEX                 opFINIT*/
        INVALID,                PAIR_X | CYCLES(2),     PAIR_X | CYCLES(5),     PAIR_X | CYCLES(8),
/*      opFNOP                  opFNOP*/
        PAIR_X | CYCLES(2),     PAIR_X | CYCLES(2),     INVALID,                INVALID,

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
        PAIR_X | CYCLES(7),     PAIR_X | CYCLES(7),     PAIR_X | CYCLES(7),     PAIR_X | CYCLES(7),
/*      FSUBd                   FSUBRd                  FDIVd                   FDIVRd*/
        PAIR_X | CYCLES(7),     PAIR_X | CYCLES(7),     PAIR_X | CYCLES(34),    PAIR_X | CYCLES(34)
};
static uint32_t opcode_timings_dc_mod3[8] =
{
/*      opFADDr                 opFMULr*/
        PAIR_X | CYCLES(7),     PAIR_X | CYCLES(7),     INVALID,                INVALID,
/*      opFSUBRr                opFSUBr                 opFDIVRr                opFDIVr*/
        PAIR_X | CYCLES(7),     PAIR_X | CYCLES(7),     PAIR_X | CYCLES(34),    PAIR_X | CYCLES(34)
};

static uint32_t opcode_timings_dd[8] =
{
/*      FLDd                                            FSTd                    FSTPd*/
        PAIR_X | CYCLES(2),     INVALID,                PAIR_X | CYCLES(2),     PAIR_X | CYCLES(2),
/*      FRSTOR                                          FSAVE                   FSTSW*/
        PAIR_X | CYCLES(72),    INVALID,                PAIR_X | CYCLES(67),    PAIR_X | CYCLES(2)
};
static uint32_t opcode_timings_dd_mod3[8] =
{
/*      FFFREE                                          FST                     FSTP*/
        PAIR_X | CYCLES(3),     INVALID,                PAIR_X | CYCLES(2),     PAIR_X | CYCLES(2),
/*      FUCOM                   FUCOMP*/
        PAIR_X | CYCLES(4),     PAIR_X | CYCLES(4),     INVALID,                INVALID
};

static uint32_t opcode_timings_de[8] =
{
/*      FIADDw                  FIMULw                  FICOMw                  FICOMPw*/
        PAIR_X | CYCLES(12),    PAIR_X | CYCLES(11),    PAIR_X | CYCLES(10),    PAIR_X | CYCLES(10),
/*      FISUBw                  FISUBRw                 FIDIVw                  FIDIVRw*/
        PAIR_X | CYCLES(27),    PAIR_X | CYCLES(27),    PAIR_X | CYCLES(38),    PAIR_X | CYCLES(38)
};
static uint32_t opcode_timings_de_mod3[8] =
{
/*      FADD                    FMUL                                            FCOMPP*/
        PAIR_X | CYCLES(7),     PAIR_X | CYCLES(7),     INVALID,                PAIR_X | CYCLES(7),
/*      FSUB                    FSUBR                   FDIV                    FDIVR*/
        PAIR_X | CYCLES(7),     PAIR_X | CYCLES(7),     PAIR_X | CYCLES(34),    PAIR_X | CYCLES(34)
};

static uint32_t opcode_timings_df[8] =
{
/*      FILDiw                                          FISTiw                  FISTPiw*/
        PAIR_X | CYCLES(8),     INVALID,                PAIR_X | CYCLES(10),    PAIR_X | CYCLES(13),
/*                              FILDiq                  FBSTP                   FISTPiq*/
        INVALID,                PAIR_X | CYCLES(8),     PAIR_X | CYCLES(63),    PAIR_X | CYCLES(13)
};
static uint32_t opcode_timings_df_mod3[8] =
{
        INVALID,                INVALID,                INVALID,                INVALID,
/*      FSTSW AX*/
        PAIR_X | CYCLES(6),     INVALID,                INVALID,                INVALID
};

static uint32_t opcode_timings_8x[8] =
{
        PAIR_XY | CYCLES_RMW,      PAIR_XY | CYCLES_RMW,      PAIR_XY | CYCLES_RMW,      PAIR_XY | CYCLES_RMW,
        PAIR_XY | CYCLES_RMW,      PAIR_XY | CYCLES_RMW,      PAIR_XY | CYCLES_RMW,      PAIR_XY | CYCLES_RM
};
static uint32_t opcode_timings_8x_mod3[8] =
{
        PAIR_XY | CYCLES_REG,      PAIR_XY | CYCLES_REG,      PAIR_XY | CYCLES_REG,      PAIR_XY | CYCLES_REG,
        PAIR_XY | CYCLES_REG,      PAIR_XY | CYCLES_REG,      PAIR_XY | CYCLES_REG,      PAIR_XY | CYCLES_REG
};
static uint32_t opcode_timings_81[8] =
{
        PAIR_XY | CYCLES_RMW,      PAIR_XY | CYCLES_RMW,      PAIR_XY | CYCLES_RMW,      PAIR_XY | CYCLES_RMW,
        PAIR_XY | CYCLES_RMW,      PAIR_XY | CYCLES_RMW,      PAIR_XY | CYCLES_RMW,      PAIR_XY | CYCLES_RM
};
static uint32_t opcode_timings_81_mod3[8] =
{
        PAIR_XY | CYCLES_REG,      PAIR_XY | CYCLES_REG,      PAIR_XY | CYCLES_REG,      PAIR_XY | CYCLES_REG,
        PAIR_XY | CYCLES_REG,      PAIR_XY | CYCLES_REG,      PAIR_XY | CYCLES_REG,      PAIR_XY | CYCLES_REG
};

static int decode_delay;
static uint8_t last_prefix;

static inline int COUNT(uint32_t c, int op_32)
{
        if (c & CYCLES_HAS_MULTI)
        {
                if (op_32 & 0x100)
                        return ((uintptr_t)c >> 8) & 0xff;
                return (uintptr_t)c & 0xff;
        }
        if (!(c & PAIR_MASK))
                return c & 0xffff;

        return c & CYCLES_MASK;
}

void codegen_timing_686_block_start()
{
        prev_full = decode_delay = 0;
        regmask_modified = last_regmask_modified = 0;
}

void codegen_timing_686_start()
{
        decode_delay = 0;
        last_prefix = 0;
}

void codegen_timing_686_prefix(uint8_t prefix, uint32_t fetchdat)
{
        if ((prefix & 0xf8) == 0xd8)
        {
                last_prefix = prefix;
                return;
        }
        if (prefix == 0x0f && (fetchdat & 0xf0) == 0x80)
        {
                /*0fh prefix is 'free' when used on conditional jumps*/
                last_prefix = prefix;
                return;
        }

        /*6x86 can decode 1 prefix per instruction per clock with no penalty. If
          either instruction has more than one prefix then decode is delayed by
          one cycle for each additional prefix*/
        decode_delay++;
        last_prefix = prefix;
}

static int check_agi(uint64_t *deps, uint8_t opcode, uint32_t fetchdat, int op_32)
{
        uint32_t addr_regmask = get_addr_regmask(deps[opcode], fetchdat, op_32);

        if (addr_regmask & IMPL_ESP)
                addr_regmask |= (1 << REG_ESP);

        if (regmask_modified & addr_regmask)
        {
                regmask_modified = 0;
                return 2;
        }

        if (last_regmask_modified & addr_regmask)
                return 1;

        return 0;
}

void codegen_timing_686_opcode(uint8_t opcode, uint32_t fetchdat, int op_32, uint32_t op_pc)
{
        uint32_t *timings;
        uint64_t *deps;
        int mod3 = ((fetchdat & 0xc0) == 0xc0);
        int bit8 = !(opcode & 1);

        switch (last_prefix)
        {
                case 0x0f:
                timings = mod3 ? opcode_timings_0f_mod3 : opcode_timings_0f;
                deps = mod3 ? opcode_deps_0f_mod3 : opcode_deps_0f;
                break;

                case 0xd8:
                timings = mod3 ? opcode_timings_d8_mod3 : opcode_timings_d8;
                deps = mod3 ? opcode_deps_d8_mod3 : opcode_deps_d8;
                opcode = (opcode >> 3) & 7;
                break;
                case 0xd9:
                timings = mod3 ? opcode_timings_d9_mod3 : opcode_timings_d9;
                deps = mod3 ? opcode_deps_d9_mod3 : opcode_deps_d9;
                opcode = mod3 ? opcode & 0x3f : (opcode >> 3) & 7;
                break;
                case 0xda:
                timings = mod3 ? opcode_timings_da_mod3 : opcode_timings_da;
                deps = mod3 ? opcode_deps_da_mod3 : opcode_deps_da;
                opcode = (opcode >> 3) & 7;
                break;
                case 0xdb:
                timings = mod3 ? opcode_timings_db_mod3 : opcode_timings_db;
                deps = mod3 ? opcode_deps_db_mod3 : opcode_deps_db;
                opcode = mod3 ? opcode & 0x3f : (opcode >> 3) & 7;
                break;
                case 0xdc:
                timings = mod3 ? opcode_timings_dc_mod3 : opcode_timings_dc;
                deps = mod3 ? opcode_deps_dc_mod3 : opcode_deps_dc;
                opcode = (opcode >> 3) & 7;
                break;
                case 0xdd:
                timings = mod3 ? opcode_timings_dd_mod3 : opcode_timings_dd;
                deps = mod3 ? opcode_deps_dd_mod3 : opcode_deps_dd;
                opcode = (opcode >> 3) & 7;
                break;
                case 0xde:
                timings = mod3 ? opcode_timings_de_mod3 : opcode_timings_de;
                deps = mod3 ? opcode_deps_de_mod3 : opcode_deps_de;
                opcode = (opcode >> 3) & 7;
                break;
                case 0xdf:
                timings = mod3 ? opcode_timings_df_mod3 : opcode_timings_df;
                deps = mod3 ? opcode_deps_df_mod3 : opcode_deps_df;
                opcode = (opcode >> 3) & 7;
                break;

                default:
                switch (opcode)
                {
                        case 0x80: case 0x82: case 0x83:
                        timings = mod3 ? opcode_timings_8x_mod3 : opcode_timings_8x;
                        deps = mod3 ? opcode_deps_8x_mod3 : opcode_deps_8x;
                        opcode = (fetchdat >> 3) & 7;
                        break;
                        case 0x81:
                        timings = mod3 ? opcode_timings_81_mod3 : opcode_timings_81;
                        deps = mod3 ? opcode_deps_81_mod3 : opcode_deps_81;
                        opcode = (fetchdat >> 3) & 7;
                        break;

                        case 0xc0: case 0xc1:
                        timings = mod3 ? opcode_timings_shift_imm_mod3 : opcode_timings_shift_imm;
                        deps = mod3 ? opcode_deps_shift_mod3 : opcode_deps_shift;
                        opcode = (fetchdat >> 3) & 7;
                        break;

                        case 0xd0: case 0xd1:
                        timings = mod3 ? opcode_timings_shift_mod3 : opcode_timings_shift;
                        deps = mod3 ? opcode_deps_shift_mod3 : opcode_deps_shift;
                        opcode = (fetchdat >> 3) & 7;
                        break;

                        case 0xd2: case 0xd3:
                        timings = mod3 ? opcode_timings_shift_cl_mod3 : opcode_timings_shift_cl;
                        deps = mod3 ? opcode_deps_shift_cl_mod3 : opcode_deps_shift_cl;
                        opcode = (fetchdat >> 3) & 7;
                        break;

                        case 0xf6:
                        timings = mod3 ? opcode_timings_f6_mod3 : opcode_timings_f6;
                        deps = mod3 ? opcode_deps_f6_mod3 : opcode_deps_f6;
                        opcode = (fetchdat >> 3) & 7;
                        break;
                        case 0xf7:
                        timings = mod3 ? opcode_timings_f7_mod3 : opcode_timings_f7;
                        deps = mod3 ? opcode_deps_f7_mod3 : opcode_deps_f7;
                        opcode = (fetchdat >> 3) & 7;
                        break;
                        case 0xff:
                        timings = mod3 ? opcode_timings_ff_mod3 : opcode_timings_ff;
                        deps = mod3 ? opcode_deps_ff_mod3 : opcode_deps_ff;
                        opcode = (fetchdat >> 3) & 7;
                        break;

                        default:
                        timings = mod3 ? opcode_timings_mod3 : opcode_timings;
                        deps = mod3 ? opcode_deps_mod3 : opcode_deps;
                        break;
                }
        }

        /*One prefix per instruction is free*/
        decode_delay--;
        if (decode_delay < 0)
                decode_delay = 0;

        if (prev_full)
        {
                uint32_t regmask = get_srcdep_mask(deps[opcode], fetchdat, bit8, op_32);
                int agi_stall = 0;

                if (regmask & IMPL_ESP)
                        regmask |= SRCDEP_ESP | DSTDEP_ESP;

		agi_stall = check_agi(prev_deps, prev_opcode, prev_fetchdat, prev_op_32);

                /*Second instruction in the pair*/
                if ((timings[opcode] & PAIR_MASK) == PAIR_NP)
                {
                        /*Instruction can not pair with previous*/
                        /*Run previous now*/
                        codegen_block_cycles += COUNT(prev_timings[prev_opcode], prev_op_32) + decode_delay + agi_stall;
                        decode_delay = (-COUNT(prev_timings[prev_opcode], prev_op_32)) + 1 + agi_stall;
                        prev_full = 0;
                        last_regmask_modified = regmask_modified;
                        regmask_modified = prev_regmask;
                }
                else if (((timings[opcode] & PAIR_MASK) == PAIR_X || (timings[opcode] & PAIR_MASK) == PAIR_X_BRANCH)
                          && (prev_timings[opcode] & PAIR_MASK) == PAIR_X)
                {
                        /*Instruction can not pair with previous*/
                        /*Run previous now*/
                        codegen_block_cycles += COUNT(prev_timings[prev_opcode], prev_op_32) + decode_delay + agi_stall;
                        decode_delay = (-COUNT(prev_timings[prev_opcode], prev_op_32)) + 1 + agi_stall;
                        prev_full = 0;
                        last_regmask_modified = regmask_modified;
                        regmask_modified = prev_regmask;
                }
                else if (prev_regmask & regmask)
                {
                        /*Instruction can not pair with previous*/
                        /*Run previous now*/
                        codegen_block_cycles += COUNT(prev_timings[prev_opcode], prev_op_32) + decode_delay + agi_stall;
                        decode_delay = (-COUNT(prev_timings[prev_opcode], prev_op_32)) + 1 + agi_stall;
                        prev_full = 0;
                        last_regmask_modified = regmask_modified;
                        regmask_modified = prev_regmask;
                }
                else
                {
                        int t1 = COUNT(prev_timings[prev_opcode], prev_op_32);
                        int t2 = COUNT(timings[opcode], op_32);
                        int t_pair = (t1 > t2) ? t1 : t2;

                        if (!t_pair)
                                fatal("Pairable 0 cycles! %02x %02x\n", opcode, prev_opcode);

			agi_stall = check_agi(deps, opcode, fetchdat, op_32);

                        codegen_block_cycles += t_pair + agi_stall;
                        decode_delay = (-t_pair) + 1 + agi_stall;

                        last_regmask_modified = regmask_modified;
                        regmask_modified = get_dstdep_mask(deps[opcode], fetchdat, bit8) | prev_regmask;
                        prev_full = 0;
                        return;
                }
        }

        if (!prev_full)
        {
                /*First instruction in the pair*/
                if ((timings[opcode] & PAIR_MASK) == PAIR_NP || (timings[opcode] & PAIR_MASK) == PAIR_X_BRANCH)
                {
                        /*Instruction not pairable*/
                        int agi_stall = 0;

			agi_stall = check_agi(deps, opcode, fetchdat, op_32);

                        codegen_block_cycles += COUNT(timings[opcode], op_32) + decode_delay + agi_stall;
                        decode_delay = (-COUNT(timings[opcode], op_32)) + 1 + agi_stall;
                        last_regmask_modified = regmask_modified;
                        regmask_modified = get_dstdep_mask(deps[opcode], fetchdat, bit8);
                }
                else
                {
                        /*Instruction might pair with next*/
                        prev_full = 1;
                        prev_opcode = opcode;
                        prev_timings = timings;
                        prev_op_32 = op_32;
                        prev_regmask = get_dstdep_mask(deps[opcode], fetchdat, bit8);
                        if (prev_regmask & IMPL_ESP)
                                prev_regmask |= SRCDEP_ESP | DSTDEP_ESP;
                        prev_deps = deps;
                        prev_fetchdat = fetchdat;
                        return;
                }
        }
}

void codegen_timing_686_block_end()
{
        if (prev_full)
        {
                /*Run previous now*/
                codegen_block_cycles += COUNT(prev_timings[prev_opcode], prev_op_32) + decode_delay;
                prev_full = 0;
        }
}

codegen_timing_t codegen_timing_686 =
{
        codegen_timing_686_start,
        codegen_timing_686_prefix,
        codegen_timing_686_opcode,
        codegen_timing_686_block_start,
        codegen_timing_686_block_end,
        NULL
};
