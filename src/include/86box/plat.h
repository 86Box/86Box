/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Define the various platform support functions.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2017-2019 Fred N. van Kempen.
 *          Copyright 2021 Laci bá'
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
enum {
    STRING_NET_ERROR,                 /* "Failed to initialize network driver..." */
    STRING_PCAP_ERROR_NO_DEVICES,     /* "No PCap devices found..." */
    STRING_PCAP_ERROR_INVALID_DEVICE, /* "Invalid PCap device..." */
    STRING_GHOSTSCRIPT_ERROR,         /* "Unable to initialize Ghostscript..." */
    STRING_HW_NOT_AVAILABLE_TITLE,    /* "Hardware not available" */
    STRING_HW_NOT_AVAILABLE_MACHINE,  /* "Machine \"%s\" is not available..." */
    STRING_HW_NOT_AVAILABLE_VIDEO,    /* "Video card \"%s\" is not available..." */
    STRING_HW_NOT_AVAILABLE_DEVICE,   /* "Device \"%s\" is not available..." */
    STRING_GHOSTPCL_ERROR,            /* "Unable to initialize GhostPCL..." */
    STRING_ESCP_ERROR,                /* "Unable to find Dot-Matrix fonts..." */
    STRING_EDID_READ_ERROR,           /* "EDID file \"%s\" is invalid." */
    STRING_EDID_TOO_LARGE,            /* "EDID file \"%s\" is too large." */
    STRING_CDROM_OPEN_ISO_ERROR,      /* "Unable to open image or folder \"%s\"" */
    STRING_CDROM_OPEN_CUE_ERROR,      /* "Unable to open Cue sheet \"%s\"" */
    STRING_CDROM_OPEN_MDS_ERROR,      /* "Unable to open MDS file \"%s\"" */
    STRING_CDROM_LOAD_IMAGE_ERROR,    /* "Unable to load CD-ROM image: %s" */
    STRING_CDROM_LOAD_MDSX_ERROR,     /* "Unable to load image \"%s\": %1 is missing..." */
    STRING_CDROM_DVD_IN_CD_DRIVE,     /* "DVD image \"%s\" in a CD-only drive..." */
    STRING_CHARDEV_CONNECT_ERROR,     /* "%s: Could not connect to %s: %s" */
    STRING_CHARDEV_CREATE_ERROR,      /* "%s: Could not create %s: %s" */
    STRING_CHARDEV_ATTACHED,          /* "%s: Attached to %s" */
    STRING_CHARDEV_VCON_IN_USE,       /* "%s: Virtual console already in use by %s" */
    STRING_CHARDEV_TERMINAL_ERROR,    /* "%s: Could not create terminal: %s" */
};

struct plat_device_vol_locked_t
{
    uintptr_t vol_nums;
    uintptr_t handles_vols[1];
};

typedef struct plat_device_vol_locked_t plat_device_vol_locked_t;

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
#endif

/* A hack (GCC-specific?) to allow us to ignore unused parameters. */
#    define UNUSED(arg) __attribute__((unused)) arg

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


#if __has_attribute(fallthrough)
# define fallthrough __attribute__((fallthrough))
#else
# if __has_attribute(__fallthrough__)
#  define fallthrough __attribute__((__fallthrough__))
# endif
# define fallthrough do {} while (0) /* fallthrough */
#endif

#endif

/* Global variables residing in the platform module. */
extern int          dopause;       /* system is paused */
extern int          mouse_capture; /* mouse is captured in app */
extern volatile int is_quit;       /* system exit requested */

#ifdef MTR_ENABLED
extern int tracing_on;
#endif

extern uint64_t timer_freq;
extern int      infocus;
extern char     emu_version[200]; /* version ID string */
extern int      rctrl_is_lalt;
extern int      update_icons;

extern int kbd_req_capture;
extern int hide_status_bar;
extern int hide_tool_bar;
extern int fullscreen_ui_visible;

/* System-related functions. */
extern FILE    *plat_fopen(const char *path, const char *mode);
extern FILE    *plat_fopen64(const char *path, const char *mode);
extern void     plat_remove(char *path);
extern int      plat_getcwd(char *bufp, int max);
extern int      plat_chdir(char *path);
extern void     plat_tempfile(char *bufp, char *prefix, char *suffix);
extern void     plat_get_exe_name(char *s, int size);
extern void     plat_get_global_config_dir(char *outbuf, size_t len);
extern void     plat_get_global_data_dir(char *outbuf, size_t len);
extern void     plat_get_temp_dir(char *outbuf, uint8_t len);
extern void     plat_get_vmm_dir(char *outbuf, size_t len);
extern void     plat_init_rom_paths(void);
extern void     plat_init_asset_paths(void);
extern int      plat_dir_check(char *path);
extern int      plat_file_check(const char *path);
extern int      plat_dir_create(char *path);
extern void    *plat_mmap(size_t size, uint8_t executable);
extern void     plat_munmap(void *ptr, size_t size);
extern uint64_t plat_timer_read(void);
extern uint32_t plat_get_ticks(void);
extern void     plat_delay_ms(uint32_t count);
extern void     plat_pause(int p);
extern void     plat_mouse_capture(int on);
extern int      plat_vidapi(const char *name);
extern char    *plat_vidapi_name(int api);
extern void     plat_resize(int x, int y, int monitor_index);
extern void     plat_resize_request(int x, int y, int monitor_index);
extern int      plat_language_code(char *langcode);
extern void     plat_language_code_r(int id, char *outbuf, int len);
extern void     plat_get_cpu_string(char *outbuf, uint8_t len);
#ifdef _WIN32
extern void     plat_get_system_directory(char *outbuf);
#endif
extern void     plat_set_thread_name(void *thread, const char *name);
extern void     plat_break(void);
extern void     plat_send_to_clipboard(unsigned char *rgb, int width, int height);
extern int      plat_run_command(const char *cmd, const char **env, const char *title);
extern void     plat_clean_up(void);

/* Windows-specific physical disk handling. */
extern plat_device_vol_locked_t* plat_lock_volumes(FILE* file);
extern void                      plat_unlock_volumes(plat_device_vol_locked_t* vol);

/* Resource management. */
extern char *plat_get_string(int id);

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
extern void rdisk_eject(uint8_t id);
extern void rdisk_mount(uint8_t id, char *fn, uint8_t wp);
extern void rdisk_reload(uint8_t id);
extern void mo_eject(uint8_t id);
extern void mo_mount(uint8_t id, char *fn, uint8_t wp);
extern void mo_reload(uint8_t id);
extern void tape_eject(uint8_t id);
extern void tape_mount(uint8_t id, char *fn, uint8_t wp);
extern void tape_reload(uint8_t id);
extern int     plat_is_block_device(const char *path);
extern int64_t plat_get_block_device_size(const char *path);

/* Other stuff. */
extern void startblit(void);
extern void endblit(void);

/* Conversion between UTF-8 and UTF-16. */
extern size_t mbstoc16s(uint16_t dst[], const char src[], int len);
extern size_t c16stombs(char dst[], const uint16_t src[], int len);

#ifdef __cplusplus
}
#endif

#endif /*EMU_PLAT_H*/
