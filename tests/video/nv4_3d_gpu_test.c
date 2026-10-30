/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Tests of the RIVA TNT's GPU lane (nv4_pgraph_3d_gpu.c) against
 *          the CPU: every batch is shaded by the lane and again by the
 *          CPU (NV4_3D_VERIFY), and no byte may differ -- a batch of
 *          states running different kernel variants, ROP fills joining
 *          a batch, and a swizzled target (rows that alias: one
 *          workgroup in order). Skipped without a Vulkan device.
 *
 *          The 3D code is included as C, as in nv4_3d_swizzle_test.c,
 *          whose stand-ins for the rest of the card this repeats; the
 *          lane and nv_gpu_vk.c are linked as they are.
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/video/nv/nv4/subsystems/nv4_pgraph_3d.c"

#define VRAM_SIZE (1 << 20)
#define COL_BASE  0x10000
#define Z_BASE    0x20000

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                \
        }                                                              \
    } while (0)

static int failures;

/* What the 3D code reaches outside itself: nothing here textures,
   threads or uses the GPU lane. */
uint64_t tsc;
double   cpuclock = 1.0;
uint32_t mem_size;
uint8_t *ram;

/* the lane's and the verifier's lines, for a failure's story */
void
pclog(const char *fmt, ...)
{
    va_list ap;

    if (!getenv("NV4_TEST_LOG") && !strstr(fmt, "verify: flush"))
        return;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void
fatal(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(2);
}

/* ROP3 from its truth table */
static uint32_t
nv4_rop3_ref(uint8_t rop, uint32_t p, uint32_t s, uint32_t d)
{
    uint32_t r = 0;

    for (int b = 0; b < 32; b++) {
        int i = (((p >> b) & 1) << 2) | (((s >> b) & 1) << 1) | ((d >> b) & 1);

        r |= (uint32_t) ((rop >> i) & 1) << b;
    }
    return r;
}

int
agpgart_resolve(uint32_t addr, uint32_t *ram_addr)
{
    (void) addr;
    (void) ram_addr;
    return 0;
}

uint8_t
mem_readb_phys(uint32_t addr)
{
    (void) addr;
    return 0;
}

uint16_t
mem_readw_phys(uint32_t addr)
{
    (void) addr;
    return 0;
}

uint32_t
mem_readl_phys(uint32_t addr)
{
    (void) addr;
    return 0;
}

int
nv4_dma_translate(nv4_t *nv4, uint32_t inst, uint32_t offset, int write, uint32_t *addr, int *target)
{
    (void) nv4;
    (void) inst;
    (void) offset;
    (void) write;
    (void) addr;
    (void) target;
    return !NV4_DMA_OK;
}

uint32_t
nv4_dma_page_left(nv4_t *nv4, uint32_t inst, uint32_t offset)
{
    (void) nv4;
    (void) inst;
    (void) offset;
    return 0x1000;
}

void
nv4_pgraph_2d_flush_run(nv4_t *nv4)
{
    (void) nv4;
}

void
nv4_pgraph_set_grobj(nv4_t *nv4, int word, int shift, int bits, uint32_t val)
{
    (void) nv4;
    (void) word;
    (void) shift;
    (void) bits;
    (void) val;
}

uint32_t
nv4_ramin_read32(nv4_t *nv4, uint32_t off)
{
    (void) nv4;
    (void) off;
    return 0;
}

/* textures here are never in system memory */
void
dma_bm_read(uint32_t phys_addr, uint8_t *data, uint32_t total_size, int transfer_size)
{
    (void) phys_addr;
    (void) transfer_size;
    memset(data, 0, total_size);
}

/* nv_gpu_vk.c's telemetry file (NV_GPU_TELEMETRY, never set here) */
char usr_path[1024];

FILE *
plat_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

void
path_append_filename(char *dest, const char *s1, const char *s2)
{
    sprintf(dest, "%s/%s", s1, s2);
}

/* the lane's variant compiler runs at once: every variant is ready */
thread_t *
thread_create_named(void (*thread_func)(void *param), void *param, const char *name)
{
    (void) name;
    thread_func(param);
    return NULL;
}

int
thread_wait(thread_t *arg)
{
    (void) arg;
    return 0;
}

event_t *
thread_create_event(void)
{
    return NULL;
}

void
thread_set_event(event_t *arg)
{
    (void) arg;
}

void
thread_reset_event(event_t *arg)
{
    (void) arg;
}

int
thread_wait_event(event_t *arg, int timeout)
{
    (void) arg;
    (void) timeout;
    return 0;
}

void
thread_destroy_event(event_t *arg)
{
    (void) arg;
}

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
static nv4_t    *nv4;
static monitor_t mon;

/* the GPU lane is there (a Vulkan device); else every test is skipped */
static int
card_new(void)
{
    nv4                      = calloc(1, sizeof(nv4_t));
    nv4->vram_size           = VRAM_SIZE;
    nv4->vram_mask           = VRAM_SIZE - 1;
    nv4->svga.vram           = calloc(1, VRAM_SIZE + 4096); /* as svga_init: the lane moves it */
    nv4->svga.changedvram    = calloc(1, VRAM_SIZE >> 12);
    nv4->svga.monitor        = &mon;
    nv4->render_threads      = 1;
    nv4->gpu_raster          = 1;
    mon.mon_changeframecount = 2;
    nv4_3d_init(nv4);
    return nv4->batch3d->gpu != NULL;
}

static void
card_free(void)
{
    nv4_3d_close(nv4);
    free(nv4->svga.vram);
    free(nv4->svga.changedvram);
    free(nv4);
}

/* After a flush: every flush verified and none differed; the GPU took
   <batches> batches more than before and left none to the CPU */
static uint64_t seen_batches;

static void
check_gpu(uint64_t batches, const char *what)
{
    uint64_t n, f;

    nv4_gpu_counts(nv4->batch3d->gpu, &n, &f);
    if (nv4->batch3d->vbad || f || (n - seen_batches != batches)) {
        fprintf(stderr, "  %s: %u of %u flushes differ, GPU took %llu (want %llu), left %llu\n", what, nv4->batch3d->vbad,
                nv4->batch3d->vflushes, (unsigned long long) (n - seen_batches), (unsigned long long) batches,
                (unsigned long long) f);
        failures++;
    }
    seen_batches = n;
}

/* The 3D surfaces as 0x53 leaves them: color A8R8G8B8 at COL_BASE,
   depth at Z_BASE (64 KB limits each); swizzled 2^wl by 2^hl, or rows
   of <pitch> (64 pixels by 32 rows). The user clip is the surface. */
static void
surfaces(int swz, int wl, int hl, uint32_t pitch, int zeta)
{
    NV4_GR(nv4, NV4_PGRAPH_SURFACE)         = swz ? 2 : 1;
    NV4_GR(nv4, NV4_PGRAPH_BSWIZZLE2)       = ((uint32_t) hl << 24) | ((uint32_t) wl << 16);
    NV4_GR(nv4, NV4_PGRAPH_BPIXEL)          = 0xc << 8;
    NV4_GR(nv4, NV4_PGRAPH_BBASE + 2 * 4)   = COL_BASE;
    NV4_GR(nv4, NV4_PGRAPH_BOFFSET + 2 * 4) = 0;
    NV4_GR(nv4, NV4_PGRAPH_BPITCH + 2 * 4)  = pitch;
    NV4_GR(nv4, NV4_PGRAPH_BLIMIT + 2 * 4)  = 0xffff;
    NV4_GR(nv4, NV4_PGRAPH_BBASE + 3 * 4)   = Z_BASE;
    NV4_GR(nv4, NV4_PGRAPH_BOFFSET + 3 * 4) = 0;
    NV4_GR(nv4, NV4_PGRAPH_BPITCH + 3 * 4)  = swz ? 0 : pitch;
    NV4_GR(nv4, NV4_PGRAPH_BLIMIT + 3 * 4)  = zeta ? 0xffff : 0x80000000;
    NV4_GR(nv4, NV4_PGRAPH_UCLIP_MIN)       = 0;
    NV4_GR(nv4, NV4_PGRAPH_UCLIP_MIN + 4)   = 0;
    NV4_GR(nv4, NV4_PGRAPH_UCLIP_MAX)       = swz ? (1u << wl) : 64;
    NV4_GR(nv4, NV4_PGRAPH_UCLIP_MAX + 4)   = swz ? (1u << hl) : 32;
    nv4_3d_flush(nv4);
}

static uint32_t
fbits(float f)
{
    uint32_t u;

    memcpy(&u, &f, 4);
    return u;
}

static void
mthd(uint32_t m, uint32_t d)
{
    CHECK(nv4_pgraph_3d_method(nv4, 0, 0x54, m, d) == NV4_GR_OK);
}

/* DX5 state, untextured: depth test LESS with writes when <zbuf>,
   alpha blending (source alpha, inverse source alpha) when <blend> */
static void
dx5_state(int zbuf, int blend)
{
    mthd(0x308, 0);
    mthd(0x310, blend ? ((1u << 20) | (5u << 24) | (6u << 28)) : 0);
    mthd(0x314, zbuf ? ((0x3fu << 24) | (2u << 16) | (1u << 14)) : 0);
}

static void
vertex(int i, float x, float y, float z, uint32_t color)
{
    uint32_t m = 0x400 + i * 0x20;

    mthd(m + 0x00, fbits(x));
    mthd(m + 0x04, fbits(y));
    mthd(m + 0x08, fbits(z));
    mthd(m + 0x0c, fbits(1.0f));
    mthd(m + 0x10, color);
    mthd(m + 0x14, 0);
}

/* A triangle, colors per vertex */
static void
tri(float x0, float y0, float x1, float y1, float x2, float y2, float z, uint32_t c0, uint32_t c1, uint32_t c2)
{
    vertex(0, x0, y0, z, c0);
    vertex(1, x1, y1, z + 0.1f, c1);
    vertex(2, x2, y2, z - 0.1f, c2);
    mthd(0x600, 0x000210);
}

/* A scene starts on known bytes, with nothing in the batch */
static void
fill_vram(void)
{
    nv4_3d_flush(nv4);
    nv4_gpu_wait(nv4->batch3d->gpu, nv4);
    for (uint32_t a = 0; a < 0x40000; a++)
        nv4->svga.vram[a] = (uint8_t) ((a * 0x9e3779b1u) >> 24);
}

/* Overlapping triangles under four states (depth, blending, both,
   neither): runs of different kernel variants in one batch */
static void
test_variants(void)
{
    fill_vram();
    surfaces(0, 0, 0, 256, 1);
    for (int i = 0; i < 6; i++) {
        float o = (float) i * 3.0f;

        dx5_state(1, 0);
        tri(1 + o, 1, 60, 5 + o, 10, 30, 0.3f + 0.05f * i, 0xff102030, 0xff405060, 0xff708090);
        dx5_state(0, 1);
        tri(2, 2 + o, 63, 10, 20 + o, 31, 0.5f, 0x80ff0000, 0x4000ff00, 0xc00000ff);
        dx5_state(1, 1);
        tri(0, 31, 40 + o, 0, 63, 20, 0.4f, 0x60abcdef, 0xa0123456, 0x20fedcba);
        dx5_state(0, 0);
        tri(5, 5, 30, 8 + o, 8, 25, 0.2f, 0xff00ffff, 0xffff00ff, 0xffffff00);
    }
    nv4_3d_flush(nv4);
    check_gpu(1, "variants");
}

/* ROP fills joining a batch that draws into their rows, under ROPs
   with and without the destination, a mono pattern and a transparent
   color: the GPU's (d & A) | (~d & B) */
static void
test_ropfill(void)
{
    static const uint8_t rops[] = { 0x55, 0x5a, 0x66, 0xa0, 0x96, 0xf0, 0x1d };

    fill_vram();
    surfaces(0, 0, 0, 256, 0);
    dx5_state(0, 0);
    tri(0, 0, 64, 0, 0, 32, 0.5f, 0xff112233, 0xff445566, 0xff778899);
    for (unsigned i = 0; i < sizeof(rops); i++) {
        uint32_t r[7];
        uint32_t s = 0x13579bdf * (i + 1), p0 = 0x00ff00ff ^ i, p1 = 0xff00ff00 ^ (i << 8);

        r[0] = nv4_rop3_ref(rops[i], p0, s, 0xffffffffu);
        r[1] = nv4_rop3_ref(rops[i], p0, s, 0);
        r[2] = nv4_rop3_ref(rops[i], p1, s, 0xffffffffu);
        r[3] = nv4_rop3_ref(rops[i], p1, s, 0);
        r[4] = 0x8142a5c3 ^ i;
        r[5] = 0x3cf00f96;
        r[6] = NV4_ROPFILL_PAT | (i % 3) | ((i & 1) ? NV4_ROPFILL_SKIP0 : 0);
        CHECK(nv4_3d_fill_rop(nv4, COL_BASE, 256, COL_BASE + 0xffff, 4, (int) i, (int) i * 2, 40 + (int) i, 20 + (int) i,
                              r));
    }
    nv4_3d_flush(nv4);
    check_gpu(1, "ROP fills");
}

/* A swizzled target with depth: rows alias, so the GPU takes the batch
   as one workgroup in order */
static void
test_serial(void)
{
    fill_vram();
    surfaces(1, 5, 4, 0, 1);
    for (int i = 0; i < 5; i++) {
        dx5_state(1, i & 1);
        tri(0, (float) i, 32, 3.0f * i, 4.0f * i, 16, 0.2f + 0.1f * i, 0xa0ff8000, 0x6000ff80, 0xff8000ff);
    }
    nv4_3d_flush(nv4);
    check_gpu(1, "swizzled target");
}

int
main(void)
{
    putenv("NV4_3D_VERIFY=1");
    putenv("NV4_GPU_RUN_MIN=1"); /* a run per state change: several variants in a batch */
    /* one card for every scene: the variants compile once */
    if (!card_new()) {
        printf("no Vulkan device: GPU tests skipped\n");
        card_free();
        return 0;
    }
    test_variants();
    test_ropfill();
    test_serial();
    card_free();
    if (failures)
        fprintf(stderr, "%d failure(s)\n", failures);
    else
        printf("nv4 3D GPU tests passed\n");
    return failures != 0;
}
