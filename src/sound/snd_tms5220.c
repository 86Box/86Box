/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Texas Instruments TMS5200/TMS5220/TMS5220C linear predictive
 *          coding speech synthesizer.
 *
 *          Ported from MAME's tms5220.cpp; the chip is clocked by its own
 *          timer at clock / 80 Hz and hands each sample to its owner. The
 *          VSM (speech ROM) interface is not implemented, so Read Byte,
 *          Read and Branch, Load Address and Speak (from ROM) behave as on
 *          a chip with no ROM attached.
 *
 * Authors: Frank Palazzolo,
 *          Aaron Giles,
 *          Jonathan Gevaryahu,
 *          Raphael Nabet,
 *          Couriersud,
 *          Michael Zapf,
 *
 *          Copyright Frank Palazzolo, Aaron Giles, Jonathan Gevaryahu,
 *          Raphael Nabet, Couriersud, Michael Zapf.
 *          Distributed under the BSD-3-Clause license.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/snd_tms5220.h>

#ifdef ENABLE_TMS5220_LOG
int tms5220_do_log = ENABLE_TMS5220_LOG;

static void
tms5220_log(const char *fmt, ...)
{
    va_list ap;

    if (tms5220_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define tms5220_log(fmt, ...)
#endif

#define FIFO_SIZE      16
#define NOCOMMAND      0xff
#define MAX_K          10
#define MAX_SCALE      64
#define MAX_CHIRP_SIZE 52

/* Forces TALK active instantly whenever SPEN would be activated (MAME's
   FAST_START_HACK). */
#define FAST_START_HACK 1

typedef struct tms5220_coeffs_t {
    int            num_k;
    int            energy_bits;
    int            pitch_bits;
    int            kbits[MAX_K];
    unsigned short energytable[MAX_SCALE];
    unsigned short pitchtable[MAX_SCALE];
    int            ktable[MAX_K][MAX_SCALE];
    int16_t        chirptable[MAX_CHIRP_SIZE];
    int8_t         interp_coeff[8];
} tms5220_coeffs_t;

#define TI_028X_LATER_ENERGY \
    { 0, 1, 2, 3, 4, 6, 8, 11, 16, 23, 33, 47, 63, 85, 114, 0 },

#define TI_2501E_PITCH                        \
    {   0,  14,  15,  16,  17,  18,  19,  20, \
       21,  22,  23,  24,  25,  26,  27,  28, \
       29,  30,  31,  32,  34,  36,  38,  40, \
       41,  43,  45,  48,  49,  51,  54,  55, \
       57,  60,  62,  64,  68,  72,  74,  76, \
       81,  85,  87,  90,  96,  99, 103, 107, \
      112, 117, 122, 127, 133, 139, 145, 151, \
      157, 164, 171, 178, 186, 194, 202, 211 },

#define TI_5220_PITCH                         \
    {   0,  15,  16,  17,  18,  19,  20,  21, \
       22,  23,  24,  25,  26,  27,  28,  29, \
       30,  31,  32,  33,  34,  35,  36,  37, \
       38,  39,  40,  41,  42,  44,  46,  48, \
       50,  52,  53,  56,  58,  60,  62,  65, \
       68,  70,  72,  76,  78,  80,  84,  86, \
       91,  94,  98, 101, 105, 109, 114, 118, \
      122, 127, 132, 137, 142, 148, 153, 159 },

#define TI_2801_2501E_LPC                                 \
    { -501, -498, -495, -490, -485, -478, -469, -459,     \
      -446, -431, -412, -389, -362, -331, -295, -253,     \
      -207, -156, -102,  -45,   13,   70,  126,  179,     \
       228,  272,  311,  345,  374,  399,  420,  437 },   \
    { -376, -357, -335, -312, -286, -258, -227, -195,     \
      -161, -124,  -87,  -49,  -10,   29,   68,  106,     \
       143,  178,  212,  243,  272,  299,  324,  346,     \
       366,  384,  400,  414,  427,  438,  448,  506 },   \
    { -407, -381, -349, -311, -268, -218, -162, -102,     \
       -39,   25,   89,  149,  206,  257,  302,  341 },   \
    { -290, -252, -209, -163, -114,  -62,   -9,   44,     \
        97,  147,  194,  238,  278,  313,  344,  371 },   \
    { -318, -283, -245, -202, -156, -107,  -56,   -3,     \
        49,  101,  150,  196,  239,  278,  313,  344 },   \
    { -193, -152, -109,  -65,  -20,   26,   71,  115,     \
       158,  198,  235,  270,  301,  330,  355,  377 },   \
    { -254, -218, -180, -140,  -97,  -53,   -8,   36,     \
        81,  124,  165,  204,  240,  274,  304,  332 },   \
    { -205, -112,  -10,   92,  187,  269,  336,  387 },   \
    { -249, -183, -110,  -32,   48,  126,  198,  261 },   \
    { -190, -133,  -73,  -10,   53,  115,  173,  227 },

#define TI_5110_5220_LPC                                  \
    { -501, -498, -497, -495, -493, -491, -488, -482,     \
      -478, -474, -469, -464, -459, -452, -445, -437,     \
      -412, -380, -339, -288, -227, -158,  -81,   -1,     \
        80,  157,  226,  287,  337,  379,  411,  436 },   \
    { -328, -303, -274, -244, -211, -175, -138,  -99,     \
       -59,  -18,   24,   64,  105,  143,  180,  215,     \
       248,  278,  306,  331,  354,  374,  392,  408,     \
       422,  435,  445,  455,  463,  470,  476,  506 },   \
    { -441, -387, -333, -279, -225, -171, -117,  -63,     \
        -9,   45,   98,  152,  206,  260,  314,  368 },   \
    { -328, -273, -217, -161, -106,  -50,    5,   61,     \
       116,  172,  228,  283,  339,  394,  450,  506 },   \
    { -328, -282, -235, -189, -142,  -96,  -50,   -3,     \
        43,   90,  136,  182,  229,  275,  322,  368 },   \
    { -256, -212, -168, -123,  -79,  -35,   10,   54,     \
        98,  143,  187,  232,  276,  320,  365,  409 },   \
    { -308, -260, -212, -164, -117,  -69,  -21,   27,     \
        75,  122,  170,  218,  266,  314,  361,  409 },   \
    { -256, -161,  -66,   29,  124,  219,  314,  409 },   \
    { -256, -176,  -96,  -15,   65,  146,  226,  307 },   \
    { -205, -132,  -59,   14,   87,  160,  234,  307 },

#define TI_LATER_CHIRP                                    \
    { 0x00, 0x03, 0x0f, 0x28, 0x4c, 0x6c, 0x71, 0x50,     \
      0x25, 0x26, 0x4c, 0x44, 0x1a, 0x32, 0x3b, 0x13,     \
      0x37, 0x1a, 0x25, 0x1f, 0x1d, 0x00, 0x00, 0x00,     \
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     \
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     \
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     \
      0x00, 0x00, 0x00, 0x00 },

#define TI_INTERP \
    { 0, 3, 3, 3, 2, 2, 1, 1 }

/* TMS5200 / CD2501E */
static const tms5220_coeffs_t t0285_2501e_coeff = {
    10, 4, 6,
    { 5, 5, 4, 4, 4, 4, 4, 3, 3, 3 },
    TI_028X_LATER_ENERGY
    TI_2501E_PITCH
    { TI_2801_2501E_LPC },
    TI_LATER_CHIRP
    TI_INTERP
};

/* TMS5220 / TMS5220C */
static const tms5220_coeffs_t tms5220_coeff = {
    10, 4, 6,
    { 5, 5, 4, 4, 4, 4, 4, 3, 3, 3 },
    TI_028X_LATER_ENERGY
    TI_5220_PITCH
    { TI_5110_5220_LPC },
    TI_LATER_CHIRP
    TI_INTERP
};

/* Sample count reload for the 5220C only; the 5200 and 5220 always reload
   with 0. */
static const uint8_t reload_table[4] = { 0, 2, 4, 6 };

struct tms5220_t {
    int                     type;
    uint32_t                clock;
    const tms5220_coeffs_t *coeff;

    /* FIFO */
    uint8_t fifo[FIFO_SIZE];
    uint8_t fifo_head;
    uint8_t fifo_tail;
    uint8_t fifo_count;
    uint8_t fifo_bits_taken;

    /* Status */
    int previous_talk_status;
    int SPEN;
    int DDIS;
    int TALK;
    int TALKD;
    int buffer_low;
    int buffer_empty;
    int irq_pin;
    int ready_pin;

    uint8_t command_register;
    int     data_latched;

    uint8_t read_byte_register;
    int     RDB_flag;

    /* Frame data */
    int     OLDE;
    int     OLDP;
    uint8_t new_frame_energy_idx;
    uint8_t new_frame_pitch_idx;
    uint8_t new_frame_k_idx[10];

    int16_t  current_energy;
    int16_t  current_pitch;
    int16_t  current_k[10];
    uint16_t previous_energy;

    uint8_t  subcycle;
    uint8_t  subc_reload;
    uint8_t  PC;
    uint8_t  IP;
    int      inhibit;
    int      uv_zpar;
    int      zpar;
    int      pitch_zero;
    uint8_t  c_variant_rate;
    uint16_t pitch_count;

    int32_t  u[11];
    int32_t  x[10];
    uint16_t RNG;
    int16_t  excitation_data;

    /* Host interface */
    int     io_ready;
    int     true_timing;
    uint8_t rs_ws;
    uint8_t read_latch;
    uint8_t write_latch;

    pc_timer_t io_ready_timer;
    pc_timer_t sample_timer;

    void (*sample_cb)(int16_t sample, void *priv);
    void (*irq_cb)(int state, void *priv);
    void *priv;
};

static void tms5220_process_command(tms5220_t *tms, uint8_t cmd);

#define HAS_RATE_CONTROL(t) ((t)->type == TMS5220_TYPE_5220C)

static int
talk_status(const tms5220_t *tms)
{
    return tms->SPEN || tms->TALKD;
}

static void
set_interrupt_state(tms5220_t *tms, int state)
{
    if (state != tms->irq_pin) {
        tms->irq_pin = state;
        if (tms->irq_cb)
            tms->irq_cb(!state, tms->priv);
    }
}

static void
update_ready_state(tms5220_t *tms)
{
    tms->ready_pin = tms->io_ready;
}

/* Delay of the given number of chip clocks, in timer units. */
static uint64_t
tms5220_clocks(const tms5220_t *tms, int clocks)
{
    return (uint64_t) (((double) clocks * 1000000.0 / (double) tms->clock) * (double) TIMER_USEC);
}

static uint8_t
status_read(tms5220_t *tms, int clear_int)
{
    if (tms->RDB_flag) {
        /* If the last command was Read Byte, return the data register. */
        tms->RDB_flag = 0;
        return tms->read_byte_register;
    }

    if (clear_int)
        set_interrupt_state(tms, 0);

    return (talk_status(tms) << 7) | (tms->buffer_low << 6) | (tms->buffer_empty << 5);
}

static int
ready_read(const tms5220_t *tms)
{
    if (!tms->true_timing)
        return ((tms->fifo_count < FIFO_SIZE) || !tms->DDIS) && tms->io_ready;

    return tms->io_ready;
}

static void
update_fifo_status_and_ints(tms5220_t *tms)
{
    update_ready_state(tms);

    /* BL is set if neither byte 9 nor 8 of the FIFO are in use. */
    if (tms->fifo_count <= 8) {
        if (!tms->buffer_low) {
            tms->buffer_low = 1;
            set_interrupt_state(tms, 1);
        }
    } else
        tms->buffer_low = 0;

    /* BE is set if the FIFO is empty. */
    if (tms->fifo_count == 0) {
        if (!tms->buffer_empty) {
            tms->buffer_empty = 1;
            set_interrupt_state(tms, 1);
        }
        /* /BE clears TALK (and through it SPEN), but only in Speak External. */
        if (tms->DDIS)
            tms->TALK = tms->SPEN = 0;
    } else
        tms->buffer_empty = 0;

    /* Interrupt on a falling edge of talk status. */
    if (tms->previous_talk_status && !talk_status(tms)) {
        set_interrupt_state(tms, 1);
        tms->DDIS                 = 0;
        tms->previous_talk_status = 0;

        /* Execute a command that was held while talking. */
        if (tms->command_register != NOCOMMAND) {
            tms5220_process_command(tms, tms->command_register);
            if (tms->data_latched)
                timer_set_delay_u64(&tms->io_ready_timer, tms5220_clocks(tms, 16));
            else
                tms->io_ready = 1;
            update_ready_state(tms);
        }
    }
    tms->previous_talk_status = talk_status(tms);

    if ((tms->rs_ws == 0x01) && !tms->RDB_flag)
        tms->read_latch = status_read(tms, 0);
}

static int
read_bits(tms5220_t *tms, int count)
{
    int val = 0;

    if (tms->DDIS) {
        while (count--) {
            val = (val << 1) | ((tms->fifo[tms->fifo_head] >> tms->fifo_bits_taken) & 1);
            tms->fifo_bits_taken++;
            if (tms->fifo_bits_taken >= 8) {
                tms->fifo_count--;
                tms->fifo[tms->fifo_head] = 0;
                tms->fifo_head            = (tms->fifo_head + 1) % FIFO_SIZE;
                tms->fifo_bits_taken      = 0;
                update_fifo_status_and_ints(tms);
            }
        }
    } else {
        /* No VSM attached: the data line floats high. */
        val = (1 << count) - 1;
    }

    return val;
}

static void
zero_new_frame(tms5220_t *tms)
{
    tms->zpar    = 1; /* zero all the parameters */
    tms->uv_zpar = 1; /* zero K5-K10 as well */
    tms->OLDE    = 1; /* silence/zpar frames are zero energy */
    tms->OLDP    = 1; /* silence/zpar frames are zero pitch */

    tms->new_frame_energy_idx = 0;
    tms->new_frame_pitch_idx  = 0;
    for (int i = 0; i < 4; i++)
        tms->new_frame_k_idx[i] = 0;
    for (int i = 4; i < 7; i++)
        tms->new_frame_k_idx[i] = 0xf;
    for (int i = 7; i < tms->coeff->num_k; i++)
        tms->new_frame_k_idx[i] = 0x7;
}

static void
tms5220_chip_reset(tms5220_t *tms)
{
    memset(tms->fifo, 0, sizeof(tms->fifo));
    tms->fifo_head = tms->fifo_tail = tms->fifo_count = tms->fifo_bits_taken = 0;

    /* IRQ is not actually cleared on start-up. */
    tms->SPEN = tms->DDIS = tms->TALK = tms->TALKD = tms->previous_talk_status = 0;
    tms->irq_pin = tms->ready_pin = 0;
    set_interrupt_state(tms, 0);
    update_ready_state(tms);
    tms->buffer_empty = tms->buffer_low = 1;

    tms->command_register = NOCOMMAND;
    tms->data_latched     = 0;
    tms->RDB_flag         = 0;

    tms->new_frame_energy_idx = 0;
    tms->current_energy       = 0;
    tms->previous_energy      = 0;
    tms->new_frame_pitch_idx  = 0;
    tms->current_pitch        = 0;
    tms->zpar                 = 0;
    tms->uv_zpar              = 0;
    memset(tms->new_frame_k_idx, 0, sizeof(tms->new_frame_k_idx));
    memset(tms->current_k, 0, sizeof(tms->current_k));

    tms->inhibit        = 1;
    tms->subcycle       = 0;
    tms->c_variant_rate = 0;
    tms->pitch_count    = 0;
    tms->PC             = 0;
    tms->subc_reload    = 1;
    tms->OLDE           = 1;
    tms->OLDP           = 1;
    tms->IP             = reload_table[tms->c_variant_rate & 0x3];
    tms->RNG            = 0x1fff;
    memset(tms->u, 0, sizeof(tms->u));
    memset(tms->x, 0, sizeof(tms->x));
}

static void
data_write(tms5220_t *tms, int data)
{
    int old_buffer_low = tms->buffer_low;

    if (tms->DDIS) {
        /* Speak External: the byte goes to the FIFO. */
        if (tms->fifo_count < FIFO_SIZE) {
            tms->fifo[tms->fifo_tail] = data;
            tms->fifo_tail            = (tms->fifo_tail + 1) % FIFO_SIZE;
            tms->fifo_count++;
            update_fifo_status_and_ints(tms);

            /* Speech starts on the edge of BL going inactive. */
            if (!tms->SPEN && (old_buffer_low && !tms->buffer_low)) {
                zero_new_frame(tms);
                tms->SPEN = 1;
#ifdef FAST_START_HACK
                tms->TALK = 1;
#endif
            }
        } else
            tms5220_log("TMS5220: FIFO overrun, byte %02X lost\n", data);

        tms->data_latched = 0;
    } else
        tms5220_process_command(tms, data);

    if (!tms->data_latched)
        tms->io_ready = 1;
}

static void
tms5220_process_command(tms5220_t *tms, uint8_t cmd)
{
    tms->command_register = cmd;

    switch (cmd & 0x70) {
        case 0x10: /* Read Byte */
            if (!talk_status(tms)) {
                tms->read_byte_register = read_bits(tms, 8);
                tms->RDB_flag           = 1;
                tms->command_register   = NOCOMMAND;
            }
            break;

        case 0x00:
        case 0x20: /* Set Rate (5220C only), otherwise NOP */
            if (HAS_RATE_CONTROL(tms))
                tms->c_variant_rate = cmd & 0x0f;
            tms->command_register = NOCOMMAND;
            break;

        case 0x30: /* Read and Branch */
            if (!talk_status(tms)) {
                tms->RDB_flag         = 0;
                tms->command_register = NOCOMMAND;
            }
            break;

        case 0x40: /* Load Address */
            if (!talk_status(tms))
                tms->command_register = NOCOMMAND;
            break;

        case 0x50: /* Speak */
            tms->SPEN = 1;
#ifdef FAST_START_HACK
            tms->TALK = 1;
#endif
            tms->DDIS = 0; /* speak using the VSM */
            zero_new_frame(tms);
            tms->command_register = NOCOMMAND;
            break;

        case 0x60: /* Speak External */
            memset(tms->fifo, 0, sizeof(tms->fifo));
            tms->fifo_head = tms->fifo_tail = tms->fifo_count = tms->fifo_bits_taken = 0;
            tms->DDIS = 1; /* speak using the FIFO */
            zero_new_frame(tms);
            tms->RDB_flag         = 0;
            tms->command_register = NOCOMMAND;
            break;

        case 0x70: /* Reset */
            tms5220_chip_reset(tms);
            tms->command_register = NOCOMMAND;
            break;

        default:
            break;
    }

    update_fifo_status_and_ints(tms);
}

static void
parse_frame(tms5220_t *tms)
{
    int rep_flag;

    tms->uv_zpar = tms->zpar = 0;

    /* On the 5220C with per-frame rate, each frame starts with 2 rate bits. */
    if (HAS_RATE_CONTROL(tms) && (tms->c_variant_rate & 0x04))
        tms->IP = reload_table[read_bits(tms, 2)];
    else
        tms->IP = reload_table[tms->c_variant_rate & 0x3];

    update_fifo_status_and_ints(tms);
    if (tms->DDIS && tms->buffer_empty)
        return;

    tms->new_frame_energy_idx = read_bits(tms, tms->coeff->energy_bits);
    update_fifo_status_and_ints(tms);
    if (tms->DDIS && tms->buffer_empty)
        return;

    /* Silence or stop frame: nothing else to read. */
    if ((tms->new_frame_energy_idx == 0) || (tms->new_frame_energy_idx == 15))
        return;

    rep_flag = read_bits(tms, 1);

    tms->new_frame_pitch_idx = read_bits(tms, tms->coeff->pitch_bits);
    tms->uv_zpar             = (tms->new_frame_pitch_idx == 0);
    update_fifo_status_and_ints(tms);
    if (tms->DDIS && tms->buffer_empty)
        return;

    if (rep_flag)
        return;

    for (int i = 0; i < 4; i++) {
        tms->new_frame_k_idx[i] = read_bits(tms, tms->coeff->kbits[i]);
        update_fifo_status_and_ints(tms);
        if (tms->DDIS && tms->buffer_empty)
            return;
    }

    /* Unvoiced frames only carry K1-K4. */
    if (tms->new_frame_pitch_idx == 0)
        return;

    for (int i = 4; i < tms->coeff->num_k; i++) {
        tms->new_frame_k_idx[i] = read_bits(tms, tms->coeff->kbits[i]);
        update_fifo_status_and_ints(tms);
        if (tms->DDIS && tms->buffer_empty)
            return;
    }
}

/* Clips the 14-bit lattice output to the 8-bit analog DAC and range-extends
   it to 16 bits. */
static int16_t
clip_analog(int16_t cliptemp)
{
    if (cliptemp > 2047)
        cliptemp = 2047;
    else if (cliptemp < -2048)
        cliptemp = -2048;

    cliptemp &= ~0xf;
    return (cliptemp << 4) | ((cliptemp & 0x7f0) >> 3) | ((cliptemp & 0x400) >> 10);
}

static int32_t
matrix_multiply(int32_t a, int32_t b)
{
    while (a > 511)
        a -= 1024;
    while (a < -512)
        a += 1024;
    while (b > 16383)
        b -= 32768;
    while (b < -16384)
        b += 32768;

    return (a * b) >> 9;
}

static int32_t
lattice_filter(tms5220_t *tms)
{
    int32_t *u = tms->u;
    int32_t *x = tms->x;
    int16_t *k = tms->current_k;

    u[10] = matrix_multiply(tms->previous_energy, (tms->excitation_data << 6));
    for (int i = 9; i >= 0; i--)
        u[i] = u[i + 1] - matrix_multiply(k[i], x[i]);
    for (int i = 9; i >= 1; i--)
        x[i] = x[i - 1] + matrix_multiply(k[i - 1], u[i - 1]);
    x[0] = u[0];

    tms->previous_energy = tms->current_energy;

    return u[0];
}

static void
tms5220_advance_counters(tms5220_t *tms, int talking)
{
    tms->subcycle++;
    if ((tms->subcycle == 2) && (tms->PC == 12)) { /* RESETF3 */
        if (talking && (tms->IP == 7) && tms->inhibit)
            tms->pitch_zero = 1;
        if (tms->IP == 7) { /* RESETL4 */
            if (talking) {
                tms->OLDE = (tms->new_frame_energy_idx == 0);
                tms->OLDP = (tms->new_frame_pitch_idx == 0);
            }
            tms->TALKD = tms->TALK;
            update_fifo_status_and_ints(tms);
            if (!tms->TALK && tms->SPEN)
                tms->TALK = 1;
        }
        tms->subcycle = tms->subc_reload;
        tms->PC       = 0;
        tms->IP       = (tms->IP + 1) & 0x7;
    } else if (tms->subcycle == 3) {
        tms->subcycle = tms->subc_reload;
        tms->PC++;
    }
}

static int16_t
tms5220_process(tms5220_t *tms)
{
    const tms5220_coeffs_t *c = tms->coeff;
    int32_t                 this_sample;
    int16_t                 out;

    if (!tms->TALKD) {
        tms5220_advance_counters(tms, 0);

        /* The chip outputs -1 on every idle sample. */
        return -1;
    }

    if ((tms->IP == 0) && (tms->PC == 12) && (tms->subcycle == 1)) {
        /* A new frame is applied at IP=0, PC=12, subcycle 1. */
        const int old_uv = tms->OLDP;
        const int old_si = tms->OLDE;
        int       new_uv;
        int       new_si;

        tms->IP = reload_table[tms->c_variant_rate & 0x3];

        parse_frame(tms);

        /* A stop frame clears TALK and SPEN; TALKD stays active while the
           energy ramps down. */
        if (tms->new_frame_energy_idx == 0x0f) {
            tms->TALK = tms->SPEN = 0;
            update_fifo_status_and_ints(tms);
        }

        /* Interpolation is inhibited on voiced/unvoiced transitions, coming
           out of silence, and going from unvoiced into silence. */
        new_uv       = (tms->new_frame_pitch_idx == 0);
        new_si       = (tms->new_frame_energy_idx == 0);
        tms->inhibit = ((!old_uv && new_uv) || (old_uv && !new_uv) ||
                        (old_si && !new_si) || (old_uv && new_si));
    } else if (tms->subcycle == 2) {
        /* Interpolate the current frame; inhibit is lifted on the last period. */
        const int inhibit_state = (tms->inhibit && (tms->IP != 0));
        const int shift         = c->interp_coeff[tms->IP];

        switch (tms->PC) {
            case 0: /* energy */
                if (tms->IP == 0)
                    tms->pitch_zero = 0;
                tms->current_energy = (tms->current_energy + (((c->energytable[tms->new_frame_energy_idx] - tms->current_energy) * (1 - inhibit_state)) >> shift)) * (1 - tms->zpar);
                break;
            case 1: /* pitch */
                tms->current_pitch = (tms->current_pitch + (((c->pitchtable[tms->new_frame_pitch_idx] - tms->current_pitch) * (1 - inhibit_state)) >> shift)) * (1 - tms->zpar);
                break;
            case 2: case 3: case 4: case 5: case 6:
            case 7: case 8: case 9: case 10: case 11: {
                const int n = tms->PC - 2;

                tms->current_k[n] = (tms->current_k[n] + (((c->ktable[n][tms->new_frame_k_idx[n]] - tms->current_k[n]) * (1 - inhibit_state)) >> shift)) * (1 - ((n < 4) ? tms->zpar : tms->uv_zpar));
                break;
            }
            default:
                break;
        }
    }

    /* Excitation: noise when unvoiced, the chirp ROM (latched at its last
       entry) when voiced. */
    if (tms->OLDP)
        tms->excitation_data = (tms->RNG & 1) ? ~0x3f : 0x40;
    else if (tms->pitch_count >= 51)
        tms->excitation_data = (int8_t) c->chirptable[51];
    else
        tms->excitation_data = (int8_t) c->chirptable[tms->pitch_count];

    /* The LFSR is clocked 20 times every sample. */
    for (int i = 0; i < 20; i++) {
        const int bitout = ((tms->RNG >> 12) & 1) ^ ((tms->RNG >> 3) & 1) ^
                           ((tms->RNG >> 2) & 1) ^ (tms->RNG & 1);
        tms->RNG = (tms->RNG << 1) | bitout;
    }

    this_sample = lattice_filter(tms);

    /* Force the result to 14 bits, as the final stage may overflow. */
    while (this_sample > 16383)
        this_sample -= 32768;
    while (this_sample < -16384)
        this_sample += 32768;

    /* Analog SPK pin output. */
    out = clip_analog((int16_t) this_sample);

    tms5220_advance_counters(tms, 1);

    tms->pitch_count++;
    if ((tms->pitch_count >= tms->current_pitch) || tms->pitch_zero)
        tms->pitch_count = 0;
    tms->pitch_count &= 0x1ff;

    return out;
}

static void
tms5220_sample_callback(void *priv)
{
    tms5220_t    *tms    = (tms5220_t *) priv;
    const int16_t sample = tms5220_process(tms);

    if (tms->sample_cb)
        tms->sample_cb(sample, tms->priv);

    timer_advance_u64(&tms->sample_timer, tms5220_clocks(tms, 80));
}

static void
tms5220_io_ready_callback(void *priv)
{
    tms5220_t *tms = (tms5220_t *) priv;

    switch (tms->rs_ws) {
        case 0x02: /* write */
            if ((tms->fifo_count >= FIFO_SIZE) && tms->DDIS) {
                /* FIFO full in Speak External: retry later. */
                timer_set_delay_u64(&tms->io_ready_timer, tms5220_clocks(tms, 16));
                break;
            }
            if (tms->command_register == NOCOMMAND) {
                tms->data_latched = 0;
                data_write(tms, tms->write_latch);
            }
            break;

        case 0x01: /* read */
            tms->read_latch = status_read(tms, 1);
            tms->io_ready   = 1;
            break;

        default: /* high impedance, or both active (illegal) */
            tms->io_ready = 1;
            break;
    }

    update_ready_state(tms);
}

static void
tms5220_line_w(tms5220_t *tms, uint8_t new_val, int state, int is_rs)
{
    tms->true_timing = 1;

    if (new_val == tms->rs_ws)
        return;

    tms->rs_ws = new_val;

    if (new_val == 0) {
        /* Both active: resets the 5220C, illegal elsewhere. */
        if (HAS_RATE_CONTROL(tms))
            tms5220_chip_reset(tms);
        return;
    } else if (new_val == 3) {
        /* High impedance. */
        tms->read_latch = 0xff;
        return;
    }

    if (!state) {
        /* High to low: /READY goes inactive and a ready cycle is scheduled. */
        tms->io_ready = 0;
        update_ready_state(tms);
        if (is_rs)
            timer_set_delay_u64(&tms->io_ready_timer, 13 * TIMER_USEC);
        else
            timer_set_delay_u64(&tms->io_ready_timer, tms5220_clocks(tms, 16));
    }
}

void
tms5220_rsq_w(tms5220_t *tms, int state)
{
    state &= 1;
    tms5220_line_w(tms, (tms->rs_ws & 0x01) | (state << 1), state, 1);
}

void
tms5220_wsq_w(tms5220_t *tms, int state)
{
    state &= 1;
    tms5220_line_w(tms, (tms->rs_ws & 0x02) | state, state, 0);
}

void
tms5220_data_w(tms5220_t *tms, uint8_t data)
{
    tms->write_latch  = data;
    tms->data_latched = 1;

    /* Without /RS and /WS, the write goes through instantly. */
    if (!tms->true_timing)
        data_write(tms, tms->write_latch);
}

uint8_t
tms5220_status_r(tms5220_t *tms)
{
    if (!tms->true_timing)
        return status_read(tms, 1);

    if (tms->rs_ws == 0x01)
        return tms->read_latch;

    return 0xff;
}

int
tms5220_readyq_r(tms5220_t *tms)
{
    return !ready_read(tms);
}

int
tms5220_intq_r(tms5220_t *tms)
{
    return !tms->irq_pin;
}

void
tms5220_reset(tms5220_t *tms)
{
    tms5220_chip_reset(tms);
}

tms5220_t *
tms5220_init(int type, uint32_t clock,
             void (*sample_cb)(int16_t sample, void *priv),
             void (*irq_cb)(int state, void *priv),
             void *priv)
{
    tms5220_t *tms = (tms5220_t *) calloc(1, sizeof(tms5220_t));

    tms->type      = type;
    tms->clock     = clock;
    tms->coeff     = (type == TMS5220_TYPE_5200) ? &t0285_2501e_coeff : &tms5220_coeff;
    tms->sample_cb = sample_cb;
    tms->irq_cb    = irq_cb;
    tms->priv      = priv;

    tms->io_ready    = 1;
    tms->true_timing = 0;
    tms->rs_ws       = 0x03; /* /RS and /WS inactive */
    tms->write_latch = 0;

    timer_add(&tms->io_ready_timer, tms5220_io_ready_callback, tms, 0);
    timer_add(&tms->sample_timer, tms5220_sample_callback, tms, 0);

    tms5220_chip_reset(tms);

    timer_set_delay_u64(&tms->sample_timer, tms5220_clocks(tms, 80));

    return tms;
}

void
tms5220_close(tms5220_t *tms)
{
    timer_disable(&tms->io_ready_timer);
    timer_disable(&tms->sample_timer);
    free(tms);
}
