/* Initialize the actual video wrapper with synthetic ROMs and inspect the
   registered bus handlers. Host presentation and clocks are passive adapters. */
#include "../../src/video/vid_pc98_86box.c"

static unsigned checks, failures;
#define CHECK(expr) do { checks++; if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); failures++; } } while (0)
uint64_t tsc, TIMER_USEC = 1ULL << 32, timer_target;
monitor_t monitors[MONITORS_NUM];
int monitor_index_global, frames;
static mem_mapping_t *maps[12];
static unsigned map_count;
static int vx_roms, font_roms = 1;
FILE *rom_fopen(const char *path, char *mode)
{
    (void)mode;
    size_t size = 0;
    unsigned seed = 0;
    if (strstr(path, "pc9801/ipl.rom") && vx_roms) { size = 0x30000; seed = 3; }
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
void picint_common(uint16_t mask, int level, int set, uint8_t *p)
{ (void)mask; (void)level; (void)set; (void)p; }
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
    CHECK(map_count == aliases + 6); /* TVRAM, CG and four graphics planes */
    const uint32_t base[] = {0xe8000u, 0xfe8000u};
    for (unsigned n = 0; n < aliases; n++) {
        mem_mapping_t *m = maps[n];
        CHECK(m->base == base[n] && m->size == 0x18000);
        CHECK(m->write_b == NULL && m->write_w == NULL && m->write_l == NULL);
        uint32_t reset = base[n] + 0x17ff0u;
        CHECK(m->read_b(reset, m->priv) == (uint8_t)(seed * 31 + 0x17f + 0xf0));
        uint32_t expected = 0;
        for (unsigned b = 0; b < 4; b++)
            expected |= (uint32_t)(uint8_t)(seed * 31 + 0x17f + 0xf0 + b) << (b * 8);
        CHECK(m->read_l(reset, m->priv) == expected);
        ipl_bank_out(0x43d, 2, d);
        CHECK(m->read_b(reset, m->priv) == (uint8_t)(seed * 31 + 0x2ff + 0xf0));
        ipl_bank_out(0x43d, 0xff, d); /* reserved writes do not change the bank */
        CHECK(d->ipl_bank == 1);
        dev->reset(d);
        CHECK(d->ipl_bank == 0);
        CHECK(m->read_b(reset, m->priv) == (uint8_t)(seed * 31 + 0x17f + 0xf0));
    }
    dev->close(d);
}
int main(void)
{
    CHECK(!pc9801_video_device.available());
    CHECK(pc9801_video_device.init(&pc9801_video_device) == NULL);
    vx_roms = 1;
    CHECK(pc9801_video_device.available());
    font_roms = 0;
    CHECK(!pc9801_video_device.available());
    CHECK(pc9801_video_device.init(&pc9801_video_device) == NULL);
    font_roms = 1;
    test_model(&pc9801_video_device, 2, 3);
    printf("PC-98 video mappings: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
