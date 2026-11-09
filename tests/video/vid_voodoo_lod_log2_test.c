/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Tests of the table log2 behind the Voodoo LOD.
 *
 *          The renderer is included as C, for its private functions, with
 *          the few platform and texture functions it calls as stubs.
 *          voodoo_fast_log2() is checked against libm's log2() over many
 *          octaves, and so is the 8.8 LOD that voodoo_lod_from_gradients()
 *          makes from it.
 *
 * Authors: Andreas Zdziarstek, <andreas.zdziarstek@gmail.com>
 *
 *          Copyright 2026 Andreas Zdziarstek.
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/video/vid_voodoo_render.c"

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                \
        }                                                              \
    } while (0)

/* The bound the comment on voodoo_fast_log2() claims. */
#define FAST_LOG2_MAX_ERROR 3e-6

#define RANDOM_SAMPLES      1000000
#define MIN_EXPONENT        -64
#define MAX_EXPONENT        64

static int failures;

/* Stubs for what the renderer calls outside this test. */
rgba8_t rgb565[0x10000];
int     tris;

void *
plat_mmap(size_t size, UNUSED(uint8_t executable), UNUSED(uint8_t *large))
{
    return malloc(size);
}

void
plat_munmap(void *ptr, UNUSED(size_t size))
{
    free(ptr);
}

uint64_t
plat_timer_read(void)
{
    return 0;
}

void
thread_set_event(UNUSED(event_t *arg))
{
}

void
thread_reset_event(UNUSED(event_t *arg))
{
}

int
thread_wait_event(UNUSED(event_t *arg), UNUSED(int timeout))
{
    return 0;
}

void
voodoo_use_texture(UNUSED(voodoo_t *voodoo), UNUSED(voodoo_params_t *params), UNUSED(int tmu))
{
}

void
fatal(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    abort();
}

/* xorshift64, so the samples are the same on every host. */
static uint64_t
next_random(uint64_t *state)
{
    uint64_t x = *state;

    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

/* A value in [2^MIN_EXPONENT, 2^(MAX_EXPONENT + 1)) with a random mantissa. */
static double
random_sample(uint64_t *state)
{
    uint64_t r        = next_random(state);
    double   mantissa = 1.0 + (double) (r >> 11) / 9007199254740992.0; /* 53 random bits in [0, 1) */
    int      exponent = MIN_EXPONENT + (int) (next_random(state) % (MAX_EXPONENT - MIN_EXPONENT + 1));

    return ldexp(mantissa, exponent);
}

static double max_error_seen;

static void
check_sample(double val)
{
    double error = fabs(voodoo_fast_log2(val) - log2(val));

    if (error > max_error_seen)
        max_error_seen = error;
    if (error > FAST_LOG2_MAX_ERROR) {
        fprintf(stderr, "voodoo_fast_log2(%.17g) = %.17g, log2() = %.17g, error %.3g\n", val, voodoo_fast_log2(val), log2(val), error);
        failures++;
    }
}

static void
test_table(void)
{
    CHECK(voodoo_log2_table[0] == 0.0);
    CHECK(voodoo_log2_table[256] == 1.0);
    for (int i = 0; i < 256; i++)
        CHECK(voodoo_log2_table[i] < voodoo_log2_table[i + 1]);
}

/* Powers of two hit a table entry with no interpolation, so they are exact. */
static void
test_powers_of_two(void)
{
    for (int exponent = MIN_EXPONENT; exponent <= MAX_EXPONENT; exponent++)
        CHECK(voodoo_fast_log2(ldexp(1.0, exponent)) == (double) exponent);
}

/* Each table entry, and the largest double just below it and the next power of two. */
static void
test_table_edges(void)
{
    for (int exponent = MIN_EXPONENT; exponent <= MAX_EXPONENT; exponent++) {
        for (int i = 0; i <= 256; i++) {
            double val = ldexp(1.0 + i / 256.0, exponent);

            check_sample(val);
            check_sample(nextafter(val, 0.0));
        }
    }
}

static void
test_random_samples(void)
{
    uint64_t state = 0x86b0c5eed1234567ULL;

    for (int i = 0; i < RANDOM_SAMPLES; i++)
        check_sample(random_sample(&state));
}

/* The LOD from the table may differ from the LOD from libm only where log2 lies
   right on a step, and then by one step. */
static void
test_lod_steps(void)
{
    uint64_t state  = 0x1234567886b0c5eeULL;
    int      differ = 0;

    for (int i = 0; i < RANDOM_SAMPLES; i++) {
        double val       = random_sample(&state);
        int    lod_table = (int) (voodoo_fast_log2(val) / 2.0 * 256.0);
        int    lod_libm  = (int) (log2(val) / 2.0 * 256.0);

        CHECK(abs(lod_table - lod_libm) <= 1);
        differ += lod_table != lod_libm;
    }
    /* Off by one should be rare: the error is far below a step. */
    CHECK(differ < RANDOM_SAMPLES / 1000);
}

int
main(void)
{
    voodoo_lod_table_init();

    test_table();
    test_powers_of_two();
    test_table_edges();
    test_random_samples();
    test_lod_steps();

    printf("max |voodoo_fast_log2() - log2()| = %.3g (bound %.3g)\n", max_error_seen, FAST_LOG2_MAX_ERROR);
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
