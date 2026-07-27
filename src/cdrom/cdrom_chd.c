/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Support for CHD images via libchdr.
 *
 * Authors: TheCollector1995, <mariogplayer@gmail.com>,
 *          Miran Grca, <mgrca8@gmail.com>
 *          Cacodemon345
 *          Aaron Giles
 *          R. Belmont
 *
 *          Copyright 2023 TheCollector1995.
 *          Copyright 2023 Miran Grca.
 *          Copyright 2026 Cacodemon345.
 *          
 *          Copyright - Aaron Giles
 *          Copyright - R. Belmont
 */

#define __STDC_FORMAT_MACROS
#include <ctype.h>
#include <inttypes.h>
#ifdef ENABLE_IMAGE_LOG
#    include <stdarg.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <wchar.h>
#include <zlib.h>
#include <limits.h>
#include <sys/stat.h>
#ifndef _WIN32
#    include <libgen.h>
#endif

#include <libchdr/chd.h>
#include <libchdr/cdrom.h>
#include <libchdr/bitstream.h>
#include <libchdr/macros.h>

#include <86box/86box.h>
#include <86box/log.h>
#include <86box/nvr.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/bswap.h>
#include <86box/cdrom.h>
#include <86box/cdrom_image.h>
#include <86box/cdrom_image_viso.h>
#include <86box/plat_dynld.h>

#undef CD_TRACK_AUDIO
#undef CD_TRACK_MODE2
#undef CD_TRACK_MODE2_MASK
#undef CD_TRACK_MODE_MASK
#undef CD_TRACK_CDI
#undef CD_TRACK_NORMAL
#undef CD_TRACK_UNK_DATA

// Note: CHD files are not able to describe session information yet. Flycast simply hardcodes it.
// Note 2: For CD metadata, if PGTYPE is valid, and starts with 'V', the pregap sectors exist in the file
// and will count in the FRAMES field.
// Otherwise, they do not exist in the file, and need to be handled appropriately.
// Note 3: Postgap shifts the start of the next track on the TOC.
// Whether it actually exists in the file or not is very unclear.
// Note 4: The padding of the frames in the CHD, depends on the FRAMES field itself, nothing else.

typedef struct TrackEntry_CHD
{
    union {
        uint8_t point;
        uint8_t sequence;
    };
    uint32_t sector_size;
    uint32_t track_type;
    uint8_t  subcode_type;
    uint32_t sector_size_pg;
    uint32_t track_type_pg;
    uint8_t  subcode_type_pg;
    bool    pregap_exists_in_file;
    int64_t start; // sectors, incl pregap
    int64_t end; // sectors, incl postgap
    int64_t pregap;
    int64_t postgap;
    uint8_t adr_ctl; // [0:3]CTL, [4:7]ADR
    bool    audioswap;

    // If the pregap exists in the file, start position of index 0 data, index 1 otherwise.
    uint64_t chd_start; // in bytes.
} TrackEntry_CHD;

typedef struct chd_image_t {
    cdrom_t *dev;
    chd_file *img_file;
    const chd_header *header;

    TrackEntry_CHD* track_entries;
    int track_size;

    uint8_t* hunk_bytes;
    uint64_t hunk_size;

    int64_t cur_hunk;

    uint64_t sectors_per_hunk;

    int64_t end_lba;

    raw_track_info_t* rti_infos;
    uint32_t rti_size; // NOT in bytes.

    bool uncompressed;
    bool is_dvd;
    uint8_t* uncompressed_chd_sectors;
    uint64_t uncompressed_length;
} chd_image_t;

typedef struct chd_image_t chd_image_t;

static void
chd_image_get_raw_track_info(UNUSED(const void *local), int *num, uint8_t *rti)
{
    chd_image_t *ioctl = (chd_image_t *) local;

    *num = ioctl->rti_size;
    memcpy(rti, ioctl->rti_infos, *num * 11);
}

static int
chd_image_get_track_info(UNUSED(const void *local), UNUSED(const uint32_t track),
                          UNUSED(int end), UNUSED(track_info_t *ti))
{
    const chd_image_t      *ioctl      = (const chd_image_t *) local;
    const raw_track_info_t *rti        = (const raw_track_info_t *) ioctl->rti_infos;
    int                     ret        = 1;
    int                     trk        = -1;
    int                     next       = -1;
    uint32_t                blocks_num = (ioctl->rti_size);

    if ((track >= 1) && (track < 99))
        for (int i = 0; i < blocks_num; i++)
            if (rti[i].point == track) {
                trk = i;
                break;
            }

    if ((track >= 1) && (track < 98))
        for (int i = 0; i < blocks_num; i++)
            if ((rti[i].point == (track + 1)) && (rti[i].session == rti[trk].session)) {
                next = i;
                break;
            }

    if ((track >= 1) && (track < 99) && (trk != -1) && (next == -1))
        for (int i = 0; i < blocks_num; i++)
            if ((rti[i].point == 0xa2) && (rti[i].session == rti[trk].session)) {
                next = i;
                break;
            }

    if ((track == 0xaa) || (trk == -1)) {
        ret = 0;
    } else {
        if (end) {
            if (next != -1) {
                ti->m = rti[next].pm;
                ti->s = rti[next].ps;
                ti->f = rti[next].pf;
            }
        } else {
            ti->m = rti[trk].pm;
            ti->s = rti[trk].ps;
            ti->f = rti[trk].pf;
        }

        ti->number = rti[trk].point;
        ti->attr   = rti[trk].adr_ctl;
    }

    return ret;
}

static int
chd_image_get_track(const chd_image_t *ioctl, const uint32_t sector)
{
    raw_track_info_t *rti    = (raw_track_info_t *) ioctl->rti_infos;
    int               track  = -1;
    int               tracks = ioctl->rti_size;

    for (int i = (tracks - 1); i >= 0; i--) {
        const raw_track_info_t *ct    = &(rti[i]);
        const uint32_t          start = (ct->pm * 60 * 75) + (ct->ps * 75) + ct->pf - 150;

        if ((ct->point >= 1) && (ct->point <= 99) && (sector >= start)) {
            track = i;
            break;
        }
    }

    return track;
}

static uint32_t
chd_image_get_last_block(const void *local)
{
    chd_image_t *ioctl = (chd_image_t *) local;

    return ioctl->end_lba;
}

static TrackEntry_CHD*
chd_image_allocate_track(chd_image_t *img)
{
    img->track_entries                    = realloc(img->track_entries, img->track_size * sizeof(TrackEntry_CHD) + sizeof(TrackEntry_CHD));
    TrackEntry_CHD *track_info = (TrackEntry_CHD *) (img->track_entries + img->track_size);
    memset(track_info, 0, sizeof(TrackEntry_CHD));
    img->track_size += 1;
    return track_info;
}

// From the horse's mouth (i.e. taken from MAME).
void chd_img_get_info_from_type_string(const char *typestring, uint32_t *trktype, uint32_t *datasize)
{
	if (!strcmp(typestring, "MODE1"))
	{
		*trktype = CD_TRACK_MODE1;
		*datasize = 2048;
	}
	else if (!strcmp(typestring, "MODE1/2048"))
	{
		*trktype = CD_TRACK_MODE1;
		*datasize = 2048;
	}
	else if (!strcmp(typestring, "MODE1_RAW"))
	{
		*trktype = CD_TRACK_MODE1_RAW;
		*datasize = 2352;
	}
	else if (!strcmp(typestring, "MODE1/2352"))
	{
		*trktype = CD_TRACK_MODE1_RAW;
		*datasize = 2352;
	}
	else if (!strcmp(typestring, "MODE2"))
	{
		*trktype = CD_TRACK_MODE2;
		*datasize = 2336;
	}
	else if (!strcmp(typestring, "MODE2/2336"))
	{
		*trktype = CD_TRACK_MODE2;
		*datasize = 2336;
	}
	else if (!strcmp(typestring, "MODE2_FORM1"))
	{
		*trktype = CD_TRACK_MODE2_FORM1;
		*datasize = 2048;
	}
	else if (!strcmp(typestring, "MODE2/2048"))
	{
		*trktype = CD_TRACK_MODE2_FORM1;
		*datasize = 2048;
	}
	else if (!strcmp(typestring, "MODE2_FORM2"))
	{
		*trktype = CD_TRACK_MODE2_FORM2;
		*datasize = 2324;
	}
	else if (!strcmp(typestring, "MODE2/2324"))
	{
		*trktype = CD_TRACK_MODE2_FORM2;
		*datasize = 2324;
	}
	else if (!strcmp(typestring, "MODE2_FORM_MIX"))
	{
		*trktype = CD_TRACK_MODE2_FORM_MIX;
		*datasize = 2336;
	}
	else if (!strcmp(typestring, "MODE2_RAW"))
	{
		*trktype = CD_TRACK_MODE2_RAW;
		*datasize = 2352;
	}
	else if (!strcmp(typestring, "MODE2/2352"))
	{
		*trktype = CD_TRACK_MODE2_RAW;
		*datasize = 2352;
	}
	else if (!strcmp(typestring, "CDI/2352"))
	{
		*trktype = CD_TRACK_MODE2_RAW;
		*datasize = 2352;
	}
	else if (!strcmp(typestring, "AUDIO"))
	{
		*trktype = CD_TRACK_AUDIO;
		*datasize = 2352;
	}
}

static int
chd_image_has_audio(UNUSED(const void *local))
{
    chd_image_t *img = (chd_image_t *) local;
    for (unsigned int i = 0; i < img->track_size; i++) {
        if (img->track_entries[i].track_type == CD_TRACK_AUDIO)
            return 1;
    }
    return 0;
}

static void
chd_image_close(void *local)
{
    chd_image_t *img = local;
    if (img->hunk_bytes)
        free(img->hunk_bytes);
    if (img->track_entries)
        free(img->track_entries);
    if (img->rti_infos)
        free(img->rti_infos);
    if (img->img_file)
        chd_close(img->img_file);
    free(img);
}

static int
chd_image_track_audio(const chd_image_t *ioctl, const uint32_t pos)
{
    raw_track_info_t *rti = (raw_track_info_t *) ioctl->rti_infos;
    int               ret = 0;

    if (1) {
        const int track   = chd_image_get_track(ioctl, pos);
        const int control = rti[track].adr_ctl;

        ret = !(control & 0x04);
    }

    return ret;
}

static void sub_to_interleaved(const uint8_t *s, uint8_t *d)
{
	for (int i = 0; i < 8 * 12; i ++) {
		int dmask = 0x80;
		int smask = 1 << (7 - (i & 7));
		(*d) = 0;
		for (int j = 0; j < 8; j++) {
			(*d) |= (s[(i / 8) + j * 12] & smask) ? dmask : 0;
			dmask >>= 1;
		}
		d++;
	}
}

static int
chd_image_read_sector_dvd(const void *local, UNUSED(uint8_t *buffer), UNUSED(uint32_t sector))
{
    chd_image_t *ioctl = (chd_image_t *) local;
    int          m = 0, s = 0, f = 0;
    if (sector == ~0u)
        sector = ioctl->dev->seek_pos;

    uint32_t lba = sector;

    memset(buffer, 0, 2448);

    uint64_t chd_offset       = 2048 * sector;
    int64_t  hunk_to_use      = (chd_offset / 2048) / ioctl->sectors_per_hunk;
    uint64_t offset_from_hunk = chd_offset - hunk_to_use * ioctl->hunk_size;

    if (ioctl->uncompressed) {
        hunk_to_use      = 0;
        offset_from_hunk = chd_offset;
    } else {
        if (hunk_to_use != ioctl->cur_hunk) {
            chd_error res = chd_read(ioctl->img_file, (uint32_t) hunk_to_use, ioctl->hunk_bytes);
            if (res != CHDERR_NONE) {
                pclog("Failed to read hunk %lld\n", hunk_to_use);
                return 0;
            }
            ioctl->cur_hunk = hunk_to_use;
        }
    }

    memcpy(&buffer[16], &ioctl->hunk_bytes[offset_from_hunk], 2048);
    /* Sync bytes. */
    buffer[0] = 0x00;
    memset(&(buffer[1]), 0xff, 10);
    buffer[11] = 0x00;

    /* Sector header. */
    FRAMES_TO_MSF(lba + 150, &m, &s, &f);
    buffer[12] = bin2bcd(m);
    buffer[13] = bin2bcd(s);
    buffer[14] = bin2bcd(f);

    /* Mode 1/Mode 2 data. */
    buffer[15]   = 1;
    uint32_t crc = cdrom_crc32(0xffffffff, buffer, 2064) ^ 0xffffffff;
    memcpy(&(buffer[2064]), &crc, 4);

    /* Compute ECC P code. */
    cdrom_compute_ecc_block(ioctl->dev, &(buffer[2076]), &(buffer[12]), 86, 24, 2, 86, 0);

    /* Compute ECC Q code. */
    cdrom_compute_ecc_block(ioctl->dev, &(buffer[2248]), &(buffer[12]), 52, 43, 86, 88, 0);

    /* Construct Q. */
    buffer[2352 + 0] = 0x41;
    buffer[2352 + 1] = bin2bcd(1);
    buffer[2352 + 2] = 1;
    FRAMES_TO_MSF((int32_t) (lba + 150), &m, &s, &f);
    buffer[2352 + 3] = bin2bcd(m);
    buffer[2352 + 4] = bin2bcd(s);
    buffer[2352 + 5] = bin2bcd(f);
    FRAMES_TO_MSF(lba + 150, &m, &s, &f);
    buffer[2352 + 7] = bin2bcd(m);
    buffer[2352 + 8] = bin2bcd(s);
    buffer[2352 + 9] = bin2bcd(f);
    for (int i = 11; i >= 0; i--)
        for (int j = 7; j >= 0; j--)
            buffer[2352 + (i * 8) + j] = ((buffer[2352 + i] >> (7 - j)) & 0x01) << 6;
    return 1;
}

static int
chd_image_read_sector(const void *local, UNUSED(uint8_t *buffer), UNUSED(uint32_t const sector))
{
    chd_image_t            *ioctl         = (chd_image_t *) local;
    uint64_t                lba           = sector == ~0u ? ioctl->dev->seek_pos : sector;
    int                     track         = chd_image_get_track(local, sector == ~0u ? ioctl->dev->seek_pos : sector);
    raw_track_info_t       *rti           = (raw_track_info_t *) ioctl->rti_infos;
    const raw_track_info_t *ct            = &(rti[track]);
    const uint32_t          start         = (ct->pm * 60 * 75) + (ct->ps * 75) + ct->pf;
    int                     m = 0, s = 0, f = 0;
    int64_t                 pregap_length = 0;
    TrackEntry_CHD*         chd_track = &ioctl->track_entries[track - 3];
    uint32_t                sector_track_type = chd_track->track_type;
    uint32_t                sector_sector_size = chd_track->sector_size;
    uint8_t                 subchannel_exists = chd_track->subcode_type;
    bool                    in_pregap = false;

    if (track == -1) {
        return 0;
    }

    memset(buffer, 0, 2448);

    if (!chd_track->pregap_exists_in_file && lba < (chd_track->start + chd_track->pregap)) {
        in_pregap = true;
    }

    if (lba >= ((chd_track->end + 1) - chd_track->postgap)) {
        in_pregap = true;
        sector_track_type = ~0u;
        sector_sector_size = 0;
        subchannel_exists = false;
    }

    if (lba < (chd_track->start + chd_track->pregap)) {
        sector_track_type = chd_track->track_type_pg;
        sector_sector_size = chd_track->sector_size_pg;
        subchannel_exists = chd_track->subcode_type_pg;
        in_pregap = true;
    }

    uint64_t chd_offset = 0;
    if (chd_track->pregap_exists_in_file) {
        chd_offset = (lba - chd_track->start) * 2448 + chd_track->chd_start;
    } else {
        chd_offset = (lba - (chd_track->start + chd_track->pregap)) * 2448 + chd_track->chd_start;
    }

    int64_t hunk_to_use = (chd_offset / 2448) / ioctl->sectors_per_hunk;
    uint64_t offset_from_hunk = chd_offset - hunk_to_use * ioctl->hunk_size;

    if (!in_pregap || (in_pregap && chd_track->pregap_exists_in_file)) {
        if (ioctl->uncompressed) {
            hunk_to_use = 0;
            offset_from_hunk = chd_offset;
        } else {
            if (hunk_to_use != ioctl->cur_hunk) {
                chd_error res = chd_read(ioctl->img_file, (uint32_t)hunk_to_use, ioctl->hunk_bytes);
                if (res != CHDERR_NONE) {
                    pclog("Failed to read hunk %lld\n", hunk_to_use);
                    return 0;
                }
                ioctl->cur_hunk = hunk_to_use;
            }
        }
    }

    uint32_t crc = 0;

    switch (sector_sector_size) {
        case 2048:
        {
            switch(sector_track_type) {
                case CD_TRACK_MODE1: {
                    if (!in_pregap || (in_pregap && chd_track->pregap_exists_in_file))
                        memcpy(&buffer[16], &ioctl->hunk_bytes[offset_from_hunk], 2048);
                    /* Sync bytes. */
                    buffer[0] = 0x00;
                    memset(&(buffer[1]), 0xff, 10);
                    buffer[11] = 0x00;

                    /* Sector header. */
                    FRAMES_TO_MSF(lba + 150, &m, &s, &f);
                    buffer[12] = bin2bcd(m);
                    buffer[13] = bin2bcd(s);
                    buffer[14] = bin2bcd(f);

                    /* Mode 1/Mode 2 data. */
                    buffer[15]         = 1;
                    crc = cdrom_crc32(0xffffffff, buffer, 2064) ^ 0xffffffff;
                    memcpy(&(buffer[2064]), &crc, 4);

                    /* Compute ECC P code. */
                    cdrom_compute_ecc_block(ioctl->dev, &(buffer[2076]), &(buffer[12]), 86, 24, 2, 86, 0);

                    /* Compute ECC Q code. */
                    cdrom_compute_ecc_block(ioctl->dev, &(buffer[2248]), &(buffer[12]), 52, 43, 86, 88, 0);
                    break;
                }
                case CD_TRACK_MODE2_FORM1: {
                    if (!in_pregap || (in_pregap && chd_track->pregap_exists_in_file))
                        memcpy(&buffer[24], &ioctl->hunk_bytes[offset_from_hunk], 2048);
                    /* Sync bytes. */
                    buffer[0] = 0x00;
                    memset(&(buffer[1]), 0xff, 10);
                    buffer[11] = 0x00;

                    /* Sector header. */
                    FRAMES_TO_MSF(lba + 150, &m, &s, &f);
                    buffer[12] = bin2bcd(m);
                    buffer[13] = bin2bcd(s);
                    buffer[14] = bin2bcd(f);

                    /* Mode 1/Mode 2 data. */
                    buffer[15]         = 2;

                    uint8_t *subheader = buffer + 16;
                    /* Construct the CD-I/XA sub-header. */
                    subheader[2] = subheader[6] = 0;

                    crc = cdrom_crc32(0xffffffff, &(buffer[16]), 2056) ^ 0xffffffff;
                    memcpy(&(buffer[2072]), &crc, 4);

                    /* Compute ECC P code. */
                    cdrom_compute_ecc_block(ioctl->dev, &(buffer[2076]), &(buffer[12]), 86, 24, 2, 86, 1);

                    /* Compute ECC Q code. */
                    cdrom_compute_ecc_block(ioctl->dev, &(buffer[2248]), &(buffer[12]), 52, 43, 86, 88, 1);
                    break;
                }
            }
            break;
        }
        case 2352:
        {
            if (!in_pregap || (in_pregap && chd_track->pregap_exists_in_file))
                memcpy(buffer, &ioctl->hunk_bytes[offset_from_hunk], 2352);
            if (chd_track->audioswap) {
                for (int i = 0; i < 2352; i += 2) {
                    uint8_t samp_1 = buffer[i];
                    buffer[i] = buffer[i + 1];
                    buffer[i + 1] = samp_1;
                }
            }
            break;
        }
        case 2324:
        {
            if (!in_pregap || (in_pregap && chd_track->pregap_exists_in_file))
                memcpy(&buffer[24], &ioctl->hunk_bytes[offset_from_hunk], 2324);
            /* Sync bytes. */
            buffer[0] = 0x00;
            memset(&(buffer[1]), 0xff, 10);
            buffer[11] = 0x00;

            /* Sector header. */
            FRAMES_TO_MSF(lba + 150, &m, &s, &f);
            buffer[12] = bin2bcd(m);
            buffer[13] = bin2bcd(s);
            buffer[14] = bin2bcd(f);

            /* Mode 1/Mode 2 data. */
            buffer[15]         = 2;
            uint8_t *subheader = buffer + 16;
            /* Construct the CD-I/XA sub-header. */
            subheader[2] = subheader[6] = 1 << 5;
            break;
        }
        case 2336:
        {
            if (!in_pregap || (in_pregap && chd_track->pregap_exists_in_file))
                memcpy(&buffer[16], &ioctl->hunk_bytes[offset_from_hunk], 2336);
            switch (chd_track->track_type) {
                case CD_TRACK_MODE2_FORM_MIX:
                case CD_TRACK_MODE2: {
                    /* Sync bytes. */
                    buffer[0] = 0x00;
                    memset(&(buffer[1]), 0xff, 10);
                    buffer[11] = 0x00;

                    /* Sector header. */
                    FRAMES_TO_MSF(lba + 150, &m, &s, &f);
                    buffer[12] = bin2bcd(m);
                    buffer[13] = bin2bcd(s);
                    buffer[14] = bin2bcd(f);

                    /* Mode 1/Mode 2 data. */
                    buffer[15] = 2;
                }
            }
            break;
        }
    }

    if (subchannel_exists) {
        if (subchannel_exists == 2) {
            sub_to_interleaved(&ioctl->hunk_bytes[offset_from_hunk + sector_sector_size], &buffer[2352]);
        } else {
            memcpy(&buffer[2352], &ioctl->hunk_bytes[offset_from_hunk + sector_sector_size], 96);
        }
        return 1;
    }
    /* Construct Q. */
    buffer[2352 + 0] = (ct->adr_ctl >> 4) | ((ct->adr_ctl & 0xf) << 4);
    buffer[2352 + 1] = bin2bcd(ct->point);
    pregap_length    = chd_track->pregap;
    if (pregap_length && ((lba + 150) - start) < pregap_length) {
        /*
            Pre-gap sector relative frame addresses count from
            the pregap length downwards.
        */
        buffer[2352 + 2] = 0;
        FRAMES_TO_MSF((int32_t) ((pregap_length - 1) - (lba + 150 - start)), &m, &s, &f);
    } else {
        buffer[2352 + 2] = 1;
        FRAMES_TO_MSF((int32_t) (lba + 150 - start), &m, &s, &f);
    }
    buffer[2352 + 3] = bin2bcd(m);
    buffer[2352 + 4] = bin2bcd(s);
    buffer[2352 + 5] = bin2bcd(f);
    FRAMES_TO_MSF(lba + 150, &m, &s, &f);
    buffer[2352 + 7] = bin2bcd(m);
    buffer[2352 + 8] = bin2bcd(s);
    buffer[2352 + 9] = bin2bcd(f);
    *(uint16_t*)&buffer[2352 + 10] = bswap16(cdrom_crc16(0xffff, &buffer[2352], 10));
    for (int i = 11; i >= 0; i--)
        for (int j = 7; j >= 0; j--)
            buffer[2352 + (i * 8) + j] = ((buffer[2352 + i] >> (7 - j)) & 0x01) << 6;

    return 1;
}

static uint8_t
chd_image_get_track_type(const void *local, const uint32_t sector)
{
    chd_image_t            *ioctl = (chd_image_t *) local;
    int                     track = chd_image_get_track(ioctl, sector);
    raw_track_info_t       *rti   = (raw_track_info_t *) (ioctl->rti_infos);
    const raw_track_info_t *trk   = &(rti[track]);
    uint8_t                 ret   = 0x00;

    if (chd_image_track_audio(ioctl, sector))
        ret = 0x08; // per CD_TRACK_AUDIO definitions in non-libchdr headers.
    else if (track != -1)
        for (int i = 0; i < ioctl->rti_size; i++) {
            const raw_track_info_t *ct = &(rti[i]);
            const raw_track_info_t *nt = &(rti[i + 1]);

            if (ct->point == 0xa0) {
                uint8_t first = ct->pm;
                uint8_t last  = nt->pm;

                if ((trk->point >= first) && (trk->point <= last)) {
                    ret = ct->ps;
                    break;
                }
            }
        }

    return ret;
}

static int
chd_image_read_dvd_structure(UNUSED(const void *local), UNUSED(const uint8_t layer), UNUSED(const uint8_t format),
                              UNUSED(uint8_t *buffer), UNUSED(uint32_t *info))
{
    return 0;
}

static int
chd_image_is_dvd(UNUSED(const void *local))
{
    chd_image_t            *ioctl = (chd_image_t *) local;
    uint8_t output[2048] = { };
    uint32_t temp_res;
    uint32_t temp_restag;
    uint8_t  temp_resflags;

    return chd_get_metadata(ioctl->img_file, DVD_METADATA_TAG, 0, output, sizeof(output), &temp_res, &temp_restag, &temp_resflags) == CHDERR_NONE;
}

static const cdrom_ops_t chd_image_ops = {
    chd_image_get_track_info,
    chd_image_get_raw_track_info,
    chd_image_read_sector,
    chd_image_get_track_type,
    chd_image_get_last_block,
    chd_image_read_dvd_structure,
    chd_image_is_dvd,
    chd_image_has_audio,
    NULL,
    chd_image_close,
    NULL
};

static const cdrom_ops_t chd_image_dvd_ops = {
    chd_image_get_track_info,
    chd_image_get_raw_track_info,
    chd_image_read_sector_dvd,
    chd_image_get_track_type,
    chd_image_get_last_block,
    chd_image_read_dvd_structure,
    chd_image_is_dvd,
    chd_image_has_audio,
    NULL,
    chd_image_close,
    NULL
};

/* Public functions */
void *
chd_image_open(cdrom_t *dev, const char *path)
{
    chd_file *file = NULL;
    chd_image_t *img = (chd_image_t*)calloc(1, sizeof(chd_image_t));

    chd_open(path, CHD_OPEN_READ, NULL, &file);
    if (file) {
        uint64_t chd_offset = 0;
        uint64_t sector_offset = 0;
        bool mode2_found = false;
        img->img_file = file;
        img->header = chd_get_header(img->img_file);

        if (img->header->version < 5) {
            warning("CHDv4 and earlier versions are not supported.");
            chd_close(file);
            free(img);
            return NULL;
        }

        if (!(img->header->hunkbytes % 2048)) {
            uint8_t output[2048] = { };
            uint32_t temp_res;
            uint32_t temp_restag;
            uint8_t  temp_resflags;

            if (chd_get_metadata(img->img_file, DVD_METADATA_TAG, 0, output, sizeof(output), &temp_res, &temp_restag, &temp_resflags) == CHDERR_NONE) {
                img->hunk_size = img->header->hunkbytes;
                img->sectors_per_hunk = img->hunk_size / 2048;
                img->is_dvd = 1;
                goto precache_start;
            }
        }

        if ((img->header->hunkbytes % (RAW_SECTOR_SIZE + 96)) != 0) {
            chd_close(file);
            free(img);
            return NULL;
        }

        img->hunk_size = img->header->hunkbytes;
        img->sectors_per_hunk = img->hunk_size / (RAW_SECTOR_SIZE + 96);

precache_start:
        switch (chd_precache_level) {
            case 0:
                break;
            case 1:
                {
                    if (chd_precache(file) != CHDERR_NONE) {
                        warning("Failed to precache file \"%s\"!\n", path);
                    }
                    break;
                }
            case 2:
            default:
                {
                    img->uncompressed_chd_sectors = calloc(img->header->totalhunks, img->header->hunkbytes);
                    if (!img->uncompressed_chd_sectors) {
                        warning("Failed to allocate %llu bytes for CHD decompression!\n", (uint64_t)img->header->totalhunks * (uint64_t)img->header->hunkbytes);
                        break;
                    }
                    img->uncompressed_length = (uint64_t)img->header->totalhunks * (uint64_t)img->header->hunkbytes;
                    for (uint64_t i = 0; i < img->header->totalhunks; i++) {
                        if (chd_read(img->img_file, i, img->uncompressed_chd_sectors + i * img->header->hunkbytes) != CHDERR_NONE) {
                            warning("Failed to read hunk %llu for CHD decompression!\n", (long long unsigned)i);
                            free(img->uncompressed_chd_sectors);
                            img->uncompressed_chd_sectors = NULL;
                            img->uncompressed_length = 0;
                            break;
                        }
                    }
                    img->uncompressed = true;
                    break;
                }
        }
        if (!img->uncompressed)
            img->hunk_bytes = calloc(1, img->header->hunkbytes);
        else
            img->hunk_bytes = img->uncompressed_chd_sectors;

        if (img->is_dvd) {
            TrackEntry_CHD* track = chd_image_allocate_track(img);

            track->pregap_exists_in_file = 0;
            track->pregap = 0;
            track->postgap = 0;
            track->point = 1;
            track->start = 0;
            track->adr_ctl = 0x14;
            track->audioswap = false;
            track->end = img->header->unitcount - 1;
            track->track_type_pg = 0;
            track->sector_size_pg = 0;
            track->subcode_type = 0;
            track->subcode_type_pg = 0;
            goto generate_raw_track_info;
        }

        while (1) {
            char type[256] = { };
            char subtype[256] = { };
            char pgtype[256] = { };
            char pgsub[256] = { };
            char temp[1024] = { };
            uint32_t temp_len = 0;
            uint32_t temp_tag = 0;
            uint8_t temp_flags = 0;
            int trk_pregap = 0;
            int trk_postgap = 0;
            int trk_num = 0;
            int frames = 0;

            if (chd_get_metadata(img->img_file, CDROM_TRACK_METADATA2_TAG, img->track_size, temp, sizeof(temp), &temp_len, &temp_tag, &temp_flags) == CHDERR_NONE) {
                sscanf(temp, CDROM_TRACK_METADATA2_FORMAT, &trk_num, type, subtype, &frames, &trk_pregap, pgtype, pgsub, &trk_postgap);
            } else if (chd_get_metadata(img->img_file, CDROM_TRACK_METADATA2_TAG, img->track_size, temp, sizeof(temp), &temp_len, &temp_tag, &temp_flags) == CHDERR_NONE) {
                sscanf(temp, CDROM_TRACK_METADATA_FORMAT, &trk_num, type, subtype, &frames);
            } else {
                break;
            }
            
            TrackEntry_CHD* track = chd_image_allocate_track(img);
            track->chd_start = chd_offset;

            chd_offset += frames * 2448 + (((frames + 3) & ~3) - frames) * 2448;
            track->pregap_exists_in_file = !!(pgtype[0] == 'V');
            track->pregap = trk_pregap;
            track->postgap = trk_postgap;
            track->point = trk_num;
            track->start = sector_offset;
            sector_offset += frames + track->postgap;
            if (!track->pregap_exists_in_file)
                sector_offset += track->pregap;
            track->adr_ctl = 0x10 | ((strcmp(type, "AUDIO")) ? 0x4 : 0x0);
            track->audioswap = !(track->adr_ctl & 4);
            track->end = sector_offset - 1;
            track->track_type_pg = ~0u;
            track->sector_size_pg = 0;
            track->subcode_type = 0;
            track->subcode_type_pg = 0;

            if (!strcmp(subtype, "RW_RAW"))
                track->subcode_type = 1;
            
            if (!strcmp(subtype, "RW"))
                track->subcode_type = 2;

            if (!strcmp(pgsub, "RW_RAW"))
                track->subcode_type_pg = 1;
            
            if (!strcmp(pgsub, "RW"))
                track->subcode_type_pg = 2;

            chd_img_get_info_from_type_string(type, &track->track_type, &track->sector_size);
            chd_img_get_info_from_type_string(pgtype + !!track->pregap_exists_in_file, &track->track_type_pg, &track->sector_size_pg);
            if (track->track_type >= CD_TRACK_MODE2 && track->track_type <= CD_TRACK_MODE2_RAW) {
                mode2_found = true;
            }
        }

        if (img->track_size == 0) {
            warning("CHD image '%s' contains no tracks!", path);
            free(img->hunk_bytes);
            chd_close(file);
            free(img);
            return NULL;
        }
generate_raw_track_info:
        img->rti_infos = calloc(1, sizeof(raw_track_info_t) * (3 + img->track_size));
        img->rti_size = 3 + img->track_size;

        int64_t first_track_sess = (int64_t) LLONG_MAX;
        int64_t last_track_sess  = (int64_t) LLONG_MIN;

        int64_t end_lba  = (int64_t) LLONG_MIN;
        for (int i = 0; i < img->track_size; i++) {
            raw_track_info_t* rti = &img->rti_infos[3 + i];

            rti->m = rti->s = rti->f = rti->zero = 0;
            rti->tno                             = 0;
            rti->session                         = 1;
            rti->adr_ctl                         = img->track_entries[i].adr_ctl;
            rti->point                           = img->track_entries[i].point;
            if (i != 0) {
                rti->pm = (cdrom_lba_to_msf_accurate(img->track_entries[i].start + img->track_entries[i].pregap) >> 16) & 0xFF;
                rti->ps = (cdrom_lba_to_msf_accurate(img->track_entries[i].start + img->track_entries[i].pregap) >> 8) & 0xFF;
                rti->pf = cdrom_lba_to_msf_accurate(img->track_entries[i].start + img->track_entries[i].pregap) & 0xFF;
            } else {
                rti->pm = (cdrom_lba_to_msf_accurate(img->track_entries[i].start) >> 16) & 0xFF;
                rti->ps = (cdrom_lba_to_msf_accurate(img->track_entries[i].start) >> 8) & 0xFF;
                rti->pf = cdrom_lba_to_msf_accurate(img->track_entries[i].start) & 0xFF;
            }

            if (img->track_entries[i].end > end_lba) {
                end_lba = img->track_entries[i].end;
            }
            if (img->track_entries[i].sequence > last_track_sess) {
                last_track_sess = img->track_entries[i].sequence;
            }
            if (img->track_entries[i].sequence < first_track_sess) {
                first_track_sess = img->track_entries[i].sequence;
            }
        }

        img->rti_infos[0].tno     = 0;
        img->rti_infos[0].point   = 0xa0;
        img->rti_infos[0].session = 1;
        img->rti_infos[0].adr_ctl = 0x14;
        img->rti_infos[0].m       = 0;
        img->rti_infos[0].s       = 0;
        img->rti_infos[0].f       = 0;
        img->rti_infos[0].zero    = 0;
        img->rti_infos[0].pm      = first_track_sess;
        img->rti_infos[0].ps      = mode2_found ? 0x20 : 0x00;
        img->rti_infos[0].pf      = 0x00;

        img->rti_infos[1].tno     = 0;
        img->rti_infos[1].point   = 0xa1;
        img->rti_infos[1].session = 1;
        img->rti_infos[1].adr_ctl = 0x10;
        img->rti_infos[1].m       = 0;
        img->rti_infos[1].s       = 0;
        img->rti_infos[1].f       = 0;
        img->rti_infos[1].zero    = 0;
        img->rti_infos[1].pm      = last_track_sess;
        img->rti_infos[1].ps      = 0x00;
        img->rti_infos[1].pf      = 0x00;

        img->rti_infos[2].tno     = 0;
        img->rti_infos[2].point   = 0xa2;
        img->rti_infos[2].session = 1;
        img->rti_infos[2].adr_ctl = 0x10;
        img->rti_infos[2].pm      = (cdrom_lba_to_msf_accurate(end_lba + 1) >> 16) & 0xFF;
        img->rti_infos[2].ps      = (cdrom_lba_to_msf_accurate(end_lba + 1) >> 8) & 0xFF;
        img->rti_infos[2].pf      = cdrom_lba_to_msf_accurate(end_lba + 1) & 0xFF;
        img->rti_infos[2].m       = 0;
        img->rti_infos[2].s       = 0;
        img->rti_infos[2].f       = 0;
        img->rti_infos[2].zero    = 0;

        img->end_lba = end_lba;

        img->dev = dev;
        img->dev->ops = img->is_dvd ? &chd_image_dvd_ops : &chd_image_ops;
        img->cur_hunk = -1;

        return img;
    }
    return NULL;
}

int
cdrom_image_is_chd(const char *fn)
{
    chd_file *file = NULL;

    chd_open(fn, CHD_OPEN_READ, NULL, &file);
    if (file) {
        chd_close(file);
        return 1;
    }
    return 0;
}
