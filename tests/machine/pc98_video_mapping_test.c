/* Initialize the actual video wrapper with synthetic ROMs and inspect the
   registered bus handlers. Host presentation and clocks are passive adapters. */
#include "../../src/video/vid_pc98_86box.c"
#include <86box/pit.h>
#include "../../src/pic.c"

static unsigned checks, failures;
#define CHECK(expr)                                                    \
    do {                                                               \
        checks++;                                                      \
        if (!(expr)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
            failures++;                                                \
        }                                                              \
    } while (0)
uint64_t        tsc, TIMER_USEC = 1ULL << 32, timer_target;
int             is286;
static unsigned vram_stall_clocks;
void
cpu_bus_stall(int clocks)
{
    tsc += clocks;
    vram_stall_clocks += clocks;
}
monitor_t             monitors[MONITORS_NUM];
int                   monitor_index_global, frames;
static mem_mapping_t *maps[12];
static unsigned       map_count;
static int            vx_roms, bx4_roms, font_roms = 1;
static unsigned       vrtc_raises;
static int            timer_patch_option;
static unsigned       timer_patch_errors;
int
machine_get_config_int(char *name)
{
    return !strcmp(name, "skip_timer_checks") && timer_patch_option;
}
int
ui_msgbox_header(int flags, char *header, char *message)
{
    (void) header;
    (void) message;
    if (flags == MBX_ERROR)
        timer_patch_errors++;
    return 0;
}
FILE *
rom_fopen(const char *path, char *mode)
{
    (void) mode;
    size_t   size = 0;
    unsigned seed = 0;
    if (strstr(path, "pc9801/ipl.rom") && vx_roms) {
        size = 0x30000;
        seed = 3;
    } else if (strstr(path, "pc9801bx4/pc98BANK") && bx4_roms) {
        size = 0x8000;
        seed = 10 + (strstr(path, "pc98BANK")[8] - '0');
    } else if (font_roms && strstr(path, "/font.rom"))
        size = PC98_FONT_DUMP_SIZE;
    else if (font_roms && strstr(path, "24256c-x"))
        size = 0x8000;
    if (!size)
        return NULL;
    FILE *f = tmpfile();
    if (!f)
        abort();
    for (unsigned i = 0; i < size; i++)
        fputc((seed * 31 + (i >> 8) + i) & 255, f);
    rewind(f);
    return f;
}
int
rom_present(const char *p)
{
    FILE *f = rom_fopen(p, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}
void(pclog)(const char *fmt, ...) { (void) fmt; }
void
timer_enable(pc_timer_t *t)
{
    t->flags |= TIMER_ENABLED;
}
void
timer_disable(pc_timer_t *t)
{
    t->flags &= ~TIMER_ENABLED;
}
void
timer_add(pc_timer_t *t, void (*cb)(void *), void *p, int start)
{
    t->callback = cb;
    t->priv     = p;
    t->flags    = start ? TIMER_ENABLED : 0;
}
static uint8_t bios_gdc_clock;
uint8_t
mem_readb_phys(uint32_t a)
{
    if (a == 0x54d)
        return bios_gdc_clock;
    for (unsigned i = 0; i < map_count; i++)
        if (a >= maps[i]->base && a - maps[i]->base < maps[i]->size && maps[i]->read_b)
            return maps[i]->read_b(a, maps[i]->priv);
    return 0;
}
void
mem_writeb_phys(uint32_t a, uint8_t v)
{
    for (unsigned i = 0; i < map_count; i++)
        if (a >= maps[i]->base && a - maps[i]->base < maps[i]->size && maps[i]->write_b) {
            maps[i]->write_b(a, v, maps[i]->priv);
            return;
        }
}
static void
irq_notification(uint16_t mask, int set, void *priv)
{
    (void) priv;
    if (set && (mask & (1u << 2)))
        vrtc_raises++;
}
static void
test_update_pending(void)
{
}
int acpi_rtc_status;
void
smi_raise(void)
{
}
void
io_sethandler(uint16_t base, uint16_t size,
              uint8_t (*rb)(uint16_t, void *), uint16_t (*rw)(uint16_t, void *),
              uint32_t (*rl)(uint16_t, void *), void (*wb)(uint16_t, uint8_t, void *),
              void (*ww)(uint16_t, uint16_t, void *), void (*wl)(uint16_t, uint32_t, void *), void *p)
{
    (void) base;
    (void) size;
    (void) rb;
    (void) rw;
    (void) rl;
    (void) wb;
    (void) ww;
    (void) wl;
    (void) p;
}
void
mem_mapping_add(mem_mapping_t *m, uint32_t base, uint32_t size,
                uint8_t (*rb)(uint32_t, void *), uint16_t (*rw)(uint32_t, void *),
                uint32_t (*rl)(uint32_t, void *), void (*wb)(uint32_t, uint8_t, void *),
                void (*ww)(uint32_t, uint16_t, void *), void (*wl)(uint32_t, uint32_t, void *),
                uint8_t *exec, uint32_t flags, void *p)
{
    *m                = (mem_mapping_t) { .base = base, .size = size, .read_b = rb, .read_w = rw, .read_l = rl, .write_b = wb, .write_w = ww, .write_l = wl, .exec = exec, .flags = flags, .priv = p };
    maps[map_count++] = m;
}
void
video_inform_monitor(int type, const video_timings_t *p, int n)
{
    (void) type;
    (void) p;
    (void) n;
}
void
video_blit_memtoscreen_monitor(int x, int y, int w, int h, int n)
{
    (void) x;
    (void) y;
    (void) w;
    (void) h;
    (void) n;
}
void
video_wait_for_buffer_monitor(int n)
{
    (void) n;
}
void
video_force_resize_set_monitor(uint8_t v, int n)
{
    (void) v;
    (void) n;
}
uint8_t
video_force_resize_get_monitor(int n)
{
    (void) n;
    return 0;
}
void
set_screen_size(int x, int y)
{
    (void) x;
    (void) y;
}

static void
test_model(const device_t *dev, unsigned aliases, unsigned seed)
{
    map_count       = 0;
    pc98_86box_t *d = dev->init(dev);
    CHECK(d != NULL);
    if (!d)
        return;
    const unsigned initial_vrtc = vrtc_raises;
    pc98_begin_vsync(d);
    CHECK(vrtc_raises == initial_vrtc + 1); /* One request after power-on. */
    CHECK(pic.irr & (1u << 2));
    pic.interrupt = 2;
    pic_acknowledge(&pic);
    CHECK(!(pic.irr & (1u << 2)) && (pic.isr & (1u << 2)));
    pic_write(0, 0x20, &pic); /* EOI releases service but does not reset CRTV. */
    CHECK(!(pic.isr & (1u << 2)));
    for (unsigned frame = 0; frame < 10; frame++)
        pc98_begin_vsync(d);
    CHECK(vrtc_raises == initial_vrtc + 1); /* No reset at 64h: no repeats. */
    CHECK(!(pic.irr & (1u << 2)));
    pc9801_video_vrtc_reset(d);
    pc9801_video_vrtc_reset(d); /* Two writes before VSYNC request one frame. */
    CHECK(!(pic.irr & (1u << 2)) && vrtc_raises == initial_vrtc + 1);
    pc98_begin_vsync(d);
    CHECK(pic.irr & (1u << 2));
    CHECK(vrtc_raises == initial_vrtc + 2);
    pc98_begin_vsync(d);
    CHECK(vrtc_raises == initial_vrtc + 2);
    dev->reset(d);
    CHECK(!(pic.irr & (1u << 2)));
    pc98_begin_vsync(d);
    pc98_begin_vsync(d);
    CHECK(vrtc_raises == initial_vrtc + 3);
    CHECK(map_count == aliases + 6); /* TVRAM, CG and four graphics planes */
    const uint32_t base[] = { 0xe8000u, 0xfe8000u, 0xfffe8000u };
    for (unsigned n = 0; n < aliases; n++) {
        mem_mapping_t *m = maps[n];
        CHECK(m->base == base[n] && m->size == 0x18000);
        CHECK(m->write_b == NULL && m->write_w == NULL && m->write_l == NULL);
        uint32_t       reset     = base[n] + 0x17ff0u;
        const unsigned reset_off = aliases == 3 ? 0x7f : 0x17f;
        CHECK(m->read_b(reset, m->priv) == (uint8_t) (seed * 31 + reset_off + 0xf0));
        uint32_t expected = 0;
        for (unsigned b = 0; b < 4; b++)
            expected |= (uint32_t) (uint8_t) (seed * 31 + reset_off + 0xf0 + b) << (b * 8);
        CHECK(m->read_l(reset, m->priv) == expected);
        ipl_bank_out(0x43d, 2, d);
        CHECK(m->read_b(reset, m->priv) == (uint8_t) ((aliases == 3 ? 17 : seed) * 31 + (aliases == 3 ? 0x7f : 0x2ff) + 0xf0));
        ipl_bank_out(0x43d, 0xff, d); /* reserved writes do not change the bank */
        CHECK(d->ipl_bank == 1);
        dev->reset(d);
        CHECK(d->ipl_bank == 0);
        CHECK(m->read_b(reset, m->priv) == (uint8_t) (seed * 31 + reset_off + 0xf0));
        if (aliases == 3) {
            for (unsigned bank = 0; bank < 8; bank++) {
                pc98_flash_bank_out(0x43f, 0xe0 + bank * 2, d);
                CHECK(m->read_b(reset, m->priv) == (uint8_t) ((10 + bank) * 31 + 0x7f + 0xf0));
                CHECK(m->read_b(base[n] + 0x8000, m->priv) == (uint8_t) (0xff));
            }
            ipl_bank_out(0x43d, 2, d);
            CHECK(d->flash_bank == 0xff);
            CHECK(m->read_b(reset, m->priv) == (uint8_t) (17 * 31 + 0x7f + 0xf0));
            dev->reset(d);
        }
    }
    dev->close(d);
}
static void
text_partition(upd7220_t *g, unsigned part, unsigned sad, unsigned lines)
{
    unsigned p   = part * 4;
    g->ra[p]     = sad;
    g->ra[p + 1] = (sad >> 8) & 0x1f;
    g->ra[p + 2] = (lines & 15) << 4;
    g->ra[p + 3] = (lines >> 4) & 0x3f;
}

static void
test_text_partitions(void)
{
    pc98_86box_t *d = calloc(1, sizeof(*d));
    bitmap_t     *b = calloc(1, sizeof(*b));
    b->w            = 640;
    b->h            = 400;
    b->dat          = calloc(640 * 400, sizeof(*b->dat));
    for (unsigned y = 0; y < 400; y++)
        b->line[y] = b->dat + y * 640;
    monitors[0].target_buffer = b;
    pc98_video_t *v           = &d->core;
    v->font_rom               = calloc(1, PC98_FONT_ADDRESS_SIZE);
    v->video_ff[3]            = 1;
    for (unsigned i = 0; i < 0x1000; i++)
        v->tvram[i | 0x1000] = 0xe1;
    for (unsigned code = 1; code <= 8; code++)
        memset(v->font_rom + 0x800 + code * 16, 0x80 >> (code - 1), 16);
    upd7220_t *g = &v->text_gdc;
    g->de        = 1;
    g->lr        = 16;
    g->pitch     = 80;
    g->al        = 400;

    /* A 40/360 scanline split falls halfway through a 16-line row.
       Distinct glyphs also catch using screen rows to offset the second SAD. */
    text_partition(g, 0, 0x300, 40);
    text_partition(g, 1, 0x600, 360);
    v->tvram[0x300] = 1;
    v->tvram[0x350] = 2;
    v->tvram[0x3a0] = 3;
    v->tvram[0x600] = 5;
    v->tvram[0x650] = 6;
    pc98_render_text(d, g, 0, 400);
    CHECK(b->line[0][0] == 0xffffffffu);
    CHECK(b->line[16][1] == 0xffffffffu);
    CHECK(b->line[39][2] == 0xffffffffu);
    CHECK(b->line[40][4] == 0xffffffffu && b->line[40][2] == 0);
    CHECK(b->line[47][4] == 0xffffffffu);
    CHECK(b->line[48][5] == 0xffffffffu);

    /* Empty areas consume no raster lines, and a short area is clipped
       exactly even when LEN is not divisible by the character height. */
    memset(b->dat, 0, 640 * 400 * sizeof(*b->dat));
    text_partition(g, 1, 0x600, 23);
    text_partition(g, 2, 0x800, 0);
    text_partition(g, 3, 0x900, 17);
    v->tvram[0x900] = 7;
    v->tvram[0x950] = 8;
    pc98_render_text(d, g, 0, 400);
    CHECK(b->line[62][5] == 0xffffffffu);
    CHECK(b->line[63][6] == 0xffffffffu);
    CHECK(b->line[64][7] == 0xffffffffu);
    CHECK(b->line[79][7] == 0xffffffffu);
    CHECK(b->line[80][7] == 0);

    /* The row clock continues across the 40-line split. With an eight-line
       scroll, its carry selects the row after the second area's SAD. */
    memset(b->dat, 0, 640 * 400 * sizeof(*b->dat));
    memset(v->font_rom + 0x800 + 9 * 16, 0x80, 8);
    memset(v->font_rom + 0x800 + 9 * 16 + 8, 0x01, 8);
    v->tvram[0x300] = v->tvram[0x600] = 9;
    v->tvram[0x650]                   = 9;
    v->tvram[0x6a0]                   = 6;
    v->txt_scroll[3]                  = 8;
    v->txt_scroll[4]                  = 0; /* Whole screen scroll window. */
    v->txt_scroll[5]                  = 31;
    pc98_render_text(d, g, 0, 400);
    CHECK(b->line[0][7] == 0xffffffffu && b->line[0][0] == 0);
    CHECK(b->line[8][1] == 0xffffffffu);
    CHECK(b->line[40][0] == 0xffffffffu && b->line[40][7] == 0);
    CHECK(b->line[48][7] == 0xffffffffu && b->line[48][0] == 0);
    CHECK(b->line[56][5] == 0xffffffffu);

    /* Normal one-area text still reaches the bottom row and wraps TVRAM. */
    memset(b->dat, 0, 640 * 400 * sizeof(*b->dat));
    memset(g->ra, 0, sizeof(g->ra));
    memset(v->txt_scroll, 0, sizeof(v->txt_scroll));
    text_partition(g, 0, 0xff0, 400);
    v->tvram[0xff0]                     = 1;
    v->tvram[0x040]                     = 2;
    v->tvram[(0xff0 + 24 * 80) & 0xfff] = 3;
    pc98_render_text(d, g, 0, 400);
    CHECK(b->line[0][0] == 0xffffffffu);
    CHECK(b->line[16][1] == 0xffffffffu);
    CHECK(b->line[399][2] == 0xffffffffu);
    memset(b->dat, 0, 640 * 400 * sizeof(*b->dat));
    pc98_render_text(d, g, 17, 18);
    CHECK(b->line[16][1] == 0 && b->line[17][1] == 0xffffffffu && b->line[18][1] == 0);
    free(v->font_rom);
    free(b->dat);
    free(b);
    free(d);
    monitors[0].target_buffer = NULL;
}

static void
test_raster_page_switch(void)
{
    pc98_86box_t *d = calloc(1, sizeof(*d));
    bitmap_t     *b = calloc(1, sizeof(*b));
    b->w            = 640;
    b->h            = 400;
    b->dat          = calloc(640 * 400, sizeof(*b->dat));
    for (unsigned y = 0; y < 400; y++)
        b->line[y] = b->dat + y * 640;
    monitors[0].target_buffer = b;
    pc98_video_t *v           = &d->core;
    upd7220_t    *g           = &v->graph_gdc;
    v->video_ff[7]            = 1;
    g->mode                   = 2;
    g->de                     = 1;
    g->lr                     = 1;
    g->pitch = g->aw = 40;
    g->al            = 400;
    g->vs            = 8;
    g->vbp           = 25;
    g->vfp           = 7;
    text_partition(g, 0, 0, 400);
    v->gvram[0][PC98_PLANE_B][0]  = 1;
    v->gvram[1][PC98_PLANE_R][0]  = 1;
    v->gvram[1][PC98_PLANE_R][40] = 1;

    pc98_86box_t *saved = pc98_active;
    pc98_active         = d;
    d->raster_phase     = 4;
    d->raster_line      = 24;
    pc98_scan_tick(d); /* First active scanline, after the back porch. */
    CHECK(b->line[0][0] == makecol(0, 0, 0xaa));
    v->display_page = 1;
    pc98_scan_tick(d);
    pc98_scan_tick(d);
    pc98_scan_tick(d);
    pc98_scan_tick(d);
    CHECK(b->line[1][0] == makecol(0xaa, 0, 0));
    CHECK(b->line[0][0] == makecol(0, 0, 0xaa));
    pc98_present_frame(d);
    CHECK(b->line[0][0] == makecol(0, 0, 0xaa));
    v->video_ff[7] = 0;
    pc98_render_lines(d, 2, 3);
    CHECK(b->line[2][0] == 0 && b->line[1][0] == makecol(0xaa, 0, 0));
    v->video_ff[7] = 1;
    g->disp        = 1;
    pc98_render_lines(d, 2, 3);
    CHECK(b->line[2][0] == makecol(0xaa, 0, 0) && b->line[3][0] == 0);
    pc98_active = saved;
    free(b->dat);
    free(b);
    free(d);
    monitors[0].target_buffer = NULL;
}

static void
test_gdc_clock_domains(void)
{
    pc98_86box_t *d         = calloc(1, sizeof(*d));
    uint64_t      saved_tsc = tsc, saved_usec = TIMER_USEC;
    TIMER_USEC = 1ULL << 32;
    tsc        = 0;
    pc98_video_init(&d->core);
    pc98_arm_raster(d, 1000);
    upd7220_write(&d->core.text_gdc, 1, 0x47);
    upd7220_write(&d->core.graph_gdc, 1, 0x47);
    CHECK(pc98_gdc_deadline(d, 0, 10) == 2);
    CHECK(pc98_gdc_deadline(d, 1, 10) == 4);
    bios_gdc_clock = 4;
    CHECK(pc98_gdc_deadline(d, 1, 10) == 4); /* RAM cannot change board clocks. */
    tsc = 2;
    pc98_gdc_sync(d);
    CHECK(d->core.text_gdc.cr == 0x47 && !d->core.text_gdc.busy_clocks);
    CHECK(d->core.graph_gdc.cr == 0 && d->core.graph_gdc.busy_clocks == 5);
    d->core.graph_clock_fast = 1;
    pc98_gdc_sync(d); /* Clock switch preserves the fractional phase. */
    CHECK(pc98_gdc_deadline(d, 1, 5) == 1);
    tsc++;
    pc98_gdc_sync(d);
    CHECK(d->core.graph_gdc.cr == 0x47 && !d->core.graph_gdc.busy_clocks);
    bios_gdc_clock = 0;
    pc98_video_reset(&d->core);
    CHECK(d->core.graph_clock_fast); /* CPU reset retains physical selection. */
    d->core.graph_clock_fast = 0;

    /* One second must yield identical clock counts and residual phase,
     * whether the guest polls thousands of times or makes one access. */
    pc98_86box_t *other = calloc(1, sizeof(*other));
    pc98_video_reset(&d->core);
    TIMER_USEC = 8ULL << 32;
    tsc        = 0;
    pc98_arm_raster(d, 1000);
    for (unsigned i = 0; i < 2; i++) {
        upd7220_t *g = i ? &d->core.graph_gdc : &d->core.text_gdc;
        g->aw        = 40;
        g->hfp       = 4;
        g->hs        = 4;
        g->hbp       = 5;
        g->vbp       = 25;
        g->al        = 400;
        g->vfp       = 7;
        g->vs        = 8;
    }
    *other = *d;
    tsc    = 8000000;
    pc98_gdc_sync(d);
    for (tsc = 1; tsc < 8000000; tsc += 613)
        pc98_gdc_sync(other);
    tsc = 8000000;
    pc98_gdc_sync(other);
    CHECK(d->core.text_gdc.bus_hclock == 5263150u % 106u);
    CHECK(d->core.graph_gdc.bus_hclock == 2631575u % 106u);
    CHECK(d->core.text_gdc.bus_hclock == other->core.text_gdc.bus_hclock);
    CHECK(d->core.graph_gdc.bus_line == other->core.graph_gdc.bus_line);
    CHECK(!d->gdc_clock_remainder[0] && !d->gdc_clock_remainder[1]);
    CHECK(!other->gdc_clock_remainder[0] && !other->gdc_clock_remainder[1]);

    /* Programmed porches, rather than one hardcoded blanking duration. */
    d->core.text_gdc.aw       = 80;
    d->core.text_gdc.hfp      = 8;
    d->core.text_gdc.hs       = 8;
    d->core.text_gdc.hbp      = 10;
    d->raster_clock_remainder = 0;
    uint64_t line             = 0;
    for (unsigned i = 1; i <= 4; i++)
        line += pc98_raster_phase_delay(d, i);
    CHECK(line == (uint64_t) ((uint128_t) TIMER_USEC * 848000000u / PC98_DOT_CLOCK_HZ));
    CHECK(pc98_raster_phase_delay(d, 2) < pc98_raster_phase_delay(d, 4));
    free(other);
    free(d);
    tsc        = saved_tsc;
    TIMER_USEC = saved_usec;
}

static void
test_vram_without_extra_waits(void)
{
    const uint64_t saved_tsc = tsc, saved_usec = TIMER_USEC;
    map_count          = 0;
    timer_patch_option = 0;
    pc98_86box_t *d    = pc9801_video_device.init(&pc9801_video_device);
    CHECK(d != NULL);
    if (!d)
        return;
    /* Stock and upgraded CPUs retain normal opcode/memory accounting.
     * VRAM callbacks must not add unmeasured GDC-clock WAITs on top. */
    const unsigned speeds[] = { 8, 10, 20, 32 };
    for (unsigned n = 0; n < sizeof(speeds) / sizeof(speeds[0]); n++) {
        is286      = 1;
        TIMER_USEC = (uint64_t) speeds[n] << 32;
        tsc        = 1000;
        pc98_reset(d);
        vram_stall_clocks    = 0;
        const uint64_t start = tsc;
        tvram_wb(0xa0000, 0x35, d);
        tvram_ww(0xa0000, 0xa135, d);
        tvram_ww(0xa0001, 0x4567, d);
        CHECK((d->core.tvram[0] >> 8) == 0x67 && (d->core.tvram[1] & 255) == 0x45);
        d->core.tvram[0x1000] = 0x00e1;
        CHECK(tvram_rw(0xa2000, d) == 0xffe1);
        gvram_wb(0xa8000, 0x23, &d->plane_map[0]);
        gvram_ww(0xa8000, 0xa123, &d->plane_map[0]);
        CHECK(gvram_rw(0xa8000, &d->plane_map[0]) == 0xa123);
        gvram_ww(0xa8001, 0x4567, &d->plane_map[0]);
        CHECK(gvram_rw(0xa8001, &d->plane_map[0]) == 0x4567);
        CHECK(tsc == start && !vram_stall_clocks);

        pc98_outb(0x7c, 0x80, d); /* GRCG TDW still writes parallel planes. */
        for (unsigned i = 0; i < 4; i++)
            pc98_outb(0x7e, 0xff, d);
        for (unsigned i = 0; i < 512; i++)
            gvram_ww(0xa8000 + 2 * i, 0, &d->plane_map[0]);
        for (unsigned i = 0; i < 4; i++)
            CHECK(d->core.gvram[0][i][0] == 0xffff);
        pc98_outb(0x7c, 0xc0, d); /* GRCG RMW retains its functional mask. */
        for (unsigned i = 0; i < 4; i++)
            pc98_outb(0x7e, 0, d);
        gvram_ww(0xa8000, 0x00ff, &d->plane_map[0]);
        pc98_outb(0x7c, 0, d);
        CHECK(gvram_rw(0xa8000, &d->plane_map[0]) == 0xff00);
        CHECK(tsc == start && !vram_stall_clocks);

        pc98_outb(0x6a, 7, d);
        pc98_outb(0x6a, 5, d);
        pc98_outb(0x6a, 6, d);
        CHECK(d->core.egc_enabled); /* CG clear still bypasses EGC. */
        gvram_ww(0xa8000, 0x2345, &d->plane_map[0]);
        CHECK(gvram_rw(0xa8000, &d->plane_map[0]) == 0x2345);
        pc98_outb(0x7c, 0x80, d);
        pc98_outw(0x4a4, 0x0400, d); /* CPU-source readback. */
        gvram_rw(0xa8000, &d->plane_map[0]);
        pc98_outb(0x7c, 0, d);

        pc98_outb(0xa6, 1, d); /* Page selection is immediate again. */
        gvram_ww(0xa8000, 0x789a, &d->plane_map[0]);
        CHECK(d->core.cpu_page == 1 && gvram_rw(0xa8000, &d->plane_map[0]) == 0x789a);
        pc98_outw(0xa4, 1, d);
        CHECK(d->core.display_page == 1);
        CHECK(tsc == start && !vram_stall_clocks);

        /* Refresh/GDC activity must not inject a CPU transaction penalty. */
        upd7220_t *g = &d->core.graph_gdc;
        g->aw        = 80;
        g->hfp       = 8;
        g->hs        = 8;
        g->hbp       = 10;
        g->vbp       = 25;
        g->al        = 400;
        g->vfp       = 7;
        g->vs        = 8;
        g->mode      = 6;
        g->de        = 0;
        upd7220_set_bus_position(g, 176, 25);
        gvram_rb(0xa8000, &d->plane_map[0]);
        CHECK(tsc == start && !vram_stall_clocks);
    }
    pc9801_video_device.close(d);
    map_count  = 0;
    is286      = 0;
    tsc        = saved_tsc;
    TIMER_USEC = saved_usec;
}

int
main(void)
{
    update_pending = test_update_pending;
    pic.icw1       = 0x11; /* Actual edge-PIC IRR and acknowledge behavior. */
    pic.imr        = 0xff; /* A request still latches while masked during POST. */
    pic_set_irq_callback(irq_notification, NULL);
    CHECK(!pc9801_video_device.available());
    CHECK(!pc9801bx4_video_device.available());
    CHECK(pc9801_video_device.init(&pc9801_video_device) == NULL);
    vx_roms = 1;
    CHECK(pc9801_video_device.available());
    CHECK(!pc9801bx4_video_device.available()); /* VX ROMs cannot satisfy BX4. */
    font_roms = 0;
    CHECK(!pc9801_video_device.available());
    CHECK(pc9801_video_device.init(&pc9801_video_device) == NULL);
    font_roms = 1;
    test_model(&pc9801_video_device, 2, 3);
    timer_patch_option = 1;
    map_count          = 0;
    CHECK(pc9801_video_device.init(&pc9801_video_device) == NULL);
    CHECK(timer_patch_errors == 1); /* Unknown fixture ROM is not patched. */
    CHECK(map_count == 0);          /* Reject before installing partial firmware maps. */
    bx4_roms = 1;
    CHECK(pc9801bx4_video_device.available());
    test_model(&pc9801bx4_video_device, 3, 14);
    CHECK(timer_patch_errors == 1); /* VX option does not patch BX4 firmware. */
    test_text_partitions();
    test_raster_page_switch();
    test_gdc_clock_domains();
    test_vram_without_extra_waits();
    printf("PC-98 video mappings and rendering: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}

int             machine;
const machine_t machines[] = { { .bus_flags = MACHINE_PC98 } };

int
machine_at_tandy_vis_init(const machine_t *model)
{
    (void) model;
    abort();
}
