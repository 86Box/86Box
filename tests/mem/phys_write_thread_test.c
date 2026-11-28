/* A physical write from a device thread must leave the CPU thread's memory
   state alone: the linear address of its current access and its write
   lookup table. A bus master that runs on its own thread would write guest
   memory through mem_writel_phys() and mem_write_phys_block() while the CPU
   thread runs; that state belongs to the CPU thread alone. A physical write
   on the CPU thread still drops the linear address, since none describes
   it. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* Include the implementation so the test reads the file-scope lookup state
   and sets up two mappings without machine setup, while running the
   production physical write paths. */
#include "../../src/mem/mem.c"

/* Definitions for what mem.c uses from the CPU, platform and machine code.
   Those paths are not run here. */
__thread int is_cpu_thread;
cpu_state_t  cpu_state;
cpu_events_t cpu_events;
uint32_t     abrt_error;
uint32_t     addr64;
uint32_t     addr64a[8];
uint8_t      ccr1;
int          cpl_override;
int          cpl_override_wp_exempt;
int          cpu_16bitbus;
int          cpu_cyrix_alignment;
uint32_t     cpu_features;
int          cpu_mem_prefetch_cycles;
int          cpu_prefetch_cycles;
int          cpu_rom_prefetch_cycles;
int          cpu_use_exec;
uint32_t     cr2;
uint32_t     cr3;
uint32_t     cr4;
int          is286;
int          is386ex;
int          is486;
int          is6117;
int          is_cxsmm;
int          is_pcjr;
int          isibm486;
uint32_t     mem_size;
uintptr_t    mmut_hit_r;
uintptr_t    mmut_hit_r2;
uint32_t     mmut_hit_r2_page;
uint32_t     mmut_hit_r_page;
uintptr_t    mmut_hit_w;
uint32_t     mmut_hit_w_page;
uint32_t     old_rammask;
int          read_type;
int          timing_misaligned;
int          unmask_a20_in_smm;

const uint32_t *
agpgart_cpu_entry(const mem_mapping_t *map, uint32_t addr, _Atomic uint32_t **gen, uint32_t *val, uint32_t *ra)
{
    (void) map; (void) addr; (void) gen; (void) val; (void) ra;
    return NULL;
}
int agpgart_is_aperture(const mem_mapping_t *map) { (void) map; return 0; }
void cpu_car_reset(void) { }
void mem_debug_check_addr(uint32_t addr, int write) { (void) addr; (void) write; }
void pcjr_waitstates(void *priv) { (void) priv; }
void pclog(const char *fmt, ...) { (void) fmt; }
void
fatal(const char *fmt, ...)
{
    fprintf(stderr, "fatal: %s\n", fmt);
    exit(1);
}
void *
plat_mmap(size_t size, uint8_t executable, uint8_t *large)
{
    (void) executable;
    if (large)
        *large = 0;
    return calloc(1, size);
}
void plat_munmap(void *ptr, size_t size) { (void) size; free(ptr); }
mutex_t *plat_thread_create_mutex(void) { return NULL; }
int plat_thread_wait_mutex(mutex_t *mutex) { (void) mutex; return 1; }
int plat_thread_release_mutex(mutex_t *mutex) { (void) mutex; return 1; }

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

#define RAM_BYTES    (64 * 1024)
#define RAM_PAGES    (RAM_BYTES >> 12)
#define EXEC_PAGE    0x1000u   /* RAM written straight into the exec bytes */
#define HANDLER_PAGE 0x2000u   /* RAM written through the handler: no exec bytes */
#define CPU_LINEAR   0x80123000u

static mem_mapping_t exec_map;
static mem_mapping_t handler_map;

/* What the CPU thread holds in the middle of an access: the linear address
   it is writing. */
static void
cpu_access_in_progress(void)
{
    mem_logical_addr = CPU_LINEAR;
}

static int
no_write_entry(void)
{
    return (writelookup2[CPU_LINEAR >> 12] == (uintptr_t) LOOKUP_INV) && (page_lookup[CPU_LINEAR >> 12] == NULL);
}

static void
drop_write_entry(void)
{
    writelookup2[CPU_LINEAR >> 12] = (uintptr_t) LOOKUP_INV;
    page_lookup[CPU_LINEAR >> 12]  = NULL;
    for (int i = 0; i < 256; i++)
        writelookup[i] = -1;
    writelnext = 0;
}

static uint8_t  block_buf[16];
static uint32_t write_result;

static void *
device_write_block(void *arg)
{
    (void) arg;
    write_result = mem_write_phys_block(block_buf, EXEC_PAGE, sizeof(block_buf));
    return NULL;
}

static void *
device_writel(void *arg)
{
    mem_writel_phys((uint32_t) (uintptr_t) arg, 0xa5a55a5a);
    return NULL;
}

/* The RAM handler itself, as a mapping write handler is called. */
static void *
device_write_raml(void *arg)
{
    mem_write_raml((uint32_t) (uintptr_t) arg, 0x5a5aa5a5, NULL);
    return NULL;
}

static int
run_on_device_thread(void *(*fn)(void *), void *arg)
{
    pthread_t t;

    if (pthread_create(&t, NULL, fn, arg) != 0)
        return 0;
    return pthread_join(t, NULL) == 0;
}

static int
row(const char *name, int ok)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    return ok ? 0 : 1;
}

static uint32_t
ram_word(uint32_t addr)
{
    uint32_t v;

    memcpy(&v, &ram[addr], sizeof(v));
    return v;
}

int
main(void)
{
    int fails = 0;

    is_cpu_thread = 1;
    cpu_use_exec  = 1;
    ram           = calloc(1, RAM_BYTES);
    ram_size      = RAM_BYTES;
    pages         = calloc(RAM_PAGES, sizeof(page_t));
    pages_sz      = RAM_PAGES;
    CHECK((ram != NULL) && (pages != NULL));
    for (uint32_t i = 0; i < RAM_PAGES; i++) {
        pages[i].mem                    = &ram[i << 12];
        pages[i].evict_prev             = EVICT_NOT_IN_LIST;
        pages[i].byte_dirty_mask        = calloc(64, sizeof(uint64_t));
        pages[i].byte_code_present_mask = calloc(64, sizeof(uint64_t));
        CHECK((pages[i].byte_dirty_mask != NULL) && (pages[i].byte_code_present_mask != NULL));
    }
    for (uint32_t i = 0; i < (sizeof(writelookup2) / sizeof(writelookup2[0])); i++)
        writelookup2[i] = (uintptr_t) LOOKUP_INV;
    drop_write_entry();
    memset(block_buf, 0x3c, sizeof(block_buf));

    exec_map.base    = EXEC_PAGE;
    exec_map.size    = 0x1000;
    exec_map.mask    = 0xfff;
    exec_map.exec    = &ram[EXEC_PAGE];
    exec_map.write_l = mem_write_raml;
    exec_map.enable  = 1;
    write_mapping_bus[EXEC_PAGE >> MEM_GRANULARITY_BITS] = &exec_map;

    handler_map.base    = HANDLER_PAGE;
    handler_map.size    = 0x1000;
    handler_map.mask    = 0xffffffff;
    handler_map.write_b = mem_write_ram;
    handler_map.write_w = mem_write_ramw;
    handler_map.write_l = mem_write_raml;
    handler_map.enable  = 1;
    write_mapping_bus[HANDLER_PAGE >> MEM_GRANULARITY_BITS] = &handler_map;

    /* W1: a device-thread block write leaves the linear address. */
    cpu_access_in_progress();
    CHECK(run_on_device_thread(device_write_block, NULL));
    CHECK(write_result == sizeof(block_buf));
    CHECK(ram_word(EXEC_PAGE) == 0x3c3c3c3c);
    if (row("W1 device-thread mem_write_phys_block keeps the CPU linear address", mem_logical_addr == CPU_LINEAR)) {
        fails++;
        fprintf(stderr, "    logical=%08x\n", mem_logical_addr);
    }

    /* W2: the same for a single write that takes the exec bytes. */
    cpu_access_in_progress();
    CHECK(run_on_device_thread(device_writel, (void *) (uintptr_t) EXEC_PAGE));
    CHECK(ram_word(EXEC_PAGE) == 0xa5a55a5a);
    if (row("W2 device-thread mem_writel_phys keeps the CPU linear address", mem_logical_addr == CPU_LINEAR)) {
        fails++;
        fprintf(stderr, "    logical=%08x\n", mem_logical_addr);
    }

    /* W3: a device-thread physical write through the RAM handler, while the
       CPU thread is writing linear CPU_LINEAR, adds no entry for that
       address to the CPU's write lookup table. */
    cpu_access_in_progress();
    CHECK(run_on_device_thread(device_writel, (void *) (uintptr_t) HANDLER_PAGE));
    CHECK(ram_word(HANDLER_PAGE) == 0xa5a55a5a);
    if (row("W3 device-thread mem_writel_phys through the RAM handler adds no CPU write lookup entry",
            no_write_entry())) {
        fails++;
        fprintf(stderr, "    writelookup2[%05x]=%llx\n", CPU_LINEAR >> 12,
                (unsigned long long) writelookup2[CPU_LINEAR >> 12]);
    }
    drop_write_entry();

    /* W4: the RAM write handler itself, called on a device thread while the
       CPU thread holds linear CPU_LINEAR, adds no entry for it. */
    cpu_access_in_progress();
    CHECK(run_on_device_thread(device_write_raml, (void *) (uintptr_t) HANDLER_PAGE));
    CHECK(ram_word(HANDLER_PAGE) == 0x5a5aa5a5);
    if (row("W4 device-thread RAM write handler adds no CPU write lookup entry", no_write_entry())) {
        fails++;
        fprintf(stderr, "    writelookup2[%05x]=%llx\n", CPU_LINEAR >> 12,
                (unsigned long long) writelookup2[CPU_LINEAR >> 12]);
    }
    drop_write_entry();

    /* W5: on the CPU thread a physical write still drops the linear
       address. */
    cpu_access_in_progress();
    mem_writel_phys(EXEC_PAGE, 0x01020304);
    fails += row("W5 CPU-thread mem_writel_phys drops the linear address", mem_logical_addr == 0xffffffff);

    /* W6: the same for a block write with no exec bytes in use, where the
       block write copies nothing and the caller writes through the
       handlers: the linear address is dropped first, so those handler
       writes add no entry from it. */
    cpu_use_exec = 0;
    cpu_access_in_progress();
    CHECK(mem_write_phys_block(block_buf, EXEC_PAGE, sizeof(block_buf)) == 0);
    fails += row("W6 CPU-thread mem_write_phys_block without exec bytes still drops the linear address",
                 mem_logical_addr == 0xffffffff);
    cpu_use_exec = 1;

    /* W7: on the CPU thread the RAM write handler still adds the entry for
       the linear address it is given. */
    cpu_access_in_progress();
    mem_write_raml(HANDLER_PAGE, 0x11111111, NULL);
    fails += row("W7 CPU-thread RAM write handler adds the CPU write lookup entry",
                 writelookup2[CPU_LINEAR >> 12] == (uintptr_t) &ram[(uintptr_t) HANDLER_PAGE - (uintptr_t) CPU_LINEAR]);
    drop_write_entry();

    for (uint32_t i = 0; i < RAM_PAGES; i++) {
        free(pages[i].byte_dirty_mask);
        free(pages[i].byte_code_present_mask);
    }
    free(pages);
    free(ram);
    return fails ? 1 : 0;
}
