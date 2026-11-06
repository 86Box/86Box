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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>

#include <86box/mem.h>
#include <86box/plat_unused.h>
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
    lapic_log_handler_t log;
    /* Where the BIOS's floating pointer was found, once it has been. The
       trigger POST code comes back again and again, and a warm reboot
       rebuilds the table in the same place; a signature check there
       costs four bytes where a search costs sixty-four kilobytes. */
    uint32_t mp_fps;
    /* The table as the last walk left it, fixups and all: the BIOS writes
       the trigger around every INT 13h transfer, so a loader reading its
       drivers through the BIOS sends it by the thousand a second, and a
       table that has not changed since has nothing left to fix or teach.
       Forgotten at every reset, after which POST builds the table again
       (in the same bytes, as likely as not), and at a state load. */
    uint8_t *mp_seen;
    uint32_t mp_seen_fps;
    uint32_t mp_seen_pcmp;
    uint32_t mp_seen_len;
    /* Where both are in plain memory, what they are read through, good
       while mem_mapping_gen is mp_seen_gen: then a trigger costs two
       compares and no lookups. NULL where they are not. */
    const uint8_t *mp_seen_fps_ptr;
    const uint8_t *mp_seen_pcmp_ptr;
    uint32_t       mp_seen_gen;
    /* Where the floating pointer was found gone (hidden, or overwritten
       once the OS took the memory) or pointing at no table: its bytes then,
       and the table's first four when it pointed at one, through the same
       mapping. The same again answers the same, with no lookup. */
    const uint8_t *mp_gone_fps_ptr;
    const uint8_t *mp_gone_pcmp_ptr;
    uint8_t        mp_gone_fps[16];
    uint8_t        mp_gone_pcmp[4];
    uint32_t       mp_gone_gen;
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
apic_platform_reset_lapic_id(void *priv, uint8_t index)
{
    apic_platform_t *platform = (apic_platform_t *) priv;

    /* 86Box policy: BSP ID follows UP APIC mode; AP slots keep their slot index. */
    if (index == 0)
        return ((platform != NULL) && platform->uniprocessor_apic) ? 0 : 15;

    return index;
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
    free(platform->mp_seen);
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
apic_platform_set_lapic_policy(apic_platform_t *platform, int uniprocessor_apic)
{
    platform->uniprocessor_apic = !!uniprocessor_apic;
}

/* One processor, and its local APIC. */
uint8_t
apic_platform_requested_lapic_count(UNUSED(apic_platform_t *platform))
{
    return APIC_UP_LAPIC_COUNT;
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
                                 APIC_UP_LAPIC_COUNT,
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
    /* A STARTUP has no processor to start: there is only the one. */
    local_apic_icr_write(platform->lapic, val, 0, NULL, NULL);
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
apic_platform_lint_deliver_all(apic_platform_t *platform, uint32_t lint_reg)
{
    return local_apic_lint_deliver_all(platform->lapic, lint_reg);
}

int
apic_platform_current_lint_deliver(apic_platform_t *platform, uint32_t lint_reg)
{
    return local_apic_current_lint_deliver(platform->lapic, lint_reg);
}

/* Where a configuration table may lie: "within any unreported, hidden
   system memory space or within the BIOS ROM region" (MP spec 1.1,
   Appendix A) -- anywhere in the 32-bit space. The floating pointer stays
   below 1 MB. */
#define APIC_MP_TABLE_LIMIT 0x100000000ULL

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
apic_platform_mp_search_ranges(apic_platform_mp_range_t ranges[4])
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

    /* "Within the last kilobyte of system base memory" (MP spec 1.1, 4):
       40:13h may count the kilobyte an EBDA would take, or not, so the
       kilobyte below the size it gives and the one at it are both looked
       at (clipped to 640K). */
    base_kb = mem_readw_phys(APIC_MP_BDA_BASE_KB);
    if (!ebda_valid && (base_kb != 0) && (base_kb <= 640)) {
        base_last_kb = (((uint32_t) base_kb) << 10) - APIC_MP_SCAN_1KB;
        if (apic_platform_mp_range_valid(base_last_kb, (base_kb < 640) ? (2 * APIC_MP_SCAN_1KB) : APIC_MP_SCAN_1KB)) {
            ranges[count].base = base_last_kb;
            ranges[count].len  = (base_kb < 640) ? (2 * APIC_MP_SCAN_1KB) : APIC_MP_SCAN_1KB;
            count++;
        }
    }

    /* And 639K-640K whatever the EBDA says: operating systems look there
       anyway, so a pointer there is one they find -- and one to hide with
       the APIC off. */
    if (ebda_valid && (ebda_base != 0x9fc00)) {
        ranges[count].base = 0x9fc00;
        ranges[count].len  = APIC_MP_SCAN_1KB;
        count++;
    }

    ranges[count].base = 0xf0000;
    ranges[count].len  = 0x10000;
    count++;

    return count;
}

/* A configuration table at pcmp: anywhere in memory, "within any
   unreported, hidden system memory space or within the BIOS ROM region"
   (MP spec 1.1, Appendix A). */
static int
apic_platform_mp_pcmp_present(uint32_t pcmp)
{
    return (pcmp != 0) && (pcmp <= 0xfffffffc) &&
           (mem_readl_phys(pcmp) == APIC_MP_PCMP_SIGNATURE);
}

/* A floating pointer at fps: LENGTH 1 (16 bytes), bytes summing to zero
   (MP spec 1.1, Table 4-1). */
static int
apic_platform_mp_fps_valid(uint32_t fps)
{
    return (mem_readl_phys(fps) == APIC_MP_FP_SIGNATURE) && (mem_readb_phys(fps + 8) == 1) &&
           (apic_platform_mp_checksum(fps, 16) == 0);
}

static int
apic_platform_mp_find_table(uint32_t *fps_out, uint32_t *pcmp_out)
{
    apic_platform_mp_range_t ranges[4];
    uint8_t                  range_count;

    range_count = apic_platform_mp_search_ranges(ranges);
    for (uint8_t range = 0; range < range_count; range++) {
        uint32_t end = ranges[range].base + ranges[range].len;

        for (uint32_t addr = ranges[range].base; (addr + 16) <= end; addr += 16) {
            /* A granule of plain memory is looked through directly: POST
               writes the trigger by the hundred thousand before it builds
               the table, and each searched these 64 KB a lookup at a time. */
            const uint32_t gran_end = (addr | MEM_GRANULARITY_MASK) + 1;
            const uint32_t stop     = (gran_end < end) ? gran_end : end;
            const uint8_t *p        = ((stop - addr) >= 16) ? mem_phys_ptr(addr, stop - addr) : NULL;
            uint32_t       sig;

            if (p != NULL) {
                const uint32_t last = addr + (((stop - addr) - 16) & ~15u);

                for (;; addr += 16, p += 16) {
                    memcpy(&sig, p, 4);
                    if ((sig == APIC_MP_FP_SIGNATURE) || (addr == last))
                        break;
                }
                if (sig != APIC_MP_FP_SIGNATURE)
                    continue;
            }
            if (apic_platform_mp_fps_valid(addr)) {
                uint32_t pcmp = mem_readl_phys(addr + 4);

                /* A table, or a default configuration: no table, feature
                   byte 1 naming it (Table 4-1). pcmp 0 then. */
                if (apic_platform_mp_pcmp_present(pcmp) || ((pcmp == 0) && mem_readb_phys(addr + 11))) {
                    *fps_out  = addr;
                    *pcmp_out = pcmp;
                    return 1;
                }
            }
        }
    }

    return 0;
}

/* Whether bytes of the table are in RAM (memory, or shadow RAM, even
   write-protected), not the ROM or flash image itself: only then are they
   fixed or hidden, so no chip image is ever edited. */
static int
apic_platform_mp_writable(uint32_t addr, uint32_t len)
{
    const uint32_t ends[2] = { addr, addr + (len ? len : 1) - 1 };

    for (int i = 0; i < 2; i++) {
        const uint8_t *p = mem_phys_ptr(ends[i], 1);

        if ((p == NULL) || (p < ram) || (p >= (ram + mem_ram_size())))
            return 0;
    }
    return 1;
}

/* Where a byte of the table is, as the processor reads it: the memory
   behind that address in the map (shadow RAM, or the ROM image itself when
   it is not shadowed), else RAM. The fixups write there, so what they write
   is what the operating system reads. */
static uint8_t *
apic_platform_mp_at(uint32_t addr)
{
    static uint8_t nowhere; /* a byte nothing is behind: writes go nowhere */
    const uint8_t *p = mem_phys_ptr(addr, 1);

    if (p != NULL)
        return (uint8_t *) p;
    return (addr < mem_ram_size()) ? &ram[addr] : &nowhere;
}

/* The extended table's length, which a table of MP spec 1.4 carries at
   offset 28h; in a 1.1 table (SPEC_REV 1) that word is reserved (MP spec
   1.1, Table 4-2) and there is no extended table. */
static uint16_t
apic_platform_mp_ext_len(uint32_t pcmp)
{
    return (mem_readb_phys(pcmp + 6) >= 4) ? mem_readw_phys(pcmp + 40) : 0;
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
    uint8_t  bsp_id;
    int      patched = 0;

    /* Compatibility fixups mutate guest MP tables only under UP APIC policy. */
    if (!platform->uniprocessor_apic)
        return;

    if (!apic_platform_bsp_lapic_id(platform, &bsp_id))
        return;

    base_len = mem_readw_phys(pcmp + 4);
    if (base_len < 44)
        return;

    ext_len        = apic_platform_mp_ext_len(pcmp);
    entry_count    = mem_readw_phys(pcmp + 34);
    entry_addr     = pcmp + 44;
    table_end      = pcmp + base_len;
    full_table_end = table_end + ext_len;
    if ((((uint64_t) pcmp + base_len) > APIC_MP_TABLE_LIMIT) || ((uint64_t) full_table_end > APIC_MP_TABLE_LIMIT) ||
        (full_table_end < table_end))
        return;

    for (uint16_t entry = 0; (entry < entry_count) && (entry_addr < table_end); entry++) {
        uint8_t type = mem_readb_phys(entry_addr);

        switch (type) {
            case 0x00:
                if ((entry_addr + 20) > table_end)
                    return;

                /* Keep existing BSP entry aligned with LAPIC policy before table use. */
                if ((mem_readb_phys(entry_addr + 3) & 0x03) == 0x03) {
                    uint8_t old_id = mem_readb_phys(entry_addr + 1);

                    if (old_id != bsp_id) {
                        *apic_platform_mp_at(entry_addr + 1) = bsp_id;
                        patched = 1;
                        apic_platform_log(platform,
                                          "IOAPIC: MP CPU BSP LAPIC ID fixup %u -> %u\n",
                                          old_id, bsp_id);
                    }
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

    if (patched) {
        /* Any byte edit invalidates MP checksums and table metadata; repair together. */
        *apic_platform_mp_at(pcmp + 4)  = base_len & 0xff;
        *apic_platform_mp_at(pcmp + 5)  = base_len >> 8;
        *apic_platform_mp_at(pcmp + 34) = entry_count & 0xff;
        *apic_platform_mp_at(pcmp + 35) = entry_count >> 8;
        *apic_platform_mp_at(pcmp + 7)  = 0x00;
        *apic_platform_mp_at(pcmp + 7)  = (uint8_t) (0x00 - apic_platform_mp_checksum(pcmp, base_len));
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
    uint16_t ext_len      = apic_platform_mp_ext_len(pcmp);
    uint8_t  ext_checksum = mem_readb_phys(pcmp + 42);
    uint32_t entry_addr   = pcmp + 44;
    uint32_t table_end    = pcmp + base_len;
    uint32_t fps_bytes    = fps_len * 16;
    int      fps_valid    = fps_len && ((fps + fps_bytes) <= 0x100000);
    int      pcmp_valid   = (base_len >= 44) && (((uint64_t) pcmp + base_len) <= APIC_MP_TABLE_LIMIT);

    apic_platform_mp_string(oem_id, pcmp + 8, 8);
    apic_platform_mp_string(product_id, pcmp + 16, 12);

    apic_platform_log(platform,
                      "IOAPIC: MP FPS addr=%08x len=%u spec=1.%u checksum=%s default_config=%02x features=%02x,%02x,%02x,%02x\n",
                      fps, fps_bytes, mem_readb_phys(fps + 9),
                      fps_valid ? (apic_platform_mp_checksum(fps, fps_bytes) ? "bad" : "ok") : "skipped",
                      mem_readb_phys(fps + 11), mem_readb_phys(fps + 12),
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
    if ((base_len < 44) || (((uint64_t) pcmp + base_len) > APIC_MP_TABLE_LIMIT))
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
apic_platform_mp_forget(apic_platform_t *platform)
{
    if (platform == NULL)
        return;

    free(platform->mp_seen);
    platform->mp_seen          = NULL;
    platform->mp_seen_len      = 0;
    /* A table rebuilt elsewhere after a reset is looked for afresh. */
    platform->mp_fps           = 0;
    platform->mp_seen_fps_ptr  = NULL;
    platform->mp_seen_pcmp_ptr = NULL;
    platform->mp_gone_fps_ptr  = NULL;
    platform->mp_gone_pcmp_ptr = NULL;
}

/* The floating pointer at fps is not there, or (pcmp_ptr) points at no
   table: remembered for the triggers after. */
static void
apic_platform_mp_gone(apic_platform_t *platform, uint32_t fps, uint32_t pcmp, int pcmp_known)
{
    const uint8_t *f = mem_phys_ptr(fps, 16);
    const uint8_t *t = pcmp_known ? mem_phys_ptr(pcmp, 4) : NULL;

    platform->mp_gone_fps_ptr  = NULL;
    platform->mp_gone_pcmp_ptr = NULL;
    if ((f == NULL) || (pcmp_known && (t == NULL)))
        return;
    memcpy(platform->mp_gone_fps, f, 16);
    if (t != NULL)
        memcpy(platform->mp_gone_pcmp, t, 4);
    platform->mp_gone_pcmp_ptr = t;
    platform->mp_gone_gen      = mem_mapping_gen;
    platform->mp_gone_fps_ptr  = f;
}

/* The whole table, extended part included, wherever in the 32-bit space it
   is. Read as the processor reads it, as the walks do: in the
   F segment that may be ROM, not ram[]. */
static uint32_t
apic_platform_mp_table_len(uint32_t pcmp)
{
    uint32_t len;

    if ((uint64_t) pcmp > (APIC_MP_TABLE_LIMIT - 44))
        return 0;
    len = (uint32_t) mem_readw_phys(pcmp + 4) + apic_platform_mp_ext_len(pcmp);
    if ((len < 44) || (((uint64_t) pcmp + len) > APIC_MP_TABLE_LIMIT))
        return 0;
    return len;
}

/* Whether the len bytes at addr are (cmp) or become (!cmp) buf's: a
   granule at a time straight from memory where it is plain memory, a byte
   at a time through the bus where it is not. */
static int
apic_platform_mp_bytes(uint8_t *buf, uint32_t addr, uint32_t len, int cmp)
{
    while (len) {
        uint32_t       span = MEM_GRANULARITY_SIZE - (addr & MEM_GRANULARITY_MASK);
        const uint8_t *p;

        if (span > len)
            span = len;
        p = mem_phys_ptr(addr, span);
        if (p != NULL) {
            if (!cmp)
                memcpy(buf, p, span);
            else if (memcmp(buf, p, span))
                return 0;
        } else {
            for (uint32_t i = 0; i < span; i++) {
                const uint8_t v = mem_readb_phys(addr + i);

                if (!cmp)
                    buf[i] = v;
                else if (buf[i] != v)
                    return 0;
            }
        }
        buf += span;
        addr += span;
        len -= span;
    }
    return 1;
}

static int
apic_platform_mp_unchanged(const apic_platform_t *platform, uint32_t fps, uint32_t pcmp)
{
    return (platform->mp_seen != NULL) && (platform->mp_seen_fps == fps) &&
           (platform->mp_seen_pcmp == pcmp) &&
           (apic_platform_mp_table_len(pcmp) == platform->mp_seen_len) &&
           apic_platform_mp_bytes(platform->mp_seen, fps, 16, 1) &&
           apic_platform_mp_bytes(platform->mp_seen + 16, pcmp, platform->mp_seen_len, 1);
}

static void
apic_platform_mp_remember(apic_platform_t *platform, uint32_t fps, uint32_t pcmp)
{
    const uint32_t len = apic_platform_mp_table_len(pcmp);

    apic_platform_mp_forget(platform);
    platform->mp_fps = fps; /* forget clears it; this table is where it was */
    if ((len == 0) || (fps > (0x100000 - 16)))
        return;
    platform->mp_seen = (uint8_t *) malloc(16 + len);
    if (platform->mp_seen == NULL)
        return;
    (void) apic_platform_mp_bytes(platform->mp_seen, fps, 16, 0);
    (void) apic_platform_mp_bytes(platform->mp_seen + 16, pcmp, len, 0);
    platform->mp_seen_fps  = fps;
    platform->mp_seen_pcmp = pcmp;
    platform->mp_seen_len  = len;
    platform->mp_seen_fps_ptr  = mem_phys_ptr(fps, 16);
    platform->mp_seen_pcmp_ptr = mem_phys_ptr(pcmp, len);
    platform->mp_seen_gen      = mem_mapping_gen;
}

int
apic_platform_mp_table_post(apic_platform_t *platform, uint8_t val,
                            const apic_platform_mp_table_policy_t *policy)
{
    uint32_t fps;
    uint32_t pcmp;

    if ((platform == NULL) || (policy == NULL) || (val != policy->post_trigger))
        return 0;

    /* The table in plain memory, mapped as it was, and the same bytes. */
    if ((platform->mp_seen_fps_ptr != NULL) && (platform->mp_seen_pcmp_ptr != NULL) &&
        (platform->mp_seen_gen == mem_mapping_gen) &&
        !memcmp(platform->mp_seen_fps_ptr, platform->mp_seen, 16) &&
        !memcmp(platform->mp_seen_pcmp_ptr, platform->mp_seen + 16, platform->mp_seen_len))
        return 1;
    /* Gone, and still the same bytes where it was. */
    if ((platform->mp_gone_fps_ptr != NULL) && (platform->mp_gone_gen == mem_mapping_gen) &&
        !memcmp(platform->mp_gone_fps_ptr, platform->mp_gone_fps, 16) &&
        ((platform->mp_gone_pcmp_ptr == NULL) || !memcmp(platform->mp_gone_pcmp_ptr, platform->mp_gone_pcmp, 4)))
        return 1;

    if ((platform->mp_fps != 0) && (mem_readl_phys(platform->mp_fps) != APIC_MP_FP_SIGNATURE)) {
        /* Hidden (or never rebuilt) since last time, unless the table is
           now somewhere else: looked for once, then remembered as gone. */
        if (!apic_platform_mp_find_table(&fps, &pcmp)) {
            apic_platform_mp_gone(platform, platform->mp_fps, 0, 0);
            return 1;
        }
    } else if (platform->mp_fps != 0) {
        fps  = platform->mp_fps;
        pcmp = mem_readl_phys(fps + 4);
        if (!apic_platform_mp_pcmp_present(pcmp) && !((pcmp == 0) && mem_readb_phys(fps + 11))) {
            if (pcmp <= 0xfffffffc)
                apic_platform_mp_gone(platform, fps, pcmp, 1);
            return 1;
        }
    } else if (!apic_platform_mp_find_table(&fps, &pcmp))
        return 0;
    platform->mp_fps = fps;

    /* A default configuration (MP spec 1.1, 5): no table to fix, only the
       pointer to hide. */
    if (pcmp == 0) {
        if (!policy->expose_mp_tables && apic_platform_mp_writable(fps, 16)) {
            apic_platform_log(platform, "IOAPIC: Patching _MP_ [%08x], default configuration %u\n", fps,
                              mem_readb_phys(fps + 11));
            for (int k = 0; k < 4; k++)
                *apic_platform_mp_at(fps + k) = 0xff;
        }
        return 1;
    }

    /* A table in ROM or flash is read, not changed: fixing or hiding it
       would edit the chip's image (and a flash's saved file). */
    const int in_ram = apic_platform_mp_writable(fps, 16) &&
                       apic_platform_mp_writable(pcmp, apic_platform_mp_table_len(pcmp));

    if (apic_platform_mp_unchanged(platform, fps, pcmp)) {
        /* The mappings moved and the table did not: look through the new. */
        platform->mp_seen_fps_ptr  = mem_phys_ptr(fps, 16);
        platform->mp_seen_pcmp_ptr = mem_phys_ptr(pcmp, platform->mp_seen_len);
        platform->mp_seen_gen      = mem_mapping_gen;
        return 1;
    }

    /* Learning is for the I/O APIC's routing, which nothing uses with the
       APIC off. */
    if (platform->uniprocessor_apic)
        apic_platform_mp_learn_legacy_irq_routes(platform, pcmp, policy);
    if (in_ram)
        apic_platform_mp_fixup_processor_lapic_ids(platform, pcmp, policy);

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
        apic_platform_mp_remember(platform, fps, pcmp);
        return 1;
    }

    /* Hide MP tables after optional logging by corrupting signatures, not
       contents. A table in ROM stays visible, and is remembered as the
       exposed one is, so the next trigger finds it unchanged. */
    if (!in_ram) {
        apic_platform_mp_remember(platform, fps, pcmp);
        return 1;
    }
    apic_platform_log(platform, "IOAPIC: Patching _MP_ [%08x] and PCMP [%08x] tables\n", fps, pcmp);
    for (int k = 0; k < 4; k++) {
        *apic_platform_mp_at(fps + k)  = 0xff;
        *apic_platform_mp_at(pcmp + k) = 0xff;
    }
    return 1;
}

int
apic_platform_ioapic_select_targets(apic_platform_t *platform, uint8_t dest,
                                    int logical,
                                    lapic_target_info_t *targets)
{
    return local_apic_select_interruptible_targets(platform->lapic, dest, logical,
                                                   APIC_IOAPIC_PHYS_BROADCAST_4BIT,
                                                   0, targets);
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
                                                         0,
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
