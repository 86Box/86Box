#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/cdrom.h>
#include <86box/version.h>
#include <86box/video.h>

#include "sdl_monitor.h"

static char *(*f_readline)(const char *)          = NULL;
static int (*f_add_history)(const char *)         = NULL;
static void (*f_rl_callback_handler_remove)(void) = NULL;

extern int exit_event, fullscreen_pending;
extern bool fast_forward;

void
monitor_init(void)
{
#if !defined(_WIN32) && ENABLE_READLINE
    libedithandle = dlopen(LIBEDIT_LIBRARY, RTLD_LOCAL | RTLD_LAZY);

    if (libedithandle) {
        f_readline    = dlsym(libedithandle, "readline");
        f_add_history = dlsym(libedithandle, "add_history");
        f_rl_callback_handler_remove = dlsym(libedithandle, "rl_callback_handler_remove");

        if (!f_readline)
            fprintf(stderr, "readline in libedit not found, line editing will be limited.\n");
    } else
        fprintf(stderr, "libedit not found, line editing will be limited.\n");
#endif
}

void
monitor_close(void)
{
    if (f_rl_callback_handler_remove)
        f_rl_callback_handler_remove();
}

static bool
monitor_process_media_command(uint8_t *id, char *fn, uint8_t *wp, char **xargv, int cmdargc)
{
    bool err = false;

    *id = atoi(xargv[1]);

    if (xargv[2][0] == '\'' || xargv[2][0] == '"') {
        for (int curarg = 2; curarg < cmdargc; curarg++) {
            if (strlen(fn) + strlen(xargv[curarg]) >= PATH_MAX) {
                err = true;
                fprintf(stderr, "Path name too long.\n");
            }

            strcat(fn, xargv[curarg] + (xargv[curarg][0] == '\'' || xargv[curarg][0] == '"'));

            if (fn[strlen(fn) - 1] == '\'' || fn[strlen(fn) - 1] == '"') {
                if (curarg + 1 < cmdargc && wp)
                    *wp = atoi(xargv[curarg + 1]);
                break;
            }

            strcat(fn, " ");
        }
    } else {
        if (strlen(xargv[2]) < PATH_MAX) {
            strcpy(fn, xargv[2]);
            if (wp) *wp = atoi(xargv[3]);
        } else {
            fprintf(stderr, "Path name too long.\n");
            err = true;
        }
    }

    if (fn[strlen(fn) - 1] == '\'' || fn[strlen(fn) - 1] == '"')
        fn[strlen(fn) - 1] = '\0';

    return err;
}

// From musl.
static char *
local_strsep(char **str, const char *sep)
{
    char *s = *str;
    char *end;

    if (!s)
        return NULL;
    end = s + strcspn(s, sep);
    if (*end)
        *end++ = 0;
    else
        end = 0;
    *str = end;

    return s;
}

void
monitor_execute_line(char *line)
{
    if (line) {
        char *xargv[512];
        int   cmdargc = 0;
        char *linecpy, *linespn;

        linecpy = linespn                 = strdup(line);
        linecpy[strcspn(linecpy, "\r\n")] = 0;

        if (!linecpy) {
            return;
        }

        if (f_add_history)
            f_add_history(linecpy);

        memset(xargv, 0, sizeof(xargv));
        while (1) {
            xargv[cmdargc++] = local_strsep(&linespn, " ");
            if (xargv[cmdargc - 1] == NULL || cmdargc >= 512)
                break;
        }
        cmdargc--;
        if (strncasecmp(xargv[0], "help", 4) == 0) {
            printf(
                "fddload <id> <filename> <wp> - Load floppy disk image into drive <id>.\n"
                "cdload <id> <filename> - Load CD-ROM image into drive <id>.\n"
                "rdiskload <id> <filename> <wp> - Load removable disk image into removable disk drive <id>.\n"
                "cartload <id> <filename> <wp> - Load cartridge image into cartridge drive <id>.\n"
                "moload <id> <filename> <wp> - Load MO image into MO drive <id>.\n\n"
                "tapeload <id> <filename> <wp> - Load tape image into tape drive <id>.\n\n"
                "fddeject <id> - eject disk from floppy drive <id>.\n"
                "cdeject <id> - eject disc from CD-ROM drive <id>.\n"
                "rdiskeject <id> - eject removable disk image from removable disk drive <id>.\n"
                "carteject <id> - eject cartridge from drive <id>.\n"
                "moeject <id> - eject image from MO drive <id>.\n\n"
                "tapeeject <id> - eject image from tape drive <id>.\n\n"
                "hardreset - hard reset the emulated system.\n"
                "pause - pause the the emulated system.\n"
                "fastfwd - toggle fast forward.\n"
                "screenshot - save a screenshot.\n"
                "fullscreen - toggle fullscreen.\n"
                "version - print version and license information.\n"
                "exit - exit " EMU_NAME ".\n");
        } else if (strncasecmp(xargv[0], "exit", 4) == 0) {
            exit_event = 1;
        } else if (strncasecmp(xargv[0], "version", 7) == 0) {
#ifndef EMU_GIT_HASH
#    define EMU_GIT_HASH "0000000"
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#    define ARCH_STR "arm64"
#elif defined(__x86_64) || defined(__x86_64__) || defined(__amd64) || defined(_M_X64)
#    define ARCH_STR "x86_64"
#else
#    define ARCH_STR "unknown arch"
#endif

#ifdef USE_DYNAREC
#    ifdef USE_NEW_DYNAREC
#        define DYNAREC_STR "new dynarec"
#    else
#        define DYNAREC_STR "old dynarec"
#    endif
#else
#    define DYNAREC_STR "no dynarec"
#endif

            printf(
                "%s v%s [%s] [%s, %s]\n\n"
                "An emulator of old computers\n"
                "Authors: Miran Grča (OBattler), RichardG867, Jasmine Iwanek, TC1995, coldbrewed, Teemu Korhonen (Manaatti), "
                "Joakim L. Gilje, Adrien Moulin (elyosh), Daniel Balsom (gloriouscow), Cacodemon345, Fred N. van Kempen (waltje), "
                "Tiseno100, reenigne, and others.\n"
                "With previous core contributions from Sarah Walker, leilei, JohnElliott, greatpsycho, and others.\n\n"
                "Released under the GNU General Public License version 2 or later. See LICENSE for more information.\n",
                EMU_NAME, EMU_VERSION_FULL, EMU_GIT_HASH, ARCH_STR, DYNAREC_STR);
        } else if (strncasecmp(xargv[0], "fullscreen", 10) == 0) {
            video_fullscreen   = video_fullscreen ? 0 : 1;
            fullscreen_pending = 1;
        } else if (strncasecmp(xargv[0], "screenshot", 10) == 0) {
            startblit();
            ++monitors[0].mon_screenshots_raw;
            endblit();
            device_force_redraw();
        } else if (strncasecmp(xargv[0], "pause", 5) == 0) {
            plat_pause(dopause ^ 1);
            printf("%s", dopause ? "Paused.\n" : "Unpaused.\n");
        } else if (strncasecmp(xargv[0], "fastfwd", 7) == 0) {
            fast_forward ^= 1;
            printf("%s", fast_forward ? "Fast forward on.\n" : "Fast forward off.\n");
        } else if (strncasecmp(xargv[0], "hardreset", 9) == 0) {
            pc_reset_hard();
        } else if (strncasecmp(xargv[0], "cdload", 6) == 0 && cmdargc >= 3) {
            uint8_t id;
            bool    err = false;
            char    fn[PATH_MAX];

            if (!xargv[2] || !xargv[1]) {
                free(linecpy);
                return;
            }
            err =  monitor_process_media_command(&id, fn, NULL, xargv, cmdargc);
            if (!err) {

                if (fn[strlen(fn) - 1] == '\''
                    || fn[strlen(fn) - 1] == '"')
                    fn[strlen(fn) - 1] = '\0';
                printf("Inserting disc into CD-ROM drive %hhu: %s\n", id, fn);
                cdrom_mount(id, fn);
            }
        } else if (strncasecmp(xargv[0], "fddeject", 8) == 0 && cmdargc >= 2) {
            floppy_eject(atoi(xargv[1]));
        } else if (strncasecmp(xargv[0], "cdeject", 7) == 0 && cmdargc >= 2) {
            cdrom_eject(atoi(xargv[1]));
        } else if (strncasecmp(xargv[0], "moeject", 7) == 0 && cmdargc >= 2) {
            mo_eject(atoi(xargv[1]));
        } else if (strncasecmp(xargv[0], "carteject", 9) == 0 && cmdargc >= 2) {
            cartridge_eject(atoi(xargv[1]));
        } else if (strncasecmp(xargv[0], "rdiskeject", 10) == 0 && cmdargc >= 2) {
            rdisk_eject(atoi(xargv[1]));
        } else if (strncasecmp(xargv[0], "tapeeject", 9) == 0 && cmdargc >= 2) {
            tape_eject(atoi(xargv[1]));
        } else if (strncasecmp(xargv[0], "fddload", 7) == 0 && cmdargc >= 4) {
            uint8_t id;
            uint8_t wp;
            bool    err = false;
            char    fn[PATH_MAX];

            memset(fn, 0, sizeof(fn));

            if (!xargv[2] || !xargv[1]) {
                free(linecpy);
                return;
            }
            err = monitor_process_media_command(&id, fn, &wp, xargv, cmdargc);
            if (!err) {
                if (fn[strlen(fn) - 1] == '\''
                    || fn[strlen(fn) - 1] == '"')
                    fn[strlen(fn) - 1] = '\0';
                printf("Inserting disk into floppy drive %c: %s\n", id + 'A', fn);
                floppy_mount(id, fn, wp);
            }
        } else if (strncasecmp(xargv[0], "moload", 6) == 0 && cmdargc >= 4) {
            uint8_t id;
            uint8_t wp;
            bool    err = false;
            char    fn[PATH_MAX];

            memset(fn, 0, sizeof(fn));

            if (!xargv[2] || !xargv[1]) {
                free(linecpy);
                return;
            }
            err = monitor_process_media_command(&id, fn, &wp, xargv, cmdargc);
            if (!err) {
                if (fn[strlen(fn) - 1] == '\''
                    || fn[strlen(fn) - 1] == '"')
                    fn[strlen(fn) - 1] = '\0';
                printf("Inserting into mo drive %hhu: %s\n", id, fn);
                mo_mount(id, fn, wp);
            }
        } else if (strncasecmp(xargv[0], "cartload", 8) == 0 && cmdargc >= 4) {
            uint8_t id;
            uint8_t wp;
            bool    err = false;
            char    fn[PATH_MAX];

            memset(fn, 0, sizeof(fn));

            if (!xargv[2] || !xargv[1]) {
                free(linecpy);
                return;
            }
            err = monitor_process_media_command(&id, fn, &wp, xargv, cmdargc);
            if (!err) {
                if (fn[strlen(fn) - 1] == '\''
                    || fn[strlen(fn) - 1] == '"')
                    fn[strlen(fn) - 1] = '\0';
                printf("Inserting tape into cartridge holder %hhu: %s\n", id, fn);
                cartridge_mount(id, fn, wp);
            }
        } else if (strncasecmp(xargv[0], "rdiskload", 9) == 0 && cmdargc >= 4) {
            uint8_t id;
            uint8_t wp;
            bool    err = false;
            char    fn[PATH_MAX];

            memset(fn, 0, sizeof(fn));

            if (!xargv[2] || !xargv[1]) {
                free(linecpy);
                return;
            }
            err = monitor_process_media_command(&id, fn, &wp, xargv, cmdargc);
            if (!err) {
                if (fn[strlen(fn) - 1] == '\''
                    || fn[strlen(fn) - 1] == '"')
                    fn[strlen(fn) - 1] = '\0';
                printf("Inserting disk into removable disk drive %c: %s\n", id + 'A', fn);
                rdisk_mount(id, fn, wp);
            }
        } else if (strncasecmp(xargv[0], "tapeload", 8) == 0 && cmdargc >= 4) {
            uint8_t id;
            uint8_t wp;
            bool    err = false;
            char    fn[PATH_MAX];

            memset(fn, 0, sizeof(fn));

            if (!xargv[2] || !xargv[1]) {
                free(linecpy);
                return;
            }
            err = monitor_process_media_command(&id, fn, &wp, xargv, cmdargc);
            if (!err) {
                if (fn[strlen(fn) - 1] == '\''
                    || fn[strlen(fn) - 1] == '"')
                    fn[strlen(fn) - 1] = '\0';
                printf("Inserting disk into tape drive %c: %s\n", id + 'A', fn);
                tape_mount(id, fn, wp);
            }
        }
        free(linecpy);
    }
}

void
monitor_thread(UNUSED(void *param))
{
#ifdef _WIN32
    (void) param;
#elif !defined(USE_CLI)
    if (isatty(fileno(stdin)) && isatty(fileno(stdout))) {
        char  *line = NULL;
        size_t n;

        printf(EMU_NAME " monitor console.\n");
        while (!exit_event) {
            if (feof(stdin))
                break;

#    ifdef ENABLE_READLINE
            if (f_readline)
                line = f_readline("(" EMU_NAME ") ");
            else {
#    endif
                printf("(" EMU_NAME ") ");
                (void) !getline(&line, &n, stdin);
#    ifdef ENABLE_READLINE
            }
#    endif

            monitor_execute_line(line);

            free(line);
            line = NULL;
        }
    }
#endif
}
