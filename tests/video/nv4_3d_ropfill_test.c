/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Tests of the RIVA TNT's 2D ROP fills joining the 3D batch
 *          (nv4_3d_fill_rop): each pixel must be the ROP3 of the
 *          pattern, the source and the destination, worked out here
 *          from the ROP's truth table, in 8-, 16- and 32-bit surfaces,
 *          without a pattern and with mono patterns of every shape and
 *          a transparent color.
 *
 *          The 3D code is included as C, as in nv4_3d_swizzle_test.c,
 *          whose stand-ins for the rest of the card this repeats.
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
/* ------------------------------------------------------------------ */
static nv4_t    *nv4;
static monitor_t mon;

static void
card_new(void)
{
    nv4                      = calloc(1, sizeof(nv4_t));
    nv4->vram_size           = VRAM_SIZE;
    nv4->vram_mask           = VRAM_SIZE - 1;
    nv4->svga.vram           = calloc(1, VRAM_SIZE);
    nv4->svga.changedvram    = calloc(1, VRAM_SIZE >> 12);
    nv4->svga.monitor        = &mon;
    nv4->render_threads      = 1;
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

/* ROP3 from its truth table, bit by bit: bit (p << 2 | s << 1 | d) of
   the code */
static uint32_t
rop_ref(uint8_t rop, uint32_t p, uint32_t s, uint32_t d)
{
    uint32_t r = 0;

    for (int b = 0; b < 32; b++) {
        int i = (((p >> b) & 1) << 2) | (((s >> b) & 1) << 1) | ((d >> b) & 1);

        r |= (uint32_t) ((rop >> i) & 1) << b;
    }
    return r;
}

static uint32_t
rd(uint32_t a, int bpp)
{
    uint32_t v = 0;

    for (int i = 0; i < bpp; i++)
        v |= (uint32_t) nv4->svga.vram[a + i] << (i * 8);
    return v;
}

/* The destination before the fill: different in every byte */
static uint32_t
dst0(int x, int y)
{
    return (uint32_t) (x * 0x9e3779b1u) ^ (uint32_t) (y * 0x85ebca77u) ^ 0x5a5a1234u;
}

/* One fill of a 20x12 rectangle at 5,3 into a <bpp> surface of pitch
   256 under <rop>, source <s>, pattern colors <pc> (none when !pat;
   <shape>, transparent colors <skip>): every pixel against rop_ref, and
   the bytes around it untouched. The batch holds a span first, so the
   fill joins it (no threads, no GPU: nv4_3d_joins needs only spans). */
static void
fill_case(int bpp, uint8_t rop, uint32_t s, int pat, const uint32_t pc[2], const uint32_t mono[2], int shape, int skip)
{
    const uint32_t mask = (bpp == 4) ? 0xffffffffu : ((1u << (bpp * 8)) - 1);
    const int      x0 = 5, y0 = 3, x1 = 25, y1 = 15;
    uint32_t       r[7];
    int            bad = 0;

    card_new();
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 256 / bpp; x++) {
            uint32_t v = dst0(x, y);

            for (int i = 0; i < bpp; i++)
                nv4->svga.vram[COL_BASE + y * 256 + x * bpp + i] = v >> (i * 8);
        }
    /* a fill state with an empty span: the batch holds spans */
    {
        nv4_3d_batch_t *b = nv4->batch3d;
        nv4_3d_state_t  st;

        memset(&st, 0, sizeof(st));
        st.fill      = 1;
        st.col_base  = COL_BASE;
        st.col_pitch = 256;
        st.col_lim   = COL_BASE + 0xffff;
        st.bpp       = bpp;
        b->states[0] = st;
        b->nstates   = 1;
        memset(&b->tris[0], 0, sizeof(b->tris[0]));
        b->ntris          = 1;
        b->spans[0].y     = 40; /* past the rows checked */
        b->spans[0].x0    = 0;
        b->spans[0].x1    = 0;
        b->spans[0].tri   = 0;
        b->nspans         = 1;
    }
    for (int i = 0; i < 2; i++) {
        uint32_t p = pat ? pc[i] : 0;

        r[i * 2]     = rop_ref(rop, p, s, 0xffffffffu);
        r[i * 2 + 1] = rop_ref(rop, p, s, 0);
    }
    r[4] = mono[0];
    r[5] = mono[1];
    r[6] = pat ? (NV4_ROPFILL_PAT | (uint32_t) shape | ((skip & 1) ? NV4_ROPFILL_SKIP0 : 0) | ((skip & 2) ? NV4_ROPFILL_SKIP1 : 0)) : 0;
    CHECK(nv4_3d_fill_rop(nv4, COL_BASE, 256, COL_BASE + 0xffff, bpp, x0, y0, x1, y1, r));
    nv4_3d_flush(nv4);

    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 256 / bpp; x++) {
            uint32_t d    = dst0(x, y) & mask;
            uint32_t want = d;

            if ((x >= x0) && (x < x1) && (y >= y0) && (y < y1)) {
                int k = 0;

                if (pat) {
                    int i = (shape == 1) ? (x & 63) : ((shape == 2) ? (y & 63) : (((y & 7) << 3) | (x & 7)));

                    k = (mono[i >> 5] >> (i & 31)) & 1;
                }
                if (!(pat && (skip & (1 << k))))
                    want = rop_ref(rop, pat ? pc[k] : 0, s, d) & mask;
            }
            if (rd(COL_BASE + y * 256 + x * bpp, bpp) != want) {
                if (bad++ < 4)
                    fprintf(stderr, "  rop %02x bpp %d pixel %d,%d: %08x, want %08x\n", rop, bpp, x, y,
                            rd(COL_BASE + y * 256 + x * bpp, bpp), want);
            }
        }
    CHECK(bad == 0);
    card_free();
}

static void
test_ropfill(void)
{
    static const uint8_t  rops[] = { 0x55, 0x5a, 0x66, 0xa0, 0x96, 0x0f, 0xf0, 0xbb, 0x00, 0xff, 0x1d };
    static const uint32_t pc[2]  = { 0x00ff8040, 0xc0102030 };
    static const uint32_t mono[2] = { 0x8142a5c3, 0x3cf00f96 };

    for (unsigned i = 0; i < sizeof(rops); i++)
        for (int bpp = 1; bpp <= 4; bpp *= 2) {
            fill_case(bpp, rops[i], 0x12345678, 0, pc, mono, 0, 0);
            for (int shape = 0; shape < 3; shape++)
                fill_case(bpp, rops[i], 0x89abcdef, 1, pc, mono, shape, 0);
            fill_case(bpp, rops[i], 0x0badf00d, 1, pc, mono, 0, 1);
            fill_case(bpp, rops[i], 0x0badf00d, 1, pc, mono, 0, 2);
        }
}

int
main(void)
{
    test_ropfill();
    if (failures)
        fprintf(stderr, "%d failure(s)\n", failures);
    else
        printf("nv4 3D ROP fill tests passed\n");
    return failures != 0;
}
