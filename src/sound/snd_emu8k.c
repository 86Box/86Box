#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define _USE_MATH_DEFINES
#include <math.h>
#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/sound.h>
#include <86box/snd_emu8k.h>
#include <86box/timer.h>
#include <86box/plat_unused.h>

#if !defined FILTER_INITIAL && !defined FILTER_MOOG && !defined FILTER_CONSTANT
#if 0
#define FILTER_INITIAL
#endif
#    define FILTER_MOOG
#if 0
#define FILTER_CONSTANT
#endif
#endif

/* AWE32Emu: RESAMPLER_POINT3 was the default from 2026-09-09.
 *
 * Before that sinc was here, justified by winning on Hi-Octane. A run of
 * tests/tune.py on 29 pairs reversed that - on Hi-Octane sinc is the WORST
 * of all five variants (ho-tr1 3.421 / ho-tr3 4.290 / ho-tr5 2.989 against
 * 3.418 / 4.273 / 2.988 for point3). The only evidence for sinc fell with
 * it; on music all variants are equal within 0.6 %.
 *
 * So block 5 of AWETEST decided - noise retuned over 59 semitones, a test
 * built directly for the interpolation. Distance of the card's third-octave
 * spectrum from the render, each candidate with its own line in dB against
 * log f subtracted (the recording path has a tilt of ~2.6 dB/oct, which has
 * nothing to do with the interpolation and lies right at Nyquist):
 *
 *     point3  2.791 dB      above 4 kHz  1.898 dB
 *     cubic   2.862 dB                   1.946 dB
 *     sinc    4.162 dB                   2.537 dB
 *
 * Counter-signal: the "colour" metric in tune.py keeps sinc 1 % ahead
 * (2.1131 against 2.1358). Against 49 % on the test built for exactly this
 * question it does not hold. The others stay available by switching the
 * define. */
/* AWE32Emu 2026-09-27: RESAMPLER_BSPLINE4 is the default, measured on the
 * card's line out (see EMU8K_READ_INTERP_BSPLINE4); the choice of POINT3
 * above rested on the internal capture, which is not linear. */
#if !defined RESAMPLER_LINEAR && !defined RESAMPLER_CUBIC && !defined RESAMPLER_POINT3 && !defined RESAMPLER_SINC && !defined RESAMPLER_BSPLINE4
#    define RESAMPLER_BSPLINE4
#endif

#if 0
#define EMU8K_DEBUG_REGISTERS
#endif

char *PORT_NAMES[][8] = {
    /* Data 0 ( 0x620/0x622) */
    {
        "AWE_CPF",
        "AWE_PTRX",
        "AWE_CVCF",
        "AWE_VTFT",
        "Unk-620-4",
        "Unk-620-5",
        "AWE_PSST",
        "AWE_CSL",
    },
    /* Data 1 0xA20 */
    {
        "AWE_CCCA",
        0,
        /*
        "AWE_HWCF4"
        "AWE_HWCF5"
        "AWE_HWCF6"
        "AWE_HWCF7"
        "AWE_SMALR"
        "AWE_SMARR"
        "AWE_SMALW"
        "AWE_SMARW"
        "AWE_SMLD"
        "AWE_SMRD"
        "AWE_WC"
        "AWE_HWCF1"
        "AWE_HWCF2"
        "AWE_HWCF3"
        */
        0, //"AWE_INIT1",
        0, //"AWE_INIT3",
        "AWE_ENVVOL",
        "AWE_DCYSUSV",
        "AWE_ENVVAL",
        "AWE_DCYSUS",
    },
    /* Data 2 0xA22 */
    {
        "AWE_CCCA",
        0,
        0, //"AWE_INIT2",
        0, //"AWE_INIT4",
        "AWE_ATKHLDV",
        "AWE_LFO1VAL",
        "AWE_ATKHLD",
        "AWE_LFO2VAL",
    },
    /* Data 3 0xE20 */
    {
        "AWE_IP",
        "AWE_IFATN",
        "AWE_PEFE",
        "AWE_FMMOD",
        "AWE_TREMFRQ",
        "AWE_FM2FRQ2",
        0,
        0,
    },
};

enum {
    ENV_STOPPED = 0,
    ENV_DELAY   = 1,
    ENV_ATTACK  = 2,
    ENV_HOLD    = 3,
    // ENV_DECAY   = 4,
    ENV_SUSTAIN = 5,
    // ENV_RELEASE = 6,
    ENV_RAMP_DOWN = 7,
    ENV_RAMP_UP   = 8
};

static int random_helper = 0;
int        dmareadbit    = 0;
int        dmawritebit   = 0;

/* cubic and linear tables resolution. Note: higher than 10 does not improve the result. */
#define CUBIC_RESOLUTION_LOG 10
#define CUBIC_RESOLUTION     (1 << CUBIC_RESOLUTION_LOG)
/* cubic_table coefficients. */
static float cubic_table[CUBIC_RESOLUTION * 4];

/* conversion from current pitch to linear frequency change (in 32.32 fixed point). */
static int64_t freqtable[65536];
/* Conversion from initial attenuation to 16 bit unsigned lineal amplitude (currently only a way to update volume target register) */
static int32_t attentable[256];
/* Conversion from envelope dbs (once rigth shifted) (0 = 0dBFS, 65535 = -96dbFS and silence ) to 16 bit unsigned lineal amplitude,
 * to convert to current volume. (0 to 65536) */
static int32_t env_vol_db_to_vol_target[65537];
/* Same as above, but to convert amplitude (once rigth shifted) (0 to 65536) to db (0 = 0dBFS, 65535 = -96dbFS and silence ).
 * it is needed so that the delay, attack and hold phase can be added to initial attenuation and tremolo */
static int32_t env_vol_amplitude_to_db[65537];
/* Conversion from envelope herts (once right shifted) to octave . it is needed so that the delay, attack and hold phase can be
 * added to initial pitch ,lfos pitch , initial filter and lfo filter */
static int32_t env_mod_hertz_to_octave[65537];
/* Conversion from envelope amount to time in samples. */
static int32_t env_attack_to_samples[128];

/* This table has been generated using the following formula:
 * Get the amount of dBs that have to be added each sample to reach 96dBs in the amount
 * of time determined by the encoded value "i".
 *      float  d = 1.0/((env_decay_to_millis[i]/96.0)*44.1);
 *      int result = round(d*21845);
 * The multiplication by 21845 gives a minimum value of 1, and a maximum accumulated value of 1<<21
 * The accumulated value has to be converted to amplitude, and that can be done with the
 * env_vol_db_to_vol_target and shifting by 8
 * In other words, the unit of the table is the 1/21845th of a dB per sample frame, to be added or
 * substracted to the accumulating value_db of the envelope. */
/* AWE32Emu: the table above (from the linux driver) is replaced by the
 * decay/release table of the Creative drivers (SBAWE32.DRV ds:1650, same in
 * three driver generations): time for 100 dB = 47513 ms / divisor(rate - 1),
 * the divisor being the 7-bit "floating" code also used for attack. Filled in
 * emu8k_init() as Q16 value units per sample (1 << 21 = 96 dB). */
static int32_t env_decay_to_dbs_or_oct[128];

/* The table "env_decay_to_millis" is based on the table "decay_time_tbl" found in the freebsd/linux
 * AWE32 driver.
 * I tried calculating it using the instructions in awe32p10 from Judge Dredd, but the formula there
 * is wrong.
 *
 */
#if 0
static int32_t env_decay_to_millis[128] = {
       0, 45120, 22614, 15990, 11307, 9508, 7995, 6723, 5653, 5184, 4754, 4359, 3997, 3665, 3361, 3082,
    2828,  2765,  2648,  2535,  2428, 2325, 2226, 2132, 2042, 1955, 1872, 1793, 1717, 1644, 1574, 1507,
    1443,  1382,  1324,  1267,  1214, 1162, 1113, 1066,  978,  936,  897,  859,  822,  787,  754,  722,
     691,   662,   634,   607,   581,  557,  533,  510,  489,  468,  448,  429,  411,  393,  377,  361,
     345,   331,   317,   303,   290,  278,  266,  255,  244,  234,  224,  214,  205,  196,  188,  180,
     172,   165,   158,   151,   145,  139,  133,  127,  122,  117,  112,  107,  102,   98,   94,   90,
      86,    82,    79,    75,    72,   69,   66,   63,   61,   58,   56,   53,   51,   49,   47,   45,
      43,    41,    39,    37,    36,   34,   33,   31,   30,   29,   28,   26,   25,   24,   23,   22,
};
#endif

/* Table represeting the LFO waveform (signed 16bits with 32768 max int. >> 15 to move back to +/-1 range). */
static int32_t lfotable[65536];
/* Table to transform the speed parameter to emu8k_mem_internal_t range. */
static int64_t lfofreqtospeed[256];

/* LFO used for the chorus. a sine wave.(signed 16bits with 32768 max int. >> 15 to move back to +/-1 range). */
static double chortable[65536];

static const int REV_BUFSIZE_STEP = 242;

/* These lines come from the awe32faq, describing the NRPN control for the initial filter
 * where it describes a linear increment filter instead of an octave-incremented one.
 * NRPN LSB 21 (Initial Filter Cutoff)
 *     Range     : [0, 127]
 *     Unit      : 62Hz
 *     Filter cutoff from 100Hz to 8000Hz

 * This table comes from the awe32faq, describing the NRPN control for the filter Q.
 * I don't know if is meant to be interpreted as the actual measured output of the
 * filter or what. Especially, I don't understand the "low" and "high" ranges.
 * What is otherwise documented is that the Q ranges from 0dB to 24dB and the attenuation
 * is half of the Q ( i.e. for 12dB Q, attenuate the input signal with -6dB)
Coeff  Low Fc(Hz)Low Q(dB)High Fc(kHz)High Q(dB)DC Attenuation(dB)
* 0           92       5       Flat       Flat     -0.0
* 1           93       6       8.5        0.5      -0.5
* 2           94       8       8.3        1        -1.2
* 3           95       10      8.2        2        -1.8
* 4           96       11      8.1        3        -2.5
* 5           97       13      8.0        4        -3.3
* 6           98       14      7.9        5        -4.1
* 7           99       16      7.8        6        -5.5
* 8           100      17      7.7        7        -6.0
* 9           100      19      7.5        9        -6.6
* 10          100      20      7.4        10       -7.2
* 11          100      22      7.3        11       -7.9
* 12          100      23      7.2        13       -8.5
* 13          100      25      7.1        15       -9.3
* 14          100      26      7.1        16       -10.1
* 15          100      28      7.0        18       -11.0
*
* Attenuation as above, codified in amplitude.*/
static int32_t filter_atten[16] = {
    /* AWE32Emu: [7] 34792 -> 37690 (+0.7 dB). The card's passband falls
     * smoothly with Q (block 7 line out, cutoffs 144/192: -3.3/-4.2/-5.2 dB at
     * Q 6/7/8); upstream has a 1.4 dB step at Q 7 followed by 0.5 dB. */
    65536, 61869, 57079, 53269, 49145, 44820, 40877, 37690, 32845, 30653, 28607,
    26392, 24630, 22463, 20487, 18470
};

/*Coefficients for the filters for a defined Q and cutoff.*/
static int32_t filt_coeffs[16][256][3];

/* AWE32Emu: local addition, not upstream. Voice DSP measured on the tester's
 * card (AWETST25, internal capture and line out); the numbers come from
 * AWE32Emu/src/Emu8000Regs.h and Emu8000.cpp, where the measurements are
 * described. */

/* Volume envelope attack: amplitude at attack phase 0, 0.05 .. 1.0 (block 8,
 * rates 0x04..0x24). Silent up to ~0.1 T, roughly linear to ~0.75 at 0.8 T,
 * faster to 1.0. The small overshoot after the attack is not modelled. */
static const double emu8k_attack_shape[21] = {
    0.000, 0.000, 0.005, 0.058, 0.116, 0.203, 0.280, 0.326, 0.372, 0.433, 0.493,
    0.537, 0.580, 0.621, 0.662, 0.703, 0.744, 0.807, 0.899, 0.947, 1.000
};

/* Volume overshoot after the attack. On the card's line out the amplitude
 * rises to ~1.08 of the plateau right after the attack and returns to 1.0
 * within a fixed ~0.4 s, independent of the attack rate (block 8, rates
 * 0x08, 0x0C, 0x14, 0x1C, 0x24, 20 ms RMS windows relative to the end of the
 * note; the internal capture shows ~1.04 because it compresses loud
 * levels). Excess amplitude every 50 ms from the end of the attack. */
static const double emu8k_overshoot[9] = {
    0.075, 0.081, 0.067, 0.053, 0.040, 0.029, 0.018, 0.007, 0.0
};
#define EMU8K_OVERSHOOT_STEP 2205 /* samples per table entry (50 ms) */

static inline int32_t
emu8k_overshoot_q12(int32_t samples)
{
    if (samples < 0 || samples >= 8 * EMU8K_OVERSHOOT_STEP)
        return 4096;
    const int    i = samples / EMU8K_OVERSHOOT_STEP;
    const double f = (double) (samples % EMU8K_OVERSHOOT_STEP) / EMU8K_OVERSHOOT_STEP;
    return (int32_t) (4096.0 * (1.0 + emu8k_overshoot[i] + (emu8k_overshoot[i + 1] - emu8k_overshoot[i]) * f));
}

/* Attack phase (value_amp_hz, 1 << 21 = end) -> index into
 * env_vol_amplitude_to_db (0..65536). */
static inline int
emu8k_attack_amp_index(int32_t phase)
{
    double pos = (double) phase / (double) (1 << 21) * 20.0;
    if (pos < 0.0)
        pos = 0.0;
    if (pos > 20.0)
        pos = 20.0;
    int idx = (int) pos;
    if (idx > 19)
        idx = 19;
    const double amp = emu8k_attack_shape[idx] + (emu8k_attack_shape[idx + 1] - emu8k_attack_shape[idx]) * (pos - idx);
    return (int) (amp * 65536.0);
}

/* Modulation envelope attack is strongly convex (block 30): 1 - (1 - x)^13.5. */
static inline int32_t
emu8k_mod_attack_value(int32_t phase)
{
    double x = (double) phase / (double) (1 << 21);
    if (x < 0.0)
        x = 0.0;
    if (x > 1.0)
        x = 1.0;
    return (int32_t) ((1.0 - pow(1.0 - x, 13.5)) * (double) (1 << 21));
}

/* One decay/release step: ramp_amount_db_oct is Q16. */
static inline int32_t
emu8k_ramp_step(emu8k_envelope_t *env)
{
    const int64_t s = (int64_t) env->ramp_amount_db_oct + env->ramp_frac;
    env->ramp_frac  = (int32_t) (s & 0xFFFF);
    return (int32_t) (s >> 16);
}

/* Filter: Chamberlin state-variable filter at 44.1 kHz (blocks 6 and 7, 157
 * notes, 0.80 dB rms). Cutoff 101.81 Hz at register 0, 29.3843 cents per
 * register step, modulation in octaves (PEFE low byte +-6, FMMOD low byte +-3),
 * clamped to [101.81 Hz, fs/6]. Q = 0.931 * 10^(Q_reg * 1.5 / 20).
 * The slope was 1.175 dB from a global fit; the resonance peaks of block 7 on
 * the line out (peak over Q 0 minus passband change, cutoffs 96/144/192, which
 * removes the input attenuation) need 1.25/3.0/4.5/.../22.25 dB for Q 1..15,
 * i.e. 1.5 dB per step (fitq.py, +-0.5 dB). */
#define FILTER_CHAMBERLIN
#define EMU8K_CHAM_BASE_HZ  101.81
#define EMU8K_CHAM_CENTS    29.3843
#define EMU8K_CHAM_Q0       0.931
#define EMU8K_CHAM_DB_PER_Q 1.5

/* Resonance near the top of the range: at cutoffs 248, 254 and 255 (6.9 kHz
 * and the fs/6 clamp) the card's peaks are much lower. Fitted Q over Q 0 in
 * dB for register Q 0..15 there (internal capture, fitq_top.py, +-0.5 dB).
 * Between 2.65 kHz (cutoff 192, where 1.5 dB per step holds) and 6.87 kHz
 * (cutoff 248) the two are blended linearly in octaves; no cutoff between
 * them was measured. */
static const double emu8k_cham_top_db[16] = {
    0.0, 0.5, 1.25, 1.75, 2.5, 3.25, 4.25, 5.0, 6.25, 7.25, 9.0, 10.75, 12.5, 13.75, 15.25, 16.75
};
#define EMU8K_CHAM_TOP_LO_HZ 2653.0
#define EMU8K_CHAM_TOP_HI_HZ 6870.0

static inline double
emu8k_cham_q_db(int q, double fc)
{
    double t = log2(fc / EMU8K_CHAM_TOP_LO_HZ) / log2(EMU8K_CHAM_TOP_HI_HZ / EMU8K_CHAM_TOP_LO_HZ);
    if (t < 0.0)
        t = 0.0;
    if (t > 1.0)
        t = 1.0;
    return (1.0 - t) * q * EMU8K_CHAM_DB_PER_Q + t * emu8k_cham_top_db[q];
}

/* Equalizer (block 35): RBJ shelves with slope 0.5 fitted to all 24 positions. */
typedef struct emu8k_eq_shelf_t {
    double gain_db, f0;
} emu8k_eq_shelf_t;

static const emu8k_eq_shelf_t emu8k_eq_treble[12] = {
    { -11.9, 1811 }, { -8.5, 1542 }, { -5.9, 1313 }, { -4.1, 1211 },
    {  -1.2, 1031 }, {  0.0, 1000 }, {  1.9, 1261 }, {  3.5, 1671 },
    {  5.97, 2231 }, { 7.90, 2457 }, { 9.69, 2188 }, { 11.16, 2231 }
};
static const emu8k_eq_shelf_t emu8k_eq_bass[12] = {
    { -11.9, 224 }, { -8.4, 273 }, { -5.9, 321 }, { -4.0, 362 },
    {  -1.1, 426 }, {  0.0, 100 }, {  2.0, 500 }, {  3.5, 564 },
    {   6.0, 636 }, {  8.0, 718 }, {  9.6, 778 }, { 12.0, 914 }
};
/* What the drivers write into the EQ slots (alsa_emu8000_init.c bass_parm,
 * treble_parm): bass -> INIT4 0x01, 0x11; treble -> INIT3 0x11, 0x13, 0x1B,
 * INIT4 0x07, 0x0B, 0x0D, 0x17, 0x19. */
static const uint16_t emu8k_eq_bass_parm[12][2] = {
    { 0xD26A, 0xD36A }, { 0xD25B, 0xD35B }, { 0xD24C, 0xD34C }, { 0xD23D, 0xD33D },
    { 0xD21F, 0xD31F }, { 0xC208, 0xC308 }, { 0xC219, 0xC319 }, { 0xC22A, 0xC32A },
    { 0xC24C, 0xC34C }, { 0xC26E, 0xC36E }, { 0xC248, 0xC384 }, { 0xC26A, 0xC36A }
};
static const uint16_t emu8k_eq_treble_parm[12][8] = {
    { 0x821E, 0xC26A, 0x031E, 0xC36A, 0x021E, 0xD208, 0x831E, 0xD308 },
    { 0x821E, 0xC25B, 0x031E, 0xC35B, 0x021E, 0xD208, 0x831E, 0xD308 },
    { 0x821E, 0xC24C, 0x031E, 0xC34C, 0x021E, 0xD208, 0x831E, 0xD308 },
    { 0x821E, 0xC23D, 0x031E, 0xC33D, 0x021E, 0xD208, 0x831E, 0xD308 },
    { 0x821E, 0xC21F, 0x031E, 0xC31F, 0x021E, 0xD208, 0x831E, 0xD308 },
    { 0x821E, 0xD208, 0x031E, 0xD308, 0x021E, 0xD208, 0x831E, 0xD308 },
    { 0x821E, 0xD208, 0x031E, 0xD308, 0x021D, 0xD219, 0x831D, 0xD319 },
    { 0x821E, 0xD208, 0x031E, 0xD308, 0x021C, 0xD22A, 0x831C, 0xD32A },
    { 0x821E, 0xD208, 0x031E, 0xD308, 0x021A, 0xD24C, 0x831A, 0xD34C },
    { 0x821E, 0xD208, 0x031E, 0xD308, 0x0219, 0xD26E, 0x8319, 0xD36E },
    { 0x821D, 0xD219, 0x031D, 0xD319, 0x0219, 0xD26E, 0x8319, 0xD36E },
    { 0x821C, 0xD22A, 0x031C, 0xD32A, 0x0219, 0xD26E, 0x8319, 0xD36E }
};

/* The DOS game driver writes the low byte with its nibbles swapped (C208 ->
 * C280); on the card that sounds the same (block 35). */
static int
emu8k_eq_word_match(uint16_t reg, uint16_t table)
{
    const uint16_t swapped = (uint16_t) ((reg & 0xFF00) | ((reg & 0x0F) << 4) | ((reg & 0xF0) >> 4));
    return reg == table || swapped == table;
}

static void
emu8k_eq_design(double *q, const emu8k_eq_shelf_t *s, int high)
{
    const double A     = pow(10.0, s->gain_db / 40.0);
    const double w0    = 2.0 * M_PI * s->f0 / 44100.0;
    const double cw    = cos(w0);
    const double alpha = sin(w0) / 2.0 * sqrt((A + 1.0 / A) * (1.0 / 0.5 - 1.0) + 2.0);
    const double sq    = 2.0 * sqrt(A) * alpha;
    double       b0, b1, b2, a0, a1, a2;
    if (high) {
        b0 = A * ((A + 1) + (A - 1) * cw + sq);
        b1 = -2 * A * ((A - 1) + (A + 1) * cw);
        b2 = A * ((A + 1) + (A - 1) * cw - sq);
        a0 = (A + 1) - (A - 1) * cw + sq;
        a1 = 2 * ((A - 1) - (A + 1) * cw);
        a2 = (A + 1) - (A - 1) * cw - sq;
    } else {
        b0 = A * ((A + 1) - (A - 1) * cw + sq);
        b1 = 2 * A * ((A - 1) - (A + 1) * cw);
        b2 = A * ((A + 1) - (A - 1) * cw - sq);
        a0 = (A + 1) + (A - 1) * cw + sq;
        a1 = -2 * ((A - 1) + (A + 1) * cw);
        a2 = (A + 1) + (A - 1) * cw - sq;
    }
    q[0] = b0 / a0;
    q[1] = b1 / a0;
    q[2] = b2 / a0;
    q[3] = a1 / a0;
    q[4] = a2 / a0;
}

/* Reverb (blocks 22): the chip has no preset register; the drivers write the
 * 28 reverb words of alsa_emu8000_init.c reverb_parm (same data as
 * SBAWE32.DRV ds:0x1A12) into these INIT slots. The preset is recognised from
 * them (at least 24 of 28 words) and drives a comb/allpass network whose room
 * size, damping, pre-delay and output gain were fitted per preset to the card
 * (internal capture, tails 0.15-0.6 s within 1.4 dB, 0.6-1.5 s within 0.7 dB). */
#define REVERB_FITTED
static const uint8_t emu8k_rv_slots[28][2] = {
    { 1, 0x03 }, { 1, 0x05 }, { 4, 0x1F }, { 1, 0x07 }, { 2, 0x14 }, { 2, 0x16 }, { 1, 0x0F },
    { 1, 0x17 }, { 1, 0x1F }, { 2, 0x07 }, { 2, 0x0F }, { 2, 0x17 }, { 2, 0x1D }, { 2, 0x1F },
    { 3, 0x01 }, { 3, 0x03 }, { 1, 0x09 }, { 1, 0x0B }, { 1, 0x11 }, { 1, 0x13 }, { 1, 0x19 },
    { 1, 0x1B }, { 2, 0x01 }, { 2, 0x03 }, { 2, 0x09 }, { 2, 0x0B }, { 2, 0x11 }, { 2, 0x13 }
};
static const uint16_t emu8k_rv_parm[8][28] = {
    { 0xB488, 0xA450, 0x9550, 0x84B5, 0x383A, 0x3EB5, 0x72F4, 0x72A4, 0x7254, 0x7204, 0x7204, 0x7204, 0x4416, 0x4516,
      0xA490, 0xA590, 0x842A, 0x852A, 0x842A, 0x852A, 0x8429, 0x8529, 0x8429, 0x8529, 0x8428, 0x8528, 0x8428, 0x8528 },
    { 0xB488, 0xA458, 0x9558, 0x84B5, 0x383A, 0x3EB5, 0x7284, 0x7254, 0x7224, 0x7224, 0x7254, 0x7284, 0x4448, 0x4548,
      0xA440, 0xA540, 0x842A, 0x852A, 0x842A, 0x852A, 0x8429, 0x8529, 0x8429, 0x8529, 0x8428, 0x8528, 0x8428, 0x8528 },
    { 0xB488, 0xA460, 0x9560, 0x84B5, 0x383A, 0x3EB5, 0x7284, 0x7254, 0x7224, 0x7224, 0x7254, 0x7284, 0x4416, 0x4516,
      0xA490, 0xA590, 0x842C, 0x852C, 0x842C, 0x852C, 0x842B, 0x852B, 0x842B, 0x852B, 0x842A, 0x852A, 0x842A, 0x852A },
    { 0xB488, 0xA470, 0x9570, 0x84B5, 0x383A, 0x3EB5, 0x7284, 0x7254, 0x7224, 0x7224, 0x7254, 0x7284, 0x4448, 0x4548,
      0xA440, 0xA540, 0x842B, 0x852B, 0x842B, 0x852B, 0x842A, 0x852A, 0x842A, 0x852A, 0x8429, 0x8529, 0x8429, 0x8529 },
    { 0xB488, 0xA470, 0x9570, 0x84B5, 0x383A, 0x3EB5, 0x7254, 0x7234, 0x7224, 0x7254, 0x7264, 0x7294, 0x44C3, 0x45C3,
      0xA404, 0xA504, 0x842A, 0x852A, 0x842A, 0x852A, 0x8429, 0x8529, 0x8429, 0x8529, 0x8428, 0x8528, 0x8428, 0x8528 },
    { 0xB4FF, 0xA470, 0x9570, 0x84B5, 0x383A, 0x3EB5, 0x7234, 0x7234, 0x7234, 0x7234, 0x7234, 0x7234, 0x4448, 0x4548,
      0xA440, 0xA540, 0x842A, 0x852A, 0x842A, 0x852A, 0x8429, 0x8529, 0x8429, 0x8529, 0x8428, 0x8528, 0x8428, 0x8528 },
    { 0xB4FF, 0xA470, 0x9500, 0x84B5, 0x333A, 0x39B5, 0x7204, 0x7204, 0x7204, 0x7204, 0x7204, 0x72F4, 0x4400, 0x4500,
      0xA4FF, 0xA5FF, 0x8420, 0x8520, 0x8420, 0x8520, 0x8420, 0x8520, 0x8420, 0x8520, 0x8420, 0x8520, 0x8420, 0x8520 },
    { 0xB4FF, 0xA490, 0x9590, 0x8474, 0x333A, 0x39B5, 0x7204, 0x7204, 0x7204, 0x7204, 0x7204, 0x72F4, 0x4400, 0x4500,
      0xA4FF, 0xA5FF, 0x8420, 0x8520, 0x8420, 0x8520, 0x8420, 0x8520, 0x8420, 0x8520, 0x8420, 0x8520, 0x8420, 0x8520 }
};
/* size, damp, pre-delay ms, output gain per preset (room 1-3, hall 1-2,
 * plate, delay, panning delay). */
/* AWE32Emu: presets 0-5 fitted 2026-09-26 on the card's LINE OUT (AWETST27
 * block 22, tick with send 255; the right output carries the reverb return
 * alone): size, output gain and early reflections against the windows 0-25,
 * 25-60, 60-150, 150-300, 300-600, 600-1200 ms after the tick, all within
 * ~1 dB (25-60 ms of presets 4/5 -2.5/-3.3 dB). The internal capture used
 * before reads these presets 2-4 dB too high (the +6.5 dB of its right
 * channel holds only for the echo presets 6/7), and preset 0 decays far
 * slower than it looked there. Preset 1's line tick is spoilt by a foreign
 * sound; its target is the internal capture minus the mean internal-line
 * difference of the other presets.
 * Damping 2026-09-27 from the line out of AWETST28 block 45 (noise burst,
 * send 255): the card's reverb is flat from 200 Hz to 4 kHz for all presets
 * and loses only the top in the loop (-2.3 dB at 8 kHz / -5.7 at 12.8 kHz in
 * 550-900 ms, -7 / -18 dB in 900-1700 ms); a one-pole of 0.03-0.10 matches
 * that within 0.8-1.5 dB (the damping of 0.6 guessed before was 17-20 dB
 * too dark at 4-8 kHz). Size, gain and early reflections refitted on the
 * ticks afterwards: windows within 0.3-1.3 dB, the tail keeps a +-1.5 dB
 * curvature (the card decays faster at first, slower later). */
static const float emu8k_rv_room[8][4] = {
    { 0.613f, 0.045f, 0.0f, 0.347f }, { 0.583f, 0.08f, 0.0f, 0.587f }, { 0.647f, 0.03f, 0.0f, 0.620f },
    { 0.692f, 0.09f, 0.0f, 0.849f }, { 0.672f, 0.06f, 0.0f, 0.988f }, { 0.694f, 0.10f, 0.0f, 0.912f },
    { 0.0f, 0.0f, 200.0f, 0.38f }, { 0.7f, 0.0f, 150.0f, 0.54f }
};

/* AWE32Emu: presets 6 (Delay) and 7 (Panning Delay) are no rooms but echo
 * trains. Card, block 22 tick with send 255, envelope cross-correlated with
 * the dry tick: echoes every 117 ms from ~115 ms on, no pre-delay (6: 113
 * 231 348 464 581 ms in both channels, -7.3 dB per echo; 7: L 113, R 231,
 * L 351, R 468, L 587, R 704, L 823 ms, -4.9 dB per echo, alternating). The right channel of that capture is
 * 6-7 dB lower for both presets alike, so it is taken as the capture path
 * and both channels get the same gain here. */
#define EMU8K_RV_ECHO_PRE_MS  0.0f
/* AWE32Emu: echo presets 6 (Delay) and 7 (Panning Delay), AWETST25 block
 * 22. Preset 6: echoes in both channels every 116.75 ms, R first at 111.25 ms,
 * L 3.5 ms later. Preset 7: L at 115.5 ms, R 115.5 ms later, period 236 ms.
 * Levels (line out, AWETST27 block 22): the first echo +5.4 dB against the
 * dry tick in both presets, then the geometric series; all echoes within
 * 0.5 dB. (The internal capture had shown a weaker first echo - it
 * compresses loud signals.) */
typedef struct {
    int   per;
    int   d[2];
    float fb;
    float c[2];
    float g[2];
} emu8k_rv_echo_parm_t;
static const emu8k_rv_echo_parm_t emu8k_rv_echo_parm[2] = {
    { 5149, { 5060, 4906 }, 0.432f, { 1.0f, 1.0f }, { 0.945f, 0.945f } },
    { 10408, { 5094, 10187 }, 0.313f, { 1.0f, 1.0f }, { 0.915f, 0.496f } }
};

/* AWE32Emu: early reflections of the room presets 0-5, from the card's
 * response to a 10 ms noise click (AWETST28 block 45, line out, the right
 * output carrying the reverb alone): the power envelope in 2 ms bins minus
 * the comb part, deconvolved by the click (non-negative least squares). The
 * card answers at once (1 ms) and then in bursts about 8-9 ms apart - 1,
 * 5-7, 15, 23-25, 33-35, 43 ms - fading into the combs; the same shape for
 * all six presets, only the strength differs. The right channel takes the
 * measured times, the left ones 0.3-1 ms later. Weights = square roots of
 * the power fractions; the preset gains set the click envelope at 2-18 ms
 * (card vs render, 2 ms bins 0-40 ms: mean difference 3.2 dB; the earlier
 * 11 taps at 4-51 ms, fitted on a 30 ms tick, put the energy at 25-50 ms).
 * The card's click response is spectrally flat in every window, so the
 * tick (a single 735 Hz tone) sees the reflections interfere: its 0-25 ms
 * window is 3-4 dB below what the broadband power gives, and fitting the
 * taps on it had skewed them. The late part (combs) stays fitted on the
 * tick, where the dense tail averages the interference out. */
static const int   emu8k_rv_er_tap[2][17] = {
    { 57, 250, 327, 432, 596, 702, 778, 1030, 1135, 1388, 1493, 1570, 1646, 1927, 2003, 2197, 2273 },
    { 44, 220, 309, 397, 573, 662, 750, 1014, 1102, 1367, 1455, 1544, 1632, 1896, 1984, 2161, 2249 }
};
static const float emu8k_rv_er_w[17] = { 0.4528f, 0.3256f, 0.3362f, 0.1924f, 0.1673f, 0.4171f, 0.1225f, 0.2145f, 0.3240f, 0.1304f, 0.1673f, 0.1871f, 0.1265f, 0.1897f, 0.1049f, 0.0949f, 0.0837f };
static const float emu8k_rv_er_gain[8] = { 0.639f, 0.417f, 0.409f, 0.408f, 0.324f, 0.435f, 0.0f, 0.0f };

static void
emu8k_rv_set_preset(emu8k_t *emu8k, int preset)
{
    const float *r       = emu8k_rv_room[preset];
    emu8k->rv_preset     = preset;
    emu8k->rv_er_gain    = emu8k_rv_er_gain[preset];
    emu8k->rv_echo_mode  = (preset == 6) ? 1 : (preset == 7) ? 2 : 0;
    if (emu8k->rv_echo_mode) {
        const emu8k_rv_echo_parm_t *e = &emu8k_rv_echo_parm[preset - 6];
        emu8k->rv_echo_per  = e->per;
        emu8k->rv_echo_fb   = e->fb;
        for (int ch = 0; ch < 2; ch++) {
            emu8k->rv_echo_d[ch] = e->d[ch];
            emu8k->rv_echo_c[ch] = e->c[ch];
            emu8k->rv_echo_g[ch] = e->g[ch];
        }
        emu8k->rv_echo_pos = 0;
        memset(emu8k->rv_echo_x, 0, sizeof(emu8k->rv_echo_x));
        memset(emu8k->rv_echo_z, 0, sizeof(emu8k->rv_echo_z));
    }
    emu8k->rv_feedback   = 0.7f + r[0] * 0.28f;
    emu8k->rv_damp       = r[1];
    emu8k->rv_in_gain    = 1.0f - emu8k->rv_feedback; /* comb bank DC gain back to 1 */
    emu8k->rv_out_gain   = r[3];
    const int len        = (int) ((emu8k->rv_echo_mode ? EMU8K_RV_ECHO_PRE_MS : r[2]) * 44100.0f / 1000.0f);
    if (len != emu8k->rv_pre_len) {
        emu8k->rv_pre_len = len;
        emu8k->rv_pre_pos = 0;
        memset(emu8k->rv_pre, 0, sizeof(emu8k->rv_pre));
    }
}

static void
emu8k_rv_init(emu8k_t *emu8k)
{
    static const int comb[8]    = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    static const int allpass[4] = { 556, 441, 341, 225 };
    for (int ch = 0; ch < 2; ch++) {
        const int spread = ch ? 23 : 0;
        for (int i = 0; i < 8; i++)
            emu8k->rv_comb_len[ch][i] = comb[i] + spread;
        for (int i = 0; i < 4; i++)
            emu8k->rv_ap_len[ch][i] = allpass[i] + spread;
    }
    emu8k->rv_pre_len = -1;
    emu8k_rv_set_preset(emu8k, 4); /* hall 2, the driver default */
}

static void
emu8k_rv_decode(emu8k_t *emu8k)
{
    int best = -1, best_hits = 0;
    for (int p = 0; p < 8; p++) {
        int hits = 0;
        for (int i = 0; i < 28; i++) {
            const int slot = emu8k_rv_slots[i][1];
            uint16_t  v;
            switch (emu8k_rv_slots[i][0]) {
                case 1: v = emu8k->init1[slot]; break;
                case 2: v = emu8k->init2[slot]; break;
                case 3: v = emu8k->init3[slot]; break;
                default: v = emu8k->init4[slot]; break;
            }
            hits += (v == emu8k_rv_parm[p][i]);
        }
        if (hits > best_hits) {
            best_hits = hits;
            best      = p;
        }
    }
    if (best_hits >= 24 && best != emu8k->rv_preset)
        emu8k_rv_set_preset(emu8k, best);
}

static void
emu8k_work_reverb_fitted(emu8k_t *emu8k, int32_t *inbuf, int32_t *outbuf, int count)
{
    emu8k_rv_decode(emu8k);
    for (int n = 0; n < count; n++) {
        float in = (float) inbuf[n] / 32768.0f;
        if (emu8k->rv_pre_len > 0) {
            const float delayed               = emu8k->rv_pre[emu8k->rv_pre_pos];
            emu8k->rv_pre[emu8k->rv_pre_pos] = in;
            if (++emu8k->rv_pre_pos >= emu8k->rv_pre_len)
                emu8k->rv_pre_pos = 0;
            in = delayed;
        }
        if (emu8k->rv_echo_mode) {
            /* x = input history, z(t) = fb * (x(t - per) + z(t - per)) = all
             * echoes after the first; channel output c*x(t - d) + z(t - d).
             * Panning delay: R is the odd echoes of the L train. */
            const int pos = emu8k->rv_echo_pos;
            const int old = (pos - emu8k->rv_echo_per) & 16383;
            emu8k->rv_echo_z[pos] = emu8k->rv_echo_fb * (emu8k->rv_echo_x[old] + emu8k->rv_echo_z[old]);
            emu8k->rv_echo_x[pos] = in;
            for (int ch = 0; ch < 2; ch++) {
                const int   p = (pos - emu8k->rv_echo_d[ch]) & 16383;
                const float v = emu8k->rv_echo_c[ch] * emu8k->rv_echo_x[p] + emu8k->rv_echo_z[p];
                outbuf[2 * n + ch] += (int32_t) (v * emu8k->rv_echo_g[ch] * 32768.0f);
            }
            emu8k->rv_echo_pos = (pos + 1) & 16383;
            continue;
        }
        const float scaled = in * emu8k->rv_in_gain;
        float       er[2]  = { 0.0f, 0.0f };
        if (emu8k->rv_er_gain > 0.0f) {
            emu8k->rv_er[emu8k->rv_er_pos] = in;
            for (int ch = 0; ch < 2; ch++) {
                float sum = 0.0f;
                for (int t = 0; t < 17; t++) {
                    int p = emu8k->rv_er_pos - emu8k_rv_er_tap[ch][t];
                    if (p < 0)
                        p += 2400;
                    sum += emu8k->rv_er[p] * emu8k_rv_er_w[t];
                }
                er[ch] = sum * emu8k->rv_er_gain;
            }
            if (++emu8k->rv_er_pos >= 2400)
                emu8k->rv_er_pos = 0;
        }
        for (int ch = 0; ch < 2; ch++) {
            float acc = 0.0f;
            for (int i = 0; i < 8; i++) {
                float      *line = emu8k->rv_comb[ch][i];
                const int   pos  = emu8k->rv_comb_pos[ch][i];
                const float out  = line[pos];
                emu8k->rv_comb_store[ch][i] = out * (1.0f - emu8k->rv_damp) + emu8k->rv_comb_store[ch][i] * emu8k->rv_damp;
                line[pos] = scaled + emu8k->rv_comb_store[ch][i] * emu8k->rv_feedback;
                emu8k->rv_comb_pos[ch][i] = (pos + 1) % emu8k->rv_comb_len[ch][i];
                acc += out;
            }
            acc *= 0.125f;
            for (int i = 0; i < 4; i++) {
                float      *line = emu8k->rv_ap[ch][i];
                const int   pos  = emu8k->rv_ap_pos[ch][i];
                const float buf  = line[pos];
                line[pos]        = acc + buf * 0.5f;
                emu8k->rv_ap_pos[ch][i] = (pos + 1) % emu8k->rv_ap_len[ch][i];
                acc              = buf - acc;
            }
            outbuf[2 * n + ch] += (int32_t) ((acc * emu8k->rv_out_gain + er[ch]) * 32768.0f);
        }
    }
}

#define READ16_SWITCH(addr, var)          \
    switch ((addr) &2) {                  \
        case 0:                           \
            ret = (var) &0xffff;          \
            break;                        \
        case 2:                           \
            ret = ((var) >> 16) & 0xffff; \
            break;                        \
    }

#define WRITE16_SWITCH(addr, var, val)                \
    switch ((addr) &2) {                              \
        case 0:                                       \
            var = (var & 0xffff0000) | (val);         \
            break;                                    \
        case 2:                                       \
            var = (var & 0x0000ffff) | ((val) << 16); \
            break;                                    \
    }

#ifdef EMU8K_DEBUG_REGISTERS
uint32_t dw_value    = 0;
uint32_t last_read   = 0;
uint32_t last_write  = 0;
uint32_t rep_count_r = 0;
uint32_t rep_count_w = 0;

#    define READ16(addr, var)                                                                                                      \
        READ16_SWITCH(addr, var)                                                                                                   \
        {                                                                                                                          \
            const char *name = 0;                                                                                                  \
            switch (addr & 0xF02) {                                                                                                \
                case 0x600:                                                                                                        \
                case 0x602:                                                                                                        \
                    name = PORT_NAMES[0][emu8k->cur_reg];                                                                          \
                    break;                                                                                                         \
                case 0xA00:                                                                                                        \
                    name = PORT_NAMES[1][emu8k->cur_reg];                                                                          \
                    break;                                                                                                         \
                case 0xA02:                                                                                                        \
                    name = PORT_NAMES[2][emu8k->cur_reg];                                                                          \
                    break;                                                                                                         \
            }                                                                                                                      \
            if (name == 0) {                                                                                                       \
                /*emu8k_log("EMU8K READ %04X-%02X(%d): %04X\n",addr,(emu8k->cur_reg)<<5|emu8k->cur_voice, emu8k->cur_voice,ret);*/ \
            } else {                                                                                                               \
                emu8k_log("EMU8K READ %s(%d) (%d): %04X\n", name, (addr & 0x2), emu8k->cur_voice, ret);                            \
            }                                                                                                                      \
        }
#    define WRITE16(addr, var, val)                                                                                                 \
        WRITE16_SWITCH(addr, var, val)                                                                                              \
        {                                                                                                                           \
            const char *name = 0;                                                                                                   \
            switch (addr & 0xF02) {                                                                                                 \
                case 0x600:                                                                                                         \
                case 0x602:                                                                                                         \
                    name = PORT_NAMES[0][emu8k->cur_reg];                                                                           \
                    break;                                                                                                          \
                case 0xA00:                                                                                                         \
                    name = PORT_NAMES[1][emu8k->cur_reg];                                                                           \
                    break;                                                                                                          \
                case 0xA02:                                                                                                         \
                    name = PORT_NAMES[2][emu8k->cur_reg];                                                                           \
                    break;                                                                                                          \
            }                                                                                                                       \
            if (name == 0) {                                                                                                        \
                /*emu8k_log("EMU8K WRITE %04X-%02X(%d): %04X\n",addr,(emu8k->cur_reg)<<5|emu8k->cur_voice,emu8k->cur_voice, val);*/ \
            } else {                                                                                                                \
                emu8k_log("EMU8K WRITE %s(%d) (%d): %04X\n", name, (addr & 0x2), emu8k->cur_voice, val);                            \
            }                                                                                                                       \
        }

#else
#    define READ16(addr, var)       READ16_SWITCH(addr, var)
#    define WRITE16(addr, var, val) WRITE16_SWITCH(addr, var, val)
#endif // EMU8K_DEBUG_REGISTERS

#ifdef ENABLE_EMU8K_LOG
int emu8k_do_log = ENABLE_EMU8K_LOG;

static void
emu8k_log(const char *fmt, ...)
{
    va_list ap;

    if (emu8k_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define emu8k_log(fmt, ...)
#endif

static uint32_t *
emu8k_voice_dword(emu8k_voice_t *voice, unsigned reg)
{
    switch (reg) {
        case EMU8K_VOICE_CPF:  return &voice->cpf;
        case EMU8K_VOICE_PTRX: return &voice->ptrx;
        case EMU8K_VOICE_CVCF: return &voice->cvcf;
        case EMU8K_VOICE_VTFT: return &voice->vtft;
        case EMU8K_VOICE_Z2:   return &voice->z2;
        case EMU8K_VOICE_Z1:   return &voice->z1;
        case EMU8K_VOICE_PSST: return &voice->psst;
        case EMU8K_VOICE_CSL:  return &voice->csl;
        case EMU8K_VOICE_CCCA: return &voice->ccca;
        default:              return NULL;
    }
}

uint32_t
emu8k_voice_read(emu8k_voice_t *voice, unsigned reg)
{
    const uint32_t *value = emu8k_voice_dword(voice, reg);
    return value ? *value : UINT32_MAX;
}

static void
emu8k_voice_sync(emu8k_voice_t *voice, unsigned reg, int high)
{
    switch (reg) {
        case EMU8K_VOICE_PSST:
            voice->loop_start.int_address = voice->psst & EMU8K_MEM_ADDRESS_MASK;
            if (high) {
                voice->vol_l = voice->psst_pan;
                voice->vol_r = 255 - voice->psst_pan;
            }
            break;
        case EMU8K_VOICE_CSL:
            voice->loop_end.int_address = voice->csl & EMU8K_MEM_ADDRESS_MASK;
            break;
        case EMU8K_VOICE_CCCA:
            voice->addr.int_address = voice->ccca & EMU8K_MEM_ADDRESS_MASK;
            voice->filterq_idx = CCCA_FILTQ_GET(voice->ccca);
            voice->filt_att = filter_atten[voice->filterq_idx];
            break;
        default:
            break;
    }
}

void
emu8k_voice_write(emu8k_voice_t *voice, unsigned reg, uint32_t val)
{
    uint32_t *value = emu8k_voice_dword(voice, reg);
    if (value) {
        *value = val;
        emu8k_voice_sync(voice, reg, 1);
    }
}

static void
emu8k_voice_set_ip(emu8k_voice_t *voice, uint16_t val)
{
    voice->ip              = val;
    voice->ptrx_pit_target = freqtable[val] >> 18;
}

static void
emu8k_voice_set_ifatn(emu8k_voice_t *voice, uint16_t val)
{
    voice->ifatn              = val;
    voice->initial_att        = ((int32_t) voice->ifatn_attenuation << 21) / 0xFF;
    voice->vtft_vol_target    = attentable[voice->ifatn_attenuation];
    voice->initial_filter     = ((int32_t) voice->ifatn_init_filter << 21) / 0xFF;
    voice->vtft_filter_target = voice->ifatn_init_filter == 0xFF ? 0xFFFF : voice->initial_filter >> 5;
}

static inline void
emu8k_update_voice_address(emu8k_voice_t *voice)
{
    voice->ccca               = ((uint32_t) voice->ccca_qcontrol << 24) | voice->addr.int_address;
    voice->cpf_curr_frac_addr = voice->addr.fract_address;
}

/* The PCI host uses both loop crossings and can advance past a short loop
   more than once in a tick. Preserve the ISA oscillator's single-wrap rule. */
static inline unsigned
emu8k_advance_voice(emu8k_voice_t *voice, uint64_t step, int pci)
{
    const uint64_t previous = voice->addr.addr;
    voice->addr.addr += step;
    voice->cpf_curr_pitch = voice->ptrx_pit_target;
    if (!pci) {
        if (voice->addr.addr >= voice->loop_end.addr) {
            voice->addr.int_address -= voice->loop_end.int_address - voice->loop_start.int_address;
            voice->addr.int_address &= EMU8K_MEM_ADDRESS_MASK;
        }
        return 0;
    }

    unsigned crossed = 0;
    if (previous < voice->loop_start.addr && voice->addr.addr >= voice->loop_start.addr)
        crossed |= EMU8K_LOOP_START;
    if (voice->addr.addr >= voice->loop_end.addr) {
        crossed |= EMU8K_LOOP_END;
        voice->addr.int_address = voice->loop_start.int_address +
            (voice->addr.int_address - voice->loop_end.int_address) %
            (voice->loop_end.int_address - voice->loop_start.int_address);
    }
    return crossed;
}

void
emu8k_reset_pci_voices(emu8k_t *emu8k)
{
    for (unsigned i = 0; i < EMU8K_VOICE_COUNT; i++) {
        if (emu8k->pci_voices & (1u << i))
            memset(&emu8k->voice[i], 0, sizeof(emu8k->voice[i]));
    }
    emu8k->pci_voices = 0;
}

static inline int16_t
EMU8K_READ(const emu8k_t *emu8k, uint32_t addr)
{
    register const emu8k_mem_pointers_t addrmem = { { addr } };
    return emu8k->ram_pointers[addrmem.hb_address][addrmem.lw_address];
}

static inline int16_t
emu8k_ram_sample(const void *context, uint32_t addr)
{
    return EMU8K_READ(context, addr);
}

#ifdef RESAMPLER_LINEAR
static inline int16_t
EMU8K_READ_INTERP_LINEAR(emu8k_sample_read_t read_sample, const void *context, uint32_t int_addr, uint16_t fract)
{
    /* The interpolation in AWE32 used a so-called patented 3-point interpolation
     * ( I guess some sort of spline having one point before and one point after).
     * Also, it has the consequence that the playback is delayed by one sample.
     * I simulate the "one sample later" than the address with addr+1 and addr+2
     * instead of +0 and +1 */
    int16_t dat1 = read_sample(context, int_addr + 1);
    int32_t dat2 = read_sample(context, int_addr + 2);
    dat1 += ((dat2 - (int32_t) dat1) * fract) >> 16;
    return dat1;
}
#endif

/* AWE32Emu: local addition, not upstream. Quadratic (Lagrange)
 * interpolation over three points - what the AWE32 documentation describes
 * as "3 Point sample interpolation" (Vu, Un-official AWE32 Programming
 * Guide, 1995). Measured against 20 recording/MIDI pairs from real hardware
 * (AWE32Emu tests/tune.py): average score 5.937 against 6.025 for the cubic
 * (Catmull-Rom) interpolation above.
 * Points 1, 2, 3 (not 0, 1, 2) because of the same interpolator offset as
 * in LINEAR and CUBIC above ("actual audio location is the point 1 word
 * higher due to interpolation offset"). */
static inline int32_t
EMU8K_READ_INTERP_POINT3(emu8k_sample_read_t read_sample, const void *context, uint32_t int_addr, uint16_t fract)
{
    const float  g    = (float) fract / 65536.0f;
    const int32_t a0  = read_sample(context, int_addr + 1);
    const int32_t a1  = read_sample(context, int_addr + 2);
    const int32_t a2  = read_sample(context, int_addr + 3);
    const float  l0   = 0.5f * (g - 1.0f) * (g - 2.0f);
    const float  l1   = -g * (g - 2.0f);
    const float  l2   = 0.5f * g * (g - 1.0f);
    return (int32_t) (a0 * l0 + a1 * l1 + a2 * l2);
}

/* AWE32Emu: local addition, not upstream. Eight-point windowed sinc
 * (Blackman window) - the same formula as Interp::Sinc in AWE32Emu/src/
 * Emu8000.cpp, so both cores use literally the same maths.
 * The playing position lies between taps 1 and 2 (interpolator offset of
 * one word, see the note "actual audio location is the point 1 word
 * higher"), so eight samples are taken symmetrically around it: -2 to 5. */
static inline int32_t
EMU8K_READ_INTERP_SINC(emu8k_sample_read_t read_sample, const void *context, uint32_t int_addr, uint16_t fract)
{
    const double x = 1.0 + (double) fract / 65536.0;
    double       acc = 0.0;
    double       norm = 0.0;

    for (int i = -2; i <= 5; i++) {
        const double d = x - (double) i;
        double       w;

        if (fabs(d) < 1e-9) {
            w = 1.0;
        } else {
            const double pd = M_PI * d;
            const double t  = (d + 3.5) / 7.0;    /* 0..1 over the whole kernel width */
            const double bw = 0.42 - 0.5 * cos(2.0 * M_PI * t)
                            + 0.08 * cos(4.0 * M_PI * t);
            w = sin(pd) / pd * bw;
        }
        acc  += (double) read_sample(context, int_addr + 1 + i) * w;
        norm += w;
    }
    return (int32_t) (norm > 1e-9 ? acc / norm : acc);
}

/* AWE32Emu: cubic B-spline over four samples (an approximating, smoothing
 * kernel: it does not pass through the samples). Line out of the card,
 * AWETST28 block 44 (noise played -3 .. +2 octaves off its pitch): card -
 * render per third-octave band within 0.1 dB up to 4 kHz for every pitch,
 * above that the same small droop at all pitches (-0.6 dB at 8 kHz, -2.6 dB
 * at 16 kHz - the card's analog output). The kernels tried before scored
 * rms 7.4 dB (3-point Lagrange), 5.5 (Catmull-Rom), 6.4 (sinc), 7.6
 * (linear), 3.4 (quadratic B-spline), this one 2.4 (the rest being the -3
 * octave row, where the card is at its noise floor). The smoothing also
 * explains what looked like an analog high cut at unity pitch before. The
 * sample position lies between p1 and p2 (the interpolator offset of one
 * word). */
static inline int32_t
EMU8K_READ_INTERP_BSPLINE4(emu8k_sample_read_t read_sample, const void *context, uint32_t a, uint16_t fract)
{
    const float g  = (float) fract / 65536.0f;
    const float g2 = g * g;
    const float g3 = g2 * g;
    const float u  = 1.0f - g;
    const float p0 = (float) read_sample(context, a + 0);
    const float p1 = (float) read_sample(context, a + 1);
    const float p2 = (float) read_sample(context, a + 2);
    const float p3 = (float) read_sample(context, a + 3);
    return (int32_t) ((u * u * u * p0 + (3.0f * g3 - 6.0f * g2 + 4.0f) * p1
                       + (-3.0f * g3 + 3.0f * g2 + 3.0f * g + 1.0f) * p2 + g3 * p3) / 6.0f);
}

static inline int32_t
EMU8K_READ_INTERP_CUBIC(emu8k_sample_read_t read_sample, const void *context, uint32_t int_addr, uint16_t fract)
{
    /*Since there are four floats in the table for each fraction, the position is 16byte aligned. */
    fract >>= 16 - CUBIC_RESOLUTION_LOG;
    fract <<= 2;

    /* TODO: I still have to verify how this works, but I think that
     * the card could use two oscillators (usually 31 and 32) where it would
     * be writing the OPL3 output, and to which, chorus and reverb could be applied to get
     * those effects for OPL3 sounds.*/
#if 0
    if ((addr & EMU8K_FM_MEM_ADDRESS) == EMU8K_FM_MEM_ADDRESS) {}
#endif

    /* This is cubic interpolation.
     * Not the same than 3-point interpolation, but a better approximation than linear
     * interpolation.
     * Also, it takes into account the "Note that the actual audio location is the point
     * 1 word higher than this value due to interpolation offset".
     * That's why the pointers are 0, 1, 2, 3 and not -1, 0, 1, 2 */
    int32_t       dat2  = read_sample(context, int_addr + 1);
    const float  *table = &cubic_table[fract];
    const int32_t dat1  = read_sample(context, int_addr);
    const int32_t dat3  = read_sample(context, int_addr + 2);
    const int32_t dat4  = read_sample(context, int_addr + 3);
    /* Note: I've ended using float for the table values to avoid some cases of integer overflow. */
    dat2 = dat1 * table[0] + dat2 * table[1] + dat3 * table[2] + dat4 * table[3];
    return dat2;
}

static inline void
EMU8K_WRITE(emu8k_t *emu8k, uint32_t addr, uint16_t val)
{
    addr &= EMU8K_MEM_ADDRESS_MASK;
    if (!emu8k->ram || addr < EMU8K_RAM_MEM_START || addr >= EMU8K_FM_MEM_ADDRESS)
        return;

    /* It looks like if an application writes to a memory part outside of the available
     * amount on the card, it wraps, and opencubicplayer uses that to detect the amount
     * of memory, as opposed to simply check at the address that it has just tried to write. */
    while (addr >= emu8k->ram_end_addr)
        addr -= emu8k->ram_end_addr - EMU8K_RAM_MEM_START;

    emu8k->ram[addr - EMU8K_RAM_MEM_START] = val;
}

static uint16_t /* AWE32Emu: renamed, wrapper below adds the trace */
emu8k_inw_untraced(uint16_t addr, void *priv)
{
    emu8k_t *emu8k = (emu8k_t *) priv;
    uint16_t ret   = 0xffff;

#ifdef EMU8K_DEBUG_REGISTERS
    if (addr == 0xE22) {
        emu8k_log("EMU8K READ POINTER: %d\n",
                  ((0x80 | ((random_helper + 1) & 0x1F)) << 8) | (emu8k->cur_reg << 5) | emu8k->cur_voice);
    } else if ((addr & 0xF00) == 0x600) {
        /* These are automatically reported by READ16 */
        if (rep_count_r > 1) {
            emu8k_log("EMU8K ...... for %d times\n", rep_count_r);
            rep_count_r = 0;
        }
        last_read = 0;
    } else if ((addr & 0xF00) == 0xA00 && emu8k->cur_reg == 0) {
        /* These are automatically reported by READ16 */
        if (rep_count_r > 1) {
            emu8k_log("EMU8K ...... for %d times\n", rep_count_r);
            rep_count_r = 0;
        }
        last_read = 0;
    } else if ((addr & 0xF00) == 0xA00 && emu8k->cur_reg == 1) {
        uint32_t tmpz = ((addr & 0xF00) << 16) | (emu8k->cur_reg << 5);
        if (tmpz != last_read) {
            if (rep_count_r > 1) {
                emu8k_log("EMU8K ...... for %d times\n", rep_count_r);
                rep_count_r = 0;
            }
            last_read = tmpz;
            emu8k_log("EMU8K READ RAM I/O or configuration or clock \n");
        }
        // emu8k_log("EMU8K READ %04X-%02X(%d/%d)\n",addr,(emu8k->cur_reg)<<5|emu8k->cur_voice, emu8k->cur_reg, emu8k->cur_voice);
    } else if ((addr & 0xF00) == 0xA00 && (emu8k->cur_reg == 2 || emu8k->cur_reg == 3)) {
        uint32_t tmpz = ((addr & 0xF00) << 16);
        if (tmpz != last_read) {
            if (rep_count_r > 1) {
                emu8k_log("EMU8K ...... for %d times\n", rep_count_r);
                rep_count_r = 0;
            }
            last_read = tmpz;
            emu8k_log("EMU8K READ INIT \n");
        }
        // emu8k_log("EMU8K READ %04X-%02X(%d/%d)\n",addr,(emu8k->cur_reg)<<5|emu8k->cur_voice, emu8k->cur_reg, emu8k->cur_voice);
    } else {
        uint32_t tmpz = (addr << 16) | (emu8k->cur_reg << 5) | emu8k->cur_voice;
        if (tmpz != last_read) {
            char    *name = 0;
            uint16_t val  = 0xBAAD;
            if (addr == 0xA20) {
                name = PORT_NAMES[1][emu8k->cur_reg];
                switch (emu8k->cur_reg) {
                    case 2:
                        val = emu8k->init1[emu8k->cur_voice];
                        break;
                    case 3:
                        val = emu8k->init3[emu8k->cur_voice];
                        break;
                    case 4:
                        val = emu8k->voice[emu8k->cur_voice].envvol;
                        break;
                    case 5:
                        val = emu8k->voice[emu8k->cur_voice].dcysusv;
                        break;
                    case 6:
                        val = emu8k->voice[emu8k->cur_voice].envval;
                        break;
                    case 7:
                        val = emu8k->voice[emu8k->cur_voice].dcysus;
                        break;
                }
            } else if (addr == 0xA22) {
                name = PORT_NAMES[2][emu8k->cur_reg];
                switch (emu8k->cur_reg) {
                    case 2:
                        val = emu8k->init2[emu8k->cur_voice];
                        break;
                    case 3:
                        val = emu8k->init4[emu8k->cur_voice];
                        break;
                    case 4:
                        val = emu8k->voice[emu8k->cur_voice].atkhldv;
                        break;
                    case 5:
                        val = emu8k->voice[emu8k->cur_voice].lfo1val;
                        break;
                    case 6:
                        val = emu8k->voice[emu8k->cur_voice].atkhld;
                        break;
                    case 7:
                        val = emu8k->voice[emu8k->cur_voice].lfo2val;
                        break;
                }
            } else if (addr == 0xE20) {
                name = PORT_NAMES[3][emu8k->cur_reg];
                switch (emu8k->cur_reg) {
                    case 0:
                        val = emu8k->voice[emu8k->cur_voice].ip;
                        break;
                    case 1:
                        val = emu8k->voice[emu8k->cur_voice].ifatn;
                        break;
                    case 2:
                        val = emu8k->voice[emu8k->cur_voice].pefe;
                        break;
                    case 3:
                        val = emu8k->voice[emu8k->cur_voice].fmmod;
                        break;
                    case 4:
                        val = emu8k->voice[emu8k->cur_voice].tremfrq;
                        break;
                    case 5:
                        val = emu8k->voice[emu8k->cur_voice].fm2frq2;
                        break;
                    case 6:
                        val = 0xffff;
                        break;
                    case 7:
                        val = 0x0c | ((emu8k->id & 0x0002) ? 0xff02 : 0);
                        break;
                }
            }
            if (rep_count_r > 1) {
                emu8k_log("EMU8K ...... for %d times\n", rep_count_r);
            }
            if (name == 0) {
                emu8k_log("EMU8K READ %04X-%02X(%d/%d): %04X\n", addr, (emu8k->cur_reg) << 5 | emu8k->cur_voice, emu8k->cur_reg, emu8k->cur_voice, val);
            } else {
                emu8k_log("EMU8K READ %s (%d): %04X\n", name, emu8k->cur_voice, val);
            }

            rep_count_r = 0;
            last_read   = tmpz;
        }
        rep_count_r++;
    }
#endif //  EMU8K_DEBUG_REGISTERS

    switch (addr & 0xF02) {
        case 0x600:
        case 0x602: /*Data0. also known as BLASTER+0x400 and EMU+0x000 */
            READ16(addr, emu8k_voice_read(&emu8k->voice[emu8k->cur_voice], emu8k->cur_reg));
            return ret;

        case 0xA00: /*Data1. also known as BLASTER+0x800 and EMU+0x400 */
            switch (emu8k->cur_reg) {
                case 0:
                    READ16(addr, emu8k->voice[emu8k->cur_voice].ccca);
                    return ret;

                case 1:
                    switch (emu8k->cur_voice) {
                        case 9:
                            READ16(addr, emu8k->hwcf4);
                            return ret;
                        case 10:
                            READ16(addr, emu8k->hwcf5);
                            return ret;
                        /* Actually, these two might be command words rather than registers, or some LFO position/buffer reset.*/
                        case 13:
                            READ16(addr, emu8k->hwcf6);
                            return ret;
                        case 14:
                            READ16(addr, emu8k->hwcf7);
                            return ret;

                        case 20:
                            READ16(addr, emu8k->smalr);
                            return ret;
                        case 21:
                            READ16(addr, emu8k->smarr);
                            return ret;
                        case 22:
                            READ16(addr, emu8k->smalw);
                            return ret;
                        case 23:
                            READ16(addr, emu8k->smarw);
                            return ret;

                        case 26:
                            {
                                uint16_t val       = emu8k->smld_buffer;
                                emu8k->smld_buffer = EMU8K_READ(emu8k, emu8k->smalr);
                                emu8k->smalr       = (emu8k->smalr + 1) & EMU8K_MEM_ADDRESS_MASK;
                                return val;
                            }

                        /*The EMU8000 PGM describes the return values of these registers as 'a VLSI error'*/
                        case 29: /*Configuration Word 1*/
                            return (emu8k->hwcf1 & 0xfe) | (emu8k->hwcf3 & 0x01);
                        case 30: /*Configuration Word 2*/
                            return ((emu8k->hwcf2 >> 4) & 0x0e) | (emu8k->hwcf1 & 0x01) | ((emu8k->hwcf3 & 0x02) ? 0x10 : 0) | ((emu8k->hwcf3 & 0x04) ? 0x40 : 0)
                                | ((emu8k->hwcf3 & 0x08) ? 0x20 : 0) | ((emu8k->hwcf3 & 0x10) ? 0x80 : 0);
                        case 31: /*Configuration Word 3*/
                            return emu8k->hwcf2 & 0x1f;

                        default:
                            break;
                    }
                    break;

                case 2:
                    return emu8k->init1[emu8k->cur_voice];

                case 3:
                    return emu8k->init3[emu8k->cur_voice];

                case 4:
                    return emu8k->voice[emu8k->cur_voice].envvol;

                case 5:
                    return emu8k->voice[emu8k->cur_voice].dcysusv;

                case 6:
                    return emu8k->voice[emu8k->cur_voice].envval;

                case 7:
                    return emu8k->voice[emu8k->cur_voice].dcysus;

                default:
                    break;
            }
            break;

        case 0xA02: /*Data2. also known as BLASTER+0x802 and EMU+0x402 */
            switch (emu8k->cur_reg) {
                case 0:
                    READ16(addr, emu8k->voice[emu8k->cur_voice].ccca);
                    return ret;

                case 1:
                    switch (emu8k->cur_voice) {
                        case 9:
                            READ16(addr, emu8k->hwcf4);
                            return ret;
                        case 10:
                            READ16(addr, emu8k->hwcf5);
                            return ret;
                        /* Actually, these two might be command words rather than registers, or some LFO position/buffer reset. */
                        case 13:
                            READ16(addr, emu8k->hwcf6);
                            return ret;
                        case 14:
                            READ16(addr, emu8k->hwcf7);
                            return ret;

                        /* Simulating empty/full bits by unsetting it once read. */
                        case 20:
                            READ16(addr, emu8k->smalr | dmareadbit);
                            /* xor with itself to set to zero faster. */
                            dmareadbit ^= dmareadbit;
                            return ret;
                        case 21:
                            READ16(addr, emu8k->smarr | dmareadbit);
                            /* xor with itself to set to zero faster.*/
                            dmareadbit ^= dmareadbit;
                            return ret;
                        case 22:
                            READ16(addr, emu8k->smalw | dmawritebit);
                            /*xor with itself to set to zero faster.*/
                            dmawritebit ^= dmawritebit;
                            return ret;
                        case 23:
                            READ16(addr, emu8k->smarw | dmawritebit);
                            /*xor with itself to set to zero faster.*/
                            dmawritebit ^= dmawritebit;
                            return ret;

                        case 26:
                            {
                                uint16_t val       = emu8k->smrd_buffer;
                                emu8k->smrd_buffer = EMU8K_READ(emu8k, emu8k->smarr);
                                emu8k->smarr       = (emu8k->smarr + 1) & EMU8K_MEM_ADDRESS_MASK;
                                return val;
                            }
                        /*TODO: We need to improve the precision of this clock, since
                         it is used by programs to wait. Not critical, but should help reduce
                         the amount of calls and wait time */
                        case 27: /*Sample Counter ( 44Khz clock) */
                            return emu8k->wc;

                        default:
                            break;
                    }
                    break;

                case 2:
                    return emu8k->init2[emu8k->cur_voice];

                case 3:
                    return emu8k->init4[emu8k->cur_voice];

                case 4:
                    return emu8k->voice[emu8k->cur_voice].atkhldv;

                case 5:
                    return emu8k->voice[emu8k->cur_voice].lfo1val;

                case 6:
                    return emu8k->voice[emu8k->cur_voice].atkhld;

                case 7:
                    return emu8k->voice[emu8k->cur_voice].lfo2val;

                default:
                    break;
            }
            break;

        case 0xE00: /*Data3. also known as BLASTER+0xC00 and EMU+0x800 */
            switch (emu8k->cur_reg) {
                case 0:
                    return emu8k->voice[emu8k->cur_voice].ip;

                case 1:
                    return emu8k->voice[emu8k->cur_voice].ifatn;

                case 2:
                    return emu8k->voice[emu8k->cur_voice].pefe;

                case 3:
                    return emu8k->voice[emu8k->cur_voice].fmmod;

                case 4:
                    return emu8k->voice[emu8k->cur_voice].tremfrq;

                case 5:
                    return emu8k->voice[emu8k->cur_voice].fm2frq2;

                case 6:
                    return 0xffff;

                case 7: /*ID?*/
                    /* AWE32Emu: upstream returns 0x1c here, which makes
                       Creative's own AWEUTIL.COM fail detection with ERR012.
                       AWEUTIL (sub_12B40) does a full 16-bit compare:
                           mov ax, 7C00h / call read_word / cmp ax, 0Ch / jz ok
                       so the register has to read exactly 0x000C, unmasked. */
                    return 0x0c | ((emu8k->id & 0x0002) ? 0xff02 : 0);

                default:
                    break;
            }
            break;

        case 0xE02: /* Pointer. also known as BLASTER+0xC02 and EMU+0x802 */
            /* LS five bits = channel number, next 3 bits = register number
             * and MS 8 bits = VLSI test register.
             * Impulse tracker tests the non variability of the LS byte that it has set, and the variability
             * of the MS byte to determine that it really is an AWE32.
             * cubic player has a similar code, where it waits until value & 0x1000 is nonzero, and then waits again until it changes to zero.*/
            random_helper = (random_helper + 1) & 0x1F;
            return ((0x80 | random_helper) << 8) | (emu8k->cur_reg << 5) | emu8k->cur_voice;

        default:
            break;
    }
    emu8k_log("EMU8K READ : Unknown register read: %04X-%02X(%d/%d) \n", addr, (emu8k->cur_reg << 5) | emu8k->cur_voice, emu8k->cur_reg, emu8k->cur_voice);
    return 0xffff;
}

/* AWE32Emu: local addition, not upstream */
uint16_t
emu8k_inw(uint16_t addr, void *priv)
{
    const uint16_t val = emu8k_inw_untraced(addr, priv);
    return val;
}

void
emu8k_outw(uint16_t addr, uint16_t val, void *priv)
{
    emu8k_t *emu8k = (emu8k_t *) priv;

    /*TODO: I would like to not call this here, but i found it was needed or else cubic player would not finish opening (take a looot more of time than usual).
     * Basically, being here means that the audio is generated in the emulation thread, instead of the audio thread.*/
    emu8k_update(emu8k);

#ifdef EMU8K_DEBUG_REGISTERS
    if (addr == 0xE22) {
        // emu8k_log("EMU8K WRITE POINTER: %d\n", val);
    } else if ((addr & 0xF00) == 0x600) {
        /* These are automatically reported by WRITE16 */
        if (rep_count_w > 1) {
            emu8k_log("EMU8K ...... for %d times\n", rep_count_w);
            rep_count_w = 0;
        }
        last_write = 0;
    } else if ((addr & 0xF00) == 0xA00 && emu8k->cur_reg == 0) {
        /* These are automatically reported by WRITE16 */
        if (rep_count_w > 1) {
            emu8k_log("EMU8K ...... for %d times\n", rep_count_w);
            rep_count_w = 0;
        }
        last_write = 0;
    } else if ((addr & 0xF00) == 0xA00 && emu8k->cur_reg == 1) {
        uint32_t tmpz = ((addr & 0xF00) << 16) | (emu8k->cur_reg << 5);
        if (tmpz != last_write) {
            if (rep_count_w > 1) {
                emu8k_log("EMU8K ...... for %d times\n", rep_count_w);
                rep_count_w = 0;
            }
            last_write = tmpz;
            emu8k_log("EMU8K WRITE RAM I/O or configuration \n");
        }
        // emu8k_log("EMU8K WRITE %04X-%02X(%d/%d): %04X\n",addr,(emu8k->cur_reg)<<5|emu8k->cur_voice,emu8k->cur_reg,emu8k->cur_voice, val);
    } else if ((addr & 0xF00) == 0xA00 && (emu8k->cur_reg == 2 || emu8k->cur_reg == 3)) {
        uint32_t tmpz = ((addr & 0xF00) << 16);
        if (tmpz != last_write) {
            if (rep_count_w > 1) {
                emu8k_log("EMU8K ...... for %d times\n", rep_count_w);
                rep_count_w = 0;
            }
            last_write = tmpz;
            emu8k_log("EMU8K WRITE INIT \n");
        }
        // emu8k_log("EMU8K WRITE %04X-%02X(%d/%d): %04X\n",addr,(emu8k->cur_reg)<<5|emu8k->cur_voice,emu8k->cur_reg,emu8k->cur_voice, val);
    } else if (addr != 0xE22) {
        uint32_t tmpz = (addr << 16) | (emu8k->cur_reg << 5) | emu8k->cur_voice;
        // if (tmpz != last_write)
        if (1) {
            char *name = 0;
            if (addr == 0xA20) {
                name = PORT_NAMES[1][emu8k->cur_reg];
            } else if (addr == 0xA22) {
                name = PORT_NAMES[2][emu8k->cur_reg];
            } else if (addr == 0xE20) {
                name = PORT_NAMES[3][emu8k->cur_reg];
            }

            if (rep_count_w > 1) {
                emu8k_log("EMU8K ...... for %d times\n", rep_count_w);
            }
            if (name == 0) {
                emu8k_log("EMU8K WRITE %04X-%02X(%d/%d): %04X\n", addr, (emu8k->cur_reg) << 5 | emu8k->cur_voice, emu8k->cur_reg, emu8k->cur_voice, val);
            } else {
                emu8k_log("EMU8K WRITE %s (%d): %04X\n", name, emu8k->cur_voice, val);
            }

            rep_count_w = 0;
            last_write  = tmpz;
        }
        rep_count_w++;
    }
#endif // EMU8K_DEBUG_REGISTERS

    switch (addr & 0xF02) {
        case 0x600:
        case 0x602: /*Data0. also known as BLASTER+0x400 and EMU+0x000 */
        {
            emu8k_voice_t *voice = &emu8k->voice[emu8k->cur_voice];
            uint32_t *reg = emu8k_voice_dword(voice, emu8k->cur_reg);
            WRITE16(addr, *reg, val);
            emu8k_voice_sync(voice, emu8k->cur_reg, addr & 2);
            return;
        }

        case 0xA00: /*Data1. also known as BLASTER+0x800 and EMU+0x400 */
            switch (emu8k->cur_reg) {
                case 0:
                    WRITE16(addr, emu8k->voice[emu8k->cur_voice].ccca, val);
                    /* TODO: Should we update only on MSB update, or this could be used as some sort of hack by applications? */
                    emu8k->voice[emu8k->cur_voice].addr.int_address = emu8k->voice[emu8k->cur_voice].ccca & EMU8K_MEM_ADDRESS_MASK;
                    return;

                case 1:
                    switch (emu8k->cur_voice) {
                        case 9:
                            WRITE16(addr, emu8k->hwcf4, val);
                            return;
                        case 10:
                            WRITE16(addr, emu8k->hwcf5, val);
                            return;
                        /* Actually, these two might be command words rather than registers, or some LFO position/buffer reset. */
                        case 13:
                            WRITE16(addr, emu8k->hwcf6, val);
                            return;
                        case 14:
                            WRITE16(addr, emu8k->hwcf7, val);
                            return;

                        case 20:
                            WRITE16(addr, emu8k->smalr, val);
                            return;
                        case 21:
                            WRITE16(addr, emu8k->smarr, val);
                            return;
                        case 22:
                            WRITE16(addr, emu8k->smalw, val);
                            return;
                        case 23:
                            WRITE16(addr, emu8k->smarw, val);
                            return;

                        case 26:
                            EMU8K_WRITE(emu8k, emu8k->smalw, val);
                            emu8k->smalw = (emu8k->smalw + 1) & EMU8K_MEM_ADDRESS_MASK;
                            return;

                        case 29:
                            emu8k->hwcf1 = val;
                            return;
                        case 30:
                            emu8k->hwcf2 = val;
                            return;
                        case 31:
                            emu8k->hwcf3 = val;
                            return;

                        default:
                            break;
                    }
                    break;

                case 2:
                    emu8k->init1[emu8k->cur_voice] = val;
                    /* Skip if in first/second initialization step */
                    if (emu8k->init1[0] != 0x03FF) {
                        switch (emu8k->cur_voice) {
                            case 0x3:
                                emu8k->reverb_engine.out_mix = val & 0xFF;
                                break;
                            case 0x5:
                                {
                                    for (uint8_t c = 0; c < 8; c++) {
                                        emu8k->reverb_engine.allpass[c].feedback = (val & 0xFF) / ((float) 0xFF);
                                    }
                                }
                                break;
                            case 0x7:
                                emu8k->reverb_engine.link_return_type = (val == 0x8474) ? 1 : 0;
                                break;
                            case 0xF:
                                emu8k->reverb_engine.reflections[0].output_gain = ((val & 0xF0) >> 4) / 15.0;
                                break;
                            case 0x17:
                                emu8k->reverb_engine.reflections[1].output_gain = ((val & 0xF0) >> 4) / 15.0;
                                break;
                            case 0x1F:
                                emu8k->reverb_engine.reflections[2].output_gain = ((val & 0xF0) >> 4) / 15.0;
                                break;
                            case 0x9:
                                emu8k->reverb_engine.reflections[0].feedback = (val & 0xF) / 15.0;
                                break;
                            case 0xB:
#if 0
                                emu8k->reverb_engine.reflections[0].feedback_r =  (val&0xF)/15.0;
#endif
                                break;
                            case 0x11:
                                emu8k->reverb_engine.reflections[1].feedback = (val & 0xF) / 15.0;
                                break;
                            case 0x13:
#if 0
                                emu8k->reverb_engine.reflections[1].feedback_r =  (val&0xF)/15.0;
#endif
                                break;
                            case 0x19:
                                emu8k->reverb_engine.reflections[2].feedback = (val & 0xF) / 15.0;
                                break;
                            case 0x1B:
#if 0
                                emu8k->reverb_engine.reflections[2].feedback_r =  (val&0xF)/15.0;
#endif
                                break;

                            default:
                                break;
                        }
                    }
                    return;

                case 3:
                    emu8k->init3[emu8k->cur_voice] = val;
                    /* Skip if in first/second initialization step */
                    if (emu8k->init1[0] != 0x03FF) {
                        switch (emu8k->cur_voice) {
                            case 9:
                                emu8k->chorus_engine.feedback = (val & 0xFF);
                                break;
                            case 12:
                                /* Limiting this to a sane value given our buffer. */
                                emu8k->chorus_engine.delay_samples_central = (val & 0x1FFF);
                                break;

                            case 1:
                                emu8k->reverb_engine.refl_in_amp = val & 0xFF;
                                break;
                            case 3:
#if 0
                                emu8k->reverb_engine.refl_in_amp_r = val&0xFF;
#endif
                                break;

                            default:
                                break;
                        }
                    }
                    return;

                case 4:
                    emu8k->voice[emu8k->cur_voice].envvol                     = val;
                    emu8k->voice[emu8k->cur_voice].vol_envelope.delay_samples = ENVVOL_TO_EMU_SAMPLES(val);
                    return;

                case 5:
                    {
                        emu8k->voice[emu8k->cur_voice].dcysusv       = val;
                        emu8k_envelope_t *const vol_env              = &emu8k->voice[emu8k->cur_voice].vol_envelope;
                        int                     old_on               = emu8k->voice[emu8k->cur_voice].env_engine_on;
                        emu8k->voice[emu8k->cur_voice].env_engine_on = DCYSUSV_GENERATOR_ENGINE_ON(val);

                        if (emu8k->voice[emu8k->cur_voice].env_engine_on && old_on != emu8k->voice[emu8k->cur_voice].env_engine_on) {
                            if (emu8k->hwcf3 != 0x04) {
                                /* This is a hack for some programs like Doom or cubic player 1.7 that don't initialize
                                   the hwcfg and init registers (doom does not init the card at all. only tests the cfg registers) */
                                emu8k->hwcf3 = 0x04;
                            }

                            /* AWE32Emu: a new note starts with a clean filter */
                            emu8k->voice[emu8k->cur_voice].cham_lp = 0.0;
                            emu8k->voice[emu8k->cur_voice].cham_bp = 0.0;
                            // reset lfos.
                            emu8k->voice[emu8k->cur_voice].lfo1_count.addr = 0;
                            emu8k->voice[emu8k->cur_voice].lfo2_count.addr = 0;
                            // Trigger envelopes
                            if (ATKHLDV_TRIGGER(emu8k->voice[emu8k->cur_voice].atkhldv)) {
                                vol_env->value_amp_hz = 0;
                                emu8k->voice[emu8k->cur_voice].overshoot_samples = -1; /* AWE32Emu */
                                if (vol_env->delay_samples) {
                                    vol_env->state = ENV_DELAY;
                                } else if (vol_env->attack_amount_amp_hz == 0) {
                                    vol_env->state = ENV_STOPPED;
                                } else {
                                    vol_env->state = ENV_ATTACK;
                                    /* TODO: Verify if "never attack" means eternal mute,
                                    * or it means skip attack, go to hold".
                                    if (vol_env->attack_amount == 0)
                                    {
                                            vol_env->value = (1 << 21);
                                            vol_env->state = ENV_HOLD;
                                    }*/
                                }
                            }

                            if (ATKHLD_TRIGGER(emu8k->voice[emu8k->cur_voice].atkhld)) {
                                emu8k_envelope_t *const mod_env = &emu8k->voice[emu8k->cur_voice].mod_envelope;
                                mod_env->value_amp_hz           = 0;
                                mod_env->value_db_oct           = 0;
                                if (mod_env->delay_samples) {
                                    mod_env->state = ENV_DELAY;
                                } else if (mod_env->attack_amount_amp_hz == 0) {
                                    mod_env->state = ENV_STOPPED;
                                } else {
                                    mod_env->state = ENV_ATTACK;
                                    /* TODO: Verify if "never attack" means eternal start,
                                        * or it means skip attack, go to hold".
                                    if (mod_env->attack_amount == 0)
                                    {
                                            mod_env->value = (1 << 21);
                                            mod_env->state = ENV_HOLD;
                                    }*/
                                }
                            }
                        }

                        /* Converting the input in dBs to envelope value range. */
                        vol_env->sustain_value_db_oct = DCYSUSV_SUS_TO_ENV_RANGE(DCYSUSV_SUSVALUE_GET(val));
                        vol_env->ramp_amount_db_oct   = env_decay_to_dbs_or_oct[DCYSUSV_DECAYRELEASE_GET(val)];
                        if (DCYSUSV_IS_RELEASE(val)) {
                            if (vol_env->state == ENV_DELAY || vol_env->state == ENV_ATTACK || vol_env->state == ENV_HOLD) {
                                vol_env->value_db_oct = env_vol_amplitude_to_db[emu8k_attack_amp_index(vol_env->value_amp_hz)] << 5; /* AWE32Emu */
                                if (vol_env->value_db_oct > (1 << 21))
                                    vol_env->value_db_oct = 1 << 21;
                            }

                            vol_env->state = (vol_env->value_db_oct >= vol_env->sustain_value_db_oct) ? ENV_RAMP_DOWN : ENV_RAMP_UP;
                        }
                    }
                    return;

                case 6:
                    emu8k->voice[emu8k->cur_voice].envval                     = val;
                    emu8k->voice[emu8k->cur_voice].mod_envelope.delay_samples = ENVVAL_TO_EMU_SAMPLES(val);
                    return;

                case 7:
                    {
                        // TODO: Look for a bug on delay (first trigger it works, next trigger it doesn't)
                        emu8k->voice[emu8k->cur_voice].dcysus = val;
                        emu8k_envelope_t *const mod_env       = &emu8k->voice[emu8k->cur_voice].mod_envelope;
                        /* Converting the input in octaves to envelope value range. */
                        mod_env->sustain_value_db_oct = DCYSUS_SUS_TO_ENV_RANGE(DCYSUS_SUSVALUE_GET(val));
                        mod_env->ramp_amount_db_oct   = env_decay_to_dbs_or_oct[DCYSUS_DECAYRELEASE_GET(val)];
                        if (DCYSUS_IS_RELEASE(val)) {
                            if (mod_env->state == ENV_DELAY || mod_env->state == ENV_ATTACK || mod_env->state == ENV_HOLD) {
                                mod_env->value_db_oct = emu8k_mod_attack_value(mod_env->value_amp_hz); /* AWE32Emu */
                                if (mod_env->value_db_oct >= (1 << 21))
                                    mod_env->value_db_oct = (1 << 21) - 1;
                            }

                            mod_env->state = (mod_env->value_db_oct >= mod_env->sustain_value_db_oct) ? ENV_RAMP_DOWN : ENV_RAMP_UP;
                        }
                    }
                    return;

                default:
                    break;
            }
            break;

        case 0xA02: /*Data2. also known as BLASTER+0x802 and EMU+0x402 */
            switch (emu8k->cur_reg) {
                case 0:
                    {
                        emu8k_voice_t *emu_voice = &emu8k->voice[emu8k->cur_voice];
                        WRITE16(addr, emu_voice->ccca, val);
                        emu8k_voice_sync(emu_voice, EMU8K_VOICE_CCCA, addr & 2);
                    }
                    return;

                case 1:
                    switch (emu8k->cur_voice) {
                        case 9:
                            WRITE16(addr, emu8k->hwcf4, val);
                            /* Skip if in first/second initialization step */
                            if (emu8k->init1[0] != 0x03FF) {
                                /*(1/256th of a 44Khz sample) */
                                /* clip the value to a reasonable value given our buffer */
                                int32_t tmp                                     = emu8k->hwcf4 & 0x1FFFFF;
                                emu8k->chorus_engine.delay_offset_samples_right = ((double) tmp) / 256.0;
                            }
                            return;
                        case 10:
                            WRITE16(addr, emu8k->hwcf5, val);
                            /* Skip if in first/second initialization step */
                            if (emu8k->init1[0] != 0x03FF) {
                                /* AWE32Emu: LFO rate = HWCF5 * 44100 / 2^24 Hz, i.e. the
                                 * 2^24 phase accumulator advances by HWCF5 per sample.
                                 * Measured on the card's chorus return (AWETST25 block 23,
                                 * instantaneous frequency of the sustained tone): HWCF5
                                 * 0x17C -> 1.00 Hz, 0x83 -> 0.35 Hz. Upstream read it as
                                 * milliHz (0.38 Hz). The table here is 65536 positions, so
                                 * HWCF5/256 positions per sample = HWCF5 << 24 in 32.32. */
                                emu8k->chorus_engine.lfo_inc.addr = (uint64_t) emu8k->hwcf5 << 24;
                            }
                            return;
                        /* Actually, these two might be command words rather than registers, or some LFO position/buffer reset.*/
                        case 13:
                            WRITE16(addr, emu8k->hwcf6, val);
                            return;
                        case 14:
                            WRITE16(addr, emu8k->hwcf7, val);
                            return;

                        case 20: /*Top 8 bits are for Empty (MT) bit or non-addressable.*/
                            WRITE16(addr, emu8k->smalr, val & 0xFF);
                            dmareadbit = 0x8000;
                            return;
                        case 21: /*Top 8 bits are for Empty (MT) bit or non-addressable.*/
                            WRITE16(addr, emu8k->smarr, val & 0xFF);
                            dmareadbit = 0x8000;
                            return;
                        case 22: /*Top 8 bits are for full bit or non-addressable.*/
                            WRITE16(addr, emu8k->smalw, val & 0xFF);
                            return;
                        case 23: /*Top 8 bits are for full bit or non-addressable.*/
                            WRITE16(addr, emu8k->smarw, val & 0xFF);
                            return;

                        case 26:
                            dmawritebit = 0x8000;
                            EMU8K_WRITE(emu8k, emu8k->smarw, val);
                            emu8k->smarw++;
                            return;

                        default:
                            break;
                    }
                    break;

                case 2:
                    emu8k->init2[emu8k->cur_voice] = val;
                    /* Skip if in first/second initialization step */
                    if (emu8k->init1[0] != 0x03FF) {
                        switch (emu8k->cur_voice) {
                            case 0x14:
                                {
                                    int multip                                  = ((val & 0xF00) >> 8) + 18;
                                    emu8k->reverb_engine.reflections[5].bufsize = multip * REV_BUFSIZE_STEP;
                                    emu8k->reverb_engine.tailL.bufsize          = (multip + 1) * REV_BUFSIZE_STEP;
                                    if (emu8k->reverb_engine.link_return_type == 0) {
                                        emu8k->reverb_engine.tailR.bufsize = (multip + 1) * REV_BUFSIZE_STEP;
                                    }
                                }
                                break;
                            case 0x16:
                                if (emu8k->reverb_engine.link_return_type == 1) {
                                    int multip                         = ((val & 0xF00) >> 8) + 18;
                                    emu8k->reverb_engine.tailR.bufsize = (multip + 1) * REV_BUFSIZE_STEP;
                                }
                                break;
                            case 0x7:
                                emu8k->reverb_engine.reflections[3].output_gain = ((val & 0xF0) >> 4) / 15.0;
                                break;
                            case 0xf:
                                emu8k->reverb_engine.reflections[4].output_gain = ((val & 0xF0) >> 4) / 15.0;
                                break;
                            case 0x17:
                                emu8k->reverb_engine.reflections[5].output_gain = ((val & 0xF0) >> 4) / 15.0;
                                break;
                            case 0x1d:
                                {
                                    for (uint8_t c = 0; c < 6; c++) {
                                        emu8k->reverb_engine.reflections[c].damp1       = (val & 0xFF) / 255.0;
                                        emu8k->reverb_engine.reflections[c].damp2       = (0xFF - (val & 0xFF)) / 255.0;
                                        emu8k->reverb_engine.reflections[c].filterstore = 0;
                                    }
                                    emu8k->reverb_engine.damper.damp1       = (val & 0xFF) / 255.0;
                                    emu8k->reverb_engine.damper.damp2       = (0xFF - (val & 0xFF)) / 255.0;
                                    emu8k->reverb_engine.damper.filterstore = 0;
                                }
                                break;
                            case 0x1f: /* filter r */
                                break;
                            case 0x1:
                                emu8k->reverb_engine.reflections[3].feedback = (val & 0xF) / 15.0;
                                break;
                            case 0x3:
#if 0
                                emu8k->reverb_engine.reflections[3].feedback_r =  (val&0xF)/15.0;
#endif
                                break;
                            case 0x9:
                                emu8k->reverb_engine.reflections[4].feedback = (val & 0xF) / 15.0;
                                break;
                            case 0xb:
#if 0
                                emu8k->reverb_engine.reflections[4].feedback_r =  (val&0xF)/15.0;
#endif
                                break;
                            case 0x11:
                                emu8k->reverb_engine.reflections[5].feedback = (val & 0xF) / 15.0;
                                break;
                            case 0x13:
#if 0
                                emu8k->reverb_engine.reflections[5].feedback_r =  (val&0xF)/15.0;
#endif
                                break;

                            default:
                                break;
                        }
                    }
                    return;

                case 3:
                    emu8k->init4[emu8k->cur_voice] = val;
                    /* Skip if in first/second initialization step */
                    if (emu8k->init1[0] != 0x03FF) {
                        switch (emu8k->cur_voice) {
                            case 0x3:
                                {
                                    /* AWE32Emu: the delay swings +-(low byte) samples, independent
                                     * of the centre delay (card: depth 0x2C/0x6E/0x84 -> 44/110/132
                                     * samples; presets 1 and 3 have different centre delays and the
                                     * same swing). Upstream used depth * delay / 256. */
                                    emu8k->chorus_engine.lfodepth_multip = val & 0xFF;
                                }
                                break;

                            case 0x1F:
                                emu8k->reverb_engine.link_return_amp = val & 0xFF;
                                break;

                            default:
                                break;
                        }
                    }
                    return;

                case 4:
                    {
                        emu8k->voice[emu8k->cur_voice].atkhldv = val;
                        emu8k_envelope_t *const vol_env        = &emu8k->voice[emu8k->cur_voice].vol_envelope;
                        vol_env->attack_samples                = env_attack_to_samples[ATKHLDV_ATTACK(val)];
                        if (vol_env->attack_samples == 0) {
                            vol_env->attack_amount_amp_hz = 0;
                        } else {
                            /* Linear amplitude increase each sample. */
                            vol_env->attack_amount_amp_hz = (1 << 21) / vol_env->attack_samples;
                        }
                        vol_env->hold_samples = ATKHLDV_HOLD_TO_EMU_SAMPLES(val);
                        if (ATKHLDV_TRIGGER(val) && emu8k->voice[emu8k->cur_voice].env_engine_on) {
                            /*TODO: I assume that "envelope trigger" is the same as new note
                             * (since changing the IP can be done when modulating pitch too) */
                            emu8k->voice[emu8k->cur_voice].lfo1_count.addr = 0;
                            emu8k->voice[emu8k->cur_voice].lfo2_count.addr = 0;

                            vol_env->value_amp_hz = 0;
                            emu8k->voice[emu8k->cur_voice].overshoot_samples = -1; /* AWE32Emu */
                            if (vol_env->delay_samples) {
                                vol_env->state = ENV_DELAY;
                            } else if (vol_env->attack_amount_amp_hz == 0) {
                                vol_env->state = ENV_STOPPED;
                            } else {
                                vol_env->state = ENV_ATTACK;
                                /* TODO: Verify if "never attack" means eternal mute,
                                * or it means skip attack, go to hold".
                                if (vol_env->attack_amount == 0)
                                {
                                        vol_env->value = (1 << 21);
                                        vol_env->state = ENV_HOLD;
                                }*/
                            }
                        }
                    }
                    return;

                case 5:
                    emu8k->voice[emu8k->cur_voice].lfo1val = val;
                    /* TODO: verify if this is set once, or set every time. */
                    emu8k->voice[emu8k->cur_voice].lfo1_delay_samples = LFOxVAL_TO_EMU_SAMPLES(val);
                    return;

                case 6:
                    {
                        emu8k->voice[emu8k->cur_voice].atkhld = val;
                        emu8k_envelope_t *const mod_env       = &emu8k->voice[emu8k->cur_voice].mod_envelope;
                        mod_env->attack_samples               = env_attack_to_samples[ATKHLD_ATTACK(val)];
                        if (mod_env->attack_samples == 0) {
                            mod_env->attack_amount_amp_hz = 0;
                        } else {
                            /* Linear amplitude increase each sample. */
                            mod_env->attack_amount_amp_hz = (1 << 21) / mod_env->attack_samples;
                        }
                        mod_env->hold_samples = ATKHLD_HOLD_TO_EMU_SAMPLES(val);
                        if (ATKHLD_TRIGGER(val) && emu8k->voice[emu8k->cur_voice].env_engine_on) {
                            mod_env->value_amp_hz = 0;
                            mod_env->value_db_oct = 0;
                            if (mod_env->delay_samples) {
                                mod_env->state = ENV_DELAY;
                            } else if (mod_env->attack_amount_amp_hz == 0) {
                                mod_env->state = ENV_STOPPED;
                            } else {
                                mod_env->state = ENV_ATTACK;
                                /* TODO: Verify if "never attack" means eternal start,
                                    * or it means skip attack, go to hold".
                                if (mod_env->attack_amount == 0)
                                {
                                        mod_env->value = (1 << 21);
                                        mod_env->state = ENV_HOLD;
                                }*/
                            }
                        }
                    }
                    return;

                case 7:
                    emu8k->voice[emu8k->cur_voice].lfo2val            = val;
                    emu8k->voice[emu8k->cur_voice].lfo2_delay_samples = LFOxVAL_TO_EMU_SAMPLES(val);

                    return;

                default:
                    break;
            }
            break;

        case 0xE00: /*Data3. also known as BLASTER+0xC00 and EMU+0x800 */
            switch (emu8k->cur_reg) {
                case 0:
                    emu8k_voice_set_ip(&emu8k->voice[emu8k->cur_voice], val);
                    return;

                case 1:
                    emu8k_voice_set_ifatn(&emu8k->voice[emu8k->cur_voice], val);
                    return;

                case 2:
                    {
                        emu8k_voice_t *const the_voice = &emu8k->voice[emu8k->cur_voice];
                        the_voice->pefe                = val;

                        int divider                           = (the_voice->pefe_modenv_filter_height < 0) ? 0x80 : 0x7F;
                        the_voice->fixed_modenv_filter_height = ((int32_t) the_voice->pefe_modenv_filter_height) * 0x4000 / divider;

                        divider                              = (the_voice->pefe_modenv_pitch_height < 0) ? 0x80 : 0x7F;
                        the_voice->fixed_modenv_pitch_height = ((int32_t) the_voice->pefe_modenv_pitch_height) * 0x4000 / divider;
                    }
                    return;

                case 3:
                    {
                        emu8k_voice_t *const the_voice = &emu8k->voice[emu8k->cur_voice];
                        the_voice->fmmod               = val;

                        int divider                    = (the_voice->fmmod_lfo1_filt_mod < 0) ? 0x80 : 0x7F;
                        the_voice->fixed_lfo1_filt_mod = ((int32_t) the_voice->fmmod_lfo1_filt_mod) * 0x4000 / divider;

                        divider                       = (the_voice->fmmod_lfo1_vibrato < 0) ? 0x80 : 0x7F;
                        the_voice->fixed_lfo1_vibrato = ((int32_t) the_voice->fmmod_lfo1_vibrato) * 0x4000 / divider;
                    }
                    return;

                case 4:
                    {
                        emu8k_voice_t *const the_voice = &emu8k->voice[emu8k->cur_voice];
                        the_voice->tremfrq             = val;
                        the_voice->lfo1_speed          = lfofreqtospeed[the_voice->tremfrq_lfo1_freq];

                        int divider                   = (the_voice->tremfrq_lfo1_tremolo < 0) ? 0x80 : 0x7F;
                        the_voice->fixed_lfo1_tremolo = ((int32_t) the_voice->tremfrq_lfo1_tremolo) * 0x4000 / divider;
                    }
                    return;

                case 5:
                    {
                        emu8k_voice_t *const the_voice = &emu8k->voice[emu8k->cur_voice];
                        the_voice->fm2frq2             = val;
                        the_voice->lfo2_speed          = lfofreqtospeed[the_voice->fm2frq2_lfo2_freq];

                        int divider                   = (the_voice->fm2frq2_lfo2_vibrato < 0) ? 0x80 : 0x7F;
                        the_voice->fixed_lfo2_vibrato = ((int32_t) the_voice->fm2frq2_lfo2_vibrato) * 0x4000 / divider;
                    }
                    return;

                case 7: /*ID? I believe that this allows applications to know if the emu is in use by another application */
                    emu8k->id = val;
                    return;

                default:
                    break;
            }
            break;

        case 0xE02: /* Pointer. also known as BLASTER+0xC02 and EMU+0x802 */
            emu8k->cur_voice = (val & 31);
            emu8k->cur_reg   = ((val >> 5) & 7);
            return;

        default:
            break;
    }
    emu8k_log("EMU8K WRITE: Unknown register write: %04X-%02X(%d/%d): %04X \n", addr, (emu8k->cur_reg) << 5 | emu8k->cur_voice,
              emu8k->cur_reg, emu8k->cur_voice, val);
}

uint8_t
emu8k_inb(uint16_t addr, void *priv)
{
    /* Reading a single byte is a feature that at least Impulse tracker uses,
     * but only on detection code and not for odd addresses.*/
    if (addr & 1)
        return emu8k_inw(addr & ~1, priv) >> 1;
    return emu8k_inw(addr, priv) & 0xff;
}

void
emu8k_outb(uint16_t addr, uint8_t val, void *priv)
{
    /* TODO: AWE32 docs says that you cannot write in bytes, but if
     * an app were to use this implementation, the content of the LS Byte would be lost.*/
    if (addr & 1)
        emu8k_outw(addr & ~1, val << 8, priv);
    else
        emu8k_outw(addr, val, priv);
}

/* AWE32Emu: chorus structure measured on the card (AWETST25 block 23, short
 * delay presets 6/7 separate the echoes in time):
 * - one delay line; the right tap reads HWCF4/256 samples EARLIER than the
 *   left one (right echoes at 24 + n*64 ms for delay 64 ms, HWCF4 40 ms),
 *   and the feedback is taken from the left tap only (both channels repeat
 *   every 64 ms);
 * - the loop is linear: on the line out (AWETST28 block 43) the return
 *   grows exactly with the send (32 -> 255: 18.1 dB, 18.0 expected), the
 *   echoes of feedback 0xC0 fall by the same 2.5 dB per pass at send 64 and
 *   255, and held tones of presets 1 and 4 give the same return and the same
 *   3rd harmonic at atten 12, 24 and 36. The saturation used before
 *   (C = 13400, then 36000) came from recordings clipped by the recorder
 *   (AWETST26 block 40) or compressed by the internal capture. */

static inline int32_t
emu8k_chorus_tap(const int32_t *line, int write, double delay)
{
    double readdouble    = (double) write - delay;
    int    read          = (int32_t) floor(readdouble);
    int    fraction_part = (readdouble - (double) read) * 65536.0;
    int    next_value;
    read %= EMU8K_LFOCHORUS_SIZE;
    if (read < 0)
        read += EMU8K_LFOCHORUS_SIZE;
    next_value = (read + 1) % EMU8K_LFOCHORUS_SIZE;
    return line[read] + (((line[next_value] - line[read]) * fraction_part) >> 16);
}

void
emu8k_work_chorus(int32_t *inbuf, int32_t *outbuf, emu8k_chorus_eng_t *engine, int count)
{
    for (int pos = 0; pos < count; pos++) {
        /* AWE32Emu: triangle LFO - the card's chorus return has a square-wave
         * frequency deviation (+-6.8 Hz at depth 110, 1 Hz), so the delay moves
         * linearly between the extremes. */
        const double phase = ((engine->lfo_pos.int_address & 0xFFFF) + engine->lfo_pos.fract_address / 65536.0) / 65536.0;
        const double tri   = (phase < 0.5) ? (4.0 * phase - 1.0) : (3.0 - 4.0 * phase);
        double offset_lfo = tri * (double) engine->lfodepth_multip;
        double delay_l    = (double) engine->delay_samples_central + offset_lfo;
        double delay_r    = delay_l - engine->delay_offset_samples_right;

        int32_t dat_l = emu8k_chorus_tap(engine->chorus_left_buffer, engine->write, delay_l);
        int32_t dat_r = emu8k_chorus_tap(engine->chorus_left_buffer, engine->write, delay_r);

        engine->chorus_left_buffer[engine->write] = *inbuf + ((dat_l * engine->feedback) >> 8);

        ++engine->write;
        engine->write %= EMU8K_LFOCHORUS_SIZE;
        engine->lfo_pos.addr += engine->lfo_inc.addr;
        engine->lfo_pos.int_address &= 0xFFFF;

        (*outbuf++) += dat_l;
        (*outbuf++) += dat_r;
        inbuf++;
    }
}

int32_t
emu8k_reverb_comb_work(emu8k_reverb_combfilter_t *comb, int32_t in)
{

    int32_t bufin;
    /* get echo */
    int32_t output = comb->reflection[comb->read_pos];
    /* apply lowpass */
    comb->filterstore = (output * comb->damp2) + (comb->filterstore * comb->damp1);
    /* appply feedback */
    bufin = in - (comb->filterstore * comb->feedback);
    /* store new value in delayed buffer */
    comb->reflection[comb->read_pos] = bufin;

    if (++comb->read_pos >= comb->bufsize)
        comb->read_pos = 0;

    return output * comb->output_gain;
}

int32_t
emu8k_reverb_diffuser_work(emu8k_reverb_combfilter_t *comb, int32_t in)
{

    int32_t bufout = comb->reflection[comb->read_pos];
    /*diffuse*/
    int32_t bufin  = -in + (bufout * comb->feedback);
    int32_t output = bufout - (bufin * comb->feedback);
    /* store new value in delayed buffer */
    comb->reflection[comb->read_pos] = bufin;

    if (++comb->read_pos >= comb->bufsize)
        comb->read_pos = 0;

    return output;
}

int32_t
emu8k_reverb_tail_work(emu8k_reverb_combfilter_t *comb, emu8k_reverb_combfilter_t *allpasses, int32_t in)
{
    int32_t output = comb->reflection[comb->read_pos];
    /* store new value in delayed buffer */
    comb->reflection[comb->read_pos] = in;

#if 0
    output = emu8k_reverb_allpass_work(&allpasses[0],output);
#endif
    output = emu8k_reverb_diffuser_work(&allpasses[1], output);
    output = emu8k_reverb_diffuser_work(&allpasses[2], output);
#if 0
    output = emu8k_reverb_allpass_work(&allpasses[3],output);
#endif

    if (++comb->read_pos >= comb->bufsize)
        comb->read_pos = 0;

    return output;
}
int32_t
emu8k_reverb_damper_work(emu8k_reverb_combfilter_t *comb, int32_t in)
{
    /* apply lowpass */
    comb->filterstore = (in * comb->damp2) + (comb->filterstore * comb->damp1);
    return comb->filterstore;
}

/* TODO: This is not a correct emulation, just a workalike implementation. */
void
emu8k_work_reverb(int32_t *inbuf, int32_t *outbuf, emu8k_reverb_eng_t *engine, int count)
{
    int pos;
    if (engine->link_return_type) {
        for (pos = 0; pos < count; pos++) {
            int32_t dat1;
            int32_t dat2;
            int32_t in;
            int32_t in2;
            in   = emu8k_reverb_damper_work(&engine->damper, inbuf[pos]);
            in2  = (in * engine->refl_in_amp) >> 8;
            dat2 = emu8k_reverb_comb_work(&engine->reflections[0], in2);
            dat2 += emu8k_reverb_comb_work(&engine->reflections[1], in2);
            dat1 = emu8k_reverb_comb_work(&engine->reflections[2], in2);
            dat2 += emu8k_reverb_comb_work(&engine->reflections[3], in2);
            dat1 += emu8k_reverb_comb_work(&engine->reflections[4], in2);
            dat2 += emu8k_reverb_comb_work(&engine->reflections[5], in2);

            dat1 += (emu8k_reverb_tail_work(&engine->tailL, &engine->allpass[0], in + dat1) * engine->link_return_amp) >> 8;
            dat2 += (emu8k_reverb_tail_work(&engine->tailR, &engine->allpass[4], in + dat2) * engine->link_return_amp) >> 8;

            (*outbuf++) += (dat1 * engine->out_mix) >> 8;
            (*outbuf++) += (dat2 * engine->out_mix) >> 8;
        }
    } else {
        for (pos = 0; pos < count; pos++) {
            int32_t dat1;
            int32_t dat2;
            int32_t in;
            int32_t in2;
            in   = emu8k_reverb_damper_work(&engine->damper, inbuf[pos]);
            in2  = (in * engine->refl_in_amp) >> 8;
            dat1 = emu8k_reverb_comb_work(&engine->reflections[0], in2);
            dat1 += emu8k_reverb_comb_work(&engine->reflections[1], in2);
            dat1 += emu8k_reverb_comb_work(&engine->reflections[2], in2);
            dat1 += emu8k_reverb_comb_work(&engine->reflections[3], in2);
            dat1 += emu8k_reverb_comb_work(&engine->reflections[4], in2);
            dat1 += emu8k_reverb_comb_work(&engine->reflections[5], in2);
            dat2 = dat1;

            dat1 += (emu8k_reverb_tail_work(&engine->tailL, &engine->allpass[0], in + dat1) * engine->link_return_amp) >> 8;
            dat2 += (emu8k_reverb_tail_work(&engine->tailR, &engine->allpass[4], in + dat2) * engine->link_return_amp) >> 8;

            (*outbuf++) += (dat1 * engine->out_mix) >> 8;
            (*outbuf++) += (dat2 * engine->out_mix) >> 8;
        }
    }
}
/* AWE32Emu: the equalizer of the chip output, measured on the card (block 35).
 * Upstream only has a TODO here. The positions are decoded from the INIT3 /
 * INIT4 slots on every call; a slot set that matches no table row keeps the
 * previous position (bass 5 / treble 9 before any write, the hardware default
 * the SDK writes as well). */
void
emu8k_work_eq(emu8k_t *emu8k, int32_t *inoutbuf, int count)
{
    const uint16_t v[10] = {
        emu8k->init4[0x01], emu8k->init4[0x11],
        emu8k->init3[0x11], emu8k->init3[0x13], emu8k->init3[0x1B],
        emu8k->init4[0x07], emu8k->init4[0x0B], emu8k->init4[0x0D], emu8k->init4[0x17], emu8k->init4[0x19]
    };
    int bass   = (emu8k->eq_bass < 0) ? 5 : emu8k->eq_bass;
    int treble = (emu8k->eq_treble < 0) ? 9 : emu8k->eq_treble;
    for (int i = 0; i < 12; i++) {
        if (emu8k_eq_word_match(v[0], emu8k_eq_bass_parm[i][0]) && emu8k_eq_word_match(v[1], emu8k_eq_bass_parm[i][1])) {
            bass = i;
            break;
        }
    }
    for (int i = 0; i < 12; i++) {
        int ok = 1;
        for (int k = 0; k < 8 && ok; k++)
            ok = emu8k_eq_word_match(v[2 + k], emu8k_eq_treble_parm[i][k]);
        if (ok) {
            treble = i;
            break;
        }
    }
    if (bass != emu8k->eq_bass || treble != emu8k->eq_treble) {
        /* The filter state is kept, so a change does not click. */
        emu8k->eq_bass   = bass;
        emu8k->eq_treble = treble;
        emu8k_eq_design(emu8k->eq_coef[0], &emu8k_eq_bass[bass], 0);
        emu8k_eq_design(emu8k->eq_coef[1], &emu8k_eq_treble[treble], 1);
    }

    for (int n = 0; n < count; n++) {
        for (int ch = 0; ch < 2; ch++) {
            double x = (double) *inoutbuf;
            for (int s = 0; s < 2; s++) {
                const double *q = emu8k->eq_coef[s];
                double       *z = emu8k->eq_z[s][ch];
                const double  y = q[0] * x + z[0];
                z[0]            = q[1] * x - q[3] * y + z[1];
                z[1]            = q[2] * x - q[4] * y;
                x               = y;
            }
            *inoutbuf++ = (int32_t) lrint(x);
        }
    }
}

/* AWE32Emu: the current volume follows the target as a one-pole slide, 1/64
 * of the difference per sample. Card (AWETST27 block 42, line out): after
 * the note-off (envelope off, volume target 0) the tone falls at a steady
 * 6.0 dB/ms (63/64 per sample = -0.137 dB) to -40 dB and below. Upstream
 * slid linearly by 0x400 per sample (silent after 1.5 ms). */
int32_t
emu8k_vol_slide(emu8k_slide_t *slide, int32_t target)
{
    const int32_t d = target - slide->last;
    if (d > 0)
        slide->last += (d + 63) >> 6;
    else if (d < 0)
        slide->last += d >> 6; /* floor: at least -1 */
    return slide->last;
}

#if 0
int32_t old_pitch[32] = { 0 };
int32_t old_cut[32]   = { 0 };
int32_t old_vol[32]   = { 0 };
#endif
static inline int32_t
emu8k_voice_sample(emu8k_voice_t *emu_voice, emu8k_sample_read_t read_sample,
                  const void *context, double sample_rate)
{
    const uint32_t addr     = emu_voice->addr.int_address;
    const uint16_t fraction = emu_voice->addr.fract_address;
    int32_t        dat;

    /* Waveform oscillator */
#ifdef RESAMPLER_LINEAR
    dat = EMU8K_READ_INTERP_LINEAR(read_sample, context, addr, fraction);

#elif defined RESAMPLER_CUBIC
    dat = EMU8K_READ_INTERP_CUBIC(read_sample, context, addr, fraction);

#elif defined RESAMPLER_SINC
    dat = EMU8K_READ_INTERP_SINC(read_sample, context, addr, fraction);
#elif defined RESAMPLER_POINT3
    dat = EMU8K_READ_INTERP_POINT3(read_sample, context, addr, fraction);
#elif defined RESAMPLER_BSPLINE4
    dat = EMU8K_READ_INTERP_BSPLINE4(read_sample, context, addr, fraction);
#endif

#ifdef FILTER_CHAMBERLIN
    /* AWE32Emu: Chamberlin SVF, see FILTER_CHAMBERLIN above. Bypassed
     * only at Q 0 with the cutoff register at 0xFF and no downward
     * modulation ("the filter does not alter the signal" [PG]). */
    if (emu_voice->filterq_idx || emu_voice->ifatn_init_filter != 0xFF || emu_voice->filt_oct_curr < 0.0) {
        double fc = EMU8K_CHAM_BASE_HZ
            * pow(2.0, emu_voice->ifatn_init_filter * EMU8K_CHAM_CENTS / 1200.0 + emu_voice->filt_oct_curr);
        if (fc < EMU8K_CHAM_BASE_HZ)
            fc = EMU8K_CHAM_BASE_HZ;
        if (fc > sample_rate / 6.0)
            fc = sample_rate / 6.0;
        const double F  = 2.0 * sin(M_PI * fc / sample_rate);
        const double qd = 1.0 / (EMU8K_CHAM_Q0 * pow(10.0, emu8k_cham_q_db(emu_voice->filterq_idx, fc) / 20.0));
        const double in = (double) dat * emu_voice->filt_att / 65536.0;
        emu_voice->cham_lp += F * emu_voice->cham_bp;
        const double hp = in - emu_voice->cham_lp - qd * emu_voice->cham_bp;
        emu_voice->cham_bp += F * hp;
        double out = emu_voice->cham_lp;
        if (out > 32767.0)
            out = 32767.0;
        else if (out < -32768.0)
            out = -32768.0;
        dat = (int32_t) out;
    }
#else
    (void) sample_rate;
    if (emu_voice->filterq_idx || emu_voice->cvcf_curr_filt_ctoff != 0xFFFF) {
        int           cutoff = emu_voice->cvcf_curr_filt_ctoff >> 8;
        const int64_t coef0  = filt_coeffs[emu_voice->filterq_idx][cutoff][0];
        const int64_t coef1  = filt_coeffs[emu_voice->filterq_idx][cutoff][1];
        const int64_t coef2  = filt_coeffs[emu_voice->filterq_idx][cutoff][2];
/* clip at twice the range */
#define ClipBuffer(buf) (buf < -16777216) ? -16777216 : (buf > 16777216) ? 16777216 \
                                                             : buf

#ifdef FILTER_INITIAL
#    define NOOP(x) (void) x;
        NOOP(coef1)
        /* Apply expected attenuation. (FILTER_MOOG does it implicitly, but this one doesn't).
         * Work in 24bits. */
        dat = (dat * emu_voice->filt_att) >> 8;

        int64_t vhp = ((-emu_voice->filt_buffer[0] * coef2) >> 24) - emu_voice->filt_buffer[1] - dat;
        emu_voice->filt_buffer[1] += (emu_voice->filt_buffer[0] * coef0) >> 24;
        emu_voice->filt_buffer[0] += (vhp * coef0) >> 24;
        dat = (int32_t) (emu_voice->filt_buffer[1] >> 8);
        if (dat > 32767)
            dat = 32767;
        else if (dat < -32768)
            dat = -32768;

#elif defined FILTER_MOOG

        /*move to 24bits*/
        dat <<= 8;

        dat -= (coef2 * emu_voice->filt_buffer[4]) >> 24; /*feedback*/
        int64_t t1                = emu_voice->filt_buffer[1];
        emu_voice->filt_buffer[1] = ((dat + emu_voice->filt_buffer[0]) * coef0 - emu_voice->filt_buffer[1] * coef1) >> 24;
        emu_voice->filt_buffer[1] = ClipBuffer(emu_voice->filt_buffer[1]);

        int64_t t2                = emu_voice->filt_buffer[2];
        emu_voice->filt_buffer[2] = ((emu_voice->filt_buffer[1] + t1) * coef0 - emu_voice->filt_buffer[2] * coef1) >> 24;
        emu_voice->filt_buffer[2] = ClipBuffer(emu_voice->filt_buffer[2]);

        int64_t t3                = emu_voice->filt_buffer[3];
        emu_voice->filt_buffer[3] = ((emu_voice->filt_buffer[2] + t2) * coef0 - emu_voice->filt_buffer[3] * coef1) >> 24;
        emu_voice->filt_buffer[3] = ClipBuffer(emu_voice->filt_buffer[3]);

        emu_voice->filt_buffer[4] = ((emu_voice->filt_buffer[3] + t3) * coef0 - emu_voice->filt_buffer[4] * coef1) >> 24;
        emu_voice->filt_buffer[4] = ClipBuffer(emu_voice->filt_buffer[4]);

        emu_voice->filt_buffer[0] = ClipBuffer(dat);

        dat = (int32_t) (emu_voice->filt_buffer[4] >> 8);
        if (dat > 32767)
            dat = 32767;
        else if (dat < -32768)
            dat = -32768;

#elif defined FILTER_CONSTANT

        /* Apply expected attenuation. (FILTER_MOOG does it implicitly, but this one is constant gain).
         * Also stay at 24bits.*/
        dat = (dat * emu_voice->filt_att) >> 8;

        emu_voice->filt_buffer[0] = (coef1 * emu_voice->filt_buffer[0]
                                     + coef0 * (dat + ((coef2 * (emu_voice->filt_buffer[0] - emu_voice->filt_buffer[1])) >> 24)))
            >> 24;
        emu_voice->filt_buffer[1] = (coef1 * emu_voice->filt_buffer[1]
                                     + coef0 * emu_voice->filt_buffer[0])
            >> 24;

        emu_voice->filt_buffer[0] = ClipBuffer(emu_voice->filt_buffer[0]);
        emu_voice->filt_buffer[1] = ClipBuffer(emu_voice->filt_buffer[1]);

        dat = (int32_t) (emu_voice->filt_buffer[1] >> 8);
        if (dat > 32767)
            dat = 32767;
        else if (dat < -32768)
            dat = -32768;

#endif
    }
#endif /* FILTER_CHAMBERLIN */
    return dat;
}

static inline void
emu8k_voice_mix(emu8k_voice_t *emu_voice, int32_t dat, int32_t *mix,
                int32_t *reverb, int32_t *chorus)
{
    /* Apply the current volume before panning and effects sends. */
    dat = (dat * emu_voice->cvcf_curr_volume) >> 16;
    if (emu_voice->overshoot_samples >= 0) { /* AWE32Emu: see emu8k_overshoot */
        dat = (int32_t) (((int64_t) dat * emu8k_overshoot_q12(emu_voice->overshoot_samples)) >> 12);
        if (++emu_voice->overshoot_samples >= 8 * EMU8K_OVERSHOOT_STEP)
            emu_voice->overshoot_samples = -1;
    }
    mix[0] += (dat * emu_voice->vol_l) >> 8;
    mix[1] += (dat * emu_voice->vol_r) >> 8;
    *reverb += (dat * emu_voice->ptrx_revb_send) >> 8;
    *chorus += (dat * emu_voice->csl_chor_send) >> 8;
}

static inline uint64_t
emu8k_voice_envelope_tick(emu8k_voice_t *emu_voice)
{
    if (!emu_voice->env_engine_on)
        return freqtable[emu_voice->ip];

    int32_t attenuation  = emu_voice->initial_att;
    int32_t filtercut    = emu_voice->initial_filter;
    int32_t currentpitch = emu_voice->ip;

    /* run envelopes */
    emu8k_envelope_t *volenv = &emu_voice->vol_envelope;
    switch (volenv->state) {
        case ENV_DELAY:
            volenv->delay_samples--;
            if (volenv->delay_samples <= 0) {
                volenv->state         = ENV_ATTACK;
                volenv->delay_samples = 0;
            }
            attenuation = 0x1FFFFF;
            break;

        case ENV_ATTACK:
            /* Attack amount is in linear amplitude */
            volenv->value_amp_hz += volenv->attack_amount_amp_hz;
            if (volenv->value_amp_hz >= (1 << 21)) {
                volenv->value_amp_hz = 1 << 21;
                emu_voice->overshoot_samples = 0; /* AWE32Emu */
                volenv->value_db_oct = 0;
                if (volenv->hold_samples) {
                    volenv->state = ENV_HOLD;
                } else {
                    /* RAMP_UP since db value is inverted and it is 0 at this point. */
                    volenv->state = ENV_RAMP_UP;
                }
            }
            /* AWE32Emu: attack shape measured on the card. Upstream: value_amp_hz >> 5. */
            attenuation += env_vol_amplitude_to_db[emu8k_attack_amp_index(volenv->value_amp_hz)] << 5;
            break;

        case ENV_HOLD:
            volenv->hold_samples--;
            if (volenv->hold_samples <= 0) {
                volenv->state = ENV_RAMP_UP;
            }
            attenuation += volenv->value_db_oct;
            break;

        case ENV_RAMP_DOWN:
            /* Decay/release amount is in fraction of dBs and is always positive */
            volenv->value_db_oct -= emu8k_ramp_step(volenv); /* AWE32Emu: Q16 step */
            if (volenv->value_db_oct <= volenv->sustain_value_db_oct) {
                volenv->value_db_oct = volenv->sustain_value_db_oct;
                volenv->state        = ENV_SUSTAIN;
            }
            attenuation += volenv->value_db_oct;
            break;

        case ENV_RAMP_UP:
            /* Decay/release amount is in fraction of dBs and is always positive */
            volenv->value_db_oct += emu8k_ramp_step(volenv); /* AWE32Emu: Q16 step */
            if (volenv->value_db_oct >= volenv->sustain_value_db_oct) {
                volenv->value_db_oct = volenv->sustain_value_db_oct;
                volenv->state        = ENV_SUSTAIN;
            }
            attenuation += volenv->value_db_oct;
            break;

        case ENV_SUSTAIN:
            attenuation += volenv->value_db_oct;
            break;

        case ENV_STOPPED:
            attenuation = 0x1FFFFF;
            break;

        default:
            break;
    }

    emu8k_envelope_t *modenv = &emu_voice->mod_envelope;
    switch (modenv->state) {
        case ENV_DELAY:
            modenv->delay_samples--;
            if (modenv->delay_samples <= 0) {
                modenv->state         = ENV_ATTACK;
                modenv->delay_samples = 0;
            }
            break;

        case ENV_ATTACK:
            /* Attack amount is in linear amplitude */
            modenv->value_amp_hz += modenv->attack_amount_amp_hz;
            modenv->value_db_oct = emu8k_mod_attack_value(modenv->value_amp_hz); /* AWE32Emu: convex attack */
            if (modenv->value_amp_hz >= (1 << 21)) {
                modenv->value_amp_hz = 1 << 21;
                modenv->value_db_oct = 1 << 21;
                if (modenv->hold_samples) {
                    modenv->state = ENV_HOLD;
                } else {
                    modenv->state = ENV_RAMP_DOWN;
                }
            }
            break;

        case ENV_HOLD:
            modenv->hold_samples--;
            if (modenv->hold_samples <= 0) {
                modenv->state = ENV_RAMP_DOWN; /* AWE32Emu: upstream ramps UP here, which jumps straight to the sustain level instead of decaying */
            }
            break;

        case ENV_RAMP_DOWN:
            /* Decay/release amount is in fraction of octave and is always positive */
            modenv->value_db_oct -= emu8k_ramp_step(modenv); /* AWE32Emu: Q16 step */
            if (modenv->value_db_oct <= modenv->sustain_value_db_oct) {
                modenv->value_db_oct = modenv->sustain_value_db_oct;
                modenv->state        = ENV_SUSTAIN;
            }
            break;

        case ENV_RAMP_UP:
            /* Decay/release amount is in fraction of octave and is always positive */
            modenv->value_db_oct += emu8k_ramp_step(modenv); /* AWE32Emu: Q16 step */
            if (modenv->value_db_oct >= modenv->sustain_value_db_oct) {
                modenv->value_db_oct = modenv->sustain_value_db_oct;
                modenv->state        = ENV_SUSTAIN;
            }
            break;

        default:
            break;
    }

    /* run lfos */
    if (emu_voice->lfo1_delay_samples) {
        emu_voice->lfo1_delay_samples--;
    } else {
        emu_voice->lfo1_count.addr += emu_voice->lfo1_speed;
        emu_voice->lfo1_count.int_address &= 0xFFFF;
    }
    if (emu_voice->lfo2_delay_samples) {
        emu_voice->lfo2_delay_samples--;
    } else {
        emu_voice->lfo2_count.addr += emu_voice->lfo2_speed;
        emu_voice->lfo2_count.int_address &= 0xFFFF;
    }

    if (emu_voice->fixed_modenv_pitch_height) {
        /* modenv range 1<<21, pitch height range 1<<14 desired range 0x1000 (+/-one octave) */
        currentpitch += ((modenv->value_db_oct >> 9) * emu_voice->fixed_modenv_pitch_height) >> 14;
    }

    if (emu_voice->fixed_lfo1_vibrato) {
        /* table range 1<<15, pitch mod range 1<<14 desired range 0x1000 (+/-one octave) */
        int32_t lfo1_vibrato = (lfotable[emu_voice->lfo1_count.int_address] * emu_voice->fixed_lfo1_vibrato) >> 17;
        currentpitch += lfo1_vibrato;
    }
    if (emu_voice->fixed_lfo2_vibrato) {
        /* table range 1<<15, pitch mod range 1<<14 desired range 0x1000 (+/-one octave) */
        int32_t lfo2_vibrato = (lfotable[emu_voice->lfo2_count.int_address] * emu_voice->fixed_lfo2_vibrato) >> 17;
        currentpitch += lfo2_vibrato;
    }

    if (emu_voice->fixed_modenv_filter_height) {
        /* modenv range 1<<21, pitch height range 1<<14 desired range 0x200000 (+/-full filter range) */
        filtercut += ((modenv->value_db_oct >> 9) * emu_voice->fixed_modenv_filter_height) >> 5;
    }

    if (emu_voice->fixed_lfo1_filt_mod) {
        /* table range 1<<15, pitch mod range 1<<14 desired range 0x100000 (+/-three octaves) */
        int32_t lfo1_filtmod = (lfotable[emu_voice->lfo1_count.int_address] * emu_voice->fixed_lfo1_filt_mod) >> 9;
        filtercut += lfo1_filtmod;
    }

    if (emu_voice->fixed_lfo1_tremolo) {
        /* table range 1<<15, pitch mod range 1<<14 desired range 0x40000 (+/-12dBs). */
        int32_t lfo1_tremolo = (lfotable[emu_voice->lfo1_count.int_address] * emu_voice->fixed_lfo1_tremolo) >> 11;
        /* AWE32Emu: the card only attenuates (block 17), upstream adds both ways */
        if (lfo1_tremolo < 0)
            attenuation -= lfo1_tremolo;
    }

    /* AWE32Emu: filter modulation in octaves for the Chamberlin filter */
    emu_voice->filt_oct_target = (double) modenv->value_db_oct / (double) (1 << 21) * emu_voice->pefe_modenv_filter_height / 127.0 * 6.0
        + lfotable[emu_voice->lfo1_count.int_address] / 32768.0 * emu_voice->fmmod_lfo1_filt_mod / 127.0 * 3.0;

    if (currentpitch > 0xFFFF)
        currentpitch = 0xFFFF;
    if (currentpitch < 0)
        currentpitch = 0;
    if (attenuation > 0x1FFFFF)
        attenuation = 0x1FFFFF;
    if (attenuation < 0)
        attenuation = 0;
    if (filtercut > 0x1FFFFF)
        filtercut = 0x1FFFFF;
    if (filtercut < 0)
        filtercut = 0;

    emu_voice->vtft_vol_target    = env_vol_db_to_vol_target[attenuation >> 5];
    emu_voice->vtft_filter_target = filtercut >> 5;
    emu_voice->ptrx_pit_target    = freqtable[currentpitch] >> 18;
    return freqtable[currentpitch];
}

static inline void
emu8k_voice_latch(emu8k_voice_t *emu_voice)
{
    emu_voice->cvcf_curr_volume     = emu8k_vol_slide(&emu_voice->volumeslide, emu_voice->vtft_vol_target);
    emu_voice->cvcf_curr_filt_ctoff = emu_voice->vtft_filter_target;
    emu_voice->filt_oct_curr        = emu_voice->filt_oct_target; /* AWE32Emu */
}

unsigned
emu8k_pci_voice_tick(emu8k_voice_t *voice, emu8k_sample_read_t read_sample,
                    const void *context, int32_t *mix, emu8k_effect_sends_t *sends)
{
    if (voice->loop_end.addr <= voice->loop_start.addr)
        return 0;

    if (voice->cvcf_curr_volume) {
        const int32_t dat = emu8k_voice_sample(voice, read_sample, context, SOUND_FREQ);
        emu8k_voice_mix(voice, dat, mix, &sends->reverb, &sends->chorus);
    }

    const uint64_t target = emu8k_voice_envelope_tick(voice);
    /* Keep the full precision of IP once the current pitch reaches its target. */
    const uint64_t step = voice->cpf_curr_pitch == voice->ptrx_pit_target
        ? target
        : (uint64_t) voice->cpf_curr_pitch << 18;
    const unsigned crossed = emu8k_advance_voice(voice, step, 1);
    emu8k_voice_latch(voice);
    emu8k_update_voice_address(voice);
    return crossed;
}

static void /* AWE32Emu: one chunk; emu8k_update() below splits at state dump frames */
emu8k_update_chunk(emu8k_t *emu8k)
{
    if (emu8k->pos >= wavetable_pos_global)
        return;

    const int      num_samples = wavetable_pos_global - emu8k->pos;
    int32_t       *buf;
    emu8k_voice_t *emu_voice;
    int            pos;
    int            num_active = 0;

    /* Voices section  */
    for (uint8_t c = 0; c < EMU8K_VOICE_COUNT; c++) {
        if (emu8k->pci_voices & (1u << c))
            continue;
        emu_voice = &emu8k->voice[c];

        if (emu_voice->env_engine_on || emu_voice->cvcf_curr_volume)
            num_active++;

        for (pos = emu8k->pos; pos < wavetable_pos_global; pos++) {
            if (emu_voice->cvcf_curr_volume) {
                int32_t dat = emu8k_voice_sample(emu_voice, emu8k_ram_sample, emu8k, WT_FREQ);
                if ((emu8k->hwcf3 & 0x04) && !CCCA_DMA_ACTIVE(emu_voice->ccca))
                    emu8k_voice_mix(emu_voice, dat, &emu8k->buffer[pos * 2],
                                    &emu8k->reverb_in_buffer[pos], &emu8k->chorus_in_buffer[pos]);
            }

            emu8k_voice_envelope_tick(emu_voice);
            /*
            I've recopilated these sentences to get an idea of how to loop

            - Set its PSST register and its CLS register to zero to cause no loops to occur.
            -Setting the Loop Start Offset and the Loop End Offset to the same value, will cause the oscillator to loop the entire memory.

            -Setting the PlayPosition greater than the Loop End Offset, will cause the oscillator to play in reverse, back to the Loop End Offset.
               It's pretty neat, but appears to be uncontrollable (the rate at which the samples are played in reverse).

            -Note that due to interpolator offset, the actual loop point is one greater than the start address
            -Note that due to interpolator offset, the actual loop point will end at an address one greater than the loop address
            -Note that the actual audio location is the point 1 word higher than this value due to interpolation offset
            -In programs that use the awe, they generally set the loop address as "loopaddress -1" to compensate for the above.
            (Note: I am already using address+1 in the interpolators so these things are already as they should.)
            */
            emu8k_advance_voice(emu_voice, (uint64_t) emu_voice->cpf_curr_pitch << 18, 0);

            emu8k_voice_latch(emu_voice);
        }

        /* Update EMU voice registers. */
        emu8k_update_voice_address(emu_voice);

#if 0
        if (emu_voice->cvcf_curr_volume != old_vol[c]) {
            pclog("EMUVOL (%d):%d\n", c, emu_voice->cvcf_curr_volume);
            old_vol[c]=emu_voice->cvcf_curr_volume;
        }
        pclog("EMUFILT :%d\n", emu_voice->cvcf_curr_filt_ctoff);
#endif
    }

    /* AWE32Emu: the effects always run, like on the chip. Upstream runs them
     * only when a voice was active in this call; the result then depends on
     * how the emulator splits the audio into update calls (the VM does it
     * at every port write), and the chorus LFO and delay lines of the VM and
     * of a replay of the same trace drift apart (AWETST25 blocks 23, 26, 27). */
    (void) num_active;
    {
        buf = &emu8k->buffer[emu8k->pos * 2];
#ifdef REVERB_FITTED
        emu8k_work_reverb_fitted(emu8k, &emu8k->reverb_in_buffer[emu8k->pos], buf, num_samples); /* AWE32Emu */
#else
        emu8k_work_reverb(&emu8k->reverb_in_buffer[emu8k->pos], buf, &emu8k->reverb_engine, num_samples);
#endif
        emu8k_work_chorus(&emu8k->chorus_in_buffer[emu8k->pos], buf, &emu8k->chorus_engine, num_samples);
        emu8k_work_eq(emu8k, buf, num_samples);
        /* AWE32Emu: output level -1.1 dB (57737/65536). Card line out,
         * AWETST28 block 43, 1..8 voices of one sine in phase: the sum clips
         * at +8.6 / +9.3 / +9.8 / +10.0 dB over one voice for 3 / 4 / 6 / 8
         * voices, the same with the wavetable mixer 12 dB lower (so it is the
         * chip, not the analog stage); the 16 bit clip gave +7.8 / +8.3 /
         * +8.7 / +8.9. One voice sits 1.1 dB lower under the clip on the
         * card. */
        for (int i = 0; i < 2 * num_samples; i++)
            buf[i] = (int32_t) (((int64_t) buf[i] * 57737) >> 16);
    }

    /* Update EMU clock. */
    emu8k->wc += num_samples;

    emu8k->pos = wavetable_pos_global;
}

/* AWE32Emu: local addition, not upstream. Layered state dump.
 *
 * EMU8K_STATE_DUMP=<file> writes the chip state every EMU8K_STATE_STEP frames
 * (default 4410) of the chip's own 44100 Hz timebase, counted from
 * emu8k_init(). The update is split exactly at those frames, which changes
 * nothing in the output (everything runs per sample), so a VM run and a
 * replay of its trace give lines for the same frames. One line per layer:
 *   S <frame> out <l> <r> | cho <write> <lfo> | rev <preset> <pre_pos> <comb_pos> | eq <bass> <treble>
 *   R <frame> <voice> ccca cpf ptrx cvcf vtft psst csl ip ifatn pefe fmmod tremfrq fm2frq2
 *     envvol dcysusv atkhldv envval dcysus atkhld lfo1val lfo2val
 *   E <frame> <voice> engine vstate vamp vdb vramp mstate mamp mdb mramp initial_att initial_filter
 *   O <frame> <voice> addr loop_start loop_end cpf_pitch lfo1 lfo2 lfo1_delay lfo2_delay vol_l vol_r
 *   F <frame> <voice> q att oct_target oct_curr lp bp
 * Voice lines only for voices that sound or run their envelopes. */
static void
emu8k_state_dump_open(emu8k_t *emu8k)
{
    const char *path = getenv("EMU8K_STATE_DUMP");
    const char *step = getenv("EMU8K_STATE_STEP");
    emu8k->state_frame = 0;
    emu8k->state_step  = (step && atoi(step) > 0) ? atoi(step) : 4410;
    emu8k->state_dump  = (path && *path) ? fopen(path, "w") : NULL;
}

static void
emu8k_state_dump_write(emu8k_t *emu8k)
{
    FILE *f = (FILE *) emu8k->state_dump;
    if (!f)
        return;
    const uint64_t fr   = emu8k->state_frame;
    const int      last = (emu8k->pos > 0) ? emu8k->pos - 1 : 0;
    fprintf(f, "S %" PRIu64 " out %d %d | cho %d %u | rev %d %d %d | eq %d %d\n", fr,
            emu8k->buffer[last * 2], emu8k->buffer[last * 2 + 1],
            emu8k->chorus_engine.write, emu8k->chorus_engine.lfo_pos.int_address,
            emu8k->rv_preset, emu8k->rv_pre_pos, emu8k->rv_comb_pos[0][0],
            emu8k->eq_bass, emu8k->eq_treble);
    for (int c = 0; c < 32; c++) {
        const emu8k_voice_t *v = &emu8k->voice[c];
        if (!v->env_engine_on && !v->cvcf_curr_volume && !v->vtft_vol_target)
            continue;
        fprintf(f, "R %" PRIu64 " %d %08X %08X %08X %08X %08X %08X %08X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X\n",
                fr, c, v->ccca, v->cpf, v->ptrx, v->cvcf, v->vtft, v->psst, v->csl, v->ip, v->ifatn, v->pefe,
                v->fmmod, v->tremfrq, v->fm2frq2, v->envvol, v->dcysusv, v->atkhldv, v->envval, v->dcysus,
                v->atkhld, v->lfo1val, v->lfo2val);
        fprintf(f, "E %" PRIu64 " %d %d %d %d %d %d %d %d %d %d %d %d\n", fr, c, v->env_engine_on,
                v->vol_envelope.state, v->vol_envelope.value_amp_hz, v->vol_envelope.value_db_oct,
                v->vol_envelope.ramp_frac, v->mod_envelope.state, v->mod_envelope.value_amp_hz,
                v->mod_envelope.value_db_oct, v->mod_envelope.ramp_frac, v->initial_att, v->initial_filter);
        fprintf(f, "O %" PRIu64 " %d %" PRIu64 " %u %u %u %" PRIu64 " %" PRIu64 " %d %d %d %d\n", fr, c,
                v->addr.addr, v->loop_start.int_address, v->loop_end.int_address, v->cpf_curr_pitch,
                v->lfo1_count.addr, v->lfo2_count.addr, v->lfo1_delay_samples, v->lfo2_delay_samples,
                v->vol_l, v->vol_r);
        fprintf(f, "F %" PRIu64 " %d %d %d %.9g %.9g %.9g %.9g\n", fr, c, v->filterq_idx, v->filt_att,
                v->filt_oct_target, v->filt_oct_curr, v->cham_lp, v->cham_bp);
    }
}

void
emu8k_update(emu8k_t *emu8k)
{
    if (!emu8k->state_dump) {
        const int before = emu8k->pos;
        emu8k_update_chunk(emu8k);
        if (emu8k->pos > before)
            emu8k->state_frame += (uint64_t) (emu8k->pos - before);
        return;
    }
    /* Split the call at the dump frames. */
    const int target = wavetable_pos_global;
    while (emu8k->pos < target) {
        const uint64_t to_dump = emu8k->state_step - (emu8k->state_frame % emu8k->state_step);
        int            end     = target;
        if ((uint64_t) (target - emu8k->pos) >= to_dump)
            end = emu8k->pos + (int) to_dump;
        const int before     = emu8k->pos;
        wavetable_pos_global = end;
        emu8k_update_chunk(emu8k);
        emu8k->state_frame += (uint64_t) (emu8k->pos - before);
        if (emu8k->state_frame % emu8k->state_step == 0)
            emu8k_state_dump_write(emu8k);
    }
    wavetable_pos_global = target;
}

void
emu8k_reset_buffer(emu8k_t *emu8k)
{
    emu8k->pos = 0;
    memset(emu8k->buffer, 0, sizeof(emu8k->buffer));
    memset(emu8k->chorus_in_buffer, 0, sizeof(emu8k->chorus_in_buffer));
    memset(emu8k->reverb_in_buffer, 0, sizeof(emu8k->reverb_in_buffer));
}

uint32_t
emu8k_inl(uint16_t addr, void *priv)
{
    uint32_t lo = emu8k_inw(addr, priv);
    uint32_t hi = emu8k_inw(addr + 2, priv);
    return lo | (hi << 16);
}

void
emu8k_outl(uint16_t addr, uint32_t val, void *priv)
{
    emu8k_outw(addr, (uint16_t) (val & 0xffffu), priv);
    emu8k_outw(addr + 2, (uint16_t) (val >> 16), priv);
}

void
emu8k_change_addr(emu8k_t *emu8k, uint16_t emu_addr)
{
    if (emu8k->addr) {
        io_removehandler(emu8k->addr, 0x0004, emu8k_inb, emu8k_inw, emu8k_inl, emu8k_outb, emu8k_outw, emu8k_outl, emu8k);
        io_removehandler(emu8k->addr + 0x400, 0x0004, emu8k_inb, emu8k_inw, emu8k_inl, emu8k_outb, emu8k_outw, emu8k_outl, emu8k);
        io_removehandler(emu8k->addr + 0x800, 0x0004, emu8k_inb, emu8k_inw, emu8k_inl, emu8k_outb, emu8k_outw, emu8k_outl, emu8k);
        emu8k->addr = 0;
    }
    if (emu_addr) {
        emu8k->addr = emu_addr;
        io_sethandler(emu8k->addr, 0x0004, emu8k_inb, emu8k_inw, emu8k_inl, emu8k_outb, emu8k_outw, emu8k_outl, emu8k);
        io_sethandler(emu8k->addr + 0x400, 0x0004, emu8k_inb, emu8k_inw, emu8k_inl, emu8k_outb, emu8k_outw, emu8k_outl, emu8k);
        io_sethandler(emu8k->addr + 0x800, 0x0004, emu8k_inb, emu8k_inw, emu8k_inl, emu8k_outb, emu8k_outw, emu8k_outl, emu8k);
    }
}

/* onboard_ram in kilobytes */
void
emu8k_init(emu8k_t *emu8k, uint16_t emu_addr, int onboard_ram)
{
    uint32_t const BLOCK_SIZE_WORDS = 0x10000;
    FILE          *fp;
    int            c;
    double         out;

    fp = rom_fopen(EMU8K_ROM_PATH, "rb");
    if (!fp && emu_addr != 0)
        fatal("AWE32.RAW not found\n");

    emu8k->rom = calloc(1024, 1024);
    /* AWE32Emu: also accept a card dump without the leading AWE-DUMP word
     * (1048574 bytes); the missing last word stays zero. */
    if (fp) {
        if (fread(emu8k->rom, 1, 1048576, fp) < 1048574)
            fatal("emu8k_init(): Error reading data\n");
        fclose(fp);
    }
    /*AWE-DUMP creates ROM images offset by 2 bytes, so if we detect this
      then correct it*/
    if (emu8k->rom[3] == 0x314d && emu8k->rom[4] == 0x474d) {
        memmove(&emu8k->rom[0], &emu8k->rom[1], (1024 * 1024) - 2);
        emu8k->rom[0x7ffff] = 0;
    }

    emu8k->empty = calloc(2, BLOCK_SIZE_WORDS);

    int j = 0;
    for (; j < 0x8; j++) {
        emu8k->ram_pointers[j] = emu8k->rom + (j * BLOCK_SIZE_WORDS);
    }
    for (; j < 0x20; j++) {
        emu8k->ram_pointers[j] = emu8k->empty;
    }

    if (onboard_ram) {
        /*Clip to 28MB, since that's the max that we can address. */
        if (onboard_ram > 0x7000)
            onboard_ram = 0x7000;
        emu8k->ram = calloc(1024, onboard_ram);
        const int i_end = onboard_ram >> 7;
        int       i     = 0;
        for (; i < i_end; i++, j++) {
            emu8k->ram_pointers[j] = emu8k->ram + (i * BLOCK_SIZE_WORDS);
        }
        emu8k->ram_end_addr = EMU8K_RAM_MEM_START + (onboard_ram << 9);
    } else {
        emu8k->ram          = 0;
        emu8k->ram_end_addr = EMU8K_RAM_MEM_START;
    }
    for (; j < 0x100; j++) {
        emu8k->ram_pointers[j] = emu8k->empty;
    }

    emu8k_reset_buffer(emu8k);

    emu8k_change_addr(emu8k, emu_addr);

    /*Create frequency table. (Convert initial pitch register value to a linear speed change)
     * The input is encoded such as 0xe000 is center note (no pitch shift)
     * and from then on , changing up or down 0x1000 (4096) increments/decrements an octave.
     * Note that this is in reference to the 44.1Khz clock that the channels play at.
     * The 65536 * 65536 is in order to left-shift the 32bit value to a 64bit value as a 32.32 fixed point.
     */
    for (c = 0; c < 0x10000; c++) {
        freqtable[c] = (uint64_t) (exp2((double) (c - 0xe000) / 4096.0) * 65536.0 * 65536.0);
    }
    /* Shortcut: minimum pitch equals stopped. I don't really know if this is true, but it's better
     * since some programs set the pitch to 0 for unused channels. */
    freqtable[0] = 0;

    /* starting at 65535 because it is used for "volume target" register conversion. */
    out = 65535.0;
    for (c = 0; c < 256; c++) {
        attentable[c] = (int32_t) out;
        out /= sqrt(1.09018); /*0.375 dB steps*/
    }
    /* Shortcut: max attenuation is silent, not -96dB. */
    attentable[255] = 0;

    /* Note: these two tables have "db" inverted: 0 dB is max volume, 65535 "db" (-96.32dBFS) is silence.
     * Important: Using 65535 as max output value because this is intended to be used with the volume target register! */
    out = 65535.0;
    for (c = 0; c < 0x10000; c++) {
#if 0
        double db = -(c*6.0205999/65535.0)*16.0;
        out = powf(10.f,db/20.f) * 65536.0;
#endif
        env_vol_db_to_vol_target[c] = (int32_t) out;
        /* calculated from the 65536th root of 65536 */
        out /= 1.00016923970;
    }
    /* Shortcut: max attenuation is silent, not -96dB. */
    env_vol_db_to_vol_target[0x10000 - 1] = 0;
    /* One more position to accept max value being 65536. */
    env_vol_db_to_vol_target[0x10000] = 0;

    for (c = 1; c < 0x10000; c++) {
        out                        = -680.32142884264 * 20.0 * log10(((double) c) / 65535.0);
        env_vol_amplitude_to_db[c] = (int32_t) out;
    }
    /*Shortcut: max attenuation is silent, not -96dB.*/
    env_vol_amplitude_to_db[0] = 65535;
    /* One more position to accept max value being 65536. */
    env_vol_amplitude_to_db[0x10000] = 0;

    for (c = 1; c < 0x10000; c++) {
        out                        = log2((((double) c) / 0x10000) + 1.0) * 65536.0;
        env_mod_hertz_to_octave[c] = (int32_t) out;
    }
    /*No hertz change, no octave change. */
    env_mod_hertz_to_octave[0] = 0;
    /* One more position to accept max value being 65536. */
    env_mod_hertz_to_octave[0x10000] = 65536;

    /* AWE32Emu: replaced by a formula derived from the **conversion table
     * in the real Creative driver** (SBAWE32.DRV, attack time table at
     * ds:1552, 127 entries in ms). Verified against that table byte by byte,
     * 0 deviations.
     *
     * The original formula came from awe32p10 (Vince Vu / Judge Dredd), an
     * unofficial description, and it differs from the driver table: up to
     * rate 31 it is exact, from 32 up it is off by up to 3.1 %. The driver
     * decides - Creative wrote it as well as the chip.
     *
     * The encoding is a 7-bit "float": the first 16 values divisor 1..16,
     * the next 16 divisor 17..32, and with every further group of 16 the
     * step doubles.  time = 11878 ms / divisor. */
    float millis;
    for (c = 0; c < 128; c++) {
        if (c == 0) {
            millis = 0; /* This means never attack. */
        } else {
            const int idx   = (c > 127 ? 127 : c) - 1;
            const int group = (idx >> 4) & 7;
            const int m     = idx & 15;
            const int div   = (group == 0) ? (m + 1) : ((m + 17) << (group - 1));
            millis          = 11878.0 / div;
        }

        env_attack_to_samples[c] = 44.1 * millis;
    }

    /* AWE32Emu: decay/release from the Creative table (see the declaration of
     * env_decay_to_dbs_or_oct): 100 dB in 47513 ms / divisor, as Q16 value
     * units per sample (1 << 21 = 96 dB). Rate 0 = no decay. */
    env_decay_to_dbs_or_oct[0] = 0;
    for (c = 1; c < 128; c++) {
        const int    idx      = c - 1;
        const int    group    = (idx >> 4) & 7;
        const int    m        = idx & 15;
        const int    div      = (group == 0) ? (m + 1) : ((m + 17) << (group - 1));
        const double db_per_s = 100.0 / (47.513 / div);
        env_decay_to_dbs_or_oct[c] = (int32_t) (db_per_s / 44100.0 * ((double) (1 << 21) / 96.0) * 65536.0 + 0.5);
    }
    emu8k->eq_bass   = -1; /* AWE32Emu: equalizer not designed yet */
    emu8k->eq_treble = -1;
    emu8k_rv_init(emu8k); /* AWE32Emu: fitted reverb */
    emu8k_state_dump_open(emu8k); /* AWE32Emu: layered state dump */

    /* The LFOs use a triangular waveform starting at zero and going 1/-1/1/-1.
     * This table is stored in signed 16bits precision, with a period of 65536 samples */
    for (c = 0; c < 65536; c++) {
        int d = (c + 16384) & 65535;
        if (d >= 32768)
            lfotable[c] = 32768 + ((32768 - d) * 2);
        else
            lfotable[c] = (d * 2) - 32768;
    }
    /* The 65536 * 65536 is in order to left-shift the 32bit value to a 64bit value as a 32.32 fixed point. */
    out = 0.01;
    for (c = 0; c < 256; c++) {
        lfofreqtospeed[c] = (uint64_t) (out * 65536.0 / 44100.0 * 65536.0 * 65536.0);
        out += 0.042;
    }

    for (c = 0; c < 65536; c++) {
        chortable[c] = sin(c * M_PI / 32768.0);
    }

    /* Filter coefficients tables. Note: Values are multiplied by *16777216 to left shift 24 bits. (i.e. 8.24 fixed point) */
    for (uint8_t qidx = 0; qidx < 16; qidx++) {
        out = 125.0; /* Start at 125Hz */
        for (c = 0; c < 256; c++) {
#ifdef FILTER_INITIAL
            float w0 = sin(2.0 * M_PI * out / 44100.0);
            /* The value 102.5f has been selected a bit randomly. Pretends to reach 0.2929 at w0 = 1.0 */
            float q = (qidx / 102.5f) * (1.0 + 1.0 / w0);
            /* Limit max value. Else it would be 470. */
            if (q > 200)
                q = 200;
            filt_coeffs[qidx][c][0] = (int32_t) (w0 * 16777216.0);
            filt_coeffs[qidx][c][1] = 16777216.0;
            filt_coeffs[qidx][c][2] = (int32_t) ((1.0f / (0.7071f + q)) * 16777216.0);
#elif defined FILTER_MOOG
            float w0 = sin(2.0 * M_PI * out / 44100.0);
            float q_factor = 1.0f - w0;
            float p = w0 + 0.8f * w0 * q_factor;
            float f = p + p - 1.0f;
            float resonance = (1.0 - pow(2.0, -qidx * 24.0 / 90.0)) * 0.8;
            float q = resonance * (1.0f + 0.5f * q_factor * (w0 + 5.6f * q_factor * q_factor));
            filt_coeffs[qidx][c][0] = (int32_t) (p * 16777216.0);
            filt_coeffs[qidx][c][1] = (int32_t) (f * 16777216.0);
            filt_coeffs[qidx][c][2] = (int32_t) (q * 16777216.0);
#elif defined FILTER_CONSTANT
            float q = (1.0 - pow(2.0, -qidx * 24.0 / 90.0)) * 0.8;
            float coef0 = sin(2.0 * M_PI * out / 44100.0);
            float coef1 = 1.0 - coef0;
            float coef2 = q * (1.0 + 1.0 / coef1);
            filt_coeffs[qidx][c][0] = (int32_t) (coef0 * 16777216.0);
            filt_coeffs[qidx][c][1] = (int32_t) (coef1 * 16777216.0);
            filt_coeffs[qidx][c][2] = (int32_t) (coef2 * 16777216.0);
#endif // FILTER_TYPE
            /* 42.66 divisions per octave (the doc says quarter seminotes which is 48, but then it would be almost an octave less) */
            out *= 1.016378315;
            /* 42 divisions. This moves the max frequency to 8.5Khz.*/
            // out *= 1.0166404394;
            /* This is a linear increment method, that corresponds to the NRPN table, but contradicts the EMU8KPRM doc: */
            // out = 100.0 + (c+1.0)*31.25; //31.25Hz steps */
        }
    }
    /* NOTE! read_pos and buffer content is implicitly initialized to zero by the sb_t structure memset on sb_awe32_init() */
    emu8k->reverb_engine.reflections[0].bufsize = 2 * REV_BUFSIZE_STEP;
    emu8k->reverb_engine.reflections[1].bufsize = 4 * REV_BUFSIZE_STEP;
    emu8k->reverb_engine.reflections[2].bufsize = 8 * REV_BUFSIZE_STEP;
    emu8k->reverb_engine.reflections[3].bufsize = 13 * REV_BUFSIZE_STEP;
    emu8k->reverb_engine.reflections[4].bufsize = 19 * REV_BUFSIZE_STEP;
    emu8k->reverb_engine.reflections[5].bufsize = 26 * REV_BUFSIZE_STEP;

    /*This is a bit random.*/
    for (c = 0; c < 4; c++) {
        emu8k->reverb_engine.allpass[3 - c].feedback = 0.5;
        emu8k->reverb_engine.allpass[3 - c].bufsize  = (4 * c) * REV_BUFSIZE_STEP + 55;
        emu8k->reverb_engine.allpass[7 - c].feedback = 0.5;
        emu8k->reverb_engine.allpass[7 - c].bufsize  = (4 * c) * REV_BUFSIZE_STEP + 55;
    }

    /* Cubic Resampling  ( 4point cubic spline) */
    double const resdouble = 1.0 / (double) CUBIC_RESOLUTION;
    for (c = 0; c < CUBIC_RESOLUTION; c++) {
        double x = (double) c * resdouble;
        /* Cubic resolution is made of four table, but I've put them all in one table to optimize memory access. */
        cubic_table[c * 4]     = (-0.5 * x * x * x + x * x - 0.5 * x);
        cubic_table[c * 4 + 1] = (1.5 * x * x * x - 2.5 * x * x + 1.0);
        cubic_table[c * 4 + 2] = (-1.5 * x * x * x + 2.0 * x * x + 0.5 * x);
        cubic_table[c * 4 + 3] = (0.5 * x * x * x - 0.5 * x * x);
    }
    /* Even when the documentation says that this has to be written by applications to initialize the card,
     * several applications and drivers ( aweman on windows, linux oss driver..) read it to detect an AWE card. */
    emu8k->hwcf1 = 0x59;
    emu8k->hwcf2 = 0x20;
    /* Initial state is muted. 0x04 is unmuted. */
    emu8k->hwcf3 = 0x00;
}

void
emu8k_close(emu8k_t *emu8k)
{
    if (emu8k->rom)
        free(emu8k->rom);
    if (emu8k->ram)
        free(emu8k->ram);
}
