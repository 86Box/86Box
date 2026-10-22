/* Use production bus-stall and retirement accounting at DMA/VRAM entry
 * boundaries. Execution and timer callbacks must advance one shared TSC. */
#include "../../src/cpu/386.c"

cpu_state_t     cpu_state;
uint64_t        tsc;
int             machine, is286, is386;
CPU            *cpu_s;
cpu_family_t   *cpu_f;
const machine_t machines[] = {
    { .internal_name = "pc9801vx",  .bus_flags = MACHINE_PC98 },
    { .internal_name = "pc9801bx4", .bus_flags = MACHINE_PC98 },
    { .internal_name = "ibmat",     .bus_flags = MACHINE_AT   }
};
static unsigned checks, failures;
#define CHECK(e)                                                    \
    do {                                                            \
        checks++;                                                   \
        if (!(e)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); \
            failures++;                                             \
        }                                                           \
    } while (0)

void
rivatimer_init(void)
{
}
void
fatal(const char *fmt, ...)
{
    fprintf(stderr, "Unexpected fatal: %s\n", fmt);
    exit(EXIT_FAILURE);
}

static void
count_timer(void *p)
{
    ++*(unsigned *) p;
}
static void
overdue_bus_wait(void)
{
    /* Real timer queue, including a surviving head. Model RESET's skipped
     * opcode epilogue by entering the next period after a synchronous wait. */
    pc_timer_t early, later;
    unsigned   early_calls = 0, later_calls = 0;
    timer_init();
    timer_add(&early, count_timer, &early_calls, 0);
    timer_add(&later, count_timer, &later_calls, 0);
    timer_set_delay_u64(&early, 5ULL << 32);
    timer_set_delay_u64(&later, 100ULL << 32);
    cycles = 100;
    cpu_bus_stall(8);
    CHECK(tsc == 8 && timer_target == 5);
    CHECK(cpu_bus_period() == 93);
    CHECK(early_calls == 1 && later_calls == 0 && timer_target == 100);
    CHECK(tsc == 8 && cycles == 92); /* Dispatch adds no second bus charge. */
    timer_set_delay_u64(&early, 0);
    CHECK(cpu_bus_period() == 93 && early_calls == 2); /* Exact CPU edge. */
    timer_set_delay_u64(&early, 3ULL << 32);
    timer_disable(&early);
    cpu_bus_stall(4);
    CHECK(cpu_bus_period() == 89 && early_calls == 2); /* Stale removed head. */
    timer_close();
}

/* NEC Hardware p.39 lists total clocks. The opcode's base bus cycle is
 * already accounted for: these callbacks must add only the WAIT clocks. */
static void
stock_vx_waits(void)
{
    CPU          stock  = { .cpu_type = CPU_286, .multi = 1.0 };
    cpu_family_t family = { .package = CPU_PKG_286, .internal_name = "286" };
    cpu_s               = &stock;
    cpu_f               = &family;
    is286               = 1;
    is386               = 0;
    machine             = 0;
    for (unsigned mhz = 8; mhz <= 10; mhz += 2) {
        stock.rspeed      = mhz * 1000000u;
        TIMER_USEC        = (uint64_t) mhz << 32;
        unsigned rom_wait = mhz == 8 ? 4 : 5;
        unsigned io_wait  = mhz == 8 ? 3 : 4;
        CHECK(cpu_pc98_vx_bus_active());
        CHECK(cpu_pc98_prefetch_cycles(0xc0000) == (int) rom_wait + 2);
        CHECK(cpu_pc98_prefetch_cycles(0xdffff) == (int) rom_wait + 2);
        CHECK(cpu_pc98_prefetch_cycles(0xe0000) == 2);
        CHECK(cpu_pc98_prefetch_cycles(0xffff0) == 2);
        cycles = 1000;
        tsc    = 1000;
        cpu_pc98_memory_wait(0xbfffe, 2);
        cpu_pc98_memory_wait(0xe0000, 2);
        cpu_pc98_memory_wait(0x00000, 2);
        CHECK(tsc == 1000 && cycles == 1000);
        cpu_pc98_memory_wait(0xc0000, 1);
        CHECK(tsc == 1000 + rom_wait && cycles == 1000 - (int) rom_wait);
        cpu_pc98_memory_wait(0xd7000, 2);
        CHECK(tsc == 1000 + 2 * rom_wait); /* One aligned 16-bit transfer. */
        cpu_pc98_memory_wait(0xd7001, 2);
        CHECK(tsc == 1000 + 4 * rom_wait); /* Two byte transfers. */
        cpu_pc98_memory_wait(0xdffff, 2);
        CHECK(tsc == 1000 + 5 * rom_wait); /* Second byte is onboard ROM. */
        cpu_pc98_memory_wait(0xbffff, 2);
        CHECK(tsc == 1000 + 6 * rom_wait); /* Second byte is external ROM. */
        cycles = 1000;
        tsc    = 1000;
        cpu_pc98_io_wait(0x60, 1);
        CHECK(tsc == 1000 + io_wait);
        cpu_pc98_io_wait(0x4a4, 2);
        CHECK(tsc == 1000 + 2 * io_wait); /* EGC even-port word is one cycle. */
        cpu_pc98_io_wait(0x61, 2);
        CHECK(tsc == 1000 + 4 * io_wait);
        cpu_pc98_io_wait(0x60, 4);
        CHECK(tsc == 1000 + 6 * io_wait);
        cpu_pc98_interrupt_wait();
        CHECK(tsc == 1010 + 6 * io_wait && cycles == 990 - (int) (6 * io_wait));
    }
    /* Force misleading legacy flags as well as normal upgrade selections.
     * The selected model, family, core, multiplier and exact clock decide. */
    const struct {
        int         model;
        uint64_t    type;
        uint32_t    speed;
        double      multiplier;
        unsigned    clock_mhz;
        const char *family_name;
    } excluded[] = {
        { 1, CPU_286,          8000000,   1, 8,   "286"     },
        { 2, CPU_286,          10000000,  1, 10,  "286"     },
        { 0, CPU_286,          6000000,   1, 6,   "286"     },
        { 0, CPU_286,          12500000,  1, 12,  "286"     },
        { 0, CPU_286,          16000000,  1, 16,  "286"     },
        { 0, CPU_386SX,        8000000,   1, 8,   "286"     },
        { 0, CPU_386SX,        20000000,  1, 20,  "i386sx"  },
        { 0, CPU_386DX,        10000000,  1, 10,  "286"     },
        { 0, CPU_386DX,        33333333,  1, 33,  "i386dx"  },
        { 0, CPU_i486DX,       8000000,   1, 8,   "286"     },
        { 0, CPU_i486DX,       33333333,  1, 33,  "i486dx"  },
        { 0, CPU_i486DX_SLENH, 100000000, 3, 100, "idx4"    },
        { 0, CPU_286,          8000000,   2, 8,   "286"     },
        { 0, CPU_286,          8000000,   1, 8,   "upgrade" },
        { 0, CPU_286,          8000000,   1, 10,  "286"     }
    };
    for (unsigned n = 0; n < sizeof(excluded) / sizeof(excluded[0]); n++) {
        machine              = excluded[n].model;
        stock.cpu_type       = excluded[n].type;
        stock.rspeed         = excluded[n].speed;
        stock.multi          = excluded[n].multiplier;
        family.internal_name = excluded[n].family_name;
        TIMER_USEC           = (uint64_t) excluded[n].clock_mhz << 32;
        is286                = 1;
        is386                = 0;
        cycles               = 1000;
        tsc                  = 1000;
        CHECK(!cpu_pc98_vx_bus_active());
        cpu_pc98_memory_wait(0xd7000, 2);
        cpu_pc98_io_wait(0x4a4, 2);
        cpu_pc98_interrupt_wait();
        CHECK(tsc == 1000 && cycles == 1000);
        CHECK(cpu_pc98_prefetch_cycles(0xd7000) == 0);
    }
    cpu_s = NULL;
    cpu_f = NULL;
    CHECK(!cpu_pc98_vx_bus_active());
    machine = 0;
}

int
main(void)
{
    cycles = 100;
    tsc    = 1000;
    cpu_bus_stall(10); /* Peripheral timer after retirement. */
    CHECK(cycles == 90 && tsc == 1010);
    int      start     = cycles;
    uint64_t start_tsc = tsc;
    cycles -= 5;
    cpu_bus_stall(8); /* Synchronous DMA during an opcode. */
    CHECK(cycles == 77 && tsc == 1018);
    cpu_bus_retire(start - cycles, start_tsc);
    CHECK(tsc == 1023); /* No missing or duplicated stolen time. */
    cpu_bus_stall(10);
    CHECK(cycles == 67 && tsc == 1033);
    start     = cycles;
    start_tsc = tsc;
    cpu_bus_stall(4);
    cpu_bus_stall(7);
    CHECK(tsc == 1044 && cycles == 56); /* Two memory callbacks see new time. */
    cycles -= 3;
    cpu_bus_retire(start - cycles, start_tsc);
    CHECK(tsc == 1047); /* Retirement adds only the opcode's other clocks. */
    cpu_bus_stall(0);
    cpu_bus_stall(-1);
    CHECK(cycles == 53 && tsc == 1047);
    cpu_bus_retire(0, tsc);
    cpu_bus_retire(-1, tsc);
    CHECK(tsc == 1047);
    stock_vx_waits();
    overdue_bus_wait();
    printf("PC-98 CPU bus accounting: %u checks, %u failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
