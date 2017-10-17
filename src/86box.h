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
 * Version:	@(#)86box.h	1.0.4	2017/10/16
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef EMU_86BOX_H
# define EMU_86BOX_H


/* Configuration values. */
#define SERIAL_MAX	2
#define PARALLEL_MAX	1


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

#define NVR_PATH        L"nvr"
#define SCREENSHOT_PATH L"screenshots"


/* Global variables. */
#ifdef ENABLE_LOG_TOGGLES
extern int buslogic_do_log;
extern int cdrom_do_log;
extern int d86f_do_log;
extern int fdc_do_log;
extern int ide_do_log;
extern int serial_do_log;
extern int nic_do_log;
#endif

extern int suppress_overscan;
extern int invert_display;
extern int scale;
extern int vid_api;
extern int vid_resize;
extern int winsizex,winsizey;

extern wchar_t exe_path[1024];
extern wchar_t cfg_path[1024];

extern uint64_t timer_freq;		/* plat.h */
extern int infocus;			/* plat.h */

extern int dump_on_exit;
extern int start_in_fullscreen;
extern int window_w, window_h, window_x, window_y, window_remember;


/* Function prototypes. */
#ifdef __cplusplus
extern "C" {
#endif

extern void	pclog(const char *format, ...);
extern void	fatal(const char *format, ...);
extern int	pc_init_modules(void);
extern int	pc_init(int argc, wchar_t *argv[]);
extern void	pc_close(void);
extern void	pc_reset_hard_close(void);
extern void	pc_reset_hard_init(void);
extern void	pc_reset_hard(void);
extern void	pc_reset(int hard);
extern void	pc_full_speed(void);
extern void	pc_speed_changed(void);
extern void	pc_send_cad(void);
extern void	pc_send_cae(void);
extern void	pc_run(void);

#ifdef __cplusplus
}
#endif


#endif	/*EMU_86BOX_H*/
