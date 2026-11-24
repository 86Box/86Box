/* A physical read from a device thread must leave the CPU thread's memory
   state alone: the page walk it last recorded, the linear address of its
   current access, and its read lookup table. Device threads (a graphics
   card's command processor, say) read guest memory through mem_readl_phys()
   and mem_read_phys_block() while the CPU thread runs; that state belongs to
   the CPU thread alone. A physical read on the CPU thread still forgets the
   walk, since no walk describes it. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* Include the implementation so the test reads the file-scope walk state and
   sets up one mapping without machine setup, while running the production
   physical read paths. */
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

#define RAM_BYTES   (64 * 1024)
#define EXEC_PAGE   0x1000u   /* RAM read straight from the exec bytes */
#define HANDLER_PAGE 0x2000u  /* RAM read through the handler: no exec bytes */
#define WALK_FRAME  0x00345000ULL
#define WALK_PREV   0x00678000ULL
#define CPU_LINEAR  0x80123000u

static mem_mapping_t exec_map;
static mem_mapping_t handler_map;

/* What the CPU thread holds in the middle of an access: a recorded walk and
   the linear address it is reading. */
static void
cpu_access_in_progress(void)
{
    mmu_walk_frame      = WALK_FRAME;
    mmu_walk_prev_frame = WALK_PREV;
    mem_logical_addr    = CPU_LINEAR;
}

static int
cpu_state_untouched(void)
{
    return (mmu_walk_frame == WALK_FRAME) && (mmu_walk_prev_frame == WALK_PREV) &&
           (mem_logical_addr == CPU_LINEAR) &&
           (readlookup2[CPU_LINEAR >> 12] == (uintptr_t) LOOKUP_INV);
}

static uint8_t  block_buf[16];
static uint32_t read_result;

static void *
device_read_block(void *arg)
{
    (void) arg;
    read_result = mem_read_phys_block(block_buf, EXEC_PAGE, sizeof(block_buf));
    return NULL;
}

static void *
device_readl(void *arg)
{
    read_result = mem_readl_phys((uint32_t) (uintptr_t) arg);
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

int
main(void)
{
    const uint32_t exec_word    = 0x11223344;
    const uint32_t handler_word = 0x55667788;
    uint32_t       first_word;
    int            fails = 0;

    is_cpu_thread = 1;
    cpu_use_exec  = 1;
    ram           = calloc(1, RAM_BYTES);
    ram_size      = RAM_BYTES;
    CHECK(ram != NULL);
    memset(readlookup, 0xff, sizeof(readlookup));
    for (uint32_t i = 0; i < (sizeof(readlookup2) / sizeof(readlookup2[0])); i++)
        readlookup2[i] = (uintptr_t) LOOKUP_INV;
    memcpy(&ram[EXEC_PAGE], &exec_word, sizeof(exec_word));
    memcpy(&ram[HANDLER_PAGE], &handler_word, sizeof(handler_word));

    exec_map.base   = EXEC_PAGE;
    exec_map.size   = 0x1000;
    exec_map.mask   = 0xfff;
    exec_map.exec   = &ram[EXEC_PAGE];
    exec_map.read_l = mem_read_raml;
    exec_map.enable = 1;
    read_mapping_bus[EXEC_PAGE >> MEM_GRANULARITY_BITS] = &exec_map;

    handler_map.base   = HANDLER_PAGE;
    handler_map.size   = 0x1000;
    handler_map.mask   = 0xffffffff;
    handler_map.read_b = mem_read_ram;
    handler_map.read_w = mem_read_ramw;
    handler_map.read_l = mem_read_raml;
    handler_map.enable = 1;
    read_mapping_bus[HANDLER_PAGE >> MEM_GRANULARITY_BITS] = &handler_map;

    /* R1: a device-thread block read leaves the walk and the linear address. */
    cpu_access_in_progress();
    CHECK(run_on_device_thread(device_read_block, NULL));
    CHECK(read_result == sizeof(block_buf));
    memcpy(&first_word, block_buf, sizeof(first_word));
    CHECK(first_word == exec_word);
    if (row("R1 device-thread mem_read_phys_block keeps the CPU walk and linear address", cpu_state_untouched())) {
        fails++;
        fprintf(stderr, "    frame=%016llx prev=%016llx logical=%08x\n", (unsigned long long) mmu_walk_frame,
                (unsigned long long) mmu_walk_prev_frame, mem_logical_addr);
    }

    /* R2: the same for a single read that takes the exec bytes. */
    cpu_access_in_progress();
    CHECK(run_on_device_thread(device_readl, (void *) (uintptr_t) EXEC_PAGE));
    CHECK(read_result == exec_word);
    if (row("R2 device-thread mem_readl_phys keeps the CPU walk and linear address", cpu_state_untouched())) {
        fails++;
        fprintf(stderr, "    frame=%016llx prev=%016llx logical=%08x\n", (unsigned long long) mmu_walk_frame,
                (unsigned long long) mmu_walk_prev_frame, mem_logical_addr);
    }

    /* R3: a device-thread read through the RAM handler, while the CPU thread
       is reading linear CPU_LINEAR, adds no entry for that address to the
       CPU's read lookup table. */
    cpu_access_in_progress();
    CHECK(run_on_device_thread(device_readl, (void *) (uintptr_t) HANDLER_PAGE));
    CHECK(read_result == handler_word);
    if (row("R3 device-thread handler read adds no CPU read lookup entry",
            readlookup2[CPU_LINEAR >> 12] == (uintptr_t) LOOKUP_INV)) {
        fails++;
        fprintf(stderr, "    readlookup2[%05x]=%llx\n", CPU_LINEAR >> 12,
                (unsigned long long) readlookup2[CPU_LINEAR >> 12]);
    }
    readlookup2[CPU_LINEAR >> 12] = (uintptr_t) LOOKUP_INV;

    /* R4: on the CPU thread a physical read still forgets the walk. */
    cpu_access_in_progress();
    CHECK(mem_read_phys_block(block_buf, EXEC_PAGE, sizeof(block_buf)) == sizeof(block_buf));
    fails += row("R4 CPU-thread mem_read_phys_block forgets the walk and linear address",
                 (mmu_walk_frame == 0xffffffffffffffffULL) && (mmu_walk_prev_frame == 0xffffffffffffffffULL) &&
                 (mem_logical_addr == 0xffffffff));

    /* R5: the same with no exec bytes in use, where the block read copies
       nothing and the caller reads through the handlers: the walk is still
       forgotten first, so those handler reads add no entry from it. */
    cpu_use_exec = 0;
    cpu_access_in_progress();
    CHECK(mem_read_phys_block(block_buf, EXEC_PAGE, sizeof(block_buf)) == 0);
    fails += row("R5 CPU-thread mem_read_phys_block without exec bytes still forgets the walk and linear address",
                 (mmu_walk_frame == 0xffffffffffffffffULL) && (mmu_walk_prev_frame == 0xffffffffffffffffULL) &&
                 (mem_logical_addr == 0xffffffff));
    cpu_use_exec = 1;

    free(ram);
    return fails ? 1 : 0;
}
