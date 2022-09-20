/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handling of hard disk image files.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>
#include <errno.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/random.h>
#include <86box/hdd.h>
#include "minivhd/minivhd.h"
#include "minivhd/minivhd_internal.h"

#define HDD_IMAGE_RAW 0
#define HDD_IMAGE_HDI 1
#define HDD_IMAGE_HDX 2
#define HDD_IMAGE_VHD 3

typedef struct
{
    FILE     *file; /* Used for HDD_IMAGE_RAW, HDD_IMAGE_HDI, and HDD_IMAGE_HDX. */
    MVHDMeta *vhd;  /* Used for HDD_IMAGE_VHD. */
    uint32_t  base;
    uint32_t  pos, last_sector;
    uint8_t   type; /* HDD_IMAGE_RAW, HDD_IMAGE_HDI, HDD_IMAGE_HDX, or HDD_IMAGE_VHD */
    uint8_t   loaded;
} hdd_image_t;

hdd_image_t hdd_images[HDD_NUM];

static char  empty_sector[512];
static char *empty_sector_1mb;

#ifdef ENABLE_HDD_IMAGE_LOG
int hdd_image_do_log = ENABLE_HDD_IMAGE_LOG;

static void
hdd_image_log(const char *fmt, ...)
{
    va_list ap;

    if (hdd_image_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define hdd_image_log(fmt, ...)
#endif

int
image_is_hdi(const char *s)
{
    if (!strcasecmp(path_get_extension((char *) s), "HDI"))
        return 1;
    else
        return 0;
}

int
image_is_hdx(const char *s, int check_signature)
{
    FILE    *f;
    uint64_t filelen;
    uint64_t signature;

    if (!strcasecmp(path_get_extension((char *) s), "HDX")) {
        if (check_signature) {
            f = plat_fopen(s, "rb");
            if (!f)
                return 0;
            if (fseeko64(f, 0, SEEK_END))
                fatal("image_is_hdx(): Error while seeking");
            filelen = ftello64(f);
            if (fseeko64(f, 0, SEEK_SET))
                fatal("image_is_hdx(): Error while seeking");
            if (filelen < 44) {
                if (f != NULL)
                    fclose(f);
                return 0;
            }
            if (fread(&signature, 1, 8, f) != 8)
                fatal("image_is_hdx(): Error reading signature\n");
            fclose(f);
            if (signature == 0xD778A82044445459ll)
                return 1;
            else
                return 0;
        } else
            return 1;
    } else
        return 0;
}

int
image_is_vhd(const char *s, int check_signature)
{
    FILE *f;

    if (!strcasecmp(path_get_extension((char *) s), "VHD")) {
        if (check_signature) {
            f = plat_fopen(s, "rb");
            if (!f)
                return 0;

            bool is_vhd = mvhd_file_is_vhd(f);
            fclose(f);
            return is_vhd ? 1 : 0;
        } else
            return 1;
    } else
        return 0;
}

void
hdd_image_calc_chs(uint32_t *c, uint32_t *h, uint32_t *s, uint32_t size)
{
    /* Calculate the geometry from size (in MB), using the algorithm provided in
    "Virtual Hard Disk Image Format Specification, Appendix: CHS Calculation" */
    uint64_t ts = ((uint64_t) size) << 11LL;
    uint32_t spt, heads, cyl, cth;
    if (ts > 65535 * 16 * 255)
        ts = 65535 * 16 * 255;

    if (ts >= 65535 * 16 * 63) {
        spt   = 255;
        heads = 16;
        cth   = (uint32_t) (ts / spt);
    } else {
        spt   = 17;
        cth   = (uint32_t) (ts / spt);
        heads = (cth + 1023) / 1024;
        if (heads < 4)
            heads = 4;
        if ((cth >= (heads * 1024)) || (heads > 16)) {
            spt   = 31;
            heads = 16;
            cth   = (uint32_t) (ts / spt);
        }
        if (cth >= (heads * 1024)) {
            spt   = 63;
            heads = 16;
            cth   = (uint32_t) (ts / spt);
        }
    }
    cyl = cth / heads;
    *c  = cyl;
    *h  = heads;
    *s  = spt;
}

static int
prepare_new_hard_disk(uint8_t id, uint64_t full_size)
{
    uint64_t target_size = (full_size + hdd_images[id].base) - ftello64(hdd_images[id].file);

    uint32_t size;
    uint32_t t, i;

    t    = (uint32_t) (target_size >> 20);     /* Amount of 1 MB blocks. */
    size = (uint32_t) (target_size & 0xfffff); /* 1 MB mask. */

    empty_sector_1mb = (char *) malloc(1048576);
    memset(empty_sector_1mb, 0, 1048576);

    /* Temporarily switch off suppression of seen messages so that the
       progress gets displayed. */
    pclog_toggle_suppr();
    pclog("Writing image sectors: [");

    /* First, write all the 1 MB blocks. */
    if (t > 0) {
        for (i = 0; i < t; i++) {
            fseek(hdd_images[id].file, 0, SEEK_END);
            fwrite(empty_sector_1mb, 1, 1048576, hdd_images[id].file);
            pclog("#");
        }
    }

    /* Then, write the remainder. */
    if (size > 0) {
        fseek(hdd_images[id].file, 0, SEEK_END);
        fwrite(empty_sector_1mb, 1, size, hdd_images[id].file);
        pclog("#");
    }
    pclog("]\n");
    /* Switch the suppression of seen messages back on. */
    pclog_toggle_suppr();

    free(empty_sector_1mb);

    hdd_images[id].last_sector = (uint32_t) (full_size >> 9) - 1;

    hdd_images[id].loaded = 1;

    return 1;
}

void
hdd_image_init(void)
{
    int i;

    for (i = 0; i < HDD_NUM; i++)
        memset(&hdd_images[i], 0, sizeof(hdd_image_t));
}

int
hdd_image_load(int id)
{
    uint32_t sector_size = 512;
    uint32_t zero        = 0;
    uint64_t signature   = 0xD778A82044445459ll;
    uint64_t full_size   = 0;
    uint64_t spt = 0, hpc = 0, tracks = 0;
    int      c, ret;
    uint64_t s         = 0;
    char    *fn        = hdd[id].fn;
    int      is_hdx[2] = { 0, 0 };
    int      is_vhd[2] = { 0, 0 };
    int      vhd_error = 0;

    memset(empty_sector, 0, sizeof(empty_sector));
    if (fn) {
        path_normalize(fn);
    }

    hdd_images[id].base = 0;

    if (hdd_images[id].loaded) {
        if (hdd_images[id].file) {
            fclose(hdd_images[id].file);
            hdd_images[id].file = NULL;
        } else if (hdd_images[id].vhd) {
            mvhd_close(hdd_images[id].vhd);
            hdd_images[id].vhd = NULL;
        }
        hdd_images[id].loaded = 0;
    }

    is_hdx[0] = image_is_hdx(fn, 0);
    is_hdx[1] = image_is_hdx(fn, 1);

    is_vhd[0] = image_is_vhd(fn, 0);
    is_vhd[1] = image_is_vhd(fn, 1);

    hdd_images[id].pos = 0;

    /* Try to open existing hard disk image */
    if (fn[0] == '.') {
        hdd_image_log("File name starts with .\n");
        memset(hdd[id].fn, 0, sizeof(hdd[id].fn));
        return 0;
    }
    hdd_images[id].file = plat_fopen(fn, "rb+");
    if (hdd_images[id].file == NULL) {
        /* Failed to open existing hard disk image */
        if (errno == ENOENT) {
            /* Failed because it does not exist,
               so try to create new file */
            if (hdd[id].wp) {
                hdd_image_log("A write-protected image must exist\n");
                memset(hdd[id].fn, 0, sizeof(hdd[id].fn));
                return 0;
            }

            hdd_images[id].file = plat_fopen(fn, "wb+");
            if (hdd_images[id].file == NULL) {
                hdd_image_log("Unable to open image\n");
                memset(hdd[id].fn, 0, sizeof(hdd[id].fn));
                return 0;
            } else {
                if (image_is_hdi(fn)) {
                    full_size           = ((uint64_t) hdd[id].spt) * ((uint64_t) hdd[id].hpc) * ((uint64_t) hdd[id].tracks) << 9LL;
                    hdd_images[id].base = 0x1000;
                    fwrite(&zero, 1, 4, hdd_images[id].file);
                    fwrite(&zero, 1, 4, hdd_images[id].file);
                    fwrite(&(hdd_images[id].base), 1, 4, hdd_images[id].file);
                    fwrite(&full_size, 1, 4, hdd_images[id].file);
                    fwrite(&sector_size, 1, 4, hdd_images[id].file);
                    fwrite(&(hdd[id].spt), 1, 4, hdd_images[id].file);
                    fwrite(&(hdd[id].hpc), 1, 4, hdd_images[id].file);
                    fwrite(&(hdd[id].tracks), 1, 4, hdd_images[id].file);
                    for (c = 0; c < 0x3f8; c++)
                        fwrite(&zero, 1, 4, hdd_images[id].file);
                    hdd_images[id].type = HDD_IMAGE_HDI;
                } else if (is_hdx[0]) {
                    full_size           = ((uint64_t) hdd[id].spt) * ((uint64_t) hdd[id].hpc) * ((uint64_t) hdd[id].tracks) << 9LL;
                    hdd_images[id].base = 0x28;
                    fwrite(&signature, 1, 8, hdd_images[id].file);
                    fwrite(&full_size, 1, 8, hdd_images[id].file);
                    fwrite(&sector_size, 1, 4, hdd_images[id].file);
                    fwrite(&(hdd[id].spt), 1, 4, hdd_images[id].file);
                    fwrite(&(hdd[id].hpc), 1, 4, hdd_images[id].file);
                    fwrite(&(hdd[id].tracks), 1, 4, hdd_images[id].file);
                    fwrite(&zero, 1, 4, hdd_images[id].file);
                    fwrite(&zero, 1, 4, hdd_images[id].file);
                    hdd_images[id].type = HDD_IMAGE_HDX;
                } else if (is_vhd[0]) {
                    fclose(hdd_images[id].file);
                    MVHDGeom geometry;
                    geometry.cyl               = hdd[id].tracks;
                    geometry.heads             = hdd[id].hpc;
                    geometry.spt               = hdd[id].spt;
                    full_size                  = ((uint64_t) hdd[id].spt) * ((uint64_t) hdd[id].hpc) * ((uint64_t) hdd[id].tracks) << 9LL;
                    hdd_images[id].last_sector = (full_size >> 9LL) - 1;

                    hdd_images[id].vhd = mvhd_create_fixed(fn, geometry, &vhd_error, NULL);
                    if (hdd_images[id].vhd == NULL)
                        fatal("hdd_image_load(): VHD: Could not create VHD : %s\n", mvhd_strerr(vhd_error));

                    hdd_images[id].type = HDD_IMAGE_VHD;
                    return 1;
                } else {
                    hdd_images[id].type = HDD_IMAGE_RAW;
                }
                hdd_images[id].last_sector = 0;
            }

            s = full_size = ((uint64_t) hdd[id].spt) * ((uint64_t) hdd[id].hpc) * ((uint64_t) hdd[id].tracks) << 9LL;

            ret = prepare_new_hard_disk(id, full_size);
            return ret;
        } else {
            /* Failed for another reason */
            hdd_image_log("Failed for another reason\n");
            memset(hdd[id].fn, 0, sizeof(hdd[id].fn));
            return 0;
        }
    } else {
        if (image_is_hdi(fn)) {
            if (fseeko64(hdd_images[id].file, 0x8, SEEK_SET) == -1)
                fatal("hdd_image_load(): HDI: Error seeking to offset 0x8\n");
            if (fread(&(hdd_images[id].base), 1, 4, hdd_images[id].file) != 4)
                fatal("hdd_image_load(): HDI: Error reading base offset\n");
            if (fseeko64(hdd_images[id].file, 0xC, SEEK_SET) == -1)
                fatal("hdd_image_load(): HDI: Error seeking to offest 0xC\n");
            full_size = 0LL;
            if (fread(&full_size, 1, 4, hdd_images[id].file) != 4)
                fatal("hdd_image_load(): HDI: Error reading full size\n");
            if (fseeko64(hdd_images[id].file, 0x10, SEEK_SET) == -1)
                fatal("hdd_image_load(): HDI: Error seeking to offset 0x10\n");
            if (fread(&sector_size, 1, 4, hdd_images[id].file) != 4)
                fatal("hdd_image_load(): HDI: Error reading sector size\n");
            if (sector_size != 512) {
                /* Sector size is not 512 */
                hdd_image_log("HDI: Sector size is not 512\n");
                fclose(hdd_images[id].file);
                hdd_images[id].file = NULL;
                memset(hdd[id].fn, 0, sizeof(hdd[id].fn));
                return 0;
            }
            if (fread(&spt, 1, 4, hdd_images[id].file) != 4)
                fatal("hdd_image_load(): HDI: Error reading sectors per track\n");
            if (fread(&hpc, 1, 4, hdd_images[id].file) != 4)
                fatal("hdd_image_load(): HDI: Error reading heads per cylinder\n");
            if (fread(&tracks, 1, 4, hdd_images[id].file) != 4)
                fatal("hdd_image_load(): HDI: Error reading number of tracks\n");
            hdd[id].spt         = spt;
            hdd[id].hpc         = hpc;
            hdd[id].tracks      = tracks;
            hdd_images[id].type = HDD_IMAGE_HDI;
        } else if (is_hdx[1]) {
            hdd_images[id].base = 0x28;
            if (fseeko64(hdd_images[id].file, 8, SEEK_SET) == -1)
                fatal("hdd_image_load(): HDX: Error seeking to offset 0x8\n");
            if (fread(&full_size, 1, 8, hdd_images[id].file) != 8)
                fatal("hdd_image_load(): HDX: Error reading full size\n");
            if (fseeko64(hdd_images[id].file, 0x10, SEEK_SET) == -1)
                fatal("hdd_image_load(): HDX: Error seeking to offset 0x10\n");
            if (fread(&sector_size, 1, 4, hdd_images[id].file) != 4)
                fatal("hdd_image_load(): HDX: Error reading sector size\n");
            if (sector_size != 512) {
                /* Sector size is not 512 */
                hdd_image_log("HDX: Sector size is not 512\n");
                fclose(hdd_images[id].file);
                hdd_images[id].file = NULL;
                memset(hdd[id].fn, 0, sizeof(hdd[id].fn));
                return 0;
            }
            if (fread(&spt, 1, 4, hdd_images[id].file) != 4)
                fatal("hdd_image_load(): HDI: Error reading sectors per track\n");
            if (fread(&hpc, 1, 4, hdd_images[id].file) != 4)
                fatal("hdd_image_load(): HDI: Error reading heads per cylinder\n");
            if (fread(&tracks, 1, 4, hdd_images[id].file) != 4)
                fatal("hdd_image_load(): HDX: Error reading number of tracks\n");
            hdd[id].spt         = spt;
            hdd[id].hpc         = hpc;
            hdd[id].tracks      = tracks;
            hdd_images[id].type = HDD_IMAGE_HDX;
        } else if (is_vhd[1]) {
            fclose(hdd_images[id].file);
            hdd_images[id].file = NULL;
            hdd_images[id].vhd  = mvhd_open(fn, (bool) 0, &vhd_error);
            if (hdd_images[id].vhd == NULL) {
                if (vhd_error == MVHD_ERR_FILE)
                    fatal("hdd_image_load(): VHD: Error opening VHD file '%s': %s\n", fn, strerror(mvhd_errno));
                else
                    fatal("hdd_image_load(): VHD: Error opening VHD file '%s': %s\n", fn, mvhd_strerr(vhd_error));
            } else if (vhd_error == MVHD_ERR_TIMESTAMP) {
                fatal("hdd_image_load(): VHD: Parent/child timestamp mismatch for VHD file '%s'\n", fn);
            }

            hdd[id].tracks      = hdd_images[id].vhd->footer.geom.cyl;
            hdd[id].hpc         = hdd_images[id].vhd->footer.geom.heads;
            hdd[id].spt         = hdd_images[id].vhd->footer.geom.spt;
            full_size           = ((uint64_t) hdd[id].spt) * ((uint64_t) hdd[id].hpc) * ((uint64_t) hdd[id].tracks) << 9LL;
            hdd_images[id].type = HDD_IMAGE_VHD;
            /* If we're here, this means there is a valid VHD footer in the
               image, which means that by definition, all valid sectors
               are there. */
            hdd_images[id].last_sector = (uint32_t) (full_size >> 9) - 1;
            hdd_images[id].loaded      = 1;
            return 1;
        } else {
            full_size           = ((uint64_t) hdd[id].spt) * ((uint64_t) hdd[id].hpc) * ((uint64_t) hdd[id].tracks) << 9LL;
            hdd_images[id].type = HDD_IMAGE_RAW;
        }
    }

    if (fseeko64(hdd_images[id].file, 0, SEEK_END) == -1)
        fatal("hdd_image_load(): Error seeking to the end of file\n");
    s = ftello64(hdd_images[id].file);
    if (s < (full_size + hdd_images[id].base))
        ret = prepare_new_hard_disk(id, full_size);
    else {
        hdd_images[id].last_sector = (uint32_t) (full_size >> 9) - 1;
        hdd_images[id].loaded      = 1;
        ret                        = 1;
    }

    return ret;
}

void
hdd_image_seek(uint8_t id, uint32_t sector)
{
    off64_t addr = sector;
    addr         = (uint64_t) sector << 9LL;

    hdd_images[id].pos = sector;
    if (hdd_images[id].type != HDD_IMAGE_VHD) {
        if (fseeko64(hdd_images[id].file, addr + hdd_images[id].base, SEEK_SET) == -1)
            fatal("hdd_image_seek(): Error seeking\n");
    }
}

void
hdd_image_read(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer)
{
    int    non_transferred_sectors;
    size_t num_read;

    if (hdd_images[id].type == HDD_IMAGE_VHD) {
        non_transferred_sectors = mvhd_read_sectors(hdd_images[id].vhd, sector, count, buffer);
        hdd_images[id].pos      = sector + count - non_transferred_sectors - 1;
    } else {
        if (fseeko64(hdd_images[id].file, ((uint64_t) (sector) << 9LL) + hdd_images[id].base, SEEK_SET) == -1) {
            fatal("Hard disk image %i: Read error during seek\n", id);
            return;
        }

        num_read           = fread(buffer, 512, count, hdd_images[id].file);
        hdd_images[id].pos = sector + num_read;
    }
}

uint32_t
hdd_image_get_last_sector(uint8_t id)
{
    return hdd_images[id].last_sector;
}

uint32_t
hdd_sectors(uint8_t id)
{
    return hdd_image_get_last_sector(id) - 1;
}

int
hdd_image_read_ex(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer)
{
    uint32_t transfer_sectors = count;
    uint32_t sectors          = hdd_sectors(id);

    if ((sectors - sector) < transfer_sectors)
        transfer_sectors = sectors - sector;

    hdd_image_read(id, sector, transfer_sectors, buffer);

    if (count != transfer_sectors)
        return 1;
    return 0;
}

void
hdd_image_write(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer)
{
    int    non_transferred_sectors;
    size_t num_write;

    if (hdd_images[id].type == HDD_IMAGE_VHD) {
        non_transferred_sectors = mvhd_write_sectors(hdd_images[id].vhd, sector, count, buffer);
        hdd_images[id].pos      = sector + count - non_transferred_sectors - 1;
    } else {
        if (fseeko64(hdd_images[id].file, ((uint64_t) (sector) << 9LL) + hdd_images[id].base, SEEK_SET) == -1) {
            fatal("Hard disk image %i: Write error during seek\n", id);
            return;
        }

        num_write          = fwrite(buffer, 512, count, hdd_images[id].file);
        hdd_images[id].pos = sector + num_write;
    }
}

int
hdd_image_write_ex(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer)
{
    uint32_t transfer_sectors = count;
    uint32_t sectors          = hdd_sectors(id);

    if ((sectors - sector) < transfer_sectors)
        transfer_sectors = sectors - sector;

    hdd_image_write(id, sector, transfer_sectors, buffer);

    if (count != transfer_sectors)
        return 1;
    return 0;
}

void
hdd_image_zero(uint8_t id, uint32_t sector, uint32_t count)
{
    if (hdd_images[id].type == HDD_IMAGE_VHD) {
        int non_transferred_sectors = mvhd_format_sectors(hdd_images[id].vhd, sector, count);
        hdd_images[id].pos          = sector + count - non_transferred_sectors - 1;
    } else {
        uint32_t i = 0;

        memset(empty_sector, 0, 512);

        if (fseeko64(hdd_images[id].file, ((uint64_t) (sector) << 9LL) + hdd_images[id].base, SEEK_SET) == -1) {
            fatal("Hard disk image %i: Zero error during seek\n", id);
            return;
        }

        for (i = 0; i < count; i++) {
            if (feof(hdd_images[id].file))
                break;

            hdd_images[id].pos = sector + i;
            fwrite(empty_sector, 512, 1, hdd_images[id].file);
        }
    }
}

int
hdd_image_zero_ex(uint8_t id, uint32_t sector, uint32_t count)
{
    uint32_t transfer_sectors = count;
    uint32_t sectors          = hdd_sectors(id);

    if ((sectors - sector) < transfer_sectors)
        transfer_sectors = sectors - sector;

    hdd_image_zero(id, sector, transfer_sectors);

    if (count != transfer_sectors)
        return 1;
    return 0;
}

uint32_t
hdd_image_get_pos(uint8_t id)
{
    return hdd_images[id].pos;
}

uint8_t
hdd_image_get_type(uint8_t id)
{
    return hdd_images[id].type;
}

void
hdd_image_unload(uint8_t id, int fn_preserve)
{
    if (strlen(hdd[id].fn) == 0)
        return;

    if (hdd_images[id].loaded) {
        if (hdd_images[id].file != NULL) {
            fclose(hdd_images[id].file);
            hdd_images[id].file = NULL;
        } else if (hdd_images[id].vhd != NULL) {
            mvhd_close(hdd_images[id].vhd);
            hdd_images[id].vhd = NULL;
        }
        hdd_images[id].loaded = 0;
    }

    hdd_images[id].last_sector = -1;

    memset(hdd[id].prev_fn, 0, sizeof(hdd[id].prev_fn));
    if (fn_preserve)
        strcpy(hdd[id].prev_fn, hdd[id].fn);
    memset(hdd[id].fn, 0, sizeof(hdd[id].fn));
}

void
hdd_image_close(uint8_t id)
{
    hdd_image_log("hdd_image_close(%i)\n", id);

    if (!hdd_images[id].loaded)
        return;

    if (hdd_images[id].file != NULL) {
        fclose(hdd_images[id].file);
        hdd_images[id].file = NULL;
    } else if (hdd_images[id].vhd != NULL) {
        mvhd_close(hdd_images[id].vhd);
        hdd_images[id].vhd = NULL;
    }

    memset(&hdd_images[id], 0, sizeof(hdd_image_t));
    hdd_images[id].loaded = 0;
}
