/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Minimal reimplementation of GLib for libslirp.
 *
 *
 *
 * Author:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */
#ifndef TINYGLIB_H
# define TINYGLIB_H

/* Define this to bypass TinyGLib and use full GLib instead. */
#ifdef TINYGLIB_USE_GLIB
#include <glib.h>
#else

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define HAVE_STDARG_H
#include <86box/86box.h>


/* Definitions */

#define G_LITTLE_ENDIAN 1234
#define G_BIG_ENDIAN 4321
#define G_PDP_ENDIAN 3412
#ifdef __BYTE_ORDER__
# if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#  define G_BYTE_ORDER G_LITTLE_ENDIAN
# elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#  define G_BYTE_ORDER G_BIG_ENDIAN
# elif __BYTE_ORDER__ == __ORDER_PDP_ENDIAN__
#  define G_BYTE_ORDER G_PDP_ENDIAN
# endif
#endif
#ifndef G_BYTE_ORDER
/* Safe to assume LE for MSVC, as Windows is LE on all architectures. */
# define G_BYTE_ORDER G_LITTLE_ENDIAN
#endif

#ifdef _WIN32
# define G_OS_WIN32 1
#else
# define G_OS_UNIX 1
#endif

#define G_SPAWN_SEARCH_PATH 0

#if defined(__LP64__) || defined(__LLP64__) || defined(_WIN64)
# define GLIB_SIZEOF_VOID_P 8
# if defined(__LLP64__) || defined(_WIN64)
#  define GLIB_SIZEOF_LONG 4
# else
#  define GLIB_SIZEOF_LONG 8
# endif
# define GLIB_SIZEOF_SIZE_T 8
# define GLIB_SIZEOF_SSIZE_T 8
#else
# define GLIB_SIZEOF_VOID_P 4
# define GLIB_SIZEOF_LONG 4
# define GLIB_SIZEOF_SIZE_T 4
# define GLIB_SIZEOF_SSIZE_T 4
#endif


/* Types */

/* Windows does not define ssize_t, so we need to define it here. */
#ifndef _SSIZE_T_DEFINED
# define _SSIZE_T_DEFINED
# undef ssize_t
# ifdef _WIN64
#  define ssize_t int64_t
# else
#  define ssize_t int32_t
# endif
#endif

#define gboolean int
#define gchar char
#define gint int
#define gint16 int16_t
#define gint32 int32_t
#define gint64 int64_t
#define glong long
#define GPid void *
#define gpointer void *
#define gsize size_t
#define GSpawnFlags void *
#define GSpawnChildSetupFunc void *
#define gssize ssize_t
#define GString char
#define GStrv char **
#define guint unsigned int
#define guint16 uint16_t
#define guint32 uint32_t
#define guint64 uint64_t

typedef struct _GDebugKey {
    char key[32];
    int val;
} GDebugKey;

typedef struct _GError {
    char message[1];
} GError;

typedef struct _GRand {
    uint8_t dummy;
} GRand;


/* Functions */
extern gboolean	g_spawn_async_with_fds(const gchar *working_directory, gchar **argv,
                                       gchar **envp, GSpawnFlags flags,
                                       GSpawnChildSetupFunc child_setup,
                                       gpointer user_data, GPid *child_pid, gint stdin_fd,
                                       gint stdout_fd, gint stderr_fd, GError **error);
extern GString	*g_string_new(gchar *base);
extern gchar	*g_string_free(GString *string, gboolean free_segment);
extern gchar	*g_strstr_len(const gchar *haystack, gssize haystack_len, const gchar *needle);
extern guint	g_strv_length(gchar **str_array);


/* Macros */
#define tinyglib_pclog(f, s, ...) pclog("TinyGLib " f "(): " s "\n", ##__VA_ARGS__)

#define GLIB_CHECK_VERSION(a, b, c) 1
#ifdef __GNUC__
# define G_GNUC_PRINTF(format_idx, arg_idx) __attribute__((__format__ (__printf__, format_idx, arg_idx)))
#else
# define G_GNUC_PRINTF(format_idx, arg_idx)
#endif
#define G_N_ELEMENTS(arr) (sizeof(arr) / sizeof((arr)[0]))
#define G_STATIC_ASSERT(e) /* this should probably do something */
#define G_UNLIKELY(e) (e)

#define g_assert(e) do { if (!(e)) fatal("TinyGLib g_assert(" #e ")\n"); } while (0)
#ifdef __GNUC__
# define g_assert_not_reached __builtin_unreachable
#else
# ifdef _MSC_VER
#  define g_assert_not_reached() __assume(0)
# else
#  define g_assert_not_reached()
# endif
#endif
#define g_critical(s, ...) fatal("TinyGLib g_critical(): " s "\n", ##__VA_ARGS__)
#ifdef TINYGLIB_DEBUG
# define g_debug(s, ...) tinyglib_pclog("g_debug", s, ##__VA_ARGS__)
#else
# define g_debug(s, ...)
#endif
#define g_error(s, ...) tinyglib_pclog("g_error", s, ##__VA_ARGS__)
#define g_error_free(err)
#define g_malloc0(s) calloc(1, s)
#define g_new(t, n) (t *) malloc(sizeof(t) * n)
#define g_new0(t, n) (t *) calloc(n, sizeof(t))
#ifdef TINYGLIB_DEBUG
# define g_parse_debug_string(s, k, n) ((!!sizeof(k)) * -1) /* unimplemented; always enables all debug flags */
#else
# define g_parse_debug_string(s, k, n) (!sizeof(k))
#endif
#define g_rand_int_range(r, min, max) (rand() % (max + 1 - min) + min)
#define g_rand_new() calloc(1, sizeof(GRand))
#define g_return_val_if_fail(e, v) if (!(e)) return (v)
#define g_shell_parse_argv(a, b, c, d) !!(sizeof(b)) /* unimplemented */
#define g_strdup(str) ((str) ? strdup(str) : NULL)
#define g_warn_if_fail(e) do { if (!(e)) pclog("TinyGLib g_warn_if_fail(" #e ")\n"); } while (0)
#define g_warn_if_reached() pclog("TinyGLib g_warn_if_reached()\n")
#define g_warning(s, ...) tinyglib_pclog("g_warning", s, ##__VA_ARGS__)


/* Remapped functions */
#define g_free free
#define g_getenv getenv
#define g_malloc malloc
#define g_rand_free free
#define g_realloc realloc
#define g_snprintf snprintf
#define g_strerror strerror
#define g_strfreev free
#define g_string_append_printf sprintf /* unimplemented */
#define g_vsnprintf vsnprintf


#endif

#endif
