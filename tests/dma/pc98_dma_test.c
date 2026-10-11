/* Exercise production DMA reset, transfers and external PC-98 page carry.
 * Only the host RAM and CPU cycle counter are passive adapters. */
#include <stdlib.h>
#include "../../src/dma.c"

int machine, is_nec, is386, is286 = 1;
cpu_state_t cpu_state;
double cpu_busspeed;
int machine_xt_ibm5550_init(const machine_t *model) { (void)model; abort(); }
int machine_at_wd76c10_init(const machine_t *model) { (void)model; abort(); }
void fatal(const char *fmt, ...) { (void)fmt; abort(); }
const machine_t machines[] = {
    { .bus_flags = MACHINE_PC98 },
    { .bus_flags = MACHINE_AT }
};
static unsigned checks, failures;
static unsigned bus_cycles;
static uint8_t host_ram[0x100000];
void sub_cycles(int n) { bus_cycles += n; }
void cpu_bus_stall(int n) { bus_cycles += n; }
void ibm5140_clock_wake(void) { abort(); }
bool m808x_86box_dma_try_request_ex(unsigned n, void (*cb)(void *), void *p)
{ (void)n; (void)cb; (void)p; abort(); }
#define CHECK(expr) do { checks++; if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); failures++; \
} } while (0)

void io_removehandler(uint16_t base, uint16_t size,
    uint8_t (*rb)(uint16_t, void *), uint16_t (*rw)(uint16_t, void *),
    uint32_t (*rl)(uint16_t, void *), void (*wb)(uint16_t, uint8_t, void *),
    void (*ww)(uint16_t, uint16_t, void *), void (*wl)(uint16_t, uint32_t, void *), void *p)
{ (void)base; (void)size; (void)rb; (void)rw; (void)rl; (void)wb; (void)ww; (void)wl; (void)p; }
void io_sethandler(uint16_t base, uint16_t size,
    uint8_t (*rb)(uint16_t, void *), uint16_t (*rw)(uint16_t, void *),
    uint32_t (*rl)(uint16_t, void *), void (*wb)(uint16_t, uint8_t, void *),
    void (*ww)(uint16_t, uint16_t, void *), void (*wl)(uint16_t, uint32_t, void *), void *p)
{ (void)base; (void)size; (void)rb; (void)rw; (void)rl; (void)wb; (void)ww; (void)wl; (void)p; }

void picint_common(uint16_t mask, int level, int set, uint8_t *p)
{ (void)mask; (void)level; (void)set; (void)p; }
uint8_t mem_readb_phys(uint32_t addr) { if (addr >= sizeof(host_ram)) abort(); return host_ram[addr]; }
void mem_writeb_phys(uint32_t addr, uint8_t value)
{ if (addr >= sizeof(host_ram)) abort(); host_ram[addr] = value; }
void mem_write_phys(void *src, uint32_t addr, int size)
{ (void)src; (void)addr; (void)size; abort(); }
void mem_invalidate_range(uint32_t a, uint32_t b) { (void)a; (void)b; }
void mem_read_phys(void *dest, uint32_t addr, int size)
{ (void)dest; (void)addr; (void)size; abort(); }
bool m808x_86box_active(void) { return false; }
bool m808x_86box_dma_cancel_request_ex(void (*cb)(void *), void *p)
{ (void)cb; (void)p; return false; }
void refreshread(void) { abort(); }
void refreshread_vx0(void) { abort(); }

static void reset_defaults(void)
{
    for (int iteration = 0; iteration < 3; iteration++) {
        machine = 0;
        dma_set_mask(0x00ffffff);
        dma_set_drq(2, 1);
        dma_set_drq(3, 1);
        dma[2].ac = 0x704374;
        dma[3].ac = 0x234567;
        dma_reset();
        CHECK(dma_mask == 0x000fffff);
        CHECK(!dma_get_drq(2) && !dma_get_drq(3));
        CHECK(!dma[2].ac && !dma[3].ac);
        CHECK(!dma[2].page_l && !dma[3].page_l);
        CHECK(!dma_command[0] && !dma_sw_req);
        CHECK(dma_xt8237_active() && dma_e == 0xff && (dma_m & 15) == 15);
    }
    machine = 1;
    dma_reset();
    CHECK(dma_mask == 0x00ffffff);
    CHECK(!dma_xt8237_active());
    /* EISA must retain its extended controller properties on reset. */
    dma_eisa = 1;
    dma_reset();
    CHECK(dma_mask == 0xffffffff && dma_advanced && dma_m == 0xff);
    dma_eisa = 0;
    machine = 0;
    dma_reset();
}

static void page_wrap(void)
{
    for (unsigned mode = 0; mode < 4; mode++) {
        for (unsigned decrement = 0; decrement < 2; decrement++) {
            for (unsigned wide = 0; wide < 2; wide++) {
                dma_set_mask(wide ? 0x00ffffff : 0x000fffff);
                dma_set_pc98_autoinc(2, mode);
                dma_t *d = &dma[2];
                const uint8_t page = decrement ? 0x70 : 0x7f;
                const uint8_t mask = (uint8_t[]){0, 0x0f, 0xf0, 0xff}[mode];
                const uint8_t next = mask ? (uint8_t)((page & ~mask) |
                    ((page + (decrement ? -1 : 1)) & mask)) : page;
                d->page = d->page_l = page;
                d->mode = decrement ? 0x20 : 0;
                d->ac = ((uint32_t)page << 16 & dma_mask) | (decrement ? 0xffff : 0);
                d->ab = ((uint32_t)page << 16 & dma_mask) | 0x1234;
                dma_pc98_adjust_page(2, decrement ? 0 : 0xffff);
                CHECK(d->page_l == next && d->page == next);
                CHECK((d->ac >> 16) == ((uint32_t)next << 16 & dma_mask) >> 16);
                CHECK((d->ab >> 16) == ((uint32_t)next << 16 & dma_mask) >> 16);
                CHECK((d->ab & 0xffff) == 0x1234);
                CHECK((d->ac & 0xffff) == (decrement ? 0xffff : 0));
                uint32_t before = d->ac;
                dma_pc98_adjust_page(2, 0x2345); /* no 64 KiB boundary */
                CHECK(d->ac == before && d->page_l == next);
            }
        }
    }
}

static void transfer_occupancy(void)
{
    machine = 0; is386 = 0;
    dma_reset();
    dma[2].mode = 0x48;
    dma[2].ac = 0x100;
    dma[2].cc = 7;
    host_ram[0x100] = 0x42;
    host_ram[0x101] = 0x17;
    bus_cycles = 0;
    dma_m = 1u << 2;
    CHECK(dma_channel_read(2) == DMA_NODATA && bus_cycles == 0);
    dma_m = 0;
    dma_set_drq(2, 1);
    CHECK(dma_channel_read(2) == 0x42);
    CHECK(bus_cycles == 10 && dma[2].ac == 0x101 && dma[2].cc == 6);
    dma_command[0] = 8;
    CHECK(dma_channel_read_only(2) == 0x17);
    CHECK(bus_cycles == 18 && dma[2].ac == 0x102 && dma[2].cc == 6);
    dma_channel_advance(2);
    CHECK(bus_cycles == 18 && dma[2].cc == 5); /* No double charge. */
    dma[2].mode = 0x44;
    CHECK(dma_channel_write(2, 0xa5) == 0);
    CHECK(host_ram[0x102] == 0xa5 && bus_cycles == 26);
    dma[2].mode = 0x40; /* Verify ignores READY and does not write memory. */
    CHECK(dma_channel_write(2, 0xff) == 0);
    CHECK(!host_ram[0x103] && bus_cycles == 32);
    dma[2].mode = 8;
    CHECK(dma_channel_write(2, 0xff) == DMA_NODATA && bus_cycles == 32);
    machine = 1;
    dma_channel_read(2);
    CHECK(bus_cycles == 32); /* Other machines keep their existing timing. */
    machine = 0;
    is386 = 1;
    dma_channel_read(2);
    CHECK(bus_cycles == 32); /* No stock-286 timing applied to upgrades. */
}

static void address_latch_timing(void)
{
    machine = 0; is386 = 0;
    for (unsigned mode = 0; mode < 3; mode++)
    for (unsigned compressed = 0; compressed < 2; compressed++)
    for (unsigned verify = 0; verify < 2; verify++)
    for (unsigned decrement = 0; decrement < 2; decrement++) {
        dma_reset(); dma_m = 0;
        dma[2].mode = (mode << 6) | (verify ? 0 : 4) | (decrement ? 0x20 : 0);
        dma[2].ac = decrement ? 0x102 : 0xfd;
        dma[2].cc = 8;
        dma_command[0] = compressed ? 8 : 0;
        dma_set_drq(2, 1);
        unsigned total = 0;
        unsigned base = (compressed ? 2 : 3) + !verify;
        for (unsigned i = 0; i < 5; i++) {
            unsigned before = bus_cycles;
            CHECK(dma_channel_write(2, 0xa5) == 0);
            /* Transfer four crosses A7/A8 in either direction. Single
             * reacquires every byte; block/demand keep the address latch. */
            unsigned expected = 2 * (base + (mode == 1 || i == 0 || i == 3));
            CHECK(bus_cycles - before == expected);
            total += expected;
        }
        CHECK(total > 0);
        dma_set_drq(2, 0);
        if (mode == 0) {
            dma_set_drq(2, 1);
            unsigned before = bus_cycles;
            CHECK(dma_channel_write(2, 0xa5) == 0);
            CHECK(bus_cycles - before == 2 * (base + 1));
        }
    }
}

static void precise_controller(void)
{
    machine = 0; is386 = 0;
    dma_init_pc98();
    CHECK(dma_xt8237_active() && dma_e == 0xff && dma_m == 15);
    dma[2].mode = 0x48; dma[2].ac = dma[2].ab = 0x100;
    dma[2].cc = dma[2].cb = 1;
    dma_pc98_write(0x15, 2, NULL);
    CHECK(dma_channel_read(2) == DMA_NODATA); /* No physical DREQ. */
    dma_set_drq(2, 1);
    CHECK((dma_pc98_read(0x11, NULL) & 0xf0) == 0x40);
    dma_pc98_write(0x11, 4, NULL); /* Controller disabled. */
    unsigned before = bus_cycles;
    CHECK(dma_channel_read(2) == DMA_NODATA && bus_cycles == before);
    dma_pc98_write(0x11, 0, NULL);
    CHECK(!(dma_channel_read(2) & DMA_OVER));
    CHECK(dma_channel_read(2) & DMA_OVER);
    CHECK(dma_m & 4);
    CHECK((dma_pc98_read(0x11, NULL) & 0x4f) == 0x44);
    CHECK((dma_pc98_read(0x11, NULL) & 0x4f) == 0x40); /* TC read-clear. */
    CHECK(dma_channel_read(2) == DMA_NODATA);

    /* Auto-init reloads address/count; external EOP also terminates. */
    dma_pc98_write(0x17, 0x5a, NULL);
    dma[2].ac = dma[2].ab = 0x100; dma[2].cc = dma[2].cb = 0;
    dma_pc98_write(0x15, 2, NULL);
    CHECK(dma_channel_read(2) & DMA_OVER);
    CHECK(dma[2].ac == 0x100 && dma[2].cc == 0 && !(dma_m & 4));
    dma[2].cc = dma[2].cb = 9;
    dma_set_eop(2, 1);
    CHECK(dma_channel_read(2) & DMA_OVER);
    CHECK(dma[2].cc == 9 && !dma_xt8237.external_eop);

    /* A split data/count API must not sneak a second byte past TC. */
    dma_pc98_write(0x17, 0x4a, NULL);
    dma[2].cc = 0;
    CHECK(dma_channel_read_only(2) != DMA_NODATA);
    before = bus_cycles;
    CHECK(dma_channel_read_only(2) == DMA_NODATA);
    CHECK(bus_cycles == before && (dma_m & 4));

    /* Demand releases on falling DREQ; block retains ownership until EOP. */
    for (unsigned mode = 0; mode <= 2; mode += 2) {
        dma_reset(); dma_m = 0;
        dma[2].mode = (mode << 6) | 8; dma[2].cc = 3;
        dma_set_drq(2, 1);
        CHECK(dma_channel_read(2) != DMA_NODATA);
        dma_set_drq(2, 0);
        CHECK(!!dma_xt8237.dack == (mode == 2));
        CHECK(dma_channel_readable(2) == (mode == 2));
    }

    /* Software verify follows fixed/rotating arbitration without host I/O. */
    dma_reset(); dma_m = 0;
    dma[1].mode = dma[2].mode = 0x80;
    dma_xt8237.sw_request = 6;
    CHECK(dma_xt8237_priority_pick(6) == 1);
    dma_command[0] = 0x10; dma_xt8237.last_service = 1;
    CHECK(dma_xt8237_priority_pick(6) == 2);
    dma_pc98_write(0x1b, 0, NULL);
    CHECK(dma_xt8237.sw_request == 0 && dma_m == 15 && !dma_xt8237.dack);

    /* Exercise page carry through actual precise-core transfers, not just
     * the external latch helper. Channel zero never becomes refresh DMA. */
    for (unsigned mode = 0; mode < 4; mode++)
    for (unsigned decrement = 0; decrement < 2; decrement++) {
        dma_reset(); dma_m = 0;
        dma_set_pc98_autoinc(2, mode);
        dma[2].mode = 0x44 | (decrement ? 0x20 : 0);
        dma[2].page = dma[2].page_l = decrement ? 0x10 : 0x0f;
        dma[2].ac = decrement ? 0 : 0xfffff;
        dma[2].cc = 1; dma_set_drq(2, 1);
        uint8_t mask = (uint8_t[]){0, 15, 240, 255}[mode];
        uint8_t page = dma[2].page_l;
        uint8_t expected = (page & ~mask) | ((page + (decrement ? -1 : 1)) & mask);
        CHECK(dma_channel_write(2, 0x35) == 0);
        CHECK(dma[2].page_l == expected);
        CHECK(dma[2].ac == (((uint32_t)expected << 16 & dma_mask) |
                            (decrement ? 0xffffu : 0u)));
    }
    machine = 1; dma_init();
    CHECK(!dma_xt8237_active() && dma_e == 255); /* Architecture switch. */
    machine = 0; dma_init_pc98();
}

static void sasi_channel_zero(void)
{
    /* PC-9801-27 uses channel zero for disk DMA, not IBM refresh. Exercise
     * BIOS-style port programming and a full 256-byte DATA IN/OUT block. */
    for (unsigned reset = 0; reset < 2; reset++) {
        machine = 0; is386 = 0;
        if (reset) dma_reset(); else dma_init_pc98();
        CHECK((dma_e & 15) == 15 && (dma_m & 15) == 15);
        dma_pc98_write(0x19, 0, NULL);
        dma_pc98_write(0x01, 0x00, NULL);
        dma_pc98_write(0x01, 0xfc, NULL);
        dma_pc98_write(0x03, 0xff, NULL);
        dma_pc98_write(0x03, 0x00, NULL);
        dma_pc98_write(0x17, 0x44, NULL); /* Board's memory-write cycle. */
        dma_pc98_write(0x15, 0x00, NULL);
        dma[1].mode = 0x89; dma[1].ac = 0x100; dma[1].cc = 9;
        dma_pc98_write(0x15, 0x01, NULL);
        dma_set_drq(1, 1);
        CHECK(dma_channel_read(1) != DMA_NODATA);
        CHECK(dma_xt8237.block_active & 2);
        dma_set_drq(0, 1); /* Effective synchronous DACK cannot lose a byte. */
        bus_cycles = 0;
        for (unsigned i = 0; i < 256; i++) {
            int result = dma_channel_write(0, (uint8_t)(i ^ 0x5a));
            CHECK(result == (i == 255 ? DMA_OVER : 0));
            CHECK(host_ram[0xfc00 + i] == (uint8_t)(i ^ 0x5a));
        }
        CHECK(bus_cycles == 256 * 10); /* No IBM refresh callback/charge. */
        CHECK(dma[0].ac == 0xfd00 && dma[0].cc == -1 && (dma_m & 1));
        CHECK(dma_channel_write(0, 0) == DMA_NODATA);
        CHECK((dma_pc98_read(0x11, NULL) & 1) == 1);
        CHECK((dma_pc98_read(0x11, NULL) & 1) == 0);
        dma_set_drq(0, 0); dma_set_drq(1, 0);
        dma_pc98_write(0x01, 0x00, NULL);
        dma_pc98_write(0x01, 0xfc, NULL);
        dma_pc98_write(0x03, 0xff, NULL);
        dma_pc98_write(0x03, 0x00, NULL);
        dma_pc98_write(0x17, 0x48, NULL);
        dma_pc98_write(0x15, 0x00, NULL);
        dma_set_drq(0, 1);
        for (unsigned i = 0; i < 256; i++)
            CHECK(dma_channel_read(0) == ((i ^ 0x5a) | (i == 255 ? DMA_OVER : 0)));
        dma_set_drq(0, 0);
    }
    machine = 1; dma_init_ibm5140(); dma_reset();
    CHECK(dma_e == 0x0e); /* Retain the IBM-specific channel-zero rule. */
    machine = 0; dma_init_pc98();
}

int main(void)
{
    reset_defaults();
    page_wrap();
    transfer_occupancy();
    address_latch_timing();
    precise_controller();
    sasi_channel_zero();
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
