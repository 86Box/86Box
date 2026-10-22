/* Exercise production Speak/Spark board ports and the real ymfm cores.
 * Host adapters capture only I/O, interrupts, mixing and timer scheduling.
 * No sound BIOS or copyrighted rhythm samples are required. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/sound.h>
#include <86box/timer.h>

static unsigned checks, failures;
#define CHECK(x)                                                    \
    do {                                                            \
        checks++;                                                   \
        if (!(x)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
            failures++;                                             \
        }                                                           \
    } while (0)
int                sound_pos_global;
int                sound_sample_rate = SOUND_FREQ;
uint64_t           tsc, TIMER_USEC = 1ull << 32;
joystick_state_t   joystick_state[GAMEPORT_MAX][MAX_JOYSTICKS];
static int         config_base = 1, config_irq = 3;
static uint16_t    irq_lines;
static pc_timer_t *active_timers[16];
static unsigned    active_count;
static void (*mix)(int32_t *, uint16_t, void *);
static void *mix_priv;
typedef struct port_t {
    uint8_t (*read)(uint16_t, void *);
    void (*write)(uint16_t, uint8_t, void *);
    void *priv;
} port_t;
static port_t ports[65536];

void(pclog)(const char *fmt, ...) { (void) fmt; }
int
device_get_config_int(const char *name)
{
    if (!strcmp(name, "base"))
        return config_base;
    if (!strcmp(name, "irq"))
        return config_irq;
    return 4; /* Sound BIOS disabled. */
}
void
picint_common(uint16_t num, int level, int set, uint8_t *state)
{
    (void) level;
    (void) state;
    if (set)
        irq_lines |= num;
    else
        irq_lines &= ~num;
}
void
sound_add_handler(void (*fn)(int32_t *, uint16_t, void *), void *priv)
{
    mix      = fn;
    mix_priv = priv;
}
int
pc98_rom_find_quiet(const char *const *paths, char *out, size_t len)
{
    (void) paths;
    (void) out;
    (void) len;
    return 0;
}
int
rom_load_linear(const char *fn, uint32_t addr, int sz, int off, uint8_t *ptr)
{
    (void) fn;
    (void) addr;
    (void) sz;
    (void) off;
    (void) ptr;
    return 0;
}
int
rom_init(rom_t *rom, const char *fn, uint32_t addr, int sz, int mask, int off, uint32_t flags)
{
    (void) rom;
    (void) fn;
    (void) addr;
    (void) sz;
    (void) mask;
    (void) off;
    (void) flags;
    return 1;
}
int
rom_init_interleaved(rom_t *rom, const char *fn1, const char *fn2, uint32_t addr, int sz, int mask, int off, uint32_t flags)
{
    (void) fn2;
    return rom_init(rom, fn1, addr, sz, mask, off, flags);
}
void
mem_mapping_disable(mem_mapping_t *m)
{
    (void) m;
}
void
mem_mapping_set_p(mem_mapping_t *m, void *p)
{
    (void) m;
    (void) p;
}
void
mem_mapping_set_exec(mem_mapping_t *m, uint8_t *p)
{
    (void) m;
    (void) p;
}
void
mem_mapping_set_handler(mem_mapping_t *m,
                        uint8_t (*rb)(uint32_t, void *), uint16_t (*rw)(uint32_t, void *), uint32_t (*rl)(uint32_t, void *),
                        void (*wb)(uint32_t, uint8_t, void *), void (*ww)(uint32_t, uint16_t, void *), void (*wl)(uint32_t, uint32_t, void *))
{
    (void) m;
    (void) rb;
    (void) rw;
    (void) rl;
    (void) wb;
    (void) ww;
    (void) wl;
}
void
io_sethandler(uint16_t base, uint16_t size,
              uint8_t (*rb)(uint16_t, void *), uint16_t (*rw)(uint16_t, void *), uint32_t (*rl)(uint16_t, void *),
              void (*wb)(uint16_t, uint8_t, void *), void (*ww)(uint16_t, uint16_t, void *), void (*wl)(uint16_t, uint32_t, void *), void *priv)
{
    (void) rw;
    (void) rl;
    (void) ww;
    (void) wl;
    for (unsigned p = base; p < base + size; p++) {
        CHECK(!ports[p].priv);
        ports[p] = (port_t) { rb, wb, priv };
    }
}
void
io_removehandler(uint16_t base, uint16_t size,
                 uint8_t (*rb)(uint16_t, void *), uint16_t (*rw)(uint16_t, void *), uint32_t (*rl)(uint16_t, void *),
                 void (*wb)(uint16_t, uint8_t, void *), void (*ww)(uint16_t, uint16_t, void *), void (*wl)(uint16_t, uint32_t, void *), void *priv)
{
    (void) rb;
    (void) rw;
    (void) rl;
    (void) wb;
    (void) ww;
    (void) wl;
    for (unsigned p = base; p < base + size; p++) {
        CHECK(ports[p].priv == priv);
        ports[p] = (port_t) { 0 };
    }
}
uint8_t
inb(uint16_t p)
{
    return ports[p].read ? ports[p].read(p, ports[p].priv) : 0xff;
}
void
outb(uint16_t p, uint8_t v)
{
    if (ports[p].write)
        ports[p].write(p, v, ports[p].priv);
}
void
timer_add(pc_timer_t *t, void (*cb)(void *), void *p, int start)
{
    memset(t, 0, sizeof(*t));
    t->callback = cb;
    t->priv     = p;
    CHECK(!start);
}
void
timer_disable(pc_timer_t *t)
{
    for (unsigned n = 0; n < active_count; n++)
        if (active_timers[n] == t) {
            active_timers[n] = active_timers[--active_count];
            break;
        }
    t->flags &= ~TIMER_ENABLED;
}
void
timer_enable(pc_timer_t *t)
{
    if (t->flags & TIMER_ENABLED)
        return;
    CHECK(active_count < 16);
    active_timers[active_count++] = t;
    t->flags |= TIMER_ENABLED;
}
void
timer_stop(pc_timer_t *t)
{
    timer_disable(t);
}
void
timer_on_auto(pc_timer_t *t, double us)
{
    timer_disable(t);
    timer_set_delay_u64(t, (uint64_t) (us * TIMER_USEC));
}
static void
advance(unsigned us)
{
    uint64_t end = tsc + us;
    for (;;) {
        pc_timer_t *next = NULL;
        for (unsigned n = 0; n < active_count; n++)
            if (active_timers[n]->ts_integer <= end && (!next || active_timers[n]->ts_integer < next->ts_integer))
                next = active_timers[n];
        if (!next)
            break;
        tsc = next->ts_integer;
        timer_disable(next);
        next->callback(next->priv);
    }
    tsc = end;
}
static void
reg(uint16_t base, unsigned bank, uint8_t addr, uint8_t value)
{
    outb(base + bank * 4, addr);
    outb(base + bank * 4 + 2, value);
}
static uint8_t
read_reg(uint16_t base, unsigned bank, uint8_t addr)
{
    outb(base + bank * 4, addr);
    return inb(base + bank * 4 + 2);
}

static void
test_speak(unsigned base_setting, unsigned irq_setting)
{
    config_base         = base_setting;
    config_irq          = irq_setting;
    const uint16_t base = base_setting ? 0x188 : 0x88;
    void          *dev  = pc98_speakboard_device.init(&pc98_speakboard_device);
    CHECK(dev != NULL);
    if (!dev)
        return;
    CHECK(inb(0xa460) == 0xff && inb(base + 0x400) == 0xff);
    CHECK(inb(base + 1) == 0xff && inb(base + 7) == 0xff);
    reg(base, 0, 0, 0x55);
    outb(base + 1, 7);
    outb(base + 3, 0x22);
    CHECK(read_reg(base, 0, 0) == 0x55); /* Odd addresses did not change PSG. */
    reg(base, 0, 7, 0x80);               /* Port A input, Port B output. */
    reg(base, 0, 15, 0);
    memset(joystick_state, 0, sizeof(joystick_state));
    CHECK(read_reg(base, 0, 14) == (0x3f | (irq_setting << 6)));
    joystick_state[0][0].plat_joystick_nr = 1;
    joystick_state[0][0].axis[1]          = -32768;
    joystick_state[0][0].button[0]        = 1;
    CHECK(read_reg(base, 0, 14) == (0x2e | (irq_setting << 6)));
    reg(base, 0, 15, 0x40);
    CHECK(read_reg(base, 0, 14) == (0x3f | (irq_setting << 6)));
    /* RAM transfers exercise extended ports immediately after reset. */
    reg(base, 1, 1, 0x02); /* Eight-bit DRAM. */
    reg(base, 1, 4, 0xff);
    reg(base, 1, 5, 0xff);
    /* Separate patterns every 64 KiB catch missing RAM or undersized RAM
     * aliased across the board's 256 KiB sample address space. */
    for (unsigned bank = 0; bank < 4; bank++) {
        reg(base, 1, 2, 0);
        reg(base, 1, 3, bank * 8);
        reg(base, 1, 0, 0x60); /* CPU write to external RAM. */
        for (unsigned n = 0; n < 32; n++)
            reg(base, 1, 8, 0x31 + bank * 32 + n);
    }
    pc98_speakboard_device.reset(dev); /* Reset preserves external sample RAM. */
    reg(base, 1, 1, 0x02);
    reg(base, 1, 4, 0xff);
    reg(base, 1, 5, 0xff);
    for (unsigned bank = 0; bank < 4; bank++) {
        reg(base, 1, 2, 0);
        reg(base, 1, 3, bank * 8);
        reg(base, 1, 0, 0x20); /* CPU read; two dummy reads. */
        (void) read_reg(base, 1, 8);
        (void) read_reg(base, 1, 8);
        for (unsigned n = 0; n < 32; n++)
            CHECK(read_reg(base, 1, 8) == 0x31 + bank * 32 + n);
    }
    /* Channel 4 belongs to the second FM register bank. It must be audible
     * without any PC-9801-86 A460h enable sequence. */
    for (unsigned op = 0; op < 4; op++) {
        reg(base, 1, 0x30 + op * 4, 1);
        reg(base, 1, 0x40 + op * 4, 0);
        reg(base, 1, 0x50 + op * 4, 0x1f);
        reg(base, 1, 0x80 + op * 4, 0x0f);
    }
    reg(base, 1, 0xb0, 7);
    reg(base, 1, 0xb4, 0xc0);
    reg(base, 1, 0xa4, 0x22);
    reg(base, 1, 0xa0, 0x69);
    reg(base, 0, 0x28, 0xf4);
    int32_t buffer[1024] = { 0 };
    mix(buffer, 512, mix_priv);
    int peak = 0;
    for (unsigned n = 0; n < 1024; n++)
        if (abs(buffer[n]) > peak)
            peak = abs(buffer[n]);
    CHECK(peak > 100);
    reg(base, 0, 0x24, 0xff);
    reg(base, 0, 0x25, 3);
    reg(base, 0, 0x27, 5); /* Start Timer A + enable Timer A IRQ. */
    advance(200);
    static const unsigned irqs[4] = { 3, 13, 10, 12 };
    CHECK(irq_lines == (1u << irqs[irq_setting]));
    CHECK(inb(base) & 1u);
    CHECK((inb(base) & 0x1f) == (inb(base + 4) & 0x1f));
    pc98_speakboard_device.reset(dev);
    CHECK(irq_lines == 0);
    CHECK(!(inb(base) & 1u));
    pc98_speakboard_device.close(dev);
    CHECK(active_count == 0);
    CHECK(!ports[base].priv);
}
static void
test_spark(void)
{
    config_base = 1;
    config_irq  = 3;
    void *dev   = pc98_sparkboard_device.init(&pc98_sparkboard_device);
    CHECK(dev != NULL);
    if (!dev)
        return;
    CHECK(ports[0x588].priv == dev);
    CHECK(inb(0x589) == 0xff);
    /* The YM3438 DAC proves the second core reaches the stereo mixer and
     * catches accidental accesses to a nonexistent third (SSG) output. */
    reg(0x588, 1, 0xb6, 0xc0); /* DAC channel left + right. */
    reg(0x588, 0, 0x2a, 0xe0);
    reg(0x588, 0, 0x2b, 0x80);
    int32_t buffer[512 * 2] = { 0 };
    mix(buffer, 512, mix_priv);
    int peak = 0;
    for (unsigned n = 0; n < 1024; n++)
        if (abs(buffer[n]) > peak)
            peak = abs(buffer[n]);
    CHECK(peak > 100);
    CHECK(inb(0xa460) == 0xff);
    pc98_sparkboard_device.reset(dev);
    memset(buffer, 0, sizeof(buffer));
    mix(buffer, 512, mix_priv);
    peak = 0;
    for (unsigned n = 0; n < 1024; n++)
        if (abs(buffer[n]) > peak)
            peak = abs(buffer[n]);
    CHECK(peak == 0);
    pc98_sparkboard_device.close(dev);
    CHECK(!ports[0x188].priv && !ports[0x588].priv);
    CHECK(active_count == 0);
}
static void
test_26k(void)
{
    config_base = 1;
    config_irq  = 3;
    void *dev   = pc98_26k_device.init(&pc98_26k_device);
    CHECK(dev != NULL);
    if (!dev)
        return;
    memset(joystick_state, 0, sizeof(joystick_state));
    reg(0x188, 0, 7, 0x80);
    reg(0x188, 0, 15, 0);
    CHECK(read_reg(0x188, 0, 14) == 0xff);
    joystick_state[0][0].plat_joystick_nr = 1;
    joystick_state[0][0].axis[0]          = 32767;
    joystick_state[0][0].button[1]        = 1;
    CHECK(read_reg(0x188, 0, 14) == 0xd7);
    reg(0x188, 0, 15, 0x40);
    CHECK(read_reg(0x188, 0, 14) == 0xff);
    CHECK(!ports[0x18c].priv);
    pc98_26k_device.close(dev);
    CHECK(active_count == 0 && !ports[0x188].priv);
}
int
main(void)
{
    for (unsigned base = 0; base < 2; base++)
        for (unsigned irq = 0; irq < 4; irq++)
            test_speak(base, irq);
    test_spark();
    test_26k();
    printf("PC-98 sound: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
