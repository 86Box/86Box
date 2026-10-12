/* Exercise the production chipset with the D24xx firmware's I/O sequences. */
#undef NDEBUG
#include <assert.h>
#include "../../src/chipset/tact83000.c"

uint32_t        mem_size;
uint8_t        *ram;
mem_mapping_t   ram_low_mapping, ram_mid_mapping, ram_high_mapping;
int             mem_a20_alt;
static unsigned a20_updates, flushes;
static uint16_t states[8192];

uint8_t
mem_read_ram(uint32_t a, void *p)
{
    (void) p;
    return ram[a];
}
uint16_t
mem_read_ramw(uint32_t a, void *p)
{
    (void) p;
    return ram[a] | (ram[a + 1] << 8);
}
uint32_t
mem_read_raml(uint32_t a, void *p)
{
    return mem_read_ramw(a, p) | ((uint32_t) mem_read_ramw(a + 2, p) << 16);
}
void
mem_write_ram(uint32_t a, uint8_t v, void *p)
{
    (void) p;
    ram[a] = v;
}
void
mem_write_ramw(uint32_t a, uint16_t v, void *p)
{
    mem_write_ram(a, v, p);
    mem_write_ram(a + 1, v >> 8, p);
}
void
mem_write_raml(uint32_t a, uint32_t v, void *p)
{
    mem_write_ramw(a, v, p);
    mem_write_ramw(a + 2, v >> 16, p);
}
void
mem_a20_recalc(void)
{
    a20_updates++;
}
void
flushmmucache(void)
{
    flushes++;
}
void
mem_mapping_disable(mem_mapping_t *m)
{
    m->enable = 0;
}
void
mem_mapping_enable(mem_mapping_t *m)
{
    m->enable = 1;
}
void
mem_mapping_set_exec(mem_mapping_t *m, uint8_t *p)
{
    m->exec = p;
}
void
mem_mapping_add(mem_mapping_t *m, uint32_t b, uint32_t sz,
                uint8_t (*rb)(uint32_t, void *), uint16_t (*rw)(uint32_t, void *), uint32_t (*rl)(uint32_t, void *),
                void (*wb)(uint32_t, uint8_t, void *), void (*ww)(uint32_t, uint16_t, void *), void (*wl)(uint32_t, uint32_t, void *),
                uint8_t *exec, uint32_t flags, void *p)
{
    m->base    = b;
    m->size    = sz;
    m->read_b  = rb;
    m->read_w  = rw;
    m->read_l  = rl;
    m->write_b = wb;
    m->write_w = ww;
    m->write_l = wl;
    m->exec    = exec;
    m->flags   = flags;
    m->priv    = p;
}
void
mem_set_access(uint8_t bitmap, int mode, uint32_t b, uint32_t sz, uint16_t access)
{
    (void) bitmap;
    (void) mode;
    for (uint32_t a = b; a < b + sz; a += 4096) {
        assert(a / 4096 < 8192);
        states[a / 4096] = access;
    }
}
void
io_sethandler(uint16_t b, uint16_t sz,
              uint8_t (*rb)(uint16_t, void *), uint16_t (*rw)(uint16_t, void *), uint32_t (*rl)(uint16_t, void *),
              void (*wb)(uint16_t, uint8_t, void *), void (*ww)(uint16_t, uint16_t, void *), void (*wl)(uint16_t, uint32_t, void *), void *p)
{
    (void) b;
    (void) sz;
    (void) rb;
    (void) rw;
    (void) rl;
    (void) wb;
    (void) ww;
    (void) wl;
    (void) p;
}

static void
reg(tact83000_t *d, uint16_t i, uint16_t v)
{
    tact_outw(0x26, i, d);
    tact_outw(0x24, v, d);
}

int
main(void)
{
    mem_size       = 4096;
    ram            = calloc(mem_size, 1024);
    tact83000_t *d = tact_init(NULL);
    reg(d, 2, 0x8d60);
    /* Bank-size probe: a 2 MB bank wraps at 2 MB, with a distinct second bank. */
    reg(d, 0x8000, 0x8000);
    reg(d, 0x8004, 0x8080);
    tact_writew(0, 0x1234, &d->page[0]);
    assert(tact_readw(0x10000, &d->page[4]) == 0x1234);
    reg(d, 0x8004, 0xa000);
    tact_writew(0x10000, 0x5678, &d->page[4]);
    assert(tact_readw(0, &d->page[0]) == 0x1234);
    assert(ram[0x200000] == 0x78);
    /* An absent bank must not alias a populated bank. */
    reg(d, 0x8008, 0xc000);
    assert(!d->page[8].mapping.enable);
    /* POST maps hidden DRAM through A0000 with the VGA aperture disabled. */
    reg(d, 0x8028, 0x8030);
    assert(!d->page[0x28].mapping.enable);
    reg(d, 2, 0x8c60);
    assert(d->page[0x28].mapping.enable);
    tact_writel(0xa0000, 0x2aad4aad, &d->page[0x28]);
    assert(mem_read_raml(0xc0000, NULL) == 0x2aad4aad);
    reg(d, 2, 0x8d60);
    assert(!d->page[0x28].mapping.enable);
    /* F0000 can be copied while ROM is read, then protected while RAM is read. */
    reg(d, 0x803c, 0x803c);
    reg(d, 7, 0x5500);
    assert(states[0xf0000 / 4096] == (MEM_READ_EXTANY | MEM_WRITE_INTERNAL));
    reg(d, 7, 0xaa00);
    assert(states[0xf0000 / 4096] == (MEM_READ_INTERNAL | MEM_WRITE_DISABLED));
    /* The ATU gate takes effect immediately, independently of KBC commands. */
    reg(d, 0x803, 0xc400);
    assert(mem_a20_alt && a20_updates);
    reg(d, 0x803, 0xc000);
    assert(!mem_a20_alt);
    reg(d, 0x83fc, 0x8000);
    assert(!d->page[64 + (0x3fc - 64) / 4].mapping.enable);
    assert(flushes);
    tact_close(d);
    free(ram);
    /* Every supported SIMM population must produce contiguous RAM. */
    for (unsigned mb = 2; mb <= 16; mb += 2) {
        mem_size = mb * 1024;
        ram      = calloc(mem_size, 1024);
        d        = tact_init(NULL);
        reg(d, 2, 0x8d60);
        unsigned addr = 0;
        for (unsigned bank = 0; bank < 4; bank++) {
            assert(d->bank_size[bank] == 0 || d->bank_size[bank] == (2 << 20) || d->bank_size[bank] == (8 << 20));
            for (unsigned off = 0; off < d->bank_size[bank]; off += 0x10000) {
                reg(d, 0x8000 | (addr >> 14), 0x8000 | (bank << 13) | (addr >> 14));
                unsigned n = addr < 0x100000 ? addr >> 14 : 64 + ((addr - 0x100000) >> 16);
                assert(d->page[n].phys == addr);
                addr += 0x10000;
            }
        }
        assert(addr == mb * 1024 * 1024);
        tact_close(d);
        free(ram);
    }
    puts("TACT83000 bank probes, shadow control, high ROM window and fast A20 passed");
    return 0;
}
