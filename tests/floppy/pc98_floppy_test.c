/* Exercise the production board ports, 765, drive timers and IMG/86F decoder.
 * Only host services (clock, IRQs, DMA memory and UI) are adapted. */
#ifndef PC98_FDC_SOURCE
#define PC98_FDC_SOURCE "../../src/floppy/fdc.c"
#endif
#ifndef PC98_FDD_SOURCE
#define PC98_FDD_SOURCE "../../src/floppy/fdd.c"
#endif
#ifndef PC98_BOARD_SOURCE
#define PC98_BOARD_SOURCE "../../src/machine/m_pc98.c"
#endif
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
double fdd_audio_get_seek_time(void *priv, int tracks, int direction);
#include PC98_FDD_SOURCE
#include PC98_FDC_SOURCE
#include PC98_BOARD_SOURCE

static unsigned checks, failures;
#define CHECK(expr) do { \
    checks++; \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

uint64_t TIMER_USEC = 1ULL << 32, tsc, timer_target;
cpu_state_t cpu_state;
int isa_cycles = 1, is486, machine;
int mem_a20_key, mem_a20_alt, mem_a20_chipset;
uint32_t mem_size;
uint8_t *ram;
uint32_t rammask;
double cpuclock;
pit_intf_t pit_devs[2];
dma_t dma[8];
const machine_t machines[1] = {{0}};
static uint16_t asserted_irqs;
static int drq[8], ibm_decodes;
static uint8_t dma_bytes[2048];
static unsigned dma_count, dma_length;
static int dma_target;
static unsigned cpu_resets;

void softresetx86(void) { cpu_resets++; }
void mem_a20_recalc(void) {}
void mem_a20_reset_vector_bypass_once(void) {}
void flushmmucache(void) {}

const char *machine_getname(int m) { (void)m; return "NEC PC-9801VX"; }
int machine_is_pcjx(int m) { (void)m; return 0; }
int machine_at_if386sx_init(const machine_t *m) { (void)m; return 0; }
void pit_device_reset(pit_t *p) { (void)p; }
void pit_set_pit_const(void *p, uint64_t c) { (void)p; (void)c; }
void speaker_set_pc98_state(int enabled, int output) { (void)enabled; (void)output; }
void timer_enable(pc_timer_t *t) { t->flags |= TIMER_ENABLED; }
void timer_disable(pc_timer_t *t) { t->flags &= ~TIMER_ENABLED; }
void timer_add(pc_timer_t *t, void (*cb)(void *), void *p, int start)
{ t->callback = cb; t->priv = p; t->flags = start ? TIMER_ENABLED : 0; }
void picint_common(uint16_t mask, int level, int set, uint8_t *p)
{ (void)level; (void)p; if (set) asserted_irqs |= mask; else asserted_irqs &= ~mask; }
void dma_set_drq(int ch, int set) { drq[ch] = set; }
int dma_get_drq(int ch) { return drq[ch]; }
int dma_mode(int ch) { (void)ch; return 0; }
int dma_channel_read(int ch) { (void)ch; return DMA_NODATA; }
int dma_channel_write(int ch, uint16_t val)
{
    if (ch != dma_target || !drq[ch] || dma_count >= dma_length)
        return DMA_NODATA;
    dma_bytes[dma_count++] = (uint8_t)val;
    return dma_count == dma_length ? DMA_OVER : 0;
}
int dma_channel_writable(int ch) { return ch == dma_target; }
void dma_set_mask(uint32_t mask) { (void)mask; }
void dma_set_pc98_autoinc(uint8_t ch, uint8_t mode) { (void)ch; (void)mode; }
void ui_sb_update_icon(int a, int b) { (void)a; (void)b; }
void ui_sb_update_icon_write(int a, int b) { (void)a; (void)b; }
int fdd_tape_present(void *p) { (void)p; return 0; }
int fdd_tape_track0(void *p) { (void)p; return 0; }
int fdd_tape_get_flags(void *p) { (void)p; return 0; }
int fdd_tape_step(void *p, int n) { (void)p; (void)n; return 0; }
void fdd_tape_init(void) {}
void fdd_audio_set_motor_enable(void *p, int n) { (void)p; (void)n; }
void fdd_audio_play_multi_track_seek(void *p, int a, int b) { (void)p; (void)a; (void)b; }
void fdd_audio_play_single_track_step(void *p, int a, int b) { (void)p; (void)a; (void)b; }
double fdd_audio_get_seek_time(void *p, int n, int dir) { (void)p; (void)n; (void)dir; return 10000; }
int floppy_ioctl_read_sector(void *p, int t, int h, int s, uint8_t *b)
{ (void)p; (void)t; (void)h; (void)s; (void)b; return 0; }
int floppy_ioctl_write_sector(void *p, int t, int h, int s, const uint8_t *b)
{ (void)p; (void)t; (void)h; (void)s; (void)b; return 0; }
void floppy_ioctl_close(void *p) { (void)p; }
FILE *plat_fopen(const char *p, const char *mode) { return fopen(p, mode); }
char *path_get_extension(char *p) { char *dot = strrchr(p, '.'); return dot ? dot + 1 : p + strlen(p); }
void (pclog)(const char *fmt, ...) { (void)fmt; }
void (pclog_ex)(const char *fmt, va_list ap) { (void)fmt; (void)ap; }
void fatal(const char *fmt, ...)
{ va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); abort(); }
uint8_t random_generate(void) { return 0; }
char *nvr_path(char *p) { return p; }
static char rtc_nvr_request[64];
FILE *nvr_fopen(char *p, char *mode)
{
    (void)mode;
    snprintf(rtc_nvr_request, sizeof(rtc_nvr_request), "%s", p);
    return NULL;
}
void fdd_set_host_device(void *p, const char *path) { (void)p; (void)path; }
int floppy_ioctl_open(void *p, int *t, int *h, int *s, int *rate)
{ (void)p; (void)t; (void)h; (void)s; (void)rate; return 0; }
void io_sethandler(uint16_t base, uint16_t size,
    uint8_t (*rb)(uint16_t, void *), uint16_t (*rw)(uint16_t, void *),
    uint32_t (*rl)(uint16_t, void *), void (*wb)(uint16_t, uint8_t, void *),
    void (*ww)(uint16_t, uint16_t, void *), void (*wl)(uint16_t, uint32_t, void *), void *p)
{
    (void)size; (void)rb; (void)rw; (void)rl; (void)wb; (void)ww; (void)wl; (void)p;
    if (base >= 0x3f0 && base <= 0x3f7) ibm_decodes++;
}
void io_removehandler(uint16_t base, uint16_t size,
    uint8_t (*rb)(uint16_t, void *), uint16_t (*rw)(uint16_t, void *),
    uint32_t (*rl)(uint16_t, void *), void (*wb)(uint16_t, uint8_t, void *),
    void (*ww)(uint16_t, uint16_t, void *), void (*wl)(uint16_t, uint32_t, void *), void *p)
{ (void)base; (void)size; (void)rb; (void)rw; (void)rl; (void)wb; (void)ww; (void)wl; (void)p; }

static void fire(pc_timer_t *t)
{
    if (!timer_is_enabled(t)) return;
    tsc = t->ts_integer;
    timer_disable(t);
    t->callback(t->priv);
}

static void setup(pc98_mb_t *board, fdc_t *fdc)
{
    memset(board, 0, sizeof(*board));
    memset(fdc, 0, sizeof(*fdc));
    memset(drives, 0, sizeof(drives));
    memset(drq, 0, sizeof(drq));
    asserted_irqs = 0;
    for (int i = 0; i < 4; i++) {
        fdc->fdd[i] = &drives[i];
        drives[i].fdc = fdc;
        drives[i].empty = 1;
    }
    fdc->flags = FDC_FLAG_PC98 | FDC_FLAG_NEC | FDC_FLAG_NO_TDR;
    fdc->irq = 11;
    fdc->dma_ch = 2;
    fdc->fifo_p = fifo16_init();
    timer_add(&fdc->timer, fdc_callback, fdc, 0);
    board->fdc = fdc;
    timer_add(&board->seek_irq_timer[0], pc98_seek_irq_640_tick, board, 0);
    timer_add(&board->seek_irq_timer[1], pc98_seek_irq_1mb_tick, board, 0);
    fdd_set_type(&drives[0], fdd_get_from_internal_name("35_2hd_nec"));
    fdc_reset(fdc);
    pc98_mb_reset(board);
    fdd_reset(); /* Actual startup ordering: media timers come after the board. */
    fire(&fdc->timer);
}

static void command(pc98_mb_t *b, unsigned bank, const uint8_t *bytes, unsigned n)
{ for (unsigned i = 0; i < n; i++) fdc_out(bank ? 0x92 : 0xca, bytes[i], b); }

static unsigned result(pc98_mb_t *b, unsigned bank, uint8_t *bytes)
{
    unsigned n = 0;
    while (n < 7 && (fdc_in(bank ? 0x90 : 0xc8, b) & 0xf0) == 0xd0)
        bytes[n++] = fdc_in(bank ? 0x92 : 0xca, b);
    return n;
}

static void test_drive_presence(void)
{
    pc98_mb_t b; fdc_t f;
    setup(&b, &f);
    fdc_out(0x4be, 0x30, &b); /* Select B and request its 3-mode ID. */
    CHECK(fdc_in(0x4be, &b) == 0);
    fdc_out(0x4be, 0x31, &b);
    CHECK(b.fdc_3mode_144[1] == 0);
    CHECK(!drives[1].motoron);
    uint8_t sense[] = {0x04, 1}, res[7];
    command(&b, 1, sense, sizeof(sense));
    fire(&f.timer);
    CHECK(result(&b, 1, res) == 1);
    CHECK((res[0] & 0x38) == 0); /* No READY, TRACK0 or two-sided input. */
    CHECK(!drives[1].motoron);
    fdd_set_type(&drives[1], fdd_get_from_internal_name("35_2hd_nec"));
    fdc_out(0x4be, 0x30, &b);
    CHECK(fdc_in(0x4be, &b) == 0xfe); /* An installed empty drive still identifies. */
    CHECK(!fdd_is_ready(&drives[1]));
    fdd_set_type(&drives[1], fdd_get_from_internal_name("525_2hd"));
    CHECK(fdc_in(0x4be, &b) == 0);
    fifo_close(f.fifo_p);
}

static void test_reset_routes(void)
{
    for (unsigned bank = 0; bank < 2; bank++) {
        pc98_mb_t b; fdc_t f;
        setup(&b, &f);
        pc98_fdc_lock_route(&b, bank);
        f.drive = 1; /* Command unit and the internal DOR's unit 0 differ. */
        f.interrupt = f.processed_cmd = 0x06;
        f.stat = 0x50;
        f.paramstogo = 7; f.data_ready = f.fintr = f.tc = 1;
        drives[1].pending.pending = drives[1].seek_in_progress = 1;
        timer_set_delay_u64(&drives[1].seek_timer, 100 * TIMER_USEC);
        timer_set_delay_u64(&f.timer, 100 * TIMER_USEC);
        dma_set_drq(bank ? 2 : 3, 1);
        uint16_t ctrl = bank ? 0xcc : 0x94; /* Reset through the opposite identity. */
        fdc_out(ctrl, 0x80, &b);
        CHECK(!drq[2] && !drq[3]);
        CHECK(!timer_is_enabled(&f.timer));
        CHECK(!timer_is_enabled(&drives[1].seek_timer));
        CHECK(!drives[1].pending.pending && !drives[1].seek_in_progress);
        CHECK(!f.paramstogo && !f.data_ready && !f.fintr && !f.tc);
        CHECK(!b.fdc_route_lock);
        CHECK(f.stat == 0);
        fdc_out(ctrl, 0, &b);
        fire(&f.timer);
        CHECK(f.stat == 0x80);
        CHECK(f.reset_stat == 4); /* Reset notifications do not count installed drives. */
        CHECK(asserted_irqs & (1u << (bank ? 10 : 11)));
        fifo_close(f.fifo_p);
    }
}

static void test_machine_reset_order(void)
{
    pc98_mb_t b; fdc_t f;
    setup(&b, &f);
    CHECK(f.rate == 0 && f.bit_rate == 500 && f.densel_polarity == 0);
    for (int i = 0; i < 3; i++) {
        pc98_fdc_lock_route(&b, 0);
        dma_set_drq(3, 1);
        f.stat = 0x50; f.paramstogo = 7; f.tc = 1;
        pc98_mb_reset(&b); /* device_reset_all visits the board before its FDC. */
        fdc_reset(&f);
        fire(&f.timer);
        CHECK(f.rate == 0 && f.bit_rate == 500);
        CHECK(f.densel_polarity == 0);
        CHECK(f.irq == 11 && f.dma_ch == 2 && f.dma);
        CHECK(!drq[2] && !drq[3]);
        CHECK(!f.paramstogo && !f.tc);
        CHECK(ibm_decodes == 0);
    }
    /* A controller reset also preserves a board-selected 640KB/1.44MB mode. */
    fdc_out(0xbe, 0, &b);
    fdc_out(0x4be, 0x11, &b);
    fdc_reset(&f);
    CHECK(f.rate == 2 && f.densel_polarity == 1);
    fifo_close(f.fifo_p);
}

static void test_motor_resync(void)
{
    pc98_mb_t b; fdc_t f;
    setup(&b, &f);
    fdc_out(0x94, 0, &b);
    CHECK(timer_is_enabled(&drives[0].poll_time));
    fdd_reset(); /* Rebuilds media timers while the board latch still says on. */
    fdc_out(0x94, 0, &b);
    CHECK(drives[0].motoron && timer_is_enabled(&drives[0].poll_time));
    CHECK(!drives[1].motoron && !timer_is_enabled(&drives[1].poll_time));
    fifo_close(f.fifo_p);
}

static void test_cpu_only_reset(void)
{
    pc98_mb_t b; fdc_t f;
    setup(&b, &f);
    pc98_fdc_lock_route(&b, 0);
    b.fdc_mode = 3; /* The selected idle identity differs from the active one. */
    b.sys_port_c = PC98_SYS_C_MCKE;
    b.sis_q_n[0] = 1;
    f.interrupt = f.processed_cmd = 0x06;
    f.stat = 0x50;
    dma_set_drq(3, 1);
    pc98_cpu_reset_out(0xf0, 0, &b);
    CHECK(cpu_resets == 1);
    CHECK(b.sys_port_c == PC98_SYS_C_MCKE);
    CHECK(b.fdc_route_lock && f.dma_ch == 3 && f.irq == 10);
    CHECK(drq[3] && f.stat == 0x50 && f.interrupt == 0x06);
    CHECK(b.sis_q_n[0] == 1);
    fdc_out(0x94, 0x80, &b); /* BIOS can then reset the controller explicitly. */
    CHECK(!drq[3] && !b.sis_q_n[0] && !b.fdc_route_lock);
    fifo_close(f.fifo_p);
}

static int tick(pc98_mb_t *b)
{
    pc_timer_t *next = NULL;
    pc_timer_t *timers[] = { &b->fdc->timer, &b->seek_irq_timer[0], &b->seek_irq_timer[1],
        &drives[0].poll_time, &drives[0].seek_timer, &drives[0].ready_timer };
    for (unsigned i = 0; i < sizeof(timers) / sizeof(timers[0]); i++)
        if (timer_is_enabled(timers[i]) && (!next || TIMER_LESS_THAN(timers[i], next)))
            next = timers[i];
    if (!next) return 0;
    fire(next);
    return 1;
}

static void read_sector(pc98_mb_t *b)
{
    fdc_out(0x94, 0, b);
    for (unsigned i = 0; i < 1000000 && !fdd_is_ready(&drives[0]); i++)
        if (!tick(b)) break;
    CHECK(fdd_is_ready(&drives[0]));
    CHECK(fdd_getrpm(&drives[0]) == 360);
    const uint8_t specify[] = { 0x03, 0xef, 0x02 };
    command(b, 1, specify, sizeof(specify));
    const uint8_t read[] = { 0x46, 0, 0, 0, 1, 3, 1, 0x1b, 0xff };
    dma_target = 2;
    dma_length = 1024;
    dma_count = 0;
    memset(dma_bytes, 0, sizeof(dma_bytes));
    command(b, 1, read, sizeof(read));
    for (unsigned i = 0; i < 200000 && (fdc_in(0x90, b) & 0xf0) != 0xd0; i++)
        if (!tick(b)) break;
    uint8_t res[7] = {0xff};
    CHECK(result(b, 1, res) == 7);
    CHECK((res[0] & 0xc0) == 0 && res[1] == 0 && res[2] == 0);
    CHECK(dma_count == 1024);
    int matches = 1;
    for (unsigned i = 0; i < 1024; i++)
        if (dma_bytes[i] != (uint8_t)(i ^ 0xa5)) matches = 0;
    CHECK(matches);
    CHECK(!drq[2] && !drq[3]);
}

static void test_real_media_resets(void)
{
    char filename[] = "pc98-floppy-test-generated.img";
    int fd = open(filename, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0600);
    FILE *file = fd < 0 ? NULL : fdopen(fd, "wb"); /* Never overwrite an existing file. */
    if (!file) fatal("Cannot create test image %s\n", filename);
    uint8_t sector[1024];
    for (unsigned i = 0; i < sizeof(sector); i++) sector[i] = (uint8_t)(i ^ 0xa5);
    for (unsigned i = 0; i < 77 * 2 * 8; i++)
        if (fwrite(sector, 1, sizeof(sector), file) != sizeof(sector)) fatal("Image write failed\n");
    CHECK(fclose(file) == 0);
    pc98_mb_t b;
    fdc_t *f = calloc(1, sizeof(*f));
    if (!f) fatal("Cannot allocate test FDC\n");
    setup(&b, f);
    fdd_set_check_bpb(&drives[0], 0);
    d86f_setup(&drives[0]);
    img_load(&drives[0], filename);
    CHECK(drives[0].readsector != NULL);
    drives[0].empty = 0;
    fdd_do_seek(&drives[0], 0);
    read_sector(&b); /* Fresh emulator startup. */
    for (unsigned i = 0; i < 3; i++) {
        fdc_out(0x94, 0x80, &b);
        fdc_out(0x94, 0, &b);
        fire(&f->timer);
        read_sector(&b); /* Firmware's controller-reset pulse. */
        pc98_mb_reset(&b);
        fdc_reset(f);
        fire(&f->timer);
        read_sector(&b); /* In-place machine reset, board then core. */
        pc98_mb_reset(&b);
        fdd_reset();
        fire(&f->timer);
        read_sector(&b); /* Media-timer rebuild used by machine recreation. */
    }
    /* The UI hard reset closes and recreates devices while retaining media. */
    for (unsigned i = 0; i < 3; i++) {
        fdc_close(f);
        CHECK(drives[0].local != NULL && drives[0].fdc == NULL);
        f = fdc_init(&fdc_pc98_device);
        b.fdc = f;
        pc98_mb_reset(&b);
        fdd_reset();
        fire(&f->timer);
        read_sector(&b);
    }
    fdd_set_motor_enable(&drives[0], 0);
    img_close(&drives[0]);
    d86f_destroy(&drives[0]);
    fdc_close(f);
    CHECK(remove(filename) == 0);
}

static void test_rtc_reset(void)
{
    pc98_rtc_t r = {.time_offset = 42, .command = 3, .stb = 1, .clk = 1};
    pc98_rtc_reset(&r);
    CHECK(r.time_offset == 42);
    CHECK(r.command == 0 && r.stb == 0 && r.clk == 0);
    pc98_rtc_save(&r);
    CHECK(!strcmp(rtc_nvr_request, "pc9801vx.rtc"));
    pc98_rtc_load(&r);
    CHECK(!strcmp(rtc_nvr_request, "pc9801vx.rtc"));
}

int main(void)
{
    test_drive_presence();
    test_reset_routes();
    test_machine_reset_order();
    test_motor_resync();
    test_cpu_only_reset();
    test_real_media_resets();
    test_rtc_reset();
    printf("PC-98 floppy: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
