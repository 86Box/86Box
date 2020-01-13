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
 * Version:	@(#)hdd_image.c	1.0.20	2018/10/28
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <wchar.h>
#include <errno.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../plat.h"
#include "../random.h"
#include "hdd.h"


typedef struct
{
    FILE *file;
    uint32_t base;
    uint32_t pos, last_sector;
    uint8_t type;
    uint8_t loaded;    
} hdd_image_t;


hdd_image_t hdd_images[HDD_NUM];

static char empty_sector[512];
static char *empty_sector_1mb;


#define VHD_OFFSET_COOKIE 0
#define VHD_OFFSET_FEATURES 8 
#define VHD_OFFSET_VERSION 12
#define VHD_OFFSET_DATA_OFFSET 16
#define VHD_OFFSET_TIMESTAMP 24
#define VHD_OFFSET_CREATOR 28
#define VHD_OFFSET_CREATOR_VERS 32
#define VHD_OFFSET_CREATOR_HOST 36
#define VHD_OFFSET_ORIG_SIZE 40
#define VHD_OFFSET_CURR_SIZE 48
#define VHD_OFFSET_GEOM_CYL 56
#define VHD_OFFSET_GEOM_HEAD 58
#define VHD_OFFSET_GEOM_SPT 59
#define VHD_OFFSET_TYPE 60
#define VHD_OFFSET_CHECKSUM 64
#define VHD_OFFSET_UUID 68
#define VHD_OFFSET_SAVED_STATE 84
#define VHD_OFFSET_RESERVED 85


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
#define hdd_image_log(fmt, ...)
#endif


int
image_is_hdi(const wchar_t *s)
{
    int len;
    wchar_t ext[5] = { 0, 0, 0, 0, 0 };
    char *ws = (char *) s;
    len = wcslen(s);
    if ((len < 4) || (s[0] == L'.'))
	return 0;
    memcpy(ext, ws + ((len - 4) << 1), 8);
    if (! wcscasecmp(ext, L".HDI"))
	return 1;
    else
	return 0;
}


int
image_is_hdx(const wchar_t *s, int check_signature)
{
    int len;
    FILE *f;
    uint64_t filelen;
    uint64_t signature;
    char *ws = (char *) s;
    wchar_t ext[5] = { 0, 0, 0, 0, 0 };
    len = wcslen(s);
    if ((len < 4) || (s[0] == L'.'))
	return 0;
    memcpy(ext, ws + ((len - 4) << 1), 8);
    if (wcscasecmp(ext, L".HDX") == 0) {
	if (check_signature) {
		f = plat_fopen((wchar_t *)s, L"rb");
		if (!f)
			return 0;
		fseeko64(f, 0, SEEK_END);
		filelen = ftello64(f);
		fseeko64(f, 0, SEEK_SET);
		if (filelen < 44)
			return 0;
		fread(&signature, 1, 8, f);
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
image_is_vhd(const wchar_t *s, int check_signature)
{
    int len;
    FILE *f;
    uint64_t filelen;
    uint64_t signature;
    char *ws = (char *) s;
    wchar_t ext[5] = { 0, 0, 0, 0, 0 };
    len = wcslen(s);
    if ((len < 4) || (s[0] == L'.'))
	return 0;
    memcpy(ext, ws + ((len - 4) << 1), 8);
    if (wcscasecmp(ext, L".VHD") == 0) {
	if (check_signature) {
		f = plat_fopen((wchar_t *)s, L"rb");
		if (!f)
			return 0;
		fseeko64(f, 0, SEEK_END);
		filelen = ftello64(f);
		fseeko64(f, -512, SEEK_END);
		if (filelen < 512)
			return 0;
		fread(&signature, 1, 8, f);
		fclose(f);
		if (signature == 0x78697463656E6F63ll)
			return 1;
		else
			return 0;
	} else
		return 1;
    } else
    	return 0;
}


static uint64_t
be_to_u64(uint8_t *bytes, int start)
{
    uint64_t n = ((uint64_t) bytes[start + 7] <<  0) | 
		 ((uint64_t) bytes[start + 6] <<  8) |
		 ((uint64_t) bytes[start + 5] << 16) | 
		 ((uint64_t) bytes[start + 4] << 24) | 
		 ((uint64_t) bytes[start + 3] << 32) | 
		 ((uint64_t) bytes[start + 2] << 40) | 
		 ((uint64_t) bytes[start + 1] << 48) | 
		 ((uint64_t) bytes[start    ] << 56);
    return n;
}


static uint32_t
be_to_u32(uint8_t *bytes, int start)
{
    uint32_t n = ((uint32_t) bytes[start + 3] <<  0) | 
		 ((uint32_t) bytes[start + 2] <<  8) | 
		 ((uint32_t) bytes[start + 1] << 16) | 
		 ((uint32_t) bytes[start    ] << 24);
    return n;
}


static uint16_t
be_to_u16(uint8_t *bytes, int start)
{
    uint16_t n = ((uint16_t) bytes[start + 1] << 0) | 
		 ((uint16_t) bytes[start    ] << 8);
    return n;
}


static uint64_t
u64_to_be(uint64_t value, int is_be)
{
    uint64_t res = 0;
    if (is_be) 
	res = value;
    else {
	uint64_t mask = 0xff00000000000000;
	res = ((value & (mask >>  0)) >> 56) |
	      ((value & (mask >>  8)) >> 40) |
	      ((value & (mask >> 16)) >> 24) |
	      ((value & (mask >> 24)) >>  8) |
	      ((value & (mask >> 32)) <<  8) |
	      ((value & (mask >> 40)) << 24) |
	      ((value & (mask >> 48)) << 40) |
	      ((value & (mask >> 56)) << 56);
    }
    return res;
}


static uint32_t
u32_to_be(uint32_t value, int is_be)
{
    uint32_t res = 0;
    if (is_be) 
	res = value;
    else {
	uint32_t mask = 0xff000000;
	res = ((value & (mask >>  0)) >> 24) |
	      ((value & (mask >>  8)) >>  8) |
	      ((value & (mask >> 16)) <<  8) |
	      ((value & (mask >> 24)) << 24);
    }
    return res;
}


static uint16_t
u16_to_be(uint16_t value, int is_be)
{
    uint16_t res = 0;
    if (is_be) 
	res = value;
    else 
	res = (value >> 8) | (value << 8);

    return res;
}


static void
mk_guid(uint8_t *guid)
{
    int n;

    for (n = 0; n < 16; n++)
	guid[n] = random_generate();

    guid[6] &= 0x0F;
    guid[6] |= 0x40;	/* Type 4 */
    guid[8] &= 0x3F;
    guid[8] |= 0x80;	/* Variant 1 */
}


static uint32_t
calc_vhd_timestamp()
{
    time_t start_time;
    time_t curr_time;
    double vhd_time;
    start_time = 946684800;	/* 1 Jan 2000 00:00 */
    curr_time = time(NULL);
    vhd_time = difftime(curr_time, start_time);

    return (uint32_t)vhd_time;
}


void
vhd_footer_from_bytes(vhd_footer_t *vhd, uint8_t *bytes)
{
    memcpy(vhd->cookie, bytes + VHD_OFFSET_COOKIE, sizeof(vhd->cookie));
    vhd->features = be_to_u32(bytes, VHD_OFFSET_FEATURES);
    vhd->version = be_to_u32(bytes, VHD_OFFSET_VERSION);
    vhd->offset = be_to_u64(bytes, VHD_OFFSET_DATA_OFFSET);
    vhd->timestamp = be_to_u32(bytes, VHD_OFFSET_TIMESTAMP);
    memcpy(vhd->creator, bytes + VHD_OFFSET_CREATOR, sizeof(vhd->creator));
    vhd->creator_vers = be_to_u32(bytes, VHD_OFFSET_CREATOR_VERS);
    memcpy(vhd->creator_host_os, bytes + VHD_OFFSET_CREATOR_HOST, sizeof(vhd->creator_host_os));
    vhd->orig_size = be_to_u64(bytes, VHD_OFFSET_ORIG_SIZE);
    vhd->curr_size = be_to_u64(bytes, VHD_OFFSET_CURR_SIZE);
    vhd->geom.cyl = be_to_u16(bytes, VHD_OFFSET_GEOM_CYL);
    vhd->geom.heads = bytes[VHD_OFFSET_GEOM_HEAD];
    vhd->geom.spt = bytes[VHD_OFFSET_GEOM_SPT];
    vhd->type = be_to_u32(bytes, VHD_OFFSET_TYPE);
    vhd->checksum = be_to_u32(bytes, VHD_OFFSET_CHECKSUM);
    memcpy(vhd->uuid, bytes + VHD_OFFSET_UUID, sizeof(vhd->uuid)); /* TODO: handle UUID's properly */
    vhd->saved_state = bytes[VHD_OFFSET_SAVED_STATE];
    memcpy(vhd->reserved, bytes + VHD_OFFSET_RESERVED, sizeof(vhd->reserved));
}


void
vhd_footer_to_bytes(uint8_t *bytes, vhd_footer_t *vhd)
{
    /* Quick endian check */
    int is_be = 0;
    uint8_t e = 1;
    uint8_t *ep = &e;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;

    if (ep[0] == 0) 
	is_be = 1;

    memcpy(bytes + VHD_OFFSET_COOKIE, vhd->cookie, sizeof(vhd->cookie));
    u32 = u32_to_be(vhd->features, is_be);
    memcpy(bytes + VHD_OFFSET_FEATURES, &u32, sizeof(vhd->features));
    u32 = u32_to_be(vhd->version, is_be);
    memcpy(bytes + VHD_OFFSET_VERSION, &u32, sizeof(vhd->version));
    u64 = u64_to_be(vhd->offset, is_be);
    memcpy(bytes + VHD_OFFSET_DATA_OFFSET, &u64, sizeof(vhd->offset));
    u32 = u32_to_be(vhd->timestamp, is_be);
    memcpy(bytes + VHD_OFFSET_TIMESTAMP, &u32, sizeof(vhd->timestamp));
    memcpy(bytes + VHD_OFFSET_CREATOR, vhd->creator, sizeof(vhd->creator));
    u32 = u32_to_be(vhd->creator_vers, is_be);
    memcpy(bytes + VHD_OFFSET_CREATOR_VERS, &u32, sizeof(vhd->creator_vers));
    memcpy(bytes + VHD_OFFSET_CREATOR_HOST, vhd->creator_host_os, sizeof(vhd->creator_host_os));
    u64 = u64_to_be(vhd->orig_size, is_be);
    memcpy(bytes + VHD_OFFSET_ORIG_SIZE, &u64, sizeof(vhd->orig_size));
    u64 = u64_to_be(vhd->curr_size, is_be);
    memcpy(bytes + VHD_OFFSET_CURR_SIZE, &u64, sizeof(vhd->curr_size));
    u16 = u16_to_be(vhd->geom.cyl, is_be);
    memcpy(bytes + VHD_OFFSET_GEOM_CYL, &u16, sizeof(vhd->geom.cyl));
    memcpy(bytes + VHD_OFFSET_GEOM_HEAD, &(vhd->geom.heads), sizeof(vhd->geom.heads));
    memcpy(bytes + VHD_OFFSET_GEOM_SPT, &(vhd->geom.spt), sizeof(vhd->geom.spt));
    u32 = u32_to_be(vhd->type, is_be);
    memcpy(bytes + VHD_OFFSET_TYPE, &u32, sizeof(vhd->type));
    u32 = u32_to_be(vhd->checksum, is_be);
    memcpy(bytes + VHD_OFFSET_CHECKSUM, &u32, sizeof(vhd->checksum));
    memcpy(bytes + VHD_OFFSET_UUID, vhd->uuid, sizeof(vhd->uuid));
    memcpy(bytes + VHD_OFFSET_SAVED_STATE, &(vhd->saved_state), sizeof(vhd->saved_state));
    memcpy(bytes + VHD_OFFSET_RESERVED, vhd->reserved, sizeof(vhd->reserved));
}


void
new_vhd_footer(vhd_footer_t **vhd)
{
    uint8_t cookie[8] = {'c', 'o', 'n', 'e', 'c', 't', 'i', 'x'};
    uint8_t creator[4] = {'8', '6', 'b', 'x'};
    uint8_t cr_host_os[4] = {'W', 'i', '2', 'k'};

    if (*vhd == NULL)
	*vhd = (vhd_footer_t *) malloc(sizeof(vhd_footer_t));

    memcpy((*vhd)->cookie, cookie, 8);
    (*vhd)->features = 0x00000002;
    (*vhd)->version = 0x00010000;
    (*vhd)->offset = 0xffffffffffffffff; /* fixed disk */
    (*vhd)->timestamp = calc_vhd_timestamp();
    memcpy((*vhd)->creator, creator, 4);
    (*vhd)->creator_vers = 0x00010000;
    memcpy((*vhd)->creator_host_os, cr_host_os, 4);
    (*vhd)->type = 2; /* fixed disk */
    mk_guid((*vhd)->uuid);
    (*vhd)->saved_state = 0;
    memset((*vhd)->reserved, 0, 427);
}


void
generate_vhd_checksum(vhd_footer_t *vhd)
{
    uint32_t chk = 0;
    int i;
    for (i = 0; i < sizeof(vhd_footer_t); i++) {
	/* We don't include the checksum field in the checksum */
	if ((i < VHD_OFFSET_CHECKSUM) || (i >= VHD_OFFSET_UUID))
		chk += ((uint8_t*)vhd)[i];
    }
    vhd->checksum = ~chk;
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
	spt = 255;
	heads = 16;
	cth = (uint32_t) (ts / spt);
    } else {
	spt = 17;
	cth = (uint32_t) (ts / spt);
	heads = (cth +1023) / 1024;
	if (heads < 4)
		heads = 4;
	if ((cth >= (heads * 1024)) || (heads > 16)) {
		spt = 31;
		heads = 16;
		cth = (uint32_t) (ts / spt);
	}
	if (cth >= (heads * 1024)) {
		spt = 63;
		heads = 16;
		cth = (uint32_t) (ts / spt);
	}
    }
    cyl = cth / heads;
    *c = cyl;
    *h = heads;
    *s = spt;
}


static int
prepare_new_hard_disk(uint8_t id, uint64_t full_size)
{
    uint64_t target_size = (full_size + hdd_images[id].base) - ftello64(hdd_images[id].file);

    uint32_t size;
    uint32_t t, i;

    t = (uint32_t) (target_size >> 20);		/* Amount of 1 MB blocks. */
    size = (uint32_t) (target_size & 0xfffff);	/* 1 MB mask. */

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


static void
hdd_image_gen_vft(int id, vhd_footer_t **vft, uint64_t full_size)
{
    /* Generate new footer. */
    new_vhd_footer(vft);
    (*vft)->orig_size = (*vft)->curr_size = full_size;
    (*vft)->geom.cyl = hdd[id].tracks;
    (*vft)->geom.heads = hdd[id].hpc;
    (*vft)->geom.spt = hdd[id].spt;
    generate_vhd_checksum(*vft);
    vhd_footer_to_bytes((uint8_t *) empty_sector, *vft);
    fseeko64(hdd_images[id].file, 0, SEEK_END);
    fwrite(empty_sector, 1, 512, hdd_images[id].file);
    free(*vft);
    *vft = NULL;
    hdd_images[id].type = 3;
}


int
hdd_image_load(int id)
{
    uint32_t sector_size = 512;
    uint32_t zero = 0;
    uint64_t signature = 0xD778A82044445459ll;
    uint64_t full_size = 0;
    uint64_t spt = 0, hpc = 0, tracks = 0;
    int c, ret;
    uint64_t s = 0;
    wchar_t *fn = hdd[id].fn;
    int is_hdx[2] = { 0, 0 };
    int is_vhd[2] = { 0, 0 };
    vhd_footer_t *vft = NULL;

    memset(empty_sector, 0, sizeof(empty_sector));

    hdd_images[id].base = 0;

    if (hdd_images[id].loaded) {
	if (hdd_images[id].file) {
		fclose(hdd_images[id].file);
		hdd_images[id].file = NULL;
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
    hdd_images[id].file = plat_fopen(fn, L"rb+");
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

		hdd_images[id].file = plat_fopen(fn, L"wb+");
		if (hdd_images[id].file == NULL) {
			hdd_image_log("Unable to open image\n");
			memset(hdd[id].fn, 0, sizeof(hdd[id].fn));
			return 0;
		} else {
			if (image_is_hdi(fn)) {
				full_size = ((uint64_t) hdd[id].spt) *
					    ((uint64_t) hdd[id].hpc) *
					    ((uint64_t) hdd[id].tracks) << 9LL;
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
				hdd_images[id].type = 1;
			} else if (is_hdx[0]) {
				full_size = ((uint64_t) hdd[id].spt) *
					    ((uint64_t) hdd[id].hpc) *
					    ((uint64_t) hdd[id].tracks) << 9LL;
				hdd_images[id].base = 0x28;
				fwrite(&signature, 1, 8, hdd_images[id].file);
				fwrite(&full_size, 1, 8, hdd_images[id].file);
				fwrite(&sector_size, 1, 4, hdd_images[id].file);
				fwrite(&(hdd[id].spt), 1, 4, hdd_images[id].file);
				fwrite(&(hdd[id].hpc), 1, 4, hdd_images[id].file);
				fwrite(&(hdd[id].tracks), 1, 4, hdd_images[id].file);
				fwrite(&zero, 1, 4, hdd_images[id].file);
				fwrite(&zero, 1, 4, hdd_images[id].file);
				hdd_images[id].type = 2;
			}
			else
				hdd_images[id].type = 0;
			hdd_images[id].last_sector = 0;
		}

		s = full_size = ((uint64_t) hdd[id].spt) *
				((uint64_t) hdd[id].hpc) *
				((uint64_t) hdd[id].tracks) << 9LL;

		ret = prepare_new_hard_disk(id, full_size);

		if (is_vhd[0]) {
			/* VHD image. */
			hdd_image_gen_vft(id, &vft, full_size);
		}

		return ret;
	} else {
		/* Failed for another reason */
		hdd_image_log("Failed for another reason\n");
		memset(hdd[id].fn, 0, sizeof(hdd[id].fn));
		return 0;
	}
    } else {
	if (image_is_hdi(fn)) {
		fseeko64(hdd_images[id].file, 0x8, SEEK_SET);
		fread(&(hdd_images[id].base), 1, 4, hdd_images[id].file);
		fseeko64(hdd_images[id].file, 0xC, SEEK_SET);
		full_size = 0LL;
		fread(&full_size, 1, 4, hdd_images[id].file);
		fseeko64(hdd_images[id].file, 0x10, SEEK_SET);
		fread(&sector_size, 1, 4, hdd_images[id].file);
		if (sector_size != 512) {
			/* Sector size is not 512 */
			hdd_image_log("HDI: Sector size is not 512\n");
			fclose(hdd_images[id].file);
			hdd_images[id].file = NULL;
			memset(hdd[id].fn, 0, sizeof(hdd[id].fn));
			return 0;
		}
		fread(&spt, 1, 4, hdd_images[id].file);
		fread(&hpc, 1, 4, hdd_images[id].file);
		fread(&tracks, 1, 4, hdd_images[id].file);
		hdd[id].spt = spt;
		hdd[id].hpc = hpc;
		hdd[id].tracks = tracks;
		hdd_images[id].type = 1;
	} else if (is_hdx[1]) {
		hdd_images[id].base = 0x28;
		fseeko64(hdd_images[id].file, 8, SEEK_SET);
		fread(&full_size, 1, 8, hdd_images[id].file);
		fseeko64(hdd_images[id].file, 0x10, SEEK_SET);
		fread(&sector_size, 1, 4, hdd_images[id].file);
		if (sector_size != 512) {
			/* Sector size is not 512 */
			hdd_image_log("HDX: Sector size is not 512\n");
			fclose(hdd_images[id].file);
			hdd_images[id].file = NULL;
			memset(hdd[id].fn, 0, sizeof(hdd[id].fn));
			return 0;
		}
		fread(&spt, 1, 4, hdd_images[id].file);
		fread(&hpc, 1, 4, hdd_images[id].file);
		fread(&tracks, 1, 4, hdd_images[id].file);
		hdd[id].spt = spt;
		hdd[id].hpc = hpc;
		hdd[id].tracks = tracks;
		hdd_images[id].type = 2;
	} else if (is_vhd[1]) {
		fseeko64(hdd_images[id].file, -512, SEEK_END);
		fread(empty_sector, 1, 512, hdd_images[id].file);
		new_vhd_footer(&vft);
		vhd_footer_from_bytes(vft, (uint8_t *) empty_sector);
		if (vft->type != 2) {
			/* VHD is not fixed size */
			hdd_image_log("VHD: Image is not fixed size\n");
			free(vft);
			vft = NULL;
			fclose(hdd_images[id].file);
			hdd_images[id].file = NULL;
			memset(hdd[id].fn, 0, sizeof(hdd[id].fn));
			return 0;
		}
		full_size = vft->orig_size;
		hdd[id].tracks = vft->geom.cyl;
		hdd[id].hpc = vft->geom.heads;
		hdd[id].spt = vft->geom.spt;
		free(vft);
		vft = NULL;
		hdd_images[id].type = 3;
		/* If we're here, this means there is a valid VHD footer in the
		   image, which means that by definition, all valid sectors
		   are there. */
		hdd_images[id].last_sector = (uint32_t) (full_size >> 9) - 1;
		hdd_images[id].loaded = 1;
		return 1;
	} else {
		full_size = ((uint64_t) hdd[id].spt) *
			    ((uint64_t) hdd[id].hpc) *
			    ((uint64_t) hdd[id].tracks) << 9LL;
		hdd_images[id].type = 0;
	}
    }

    fseeko64(hdd_images[id].file, 0, SEEK_END);
    s = ftello64(hdd_images[id].file);
    if (s < (full_size + hdd_images[id].base))
	ret = prepare_new_hard_disk(id, full_size);
    else {
	hdd_images[id].last_sector = (uint32_t) (full_size >> 9) - 1;
	hdd_images[id].loaded = 1;
	ret = 1;
    }

    if (is_vhd[0]) {
	fseeko64(hdd_images[id].file, 0, SEEK_END);
	s = ftello64(hdd_images[id].file);
	if (s == (full_size + hdd_images[id].base)) {
		/* VHD image. */
		hdd_image_gen_vft(id, &vft, full_size);
	}
    }

    return ret;
}


void
hdd_image_seek(uint8_t id, uint32_t sector)
{
    off64_t addr = sector;
    addr = (uint64_t)sector << 9LL;

    hdd_images[id].pos = sector;
    fseeko64(hdd_images[id].file, addr + hdd_images[id].base, SEEK_SET);
}


void
hdd_image_read(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer)
{
    int i;

    fseeko64(hdd_images[id].file, ((uint64_t)(sector) << 9LL) + hdd_images[id].base, SEEK_SET);

    for (i = 0; i < count; i++) {
	if (feof(hdd_images[id].file))
		break;

	hdd_images[id].pos = sector + i;
	fread(buffer + (i << 9), 1, 512, hdd_images[id].file);
    }
}


uint32_t
hdd_sectors(uint8_t id)
{
    fseeko64(hdd_images[id].file, 0, SEEK_END);
    return (uint32_t) ((ftello64(hdd_images[id].file) - hdd_images[id].base) >> 9);
}


int
hdd_image_read_ex(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer)
{
    uint32_t transfer_sectors = count;
    uint32_t sectors = hdd_sectors(id);

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
    int i;

    fseeko64(hdd_images[id].file, ((uint64_t)(sector) << 9LL) + hdd_images[id].base, SEEK_SET);

    for (i = 0; i < count; i++) {
	if (feof(hdd_images[id].file))
		break;

	hdd_images[id].pos = sector + i;
	fwrite(buffer + (i << 9), 512, 1, hdd_images[id].file);
    }
}


int
hdd_image_write_ex(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer)
{
    uint32_t transfer_sectors = count;
    uint32_t sectors = hdd_sectors(id);

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
    uint32_t i = 0;

    memset(empty_sector, 0, 512);

    fseeko64(hdd_images[id].file, ((uint64_t)(sector) << 9LL) + hdd_images[id].base, SEEK_SET);

    for (i = 0; i < count; i++) {
	if (feof(hdd_images[id].file))
		break;

	hdd_images[id].pos = sector + i;
	fwrite(empty_sector, 512, 1, hdd_images[id].file);
    }
}


int
hdd_image_zero_ex(uint8_t id, uint32_t sector, uint32_t count)
{
    uint32_t transfer_sectors = count;
    uint32_t sectors = hdd_sectors(id);

    if ((sectors - sector) < transfer_sectors)
	transfer_sectors = sectors - sector;

    hdd_image_zero(id, sector, transfer_sectors);

    if (count != transfer_sectors)
	return 1;
    return 0;
}


uint32_t
hdd_image_get_last_sector(uint8_t id)
{
    return hdd_images[id].last_sector;
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
    if (wcslen(hdd[id].fn) == 0)
	return;

    if (hdd_images[id].loaded) {
	if (hdd_images[id].file != NULL) {
		fclose(hdd_images[id].file);
		hdd_images[id].file = NULL;
	}
	hdd_images[id].loaded = 0;
    }

    hdd_images[id].last_sector = -1;

    memset(hdd[id].prev_fn, 0, sizeof(hdd[id].prev_fn));
    if (fn_preserve)
	wcscpy(hdd[id].prev_fn, hdd[id].fn);
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
    }
    memset(&hdd_images[id], 0, sizeof(hdd_image_t));
    hdd_images[id].loaded = 0;
}
