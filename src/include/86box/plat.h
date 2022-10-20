/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Define the various platform support functions.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 *		Copyright 2021 Laci bá'
 */

#ifndef EMU_PLAT_H
#define EMU_PLAT_H

#include <stdint.h>
#include <stdio.h>
#include "86box/device.h"
#include "86box/machine.h"
#ifndef GLOBAL
#    define GLOBAL extern
#endif

/* String ID numbers. */
#include <86box/language.h>

/* The Win32 API uses _wcsicmp. */
#ifdef _WIN32
#    define wcscasecmp _wcsicmp
#    define strcasecmp _stricmp
#else
/* Declare these functions to avoid warnings. They will redirect to strcasecmp and strncasecmp respectively. */
extern int stricmp(const char *s1, const char *s2);
extern int strnicmp(const char *s1, const char *s2, size_t n);
#endif

#if (defined(__HAIKU__) || defined(__unix__) || defined(__APPLE__)) && !defined(__linux__)
/* FreeBSD has largefile by default. */
#    define fopen64  fopen
#    define fseeko64 fseeko
#    define ftello64 ftello
#    define off64_t  off_t
#elif defined(_MSC_VER)
//# define fopen64	fopen
#    define fseeko64 _fseeki64
#    define ftello64 _ftelli64
#    define off64_t  off_t
#endif

#ifdef _MSC_VER
#    define UNUSED(arg) arg
#else
/* A hack (GCC-specific?) to allow us to ignore unused parameters. */
#    define UNUSED(arg) __attribute__((unused)) arg
#endif

/* Return the size (in wchar's) of a wchar_t array. */
#define sizeof_w(x) (sizeof((x)) / sizeof(wchar_t))

#ifdef __cplusplus
#    include <atomic>
#    define atomic_flag_t std::atomic_flag
#    define atomic_bool_t std::atomic_bool
extern "C" {
#else
#    include <stdatomic.h>
#    define atomic_flag_t atomic_flag
#    define atomic_bool_t atomic_bool
#endif

#if defined(_MSC_VER)
#    define ssize_t intptr_t
#endif

/* Global variables residing in the platform module. */
extern int dopause,          /* system is paused */
    mouse_capture;           /* mouse is captured in app */
extern volatile int is_quit; /* system exit requested */

#ifdef MTR_ENABLED
extern int tracing_on;
#endif

extern uint64_t timer_freq;
extern int      infocus;
extern char     emu_version[200]; /* version ID string */
extern int      rctrl_is_lalt;
extern int      update_icons;

extern int kbd_req_capture, hide_status_bar, hide_tool_bar;

/* System-related functions. */
extern char    *fix_exe_path(char *str);
extern FILE    *plat_fopen(const char *path, const char *mode);
extern FILE    *plat_fopen64(const char *path, const char *mode);
extern void     plat_remove(char *path);
extern int      plat_getcwd(char *bufp, int max);
extern int      plat_chdir(char *path);
extern void     plat_tempfile(char *bufp, char *prefix, char *suffix);
extern void     plat_get_exe_name(char *s, int size);
extern void     plat_init_rom_paths();
extern int      plat_dir_check(char *path);
extern int      plat_dir_create(char *path);
extern void    *plat_mmap(size_t size, uint8_t executable);
extern void     plat_munmap(void *ptr, size_t size);
extern uint64_t plat_timer_read(void);
extern uint32_t plat_get_ticks(void);
extern uint32_t plat_get_micro_ticks(void);
extern void     plat_delay_ms(uint32_t count);
extern void     plat_pause(int p);
extern void     plat_mouse_capture(int on);
extern int      plat_vidapi(char *name);
extern char    *plat_vidapi_name(int api);
extern int      plat_setvid(int api);
extern void     plat_vidsize(int x, int y);
extern void     plat_setfullscreen(int on);
extern void     plat_resize_monitor(int x, int y, int monitor_index);
extern void     plat_resize_request(int x, int y, int monitor_index);
extern void     plat_resize(int x, int y);
extern void     plat_vidapi_enable(int enabled);
extern void     plat_vidapi_reload(void);
extern void     plat_vid_reload_options(void);
extern uint32_t plat_language_code(char *langcode);
extern void     plat_language_code_r(uint32_t lcid, char *outbuf, int len);

/* Resource management. */
extern void     set_language(uint32_t id);
extern wchar_t *plat_get_string(int id);

/* Emulator start/stop support functions. */
extern void do_start(void);
extern void do_stop(void);

/* Power off. */
extern void plat_power_off(void);

/* Platform-specific device support. */
extern void cassette_mount(char *fn, uint8_t wp);
extern void cassette_eject(void);
extern void cartridge_mount(uint8_t id, char *fn, uint8_t wp);
extern void cartridge_eject(uint8_t id);
extern void floppy_mount(uint8_t id, char *fn, uint8_t wp);
extern void floppy_eject(uint8_t id);
extern void cdrom_mount(uint8_t id, char *fn);
extern void plat_cdrom_ui_update(uint8_t id, uint8_t reload);
extern void zip_eject(uint8_t id);
extern void zip_mount(uint8_t id, char *fn, uint8_t wp);
extern void zip_reload(uint8_t id);
extern void mo_eject(uint8_t id);
extern void mo_mount(uint8_t id, char *fn, uint8_t wp);
extern void mo_reload(uint8_t id);
extern int  ioctl_open(uint8_t id, char d);
extern void ioctl_reset(uint8_t id);
extern void ioctl_close(uint8_t id);

/* Other stuff. */
extern void startblit(void);
extern void endblit(void);
extern void take_screenshot(void);

/* Conversion between UTF-8 and UTF-16. */
extern size_t mbstoc16s(uint16_t dst[], const char src[], int len);
extern size_t c16stombs(char dst[], const uint16_t src[], int len);

#ifdef MTR_ENABLED
extern void init_trace(void);
extern void shutdown_trace(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /*EMU_PLAT_H*/
