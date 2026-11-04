/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Tests of the S3 graphics engine, through its port and MMIO
 *          registers, on every chip vid_s3.c emulates.
 *
 *          The engine is included as C, for its private functions. The
 *          FIFO thread runs on the test's own thread. Every test runs on
 *          every chip, but is only asserted on the chips it lists: those
 *          whose S3 databook (or S3.TXT, below) states its rule, with the
 *          page or line cited at the test. -v lists every result per chip.
 *
 *          The Vision868, Vision968 and Trio64V2 books describe their
 *          changes against the Vision864, Vision964 and Trio64V+ books
 *          (Vision868 p. 1-1, Vision968 p. 1-1, Trio64V2 p. 1-5): a page
 *          cited from the older book also covers the newer chip, unless
 *          its own book changes the rule. For the 86C911, 86C924, 86C801,
 *          86C805 and 86C805i, which have no databook here, the rules come
 *          from S3.TXT in Finn Thoegersen's VGADOC, cited by line; it has
 *          the 86C928PCI as the PCI version of the 86C928 (line 14):
 *          https://pdos.csail.mit.edu/6.828/2018/readings/hardware/vgadoc/S3.TXT
 *
 * Authors: Andreas Zdziarstek, <https://github.com/gmcn42>
 *
 *          Copyright 2026 Andreas Zdziarstek.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/video/vid_s3.c"

#define VRAM_SIZE (2 << 20)

/* One bit per chip, for the chips a test is asserted on. */
#define C_911      (1 << 0)
#define C_924      (1 << 1)
#define C_928      (1 << 2)
#define C_928PCI   (1 << 3)
#define C_801      (1 << 4)
#define C_805      (1 << 5)
#define C_805I     (1 << 6)
#define C_964      (1 << 7)
#define C_968      (1 << 8)
#define C_864      (1 << 9)
#define C_868      (1 << 10)
#define C_TRIO32   (1 << 11)
#define C_TRIO64   (1 << 12)
#define C_TRIO64V  (1 << 13)
#define C_TRIO64V2 (1 << 14)
#define C_ALL      ((1 << 15) - 1)

typedef struct {
    int         chip;
    int         bit;
    const char *name;
    int         packed_mmio; /* As s3_init() sets it */
    int         max_bpp;     /* CR50 pixel length (S3.TXT lines 428, 433-435): none on the 911/924 */
} chip_t;

static const chip_t chips[] = {
    { S3_86C911,    C_911,      "911",       0, 8  },
    { S3_86C924,    C_924,      "924",       0, 8  },
    { S3_86C928,    C_928,      "928",       0, 32 },
    { S3_86C928PCI, C_928PCI,   "928PCI",    0, 32 },
    { S3_86C801,    C_801,      "801",       0, 16 },
    { S3_86C805,    C_805,      "805",       0, 16 },
    { S3_86C805I,   C_805I,     "805i",      0, 16 },
    { S3_VISION964, C_964,      "Vision964", 1, 32 },
    { S3_VISION968, C_968,      "Vision968", 1, 32 },
    { S3_VISION864, C_864,      "Vision864", 0, 32 },
    { S3_VISION868, C_868,      "Vision868", 1, 32 },
    { S3_TRIO32,    C_TRIO32,   "Trio32",    1, 32 },
    { S3_TRIO64,    C_TRIO64,   "Trio64",    1, 32 },
    { S3_TRIO64V,   C_TRIO64V,  "Trio64V+",  1, 32 },
    { S3_TRIO64V2,  C_TRIO64V2, "Trio64V2",  1, 32 }
};

#define NUM_CHIPS (sizeof(chips) / sizeof(chips[0]))
#define MAX_TESTS 32

double    cpuclock = 50000000.0;
monitor_t monitors[MONITORS_NUM];
int       monitor_index_global;
uint32_t *video_15to32, *video_16to32;
int       xga_active;

static s3_t      s3;
static uint8_t   vram[VRAM_SIZE];
static uint8_t   changed[VRAM_SIZE >> 12];
static monitor_t monitor;
static int       fifo_waits;
static char      wake_event, not_full_event;

static int           verbose;
static int           failures;
static const chip_t *cur_chip;
static int           cur_bpp;
static int           asserted; /* The current test lists the current chip */
static int           cur_failed;
static int           num_tests;
static const char   *test_names[MAX_TESTS];
static char          results[MAX_TESTS][NUM_CHIPS];

// clang-format off
/* Outside the tested paths. */
void bt48x_hwcursor_draw(svga_t *svga, int displine) { abort(); }
void bt48x_ramdac_out(uint16_t addr, int rs2, int rs3, uint8_t val, void *priv, svga_t *svga) { abort(); }
uint8_t bt48x_ramdac_in(uint16_t addr, int rs2, int rs3, void *priv, svga_t *svga) { abort(); }
void bt48x_recalctimings(void *priv, svga_t *svga) { abort(); }
void att49x_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga) { abort(); }
uint8_t att49x_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga) { abort(); }
void att498_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga) { abort(); }
uint8_t att498_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga) { abort(); }
void ibm_rgb528_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga) { abort(); }
uint8_t ibm_rgb528_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga) { abort(); }
void ibm_rgb528_recalctimings(void *priv, svga_t *svga) { abort(); }
void mu9c1880_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga) { abort(); }
uint8_t mu9c1880_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga) { abort(); }
void sc1148x_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga) { abort(); }
uint8_t sc1148x_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga) { abort(); }
void sc1502x_ramdac_out(uint16_t addr, uint8_t val, void *priv, svga_t *svga) { abort(); }
uint8_t sc1502x_ramdac_in(uint16_t addr, void *priv, svga_t *svga) { abort(); }
void sdac_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga) { abort(); }
uint8_t sdac_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga) { abort(); }
void stg_ramdac_out(uint16_t addr, uint8_t val, void *priv, svga_t *svga) { abort(); }
uint8_t stg_ramdac_in(uint16_t addr, void *priv, svga_t *svga) { abort(); }
void tvp3026_ramdac_out(uint16_t addr, int rs2, int rs3, uint8_t val, void *priv, svga_t *svga) { abort(); }
uint8_t tvp3026_ramdac_in(uint16_t addr, int rs2, int rs3, void *priv, svga_t *svga) { abort(); }
void tvp3026_recalctimings(void *priv, svga_t *svga) { abort(); }
bool nmc93cxx_eeprom_read(nmc93cxx_eeprom_t *dev) { abort(); }
void nmc93cxx_eeprom_write(nmc93cxx_eeprom_t *dev, bool eecs, bool eesk, bool eedi) { abort(); }
void i2c_gpio_set(void *dev_handle, uint8_t scl, uint8_t sda) { abort(); }
uint8_t i2c_gpio_get_sda(void *dev_handle) { abort(); }
uint8_t i2c_gpio_get_scl(void *dev_handle) { abort(); }
uint8_t svga_in(uint16_t addr, void *priv) { abort(); }
uint8_t svga_read_linear(uint32_t addr, void *priv) { abort(); }
uint16_t svga_readw_linear(uint32_t addr, void *priv) { abort(); }
void svga_write_linear(uint32_t addr, uint8_t val, void *priv) { abort(); }
void svga_writel_linear(uint32_t addr, uint32_t val, void *priv) { abort(); }
uint8_t xga_read_test(uint32_t addr, void *priv) { abort(); }
void xga_write_test(uint32_t addr, uint8_t val, void *priv) { abort(); }

/* Mappings, IRQs and timings don't matter here. */
void mem_mapping_enable(mem_mapping_t *map) { }
void mem_mapping_disable(mem_mapping_t *map) { }
void mem_mapping_set_addr(mem_mapping_t *map, uint32_t base, uint32_t size) { }
void mem_mapping_set_handler(mem_mapping_t *map, uint8_t (*read_b)(uint32_t addr, void *priv),
                             uint16_t (*read_w)(uint32_t addr, void *priv),
                             uint32_t (*read_l)(uint32_t addr, void *priv),
                             void (*write_b)(uint32_t addr, uint8_t val, void *priv),
                             void (*write_w)(uint32_t addr, uint16_t val, void *priv),
                             void (*write_l)(uint32_t addr, uint32_t val, void *priv)) { }
void pci_irq(uint8_t slot, uint8_t pci_int, int level, int set, uint8_t *irq_state) { }
void svga_out(uint16_t addr, uint8_t val, void *priv) { }
void svga_recalctimings(svga_t *svga) { }
void svga_vert_int_crtc_write(svga_t *svga) { }
int svga_vert_int_active(const svga_t *svga) { return 0; }
uint64_t plat_timer_read(void) { return 0; }
// clang-format on

/* The ports as vid_s3.c registers them, per chip. */
static struct {
    uint8_t (*inb)(uint16_t port, void *priv);
    uint16_t (*inw)(uint16_t port, void *priv);
    void (*outb)(uint16_t port, uint8_t val, void *priv);
    void (*outw)(uint16_t port, uint16_t val, void *priv);
} io[0x10000];

void
io_sethandler(uint16_t base, uint16_t size, uint8_t (*inb)(uint16_t port, void *priv),
              uint16_t (*inw)(uint16_t port, void *priv), uint32_t (*inl)(uint16_t port, void *priv),
              void (*outb)(uint16_t port, uint8_t val, void *priv),
              void (*outw)(uint16_t port, uint16_t val, void *priv),
              void (*outl)(uint16_t port, uint32_t val, void *priv), void *priv)
{
    for (int i = 0; i < size; i++) {
        io[base + i].inb  = inb;
        io[base + i].inw  = inw;
        io[base + i].outb = outb;
        io[base + i].outw = outw;
    }
}

void
io_removehandler(uint16_t base, uint16_t size, uint8_t (*inb)(uint16_t port, void *priv),
                 uint16_t (*inw)(uint16_t port, void *priv), uint32_t (*inl)(uint16_t port, void *priv),
                 void (*outb)(uint16_t port, uint8_t val, void *priv),
                 void (*outw)(uint16_t port, uint16_t val, void *priv),
                 void (*outl)(uint16_t port, uint32_t val, void *priv), void *priv)
{
    memset(&io[base], 0, size * sizeof(io[0]));
}

static float
test_clock(int clock, void *priv)
{
    return 40000000.0f;
}

/* Run the FIFO thread's loop once: its second wait for work ends it. */
static void
drain(void)
{
    fifo_waits         = 0;
    s3.fifo_thread_run = 1;
    mach64_fifo_thread(&s3);
}

void
thread_set_event(event_t *event)
{
}
void
thread_reset_event(event_t *event)
{
}

int
thread_wait_event(event_t *event, int timeout)
{
    if (event == s3.wake_fifo_thread) {
        if (++fifo_waits > 1)
            s3.fifo_thread_run = 0;
    } else
        drain(); /* Waiting for room or for the engine to go idle. */
    return 0;
}

static void
check(int cond, const char *fmt, ...)
{
    va_list ap;

    if (cond)
        return;
    cur_failed = 1;
    if (!asserted && !verbose)
        return;
    fprintf(stderr, "%s: %s, %d bpp: ", asserted ? "FAIL" : "differs", cur_chip->name, cur_bpp);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    if (asserted)
        failures++;
}

static void
crtc_w(uint8_t index, uint8_t val)
{
    s3_out(0x3d4, index, &s3);
    s3_out(0x3d5, val, &s3);
}

/* As the I/O bus does it: a port without a word handler takes two byte accesses. */
static void
port_w(uint16_t port, uint16_t val)
{
    if (io[port].outw)
        io[port].outw(port, val, &s3);
    else if (io[port].outb) {
        io[port].outb(port, val, &s3);
        if (io[port + 1].outb)
            io[port + 1].outb(port + 1, val >> 8, &s3);
    }
}

static uint16_t
port_r(uint16_t port)
{
    uint16_t val = 0xffff;

    drain();
    if (io[port].inw)
        val = io[port].inw(port, &s3);
    else if (io[port].inb)
        val = io[port].inb(port, &s3) | ((io[port + 1].inb ? io[port + 1].inb(port + 1, &s3) : 0xff) << 8);
    return val;
}

/* A 1024-pixel-wide enhanced mode as a driver sets it, with standard MMIO and open scissors. */
static void
card_setup(const chip_t *c, int bpp)
{
    svga_t *svga = &s3.svga;

    memset(&s3, 0, sizeof(s3));
    memset(vram, 0, sizeof(vram));
    monitor.mon_changeframecount = 2;
    svga->priv                   = &s3;
    svga->monitor                = &monitor;
    svga->vram                   = vram;
    svga->changedvram            = changed;
    svga->vram_max               = VRAM_SIZE;
    svga->decode_mask            = VRAM_SIZE - 1;
    svga->getclock               = test_clock;
    svga->miscout                = 0x01; /* CRTC at 3D4h */
    svga->bpp                    = bpp;
    svga->hdisp_old              = 1024;
    svga->dispend                = 768;
    s3.chip                      = c->chip;
    s3.packed_mmio               = c->packed_mmio;
    s3.vram_mask                 = VRAM_SIZE - 1;
    s3.accel_start               = s3_accel_start;
    s3.wake_fifo_thread          = &wake_event;
    s3.fifo_not_full_event       = &not_full_event;

    memset(io, 0, sizeof(io));
    s3_io_set(&s3);
    crtc_w(0x38, 0x48); /* Unlock the S3 registers */
    crtc_w(0x39, 0xa5);
    crtc_w(0x40, 0x01); /* Enhanced register access */
    crtc_w(0x50, (bpp == 32) ? 0x30 : ((bpp == 16) ? 0x10 : 0x00));
    crtc_w(0x53, 0x10); /* Standard MMIO */
    s3_accel_out(0x4ae8, 0x01, &s3);
    s3_recalctimings(svga);

    port_w(0xbee8, 0x1000); /* SCISSORS_T */
    port_w(0xbee8, 0x2000); /* SCISSORS_L */
    port_w(0xbee8, 0x3000 | 767);
    port_w(0xbee8, 0x4000 | 1023);
    port_w(0xbee8, 0xe000); /* MULT_MISC */
    port_w(0xaae8, 0xffff); /* WRT_MASK */
    if (bpp == 32)
        port_w(0xaae8, 0xffff);
    drain();

    cur_chip = c;
    cur_bpp  = bpp;
}

/* Run a test on every chip at each depth it has; assert it on the chips in `asserted_on`. */
static void
run(const char *name, void (*test)(void), int asserted_on)
{
    static const int depths[] = { 8, 16, 32 };

    test_names[num_tests] = name;
    for (size_t i = 0; i < NUM_CHIPS; i++) {
        char result = '-';

        for (size_t d = 0; d < sizeof(depths) / sizeof(depths[0]); d++) {
            if (depths[d] > chips[i].max_bpp)
                continue;
            card_setup(&chips[i], depths[d]);
            asserted   = !!(asserted_on & chips[i].bit);
            cur_failed = 0;
            test();
            if (cur_failed)
                result = asserted ? 'F' : 'x';
            else if (result == '-')
                result = 'o';
        }
        results[num_tests][i] = result;
    }
    num_tests++;
}

static void
print_matrix(void)
{
    printf("%-24s", "");
    for (size_t i = 0; i < NUM_CHIPS; i++)
        printf(" %-9s", chips[i].name);
    printf("\n");
    for (int t = 0; t < num_tests; t++) {
        printf("%-24s", test_names[t]);
        for (size_t i = 0; i < NUM_CHIPS; i++)
            printf(" %-9c", results[t][i]);
        printf("\n");
    }
    printf("o = passes, F = fails, x = fails on a chip the test isn't asserted on\n");
}

/* CUR_X and CUR_Y (86E8h, 82E8h) are read/write: 86C928 p. 10-5, Vision864 p. 18-4,
   Vision964 p. 18-4, Trio32/64 pp. 18-4 and 18-5, Trio64V+ p. 20-4, S3.TXT lines 844-848. */
static void
test_cur_xy(void)
{
    port_w(0x86e8, 123);
    port_w(0x82e8, 456);
    check(port_r(0x86e8) == 123, "CUR_X reads %u", port_r(0x86e8));
    check(port_r(0x82e8) == 456, "CUR_Y reads %u", port_r(0x82e8));
}

int
main(int argc, char **argv)
{
    verbose = (argc > 1) && !strcmp(argv[1], "-v");

    run("cur_xy", test_cur_xy, C_ALL);

    if (verbose)
        print_matrix();
    if (failures)
        fprintf(stderr, "%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
