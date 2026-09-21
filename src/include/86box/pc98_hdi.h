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
    PC98_HDI_ANEX86,   /* .hdi - Anex86                      */
    PC98_HDI_T98NEXT   /* .nhd - T98-Next                    */
} pc98_hdi_format_t;

typedef struct pc98_hdi_t {
    FILE             *f;
    pc98_hdi_format_t format;
    uint32_t          base;        /* offset of the first data byte      */
    uint32_t          sector_size; /* 256 or 512, from the header        */
    uint32_t          spt;         /* sectors per track                  */
    uint32_t          hpc;         /* heads per cylinder                 */
    uint32_t          cylinders;
    uint32_t          blocks;      /* total capacity in native sectors  */
    int               read_only;
} pc98_hdi_t;

/*
 * Returns 1 when the file was recognised AND its header passes the
 * consistency check below, 0 otherwise.  A file that is not one of these
 * containers is not an error - the caller falls back to 86Box's image
 * layer.
 */
int pc98_hdi_open(pc98_hdi_t *img, const char *path);
void pc98_hdi_close(pc98_hdi_t *img);

/* lba and count are in img->sector_size logical sectors. */
int pc98_hdi_read(pc98_hdi_t *img, uint32_t lba, uint32_t count, uint8_t *buf);
int pc98_hdi_write(pc98_hdi_t *img, uint32_t lba, uint32_t count,
                   const uint8_t *buf);

/* Byte-linear 256-byte view used when CT=0 is explicitly selected over a
 * container whose host storage sectors are 512 bytes. */
int pc98_hdi_read_256(pc98_hdi_t *img, uint32_t lba, uint32_t count,
                      uint8_t *buf);
int pc98_hdi_write_256(pc98_hdi_t *img, uint32_t lba, uint32_t count,
                       const uint8_t *buf);

/* PC-9801-27 logical access. On 512-byte media, 33 logical blocks occupy
 * 17 physical sectors and the last high half is skipped. */
int pc98_hdi_sasi_read(pc98_hdi_t *img, uint32_t lba, uint32_t count,
                       uint32_t bios_spt, uint32_t bios_heads, uint8_t *buf);
int pc98_hdi_sasi_write(pc98_hdi_t *img, uint32_t lba, uint32_t count,
                        uint32_t bios_spt, uint32_t bios_heads,
                        const uint8_t *buf);

/*
 * Header sanity check, exposed for the tests.  A header is accepted only
 * when the geometry it states accounts for the data actually in the file,
 * and when the sector size is one of the two this can represent.  That
 * makes a misparsed or hand-edited header fail loudly at load rather than
 * silently reading a drive at the wrong offsets.
 */
int pc98_hdi_header_is_sane(const pc98_hdi_t *img, uint64_t file_size);

#ifdef __cplusplus
}
#endif

#endif /*EMU_PC98_HDI_H*/
