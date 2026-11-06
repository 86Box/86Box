/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Copyright 2026 skiretic.
 */
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include <86box/86box.h> /* cpu_use_dynarec */
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/chipset.h> /* ioapic_retry_deliveries */

#include "cpu.h"
#include "lapic.h"
#include "lapic_internal.h"
#include "lapic_policy.h"

void
lapic_mmio_device_init(lapic_mmio_device_t *dev, void *owner)
{
    if (dev == NULL)
        return;

    memset(dev, 0, sizeof(*dev));
    dev->owner = owner;
}

void
lapic_mmio_device_set_lapics(lapic_mmio_device_t *dev, lapic_state_t *lapics)
{
    if (dev == NULL)
        return;

    dev->lapics = lapics;
}

void
lapic_mmio_device_set_enabled(lapic_mmio_device_t *dev, int enabled)
{
    if (dev == NULL)
        return;

    dev->enabled = !!enabled;
}

int
lapic_mmio_device_enabled(const lapic_mmio_device_t *dev)
{
    return (dev != NULL) && dev->enabled;
}

void
lapic_mmio_device_set_active_count(lapic_mmio_device_t *dev, uint8_t active_count)
{
    if (dev == NULL)
        return;

    dev->active_count = active_count;
}

uint8_t
lapic_mmio_device_active_count(const lapic_mmio_device_t *dev)
{
    return (dev != NULL) ? dev->active_count : 0;
}

void
lapic_mmio_device_set_current_index_source(lapic_mmio_device_t *dev,
                                           lapic_current_index_source_t source,
                                           void *priv)
{
    if (dev == NULL)
        return;

    dev->current_index = source;
    dev->current_index_priv = priv;
}

uint8_t
lapic_mmio_device_current_index(const lapic_mmio_device_t *dev)
{
    uint8_t index;

    if ((dev == NULL) || (dev->current_index == NULL))
        return 0;

    index = dev->current_index(dev->current_index_priv);
    /* Out-of-range owner callbacks fall back to CPU 0; no caller may index past active LAPICs. */
    if ((dev->active_count != 0) && (index >= dev->active_count))
        return 0;

    return index;
}

void
lapic_mmio_device_set_external_ioapic(lapic_mmio_device_t *dev, void *external_ioapic,
                                      lapic_external_eoi_handler_t external_eoi)
{
    if (dev == NULL)
        return;

    /* IOAPIC ownership is all-or-nothing: pointer without callback is inert. */
    dev->external_ioapic_present = (external_ioapic != NULL) && (external_eoi != NULL);
    dev->external_ioapic = external_ioapic;
    dev->external_eoi = external_eoi;
}

int
lapic_mmio_device_has_external_ioapic(const lapic_mmio_device_t *dev)
{
    return (dev != NULL) && dev->external_ioapic_present;
}

void
lapic_mmio_device_external_eoi(lapic_mmio_device_t *dev, uint8_t vector)
{
    if (!lapic_mmio_device_has_external_ioapic(dev))
        return;

    dev->external_eoi(dev->external_ioapic, vector);
}

void
lapic_mmio_device_set_log_handler(lapic_mmio_device_t *dev, lapic_log_handler_t log)
{
    if (dev == NULL)
        return;

    dev->log = log;
}

void
lapic_mmio_device_set_icr_write_handler(lapic_mmio_device_t *dev,
                                        lapic_mmio_icr_write_handler_t icr_write)
{
    if (dev == NULL)
        return;

    dev->icr_write = icr_write;
}

void
lapic_log(lapic_mmio_device_t *dev, const char *fmt, ...)
{
    va_list ap;

    if ((dev == NULL) || (dev->log == NULL))
        return;

    va_start(ap, fmt);
    dev->log(dev->owner, fmt, ap);
    va_end(ap);
}

int
lapic_log_first(uint8_t *count, uint8_t limit)
{
    /* Per-site throttle only; counters are diagnostics, not emulated state. */
    if (*count >= limit)
        return 0;

    (*count)++;
    return 1;
}

static int
lapic_mirror_reg_valid(uint32_t reg)
{
    return lapic_reg_valid(reg);
}

uint32_t
lapic_mirror_read_reg(const lapic_state_t *lapic, uint32_t reg)
{
    if ((lapic == NULL) || !lapic_mirror_reg_valid(reg))
        return 0;

    /* Mirror stores model-owned LAPIC regs when CPU backend does not synthesize the access. */
    return lapic->regs[reg >> 4];
}

void
lapic_mirror_write_reg(lapic_state_t *lapic, uint32_t reg, uint32_t val)
{
    if ((lapic == NULL) || !lapic_mirror_reg_valid(reg))
        return;

    lapic->regs[reg >> 4] = val;
}

uint32_t
lapic_mmio_base(const lapic_state_t *lapic)
{
    return cpu_lapic_mmio_base_read_index(lapic->index);
}

void
lapic_update_mapping(lapic_mmio_device_t *dev)
{
    uint8_t        index;
    lapic_state_t *lapic;
    uint32_t       base;

    if ((dev == NULL) || (dev->lapics == NULL))
        return;

    index = lapic_current_index(dev->lapics, dev->active_count,
                                dev->current_index, dev->current_index_priv);
    lapic = lapic_at(dev->lapics, dev->active_count, index);

    /* Expose only the selected CPU's LAPIC mapping; other CPU mappings stay dormant. */
    for (uint8_t i = 0; i < APIC_MAX_LAPICS; i++) {
        if ((i != index) && dev->lapics[i].mapping_added)
            mem_mapping_disable(&dev->lapics[i].mapping);
    }

    if ((lapic == NULL) || !lapic->mapping_added)
        return;

    base = lapic_mmio_base(lapic);

    if (cpu_lapic_mmio_mapping_enabled_index(lapic->index))
        mem_mapping_set_addr(&lapic->mapping, base, LAPIC_MMIO_SIZE);
    else
        mem_mapping_disable(&lapic->mapping);

    lapic_log(dev, "LAPIC: base MSR=%08x%08x mmio=%08x enabled=%u bsp=%u\n",
              (uint32_t) (lapic->base_msr >> 32), (uint32_t) lapic->base_msr,
              base, cpu_lapic_mmio_mapping_enabled_index(lapic->index),
              cpu_lapic_bsp_index(lapic->index));
}

static lapic_state_t *
lapic_mmio_current_target(lapic_mmio_device_t *dev, uint32_t addr, uint32_t *reg)
{
    lapic_state_t *lapic;

    if ((dev == NULL) || (dev->lapics == NULL))
        return NULL;

    lapic = lapic_current(dev->lapics, dev->active_count,
                          dev->current_index, dev->current_index_priv);
    if (lapic == NULL)
        return NULL;

    if (reg != NULL) {
        uint32_t offset = addr - lapic_mmio_base(lapic);

        /* Registers are dword-spaced; byte/word frontends align before this point. */
        *reg = (offset < 0x400) ? (offset & 0x3f0) : 0xffffffffu;
    }

    return lapic;
}

static void
lapic_mmio_illegal_reg(lapic_state_t *lapic)
{
    /* "An Error interrupt is generated when one of the error bits is set"
       (Pentium Vol. 3, 19.3.3). */
    lapic_error(lapic, LAPIC_ESR_ILLEGAL_REG_ADDR);
}

static uint32_t
lapic_mmio_read_reg(lapic_state_t *lapic, uint32_t reg)
{
    uint32_t val;

    if (!lapic_mmio_reg_defined(reg)) {
        lapic_mmio_illegal_reg(lapic);
        return 0;
    }

    if (cpu_lapic_mmio_read_index(lapic->index, reg, &val))
        return val;

    return lapic_mirror_read_reg(lapic, reg);
}

uint32_t
lapic_readl(uint32_t addr, void *priv)
{
    lapic_mmio_device_t *dev = (lapic_mmio_device_t *) priv;
    uint32_t             reg;
    uint32_t             ret;
    lapic_state_t       *lapic = lapic_mmio_current_target(dev, addr, &reg);

    if (lapic == NULL)
        return 0xffffffff;

    /* The timer's current count is worked out from the machine's clock,
       which only moves between blocks unless brought up to date. */
#ifdef USE_DYNAREC
    if ((reg == LAPIC_REG_CURRENT_CNT) && cpu_use_dynarec)
        update_tsc();
#endif

    /* Every register is read from the CPU-owned state (cpu_lapic_mmio_read_index);
       only PPR is derived and must be current. The whole mirror was once
       rebuilt here, on every read -- the NT HAL reads TPR at every IRQL change. */
    if (reg == LAPIC_REG_PPR)
        lapic_sync_ppr(lapic);
    ret = lapic_mmio_read_reg(lapic, reg);
    if (reg == LAPIC_REG_ESR)
        cpu_lapic_esr_read_clear_index(lapic->index);

    if ((reg < 0x400) && lapic_log_first(&lapic->read_log_count[reg >> 4], 8))
        lapic_log(dev, "LAPIC: read reg=%03x val=%08x count=%u\n",
                  reg, ret, lapic->read_log_count[reg >> 4]);

    return ret;
}

uint16_t
lapic_readw(uint32_t addr, void *priv)
{
    uint32_t ret = lapic_readl(addr & ~0x03, priv);

    return ret >> ((addr & 0x02) * 8);
}

uint8_t
lapic_readb(uint32_t addr, void *priv)
{
    uint32_t ret = lapic_readl(addr & ~0x03, priv);

    return ret >> ((addr & 0x03) * 8);
}

void
lapic_writel(uint32_t addr, uint32_t val, void *priv)
{
    lapic_mmio_device_t *dev = (lapic_mmio_device_t *) priv;
    uint32_t             reg;
    uint32_t             mirror_val = val;
    uint32_t             timer_current = 0;
    int                  timer_divide_active = 0;
    lapic_state_t       *lapic = lapic_mmio_current_target(dev, addr, &reg);

    if (lapic == NULL)
        return;

    if (!lapic_mmio_reg_defined(reg)) {
        lapic_mmio_illegal_reg(lapic);
        return;
    }

    if (lapic_log_first(&lapic->write_log_count[reg >> 4], 8))
        lapic_log(dev, "LAPIC: write reg=%03x val=%08x count=%u\n",
                  reg, val, lapic->write_log_count[reg >> 4]);

    if ((reg == LAPIC_REG_DIVIDE_CFG) &&
        cpu_lapic_timer_reload_read_index(lapic->index)) {
        /* The count so far, from a clock brought up to date (as a read of
           the current count does). */
#ifdef USE_DYNAREC
        if (cpu_use_dynarec)
            update_tsc();
#endif
        timer_current = lapic_timer_current(lapic);
        timer_divide_active = !!timer_current;
        /* Preserve elapsed timer state across divisor writes before backend mutates config. */
        if (timer_divide_active)
            lapic_timer_host_stop(lapic);
    }

    int action = cpu_lapic_mmio_write_action_index(lapic->index, reg, val, &mirror_val);

    switch (action) {
        case CPU_LAPIC_MMIO_WRITE_EOI: {
            int level = 0;
            int vector = cpu_lapic_eoi_index(lapic->index, &level);

            if (vector >= 0) {
                /* A slot came free: what was refused for want of one goes
                   again (cpu_lapic_slot_free_index). */
                lapic_slot_freed();
                if (lapic_log_first(&lapic->eoi_log_count[vector], 8))
                    lapic_log(dev, "LAPIC: EOI vector=%02x count=%u\n",
                              vector, lapic->eoi_log_count[vector]);
                /* Notify external IOAPIC only for level-triggered vectors. */
                if (level) {
                    lapic_mmio_device_external_eoi(dev, vector);
                    lapic_bus_sent_by(lapic);
                }
            } else {
                lapic_log(dev, "LAPIC: EOI with empty ISR\n");
            }
            return;
        }

        case CPU_LAPIC_MMIO_WRITE_READ_ONLY:
            lapic_log(dev, "LAPIC: ignored write to read-only reg=%03x\n", reg);
            return;

        case CPU_LAPIC_MMIO_WRITE_MIRROR:
            lapic_mirror_write_reg(lapic, reg, mirror_val);
            break;

        default:
            break;
    }

    if (reg == LAPIC_REG_INITIAL_CNT) {
        lapic_timer_arm(dev, val);
    } else if (timer_divide_active) {
        cpu_lapic_timer_rebase_current_index(lapic->index, timer_current);
        lapic_mirror_write_reg(lapic, LAPIC_REG_CURRENT_CNT, timer_current);
        cpu_lapic_timer_host_schedule_index(lapic->index, timer_current, 0);
    } else if ((reg == LAPIC_REG_ICR_LO) && (dev->icr_write != NULL)) {
        dev->icr_write(dev->owner, val);
    } else if (reg == LAPIC_REG_TPR) {
        lapic_sync_ppr(lapic);
    } else if ((reg == LAPIC_REG_SVR) || (reg == LAPIC_REG_LDR) || (reg == LAPIC_REG_DFR)) {
        /* This APIC may now take what the I/O APIC could not send it. */
        ioapic_retry_deliveries();
    }
}

/* What a narrow write merges into: the register as stored, not a read of
   it. A read has effects of its own -- the P5's ESR clears -- and "a write
   to the ESR has no side effects" (Pentium Vol. 3, 19.3.3). */
static uint32_t
lapic_narrow_base(uint32_t addr, void *priv)
{
    lapic_mmio_device_t *dev = (lapic_mmio_device_t *) priv;
    uint32_t             reg;
    lapic_state_t       *lapic = lapic_mmio_current_target(dev, addr, &reg);

    return (lapic != NULL) ? lapic_mmio_read_reg(lapic, reg) : 0;
}

void
lapic_writew(uint32_t addr, uint16_t val, void *priv)
{
    uint32_t shift = (addr & 0x02) * 8;
    uint32_t cur   = lapic_narrow_base(addr & ~0x03, priv);

    /* Narrow writes reuse dword policy/sanitizers by merging through the aligned register. */
    cur &= ~(0xffffu << shift);
    cur |= ((uint32_t) val) << shift;
    lapic_writel(addr & ~0x03, cur, priv);
}

void
lapic_writeb(uint32_t addr, uint8_t val, void *priv)
{
    uint32_t shift = (addr & 0x03) * 8;
    uint32_t cur   = lapic_narrow_base(addr & ~0x03, priv);

    cur &= ~(0xffu << shift);
    cur |= ((uint32_t) val) << shift;
    lapic_writel(addr & ~0x03, cur, priv);
}
