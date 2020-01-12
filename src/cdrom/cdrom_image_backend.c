/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		CD-ROM image file handling module, translated to C from
 *		cdrom_dosbox.cpp.
 *
 * Version:	@(#)cdrom_image_backend.c	1.0.2	2020/01/11
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		The DOSBox Team, <unknown>
 *
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2017-2020 Fred N. van Kempen.
 *		Copyright 2002-2020 The DOSBox Team.
 */
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define __STDC_FORMAT_MACROS
#include <stdarg.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#ifdef _WIN32
# include <string.h>
#else
# include <libgen.h>
#endif
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../plat.h"
#include "cdrom_image_backend.h"


#define CDROM_BCD(x)      (((x) % 10) | (((x) / 10) << 4))

#define MAX_LINE_LENGTH		512
#define MAX_FILENAME_LENGTH	256
#define CROSS_LEN		512


#ifdef ENABLE_CDROM_IMAGE_BACKEND_LOG
int cdrom_image_backend_do_log = ENABLE_CDROM_IMAGE_BACKEND_LOG;


void
cdrom_image_backend_log(const char *fmt, ...)
{
    va_list ap;

    if (cdrom_image_backend_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define cdrom_image_backend_log(fmt, ...)
#endif


/* Binary file functions. */
static int
bin_read(void *p, uint8_t *buffer, uint64_t seek, size_t count)
{
    track_file_t *tf = (track_file_t *) p;

    cdrom_image_backend_log("CDROM: binary_read(%08lx, pos=%" PRIu64 " count=%lu\n",
		     tf->file, seek, count);

    if (tf->file == NULL)
	return 0;

    fseeko64(tf->file, seek, SEEK_SET);

    if (fread(buffer, count, 1, tf->file) != 1) {
#ifdef ENABLE_cdrom_image_backend_log
	cdrom_image_backend_log("CDROM: binary_read failed!\n");
#endif
	return 0;
    }

    return 1;
}


static uint64_t
bin_get_length(void *p)
{
    off64_t len;
    track_file_t *tf = (track_file_t *) p;

    cdrom_image_backend_log("CDROM: binary_length(%08lx)\n", bf->file);

    if (tf->file == NULL)
	return 0;

    fseeko64(tf->file, 0, SEEK_END);
    len = ftello64(tf->file);
    cdrom_image_backend_log("CDROM: binary_length(%08lx) = %" PRIu64 "\n", tf->file, len);

    return len;
}


static void
bin_close(void *p)
{
    track_file_t *tf = (track_file_t *) p;

    if (tf == NULL)
	return;

    if (tf->file != NULL) {
	fclose(tf->file);
	tf->file = NULL;
    }

    memset(tf->fn, 0x00, sizeof(tf->fn));

    free(p);
}


static track_file_t *
bin_init(const wchar_t *filename, int *error)
{
    track_file_t *tf = (track_file_t *) malloc(sizeof(track_file_t));

    memset(tf->fn, 0x00, sizeof(tf->fn));
    wcscpy(tf->fn, filename);
    tf->file = plat_fopen64(tf->fn, L"rb");
    cdrom_image_backend_log("CDROM: binary_open(%ls) = %08lx\n", tf->fn, tf->file);

    *error = (tf->file == NULL);

    /* Set the function pointers. */
    if (!*error) {
	tf->read = bin_read;
	tf->get_length = bin_get_length;
	tf->close = bin_close;
    }

    return tf;
}


static track_file_t *
track_file_init(const wchar_t *filename, int *error)
{
    /* Current we only support .BIN files, either combined or one per
       track. In the future, more is planned. */
    return bin_init(filename, error);
}


static void
track_file_close(track_t *trk)
{
    if (trk == NULL)
	return;

    if (trk->file == NULL)
	return;

    trk->file->close(trk->file);
    trk->file = NULL;
}


/* Root functions. */
static void
cdi_clear_tracks(cd_img_t *cdi)
{
    int i;
    track_file_t *last = NULL;
    track_t *cur = NULL;

    if ((cdi->tracks == NULL) || (cdi->tracks_num == 0))
	return;

    for (i = 0; i < cdi->tracks_num; i++) {
	cur = &cdi->tracks[i];

	if (cur->file != last) {
		track_file_close(cur);
		last = cur->file;
	}
    }

    /* Now free the array. */
    free(cdi->tracks);
    cdi->tracks = NULL;

    /* Mark that there's no tracks. */
    cdi->tracks_num = 0;
}


void
cdi_close(cd_img_t *cdi)
{
    cdi_clear_tracks(cdi);
    free(cdi);
}


int
cdi_set_device(cd_img_t *cdi, const wchar_t *path)
{
    if (cdi_load_cue(cdi, path))
	return 1;

    if (cdi_load_iso(cdi, path))
	return 1;

    return 0;
}


/* TODO: This never returns anything other than 1, should it even be an int? */
int
cdi_get_audio_tracks(cd_img_t *cdi, int *st_track, int *end, TMSF *lead_out)
{
    *st_track = 1;
    *end = cdi->tracks_num - 1;
    FRAMES_TO_MSF(cdi->tracks[*end].start + 150, &lead_out->min, &lead_out->sec, &lead_out->fr);

    return 1;
}


/* This replaces both Info and EndInfo, they are specified by a variable. */
int
cdi_get_audio_track_info(cd_img_t *cdi, int end, int track, int *track_num, TMSF *start, uint8_t *attr)
{
    track_t *trk = &cdi->tracks[track - 1];
    int pos = trk->start + 150;

    if ((track < 1) || (track > cdi->tracks_num))
	return 0;

    pos = trk->start + 150;

    FRAMES_TO_MSF(pos, &start->min, &start->sec, &start->fr);
    *track_num = trk->track_number;
    *attr = trk->attr;

    return 1;
}


int
cdi_get_track(cd_img_t *cdi, uint32_t sector)
{
    int i;
    track_t *cur, *next;

    /* There must be at least two tracks - data and lead out. */
    if (cdi->tracks_num < 2)
	return -1;

    /* This has a problem - the code skips the last track, which is
       lead out - is that correct? */
    for (i = 0; i < (cdi->tracks_num - 1); i++) {
	cur = &cdi->tracks[i];
	next = &cdi->tracks[i + 1];
	if ((cur->start <= sector) && (sector < next->start))
		return cur->number;
    }

    return -1;
}


/* TODO: See if track start is adjusted by 150 or not. */
int
cdi_get_audio_sub(cd_img_t *cdi, uint32_t sector, uint8_t *attr, uint8_t *track, uint8_t *index, TMSF *rel_pos, TMSF *abs_pos)
{
    int cur_track = cdi_get_track(cdi, sector);
    track_t *trk;

    if (cur_track < 1)
	return 0;

    *track = (uint8_t) cur_track;
    trk = &cdi->tracks[*track - 1];
    *attr = trk->attr;
    *index = 1;

    FRAMES_TO_MSF(sector + 150, &abs_pos->min, &abs_pos->sec, &abs_pos->fr);

    /* Absolute position should be adjusted by 150, not the relative ones. */
    FRAMES_TO_MSF(sector - trk->start, &rel_pos->min, &rel_pos->sec, &rel_pos->fr);

    return 1;
}


int
cdi_read_sector(cd_img_t *cdi, uint8_t *buffer, int raw, uint32_t sector)
{
    size_t length;
    int track = cdi_get_track(cdi, sector) - 1;
    uint64_t sect = (uint64_t) sector, seek;
    track_t *trk;
    int track_is_raw, ret;
    int raw_size, cooked_size;
    uint64_t offset = 0ULL;
    int m = 0, s = 0, f = 0;

    if (track < 0)
	return 0;

    trk = &cdi->tracks[track];
    track_is_raw = ((trk->sector_size == RAW_SECTOR_SIZE) || (trk->sector_size == 2448));
    if (raw && !track_is_raw)
	return 0;
    seek = trk->skip + ((sect - trk->start) * trk->sector_size);

    if (track_is_raw)
	raw_size = trk->sector_size;
    else
	raw_size = 2448;

    if (trk->mode2 && (trk->form != 1)) {
	if (trk->form == 2)
		cooked_size = (track_is_raw ? 2328 : trk->sector_size);	/* Both 2324 + ECC and 2328 variants are valid. */
	else
		cooked_size = 2336;
    } else
	cooked_size = COOKED_SECTOR_SIZE;

    length = (raw ? raw_size : cooked_size);

    if (trk->mode2 && (trk->form >= 1))
	offset = 24ULL;
    else
	offset = 16ULL;

    if (raw && !track_is_raw) {
	memset(buffer, 0x00, 2448);
    	ret = trk->file->read(trk->file, buffer + offset, seek, length);
	if (!ret)
		return 0;
	/* Construct the rest of the raw sector. */
	memset(buffer + 1, 0xff, 10);
	buffer += 12;
	FRAMES_TO_MSF(sector + 150, &m, &s, &f);
	/* These have to be BCD. */
	buffer[12] = CDROM_BCD(m & 0xff);
	buffer[13] = CDROM_BCD(s & 0xff);
	buffer[14] = CDROM_BCD(f & 0xff);
	buffer[15] = trk->mode2 ? 2 : 1;	/* Data, should reflect the actual sector type. */
	return 1;
    } else if (!raw && track_is_raw)
    	return trk->file->read(trk->file, buffer, seek + offset, length);
    else
    	return trk->file->read(trk->file, buffer, seek, length);
}


int
cdi_read_sectors(cd_img_t *cdi, uint8_t *buffer, int raw, uint32_t sector, uint32_t num)
{
    int sector_size, success = 1;
    uint8_t buf_len, *buf;
    uint32_t i;

    /* TODO: This fails to account for Mode 2. Shouldn't we have a function 
	     to get sector size? */
    sector_size = raw ? RAW_SECTOR_SIZE : COOKED_SECTOR_SIZE;
    buf_len = num * sector_size;
    buf = (uint8_t *) malloc(buf_len * sizeof(uint8_t));

    for (i = 0; i < num; i++) {
	success = cdi_read_sector(cdi, &buf[i * sector_size], raw, sector + i);
	if (!success)
		break;
    }

    memcpy((void *) buffer, buf, buf_len);
    free(buf);
    buf = NULL;

    return success;
}


/* TODO: Do CUE+BIN images with a sector size of 2448 even exist? */
int
cdi_read_sector_sub(cd_img_t *cdi, uint8_t *buffer, uint32_t sector)
{
    int track = cdi_get_track(cdi, sector) - 1;
    track_t *trk;
    uint64_t s = (uint64_t) sector, seek;

    if (track < 0)
	return 0;

    trk = &cdi->tracks[track];
    seek = trk->skip + ((s - trk->start) * trk->sector_size);
    if (trk->sector_size != 2448)
	return 0;

    return trk->file->read(trk->file, buffer, seek, 2448);
}


int
cdi_get_sector_size(cd_img_t *cdi, uint32_t sector)
{
    int track = cdi_get_track(cdi, sector) - 1;
    track_t *trk;

    if (track < 0)
	return 0;

    trk = &cdi->tracks[track];
    return trk->sector_size;
}


int
cdi_is_mode2(cd_img_t *cdi, uint32_t sector)
{
    int track = cdi_get_track(cdi, sector) - 1;
    track_t *trk;

    if (track < 0)
	return 0;

    trk = &cdi->tracks[track];

    return !!(trk->mode2);
}


int
cdi_get_mode2_form(cd_img_t *cdi, uint32_t sector)
{
    int track = cdi_get_track(cdi, sector) - 1;
    track_t *trk;

    if (track < 0)
	return 0;

    trk = &cdi->tracks[track];

    return trk->form;
}


static int
cdi_can_read_pvd(track_file_t *file, uint64_t sector_size, int mode2, int form)
{
    uint8_t pvd[COOKED_SECTOR_SIZE];
    uint64_t seek = 16ULL * sector_size;	/* First VD is located at sector 16. */

    if ((!mode2 || (form == 0)) && (sector_size == RAW_SECTOR_SIZE))
	seek += 16;
    if (mode2 && (form >= 1))
	seek += 24;

    file->read(file, pvd, seek, COOKED_SECTOR_SIZE);

    return ((pvd[0] == 1 && !strncmp((char*)(&pvd[1]), "CD001", 5) && pvd[6] == 1) ||
            (pvd[8] == 1 && !strncmp((char*)(&pvd[9]), "CDROM", 5) && pvd[14] == 1));
}


/* This reallocates the array and returns the pointer to the last track. */
static void
cdi_track_push_back(cd_img_t *cdi, track_t *trk)
{
    /* This has to be done so situations in which realloc would misbehave
       can be detected and reported to the user. */
    if ((cdi->tracks != NULL) && (cdi->tracks_num == 0))
	fatal("CD-ROM Image: Non-null tracks array at 0 loaded tracks\n");
    if ((cdi->tracks == NULL) && (cdi->tracks_num != 0))
	fatal("CD-ROM Image: Null tracks array at non-zero loaded tracks\n");

    cdi->tracks = realloc(cdi->tracks, (cdi->tracks_num + 1) * sizeof(track_t));
    memcpy(&(cdi->tracks[cdi->tracks_num]), trk, sizeof(track_t));
    cdi->tracks_num++;
}


int
cdi_load_iso(cd_img_t *cdi, const wchar_t *filename)
{
    int error;
    track_t trk;

    cdi->tracks = NULL;
    cdi->tracks_num = 0;

    memset(&trk, 0, sizeof(track_t));

    /* Data track (shouldn't there be a lead in track?). */
    trk.file = bin_init(filename, &error);
    if (error) {
	if (trk.file != NULL)
		trk.file->close(trk.file);
	return 0;
    }
    trk.number = 1;
    trk.track_number = 1;
    trk.attr = DATA_TRACK;

    /* Try to detect ISO type. */
    trk.form = 0;
    trk.mode2 = 0;
    /* TODO: Merge the first and last cases since they result in the same thing. */
    if (cdi_can_read_pvd(trk.file, RAW_SECTOR_SIZE, 0, 0))
	trk.sector_size = RAW_SECTOR_SIZE;
    else if (cdi_can_read_pvd(trk.file, 2336, 1, 0)) {
	trk.sector_size = 2336;
	trk.mode2 = 1;
    } else if (cdi_can_read_pvd(trk.file, 2324, 1, 2)) {
	trk.sector_size = 2324;
	trk.mode2 = 1;
	trk.form = 2;
    } else if (cdi_can_read_pvd(trk.file, RAW_SECTOR_SIZE, 1, 0)) {
	trk.sector_size = RAW_SECTOR_SIZE;
	trk.mode2 = 1;
    } else {
	/* We use 2048 mode 1 as the default. */
	trk.sector_size = COOKED_SECTOR_SIZE;
    }

    trk.length = trk.file->get_length(trk.file) / trk.sector_size;
    cdi_track_push_back(cdi, &trk);

    /* Lead out track. */
    trk.number = 2;
    trk.track_number = 0xAA;
    trk.attr = 0x16;	/* Was originally 0x00, but I believe 0x16 is appropriate. */
    trk.start = trk.length;
    trk.length = 0;
    trk.file = NULL;
    cdi_track_push_back(cdi, &trk);

    return 1;
}


static int
cdi_cue_get_buffer(char *str, char **line, int up)
{
    char *s = *line;
    char *p = str;
    int quote = 0;
    int done = 0;
    int space = 1;

    /* Copy to local buffer until we have end of string or whitespace. */
    while (! done) {
	switch(*s) {
		case '\0':
			if (quote) {
				/* Ouch, unterminated string.. */
				return 0;
			}
			done = 1;
			break;

		case '\"':
			quote ^= 1;
			break;

		case ' ':
		case '\t':
			if (space)
				break;

			if (! quote) {
				done = 1;
				break;
			}
			/*FALLTHROUGH*/

		default:
			if (up && islower((int) *s))
				*p++ = toupper((int) *s);
			else
				*p++ = *s;
			space = 0;
			break;
	}

	if (! done)
		s++;
    }
    *p = '\0';

    *line = s;

    return 1;
}


static int
cdi_cue_get_keyword(char **dest, char **line)
{
    char temp[1024];
    int success;

    success = cdi_cue_get_buffer(temp, line, 1);
    if (success)
	*dest = temp;

    return success;
}


/* Get a string from the input line, handling quotes properly. */
static uint64_t
cdi_cue_get_number(char **line)
{
    char temp[128];
    uint64_t num;

    if (!cdi_cue_get_buffer(temp, line, 0))
	return 0;

    if (sscanf(temp, "%" PRIu64, &num) != 1)
	return 0;

    return num;
}


static int
cdi_cue_get_frame(uint64_t *frames, char **line)
{
    char temp[128];
    int min, sec, fr;
    int success;

    success = cdi_cue_get_buffer(temp, line, 0);
    if (! success) return 0;

    success = sscanf(temp, "%d:%d:%d", &min, &sec, &fr) == 3;
    if (! success) return 0;

    *frames = MSF_TO_FRAMES(min, sec, fr);

    return 1;
}


static int
cdi_add_track(cd_img_t *cdi, track_t *cur, uint64_t *shift, uint64_t prestart, uint64_t *total_pregap, uint64_t cur_pregap)
{
    /* Frames between index 0 (prestart) and 1 (current track start) must be skipped. */
    uint64_t skip, temp;
    track_t *prev = NULL;

    if (prestart > 0) {
	if (prestart > cur->start)
		return 0;
	skip = cur->start - prestart;
    } else
	skip = 0ULL;

    if ((cdi->tracks != NULL) && (cdi->tracks_num != 0))
	prev = &cdi->tracks[cdi->tracks_num - 1];

    /* First track (track number must be 1). */
    if (cdi->tracks_num == 0) {
	/* I guess this makes sure the structure is not filled with invalid data. */
	if (cur->number != 1)
		return 0;
	cur->skip = skip * cur->sector_size;
	cur->start += cur_pregap;
	*total_pregap = cur_pregap;
	cdi_track_push_back(cdi, cur);
	return 1;
    }

    /* Current track consumes data from the same file as the previous. */
    if (prev->file == cur->file) {
	cur->start += *shift;
	prev->length = cur->start + *total_pregap - prev->start - skip;
	cur->skip += prev->skip + (prev->length * prev->sector_size) + (skip * cur->sector_size);
	*total_pregap += cur_pregap;
	cur->start += *total_pregap;
    } else {
	temp = prev->file->get_length(prev->file) - ((uint64_t) prev->skip);
	prev->length = temp / ((uint64_t) prev->sector_size);
	if ((temp % prev->sector_size) != 0)
		prev->length++;		/* Padding. */

	cur->start += prev->start + prev->length + cur_pregap;
	cur->skip = skip * cur->sector_size;
	*shift += prev->start + prev->length;
	*total_pregap = cur_pregap;
    }

    /* Error checks. */
    if (cur->number <= 1)
	return 0;
    if ((prev->number + 1) != cur->number)
	return 0;
    if (cur->start < (prev->start + prev->length))
	return 0;

    cdi_track_push_back(cdi, cur);

    return 1;
}


int
cdi_load_cue(cd_img_t *cdi, const wchar_t *cuefile)
{
    track_t trk;
    wchar_t pathname[MAX_FILENAME_LENGTH], filename[MAX_FILENAME_LENGTH];
    wchar_t temp[MAX_FILENAME_LENGTH];
    uint64_t shift = 0ULL, prestart = 0ULL;
    uint64_t cur_pregap = 0ULL, total_pregap = 0ULL;
    uint64_t frame = 0ULL, index;
    int i, success;
    int error, can_add_track = 0;
    FILE *fp;
    char buf[MAX_LINE_LENGTH], ansi[MAX_FILENAME_LENGTH];
    char *line, *command;
    char *type;

    cdi->tracks = NULL;
    cdi->tracks_num = 0;

    memset(&trk, 0, sizeof(track_t));

    /* Get a copy of the filename into pathname, we need it later. */
    memset(pathname, 0, MAX_FILENAME_LENGTH * sizeof(wchar_t));
    plat_get_dirname(pathname, cuefile);

    /* Open the file. */
    fp = plat_fopen((wchar_t *) cuefile, L"r");
    if (fp == NULL)
	return 0;

    success = 0;

    for (;;) {
	line = buf;

	/* Read a line from the cuesheet file. */
	if (feof(fp) || fgets(buf, sizeof(buf), fp) == NULL || ferror(fp))
		break;

	/* Do two iterations to make sure to nuke even if it's \r\n or \n\r,
	   but do checks to make sure we're not nuking other bytes. */
	for (i = 0; i < 2; i++) {
		if (strlen(buf) > 0) {
			if (buf[strlen(buf) - 1] == '\n')
				buf[strlen(buf) - 1] = '\0';	/* nuke trailing newline */
			else if (buf[strlen(buf) - 1] == '\r')
				buf[strlen(buf) - 1] = '\0';	/* nuke trailing newline */
		}
	}

	success = cdi_cue_get_keyword(&command, &line);

	if (!strcmp(command, "TRACK")) {
		if (can_add_track)
			success = cdi_add_track(cdi, &trk, &shift, prestart, &total_pregap, cur_pregap);
		else
			success = 1;

		trk.start = 0;
		trk.skip = 0;
		cur_pregap = 0;
		prestart = 0;

		trk.number = cdi_cue_get_number(&line);
		trk.track_number = trk.number;
		success = cdi_cue_get_keyword(&type, &line);
		if (!success)
			break;

		trk.form = 0;
		trk.mode2 = 0;

		if (!strcmp(type, "AUDIO")) {
			trk.sector_size = RAW_SECTOR_SIZE;
			trk.attr = AUDIO_TRACK;
		} else if (!strcmp(type, "MODE1/2048")) {
			trk.sector_size = COOKED_SECTOR_SIZE;
			trk.attr = DATA_TRACK;
		} else if (!strcmp(type, "MODE1/2352")) {
			trk.sector_size = RAW_SECTOR_SIZE;
			trk.attr = DATA_TRACK;
		} else if (!strcmp(type, "MODE1/2448")) {
			trk.sector_size = 2448;
			trk.attr = DATA_TRACK;
		} else if (!strcmp(type, "MODE2/2048")) {
			trk.form = 1;
			trk.sector_size = COOKED_SECTOR_SIZE;
			trk.attr = DATA_TRACK;
			trk.mode2 = 1;
		} else if (!strcmp(type, "MODE2/2324")) {
			trk.form = 2;
			trk.sector_size = 2324;
			trk.attr = DATA_TRACK;
			trk.mode2 = 1;
		} else if (!strcmp(type, "MODE2/2328")) {
			trk.form = 2;
			trk.sector_size = 2328;
			trk.attr = DATA_TRACK;
			trk.mode2 = 1;
		} else if (!strcmp(type, "MODE2/2336")) {
			trk.sector_size = 2336;
			trk.attr = DATA_TRACK;
			trk.mode2 = 1;
		} else if (!strcmp(type, "MODE2/2352")) {
			trk.form = 1;		/* Assume this is XA Mode 2 Form 1. */
			trk.sector_size = RAW_SECTOR_SIZE;
			trk.attr = DATA_TRACK;
			trk.mode2 = 1;
		} else if (!strcmp(type, "MODE2/2448")) {
			trk.form = 1;		/* Assume this is XA Mode 2 Form 1. */
			trk.sector_size = 2448;
			trk.attr = DATA_TRACK;
			trk.mode2 = 1;
		} else if (!strcmp(type, "CDG/2448")) {
			trk.sector_size = 2448;
			trk.attr = DATA_TRACK;
			trk.mode2 = 1;
		} else if (!strcmp(type, "CDI/2336")) {
			trk.sector_size = 2336;
			trk.attr = DATA_TRACK;
			trk.mode2 = 1;
		} else if (!strcmp(type, "CDI/2352")) {
			trk.sector_size = RAW_SECTOR_SIZE;
			trk.attr = DATA_TRACK;
			trk.mode2 = 1;
		} else
			success = 0;

		can_add_track = 1;
	} else if (!strcmp(command, "INDEX")) {
		index = cdi_cue_get_number(&line);
		success = cdi_cue_get_frame(&frame, &line);

		switch(index) {
			case 0:
				prestart = frame;
				break;

			case 1:
				trk.start = frame;
				break;

			default:
				/* ignore other indices */
				break;
		}
	} else if (!strcmp(command, "FILE")) {
		if (can_add_track)
			success = cdi_add_track(cdi, &trk, &shift, prestart, &total_pregap, cur_pregap);
		else
			success = 1;
		can_add_track = 0;

		memset(ansi, 0, MAX_FILENAME_LENGTH * sizeof(char));
		memset(filename, 0, MAX_FILENAME_LENGTH * sizeof(wchar_t));

		success = cdi_cue_get_buffer(ansi, &line, 0);
		if (!success)
			break;
		success = cdi_cue_get_keyword(&type, &line);
		if (!success)
			break;

		trk.file = NULL;
		error = 1;

		if (!strcmp(type, "BINARY")) {
			memset(temp, 0, MAX_FILENAME_LENGTH * sizeof(wchar_t));
			mbstowcs(temp, ansi, sizeof_w(temp));
			plat_append_filename(filename, pathname, temp);
			trk.file = track_file_init(filename, &error);
		}
		if (error) {
#ifdef ENABLE_cdrom_image_backend_log
			cdrom_image_backend_log("CUE: cannot open fille '%ls' in cue sheet!\n",
					 filename);
#endif
			if (trk.file != NULL) {
				trk.file->close(trk.file);
				trk.file = NULL;
			}
			success = 0;
		}
	} else if (!strcmp(command, "PREGAP"))
		success = cdi_cue_get_frame(&cur_pregap, &line);
	else if (!strcmp(command, "CATALOG") || !strcmp(command, "CDTEXTFILE") || !strcmp(command, "FLAGS") || !strcmp(command, "ISRC") ||
		 !strcmp(command, "PERFORMER") || !strcmp(command, "POSTGAP") || !strcmp(command, "REM") ||
		 !strcmp(command, "SONGWRITER") || !strcmp(command, "TITLE") || !strcmp(command, "")) {
		/* Ignored commands. */
		success = 1;
	} else {
#ifdef ENABLE_cdrom_image_backend_log
		cdrom_image_backend_log("CUE: unsupported command '%s' in cue sheet!\n",
				 command.c_str());
#endif
		success = 0;
	}

	if (!success)
		break;
    }

    fclose(fp);
    if (!success)
	return 0;

    /* Add last track. */
    if (!cdi_add_track(cdi, &trk, &shift, prestart, &total_pregap, cur_pregap))
	return 0;

    /* Add lead out track. */
    trk.number++;
    trk.track_number = 0xAA;
    trk.attr = 0x16;	/* Was 0x00 but I believe 0x16 is appropriate. */
    trk.start = 0;
    trk.length = 0;
    trk.file = NULL;
    if (!cdi_add_track(cdi, &trk, &shift, 0, &total_pregap, 0))
	return 0;

    return 1;
}


int
cdi_has_data_track(cd_img_t *cdi)
{
    int i;

    if ((cdi == NULL) || (cdi->tracks == NULL))
	return 0;

    /* Data track has attribute 0x14. */
    for (i = 0; i < cdi->tracks_num; i++) {
	if (cdi->tracks[i].attr == DATA_TRACK)
		return 1;
    }

    return 0;
}


int
cdi_has_audio_track(cd_img_t *cdi)
{
    int i;

    if ((cdi == NULL) || (cdi->tracks == NULL))
	return 0;

    /* Audio track has attribute 0x14. */
    for (i = 0; i < cdi->tracks_num; i++) {
	if (cdi->tracks[i].attr == AUDIO_TRACK)
		return 1;
    }

    return 0;
}
