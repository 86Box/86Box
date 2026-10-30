/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Tests of the RIVA TNT's Direct3D classes drawing into a
 *          swizzled colour surface (render to texture).
 *
 *          The 3D code is included as C. The surface registers are set
 *          as the 3D surfaces class (0x53) leaves them, triangles go in
 *          through the DX5 class's (0x54) methods, and the pixels are
 *          read back from video memory. The swizzled layout is worked
 *          out here, not with the engine's helper.
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

void
pclog(const char *fmt, ...)
{
    (void) fmt;
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

struct nv4_gpu_s *
nv4_gpu_init(nv4_t *nv4)
{
    (void) nv4;
    return NULL;
}

int
nv4_gpu_shade(struct nv4_gpu_s *g, nv4_t *nv4, const nv4_3d_batch_t *b)
{
    (void) g;
    (void) nv4;
    (void) b;
    return 0;
}

void
nv4_gpu_close(struct nv4_gpu_s *g)
{
    (void) g;
}

void
nv4_gpu_wait(struct nv4_gpu_s *g, nv4_t *nv4)
{
    (void) g;
    (void) nv4;
}

int
nv4_gpu_pending_overlaps(struct nv4_gpu_s *g, uint32_t lo, uint32_t hi)
{
    (void) g;
    (void) lo;
    (void) hi;
    return 0;
}

void
nv4_gpu_counts(struct nv4_gpu_s *g, uint64_t *batches, uint64_t *fallbacks)
{
    (void) g;
    *batches = *fallbacks = 0;
}

thread_t *
thread_create_named(void (*thread_func)(void *param), void *param, const char *name)
{
    (void) thread_func;
    (void) param;
    (void) name;
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
static nv4_t     *nv4;
static monitor_t  mon;

static void
card_new(void)
{
    nv4                    = calloc(1, sizeof(nv4_t));
    nv4->vram_size         = VRAM_SIZE;
    nv4->vram_mask         = VRAM_SIZE - 1;
    nv4->svga.vram         = calloc(1, VRAM_SIZE);
    nv4->svga.changedvram  = calloc(1, VRAM_SIZE >> 12);
    nv4->svga.monitor      = &mon;
    nv4->render_threads    = 1;
    mon.mon_changeframecount = 2;
    nv4_3d_init(nv4);
}

static void
card_free(void)
{
    nv4_3d_close(nv4);
    free(nv4->svga.vram);
    free(nv4->svga.changedvram);
    free(nv4);
}

/* The 3D surfaces as 0x53 leaves them: color A8R8G8B8 (format 0xc) at
   COL_BASE, depth at Z_BASE, both with 64 KB limits; type 2 swizzled
   2^wl by 2^hl, or type 1 with <pitch>. The user clip spans the
   surface. */
static void
surfaces(int swz, int wl, int hl, uint32_t pitch, int zeta)
{
    NV4_GR(nv4, NV4_PGRAPH_SURFACE)          = swz ? 2 : 1;
    NV4_GR(nv4, NV4_PGRAPH_BSWIZZLE2)        = ((uint32_t) hl << 24) | ((uint32_t) wl << 16);
    NV4_GR(nv4, NV4_PGRAPH_BPIXEL)           = 0xc << 8;
    NV4_GR(nv4, NV4_PGRAPH_BBASE + 2 * 4)    = COL_BASE;
    NV4_GR(nv4, NV4_PGRAPH_BOFFSET + 2 * 4)  = 0;
    NV4_GR(nv4, NV4_PGRAPH_BPITCH + 2 * 4)   = pitch;
    NV4_GR(nv4, NV4_PGRAPH_BLIMIT + 2 * 4)   = 0xffff;
    NV4_GR(nv4, NV4_PGRAPH_BBASE + 3 * 4)    = Z_BASE;
    NV4_GR(nv4, NV4_PGRAPH_BOFFSET + 3 * 4)  = 0;
    NV4_GR(nv4, NV4_PGRAPH_BPITCH + 3 * 4)   = swz ? 0 : pitch;
    NV4_GR(nv4, NV4_PGRAPH_BLIMIT + 3 * 4)   = zeta ? 0xffff : 0x80000000;
    NV4_GR(nv4, NV4_PGRAPH_UCLIP_MIN)        = 0;
    NV4_GR(nv4, NV4_PGRAPH_UCLIP_MIN + 4)    = 0;
    NV4_GR(nv4, NV4_PGRAPH_UCLIP_MAX)        = 0x1000;
    NV4_GR(nv4, NV4_PGRAPH_UCLIP_MAX + 4)    = 0x1000;
    /* as a 0x53 method would: the next triangle derives a new state */
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

/* DX5 state: untextured, no blending, no culling, depth test LESS with
   writes when <zbuf>. */
static void
dx5_state(int zbuf)
{
    mthd(0x308, 0); /* no texture */
    mthd(0x310, 0); /* blend: none */
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

/* A rectangle x0..x1, y0..y1 (pixel edges) as two triangles. */
static void
rect(float x0, float y0, float x1, float y1, float z, uint32_t color)
{
    vertex(0, x0, y0, z, color);
    vertex(1, x1, y0, z, color);
    vertex(2, x1, y1, z, color);
    vertex(3, x0, y1, z, color);
    mthd(0x600, 0x320210); /* triangles 0,1,2 and 0,2,3 */
}

/* Texel x,y of a 2^wl by 2^hl swizzled surface: bit i of x, then bit i
   of y, while each lasts. */
static uint32_t
swz_ref(int x, int y, int wl, int hl)
{
    uint32_t r = 0;
    int      n = 0;

    for (int i = 0; i < 12; i++) {
        if (i < wl)
            r |= (uint32_t) ((x >> i) & 1) << n++;
        if (i < hl)
            r |= (uint32_t) ((y >> i) & 1) << n++;
    }
    return r;
}

static uint32_t
rd32(uint32_t a)
{
    uint32_t v;

    memcpy(&v, nv4->svga.vram + a, 4);
    return v;
}

/* Every pixel of a swizzled target holds <in> inside x0..x1-1,
   y0..y1-1 and 0 outside it; nothing past the surface is written. */
static void
check_swz(int wl, int hl, int x0, int y0, int x1, int y1, uint32_t in)
{
    int w = 1 << wl, h = 1 << hl;
    int bad = 0;

    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t want = ((x >= x0) && (x < x1) && (y >= y0) && (y < y1)) ? in : 0;

            if (rd32(COL_BASE + swz_ref(x, y, wl, hl) * 4) != want) {
                if (bad++ < 4)
                    fprintf(stderr, "  pixel %d,%d: %08x, want %08x\n", x, y,
                            rd32(COL_BASE + swz_ref(x, y, wl, hl) * 4), want);
            }
        }
    CHECK(bad == 0);
    for (uint32_t a = COL_BASE + (uint32_t) w * h * 4; a < COL_BASE + 0x4000; a++)
        if (nv4->svga.vram[a]) {
            fprintf(stderr, "  byte past the surface at %06x\n", a);
            failures++;
            break;
        }
}

/* A square 8x8 target: a 4x2 block lands at the swizzled offsets. */
static void
test_square(void)
{
    card_new();
    surfaces(1, 3, 3, 0, 0);
    dx5_state(0);
    rect(0, 0, 4, 2, 0.5f, 0xff112233);
    nv4_3d_flush(nv4);
    check_swz(3, 3, 0, 0, 4, 2, 0xff112233);
    /* pixel 1,0 is texel 1, pixel 0,1 texel 2: not the pitch layout */
    CHECK(rd32(COL_BASE + 1 * 4) == 0xff112233);
    CHECK(rd32(COL_BASE + 2 * 4) == 0xff112233);
    CHECK(rd32(COL_BASE + 4 * 4) == 0xff112233);  /* texel 4 = pixel 2,0 */
    CHECK(rd32(COL_BASE + 16 * 4) == 0);          /* texel 16 = pixel 4,0: outside */
    card_free();
}

/* A 16x4 target (u runs on alone past v's two bits), drawn past its
   edges: the surface's size clips it. */
static void
test_wide_clipped(void)
{
    card_new();
    surfaces(1, 4, 2, 0, 0);
    dx5_state(0);
    rect(2, 1, 40, 30, 0.5f, 0xff445566);
    nv4_3d_flush(nv4);
    check_swz(4, 2, 2, 1, 16, 4, 0xff445566);
    card_free();
}

/* Depth on a swizzled target follows the same layout: a far rectangle
   behind a near one loses where they meet. */
static void
test_depth(void)
{
    int bad = 0;

    card_new();
    surfaces(1, 3, 3, 0, 1);
    /* clear depth to the far plane */
    for (int i = 0; i < 64; i++) {
        uint32_t far = 0x00ffffff;

        memcpy(nv4->svga.vram + Z_BASE + i * 4, &far, 4);
    }
    dx5_state(1);
    rect(0, 0, 8, 4, 0.25f, 0xff0000ff); /* near, top half */
    rect(0, 0, 8, 8, 0.75f, 0xffff0000); /* far, everywhere */
    nv4_3d_flush(nv4);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            uint32_t o   = swz_ref(x, y, 3, 3) * 4;
            uint32_t col = (y < 4) ? 0xff0000ff : 0xffff0000;
            uint32_t z   = (uint32_t) (((y < 4) ? 0.25f : 0.75f) * 16777215.0f);

            if ((rd32(COL_BASE + o) != col) || ((rd32(Z_BASE + o) & 0xffffff) != z))
                bad++;
        }
    CHECK(bad == 0);
    card_free();
}

/* The pitch layout is untouched: rows at base + y * pitch. */
static void
test_pitch(void)
{
    int bad = 0;

    card_new();
    surfaces(0, 0, 0, 64, 0);
    dx5_state(0);
    rect(0, 0, 4, 2, 0.5f, 0xff778899);
    nv4_3d_flush(nv4);
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 16; x++)
            if (rd32(COL_BASE + y * 64 + x * 4) != (((x < 4) && (y < 2)) ? 0xff778899 : 0))
                bad++;
    CHECK(bad == 0);
    card_free();
}

/* With rows shaded apart (as with threads or the GPU), a swizzled state
   takes no row regions and makes its batch serial, and the batch is cut
   where the target switches between the two layouts. */
static void
test_batch_split(void)
{
    nv4_3d_batch_t *b;

    card_new();
    b           = nv4->batch3d;
    b->parallel = 1; /* one thread, no GPU: still shaded inline */

    surfaces(0, 0, 0, 64, 0);
    dx5_state(0);
    rect(0, 0, 4, 2, 0.5f, 0xff010101);
    CHECK(b->nregions == 1);
    CHECK(!b->serial);
    b->st_flushes = 0;

    /* the switch to a swizzled target without a 0x53 method's flush:
       only the state rule may cut the batch */
    NV4_GR(nv4, NV4_PGRAPH_SURFACE)   = 2;
    NV4_GR(nv4, NV4_PGRAPH_BSWIZZLE2) = (3u << 24) | (3u << 16);
    NV4_GR(nv4, NV4_PGRAPH_BBASE + 2 * 4) = COL_BASE + 0x8000;
    dx5_state(0);
    rect(0, 0, 8, 8, 0.5f, 0xff020202);
    CHECK(b->st_flushes == 1);
    CHECK(b->serial);
    CHECK(b->nregions == 0);
    CHECK(b->nstates == 1);

    NV4_GR(nv4, NV4_PGRAPH_SURFACE)       = 1;
    NV4_GR(nv4, NV4_PGRAPH_BBASE + 2 * 4) = COL_BASE;
    dx5_state(0);
    rect(0, 2, 4, 4, 0.5f, 0xff030303);
    CHECK(b->st_flushes == 2);
    CHECK(!b->serial);
    nv4_3d_flush(nv4);

    CHECK(rd32(COL_BASE + 0 * 64) == 0xff010101);
    CHECK(rd32(COL_BASE + 3 * 64) == 0xff030303);
    CHECK(rd32(COL_BASE + 0x8000 + 63 * 4) == 0xff020202);
    card_free();
}

int
main(void)
{
    test_square();
    test_wide_clipped();
    test_depth();
    test_pitch();
    test_batch_split();
    if (failures)
        fprintf(stderr, "%d failure(s)\n", failures);
    else
        printf("nv4 3D swizzle tests passed\n");
    return failures != 0;
}
