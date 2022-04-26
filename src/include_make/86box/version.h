/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for project version, branding, and external links.
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2020 Miran Grca.
 */

#define _LSTR(s) L ## s
#define LSTR(s) _LSTR(s)

/* Version info. */
#define EMU_NAME	"86Box"
#define EMU_NAME_W	LSTR(EMU_NAME)

#define EMU_VERSION	"3.5"
#define EMU_VERSION_W	LSTR(EMU_VERSION)
#define EMU_VERSION_EX	"3.50"
#define EMU_VERSION_MAJ	3
#define EMU_VERSION_MIN	5
#define EMU_VERSION_PATCH 0

#define EMU_BUILD_NUM	0

#define EMU_VERSION_FULL	EMU_VERSION
#define EMU_VERSION_FULL_W	EMU_VERSION_W

#define COPYRIGHT_YEAR	"2022"

/* Web URL info. */
#define EMU_SITE	"86box.net"
#define EMU_SITE_W	LSTR(EMU_SITE)
#define EMU_ROMS_URL	"https://github.com/86Box/roms/releases/latest"
#define EMU_ROMS_URL_W	LSTR(EMU_ROMS_URL)
#ifdef RELEASE_BUILD
# define EMU_DOCS_URL	"https://86box.readthedocs.io/en/v3.5/"
#else
# define EMU_DOCS_URL	"https://86box.readthedocs.io"
#endif
#define EMU_DOCS_URL_W	LSTR(EMU_DOCS_URL)
