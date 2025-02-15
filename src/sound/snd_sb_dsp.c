/*Jazz sample rates :
  386-33 - 12kHz
  486-33 - 20kHz
  486-50 - 32kHz
  Pentium - 45kHz*/

#define _USE_MATH_DEFINES
#include <math.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/filters.h>
#include <86box/io.h>
#include <86box/midi.h>
#include <86box/pic.h>
#include <86box/snd_azt2316a.h>
#include <86box/sound.h>
#include <86box/timer.h>
#include <86box/snd_sb.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>

/* NON-PCM SAMPLE FORMATS */
#define ADPCM_4  1
#define ADPCM_26 2
#define ADPCM_2  3
#define ESPCM_4  4
#define ESPCM_3  5
/*      ESPCM_2?   */
#define ESPCM_1  7
#define ESPCM_4E 8    /* For differentiating between 4-bit encoding and decoding modes. */

/* The recording safety margin is intended for uneven "len" calls to the get_buffer mixer calls on sound_sb. */
#define SB_DSP_REC_SAFEFTY_MARGIN 4096

enum {
    DSP_S_NORMAL = 0,
    DSP_S_RESET,
    DSP_S_RESET_WAIT
};

void pollsb(void *priv);
void sb_poll_i(void *priv);

static int sbe2dat[4][9] = {
    {  0x01, -0x02, -0x04,  0x08, -0x10,  0x20,  0x40, -0x80, -106 },
    { -0x01,  0x02, -0x04,  0x08,  0x10, -0x20,  0x40, -0x80,  165 },
    { -0x01,  0x02,  0x04, -0x08,  0x10, -0x20, -0x40,  0x80, -151 },
    {  0x01, -0x02,  0x04, -0x08, -0x10,  0x20, -0x40,  0x80,  90  }
};

static int sb_commands[256] = {
    -1,  2, -1,  0,  1,  2, -1,  0,  1, -1, -1, -1, -1, -1,  2,  1,
     1, -1, -1, -1,  2, -1,  2,  2, -1, -1, -1, -1,  0, -1, -1,  0,
     0, -1, -1, -1,  2, -1, -1, -1, -1, -1, -1, -1,  0, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
     1,  2,  2, -1, -1, -1, -1, -1,  2, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1,  2,  2,  2,  2, -1, -1, -1, -1, -1,  0, -1,  0,
     2,  2, -1, -1, -1, -1, -1, -1,  2,  2, -1, -1, -1, -1, -1, -1,
     0, -1, -1, -1, -1, -1, -1, -1,  0, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     0,  0, -1,  0,  0,  0,  0, -1,  0,  0,  0, -1, -1, -1, -1, -1,
     1,  0,  1,  0,  1, -1, -1,  0,  0, -1, -1, -1, -1, -1, -1, -1,
    -1, -1,  0,  0, -1, -1, -1, -1, -1,  1,  2, -1, -1, -1, -1,  0
};

#if 0
// Currently unused, here for reference if ever needed
char     sb202_copyright[] = "COPYRIGHT(C) CREATIVE TECHNOLOGY PTE. LTD. (1991) "
#endif
char     sb16_copyright[]  = "COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.";
uint16_t sb_dsp_versions[] = {
    0,     /* Pad */
    0,     /* SADLIB      - No DSP */
    0x105, /* SB_DSP_105      - SB1/1.5, DSP v1.05 */
    0x200, /* SB_DSP_200      - SB1.5/2, DSP v2.00 */
    0x201, /* SB_DSP_201      - SB1.5/2, DSP v2.01 - needed for high-speed DMA */
    0x202, /* SB_DSP_202      - SB2, DSP v2.02 */
    0x300, /* SB_PRO_DSP_300  - SB Pro, DSP v3.00 */
    0x302, /* SBPRO2_DSP_302 - SB Pro 2, DSP v3.02 + OPL3 */
    0x404, /* SB16_DSP_404        - DSP v4.04 + OPL3 */
    0x405, /* SB16_405        - DSP v4.05 + OPL3 */
    0x406, /* SB16_406        - DSP v4.06 + OPL3 */
    0x40b, /* SB16_411        - DSP v4.11 + OPL3 */
    0x40c, /* SBAWE32         - DSP v4.12 + OPL3 */
    0x40d, /* SBAWE32PNP      - DSP v4.13 + OPL3 */
    0x410  /* SBAWE64         - DSP v4.16 + OPL3 */
};

/*These tables were 'borrowed' from DOSBox*/
int8_t scaleMap4[64] = {
    0,  1,  2,  3,  4,  5,  6,  7,  0,  -1,  -2,  -3,  -4,  -5,  -6,  -7,
    1,  3,  5,  7,  9, 11, 13, 15, -1,  -3,  -5,  -7,  -9, -11, -13, -15,
    2,  6, 10, 14, 18, 22, 26, 30, -2,  -6, -10, -14, -18, -22, -26, -30,
    4, 12, 20, 28, 36, 44, 52, 60, -4, -12, -20, -28, -36, -44, -52, -60
};

uint8_t adjustMap4[64] = {
      0, 0, 0, 0, 0, 16, 16, 16,
      0, 0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0, 16, 16, 16,
    240, 0, 0, 0, 0,  0,  0,  0,
    240, 0, 0, 0, 0,  0,  0,  0
};

int8_t scaleMap26[40] = {
    0,  1,  2,  3,  0,  -1,  -2,  -3,
    1,  3,  5,  7, -1,  -3,  -5,  -7,
    2,  6, 10, 14, -2,  -6, -10, -14,
    4, 12, 20, 28, -4, -12, -20, -28,
    5, 15, 25, 35, -5, -15, -25, -35
};

uint8_t adjustMap26[40] = {
      0, 0, 0, 8,   0, 0, 0, 8,
    248, 0, 0, 8, 248, 0, 0, 8,
    248, 0, 0, 8, 248, 0, 0, 8,
    248, 0, 0, 8, 248, 0, 0, 8,
    248, 0, 0, 0, 248, 0, 0, 0
};

int8_t scaleMap2[24] = {
    0,  1,  0,  -1, 1,  3,  -1,  -3,
    2,  6, -2,  -6, 4, 12,  -4, -12,
    8, 24, -8, -24, 6, 48, -16, -48
};

uint8_t adjustMap2[24] = {
      0, 4,   0, 4,
    252, 4, 252, 4,
    252, 4, 252, 4,
    252, 4, 252, 4,
    252, 4, 252, 4,
    252, 0, 252, 0
};

// clang-format off
/* Upper half only used for ESPCM_3 mode. */
/* TODO: Extract actual table (or exact ranges + range interpolation algo, whatever it is) from chip, someday, somehow.
 * This current table is part software reverse engineering, part guesswork/extrapolation.
 * It's close enough to what's in the chip to produce acceptable results, but not exact.
 **/
int8_t espcm_range_map[512] = {
       -8,  -7,  -6,  -5,  -4,  -3,  -2,  -1,   0,   1,   2,   3,   4,   5,   6,   7,
      -10,  -8,  -7,  -5,  -4,  -3,  -2,  -1,   0,   2,   3,   4,   5,   6,   8,   9,
      -12, -11,  -9,  -8,  -6,  -5,  -3,  -2,   0,   2,   3,   5,   6,   8,  10,  11,
      -14, -12, -11,  -9,  -7,  -5,  -4,  -2,   0,   2,   4,   5,   7,   9,  11,  13,
      -16, -14, -12, -10,  -8,  -6,  -4,  -2,   0,   2,   4,   6,   8,  10,  12,  14,
      -21, -18, -16, -13, -11,  -8,  -6,  -3,   0,   2,   5,   7,  10,  12,  15,  18,
      -27, -24, -21, -17, -14, -11,  -8,  -4,   0,   3,   7,  10,  13,  17,  20,  24,
      -35, -28, -24, -20, -16, -12,  -8,  -4,   0,   4,   8,  12,  16,  20,  24,  28,
      -40, -35, -30, -25, -20, -15, -10,  -5,   0,   5,  10,  15,  20,  25,  30,  35,
      -48, -42, -36, -30, -24, -18, -12,  -6,   0,   6,  12,  18,  24,  30,  36,  43,
      -56, -49, -42, -35, -28, -21, -14,  -7,   0,   7,  14,  21,  28,  35,  42,  49,
      -72, -63, -54, -45, -36, -27, -18,  -9,   0,   9,  18,  27,  36,  45,  54,  63,
      -85, -74, -64, -53, -43, -32, -22, -11,   0,  11,  22,  33,  43,  54,  64,  75,
     -102, -98, -85, -71, -58, -45, -31, -14,   0,  13,  26,  39,  52,  65,  78,  90,
     -127,-112, -96, -80, -64, -48, -32, -16,   0,  16,  32,  48,  64,  80,  96, 112,
     -128,-127,-109, -91, -73, -54, -36, -18,   0,  18,  36,  54,  73,  91, 109, 127,
       -8,  -7,  -6,  -5,  -4,  -3,  -2,  -1,   0,   1,   2,   3,   4,   5,   6,   7,
      -10,  -9,  -8,  -6,  -5,  -4,  -3,  -2,  -1,   1,   2,   3,   4,   6,   7,   8,
      -13, -11,  -9,  -7,  -6,  -5,  -3,  -2,  -1,   2,   3,   5,   6,   7,   9,  10,
      -15, -13, -12, -10,  -8,  -6,  -5,  -3,  -1,   2,   3,   5,   6,   8,  10,  12,
      -18, -15, -13, -11,  -9,  -7,  -5,  -3,  -1,   2,   3,   5,   7,   9,  11,  13,
      -24, -20, -17, -15, -12, -10,  -7,  -5,  -2,   2,   3,   6,   8,  11,  13,  16,
      -29, -26, -23, -19, -16, -13, -10,  -6,  -2,   2,   5,   8,  11,  15,  18,  22,
      -34, -30, -26, -22, -18, -14, -10,  -6,  -2,   2,   6,  10,  14,  18,  22,  26,
      -43, -38, -33, -28, -23, -18, -13,  -8,  -3,   2,   7,  12,  17,  22,  27,  32,
      -51, -45, -39, -33, -27, -21, -15,  -9,  -3,   3,   9,  15,  21,  27,  33,  39,
      -60, -53, -46, -39, -32, -25, -18, -11,  -4,   3,  10,  17,  24,  31,  38,  45,
      -77, -68, -59, -50, -41, -32, -23, -14,  -5,   4,  13,  22,  31,  40,  49,  58,
      -90, -80, -69, -59, -48, -38, -27, -17,  -6,   5,  16,  27,  38,  48,  59,  69,
     -112,-104, -91, -78, -65, -52, -38, -23,  -7,   6,  19,  32,  45,  58,  71,  84,
     -128,-120,-104, -88, -72, -56, -40, -24,  -8,   8,  24,  40,  56,  72,  88, 104,
     -128,-128,-118,-100, -82, -64, -45, -27,  -9,   9,  27,  45,  63,  82, 100, 118
};

/* address = table_index(9:8) | dsp->espcm_last_value(7:3) | codeword(2:0)
 * the value is a base index into espcm_range_map with bits at (8, 3:0),
 * to be OR'ed with dsp->espcm_range at (7:4)
 */
uint16_t espcm3_dpcm_tables[1024] =
{
    /* Table 0 */
     256, 257, 258, 259, 260, 263, 266, 269,   0, 257, 258, 259, 260, 263, 266, 269,
       0,   1, 258, 259, 260, 263, 266, 269,   1,   2, 259, 260, 261, 263, 266, 269,
       1,   3, 260, 261, 262, 264, 266, 269,   1,   3,   4, 261, 262, 264, 266, 269,
       2,   4,   5, 262, 263, 264, 266, 269,   2,   4,   6, 263, 264, 265, 267, 269,
       2,   4,   6,   7, 264, 265, 267, 269,   2,   5,   7,   8, 265, 266, 267, 269,
       2,   5,   7,   8,   9, 266, 268, 270,   2,   5,   7,   9,  10, 267, 268, 270,
       2,   5,   8,  10,  11, 268, 269, 270,   2,   5,   8,  11,  12, 269, 270, 271,
       2,   5,   8,  11,  12,  13, 270, 271,   2,   5,   8,  11,  12,  13,  14, 271,
       0, 257, 258, 259, 260, 263, 266, 269,   0,   1, 258, 259, 260, 263, 266, 269,
       0,   1,   2, 259, 260, 263, 266, 269,   1,   2,   3, 260, 261, 263, 266, 269,
       1,   3,   4, 261, 262, 264, 266, 269,   1,   3,   5, 262, 263, 264, 266, 269,
       2,   4,   5,   6, 263, 264, 266, 269,   2,   4,   6,   7, 264, 265, 267, 269,
       2,   4,   6,   7,   8, 265, 267, 269,   2,   5,   7,   8,   9, 266, 267, 269,
       2,   5,   7,   9,  10, 267, 268, 270,   2,   5,   7,   9,  10,  11, 268, 270,
       2,   5,   8,  10,  11,  12, 269, 270,   2,   5,   8,  11,  12,  13, 270, 271,
       2,   5,   8,  11,  12,  13,  14, 271,   2,   5,   8,  11,  12,  13,  14,  15,
    /* Table 1 */
     257, 260, 262, 263, 264, 265, 267, 270, 257, 260, 262, 263, 264, 265, 267, 270,
       1, 260, 262, 263, 264, 265, 267, 270,   1, 260, 262, 263, 264, 265, 267, 270,
       1, 260, 262, 263, 264, 265, 267, 270,   1,   4, 262, 263, 264, 265, 267, 270,
       1,   4, 262, 263, 264, 265, 267, 270,   1,   4,   6, 263, 264, 265, 267, 270,
       1,   4,   6,   7, 264, 265, 267, 270,   1,   4,   6,   7,   8, 265, 267, 270,
       1,   4,   6,   7,   8,   9, 267, 270,   1,   4,   6,   7,   8,   9, 267, 270,
       1,   4,   6,   7,   8,   9,  11, 270,   1,   4,   6,   7,   8,   9,  11, 270,
       1,   4,   6,   7,   8,   9,  11, 270,   1,   4,   6,   7,   8,   9,  11,  14,
     257, 260, 262, 263, 264, 265, 267, 270,   1, 260, 262, 263, 264, 265, 267, 270,
       1, 260, 262, 263, 264, 265, 267, 270,   1, 260, 262, 263, 264, 265, 267, 270,
       1,   4, 262, 263, 264, 265, 267, 270,   1,   4, 262, 263, 264, 265, 267, 270,
       1,   4,   6, 263, 264, 265, 267, 270,   1,   4,   6,   7, 264, 265, 267, 270,
       1,   4,   6,   7,   8, 265, 267, 270,   1,   4,   6,   7,   8,   9, 267, 270,
       1,   4,   6,   7,   8,   9, 267, 270,   1,   4,   6,   7,   8,   9,  11, 270,
       1,   4,   6,   7,   8,   9,  11, 270,   1,   4,   6,   7,   8,   9,  11, 270,
       1,   4,   6,   7,   8,   9,  11,  14,   1,   4,   6,   7,   8,   9,  11,  14,
    /* Table 2 */
     256, 257, 258, 259, 260, 262, 265, 268,   0, 257, 258, 259, 260, 262, 265, 268,
       0,   1, 258, 259, 260, 262, 265, 269,   1,   2, 259, 260, 261, 263, 265, 269,
       1,   3, 260, 261, 262, 263, 265, 269,   1,   3,   4, 261, 262, 263, 265, 269,
       1,   3,   5, 262, 263, 264, 266, 269,   1,   4,   5,   6, 263, 264, 266, 269,
       1,   4,   6,   7, 264, 265, 266, 269,   1,   4,   6,   7,   8, 265, 266, 269,
       2,   4,   6,   7,   8,   9, 267, 269,   2,   4,   6,   7,   8,   9, 267, 269,
       2,   5,   7,   8,   9,  10,  11, 270,   2,   5,   7,   8,   9,  10,  11, 270,
       2,   5,   8,   9,  10,  11,  12, 270,   2,   6,   8,  10,  11,  12,  13,  14,
     257, 258, 259, 260, 261, 263, 265, 269,   1, 259, 260, 261, 262, 263, 266, 269,
       1, 260, 261, 262, 263, 264, 266, 269,   1, 260, 261, 262, 263, 264, 266, 269,
       2,   4, 262, 263, 264, 265, 267, 269,   2,   4, 262, 263, 264, 265, 267, 269,
       2,   5,   6, 263, 264, 265, 267, 270,   2,   5,   6,   7, 264, 265, 267, 270,
       2,   5,   7,   8, 265, 266, 267, 270,   2,   5,   7,   8,   9, 266, 268, 270,
       2,   6,   8,   9,  10, 267, 268, 270,   2,   6,   8,   9,  10,  11, 268, 270,
       2,   6,   8,  10,  11,  12, 269, 270,   2,   6,   9,  11,  12,  13, 270, 271,
       3,   6,   9,  11,  12,  13,  14, 271,   3,   6,   9,  11,  12,  13,  14,  15,
    /* Table 3 */
     256, 258, 260, 261, 262, 263, 264, 265,   0, 258, 260, 261, 262, 263, 264, 265,
       1, 259, 260, 261, 262, 263, 264, 266,   1, 259, 260, 261, 262, 263, 264, 266,
       1,   3, 260, 261, 262, 263, 264, 266,   1,   3,   4, 261, 262, 263, 264, 267,
       1,   3,   4,   5, 262, 263, 264, 267,   1,   3,   4,   5,   6, 263, 264, 267,
       1,   3,   5,   6,   7, 264, 265, 268,   1,   3,   5,   6,   7,   8, 265, 268,
       1,   4,   6,   7,   8,   9, 266, 269,   1,   4,   6,   7,   8,   9,  10, 269,
       1,   4,   6,   7,   8,   9,  10, 269,   1,   4,   6,   7,   8,   9,  11, 270,
       1,   4,   6,   7,   8,   9,  11, 270,   1,   4,   6,   7,   8,   9,  11,  14,
     257, 260, 262, 263, 264, 265, 267, 270,   1, 260, 262, 263, 264, 265, 267, 270,
       1, 260, 262, 263, 264, 265, 267, 270,   2, 261, 262, 263, 264, 265, 267, 270,
       2, 261, 262, 263, 264, 265, 267, 270,   2,   5, 262, 263, 264, 265, 267, 270,
       3,   6, 263, 264, 265, 266, 268, 270,   3,   6,   7, 264, 265, 266, 268, 270,
       4,   7,   8, 265, 266, 267, 268, 270,   4,   7,   8,   9, 266, 267, 268, 270,
       4,   7,   8,   9,  10, 267, 268, 270,   5,   7,   8,   9,  10,  11, 268, 270,
       5,   7,   8,   9,  10,  11,  12, 270,   5,   7,   8,   9,  10,  11,  12, 270,
       6,   7,   8,   9,  10,  11,  13, 271,   6,   7,   8,   9,  10,  11,  13,  15
};
// clang-format on

double low_fir_sb16_coef[5][SB16_NCoef];

#ifdef ENABLE_SB_DSP_LOG
int sb_dsp_do_log = ENABLE_SB_DSP_LOG;

static void
sb_dsp_log(const char *fmt, ...)
{
    va_list ap;

    if (sb_dsp_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sb_dsp_log(fmt, ...)
#endif

#define ESSreg(reg) (dsp)->ess_regs[reg - 0xA0]

static __inline double
sinc(double x)
{
    return sin(M_PI * x) / (M_PI * x);
}

static void
recalc_sb16_filter(const int c, const int playback_freq)
{
    /* Cutoff frequency = playback / 2 */
    int          n;
    const double fC = ((double) playback_freq) / (double) FREQ_96000;

    for (n = 0; n < SB16_NCoef; n++) {
        /* Blackman window */
        const double w = 0.42 - (0.5 * cos((2.0 * n * M_PI) / (double) (SB16_NCoef - 1))) +
                     (0.08 * cos((4.0 * n * M_PI) / (double) (SB16_NCoef - 1)));
        /* Sinc filter */
        const double h = sinc(2.0 * fC * ((double) n - ((double) (SB16_NCoef - 1) / 2.0)));

        /* Create windowed-sinc filter */
        low_fir_sb16_coef[c][n] = w * h;
    }

    low_fir_sb16_coef[c][(SB16_NCoef - 1) / 2] = 1.0;

    double gain = 0.0;
    for (n = 0; n < SB16_NCoef; n++)
        gain += low_fir_sb16_coef[c][n];

    /* Normalise filter, to produce unity gain */
    for (n = 0; n < SB16_NCoef; n++)
        low_fir_sb16_coef[c][n] /= gain;
}

static void
recalc_opl_filter(const int playback_freq)
{
    /* Cutoff frequency = playback / 2 */
    int          n;
    const double fC = ((double) playback_freq) / (double) (FREQ_49716 * 2);

    for (n = 0; n < SB16_NCoef; n++) {
        /* Blackman window */
        const double w = 0.42 - (0.5 * cos((2.0 * n * M_PI) / (double) (SB16_NCoef - 1))) +
                     (0.08 * cos((4.0 * n * M_PI) / (double) (SB16_NCoef - 1)));
        /* Sinc filter */
        const double h = sinc(2.0 * fC * ((double) n - ((double) (SB16_NCoef - 1) / 2.0)));

        /* Create windowed-sinc filter */
        low_fir_sb16_coef[1][n] = w * h;
    }

    low_fir_sb16_coef[1][(SB16_NCoef - 1) / 2] = 1.0;

    double gain = 0.0;
    for (n = 0; n < SB16_NCoef; n++)
        gain += low_fir_sb16_coef[1][n];

    /* Normalise filter, to produce unity gain */
    for (n = 0; n < SB16_NCoef; n++)
        low_fir_sb16_coef[1][n] /= gain;
}

static void
sb_irq_update_pic(void *priv, const int set)
{
    const sb_dsp_t *dsp = (sb_dsp_t *) priv;
    if (set)
        picint(1 << dsp->sb_irqnum);
    else
        picintc(1 << dsp->sb_irqnum);
}

void
sb_update_mask(sb_dsp_t *dsp, int irqm8, int irqm16, int irqm401)
{
    int clear = 0;

    if (!dsp->sb_irqm8 && irqm8)
        clear |= 1;
    dsp->sb_irqm8 = irqm8;
    if (!dsp->sb_irqm16 && irqm16)
        clear |= 1;
    dsp->sb_irqm16 = irqm16;
    if (!dsp->sb_irqm401 && irqm401)
        clear |= 1;
    dsp->sb_irqm401 = irqm401;

    if (clear)
        dsp->irq_update(dsp->irq_priv, 0);
}

void
sb_update_status(sb_dsp_t *dsp, int bit, int set)
{
    int masked = 0;

    if (dsp->sb_irq8 || dsp->sb_irq16)
        return;

    /* NOTE: not on ES1688 or ES1868 */
    if (IS_ESS(dsp) && (dsp->sb_subtype != SB_SUBTYPE_ESS_ES1688) && !(ESSreg(0xB1) & 0x10))
        /* If ESS playback, and IRQ disabled, do not fire. */
        return;

    switch (bit) {
        default:
        case 0:
            dsp->sb_irq8 = set;
            masked       = dsp->sb_irqm8;
            break;
        case 1:
            dsp->sb_irq16 = set;
            masked        = dsp->sb_irqm16;
            break;
        case 2:
            dsp->sb_irq401 = set;
            masked         = dsp->sb_irqm401;
            break;
    }

    /* NOTE: not on ES1688, apparently; investigate on ES1868 */
    if (IS_ESS(dsp) && (dsp->sb_subtype > SB_SUBTYPE_ESS_ES1688)) {
        /* TODO: Investigate real hardware for this (the ES1887 datasheet documents this bit somewhat oddly.) */
        if (dsp->ess_playback_mode && bit <= 1 && set && !masked) {
            if (!(ESSreg(0xB1) & 0x40)) // if ESS playback, and IRQ disabled, do not fire
            {
                return;
            }
        }
    }

    if (set && !masked)
        dsp->irq_update(dsp->irq_priv, 1);
    else if (!set)
        dsp->irq_update(dsp->irq_priv, 0);
}

void
sb_irq(sb_dsp_t *dsp, int irq8)
{
    sb_update_status(dsp, !irq8, 1);
}

void
sb_irqc(sb_dsp_t *dsp, int irq8)
{
    sb_update_status(dsp, !irq8, 0);
}

static void
sb_dsp_irq_update(void *priv, const int set)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;

    sb_update_status(dsp, 2, set);
}

static int
sb_dsp_irq_pending(void *priv)
{
    const sb_dsp_t *dsp = (sb_dsp_t *) priv;

    return dsp->sb_irq401;
}

void
sb_dsp_set_mpu(sb_dsp_t *dsp, mpu_t *mpu)
{
    dsp->mpu = mpu;

    if (IS_NOT_ESS(dsp) && (mpu != NULL))
        mpu401_irq_attach(mpu, sb_dsp_irq_update, sb_dsp_irq_pending, dsp);
}

static void
sb_stop_dma(const sb_dsp_t *dsp)
{
    dma_set_drq(dsp->sb_8_dmanum, 0);

    if (dsp->sb_16_dmanum != 0xff) {
        if (dsp->sb_16_dmanum == 4)
            dma_set_drq(dsp->sb_8_dmanum, 0);
        else
            dma_set_drq(dsp->sb_16_dmanum, 0);
    }

    if (dsp->sb_16_8_dmanum != 0xff)
        dma_set_drq(dsp->sb_16_8_dmanum, 0);
}

static void
sb_finish_dma(sb_dsp_t *dsp)
{
    if (dsp->ess_playback_mode) {
        ESSreg(0xB8) &= ~0x01;
        dma_set_drq(dsp->sb_8_dmanum, 0);
    } else
        sb_stop_dma(dsp);
}

void
sb_dsp_reset(sb_dsp_t *dsp)
{
    midi_clear_buffer();

    if (dsp->sb_8_enable) {
        dsp->sb_8_enable = 0;
        sb_finish_dma(dsp);
    }

    if (dsp->sb_16_enable) {
        dsp->sb_16_enable = 0;
        sb_finish_dma(dsp);
    }

    timer_disable(&dsp->output_timer);
    timer_disable(&dsp->input_timer);

    dsp->sb_command = 0;

    dsp->sb_8_length  = 0xffff;
    dsp->sb_8_autolen = 0xffff;

    dsp->sb_irq8     = 0;
    dsp->sb_irq16    = 0;
    dsp->sb_irq401   = 0;
    dsp->sb_16_pause = 0;
    dsp->sb_read_wp = dsp->sb_read_rp = 0;
    dsp->sb_data_stat                 = -1;
    dsp->sb_speaker                   = 0;
    dsp->sb_pausetime                 = -1LL;
    dsp->sbe2                         = 0xAA;
    dsp->sbe2count                    = 0;

    dsp->sbreset = 0;

    dsp->record_pos_read  = 0;
    dsp->record_pos_write = SB_DSP_REC_SAFEFTY_MARGIN;

    dsp->irq_update(dsp->irq_priv, 0);

    dsp->asp_data_len = 0;
}

void
sb_doreset(sb_dsp_t *dsp)
{
    sb_dsp_reset(dsp);

    if (IS_AZTECH(dsp)) {
        sb_commands[8] = 1;
        sb_commands[9] = 1;
    } else {
        if (dsp->sb_type >= SB16_DSP_404)
            sb_commands[8] = 1;
        else
            sb_commands[8] = -1;
    }

    dsp->sb_asp_mode      = 0;
    dsp->sb_asp_ram_index = 0;
    for (uint16_t c = 0; c < 256; c++)
        dsp->sb_asp_regs[c] = 0;

    dsp->sb_asp_regs[5] = 0x01;
    dsp->sb_asp_regs[9] = 0xf8;

    /* Initialize ESS registers */
    ESSreg(0xA5) = 0xf8;
}

void
sb_dsp_speed_changed(sb_dsp_t *dsp)
{
    if (dsp->sb_timeo < 256)
        dsp->sblatcho = (double) (TIMER_USEC * (256 - dsp->sb_timeo));
    else
        dsp->sblatcho = ((double) TIMER_USEC * (1000000.0 / (double) (dsp->sb_timeo - 256)));

    if (dsp->sb_timei < 256)
        dsp->sblatchi = (double) (TIMER_USEC * (256 - dsp->sb_timei));
    else
        dsp->sblatchi = ((double) TIMER_USEC * (1000000.0 / (double) (dsp->sb_timei - 256)));
}

void
sb_add_data(sb_dsp_t *dsp, uint8_t v)
{
    dsp->sb_read_data[dsp->sb_read_wp++] = v;
    dsp->sb_read_wp &= 0xff;
}

static unsigned int
sb_ess_get_dma_counter(const sb_dsp_t *dsp)
{
    unsigned int c = (unsigned int) ESSreg(0xA5) << 8U;
    c |= (unsigned int) ESSreg(0xA4);

    return c;
}

static unsigned int
sb_ess_get_dma_len(const sb_dsp_t *dsp)
{
    return 0x10000U - sb_ess_get_dma_counter(dsp);
}

static void
sb_resume_dma(const sb_dsp_t *dsp, const int is_8)
{
    if IS_ESS(dsp)
    {
        dma_set_drq(dsp->sb_8_dmanum, 1);
        dma_set_drq(dsp->sb_16_8_dmanum, 1);
    } else if (is_8)
        dma_set_drq(dsp->sb_8_dmanum, 1);
    else {
        if (dsp->sb_16_dmanum != 0xff) {
            if (dsp->sb_16_dmanum == 4)
                dma_set_drq(dsp->sb_8_dmanum, 1);
            else
                dma_set_drq(dsp->sb_16_dmanum, 1);
        }

        if (dsp->sb_16_8_dmanum != 0xff)
            dma_set_drq(dsp->sb_16_8_dmanum, 1);
    }
}

void
sb_start_dma(sb_dsp_t *dsp, int dma8, int autoinit, uint8_t format, int len)
{
    sb_stop_dma(dsp);

    dsp->sb_pausetime = -1;

    if (dma8) {
        dsp->sb_8_length = dsp->sb_8_origlength = len;
        dsp->sb_8_format                        = format;
        dsp->sb_8_autoinit                      = autoinit;
        dsp->sb_8_pause                         = 0;
        dsp->sb_8_enable                        = 1;
        dsp->dma_ff                             = 0;

        if (dsp->sb_16_enable && dsp->sb_16_output)
            dsp->sb_16_enable = 0;
        dsp->sb_8_output = 1;
        if (!timer_is_enabled(&dsp->output_timer))
            timer_set_delay_u64(&dsp->output_timer, (uint64_t) dsp->sblatcho);
        dsp->sbleftright = dsp->sbleftright_default;
        dsp->sbdacpos    = 0;

        dma_set_drq(dsp->sb_8_dmanum, 1);
    } else {
        dsp->sb_16_length = dsp->sb_16_origlength = len;
        dsp->sb_16_format                         = format;
        dsp->sb_16_autoinit                       = autoinit;
        dsp->sb_16_pause                          = 0;
        dsp->sb_16_enable                         = 1;
        if (dsp->sb_8_enable && dsp->sb_8_output)
            dsp->sb_8_enable = 0;
        dsp->sb_16_output = 1;
        if (!timer_is_enabled(&dsp->output_timer))
            timer_set_delay_u64(&dsp->output_timer, (uint64_t) dsp->sblatcho);

        if (dsp->sb_16_dma_supported) {
            if (dsp->sb_16_dmanum == 4)
                dma_set_drq(dsp->sb_8_dmanum, 1);
            else
                dma_set_drq(dsp->sb_16_dmanum, 1);
        } else
            dma_set_drq(dsp->sb_16_8_dmanum, 1);
    }

    /* This will be set later for ESS playback/record modes. */
    dsp->ess_playback_mode = 0;
}

void
sb_start_dma_i(sb_dsp_t *dsp, int dma8, int autoinit, uint8_t format, int len)
{
    sb_stop_dma(dsp);

    if (dma8) {
        dsp->sb_8_length = dsp->sb_8_origlength = len;
        dsp->sb_8_format                        = format;
        dsp->sb_8_autoinit                      = autoinit;
        dsp->sb_8_pause                         = 0;
        dsp->sb_8_enable                        = 1;
        if (dsp->sb_16_enable && !dsp->sb_16_output)
            dsp->sb_16_enable = 0;
        dsp->sb_8_output = 0;
        if (!timer_is_enabled(&dsp->input_timer))
            timer_set_delay_u64(&dsp->input_timer, (uint64_t) dsp->sblatchi);

        dma_set_drq(dsp->sb_8_dmanum, 1);
    } else {
        dsp->sb_16_length = dsp->sb_16_origlength = len;
        dsp->sb_16_format                         = format;
        dsp->sb_16_autoinit                       = autoinit;
        dsp->sb_16_pause                          = 0;
        dsp->sb_16_enable                         = 1;
        if (dsp->sb_8_enable && !dsp->sb_8_output)
            dsp->sb_8_enable = 0;
        dsp->sb_16_output = 0;
        if (!timer_is_enabled(&dsp->input_timer))
            timer_set_delay_u64(&dsp->input_timer, (uint64_t) dsp->sblatchi);

        if (dsp->sb_16_dma_supported) {
            if (dsp->sb_16_dmanum == 4)
                dma_set_drq(dsp->sb_8_dmanum, 1);
            else
                dma_set_drq(dsp->sb_16_dmanum, 1);
        } else
            dma_set_drq(dsp->sb_16_8_dmanum, 1);
    }

    memset(dsp->record_buffer, 0, sizeof(dsp->record_buffer));
}

void
sb_start_dma_ess(sb_dsp_t *dsp)
{
    uint8_t real_format  = 0;
    dsp->ess_dma_counter = sb_ess_get_dma_counter(dsp);
    uint32_t len         = sb_ess_get_dma_len(dsp);

    if (IS_ESS(dsp)) {
        dma_set_drq(dsp->sb_8_dmanum, 0);
        dma_set_drq(dsp->sb_16_8_dmanum, 0);
    }
    real_format |= !!(ESSreg(0xB7) & 0x20) ? 0x10 : 0;
    real_format |= !!(ESSreg(0xB7) & 0x8) ? 0x20 : 0;
    if (!!(ESSreg(0xB8) & 8))
        sb_start_dma_i(dsp, !(ESSreg(0xB7) & 4), (ESSreg(0xB8) >> 2) & 1, real_format, (int) len);
    else
        sb_start_dma(dsp, !(ESSreg(0xB7) & 4), (ESSreg(0xB8) >> 2) & 1, real_format, (int) len);
    dsp->ess_playback_mode = 1;
    dma_set_drq(dsp->sb_8_dmanum, 1);
    dma_set_drq(dsp->sb_16_8_dmanum, 1);
}

void
sb_stop_dma_ess(sb_dsp_t *dsp)
{
    dsp->sb_8_enable = dsp->sb_16_enable = 0;
    dma_set_drq(dsp->sb_16_8_dmanum, 0);
    dma_set_drq(dsp->sb_8_dmanum, 0);
}

static void
sb_ess_update_dma_status(sb_dsp_t *dsp)
{
    bool dma_en = (ESSreg(0xB8) & 1) ? true : false;

    /* If the DRQ is disabled, do not start. */
    if (!(ESSreg(0xB2) & 0x40))
        dma_en = false;

    if (dma_en) {
        if (!dsp->sb_8_enable && !dsp->sb_16_enable)
            sb_start_dma_ess(dsp);
    } else {
        if (dsp->sb_8_enable || dsp->sb_16_enable)
            sb_stop_dma_ess(dsp);
    }
}

int
sb_8_read_dma(void *priv)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;
    int ret;

    dsp->activity &= 0xdf;

    if (dsp->sb_8_dmanum >= 4) {
       if (dsp->dma_ff) {
           uint32_t temp = (dsp->dma_data & 0xff00) >> 8;
           temp |= (dsp->dma_data & 0xffff0000);
           ret = (int) temp;
       } else {
           dsp->dma_data = dma_channel_read(dsp->sb_8_dmanum);

           if (dsp->dma_data == DMA_NODATA)
               return DMA_NODATA;

           ret = dsp->dma_data & 0xff;
       }

       dsp->dma_ff = !dsp->dma_ff;
    } else
        ret = dma_channel_read(dsp->sb_8_dmanum);

    return ret;
}

int
sb_8_write_dma(void *priv, uint8_t val)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;

    dsp->activity &= 0xdf;

    return dma_channel_write(dsp->sb_8_dmanum, val) == DMA_NODATA;
}

/*
   Supported    High DMA    Translation    Channel
   ----------------------------------------------------
   0            0           0              First 8-bit
   0            0           1              First 8-bit
   0            1           0              Second 8-bit
   0            1           1              Second 8-bit
   1            0           0              First 8-bit
   1            0           1              First 8-bit
   1            1           0              16-bit
   1            1           1              Second 8-bit
 */
int
sb_16_read_dma(void *priv)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;

    int ret;
    int dma_ch = dsp->sb_16_dmanum;

    dsp->activity &= 0xdf;

    if (dsp->sb_16_dma_enabled && dsp->sb_16_dma_supported && !dsp->sb_16_dma_translate && (dma_ch != 4))
        ret = dma_channel_read(dma_ch);
    else {
        if (dsp->sb_16_dma_enabled) {
            /* High DMA channel enabled, either translation is enabled or
               16-bit transfers are not supported. */
            if (dsp->sb_16_dma_supported && !dsp->sb_16_dma_translate && (dma_ch == 4))
                dma_ch = dsp->sb_8_dmanum;
            else
                dma_ch = dsp->sb_16_8_dmanum;
        } else
            /* High DMA channel disabled, always use the first 8-bit channel. */
            dma_ch = dsp->sb_8_dmanum;
        int temp = dma_channel_read(dma_ch);
        ret  = temp;
        if ((temp != DMA_NODATA) && !(temp & DMA_OVER)) {
            temp = dma_channel_read(dma_ch);
            if (temp == DMA_NODATA)
                ret = DMA_NODATA;
            else {
                const int dma_flags = temp & DMA_OVER;
                temp &= ~DMA_OVER;
                ret |= (temp << 8) | dma_flags;
            }
        }
    }

    return ret;
}

int
sb_16_write_dma(void *priv, uint16_t val)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;
    int dma_ch = dsp->sb_16_dmanum;
    int ret;

    dsp->activity &= 0xdf;

    if (dsp->sb_16_dma_enabled && dsp->sb_16_dma_supported && !dsp->sb_16_dma_translate && (dma_ch != 4))
        ret = dma_channel_write(dma_ch, val) == DMA_NODATA;
    else {
        if (dsp->sb_16_dma_enabled) {
            /* High DMA channel enabled, either translation is enabled or
               16-bit transfers are not supported. */
            if (dsp->sb_16_dma_supported && !dsp->sb_16_dma_translate && (dma_ch == 4))
                dma_ch = dsp->sb_8_dmanum;
            else
                dma_ch = dsp->sb_16_8_dmanum;
        } else
            /* High DMA channel disabled, always use the first 8-bit channel. */
            dma_ch = dsp->sb_8_dmanum;
        int temp = dma_channel_write(dma_ch, val & 0xff);
        ret  = temp;
        if ((temp != DMA_NODATA) && (temp != DMA_OVER)) {
            temp = dma_channel_write(dma_ch, val >> 8);
            ret  = temp;
        }
    }

    return ret;
}

void
sb_ess_update_irq_drq_readback_regs(sb_dsp_t *dsp, bool legacy)
{
    sb_t        *ess   = (sb_t *) dsp->parent;
    ess_mixer_t *mixer = &ess->mixer_ess;
    uint8_t      t     = 0x00;

    /* IRQ control */
    if (legacy) {
        t |= 0x80;
    }
    switch (dsp->sb_irqnum) {
        default:
            break;
        case 2:
        case 9:
            t |= 0x0;
            break;
        case 5:
            t |= 0x5;
            break;
        case 7:
            t |= 0xA;
            break;
        case 10:
            t |= 0xF;
            break;
    }
    ESSreg(0xB1) = (ESSreg(0xB1) & 0xF0) | t;
    if ((mixer != NULL) && (ess->mpu != NULL) && (((mixer->regs[0x40] >> 5) & 0x7) == 2))
        mpu401_setirq(ess->mpu, ess->dsp.sb_irqnum);

    /* DRQ control */
    t = 0x00;
    if (legacy) {
        t |= 0x80;
    }
    switch (dsp->sb_8_dmanum) {
        default:
            break;
        case 0:
            t |= 0x5;
            break;
        case 1:
            t |= 0xA;
            break;
        case 3:
            t |= 0xF;
            break;
    }
    ESSreg(0xB2) = (ESSreg(0xB2) & 0xF0) | t;
}

void
sb_dsp_setirq(sb_dsp_t *dsp, int irq)
{
    sb_dsp_log("IRQ now: %i\n", irq);
    dsp->sb_irqnum = irq;

    if (IS_ESS(dsp)) {
        sb_ess_update_irq_drq_readback_regs(dsp, true);

        ESSreg(0xB1) = (ESSreg(0xB1) & 0xEF) | 0x10;
    }
}

void
sb_dsp_setdma8(sb_dsp_t *dsp, int dma)
{
    sb_dsp_log("8-bit DMA now: %i\n", dma);
    dsp->sb_8_dmanum = dma;

    if (IS_ESS(dsp))
        sb_ess_update_irq_drq_readback_regs(dsp, true);
}

void
sb_dsp_setdma16(sb_dsp_t *dsp, int dma)
{
    sb_dsp_log("16-bit DMA now: %i\n", dma);
    dsp->sb_16_dmanum = dma;
}

void
sb_dsp_setdma16_8(sb_dsp_t *dsp, int dma)
{
    sb_dsp_log("16-bit to 8-bit translation DMA now: %i\n", dma);
    dsp->sb_16_8_dmanum = dma;
}

void
sb_dsp_setdma16_enabled(sb_dsp_t *dsp, int enabled)
{
    sb_dsp_log("16-bit DMA now: %sabled\n", enabled ? "en" : "dis");
    dsp->sb_16_dma_enabled = enabled;
}

void
sb_dsp_setdma16_supported(sb_dsp_t *dsp, int supported)
{
    sb_dsp_log("16-bit DMA now: %ssupported\n", supported ? "" : "not ");
    dsp->sb_16_dma_supported = supported;
}

void
sb_dsp_setdma16_translate(sb_dsp_t *dsp, const int translate)
{
    sb_dsp_log("16-bit to 8-bit translation now: %sabled\n", translate ? "en" : "dis");
    dsp->sb_16_dma_translate = translate;
}

static void
sb_ess_update_reg_a2(sb_dsp_t *dsp, const uint8_t val)
{
    const double freq  = (7160000.0 / (256.0 - ((double) val))) * 41.0;
    const int    temp  = (int) freq;
    ESSreg(0xA2) = val;

    if (dsp->sb_freq != temp)
        recalc_sb16_filter(0, temp);
    dsp->sb_freq = temp;
}

/* TODO: Investigate ESS cards' filtering on real hardware as well.
    (DOSBox-X did it purely off some laptop's ESS chip, which isn't a good look.) */
static void
sb_ess_update_filter_freq(sb_dsp_t *dsp)
{
    const double temp = (7160000.0 / (((((double) dsp->sb_freq) / 2.0) * 0.80) * 82.0)) - 256.0;

    if (dsp->sb_freq >= 22050)
        ESSreg(0xA1) = 256 - (795500UL / dsp->sb_freq);
    else
        ESSreg(0xA1) = 128 - (397700UL / dsp->sb_freq);

    sb_ess_update_reg_a2(dsp, (uint8_t) temp);
}

static uint8_t
sb_ess_read_reg(const sb_dsp_t *dsp, const uint8_t reg)
{
    return ESSreg(reg);
}

static void
sb_ess_update_autolen(sb_dsp_t *dsp)
{
    dsp->sb_8_autolen = dsp->sb_16_autolen = (int) sb_ess_get_dma_len(dsp);
}

static void
sb_ess_write_reg(sb_dsp_t *dsp, const uint8_t reg, uint8_t data)
{
    uint8_t chg;

    switch (reg) {
        case 0xA1: /* Extended Mode Sample Rate Generator */
            {
                ESSreg(reg) = data;
                if (data & 0x80)
                    dsp->sb_freq = (int) (795500UL / (256ul - data));
                else
                    dsp->sb_freq = (int) (397700UL / (128ul - data));
                const double temp          = 1000000.0 / dsp->sb_freq;
                dsp->sblatchi = dsp->sblatcho = ((double) TIMER_USEC * temp);

                dsp->sb_timei = dsp->sb_timeo;
                break;
            }
        case 0xA2: /* Filter divider (effectively, a hardware lowpass filter under S/W control) */
            sb_ess_update_reg_a2(dsp, data);
            break;

        case 0xA4: /* DMA Transfer Count Reload (low) */
        case 0xA5: /* DMA Transfer Count Reload (high) */
            ESSreg(reg) = data;
            sb_ess_update_autolen(dsp);
            if ((dsp->sb_16_length < 0 && !dsp->sb_16_enable) && (dsp->sb_8_length < 0 && !dsp->sb_8_enable))
                dsp->ess_reload_len = 1;
            break;

        case 0xA8: /* Analog Control */
            /* bits 7:5   0                  Reserved. Always write 0
             * bit  4     1                  Reserved. Always write 1
             * bit  3     Record monitor     1=Enable record monitor
             *            enable
             * bit  2     0                  Reserved. Always write 0
             * bits 1:0   Stereo/mono select 00=Reserved
             *                               01=Stereo
             *                               10=Mono
             *                               11=Reserved */
            chg         = ESSreg(reg) ^ data;
            ESSreg(reg) = data;
            if (chg & 0x3) {
                if (dsp->sb_16_enable || dsp->sb_8_enable) {
                    uint8_t real_format = 0x00;
                    real_format |= !!(ESSreg(0xB7) & 0x20) ? 0x10 : 0;
                    real_format |= !!(ESSreg(0xB7) & 0x8) ? 0x20 : 0;

                    if (dsp->sb_16_enable)
                        dsp->sb_16_format = real_format;

                    if (dsp->sb_8_enable)
                        dsp->sb_8_format = real_format;
                }
            }
            break;

        case 0xB1:                                              /* Legacy Audio Interrupt Control */
            ESSreg(reg) = (ESSreg(reg) & 0x0F) + (data & 0xF0); // lower 4 bits not writeable
            switch (data & 0x0C) {
                default:
                    break;
                case 0x00:
                    dsp->sb_irqnum = 2;
                    break;
                case 0x04:
                    dsp->sb_irqnum = 5;
                    break;
                case 0x08:
                    dsp->sb_irqnum = 7;
                    break;
                case 0x0C:
                    dsp->sb_irqnum = 10;
                    break;
            }
            sb_ess_update_irq_drq_readback_regs(dsp, false);
            break;
        case 0xB2: /* DRQ Control */
            chg         = ESSreg(reg) ^ data;
            ESSreg(reg) = (ESSreg(reg) & 0x0F) + (data & 0xF0); // lower 4 bits not writeable
            switch (data & 0x0C) {
                default:
                    break;
                case 0x00:
                    dsp->sb_8_dmanum = -1;
                    break;
                case 0x04:
                    dsp->sb_8_dmanum = 0;
                    break;
                case 0x08:
                    dsp->sb_8_dmanum = 1;
                    break;
                case 0x0C:
                    dsp->sb_8_dmanum = 3;
                    break;
            }
            sb_ess_update_irq_drq_readback_regs(dsp, false);
            if (chg & 0x40)
                sb_ess_update_dma_status(dsp);
            break;
        case 0xB5: /* DAC Direct Access Holding (low) */
        case 0xB6: /* DAC Direct Access Holding (high) */
            ESSreg(reg) = data;
            break;

        case 0xB7: /* Audio 1 Control 1 */
            /* bit  7     Enable FIFO to/from codec
             * bit  6     Opposite from bit 3               Must be set opposite to bit 3
             * bit  5     FIFO signed mode                  1=Data is signed twos-complement   0=Data is unsigned
             * bit  4     Reserved                          Always write 1
             * bit  3     FIFO stereo mode                  1=Data is stereo
             * bit  2     FIFO 16-bit mode                  1=Data is 16-bit
             * bit  1     Reserved                          Always write 0
             * bit  0     Generate load signal */
            chg         = ESSreg(reg) ^ data;
            ESSreg(reg) = data;

            if (chg & 4)
                sb_ess_update_autolen(dsp);

            if (chg & 0x0C) {
                if (dsp->sb_16_enable || dsp->sb_8_enable) {
                    sb_stop_dma_ess(dsp);
                    sb_start_dma_ess(dsp);
                }
            }
            break;

        case 0xB8: /* Audio 1 Control 2 */
            /* bits 7:4   reserved
             * bit  3     CODEC mode         1=first DMA converter in ADC mode
             *                               0=first DMA converter in DAC mode
             * bit  2     DMA mode           1=auto-initialize mode
             *                               0=normal DMA mode
             * bit  1     DMA read enable    1=first DMA is read (for ADC)
             *                               0=first DMA is write (for DAC)
             * bit  0     DMA xfer enable    1=DMA is allowed to proceed */
            data &= 0xF;
            chg         = ESSreg(reg) ^ data;
            ESSreg(reg) = data;

            if (chg & 1) {
                if (dsp->sb_16_enable || dsp->sb_8_enable) {
                    if (dsp->sb_16_enable)
                        dsp->sb_16_length = (int) sb_ess_get_dma_len(dsp);
                    if (dsp->sb_8_enable)
                        dsp->sb_8_length = (int) sb_ess_get_dma_len(dsp);
                } else
                    dsp->ess_reload_len = 1;
            }

            if (chg & 0x4) {
                if (dsp->sb_16_enable) {
                    dsp->sb_16_autoinit = (ESSreg(0xB8) & 0x4) != 0;
                }
                if (dsp->sb_8_enable) {
                    dsp->sb_8_autoinit = (ESSreg(0xB8) & 0x4) != 0;
                }
            }

            if (chg & 0xB) {
                if (chg & 0xA)
                    sb_stop_dma_ess(dsp); /* changing capture/playback direction? stop DMA to reinit */
                sb_ess_update_dma_status(dsp);
            }
            break;

        case 0xB9: /* Audio 1 Transfer Type */
        case 0xBA: /* Left Channel ADC Offset Adjust */
        case 0xBB: /* Right Channel ADC Offset Adjust */
        case 0xC3: /* Internal state register */
        case 0xCF: /* GPO0/1 power management register */
            ESSreg(reg) = data;
            break;

        default:
            sb_dsp_log("UNKNOWN ESS register write reg=%02xh val=%02xh\n", reg, data);
            break;
    }
}

void
sb_exec_command(sb_dsp_t *dsp)
{
    int temp;
    int c;

    sb_dsp_log("sb_exec_command : SB command %02X\n", dsp->sb_command);

    /* Update 8051 ram with the current DSP command.
       See https://github.com/joncampbell123/dosbox-x/issues/1044 */
    if (dsp->sb_type >= SB16_DSP_404) {
        dsp->sb_8051_ram[0x20] = dsp->sb_command;
    }

    if (IS_ESS(dsp) && dsp->sb_command >= 0xA0 && dsp->sb_command <= 0xCF) {
        if (dsp->sb_command == 0xC6 || dsp->sb_command == 0xC7) {
            dsp->ess_extended_mode = !!(dsp->sb_command == 0xC6);
            return;
        } else if (dsp->sb_command == 0xC2) {
            sb_ess_write_reg(dsp, 0xC3, dsp->sb_data[0]);
        } else if (dsp->sb_command == 0xC3) {
            sb_add_data(dsp, sb_ess_read_reg(dsp, 0xC3));
        } else if (dsp->sb_command == 0xCE) {
            sb_add_data(dsp, sb_ess_read_reg(dsp, 0xCF));
        } else if (dsp->sb_command == 0xCF) {
            sb_ess_write_reg(dsp, 0xCF, dsp->sb_data[0]);
        } else if (dsp->sb_command == 0xC0) {
            sb_add_data(dsp, sb_ess_read_reg(dsp, dsp->sb_data[0]));
        } else if (dsp->sb_command < 0xC0 && dsp->ess_extended_mode) {
            sb_ess_write_reg(dsp, dsp->sb_command, dsp->sb_data[0]);
        }
        return;
    }

    switch (dsp->sb_command) {
        case 0x01: /* ???? */
            if (dsp->sb_type >= SB16_DSP_404)
                dsp->asp_data_len = dsp->sb_data[0] + (dsp->sb_data[1] << 8) + 1;
            break;
        case 0x03: /* ASP status */
            if (dsp->sb_type >= SB16_DSP_404)
                sb_add_data(dsp, 0);
            break;
        case 0x04: /* ASP set mode register */
            if (dsp->sb_type >= SB16_DSP_404) {
                dsp->sb_asp_mode = dsp->sb_data[0];
                if (dsp->sb_asp_mode & 4)
                    dsp->sb_asp_ram_index = 0;
                sb_dsp_log("SB16 ASP set mode %02X\n", dsp->sb_asp_mode);
            } /* else DSP Status (Obsolete) */
            break;
        case 0x05: /* ASP set codec parameter */
            if (dsp->sb_type >= SB16_DSP_404) {
                sb_dsp_log("SB16 ASP unknown codec params %02X, %02X\n", dsp->sb_data[0], dsp->sb_data[1]);
            }
            break;
        case 0x07:
            break;
        case 0x08: /* ASP get version / AZTECH type/EEPROM access */
            if (IS_AZTECH(dsp)) {
                if ((dsp->sb_data[0] == 0x05 || dsp->sb_data[0] == 0x55) && dsp->sb_subtype == SB_SUBTYPE_CLONE_AZT2316A_0X11)
                    sb_add_data(dsp, 0x11); /* AZTECH get type, WASHINGTON/latest - according to devkit. E.g.: The one in the Itautec Infoway Multimidia */
                else if ((dsp->sb_data[0] == 0x05 || dsp->sb_data[0] == 0x55) && dsp->sb_subtype == SB_SUBTYPE_CLONE_AZT1605_0X0C)
                    sb_add_data(dsp, 0x0C); /* AZTECH get type, CLINTON - according to devkit. E.g.: The one in the Packard Bell Legend 100CD */
                else if (dsp->sb_data[0] == 0x08) {
                    /* EEPROM address to write followed by byte */
                    if (dsp->sb_data[1] < 0 || dsp->sb_data[1] >= AZTECH_EEPROM_SIZE)
                        fatal("AZT EEPROM: out of bounds write to %02X\n", dsp->sb_data[1]);
                    sb_dsp_log("EEPROM write = %02x\n", dsp->sb_data[2]);
                    dsp->azt_eeprom[dsp->sb_data[1]] = dsp->sb_data[2];
                    break;
                } else if (dsp->sb_data[0] == 0x07) {
                    /* EEPROM address to read */
                    if (dsp->sb_data[1] < 0 || dsp->sb_data[1] >= AZTECH_EEPROM_SIZE)
                        fatal("AZT EEPROM: out of bounds read to %02X\n", dsp->sb_data[1]);
                    sb_dsp_log("EEPROM read = %02x\n", dsp->azt_eeprom[dsp->sb_data[1]]);
                    sb_add_data(dsp, dsp->azt_eeprom[dsp->sb_data[1]]);
                    break;
                } else
                    sb_dsp_log("AZT2316A: UNKNOWN 0x08 COMMAND: %02X\n", dsp->sb_data[0]); /* 0x08 (when shutting down, driver tries to read 1 byte of response), 0x55, 0x0D, 0x08D seen */
                break;
            }
            if (dsp->sb_type == SBAWE64_DSP_416) /* AWE64 has no ASP or a socket for it */
                sb_add_data(dsp, 0xFF);
            else if (dsp->sb_type >= SB16_DSP_404)
                sb_add_data(dsp, 0x18);
            break;
        case 0x09: /* AZTECH mode set */
            if (IS_AZTECH(dsp)) {
                if (dsp->sb_data[0] == 0x00) {
                    sb_dsp_log("AZT2316A: WSS MODE!\n");
                    azt2316a_enable_wss(1, dsp->parent);
                } else if (dsp->sb_data[0] == 0x01) {
                    sb_dsp_log("AZT2316A: SB8PROV2 MODE!\n");
                    azt2316a_enable_wss(0, dsp->parent);
                } else
                    sb_dsp_log("AZT2316A: UNKNOWN MODE! = %02x\n", dsp->sb_data[0]); // sequences 0x02->0xFF, 0x04->0xFF seen
            }
            break;
        case 0x0E: /* ASP set register */
            if (dsp->sb_type >= SB16_DSP_404) {
                dsp->sb_asp_regs[dsp->sb_data[0]] = dsp->sb_data[1];

                if ((dsp->sb_data[0] == 0x83) && (dsp->sb_asp_mode & 128) && (dsp->sb_asp_mode & 8)) { /* ASP memory write */
                    if (dsp->sb_asp_mode & 8)
                        dsp->sb_asp_ram_index = 0;

                    dsp->sb_asp_ram[dsp->sb_asp_ram_index] = dsp->sb_data[1];

                    if (dsp->sb_asp_mode & 2) {
                        dsp->sb_asp_ram_index++;
                        if (dsp->sb_asp_ram_index >= 2048)
                            dsp->sb_asp_ram_index = 0;
                    }
                }
                sb_dsp_log("SB16 ASP write reg %02X, val %02X\n", dsp->sb_data[0], dsp->sb_data[1]);
            }
            break;
        case 0x0F: /* ASP get register */
            if (dsp->sb_type >= SB16_DSP_404) {
                if ((dsp->sb_data[0] == 0x83) && (dsp->sb_asp_mode & 128) && (dsp->sb_asp_mode & 8)) { /* ASP memory read */
                    if (dsp->sb_asp_mode & 8)
                        dsp->sb_asp_ram_index = 0;

                    dsp->sb_asp_regs[0x83] = dsp->sb_asp_ram[dsp->sb_asp_ram_index];

                    if (dsp->sb_asp_mode & 1) {
                        dsp->sb_asp_ram_index++;
                        if (dsp->sb_asp_ram_index >= 2048)
                            dsp->sb_asp_ram_index = 0;
                    }
                } else if (dsp->sb_data[0] == 0x83) {
                    dsp->sb_asp_regs[0x83] = 0x18;
                }
                sb_add_data(dsp, dsp->sb_asp_regs[dsp->sb_data[0]]);
                sb_dsp_log("SB16 ASP read reg %02X, val %02X\n", dsp->sb_data[0], dsp->sb_asp_regs[dsp->sb_data[0]]);
            }
            break;
        case 0x10: /* 8-bit direct mode */
            sb_dsp_update(dsp);
            dsp->sbdat = dsp->sbdatl = dsp->sbdatr = (int16_t) ((dsp->sb_data[0] ^ 0x80) << 8);
            // FIXME: What does the ESS AudioDrive do to its filter/sample rate divider registers when emulating this Sound Blaster command?
            ESSreg(0xA1) = 128 - (397700 / 22050);
            ESSreg(0xA2) = 256 - (7160000 / (82 * ((4 * 22050) / 10)));
            break;
        case 0x14: /* 8-bit single cycle DMA output */
            sb_start_dma(dsp, 1, 0, 0, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
            break;
        case 0x17: /* 2-bit ADPCM output with reference */
            dsp->sbref  = dsp->dma_readb(dsp->dma_priv);
            dsp->sbstep = 0;
            fallthrough;
        case 0x16: /* 2-bit ADPCM output */
            sb_start_dma(dsp, 1, 0, ADPCM_2, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
            dsp->sbdat2 = dsp->dma_readb(dsp->dma_priv);
            dsp->sb_8_length--;
            dsp->ess_dma_counter++;
            if (dsp->sb_command == 0x17) {
                dsp->sb_8_length--;
                dsp->ess_dma_counter++;
            }
            break;
        case 0x1C: /* 8-bit autoinit DMA output */
            if (dsp->sb_type >= SB_DSP_200)
                sb_start_dma(dsp, 1, 1, 0, dsp->sb_8_autolen);
            break;
        case 0x1F: /* 2-bit ADPCM autoinit output */
            if (dsp->sb_type >= SB_DSP_200) {
                sb_start_dma(dsp, 1, 1, ADPCM_2, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
                dsp->sbdat2 = dsp->dma_readb(dsp->dma_priv);
                dsp->sb_8_length--;
                dsp->ess_dma_counter++;
            }
            break;
        case 0x20: /* 8-bit direct input */
            sb_add_data(dsp, (dsp->record_buffer[dsp->record_pos_read] >> 8) ^ 0x80);
            /* Due to the current implementation, I need to emulate a samplerate, even if this
               mode does not imply such samplerate. Position is increased in sb_poll_i(). */
            if (!timer_is_enabled(&dsp->input_timer)) {
                dsp->sb_timei = 256 - 22;
                dsp->sblatchi = (double) ((double) TIMER_USEC * 22.0);
                temp          = 1000000 / 22;
                dsp->sb_freq  = temp;
                timer_set_delay_u64(&dsp->input_timer, (uint64_t) dsp->sblatchi);
            }
            break;
        case 0x24: /* 8-bit single cycle DMA input */
            sb_start_dma_i(dsp, 1, 0, 0, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
            break;
        case 0x28: /* Direct ADC, 8-bit (Burst) */
            break;
        case 0x2C: /* 8-bit autoinit DMA input */
            if (dsp->sb_type >= SB_DSP_200)
                sb_start_dma_i(dsp, 1, 1, 0, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
            break;
        case 0x30: /* MIDI Polling mode input */
            sb_dsp_log("MIDI polling mode input\n");
            dsp->midi_in_poll = 1;
            dsp->uart_irq     = 0;
            break;
        case 0x31: /* MIDI Interrupt mode input */
            sb_dsp_log("MIDI interrupt mode input\n");
            dsp->midi_in_poll = 0;
            dsp->uart_irq     = 1;
            break;
        case 0x32: /* MIDI Read Timestamp Poll */
        case 0x33: /* MIDI Read Timestamp Interrupt */
            break;
        case 0x34: /* MIDI In poll  */
            if (dsp->sb_type < SB_DSP_200)
                break;
            sb_dsp_log("MIDI poll in\n");
            dsp->midi_in_poll = 1;
            dsp->uart_midi    = 1;
            dsp->uart_irq     = 0;
            break;
        case 0x35: /* MIDI In irq */
            if (dsp->sb_type < SB_DSP_200)
                break;
            sb_dsp_log("MIDI irq in\n");
            dsp->midi_in_poll = 0;
            dsp->uart_midi    = 1;
            dsp->uart_irq     = 1;
            break;
        case 0x36:
        case 0x37: /* MIDI timestamps */
            break;
        case 0x38: /* Write to SB MIDI Output (Raw) */
            dsp->onebyte_midi = 1;
            break;
        case 0x40: /* Set time constant */
            dsp->sb_timei = dsp->sb_timeo = dsp->sb_data[0];
            dsp->sblatcho = dsp->sblatchi = (double) (TIMER_USEC * (256 - dsp->sb_data[0]));
            temp                          = 256 - dsp->sb_data[0];
            temp                          = 1000000 / temp;
            sb_dsp_log("Sample rate - %ihz (%f)\n", temp, dsp->sblatcho);
            if ((dsp->sb_freq != temp) && (dsp->sb_type >= SB16_DSP_404))
                recalc_sb16_filter(0, temp);
            dsp->sb_freq = temp;
            if (IS_ESS(dsp)) {
                sb_ess_update_filter_freq(dsp);
            }
            break;
        case 0x41: /* Set output sampling rate */
        case 0x42: /* Set input sampling rate */
            if (dsp->sb_type >= SB16_DSP_404) {
                dsp->sblatcho = (double) ((double) TIMER_USEC * (1000000.0 / (double) (dsp->sb_data[1] + (dsp->sb_data[0] << 8))));
                sb_dsp_log("Sample rate - %ihz (%f)\n", dsp->sb_data[1] + (dsp->sb_data[0] << 8), dsp->sblatcho);
                temp          = dsp->sb_freq;
                dsp->sb_freq  = dsp->sb_data[1] + (dsp->sb_data[0] << 8);
                dsp->sb_timeo = 256 + dsp->sb_freq;
                dsp->sblatchi = dsp->sblatcho;
                dsp->sb_timei = dsp->sb_timeo;
                if (dsp->sb_freq != temp)
                    recalc_sb16_filter(0, dsp->sb_freq);
                dsp->sb_8051_ram[0x13] = dsp->sb_freq & 0xff;
                dsp->sb_8051_ram[0x14] = (dsp->sb_freq >> 8) & 0xff;
            }
            break;
        case 0x45: /* Continue Auto-Initialize DMA, 8-bit */
        case 0x47: /* Continue Auto-Initialize DMA, 16-bit */
            break;
        case 0x48: /* Set DSP block transfer size */
            if (dsp->sb_type >= SB_DSP_200)
                dsp->sb_8_autolen = dsp->sb_data[0] + (dsp->sb_data[1] << 8);
            break;
        case 0x65: /* 4-bit ESPCM output with reference */
        case 0x64: /* 4-bit ESPCM output */
            if (IS_ESS(dsp)) {
                if (dsp->espcm_mode != ESPCM_4 || (dsp->sb_8_enable && dsp->sb_8_pause)) {
                    fifo_reset(dsp->espcm_fifo);
                    dsp->espcm_sample_idx = 0;
                }
                dsp->espcm_mode = ESPCM_4;
                sb_start_dma(dsp, 1, 0, ESPCM_4, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
            }
            break;
        case 0x67: /* 3-bit ESPCM output with reference */
        case 0x66: /* 3-bit ESPCM output */
            if (IS_ESS(dsp)) {
                if (dsp->espcm_mode != ESPCM_3 || (dsp->sb_8_enable && dsp->sb_8_pause)) {
                    fifo_reset(dsp->espcm_fifo);
                    dsp->espcm_sample_idx = 0;
                }
                dsp->espcm_mode = ESPCM_3;
                sb_start_dma(dsp, 1, 0, ESPCM_3, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
            }
            break;
        case 0x6D: /* 1-bit ESPCM output with reference */
        case 0x6C: /* 1-bit ESPCM output */
            if (IS_ESS(dsp)) {
                if (dsp->espcm_mode != ESPCM_1 || (dsp->sb_8_enable && dsp->sb_8_pause)) {
                    fifo_reset(dsp->espcm_fifo);
                    dsp->espcm_sample_idx = 0;
                }
                dsp->espcm_mode = ESPCM_1;
                sb_start_dma(dsp, 1, 0, ESPCM_1, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
            }
            break;
        case 0x6F: /* 4-bit ESPCM input with reference */
        case 0x6E: /* 4-bit ESPCM input */
            if (IS_ESS(dsp)) {
                if (dsp->espcm_mode != ESPCM_4E || (dsp->sb_8_enable && dsp->sb_8_pause)) {
                    fifo_reset(dsp->espcm_fifo);
                    dsp->espcm_sample_idx = 0;
                }
                dsp->espcm_mode = ESPCM_4E;
                sb_start_dma_i(dsp, 1, 0, ESPCM_4E, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
            }
            break;
        case 0x75: /* 4-bit ADPCM output with reference */
            dsp->sbref  = dsp->dma_readb(dsp->dma_priv);
            dsp->sbstep = 0;
            fallthrough;
        case 0x74: /* 4-bit ADPCM output */
            sb_start_dma(dsp, 1, 0, ADPCM_4, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
            dsp->sbdat2 = dsp->dma_readb(dsp->dma_priv);
            dsp->sb_8_length--;
            dsp->ess_dma_counter++;
            if (dsp->sb_command == 0x75) {
                dsp->sb_8_length--;
                dsp->ess_dma_counter++;
            }
            break;
        case 0x77: /* 2.6-bit ADPCM output with reference */
            dsp->sbref  = dsp->dma_readb(dsp->dma_priv);
            dsp->sbstep = 0;
            fallthrough;
        case 0x76: /* 2.6-bit ADPCM output */
            sb_start_dma(dsp, 1, 0, ADPCM_26, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
            dsp->sbdat2 = dsp->dma_readb(dsp->dma_priv);
            dsp->sb_8_length--;
            dsp->ess_dma_counter++;
            if (dsp->sb_command == 0x77) {
                dsp->sb_8_length--;
                dsp->ess_dma_counter++;
            }
            break;
        case 0x7D: /* 4-bit ADPCM autoinit output */
            if (dsp->sb_type >= SB_DSP_200) {
                sb_start_dma(dsp, 1, 1, ADPCM_4, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
                dsp->sbdat2 = dsp->dma_readb(dsp->dma_priv);
                dsp->sb_8_length--;
                dsp->ess_dma_counter++;
            }
            break;
        case 0x7F: /* 2.6-bit ADPCM autoinit output */
            if (dsp->sb_type >= SB_DSP_200) {
                sb_start_dma(dsp, 1, 1, ADPCM_26, dsp->sb_data[0] + (dsp->sb_data[1] << 8));
                dsp->sbdat2 = dsp->dma_readb(dsp->dma_priv);
                dsp->sb_8_length--;
                dsp->ess_dma_counter++;
            }
            break;
        case 0x80: /* Pause DAC */
            dsp->sb_pausetime = dsp->sb_data[0] + (dsp->sb_data[1] << 8);
            if (!timer_is_enabled(&dsp->output_timer))
                timer_set_delay_u64(&dsp->output_timer, (uint64_t) trunc(dsp->sblatcho));
            break;
        case 0x90: /* High speed 8-bit autoinit DMA output */
            if ((dsp->sb_type >= SB_DSP_201) && (dsp->sb_type < SB16_DSP_404)) // TODO docs need validated
                sb_start_dma(dsp, 1, 1, 0, dsp->sb_8_autolen);
            break;
        case 0x91: /* High speed 8-bit single cycle DMA output */
            if ((dsp->sb_type >= SB_DSP_201) && (dsp->sb_type < SB16_DSP_404)) // TODO docs need validated
                sb_start_dma(dsp, 1, 0, 0, dsp->sb_8_autolen);
            break;
        case 0x98: /* High speed 8-bit autoinit DMA input */
            if ((dsp->sb_type >= SB_DSP_201) && (dsp->sb_type < SB16_DSP_404)) // TODO docs need validated
                sb_start_dma_i(dsp, 1, 1, 0, dsp->sb_8_autolen);
            break;
        case 0x99: /* High speed 8-bit single cycle DMA input */
            if ((dsp->sb_type >= SB_DSP_201) && (dsp->sb_type < SB16_DSP_404)) // TODO docs need validated
                sb_start_dma_i(dsp, 1, 0, 0, dsp->sb_8_autolen);
            break;
        case 0xA0: /* Set input mode to mono */
        case 0xA8: /* Set input mode to stereo */
            if ((dsp->sb_type < SBPRO_DSP_300) || (dsp->sb_type > SBPRO2_DSP_302))
                break;
            /* TODO: Implement. 3.xx-only command. */
            break;
        case 0xB0:
        case 0xB1:
        case 0xB2:
        case 0xB3:
        case 0xB4:
        case 0xB5:
        case 0xB6:
        case 0xB7: /* 16-bit DMA output */
            if (dsp->sb_type >= SB16_DSP_404) {
                sb_start_dma(dsp, 0, dsp->sb_command & 4, dsp->sb_data[0],
                             dsp->sb_data[1] + (dsp->sb_data[2] << 8));
                dsp->sb_16_autolen = dsp->sb_data[1] + (dsp->sb_data[2] << 8);
            }
            break;
        case 0xB8:
        case 0xB9:
        case 0xBA:
        case 0xBB:
        case 0xBC:
        case 0xBD:
        case 0xBE:
        case 0xBF: /* 16-bit DMA input */
            if (dsp->sb_type >= SB16_DSP_404) {
                sb_start_dma_i(dsp, 0, dsp->sb_command & 4, dsp->sb_data[0],
                               dsp->sb_data[1] + (dsp->sb_data[2] << 8));
                dsp->sb_16_autolen = dsp->sb_data[1] + (dsp->sb_data[2] << 8);
            }
            break;
        case 0xC0:
        case 0xC1:
        case 0xC2:
        case 0xC3:
        case 0xC4:
        case 0xC5:
        case 0xC6:
        case 0xC7: /* 8-bit DMA output */
            if (dsp->sb_type >= SB16_DSP_404) {
                sb_start_dma(dsp, 1, dsp->sb_command & 4, dsp->sb_data[0],
                             dsp->sb_data[1] + (dsp->sb_data[2] << 8));
                dsp->sb_8_autolen = dsp->sb_data[1] + (dsp->sb_data[2] << 8);
            }
            break;
        case 0xC8:
        case 0xC9:
        case 0xCA:
        case 0xCB:
        case 0xCC:
        case 0xCD:
        case 0xCE:
        case 0xCF: /* 8-bit DMA input */
            if (dsp->sb_type >= SB16_DSP_404) {
                sb_start_dma_i(dsp, 1, dsp->sb_command & 4, dsp->sb_data[0],
                               dsp->sb_data[1] + (dsp->sb_data[2] << 8));
                dsp->sb_8_autolen = dsp->sb_data[1] + (dsp->sb_data[2] << 8);
            }
            break;
        case 0xD0: /* Pause 8-bit DMA */
            dsp->sb_8_pause = 1;
            sb_stop_dma(dsp);
            break;
        case 0xD1: /* Speaker on */
            if (IS_NOT_ESS(dsp)) {
                if (dsp->sb_type < SB_DSP_200) {
                    dsp->sb_8_pause = 1;
                    sb_stop_dma(dsp);
                } else if (dsp->sb_type < SB16_DSP_404)
                    dsp->muted = 0;
            }
            dsp->sb_speaker = 1;
            break;
        case 0xD3: /* Speaker off */
            if (IS_NOT_ESS(dsp)) {
                if (dsp->sb_type < SB_DSP_201) {
                    dsp->sb_8_pause = 1;
                    sb_stop_dma(dsp);
                } else if (dsp->sb_type < SB16_DSP_404)
                    dsp->muted = 1;
            }
            dsp->sb_speaker = 0;
            break;
        case 0xD4: /* Continue 8-bit DMA */
            dsp->sb_8_pause = 0;
            sb_resume_dma(dsp, 1);
            break;
        case 0xD5: /* Pause 16-bit DMA */
            if (dsp->sb_type >= SB16_DSP_404) {
                dsp->sb_16_pause = 1;
                sb_stop_dma(dsp);
            }
            break;
        case 0xD6: /* Continue 16-bit DMA */
            if (dsp->sb_type >= SB16_DSP_404) {
                dsp->sb_16_pause = 0;
                sb_resume_dma(dsp, 1);
            }
            break;
        case 0xD8: /* Get speaker status */
            if (dsp->sb_type >= SB_DSP_200)
                sb_add_data(dsp, dsp->sb_speaker ? 0xff : 0);
            break;
        case 0xD9: /* Exit 16-bit auto-init mode */
            if (dsp->sb_type >= SB16_DSP_404)
                dsp->sb_16_autoinit = 0;
            break;
        case 0xDA: /* Exit 8-bit auto-init mode */
            if (dsp->sb_type >= SB_DSP_200)
                dsp->sb_8_autoinit = 0;
            break;
        case 0xE0: /* DSP identification */
            sb_add_data(dsp, ~dsp->sb_data[0]);
            break;
        case 0xE1: /* Get DSP version */
            if (IS_ESS(dsp)) {
                /*
                   0x03 0x01 (Sound Blaster Pro compatibility) confirmed by both the
                   ES1888 datasheet and the probing of the real ES688 and ES1688 cards.
                 */
                sb_add_data(dsp, 0x3);
                sb_add_data(dsp, 0x1);
                break;
            }
            if (IS_AZTECH(dsp)) {
                if (dsp->sb_subtype == SB_SUBTYPE_CLONE_AZT2316A_0X11) {
                    sb_add_data(dsp, 0x3);
                    sb_add_data(dsp, 0x1);
                } else if (dsp->sb_subtype == SB_SUBTYPE_CLONE_AZT1605_0X0C) {
                    sb_add_data(dsp, 0x2);
                    sb_add_data(dsp, 0x1);
                }
                break;
            }
            sb_add_data(dsp, sb_dsp_versions[dsp->sb_type] >> 8);
            sb_add_data(dsp, sb_dsp_versions[dsp->sb_type] & 0xff);
            break;
        case 0xE2: /* Stupid ID/protection */
            for (c = 0; c < 8; c++) {
                if (dsp->sb_data[0] & (1 << c))
                    dsp->sbe2 += sbe2dat[dsp->sbe2count & 3][c];
            }
            dsp->sbe2 += sbe2dat[dsp->sbe2count & 3][8];
            dsp->sbe2count++;
            if (dsp->dma_writeb(dsp->dma_priv, dsp->sbe2))
                /* Undocumented behavior: If write to DMA fails, the byte is written
                   to the CPU instead. The NT 3.1 Sound Blaster Pro driver relies on this. */
                sb_add_data(dsp, dsp->sbe2);
            break;
        case 0xE3: /* DSP copyright */
            if (dsp->sb_type >= SB16_DSP_404) {
                c = 0;
                while (sb16_copyright[c])
                    sb_add_data(dsp, sb16_copyright[c++]);
                sb_add_data(dsp, 0);
            } /* else if (IS_ESS(dsp))
                sb_add_data(dsp, 0); */
            /*
               TODO: What ESS card returns 0x00 here? Probing of the real ES688 and ES1688 cards
                     revealed that they in fact return nothing on this command.
             */
            break;
        case 0xE4: /* Write test register */
            dsp->sb_test = dsp->sb_data[0];
            break;
        case 0xE7: /* ESS detect/read config on ESS cards */
            if (IS_ESS(dsp)) {
                switch (dsp->sb_subtype) {
                    default:
                        break;
                    case SB_SUBTYPE_ESS_ES688:
                        sb_add_data(dsp, 0x68);
                        /*
                           80h:     ESSCFG fails to detect the AudioDrive;
                           81h-83h: ES??88, Windows 3.1 driver expects MPU-401 and gives a legacy mixer error;
                           84h:     ES688, Windows 3.1 driver expects MPU-401, returned by DOSBox-X;
                           85h-87h: ES688, Windows 3.1 driver does not expect MPU-401:
                                    85h: Returned by MSDOS622's real ESS688,
                                    86h: Returned by Dizzy's real ES688.
                           We return 86h if MPU is absent, 84h otherwise, who knows what the actual
                           PnP ES688 returns here.
                         */
                        sb_add_data(dsp, 0x80 | ((dsp->mpu != NULL) ? 0x04 : 0x06));
                        break;
                    case SB_SUBTYPE_ESS_ES1688:
                        sb_add_data(dsp, 0x68);
                        /*
                           89h:     ES1688, returned by DOSBox-X, determined via Windows driver
                                    debugging;
                           8Bh:     ES1688, returned by both MSDOS622's and Dizzy's real ES1688's.
                         */
                        sb_add_data(dsp, 0x80 | 0x0b);
                        break;
                }
            }
            break;
        case 0xE8: /* Read test register */
            sb_add_data(dsp, dsp->sb_test);
            break;
        case 0xF2: /* Trigger 8-bit IRQ */
            sb_dsp_log("Trigger 8-bit IRQ\n");
            timer_set_delay_u64(&dsp->irq_timer, (10ULL * TIMER_USEC));
            break;
        case 0xF3: /* Trigger 16-bit IRQ */
            sb_dsp_log("Trigger 16-bit IRQ\n");
            if (IS_ESS(dsp))
                dsp->ess_irq_generic = true;
            else
                timer_set_delay_u64(&dsp->irq16_timer, (10ULL * TIMER_USEC));
            break;
        case 0xF8:
            if (dsp->sb_type < SB16_DSP_404)
                sb_add_data(dsp, 0);
            break;
        case 0xF9: /* SB16 8051 RAM read */
            if (dsp->sb_type >= SB16_DSP_404)
                sb_add_data(dsp, dsp->sb_8051_ram[dsp->sb_data[0]]);
            break;
        case 0xFA: /* SB16 8051 RAM write */
            if (dsp->sb_type >= SB16_DSP_404)
                dsp->sb_8051_ram[dsp->sb_data[0]] = dsp->sb_data[1];
            break;
        case 0xFF: /* No, that's not how you program auto-init DMA */
            break;

            /* TODO: Some more data about the DSP registeres
             * http://the.earth.li/~tfm/oldpage/sb_dsp.html
             * http://www.synchrondata.com/pheaven/www/area19.htm
             * http://www.dcee.net/Files/Programm/Sound/
             * https://github.com/schlae/sb-firmware/blob/master/sbv202.asm
             *  008h           Halt (Infinate Loop)                                SB2???
             *  018h           DMA playback with auto init DMA.                    SB2???
             *  028h           Auto-init direct ADC                                SB2???
             *  036h           (Timestamp)                                         SB???
             *  037h           (Timestamp)                                         SB???
             *  050h           Stops playback of SRAM samples                      SB???
             *  051h           Plays back samples stored in SRAM.                  SB???
             *  058h           Load data into SRAM                                 SB???
             *  059h           Fetches the samples and then immediately plays them back.    SB???
             *  078h           Auto-init DMA ADPCM                                 SB2???
             *  07Ah           2.6-bit ADPCM                                       SB???
             *  0E3h           DSP Copyright                                       SBPro2??? (SBPRO2_DSP_302)
             *  0F0h           Sine Generator                                      SB        (SB_DSP_105, DSP20x)
             *  0F1h           DSP Auxiliary Status (Obsolete)                     SB-Pro2   (DSP20x, SBPRO2_DSP_302)
             *  0F2h           IRQ Request, 8-bit                                  SB        (SB_DSP_105, DSP20x)
             *  0F3h           IRQ Request, 16-bit                                 SB16
             *  0F4h           Perform ROM checksum                                SB        (SB_DSP_105, DSP20x)
             *  0FBh           DSP Status                                          SB16
             *  0FCh           DSP Auxiliary Status                                SB16
             *  0FDh           DSP Command Status                                  SB16
             */

        default:
            sb_dsp_log("Unknown DSP command: %02X\n", dsp->sb_command);
            break;
    }

    /* Update 8051 ram with the last DSP command.
       See https://github.com/joncampbell123/dosbox-x/issues/1044 */
    if (dsp->sb_type >= SB16_DSP_404)
        dsp->sb_8051_ram[0x30] = dsp->sb_command;
}

static void
sb_do_reset(sb_dsp_t *dsp, const uint8_t v)
{
    if (((v & 1) != 0) && (dsp->state != DSP_S_RESET)) {
        sb_dsp_reset(dsp);
        dsp->sb_read_rp = dsp->sb_read_wp = 0;
        dsp->state = DSP_S_RESET;
    } else if (((v & 1) == 0) && (dsp->state == DSP_S_RESET)) {
        dsp->state = DSP_S_RESET_WAIT;
        dsp->sb_read_rp = dsp->sb_read_wp = 0;
        sb_add_data(dsp, 0xaa);
    }
}

void
sb_write(uint16_t addr, uint8_t val, void *priv)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;

    sb_dsp_log("[%04X:%08X] DSP: [W] %04X = %02X\n", CS, cpu_state.pc, addr, val);

    /* Sound Blasters prior to Sound Blaster 16 alias the I/O ports. */
    if ((dsp->sb_type < SB16_DSP_404) && (IS_NOT_ESS(dsp) || ((addr & 0xF) != 0xE)))
        addr &= 0xfffe;

    switch (addr & 0xF) {
        case 6: /* Reset */
            sb_do_reset(dsp, val);

            if (!(val & 2) && (dsp->espcm_fifo_reset & 2)) {
                fifo_reset(dsp->espcm_fifo);
            }
            dsp->espcm_fifo_reset = val;
            dsp->uart_midi        = 0;
            dsp->uart_irq         = 0;
            dsp->onebyte_midi     = 0;
            return;
        case 0xC: /* Command/data write */
            if (dsp->uart_midi || dsp->onebyte_midi) {
                midi_raw_out_byte(val);
                dsp->onebyte_midi = 0;
                return;
            }
            timer_set_delay_u64(&dsp->wb_timer, TIMER_USEC * 1);
            if (dsp->asp_data_len) {
                sb_dsp_log("ASP data %i\n", dsp->asp_data_len);
                dsp->asp_data_len--;
                if (!dsp->asp_data_len)
                    sb_add_data(dsp, 0);
                return;
            }
            if (dsp->sb_data_stat == -1) {
                dsp->sb_command = val;
                if (val == 0x01)
                    sb_add_data(dsp, 0);
                dsp->sb_data_stat++;
                if (IS_AZTECH(dsp)) {
                    /* variable length commands */
                    if (dsp->sb_command == 0x08 && dsp->sb_data_stat == 1 && dsp->sb_data[0] == 0x08)
                        sb_commands[dsp->sb_command] = 3;
                    else if (dsp->sb_command == 0x08 && dsp->sb_data_stat == 1 && dsp->sb_data[0] == 0x07)
                        sb_commands[dsp->sb_command] = 2;
                }
                if (IS_ESS(dsp) && dsp->sb_command >= 0x64 && dsp->sb_command <= 0x6F) {
                    sb_commands[dsp->sb_command] = 2;
                } else if (IS_ESS(dsp) && dsp->sb_command >= 0xA0 && dsp->sb_command <= 0xCF) {
                    if (dsp->sb_command <= 0xC0
                        || dsp->sb_command == 0xC2
                        || dsp->sb_command == 0xCF) {
                        sb_commands[dsp->sb_command] = 1;
                    } else if (dsp->sb_command == 0xC3
                               || dsp->sb_command == 0xC6
                               || dsp->sb_command == 0xC7
                               || dsp->sb_command == 0xCE) {
                        sb_commands[dsp->sb_command] = 0;
                    } else {
                        sb_commands[dsp->sb_command] = -1;
                    }
                }
            } else {
                dsp->sb_data[dsp->sb_data_stat++] = val;
            }
            if (dsp->sb_data_stat == sb_commands[dsp->sb_command] || sb_commands[dsp->sb_command] == -1) {
                sb_exec_command(dsp);
                dsp->sb_data_stat = -1;
                if (IS_AZTECH(dsp)) {
                    /* variable length commands */
                    if (dsp->sb_command == 0x08)
                        sb_commands[dsp->sb_command] = 1;
                }
            }
            break;

        default:
            break;
    }
}

uint8_t
sb_read(uint16_t addr, void *priv)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;
    uint8_t   ret = 0x00;

    /* Sound Blasters prior to Sound Blaster 16 alias the I/O ports. */
    if ((dsp->sb_type < SB16_DSP_404) && (IS_NOT_ESS(dsp) || ((addr & 0xF) != 0xF)))
        /* Exception: ESS AudioDrive does not alias port base+0xf */
            addr &= 0xfffe;

    switch (addr & 0xf) {
        case 0x6:
            if (IS_ESS(dsp)) {
                ret = (dsp->espcm_fifo_reset & 0x03) | 0x08 | (dsp->activity & 0xe0);
                dsp->activity |= 0xe0;
            } else
                ret = 0xff;
            break;
        case 0x7:
        case 0xB:
            /*
               These two ports are tested for random noise by OS/2 Warp 4.0, so
               return 0xff to get through said test.
             */
            ret = 0xff;
            break;
        case 0xA: /* Read data */
            if (dsp->mpu && dsp->uart_midi)
                ret = MPU401_ReadData(dsp->mpu);
            else {
                if (dsp->sb_read_rp != dsp->sb_read_wp) {
                    dsp->sbreaddat = dsp->sb_read_data[dsp->sb_read_rp];
                    dsp->sb_read_rp++;
                    dsp->sb_read_rp &= 0xff;
                }
                ret = dsp->sbreaddat;
            }
            /* Advance the state just in case something reads from here
               without reading the status first. */
            if (dsp->state == DSP_S_RESET_WAIT)
                dsp->state = DSP_S_NORMAL;
            break;
        case 0xC: /* Write data ready */
            /* Advance the state just in case something reads from here
               without reading the status first. */
            if (dsp->state == DSP_S_RESET_WAIT)
                dsp->state = DSP_S_NORMAL;
            if ((dsp->state == DSP_S_NORMAL) || IS_ESS(dsp)) {
                if (dsp->sb_8_enable || dsp->sb_type >= SB16_DSP_404)
                    dsp->busy_count = (dsp->busy_count + 1) & 3;
                else
                    dsp->busy_count = 0;
                if (IS_ESS(dsp)) {
                    if (dsp->wb_full || (dsp->busy_count & 2))
                        dsp->wb_full = timer_is_enabled(&dsp->wb_timer);

                    const uint8_t busy_flag   = dsp->wb_full ? 0x80 : 0x00;
                    const uint8_t data_rdy    = (dsp->sb_read_rp == dsp->sb_read_wp) ? 0x00 : 0x40;
                    const uint8_t fifo_full   = 0; /* Unimplemented */
                    const uint8_t fifo_empty  = 0; /* (this is for the 256-byte extended mode FIFO, */
                    const uint8_t fifo_half   = 0; /* not the standard 64-byte FIFO) */
                    const uint8_t irq_generic = dsp->ess_irq_generic ? 0x04 : 0x00;
                    const uint8_t irq_fifohe  = 0; /* Unimplemented (ditto) */
                    const uint8_t irq_dmactr  = dsp->ess_irq_dmactr ? 0x01 : 0x00;

                    ret = busy_flag | data_rdy | fifo_full | fifo_empty | fifo_half | irq_generic | irq_fifohe | irq_dmactr;
                } else if (dsp->wb_full || (dsp->busy_count & 2)) {
                    dsp->wb_full = timer_is_enabled(&dsp->wb_timer);
                    if (IS_AZTECH(dsp)) {
                        sb_dsp_log("SB Write Data Aztech read 0x80\n");
                        ret = 0x80;
                    } else {
                        sb_dsp_log("SB Write Data Creative read 0xff\n");
                        if ((dsp->sb_type >= SB_DSP_201) && (dsp->sb_type < SB16_DSP_404) && IS_NOT_ESS(dsp))
                            ret = 0xaa;
                        else
                            ret = 0xff;
                    }
                } else if (IS_AZTECH(dsp)) {
                    sb_dsp_log("SB Write Data Aztech read 0x00\n");
                    ret = 0x00;
                } else {
                    sb_dsp_log("SB Write Data Creative read 0x7f\n");
                    if ((dsp->sb_type >= SB_DSP_201) && (dsp->sb_type < SB16_DSP_404) && IS_NOT_ESS(dsp))
                        ret = 0x2a;
                    else
                        ret = 0x7f;
                }
            } else if (IS_AZTECH(dsp))
                ret = 0x00;
            else
                ret = 0xff;
            break;
        case 0xE: /* Read data ready */
            dsp->irq_update(dsp->irq_priv, 0);
            dsp->sb_irq8 = dsp->sb_irq16 = 0;
            dsp->ess_irq_generic = dsp->ess_irq_dmactr = false;
            /*
               Only bit 7 is defined but aztech diagnostics fail if the others are set.
               Keep the original behavior to not interfere with what's already working.
             */
            if (IS_AZTECH(dsp)) {
                sb_dsp_log("SB Read Data Aztech read %02X, Read RP = %d, Read WP = %d\n",
                           (dsp->sb_read_rp == dsp->sb_read_wp) ? 0x00 : 0x80, dsp->sb_read_rp, dsp->sb_read_wp);
                ret = (dsp->sb_read_rp == dsp->sb_read_wp) ? 0x00 : 0x80;
            } else {
                sb_dsp_log("SB Read Data Creative read %02X\n", (dsp->sb_read_rp == dsp->sb_read_wp) ? 0x7f : 0xff);
                if ((dsp->sb_type < SB16_DSP_404) && IS_NOT_ESS(dsp))
                    ret = (dsp->sb_read_rp == dsp->sb_read_wp) ? 0x2a : 0xaa;
                else
                    ret = (dsp->sb_read_rp == dsp->sb_read_wp) ? 0x7f : 0xff;
            }
            if (dsp->state == DSP_S_RESET_WAIT)
                dsp->state = DSP_S_NORMAL;
            break;
        case 0xF: /* 16-bit ack */
            if (IS_NOT_ESS(dsp)) {
                dsp->sb_irq16 = 0;
                if (!dsp->sb_irq8)
                    dsp->irq_update(dsp->irq_priv, 0);
                sb_dsp_log("SB 16-bit ACK read 0xFF\n");
            }
            ret = 0xff;
            break;

        default:
            break;
    }

    sb_dsp_log("[%04X:%08X] DSP: [R] %04X = %02X\n", CS, cpu_state.pc, a, ret);

    return ret;
}

void
sb_dsp_input_msg(void *priv, uint8_t *msg, uint32_t len)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;

    sb_dsp_log("MIDI in sysex = %d, uart irq = %d, msg = %d\n", dsp->midi_in_sysex, dsp->uart_irq, len);

    if (!dsp->uart_irq && !dsp->midi_in_poll && (dsp->mpu != NULL)) {
        MPU401_InputMsg(dsp->mpu, msg, len);
        return;
    }

    if (dsp->midi_in_sysex)
        return;

    if (dsp->uart_irq) {
        for (uint32_t i = 0; i < len; i++)
            sb_add_data(dsp, msg[i]);
        sb_irq(dsp, 1);
        dsp->ess_irq_generic = true;
    } else if (dsp->midi_in_poll) {
        for (uint32_t i = 0; i < len; i++)
            sb_add_data(dsp, msg[i]);
    }
}

int
sb_dsp_input_sysex(void *priv, uint8_t *buffer, uint32_t len, int abort)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;

    if (!dsp->uart_irq && !dsp->midi_in_poll && (dsp->mpu != NULL))
        return MPU401_InputSysex(dsp->mpu, buffer, len, abort);

    if (abort) {
        dsp->midi_in_sysex = 0;
        return 0;
    }

    dsp->midi_in_sysex = 1;

    for (uint32_t i = 0; i < len; i++) {
        if (dsp->sb_read_rp == dsp->sb_read_wp) {
            sb_dsp_log("Length sysex SB = %d\n", len - i);
            return (int) (len - i);
        }

        sb_add_data(dsp, buffer[i]);
    }

    dsp->midi_in_sysex = 0;

    return 0;
}

void
sb_dsp_irq_poll(void *priv)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;

    sb_irq(dsp, 1);
    dsp->ess_irq_generic = true;
}

void
sb_dsp_irq16_poll(void *priv)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;

    sb_irq(dsp, 0);
    dsp->ess_irq_generic = true;
}

void
sb_dsp_init(sb_dsp_t *dsp, int type, int subtype, void *parent)
{
    dsp->sb_type    = type;
    dsp->sb_subtype = subtype;
    dsp->parent     = parent;
    dsp->activity   = 0xe0;

    /* Default values. Use sb_dsp_setxxx() methods to change. */
    dsp->sb_irqnum    = 7;
    dsp->sb_8_dmanum  = 1;
    if (type >= SB16_DSP_404)
        dsp->sb_16_dmanum = 5;
    else
        dsp->sb_16_dmanum = 0xff;
    if ((type >= SB16_DSP_404) || IS_ESS(dsp))
        dsp->sb_16_8_dmanum = 0x1;
    dsp->mpu          = NULL;

    dsp->sbleftright_default = 0;

    dsp->irq_update = sb_irq_update_pic;
    dsp->irq_priv   = dsp;
    dsp->dma_readb  = sb_8_read_dma;
    dsp->dma_readw  = sb_16_read_dma;
    dsp->dma_writeb = sb_8_write_dma;
    dsp->dma_writew = sb_16_write_dma;
    dsp->dma_priv   = dsp;

    sb_doreset(dsp);

    timer_add(&dsp->output_timer, pollsb, dsp, 0);
    timer_add(&dsp->input_timer, sb_poll_i, dsp, 0);
    timer_add(&dsp->wb_timer, NULL, dsp, 0);
    timer_add(&dsp->irq_timer, sb_dsp_irq_poll, dsp, 0);
    timer_add(&dsp->irq16_timer, sb_dsp_irq16_poll, dsp, 0);

    if (IS_ESS(dsp))
        /* Initialize ESS filter to 8 kHz. This will be recalculated when a set frequency command is
           sent. */
        recalc_sb16_filter(0, 8000 * 2);
    else {
        timer_add(&dsp->irq16_timer, sb_dsp_irq16_poll, dsp, 0);
        /* Initialise SB16 filter to same cutoff as 8-bit SBs (3.2 kHz). This will be recalculated when
           a set frequency command is sent. */
        recalc_sb16_filter(0, 3200 * 2);
    }
    if (IS_ESS(dsp) || (dsp->sb_type >= SBPRO2_DSP_302)) {
        /* OPL3 or dual OPL2 is stereo. */
        if (dsp->sb_has_real_opl)
            recalc_opl_filter(FREQ_49716 * 2);
        else
            recalc_sb16_filter(1, FREQ_48000 * 2);
    } else {
        /* OPL2 is mono. */
        if (dsp->sb_has_real_opl)
            recalc_opl_filter(FREQ_49716);
        else
            recalc_sb16_filter(1, FREQ_48000);
    }
    /* CD Audio is stereo. */
    recalc_sb16_filter(2, FREQ_44100 * 2);
    /* PC speaker is mono. */
    recalc_sb16_filter(3, 18939);
    /* E-MU 8000 is stereo. */
    recalc_sb16_filter(4, FREQ_44100 * 2);

    /* Initialize SB16 8051 RAM and ASP internal RAM */
    memset(dsp->sb_8051_ram, 0x00, sizeof(dsp->sb_8051_ram));
    dsp->sb_8051_ram[0x0e] = 0xff;
    dsp->sb_8051_ram[0x0f] = 0x07;
    dsp->sb_8051_ram[0x37] = 0x38;

    memset(dsp->sb_asp_ram, 0xff, sizeof(dsp->sb_asp_ram));

    dsp->espcm_fifo = fifo64_init();
    fifo_set_trigger_len(dsp->espcm_fifo, 1);
}

void
sb_dsp_setaddr(sb_dsp_t *dsp, uint16_t addr)
{
    sb_dsp_log("sb_dsp_setaddr : %04X\n", addr);
    if (dsp->sb_addr != 0) {
        io_removehandler(dsp->sb_addr + 6, 0x0002, sb_read, NULL, NULL, sb_write, NULL, NULL, dsp);
        io_removehandler(dsp->sb_addr + 0xa, 0x0006, sb_read, NULL, NULL, sb_write, NULL, NULL, dsp);
    }
    dsp->sb_addr = addr;
    if (dsp->sb_addr != 0) {
        io_sethandler(dsp->sb_addr + 6, 0x0002, sb_read, NULL, NULL, sb_write, NULL, NULL, dsp);
        io_sethandler(dsp->sb_addr + 0xa, 0x0006, sb_read, NULL, NULL, sb_write, NULL, NULL, dsp);
    }
}

void
sb_dsp_set_real_opl(sb_dsp_t *dsp, uint8_t has_real_opl)
{
    dsp->sb_has_real_opl = has_real_opl;
}

void
sb_dsp_set_stereo(sb_dsp_t *dsp, int stereo)
{
    dsp->stereo = stereo;
}

void
sb_dsp_irq_attach(sb_dsp_t *dsp, void (*irq_update)(void *priv, int set), void *priv)
{
    dsp->irq_update = irq_update;
    dsp->irq_priv   = priv;
}

void
sb_dsp_dma_attach(sb_dsp_t *dsp,
                  int (*dma_readb)(void *priv),
                  int (*dma_readw)(void *priv),
                  int (*dma_writeb)(void *priv, uint8_t val),
                  int (*dma_writew)(void *priv, uint16_t val),
                  void *priv)
{
    dsp->dma_readb  = dma_readb;
    dsp->dma_readw  = dma_readw;
    dsp->dma_writeb = dma_writeb;
    dsp->dma_writew = dma_writew;
    dsp->dma_priv   = priv;
}

void
sb_espcm_fifoctl_run(sb_dsp_t *dsp)
{
    if (fifo_get_empty(dsp->espcm_fifo) && !dsp->sb_8_pause) {
        while (!fifo_get_full(dsp->espcm_fifo)) {
            int32_t val;
            val = dsp->dma_readb(dsp->dma_priv);
            dsp->ess_dma_counter++;
            fifo_write(val & 0xff, dsp->espcm_fifo);
            if (val & DMA_OVER) {
                break;
            }
        }
    }
}

void
pollsb(void *priv)
{
    sb_dsp_t *dsp = (sb_dsp_t *) priv;
    int       tempi;
    int       ref;
    int       data[2];

    timer_advance_u64(&dsp->output_timer, (uint64_t) dsp->sblatcho);
    if (dsp->sb_8_enable && dsp->sb_pausetime < 0 && dsp->sb_8_output) {
        sb_dsp_update(dsp);

        switch (dsp->sb_8_format) {
            case 0x00: /* Mono unsigned */
                if (!dsp->sb_8_pause) {
                    data[0] = dsp->dma_readb(dsp->dma_priv);
                    /* Needed to prevent clicking in Worms, which programs the DSP to
                    auto-init DMA but programs the DMA controller to single cycle */
                    if (data[0] == DMA_NODATA)
                        break;
                    dsp->sbdat = (int16_t) ((data[0] ^ 0x80) << 8);
                    if (dsp->stereo) {
                        sb_dsp_log("pollsb: Mono unsigned, dsp->stereo, %s channel, %04X\n",
                                   dsp->sbleftright ? "left" : "right", dsp->sbdat);
                        if (dsp->sbleftright)
                            dsp->sbdatl = dsp->sbdat;
                        else
                            dsp->sbdatr = dsp->sbdat;
                        dsp->sbleftright = !dsp->sbleftright;
                    } else
                        dsp->sbdatl = dsp->sbdatr = dsp->sbdat;
                    dsp->sb_8_length--;
                    dsp->ess_dma_counter++;
                }
                break;
            case 0x10: /* Mono signed */
                if (!dsp->sb_8_pause) {
                    data[0] = dsp->dma_readb(dsp->dma_priv);
                    if (data[0] == DMA_NODATA)
                        break;
                    dsp->sbdat = (int16_t) (data[0] << 8);
                    if (dsp->stereo) {
                        sb_dsp_log("pollsb: Mono signed, dsp->stereo, %s channel, %04X\n",
                                   dsp->sbleftright ? "left" : "right", data[0], dsp->sbdat);
                        if (dsp->sbleftright)
                            dsp->sbdatl = dsp->sbdat;
                        else
                            dsp->sbdatr = dsp->sbdat;
                        dsp->sbleftright = !dsp->sbleftright;
                    } else
                        dsp->sbdatl = dsp->sbdatr = dsp->sbdat;
                    dsp->sb_8_length--;
                    dsp->ess_dma_counter++;
                }
                break;
            case 0x20: /* Stereo unsigned */
                if (!dsp->sb_8_pause) {
                    data[0] = dsp->dma_readb(dsp->dma_priv);
                    data[1] = dsp->dma_readb(dsp->dma_priv);
                    if ((data[0] == DMA_NODATA) || (data[1] == DMA_NODATA))
                        break;
                    dsp->sbdatl = (int16_t) ((data[0] ^ 0x80) << 8);
                    dsp->sbdatr = (int16_t) ((data[1] ^ 0x80) << 8);
                    dsp->sb_8_length -= 2;
                    dsp->ess_dma_counter += 2;
                }
                break;
            case 0x30: /* Stereo signed */
                if (!dsp->sb_8_pause) {
                    data[0] = dsp->dma_readb(dsp->dma_priv);
                    data[1] = dsp->dma_readb(dsp->dma_priv);
                    if ((data[0] == DMA_NODATA) || (data[1] == DMA_NODATA))
                        break;
                    dsp->sbdatl = (int16_t) (data[0] << 8);
                    dsp->sbdatr = (int16_t) (data[1] << 8);
                    dsp->sb_8_length -= 2;
                    dsp->ess_dma_counter += 2;
                }
                break;

            case ADPCM_4:
                if (!dsp->sb_8_pause) {
                    if (dsp->sbdacpos)
                        tempi = (dsp->sbdat2 & 0xF) + dsp->sbstep;
                    else
                        tempi = (dsp->sbdat2 >> 4) + dsp->sbstep;
                    if (tempi < 0)
                        tempi = 0;
                    if (tempi > 63)
                        tempi = 63;

                    ref = dsp->sbref + scaleMap4[tempi];
                    if (ref > 0xff)
                        dsp->sbref = 0xff;
                    else if (ref < 0x00)
                        dsp->sbref = 0x00;
                    else
                        dsp->sbref = ref;

                    dsp->sbstep = (int8_t) ((dsp->sbstep + adjustMap4[tempi]) & 0xff);
                    dsp->sbdat  = (int16_t) ((dsp->sbref ^ 0x80) << 8);

                    dsp->sbdacpos++;

                    if (dsp->sbdacpos >= 2) {
                        dsp->sbdacpos = 0;
                        dsp->sbdat2   = dsp->dma_readb(dsp->dma_priv);
                        dsp->sb_8_length--;
                        dsp->ess_dma_counter++;
                    }

                    if (dsp->stereo) {
                        sb_dsp_log("pollsb: ADPCM 4, dsp->stereo, %s channel, %04X\n",
                                   dsp->sbleftright ? "left" : "right", dsp->sbdat);
                        if (dsp->sbleftright)
                            dsp->sbdatl = dsp->sbdat;
                        else
                            dsp->sbdatr = dsp->sbdat;
                        dsp->sbleftright = !dsp->sbleftright;
                    } else
                        dsp->sbdatl = dsp->sbdatr = dsp->sbdat;
                }
                break;

            case ADPCM_26:
                if (!dsp->sb_8_pause) {
                    if (!dsp->sbdacpos)
                        tempi = (dsp->sbdat2 >> 5) + dsp->sbstep;
                    else if (dsp->sbdacpos == 1)
                        tempi = ((dsp->sbdat2 >> 2) & 7) + dsp->sbstep;
                    else
                        tempi = ((dsp->sbdat2 << 1) & 7) + dsp->sbstep;

                    if (tempi < 0)
                        tempi = 0;
                    if (tempi > 39)
                        tempi = 39;

                    ref = dsp->sbref + scaleMap26[tempi];
                    if (ref > 0xff)
                        dsp->sbref = 0xff;
                    else if (ref < 0x00)
                        dsp->sbref = 0x00;
                    else
                        dsp->sbref = ref;
                    dsp->sbstep = (int8_t) ((dsp->sbstep + adjustMap26[tempi]) & 0xff);

                    dsp->sbdat = (int16_t) ((dsp->sbref ^ 0x80) << 8);

                    dsp->sbdacpos++;
                    if (dsp->sbdacpos >= 3) {
                        dsp->sbdacpos = 0;
                        dsp->sbdat2   = dsp->dma_readb(dsp->dma_priv);
                        dsp->sb_8_length--;
                        dsp->ess_dma_counter++;
                    }

                    if (dsp->stereo) {
                        sb_dsp_log("pollsb: ADPCM 26, dsp->stereo, %s channel, %04X\n",
                                   dsp->sbleftright ? "left" : "right", dsp->sbdat);
                        if (dsp->sbleftright)
                            dsp->sbdatl = dsp->sbdat;
                        else
                            dsp->sbdatr = dsp->sbdat;
                        dsp->sbleftright = !dsp->sbleftright;
                    } else
                        dsp->sbdatl = dsp->sbdatr = dsp->sbdat;
                }
                break;

            case ADPCM_2:
                if (!dsp->sb_8_pause) {
                    tempi = ((dsp->sbdat2 >> ((3 - dsp->sbdacpos) * 2)) & 3) + dsp->sbstep;
                    if (tempi < 0)
                        tempi = 0;
                    if (tempi > 23)
                        tempi = 23;

                    ref = dsp->sbref + scaleMap2[tempi];
                    if (ref > 0xff)
                        dsp->sbref = 0xff;
                    else if (ref < 0x00)
                        dsp->sbref = 0x00;
                    else
                        dsp->sbref = ref;
                    dsp->sbstep = (int8_t) ((dsp->sbstep + adjustMap2[tempi]) & 0xff);

                    dsp->sbdat = (int16_t) ((dsp->sbref ^ 0x80) << 8);

                    dsp->sbdacpos++;
                    if (dsp->sbdacpos >= 4) {
                        dsp->sbdacpos = 0;
                        dsp->sbdat2   = dsp->dma_readb(dsp->dma_priv);
                        dsp->sb_8_length--;
                        dsp->ess_dma_counter++;
                    }

                    if (dsp->stereo) {
                        sb_dsp_log("pollsb: ADPCM 2, dsp->stereo, %s channel, %04X\n",
                                   dsp->sbleftright ? "left" : "right", dsp->sbdat);
                        if (dsp->sbleftright)
                            dsp->sbdatl = dsp->sbdat;
                        else
                            dsp->sbdatr = dsp->sbdat;
                        dsp->sbleftright = !dsp->sbleftright;
                    } else
                        dsp->sbdatl = dsp->sbdatr = dsp->sbdat;
                }
                break;

            case ESPCM_4:
                if (dsp->espcm_sample_idx >= 19)
                    dsp->espcm_sample_idx = 0;
                if (dsp->espcm_sample_idx == 0) {
                    sb_espcm_fifoctl_run(dsp);
                    if (fifo_get_empty(dsp->espcm_fifo))
                        break;
                    dsp->espcm_byte_buffer[0] = fifo_read(dsp->espcm_fifo);

                    dsp->espcm_range = dsp->espcm_byte_buffer[0] & 0x0F;
                    tempi            = dsp->espcm_byte_buffer[0] >> 4;
                } else if (dsp->espcm_sample_idx & 1) {
                    sb_espcm_fifoctl_run(dsp);
                    if (fifo_get_empty(dsp->espcm_fifo))
                        break;
                    dsp->espcm_byte_buffer[0] = fifo_read(dsp->espcm_fifo);
                    dsp->sb_8_length--;

                    tempi = dsp->espcm_byte_buffer[0] & 0x0F;
                } else
                    tempi = dsp->espcm_byte_buffer[0] >> 4;

                if (dsp->espcm_sample_idx == 18)
                    dsp->sb_8_length--;

                dsp->espcm_sample_idx++;

                tempi |= (dsp->espcm_range << 4);
                data[0]    = (int) espcm_range_map[tempi];
                dsp->sbdat = (int16_t) (data[0] << 8);
                if (dsp->stereo) {
                    sb_dsp_log("pollsb: ESPCM 4, dsp->stereo, %s channel, %04X\n",
                               dsp->sbleftright ? "left" : "right", dsp->sbdat);
                    if (dsp->sbleftright)
                        dsp->sbdatl = dsp->sbdat;
                    else
                        dsp->sbdatr = dsp->sbdat;
                    dsp->sbleftright = !dsp->sbleftright;
                } else
                    dsp->sbdatl = dsp->sbdatr = dsp->sbdat;
                break;

            case ESPCM_3:
                if (dsp->espcm_sample_idx >= 19)
                    dsp->espcm_sample_idx = 0;
                if (dsp->espcm_sample_idx == 0) {
                    sb_espcm_fifoctl_run(dsp);
                    if (fifo_get_empty(dsp->espcm_fifo))
                        break;
                    dsp->espcm_byte_buffer[0] = fifo_read(dsp->espcm_fifo);

                    dsp->espcm_range      = dsp->espcm_byte_buffer[0] & 0x0F;
                    tempi                 = dsp->espcm_byte_buffer[0] >> 4;
                    dsp->espcm_last_value = tempi;
                } else if (dsp->espcm_sample_idx == 1) {
                    for (tempi = 0; tempi < 4; tempi++) {
                        sb_espcm_fifoctl_run(dsp);
                        if (fifo_get_empty(dsp->espcm_fifo))
                            break;
                        dsp->espcm_byte_buffer[tempi] = fifo_read(dsp->espcm_fifo);
                        dsp->sb_8_length--;
                    }
                    if (tempi < 4)
                        break;

                    dsp->espcm_table_index = dsp->espcm_byte_buffer[0] & 0x03;

                    dsp->espcm_code_buffer[0] = (dsp->espcm_byte_buffer[0] >> 2) & 0x07;
                    dsp->espcm_code_buffer[1] = (dsp->espcm_byte_buffer[0] >> 5) & 0x07;
                    dsp->espcm_code_buffer[2] = (dsp->espcm_byte_buffer[1]) & 0x07;
                    dsp->espcm_code_buffer[3] = (dsp->espcm_byte_buffer[1] >> 3) & 0x07;
                    dsp->espcm_code_buffer[4] = ((dsp->espcm_byte_buffer[1] >> 6) & 0x03) | ((dsp->espcm_byte_buffer[2] & 0x01) << 2);
                    dsp->espcm_code_buffer[5] = (dsp->espcm_byte_buffer[2] >> 1) & 0x07;
                    dsp->espcm_code_buffer[6] = (dsp->espcm_byte_buffer[2] >> 4) & 0x07;
                    dsp->espcm_code_buffer[7] = ((dsp->espcm_byte_buffer[2] >> 7) & 0x01) | ((dsp->espcm_byte_buffer[3] & 0x03) << 1);
                    dsp->espcm_code_buffer[8] = (dsp->espcm_byte_buffer[3] >> 2) & 0x07;
                    dsp->espcm_code_buffer[9] = (dsp->espcm_byte_buffer[3] >> 5) & 0x07;

                    tempi                 = (dsp->espcm_table_index << 8) | (dsp->espcm_last_value << 3) | dsp->espcm_code_buffer[0];
                    tempi                 = espcm3_dpcm_tables[tempi];
                    dsp->espcm_last_value = tempi;
                } else if (dsp->espcm_sample_idx == 11) {
                    for (tempi = 1; tempi < 4; tempi++) {
                        sb_espcm_fifoctl_run(dsp);
                        if (fifo_get_empty(dsp->espcm_fifo))
                            break;
                        dsp->espcm_byte_buffer[tempi] = fifo_read(dsp->espcm_fifo);
                        dsp->sb_8_length--;
                    }
                    if (tempi < 4)
                        break;

                    dsp->espcm_code_buffer[0] = (dsp->espcm_byte_buffer[1]) & 0x07;
                    dsp->espcm_code_buffer[1] = (dsp->espcm_byte_buffer[1] >> 3) & 0x07;
                    dsp->espcm_code_buffer[2] = ((dsp->espcm_byte_buffer[1] >> 6) & 0x03) | ((dsp->espcm_byte_buffer[2] & 0x01) << 2);
                    dsp->espcm_code_buffer[3] = (dsp->espcm_byte_buffer[2] >> 1) & 0x07;
                    dsp->espcm_code_buffer[4] = (dsp->espcm_byte_buffer[2] >> 4) & 0x07;
                    dsp->espcm_code_buffer[5] = ((dsp->espcm_byte_buffer[2] >> 7) & 0x01) | ((dsp->espcm_byte_buffer[3] & 0x03) << 1);
                    dsp->espcm_code_buffer[6] = (dsp->espcm_byte_buffer[3] >> 2) & 0x07;
                    dsp->espcm_code_buffer[7] = (dsp->espcm_byte_buffer[3] >> 5) & 0x07;

                    tempi                 = (dsp->espcm_table_index << 8) | (dsp->espcm_last_value << 3) | dsp->espcm_code_buffer[0];
                    tempi                 = espcm3_dpcm_tables[tempi];
                    dsp->espcm_last_value = tempi;
                } else {
                    tempi                 = (dsp->espcm_table_index << 8) | (dsp->espcm_last_value << 3) | dsp->espcm_code_buffer[(dsp->espcm_sample_idx - 1) % 10];
                    tempi                 = espcm3_dpcm_tables[tempi];
                    dsp->espcm_last_value = tempi;
                }

                if (dsp->espcm_sample_idx == 18) {
                    dsp->sb_8_length--;
                }

                dsp->espcm_sample_idx++;

                tempi |= (dsp->espcm_range << 4);
                data[0]    = (int) (espcm_range_map[tempi]);
                dsp->sbdat = (int16_t) (data[0] << 8);
                if (dsp->stereo) {
                    sb_dsp_log("pollsb: ESPCM 3, dsp->stereo, %s channel, %04X\n",
                               dsp->sbleftright ? "left" : "right", dsp->sbdat);
                    if (dsp->sbleftright)
                        dsp->sbdatl = dsp->sbdat;
                    else
                        dsp->sbdatr = dsp->sbdat;
                    dsp->sbleftright = !dsp->sbleftright;
                } else
                    dsp->sbdatl = dsp->sbdatr = dsp->sbdat;
                break;

            case ESPCM_1:
                if (dsp->espcm_sample_idx >= 19)
                    dsp->espcm_sample_idx = 0;
                if (dsp->espcm_sample_idx == 0) {
                    sb_espcm_fifoctl_run(dsp);

                    if (fifo_get_empty(dsp->espcm_fifo))
                        break;

                    dsp->espcm_byte_buffer[0] = fifo_read(dsp->espcm_fifo);

                    dsp->espcm_range = dsp->espcm_byte_buffer[0] & 0x0F;
                    dsp->espcm_byte_buffer[0] >>= 5;
                    tempi = dsp->espcm_byte_buffer[0] & 1 ? 0xC : 0x4;
                    dsp->espcm_byte_buffer[0] >>= 1;
                } else if ((dsp->espcm_sample_idx == 3) | (dsp->espcm_sample_idx == 11)) {
                    sb_espcm_fifoctl_run(dsp);
                    if (fifo_get_empty(dsp->espcm_fifo)) {
                        break;
                    }
                    dsp->espcm_byte_buffer[0] = fifo_read(dsp->espcm_fifo);
                    dsp->sb_8_length--;

                    tempi = dsp->espcm_byte_buffer[0] & 1 ? 0xC : 0x4;
                    dsp->espcm_byte_buffer[0] >>= 1;
                } else {
                    tempi = dsp->espcm_byte_buffer[0] & 1 ? 0xC : 0x4;
                    dsp->espcm_byte_buffer[0] >>= 1;
                }

                if (dsp->espcm_sample_idx == 18)
                    dsp->sb_8_length--;

                dsp->espcm_sample_idx++;

                tempi     |= (dsp->espcm_range << 4);
                data[0]    = (int) espcm_range_map[tempi];
                dsp->sbdat = (int16_t) (data[0] << 8);
                if (dsp->stereo) {
                    sb_dsp_log("pollsb: ESPCM 1, dsp->stereo, %s channel, %04X\n",
                               dsp->sbleftright ? "left" : "right", dsp->sbdat);
                    if (dsp->sbleftright)
                        dsp->sbdatl = dsp->sbdat;
                    else
                        dsp->sbdatr = dsp->sbdat;
                    dsp->sbleftright = !dsp->sbleftright;
                } else
                    dsp->sbdatl = dsp->sbdatr = dsp->sbdat;
                break;

            default:
                break;
        }

        if (dsp->sb_8_length < 0 && !dsp->ess_playback_mode) {
            if (dsp->sb_8_autoinit)
                dsp->sb_8_length = dsp->sb_8_origlength = dsp->sb_8_autolen;
            else {
                dsp->sb_8_enable = 0;
                timer_disable(&dsp->output_timer);
                sb_finish_dma(dsp);
            }
            sb_irq(dsp, 1);
            dsp->ess_irq_generic = true;
        }
        if (dsp->ess_dma_counter > 0xffff) {
            if (dsp->ess_playback_mode) {
                if (!dsp->sb_8_autoinit) {
                    dsp->sb_8_enable = 0;
                    timer_disable(&dsp->output_timer);
                    sb_finish_dma(dsp);
                }
                if (ESSreg(0xB1) & 0x40) {
                    sb_irq(dsp, 1);
                    dsp->ess_irq_dmactr = true;
                }
            }
            const uint32_t temp        = dsp->ess_dma_counter & 0xffff;
            dsp->ess_dma_counter       = sb_ess_get_dma_counter(dsp);
            dsp->ess_dma_counter      += temp;
        }
    }
    if (dsp->sb_16_enable && !dsp->sb_16_pause && (dsp->sb_pausetime < 0LL) && dsp->sb_16_output) {
        sb_dsp_update(dsp);

        switch (dsp->sb_16_format) {
            case 0x00: /* Mono unsigned */
                data[0] = dsp->dma_readw(dsp->dma_priv);
                if (data[0] == DMA_NODATA)
                    break;
                dsp->sbdatl = dsp->sbdatr = (int16_t) ((data[0] & 0xffff) ^ 0x8000);
                dsp->sb_16_length--;
                dsp->ess_dma_counter += 2;
                break;
            case 0x10: /* Mono signed */
                data[0] = dsp->dma_readw(dsp->dma_priv);
                if (data[0] == DMA_NODATA)
                    break;
                dsp->sbdatl = dsp->sbdatr = (int16_t) (data[0] & 0xffff);
                dsp->sb_16_length--;
                dsp->ess_dma_counter += 2;
                break;
            case 0x20: /* Stereo unsigned */
                data[0] = dsp->dma_readw(dsp->dma_priv);
                data[1] = dsp->dma_readw(dsp->dma_priv);
                if ((data[0] == DMA_NODATA) || (data[1] == DMA_NODATA))
                    break;
                dsp->sbdatl = (int16_t) ((data[0] & 0xffff) ^ 0x8000);
                dsp->sbdatr = (int16_t) ((data[1] & 0xffff) ^ 0x8000);
                dsp->sb_16_length -= 2;
                dsp->ess_dma_counter += 4;
                break;
            case 0x30: /* Stereo signed */
                data[0] = dsp->dma_readw(dsp->dma_priv);
                data[1] = dsp->dma_readw(dsp->dma_priv);
                if ((data[0] == DMA_NODATA) || (data[1] == DMA_NODATA))
                    break;
                dsp->sbdatl = (int16_t) (data[0] & 0xffff);
                dsp->sbdatr = (int16_t) (data[1] & 0xffff);
                dsp->sb_16_length -= 2;
                dsp->ess_dma_counter += 4;
                break;

            default:
                break;
        }

        if (dsp->sb_16_length < 0 && !dsp->ess_playback_mode) {
            sb_dsp_log("16DMA over %i\n", dsp->sb_16_autoinit);
            if (dsp->sb_16_autoinit)
                dsp->sb_16_length = dsp->sb_16_origlength = dsp->sb_16_autolen;
            else {
                dsp->sb_16_enable = 0;
                timer_disable(&dsp->output_timer);
                sb_finish_dma(dsp);
            }
            sb_irq(dsp, 0);
            dsp->ess_irq_generic = true;
        }
        if (dsp->ess_dma_counter > 0xffff) {
            if (dsp->ess_playback_mode) {
                if (!dsp->sb_16_autoinit) {
                    dsp->sb_16_enable = 0;
                    timer_disable(&dsp->output_timer);
                    sb_finish_dma(dsp);
                }
                if (ESSreg(0xB1) & 0x40) {
                    sb_irq(dsp, 0);
                    dsp->ess_irq_dmactr = true;
                }
            }
            const uint32_t temp        = dsp->ess_dma_counter & 0xffff;
            dsp->ess_dma_counter       = sb_ess_get_dma_counter(dsp);
            dsp->ess_dma_counter      += temp;
        }
    }
    if (dsp->sb_pausetime > -1) {
        dsp->sb_pausetime--;
        if (dsp->sb_pausetime < 0) {
            sb_irq(dsp, 1);
            dsp->ess_irq_generic = true;
            if (!dsp->sb_8_enable)
                timer_disable(&dsp->output_timer);
            sb_dsp_log("SB pause over\n");
        }
    }
}

void
sb_poll_i(void *priv)
{
    sb_dsp_t *dsp       = (sb_dsp_t *) priv;
    int       processed = 0;

    timer_advance_u64(&dsp->input_timer, (uint64_t) dsp->sblatchi);

    if (dsp->sb_8_enable && !dsp->sb_8_pause && dsp->sb_pausetime < 0 && !dsp->sb_8_output) {
        switch (dsp->sb_8_format) {
            case 0x00: /* Mono unsigned As the manual says, only the left channel is recorded */
                dsp->dma_writeb(dsp->dma_priv, (dsp->record_buffer[dsp->record_pos_read] >> 8) ^ 0x80);
                dsp->sb_8_length--;
                dsp->ess_dma_counter++;
                dsp->record_pos_read += 2;
                dsp->record_pos_read &= 0xFFFF;
                break;
            case 0x10: /* Mono signed As the manual says, only the left channel is recorded */
                dsp->dma_writeb(dsp->dma_priv, (dsp->record_buffer[dsp->record_pos_read] >> 8));
                dsp->sb_8_length--;
                dsp->ess_dma_counter++;
                dsp->record_pos_read += 2;
                dsp->record_pos_read &= 0xFFFF;
                break;
            case 0x20: /* Stereo unsigned */
                dsp->dma_writeb(dsp->dma_priv, (dsp->record_buffer[dsp->record_pos_read] >> 8) ^ 0x80);
                dsp->dma_writeb(dsp->dma_priv, (dsp->record_buffer[dsp->record_pos_read + 1] >> 8) ^ 0x80);
                dsp->sb_8_length -= 2;
                dsp->ess_dma_counter += 2;
                dsp->record_pos_read += 2;
                dsp->record_pos_read &= 0xFFFF;
                break;
            case 0x30: /* Stereo signed */
                dsp->dma_writeb(dsp->dma_priv, (dsp->record_buffer[dsp->record_pos_read] >> 8));
                dsp->dma_writeb(dsp->dma_priv, (dsp->record_buffer[dsp->record_pos_read + 1] >> 8));
                dsp->sb_8_length -= 2;
                dsp->ess_dma_counter += 2;
                dsp->record_pos_read += 2;
                dsp->record_pos_read &= 0xFFFF;
                break;
            case ESPCM_4E:
                /*
                   I assume the real hardware double-buffers the blocks or something like that.
                   We're not gonna do that here.
                 */
                dsp->espcm_sample_buffer[dsp->espcm_sample_idx] = (int8_t) (dsp->record_buffer[dsp->record_pos_read] >> 8);
                dsp->espcm_sample_idx++;
                dsp->record_pos_read += 2;
                dsp->record_pos_read &= 0xFFFF;
                if (dsp->espcm_sample_idx >= 19) {
                    int     i, table_addr;
                    int8_t  min_sample = 127, max_sample = -128, s;

                    for (i = 0; i < 19; i++) {
                        s = dsp->espcm_sample_buffer[i];
                        if (s < min_sample)
                            min_sample = s;
                        if (s > max_sample)
                            max_sample = s;
                    }
                    if (min_sample < 0) {
                        if (min_sample == -128)
                            min_sample = 127;    /* Clip it to make it fit into int8_t. */
                        else
                            min_sample = (int8_t) -min_sample;
                    }
                    if (max_sample < 0) {
                        if (max_sample == -128)
                            max_sample = 127;    /* Clip it to make it fit into int8_t. */
                        else
                            max_sample = (int8_t) -max_sample;
                    }
                    if (min_sample > max_sample)
                        max_sample = min_sample;

                    for (table_addr = 15; table_addr < 256; table_addr += 16) {
                        if (max_sample <= espcm_range_map[table_addr])
                            break;
                    }
                    dsp->espcm_range = table_addr >> 4;

                    for (i = 0; i < 19; i++) {
                        int last_sigma = 9999;
                        table_addr = dsp->espcm_range << 4;
                        s          = dsp->espcm_sample_buffer[i];
                        for (; (table_addr >> 4) == dsp->espcm_range; table_addr++) {
                            int sigma = espcm_range_map[table_addr] - s;
                            if (sigma < 0)
                                sigma = -sigma;
                            if (sigma > last_sigma)
                                break;
                            last_sigma = sigma;
                        }
                        table_addr--;
                        dsp->espcm_code_buffer[i] = table_addr & 0x0F;
                    }

                    uint8_t b = dsp->espcm_range | (dsp->espcm_code_buffer[0] << 4);
                    dsp->dma_writeb(dsp->dma_priv, b);
                    dsp->sb_8_length--;
                    dsp->ess_dma_counter++;

                    for (i = 1; i < 10; i++) {
                        b = dsp->espcm_code_buffer[i * 2 - 1] | (dsp->espcm_code_buffer[i * 2] << 4);
                        dsp->dma_writeb(dsp->dma_priv, b);
                        dsp->sb_8_length--;
                        dsp->ess_dma_counter++;
                    }

                    dsp->espcm_sample_idx = 0;
                }

            default:
                break;
        }

        if (dsp->sb_8_length < 0 && !dsp->ess_playback_mode) {
            if (dsp->sb_8_autoinit)
                dsp->sb_8_length = dsp->sb_8_origlength = dsp->sb_8_autolen;
            else {
                dsp->sb_8_enable = 0;
                timer_disable(&dsp->input_timer);
                sb_finish_dma(dsp);
            }
            sb_irq(dsp, 1);
            dsp->ess_irq_generic = true;
        }
        if (dsp->ess_dma_counter > 0xffff) {
            if (dsp->ess_playback_mode) {
                if (!dsp->sb_8_autoinit) {
                    dsp->sb_8_enable = 0;
                    timer_disable(&dsp->input_timer);
                    sb_finish_dma(dsp);
                }
                if (ESSreg(0xB1) & 0x40) {
                    sb_irq(dsp, 1);
                    dsp->ess_irq_dmactr = true;
                }
            }
            uint32_t temp        = dsp->ess_dma_counter & 0xffff;
            dsp->ess_dma_counter = sb_ess_get_dma_counter(dsp);
            dsp->ess_dma_counter += temp;
        }
        processed = 1;
    }
    if (dsp->sb_16_enable && !dsp->sb_16_pause && (dsp->sb_pausetime < 0LL) && !dsp->sb_16_output) {
        switch (dsp->sb_16_format) {
            case 0x00: /* Unsigned mono. As the manual says, only the left channel is recorded */
                if (dsp->dma_writew(dsp->dma_priv, dsp->record_buffer[dsp->record_pos_read] ^ 0x8000))
                    return;
                dsp->sb_16_length--;
                dsp->ess_dma_counter += 2;
                dsp->record_pos_read += 2;
                dsp->record_pos_read &= 0xFFFF;
                break;
            case 0x10: /* Signed mono. As the manual says, only the left channel is recorded */
                if (dsp->dma_writew(dsp->dma_priv, dsp->record_buffer[dsp->record_pos_read]))
                    return;
                dsp->sb_16_length--;
                dsp->ess_dma_counter += 2;
                dsp->record_pos_read += 2;
                dsp->record_pos_read &= 0xFFFF;
                break;
            case 0x20: /* Unsigned stereo */
                if (dsp->dma_writew(dsp->dma_priv, dsp->record_buffer[dsp->record_pos_read] ^ 0x8000))
                    return;
                dsp->dma_writew(dsp->dma_priv, dsp->record_buffer[dsp->record_pos_read + 1] ^ 0x8000);
                dsp->sb_16_length -= 2;
                dsp->ess_dma_counter += 4;
                dsp->record_pos_read += 2;
                dsp->record_pos_read &= 0xFFFF;
                break;
            case 0x30: /* Signed stereo */
                if (dsp->dma_writew(dsp->dma_priv, dsp->record_buffer[dsp->record_pos_read]))
                    return;
                dsp->dma_writew(dsp->dma_priv, dsp->record_buffer[dsp->record_pos_read + 1]);
                dsp->sb_16_length -= 2;
                dsp->ess_dma_counter += 4;
                dsp->record_pos_read += 2;
                dsp->record_pos_read &= 0xFFFF;
                break;

            default:
                break;
        }

        if (dsp->sb_16_length < 0 && !dsp->ess_playback_mode) {
            if (dsp->sb_16_autoinit)
                dsp->sb_16_length = dsp->sb_16_origlength = dsp->sb_16_autolen;
            else {
                dsp->sb_16_enable = 0;
                timer_disable(&dsp->input_timer);
                sb_finish_dma(dsp);
            }
            sb_irq(dsp, 0);
            dsp->ess_irq_generic = true;
        }
        if (dsp->ess_dma_counter > 0xffff) {
            if (dsp->ess_playback_mode) {
                if (!dsp->sb_16_autoinit) {
                    dsp->sb_16_enable = 0;
                    timer_disable(&dsp->input_timer);
                    sb_finish_dma(dsp);
                }
                if (ESSreg(0xB1) & 0x40) {
                    sb_irq(dsp, 0);
                    dsp->ess_irq_dmactr = true;
                }
            }
            uint32_t temp        = dsp->ess_dma_counter & 0xffff;
            dsp->ess_dma_counter = sb_ess_get_dma_counter(dsp);
            dsp->ess_dma_counter += temp;
        }
        processed = 1;
    }
    /* Assume this is direct mode */
    if (!processed) {
        dsp->record_pos_read += 2;
        dsp->record_pos_read &= 0xFFFF;
    }
}

void
sb_dsp_update(sb_dsp_t *dsp)
{
    if (dsp->muted) {
        dsp->sbdatl = 0;
        dsp->sbdatr = 0;
    }
    for (; dsp->pos < sound_pos_global; dsp->pos++) {
        dsp->buffer[dsp->pos * 2]     = dsp->sbdatl;
        dsp->buffer[dsp->pos * 2 + 1] = dsp->sbdatr;
    }
}

void
sb_dsp_close(UNUSED(sb_dsp_t *dsp))
{
    //
}
