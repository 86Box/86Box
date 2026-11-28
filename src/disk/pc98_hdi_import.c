/* PC-98 MS-DOS media import for the real PC-9801-27's fixed 33x256 layout.
 * Relocate complete FAT volumes, preserving their FATs, directories, file
 * contents and DOS installation. Only disk/partition geometry and the DOS
 * boot loader's physical-sector metadata change. No guest BIOS calls are
 * intercepted. This file is part of 86Box, licensed under GPLv2 or later. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/pc98_hdi.h>
#include <86box/pc98_sasi.h>
#include <86box/plat.h>

#define IMPORT_HEADER_SIZE 4096u
#define IMPORT_MARK        "86BoxSASI2"
#define MAX_PARTITIONS     8u

typedef struct import_partition_t {
    uint8_t  entry[32];
    uint8_t  boot[4096];
    uint64_t source_offset;
    uint32_t bytes;
    uint32_t target_lba;
    uint32_t metadata_offset;
    uint32_t data_bytes;
    uint16_t bps;
} import_partition_t;

typedef struct import_plan_t {
    import_partition_t partitions[MAX_PARTITIONS];
    unsigned           count;
    unsigned           dip;
} import_plan_t;

static uint16_t
get16(const uint8_t *p)
{
    return p[0] | ((uint16_t) p[1] << 8);
}
static uint32_t
get32(const uint8_t *p)
{
    return get16(p) | ((uint32_t) get16(p + 2) << 16);
}
static void
put16(uint8_t *p, uint16_t n)
{
    p[0] = n;
    p[1] = n >> 8;
}
static void
put32(uint8_t *p, uint32_t n)
{
    put16(p, n);
    put16(p + 2, n >> 16);
}
static void
put64(uint8_t *p, uint64_t n)
{
    put32(p, n);
    put32(p + 4, n >> 32);
}

static int
fail(char *error, size_t size, const char *message)
{
    if (error && size)
        snprintf(error, size, "%s", message);
    return -1;
}

static int
read_at(pc98_hdi_t *img, uint64_t offset, void *buf, size_t size)
{
    const uint64_t bytes = (uint64_t) img->blocks * img->sector_size;
    return offset <= bytes && size <= bytes - offset && fseeko64(img->f, (int64_t) ((uint64_t) img->base + offset), SEEK_SET) == 0 && fread(buf, 1, size, img->f) == size;
}

static int
active_entry(const uint8_t *entry)
{
    return (entry[0] & 0x80) && (entry[1] & 0x80);
}

static uint64_t
chs_offset(const pc98_hdi_t *img, const uint8_t *chs)
{
    return (((uint64_t) get16(chs + 2) * img->hpc + chs[1]) * img->spt + chs[0]) * img->sector_size;
}

static int
plan_import(pc98_hdi_t *src, import_plan_t *plan, char *error, size_t error_size)
{
    uint8_t        ipl[8], table[4096];
    const uint64_t source_bytes = (uint64_t) src->blocks * src->sector_size;
    unsigned       active       = 0;
    memset(plan, 0, sizeof(*plan));
    if (!read_at(src, 0, ipl, sizeof(ipl)) || memcmp(ipl + 4, "IPL1", 4) || !read_at(src, src->sector_size, table, src->sector_size))
        return fail(error, error_size, "No supported PC-98 IPL1 partition table; use a controller matching this disk.");

    for (unsigned slot = 0; slot < src->sector_size / 32u; slot++) {
        const uint8_t *entry = table + slot * 32u;
        if (!entry[0] && !entry[1])
            continue;
        if (slot >= MAX_PARTITIONS)
            return fail(error, error_size, "The 256-byte SASI partition table cannot hold more than eight entries.");
        /* PC-98 MS-DOS partition IDs; preserve flags and the partition name. */
        if ((entry[0] & 0x7f) != 0x21 || (entry[1] & 0x7f) != 0x01)
            return fail(error, error_size, "Automatic SASI import supports MS-DOS FAT12/16 partitions only.");
        if (entry[8] >= src->spt || entry[9] >= src->hpc || get16(entry + 10) >= src->cylinders)
            return fail(error, error_size, "Partition start is outside the image geometry.");
        if (memcmp(entry + 4, entry + 8, 4))
            return fail(error, error_size, "Separate partition IPL locations require a controller matching the original disk.");
        import_partition_t *part = &plan->partitions[plan->count++];
        memcpy(part->entry, entry, 32);
        part->source_offset = chs_offset(src, entry + 8);
        if (part->source_offset < 8192 || !read_at(src, part->source_offset, part->boot, 512))
            return fail(error, error_size, "Partition overlaps the IPL boot area or its boot sector is unreadable.");
        part->bps          = get16(part->boot + 11);
        const unsigned spc = part->boot[13], reserved = get16(part->boot + 14);
        const unsigned fats = part->boot[16], root_entries = get16(part->boot + 17);
        const unsigned fat_sectors = get16(part->boot + 22);
        uint32_t       total       = get16(part->boot + 19);
        if (!total)
            total = get32(part->boot + 32);
        if (part->bps < 256 || part->bps > sizeof(part->boot) || (part->bps & (part->bps - 1u)) || !spc || (spc & (spc - 1u)) || !reserved || (fats != 1 && fats != 2) || !root_entries || !fat_sectors || !total)
            return fail(error, error_size, "Unsupported FAT BPB; automatic import requires a FAT12/16 DOS volume.");
        const uint32_t root_sectors = (root_entries * 32u + part->bps - 1u) / part->bps;
        const uint32_t data_sectors = reserved + fats * fat_sectors + root_sectors;
        const uint64_t volume_bytes = (uint64_t) total * part->bps;
        if (total <= data_sectors || volume_bytes > UINT32_MAX || part->source_offset > source_bytes || volume_bytes > source_bytes - part->source_offset)
            return fail(error, error_size, "FAT volume extends beyond the backing image.");
        const uint32_t clusters  = (total - data_sectors) / spc;
        const uint32_t fat_bytes = fat_sectors * part->bps;
        if (!clusters || clusters >= 65525u || (clusters < 4085u ? ((uint64_t) (clusters + 2u) * 3u + 1u) / 2u : (uint64_t) (clusters + 2u) * 2u) > fat_bytes)
            return fail(error, error_size, "FAT is too small or this is not a FAT12/16 volume.");
        part->bytes      = (uint32_t) volume_bytes;
        part->data_bytes = data_sectors * part->bps;
        if (part->data_bytes % src->sector_size || !read_at(src, part->source_offset, part->boot, part->bps))
            return fail(error, error_size, "FAT boot layout is not aligned to the source physical sectors.");
        for (unsigned i = 0; i + 1 < plan->count; i++) {
            const import_partition_t *other = &plan->partitions[i];
            if (part->source_offset < other->source_offset + other->bytes && other->source_offset < part->source_offset + part->bytes)
                return fail(error, error_size, "Overlapping partitions cannot be imported.");
        }
        /* The old NEC DOS BPB stores these fields at 24, where a standard
         * BPB would put CHS. NEC DOS 6.x places them after its extended BPB. */
        uint8_t metadata[8];
        put32(metadata, part->source_offset / src->sector_size);
        put16(metadata + 4, part->data_bytes / src->sector_size);
        put16(metadata + 6, src->sector_size);
        unsigned matches = 0;
        for (unsigned at = 24; at + 8 <= 128; at++) {
            if (!memcmp(part->boot + at, metadata, 8)) {
                part->metadata_offset = at;
                matches++;
            }
        }
        if (matches != 1 || part->data_bytes / 256u > 65535u)
            return fail(error, error_size, "Unrecognized DOS boot-sector layout; original image has been left untouched.");
        /* Reject a data volume masquerading as a bootable DOS installation. */
        if (active_entry(entry)) {
            active++;
            uint8_t        root[32];
            unsigned       core    = 0;
            const uint64_t root_at = part->source_offset + (uint64_t) (reserved + fats * fat_sectors) * part->bps;
            for (unsigned i = 0; i < root_entries; i++) {
                if (!read_at(src, root_at + i * 32u, root, 32))
                    return fail(error, error_size, "Cannot read the DOS root directory.");
                if (!root[0])
                    break;
                if (root[0] == 0xe5 || (root[11] & 0x18))
                    continue;
                if (!memcmp(root, "IO      SYS", 11))
                    core |= 1;
                if (!memcmp(root, "MSDOS   SYS", 11))
                    core |= 2;
                if (!memcmp(root, "COMMAND COM", 11))
                    core |= 4;
            }
            if (core != 7)
                return fail(error, error_size, "Automatic import requires the image's own IO.SYS, MSDOS.SYS and COMMAND.COM.");
        }
    }
    if (!plan->count || active != 1)
        return fail(error, error_size, "Automatic import requires exactly one active MS-DOS partition.");

    const unsigned dips[] = { 0, 1, 4, 6 };
    for (unsigned i = 0; i < sizeof(dips) / sizeof(dips[0]); i++) {
        const pc98_sasi_geometry_t *g              = &pc98_sasi_geometry[dips[i]];
        const uint32_t              cylinder_bytes = g->heads * 33u * 256u;
        uint64_t                    next           = cylinder_bytes;
        for (unsigned p = 0; p < plan->count; p++) {
            import_partition_t *part = &plan->partitions[p];
            part->target_lba         = next / 256u;
            next                     = ((next + part->bytes + cylinder_bytes - 1u) / cylinder_bytes) * cylinder_bytes;
        }
        if (next <= (uint64_t) g->user_cylinders * cylinder_bytes) {
            plan->dip = dips[i];
            return 1;
        }
    }
    return fail(error, error_size, "DOS partitions do not fit the PC-9801-27's 40 MB drive limit.");
}

static uint32_t
boot_area_size(const import_plan_t *plan)
{
    const pc98_sasi_geometry_t *g              = &pc98_sasi_geometry[plan->dip];
    const uint32_t              cylinder_bytes = g->heads * 33u * 256u;
    uint64_t                    first          = plan->partitions[0].source_offset;
    for (unsigned i = 1; i < plan->count; i++)
        if (plan->partitions[i].source_offset < first)
            first = plan->partitions[i].source_offset;
    return first < cylinder_bytes ? (uint32_t) first : cylinder_bytes;
}

static int
hash_range(FILE *file, uint64_t offset, uint64_t bytes, uint64_t *hash)
{
    uint8_t buf[65536];
    if (fseeko64(file, (int64_t) offset, SEEK_SET))
        return 0;
    while (bytes) {
        const size_t n = bytes < sizeof(buf) ? (size_t) bytes : sizeof(buf);
        if (fread(buf, 1, n, file) != n)
            return 0;
        for (size_t i = 0; i < n; i++)
            *hash = (*hash ^ buf[i]) * UINT64_C(1099511628211);
        bytes -= n;
    }
    return 1;
}

/* Fingerprint the header and every byte copied from the source, including
 * unused clusters inside each volume. Unallocated space outside the imported
 * partitions need not be scanned, even in a very large source container.
 * This is a stale-import check, not an authentication hash. */
static int
source_marker(pc98_hdi_t *src, const import_plan_t *plan, uint8_t marker[32])
{
    uint64_t hash = UINT64_C(14695981039346656037);
    if (fseeko64(src->f, 0, SEEK_END))
        return 0;
    const int64_t end = ftello64(src->f);
    if (end < 0)
        return 0;
    const size_t header_bytes = src->format == PC98_HDI_T98NEXT ? 0x120 : 0x20;
    if (!hash_range(src->f, 0, header_bytes, &hash) || !hash_range(src->f, src->base, boot_area_size(plan), &hash))
        return 0;
    for (unsigned i = 0; i < plan->count; i++)
        if (!hash_range(src->f, (uint64_t) src->base + plan->partitions[i].source_offset,
                        plan->partitions[i].bytes, &hash))
            return 0;
    memset(marker, 0, 32);
    memcpy(marker, IMPORT_MARK, sizeof(IMPORT_MARK));
    put64(marker + 16, (uint64_t) end);
    put64(marker + 24, hash);
    return 1;
}

static int
write_at(FILE *f, uint64_t offset, const void *buf, size_t bytes)
{
    return fseeko64(f, (int64_t) offset, SEEK_SET) == 0 && fwrite(buf, 1, bytes, f) == bytes;
}

static int
copy_bytes(pc98_hdi_t *src, uint64_t offset, FILE *out, uint64_t target, uint32_t bytes)
{
    uint8_t buf[65536];
    while (bytes) {
        const uint32_t n = bytes < sizeof(buf) ? bytes : sizeof(buf);
        if (!read_at(src, offset, buf, n) || !write_at(out, target, buf, n))
            return 0;
        offset += n;
        target += n;
        bytes -= n;
    }
    return 1;
}

int
pc98_hdi_import_sasi(pc98_hdi_t *src, const char *output, char *error, size_t error_size)
{
    import_plan_t plan;
    uint8_t       header[IMPORT_HEADER_SIZE] = { 0 }, marker[32], table[256] = { 0 };
    if (!src || !src->f || !output || !*output)
        return fail(error, error_size, "Missing source image or import output path.");
    if (plan_import(src, &plan, error, error_size) < 0)
        return -1;
    if (!source_marker(src, &plan, marker))
        return fail(error, error_size, "Cannot fingerprint the source image.");
    const pc98_sasi_geometry_t *g              = &pc98_sasi_geometry[plan.dip];
    const uint32_t              cylinder_bytes = g->heads * 33u * 256u;
    const uint32_t              disk_bytes     = g->user_cylinders * cylinder_bytes;
    put32(header + 8, IMPORT_HEADER_SIZE);
    put32(header + 12, disk_bytes);
    put32(header + 16, 256);
    put32(header + 20, 33);
    put32(header + 24, g->heads);
    put32(header + 28, g->user_cylinders);
    /* Exclusive creation protects both existing imports and the original. */
    FILE *out = plat_fopen(output, "w+bx");
    if (!out)
        return fail(error, error_size, "Cannot create SASI import; an existing file is never overwritten.");
    int           ok   = write_at(out, 0, header, sizeof(header));
    const uint8_t zero = 0;
    ok                 = ok && write_at(out, IMPORT_HEADER_SIZE + (uint64_t) disk_bytes - 1u, &zero, 1);
    /* Preserve the source IPL/partition-menu program in its reserved area.
     * IPL1 asks BIOS AH=84h for sector size, so its 256/512-sector branches
     * continue to execute normally on the target controller. */
    ok = ok && copy_bytes(src, 0, out, IMPORT_HEADER_SIZE, boot_area_size(&plan));
    for (unsigned i = 0; ok && i < plan.count; i++) {
        import_partition_t *p              = &plan.partitions[i];
        const uint32_t      first_cylinder = p->target_lba / (g->heads * 33u);
        const uint32_t      last_cylinder  = (p->target_lba + (p->bytes - 1u) / 256u) / (g->heads * 33u);
        /* IPL/menu start and partition start both move to the new cylinder. */
        memset(p->entry + 4, 0, 12);
        put16(p->entry + 6, first_cylinder);
        put16(p->entry + 10, first_cylinder);
        put16(p->entry + 14, last_cylinder);
        memcpy(table + i * 32u, p->entry, 32);
        put32(p->boot + p->metadata_offset, p->target_lba);
        put16(p->boot + p->metadata_offset + 4, p->data_bytes / 256u);
        put16(p->boot + p->metadata_offset + 6, 256);
        if (p->metadata_offset > 24) {
            put16(p->boot + 24, 33);
            put16(p->boot + 26, g->heads);
            put32(p->boot + 28, p->target_lba);
        }
        const uint64_t target = IMPORT_HEADER_SIZE + (uint64_t) p->target_lba * 256u;
        ok                    = copy_bytes(src, p->source_offset, out, target, p->bytes) && write_at(out, target, p->boot, p->bps);
    }
    ok = ok && write_at(out, IMPORT_HEADER_SIZE + 256u, table, sizeof(table)) && fflush(out) == 0;
    uint8_t final_marker[32];
    ok = ok && source_marker(src, &plan, final_marker) && !memcmp(final_marker, marker, sizeof(marker));
    /* Publish the reusable-import marker last. An interrupted import must
     * never be mistaken for a complete disk on the next emulator startup. */
    ok = ok && write_at(out, 64, marker, sizeof(marker)) && fflush(out) == 0;
    if (fclose(out))
        ok = 0;
    if (!ok) {
        plat_remove((char *) output);
        return fail(error, error_size, "SASI import failed while copying the disk; the original was not changed.");
    }
    return 1;
}

int
pc98_hdi_open_sasi(pc98_hdi_t *img, const char *path, char *error, size_t error_size)
{
    if (error && error_size)
        error[0] = 0;
    const int result = pc98_hdi_open(img, path);
    if (result <= 0) {
        if (result < 0)
            fail(error, error_size, "Native disk container cannot be opened or has an invalid header.");
        return result;
    }
    const uint32_t blocks       = pc98_hdi_sasi_blocks(img, 33);
    const int      dip          = pc98_sasi_dip_for_blocks(blocks);
    const unsigned native_heads = dip >= 0 ? pc98_sasi_geometry[dip].heads : 4u;
    if (img->sector_size == 256 && img->spt == 33 && img->hpc == native_heads)
        return 1;
    /* Retain the explicit padded-track format, including blank images that
     * users intend to format with the native controller. An ordinary DOS
     * table at physical sector 1 must instead be relocated by import. */
    uint8_t table[256];
    int     native_table = 0, physical_table = 0;
    if (read_at(img, 256, table, sizeof(table)))
        for (unsigned i = 0; i < 8; i++)
            native_table |= active_entry(table + i * 32);
    if (read_at(img, img->sector_size, table, sizeof(table)))
        for (unsigned i = 0; i < 8; i++)
            physical_table |= table[i * 32] != 0 || table[i * 32 + 1] != 0;
    if (img->sector_size == 512 && img->spt == 17 && (native_table || !physical_table))
        return 1;
    char output[4096];
    if (snprintf(output, sizeof(output), "%s.sasi.hdi", path) >= (int) sizeof(output)) {
        pc98_hdi_close(img);
        return fail(error, error_size, "The SASI import path is too long.");
    }
    uint8_t       marker[32], existing[32];
    import_plan_t plan;
    if (plan_import(img, &plan, error, error_size) < 0) {
        pc98_hdi_close(img);
        return -1;
    }
    if (!source_marker(img, &plan, marker)) {
        pc98_hdi_close(img);
        return fail(error, error_size, "Cannot fingerprint the original disk.");
    }
    FILE *cached = plat_fopen(output, "rb");
    if (cached) {
        const int matches = fseeko64(cached, 64, SEEK_SET) == 0 && fread(existing, 1, 32, cached) == 32 && !memcmp(existing, marker, 32);
        fclose(cached);
        if (!matches) {
            pc98_hdi_close(img);
            return fail(error, error_size, "Existing .sasi.hdi belongs to a different source; move it aside to import again.");
        }
    } else if (pc98_hdi_import_sasi(img, output, error, error_size) < 0) {
        pc98_hdi_close(img);
        return -1;
    }
    pc98_hdi_close(img);
    if (pc98_hdi_open(img, output) != 1)
        return fail(error, error_size, "The imported SASI image cannot be opened; its file has been preserved.");
    img->sasi_imported = 1;
    return 1;
}
