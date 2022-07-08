/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation POSIX OpenDir(3) and friends for Win32 API.
 *
 *		Based on old original code @(#)dir_win32.c 1.2.0 2007/04/19
 *
 *
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 1998-2007 MicroWalt Corporation
 *		Copyright 2017 Fred N. van Kempen
 */
#include <io.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/plat_dir.h>


#define SUFFIX		"\\*"
#define FINDATA	struct _finddata_t
#define FINDFIRST	_findfirst
#define FINDNEXT	_findnext


/* Open a directory. */
DIR *
opendir(const char *name)
{
    DIR *p;

    /* Create a new control structure. */
    p = (DIR *) malloc(sizeof(DIR));
    if (p == NULL)
	return(NULL);
    memset(p, 0x00, sizeof(DIR));
    p->flags = (DIR_F_LOWER | DIR_F_SANE);
    p->offset = 0;
    p->sts = 0;

    /* Create a work area. */
    p->dta = (char *)malloc(sizeof(FINDATA));
    if (p->dta == NULL) {
	free(p);
	return(NULL);
    }
    memset(p->dta, 0x00, sizeof(struct _finddata_t));

    /* Add search filespec. */
    strcpy(p->dir, name);
    strcat(p->dir, SUFFIX);

    /* Special case: flag if we are in the root directory. */
    if (strlen(p->dir) == 3)
	p->flags |= DIR_F_ISROOT;

    /* Start the searching by doing a FindFirst. */
    p->handle = FINDFIRST(p->dir, (FINDATA *)p->dta);
    if (p->handle < 0L) {
	free(p->dta);
	free(p);
	return(NULL);
    }

    /* All OK. */
    return(p);
}


/* Close an open directory. */
int
closedir(DIR *p)
{
    if (p == NULL)
	return(0);

    _findclose(p->handle);

    if (p->dta != NULL)
	free(p->dta);
    free(p);

    return(0);
}


/*
 * Read the next entry from a directory.
 * Note that the DOS (FAT), Windows (FAT, FAT32) and Windows NTFS
 * file systems do not have a root directory containing the UNIX-
 * standard "." and ".." entries.  Many applications do assume
 * this anyway, so we simply fake these entries.
 */
struct dirent *
readdir(DIR *p)
{
    FINDATA *ffp;

    if (p == NULL || p->sts == 1)
	return(NULL);

    /* Format structure with current data. */
    ffp = (FINDATA *)p->dta;
    p->dent.d_ino = 1L;
    p->dent.d_off = p->offset++;
    switch(p->offset) {
	case 1:		/* . */
		strncpy(p->dent.d_name, ".", MAXNAMLEN+1);
		p->dent.d_reclen = 1;
		break;

	case 2:		/* .. */
		strncpy(p->dent.d_name, "..", MAXNAMLEN+1);
		p->dent.d_reclen = 2;
		break;

	default:	/* regular entry. */
		strncpy(p->dent.d_name, ffp->name, MAXNAMLEN+1);
		p->dent.d_reclen = (char)strlen(p->dent.d_name);
    }

    /* Read next entry. */
    p->sts = 0;

    /* Fake the "." and ".." entries here.. */
    if ((p->flags & DIR_F_ISROOT) && (p->offset <= 2))
	return(&(p->dent));

    /* Get the next entry if we did not fake the above. */
    if (FINDNEXT(p->handle, ffp) < 0)
	p->sts = 1;

    return(&(p->dent));
}


/* Report current position within the directory. */
long
telldir(DIR *p)
{
    return(p->offset);
}


void
seekdir(DIR *p, long newpos)
{
    short pos;

    /* First off, rewind to start of directory. */
    p->handle = FINDFIRST(p->dir, (FINDATA *)p->dta);
    if (p->handle < 0L) {
	p->sts = 1;
	return;
    }
    p->offset = 0;
    p->sts = 0;

    /* If we are rewinding, that's all... */
    if (newpos == 0L) return;

    /* Nope.. read entries until we hit the right spot. */
    pos = (short) newpos;
    while (p->offset != pos) {
	p->offset++;
	if (FINDNEXT(p->handle, (FINDATA *)p->dta) < 0) {
		p->sts = 1;
		return;
	}
    }
}
