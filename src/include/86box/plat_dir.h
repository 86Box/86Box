/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the platform OpenDir module.
 *
 *
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2017 Fred N. van Kempen.
 */

#ifndef PLAT_DIR_H
# define PLAT_DIR_H

#ifdef _MAX_FNAME
# define MAXNAMLEN	_MAX_FNAME
#else
# define MAXNAMLEN	15
#endif
# define MAXDIRLEN	127


struct dirent {
    long		d_ino;
    unsigned short 	d_reclen;
    unsigned short	d_off;
#ifdef UNICODE
    wchar_t		d_name[MAXNAMLEN + 1];
#else
    char		d_name[MAXNAMLEN + 1];
#endif
};
#define	d_namlen	d_reclen


typedef struct {
    short	flags;			/* internal flags		*/
    short	offset;			/* offset of entry into dir	*/
    long	handle;			/* open handle to Win32 system	*/
    short	sts;			/* last known status code	*/
    char	*dta;			/* internal work data		*/
#ifdef UNICODE
    wchar_t	dir[MAXDIRLEN+1];	/* open dir			*/
#else
    char	dir[MAXDIRLEN+1];	/* open dir			*/
#endif
    struct dirent dent;			/* actual directory entry	*/
} DIR;


/* Directory routine flags. */
#define DIR_F_LOWER	0x0001		/* force to lowercase		*/
#define DIR_F_SANE	0x0002		/* force this to sane path	*/
#define DIR_F_ISROOT	0x0010		/* this is the root directory	*/


/* Function prototypes. */
extern DIR		*opendir(const char *);
extern struct dirent	*readdir(DIR *);
extern long		telldir(DIR *);
extern void		seekdir(DIR *, long);
extern int		closedir(DIR *);

#define rewinddir(dirp)	seekdir(dirp, 0L)


#endif	/*PLAT_DIR_H*/
