/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Modem speaker audio emulation.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *          Xeon3D (original creator)
 *
 *          Copyright 2026 Xeon3D.
 *          Copyright 2025-2026 Jasmine Iwanek.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include <86box/86box.h>
#include <86box/sound.h>
#include <86box/modem/modem_sound.h>

#ifndef M_PI
#    define M_PI 3.14159265358979323846
#endif

#define MODEM_SOUND_SLOTS 8
#define EVENT_QUEUE_SIZE 16
#define MAX_STEPS 2048
#define DIAL_TONE_MS 800
#define HANDSHAKE_MS 8000
#define HANDSHAKE_CONNECT_OVERLAP_MS 6000 /* CONNECT comes this long before the full handshake ends */

/* Sound while online (after CONNECT) is off by default. Define as e.g. 0.08 to get the
   old faint line hiss with ATM2. */
#ifndef MODEM_SOUND_ONLINE_HISS
#    define MODEM_SOUND_ONLINE_HISS 0.0
#endif

#define HS_MAX_SEGS 40

enum {
    RING_PATTERN_NORTH_AMERICA,
    RING_PATTERN_UK,
    RING_PATTERN_EUROPE,
    RING_PATTERN_JAPAN
};

typedef struct {
    int dial_f1;
    int dial_f2;
    int ring_f1;
    int ring_f2;
    int busy_f1;
    int busy_f2;
    int ring_pattern;
    uint32_t ring_wait_ms;
    uint32_t ring_cycle_ms;
    uint32_t busy_on_ms;
    uint32_t busy_off_ms;
} modem_tone_profile_t;

/* Continental Europe uses the German 425 Hz profile as a representative. */
static const modem_tone_profile_t modem_tone_profiles[MODEM_SOUND_REGION_COUNT] = {
    [MODEM_SOUND_REGION_NORTH_AMERICA] = {
        .dial_f1 = 350, .dial_f2 = 440, .ring_f1 = 440, .ring_f2 = 480,
        .busy_f1 = 480, .busy_f2 = 620, .ring_pattern = RING_PATTERN_NORTH_AMERICA,
        .ring_wait_ms = 2600, .ring_cycle_ms = 6000, .busy_on_ms = 500, .busy_off_ms = 500
    },
    [MODEM_SOUND_REGION_UK] = {
        .dial_f1 = 350, .dial_f2 = 440, .ring_f1 = 400, .ring_f2 = 450,
        .busy_f1 = 400, .busy_f2 = 0, .ring_pattern = RING_PATTERN_UK,
        .ring_wait_ms = 2900, .ring_cycle_ms = 3000, .busy_on_ms = 375, .busy_off_ms = 375
    },
    [MODEM_SOUND_REGION_EUROPE] = {
        .dial_f1 = 425, .dial_f2 = 0, .ring_f1 = 425, .ring_f2 = 0,
        .busy_f1 = 425, .busy_f2 = 0, .ring_pattern = RING_PATTERN_EUROPE,
        .ring_wait_ms = 5000, .ring_cycle_ms = 5000, .busy_on_ms = 480, .busy_off_ms = 480
    },
    [MODEM_SOUND_REGION_JAPAN] = {
        .dial_f1 = 400, .dial_f2 = 0, .ring_f1 = 400, .ring_f2 = 0,
        .busy_f1 = 400, .busy_f2 = 0, .ring_pattern = RING_PATTERN_JAPAN,
        .ring_wait_ms = 2800, .ring_cycle_ms = 3000, .busy_on_ms = 500, .busy_off_ms = 500
    }
};

typedef struct {
    int kind;
    int digit;
    uint32_t samples;
} dial_step_t;

typedef struct {
    int type;
    int arg;
    char number[128];
} sound_event_t;

/* ---- Handshake plan -------------------------------------------------------------------
 * The handshake is described as a list of overlapping signal segments (times in ms from
 * the moment the call is answered). The result is a recognisable imitation of what a real
 * modem speaker produces (answer tone, V.8 call menu, probing, training), not a bit-exact
 * copy of the ITU signals. */
enum { SEG_TONE, SEG_ANS, SEG_FSK, SEG_PSK, SEG_MULTI, SEG_NOISE };

typedef struct {
    int kind;
    uint32_t t0;
    uint32_t t1;
    double f1;    /* tone / FSK space / PSK carrier */
    double f2;    /* second tone / FSK mark */
    double rate;  /* FSK or PSK symbol rate; ANS: AM depth */
    int points;   /* PSK constellation size (1 = steady carrier) */
    double level;
} hs_seg_t;

typedef struct {
    hs_seg_t seg[HS_MAX_SEGS];
    int count;
    uint32_t total; /* end of the handshake */
    uint32_t cd;    /* carrier detect: ATM1 and ATM3 silence the speaker from here */
} hs_plan_t;

typedef struct {
    double ph1;
    double ph2;
    double pos;
    double pi, pq, ci, cq;
    double lp1, lp2;
    uint32_t rng;
    int bit;
} hs_state_t;

enum { PH_IDLE, PH_DIAL, PH_RING, PH_HANDSHAKE, PH_ONLINE, PH_BUSY, PH_RINGING };
enum { STEP_TONE, STEP_DTMF, STEP_PULSE, STEP_SILENCE, STEP_MAKE };

struct modem_sound_t {
    sound_event_t events[EVENT_QUEUE_SIZE];
    atomic_uint write_index;
    atomic_uint read_index;
    atomic_int mode;
    atomic_int level;
    atomic_int region;
    atomic_int full;
    atomic_int off;
    atomic_int standard;
    int in_use;
    int handler_registered;
    int phase;
    int rate;
    int pulse;
    int s8;
    uint32_t sample;
    uint32_t step_start;
    int step_count;
    int step_index;
    int dial_connect_after;
    int pending_event;
    int pending_arg;
    int audible_now;
    int hs_answerer;
    int hs_full;
    int hs_off;
    int hs_connected; /* CONNECT arrived during the full handshake, which plays out regardless */
    hs_plan_t hs_plan;
    hs_state_t hs_state[HS_MAX_SEGS];
    dial_step_t steps[MAX_STEPS];
    double phase1;
    double phase2;
    double phase3;
    double phase4;
    double noise_filter;
    uint32_t noise_state;
};

static modem_sound_t *modem_speakers[MODEM_SOUND_SLOTS];

static double
modem_oscillator(double *phase, double frequency, int rate)
{
    *phase += 2.0 * M_PI * frequency / rate;
    if (*phase >= 2.0 * M_PI)
        *phase -= 2.0 * M_PI;
    return sin(*phase);
}

static double
modem_noise(modem_sound_t *sound)
{
    sound->noise_state = sound->noise_state * 1664525u + 1013904223u;
    return ((double) (sound->noise_state >> 8) / 8388608.0) - 1.0;
}

static const modem_tone_profile_t *
modem_sound_profile(modem_sound_t *sound)
{
    int region = atomic_load(&sound->region);
    if (region < 0 || region >= MODEM_SOUND_REGION_COUNT)
        region = MODEM_SOUND_REGION_NORTH_AMERICA;
    return &modem_tone_profiles[region];
}

static double
modem_tone(modem_sound_t *sound, int f1, int f2)
{
    double value = modem_oscillator(&sound->phase1, f1, sound->rate);
    if (f2)
        value = 0.5 * value + 0.5 * modem_oscillator(&sound->phase2, f2, sound->rate);
    return value;
}

static double
modem_dial_tone(modem_sound_t *sound)
{
    const modem_tone_profile_t *profile = modem_sound_profile(sound);
    return modem_tone(sound, profile->dial_f1, profile->dial_f2);
}

static int
modem_ring_cadence_on(const modem_tone_profile_t *profile, uint32_t ms)
{
    const uint32_t cycle = ms % profile->ring_cycle_ms;

    switch (profile->ring_pattern) {
        case RING_PATTERN_UK:
            return cycle < 400 || (cycle >= 600 && cycle < 1000);
        case RING_PATTERN_EUROPE:
            return cycle < 1000;
        case RING_PATTERN_JAPAN:
            return cycle < 1000;
        case RING_PATTERN_NORTH_AMERICA:
        default:
            return cycle < 2000;
    }
}

static double
modem_line_fizz(modem_sound_t *sound, double carrier, double noise_level)
{
    const double white = modem_noise(sound);
    sound->noise_filter += 0.025 * (white - sound->noise_filter);
    const double filtered_noise = white - sound->noise_filter;
    const double flutter = 0.96 + 0.04 * modem_oscillator(&sound->phase4, 7.0, sound->rate);
    double fizz = carrier * flutter + noise_level * filtered_noise;
    if ((sound->noise_state & 0x7fffu) == 0)
        fizz += (sound->noise_state & 0x8000u) ? 0.12 : -0.12;
    return fizz;
}

static uint32_t
modem_ms_samples(modem_sound_t *sound, uint32_t milliseconds)
{
    return (uint32_t) (((uint64_t) milliseconds * (uint64_t) sound->rate) / 1000u);
}

static void
modem_add_step(modem_sound_t *sound, int kind, int digit, uint32_t ms)
{
    if (!ms || sound->step_count >= MAX_STEPS)
        return;
    sound->steps[sound->step_count++] = (dial_step_t) {
        .kind = kind,
        .digit = digit,
        .samples = modem_ms_samples(sound, ms)
    };
}

static void
modem_build_dial(modem_sound_t *sound, const char *number, int s6, int s8, int s11,
                 int pulse, int detect)
{
    sound->step_count = 0;
    modem_add_step(sound, STEP_TONE, 0, detect ? DIAL_TONE_MS : (uint32_t) s6 * 1000u);
    for (const char *p = number; p && *p; p++) {
        const char c = *p;
        if (c == 'T' || c == 't')
            pulse = 0;
        else if (c == 'P' || c == 'p')
            pulse = 1;
        else if (c == ',')
            modem_add_step(sound, STEP_TONE, 0, (uint32_t) s8 * 1000u);
        else if (c == 'W' || c == 'w')
            modem_add_step(sound, STEP_TONE, 0, DIAL_TONE_MS);
        else if (c >= '0' && c <= '9') {
            if (pulse) {
                const int count = c == '0' ? 10 : c - '0';
                for (int i = 0; i < count; i++) {
                    modem_add_step(sound, STEP_PULSE, 0, 60);
                    modem_add_step(sound, STEP_MAKE, 0, 40);
                }
                modem_add_step(sound, STEP_SILENCE, 0, 700);
            } else {
                modem_add_step(sound, STEP_DTMF, c, (uint32_t) s11);
                modem_add_step(sound, STEP_SILENCE, 0, (uint32_t) s11);
            }
        } else if (!pulse && (c == '*' || c == '#' || (c >= 'A' && c <= 'D'))) {
            modem_add_step(sound, STEP_DTMF, c, (uint32_t) s11);
            modem_add_step(sound, STEP_SILENCE, 0, (uint32_t) s11);
        }
    }
    modem_add_step(sound, STEP_SILENCE, 0, 600);
    sound->step_index = 0;
    sound->step_start = 0;
    sound->phase = PH_DIAL;
    sound->sample = 0;
}

uint32_t
modem_sound_dial_ms(const char *number, int s6, int s8, int s11, int pulse, int detect)
{
    uint32_t ms = (detect ? DIAL_TONE_MS : (uint32_t) s6 * 1000u) + 600;
    for (const char *p = number; p && *p; p++) {
        const char c = *p;
        if (c == 'T' || c == 't')
            pulse = 0;
        else if (c == 'P' || c == 'p')
            pulse = 1;
        else if (c == ',')
            ms += (uint32_t) s8 * 1000u;
        else if (c == 'W' || c == 'w')
            ms += DIAL_TONE_MS;
        else if (c >= '0' && c <= '9')
            ms += pulse ? (uint32_t) ((c == '0' ? 10 : c - '0') * 100 + 700) : (uint32_t) s11 * 2u;
        else if (!pulse && (c == '*' || c == '#' || (c >= 'A' && c <= 'D')))
            ms += (uint32_t) s11 * 2u;
    }
    return ms;
}

/* ---- Handshake plan construction ----------------------------------------------------- */

static void
hs_add(hs_plan_t *plan, int kind, uint32_t t0, uint32_t t1, double f1, double f2, double rate,
       int points, double level)
{
    if (plan->count >= HS_MAX_SEGS || t1 <= t0)
        return;
    plan->seg[plan->count++] = (hs_seg_t) {
        .kind = kind, .t0 = t0, .t1 = t1, .f1 = f1, .f2 = f2,
        .rate = rate, .points = points, .level = level
    };
}

static int
hs_resolve_standard(int standard, uint32_t baud)
{
    if (standard > MODEM_SOUND_STD_AUTO && standard < MODEM_SOUND_STD_COUNT)
        return standard;
    if (baud <= 300)
        return MODEM_SOUND_STD_V21;
    if (baud <= 1200)
        return MODEM_SOUND_STD_V22;
    if (baud <= 2400)
        return MODEM_SOUND_STD_V22BIS;
    if (baud <= 9600)
        return MODEM_SOUND_STD_V32;
    if (baud <= 14400)
        return MODEM_SOUND_STD_V32BIS;
    if (baud <= 38400)
        return MODEM_SOUND_STD_V34;
    return MODEM_SOUND_STD_V90;
}

/* V.8 call menu shared by V.34 and V.90: ANSam, CM, JM, CJ. Returns the end time. */
static uint32_t
hs_v8(hs_plan_t *p)
{
    hs_add(p, SEG_ANS, 600, 2100, 2100, 0, 0.2, 0, 0.45);     /* ANSam: 2100 Hz, 15 Hz AM, reversals */
    hs_add(p, SEG_FSK, 2100, 2700, 1180, 980, 300, 0, 0.40);  /* CM, caller, V.21 channel 1 */
    hs_add(p, SEG_FSK, 2700, 3400, 1850, 1650, 300, 0, 0.40); /* JM, answerer, V.21 channel 2 */
    hs_add(p, SEG_FSK, 3400, 3600, 1180, 980, 300, 0, 0.35);  /* CJ */
    return 3900;
}

static void
hs_plan_build(hs_plan_t *p, int std)
{
    uint32_t t;

    memset(p, 0, sizeof(*p));
    switch (std) {
        case MODEM_SOUND_STD_V21:
            hs_add(p, SEG_ANS, 600, 3300, 2100, 0, 0.0, 0, 0.45);
            hs_add(p, SEG_FSK, 3400, 4200, 1180, 980, 300, 0, 0.30); /* steady marks */
            hs_add(p, SEG_FSK, 3400, 4200, 1850, 1650, 300, 0, 0.30);
            p->cd    = 3600;
            p->total = 4200;
            break;
        case MODEM_SOUND_STD_V22:
        case MODEM_SOUND_STD_V22BIS: {
            const int bis = std == MODEM_SOUND_STD_V22BIS;
            hs_add(p, SEG_ANS, 600, 3900, 2100, 0, 0.0, 0, 0.45);
            hs_add(p, SEG_PSK, 3975, 4575, 2400, 0, 600, 1, 0.35); /* answerer unscrambled ones */
            hs_add(p, SEG_PSK, 4300, 4575, 1200, 0, 600, 1, 0.30); /* caller unscrambled ones */
            t = 4575;
            if (bis) {
                hs_add(p, SEG_PSK, t, t + 100, 2400, 0, 600, 2, 0.35); /* S1 */
                hs_add(p, SEG_PSK, t, t + 100, 1200, 0, 600, 2, 0.30);
                t += 100;
            }
            hs_add(p, SEG_PSK, t, t + 1100, 2400, 0, 600, 4, 0.35);   /* scrambled ones */
            hs_add(p, SEG_PSK, t, t + 1100, 1200, 0, 600, 4, 0.30);
            t += 1100;
            p->cd = t;
            hs_add(p, SEG_PSK, t, t + (bis ? 900 : 500), 2400, 0, 600, bis ? 16 : 4, 0.35);
            hs_add(p, SEG_PSK, t, t + (bis ? 900 : 500), 1200, 0, 600, bis ? 16 : 4, 0.30);
            p->total = t + (bis ? 900 : 500);
            break;
        }
        case MODEM_SOUND_STD_V32:
        case MODEM_SOUND_STD_V32BIS: {
            const int bis = std == MODEM_SOUND_STD_V32BIS;
            hs_add(p, SEG_ANS, 600, 3900, 2100, 0, 0.0, 0, 0.45);   /* ANS with phase reversals */
            hs_add(p, SEG_TONE, 4000, 4700, 600, 3000, 0, 0, 0.40); /* AC (caller) */
            hs_add(p, SEG_TONE, 4700, 5300, 600, 3000, 0, 0, 0.40); /* AA (answerer) */
            hs_add(p, SEG_PSK, 5300, 5600, 1800, 0, 2400, 2, 0.40); /* S / S-bar */
            t = 5600 + (bis ? 3000u : 2400u);
            hs_add(p, SEG_PSK, 5600, t, 1800, 0, 2400, 4, 0.45);    /* TRN, both ends, same band */
            hs_add(p, SEG_PSK, t, t + 800, 1800, 0, 2400, 4, 0.40); /* R1 / R2 rate signals */
            t += bis ? 900u : 700u;
            p->cd = t;
            hs_add(p, SEG_PSK, t, t + (bis ? 1000 : 300), 1800, 0, 2400, 16, 0.40); /* E / B1 */
            p->total = t + (bis ? 1000 : 300);
            break;
        }
        case MODEM_SOUND_STD_K56FLEX:
        case MODEM_SOUND_STD_X2:
        case MODEM_SOUND_STD_V92: {
            /* Pre-V.90 56k families and V.92. All open with the V.8 menu and V.34-style
               probing; they differ in the proprietary/PCM training that follows. */
            const int k56 = std == MODEM_SOUND_STD_K56FLEX;
            const int x2  = std == MODEM_SOUND_STD_X2;
            const int v92 = std == MODEM_SOUND_STD_V92;
            t = hs_v8(p);
            hs_add(p, SEG_PSK, t, t + 500, 1200, 0, 600, 4, 0.35);         /* INFO0c */
            hs_add(p, SEG_PSK, t + 500, t + 1000, 2400, 0, 600, 4, 0.35);  /* INFO0a */
            t += 1000;
            hs_add(p, SEG_ANS, t, t + 600, 2400, 0, 0.0, 0, 0.35);         /* Tone A */
            hs_add(p, SEG_TONE, t, t + 600, 1200, 0, 0, 0, 0.30);          /* Tone B */
            t += 600;
            if (x2) { /* X2 identification burst */
                hs_add(p, SEG_TONE, t, t + 200, 1200, 2400, 0, 0, 0.35);
                hs_add(p, SEG_TONE, t + 300, t + 500, 1800, 0, 0, 0, 0.35);
                t += 600;
            }
            hs_add(p, SEG_MULTI, t, t + 160, 150, 0, 0, 0, 0.55);          /* L1 probing */
            t += 160;
            {
                const uint32_t l2 = v92 ? 1100u : (k56 ? 1700u : 1400u);
                hs_add(p, SEG_MULTI, t, t + l2, 150, 0, 0, 0, 0.55);       /* L2 probing */
                t += l2 + 300;
            }
            if (k56) {
                hs_add(p, SEG_PSK, t, t + 1500, 2400, 0, 2400, 16, 0.40);  /* K56flex line probe */
                t += 1500;
                hs_add(p, SEG_NOISE, t, t + 2600, 0, 0, 0, 0, 0.45);       /* digital training */
                t += 2600;
                hs_add(p, SEG_PSK, t, t + 2000, 1800, 0, 3200, 4, 0.45);
                t += 2000;
                hs_add(p, SEG_PSK, t, t + 1500, 1800, 0, 2400, 16, 0.40);  /* parameter exchange */
                t += 1500;
                p->cd = t;
                hs_add(p, SEG_NOISE, t, t + 1500, 0, 0, 0, 0, 0.45);
                p->total = t + 1500;
            } else if (x2) {
                hs_add(p, SEG_NOISE, t, t + 1600, 0, 0, 0, 0, 0.45);       /* X2 PCM probing */
                t += 1600;
                hs_add(p, SEG_PSK, t, t + 2400, 1800, 0, 3200, 4, 0.45);
                t += 2400;
                hs_add(p, SEG_NOISE, t, t + 2200, 0, 0, 0, 0, 0.40);
                t += 2200;
                hs_add(p, SEG_PSK, t, t + 1800, 1800, 0, 2400, 16, 0.40);
                t += 1800;
                p->cd = t;
                hs_add(p, SEG_NOISE, t, t + 1800, 0, 0, 0, 0, 0.45);
                p->total = t + 1800;
            } else { /* V.92 with Quick Connect: shortened training */
                hs_add(p, SEG_NOISE, t, t + 1500, 0, 0, 0, 0, 0.45);       /* digital impairment learning */
                t += 1500;
                hs_add(p, SEG_PSK, t, t + 1600, 1800, 0, 3200, 4, 0.45);
                t += 1600;
                hs_add(p, SEG_PSK, t, t + 1200, 1800, 0, 2400, 16, 0.40);
                t += 1200;
                p->cd = t;
                hs_add(p, SEG_NOISE, t, t + 1400, 0, 0, 0, 0, 0.45);
                p->total = t + 1400;
            }
            break;
        }
        case MODEM_SOUND_STD_V34:
        case MODEM_SOUND_STD_V90:
        default: {
            const int v90 = std == MODEM_SOUND_STD_V90;
            t = hs_v8(p);
            hs_add(p, SEG_PSK, t, t + 500, 1200, 0, 600, 4, 0.35);         /* INFO0c */
            hs_add(p, SEG_PSK, t + 500, t + 1000, 2400, 0, 600, 4, 0.35);  /* INFO0a */
            t += 1000;
            hs_add(p, SEG_ANS, t, t + 600, 2400, 0, 0.0, 0, 0.35);         /* Tone A */
            hs_add(p, SEG_TONE, t, t + 600, 1200, 0, 0, 0, 0.30);          /* Tone B */
            t += 600;
            hs_add(p, SEG_MULTI, t, t + 160, 150, 0, 0, 0, 0.55);          /* L1 probing */
            t += 160;
            hs_add(p, SEG_MULTI, t, t + (v90 ? 1500u : 1900u), 150, 0, 0, 0, 0.55); /* L2 probing */
            t += v90 ? 1500u : 1900u;
            t += 300; /* quiet while the far end analyses the probe */
            if (v90) {
                hs_add(p, SEG_NOISE, t, t + 2300, 0, 0, 0, 0, 0.45);       /* digital training (Ts) */
                t += 2300;
                hs_add(p, SEG_PSK, t, t + 3000, 1800, 0, 3200, 4, 0.45);   /* V.34-style probing/PP */
                t += 3000;
                hs_add(p, SEG_PSK, t, t + 2000, 1800, 0, 2400, 16, 0.40);  /* MPh */
                t += 2000;
                p->cd = t;
                hs_add(p, SEG_NOISE, t, t + 1900, 0, 0, 0, 0, 0.45);       /* PCM data mode training */
                p->total = t + 1900;
            } else {
                hs_add(p, SEG_PSK, t, t + 2500, 1900, 0, 3200, 4, 0.45);   /* PP / TRN */
                t += 2500;
                hs_add(p, SEG_PSK, t, t + 1700, 1800, 0, 2400, 16, 0.40);  /* MPh */
                t += 1700;
                hs_add(p, SEG_PSK, t, t + 1500, 1800, 0, 600, 4, 0.35);    /* INFO1 / rate exchange */
                t += 1500;
                p->cd = t;
                hs_add(p, SEG_PSK, t, t + 1500, 1959, 0, 3429, 16, 0.45);  /* B1 / scrambled ones */
                p->total = t + 1500;
            }
            break;
        }
    }
}

uint32_t
modem_sound_ring_ms(modem_sound_t *sound)
{
    const modem_tone_profile_t *profile = sound ? modem_sound_profile(sound)
                                                : &modem_tone_profiles[MODEM_SOUND_REGION_NORTH_AMERICA];
    if (MODEM_SOUND_RING_COUNT == 0)
        return 0;
    return profile->ring_wait_ms + (MODEM_SOUND_RING_COUNT - 1) * profile->ring_cycle_ms;
}

uint32_t modem_sound_handshake_ms(void) { return HANDSHAKE_MS; }

uint32_t
modem_sound_handshake_for(modem_sound_t *sound, uint32_t baud)
{
    if (sound && atomic_load(&sound->off))
        return 0;
    if (!sound || !atomic_load(&sound->full))
        return HANDSHAKE_MS;
    hs_plan_t plan;
    hs_plan_build(&plan, hs_resolve_standard(atomic_load(&sound->standard), baud));
    return plan.total;
}

uint32_t
modem_sound_connect_for(modem_sound_t *sound, uint32_t baud)
{
    const uint32_t total = modem_sound_handshake_for(sound, baud);

    /* Short handshakes keep their first half, so the answer tone still starts first. */
    return total > HANDSHAKE_CONNECT_OVERLAP_MS * 2 ? total - HANDSHAKE_CONNECT_OVERLAP_MS : total / 2;
}

void
modem_sound_set_handshake(modem_sound_t *sound, int handshake, int standard)
{
    if (!sound)
        return;
    atomic_store(&sound->full, handshake == MODEM_SOUND_HANDSHAKE_FULL);
    atomic_store(&sound->off, handshake == MODEM_SOUND_HANDSHAKE_OFF);
    atomic_store(&sound->standard,
                 (standard >= 0 && standard < MODEM_SOUND_STD_COUNT) ? standard : MODEM_SOUND_STD_AUTO);
}

int
modem_sound_full(modem_sound_t *sound)
{
    return sound ? atomic_load(&sound->full) : 0;
}

void
modem_sound_set_region(modem_sound_t *sound, int region)
{
    if (!sound)
        return;
    if (region < 0 || region >= MODEM_SOUND_REGION_COUNT)
        region = MODEM_SOUND_REGION_NORTH_AMERICA;
    atomic_store(&sound->region, region);
}

void
modem_sound_event(modem_sound_t *sound, int type, const char *number, int arg)
{
    if (!sound)
        return;
    const unsigned write = atomic_load(&sound->write_index);
    const unsigned read = atomic_load(&sound->read_index);
    if (write - read >= EVENT_QUEUE_SIZE)
        return;
    sound_event_t *event = &sound->events[write % EVENT_QUEUE_SIZE];
    event->type = type;
    event->arg = arg;
    snprintf(event->number, sizeof(event->number), "%s", number ? number : "");
    atomic_store(&sound->write_index, write + 1);
}

void
modem_sound_speaker(modem_sound_t *sound, int mode, int level)
{
    if (!sound)
        return;
    atomic_store(&sound->mode, mode < 0 ? 0 : (mode > 3 ? 3 : mode));
    atomic_store(&sound->level, level < 0 ? 0 : (level > 3 ? 3 : level));
}

static void
modem_sound_start_handshake(modem_sound_t *sound, int arg)
{
    sound->phase = PH_HANDSHAKE;
    sound->sample = 0;
    sound->phase1 = sound->phase2 = sound->phase3 = 0.0;
    sound->hs_full = atomic_load(&sound->full);
    sound->hs_off = atomic_load(&sound->off);
    sound->hs_connected = 0;
    sound->hs_answerer = !!(arg & MODEM_SOUND_ANSWER_ANSWERER);
    if (sound->hs_full) {
        const uint32_t baud = (uint32_t) (arg >> 8) * 100u;
        hs_plan_build(&sound->hs_plan, hs_resolve_standard(atomic_load(&sound->standard), baud));
        memset(sound->hs_state, 0, sizeof(sound->hs_state));
        for (int i = 0; i < HS_MAX_SEGS; i++)
            sound->hs_state[i].rng = 0x9e3779b9u * (uint32_t) (i + 1) + 12345u;
    }
}

static void
modem_sound_apply_event(modem_sound_t *sound, int type, int arg)
{
    switch (type) {
        case MODEM_SOUND_ANSWER:
            modem_sound_start_handshake(sound, arg);
            break;
        case MODEM_SOUND_CONNECT:
            if (sound->phase == PH_HANDSHAKE && sound->hs_full) {
                sound->hs_connected = 1;
                break;
            }
            sound->phase = PH_ONLINE;
            sound->sample = 0;
            break;
        case MODEM_SOUND_BUSY:
            sound->phase = PH_BUSY;
            sound->sample = 0;
            break;
        case MODEM_SOUND_RINGING:
            sound->phase = PH_RINGING;
            sound->sample = 0;
            sound->phase1 = sound->phase2 = sound->phase3 = 0.0;
            break;
        case MODEM_SOUND_HANGUP:
        default:
            sound->phase = PH_IDLE;
            sound->sample = 0;
            break;
    }
}

static void
modem_sound_take_events(modem_sound_t *sound)
{
    const unsigned write = atomic_load(&sound->write_index);
    unsigned read = atomic_load(&sound->read_index);
    while (read != write) {
        const sound_event_t *event = &sound->events[read % EVENT_QUEUE_SIZE];
        if (event->type != MODEM_SOUND_DIAL && sound->phase == PH_DIAL
            && sound->step_index < sound->step_count) {
            /* Do not let a fast failure/hangup erase the dial tones before they play. */
            sound->pending_event = event->type;
            sound->pending_arg = event->arg;
            read++;
            continue;
        }
        switch (event->type) {
            case MODEM_SOUND_DIAL:
                modem_build_dial(sound, event->number, (event->arg >> 10) & 0xff,
                                 event->arg & 0xff, (event->arg >> 18) & 0xff,
                                 (event->arg >> 8) & 1, (event->arg >> 26) & 1);
                sound->dial_connect_after = !!(event->arg & MODEM_SOUND_DIAL_CONNECT_AFTER);
                sound->pending_event = -1;
                break;
            default:
                modem_sound_apply_event(sound, event->type, event->arg);
                break;
        }
        read++;
    }
    atomic_store(&sound->read_index, read);
}

static double
modem_dtmf(modem_sound_t *sound, int digit)
{
    static const char keys[] = "123A456B789C*0#D";
    static const double rows[] = { 697, 770, 852, 941 };
    static const double cols[] = { 1209, 1336, 1477, 1633 };
    const char *key = strchr(keys, digit);
    if (!key)
        return 0.0;
    const int index = (int) (key - keys);
    return 0.5 * modem_oscillator(&sound->phase1, rows[index / 4], sound->rate)
         + 0.5 * modem_oscillator(&sound->phase2, cols[index % 4], sound->rate);
}

static double
modem_ringback(modem_sound_t *sound)
{
    const modem_tone_profile_t *profile = modem_sound_profile(sound);
    const uint32_t ms = (uint32_t) (((uint64_t) sound->sample * 1000u) / sound->rate);
    return modem_ring_cadence_on(profile, ms)
               ? modem_tone(sound, profile->ring_f1, profile->ring_f2)
               : 0.0;
}

static double
modem_busy_tone(modem_sound_t *sound)
{
    const modem_tone_profile_t *profile = modem_sound_profile(sound);
    const uint32_t ms = (uint32_t) (((uint64_t) sound->sample * 1000u) / sound->rate);
    const uint32_t cycle = profile->busy_on_ms + profile->busy_off_ms;
    return (ms % cycle < profile->busy_on_ms)
               ? modem_tone(sound, profile->busy_f1, profile->busy_f2)
               : 0.0;
}

static double
modem_ringing_tone(modem_sound_t *sound)
{
    const modem_tone_profile_t *profile = modem_sound_profile(sound);
    const uint32_t ms = (uint32_t) (((uint64_t) sound->sample * 1000u) / sound->rate);
    if (!modem_ring_cadence_on(profile, ms))
        return 0.0;

    /* A 20 Hz burst-gated dual tone gives the modem speaker a bell-like ring. */
    const double carrier = modem_tone(sound, profile->ring_f1, profile->ring_f2);
    const double ring_gate = modem_oscillator(&sound->phase3, 20, sound->rate);
    return (ring_gate > 0.0) ? carrier : 0.0;
}

/* ---- Handshake rendering ------------------------------------------------------------- */

static uint32_t
hs_next(hs_state_t *st)
{
    st->rng ^= st->rng << 13;
    st->rng ^= st->rng >> 17;
    st->rng ^= st->rng << 5;
    return st->rng;
}

static void
hs_next_point(hs_state_t *st, int points, double *i, double *q)
{
    const uint32_t r = hs_next(st) >> 8;

    switch (points) {
        case 1:
            *i = 1.0;
            *q = 0.0;
            break;
        case 2: /* alternating 00 / 11 pattern */
            st->bit ^= 1;
            *i = st->bit ? 1.0 : -1.0;
            *q = 0.0;
            break;
        case 4: {
            static const double c[4] = { 0.7071, -0.7071, -0.7071, 0.7071 };
            *i = c[r & 3];
            *q = c[(r >> 2) & 3] * ((r & 3) == ((r >> 2) & 3) ? 1.0 : -1.0);
            break;
        }
        default: {
            static const double c[4] = { -0.75, -0.25, 0.25, 0.75 };
            *i = c[r & 3];
            *q = c[(r >> 2) & 3];
            break;
        }
    }
}

static double
hs_segment_sample(modem_sound_t *sound, const hs_seg_t *seg, hs_state_t *st, uint32_t local,
                  uint32_t length)
{
    const int rate = sound->rate;
    const double t = (double) local / rate;
    double v = 0.0;

    switch (seg->kind) {
        case SEG_TONE:
            v = 0.5 * modem_oscillator(&st->ph1, seg->f1, rate);
            if (seg->f2 > 0.0)
                v += 0.5 * modem_oscillator(&st->ph2, seg->f2, rate);
            break;
        case SEG_ANS: {
            /* Phase reversal every 450 ms; ANSam also has 15 Hz amplitude modulation. */
            const double sign = (((uint64_t) (t * 1000.0) / 450u) & 1u) ? -1.0 : 1.0;
            const double am = 1.0 + seg->rate * sin(2.0 * M_PI * 15.0 * t);
            v = sign * am / (1.0 + seg->rate) * modem_oscillator(&st->ph1, seg->f1, rate);
            break;
        }
        case SEG_FSK: {
            st->pos += seg->rate / rate;
            if (st->pos >= 1.0) {
                st->pos -= 1.0;
                st->bit = (int) ((hs_next(st) >> 11) & 1u);
            }
            v = modem_oscillator(&st->ph1, st->bit ? seg->f2 : seg->f1, rate);
            break;
        }
        case SEG_PSK: {
            if (local == 0) {
                hs_next_point(st, seg->points, &st->ci, &st->cq);
                st->pi = st->ci;
                st->pq = st->cq;
            }
            st->pos += seg->rate / rate;
            if (st->pos >= 1.0) {
                st->pos -= 1.0;
                st->pi = st->ci;
                st->pq = st->cq;
                hs_next_point(st, seg->points, &st->ci, &st->cq);
            }
            /* Smooth the jump between symbols over the first 30% of the symbol. */
            const double w = st->pos < 0.3 ? 0.5 - 0.5 * cos(M_PI * st->pos / 0.3) : 1.0;
            const double bi = st->pi + (st->ci - st->pi) * w;
            const double bq = st->pq + (st->cq - st->pq) * w;
            st->ph1 += 2.0 * M_PI * seg->f1 / rate;
            if (st->ph1 >= 2.0 * M_PI)
                st->ph1 -= 2.0 * M_PI;
            v = bi * cos(st->ph1) - bq * sin(st->ph1);
            break;
        }
        case SEG_MULTI: {
            /* Probing signal: tones every 150 Hz (V.34 skips the multiples that clash with
               the carriers), with Schroeder phases so it does not turn into a click. */
            static const int ks[] = { 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 13, 14, 15, 16, 17, 19, 20, 21, 22, 23 };
            const int n = (int) (sizeof(ks) / sizeof(ks[0]));
            for (int k = 0; k < n; k++)
                v += sin(2.0 * M_PI * seg->f1 * ks[k] * t + M_PI * k * k / n);
            v /= 4.5;
            break;
        }
        case SEG_NOISE: {
            const double white = modem_noise(sound);
            st->lp1 += 0.40 * (white - st->lp1);
            st->lp2 += 0.04 * (white - st->lp2);
            v = 2.2 * (st->lp1 - st->lp2);
            break;
        }
        default:
            break;
    }

    /* 3 ms ramps stop the segment edges from clicking. */
    const double ramp = 0.003 * rate;
    double env = 1.0;
    if (local < ramp)
        env = local / ramp;
    if (length - local < ramp)
        env = fmin(env, (length - local) / ramp);
    return v * seg->level * env;
}

static double
hs_sample(modem_sound_t *sound, uint32_t ms)
{
    double sum = 0.0;

    for (int i = 0; i < sound->hs_plan.count; i++) {
        const hs_seg_t *seg = &sound->hs_plan.seg[i];
        if (ms < seg->t0 || ms >= seg->t1)
            continue;
        const uint32_t start  = (uint32_t) (((uint64_t) seg->t0 * (uint64_t) sound->rate) / 1000u);
        const uint32_t length = (uint32_t) (((uint64_t) (seg->t1 - seg->t0) * (uint64_t) sound->rate) / 1000u);
        if (sound->sample < start)
            continue;
        sum += hs_segment_sample(sound, seg, &sound->hs_state[i], sound->sample - start, length);
    }
    return tanh(sum * 0.8);
}

/* ATM: 0 off, 1 on until carrier is detected, 2 always on, 3 on while answering only. */
static int
hs_audible(const modem_sound_t *sound, int mode, uint32_t ms)
{
    switch (mode) {
        case 1:
            return ms < sound->hs_plan.cd;
        case 2:
            return 1;
        case 3:
            return sound->hs_answerer && ms < sound->hs_plan.cd;
        default:
            return 0;
    }
}

static double
modem_sound_sample(modem_sound_t *sound, int mode)
{
    double value = 0.0;
    sound->audible_now = 0;
    if (sound->phase == PH_DIAL) {
        if (sound->step_index < sound->step_count) {
            const dial_step_t *step = &sound->steps[sound->step_index];
            const uint32_t elapsed = sound->sample - sound->step_start;
            if (mode == 1 || mode == 2) {
                sound->audible_now = 1;
                switch (step->kind) {
                    case STEP_TONE:
                        value = modem_dial_tone(sound);
                        break;
                    case STEP_DTMF: value = modem_dtmf(sound, step->digit); break;
                    case STEP_PULSE:
                    case STEP_MAKE: {
                        /* Relay break and make each produce a short, decaying click. */
                        const double age = (double) elapsed / sound->rate;
                        value = age < 0.015 ? 0.9 * modem_noise(sound) * exp(-age / 0.0025) : 0.0;
                        break;
                    }
                    default: break;
                }
            }
            if (elapsed + 1 >= step->samples) {
                sound->step_index++;
                sound->step_start = sound->sample + 1;
            }
        } else {
            if (sound->dial_connect_after) {
                sound->dial_connect_after = 0;
                sound->phase = PH_ONLINE;
                sound->sample = 0;
            } else if (sound->pending_event >= 0) {
                const int pending_event = sound->pending_event;
                sound->pending_event = -1;
                modem_sound_apply_event(sound, pending_event, sound->pending_arg);
            } else {
                sound->phase = PH_RING;
                sound->sample = 0;
            }
        }
    } else if (sound->phase == PH_RING) {
        /* Ringback happens while dialling, so ATM3 keeps it silent. */
        if (mode == 1 || mode == 2) {
            sound->audible_now = 1;
            value = 0.6 * modem_ringback(sound);
        }
    } else if (sound->phase == PH_BUSY) {
        const uint32_t ms = (uint32_t) (((uint64_t) sound->sample * 1000u) / sound->rate);
        if (ms >= 3000)
            sound->phase = PH_IDLE;
        else if (mode == 1 || mode == 2) {
            sound->audible_now = 1;
            value = 0.6 * modem_busy_tone(sound);
        }
    } else if (sound->phase == PH_RINGING) {
        if (mode != 0) {
            sound->audible_now = 1;
            value = 0.6 * modem_ringing_tone(sound);
        }
    } else if (sound->phase == PH_HANDSHAKE) {
        const uint32_t ms = (uint32_t) (((uint64_t) sound->sample * 1000u) / sound->rate);
        if (sound->hs_full) {
            if (ms >= sound->hs_plan.total) {
                if (sound->hs_connected) {
                    sound->phase = PH_ONLINE;
                    sound->sample = 0;
                }
            } else if ((sound->hs_connected && mode != 0) || hs_audible(sound, mode, ms)) {
                sound->audible_now = 1;
                value = modem_line_fizz(sound, hs_sample(sound, ms), 0.04);
            }
        } else if (!sound->hs_off && (mode == 1 || mode == 2 || mode == 3)) {
            sound->audible_now = 1;
            const double hiss = modem_noise(sound);
            value = ms < 1300 ? modem_oscillator(&sound->phase1, 2100, sound->rate)
                 : ms < 4000 ? 0.5 * modem_oscillator(&sound->phase1, 1200, sound->rate)
                             + 0.5 * modem_oscillator(&sound->phase2, 2400, sound->rate)
                 : 0.6 * hiss + 0.4 * hiss * modem_oscillator(&sound->phase3, 1800, sound->rate);
            value = modem_line_fizz(sound, value, ms < 4000 ? 0.08 : 0.18);
        }
    } else if (sound->phase == PH_ONLINE && mode == 2 && MODEM_SOUND_ONLINE_HISS > 0.0) {
        sound->audible_now = 1;
        value = modem_line_fizz(sound, 0.0, MODEM_SOUND_ONLINE_HISS);
    }
    sound->sample++;
    return value;
}

static void
modem_sound_get_buffer(int32_t *buffer, uint16_t length, void *priv)
{
    modem_sound_t *sound = (modem_sound_t *) priv;
    if (!sound || !buffer || sound_sample_rate <= 0)
        return;
    sound->rate = sound_sample_rate;
    modem_sound_take_events(sound);
    const int mode = atomic_load(&sound->mode);
    const int level = atomic_load(&sound->level);
    static const double gains[4] = { 0.30, 0.30, 0.55, 0.85 };
    const double gain = 11000.0 * gains[level];
    for (uint16_t i = 0; i < length; i++) {
        double sample = modem_sound_sample(sound, mode);
        if (sound->audible_now)
            sample += 0.01 * modem_noise(sound);
        if (mode == 0)
            sample = 0.0;
        const int32_t output = (int32_t) (sample * gain);
        buffer[(i << 1)] += output;
        buffer[(i << 1) + 1] += output;
    }
}

modem_sound_t *
modem_sound_init(void)
{
    for (int i = 0; i < MODEM_SOUND_SLOTS; i++) {
        if (!modem_speakers[i]) {
            modem_speakers[i] = (modem_sound_t *) calloc(1, sizeof(modem_sound_t));
            if (!modem_speakers[i])
                return NULL;
            atomic_init(&modem_speakers[i]->write_index, 0);
            atomic_init(&modem_speakers[i]->read_index, 0);
            atomic_init(&modem_speakers[i]->mode, 1);
            atomic_init(&modem_speakers[i]->level, 2);
            atomic_init(&modem_speakers[i]->region, MODEM_SOUND_REGION_NORTH_AMERICA);
            atomic_init(&modem_speakers[i]->full, 0);
            atomic_init(&modem_speakers[i]->off, 0);
            atomic_init(&modem_speakers[i]->standard, MODEM_SOUND_STD_AUTO);
            modem_speakers[i]->noise_state = 0x4d4f4445u + (uint32_t) i;
        }
        if (!modem_speakers[i]->in_use) {
            modem_sound_t *sound = modem_speakers[i];
            sound->in_use = 1;
            sound->phase = PH_IDLE;
            sound->step_count = sound->step_index = 0;
            sound->dial_connect_after = 0;
            sound->pending_event = -1;
            sound->pending_arg = 0;
            atomic_store(&sound->read_index, atomic_load(&sound->write_index));
            if (!sound->handler_registered) {
                sound_add_handler(modem_sound_get_buffer, sound);
                sound->handler_registered = 1;
            }
            return sound;
        }
    }
    return NULL;
}

void
modem_sound_close(modem_sound_t *sound)
{
    if (sound) {
        modem_sound_event(sound, MODEM_SOUND_HANGUP, NULL, 0);
        sound->in_use = 0;
    }
}
