/*
 * 86Box       The PC Card controller (src/pcmcia/pcic_pd6722.c) against a
 *             fake card, fake I/O space, memory map and PIC: what a socket
 *             services driver does -- identify the chip, find the card, power
 *             it, open a memory window onto its attribute memory and an I/O
 *             window onto its ports, and steer its interrupt.
 *
 *             No test framework; returns non-zero on failure.
 *
 * Authors:    Marcos Alves
 *
 *             Copyright 2026 Marcos Alves.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/pcmcia.h>
#include <86box/plat_unused.h>

static int checks, failures;
#define CHECK(c, ...)                                     \
    do {                                                  \
        checks++;                                         \
        if (!(c)) {                                       \
            failures++;                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)

/* ---- the platform ---------------------------------------------------- */

void pclog_ex(UNUSED(const char *f), UNUSED(va_list a)) { }
void pclog(UNUSED(const char *f), ...) { }

static int inits;
void *
device_add(const device_t *d)
{
    inits++;
    return d->init(d);
}

/* I/O space: one handler set per port. */
static struct {
    uint8_t (*inb)(uint16_t, void *);
    void (*outb)(uint16_t, uint8_t, void *);
    void *priv;
} port[0x10000];

void
io_sethandler(uint16_t base, uint16_t size, uint8_t (*inb)(uint16_t, void *), UNUSED(uint16_t (*inw)(uint16_t, void *)),
              UNUSED(uint32_t (*inl)(uint16_t, void *)), void (*outb)(uint16_t, uint8_t, void *),
              UNUSED(void (*outw)(uint16_t, uint16_t, void *)), UNUSED(void (*outl)(uint16_t, uint32_t, void *)), void *priv)
{
    for (int i = 0; i < size; i++) {
        port[base + i].inb  = inb;
        port[base + i].outb = outb;
        port[base + i].priv = priv;
    }
}

void
io_removehandler(uint16_t base, uint16_t size, UNUSED(uint8_t (*inb)(uint16_t, void *)), UNUSED(uint16_t (*inw)(uint16_t, void *)),
                 UNUSED(uint32_t (*inl)(uint16_t, void *)), UNUSED(void (*outb)(uint16_t, uint8_t, void *)),
                 UNUSED(void (*outw)(uint16_t, uint16_t, void *)), UNUSED(void (*outl)(uint16_t, uint32_t, void *)), UNUSED(void *priv))
{
    for (int i = 0; i < size; i++)
        memset(&port[base + i], 0, sizeof(port[0]));
}

uint8_t
inb(uint16_t p)
{
    return port[p].inb ? port[p].inb(p, port[p].priv) : 0xff;
}

void
outb(uint16_t p, uint8_t v)
{
    if (port[p].outb)
        port[p].outb(p, v, port[p].priv);
}

/* The memory map: the mappings the controller made. */
static mem_mapping_t *maps[16];
static int            n_maps;

void
mem_mapping_add(mem_mapping_t *m, uint32_t base, uint32_t size, uint8_t (*rb)(uint32_t, void *), uint16_t (*rw)(uint32_t, void *),
                uint32_t (*rl)(uint32_t, void *), void (*wb)(uint32_t, uint8_t, void *), void (*ww)(uint32_t, uint16_t, void *),
                void (*wl)(uint32_t, uint32_t, void *), uint8_t *exec, uint32_t flags, void *priv)
{
    memset(m, 0, sizeof(*m));
    m->base    = base;
    m->size    = size;
    m->read_b  = rb;
    m->read_w  = rw;
    m->read_l  = rl;
    m->write_b = wb;
    m->write_w = ww;
    m->write_l = wl;
    m->exec    = exec;
    m->flags   = flags;
    m->priv    = priv;
    m->enable  = size > 0;
    maps[n_maps++] = m;
}

void
mem_mapping_set_addr(mem_mapping_t *m, uint32_t base, uint32_t size)
{
    m->base   = base;
    m->size   = size;
    m->enable = 1;
}

void mem_mapping_disable(mem_mapping_t *m) { m->enable = 0; }

static mem_mapping_t *
map_at(uint32_t a)
{
    for (int i = 0; i < n_maps; i++)
        if (maps[i]->enable && (a >= maps[i]->base) && (a - maps[i]->base < maps[i]->size))
            return maps[i];
    return NULL;
}

static uint8_t
memrb(uint32_t a)
{
    mem_mapping_t *m = map_at(a);
    return m ? m->read_b(a, m->priv) : 0xff;
}

static void
memwb(uint32_t a, uint8_t v)
{
    mem_mapping_t *m = map_at(a);
    if (m)
        m->write_b(a, v, m->priv);
}

/* The PIC: which IRQs are raised. */
static int irq_up[16];

void
picint_common(uint16_t num, UNUSED(int level), int set, UNUSED(uint8_t *st))
{
    for (int i = 0; i < 16; i++)
        if (num & (1 << i))
            irq_up[i] = set;
}

/* The controller's poll timer: run by hand. */
static void (*poll_cb)(void *);
static void *poll_priv;

void
timer_add(pc_timer_t *t, void (*cb)(void *), void *priv, UNUSED(int start))
{
    (void) t;
    poll_cb   = cb;
    poll_priv = priv;
}
void timer_on_auto(UNUSED(pc_timer_t *t), UNUSED(double period)) { }
void timer_stop(UNUSED(pc_timer_t *t)) { }

void pcmcia_slots_poll(void) { }   /* pcmcia.c's hot-plug requests: none here */

static void
poll(void)
{
    poll_cb(poll_priv);
}

/* ---- the card -------------------------------------------------------- */

static int     card_resets;
static uint8_t card_attr[0x1000];
static uint8_t card_io_last;
static int     card_io_port;

static uint8_t card_attr_read(uint32_t a, UNUSED(void *p)) { return (a < sizeof(card_attr)) ? card_attr[a] : 0xff; }
static void    card_attr_write(uint32_t a, uint8_t v, UNUSED(void *p)) { if (a < sizeof(card_attr)) card_attr[a] = v; }
static uint8_t card_io_read(uint16_t p, UNUSED(void *x)) { card_io_port = p; return 0x5a; }
static void    card_io_write(uint16_t p, uint8_t v, UNUSED(void *x)) { card_io_port = p; card_io_last = v; }
static void    card_reset(UNUSED(void *p)) { card_resets++; }

/* Common memory: 8 KB, as on a memory card. */
static uint8_t card_common[0x2000];
static uint8_t card_common_read(uint32_t a, UNUSED(void *p)) { return (a < sizeof(card_common)) ? card_common[a] : 0xff; }
static void    card_common_write(uint32_t a, uint8_t v, UNUSED(void *p)) { if (a < sizeof(card_common)) card_common[a] = v; }

static const pccard_t card = {
    .name        = "test card",
    .attr_read   = card_attr_read,
    .common_read  = card_common_read,
    .common_write = card_common_write,
    .attr_write = card_attr_write,
    .io_read    = card_io_read,
    .io_write   = card_io_write,
    .reset      = card_reset,
};

/* ---- the controller, as a driver sees it ------------------------------ */

static uint8_t
reg(int s, int r)
{
    outb(0x3e0, (s << 6) | r);
    return inb(0x3e1);
}

static void
setreg(int s, int r, uint8_t v)
{
    outb(0x3e0, (s << 6) | r);
    outb(0x3e1, v);
}

static void
test_identify(void)
{
    CHECK(reg(0, 0x00) == 0x83, "revision 0x83 (%02X)", reg(0, 0x00));
    setreg(0, 0x1f, 0);
    uint8_t a = reg(0, 0x1f), b = reg(0, 0x1f);
    CHECK(((a & 0xc0) == 0xc0) && ((b & 0xc0) == 0x00), "Cirrus signature toggles (%02X %02X)", a, b);
    CHECK(a & 0x20, "two sockets");
    CHECK(reg(2, 0x00) == 0xff, "no socket C");
    setreg(0, 0x2e, 0x0a);
    CHECK(reg(0, 0x2e) == 0x0a, "extended index reads back");
}

static void
test_card_detect(void)
{
    CHECK((reg(0, 0x01) & 0x0c) == 0x0c, "socket A: card detected (%02X)", reg(0, 0x01));
    CHECK(reg(1, 0x01) == 0x00, "socket B: empty");
}

static void
test_power(void)
{
    int before = card_resets;

    setreg(0, 0x02, 0x90);              /* VCC on, outputs enabled */
    CHECK(!(reg(0, 0x01) & 0x20), "still in RESET: not ready");
    setreg(0, 0x03, 0x40 | 0x20 | 5);   /* out of RESET, I/O card, IRQ 5 */
    CHECK(card_resets == before + 1, "going live resets the card");
    CHECK((reg(0, 0x01) & 0x60) == 0x60, "powered and ready (%02X)", reg(0, 0x01));
}

static void
test_memory_window(void)
{
    /* Window 0: host D0000-D0FFF onto attribute memory from card address 0. */
    const uint16_t off = (0x4000000 - 0xd0000) >> 12;

    card_attr[0] = 0x01;   /* CISTPL_DEVICE */
    card_attr[2] = 0x03;
    setreg(0, 0x10, 0xd0);
    setreg(0, 0x11, 0x00);
    setreg(0, 0x12, 0xd0);
    setreg(0, 0x13, 0x00);
    setreg(0, 0x14, off & 0xff);
    setreg(0, 0x15, ((off >> 8) & 0x3f) | 0x40);
    CHECK(memrb(0xd0000) == 0xff, "window closed until enabled");
    setreg(0, 0x06, 0x01);
    CHECK(memrb(0xd0000) == 0x01 && memrb(0xd0002) == 0x03, "the CIS through the window (%02X %02X)", memrb(0xd0000), memrb(0xd0002));
    memwb(0xd03c0, 0x21);
    CHECK(card_attr[0x3c0] == 0x21, "a write reaches the card's COR");
    setreg(0, 0x15, ((off >> 8) & 0x3f) | 0x40 | 0x80);
    memwb(0xd03c0, 0x00);
    CHECK(card_attr[0x3c0] == 0x21, "a write-protected window does not write");
}

static void
test_io_window(void)
{
    setreg(0, 0x08, 0x00);   /* 0x300-0x31F */
    setreg(0, 0x09, 0x03);
    setreg(0, 0x0a, 0x1f);
    setreg(0, 0x0b, 0x03);
    CHECK(inb(0x300) == 0xff, "I/O window closed until enabled");
    setreg(0, 0x06, 0x41);
    CHECK(inb(0x310) == 0x5a && card_io_port == 0x310, "the card's ports at 0x300 (%04X)", card_io_port);
    outb(0x31f, 0x77);
    CHECK(card_io_last == 0x77, "writes reach the card");
    setreg(0, 0x06, 0x01);
    CHECK(inb(0x310) == 0xff, "closing the window unmaps the ports");
    setreg(0, 0x06, 0x41);
}

static void
test_card_irq(void)
{
    pcmcia_card_irq(0, 1);
    CHECK(irq_up[5], "the card's IREQ on IRQ 5");
    pcmcia_card_irq(0, 0);
    CHECK(!irq_up[5], "and released");
    pcmcia_card_irq(0, 1);
    setreg(0, 0x03, 0x40 | 0x20 | 9);
    CHECK(!irq_up[5] && irq_up[9], "steered to IRQ 9 when the driver says so");
    setreg(0, 0x03, 0x40 | 9);
    CHECK(!irq_up[9], "a memory-only card raises no IRQ");
    pcmcia_card_irq(0, 0);
    setreg(0, 0x03, 0x40 | 0x20 | 5);
}

static void
test_irq_probe(void)
{
    /* Linux i82365: status-change IRQ 3, software interrupt. */
    setreg(0, 0x05, 0x30);
    setreg(0, 0x16, 0x20);
    CHECK(irq_up[3], "the software interrupt raises IRQ 3");
    CHECK(reg(0, 0x04) & 0x08, "as a card detect change");
    CHECK(!irq_up[3] && reg(0, 0x04) == 0, "reading the change clears it");
    setreg(0, 0x05, 0x00);
}

static void
test_power_off(void)
{
    int before = card_resets;

    setreg(0, 0x02, 0x00);
    CHECK(card_resets == before + 1, "power-off resets the card");
    CHECK(memrb(0xd0000) == 0xff && inb(0x310) == 0xff, "an unpowered card does not answer");
}

static void
test_common_memory(void)
{
    /* Window 1: host D2000-D3FFF onto common memory from card address 0. */
    const uint16_t off = (0x4000000 - 0xd2000) >> 12;

    setreg(0, 0x18, 0xd2);
    setreg(0, 0x19, 0x00);
    setreg(0, 0x1a, 0xd3);
    setreg(0, 0x1b, 0x00);
    setreg(0, 0x1c, off & 0xff);
    setreg(0, 0x1d, (off >> 8) & 0x3f);   /* REG clear: common memory */
    setreg(0, 0x06, 0x43);
    card_common[0x10] = 0x77;
    CHECK(memrb(0xd2010) == 0x77, "common memory through a window (%02X)", memrb(0xd2010));
    memwb(0xd3abc, 0x42);
    CHECK(card_common[0x1abc] == 0x42, "a write reaches the card's common memory");
    CHECK(card_attr[0x1abc] != 0x42, "and not its attribute memory");
    CHECK(memrb(0xd0000) == card_attr[0], "the attribute window beside it still reads the CIS");
    setreg(0, 0x1d, ((off >> 8) & 0x3f) | 0x80);
    memwb(0xd2010, 0x00);
    CHECK(card_common[0x10] == 0x77, "a write-protected common window does not write");
}

/* Card detect changes: a card hot-unplugged and plugged back in,
   each socket with its own status changes, the two sharing an IRQ. */
static int     b_resets;
static uint8_t b_attr_read(UNUSED(uint32_t a), UNUSED(void *p)) { return 0x01; }
static void    b_reset(UNUSED(void *p)) { b_resets++; }
static const pccard_t card_b = { .name = "card B", .attr_read = b_attr_read, .reset = b_reset };

static void
test_card_events(void)
{
    poll();   /* the controller is running: changes are reported from now on */
    setreg(0, 0x05, 0xb8);   /* socket A: card detect changes on IRQ 11 */
    setreg(1, 0x05, 0xb8);   /* socket B: the same IRQ */
    CHECK(!irq_up[11], "no change, no interrupt");

    pcmcia_insert(0, NULL);   /* hot-unplugged */
    CHECK(!(reg(0, 0x01) & 0x0c), "pulled out: socket A's card detect goes");
    CHECK(memrb(0xd2010) == 0xff, "and its windows stop answering");
    CHECK(irq_up[11], "a card detect change interrupt");

    pcmcia_insert(1, &card_b);   /* a card into socket B as well */
    CHECK((reg(1, 0x01) & 0x0c) == 0x0c, "socket B holds a card");
    CHECK(reg(0, 0x04) == 0x08, "socket A: card detect changed");
    CHECK(irq_up[11], "socket B's change still holds the shared IRQ after A's is read");
    CHECK(reg(0, 0x04) == 0x00, "reading A's status change cleared only A's");
    CHECK(reg(1, 0x04) == 0x08, "socket B: card detect changed");
    CHECK(!irq_up[11], "both read: the IRQ drops");

    pcmcia_insert(0, &card);   /* hot-plugged */
    CHECK((reg(0, 0x01) & 0x0c) == 0x0c, "put back: socket A's card detect returns");
    CHECK(irq_up[11] && (reg(0, 0x04) == 0x08), "with a card detect change");
    CHECK(!irq_up[11], "read: down again");

    setreg(0, 0x05, 0xb0);   /* A: IRQ 11, no changes enabled */
    pcmcia_insert(0, NULL);
    CHECK(!irq_up[11] && (reg(0, 0x04) == 0x08), "a change not enabled is latched without an interrupt");
    pcmcia_insert(0, &card);
    reg(0, 0x04);

    /* Software interrupts are per socket too. */
    setreg(0, 0x05, 0x50);
    setreg(1, 0x05, 0x70);
    setreg(1, 0x16, 0x20);
    CHECK(irq_up[7] && !irq_up[5], "socket B's software interrupt on B's IRQ, 7");
    CHECK(reg(0, 0x04) == 0x00, "and nothing on socket A");
    reg(1, 0x04);
    CHECK(!irq_up[7], "B's cleared by reading B's status change");
    setreg(0, 0x05, 0x00);
    setreg(1, 0x05, 0x00);
    pcmcia_insert(1, NULL);
    reg(1, 0x04);
}

static void
test_unplug(void)
{
    setreg(0, 0x02, 0x90);
    pcmcia_insert(0, NULL);
    CHECK(reg(0, 0x01) == 0x00, "pulled out: no card detect");
    CHECK(inb(0x310) == 0xff, "and no ports");
}

int
main(void)
{
    /* A card plugged in before the controller exists, as at a hard reset. */
    pcmcia_insert(0, &card);
    pcmcia_controller_add();
    pcmcia_controller_add();   /* an I/O board asking for it too */
    CHECK(inits == 1, "one controller (%d)", inits);

    test_identify();
    test_card_detect();
    test_power();
    test_memory_window();
    test_io_window();
    test_card_irq();
    test_irq_probe();
    test_common_memory();
    test_power_off();
    test_card_events();
    test_unplug();

    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
