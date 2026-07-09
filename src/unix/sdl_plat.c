#include <SDL.h>

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
#    include <direct.h>
#    include <windows.h>
#endif

#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/config.h>
#include <86box/hdd.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/version.h>
#include "cpu.h"

#include "sdl_render.h"

/*
 * File system manipulation
 */

int
plat_getcwd(char *bufp, int max)
{
    return getcwd(bufp, max) != 0;
}

int
plat_chdir(char *str)
{
    return chdir(str);
}

FILE *
plat_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

FILE *
plat_fopen64(const char *path, const char *mode)
{
    return fopen(path, mode);
}

int
plat_dir_check(char *path)
{
    struct stat stats;
    if (stat(path, &stats) < 0)
        return 0;
    return S_ISDIR(stats.st_mode);
}

int
plat_file_check(const char *path)
{
    struct stat stats;
    if (stat(path, &stats) < 0)
        return 0;
    return !S_ISDIR(stats.st_mode);
}

int
plat_dir_create(char *path)
{
#ifdef _WIN32
    return mkdir(path);
#else
    return mkdir(path, S_IRWXU);
#endif
}

void
plat_remove(char *path)
{
    remove(path);
}

/*
 * String localization
 */

char *
plat_get_string(int i)
{
    switch (i) {
        case STRING_PCAP_ERROR_NO_DEVICES:
            return "No PCap devices found. Make sure libpcap is installed and that you are on a libpcap-compatible network connection.";
        case STRING_PCAP_ERROR_INVALID_DEVICE:
            return "Invalid PCap device. Make sure libpcap is installed and that you are on a libpcap-compatible network connection.";
        case STRING_GHOSTSCRIPT_ERROR:
            return "Unable to initialize Ghostscript. libgs is required for automatic conversion of PostScript files to PDF.\n\nAny documents sent to the generic PostScript printer will be saved as PostScript (.ps) files.";
        case STRING_GHOSTPCL_ERROR:
            return "Unable to initialize GhostPCL. libgpcl6 is required for automatic conversion of PCL files to PDF.\n\nAny documents sent to the generic PCL printer will be saved as Printer Command Language (.pcl) files.";
        case STRING_HW_NOT_AVAILABLE_MACHINE:
            return "Machine \"%s\" is not available due to missing ROMs in the roms/machines directory. Switching to an available machine.";
        case STRING_HW_NOT_AVAILABLE_VIDEO:
            return "Video card \"%s\" is not available due to missing ROMs in the roms/video directory. Switching to an available video card.";
        case STRING_HW_NOT_AVAILABLE_DEVICE:
            return "Device \"%s\" is not available due to missing ROMs. Ignoring the device.";
        case STRING_HW_NOT_AVAILABLE_TITLE:
            return "Hardware not available";
        case STRING_NET_ERROR:
            return "Failed to initialize network driver:\n\n%s\n\nThe network configuration will be switched to the null driver.";
        case STRING_ESCP_ERROR:
            return "Unable to find Dot-Matrix fonts. TrueType fonts in the \"roms/printer/fonts\" directory are required for the emulation of the Generic ESC/P 2 Dot-Matrix Printer.";
        case STRING_EDID_READ_ERROR:
            return "EDID file \"%s\" is invalid.";
        case STRING_EDID_TOO_LARGE:
            return "EDID file \"%s\" is too large.";
        case STRING_CDROM_OPEN_ISO_ERROR:
            return "Unable to open image or folder \"%s\".";
        case STRING_CDROM_OPEN_CUE_ERROR:
            return "Unable to open cue sheet \"%s\".";
        case STRING_CDROM_OPEN_MDS_ERROR:
            return "Unable to open MDS file \"%s\".";
        case STRING_CDROM_LOAD_IMAGE_ERROR:
            return "Unable to load CD-ROM image \"%s\".";
        case STRING_CDROM_LOAD_MDSX_ERROR:
            return "Unable to load image \"%s\": mdsx.so is missing, which is required for Daemon Tools MDS v2 and MDX image support.";
        case STRING_CDROM_DVD_IN_CD_DRIVE:
            return "The DVD image \"%s\" has been inserted into a drive that does not support DVD media and will be ignored.";
        case STRING_CHARDEV_CONNECT_ERROR:
            return "%s: Could not connect to %s: %s";
        case STRING_CHARDEV_CREATE_ERROR:
            return "%s: Could not create %s: %s";
        case STRING_CHARDEV_ATTACHED:
            return "%s: Attached to %s";
        case STRING_CHARDEV_VCON_IN_USE:
            return "%s: Virtual console already in use by %s";
        case STRING_CHARDEV_TERMINAL_ERROR:
            return "%s: Could not create terminal: %s";
    }
    return "";
}

/* Converts the language code string to a numeric language ID */
int
plat_language_code(UNUSED(char *langcode))
{
    /* or maybe not */
    return 0;
}

/* Converts the numeric language ID to a language code string */
void
plat_language_code_r(UNUSED(int id), UNUSED(char *outbuf), UNUSED(int len))
{
    /* or maybe not */
    return;
}

/*
 * Path manipulation
 */

void
path_slash(char *path)
{
    if (path[strlen(path) - 1] != '/') {
        strcat(path, "/");
    }
    path_normalize(path);
}

const char *
path_get_slash(char *path)
{
    char *ret = "";

    if (path[strlen(path) - 1] != '/')
        ret = "/";

    return ret;
}

/* Return the last element of a pathname. */
char *
path_get_basename(const char *path)
{
    int c = (int) strlen(path);

    while (c > 0) {
        if (path[c] == '/')
            return ((char *) &path[c + 1]);
        c--;
    }

    return ((char *) path);
}

char *
path_get_filename(char *s)
{
    int c = strlen(s) - 1;

    while (c > 0) {
        if (s[c] == '/' || s[c] == '\\')
            return (&s[c + 1]);
        c--;
    }

    return s;
}

char *
path_get_extension(char *s)
{
    int c = strlen(s) - 1;

    if (c <= 0)
        return s;

    while (c && s[c] != '.')
        c--;

    if (!c)
        return (&s[strlen(s)]);

    return (&s[c + 1]);
}

void
path_append_filename(char *dest, const char *s1, const char *s2)
{
    strcpy(dest, s1);
    path_slash(dest);
    strcat(dest, s2);
}

void
path_get_dirname(char *dest, const char *path)
{
    int   c   = (int) strlen(path);
    char *ptr = (char *) path;

    while (c > 0) {
        if (path[c] == '/' || path[c] == '\\') {
            ptr = (char *) &path[c];
            break;
        }
        c--;
    }

    /* Copy to destination. */
    while (path < ptr)
        *dest++ = *path++;
    *dest = '\0';
}

/*
 * Common locations
 */

void
plat_get_exe_name(char *s, int size)
{
    char *basepath = SDL_GetBasePath();

    snprintf(s, size, "%s%s", basepath, basepath[strlen(basepath) - 1] == '/' ? EMU_NAME : "/" EMU_NAME);
}

void
plat_get_global_config_dir(char *outbuf, const size_t len)
{
    return plat_get_global_data_dir(outbuf, len);
}

void
plat_get_vmm_dir(char *outbuf, const size_t len)
{
    // Return empty string. SDL 86Box does not have a VM manager
    if (len > 0)
        outbuf[0] = 0;
}

/*
 * Timer functions
 */

uint64_t
plat_timer_read(void)
{
    return SDL_GetPerformanceCounter();
}

static uint64_t
plat_get_ticks_common(void)
{
    static int first_use = 1;
    static uint64_t starting_time;
    static uint64_t frequency;

    uint64_t ending_time;
    uint64_t elapsed_microseconds;

    if (first_use) {
        frequency     = SDL_GetPerformanceFrequency();
        starting_time = SDL_GetPerformanceCounter();
        first_use     = 0;
    }
    ending_time          = SDL_GetPerformanceCounter();
    elapsed_microseconds = ((ending_time - starting_time) * 1000000) / frequency;

    return elapsed_microseconds;
}

uint64_t
plat_get_ns(void)
{
    static int first_use = 1;
    static uint64_t starting_time;
    static uint64_t frequency;

    uint64_t ending_time;
    uint64_t elapsed_nanoseconds;

    if (first_use) {
        frequency     = SDL_GetPerformanceFrequency();
        starting_time = SDL_GetPerformanceCounter();
        first_use     = 0;
    }
    ending_time          = SDL_GetPerformanceCounter();
    elapsed_nanoseconds = ((ending_time - starting_time) * 1000000000ull) / frequency;

    return elapsed_nanoseconds;
}

uint32_t
plat_get_ticks(void)
{
    return (uint32_t) (plat_get_ticks_common() / 1000);
}

void
plat_delay_ms(uint32_t count)
{
    SDL_Delay(count);
}

/*
 * Emulator support
 */

void
plat_power_off(void)
{
    confirm_exit_cmdl = 0;
    hdd_image_sync_all();
    nvr_save();
    config_save();

    /* Deduct a sufficiently large number of cycles that no instructions will
       run before the main thread is terminated */
    cycles -= 99999999;

    cpu_thread_run = 0;
}

void
plat_pause(int p)
{
    static char oldtitle[512];
    char        title[512];

    if ((!!p) == dopause)
        return;

    if ((p == 0) && (time_sync & TIME_SYNC_ENABLED))
        nvr_time_sync();

    do_pause(p);
    if (p) {
        strncpy(oldtitle, ui_window_title(NULL), sizeof(oldtitle) - 1);
        strcpy(title, oldtitle);
        strcat(title, " - PAUSED");
        ui_window_title(title);
    } else {
        ui_window_title(oldtitle);
    }
}

char *
plat_vidapi_name(UNUSED(int i))
{
    return "default";
}

void
plat_get_cpu_string(char *outbuf, uint8_t len)
{
    char cpu_string[] = "Unknown";

    strncpy(outbuf, cpu_string, len);
}

/*
 * Miscellaneous functions
 */

int
stricmp(const char *s1, const char *s2)
{
    return strcasecmp(s1, s2);
}

int
strnicmp(const char *s1, const char *s2, size_t n)
{
    return strncasecmp(s1, s2, n);
}
