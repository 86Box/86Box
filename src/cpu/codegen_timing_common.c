#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

#include "codegen_timing_common.h"

uint64_t opcode_deps[256] =
{
/*      ADD                                     ADD                                     ADD                              ADD*/
/*00*/  SRCDEP_REG | MODRM,                     SRCDEP_REG | MODRM,                     SRCDEP_REG | DSTDEP_REG | MODRM, SRCDEP_REG | DSTDEP_REG | MODRM,
/*      ADD                                     ADD                                     PUSH ES                          POP ES*/
        SRCDEP_EAX | DSTDEP_EAX | HAS_IMM8,     SRCDEP_EAX | DSTDEP_EAX | HAS_IMM1632,  IMPL_ESP,                        IMPL_ESP,
/*      OR                                      OR                                      OR                               OR*/
        SRCDEP_REG | MODRM,                     SRCDEP_REG | MODRM,                     SRCDEP_REG | DSTDEP_REG | MODRM, SRCDEP_REG | DSTDEP_REG | MODRM,
/*      OR                                      OR                                      PUSH CS*/
        SRCDEP_EAX | DSTDEP_EAX | HAS_IMM8,     SRCDEP_EAX | DSTDEP_EAX | HAS_IMM1632,  IMPL_ESP,                        0,

/*      ADC                                     ADC                                     ADC                              ADC*/
/*10*/  SRCDEP_REG | MODRM,                     SRCDEP_REG | MODRM,                     SRCDEP_REG | DSTDEP_REG | MODRM, SRCDEP_REG | DSTDEP_REG | MODRM,
/*      ADC                                     ADC                                     PUSH SS                          POP SS*/
        SRCDEP_EAX | DSTDEP_EAX | HAS_IMM8,     SRCDEP_EAX | DSTDEP_EAX | HAS_IMM1632,  IMPL_ESP,                        IMPL_ESP,
/*      SBB                                     SBB                                     SBB                              SBB*/
        SRCDEP_REG | MODRM,                     SRCDEP_REG | MODRM,                     SRCDEP_REG | DSTDEP_REG | MODRM, SRCDEP_REG | DSTDEP_REG | MODRM,
/*      SBB                                     SBB                                     PUSH DS                          POP DS*/
        SRCDEP_EAX | DSTDEP_EAX | HAS_IMM8,     SRCDEP_EAX | DSTDEP_EAX | HAS_IMM1632,  IMPL_ESP,                        IMPL_ESP,

/*      AND                                     AND                                     AND                              AND*/
/*20*/  SRCDEP_REG | MODRM,                     SRCDEP_REG | MODRM,                     SRCDEP_REG | DSTDEP_REG | MODRM, SRCDEP_REG | DSTDEP_REG | MODRM,
/*      AND                                     AND                                                                      DAA*/
        SRCDEP_EAX | DSTDEP_EAX | HAS_IMM8,     SRCDEP_EAX | DSTDEP_EAX | HAS_IMM1632,  0,                               SRCDEP_EAX | DSTDEP_EAX,
/*      SUB                                     SUB                                     SUB                              SUB*/
        SRCDEP_REG | MODRM,                     SRCDEP_REG | MODRM,                     SRCDEP_REG | DSTDEP_REG | MODRM, SRCDEP_REG | DSTDEP_REG | MODRM,
/*      SUB                                     SUB                                                                      DAS*/
        SRCDEP_EAX | DSTDEP_EAX | HAS_IMM8,     SRCDEP_EAX | DSTDEP_EAX | HAS_IMM1632,  0,                               SRCDEP_EAX | DSTDEP_EAX,

/*      XOR                                     XOR                                     XOR                              XOR*/
/*30*/  SRCDEP_REG | MODRM,                     SRCDEP_REG | MODRM,                     SRCDEP_REG | DSTDEP_REG | MODRM, SRCDEP_REG | DSTDEP_REG | MODRM,
/*      XOR                                     XOR                                                                      AAA*/
        SRCDEP_EAX | DSTDEP_EAX | HAS_IMM8,     SRCDEP_EAX | DSTDEP_EAX | HAS_IMM1632,  0,                               SRCDEP_EAX | DSTDEP_EAX,
/*      CMP                                     CMP                                     CMP                              CMP*/
        SRCDEP_REG | MODRM,                     SRCDEP_REG | MODRM,                     SRCDEP_REG | MODRM,              SRCDEP_REG | MODRM,
/*      CMP                                     CMP                                                                      AAS*/
        SRCDEP_EAX | HAS_IMM8,                  SRCDEP_EAX | HAS_IMM1632,               0,                               SRCDEP_EAX | DSTDEP_EAX,

/*      INC EAX                  INC ECX                  INC EDX                  INC EBX*/
/*40*/  SRCDEP_EAX | DSTDEP_EAX, SRCDEP_ECX | DSTDEP_ECX, SRCDEP_EDX | DSTDEP_EDX, SRCDEP_EBX | DSTDEP_EBX,
/*      INC ESP                  INC EBP                  INC ESI                  INC EDI*/
        SRCDEP_ESP | DSTDEP_ESP, SRCDEP_EBP | DSTDEP_EBP, SRCDEP_ESI | DSTDEP_ESI, SRCDEP_EDI | DSTDEP_EDI,
/*      DEC EAX                  DEC ECX                  DEC EDX                  DEC EBX*/
        SRCDEP_EAX | DSTDEP_EAX, SRCDEP_ECX | DSTDEP_ECX, SRCDEP_EDX | DSTDEP_EDX, SRCDEP_EBX | DSTDEP_EBX,
/*      DEC ESP                  DEC EBP                  DEC ESI                  DEC EDI*/
        SRCDEP_ESP | DSTDEP_ESP, SRCDEP_EBP | DSTDEP_EBP, SRCDEP_ESI | DSTDEP_ESI, SRCDEP_EDI | DSTDEP_EDI,

/*      PUSH EAX               PUSH ECX               PUSH EDX               PUSH EBX*/
/*50*/  SRCDEP_EAX | IMPL_ESP, SRCDEP_ECX | IMPL_ESP, SRCDEP_EDX | IMPL_ESP, SRCDEP_EBX | IMPL_ESP,
/*      PUSH ESP               PUSH EBP               PUSH ESI               PUSH EDI*/
        SRCDEP_ESP | IMPL_ESP, SRCDEP_EBP | IMPL_ESP, SRCDEP_ESI | IMPL_ESP, SRCDEP_EDI | IMPL_ESP,
/*      POP EAX                POP ECX                POP EDX                POP EBX*/
        DSTDEP_EAX | IMPL_ESP, DSTDEP_ECX | IMPL_ESP, DSTDEP_EDX | IMPL_ESP, DSTDEP_EBX | IMPL_ESP,
/*      POP ESP                POP EBP                POP ESI                POP EDI*/
        DSTDEP_ESP | IMPL_ESP, DSTDEP_EBP | IMPL_ESP, DSTDEP_ESI | IMPL_ESP, DSTDEP_EDI | IMPL_ESP,

/*      PUSHA                  POPA                     BOUND                    ARPL*/
/*60*/  IMPL_ESP,              IMPL_ESP,                0,                       0,
        0,                     0,                       0,                       0,
/*      PUSH imm               IMUL                     PUSH imm                 IMUL*/
        IMPL_ESP | HAS_IMM1632,DSTDEP_REG | MODRM,      IMPL_ESP | HAS_IMM8,     DSTDEP_REG | MODRM,
/*      INSB                   INSW                     OUTSB                    OUTSW*/
        0,                     0,                       0,                       0,

/*      Jxx*/
/*70*/  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

/*80*/  0, 0, 0, 0,
/*      TEST                     TEST                     XCHG                                 XCHG*/
        SRCDEP_REG | MODRM,      SRCDEP_REG | MODRM,      SRCDEP_REG | DSTDEP_REG | MODRM,     SRCDEP_REG | DSTDEP_REG | MODRM,
/*      MOV                      MOV                      MOV                                  MOV*/
        SRCDEP_REG | MODRM,      SRCDEP_REG | MODRM,      DSTDEP_REG | MODRM,                  DSTDEP_REG | MODRM,
/*      MOV from seg             LEA                      MOV to seg                           POP*/
        MODRM,                   DSTDEP_REG | MODRM,      MODRM,                               IMPL_ESP | MODRM,

/*      NOP                                                XCHG                                                XCHG                                              XCHG*/
/*90*/  0,                                                 SRCDEP_EAX | DSTDEP_EAX | SRCDEP_ECX | DSTDEP_ECX, SRCDEP_EAX | DSTDEP_EAX | SRCDEP_EDX | DSTDEP_EDX, SRCDEP_EAX | DSTDEP_EAX | SRCDEP_EBX | DSTDEP_EBX,
/*      XCHG                                               XCHG                                                XCHG                                              XCHG*/
        SRCDEP_EAX | DSTDEP_EAX | SRCDEP_ESP | DSTDEP_ESP, SRCDEP_EAX | DSTDEP_EAX | SRCDEP_EBP | DSTDEP_EBP, SRCDEP_EAX | DSTDEP_EAX | SRCDEP_ESI | DSTDEP_ESI, SRCDEP_EAX | DSTDEP_EAX | SRCDEP_EDI | DSTDEP_EDI,
/*      CBW                                                CWD                                                CALL far                                           WAIT*/
        SRCDEP_EAX | DSTDEP_EAX,                           SRCDEP_EAX | DSTDEP_EDX,                           0,                                                 0,
/*      PUSHF                                              POPF                                               SAHF                                               LAHF*/
        IMPL_ESP,                                          IMPL_ESP,                                          SRCDEP_EAX,                                        DSTDEP_EAX,

/*      MOV                      MOV                      MOV                      MOV*/
/*a0*/  DSTDEP_EAX,              DSTDEP_EAX,              SRCDEP_EAX,              SRCDEP_EAX,
/*      MOVSB                    MOVSW                    CMPSB                    CMPSW*/
        0,                       0,                       0,                       0,
/*      TEST                     TEST                     STOSB                    STOSW*/
        SRCDEP_EAX,              SRCDEP_EAX,              0,                       0,
/*      LODSB                    LODSW                    SCASB                    SCASW*/
        0,                       0,                       0,                       0,

/*      MOV*/
/*b0*/  DSTDEP_EAX | HAS_IMM8,    DSTDEP_ECX | HAS_IMM8,    DSTDEP_EDX | HAS_IMM8,    DSTDEP_EBX | HAS_IMM8,
        DSTDEP_EAX | HAS_IMM8,    DSTDEP_ECX | HAS_IMM8,    DSTDEP_EDX | HAS_IMM8,    DSTDEP_EBX | HAS_IMM8,
        DSTDEP_EAX | HAS_IMM1632, DSTDEP_ECX | HAS_IMM1632, DSTDEP_EDX | HAS_IMM1632, DSTDEP_EBX | HAS_IMM1632,
        DSTDEP_ESP | HAS_IMM1632, DSTDEP_EBP | HAS_IMM1632, DSTDEP_ESI | HAS_IMM1632, DSTDEP_EDI | HAS_IMM1632,

/*                                                      RET imm                   RET*/
/*c0*/  0,                       0,                     SRCDEP_ESP | DSTDEP_ESP,  IMPL_ESP,
/*      LES                      LDS                    MOV                       MOV*/
        DSTDEP_REG | MODRM,      DSTDEP_REG | MODRM,    MODRM | HAS_IMM8,         MODRM | HAS_IMM1632,
/*      ENTER                    LEAVE                  RETF                      RETF*/
        IMPL_ESP,                IMPL_ESP,              IMPL_ESP,                 IMPL_ESP,
/*      INT3                     INT                    INTO                      IRET*/
        0,                       0,                     0,                        0,


/*d0*/  0,                       0,                       0,                      0,
/*      AAM                      AAD                      SETALC                  XLAT*/
        SRCDEP_EAX | DSTDEP_EAX, SRCDEP_EAX | DSTDEP_EAX, SRCDEP_EAX,             SRCDEP_EAX | SRCDEP_EBX,
        0,                       0,                       0,                      0,
        0,                       0,                       0,                      0,

/*      LOOPNE                   LOOPE                    LOOP                     JCXZ*/
/*e0*/  SRCDEP_ECX | DSTDEP_ECX, SRCDEP_ECX | DSTDEP_ECX, SRCDEP_ECX | DSTDEP_ECX, SRCDEP_ECX,
/*      IN AL                    IN AX                    OUT_AL                   OUT_AX*/
        DSTDEP_EAX,              DSTDEP_EAX,              SRCDEP_EAX,              SRCDEP_EAX,
/*      CALL                     JMP                      JMP                      JMP*/
        IMPL_ESP,                0,                       0,                       0,
/*      IN AL                    IN AX                    OUT_AL                   OUT_AX*/
        SRCDEP_EDX | DSTDEP_EAX, SRCDEP_EDX | DSTDEP_EAX, SRCDEP_EDX | SRCDEP_EAX, SRCDEP_EDX | SRCDEP_EAX,

/*                                                        REPNE                    REPE*/
/*f0*/  0,                       0,                       0,                       0,
/*      HLT                      CMC*/
        0,                       0,                       0,                       0,
/*      CLC                      STC                      CLI                      STI*/
        0,                       0,                       0,                       0,
/*      CLD                      STD                      INCDEC*/
        0,                       0,                       MODRM,                   0
};

uint64_t opcode_deps_mod3[256] =
{
/*      ADD                      ADD                 ADD                                          ADD*/
/*00*/  SRCDEP_REG | SRCDEP_RM | DSTDEP_RM | MODRM,  SRCDEP_REG | SRCDEP_RM | DSTDEP_RM | MODRM,  SRCDEP_REG | DSTDEP_REG | SRCDEP_RM | MODRM, SRCDEP_REG | DSTDEP_REG | SRCDEP_RM | MODRM,
/*      ADD                                          ADD                                          PUSH ES                                      POP ES*/
        SRCDEP_EAX | DSTDEP_EAX | HAS_IMM8,          SRCDEP_EAX | DSTDEP_EAX | HAS_IMM1632,       IMPL_ESP,                                    IMPL_ESP,
/*      OR                                           OR                                           OR                                           OR*/
        SRCDEP_REG | SRCDEP_RM | DSTDEP_RM | MODRM,  SRCDEP_REG | SRCDEP_RM | DSTDEP_RM | MODRM,  SRCDEP_REG | DSTDEP_REG | SRCDEP_RM | MODRM, SRCDEP_REG | DSTDEP_REG | SRCDEP_RM | MODRM,
/*      OR                                           OR                                           PUSH CS*/
        SRCDEP_EAX | DSTDEP_EAX | HAS_IMM8,          SRCDEP_EAX | DSTDEP_EAX | HAS_IMM1632,       IMPL_ESP,                                    0,

/*      ADC                                          ADC                                          ADC                                          ADC*/
/*10*/  SRCDEP_REG | SRCDEP_RM | DSTDEP_RM | MODRM,  SRCDEP_REG | SRCDEP_RM | DSTDEP_RM | MODRM,  SRCDEP_REG | DSTDEP_REG | SRCDEP_RM | MODRM, SRCDEP_REG | DSTDEP_REG | SRCDEP_RM | MODRM,
/*      ADC                                          ADC                                          PUSH SS                                      POP SS*/
        SRCDEP_EAX | DSTDEP_EAX | HAS_IMM8,          SRCDEP_EAX | DSTDEP_EAX | HAS_IMM1632,       IMPL_ESP,                                    IMPL_ESP,
/*      SBB                                          SBB                                          SBB                                          SBB*/
        SRCDEP_REG |SRCDEP_RM | DSTDEP_RM |  MODRM,  SRCDEP_REG | SRCDEP_RM | DSTDEP_RM | MODRM,  SRCDEP_REG | DSTDEP_REG | SRCDEP_RM | MODRM, SRCDEP_REG | DSTDEP_REG | SRCDEP_RM | MODRM,
/*      SBB                                          SBB                                          PUSH DS                                      POP DS*/
        SRCDEP_EAX | DSTDEP_EAX | HAS_IMM8,          SRCDEP_EAX | DSTDEP_EAX | HAS_IMM1632,       IMPL_ESP,                                    IMPL_ESP,

/*      AND                                          AND                                          AND                                          AND*/
/*20*/  SRCDEP_REG | SRCDEP_RM | DSTDEP_RM | MODRM,  SRCDEP_REG | SRCDEP_RM | DSTDEP_RM | MODRM,  SRCDEP_REG | DSTDEP_REG | SRCDEP_RM | MODRM, SRCDEP_REG | DSTDEP_REG | SRCDEP_RM | MODRM,
/*      AND                                          AND                                                                                       DAA*/
        SRCDEP_EAX | DSTDEP_EAX | HAS_IMM8,          SRCDEP_EAX | DSTDEP_EAX | HAS_IMM1632,       0,                                           SRCDEP_EAX | DSTDEP_EAX,
/*      SUB                                          SUB                                          SUB                                          SUB*/
        SRCDEP_REG | SRCDEP_RM | DSTDEP_RM | MODRM,  SRCDEP_REG | SRCDEP_RM | DSTDEP_RM | MODRM,  SRCDEP_REG | DSTDEP_REG | SRCDEP_RM | MODRM, SRCDEP_REG | DSTDEP_REG | SRCDEP_RM | MODRM,
/*      SUB                                          SUB                                                                                       DAS*/
        SRCDEP_EAX | DSTDEP_EAX | HAS_IMM8,          SRCDEP_EAX | DSTDEP_EAX | HAS_IMM1632,       0,                                           SRCDEP_EAX | DSTDEP_EAX,

/*      XOR                                          XOR                                          XOR                                          XOR*/
/*30*/  SRCDEP_REG | SRCDEP_RM | DSTDEP_RM | MODRM,  SRCDEP_REG | SRCDEP_RM | DSTDEP_RM | MODRM,  SRCDEP_REG | DSTDEP_REG | SRCDEP_RM | MODRM, SRCDEP_REG | DSTDEP_REG | SRCDEP_RM | MODRM,
/*      XOR                                          XOR                                                                                       AAA*/
        SRCDEP_EAX | DSTDEP_EAX | HAS_IMM8,          SRCDEP_EAX | DSTDEP_EAX | HAS_IMM1632,       0,                                           SRCDEP_EAX | DSTDEP_EAX,
/*      CMP                                          CMP                                          CMP                                          CMP*/
        SRCDEP_REG | SRCDEP_RM | MODRM,              SRCDEP_REG | SRCDEP_RM | MODRM,              SRCDEP_REG | SRCDEP_RM | MODRM,              SRCDEP_REG | SRCDEP_RM | MODRM,
/*      CMP                                          CMP                                                                                       AAS*/
        SRCDEP_EAX | HAS_IMM8,                       SRCDEP_EAX | HAS_IMM1632,                    0,                                           SRCDEP_EAX | DSTDEP_EAX,

/*      INC EAX                  INC ECX                  INC EDX                  INC EBX*/
/*40*/  SRCDEP_EAX | DSTDEP_EAX, SRCDEP_ECX | DSTDEP_ECX, SRCDEP_EDX | DSTDEP_EDX, SRCDEP_EBX | DSTDEP_EBX,
/*      INC ESP                  INC EBP                  INC ESI                  INC EDI*/
        SRCDEP_ESP | DSTDEP_ESP, SRCDEP_EBP | DSTDEP_EBP, SRCDEP_ESI | DSTDEP_ESI, SRCDEP_EDI | DSTDEP_EDI,
/*      DEC EAX                  DEC ECX                  DEC EDX                  DEC EBX*/
        SRCDEP_EAX | DSTDEP_EAX, SRCDEP_ECX | DSTDEP_ECX, SRCDEP_EDX | DSTDEP_EDX, SRCDEP_EBX | DSTDEP_EBX,
/*      DEC ESP                  DEC EBP                  DEC ESI                  DEC EDI*/
        SRCDEP_ESP | DSTDEP_ESP, SRCDEP_EBP | DSTDEP_EBP, SRCDEP_ESI | DSTDEP_ESI, SRCDEP_EDI | DSTDEP_EDI,

/*      PUSH EAX               PUSH ECX               PUSH EDX               PUSH EBX*/
/*50*/  SRCDEP_EAX | IMPL_ESP, SRCDEP_ECX | IMPL_ESP, SRCDEP_EDX | IMPL_ESP, SRCDEP_EBX | IMPL_ESP,
/*      PUSH ESP               PUSH EBP               PUSH ESI               PUSH EDI*/
        SRCDEP_ESP | IMPL_ESP, SRCDEP_EBP | IMPL_ESP, SRCDEP_ESI | IMPL_ESP, SRCDEP_EDI | IMPL_ESP,
/*      POP EAX                POP ECX                POP EDX                POP EBX*/
        DSTDEP_EAX | IMPL_ESP, DSTDEP_ECX | IMPL_ESP, DSTDEP_EDX | IMPL_ESP, DSTDEP_EBX | IMPL_ESP,
/*      POP ESP                POP EBP                POP ESI                POP EDI*/
        DSTDEP_ESP | IMPL_ESP, DSTDEP_EBP | IMPL_ESP, DSTDEP_ESI | IMPL_ESP, DSTDEP_EDI | IMPL_ESP,

/*      PUSHA                  POPA                             BOUND                    ARPL*/
/*60*/  IMPL_ESP,              IMPL_ESP,                        0,                       0,
        0,                     0,                               0,                       0,
/*      PUSH imm               IMUL                             PUSH imm                 IMUL*/
        IMPL_ESP | HAS_IMM1632,DSTDEP_REG | SRCDEP_RM | MODRM,  IMPL_ESP | HAS_IMM8,     DSTDEP_REG | SRCDEP_RM | MODRM,
/*      INSB                   INSW                             OUTSB                    OUTSW*/
        0,                     0,                               0,                       0,

/*      Jxx*/
/*70*/  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

/*80*/  0,                              0,                              0,                                                        0,
/*      TEST                            TEST                            XCHG                                                      XCHG*/
        SRCDEP_REG | SRCDEP_RM | MODRM, SRCDEP_REG | SRCDEP_RM | MODRM, SRCDEP_REG | DSTDEP_REG | SRCDEP_RM | DSTDEP_RM | MODRM,  SRCDEP_REG | DSTDEP_REG | SRCDEP_RM | DSTDEP_RM | MODRM,
/*      MOV                             MOV                             MOV                                                       MOV*/
        SRCDEP_REG | DSTDEP_RM | MODRM, SRCDEP_REG | DSTDEP_RM | MODRM, SRCDEP_RM | DSTDEP_REG | MODRM,                           SRCDEP_RM | DSTDEP_REG | MODRM,
/*      MOV from seg                    LEA                             MOV to seg                                                POP*/
        DSTDEP_RM | MODRM,              DSTDEP_REG | MODRM,             SRCDEP_RM | MODRM,                                        IMPL_ESP | DSTDEP_RM | MODRM,

/*      NOP                                                XCHG                                                XCHG                                              XCHG*/
/*90*/  0,                                                 SRCDEP_EAX | DSTDEP_EAX | SRCDEP_ECX | DSTDEP_ECX, SRCDEP_EAX | DSTDEP_EAX | SRCDEP_EDX | DSTDEP_EDX, SRCDEP_EAX | DSTDEP_EAX | SRCDEP_EBX | DSTDEP_EBX,
/*      XCHG                                               XCHG                                                XCHG                                              XCHG*/
        SRCDEP_EAX | DSTDEP_EAX | SRCDEP_ESP | DSTDEP_ESP, SRCDEP_EAX | DSTDEP_EAX | SRCDEP_EBP | DSTDEP_EBP, SRCDEP_EAX | DSTDEP_EAX | SRCDEP_ESI | DSTDEP_ESI, SRCDEP_EAX | DSTDEP_EAX | SRCDEP_EDI | DSTDEP_EDI,
/*      CBW                                                CWD                                                CALL far                                           WAIT*/
        SRCDEP_EAX | DSTDEP_EAX,                           SRCDEP_EAX | DSTDEP_EDX,                           0,                                                 0,
/*      PUSHF                                              POPF                                               SAHF                                               LAHF*/
        IMPL_ESP,                                          IMPL_ESP,                                          SRCDEP_EAX,                                        DSTDEP_EAX,

/*      MOV                      MOV                      MOV                      MOV*/
/*a0*/  DSTDEP_EAX,              DSTDEP_EAX,              SRCDEP_EAX,              SRCDEP_EAX,
/*      MOVSB                    MOVSW                    CMPSB                    CMPSW*/
        0,                       0,                       0,                       0,
/*      TEST                     TEST                     STOSB                    STOSW*/
        SRCDEP_EAX,              SRCDEP_EAX,              0,                       0,
/*      LODSB                    LODSW                    SCASB                    SCASW*/
        0,                       0,                       0,                       0,

/*      MOV*/
/*b0*/  DSTDEP_EAX | HAS_IMM8,    DSTDEP_ECX | HAS_IMM8,    DSTDEP_EDX | HAS_IMM8,    DSTDEP_EBX | HAS_IMM8,
        DSTDEP_EAX | HAS_IMM8,    DSTDEP_ECX | HAS_IMM8,    DSTDEP_EDX | HAS_IMM8,    DSTDEP_EBX | HAS_IMM8,
        DSTDEP_EAX | HAS_IMM1632, DSTDEP_ECX | HAS_IMM1632, DSTDEP_EDX | HAS_IMM1632, DSTDEP_EBX | HAS_IMM1632,
        DSTDEP_ESP | HAS_IMM1632, DSTDEP_EBP | HAS_IMM1632, DSTDEP_ESI | HAS_IMM1632, DSTDEP_EDI | HAS_IMM1632,

/*                                                      RET imm                         RET*/
/*c0*/  0,                       0,                     SRCDEP_ESP | DSTDEP_ESP,        IMPL_ESP,
/*      LES                      LDS                    MOV                             MOV*/
        DSTDEP_REG | MODRM,      DSTDEP_REG | MODRM,    DSTDEP_RM | MODRM | HAS_IMM8,   DSTDEP_RM | MODRM | HAS_IMM1632,
/*      ENTER                    LEAVE                  RETF                            RETF*/
        IMPL_ESP,                IMPL_ESP,              IMPL_ESP,                       IMPL_ESP,
/*      INT3                     INT                    INTO                            IRET*/
        0,                       0,                     0,                              0,


/*d0*/  0,                       0,                       0,                      0,
/*      AAM                      AAD                      SETALC                  XLAT*/
        SRCDEP_EAX | DSTDEP_EAX, SRCDEP_EAX | DSTDEP_EAX, SRCDEP_EAX,             SRCDEP_EAX | SRCDEP_EBX,
        0,                       0,                       0,                      0,
        0,                       0,                       0,                      0,

/*      LOOPNE                   LOOPE                    LOOP                     JCXZ*/
/*e0*/  SRCDEP_ECX | DSTDEP_ECX, SRCDEP_ECX | DSTDEP_ECX, SRCDEP_ECX | DSTDEP_ECX, SRCDEP_ECX,
/*      IN AL                    IN AX                    OUT_AL                   OUT_AX*/
        DSTDEP_EAX,              DSTDEP_EAX,              SRCDEP_EAX,              SRCDEP_EAX,
/*      CALL                     JMP                      JMP                      JMP*/
        IMPL_ESP,                0,                       0,                       0,
/*      IN AL                    IN AX                    OUT_AL                   OUT_AX*/
        SRCDEP_EDX | DSTDEP_EAX, SRCDEP_EDX | DSTDEP_EAX, SRCDEP_EDX | SRCDEP_EAX, SRCDEP_EDX | SRCDEP_EAX,

/*                                                        REPNE                    REPE*/
/*f0*/  0,                       0,                       0,                       0,
/*      HLT                      CMC*/
        0,                       0,                       0,                       0,
/*      CLC                      STC                      CLI                      STI*/
        0,                       0,                       0,                       0,
/*      CLD                      STD                      INCDEC*/
        0,                       0,                       SRCDEP_RM | DSTDEP_RM | MODRM, 0
};

uint64_t opcode_deps_0f[256] =
{
/*00*/  MODRM,  MODRM,   MODRM,   MODRM,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      MODRM,   0,       MODRM,

/*10*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*20*/  MODRM,  MODRM,   MODRM,   MODRM,
        MODRM,  MODRM,   0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*30*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*40*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*50*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*60*/  MODRM | MMX_SHIFTPACK,  MODRM | MMX_SHIFTPACK,   MODRM | MMX_SHIFTPACK,   MODRM | MMX_SHIFTPACK,
        MODRM,                  MODRM,                   MODRM,                   MODRM,
        MODRM | MMX_SHIFTPACK,  MODRM | MMX_SHIFTPACK,   MODRM | MMX_SHIFTPACK,   MODRM | MMX_SHIFTPACK,
        0,                      0,                       MODRM,                   MODRM,

/*70*/  0,      MODRM | MMX_SHIFTPACK,   MODRM | MMX_SHIFTPACK,   MODRM | MMX_SHIFTPACK,
        MODRM,  MODRM,   MODRM,   0,
        0,      0,       0,       0,
        0,      0,       MODRM,   MODRM,

/*80*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*90*/  MODRM,  MODRM,   MODRM,   MODRM,
        MODRM,  MODRM,   MODRM,   MODRM,
        MODRM,  MODRM,   MODRM,   MODRM,
        MODRM,  MODRM,   MODRM,   MODRM,

/*a0*/  MODRM,  MODRM,   MODRM,   MODRM,
        MODRM,  MODRM,   0,       0,
        MODRM,  MODRM,   0,       MODRM,
        MODRM,  MODRM,   MODRM,   MODRM,

/*b0*/  MODRM,  MODRM,   MODRM,   MODRM,
        MODRM,  MODRM,   MODRM,   MODRM,
        0,      0,       MODRM,   MODRM,
        MODRM,  MODRM,   MODRM,   MODRM,

/*c0*/  MODRM,  MODRM,   0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*d0*/  0,      MODRM | MMX_SHIFTPACK,  MODRM | MMX_SHIFTPACK,   MODRM | MMX_SHIFTPACK,
        0,      MODRM | MMX_MULTIPLY,   0,                       0,
        MODRM,  MODRM,                  0,                       MODRM,
        MODRM,  MODRM,                  0,                       MODRM,

/*e0*/  0,      MODRM | MMX_SHIFTPACK,  MODRM | MMX_SHIFTPACK,   0,
        0,      MODRM | MMX_MULTIPLY,   0,                       0,
        MODRM,  MODRM,                  0,                       MODRM,
        MODRM,  MODRM,                  0,                       MODRM,

/*f0*/  0,      MODRM | MMX_SHIFTPACK,  MODRM | MMX_SHIFTPACK,   MODRM | MMX_SHIFTPACK,
        0,      MODRM | MMX_MULTIPLY,   0,                       0,
        MODRM,  MODRM,                  MODRM,                   0,
        MODRM,  MODRM,                  MODRM,                   0,
};
uint64_t opcode_deps_0f_mod3[256] =
{
/*00*/  MODRM,  MODRM,   MODRM,   MODRM,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      MODRM,   0,       MODRM,

/*10*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*20*/  MODRM,  MODRM,   MODRM,   MODRM,
        MODRM,  MODRM,   0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*30*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*40*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*50*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*60*/  MODRM | MMX_SHIFTPACK,  MODRM | MMX_SHIFTPACK,   MODRM | MMX_SHIFTPACK,   MODRM | MMX_SHIFTPACK,
        MODRM,                  MODRM,                   MODRM,                   MODRM,
        MODRM | MMX_SHIFTPACK,  MODRM | MMX_SHIFTPACK,   MODRM | MMX_SHIFTPACK,   MODRM | MMX_SHIFTPACK,
        0,                      0,                       MODRM,                   MODRM,

/*70*/  0,      MODRM | MMX_SHIFTPACK,   MODRM | MMX_SHIFTPACK,   MODRM | MMX_SHIFTPACK,
        MODRM,  MODRM,   MODRM,   0,
        0,      0,       0,       0,
        0,      0,       MODRM,   MODRM,

/*80*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*90*/  MODRM,  MODRM,   MODRM,   MODRM,
        MODRM,  MODRM,   MODRM,   MODRM,
        MODRM,  MODRM,   MODRM,   MODRM,
        MODRM,  MODRM,   MODRM,   MODRM,

/*a0*/  MODRM,  MODRM,   MODRM,   MODRM,
        MODRM,  MODRM,   0,       0,
        MODRM,  MODRM,   0,       MODRM,
        MODRM,  MODRM,   MODRM,   MODRM,

/*b0*/  MODRM,  MODRM,   MODRM,   MODRM,
        MODRM,  MODRM,   MODRM,   MODRM,
        0,      0,       MODRM,   MODRM,
        MODRM,  MODRM,   MODRM,   MODRM,

/*c0*/  MODRM,  MODRM,   0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*d0*/  0,      MODRM | MMX_SHIFTPACK,  MODRM | MMX_SHIFTPACK,   MODRM | MMX_SHIFTPACK,
        0,      MODRM | MMX_MULTIPLY,   0,                       0,
        MODRM,  MODRM,                  0,                       MODRM,
        MODRM,  MODRM,                  0,                       MODRM,

/*e0*/  0,      MODRM | MMX_SHIFTPACK,  MODRM | MMX_SHIFTPACK,   0,
        0,      MODRM | MMX_MULTIPLY,   0,                       0,
        MODRM,  MODRM,                  0,                       MODRM,
        MODRM,  MODRM,                  0,                       MODRM,

/*f0*/  0,      MODRM | MMX_SHIFTPACK,  MODRM | MMX_SHIFTPACK,   MODRM | MMX_SHIFTPACK,
        0,      MODRM | MMX_MULTIPLY,   0,                       0,
        MODRM,  MODRM,                  MODRM,                   0,
        MODRM,  MODRM,                  MODRM,                   0,
};

uint64_t opcode_deps_0f0f[256] =
{
/*00*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      MODRM,   0,       0,

/*10*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      MODRM,   0,       0,

/*20*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*30*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*40*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*50*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*60*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*70*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*80*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*90*/  MODRM,  0,       0,       0,
        MODRM,  0,       MODRM,   MODRM,
        0,      0,       MODRM,   0,
        0,      0,       MODRM,   0,

/*a0*/  MODRM,  0,       0,       0,
        MODRM,  0,       MODRM,   MODRM,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*b0*/  MODRM,  0,       0,       0,
        MODRM,  0,       MODRM,   MODRM,
        0,      0,       0,       0,
        0,      0,       0,       MODRM,

/*c0*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*d0*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*e0*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*f0*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
};
uint64_t opcode_deps_0f0f_mod3[256] =
{
/*00*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      MODRM,   0,       0,

/*10*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      MODRM,   0,       0,

/*20*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*30*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*40*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*50*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*60*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*70*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*80*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*90*/  MODRM,  0,       0,       0,
        MODRM,  0,       MODRM,   MODRM,
        0,      0,       MODRM,   0,
        0,      0,       MODRM,   0,

/*a0*/  MODRM,  0,       0,       0,
        MODRM,  0,       MODRM,   MODRM,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*b0*/  MODRM,  0,       0,       0,
        MODRM,  0,       MODRM,   MODRM,
        0,      0,       0,       0,
        0,      0,       0,       MODRM,

/*c0*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*d0*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*e0*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,

/*f0*/  0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
        0,      0,       0,       0,
};

uint64_t opcode_deps_shift[8] =
{
        MODRM,    MODRM,    MODRM,    MODRM,
        MODRM,    MODRM,    MODRM,    MODRM,
};
uint64_t opcode_deps_shift_mod3[8] =
{
        SRCDEP_RM | DSTDEP_RM | MODRM, SRCDEP_RM | DSTDEP_RM | MODRM, SRCDEP_RM | DSTDEP_RM | MODRM, SRCDEP_RM | DSTDEP_RM | MODRM,
        SRCDEP_RM | DSTDEP_RM | MODRM, SRCDEP_RM | DSTDEP_RM | MODRM, SRCDEP_RM | DSTDEP_RM | MODRM, SRCDEP_RM | DSTDEP_RM | MODRM,
};

uint64_t opcode_deps_shift_cl[8] =
{
        MODRM | SRCDEP_ECX,    MODRM | SRCDEP_ECX,    MODRM | SRCDEP_ECX,    MODRM | SRCDEP_ECX,
        MODRM | SRCDEP_ECX,    MODRM | SRCDEP_ECX,    MODRM | SRCDEP_ECX,    MODRM | SRCDEP_ECX,
};
uint64_t opcode_deps_shift_cl_mod3[8] =
{
        SRCDEP_RM | DSTDEP_RM | MODRM | SRCDEP_ECX, SRCDEP_RM | DSTDEP_RM | MODRM | SRCDEP_ECX, SRCDEP_RM | DSTDEP_RM | MODRM | SRCDEP_ECX, SRCDEP_RM | DSTDEP_RM | MODRM | SRCDEP_ECX,
        SRCDEP_RM | DSTDEP_RM | MODRM | SRCDEP_ECX, SRCDEP_RM | DSTDEP_RM | MODRM | SRCDEP_ECX, SRCDEP_RM | DSTDEP_RM | MODRM | SRCDEP_ECX, SRCDEP_RM | DSTDEP_RM | MODRM | SRCDEP_ECX,
};

uint64_t opcode_deps_f6[8] =
{
/*      TST                                                                                           NOT                                                         NEG*/
        MODRM,                                          0,                                            MODRM,                                                      MODRM,
/*      MUL                                            IMUL                                           DIV                                                         IDIV*/
        SRCDEP_EAX | DSTDEP_EAX | DSTDEP_EDX | MODRM,  SRCDEP_EAX | DSTDEP_EAX | DSTDEP_EDX | MODRM,  SRCDEP_EAX | SRCDEP_EDX | DSTDEP_EAX | DSTDEP_EDX | MODRM,  SRCDEP_EAX | SRCDEP_EDX | DSTDEP_EAX | DSTDEP_EDX | MODRM
};
uint64_t opcode_deps_f6_mod3[8] =
{
/*      TST                                                                                                                   NOT                                                                     NEG*/
        SRCDEP_RM | MODRM,                                         0,                                                         SRCDEP_RM | DSTDEP_RM | MODRM,                                          SRCDEP_RM | DSTDEP_RM | MODRM,
/*      MUL                                                        IMUL                                                       DIV                                                                     IDIV*/
        SRCDEP_EAX | DSTDEP_EAX | DSTDEP_EDX | SRCDEP_RM | MODRM,  SRCDEP_EAX | DSTDEP_EAX | DSTDEP_EDX | SRCDEP_RM | MODRM,  SRCDEP_EAX | SRCDEP_EDX | DSTDEP_EAX | DSTDEP_EDX | SRCDEP_RM | MODRM,  SRCDEP_EAX | SRCDEP_EDX | DSTDEP_EAX | DSTDEP_EDX | MODRM
};
uint64_t opcode_deps_f7[8] =
{
/*      TST                                                                                           NOT                                                         NEG*/
        MODRM,                                          0,                                            MODRM,                                                      MODRM,
/*      MUL                                            IMUL                                           DIV                                                         IDIV*/
        SRCDEP_EAX | DSTDEP_EAX | DSTDEP_EDX | MODRM,  SRCDEP_EAX | DSTDEP_EAX | DSTDEP_EDX | MODRM,  SRCDEP_EAX | SRCDEP_EDX | DSTDEP_EAX | DSTDEP_EDX | MODRM,  SRCDEP_EAX | SRCDEP_EDX | DSTDEP_EAX | DSTDEP_EDX | MODRM
};
uint64_t opcode_deps_f7_mod3[8] =
{
/*      TST                                                                                                                   NOT                                                                     NEG*/
        SRCDEP_RM | MODRM,                                         0,                                                         SRCDEP_RM | DSTDEP_RM | MODRM,                                          SRCDEP_RM | DSTDEP_RM | MODRM,
/*      MUL                                                        IMUL                                                       DIV                                                                     IDIV*/
        SRCDEP_EAX | DSTDEP_EAX | DSTDEP_EDX | SRCDEP_RM | MODRM,  SRCDEP_EAX | DSTDEP_EAX | DSTDEP_EDX | SRCDEP_RM | MODRM,  SRCDEP_EAX | SRCDEP_EDX | DSTDEP_EAX | DSTDEP_EDX | SRCDEP_RM | MODRM,  SRCDEP_EAX | SRCDEP_EDX | DSTDEP_EAX | DSTDEP_EDX | MODRM
};
uint64_t opcode_deps_ff[8] =
{
/*      INC      DEC      CALL               CALL far*/
        MODRM,   MODRM,   MODRM | IMPL_ESP,  MODRM,
/*      JMP      JMP far  PUSH*/
        MODRM,   MODRM,   MODRM | IMPL_ESP,  0
};
uint64_t opcode_deps_ff_mod3[8] =
{
/*      INC                              DEC                              CALL                           CALL far*/
        SRCDEP_RM | DSTDEP_RM | MODRM,   SRCDEP_RM | DSTDEP_RM | MODRM,   SRCDEP_RM | MODRM | IMPL_ESP,  MODRM,
/*      JMP                              JMP far                          PUSH*/
        SRCDEP_RM | MODRM,               MODRM,                           SRCDEP_RM | MODRM | IMPL_ESP,  0
};

uint64_t opcode_deps_d8[8] =
{
/*      FADDs                FMULs                FCOMs                  FCOMPs*/
        FPU_RW_ST0 | MODRM,  FPU_RW_ST0 | MODRM,  FPU_READ_ST0 | MODRM,  FPU_POP | FPU_READ_ST0 | MODRM,
/*      FSUBs                FSUBRs               FDIVs                  FDIVRs*/
        FPU_RW_ST0 | MODRM,  FPU_RW_ST0 | MODRM,  FPU_RW_ST0 | MODRM,    FPU_RW_ST0 | MODRM
};
uint64_t opcode_deps_d8_mod3[8] =
{
/*      FADD                            FMUL                            FCOM                            FCOMP*/
        FPU_RW_ST0 | FPU_READ_STREG,    FPU_RW_ST0 | FPU_READ_STREG,    FPU_READ_ST0 | FPU_READ_STREG,  FPU_POP | FPU_READ_ST0 | FPU_READ_STREG,
/*      FSUB                            FSUBR                           FDIV                            FDIVR*/
        FPU_RW_ST0 | FPU_READ_STREG,    FPU_RW_ST0 | FPU_READ_STREG,    FPU_RW_ST0 | FPU_READ_STREG,    FPU_RW_ST0 | FPU_READ_STREG
};

uint64_t opcode_deps_d9[8] =
{
/*      FLDs                        FSTs                   FSTPs*/
        FPU_PUSH | MODRM, 0,        FPU_READ_ST0 | MODRM,  FPU_POP | MODRM,
/*      FLDENV            FLDCW     FSTENV                 FSTCW*/
        MODRM,            MODRM,    MODRM,                 MODRM
};
uint64_t opcode_deps_d9_mod3[64] =
{
        /*FLD*/
        FPU_PUSH | FPU_READ_STREG,  FPU_PUSH | FPU_READ_STREG,  FPU_PUSH | FPU_READ_STREG,  FPU_PUSH | FPU_READ_STREG,
        FPU_PUSH | FPU_READ_STREG,  FPU_PUSH | FPU_READ_STREG,  FPU_PUSH | FPU_READ_STREG,  FPU_PUSH | FPU_READ_STREG,
        /*FXCH*/
        FPU_FXCH,                   FPU_FXCH,                   FPU_FXCH,                   FPU_FXCH,
        FPU_FXCH,                   FPU_FXCH,                   FPU_FXCH,                   FPU_FXCH,
        /*FNOP*/
        0, 0, 0, 0, 0, 0, 0, 0,
        /*FSTP*/
        FPU_READ_ST0 | FPU_WRITE_STREG | FPU_POP,  FPU_READ_ST0 | FPU_WRITE_STREG | FPU_POP,  FPU_READ_ST0 | FPU_WRITE_STREG | FPU_POP,  FPU_READ_ST0 | FPU_WRITE_STREG | FPU_POP,
        FPU_READ_ST0 | FPU_WRITE_STREG | FPU_POP,  FPU_READ_ST0 | FPU_WRITE_STREG | FPU_POP,  FPU_READ_ST0 | FPU_WRITE_STREG | FPU_POP,  FPU_READ_ST0 | FPU_WRITE_STREG | FPU_POP,
/*      opFCHS                      opFABS*/
        0,                          0,        0, 0,
/*      opFTST                      opFXAM*/
        0,                          0,        0, 0,
/*      opFLD1                      opFLDL2T            opFLDL2E            opFLDPI*/
        FPU_PUSH,                   FPU_PUSH,           FPU_PUSH,           FPU_PUSH,
/*      opFLDEG2                    opFLDLN2            opFLDZ*/
        FPU_PUSH,                   FPU_PUSH,           FPU_PUSH,           0,
/*      opF2XM1                     opFYL2X             opFPTAN             opFPATAN*/
        0,                          0,                  0,                  0,
/*                                                      opFDECSTP           opFINCSTP,*/
        0,                          0,                  0,                  0,
/*      opFPREM                                         opFSQRT             opFSINCOS*/
        0,                          0,                  0,                  0,
/*      opFRNDINT                   opFSCALE            opFSIN              opFCOS*/
        0,                          0,                  0,                  0
};

uint64_t opcode_deps_da[8] =
{
/*      FIADDl               FIMULl               FICOMl                 FICOMPl*/
        FPU_RW_ST0 | MODRM,  FPU_RW_ST0 | MODRM,  FPU_READ_ST0 | MODRM,  FPU_READ_ST0 | FPU_POP | MODRM,
/*      FISUBl               FISUBRl              FIDIVl                 FIDIVRl*/
        FPU_RW_ST0 | MODRM,  FPU_RW_ST0 | MODRM,  FPU_RW_ST0 | MODRM,    FPU_RW_ST0 | MODRM
};
uint64_t opcode_deps_da_mod3[8] =
{
        0,                   0,                   0,                     0,
/*                           FCOMPP*/
        0,                   FPU_POP2,            0,                     0
};


uint64_t opcode_deps_db[8] =
{
/*      FLDil                                     FSTil                  FSTPil*/
        FPU_PUSH | MODRM,    0,                   FPU_READ_ST0 | MODRM,  FPU_READ_ST0 | FPU_POP | MODRM,
/*                           FLDe                                        FSTPe*/
        0,                   FPU_PUSH | MODRM,    0,                     FPU_READ_ST0 | FPU_POP | MODRM
};
uint64_t opcode_deps_db_mod3[64] =
{
        0, 0, 0, 0, 0, 0, 0, 0,

        0, 0, 0, 0, 0, 0, 0, 0,

        0, 0, 0, 0, 0, 0, 0, 0,


        0, 0, 0, 0, 0, 0, 0, 0,

/*              opFNOP opFCLEX opFINIT*/
        0,      0,     0,      0,
/*      opFNOP  opFNOP*/
        0,      0,     0,      0,

        0, 0, 0, 0, 0, 0, 0, 0,

        0, 0, 0, 0, 0, 0, 0, 0,

        0, 0, 0, 0, 0, 0, 0, 0,
};

uint64_t opcode_deps_dc[8] =
{
/*      FADDd                FMULd                FCOMd                  FCOMPd*/
        FPU_RW_ST0 | MODRM,  FPU_RW_ST0 | MODRM,  FPU_READ_ST0 | MODRM,  FPU_READ_ST0 | FPU_POP | MODRM,
/*      FSUBd                FSUBRd               FDIVd                  FDIVRd*/
        FPU_RW_ST0 | MODRM,  FPU_RW_ST0 | MODRM,  FPU_RW_ST0 | MODRM,    FPU_RW_ST0 | MODRM
};
uint64_t opcode_deps_dc_mod3[8] =
{
/*      opFADDr                         opFMULr*/
        FPU_READ_ST0 | FPU_RW_STREG,    FPU_READ_ST0 | FPU_RW_STREG,    0,                             0,
/*      opFSUBRr                        opFSUBr                         opFDIVRr                       opFDIVr*/
        FPU_READ_ST0 | FPU_RW_STREG,    FPU_READ_ST0 | FPU_RW_STREG,    FPU_READ_ST0 | FPU_RW_STREG,   FPU_READ_ST0 | FPU_RW_STREG
};

uint64_t opcode_deps_dd[8] =
{
/*      FLDd                            FSTd                   FSTPd*/
        FPU_PUSH | MODRM, 0,            FPU_READ_ST0 | MODRM,  FPU_READ_ST0 | FPU_POP | MODRM,
/*      FRSTOR                          FSAVE                  FSTSW*/
        MODRM,            0,            MODRM,                 MODRM
};
uint64_t opcode_deps_dd_mod3[8] =
{
/*      FFFREE                                                                    FST                               FSTP*/
        0,                              0,                                        FPU_READ_ST0 | FPU_WRITE_STREG,   FPU_READ_ST0 | FPU_WRITE_STREG | FPU_POP,
/*      FUCOM                                                                     FUCOMP*/
        FPU_READ_ST0 | FPU_READ_STREG,  FPU_READ_ST0 | FPU_READ_STREG | FPU_POP,  0,                                0
};

uint64_t opcode_deps_de[8] =
{
/*      FIADDw               FIMULw               FICOMw                 FICOMPw*/
        FPU_RW_ST0 | MODRM,  FPU_RW_ST0 | MODRM,  FPU_READ_ST0 | MODRM,  FPU_READ_ST0 | FPU_POP | MODRM,
/*      FISUBw               FISUBRw              FIDIVw                 FIDIVRw*/
        FPU_RW_ST0 | MODRM,  FPU_RW_ST0 | MODRM,  FPU_RW_ST0 | MODRM,    FPU_RW_ST0 | MODRM
};
uint64_t opcode_deps_de_mod3[8] =
{
/*      FADDP                                   FMULP                                                                            FCOMPP*/
        FPU_READ_ST0 | FPU_RW_STREG | FPU_POP,  FPU_READ_ST0 | FPU_RW_STREG | FPU_POP,   0,                                      FPU_READ_ST0 | FPU_READ_ST1 | FPU_POP2,
/*      FSUBP                                   FSUBRP                                   FDIVP                                   FDIVRP*/
        FPU_READ_ST0 | FPU_RW_STREG | FPU_POP,  FPU_READ_ST0 | FPU_RW_STREG | FPU_POP,   FPU_READ_ST0 | FPU_RW_STREG | FPU_POP,  FPU_READ_ST0 | FPU_RW_STREG | FPU_POP
};

uint64_t opcode_deps_df[8] =
{
/*      FILDiw                                FISTiw                           FISTPiw*/
        FPU_PUSH | MODRM,  0,                 FPU_READ_ST0 | MODRM,            FPU_READ_ST0 | FPU_POP | MODRM,
/*                         FILDiq             FBSTP                            FISTPiq*/
        0,                 FPU_PUSH | MODRM,  FPU_READ_ST0 | FPU_POP | MODRM,  FPU_READ_ST0 | FPU_POP | MODRM
};
uint64_t opcode_deps_df_mod3[8] =
{
        0, 0, 0, 0,
/*      FSTSW AX*/
        0, 0, 0, 0
};

uint64_t opcode_deps_81[8] =
{
        MODRM | HAS_IMM1632,  MODRM | HAS_IMM1632,  MODRM | HAS_IMM1632,  MODRM | HAS_IMM1632,
        MODRM | HAS_IMM1632,  MODRM | HAS_IMM1632,  MODRM | HAS_IMM1632,  MODRM | HAS_IMM1632
};
uint64_t opcode_deps_81_mod3[8] =
{
        SRCDEP_RM | DSTDEP_RM | MODRM | HAS_IMM1632,  SRCDEP_RM | DSTDEP_RM | MODRM | HAS_IMM1632,  SRCDEP_RM | DSTDEP_RM | MODRM | HAS_IMM1632,  SRCDEP_RM | DSTDEP_RM | MODRM | HAS_IMM1632,
        SRCDEP_RM | DSTDEP_RM | MODRM | HAS_IMM1632,  SRCDEP_RM | DSTDEP_RM | MODRM | HAS_IMM1632,  SRCDEP_RM | DSTDEP_RM | MODRM | HAS_IMM1632,  SRCDEP_RM | MODRM | HAS_IMM1632
};
uint64_t opcode_deps_8x[8] =
{
        MODRM | HAS_IMM8,  MODRM | HAS_IMM8,  MODRM | HAS_IMM8,  MODRM | HAS_IMM8,
        MODRM | HAS_IMM8,  MODRM | HAS_IMM8,  MODRM | HAS_IMM8,  MODRM | HAS_IMM8
};
uint64_t opcode_deps_8x_mod3[8] =
{
        SRCDEP_RM | DSTDEP_RM | MODRM | HAS_IMM8,  SRCDEP_RM | DSTDEP_RM | MODRM | HAS_IMM8,  SRCDEP_RM | DSTDEP_RM | MODRM | HAS_IMM8,  SRCDEP_RM | DSTDEP_RM | MODRM | HAS_IMM8,
        SRCDEP_RM | DSTDEP_RM | MODRM | HAS_IMM8,  SRCDEP_RM | DSTDEP_RM | MODRM | HAS_IMM8,  SRCDEP_RM | DSTDEP_RM | MODRM | HAS_IMM8,  SRCDEP_RM | MODRM | HAS_IMM8
};
