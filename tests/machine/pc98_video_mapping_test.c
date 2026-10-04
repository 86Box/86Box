/* Initialize the actual video wrapper with synthetic ROMs and inspect the
   registered bus handlers. Host presentation and clocks are passive adapters. */
#include "../../src/video/vid_pc98_86box.c"
#include <86box/pit.h>
#include "../../src/pic.c"

static unsigned checks, failures;
#define CHECK(expr) do { checks++; if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); failures++; } } while (0)
uint64_t tsc, TIMER_USEC = 1ULL << 32, timer_target;
monitor_t monitors[MONITORS_NUM];
int monitor_index_global, frames;
static mem_mapping_t *maps[12];
static unsigned map_count;
static int vx_roms, bx4_roms, font_roms = 1;
static unsigned vrtc_raises;
static int timer_patch_option;
static unsigned timer_patch_errors;
int machine_get_config_int(char *name)
{ return !strcmp(name, "skip_timer_checks") && timer_patch_option; }
int ui_msgbox_header(int flags, char *header, char *message)
{ (void)header; (void)message; if (flags == MBX_ERROR) timer_patch_errors++; return 0; }
FILE *rom_fopen(const char *path, char *mode)
{
    (void)mode;
    size_t size = 0;
    unsigned seed = 0;
    if (strstr(path, "pc9801/ipl.rom") && vx_roms) { size = 0x30000; seed = 3; }
    else if (strstr(path, "pc9801bx4/pc98BANK") && bx4_roms) {
        size = 0x8000;
        seed = 10 + (strstr(path, "pc98BANK")[8] - '0');
    }
    else if (font_roms && strstr(path, "/font.rom")) size = PC98_FONT_DUMP_SIZE;
    else if (font_roms && strstr(path, "24256c-x")) size = 0x8000;
    if (!size) return NULL;
    FILE *f = tmpfile();
    if (!f) abort();
    for (unsigned i = 0; i < size; i++) fputc((seed * 31 + (i >> 8) + i) & 255, f);
    rewind(f);
    return f;
}
int rom_present(const char *p)
{
    FILE *f = rom_fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}
void (pclog)(const char *fmt, ...) { (void)fmt; }
void timer_enable(pc_timer_t *t) { t->flags |= TIMER_ENABLED; }
void timer_disable(pc_timer_t *t) { t->flags &= ~TIMER_ENABLED; }
void timer_add(pc_timer_t *t, void (*cb)(void *), void *p, int start)
{ t->callback = cb; t->priv = p; t->flags = start ? TIMER_ENABLED : 0; }
uint8_t mem_readb_phys(uint32_t a) { (void)a; return 0; }
void mem_writeb_phys(uint32_t a, uint8_t v) { (void)a; (void)v; }
static void irq_notification(uint16_t mask, int set, void *priv)
{ (void)priv; if (set && (mask & (1u << 2))) vrtc_raises++; }
static void test_update_pending(void) { }
int acpi_rtc_status;
void smi_raise(void) { }
void io_sethandler(uint16_t base, uint16_t size,
    uint8_t (*rb)(uint16_t, void *), uint16_t (*rw)(uint16_t, void *),
    uint32_t (*rl)(uint16_t, void *), void (*wb)(uint16_t, uint8_t, void *),
    void (*ww)(uint16_t, uint16_t, void *), void (*wl)(uint16_t, uint32_t, void *), void *p)
{ (void)base; (void)size; (void)rb; (void)rw; (void)rl; (void)wb; (void)ww; (void)wl; (void)p; }
void mem_mapping_add(mem_mapping_t *m, uint32_t base, uint32_t size,
    uint8_t (*rb)(uint32_t, void *), uint16_t (*rw)(uint32_t, void *),
    uint32_t (*rl)(uint32_t, void *), void (*wb)(uint32_t, uint8_t, void *),
    void (*ww)(uint32_t, uint16_t, void *), void (*wl)(uint32_t, uint32_t, void *),
    uint8_t *exec, uint32_t flags, void *p)
{
    *m = (mem_mapping_t){.base = base, .size = size, .read_b = rb, .read_w = rw,
        .read_l = rl, .write_b = wb, .write_w = ww, .write_l = wl,
        .exec = exec, .flags = flags, .priv = p};
    maps[map_count++] = m;
}
void video_inform_monitor(int type, const video_timings_t *p, int n)
{ (void)type; (void)p; (void)n; }
void video_blit_memtoscreen_monitor(int x, int y, int w, int h, int n)
{ (void)x; (void)y; (void)w; (void)h; (void)n; }
void video_force_resize_set_monitor(uint8_t v, int n) { (void)v; (void)n; }
uint8_t video_force_resize_get_monitor(int n) { (void)n; return 0; }
void set_screen_size(int x, int y) { (void)x; (void)y; }

static void test_model(const device_t *dev, unsigned aliases, unsigned seed)
{
    map_count = 0;
    pc98_86box_t *d = dev->init(dev);
    CHECK(d != NULL);
    if (!d) return;
    const unsigned initial_vrtc = vrtc_raises;
    pc98_begin_vsync(d);
    CHECK(vrtc_raises == initial_vrtc + 1); /* One request after power-on. */
    CHECK(pic.irr & (1u << 2));
    pic.interrupt = 2;
    pic_acknowledge(&pic);
    CHECK(!(pic.irr & (1u << 2)) && (pic.isr & (1u << 2)));
    pic_write(0, 0x20, &pic); /* EOI releases service but does not reset CRTV. */
    CHECK(!(pic.isr & (1u << 2)));
    for (unsigned frame = 0; frame < 10; frame++) pc98_begin_vsync(d);
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
    const uint32_t base[] = {0xe8000u, 0xfe8000u, 0xfffe8000u};
    for (unsigned n = 0; n < aliases; n++) {
        mem_mapping_t *m = maps[n];
        CHECK(m->base == base[n] && m->size == 0x18000);
        CHECK(m->write_b == NULL && m->write_w == NULL && m->write_l == NULL);
        uint32_t reset = base[n] + 0x17ff0u;
        const unsigned reset_off = aliases == 3 ? 0x7f : 0x17f;
        CHECK(m->read_b(reset, m->priv) == (uint8_t)(seed * 31 + reset_off + 0xf0));
        uint32_t expected = 0;
        for (unsigned b = 0; b < 4; b++)
            expected |= (uint32_t)(uint8_t)(seed * 31 + reset_off + 0xf0 + b) << (b * 8);
        CHECK(m->read_l(reset, m->priv) == expected);
        ipl_bank_out(0x43d, 2, d);
        CHECK(m->read_b(reset, m->priv) == (uint8_t)((aliases == 3 ? 17 : seed) * 31 +
                                                    (aliases == 3 ? 0x7f : 0x2ff) + 0xf0));
        ipl_bank_out(0x43d, 0xff, d); /* reserved writes do not change the bank */
        CHECK(d->ipl_bank == 1);
        dev->reset(d);
        CHECK(d->ipl_bank == 0);
        CHECK(m->read_b(reset, m->priv) == (uint8_t)(seed * 31 + reset_off + 0xf0));
        if (aliases == 3) {
            for (unsigned bank = 0; bank < 8; bank++) {
                pc98_flash_bank_out(0x43f, 0xe0 + bank * 2, d);
                CHECK(m->read_b(reset, m->priv) == (uint8_t)((10 + bank) * 31 + 0x7f + 0xf0));
                CHECK(m->read_b(base[n] + 0x8000, m->priv) == (uint8_t)(0xff));
            }
            ipl_bank_out(0x43d, 2, d);
            CHECK(d->flash_bank == 0xff);
            CHECK(m->read_b(reset, m->priv) == (uint8_t)(17 * 31 + 0x7f + 0xf0));
            dev->reset(d);
        }
    }
    dev->close(d);
}
static void text_partition(upd7220_t *g, unsigned part, unsigned sad, unsigned lines)
{
    unsigned p = part * 4;
    g->ra[p] = sad;
    g->ra[p + 1] = (sad >> 8) & 0x1f;
    g->ra[p + 2] = (lines & 15) << 4;
    g->ra[p + 3] = (lines >> 4) & 0x3f;
}

static void test_text_partitions(void)
{
    pc98_86box_t *d = calloc(1, sizeof(*d));
    bitmap_t *b = calloc(1, sizeof(*b));
    b->w = 640;
    b->h = 400;
    b->dat = calloc(640 * 400, sizeof(*b->dat));
    for (unsigned y = 0; y < 400; y++) b->line[y] = b->dat + y * 640;
    monitors[0].target_buffer = b;
    pc98_video_t *v = &d->core;
    v->font_rom = calloc(1, PC98_FONT_ADDRESS_SIZE);
    v->video_ff[3] = 1;
    for (unsigned i = 0; i < 0x1000; i++) v->tvram[i | 0x1000] = 0xe1;
    for (unsigned code = 1; code <= 8; code++)
        memset(v->font_rom + 0x800 + code * 16, 0x80 >> (code - 1), 16);
    upd7220_t *g = &v->text_gdc;
    g->de = 1;
    g->lr = 16;
    g->pitch = 80;
    g->al = 400;

    /* A 40/360 scanline split falls halfway through a 16-line row.
       Distinct glyphs also catch using screen rows to offset the second SAD. */
    text_partition(g, 0, 0x300, 40);
    text_partition(g, 1, 0x600, 360);
    v->tvram[0x300] = 1;
    v->tvram[0x350] = 2;
    v->tvram[0x3a0] = 3;
    v->tvram[0x600] = 5;
    v->tvram[0x650] = 6;
    pc98_render_text(d, g);
    CHECK(b->line[0][0] == 0xffffffffu);
    CHECK(b->line[16][1] == 0xffffffffu);
    CHECK(b->line[39][2] == 0xffffffffu);
    CHECK(b->line[40][4] == 0xffffffffu && b->line[40][2] == 0);
    CHECK(b->line[55][4] == 0xffffffffu);
    CHECK(b->line[56][5] == 0xffffffffu);

    /* Empty areas consume no raster lines, and a short area is clipped
       exactly even when LEN is not divisible by the character height. */
    memset(b->dat, 0, 640 * 400 * sizeof(*b->dat));
    text_partition(g, 1, 0x600, 23);
    text_partition(g, 2, 0x800, 0);
    text_partition(g, 3, 0x900, 17);
    v->tvram[0x900] = 7;
    v->tvram[0x950] = 8;
    pc98_render_text(d, g);
    CHECK(b->line[62][5] == 0xffffffffu);
    CHECK(b->line[63][6] == 0xffffffffu);
    CHECK(b->line[79][7] == 0xffffffffu);
    CHECK(b->line[80][7] == 0);

    /* Smooth scrolling carries into the next VRAM row within each area.
       A new partition starts its character scanline counter at zero. */
    memset(b->dat, 0, 640 * 400 * sizeof(*b->dat));
    memset(v->font_rom + 0x800 + 9 * 16, 0x80, 8);
    memset(v->font_rom + 0x800 + 9 * 16 + 8, 0x01, 8);
    v->tvram[0x300] = v->tvram[0x600] = 9;
    v->txt_scroll[3] = 8;
    v->txt_scroll[4] = 0; /* Whole screen scroll window. */
    v->txt_scroll[5] = 31;
    pc98_render_text(d, g);
    CHECK(b->line[0][7] == 0xffffffffu && b->line[0][0] == 0);
    CHECK(b->line[8][1] == 0xffffffffu);
    CHECK(b->line[40][7] == 0xffffffffu);
    CHECK(b->line[48][5] == 0xffffffffu);

    /* Normal one-area text still reaches the bottom row and wraps TVRAM. */
    memset(b->dat, 0, 640 * 400 * sizeof(*b->dat));
    memset(g->ra, 0, sizeof(g->ra));
    memset(v->txt_scroll, 0, sizeof(v->txt_scroll));
    text_partition(g, 0, 0xff0, 400);
    v->tvram[0xff0] = 1;
    v->tvram[0x040] = 2;
    v->tvram[(0xff0 + 24 * 80) & 0xfff] = 3;
    pc98_render_text(d, g);
    CHECK(b->line[0][0] == 0xffffffffu);
    CHECK(b->line[16][1] == 0xffffffffu);
    CHECK(b->line[399][2] == 0xffffffffu);
    free(v->font_rom);
    free(b->dat);
    free(b);
    free(d);
    monitors[0].target_buffer = NULL;
}

int main(void)
{
    update_pending = test_update_pending;
    pic.icw1 = 0x11; /* Actual edge-PIC IRR and acknowledge behavior. */
    pic.imr = 0xff; /* A request still latches while masked during POST. */
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
    map_count = 0;
    CHECK(pc9801_video_device.init(&pc9801_video_device) == NULL);
    CHECK(timer_patch_errors == 1); /* Unknown fixture ROM is not patched. */
    CHECK(map_count == 0); /* Reject before installing partial firmware maps. */
    bx4_roms = 1;
    CHECK(pc9801bx4_video_device.available());
    test_model(&pc9801bx4_video_device, 3, 14);
    CHECK(timer_patch_errors == 1); /* VX option does not patch BX4 firmware. */
    test_text_partitions();
    printf("PC-98 video mappings and rendering: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
