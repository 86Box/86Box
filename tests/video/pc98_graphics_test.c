/* Capture-driven regressions for the standalone PC-98 graphics cores.
 * These exercise host commands/VRAM accesses, including their ordering.
 * They do not substitute for rerunning the DOS suite or physical hardware. */
#include <86box/vid_pc98.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned failures, checks;
#define CHECK(expr)                                                    \
    do {                                                               \
        checks++;                                                      \
        if (!(expr)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #expr); \
            failures++;                                                \
            return;                                                    \
        }                                                              \
    } while (0)

typedef struct fixture_t {
    upd7220_t g;
    uint16_t  ram[0x40000];
    unsigned  writes;
    int       drq;
} fixture_t;

static fixture_t    f;
static pc98_video_t v;
static uint16_t
rd(void *p, uint32_t a)
{
    return ((fixture_t *) p)->ram[a];
}
static void
wr(void *p, uint32_t a, uint16_t x)
{
    fixture_t *t = p;
    t->ram[a]    = x;
    t->writes++;
}
static void
drq(void *p, int level)
{
    ((fixture_t *) p)->drq = level;
}
static void
init_gdc(void)
{
    memset(&f, 0, sizeof(f));
    upd7220_init(&f.g, &f, rd, wr);
    f.g.drq_changed = drq;
    f.g.mode        = 2;
    f.g.pitch       = 40;
    f.g.ra[8] = f.g.ra[9] = 0xff;
    f.g.pattern           = 0xffff;
}
static void
settle(upd7220_t *g)
{
    upd7220_advance(g, 1000000);
}
static void
cmd(upd7220_t *g, uint8_t op, const uint8_t *p, unsigned n)
{
    upd7220_write(g, 1, op);
    for (unsigned i = 0; i < n; i++) {
        if (upd7220_read(g, 0) & UPD7220_SR_FULL)
            settle(g);
        upd7220_write(g, 0, p[i]);
    }
    settle(g);
}
static void
figs(upd7220_t *g, unsigned type, unsigned dir, unsigned dc,
     unsigned d, unsigned d2, unsigned d1, unsigned dm)
{
    uint8_t p[] = { (uint8_t) ((type << 3) | dir), (uint8_t) dc, (uint8_t) (dc >> 8),
                    (uint8_t) d, (uint8_t) (d >> 8), (uint8_t) d2, (uint8_t) (d2 >> 8),
                    (uint8_t) d1, (uint8_t) (d1 >> 8), (uint8_t) dm, (uint8_t) (dm >> 8) };
    cmd(g, 0x4c, p, sizeof(p));
}

static void
partial_figs(unsigned same_p1, unsigned fields)
{
    init_gdc();
    figs(&f.g, 1, 2, 0x309, 71, 43, 7, 5);
    f.g.pattern = 0xa55a; /* FIGS must not reload this latch from PRAM. */
    uint8_t p[] = { (uint8_t) ((same_p1 ? 1 : 0) << 3 | 2), 6, 0 };
    cmd(&f.g, 0x4c, p, fields ? 3 : 1);
    CHECK(f.g.figs.dc == (fields ? 6 : 0));
    CHECK(f.g.figs.d == 8 && f.g.figs.d2 == 8);
    CHECK(f.g.figs.d1 == 0xffff && f.g.figs.dm == 0xffff);
    CHECK(f.g.figs.gd == 0 && f.g.pattern == 0xa55a);
    cmd(&f.g, 0x6c, NULL, 0);
    CHECK(f.writes == (fields ? 7 : 1));
}

static void
egc_setup(void)
{
    pc98_video_init(&v);
    pc98_video_out8(&v, 0x7c, 0x80);
    pc98_video_out8(&v, 0x6a, 7);
    pc98_video_out8(&v, 0x6a, 5);
    pc98_video_out8(&v, 0x6a, 6);
    pc98_video_out16(&v, 0x4a0, 0xfff0);
    pc98_video_out16(&v, 0x4a2, 0x00ff);
    pc98_video_out16(&v, 0x4a8, 0xffff);
    pc98_video_out16(&v, 0x4ac, 0);
    pc98_video_out16(&v, 0x4ae, 15);
}
static uint16_t
reverse_bytes(uint16_t x)
{
    x = (uint16_t) (((x & 0x5555) << 1) | ((x >> 1) & 0x5555));
    x = (uint16_t) (((x & 0x3333) << 2) | ((x >> 2) & 0x3333));
    return (uint16_t) (((x & 0x0f0f) << 4) | ((x >> 4) & 0x0f0f));
}
static const uint16_t old_d[4] = { 0xa55a, 0xc33c, 0xf00f, 0x6996 };
static void
seed_destination(unsigned off)
{
    for (unsigned plane = 0; plane < 4; plane++) {
        v.gvram[0][plane][off] = reverse_bytes(old_d[plane]);
        v.egc.pat[plane]       = reverse_bytes((uint16_t) (0x1357 + plane));
    }
}
static void
egc_capture(void)
{
    egc_setup();
    seed_destination(0);
    pc98_video_out16(&v, 0x4a4, 0x0200); /* Compare=0, PLOAD_W, direct CPU. */
    pc98_gvram_write16(&v, 0, 0, 0, 0xffff);
    for (unsigned i = 0; i < 4; i++)
        CHECK(v.egc.pat[i] == reverse_bytes(old_d[i]));
}
static void
egc_use_captured_pattern(void)
{
    egc_setup();
    seed_destination(0);
    pc98_video_out16(&v, 0x4a4, 0x0200);
    pc98_gvram_write16(&v, 0, 0, 0, 0xffff);
    pc98_video_out16(&v, 0x4a4, 0x1000); /* Direct P on a later write. */
    pc98_video_out16(&v, 0x4ae, 15);
    pc98_gvram_write16(&v, 0, 1, 0, 0xffff);
    for (unsigned i = 0; i < 4; i++)
        CHECK(v.gvram[0][i][1] == reverse_bytes(old_d[i]));
}
static void
egc_prewrite_rop(void)
{
    egc_setup();
    seed_destination(0);
    pc98_video_out16(&v, 0x4a4, 0x0af0); /* ROP S, PLOAD_W. */
    pc98_gvram_write16(&v, 0, 0, 0xffff, 0xffff);
    for (unsigned i = 0; i < 4; i++)
        CHECK(v.egc.pat[i] == reverse_bytes(old_d[i]));
}
static void
egc_read_and_byte_latches(void)
{
    egc_setup();
    seed_destination(0);
    uint16_t saved[4];
    memcpy(saved, v.egc.pat, sizeof(saved));
    pc98_video_out16(&v, 0x4a2, 0x02ff);
    pc98_video_out16(&v, 0x4a4, 0x0100);
    CHECK(pc98_gvram_read16(&v, 0, 0, 0xffff) == old_d[2]);
    for (unsigned i = 0; i < 4; i++)
        CHECK(v.egc.pat[i] == (i == 2 ? reverse_bytes(old_d[i]) : saved[i]));
    memcpy(saved, v.egc.pat, sizeof(saved));
    pc98_video_out16(&v, 0x4a0, 0xfff4); /* Inhibit green. */
    pc98_video_out16(&v, 0x4a4, 0x0200);
    pc98_gvram_write8(&v, 0, 0, 0);
    for (unsigned i = 0; i < 4; i++)
        CHECK(v.egc.pat[i] == (i == 2 ? saved[i] : (uint16_t) ((saved[i] & 0xff00) | (reverse_bytes(old_d[i]) & 0xff))));
}
static void
rop_truth_table(void)
{
    /* All 256 Boolean functions, with all eight input tuples in parallel. */
    for (unsigned truth = 0; truth < 256; truth++)
        CHECK(pc98_egc_rop((uint8_t) truth, 0xf0f0, 0xaaaa, 0xcccc) == (uint16_t) (truth | (truth << 8)));
}

static void
clocked_drawing_and_fifo(void)
{
    init_gdc();
    uint8_t p = 40;
    upd7220_write(&f.g, 1, 0x47);
    upd7220_write(&f.g, 0, p);
    CHECK(f.g.fifo_count == 1 && !(upd7220_read(&f.g, 0) & UPD7220_SR_DRAW));
    CHECK(upd7220_read(&f.g, 1) == 0xff); /* Does not erase input. */
    upd7220_advance(&f.g, 11);
    CHECK(f.g.pitch == 40 && f.g.fifo_count == 0);
    upd7220_advance(&f.g, 1);
    f.g.mask = 1;
    figs(&f.g, 0, 2, 7, 8, 8, 0x3fff, 0x3fff);
    upd7220_write(&f.g, 1, 0x6c);
    CHECK(f.writes == 0);
    upd7220_advance(&f.g, 18);
    CHECK((upd7220_read(&f.g, 0) & UPD7220_SR_DRAW) && f.writes == 0);
    upd7220_advance(&f.g, 4);
    CHECK(f.writes == 1 && f.ram[0] == 1 && f.g.mask == 2);
    for (unsigned i = 0; i < 16; i++)
        upd7220_write(&f.g, 1, 0x46);
    CHECK(f.g.fifo_count == 16 && (upd7220_read(&f.g, 0) & UPD7220_SR_FULL));
    upd7220_write(&f.g, 1, 0x47); /* Overflow cannot bypass the engine. */
    CHECK(f.g.fifo_count == 16);
    CHECK(f.g.fifo[(f.g.fifo_tail + 15) & 15] == 0x47);
    upd7220_advance(&f.g, 27);
    CHECK(f.writes == 7 && (upd7220_read(&f.g, 0) & UPD7220_SR_DRAW));
    upd7220_advance(&f.g, 1);
    CHECK(f.writes == 8 && !(upd7220_read(&f.g, 0) & UPD7220_SR_DRAW));
    CHECK(f.g.fifo_count == 15 && !(upd7220_read(&f.g, 0) & UPD7220_SR_FULL));
    settle(&f.g);
    CHECK(f.g.fifo_count == 0 && f.g.pitch == 40);
}
static void
work_scaling_and_wdat(void)
{
    init_gdc();
    figs(&f.g, 0, 2, 63, 8, 8, 0x3fff, 0x3fff);
    uint8_t curs[] = { 0, 0, 8 }; /* 7220A raw bitmap write gate. */
    cmd(&f.g, 0x49, curs, 3);
    uint8_t mask[] = { 0xff, 0xff };
    cmd(&f.g, 0x4a, mask, 2);
    uint8_t word[] = { 0x5a, 0xa5 };
    upd7220_write(&f.g, 1, 0x20);
    upd7220_write(&f.g, 0, word[0]);
    upd7220_write(&f.g, 0, word[1]);
    upd7220_advance(&f.g, 18 + 4 * 63);
    CHECK(f.writes == 63 && (f.g.sr & UPD7220_SR_DRAW));
    upd7220_advance(&f.g, 4);
    CHECK(f.writes == 64 && !(f.g.sr & UPD7220_SR_DRAW));
    CHECK(f.ram[0] == 0xa55a && f.ram[63] == 0xa55a && f.g.ead == 64);
    CHECK(f.g.figs.dc == 0);
    upd7220_write(&f.g, 0, 0xaa);
    upd7220_write(&f.g, 0, 0x55);
    settle(&f.g);
    CHECK(f.writes == 65 && f.ram[64] == 0x55aa);
}
static void
reset_recovers_full_fifo(void)
{
    init_gdc();
    figs(&f.g, 0, 2, 999, 8, 8, 0x3fff, 0x3fff);
    upd7220_write(&f.g, 1, 0x6c);
    upd7220_advance(&f.g, 22);
    for (unsigned i = 0; i < 16; i++)
        upd7220_write(&f.g, 1, 0x46);
    upd7220_write(&f.g, 1, 0);
    CHECK(f.g.fifo_count == 0 && !(f.g.sr & (UPD7220_SR_DRAW | UPD7220_SR_DMA)));
    settle(&f.g);
    CHECK(f.writes == 1 && f.g.ead == 0 && f.g.mask == 0);
}
static void
rdat_refill_and_abort(void)
{
    for (unsigned type = 0; type < 4; type++) {
        init_gdc();
        for (unsigned i = 0; i < 32; i++)
            f.ram[i] = (uint16_t) (0xb000 + i);
        figs(&f.g, 0, 2, 19, 8, 8, 0x3fff, 0x3fff);
        cmd(&f.g, (uint8_t) (0xa0 | (type << 3)), NULL, 0);
        CHECK(f.g.fifo_count == 16 && (f.g.sr & UPD7220_SR_DRDY));
        if (type == 0) {
            CHECK(upd7220_read(&f.g, 1) == 0);
            upd7220_advance(&f.g, 100);
            CHECK(f.g.fifo_count == 15); /* Need two free bytes. */
            CHECK(upd7220_read(&f.g, 1) == 0xb0);
            upd7220_advance(&f.g, 3);
            CHECK(f.g.fifo_count == 14);
            upd7220_advance(&f.g, 1);
            CHECK(f.g.fifo_count == 16);
        }
        /* CURD replaces unread output; it must not inherit RDAT's count. */
        cmd(&f.g, 0xe0, NULL, 0);
        CHECK(f.g.transfer_words_left == 0 && f.g.fifo_count == 5);
        for (unsigned i = 0; i < 5; i++)
            (void) upd7220_read(&f.g, 1);
        settle(&f.g);
        CHECK(f.g.fifo_count == 0 && !(f.g.sr & UPD7220_SR_DRDY));
    }
}
static void
dma_word_addresses_and_reset(void)
{
    init_gdc();
    f.ram[0] = 0x1234;
    f.ram[1] = 0xabcd;
    figs(&f.g, 0, 2, 1, 8, 8, 0x3fff, 0x3fff);
    cmd(&f.g, 0xa4, NULL, 0);
    CHECK(f.drq == 1);
    CHECK(upd7220_dack_read(&f.g) == 0x34);
    CHECK(upd7220_dack_read(&f.g) == 0x12);
    CHECK(f.g.ead == 1 && f.g.mask == 0xffff);
    CHECK(upd7220_dack_read(&f.g) == 0xcd);
    CHECK(upd7220_dack_read(&f.g) == 0xab && f.drq == 0);
    cmd(&f.g, 0x24, NULL, 0);
    upd7220_write(&f.g, 1, 0);
    CHECK(f.drq == 0 && f.g.dma_bytes_left == 0);
    cmd(&f.g, 0x24, NULL, 0);
    CHECK(f.drq == 1);
    upd7220_reset(&f.g);
    CHECK(f.drq == 0 && f.g.sr == UPD7220_SR_EMPTY && f.g.mask == 0xffff);
}
static void
long_parameter_stream(void)
{
    init_gdc();
    cmd(&f.g, 0x78, NULL, 0);
    for (unsigned i = 0; i < 300; i++) {
        upd7220_write(&f.g, 0, (uint8_t) i);
        settle(&f.g);
    }
    CHECK(f.g.param_ptr == sizeof(f.g.pr));
    CHECK(f.g.ra[8] == 0 && f.g.ra[15] == 7);
    uint8_t p = 17;
    cmd(&f.g, 0x47, &p, 1);
    CHECK(f.g.pitch == 17);
}

static void
rectangle_character_and_empty_work(void)
{
    init_gdc();
    figs(&f.g, 8, 2, 0, 11, 9, 0x3fff, 0x3fff);
    upd7220_write(&f.g, 1, 0x6c);
    upd7220_advance(&f.g, 18 + 4 * 40 - 1);
    CHECK(f.writes == 39 && (f.g.sr & UPD7220_SR_DRAW));
    upd7220_advance(&f.g, 1);
    CHECK(f.writes == 40 && !(f.g.sr & UPD7220_SR_DRAW));
    figs(&f.g, 8, 2, 0, 0, 0, 0x3fff, 0x3fff);
    cmd(&f.g, 0x6c, NULL, 0);
    CHECK(f.writes == 40 && !(f.g.sr & UPD7220_SR_DRAW));

    uint8_t zoom = 2;
    cmd(&f.g, 0x46, &zoom, 1);
    figs(&f.g, 2, 2, 5, 8, 8, 0x3fff, 0x3fff);
    upd7220_write(&f.g, 1, 0x68);
    upd7220_advance(&f.g, 16 + 4 * 432 + 6 * 17 - 1);
    CHECK(f.writes == 471 && (f.g.sr & UPD7220_SR_DRAW));
    upd7220_advance(&f.g, 1);
    CHECK(f.writes == 472 && !(f.g.sr & UPD7220_SR_DRAW));

    /* Display zoom lengthens memory cycles separately from character zoom. */
    zoom = 0x20;
    cmd(&f.g, 0x46, &zoom, 1);
    figs(&f.g, 0, 2, 0, 8, 8, 0x3fff, 0x3fff);
    upd7220_write(&f.g, 1, 0x6c);
    upd7220_advance(&f.g, 23);
    CHECK(f.writes == 472 && (f.g.sr & UPD7220_SR_DRAW));
    upd7220_advance(&f.g, 1);
    CHECK(f.writes == 473 && !(f.g.sr & UPD7220_SR_DRAW));
}

static void
bus_fixture(unsigned mode, unsigned line, unsigned h, unsigned zoom)
{
    init_gdc();
    f.g.aw   = 40;
    f.g.hfp  = 4;
    f.g.hs   = 4;
    f.g.hbp  = 5;
    f.g.vbp  = 25;
    f.g.al   = 400;
    f.g.vfp  = 7;
    f.g.vs   = 8;
    f.g.mode = (uint8_t) mode;
    f.g.de   = 1;
    f.g.disp = (uint8_t) zoom;
    upd7220_enable_bus_timing(&f.g, 1);
    upd7220_set_bus_position(&f.g, h, (uint16_t) line);
}

/* An independent edge-by-edge oracle: each clock interval must be free,
 * with refresh taking precedence and flashless display owning AW. */
static int
reference_free(unsigned tick, unsigned mode, unsigned de)
{
    unsigned h = tick % 106, line = (tick / 106) % 440;
    if ((mode & 4) && h >= 88 && h < 96)
        return 0;
    return !(de && (mode & 16) && h < 80 && line >= 25 && line < 425);
}

static void
display_bus_arbitration(void)
{
    const unsigned modes[]  = { 2, 6, 18, 22 };
    const unsigned lines[]  = { 0, 24, 25, 424, 425, 439 };
    const unsigned starts[] = { 0, 1, 59, 61, 62, 63, 64, 65, 69, 77, 87, 105 };
    const unsigned zooms[]  = { 0, 1, 2, 15 };
    for (unsigned m = 0; m < 4; m++)
        for (unsigned l = 0; l < 6; l++)
            for (unsigned s = 0; s < 12; s++)
                for (unsigned z = 0; z < 4; z++)
                    for (unsigned de = 0; de < 2; de++) {
                        bus_fixture(modes[m], lines[l], starts[s], zooms[z]);
                        f.g.de           = (uint8_t) de;
                        unsigned initial = lines[l] * 106 + starts[s];
                        unsigned cost    = zooms[z] > 1 ? 2 * (zooms[z] + 1) : 4;
                        unsigned grant   = initial + 18; /* FIGD preparation. */
                        for (;; grant++) {
                            if (grant & 1)
                                continue;
                            unsigned free;
                            for (free = 0; free < cost; free++)
                                if (!reference_free(grant + free, modes[m], de))
                                    break;
                            if (free == cost)
                                break;
                        }
                        upd7220_write(&f.g, 1, 0x6c);
                        upd7220_advance(&f.g, grant - initial + cost - 1);
                        CHECK(f.writes == 0 && (f.g.sr & UPD7220_SR_DRAW));
                        upd7220_advance(&f.g, 1);
                        CHECK(f.writes == 1 && !(f.g.sr & UPD7220_SR_DRAW));
                        CHECK(f.g.bus_hclock == (grant + cost) % 106);
                        CHECK(f.g.bus_line == ((grant + cost) / 106) % 440);
                    }
}

static void
bus_chunk_invariance_and_reset(void)
{
    static fixture_t other;
    bus_fixture(22, 424, 17, 0);
    f.g.figs.dc = 999;
    upd7220_write(&f.g, 1, 0x6c);
    other          = f;
    other.g.opaque = &other;
    upd7220_advance(&f.g, 9000);
    for (unsigned i = 0; i < 9000; i++)
        upd7220_advance(&other.g, 1);
    other.g.opaque = &f;
    CHECK(!memcmp(&f, &other, sizeof(f)));
    CHECK(f.writes == 1000);
    upd7220_advance(&f.g, 440u * 106u + 13u);
    CHECK(f.g.bus_hclock == (other.g.bus_hclock + 13u) % 106u);
    bus_fixture(22, 25, 0, 0);
    upd7220_write(&f.g, 1, 0x6c);
    upd7220_advance(&f.g, 18);
    CHECK(f.g.bus_waiting && !f.writes);
    upd7220_write(&f.g, 1, 0);
    upd7220_advance(&f.g, 1000);
    CHECK(!f.writes && !f.g.bus_waiting && !f.g.row_waiting);
    upd7220_reset(&f.g);
    CHECK(f.g.bus_timing_enabled && !f.g.bus_hclock && !f.g.bus_line);
}

static void
character_row_preparation(void)
{
    init_gdc();
    figs(&f.g, 2, 2, 1, 8, 8, 0x3fff, 0x3fff);
    upd7220_write(&f.g, 1, 0x68);
    upd7220_write(&f.g, 1, 0x47); /* Parser waits through the row gap. */
    upd7220_write(&f.g, 0, 77);
    upd7220_advance(&f.g, 16 + 8 * 4);
    CHECK(f.writes == 8 && !(f.g.sr & UPD7220_SR_DRAW));
    CHECK(f.g.row_waiting && f.g.fifo_count == 2);
    upd7220_advance(&f.g, 5);
    CHECK(f.writes == 8 && !(f.g.sr & UPD7220_SR_DRAW));
    upd7220_advance(&f.g, 1);
    CHECK((f.g.sr & UPD7220_SR_DRAW) && f.writes == 8);
    upd7220_advance(&f.g, 8 * 4);
    CHECK(f.writes == 16 && !(f.g.sr & UPD7220_SR_DRAW));
    upd7220_advance(&f.g, 12);
    CHECK(f.g.pitch == 77);
}

static void
dma_blanking_windows(void)
{
    bus_fixture(22, 24, 61, 0);
    upd7220_write(&f.g, 1, 0xa4);
    upd7220_advance(&f.g, 14); /* Back porch AW: two-byte request. */
    CHECK(f.drq && upd7220_next_event(&f.g) == 5);
    upd7220_advance(&f.g, 5);
    CHECK(!f.drq && f.g.dma_bytes_left == 2);
    CHECK(upd7220_dack_read(&f.g) == 0xff && f.g.dma_bytes_left == 2);
    upd7220_advance(&f.g, 26);
    CHECK(!f.drq && f.g.bus_line == 25); /* Flashless AL blocked. */
    upd7220_set_bus_position(&f.g, 0, 0);
    CHECK(f.drq);
    upd7220_dack_read(&f.g);
    upd7220_dack_read(&f.g);
    CHECK(!f.drq && !(f.g.sr & UPD7220_SR_DMA));
    bus_fixture(6, 25, 0, 0);
    upd7220_write(&f.g, 1, 0x24);
    upd7220_advance(&f.g, 12);
    CHECK(f.drq); /* Flash mode permits active display. */
    upd7220_set_bus_position(&f.g, 0, 432);
    CHECK(!f.drq); /* VFP/VS does not request DMA. */
    upd7220_set_bus_position(&f.g, 0, 0);
    upd7220_dack_write(&f.g, 0xaa);
    upd7220_dack_write(&f.g, 0x55);
    CHECK(f.ram[0] == 0x55aa && !f.drq);
}

static void
rendering_characterization(void)
{
    /* Snapshots of the pre-refactor renderer with complete FIGS parameters.
     * 480 cases: five primitives, eight directions, four RMW modes, three
     * character zooms. These preserve existing geometry, not certify hardware.
     * Hashes include the whole VRAM, final EAD/mask/direction and write count. */
    const uint64_t expected[] = {
        UINT64_C(0xdb749d3f831dc543), UINT64_C(0x5ed936d4dfdfeee9),
        UINT64_C(0xddd9ca32d6efb672), UINT64_C(0x2e4d8ec247bc5cbe),
        UINT64_C(0x6f2d5287bc2a7be7)
    };
    const unsigned types[] = { 0, 1, 4, 8, 2 };
    const uint64_t prime   = UINT64_C(1099511628211);
    for (unsigned kind = 0; kind < 5; kind++) {
        uint64_t matrix = UINT64_C(14695981039346656037);
        for (unsigned dir = 0; dir < 8; dir++)
            for (unsigned mod = 0; mod < 4; mod++)
                for (unsigned zoom = 0; zoom < 3; zoom++) {
                    init_gdc();
                    for (unsigned a = 0; a < 0x40000; a++)
                        f.ram[a] = (uint16_t) (a * 317u + 0x36a5);
                    for (unsigned a = 8; a < 16; a++)
                        f.g.ra[a] = (uint8_t) (a * 37u + 11);
                    f.g.pattern = (uint16_t) (f.g.ra[8] | ((uint16_t) f.g.ra[9] << 8));
                    f.g.ead     = 0x21000;
                    f.g.mask    = 0x0080;
                    uint8_t z   = (uint8_t) zoom;
                    cmd(&f.g, 0x46, &z, 1);
                    cmd(&f.g, (uint8_t) (0x20 | mod), NULL, 0);
                    unsigned d = kind == 2 ? 23 : (kind == 3 ? 11 : 8);
                    figs(&f.g, types[kind], dir, kind == 4 ? 5 : 17, d, 9, 0x3ff9,
                         kind == 2 ? 3 : 0x3fff);
                    cmd(&f.g, kind == 4 ? 0x68 : 0x6c, NULL, 0);
                    uint64_t hash = UINT64_C(14695981039346656037);
                    for (unsigned a = 0; a < 0x40000; a++) {
                        hash = (hash ^ (f.ram[a] & 255)) * prime;
                        hash = (hash ^ (f.ram[a] >> 8)) * prime;
                    }
                    matrix = (matrix ^ hash) * prime;
                    matrix = (matrix ^ f.g.ead) * prime;
                    matrix = (matrix ^ f.g.mask) * prime;
                    matrix = (matrix ^ f.g.figs.dir) * prime;
                    matrix = (matrix ^ f.writes) * prime;
                }
        CHECK(matrix == expected[kind]);
    }
}

static void
external_memory_ownership(void)
{
    init_gdc();
    f.g.figs.dc = 2;
    upd7220_write(&f.g, 1, 0x6c);
    upd7220_advance(&f.g, 18);
    CHECK(upd7220_host_bus_begin(&f.g) == 4);
    upd7220_advance(&f.g, 3);
    CHECK(!f.writes);
    upd7220_advance(&f.g, 1);
    CHECK(f.writes == 1 && f.g.work_index == 1 && !f.g.busy_clocks);
    upd7220_advance(&f.g, 100);
    CHECK(f.writes == 1); /* CPU reservation blocks subsequent memory work. */
    upd7220_host_bus_end(&f.g);
    upd7220_advance(&f.g, 8);
    CHECK(f.writes == 3 && !f.g.work_kind);

    init_gdc();
    upd7220_write(&f.g, 1, 0x6c);
    CHECK(!upd7220_host_bus_begin(&f.g)); /* Command decode uses no VRAM. */
    upd7220_advance(&f.g, 100);
    CHECK(!f.writes && f.g.work_kind && !f.g.busy_clocks);
    upd7220_host_bus_end(&f.g);
    upd7220_advance(&f.g, 4);
    CHECK(f.writes == 1);

    init_gdc();
    f.g.aw   = 80;
    f.g.hfp  = 8;
    f.g.hs   = 8;
    f.g.hbp  = 10;
    f.g.vbp  = 25;
    f.g.al   = 400;
    f.g.vfp  = 7;
    f.g.vs   = 8;
    f.g.mode = 0x16;
    f.g.de   = 1;
    upd7220_enable_bus_timing(&f.g, 1);
    upd7220_set_bus_position(&f.g, 0, 25);
    upd7220_write(&f.g, 1, 0x6c);
    upd7220_advance(&f.g, 18);
    CHECK(f.g.bus_waiting && !upd7220_host_bus_begin(&f.g));
    CHECK(!f.g.busy_clocks && !f.g.bus_waiting);
    CHECK(upd7220_host_bus_wait(&f.g, 2) == 0); /* CPU glue ignores flashless. */
    upd7220_set_bus_position(&f.g, 176, 25);
    CHECK(upd7220_host_bus_wait(&f.g, 2) == 16);
    upd7220_set_bus_position(&f.g, 174, 25);
    CHECK(upd7220_host_bus_wait(&f.g, 4) == 18); /* No RMW across refresh. */
    f.g.de = 0;
    upd7220_set_bus_position(&f.g, 176, 0);
    CHECK(upd7220_host_bus_wait(&f.g, 2) == 16);
    f.g.mode &= ~4u;
    CHECK(upd7220_host_bus_wait(&f.g, 2) == 0);
    upd7220_reset(&f.g);
    CHECK(!f.g.host_bus_requested); /* Reset cannot strand bus ownership. */
}

int
main(void)
{
    partial_figs(0, 0);
    partial_figs(0, 1);
    partial_figs(1, 0);
    partial_figs(1, 1);
    egc_capture();
    egc_use_captured_pattern();
    egc_prewrite_rop();
    egc_read_and_byte_latches();
    rop_truth_table();
    clocked_drawing_and_fifo();
    work_scaling_and_wdat();
    reset_recovers_full_fifo();
    rdat_refill_and_abort();
    dma_word_addresses_and_reset();
    long_parameter_stream();
    rectangle_character_and_empty_work();
    rendering_characterization();
    display_bus_arbitration();
    bus_chunk_invariance_and_reset();
    character_row_preparation();
    dma_blanking_windows();
    external_memory_ownership();
    printf("PC-98 graphics: %u checks, %u failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
