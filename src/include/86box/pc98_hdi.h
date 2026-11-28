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
 *          86Box's own image layer is 512-byte-sector only: hdd_image_read()
 *          seeks with `sector << 9` and reads `fread(buffer, 512, count)`,
 *          and hdd_image_load() explicitly refuses an HDI whose header says
 *          anything else ("HDI: Sector size is not 512") and falls back to
 *          treating the file as headerless raw.
 *
 *          PC-98 media exists with 256- and 512-byte physical sector formats.
 *          The PC-9801-27 ROM still addresses 256-byte SASI logical blocks.
 */
#ifndef EMU_PC98_HDI_H
#define EMU_PC98_HDI_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Minimum native sector size, retained for source compatibility. */
#define PC98_HDI_BLOCK_SIZE 256u

typedef enum pc98_hdi_format_t {
    PC98_HDI_NONE = 0,
    PC98_HDI_ANEX86, /* .hdi - Anex86                      */
    PC98_HDI_T98NEXT /* .nhd - T98-Next                    */
} pc98_hdi_format_t;

typedef struct pc98_hdi_t {
    FILE             *f;
    pc98_hdi_format_t format;
    uint32_t          base;        /* offset of the first data byte      */
    uint32_t          sector_size; /* 256..4096, from the header         */
    uint32_t          spt;         /* sectors per track                  */
    uint32_t          hpc;         /* heads per cylinder                 */
    uint32_t          cylinders;
    uint32_t          blocks; /* total capacity in native sectors  */
    int               read_only;
    int               sasi_imported; /* open_sasi selected a separate copy */
} pc98_hdi_t;

/*
 * Returns 1 for a valid native container, 0 for a different file type,
 * and -1 for an HDI/NHD that cannot be opened or has an invalid header.
 * Only 0 permits falling back to the generic image layer: treating a bad
 * native container as raw exposes its header as disk sectors.
 */
int  pc98_hdi_open(pc98_hdi_t *img, const char *path);
void pc98_hdi_close(pc98_hdi_t *img);

/* Open for the PC-9801-27. Compatible media is opened directly; supported
 * PC-98 MS-DOS FAT12/16 media with different geometry is imported into
 * <path>.sasi.hdi. The original is never written. Existing imports are reused
 * only if their recorded source fingerprint matches; they are never replaced.
 * error receives a reason on failure, including unsupported guest boot code. */
int pc98_hdi_open_sasi(pc98_hdi_t *img, const char *path,
                       char *error, size_t error_size);

/* Create a new native SASI image without replacing an existing path. */
int pc98_hdi_import_sasi(pc98_hdi_t *source, const char *output,
                         char *error, size_t error_size);

/* lba and count are in img->sector_size logical sectors. */
int pc98_hdi_read(pc98_hdi_t *img, uint32_t lba, uint32_t count, uint8_t *buf);
int pc98_hdi_write(pc98_hdi_t *img, uint32_t lba, uint32_t count,
                   const uint8_t *buf);

/* Byte-linear 256-byte view of an image payload.  PC-9801-27 callers should
 * use the SASI accessors below so 512-byte physical-track padding is skipped. */
int pc98_hdi_read_256(pc98_hdi_t *img, uint32_t lba, uint32_t count,
                      uint8_t *buf);
int pc98_hdi_write_256(pc98_hdi_t *img, uint32_t lba, uint32_t count,
                       const uint8_t *buf);

/* PC-9801-27 logical access uses 256-byte blocks and 33 blocks per track.
 * A 512-byte container with 17 physical sectors per track stores 33 logical
 * blocks per track; the upper half of its last sector is padding. */
int      pc98_hdi_sasi_read(pc98_hdi_t *img, uint32_t lba, uint32_t count,
                            uint32_t bios_spt, uint32_t bios_heads, uint8_t *buf);
int      pc98_hdi_sasi_write(pc98_hdi_t *img, uint32_t lba, uint32_t count,
                             uint32_t bios_spt, uint32_t bios_heads,
                             const uint8_t *buf);
uint32_t pc98_hdi_sasi_blocks(const pc98_hdi_t *img, uint32_t bios_spt);

/*
 * Header sanity check, exposed for the tests.  A header is accepted only
 * when the geometry it states accounts for the data actually in the file,
 * and when the sector size is a power of two from 256 through 4096. That
 * makes a misparsed or hand-edited header fail loudly at load rather than
 * silently reading a drive at the wrong offsets.
 */
int pc98_hdi_header_is_sane(const pc98_hdi_t *img, uint64_t file_size);

#ifdef __cplusplus
}
#endif

#endif /*EMU_PC98_HDI_H*/
