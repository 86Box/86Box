/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM PC
 *          systems and compatibles.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Copyright 2026 skiretic.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <86box/mem.h>
#include "apic_platform.h"

#define APIC_MP_FP_SIGNATURE   0x5f504d5f
#define APIC_MP_PCMP_SIGNATURE 0x504d4350
#define APIC_MP_SCAN_1KB       1024
#define APIC_MP_BDA_EBDA_SEG   0x040e
#define APIC_MP_BDA_BASE_KB    0x0413
#define APIC_IOAPIC_PHYS_BROADCAST_4BIT 1

/* Platform owns policy and lifetime; LAPIC core owns register/timer state. */
struct apic_platform_t {
    local_apic_t *lapic;
    void         *owner;
    uint8_t       uniprocessor_apic;
    uint8_t       multi_lapic;
    uint8_t       second_cpu_card;
    uint8_t       topology_lapic_slots_max;
    uint8_t       ap_startup;
    uint8_t       ap_safe_park;
    uint8_t       ap_route_parked;
    lapic_log_handler_t log;
};

typedef struct apic_platform_mp_range_t {
    uint32_t base;
    uint32_t len;
} apic_platform_mp_range_t;

static void
apic_platform_log(apic_platform_t *platform, const char *fmt, ...)
{
    va_list ap;

    if ((platform == NULL) || (platform->log == NULL))
        return;

    va_start(ap, fmt);
    platform->log(platform->owner, fmt, ap);
    va_end(ap);
}

static uint8_t
apic_platform_topology_lapic_slots(const apic_platform_t *platform)
{
    /* Extra slots are a board policy input; LAPIC core sees only the clamped result. */
    if ((platform == NULL) || !platform->second_cpu_card)
        return APIC_UP_LAPIC_COUNT;

    return platform->topology_lapic_slots_max ?
           platform->topology_lapic_slots_max : APIC_UP_LAPIC_COUNT;
}

static uint8_t
apic_platform_lapic_count_limit(const apic_platform_t *platform)
{
    uint8_t limit;

    if ((platform == NULL) || !platform->multi_lapic)
        return APIC_UP_LAPIC_COUNT;

    limit = apic_platform_topology_lapic_slots(platform);
    if (limit > APIC_MAX_ACTIVE_LAPICS)
        limit = APIC_MAX_ACTIVE_LAPICS;

    return limit;
}

static uint8_t
apic_platform_lapic_count_limit_or_up(apic_platform_t *platform)
{
    uint8_t limit = apic_platform_lapic_count_limit(platform);

    return limit ? limit : APIC_UP_LAPIC_COUNT;
}

static uint8_t
apic_platform_reset_lapic_id(void *priv, uint8_t index)
{
    apic_platform_t *platform = (apic_platform_t *) priv;

    /* 86Box policy: BSP ID follows UP APIC mode; AP slots keep their slot index. */
    if (index == 0)
        return ((platform != NULL) && platform->uniprocessor_apic) ? 0 : 15;

    return index;
}

static int
apic_platform_lapic_icr_startup(void *priv, uint8_t target_index,
                                uint8_t vector)
{
    apic_platform_t *platform = (apic_platform_t *) priv;

    if ((platform == NULL) || !platform->ap_startup)
        return 0;

    /* Startup side effect stays platform-owned; LAPIC need not know AP parking. */
    if (lapic_ap_startup_request(target_index, vector) && platform->ap_safe_park) {
        apic_platform_log(platform,
                          "LAPIC: AP safe-park start slot=%u vector=%02x\n",
                          target_index, vector);
        return 1;
    }

    return 0;
}

apic_platform_t *
apic_platform_create(void *owner)
{
    apic_platform_t *platform = calloc(1, sizeof(apic_platform_t));

    if (platform == NULL)
        return NULL;

    platform->owner = owner;
    platform->lapic = local_apic_create(owner);
    if (platform->lapic == NULL) {
        free(platform);
        return NULL;
    }

    return platform;
}

void
apic_platform_free(apic_platform_t *platform)
{
    if (platform == NULL)
        return;

    local_apic_free(platform->lapic);
    free(platform);
}

void
apic_platform_set_log_handler(apic_platform_t *platform, lapic_log_handler_t log)
{
    platform->log = log;
    local_apic_set_log_handler(platform->lapic, log);
}

void
apic_platform_set_icr_write_handler(apic_platform_t *platform,
                                    lapic_mmio_icr_write_handler_t icr_write)
{
    local_apic_set_icr_write_handler(platform->lapic, icr_write);
}

void
apic_platform_set_lapic_count_config(apic_platform_t *platform, uint8_t count)
{
    local_apic_set_count_config(platform->lapic, count);
}

void
apic_platform_ensure_lapic_count_min(apic_platform_t *platform, uint8_t count)
{
    local_apic_ensure_count_config_min(platform->lapic, count);
}

void
apic_platform_set_lapic_policy(apic_platform_t *platform,
                               int uniprocessor_apic,
                               int multi_lapic,
                               int second_cpu_card,
                               uint8_t topology_lapic_slots_max,
                               int ap_startup,
                               int ap_safe_park,
                               int ap_route_parked)
{
    platform->uniprocessor_apic = !!uniprocessor_apic;
    platform->multi_lapic = !!multi_lapic;
    platform->second_cpu_card = !!second_cpu_card;
    platform->topology_lapic_slots_max = topology_lapic_slots_max;
    platform->ap_startup = !!ap_startup;
    platform->ap_safe_park = !!ap_safe_park;
    platform->ap_route_parked = !!ap_route_parked;
}

uint8_t
apic_platform_requested_lapic_count(apic_platform_t *platform)
{
    return local_apic_configured_count(platform->lapic,
                                       apic_platform_lapic_count_limit_or_up(platform));
}

void
apic_platform_reset_lapics(apic_platform_t *platform)
{
    local_apic_reset(platform->lapic,
                     apic_platform_requested_lapic_count(platform),
                     apic_platform_reset_lapic_id, platform);
}

int
apic_platform_lapic_state_save(apic_platform_t *platform,
                               struct ioapic_state_t *state)
{
    return local_apic_state_save(platform->lapic, state);
}

int
apic_platform_lapic_state_load(apic_platform_t *platform,
                               const struct ioapic_state_t *state)
{
    /* Load through current machine policy so saved LAPIC IDs/counts stay bounded. */
    return local_apic_state_load(platform->lapic, state,
                                 apic_platform_lapic_count_limit_or_up(platform),
                                 apic_platform_reset_lapic_id, platform);
}

void
apic_platform_enable_lapic_mappings(apic_platform_t *platform)
{
    local_apic_enable_mappings(platform->lapic);
}

void
apic_platform_update_lapic_mapping(apic_platform_t *platform)
{
    local_apic_update_mapping(platform->lapic);
}

void
apic_platform_stop_lapic_timers(apic_platform_t *platform)
{
    local_apic_stop_timers(platform->lapic);
}

void
apic_platform_disable_lapic_mappings(apic_platform_t *platform)
{
    local_apic_disable_mappings(platform->lapic);
}

void
apic_platform_set_external_ioapic(apic_platform_t *platform,
                                  void *external_ioapic,
                                  lapic_external_eoi_handler_t external_eoi)
{
    local_apic_set_external_ioapic(platform->lapic, external_ioapic,
                                   external_eoi);
}

void
apic_platform_lapic_icr_write(apic_platform_t *platform, uint32_t val)
{
    local_apic_icr_write(platform->lapic, val, platform->ap_route_parked,
                         apic_platform_lapic_icr_startup, platform);
}

uint8_t
apic_platform_lapic_count(apic_platform_t *platform)
{
    return local_apic_count(platform->lapic);
}

int
apic_platform_lapic_id_at(apic_platform_t *platform, uint8_t index, uint8_t *id)
{
    return local_apic_id_at(platform->lapic, index, id);
}

int
apic_platform_bsp_lapic_id(apic_platform_t *platform, uint8_t *id)
{
    return local_apic_bsp_id(platform->lapic, id);
}

int
apic_platform_current_external_pending(apic_platform_t *platform,
                                       local_apic_pic_pending_handler_t pic_pending,
                                       void *pic_priv)
{
    return local_apic_current_external_pending(platform->lapic, pic_pending,
                                               pic_priv);
}

int
apic_platform_current_external_vector(apic_platform_t *platform,
                                      local_apic_pic_pending_handler_t pic_pending,
                                      local_apic_pic_interrupt_handler_t pic_interrupt,
                                      void *pic_priv)
{
    return local_apic_current_external_vector(platform->lapic, pic_pending,
                                             pic_interrupt, pic_priv);
}

int
apic_platform_current_base_enabled(apic_platform_t *platform)
{
    return local_apic_current_base_enabled(platform->lapic);
}

int
apic_platform_current_software_enabled(apic_platform_t *platform)
{
    return local_apic_current_software_enabled(platform->lapic);
}

int
apic_platform_current_lint_deliver(apic_platform_t *platform, uint32_t lint_reg)
{
    return local_apic_current_lint_deliver(platform->lapic, lint_reg);
}

static uint8_t
apic_platform_mp_checksum(uint32_t addr, uint32_t len)
{
    uint8_t sum = 0x00;

    for (uint32_t i = 0; i < len; i++)
        sum += mem_readb_phys(addr + i);

    return sum;
}

static int
apic_platform_mp_range_valid(uint32_t base, uint32_t len)
{
    return len && (base < 0x100000) && (len <= (0x100000 - base));
}

static uint8_t
apic_platform_mp_search_ranges(apic_platform_mp_range_t ranges[3])
{
    uint8_t  count = 0;
    uint16_t ebda_segment;
    uint32_t ebda_base;
    int      ebda_valid;
    uint16_t base_kb;
    uint32_t base_last_kb;

    ebda_segment = mem_readw_phys(APIC_MP_BDA_EBDA_SEG);
    ebda_base    = ((uint32_t) ebda_segment) << 4;
    ebda_valid    = (ebda_segment != 0) && (ebda_base >= 0x0400) &&
                    ((ebda_base + APIC_MP_SCAN_1KB) <= 0x0a0000);
    if (ebda_valid) {
        ranges[count].base = ebda_base;
        ranges[count].len  = APIC_MP_SCAN_1KB;
        count++;
    }

    base_kb = mem_readw_phys(APIC_MP_BDA_BASE_KB);
    if (!ebda_valid && (base_kb != 0) && (base_kb <= 640)) {
        base_last_kb = (((uint32_t) base_kb) << 10) - APIC_MP_SCAN_1KB;
        if (apic_platform_mp_range_valid(base_last_kb, APIC_MP_SCAN_1KB)) {
            ranges[count].base = base_last_kb;
            ranges[count].len  = APIC_MP_SCAN_1KB;
            count++;
        }
    }

    ranges[count].base = 0xf0000;
    ranges[count].len  = 0x10000;
    count++;

    return count;
}

static int
apic_platform_mp_pcmp_present(uint32_t pcmp)
{
    return (pcmp <= 0xffffc) &&
           (mem_readl_phys(pcmp) == APIC_MP_PCMP_SIGNATURE);
}

static int
apic_platform_mp_find_table(uint32_t *fps_out, uint32_t *pcmp_out)
{
    apic_platform_mp_range_t ranges[3];
    uint8_t                  range_count;

    range_count = apic_platform_mp_search_ranges(ranges);
    for (uint8_t range = 0; range < range_count; range++) {
        uint32_t end = ranges[range].base + ranges[range].len;

        for (uint32_t addr = ranges[range].base; (addr + 16) <= end; addr += 16) {
            if (mem_readl_phys(addr) == APIC_MP_FP_SIGNATURE) {
                uint32_t pcmp = mem_readl_phys(addr + 4);

                if (apic_platform_mp_pcmp_present(pcmp)) {
                    *fps_out  = addr;
                    *pcmp_out = pcmp;
                    return 1;
                }
            }
        }
    }

    return 0;
}

static void
apic_platform_mp_fixup_processor_lapic_ids(
        apic_platform_t *platform,
        uint32_t pcmp,
        const apic_platform_mp_table_policy_t *policy)
{
    uint16_t base_len;
    uint16_t ext_len;
    uint16_t entry_count;
    uint32_t entry_addr;
    uint32_t table_end;
    uint32_t full_table_end;
    uint8_t  active_count;
    uint8_t  bsp_id;
    uint8_t  next_ap_index = 1;
    uint8_t  enabled_cpu_count = 0;
    uint32_t first_cpu_entry = 0;
    uint32_t cpu_insert_addr = 0;
    int      patched = 0;

    /* Compatibility fixups mutate guest MP tables only under UP APIC policy. */
    if (!platform->uniprocessor_apic)
        return;

    if (!apic_platform_bsp_lapic_id(platform, &bsp_id))
        return;
    active_count = apic_platform_lapic_count(platform);

    base_len = mem_readw_phys(pcmp + 4);
    if (base_len < 44)
        return;

    ext_len        = mem_readw_phys(pcmp + 40);
    entry_count    = mem_readw_phys(pcmp + 34);
    entry_addr     = pcmp + 44;
    table_end      = pcmp + base_len;
    full_table_end = table_end + ext_len;
    if (((pcmp + base_len) > 0x100000) || (full_table_end > 0x100000) ||
        (full_table_end < table_end))
        return;

    for (uint16_t entry = 0; (entry < entry_count) && (entry_addr < table_end); entry++) {
        uint8_t type = mem_readb_phys(entry_addr);

        switch (type) {
            case 0x00:
                if ((entry_addr + 20) > table_end)
                    return;

                if (!first_cpu_entry)
                    first_cpu_entry = entry_addr;
                if (entry_addr == first_cpu_entry)
                    cpu_insert_addr = entry_addr + 20;
                if (mem_readb_phys(entry_addr + 3) & 0x01)
                    enabled_cpu_count++;

                /* Keep existing BSP entry aligned with LAPIC policy before table use. */
                if ((mem_readb_phys(entry_addr + 3) & 0x03) == 0x03) {
                    uint8_t old_id = mem_readb_phys(entry_addr + 1);

                    if (old_id != bsp_id) {
                        ram[entry_addr + 1] = bsp_id;
                        patched = 1;
                        apic_platform_log(platform,
                                          "IOAPIC: MP CPU BSP LAPIC ID fixup %u -> %u\n",
                                          old_id, bsp_id);
                    }
                } else if (platform->second_cpu_card && platform->ap_safe_park &&
                           policy->multi_lapic_mp_cpus &&
                           (mem_readb_phys(entry_addr + 3) & 0x01) &&
                           (next_ap_index < active_count)) {
                    uint8_t ap_id;

                    if (apic_platform_lapic_id_at(platform, next_ap_index, &ap_id)) {
                        uint8_t old_id = mem_readb_phys(entry_addr + 1);

                        if (old_id != ap_id) {
                            ram[entry_addr + 1] = ap_id;
                            patched = 1;
                            apic_platform_log(platform,
                                              "IOAPIC: MP CPU AP LAPIC ID fixup slot=%u %u -> %u\n",
                                              next_ap_index, old_id, ap_id);
                        }
                    }
                    next_ap_index++;
                }
                entry_addr += 20;
                break;

            case 0x01:
            case 0x03:
            case 0x04:
                entry_addr += 8;
                break;

            case 0x02:
                entry_addr += 8;
                break;

            default:
                return;
        }
    }

    while (platform->second_cpu_card && platform->ap_safe_park &&
           policy->multi_lapic_mp_cpus && first_cpu_entry && cpu_insert_addr &&
           (enabled_cpu_count < active_count) &&
           (entry_count < UINT16_MAX) && ((full_table_end + 20) <= 0x100000)) {
        uint8_t missing_index = enabled_cpu_count;
        uint8_t ap_id;

        /* Synthesize missing AP rows by cloning CPU entry shape, then clearing BSP flag. */
        if (!apic_platform_lapic_id_at(platform, missing_index, &ap_id) ||
            (cpu_insert_addr < (pcmp + 44)) || (cpu_insert_addr > table_end))
            break;

        memmove(&ram[cpu_insert_addr + 20], &ram[cpu_insert_addr], full_table_end - cpu_insert_addr);
        memcpy(&ram[cpu_insert_addr], &ram[first_cpu_entry], 20);
        ram[cpu_insert_addr + 1] = ap_id;
        ram[cpu_insert_addr + 3] = (ram[cpu_insert_addr + 3] | 0x01) & ~0x02;
        cpu_insert_addr += 20;
        table_end += 20;
        full_table_end += 20;
        base_len += 20;
        entry_count++;
        enabled_cpu_count++;
        patched = 1;
        apic_platform_log(platform,
                          "IOAPIC: MP CPU AP entry synth slot=%u lapic_id=%u insert=%08x\n",
                          missing_index, ap_id, cpu_insert_addr - 20);
    }

    if (patched) {
        /* Any byte edit invalidates MP checksums and table metadata; repair together. */
        ram[pcmp + 4] = base_len & 0xff;
        ram[pcmp + 5] = base_len >> 8;
        ram[pcmp + 34] = entry_count & 0xff;
        ram[pcmp + 35] = entry_count >> 8;
        ram[pcmp + 7] = 0x00;
        ram[pcmp + 7] = (uint8_t) (0x00 - apic_platform_mp_checksum(pcmp, base_len));
    }
}

static void
apic_platform_mp_string(char *dst, uint32_t addr, uint8_t len)
{
    uint8_t pos;

    for (pos = 0; pos < len; pos++)
        dst[pos] = mem_readb_phys(addr + pos);

    while (pos && ((dst[pos - 1] == ' ') || (dst[pos - 1] == '\0')))
        pos--;

    dst[pos] = '\0';
}

static const char *
apic_platform_mp_int_type(uint8_t type)
{
    switch (type) {
        case 0x00: return "INT";
        case 0x01: return "NMI";
        case 0x02: return "SMI";
        case 0x03: return "ExtINT";
        default:   return "unknown";
    }
}

static const char *
apic_platform_mp_polarity(uint16_t flags)
{
    switch (flags & 0x0003) {
        case 0x0000: return "bus";
        case 0x0001: return "high";
        case 0x0003: return "low";
        default:     return "reserved";
    }
}

static const char *
apic_platform_mp_trigger(uint16_t flags)
{
    switch (flags & 0x000c) {
        case 0x0000: return "bus";
        case 0x0004: return "edge";
        case 0x000c: return "level";
        default:     return "reserved";
    }
}

static void
apic_platform_log_mp_table(apic_platform_t *platform, uint32_t fps, uint32_t pcmp)
{
    char     oem_id[9];
    char     product_id[13];
    uint8_t  fps_len      = mem_readb_phys(fps + 8);
    uint16_t base_len     = mem_readw_phys(pcmp + 4);
    uint8_t  spec_rev     = mem_readb_phys(pcmp + 6);
    uint32_t oem_table    = mem_readl_phys(pcmp + 28);
    uint16_t oem_len      = mem_readw_phys(pcmp + 32);
    uint16_t entry_count  = mem_readw_phys(pcmp + 34);
    uint32_t lapic_addr   = mem_readl_phys(pcmp + 36);
    uint16_t ext_len      = mem_readw_phys(pcmp + 40);
    uint8_t  ext_checksum = mem_readb_phys(pcmp + 42);
    uint32_t entry_addr   = pcmp + 44;
    uint32_t table_end    = pcmp + base_len;
    uint32_t fps_bytes    = fps_len * 16;
    int      fps_valid    = fps_len && ((fps + fps_bytes) <= 0x100000);
    int      pcmp_valid   = (base_len >= 44) && ((pcmp + base_len) <= 0x100000);

    apic_platform_mp_string(oem_id, pcmp + 8, 8);
    apic_platform_mp_string(product_id, pcmp + 16, 12);

    apic_platform_log(platform,
                      "IOAPIC: MP FPS addr=%08x len=%u spec=1.%u checksum=%s default_config=%02x features=%02x,%02x,%02x,%02x,%02x\n",
                      fps, fps_bytes, mem_readb_phys(fps + 9),
                      fps_valid ? (apic_platform_mp_checksum(fps, fps_bytes) ? "bad" : "ok") : "skipped",
                      mem_readb_phys(fps + 11), mem_readb_phys(fps + 11), mem_readb_phys(fps + 12),
                      mem_readb_phys(fps + 13), mem_readb_phys(fps + 14), mem_readb_phys(fps + 15));

    apic_platform_log(platform,
                      "IOAPIC: MP PCMP addr=%08x len=%u spec=1.%u checksum=%s oem=\"%s\" product=\"%s\" entries=%u lapic=%08x oem_table=%08x oem_len=%u ext_len=%u ext_checksum=%02x\n",
                      pcmp, base_len, spec_rev,
                      pcmp_valid ? (apic_platform_mp_checksum(pcmp, base_len) ? "bad" : "ok") : "skipped",
                      oem_id, product_id, entry_count, lapic_addr, oem_table, oem_len, ext_len, ext_checksum);

    if (!pcmp_valid) {
        apic_platform_log(platform, "IOAPIC: MP PCMP base table has invalid length, skipping entry decode\n");
        return;
    }

    for (uint16_t entry = 0; (entry < entry_count) && (entry_addr < table_end); entry++) {
        uint8_t type = mem_readb_phys(entry_addr);

        switch (type) {
            case 0x00: {
                if ((entry_addr + 20) > table_end) {
                    apic_platform_log(platform, "IOAPIC: MP entry %u processor overruns base table\n", entry);
                    return;
                }
                apic_platform_log(platform,
                                  "IOAPIC: MP CPU entry=%u lapic_id=%u lapic_ver=%02x flags=%02x enabled=%u bsp=%u signature=%08x features=%08x\n",
                                  entry, mem_readb_phys(entry_addr + 1),
                                  mem_readb_phys(entry_addr + 2),
                                  mem_readb_phys(entry_addr + 3),
                                  !!(mem_readb_phys(entry_addr + 3) & 0x01),
                                  !!(mem_readb_phys(entry_addr + 3) & 0x02),
                                  mem_readl_phys(entry_addr + 4),
                                  mem_readl_phys(entry_addr + 8));
                entry_addr += 20;
                break;
            }

            case 0x01: {
                char bus_type[7];

                if ((entry_addr + 8) > table_end) {
                    apic_platform_log(platform, "IOAPIC: MP entry %u bus overruns base table\n", entry);
                    return;
                }
                apic_platform_mp_string(bus_type, entry_addr + 2, 6);
                apic_platform_log(platform, "IOAPIC: MP BUS entry=%u id=%u type=\"%s\"\n",
                                  entry, mem_readb_phys(entry_addr + 1), bus_type);
                entry_addr += 8;
                break;
            }

            case 0x02:
                if ((entry_addr + 8) > table_end) {
                    apic_platform_log(platform, "IOAPIC: MP entry %u I/O APIC overruns base table\n", entry);
                    return;
                }
                apic_platform_log(platform,
                                  "IOAPIC: MP IOAPIC entry=%u id=%u ver=%02x flags=%02x enabled=%u addr=%08x\n",
                                  entry, mem_readb_phys(entry_addr + 1),
                                  mem_readb_phys(entry_addr + 2),
                                  mem_readb_phys(entry_addr + 3),
                                  !!(mem_readb_phys(entry_addr + 3) & 0x01),
                                  mem_readl_phys(entry_addr + 4));
                entry_addr += 8;
                break;

            case 0x03:
            case 0x04:
                if ((entry_addr + 8) > table_end) {
                    apic_platform_log(platform, "IOAPIC: MP entry %u interrupt overruns base table\n", entry);
                    return;
                }
                apic_platform_log(platform,
                                  "IOAPIC: MP %sINT entry=%u type=%s flags=%04x polarity=%s trigger=%s src_bus=%u src_irq=%u dst_id=%u dst_int=%u\n",
                                  (type == 0x03) ? "IO" : "L", entry,
                                  apic_platform_mp_int_type(mem_readb_phys(entry_addr + 1)),
                                  mem_readw_phys(entry_addr + 2),
                                  apic_platform_mp_polarity(mem_readw_phys(entry_addr + 2)),
                                  apic_platform_mp_trigger(mem_readw_phys(entry_addr + 2)),
                                  mem_readb_phys(entry_addr + 4),
                                  mem_readb_phys(entry_addr + 5),
                                  mem_readb_phys(entry_addr + 6),
                                  mem_readb_phys(entry_addr + 7));
                entry_addr += 8;
                break;

            default:
                apic_platform_log(platform,
                                  "IOAPIC: MP unknown entry=%u type=%02x addr=%08x, stopping decode\n",
                                  entry, type, entry_addr);
                return;
        }
    }

    if (entry_addr != table_end)
        apic_platform_log(platform, "IOAPIC: MP decode ended at %08x, table end is %08x\n",
                          entry_addr, table_end);
}

static void
apic_platform_mp_learn_legacy_irq_routes(
        apic_platform_t *platform,
        uint32_t pcmp,
        const apic_platform_mp_table_policy_t *policy)
{
    uint8_t  legacy_bus[256] = { 0 };
    uint8_t  pci_bus[256] = { 0 };
    uint16_t base_len;
    uint16_t entry_count;
    uint32_t entry_addr;
    uint32_t table_end;

    if ((policy == NULL) ||
        ((policy->legacy_irq_route_handler == NULL) &&
         (policy->pci_irq_route_handler == NULL)))
        return;

    base_len = mem_readw_phys(pcmp + 4);
    if ((base_len < 44) || ((pcmp + base_len) > 0x100000))
        return;

    entry_count = mem_readw_phys(pcmp + 34);
    entry_addr  = pcmp + 44;
    table_end   = pcmp + base_len;

    for (uint16_t entry = 0; (entry < entry_count) && (entry_addr < table_end); entry++) {
        uint8_t type = mem_readb_phys(entry_addr);

        switch (type) {
            case 0x00:
                entry_addr += 20;
                break;

            case 0x01: {
                char bus_type[7];
                uint8_t bus_id;

                if ((entry_addr + 8) > table_end)
                    return;

                bus_id = mem_readb_phys(entry_addr + 1);
                apic_platform_mp_string(bus_type, entry_addr + 2, 6);
                if (!strcmp(bus_type, "ISA") || !strcmp(bus_type, "EISA") ||
                    !strcmp(bus_type, "MCA"))
                    legacy_bus[bus_id] = 1;
                else if (!strcmp(bus_type, "PCI"))
                    pci_bus[bus_id] = 1;
                entry_addr += 8;
                break;
            }

            case 0x02:
                entry_addr += 8;
                break;

            case 0x03:
                if ((entry_addr + 8) > table_end)
                    return;

                if (mem_readb_phys(entry_addr + 1) == 0x00) {
                    uint8_t src_bus = mem_readb_phys(entry_addr + 4);
                    uint8_t src_irq = mem_readb_phys(entry_addr + 5);
                    uint8_t dst_int = mem_readb_phys(entry_addr + 7);

                    apic_platform_log(platform,
                                      "IOAPIC: MP IOINT learn src_bus=%u src_irq=%u bus_type=%s dst_int=%u\n",
                                      src_bus, src_irq, pci_bus[src_bus] ? "PCI" :
                                      (legacy_bus[src_bus] ? "legacy" : "unknown"),
                                      dst_int);

                    if ((policy->legacy_irq_route_handler != NULL) &&
                        legacy_bus[src_bus] && (src_irq < 16) && (dst_int < 24))
                        policy->legacy_irq_route_handler(policy->legacy_irq_route_priv,
                                                         src_irq, dst_int);

                    if ((policy->pci_irq_route_handler != NULL) &&
                        pci_bus[src_bus] && ((src_irq >> 2) < 32) &&
                        (dst_int < 24))
                        policy->pci_irq_route_handler(policy->pci_irq_route_priv,
                                                      src_bus, src_irq >> 2,
                                                      src_irq & 0x03, dst_int);
                }
                entry_addr += 8;
                break;

            case 0x04:
                entry_addr += 8;
                break;

            default:
                return;
        }
    }
}

void
apic_platform_mp_table_post(apic_platform_t *platform, uint8_t val,
                            const apic_platform_mp_table_policy_t *policy)
{
    uint32_t fps;
    uint32_t pcmp;

    if ((platform == NULL) || (policy == NULL) || (val != policy->post_trigger))
        return;

    apic_platform_log(platform, "IOAPIC: Caught POST %02X\n", val);

    if (!apic_platform_mp_find_table(&fps, &pcmp))
        return;

    apic_platform_mp_fixup_processor_lapic_ids(platform, pcmp, policy);
    apic_platform_mp_learn_legacy_irq_routes(platform, pcmp, policy);

    if ((policy->mp_table_logged != NULL) && !*policy->mp_table_logged) {
        apic_platform_log_mp_table(platform, fps, pcmp);
        *policy->mp_table_logged = 1;
    } else if ((policy->mp_table_skip_logged != NULL) && !*policy->mp_table_skip_logged) {
        apic_platform_log(platform, "IOAPIC: MP table already logged, suppressing repeated POST decode\n");
        *policy->mp_table_skip_logged = 1;
    }
    if (policy->expose_mp_tables) {
        if ((policy->mp_table_skip_logged != NULL) && !*policy->mp_table_skip_logged)
            apic_platform_log(platform, "IOAPIC: Leaving _MP_ [%08x] and PCMP [%08x] tables visible\n", fps, pcmp);
        return;
    }

    /* Hide MP tables after optional logging by corrupting signatures, not contents. */
    apic_platform_log(platform, "IOAPIC: Patching _MP_ [%08x] and PCMP [%08x] tables\n", fps, pcmp);
    ram[fps] = ram[fps + 1] = ram[fps + 2] = ram[fps + 3] = 0xff;
    ram[pcmp] = ram[pcmp + 1] = ram[pcmp + 2] = ram[pcmp + 3] = 0xff;
}

int
apic_platform_ioapic_select_targets(apic_platform_t *platform, uint8_t dest,
                                    int logical,
                                    lapic_target_info_t *targets)
{
    return local_apic_select_interruptible_targets(platform->lapic, dest, logical,
                                                   APIC_IOAPIC_PHYS_BROADCAST_4BIT,
                                                   platform->ap_route_parked, targets);
}

apic_bus_result_t
apic_platform_ioapic_deliver(apic_platform_t *platform, const lapic_target_info_t *targets,
                             uint8_t *lowest_priority_next,
                             const apic_bus_message_t *message)
{
    /* LAPIC core applies arbitration; IOAPIC keeps the tie cursor storage. */
    if ((message != NULL) && (message->delivery == APIC_BUS_DELIVERY_LOWEST))
        return local_apic_targets_accept_lowest_priority(platform->lapic, targets,
                                                         lowest_priority_next,
                                                         platform->ap_route_parked,
                                                         message);

    return local_apic_targets_accept_fixed(platform->lapic, targets, message);
}

int
apic_platform_ioapic_deliver_init(apic_platform_t *platform,
                                  const lapic_target_info_t *targets)
{
    /* "INIT is always treated as an edge triggered interrupt, even if
       programmed otherwise" (82093AA, delivery mode 101): no de-assert. */
    return local_apic_targets_deliver_init(platform->lapic, targets);
}

int
apic_platform_ioapic_mark_extint(apic_platform_t *platform,
                                 const lapic_target_info_t *targets,
                                 uint8_t pin)
{
    return local_apic_targets_mark_extint(platform->lapic, targets, pin);
}

uint8_t
apic_platform_current_lapic_id(apic_platform_t *platform)
{
    return local_apic_current_id(platform->lapic);
}

void
apic_platform_ioapic_bus_message_complete(apic_platform_t *platform,
                                          uint8_t *ioapic_arb_id)
{
    local_apic_arbitration_rotate_with_ioapic(platform->lapic, ioapic_arb_id);
}
