/* Use production bus-stall and retirement accounting at DMA entry
 * boundaries. Execution and timer callbacks must advance one shared TSC. */
#include "../../src/cpu/386.c"

cpu_state_t cpu_state;
uint64_t tsc;
static unsigned checks, failures;
#define CHECK(e) do { checks++; if (!(e)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); failures++; } } while (0)

void rivatimer_init(void) { }
void fatal(const char *fmt, ...)
{
    fprintf(stderr, "Unexpected fatal: %s\n", fmt);
    exit(EXIT_FAILURE);
}

static void count_timer(void *p) { ++*(unsigned *)p; }
static void overdue_bus_wait(void)
{
    /* Real timer queue, including a surviving head. Model RESET's skipped
     * opcode epilogue by entering the next period after a synchronous wait. */
    pc_timer_t early, later;
    unsigned early_calls = 0, later_calls = 0;
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

int main(void)
{
    cycles = 100;
    tsc = 1000;
    cpu_bus_stall(10); /* Peripheral timer after retirement. */
    CHECK(cycles == 90 && tsc == 1010);
    int start = cycles;
    uint64_t start_tsc = tsc;
    cycles -= 5;
    cpu_bus_stall(8); /* Synchronous DMA during an opcode. */
    CHECK(cycles == 77 && tsc == 1018);
    cpu_bus_retire(start - cycles, start_tsc);
    CHECK(tsc == 1023); /* No missing or duplicated stolen time. */
    cpu_bus_stall(10);
    CHECK(cycles == 67 && tsc == 1033);
    start = cycles;
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
    overdue_bus_wait();
    printf("PC-98 CPU bus accounting: %u checks, %u failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
