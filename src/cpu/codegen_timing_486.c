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
#include "codegen_ops.h"
#include "codegen_timing_common.h"

#define CYCLES(c) (int *)c
#define CYCLES2(c16, c32) (int *)((-1 & ~0xffff) | c16 | (c32 << 8))

static int *opcode_timings[256] =
{
/*00*/  &timing_mr,     &timing_mr,     &timing_rm,     &timing_rm,     &timing_rr,     &timing_rr,     CYCLES(2),      CYCLES(3),      &timing_mr,     &timing_mr,     &timing_rm,     &timing_rm,     &timing_rr,     &timing_rr,     CYCLES(2),      NULL,
/*10*/  &timing_mr,     &timing_mr,     &timing_rm,     &timing_rm,     &timing_rr,     &timing_rr,     CYCLES(2),      CYCLES(3),      &timing_mr,     &timing_mr,     &timing_rm,     &timing_rm,     &timing_rr,     &timing_rr,     CYCLES(2),      CYCLES(3),
/*20*/  &timing_mr,     &timing_mr,     &timing_rm,     &timing_rm,     &timing_rr,     &timing_rr,     CYCLES(4),      CYCLES(3),      &timing_mr,     &timing_mr,     &timing_rm,     &timing_rm,     &timing_rr,     &timing_rr,     CYCLES(4),      CYCLES(3),
/*30*/  &timing_mr,     &timing_mr,     &timing_rm,     &timing_rm,     &timing_rr,     &timing_rr,     CYCLES(4),      CYCLES(2),      &timing_mr,     &timing_mr,     &timing_rm,     &timing_rm,     &timing_rr,     &timing_rr,     CYCLES(4),      CYCLES(2),

/*40*/  &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,
/*50*/  CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),
/*60*/  CYCLES(11),     CYCLES(9),      CYCLES(7),      CYCLES(9),      CYCLES(4),      CYCLES(4),      CYCLES(2),      CYCLES(2),      CYCLES(1),      CYCLES2(17,25), CYCLES(1),      CYCLES2(17,20), CYCLES(17),     CYCLES(17),     CYCLES(17),     CYCLES(17),
/*70*/  &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,

/*80*/  &timing_mr,     &timing_mr,     &timing_mr,     &timing_mr,     &timing_rm,     &timing_rm,     CYCLES(5),      CYCLES(5),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(3),      CYCLES(1),      CYCLES(5),      CYCLES(6),
/*90*/  CYCLES(1),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(0),      CYCLES(4),      CYCLES(4),      CYCLES(5),      CYCLES(2),      CYCLES(3),
/*a0*/  CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(7),      CYCLES(7),      CYCLES(8),      CYCLES(8),      CYCLES(1),      CYCLES(1),      CYCLES(5),      CYCLES(5),      CYCLES(5),      CYCLES(5),      CYCLES(6),      CYCLES(6),
/*b0*/  &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,

/*c0*/  CYCLES(4),      CYCLES(4),      CYCLES(5),      CYCLES(5),      CYCLES(6),      CYCLES(6),      CYCLES(1),      CYCLES(1),      CYCLES(14),     CYCLES(5),      CYCLES(0),      CYCLES(0),      &timing_int,    &timing_int,    CYCLES(3),      CYCLES(0),
/*d0*/  CYCLES(4),      CYCLES(4),      CYCLES(4),      CYCLES(4),      CYCLES(15),     CYCLES(14),     CYCLES(2),      CYCLES(4),      NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  CYCLES(6),      CYCLES(6),      CYCLES(6),      CYCLES(5),      CYCLES(14),     CYCLES(14),     CYCLES(16),     CYCLES(16),     CYCLES(3),      CYCLES(3),      CYCLES(17),     CYCLES(3),      CYCLES(14),     CYCLES(14),     CYCLES(14),     CYCLES(14),
/*f0*/  CYCLES(4),      CYCLES(0),      CYCLES(0),      CYCLES(0),      CYCLES(4),      CYCLES(2),      NULL,           NULL,           CYCLES(2),      CYCLES(2),      CYCLES(3),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(3),      NULL
};

static int *opcode_timings_mod3[256] =
{
/*00*/  &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     CYCLES(2),      CYCLES(3),      &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     CYCLES(2),      NULL,
/*10*/  &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     CYCLES(2),      CYCLES(3),      &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     CYCLES(2),      CYCLES(3),
/*20*/  &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     CYCLES(4),      CYCLES(3),      &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     CYCLES(4),      CYCLES(3),
/*30*/  &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     CYCLES(4),      CYCLES(2),      &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     CYCLES(4),      CYCLES(2),

/*40*/  &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,
/*50*/  CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),
/*60*/  CYCLES(11),     CYCLES(9),      CYCLES(7),      CYCLES(9),      CYCLES(4),      CYCLES(4),      CYCLES(2),      CYCLES(2),      CYCLES(1),      CYCLES2(14,25), CYCLES(1),      CYCLES2(17,20), CYCLES(17),     CYCLES(17),     CYCLES(17),     CYCLES(17),
/*70*/  &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,

/*80*/  &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     CYCLES(5),      CYCLES(5),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(2),      CYCLES(1),      CYCLES(2),      CYCLES(1),
/*90*/  CYCLES(1),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(0),      CYCLES(4),      CYCLES(4),      CYCLES(5),      CYCLES(2),      CYCLES(3),
/*a0*/  CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(7),      CYCLES(7),      CYCLES(8),      CYCLES(8),      CYCLES(1),      CYCLES(1),      CYCLES(5),      CYCLES(5),      CYCLES(5),      CYCLES(5),      CYCLES(6),      CYCLES(6),
/*b0*/  &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,

/*c0*/  CYCLES(4),      CYCLES(4),      CYCLES(5),      CYCLES(5),      CYCLES(6),      CYCLES(6),      CYCLES(1),      CYCLES(1),      CYCLES(14),     CYCLES(5),      CYCLES(0),      CYCLES(0),      &timing_int,    &timing_int,    CYCLES(3),      CYCLES(0),
/*d0*/  CYCLES(4),      CYCLES(4),      CYCLES(4),      CYCLES(4),      CYCLES(15),     CYCLES(14),     CYCLES(2),      CYCLES(4),      NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*e0*/  CYCLES(6),      CYCLES(6),      CYCLES(6),      CYCLES(5),      CYCLES(14),     CYCLES(14),     CYCLES(16),     CYCLES(16),     CYCLES(3),      CYCLES(3),      CYCLES(17),     CYCLES(3),      CYCLES(14),     CYCLES(14),     CYCLES(14),     CYCLES(14),
/*f0*/  CYCLES(4),      CYCLES(0),      CYCLES(0),      CYCLES(0),      CYCLES(4),      CYCLES(2),      NULL,           NULL,           CYCLES(2),      CYCLES(2),      CYCLES(3),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(3),      NULL
};

static int *opcode_timings_0f[256] =
{
/*00*/  CYCLES(20),     CYCLES(11),     CYCLES(11),     CYCLES(10),     NULL,           CYCLES(195),    CYCLES(7),      NULL,           CYCLES(1000),   CYCLES(10000),  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*20*/  CYCLES(6),      CYCLES(6),      CYCLES(6),      CYCLES(6),      CYCLES(6),      CYCLES(6),      NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*30*/  CYCLES(9),      CYCLES(1),      CYCLES(9),      NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*40*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*60*/  &timing_rm,     &timing_rm,     &timing_rm,     &timing_rm,     &timing_rm,     &timing_rm,     &timing_rm,     &timing_rm,     &timing_rm,     &timing_rm,     &timing_rm,     &timing_rm,     NULL,           NULL,           &timing_rm,     &timing_rm,
/*70*/  NULL,           &timing_rm,     &timing_rm,     &timing_rm,     &timing_rm,     &timing_rm,     &timing_rm,     CYCLES(100),    NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           &timing_rm,     &timing_rm,

/*80*/  &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,
/*90*/  CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),
/*a0*/  CYCLES(3),      CYCLES(3),      CYCLES(14),     CYCLES(8),      CYCLES(3),      CYCLES(4),      NULL,           NULL,           CYCLES(3),      CYCLES(3),      NULL,           CYCLES(13),     CYCLES(3),      CYCLES(3),      NULL,           CYCLES2(18,30),
/*b0*/  CYCLES(10),     CYCLES(10),     CYCLES(6),      CYCLES(13),     CYCLES(6),      CYCLES(6),      CYCLES(3),      CYCLES(3),      NULL,           NULL,           CYCLES(6),      CYCLES(13),     CYCLES(7),      CYCLES(7),      CYCLES(3),      CYCLES(3),

/*c0*/  CYCLES(4),      CYCLES(4),      NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),
/*d0*/  NULL,           &timing_rm,     &timing_rm,     &timing_rm,     NULL,           &timing_rm,     NULL,           NULL,           &timing_rm,     &timing_rm,     NULL,           &timing_rm,     &timing_rm,     &timing_rm,     NULL,           &timing_rm,
/*e0*/  NULL,           &timing_rm,     &timing_rm,     NULL,           NULL,           &timing_rm,     NULL,           NULL,           &timing_rm,     &timing_rm,     NULL,           &timing_rm,     &timing_rm,     &timing_rm,     NULL,           &timing_rm,
/*f0*/  NULL,           &timing_rm,     &timing_rm,     &timing_rm,     NULL,           &timing_rm,     NULL,           NULL,           &timing_rm,     &timing_rm,     &timing_rm,     NULL,           &timing_rm,     &timing_rm,     &timing_rm,     NULL,
};
static int *opcode_timings_0f_mod3[256] =
{
/*00*/  CYCLES(20),     CYCLES(11),     CYCLES(11),     CYCLES(10),     NULL,           CYCLES(195),    CYCLES(7),      NULL,           CYCLES(1000),   CYCLES(10000),  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*10*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*20*/  CYCLES(6),      CYCLES(6),      CYCLES(6),      CYCLES(6),      CYCLES(6),      CYCLES(6),      NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*30*/  CYCLES(9),      CYCLES(1),      CYCLES(9),      NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,

/*40*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*50*/  NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*60*/  &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     NULL,           NULL,           &timing_rr,     &timing_rr,
/*70*/  NULL,           &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     &timing_rr,     CYCLES(100),    NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           &timing_rr,     &timing_rr,

/*80*/  &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,    &timing_bnt,
/*90*/  CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),
/*a0*/  CYCLES(3),      CYCLES(3),      CYCLES(14),     CYCLES(8),      CYCLES(3),      CYCLES(4),      NULL,           NULL,           CYCLES(3),      CYCLES(3),      NULL,           CYCLES(13),     CYCLES(3),      CYCLES(3),      NULL,           CYCLES2(18,30),
/*b0*/  CYCLES(10),     CYCLES(10),     CYCLES(6),      CYCLES(13),     CYCLES(6),      CYCLES(6),      CYCLES(3),      CYCLES(3),      NULL,           NULL,           CYCLES(6),      CYCLES(13),     CYCLES(7),      CYCLES(7),      CYCLES(3),      CYCLES(3),

/*c0*/  CYCLES(4),      CYCLES(4),      NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),
/*d0*/  NULL,           &timing_rr,     &timing_rr,     &timing_rr,     NULL,           &timing_rr,     NULL,           NULL,           &timing_rr,     &timing_rr,     NULL,           &timing_rr,     &timing_rr,     &timing_rr,     NULL,           &timing_rr,
/*e0*/  NULL,           &timing_rr,     &timing_rr,     NULL,           NULL,           &timing_rr,     NULL,           NULL,           &timing_rr,     &timing_rr,     NULL,           &timing_rr,     &timing_rr,     &timing_rr,     NULL,           &timing_rr,
/*f0*/  NULL,           &timing_rr,     &timing_rr,     &timing_rr,     NULL,           &timing_rr,     NULL,           NULL,           &timing_rr,     &timing_rr,     &timing_rr,     NULL,           &timing_rr,     &timing_rr,     &timing_rr,     NULL,
};

static int *opcode_timings_shift[8] =
{
        CYCLES(7),      CYCLES(7),      CYCLES(10),     CYCLES(10),     CYCLES(7),      CYCLES(7),      CYCLES(7),      CYCLES(7)
};
static int *opcode_timings_shift_mod3[8] =
{
        CYCLES(3),      CYCLES(3),      CYCLES(9),      CYCLES(9),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3)
};

static int *opcode_timings_f6[8] =
{
        &timing_rm,     NULL,           &timing_mm,     &timing_mm,     CYCLES(13),     CYCLES(14),     CYCLES(16),     CYCLES(19)
};
static int *opcode_timings_f6_mod3[8] =
{
        &timing_rr,     NULL,           &timing_rr,     &timing_rr,     CYCLES(13),     CYCLES(14),     CYCLES(16),     CYCLES(19)
};
static int *opcode_timings_f7[8] =
{
        &timing_rm,     NULL,           &timing_mm,     &timing_mm,     CYCLES(21),     CYCLES2(22,38), CYCLES2(24,40), CYCLES2(27,43)
};
static int *opcode_timings_f7_mod3[8] =
{
        &timing_rr,     NULL,           &timing_rr,     &timing_rr,     CYCLES(21),     CYCLES2(22,38), CYCLES2(24,40), CYCLES2(27,43)
};
static int *opcode_timings_ff[8] =
{
        &timing_mm,     &timing_mm,     CYCLES(5),      CYCLES(0),      CYCLES(5),      CYCLES(0),      CYCLES(5),      NULL
};
static int *opcode_timings_ff_mod3[8] =
{
        &timing_rr,     &timing_rr,     CYCLES(5),      CYCLES(0),      CYCLES(5),      CYCLES(0),      CYCLES(5),      NULL
};

static int *opcode_timings_d8[8] =
{
/*      FADDil          FMULil          FCOMil          FCOMPil         FSUBil          FSUBRil         FDIVil          FDIVRil*/
        CYCLES(8),      CYCLES(11),     CYCLES(4),      CYCLES(4),      CYCLES(8),      CYCLES(8),      CYCLES(73),     CYCLES(73)
};
static int *opcode_timings_d8_mod3[8] =
{
/*      FADD            FMUL            FCOM            FCOMP           FSUB            FSUBR           FDIV            FDIVR*/
        CYCLES(8),      CYCLES(16),     CYCLES(4),      CYCLES(4),      CYCLES(8),      CYCLES(8),      CYCLES(73),     CYCLES(73)
};

static int *opcode_timings_d9[8] =
{
/*      FLDs                            FSTs            FSTPs           FLDENV          FLDCW           FSTENV          FSTCW*/
        CYCLES(3),      NULL,           CYCLES(7),      CYCLES(7),      CYCLES(34),     CYCLES(4),      CYCLES(67),     CYCLES(3)
};
static int *opcode_timings_d9_mod3[64] =
{
        /*FLD*/
        CYCLES(4),      CYCLES(4),      CYCLES(4),      CYCLES(4),      CYCLES(4),      CYCLES(4),      CYCLES(4),      CYCLES(4),
        /*FXCH*/
        CYCLES(4),      CYCLES(4),      CYCLES(4),      CYCLES(4),      CYCLES(4),      CYCLES(4),      CYCLES(4),      CYCLES(4),
        /*FNOP*/
        CYCLES(3),      NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
        /*FSTP*/
        CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),
/*      opFCHS          opFABS                                          opFTST          opFXAM*/
        CYCLES(6),      CYCLES(3),      NULL,           NULL,           CYCLES(4),      CYCLES(8),      NULL,           NULL,
/*      opFLD1          opFLDL2T        opFLDL2E        opFLDPI         opFLDEG2        opFLDLN2        opFLDZ*/
        CYCLES(4),      CYCLES(8),      CYCLES(8),      CYCLES(8),      CYCLES(8),      CYCLES(8),      CYCLES(4),      NULL,
/*      opF2XM1         opFYL2X         opFPTAN         opFPATAN                                        opFDECSTP       opFINCSTP,*/
        CYCLES(140),    CYCLES(196),    CYCLES(200),    CYCLES(218),    NULL,           NULL,           CYCLES(3),      CYCLES(3),
/*      opFPREM                         opFSQRT         opFSINCOS       opFRNDINT       opFSCALE        opFSIN          opFCOS*/
        CYCLES(70),     NULL,           CYCLES(83),     CYCLES(292),    CYCLES(21),     CYCLES(30),     CYCLES(257),    CYCLES(257)
};

static int *opcode_timings_da[8] =
{
/*      FADDil          FMULil          FCOMil          FCOMPil         FSUBil          FSUBRil         FDIVil          FDIVRil*/
        CYCLES(8),      CYCLES(11),     CYCLES(4),      CYCLES(4),      CYCLES(8),      CYCLES(8),      CYCLES(73),     CYCLES(73)
};
static int *opcode_timings_da_mod3[8] =
{
        NULL,           NULL,           NULL,           NULL,           NULL,           CYCLES(5),      NULL,           NULL
};


static int *opcode_timings_db[8] =
{
/*      FLDil                           FSTil           FSTPil                          FLDe                            FSTPe*/
        CYCLES(9),      NULL,           CYCLES(28),     CYCLES(28),     NULL,           CYCLES(5),      NULL,           CYCLES(6)
};
static int *opcode_timings_db_mod3[64] =
{
        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
/*                      opFNOP          opFCLEX         opFINIT         opFNOP          opFNOP*/
        NULL,           CYCLES(3),      CYCLES(7),      CYCLES(17),     CYCLES(3),      CYCLES(3),      NULL,
        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
        NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,           NULL,
};

static int *opcode_timings_dc[8] =
{
/*      opFADDd_a16     opFMULd_a16     opFCOMd_a16     opFCOMPd_a16    opFSUBd_a16     opFSUBRd_a16    opFDIVd_a16     opFDIVRd_a16*/
        CYCLES(8),      CYCLES(11),     CYCLES(4),      CYCLES(4),      CYCLES(8),      CYCLES(8),      CYCLES(73),     CYCLES(73)
};
static int *opcode_timings_dc_mod3[8] =
{
/*      opFADDr         opFMULr                                         opFSUBRr        opFSUBr         opFDIVRr        opFDIVr*/
        CYCLES(8),      CYCLES(16),     NULL,           NULL,           CYCLES(8),      CYCLES(8),      CYCLES(73),     CYCLES(73)
};

static int *opcode_timings_dd[8] =
{
/*      FLDd                            FSTd            FSTPd           FRSTOR                           FSAVE          FSTSW*/
        CYCLES(3),      NULL,           CYCLES(8),      CYCLES(8),      CYCLES(131),     NULL,           CYCLES(154),   CYCLES(3)
};
static int *opcode_timings_dd_mod3[8] =
{
/*      FFFREE                          FST             FSTP            FUCOM            FUCOMP*/
        CYCLES(3),      NULL,           CYCLES(3),      CYCLES(3),      CYCLES(4),       CYCLES(4),      NULL,          NULL
};

static int *opcode_timings_de[8] =
{
/*      FADDiw          FMULiw          FCOMiw          FCOMPiw         FSUBil          FSUBRil         FDIVil          FDIVRil*/
        CYCLES(8),      CYCLES(11),     CYCLES(4),      CYCLES(4),      CYCLES(8),      CYCLES(8),      CYCLES(73),     CYCLES(73)
};
static int *opcode_timings_de_mod3[8] =
{
/*      FADD            FMUL                            FCOMPP          FSUB            FSUBR           FDIV            FDIVR*/
        CYCLES(8),      CYCLES(16),     NULL,           CYCLES(5),      CYCLES(8),      CYCLES(8),      CYCLES(73),     CYCLES(73)
};

static int *opcode_timings_df[8] =
{
/*      FILDiw                          FISTiw          FISTPiw                          FILDiq          FBSTP          FISTPiq*/
        CYCLES(13),     NULL,           CYCLES(29),     CYCLES(29),     NULL,            CYCLES(10),     CYCLES(172),   CYCLES(28)
};
static int *opcode_timings_df_mod3[8] =
{
/*      FFREE                           FST             FSTP            FUCOM            FUCOMP*/
        CYCLES(3),      NULL,           CYCLES(3),      CYCLES(3),      CYCLES(4),       CYCLES(4),      NULL,          NULL
};

static int *opcode_timings_8x[8] =
{
        &timing_mr,     &timing_mr,     &timing_mr,     &timing_mr,     &timing_mr,     &timing_mr,     &timing_mr,     &timing_rm
};
static int *opcode_timings_8x_mod3[8] =
{
        &timing_mr,     &timing_mr,     &timing_mr,     &timing_mr,     &timing_mr,     &timing_mr,     &timing_mr,     &timing_rm
};
static int *opcode_timings_81[8] =
{
        &timing_mr,     &timing_mr,     &timing_mr,     &timing_mr,     &timing_mr,     &timing_mr,     &timing_mr,     &timing_rm
};
static int *opcode_timings_81_mod3[8] =
{
        &timing_mr,     &timing_mr,     &timing_mr,     &timing_mr,     &timing_mr,     &timing_mr,     &timing_mr,     &timing_rm
};

static int timing_count;
static uint8_t last_prefix;
static uint32_t regmask_modified;

static inline int COUNT(int *c, int op_32)
{
        if ((uintptr_t)c <= 10000)
                return (int)(uintptr_t)c;
        if (((uintptr_t)c & ~0xffff) == (-1 & ~0xffff))
        {
                if (op_32 & 0x100)
                        return ((uintptr_t)c >> 8) & 0xff;
                return (uintptr_t)c & 0xff;
        }
        return *c;
}

void codegen_timing_486_block_start()
{
        regmask_modified = 0;
}

void codegen_timing_486_start()
{
        timing_count = 0;
        last_prefix = 0;
}

void codegen_timing_486_prefix(uint8_t prefix, uint32_t fetchdat)
{
        timing_count += COUNT(opcode_timings[prefix], 0);
        last_prefix = prefix;
}

void codegen_timing_486_opcode(uint8_t opcode, uint32_t fetchdat, int op_32, uint32_t op_pc)
{
        int **timings;
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

                        case 0xc0: case 0xc1: case 0xd0: case 0xd1: case 0xd2: case 0xd3:
                        timings = mod3 ? opcode_timings_shift_mod3 : opcode_timings_shift;
                        deps = mod3 ? opcode_deps_shift_mod3 : opcode_deps_shift;
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

        timing_count += COUNT(timings[opcode], op_32);
        if (regmask_modified & get_addr_regmask(deps[opcode], fetchdat, op_32))
                timing_count++; /*AGI stall*/
        codegen_block_cycles += timing_count;

        regmask_modified = get_dstdep_mask(deps[opcode], fetchdat, bit8);
}

void codegen_timing_486_block_end()
{
}

codegen_timing_t codegen_timing_486 =
{
        codegen_timing_486_start,
        codegen_timing_486_prefix,
        codegen_timing_486_opcode,
        codegen_timing_486_block_start,
        codegen_timing_486_block_end,
        NULL
};
