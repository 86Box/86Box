/* Exercise relocated RAM through the production memory bus and page table. */
#include "../../src/mem/mem.c"

uint32_t mem_size;
cpu_state_t cpu_state;
int cpu_16bitbus, is286 = 1, is6117, cpu_use_exec;
int is_pcjr, is_cxsmm;
uint8_t ccr1;
void pcjr_waitstates(void *priv) { (void) priv; }

void *plat_mmap(size_t size, uint8_t executable, uint8_t *large)
{
    (void) executable;
    *large = 0;
    return calloc(1, size);
}

void plat_munmap(void *ptr, size_t size)
{
    (void) size;
    free(ptr);
}

void (pclog)(const char *fmt, ...) { (void) fmt; }
void (pclog_ex)(const char *fmt, va_list ap) { (void) fmt; (void) ap; }
void fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    abort();
}

static unsigned checks, failures;
#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static void check_relocated_ram(uint32_t kb)
{
    mem_size = kb;
    cpu_16bitbus = 0;
    mem_reset();
    mem_remap_top_extended(384);
    const uint32_t base = kb << 10;
    const uint32_t end = base + 384 * 1024;
    CHECK(ram_remapped_mapping.enable);
    CHECK(ram_remapped_mapping.base == base);
    CHECK(ram_remapped_mapping.size == 384 * 1024);
    CHECK(pages[base >> 12].mem == ram + 0xa0000);
    CHECK(pages[(end - 1) >> 12].mem == ram + 0xff000);
#ifdef USE_NEW_DYNAREC
    CHECK(pages[base >> 12].byte_dirty_mask == byte_dirty_mask + 0xa0 * 64);
    CHECK(pages[(end - 1) >> 12].byte_code_present_mask == byte_code_present_mask + 0xff * 64);
#endif
    for (cpu_use_exec = 0; cpu_use_exec <= 1; cpu_use_exec++) {
        mem_writeb_phys(base, 0x53);
        mem_writel_phys(end - 4, 0x19283746);
        CHECK(mem_readb_phys(base) == 0x53 && ram[0xa0000] == 0x53);
        CHECK(mem_readl_phys(end - 4) == 0x19283746);
        CHECK(*(uint32_t *) (ram + 0xffffc) == 0x19283746);
        CHECK(mem_readb_phys(end) == 0xff);
        CHECK(mem_readb_phys(0xa0000) == 0xff);
    }
    cpu_use_exec = 0;
    mem_write_ramb_page(base + 0x10, 0x69, &pages[base >> 12]);
    CHECK(ram[0xa0010] == 0x69);
    mem_remap_top_extended(0);
    CHECK(!ram_remapped_mapping.enable);
    CHECK(mem_readb_phys(base) == 0xff);
}

int main(void)
{
    mem_init();
    check_relocated_ram(8832);
    check_relocated_ram(32768);

    /* Existing chipset callers and 24-bit CPUs retain the relocation limit. */
    mem_size = 32768;
    mem_reset();
    mem_remap_top(384);
    CHECK(!ram_remapped_mapping.enable);
    cpu_16bitbus = 1;
    mem_reset();
    mem_remap_top_extended(384);
    CHECK(!ram_remapped_mapping.enable);
    cpu_16bitbus = 0;

    /* Reject a relocation beyond the allocated page table. */
    const uint32_t saved_pages = pages_sz;
    mem_remap_top_extended(384);
    CHECK(!ram_remapped_mapping.enable);
    CHECK(pages_sz == saved_pages);

    mem_size = 640;
    mem_reset();
    mem_remap_top_extended(384);
    CHECK(!ram_remapped_mapping.enable);
    mem_close();
    printf("RAM relocation: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
