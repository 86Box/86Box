/* Stock 8/10 MHz Intel 286 VX board waits added to the 286's existing zero-wait bus cycles.
 * NEC Technical Data Book, Hardware (1993), printed p.39. */
#include <stdint.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/machine.h>
#include <86box/timer.h>
#include "cpu.h"

int
cpu_pc98_vx_bus_active(void)
{
    if (!cpu_s || cpu_s->cpu_type != CPU_286 || (cpu_s->rspeed != 8000000 && cpu_s->rspeed != 10000000) || cpu_s->multi != 1.0)
        return 0;
    if (!cpu_f || cpu_f->package != CPU_PKG_286 || !cpu_f->internal_name || strcmp(cpu_f->internal_name, "286"))
        return 0;
    if (!IS_ARCH(machine, MACHINE_PC98) || !machines[machine].internal_name || strcmp(machines[machine].internal_name, "pc9801vx"))
        return 0;
    return TIMER_USEC == ((uint64_t) cpu_s->rspeed << 32) / 1000000u;
}

static int
external_wait(void)
{
    return TIMER_USEC > (8ULL << 32) ? 5 : 4;
}

/* Physical byte/word transfers. The 286 core already supplies the two base
 * clocks; charge only WAIT extensions. Onboard DRAM's undocumented boundary
 * rule and board-dependent expansion waits are deliberately not guessed. */
void
cpu_pc98_memory_wait(uint32_t addr, unsigned size)
{
    if (!cpu_pc98_vx_bus_active())
        return;
    unsigned waits = 0;
    while (size) {
        unsigned width = (addr & 1) || size == 1 ? 1 : 2;
        if (addr >= 0xc0000 && addr < 0xe0000)
            waits += external_wait();
        addr += width;
        size -= width;
    }
    cpu_bus_stall((int) waits);
}

void
cpu_pc98_io_wait(uint16_t port, unsigned size)
{
    if (!cpu_pc98_vx_bus_active())
        return;
    unsigned transfers = (size + (port & 1) + 1) / 2;
    unsigned wait      = TIMER_USEC > (8ULL << 32) ? 4 : 3;
    cpu_bus_stall((int) (transfers * wait));
}

void
cpu_pc98_interrupt_wait(void)
{
    /* Two acknowledge bus cycles; each has five WAITs at either VX speed. */
    if (cpu_pc98_vx_bus_active())
        cpu_bus_stall(10);
}

int
cpu_pc98_prefetch_cycles(uint32_t addr)
{
    if (!cpu_pc98_vx_bus_active())
        return 0;
    return 2 + (addr >= 0xc0000 && addr < 0xe0000 ? external_wait() : 0);
}
