/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Main emulator include file.
 *
 * Version:	@(#)86box.h	1.0.2	2017/06/04
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2016-2017 Miran Grca.
 */
#ifndef BOX_H
# define BOX_H


#if defined(ENABLE_BUSLOGIC_LOG) || \
    defined(ENABLE_CDROM_LOG) || \
    defined(ENABLE_D86F_LOG) || \
    defined(ENABLE_FDC_LOG) || \
    defined(ENABLE_IDE_LOG) || \
    defined(ENABLE_NIC_LOG)
# define ENABLE_LOG_TOGGLES	1
#endif

#if defined(ENABLE_LOG_BREAKPOINT) || defined(ENABLE_VRAM_DUMP)
# define ENABLE_LOG_COMMANDS	1
#endif

#define EMU_VERSION	"2.00"
#define EMU_VERSION_W	L"2.00"

#define EMU_NAME	"86Box"
#define EMU_NAME_W	L"86Box"

#define CONFIG_FILE_W	L"86box.cfg"


#endif	/*BOX_H*/
