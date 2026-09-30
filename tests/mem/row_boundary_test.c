/* A DRAM row must change the memory state only for its own address range. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Include the implementation so the test can arrange a row without chipset
   setup, while still exercising the production row_allocate path. */
#include "../../src/mem/row.c"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

page_t *pages;
uint32_t pages_sz;
uint32_t mem_size;
uint8_t *ram;
uint8_t page_ff[4096];
mem_mapping_t ram_low_mapping, ram_mid_mapping, ram_high_mapping;
uint32_t mem_logical_addr;
#ifdef USE_NEW_DYNAREC
uint64_t *byte_dirty_mask, *byte_code_present_mask;
#endif

void addreadlookup(uint32_t virt, uint32_t phys) { (void)virt; (void)phys; }
void addwritelookup(uint32_t virt, uint32_t phys) { (void)virt; (void)phys; }
uint8_t mem_read_ram(uint32_t addr, void *priv) { (void)addr; (void)priv; return 0; }
uint16_t mem_read_ramw(uint32_t addr, void *priv) { (void)addr; (void)priv; return 0; }
uint32_t mem_read_raml(uint32_t addr, void *priv) { (void)addr; (void)priv; return 0; }
void mem_write_ram(uint32_t addr, uint8_t val, void *priv) { (void)addr; (void)val; (void)priv; }
void mem_write_ramw(uint32_t addr, uint16_t val, void *priv) { (void)addr; (void)val; (void)priv; }
void mem_write_raml(uint32_t addr, uint32_t val, void *priv) { (void)addr; (void)val; (void)priv; }
void mem_write_ramb_page(uint32_t addr, uint8_t val, page_t *page) { (void)addr; (void)val; (void)page; }
void mem_write_ramw_page(uint32_t addr, uint16_t val, page_t *page) { (void)addr; (void)val; (void)page; }
void mem_write_raml_page(uint32_t addr, uint32_t val, page_t *page) { (void)addr; (void)val; (void)page; }

static uint32_t access_base, access_size;
static uint16_t access_mode;
static unsigned access_calls;

void mem_set_access(uint8_t bitmap, int mode, uint32_t base, uint32_t size, uint16_t access)
{
    if (bitmap != ACCESS_ALL || mode != 0) {
        fprintf(stderr, "unexpected memory access mode\n");
        exit(1);
    }
    access_base = base;
    access_size = size;
    access_mode = access;
    access_calls++;
}

/* Referenced only by row_init/row_reset paths the test does not run; defined
   so the test links without relying on linker dead-stripping (lld-link, MSVC). */
void spd_write_drbs(uint8_t *regs, uint8_t reg_min, uint8_t reg_max, uint8_t drb_unit)
{
    (void)regs; (void)reg_min; (void)reg_max; (void)drb_unit;
}
void flushmmucache(void) { }
void mem_mapping_add(mem_mapping_t *mapping, uint32_t base, uint32_t size,
                     uint8_t (*rb)(uint32_t, void *), uint16_t (*rw)(uint32_t, void *),
                     uint32_t (*rl)(uint32_t, void *), void (*wb)(uint32_t, uint8_t, void *),
                     void (*ww)(uint32_t, uint16_t, void *), void (*wl)(uint32_t, uint32_t, void *),
                     uint8_t *exec, uint32_t flags, void *priv)
{
    (void)mapping; (void)base; (void)size; (void)rb; (void)rw; (void)rl;
    (void)wb; (void)ww; (void)wl; (void)exec; (void)flags; (void)priv;
}

void mem_mapping_set_addr(mem_mapping_t *mapping, uint32_t base, uint32_t size) { (void)mapping; (void)base; (void)size; }
void mem_mapping_set_exec(mem_mapping_t *mapping, uint8_t *exec) { (void)mapping; (void)exec; }
void mem_mapping_set_mask(mem_mapping_t *mapping, uint32_t mask) { (void)mapping; (void)mask; }
void mem_mapping_disable(mem_mapping_t *mapping) { (void)mapping; }
void mem_mapping_set_handler(mem_mapping_t *mapping,
                             uint8_t (*rb)(uint32_t, void *), uint16_t (*rw)(uint32_t, void *),
                             uint32_t (*rl)(uint32_t, void *), void (*wb)(uint32_t, uint8_t, void *),
                             void (*ww)(uint32_t, uint16_t, void *), void (*wl)(uint32_t, uint32_t, void *))
{
    (void)mapping; (void)rb; (void)rw; (void)rl; (void)wb; (void)ww; (void)wl;
}

int main(void)
{
    const uint32_t mib = 1024 * 1024;
    uint8_t *memory = calloc(4, mib);
    pages = calloc(4 * mib / 4096, sizeof(*pages));
    CHECK(memory && pages);
#ifdef USE_NEW_DYNAREC
    byte_dirty_mask = calloc(4 * mib / 4096 * 64, sizeof(*byte_dirty_mask));
    byte_code_present_mask = calloc(4 * mib / 4096 * 64, sizeof(*byte_code_present_mask));
    CHECK(byte_dirty_mask && byte_code_present_mask);
#endif

    row_t row = { 0 };
    row.buf = memory;
    row.host_base = 2 * mib;
    row.host_size = mib;
    row.ram_base = 2 * mib;
    row.ram_size = mib;
    row.ram_mask = mib - 1;
    rows = &row;

    row_allocate(0, 1);

    CHECK(access_calls == 1);
    CHECK(access_base == 2 * mib);
    if (access_size != mib) {
        fprintf(stderr, "row at 2 MiB: expected 1 MiB state span, got %u bytes\n", access_size);
        return 1;
    }
    CHECK(access_mode == (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL));
    free(pages);
    free(memory);
#ifdef USE_NEW_DYNAREC
    free(byte_dirty_mask);
    free(byte_code_present_mask);
#endif
    return 0;
}
