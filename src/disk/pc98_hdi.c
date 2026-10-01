/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          PC-98 native hard disk image containers.
 *
 *          Anex86 .HDI header, all little-endian 32-bit unless noted:
 *
 *              0x00  reserved (0)
 *              0x04  drive type
 *              0x08  header size - the offset of the first data byte
 *              0x0C  data size in bytes
 *              0x10  sector size (256 on SASI, 512 on IDE)
 *              0x14  sectors per track
 *              0x18  heads per cylinder
 *              0x1C  cylinders
 *
 *          T98-Next .NHD:
 *
 *              0x00   char[16]  "T98HDDIMAGE.R0\0"
 *              0x10   char[256] comment
 *              0x110  dword     header size
 *              0x114  dword     cylinders
 *              0x118  word      heads
 *              0x11A  word      sectors per track
 *              0x11C  word      sector size
 *
 *          Both are accepted only when the geometry in the header accounts
 *          for the data actually present, so a misparse fails at load
 *          instead of reading the drive at the wrong offsets.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include <86box/pc98_hdi.h>
#include <86box/plat.h>

#define NHD_SIGNATURE "T98HDDIMAGE.R0"

static uint32_t
rd32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

static uint16_t
rd16(const uint8_t *p)
{
    return (uint16_t) ((uint16_t) p[0] | ((uint16_t) p[1] << 8));
}

static const char *
extension_of(const char *path)
{
    const char *dot = NULL;

    for (const char *p = path; *p; p++) {
        if (*p == '.')
            dot = p;
        else if (*p == '/' || *p == '\\')
            dot = NULL;
    }
    return dot ? dot + 1 : "";
}

static int
ext_is(const char *path, const char *want)
{
    const char *e = extension_of(path);

    for (; *e && *want; e++, want++) {
        char a = *e;
        char b = *want;
        if (a >= 'A' && a <= 'Z')
            a = (char) (a + 32);
        if (b >= 'A' && b <= 'Z')
            b = (char) (b + 32);
        if (a != b)
            return 0;
    }
    return !*e && !*want;
}

int
pc98_hdi_header_is_sane(const pc98_hdi_t *img, uint64_t file_size)
{
    /* Only the two sizes this layer can express. */
    if (img->sector_size != 256u && img->sector_size != 512u)
        return 0;

    if (!img->spt || !img->hpc || !img->cylinders)
        return 0;

    /* Guard the multiplication below against a nonsense header. */
    if (img->spt > 0x10000u || img->hpc > 0x100u || img->cylinders > 0x10000u)
        return 0;

    if (file_size <= (uint64_t) img->base)
        return 0;

    const uint64_t geometry = (uint64_t) img->spt * img->hpc * img->cylinders *
                              img->sector_size;
    const uint64_t payload = file_size - (uint64_t) img->base;

    /*
     * The geometry has to fit in the file.  A trailing slack of less than
     * one cylinder is normal - some writers pad - but a header claiming
     * more than the file holds would read past the end.
     */
    if (geometry > payload)
        return 0;

    return 1;
}

static int
open_anex86(pc98_hdi_t *img, uint64_t file_size)
{
    uint8_t hdr[0x20];

    if (fseek(img->f, 0, SEEK_SET) != 0)
        return 0;
    if (fread(hdr, 1, sizeof(hdr), img->f) != sizeof(hdr))
        return 0;

    img->format      = PC98_HDI_ANEX86;
    img->base        = rd32(hdr + 0x08);
    img->sector_size = rd32(hdr + 0x10);
    img->spt         = rd32(hdr + 0x14);
    img->hpc         = rd32(hdr + 0x18);
    img->cylinders   = rd32(hdr + 0x1c);

    return pc98_hdi_header_is_sane(img, file_size);
}

static int
open_t98next(pc98_hdi_t *img, uint64_t file_size)
{
    uint8_t hdr[0x120];

    if (fseek(img->f, 0, SEEK_SET) != 0)
        return 0;
    if (fread(hdr, 1, sizeof(hdr), img->f) != sizeof(hdr))
        return 0;

    if (memcmp(hdr, NHD_SIGNATURE, sizeof(NHD_SIGNATURE) - 1) != 0)
        return 0;

    img->format      = PC98_HDI_T98NEXT;
    img->base        = rd32(hdr + 0x110);
    img->cylinders   = rd32(hdr + 0x114);
    img->hpc         = rd16(hdr + 0x118);
    img->spt         = rd16(hdr + 0x11a);
    img->sector_size = rd16(hdr + 0x11c);

    return pc98_hdi_header_is_sane(img, file_size);
}

int
pc98_hdi_open(pc98_hdi_t *img, const char *path)
{
    memset(img, 0, sizeof(*img));

    if (!path || !*path)
        return 0;

    /* Only claim files that announce themselves as one of these. */
    const int maybe_hdi = ext_is(path, "hdi");
    const int maybe_nhd = ext_is(path, "nhd");
    if (!maybe_hdi && !maybe_nhd)
        return 0;

    img->f = plat_fopen(path, "r+b");
    if (!img->f) {
        img->f = plat_fopen(path, "rb");
        if (!img->f)
            return 0;
        img->read_only = 1;
    }

    if (fseek(img->f, 0, SEEK_END) != 0) {
        pc98_hdi_close(img);
        return 0;
    }
    const long end = ftell(img->f);
    if (end <= 0) {
        pc98_hdi_close(img);
        return 0;
    }
    const uint64_t file_size = (uint64_t) end;

    int ok = 0;
    if (maybe_nhd)
        ok = open_t98next(img, file_size);
    if (!ok && maybe_hdi)
        ok = open_anex86(img, file_size);

    if (!ok) {
        /* Leave the file alone: the caller falls back to 86Box's own image
         * layer, which may still be able to make sense of it. */
        const int ro = img->read_only;
        pc98_hdi_close(img);
        img->read_only = ro;
        return 0;
    }

    img->blocks = (uint32_t) ((uint64_t) img->spt * img->hpc * img->cylinders);
    return 1;
}

void
pc98_hdi_close(pc98_hdi_t *img)
{
    if (img->f)
        fclose(img->f);
    memset(img, 0, sizeof(*img));
}

static int
seek_to_block(pc98_hdi_t *img, uint32_t lba, uint32_t count)
{
    if (!img->f)
        return 0;
    if (count == 0u || lba >= img->blocks || count > img->blocks - lba)
        return 0;

    const uint64_t off = (uint64_t) img->base +
                         ((uint64_t) lba * img->sector_size);

    /*
     * Plain fseek() is enough here, and avoids the fseeko64/_fseeki64
     * portability dance 86Box's own image layer has to do: the largest
     * geometry the board BIOS can describe is 175296 blocks, just under
     * 45 MB, so a SASI image never approaches the 2 GB a 32-bit long
     * guarantees.  The check keeps that reasoning enforced rather than
     * assumed.
     */
    if (off > 0x7ffffffful)
        return 0;

    return fseek(img->f, (long) off, SEEK_SET) == 0;
}

/*
 * Container access uses the sector size recorded in the container header.
 */
int
pc98_hdi_read(pc98_hdi_t *img, uint32_t lba, uint32_t count, uint8_t *buf)
{
    if (!seek_to_block(img, lba, count))
        return 0;
    return fread(buf, img->sector_size, count, img->f) == count;
}

int
pc98_hdi_write(pc98_hdi_t *img, uint32_t lba, uint32_t count,
               const uint8_t *buf)
{
    if (img->read_only)
        return 0;
    if (!seek_to_block(img, lba, count))
        return 0;
    if (fwrite(buf, img->sector_size, count, img->f) != count)
        return 0;
    fflush(img->f);
    return 1;
}

static int
seek_to_256_block(pc98_hdi_t *img, uint32_t lba, uint32_t count)
{
    if (!img->f || !count)
        return 0;
    const uint64_t blocks256 = ((uint64_t) img->blocks * img->sector_size) /
                               PC98_HDI_BLOCK_SIZE;
    if (lba >= blocks256 || count > blocks256 - lba)
        return 0;
    const uint64_t off = (uint64_t) img->base +
                         (uint64_t) lba * PC98_HDI_BLOCK_SIZE;
    return off <= 0x7ffffffful && fseek(img->f, (long) off, SEEK_SET) == 0;
}

int
pc98_hdi_read_256(pc98_hdi_t *img, uint32_t lba, uint32_t count, uint8_t *buf)
{
    if (!seek_to_256_block(img, lba, count))
        return 0;
    return fread(buf, PC98_HDI_BLOCK_SIZE, count, img->f) == count;
}

int
pc98_hdi_write_256(pc98_hdi_t *img, uint32_t lba, uint32_t count,
                    const uint8_t *buf)
{
    if (img->read_only || !seek_to_256_block(img, lba, count))
        return 0;
    if (fwrite(buf, PC98_HDI_BLOCK_SIZE, count, img->f) != count)
        return 0;
    fflush(img->f);
    return 1;
}


uint32_t
pc98_hdi_sasi_blocks(const pc98_hdi_t *img, uint32_t bios_spt)
{
    if (!img || !img->spt || !bios_spt)
        return 0u;

    if (img->sector_size == PC98_HDI_BLOCK_SIZE)
        return img->blocks;
    if (img->sector_size != 512u ||
        (img->blocks % img->spt) != 0u ||
        ((uint64_t) img->spt * img->sector_size) <
            ((uint64_t) bios_spt * PC98_HDI_BLOCK_SIZE))
        return 0u;

    /* The board ROM converts CHS with 33 logical sectors per track.  On a
     * 17x512 image those 33 blocks occupy the first 8448 bytes of each
     * 8704-byte physical track; the last 256 bytes are not a SASI block. */
    const uint64_t tracks = img->blocks / img->spt;
    const uint64_t blocks = tracks * bios_spt;
    return blocks <= UINT32_MAX ? (uint32_t) blocks : 0u;
}

int
pc98_hdi_sasi_read(pc98_hdi_t *img, uint32_t lba, uint32_t count,
                   uint32_t bios_spt, uint32_t bios_heads, uint8_t *buf)
{
    (void) bios_heads;

    if (!img || !img->f)
        return 0;
    if (img->sector_size == PC98_HDI_BLOCK_SIZE)
        return pc98_hdi_read_256(img, lba, count, buf);

    const uint32_t blocks = pc98_hdi_sasi_blocks(img, bios_spt);
    if (!blocks || !count || lba >= blocks || count > blocks - lba)
        return 0;

    while (count) {
        const uint32_t track = lba / bios_spt;
        const uint32_t slot = lba % bios_spt;
        uint32_t chunk = bios_spt - slot;
        if (chunk > count)
            chunk = count;

        const uint64_t off = (uint64_t) img->base +
                             (uint64_t) track * img->spt * img->sector_size +
                             (uint64_t) slot * PC98_HDI_BLOCK_SIZE;
        const uint64_t end = off + (uint64_t) chunk * PC98_HDI_BLOCK_SIZE;
        if (end > 0x7fffffffull || fseek(img->f, (long) off, SEEK_SET) != 0 ||
            fread(buf, PC98_HDI_BLOCK_SIZE, chunk, img->f) != chunk)
            return 0;

        lba += chunk;
        count -= chunk;
        buf += (size_t) chunk * PC98_HDI_BLOCK_SIZE;
    }

    return 1;
}

int
pc98_hdi_sasi_write(pc98_hdi_t *img, uint32_t lba, uint32_t count,
                    uint32_t bios_spt, uint32_t bios_heads,
                    const uint8_t *buf)
{
    (void) bios_heads;

    if (!img || !img->f || img->read_only)
        return 0;
    if (img->sector_size == PC98_HDI_BLOCK_SIZE)
        return pc98_hdi_write_256(img, lba, count, buf);

    const uint32_t blocks = pc98_hdi_sasi_blocks(img, bios_spt);
    if (!blocks || !count || lba >= blocks || count > blocks - lba)
        return 0;

    while (count) {
        const uint32_t track = lba / bios_spt;
        const uint32_t slot = lba % bios_spt;
        uint32_t chunk = bios_spt - slot;
        if (chunk > count)
            chunk = count;

        const uint64_t off = (uint64_t) img->base +
                             (uint64_t) track * img->spt * img->sector_size +
                             (uint64_t) slot * PC98_HDI_BLOCK_SIZE;
        const uint64_t end = off + (uint64_t) chunk * PC98_HDI_BLOCK_SIZE;
        if (end > 0x7fffffffull || fseek(img->f, (long) off, SEEK_SET) != 0 ||
            fwrite(buf, PC98_HDI_BLOCK_SIZE, chunk, img->f) != chunk)
            return 0;

        lba += chunk;
        count -= chunk;
        buf += (size_t) chunk * PC98_HDI_BLOCK_SIZE;
    }

    return fflush(img->f) == 0;
}
