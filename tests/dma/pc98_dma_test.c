/* Exercise production DMA reset and the external PC-98 page carry logic.
 * No transfer data or controller completions are synthesized. */
#include <stdlib.h>
#include "../../src/dma.c"

int machine, is_nec, is286 = 1;
const machine_t machines[] = {
    { .bus_flags = MACHINE_PC98 },
    { .bus_flags = MACHINE_AT }
};
static unsigned checks, failures;
#define CHECK(expr) do { checks++; if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); failures++; \
} } while (0)

void io_removehandler(uint16_t base, uint16_t size,
    uint8_t (*rb)(uint16_t, void *), uint16_t (*rw)(uint16_t, void *),
    uint32_t (*rl)(uint16_t, void *), void (*wb)(uint16_t, uint8_t, void *),
    void (*ww)(uint16_t, uint16_t, void *), void (*wl)(uint16_t, uint32_t, void *), void *p)
{ (void)base; (void)size; (void)rb; (void)rw; (void)rl; (void)wb; (void)ww; (void)wl; (void)p; }

void picint_common(uint16_t mask, int level, int set, uint8_t *p)
{ (void)mask; (void)level; (void)set; (void)p; }
uint8_t mem_readb_phys(uint32_t addr) { (void)addr; abort(); }
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
    }
    machine = 1;
    dma_reset();
    CHECK(dma_mask == 0x00ffffff);
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

int main(void)
{
    reset_defaults();
    page_wrap();
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
