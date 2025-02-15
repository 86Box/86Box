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

#if !defined RESAMPLER_LINEAR && !defined RESAMPLER_CUBIC
#if 0
#define RESAMPLER_LINEAR
#endif
#    define RESAMPLER_CUBIC
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
static int32_t env_decay_to_dbs_or_oct[128] = {
       0,    1,    2,    3,    4,    5,    6,    7,    8,    9,   10,   11,   12,   13,   14,   15,
      16,   17,   18,   19,   20,   20,   21,   22,   23,   24,   25,   27,   28,   29,   30,   32,
      33,   34,   36,   38,   39,   41,   43,   45,   49,   51,   53,   55,   58,   60,   63,   66,
      69,   72,   75,   78,   82,   85,   89,   93,   97,  102,  106,  111,  116,  121,  126,  132,
     138,  144,  150,  157,  164,  171,  179,  186,  195,  203,  212,  222,  232,  243,  253,  264,
     276,  288,  301,  315,  328,  342,  358,  374,  390,  406,  425,  444,  466,  485,  506,  528,
     553,  580,  602,  634,  660,  689,  721,  755,  780,  820,  849,  897,  932,  970, 1012, 1057,
    1106, 1160, 1219, 1285, 1321, 1399, 1441, 1534, 1585, 1640, 1698, 1829, 1902, 1981, 2068, 2162
};

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
    65536, 61869, 57079, 53269, 49145, 44820, 40877, 34792, 32845, 30653, 28607,
    26392, 24630, 22463, 20487, 18470
};

/*Coefficients for the filters for a defined Q and cutoff.*/
static int32_t filt_coeffs[16][256][3];

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

static inline int16_t
EMU8K_READ(emu8k_t *emu8k, uint32_t addr)
{
    register const emu8k_mem_pointers_t addrmem = { { addr } };
    return emu8k->ram_pointers[addrmem.hb_address][addrmem.lw_address];
}

#if NOTUSED
static inline int16_t
EMU8K_READ_INTERP_LINEAR(emu8k_t *emu8k, uint32_t int_addr, uint16_t fract)
{
    /* The interpolation in AWE32 used a so-called patented 3-point interpolation
     * ( I guess some sort of spline having one point before and one point after).
     * Also, it has the consequence that the playback is delayed by one sample.
     * I simulate the "one sample later" than the address with addr+1 and addr+2
     * instead of +0 and +1 */
    int16_t dat1 = EMU8K_READ(emu8k, int_addr + 1);
    int32_t dat2 = EMU8K_READ(emu8k, int_addr + 2);
    dat1 += ((dat2 - (int32_t) dat1) * fract) >> 16;
    return dat1;
}
#endif

static inline int32_t
EMU8K_READ_INTERP_CUBIC(emu8k_t *emu8k, uint32_t int_addr, uint16_t fract)
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
    int32_t       dat2  = EMU8K_READ(emu8k, int_addr + 1);
    const float  *table = &cubic_table[fract];
    const int32_t dat1  = EMU8K_READ(emu8k, int_addr);
    const int32_t dat3  = EMU8K_READ(emu8k, int_addr + 2);
    const int32_t dat4  = EMU8K_READ(emu8k, int_addr + 3);
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

uint16_t
emu8k_inw(uint16_t addr, void *priv)
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
                        val = 0x1c | ((emu8k->id & 0x0002) ? 0xff02 : 0);
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
            switch (emu8k->cur_reg) {
                case 0:
                    READ16(addr, emu8k->voice[emu8k->cur_voice].cpf);
                    return ret;

                case 1:
                    READ16(addr, emu8k->voice[emu8k->cur_voice].ptrx);
                    return ret;

                case 2:
                    READ16(addr, emu8k->voice[emu8k->cur_voice].cvcf);
                    return ret;

                case 3:
                    READ16(addr, emu8k->voice[emu8k->cur_voice].vtft);
                    return ret;

                case 4:
                    READ16(addr, emu8k->voice[emu8k->cur_voice].unknown_data0_4);
                    return ret;

                case 5:
                    READ16(addr, emu8k->voice[emu8k->cur_voice].unknown_data0_5);
                    return ret;

                case 6:
                    READ16(addr, emu8k->voice[emu8k->cur_voice].psst);
                    return ret;

                case 7:
                    READ16(addr, emu8k->voice[emu8k->cur_voice].csl);
                    return ret;

                default:
                    break;
            }
            break;

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
                    return 0x1c | ((emu8k->id & 0x0002) ? 0xff02 : 0);

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
            switch (emu8k->cur_reg) {
                case 0:
                    /* The docs says that this value is constantly updating, and it should have no actual effect. Actions should be done over ptrx */
                    WRITE16(addr, emu8k->voice[emu8k->cur_voice].cpf, val);
                    return;

                case 1:
                    WRITE16(addr, emu8k->voice[emu8k->cur_voice].ptrx, val);
                    return;

                case 2:
                    /* The docs says that this value is constantly updating, and it should have no actual effect. Actions should be done over vtft */
                    WRITE16(addr, emu8k->voice[emu8k->cur_voice].cvcf, val);
                    return;

                case 3:
                    WRITE16(addr, emu8k->voice[emu8k->cur_voice].vtft, val);
                    return;

                case 4:
                    WRITE16(addr, emu8k->voice[emu8k->cur_voice].unknown_data0_4, val);
                    return;

                case 5:
                    WRITE16(addr, emu8k->voice[emu8k->cur_voice].unknown_data0_5, val);
                    return;

                case 6:
                    {
                        emu8k_voice_t *emu_voice = &emu8k->voice[emu8k->cur_voice];
                        WRITE16(addr, emu_voice->psst, val);
                        /* TODO: Should we update only on MSB update, or this could be used as some sort of hack by applications? */
                        emu_voice->loop_start.int_address = emu_voice->psst & EMU8K_MEM_ADDRESS_MASK;
                        if (addr & 2) {
                            emu_voice->vol_l = emu_voice->psst_pan;
                            emu_voice->vol_r = 255 - (emu_voice->psst_pan);
                        }
                    }
                    return;

                case 7:
                    WRITE16(addr, emu8k->voice[emu8k->cur_voice].csl, val);
                    /* TODO: Should we update only on MSB update, or this could be used as some sort of hack by applications? */
                    emu8k->voice[emu8k->cur_voice].loop_end.int_address = emu8k->voice[emu8k->cur_voice].csl & EMU8K_MEM_ADDRESS_MASK;
                    return;

                default:
                    break;
            }
            break;

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

                            // reset lfos.
                            emu8k->voice[emu8k->cur_voice].lfo1_count.addr = 0;
                            emu8k->voice[emu8k->cur_voice].lfo2_count.addr = 0;
                            // Trigger envelopes
                            if (ATKHLDV_TRIGGER(emu8k->voice[emu8k->cur_voice].atkhldv)) {
                                vol_env->value_amp_hz = 0;
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
                                vol_env->value_db_oct = env_vol_amplitude_to_db[vol_env->value_amp_hz >> 5] << 5;
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
                                mod_env->value_db_oct = env_mod_hertz_to_octave[mod_env->value_amp_hz >> 9] << 9;
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
                        emu_voice->addr.int_address = emu_voice->ccca & EMU8K_MEM_ADDRESS_MASK;
                        uint32_t paramq             = CCCA_FILTQ_GET(emu_voice->ccca);
                        emu_voice->filt_att         = filter_atten[paramq];
                        emu_voice->filterq_idx      = paramq;
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
                                /* The scale of this value is unknown. I've taken it as milliHz.
                                 * Another interpretation could be periods. (and so, Hz = 1/period)*/
                                double osc_speed = emu8k->hwcf5; //*1.316;
#if 1                                                            // milliHz
                                /*milliHz to lfotable samples.*/
                                osc_speed *= 65.536 / 44100.0;
#elif 0 // periods
                                /* 44.1Khz ticks to lfotable samples.*/
                                osc_speed = 65.536 / osc_speed;
#endif
                                /*left shift 32bits for 32.32 fixed.point*/
                                osc_speed *= 65536.0 * 65536.0;
                                emu8k->chorus_engine.lfo_inc.addr = (uint64_t) osc_speed;
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
                                    int32_t samples                      = ((val & 0xFF) * emu8k->chorus_engine.delay_samples_central) >> 8;
                                    emu8k->chorus_engine.lfodepth_multip = samples;
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
                    emu8k->voice[emu8k->cur_voice].ip              = val;
                    emu8k->voice[emu8k->cur_voice].ptrx_pit_target = freqtable[val] >> 18;
                    return;

                case 1:
                    {
                        emu8k_voice_t *const the_voice = &emu8k->voice[emu8k->cur_voice];
                        if ((val & 0xFF) == 0 && the_voice->cvcf_curr_volume == 0 && the_voice->vtft_vol_target == 0
                            && the_voice->dcysusv == 0x80 && the_voice->ip == 0) {
                            // Patch to avoid some clicking noises with Impulse tracker or other software that sets
                            // different values to 0 to set noteoff, but here, 0 means no attenuation = full volume.
                            return;
                        }
                        the_voice->ifatn           = val;
                        the_voice->initial_att     = (((int32_t) the_voice->ifatn_attenuation << 21) / 0xFF);
                        the_voice->vtft_vol_target = attentable[the_voice->ifatn_attenuation];

                        the_voice->initial_filter = (((int32_t) the_voice->ifatn_init_filter << 21) / 0xFF);
                        if (the_voice->ifatn_init_filter == 0xFF) {
                            the_voice->vtft_filter_target = 0xFFFF;
                        } else {
                            the_voice->vtft_filter_target = the_voice->initial_filter >> 5;
                        }
                    }
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

/* TODO: This is not a correct emulation, just a workalike implementation. */
void
emu8k_work_chorus(int32_t *inbuf, int32_t *outbuf, emu8k_chorus_eng_t *engine, int count)
{
    for (int pos = 0; pos < count; pos++) {
        double lfo_inter1 = chortable[engine->lfo_pos.int_address];
#if 0
        double lfo_inter2 = chortable[(engine->lfo_pos.int_address+1)&0xFFFF];
#endif

        double offset_lfo = lfo_inter1; //= lfo_inter1 + ((lfo_inter2-lfo_inter1)*engine->lfo_pos.fract_address/65536.0);
        offset_lfo *= engine->lfodepth_multip;

        /* Work left */
        double readdouble    = (double) engine->write - (double) engine->delay_samples_central - offset_lfo;
        int    read          = (int32_t) floor(readdouble);
        int    fraction_part = (readdouble - (double) read) * 65536.0;
        int    next_value    = read + 1;
        if (read < 0) {
            read += EMU8K_LFOCHORUS_SIZE;
            if (next_value < 0)
                next_value += EMU8K_LFOCHORUS_SIZE;
        } else if (next_value >= EMU8K_LFOCHORUS_SIZE) {
            next_value -= EMU8K_LFOCHORUS_SIZE;
            if (read >= EMU8K_LFOCHORUS_SIZE)
                read -= EMU8K_LFOCHORUS_SIZE;
        }
        int32_t dat1 = engine->chorus_left_buffer[read];
        int32_t dat2 = engine->chorus_left_buffer[next_value];
        dat1 += ((dat2 - dat1) * fraction_part) >> 16;

        engine->chorus_left_buffer[engine->write] = *inbuf + ((dat1 * engine->feedback) >> 8);

        /* Work right */
        readdouble = (double) engine->write - (double) engine->delay_samples_central - engine->delay_offset_samples_right - offset_lfo;
        read       = (int32_t) floor(readdouble);
        next_value = read + 1;
        if (read < 0) {
            read += EMU8K_LFOCHORUS_SIZE;
            if (next_value < 0)
                next_value += EMU8K_LFOCHORUS_SIZE;
        } else if (next_value >= EMU8K_LFOCHORUS_SIZE) {
            next_value -= EMU8K_LFOCHORUS_SIZE;
            if (read >= EMU8K_LFOCHORUS_SIZE)
                read -= EMU8K_LFOCHORUS_SIZE;
        }
        int32_t dat3 = engine->chorus_right_buffer[read];
        int32_t dat4 = engine->chorus_right_buffer[next_value];
        dat3 += ((dat4 - dat3) * fraction_part) >> 16;

        engine->chorus_right_buffer[engine->write] = *inbuf + ((dat3 * engine->feedback) >> 8);

        ++engine->write;
        engine->write %= EMU8K_LFOCHORUS_SIZE;
        engine->lfo_pos.addr += engine->lfo_inc.addr;
        engine->lfo_pos.int_address &= 0xFFFF;

        (*outbuf++) += dat1;
        (*outbuf++) += dat3;
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
void
emu8k_work_eq(UNUSED(int32_t *inoutbuf), UNUSED(int count))
{
    // TODO: Work EQ over buf
}

int32_t
emu8k_vol_slide(emu8k_slide_t *slide, int32_t target)
{
    if (slide->last < target) {
        slide->last += 0x400;
        if (slide->last > target)
            slide->last = target;
    } else if (slide->last > target) {
        slide->last -= 0x400;
        if (slide->last < target)
            slide->last = target;
    }
    return slide->last;
}

#if 0
int32_t old_pitch[32] = { 0 };
int32_t old_cut[32]   = { 0 };
int32_t old_vol[32]   = { 0 };
#endif
void
emu8k_update(emu8k_t *emu8k)
{
    if (emu8k->pos >= wavetable_pos_global)
        return;

    int32_t       *buf;
    emu8k_voice_t *emu_voice;
    int            pos;

    /* Clean the buffers since we will accumulate into them. */
    buf = &emu8k->buffer[emu8k->pos * 2];
    memset(buf, 0, 2 * (wavetable_pos_global - emu8k->pos) * sizeof(emu8k->buffer[0]));
    memset(&emu8k->chorus_in_buffer[emu8k->pos], 0, (wavetable_pos_global - emu8k->pos) * sizeof(emu8k->chorus_in_buffer[0]));
    memset(&emu8k->reverb_in_buffer[emu8k->pos], 0, (wavetable_pos_global - emu8k->pos) * sizeof(emu8k->reverb_in_buffer[0]));

    /* Voices section  */
    for (uint8_t c = 0; c < 32; c++) {
        emu_voice = &emu8k->voice[c];
        buf       = &emu8k->buffer[emu8k->pos * 2];

        for (pos = emu8k->pos; pos < wavetable_pos_global; pos++) {
            int32_t dat;

            if (emu_voice->cvcf_curr_volume) {
                /* Waveform oscillator */
#ifdef RESAMPLER_LINEAR
                dat = EMU8K_READ_INTERP_LINEAR(emu8k, emu_voice->addr.int_address,
                                               emu_voice->addr.fract_address);

#elif defined RESAMPLER_CUBIC
                dat = EMU8K_READ_INTERP_CUBIC(emu8k, emu_voice->addr.int_address,
                                              emu_voice->addr.fract_address);
#endif

                /* Filter section */
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
                    if (dat > 32767) {
                        dat = 32767;
                    } else if (dat < -32768) {
                        dat = -32768;
                    }

#elif defined FILTER_MOOG

                    /*move to 24bits*/
                    dat <<= 8;

                    dat -= (coef2 * emu_voice->filt_buffer[4]) >> 24; /*feedback*/
                    int64_t t1 = emu_voice->filt_buffer[1];
                    emu_voice->filt_buffer[1] = ((dat + emu_voice->filt_buffer[0]) * coef0 - emu_voice->filt_buffer[1] * coef1) >> 24;
                    emu_voice->filt_buffer[1] = ClipBuffer(emu_voice->filt_buffer[1]);

                    int64_t t2 = emu_voice->filt_buffer[2];
                    emu_voice->filt_buffer[2] = ((emu_voice->filt_buffer[1] + t1) * coef0 - emu_voice->filt_buffer[2] * coef1) >> 24;
                    emu_voice->filt_buffer[2] = ClipBuffer(emu_voice->filt_buffer[2]);

                    int64_t t3 = emu_voice->filt_buffer[3];
                    emu_voice->filt_buffer[3] = ((emu_voice->filt_buffer[2] + t2) * coef0 - emu_voice->filt_buffer[3] * coef1) >> 24;
                    emu_voice->filt_buffer[3] = ClipBuffer(emu_voice->filt_buffer[3]);

                    emu_voice->filt_buffer[4] = ((emu_voice->filt_buffer[3] + t3) * coef0 - emu_voice->filt_buffer[4] * coef1) >> 24;
                    emu_voice->filt_buffer[4] = ClipBuffer(emu_voice->filt_buffer[4]);

                    emu_voice->filt_buffer[0] = ClipBuffer(dat);

                    dat = (int32_t) (emu_voice->filt_buffer[4] >> 8);
                    if (dat > 32767) {
                        dat = 32767;
                    } else if (dat < -32768) {
                        dat = -32768;
                    }

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
                    if (dat > 32767) {
                        dat = 32767;
                    } else if (dat < -32768) {
                        dat = -32768;
                    }

#endif
                }
                if ((emu8k->hwcf3 & 0x04) && !CCCA_DMA_ACTIVE(emu_voice->ccca)) {
                    /*volume and pan*/
                    dat = (dat * emu_voice->cvcf_curr_volume) >> 16;

                    (*buf++) += (dat * emu_voice->vol_l) >> 8;
                    (*buf++) += (dat * emu_voice->vol_r) >> 8;

                    /* Effects section */
                    if (emu_voice->ptrx_revb_send > 0) {
                        emu8k->reverb_in_buffer[pos] += (dat * emu_voice->ptrx_revb_send) >> 8;
                    }
                    if (emu_voice->csl_chor_send > 0) {
                        emu8k->chorus_in_buffer[pos] += (dat * emu_voice->csl_chor_send) >> 8;
                    }
                }
            }

            if (emu_voice->env_engine_on) {
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
                            volenv->value_db_oct = 0;
                            if (volenv->hold_samples) {
                                volenv->state = ENV_HOLD;
                            } else {
                                /* RAMP_UP since db value is inverted and it is 0 at this point. */
                                volenv->state = ENV_RAMP_UP;
                            }
                        }
                        attenuation += env_vol_amplitude_to_db[volenv->value_amp_hz >> 5] << 5;
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
                        volenv->value_db_oct -= volenv->ramp_amount_db_oct;
                        if (volenv->value_db_oct <= volenv->sustain_value_db_oct) {
                            volenv->value_db_oct = volenv->sustain_value_db_oct;
                            volenv->state        = ENV_SUSTAIN;
                        }
                        attenuation += volenv->value_db_oct;
                        break;

                    case ENV_RAMP_UP:
                        /* Decay/release amount is in fraction of dBs and is always positive */
                        volenv->value_db_oct += volenv->ramp_amount_db_oct;
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
                        modenv->value_db_oct = env_mod_hertz_to_octave[modenv->value_amp_hz >> 5] << 5;
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
                            modenv->state = ENV_RAMP_UP;
                        }
                        break;

                    case ENV_RAMP_DOWN:
                        /* Decay/release amount is in fraction of octave and is always positive */
                        modenv->value_db_oct -= modenv->ramp_amount_db_oct;
                        if (modenv->value_db_oct <= modenv->sustain_value_db_oct) {
                            modenv->value_db_oct = modenv->sustain_value_db_oct;
                            modenv->state        = ENV_SUSTAIN;
                        }
                        break;

                    case ENV_RAMP_UP:
                        /* Decay/release amount is in fraction of octave and is always positive */
                        modenv->value_db_oct += modenv->ramp_amount_db_oct;
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
                    attenuation += lfo1_tremolo;
                }

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
            }
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
            emu_voice->addr.addr += ((uint64_t) emu_voice->cpf_curr_pitch) << 18;
            if (emu_voice->addr.addr >= emu_voice->loop_end.addr) {
                emu_voice->addr.int_address -= (emu_voice->loop_end.int_address - emu_voice->loop_start.int_address);
                emu_voice->addr.int_address &= EMU8K_MEM_ADDRESS_MASK;
            }

            /* TODO: How and when are the target and current values updated */
            emu_voice->cpf_curr_pitch       = emu_voice->ptrx_pit_target;
            emu_voice->cvcf_curr_volume     = emu8k_vol_slide(&emu_voice->volumeslide, emu_voice->vtft_vol_target);
            emu_voice->cvcf_curr_filt_ctoff = emu_voice->vtft_filter_target;
        }

        /* Update EMU voice registers. */
        emu_voice->ccca               = (((uint32_t) emu_voice->ccca_qcontrol) << 24) | emu_voice->addr.int_address;
        emu_voice->cpf_curr_frac_addr = emu_voice->addr.fract_address;

#if 0
        if (emu_voice->cvcf_curr_volume != old_vol[c]) {
            pclog("EMUVOL (%d):%d\n", c, emu_voice->cvcf_curr_volume);
            old_vol[c]=emu_voice->cvcf_curr_volume;
        }
        pclog("EMUFILT :%d\n", emu_voice->cvcf_curr_filt_ctoff);
#endif
    }

    buf = &emu8k->buffer[emu8k->pos * 2];
    emu8k_work_reverb(&emu8k->reverb_in_buffer[emu8k->pos], buf, &emu8k->reverb_engine, wavetable_pos_global - emu8k->pos);
    emu8k_work_chorus(&emu8k->chorus_in_buffer[emu8k->pos], buf, &emu8k->chorus_engine, wavetable_pos_global - emu8k->pos);
    emu8k_work_eq(buf, wavetable_pos_global - emu8k->pos);

    /* Update EMU clock. */
    emu8k->wc += (wavetable_pos_global - emu8k->pos);

    emu8k->pos = wavetable_pos_global;
}

void
emu8k_change_addr(emu8k_t *emu8k, uint16_t emu_addr)
{
    if (emu8k->addr) {
        io_removehandler(emu8k->addr, 0x0004, emu8k_inb, emu8k_inw, NULL, emu8k_outb, emu8k_outw, NULL, emu8k);
        io_removehandler(emu8k->addr + 0x400, 0x0004, emu8k_inb, emu8k_inw, NULL, emu8k_outb, emu8k_outw, NULL, emu8k);
        io_removehandler(emu8k->addr + 0x800, 0x0004, emu8k_inb, emu8k_inw, NULL, emu8k_outb, emu8k_outw, NULL, emu8k);
        emu8k->addr = 0;
    }
    if (emu_addr) {
        emu8k->addr = emu_addr;
        io_sethandler(emu8k->addr, 0x0004, emu8k_inb, emu8k_inw, NULL, emu8k_outb, emu8k_outw, NULL, emu8k);
        io_sethandler(emu8k->addr + 0x400, 0x0004, emu8k_inb, emu8k_inw, NULL, emu8k_outb, emu8k_outw, NULL, emu8k);
        io_sethandler(emu8k->addr + 0x800, 0x0004, emu8k_inb, emu8k_inw, NULL, emu8k_outb, emu8k_outw, NULL, emu8k);
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
    if (!fp)
        fatal("AWE32.RAW not found\n");

    emu8k->rom = malloc(1024 * 1024);
    if (fread(emu8k->rom, 1, 1048576, fp) != 1048576)
        fatal("emu8k_init(): Error reading data\n");
    fclose(fp);
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

    /* This formula comes from vince vu/judge dredd's awe32p10 and corresponds to what the freebsd/linux AWE32 driver has. */
    float millis;
    for (c = 0; c < 128; c++) {
        if (c == 0)
            millis = 0; /* This means never attack. */
        else if (c < 32)
            millis = 11878.0 / c;
        else
            millis = 360 * exp((c - 32) / (16.0 / log(1.0 / 2.0)));

        env_attack_to_samples[c] = 44.1 * millis;
        /* This is an alternate formula with linear increments, but probably incorrect:
         * millis = (256+4096*(0x7F-c)) */
    }

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
    free(emu8k->rom);
    free(emu8k->ram);
}
