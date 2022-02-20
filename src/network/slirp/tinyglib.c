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
#include <tinyglib.h>
#include <string.h>

/* Must be a function, as libslirp redefines it as a macro. */
gboolean
g_spawn_async_with_fds(const gchar *working_directory, gchar **argv,
                       gchar **envp, GSpawnFlags flags,
                       GSpawnChildSetupFunc child_setup,
                       gpointer user_data, GPid *child_pid, gint stdin_fd,
                       gint stdout_fd, gint stderr_fd, GError **error)
{
    return 0;
}


/* Needs bounds checking, but not really used by libslirp. */
GString *
g_string_new(gchar *base)
{
    char *ret = malloc(4096);
    if (base)
	strcpy(ret, base);
    return ret;
}


/* Unimplemented, as with anything related to GString. */
gchar *
g_string_free(GString *string, gboolean free_segment)
{
    return (free_segment ? NULL : string);
}


/* Implementation borrowed from GLib itself. */
gchar *
g_strstr_len(const gchar *haystack, gssize haystack_len, const gchar *needle)
{
    if (haystack_len < 0)
	return strstr(haystack, needle);
    else {
	const gchar *p = haystack;
	gsize needle_len = strlen(needle);
	gsize haystack_len_unsigned = haystack_len;
	const gchar *end;
	gsize i;

	if (needle_len == 0)
		return (gchar *) haystack;

	if (haystack_len_unsigned < needle_len)
		return NULL;

	end = haystack + haystack_len - needle_len;

	while (p <= end && *p) {
		for (i = 0; i < needle_len; i++)
			if (p[i] != needle[i])
				goto next;

		return (gchar *)p;

next:
		p++;
	}

	return NULL;
    }
}


/* Implementation borrowed from GLib itself. */
guint
g_strv_length(gchar **str_array)
{
    guint i = 0;
    while (str_array[i] != NULL)
    	++i;
    return i;
}

/* Implementation borrowed from GLib itself. */
gsize
g_strlcpy (gchar       *dest,
           const gchar *src,
           gsize        dest_size)
{
  gchar *d = dest;
  const gchar *s = src;
  gsize n = dest_size;

  if (dest == NULL) return 0;
  if (src  == NULL) return 0;

  /* Copy as many bytes as will fit */
  if (n != 0 && --n != 0)
    do
      {
        gchar c = *s++;

        *d++ = c;
        if (c == 0)
          break;
      }
    while (--n != 0);

  /* If not enough room in dest, add NUL and traverse rest of src */
  if (n == 0)
    {
      if (dest_size != 0)
        *d = 0;
      while (*s++)
        ;
    }

  return s - src - 1;  /* count does not include NUL */
}
