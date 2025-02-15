/*Since IDT/Centaur didn't document cycle timings in the WinChip datasheets, and
  I don't currently own a WinChip 2 to test against, most of the timing here is
  a guess. This code makes the current (probably wrong) assumptions :
  - FPU uses same timings as a Pentium, except for FXCH (which doesn't pair)
  - 3DNow! instructions perfectly pair
  - MMX follows mostly Pentium rules - one pipeline has shift/pack, one has
    multiply, and other instructions can execute in either pipeline
  - Instructions with prefixes can pair if both instructions are fully decoded
    when the first instruction starts execution.*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86_ops.h"
#include "x87_sf.h"
#include "x87.h"
#include "codegen.h"
#include "codegen_ops.h"
#include "codegen_timing_common.h"

/*Instruction has different execution time for 16 and 32 bit data. Does not pair */
#define CYCLES_HAS_MULTI    (1 << 31)

#define CYCLES_FPU          (1 << 30)

#define CYCLES_IS_MMX_MUL   (1 << 29)
#define CYCLES_IS_MMX_SHIFT (1 << 28)
#define CYCLES_IS_MMX_ANY   (1 << 27)
#define CYCLES_IS_3DNOW     (1 << 26)

#define CYCLES_MMX_MUL(c)   (CYCLES_IS_MMX_MUL | c)
#define CYCLES_MMX_SHIFT(c) (CYCLES_IS_MMX_SHIFT | c)
#define CYCLES_MMX_ANY(c)   (CYCLES_IS_MMX_ANY | c)
#define CYCLES_3DNOW(c)     (CYCLES_IS_3DNOW | c)

#define CYCLES_IS_MMX       (CYCLES_IS_MMX_MUL | CYCLES_IS_MMX_SHIFT | CYCLES_IS_MMX_ANY | CYCLES_IS_3DNOW)

#define GET_CYCLES(c)       (c & ~(CYCLES_HAS_MULTI | CYCLES_FPU | CYCLES_IS_MMX))

#define CYCLES(c)           c
#define CYCLES2(c16, c32)   (CYCLES_HAS_MULTI | c16 | (c32 << 8))

/*comp_time = cycles until instruction complete
  i_overlap = cycles that overlap with integer
  f_overlap = cycles that overlap with subsequent FPU*/
#define FPU_CYCLES(comp_time, i_overlap, f_overlap) (comp_time) | (i_overlap << 8) | (f_overlap << 16) | CYCLES_FPU

#define FPU_COMP_TIME(timing)                       (timing & 0xff)
#define FPU_I_OVERLAP(timing)                       ((timing >> 8) & 0xff)
#define FPU_F_OVERLAP(timing)                       ((timing >> 16) & 0xff)

#define FPU_I_LATENCY(timing)                       (FPU_COMP_TIME(timing) - FPU_I_OVERLAP(timing))

#define FPU_F_LATENCY(timing)                       (FPU_I_OVERLAP(timing) - FPU_F_OVERLAP(timing))

#define FPU_RESULT_LATENCY(timing)                  ((timing >> 8) & 0xff)

#define INVALID                                     0

static uint32_t opcode_timings_winchip2[256] = {
    // clang-format off
/*00*/  CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(1),      CYCLES(1),      CYCLES(2),      CYCLES(3),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(1),      CYCLES(1),      CYCLES(2),      INVALID,
/*10*/  CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(1),      CYCLES(1),      CYCLES(2),      CYCLES(3),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(1),      CYCLES(1),      CYCLES(2),      CYCLES(3),
/*20*/  CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(3),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(3),
/*30*/  CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(2),

/*40*/  CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),
/*50*/  CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),
/*60*/  CYCLES(11),     CYCLES(9),      CYCLES(7),      CYCLES(9),      CYCLES(4),      CYCLES(4),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES2(17,25), CYCLES(1),      CYCLES2(17,20), CYCLES(17),     CYCLES(17),     CYCLES(17),     CYCLES(17),
/*70*/  CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),

/*80*/  CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(5),      CYCLES(5),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(3),      CYCLES(1),      CYCLES(5),      CYCLES(6),
/*90*/  CYCLES(1),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(0),      CYCLES(4),      CYCLES(4),      CYCLES(5),      CYCLES(2),      CYCLES(3),
/*a0*/  CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(7),      CYCLES(7),      CYCLES(8),      CYCLES(8),      CYCLES(1),      CYCLES(1),      CYCLES(5),      CYCLES(5),      CYCLES(5),      CYCLES(5),      CYCLES(6),      CYCLES(6),
/*b0*/  CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),

/*c0*/  CYCLES(4),      CYCLES(4),      CYCLES(5),      CYCLES(5),      CYCLES(6),      CYCLES(6),      CYCLES(1),      CYCLES(1),      CYCLES(14),     CYCLES(5),      CYCLES(0),      CYCLES(0),      CYCLES(0),      CYCLES(0),      CYCLES(3),      CYCLES(0),
/*d0*/  CYCLES(4),      CYCLES(4),      CYCLES(4),      CYCLES(4),      CYCLES(15),     CYCLES(14),     CYCLES(2),      CYCLES(4),      INVALID,        INVALID,        INVALID,        INVALID,        INVALID,        INVALID,        INVALID,        INVALID,
/*e0*/  CYCLES(6),      CYCLES(6),      CYCLES(6),      CYCLES(5),      CYCLES(14),     CYCLES(14),     CYCLES(16),     CYCLES(16),     CYCLES(3),      CYCLES(3),      CYCLES(17),     CYCLES(3),      CYCLES(14),     CYCLES(14),     CYCLES(14),     CYCLES(14),
/*f0*/  CYCLES(4),      CYCLES(0),      CYCLES(0),      CYCLES(0),      CYCLES(4),      CYCLES(2),      INVALID,        INVALID,        CYCLES(2),      CYCLES(2),      CYCLES(3),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(3),      INVALID
    // clang-format on
};

static uint32_t opcode_timings_winchip2_mod3[256] = {
    // clang-format off
/*00*/  CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(2),      CYCLES(3),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(2),      INVALID,
/*10*/  CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(2),      CYCLES(3),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(2),      CYCLES(3),
/*20*/  CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(3),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(3),
/*30*/  CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(2),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(2),

/*40*/  CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),
/*50*/  CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),
/*60*/  CYCLES(11),     CYCLES(9),      CYCLES(7),      CYCLES(9),      CYCLES(4),      CYCLES(4),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES2(14,25), CYCLES(1),      CYCLES2(17,20), CYCLES(17),     CYCLES(17),     CYCLES(17),     CYCLES(17),
/*70*/  CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),

/*80*/  CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(5),      CYCLES(5),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(2),      CYCLES(1),      CYCLES(2),      CYCLES(1),
/*90*/  CYCLES(1),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(0),      CYCLES(4),      CYCLES(4),      CYCLES(5),      CYCLES(2),      CYCLES(3),
/*a0*/  CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(7),      CYCLES(7),      CYCLES(8),      CYCLES(8),      CYCLES(1),      CYCLES(1),      CYCLES(5),      CYCLES(5),      CYCLES(5),      CYCLES(5),      CYCLES(6),      CYCLES(6),
/*b0*/  CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),      CYCLES(1),

/*c0*/  CYCLES(4),      CYCLES(4),      CYCLES(5),      CYCLES(5),      CYCLES(6),      CYCLES(6),      CYCLES(1),      CYCLES(1),      CYCLES(14),     CYCLES(5),      CYCLES(0),      CYCLES(0),      CYCLES(0),      CYCLES(0),      CYCLES(3),      CYCLES(0),
/*d0*/  CYCLES(4),      CYCLES(4),      CYCLES(4),      CYCLES(4),      CYCLES(15),     CYCLES(14),     CYCLES(2),      CYCLES(4),      INVALID,        INVALID,        INVALID,        INVALID,        INVALID,        INVALID,        INVALID,        INVALID,
/*e0*/  CYCLES(6),      CYCLES(6),      CYCLES(6),      CYCLES(5),      CYCLES(14),     CYCLES(14),     CYCLES(16),     CYCLES(16),     CYCLES(3),      CYCLES(3),      CYCLES(17),     CYCLES(3),      CYCLES(14),     CYCLES(14),     CYCLES(14),     CYCLES(14),
/*f0*/  CYCLES(4),      CYCLES(0),      CYCLES(0),      CYCLES(0),      CYCLES(4),      CYCLES(2),      INVALID,        INVALID,        CYCLES(2),      CYCLES(2),      CYCLES(3),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(3),      INVALID,
    // clang-format on
};

static uint32_t opcode_timings_winchip2_0f[256] = {
    // clang-format off
/*00*/  CYCLES(20),          CYCLES(11),          CYCLES(11),          CYCLES(10),          INVALID,           CYCLES(195),       CYCLES(7),         INVALID,             CYCLES(1000),        CYCLES(10000),       INVALID,             INVALID,             INVALID,           CYCLES_3DNOW(1),   CYCLES(1),         CYCLES_3DNOW(1),
/*10*/  INVALID,             INVALID,             INVALID,             INVALID,             INVALID,           INVALID,           INVALID,           INVALID,             INVALID,             INVALID,             INVALID,             INVALID,             INVALID,           INVALID,           INVALID,           INVALID,
/*20*/  CYCLES(6),           CYCLES(6),           CYCLES(6),           CYCLES(6),           CYCLES(6),         CYCLES(6),         INVALID,           INVALID,             INVALID,             INVALID,             INVALID,             INVALID,             INVALID,           INVALID,           INVALID,           INVALID,
/*30*/  CYCLES(9),           CYCLES(1),           CYCLES(9),           INVALID,             INVALID,           INVALID,           INVALID,           INVALID,             INVALID,             INVALID,             INVALID,             INVALID,             INVALID,           INVALID,           INVALID,           INVALID,

/*40*/  INVALID,             INVALID,             INVALID,             INVALID,             INVALID,           INVALID,           INVALID,           INVALID,             INVALID,             INVALID,             INVALID,             INVALID,             INVALID,           INVALID,           INVALID,           INVALID,
/*50*/  INVALID,             INVALID,             INVALID,             INVALID,             INVALID,           INVALID,           INVALID,           INVALID,             INVALID,             INVALID,             INVALID,             INVALID,             INVALID,           INVALID,           INVALID,           INVALID,
/*60*/  CYCLES_MMX_SHIFT(2), CYCLES_MMX_SHIFT(2), CYCLES_MMX_SHIFT(2), CYCLES_MMX_SHIFT(2), CYCLES_MMX_ANY(2), CYCLES_MMX_ANY(2), CYCLES_MMX_ANY(2), CYCLES_MMX_SHIFT(2), CYCLES_MMX_SHIFT(2), CYCLES_MMX_SHIFT(2), CYCLES_MMX_SHIFT(2), CYCLES_MMX_SHIFT(2), INVALID,           INVALID,           CYCLES_MMX_ANY(2), CYCLES_MMX_ANY(2),
/*70*/  INVALID,             CYCLES_MMX_SHIFT(2), CYCLES_MMX_SHIFT(2), CYCLES_MMX_SHIFT(2), CYCLES_MMX_ANY(2), CYCLES_MMX_ANY(2), CYCLES_MMX_ANY(2), CYCLES(100),         INVALID,             INVALID,             INVALID,             INVALID,             INVALID,           INVALID,           CYCLES_MMX_ANY(2), CYCLES_MMX_ANY(2),

/*80*/  CYCLES(1),           CYCLES(1),           CYCLES(1),           CYCLES(1),           CYCLES(1),         CYCLES(1),         CYCLES(1),         CYCLES(1),           CYCLES(1),           CYCLES(1),           CYCLES(1),           CYCLES(1),           CYCLES(1),         CYCLES(1),         CYCLES(1),         CYCLES(1),
/*90*/  CYCLES(3),           CYCLES(3),           CYCLES(3),           CYCLES(3),           CYCLES(3),         CYCLES(3),         CYCLES(3),         CYCLES(3),           CYCLES(3),           CYCLES(3),           CYCLES(3),           CYCLES(3),           CYCLES(3),         CYCLES(3),         CYCLES(3),         CYCLES(3),
/*a0*/  CYCLES(3),           CYCLES(3),           CYCLES(14),          CYCLES(8),           CYCLES(3),         CYCLES(4),         INVALID,           INVALID,             CYCLES(3),           CYCLES(3),           INVALID,             CYCLES(13),          CYCLES(3),         CYCLES(3),         INVALID,           CYCLES2(18,30),
/*b0*/  CYCLES(10),          CYCLES(10),          CYCLES(6),           CYCLES(13),          CYCLES(6),         CYCLES(6),         CYCLES(3),         CYCLES(3),           INVALID,             INVALID,             CYCLES(6),           CYCLES(13),          CYCLES(7),         CYCLES(7),         CYCLES(3),         CYCLES(3),

/*c0*/  CYCLES(4),           CYCLES(4),           INVALID,             INVALID,             INVALID,           INVALID,           INVALID,           CYCLES(3),           CYCLES(1),           CYCLES(1),           CYCLES(1),           CYCLES(1),           CYCLES(1),         CYCLES(1),         CYCLES(1),         CYCLES(1),
/*d0*/  INVALID,             CYCLES_MMX_SHIFT(2), CYCLES_MMX_SHIFT(2), CYCLES_MMX_SHIFT(2), INVALID,           CYCLES_MMX_MUL(2), INVALID,           INVALID,             CYCLES_MMX_ANY(2),   CYCLES_MMX_ANY(2),   INVALID,             CYCLES_MMX_ANY(2),   CYCLES_MMX_ANY(2), CYCLES_MMX_ANY(2), INVALID,           CYCLES_MMX_ANY(2),
/*e0*/  INVALID,             CYCLES_MMX_SHIFT(2), CYCLES_MMX_SHIFT(2), INVALID,             INVALID,           CYCLES_MMX_MUL(2), INVALID,           INVALID,             CYCLES_MMX_ANY(2),   CYCLES_MMX_ANY(2),   INVALID,             CYCLES_MMX_ANY(2),   CYCLES_MMX_ANY(2), CYCLES_MMX_ANY(2), INVALID,           CYCLES_MMX_ANY(2),
/*f0*/  INVALID,             CYCLES_MMX_SHIFT(2), CYCLES_MMX_SHIFT(2), CYCLES_MMX_SHIFT(2), INVALID,           CYCLES_MMX_MUL(2), INVALID,           INVALID,             CYCLES_MMX_ANY(2),   CYCLES_MMX_ANY(2),   CYCLES_MMX_ANY(2),   INVALID,             CYCLES_MMX_ANY(2), CYCLES_MMX_ANY(2), CYCLES_MMX_ANY(2), INVALID,
    // clang-format on
};
static uint32_t opcode_timings_winchip2_0f_mod3[256] = {
    // clang-format off
/*00*/  CYCLES(20),          CYCLES(11),          CYCLES(11),          CYCLES(10),          INVALID,           CYCLES(195),       CYCLES(7),         INVALID,             CYCLES(1000),        CYCLES(10000),       INVALID,             INVALID,             INVALID,           CYCLES_3DNOW(1),   CYCLES(1),         CYCLES_3DNOW(1),
/*10*/  INVALID,             INVALID,             INVALID,             INVALID,             INVALID,           INVALID,           INVALID,           INVALID,             INVALID,             INVALID,             INVALID,             INVALID,             INVALID,           INVALID,           INVALID,           INVALID,
/*20*/  CYCLES(6),           CYCLES(6),           CYCLES(6),           CYCLES(6),           CYCLES(6),         CYCLES(6),         INVALID,           INVALID,             INVALID,             INVALID,             INVALID,             INVALID,             INVALID,           INVALID,           INVALID,           INVALID,
/*30*/  CYCLES(9),           CYCLES(1),           CYCLES(9),           INVALID,             INVALID,           INVALID,           INVALID,           INVALID,             INVALID,             INVALID,             INVALID,             INVALID,             INVALID,           INVALID,           INVALID,           INVALID,

/*40*/  INVALID,             INVALID,             INVALID,             INVALID,             INVALID,           INVALID,           INVALID,           INVALID,             INVALID,             INVALID,             INVALID,             INVALID,             INVALID,           INVALID,           INVALID,           INVALID,
/*50*/  INVALID,             INVALID,             INVALID,             INVALID,             INVALID,           INVALID,           INVALID,           INVALID,             INVALID,             INVALID,             INVALID,             INVALID,             INVALID,           INVALID,           INVALID,           INVALID,
/*60*/  CYCLES_MMX_SHIFT(1), CYCLES_MMX_SHIFT(1), CYCLES_MMX_SHIFT(1), CYCLES_MMX_SHIFT(1), CYCLES_MMX_ANY(1), CYCLES_MMX_ANY(1), CYCLES_MMX_ANY(1), CYCLES_MMX_SHIFT(1), CYCLES_MMX_SHIFT(1), CYCLES_MMX_SHIFT(1), CYCLES_MMX_SHIFT(1), CYCLES_MMX_SHIFT(1), INVALID,           INVALID,           CYCLES_MMX_ANY(1), CYCLES_MMX_ANY(1),
/*70*/  INVALID,             CYCLES_MMX_SHIFT(1), CYCLES_MMX_SHIFT(1), CYCLES_MMX_SHIFT(1), CYCLES_MMX_ANY(1), CYCLES_MMX_ANY(1), CYCLES_MMX_ANY(1), CYCLES(100),         INVALID,             INVALID,             INVALID,             INVALID,             INVALID,           INVALID,           CYCLES_MMX_ANY(1), CYCLES_MMX_ANY(1),

/*80*/  CYCLES(1),           CYCLES(1),           CYCLES(1),           CYCLES(1),           CYCLES(1),         CYCLES(1),         CYCLES(1),         CYCLES(1),           CYCLES(1),           CYCLES(1),           CYCLES(1),           CYCLES(1),           CYCLES(1),         CYCLES(1),         CYCLES(1),         CYCLES(1),
/*90*/  CYCLES(3),           CYCLES(3),           CYCLES(3),           CYCLES(3),           CYCLES(3),         CYCLES(3),         CYCLES(3),         CYCLES(3),           CYCLES(3),           CYCLES(3),           CYCLES(3),           CYCLES(3),           CYCLES(3),         CYCLES(3),         CYCLES(3),         CYCLES(3),
/*a0*/  CYCLES(3),           CYCLES(3),           CYCLES(14),          CYCLES(8),           CYCLES(3),         CYCLES(4),         INVALID,           INVALID,             CYCLES(3),           CYCLES(3),           INVALID,             CYCLES(13),          CYCLES(3),         CYCLES(3),         INVALID,           CYCLES2(18,30),
/*b0*/  CYCLES(10),          CYCLES(10),          CYCLES(6),           CYCLES(13),          CYCLES(6),         CYCLES(6),         CYCLES(3),         CYCLES(3),           INVALID,             INVALID,             CYCLES(6),           CYCLES(13),          CYCLES(7),         CYCLES(7),         CYCLES(3),         CYCLES(3),

/*c0*/  CYCLES(4),           CYCLES(4),           INVALID,             INVALID,             INVALID,           INVALID,           INVALID,           CYCLES(3),           CYCLES(1),           CYCLES(1),           CYCLES(1),           CYCLES(1),           CYCLES(1),         CYCLES(1),         CYCLES(1),         CYCLES(1),
/*d0*/  INVALID,             CYCLES_MMX_SHIFT(1), CYCLES_MMX_SHIFT(1), CYCLES_MMX_SHIFT(1), INVALID,           CYCLES_MMX_MUL(1), INVALID,           INVALID,             CYCLES_MMX_ANY(1),   CYCLES_MMX_ANY(1),   INVALID,             CYCLES_MMX_ANY(1),   CYCLES_MMX_ANY(1), CYCLES_MMX_ANY(1), INVALID,           CYCLES_MMX_ANY(1),
/*e0*/  INVALID,             CYCLES_MMX_SHIFT(1), CYCLES_MMX_SHIFT(1), INVALID,             INVALID,           CYCLES_MMX_MUL(1), INVALID,           INVALID,             CYCLES_MMX_ANY(1),   CYCLES_MMX_ANY(1),   INVALID,             CYCLES_MMX_ANY(1),   CYCLES_MMX_ANY(1), CYCLES_MMX_ANY(1), INVALID,           CYCLES_MMX_ANY(1),
/*f0*/  INVALID,             CYCLES_MMX_SHIFT(1), CYCLES_MMX_SHIFT(1), CYCLES_MMX_SHIFT(1), INVALID,           CYCLES_MMX_MUL(1), INVALID,           INVALID,             CYCLES_MMX_ANY(1),   CYCLES_MMX_ANY(1),   CYCLES_MMX_ANY(1),   INVALID,             CYCLES_MMX_ANY(1), CYCLES_MMX_ANY(1), CYCLES_MMX_ANY(1), INVALID,
    // clang-format on
};

static uint32_t opcode_timings_winchip2_shift[8] = {
    // clang-format off
        CYCLES(7),      CYCLES(7),      CYCLES(10),     CYCLES(10),     CYCLES(7),      CYCLES(7),      CYCLES(7),      CYCLES(7)
    // clang-format on
};
static uint32_t opcode_timings_winchip2_shift_mod3[8] = {
    // clang-format off
        CYCLES(3),      CYCLES(3),      CYCLES(9),      CYCLES(9),      CYCLES(3),      CYCLES(3),      CYCLES(3),      CYCLES(3)
    // clang-format on
};

static uint32_t opcode_timings_winchip2_f6[8] = {
    // clang-format off
        CYCLES(2),      INVALID,        CYCLES(2),      CYCLES(2),      CYCLES(13),     CYCLES(14),     CYCLES(16),     CYCLES(19)
    // clang-format on
};
static uint32_t opcode_timings_winchip2_f6_mod3[8] = {
    // clang-format off
        CYCLES(1),      INVALID,        CYCLES(1),      CYCLES(1),      CYCLES(13),     CYCLES(14),     CYCLES(16),     CYCLES(19)
    // clang-format on
};
static uint32_t opcode_timings_winchip2_f7[8] = {
    // clang-format off
        CYCLES(2),      INVALID,        CYCLES(2),      CYCLES(2),      CYCLES(21),     CYCLES2(22,38), CYCLES2(24,40), CYCLES2(27,43)
    // clang-format on
};
static uint32_t opcode_timings_winchip2_f7_mod3[8] = {
    // clang-format off
        CYCLES(1),      INVALID,        CYCLES(1),      CYCLES(1),      CYCLES(21),     CYCLES2(22,38), CYCLES2(24,40), CYCLES2(27,43)
    // clang-format on
};
static uint32_t opcode_timings_winchip2_ff[8] = {
    // clang-format off
        CYCLES(2),      CYCLES(2),      CYCLES(5),      CYCLES(0),      CYCLES(5),      CYCLES(0),      CYCLES(5),      INVALID
    // clang-format on
};
static uint32_t opcode_timings_winchip2_ff_mod3[8] = {
    // clang-format off
        CYCLES(1),      CYCLES(1),      CYCLES(5),      CYCLES(0),      CYCLES(5),      CYCLES(0),      CYCLES(5),      INVALID
    // clang-format on
};

static uint32_t opcode_timings_winchip2_d8[8] = {
    // clang-format off
/*      FADDs               FMULs               FCOMs                 FCOMPs*/
        FPU_CYCLES(3,2,2),  FPU_CYCLES(3,2,2),  FPU_CYCLES(1,0,0),    FPU_CYCLES(1,0,0),
/*      FSUBs               FSUBRs              FDIVs                 FDIVRs*/
        FPU_CYCLES(3,2,2),  FPU_CYCLES(3,2,2),  FPU_CYCLES(39,38,2),  FPU_CYCLES(39,38,2)
    // clang-format on
};
static uint32_t opcode_timings_winchip2_d8_mod3[8] = {
    // clang-format off
/*      FADD                  FMUL                  FCOM                  FCOMP*/
        FPU_CYCLES(3,2,2),    FPU_CYCLES(3,2,2),    FPU_CYCLES(1,0,0),    FPU_CYCLES(1,0,0),
/*      FSUB                  FSUBR                 FDIV                  FDIVR*/
        FPU_CYCLES(3,2,2),    FPU_CYCLES(3,2,2),    FPU_CYCLES(39,38,2),  FPU_CYCLES(39,38,2)
    // clang-format on
};

static uint32_t opcode_timings_winchip2_d9[8] = {
    // clang-format off
/*      FLDs                                     FSTs                 FSTPs*/
        FPU_CYCLES(1,0,0),   INVALID,            FPU_CYCLES(2,0,0),   FPU_CYCLES(2,0,0),
/*      FLDENV               FLDCW               FSTENV               FSTCW*/
        FPU_CYCLES(32,0,0),  FPU_CYCLES(8,0,0),  FPU_CYCLES(48,0,0),  FPU_CYCLES(2,0,0)
    // clang-format on
};
static uint32_t opcode_timings_winchip2_d9_mod3[64] = {
    // clang-format off
        /*FLD*/
        FPU_CYCLES(1,0,0),  FPU_CYCLES(1,0,0),  FPU_CYCLES(1,0,0),  FPU_CYCLES(1,0,0),
        FPU_CYCLES(1,0,0),  FPU_CYCLES(1,0,0),  FPU_CYCLES(1,0,0),  FPU_CYCLES(1,0,0),
        /*FXCH*/
        FPU_CYCLES(1,0,0),  FPU_CYCLES(1,0,0),  FPU_CYCLES(1,0,0),  FPU_CYCLES(1,0,0),
        FPU_CYCLES(1,0,0),  FPU_CYCLES(1,0,0),  FPU_CYCLES(1,0,0),  FPU_CYCLES(1,0,0),
        /*FNOP*/
        FPU_CYCLES(3,0,0),  INVALID,            INVALID,            INVALID,
        INVALID,            INVALID,            INVALID,            INVALID,
        /*FSTP*/
        FPU_CYCLES(1,0,0),  FPU_CYCLES(1,0,0),  FPU_CYCLES(1,0,0),  FPU_CYCLES(1,0,0),
        FPU_CYCLES(1,0,0),  FPU_CYCLES(1,0,0),  FPU_CYCLES(1,0,0),  FPU_CYCLES(1,0,0),
/*      opFCHS              opFABS*/
        FPU_CYCLES(1,0,0),  FPU_CYCLES(1,0,0),  INVALID,            INVALID,
/*      opFTST              opFXAM*/
        FPU_CYCLES(1,0,0),  FPU_CYCLES(21,4,0), INVALID,            INVALID,
/*      opFLD1              opFLDL2T            opFLDL2E            opFLDPI*/
        FPU_CYCLES(2,0,0),  FPU_CYCLES(5,2,2),  FPU_CYCLES(5,2,2),  FPU_CYCLES(5,2,2),
/*      opFLDEG2            opFLDLN2            opFLDZ*/
        FPU_CYCLES(5,2,2),  FPU_CYCLES(5,2,2),  FPU_CYCLES(2,0,0),  INVALID,
/*      opF2XM1             opFYL2X             opFPTAN             opFPATAN*/
        FPU_CYCLES(53,2,2), FPU_CYCLES(103,2,2),FPU_CYCLES(120,36,0),FPU_CYCLES(112,2,2),
/*                                              opFDECSTP           opFINCSTP,*/
        INVALID,            INVALID,            FPU_CYCLES(2,0,0),  FPU_CYCLES(2,0,0),
/*      opFPREM                                 opFSQRT             opFSINCOS*/
        FPU_CYCLES(64,2,2), INVALID,            FPU_CYCLES(70,69,2),FPU_CYCLES(89,2,2),
/*      opFRNDINT           opFSCALE            opFSIN              opFCOS*/
        FPU_CYCLES(9,0,0),  FPU_CYCLES(20,5,0), FPU_CYCLES(65,2,2), FPU_CYCLES(65,2,2)
    // clang-format on
};

static uint32_t opcode_timings_winchip2_da[8] = {
    // clang-format off
/*      FIADDl              FIMULl              FICOMl                FICOMPl*/
        FPU_CYCLES(6,2,2),  FPU_CYCLES(6,2,2),  FPU_CYCLES(4,0,0),    FPU_CYCLES(4,0,0),
/*      FISUBl              FISUBRl             FIDIVl                FIDIVRl*/
        FPU_CYCLES(6,2,2),  FPU_CYCLES(6,2,2),  FPU_CYCLES(42,38,2),  FPU_CYCLES(42,38,2)
    // clang-format on
};
static uint32_t opcode_timings_winchip2_da_mod3[8] = {
    // clang-format off
        INVALID,            INVALID,            INVALID,              INVALID,
/*                          FCOMPP*/
        INVALID,            FPU_CYCLES(1,0,0),  INVALID,              INVALID
    // clang-format on
};

static uint32_t opcode_timings_winchip2_db[8] = {
    // clang-format off
/*      FLDil                                   FSTil                 FSTPil*/
        FPU_CYCLES(3,2,2),  INVALID,            FPU_CYCLES(6,0,0),    FPU_CYCLES(6,0,0),
/*                          FLDe                                      FSTPe*/
        INVALID,            FPU_CYCLES(3,0,0),  INVALID,              FPU_CYCLES(3,0,0)
    // clang-format on
};
static uint32_t opcode_timings_winchip2_db_mod3[64] = {
    // clang-format off
        INVALID,            INVALID,            INVALID,              INVALID,
        INVALID,            INVALID,            INVALID,              INVALID,

        INVALID,            INVALID,            INVALID,              INVALID,
        INVALID,            INVALID,            INVALID,              INVALID,

        INVALID,            INVALID,            INVALID,              INVALID,
        INVALID,            INVALID,            INVALID,              INVALID,

        INVALID,            INVALID,            INVALID,              INVALID,
        INVALID,            INVALID,            INVALID,              INVALID,

/*                          opFNOP              opFCLEX               opFINIT*/
        INVALID,            FPU_CYCLES(1,0,0),  FPU_CYCLES(7,0,0),    FPU_CYCLES(17,0,0),
/*      opFNOP              opFNOP*/
        FPU_CYCLES(1,0,0),  FPU_CYCLES(1,0,0),  INVALID,              INVALID,

        INVALID,            INVALID,            INVALID,              INVALID,
        INVALID,            INVALID,            INVALID,              INVALID,

        INVALID,            INVALID,            INVALID,              INVALID,
        INVALID,            INVALID,            INVALID,              INVALID,

        INVALID,            INVALID,            INVALID,              INVALID,
        INVALID,            INVALID,            INVALID,              INVALID,
    // clang-format on
};

static uint32_t opcode_timings_winchip2_dc[8] = {
    // clang-format off
/*      FADDd               FMULd               FCOMd                 FCOMPd*/
        FPU_CYCLES(3,2,2),  FPU_CYCLES(3,2,2),  FPU_CYCLES(1,0,0),    FPU_CYCLES(1,0,0),
/*      FSUBd               FSUBRd              FDIVd                 FDIVRd*/
        FPU_CYCLES(3,2,2),  FPU_CYCLES(3,2,2),  FPU_CYCLES(39,38,2),  FPU_CYCLES(39,38,2)
    // clang-format on
};
static uint32_t opcode_timings_winchip2_dc_mod3[8] = {
    // clang-format off
/*      opFADDr               opFMULr*/
        FPU_CYCLES(3,2,2),    FPU_CYCLES(3,2,2),INVALID,              INVALID,
/*      opFSUBRr              opFSUBr           opFDIVRr              opFDIVr*/
        FPU_CYCLES(3,2,2),    FPU_CYCLES(3,2,2),FPU_CYCLES(39,38,2),  FPU_CYCLES(39,38,2)
    // clang-format on
};

static uint32_t opcode_timings_winchip2_dd[8] = {
    // clang-format off
/*      FLDd                                    FSTd                  FSTPd*/
        FPU_CYCLES(1,0,0),    INVALID,          FPU_CYCLES(2,0,0),    FPU_CYCLES(2,0,0),
/*      FRSTOR                                  FSAVE                 FSTSW*/
        FPU_CYCLES(70,0,0),   INVALID,          FPU_CYCLES(127,0,0),  FPU_CYCLES(6,0,0)
    // clang-format on
};
static uint32_t opcode_timings_winchip2_dd_mod3[8] = {
    // clang-format off
/*      FFFREE                                  FST                   FSTP*/
        FPU_CYCLES(2,0,0),    INVALID,          FPU_CYCLES(1,0,0),    FPU_CYCLES(1,0,0),
/*      FUCOM                 FUCOMP*/
        FPU_CYCLES(1,0,0),    FPU_CYCLES(1,0,0),INVALID,              INVALID
    // clang-format on
};

static uint32_t opcode_timings_winchip2_de[8] = {
    // clang-format off
/*      FIADDw              FIMULw              FICOMw                FICOMPw*/
        FPU_CYCLES(6,2,2),  FPU_CYCLES(6,2,2),  FPU_CYCLES(4,0,0),    FPU_CYCLES(4,0,0),
/*      FISUBw              FISUBRw             FIDIVw                FIDIVRw*/
        FPU_CYCLES(6,2,2),  FPU_CYCLES(6,2,2),  FPU_CYCLES(42,38,2),  FPU_CYCLES(42,38,2)
    // clang-format on
};
static uint32_t opcode_timings_winchip2_de_mod3[8] = {
    // clang-format off
/*      FADDP               FMULP                                     FCOMPP*/
        FPU_CYCLES(3,2,2),  FPU_CYCLES(3,2,2),  INVALID,              FPU_CYCLES(1,0,0),
/*      FSUBP               FSUBRP              FDIVP                 FDIVRP*/
        FPU_CYCLES(3,2,2),  FPU_CYCLES(3,2,2),  FPU_CYCLES(39,38,2),  FPU_CYCLES(39,38,2)
    // clang-format on
};

static uint32_t opcode_timings_winchip2_df[8] = {
    // clang-format off
/*      FILDiw                                  FISTiw                FISTPiw*/
        FPU_CYCLES(3,2,2),  INVALID,            FPU_CYCLES(6,0,0),    FPU_CYCLES(6,0,0),
/*                          FILDiq              FBSTP                 FISTPiq*/
        INVALID,            FPU_CYCLES(3,2,2),  FPU_CYCLES(148,0,0),  FPU_CYCLES(6,0,0)
    // clang-format on
};
static uint32_t opcode_timings_winchip2_df_mod3[8] = {
    // clang-format off
        INVALID,            INVALID,            INVALID,              INVALID,
/*      FSTSW AX*/
        FPU_CYCLES(6,0,0),  INVALID,            INVALID,              INVALID
    // clang-format on
};

static uint32_t opcode_timings_winchip2_8x[8] = {
    // clang-format off
        CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2)
    // clang-format on
};
static uint32_t opcode_timings_winchip2_8x_mod3[8] = {
    // clang-format off
        CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2)
    // clang-format on
};
static uint32_t opcode_timings_winchip2_81[8] = {
    // clang-format off
        CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2)
    // clang-format on
};
static uint32_t opcode_timings_winchip2_81_mod3[8] = {
    // clang-format off
        CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2),      CYCLES(2)
    // clang-format on
};

static int      timing_count;
static uint8_t  last_prefix;
static uint32_t regmask_modified;
static int      decode_delay;
static int      decode_delay_offset;
static int      fpu_latency;
static int      fpu_st_latency[8];

static int       u_pipe_full;
static uint32_t  u_pipe_opcode;
static uint32_t *u_pipe_timings;
static uint32_t  u_pipe_op_32;
static uint32_t  u_pipe_regmask;
static uint32_t  u_pipe_fetchdat;
static int       u_pipe_decode_delay_offset;
static uint64_t *u_pipe_deps;

int
can_pair(uint32_t timing_a, uint32_t timing_b, uint8_t regmask_b)
{
    /*Only MMX/3DNow instructions can pair*/
    if (!(timing_b & CYCLES_IS_MMX))
        return 0;
    /*Only one MMX multiply per cycle*/
    if ((timing_a & CYCLES_IS_MMX_MUL) && (timing_b & CYCLES_IS_MMX_MUL))
        return 0;
    /*Only one MMX shift/pack per cycle*/
    if ((timing_a & CYCLES_IS_MMX_SHIFT) && (timing_b & CYCLES_IS_MMX_SHIFT))
        return 0;
    /*Second instruction can not access registers written by first*/
    if (u_pipe_regmask & regmask_b)
        return 0;
    /*Must have had enough time to decode prefixes*/
    if ((decode_delay + decode_delay_offset + u_pipe_decode_delay_offset) > 0)
        return 0;

    return 1;
}

static inline int
COUNT(uint32_t c, int op_32)
{
    if (c & CYCLES_FPU)
        return FPU_I_LATENCY(c);
    if (c & CYCLES_HAS_MULTI) {
        if (op_32 & 0x100)
            return (c >> 8) & 0xff;
        return c & 0xff;
    }
    return GET_CYCLES(c);
}

static int
check_agi(uint64_t *deps, uint8_t opcode, uint32_t fetchdat, int op_32)
{
    uint32_t addr_regmask = get_addr_regmask(deps[opcode], fetchdat, op_32);

    /*Instructions that use ESP implicitly (eg PUSH, POP, CALL etc) do not
      cause AGIs with each other, but do with instructions that use it explicitly*/
    if ((addr_regmask & REGMASK_IMPL_ESP) && (regmask_modified & (1 << REG_ESP)) && !(regmask_modified & REGMASK_IMPL_ESP))
        addr_regmask |= (1 << REG_ESP);

    return (regmask_modified & addr_regmask) & ~REGMASK_IMPL_ESP;
}

static int
codegen_fpu_latencies(uint64_t deps, int reg)
{
    int latency = fpu_latency;

    if ((deps & FPU_RW_ST0) && fpu_st_latency[0] && fpu_st_latency[0] > latency)
        latency = fpu_st_latency[0];
    if ((deps & FPU_RW_ST1) && fpu_st_latency[1] && fpu_st_latency[1] > latency)
        latency = fpu_st_latency[1];
    if ((deps & FPU_RW_STREG) && fpu_st_latency[reg] && fpu_st_latency[reg] > latency)
        latency = fpu_st_latency[reg];

    return latency;
}

#define SUB_AND_CLAMP(latency, count) \
    latency -= count;                 \
    if (latency < 0)                  \
    latency = 0

static void
codegen_fpu_latency_clock(int count)
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

static void
codegen_instruction(uint32_t *timings, uint64_t *deps, uint8_t opcode, uint32_t fetchdat, int decode_delay_offset, int op_32, int exec_delay)
{
    int instr_cycles;
    int latency = 0;

    if ((timings[opcode] & CYCLES_FPU) && !(deps[opcode] & FPU_FXCH))
        instr_cycles = latency = codegen_fpu_latencies(deps[opcode], fetchdat & 7);
    else
        instr_cycles = 0;

    if ((decode_delay + decode_delay_offset) > 0)
        codegen_fpu_latency_clock(decode_delay + decode_delay_offset + instr_cycles);
    else
        codegen_fpu_latency_clock(instr_cycles);
    instr_cycles += COUNT(timings[opcode], op_32);
    instr_cycles += exec_delay;
    if ((decode_delay + decode_delay_offset) > 0)
        codegen_block_cycles += instr_cycles + decode_delay + decode_delay_offset;
    else
        codegen_block_cycles += instr_cycles;
    decode_delay = (-instr_cycles) + 1;

    if (deps[opcode] & FPU_POP) {
        for (uint8_t c = 0; c < 7; c++)
            fpu_st_latency[c] = fpu_st_latency[c + 1];
        fpu_st_latency[7] = 0;
    }
    if (deps[opcode] & FPU_POP2) {
        for (uint8_t c = 0; c < 6; c++)
            fpu_st_latency[c] = fpu_st_latency[c + 2];
        fpu_st_latency[6] = fpu_st_latency[7] = 0;
    }
    if (timings[opcode] & CYCLES_FPU) {
#if 0
        if (fpu_latency)
            fatal("Bad latency FPU\n");*/
#endif
        fpu_latency = FPU_F_LATENCY(timings[opcode]);
    }

    if (deps[opcode] & FPU_PUSH) {
        for (uint8_t c = 0; c < 7; c++)
            fpu_st_latency[c + 1] = fpu_st_latency[c];
        fpu_st_latency[0] = 0;
    }
    if (deps[opcode] & FPU_WRITE_ST0) {
#if 0
        if (fpu_st_latency[0])
            fatal("Bad latency ST0\n");*/
#endif
        fpu_st_latency[0] = FPU_RESULT_LATENCY(timings[opcode]);
    }
    if (deps[opcode] & FPU_WRITE_ST1) {
#if 0
        if (fpu_st_latency[1])
            fatal("Bad latency ST1\n");*/
#endif
        fpu_st_latency[1] = FPU_RESULT_LATENCY(timings[opcode]);
    }
    if (deps[opcode] & FPU_WRITE_STREG) {
        int reg = fetchdat & 7;
        if (deps[opcode] & FPU_POP)
            reg--;
        if (reg >= 0 && !(reg == 0 && (deps[opcode] & FPU_WRITE_ST0)) && !(reg == 1 && (deps[opcode] & FPU_WRITE_ST1))) {
#if 0
            if (fpu_st_latency[reg])
                fatal("Bad latency STREG %i %08x %i %016llx %02x\n",fpu_st_latency[reg], fetchdat, reg, timings[opcode], opcode);*/
#endif
            fpu_st_latency[reg] = FPU_RESULT_LATENCY(timings[opcode]);
        }
    }
}

static void
codegen_timing_winchip2_block_start(void)
{
    regmask_modified = 0;
    decode_delay = decode_delay_offset = 0;
    u_pipe_full                        = 0;
}

static void
codegen_timing_winchip2_start(void)
{
    timing_count = 0;
    last_prefix  = 0;
}

static void
codegen_timing_winchip2_prefix(uint8_t prefix, UNUSED(uint32_t fetchdat))
{
    if (prefix == 0x0f) {
        /*0fh prefix is 'free'*/
        last_prefix = prefix;
        return;
    }
    /*On WinChip all prefixes take 1 cycle to decode. Decode may be shadowed
      by execution of previous instructions*/
    decode_delay_offset++;
    last_prefix = prefix;
}

static void
codegen_timing_winchip2_opcode(uint8_t opcode, uint32_t fetchdat, int op_32, UNUSED(uint32_t op_pc))
{
    uint32_t *timings;
    uint64_t *deps;
    int       mod3      = ((fetchdat & 0xc0) == 0xc0);
    int       bit8      = !(opcode & 1);
    int       agi_stall = 0;

    switch (last_prefix) {
        case 0x0f:
            timings = mod3 ? opcode_timings_winchip2_0f_mod3 : opcode_timings_winchip2_0f;
            deps    = mod3 ? opcode_deps_0f_mod3 : opcode_deps_0f;
            break;

        case 0xd8:
            timings = mod3 ? opcode_timings_winchip2_d8_mod3 : opcode_timings_winchip2_d8;
            deps    = mod3 ? opcode_deps_d8_mod3 : opcode_deps_d8;
            opcode  = (opcode >> 3) & 7;
            break;
        case 0xd9:
            timings = mod3 ? opcode_timings_winchip2_d9_mod3 : opcode_timings_winchip2_d9;
            deps    = mod3 ? opcode_deps_d9_mod3 : opcode_deps_d9;
            opcode  = mod3 ? opcode & 0x3f : (opcode >> 3) & 7;
            break;
        case 0xda:
            timings = mod3 ? opcode_timings_winchip2_da_mod3 : opcode_timings_winchip2_da;
            deps    = mod3 ? opcode_deps_da_mod3 : opcode_deps_da;
            opcode  = (opcode >> 3) & 7;
            break;
        case 0xdb:
            timings = mod3 ? opcode_timings_winchip2_db_mod3 : opcode_timings_winchip2_db;
            deps    = mod3 ? opcode_deps_db_mod3 : opcode_deps_db;
            opcode  = mod3 ? opcode & 0x3f : (opcode >> 3) & 7;
            break;
        case 0xdc:
            timings = mod3 ? opcode_timings_winchip2_dc_mod3 : opcode_timings_winchip2_dc;
            deps    = mod3 ? opcode_deps_dc_mod3 : opcode_deps_dc;
            opcode  = (opcode >> 3) & 7;
            break;
        case 0xdd:
            timings = mod3 ? opcode_timings_winchip2_dd_mod3 : opcode_timings_winchip2_dd;
            deps    = mod3 ? opcode_deps_dd_mod3 : opcode_deps_dd;
            opcode  = (opcode >> 3) & 7;
            break;
        case 0xde:
            timings = mod3 ? opcode_timings_winchip2_de_mod3 : opcode_timings_winchip2_de;
            deps    = mod3 ? opcode_deps_de_mod3 : opcode_deps_de;
            opcode  = (opcode >> 3) & 7;
            break;
        case 0xdf:
            timings = mod3 ? opcode_timings_winchip2_df_mod3 : opcode_timings_winchip2_df;
            deps    = mod3 ? opcode_deps_df_mod3 : opcode_deps_df;
            opcode  = (opcode >> 3) & 7;
            break;

        default:
            switch (opcode) {
                case 0x80:
                case 0x82:
                case 0x83:
                    timings = mod3 ? opcode_timings_winchip2_8x_mod3 : opcode_timings_winchip2_8x;
                    deps    = mod3 ? opcode_deps_8x_mod3 : opcode_deps_8x;
                    opcode  = (fetchdat >> 3) & 7;
                    break;
                case 0x81:
                    timings = mod3 ? opcode_timings_winchip2_81_mod3 : opcode_timings_winchip2_81;
                    deps    = mod3 ? opcode_deps_81_mod3 : opcode_deps_81;
                    opcode  = (fetchdat >> 3) & 7;
                    break;

                case 0xc0:
                case 0xc1:
                case 0xd0:
                case 0xd1:
                case 0xd2:
                case 0xd3:
                    timings = mod3 ? opcode_timings_winchip2_shift_mod3 : opcode_timings_winchip2_shift;
                    deps    = mod3 ? opcode_deps_shift_mod3 : opcode_deps_shift;
                    opcode  = (fetchdat >> 3) & 7;
                    break;

                case 0xf6:
                    timings = mod3 ? opcode_timings_winchip2_f6_mod3 : opcode_timings_winchip2_f6;
                    deps    = mod3 ? opcode_deps_f6_mod3 : opcode_deps_f6;
                    opcode  = (fetchdat >> 3) & 7;
                    break;
                case 0xf7:
                    timings = mod3 ? opcode_timings_winchip2_f7_mod3 : opcode_timings_winchip2_f7;
                    deps    = mod3 ? opcode_deps_f7_mod3 : opcode_deps_f7;
                    opcode  = (fetchdat >> 3) & 7;
                    break;
                case 0xff:
                    timings = mod3 ? opcode_timings_winchip2_ff_mod3 : opcode_timings_winchip2_ff;
                    deps    = mod3 ? opcode_deps_ff_mod3 : opcode_deps_ff;
                    opcode  = (fetchdat >> 3) & 7;
                    break;

                default:
                    timings = mod3 ? opcode_timings_winchip2_mod3 : opcode_timings_winchip2;
                    deps    = mod3 ? opcode_deps_mod3 : opcode_deps;
                    break;
            }
    }

    if (u_pipe_full) {
        uint8_t regmask = get_srcdep_mask(deps[opcode], fetchdat, bit8, u_pipe_op_32);

        if (can_pair(u_pipe_timings[u_pipe_opcode], timings[opcode], regmask)) {
            int      cycles_a  = u_pipe_timings[u_pipe_opcode] & 0xff;
            int      cycles_b  = timings[opcode] & 0xff;
            uint32_t timing    = (cycles_a > cycles_b) ? u_pipe_timings[u_pipe_opcode] : timings[opcode];
            uint64_t temp_deps = 0;

            if (check_agi(deps, opcode, fetchdat, op_32) || check_agi(u_pipe_deps, u_pipe_opcode, u_pipe_fetchdat, u_pipe_op_32))
                agi_stall = 1;

            codegen_instruction(&timing, &temp_deps, 0, 0, 0, 0, agi_stall);
            u_pipe_full         = 0;
            decode_delay_offset = 0;
            regmask_modified    = get_dstdep_mask(deps[opcode], fetchdat, bit8) | u_pipe_regmask;
            return;
        } else {
            /*No pairing, run first instruction now*/
            if (check_agi(u_pipe_deps, u_pipe_opcode, u_pipe_fetchdat, u_pipe_op_32))
                agi_stall = 1;
            codegen_instruction(u_pipe_timings, u_pipe_deps, u_pipe_opcode, u_pipe_fetchdat, u_pipe_decode_delay_offset, u_pipe_op_32, agi_stall);
            u_pipe_full      = 0;
            regmask_modified = u_pipe_regmask;
        }
    }
    if (timings[opcode] & CYCLES_IS_MMX) {
        /*Might pair with next instruction*/
        u_pipe_full                = 1;
        u_pipe_opcode              = opcode;
        u_pipe_timings             = timings;
        u_pipe_op_32               = op_32;
        u_pipe_regmask             = get_dstdep_mask(deps[opcode], fetchdat, bit8);
        u_pipe_fetchdat            = fetchdat;
        u_pipe_decode_delay_offset = decode_delay_offset;
        u_pipe_deps                = deps;
        decode_delay_offset        = 0;
        return;
    }

    if (check_agi(deps, opcode, fetchdat, op_32))
        agi_stall = 1;
    codegen_instruction(timings, deps, opcode, fetchdat, decode_delay_offset, op_32, agi_stall);
    decode_delay_offset = 0;
    regmask_modified    = get_dstdep_mask(deps[opcode], fetchdat, bit8);
}

static void
codegen_timing_winchip2_block_end(void)
{
    if (u_pipe_full) {
        int agi_stall = 0;

        if (check_agi(u_pipe_deps, u_pipe_opcode, u_pipe_fetchdat, u_pipe_op_32))
            agi_stall = 1;
        codegen_instruction(u_pipe_timings, u_pipe_deps, u_pipe_opcode, u_pipe_fetchdat, u_pipe_decode_delay_offset, u_pipe_op_32, agi_stall);
        u_pipe_full = 0;
    }
}

codegen_timing_t codegen_timing_winchip2 = {
    codegen_timing_winchip2_start,
    codegen_timing_winchip2_prefix,
    codegen_timing_winchip2_opcode,
    codegen_timing_winchip2_block_start,
    codegen_timing_winchip2_block_end,
    NULL
};
