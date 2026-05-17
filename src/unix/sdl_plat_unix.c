#include <stdlib.h>
#include <string.h>

#ifdef __HAIKU__
#    include <OS.h>
#endif

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>
#include <spawn.h>

/* Block device ioctl headers */
#ifdef __linux__
#    include <linux/fs.h>
#endif
#ifdef __APPLE__
#    include <sys/disk.h>
#endif
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#    include <sys/disk.h>
#    include <sys/disklabel.h>
#endif

#ifdef __APPLE__
#    include "macOSXGlue.h"
#endif

#include <SDL.h>

#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/plat.h>
#include <86box/path.h>
#include <86box/rom.h>
#include <86box/version.h>

#define __USE_GNU 1 /* shouldn't be done, yet it is */
#include <pthread.h>

#define TMPFILE_BUFSIZE 1024 // Assumed max buffer size

/*
 * Common filesystem locations
 */

void
plat_tempfile(char *bufp, char *prefix, char *suffix)
{
    struct tm     *calendertime;
    struct timeval t;
    time_t         curtime;
    size_t         used = 0;

    if (prefix != NULL)
        used = snprintf(bufp, TMPFILE_BUFSIZE, "%s-", prefix);
    else if (TMPFILE_BUFSIZE > 0)
        bufp[0] = '\0';

    gettimeofday(&t, NULL);
    curtime      = time(NULL);
    calendertime = localtime(&curtime);

    if (used < TMPFILE_BUFSIZE) {
        snprintf(bufp + used, TMPFILE_BUFSIZE - used,
                 "%d%02d%02d-%02d%02d%02d-%03" PRId32 "%s",
                 calendertime->tm_year, calendertime->tm_mon, calendertime->tm_mday,
                 calendertime->tm_hour, calendertime->tm_min, calendertime->tm_sec,
                 (int32_t) (t.tv_usec / 1000), suffix);
    }
}
#undef TMPFILE_BUFSIZE

void
plat_get_global_data_dir(char *outbuf, const size_t len)
{
    if (len == 0)
        return;

    if (portable_mode) {
        strncpy(outbuf, exe_path, len);
    } else {
        char *prefPath = SDL_GetPrefPath(NULL, EMU_NAME);

        if (prefPath != NULL) {
            strncpy(outbuf, prefPath, len);
            SDL_free(prefPath);
        } else {
            strncpy(outbuf, exe_path, len);
        }
    }

    outbuf[len - 1] = '\0';
    path_slash(outbuf);
}

void
plat_get_temp_dir(char *outbuf, uint8_t len)
{
    const char *tmpdir = getenv("TMPDIR");
    if (tmpdir == NULL) {
        tmpdir = "/tmp";
    }
    strncpy(outbuf, tmpdir, len);
    path_slash(outbuf);
}

#define TMP_PATH_BUFSIZE 1024

void
plat_init_rom_paths(void)
{
#ifndef __APPLE__
    const char *xdg_data_home = getenv("XDG_DATA_HOME");
    if (xdg_data_home) {
        char   xdg_rom_path[TMP_PATH_BUFSIZE] = { 0 };
        size_t used                           = snprintf(xdg_rom_path, sizeof(xdg_rom_path), "%s/", xdg_data_home);
        if (used < sizeof(xdg_rom_path))
            used += snprintf(xdg_rom_path + used, sizeof(xdg_rom_path) - used, EMU_NAME "/");
        if (used < sizeof(xdg_rom_path) && !plat_dir_check(xdg_rom_path))
            plat_dir_create(xdg_rom_path);
        if (used < sizeof(xdg_rom_path))
            used += snprintf(xdg_rom_path + used, sizeof(xdg_rom_path) - used, "roms/");
        if (used < sizeof(xdg_rom_path) && !plat_dir_check(xdg_rom_path))
            plat_dir_create(xdg_rom_path);
        if (used < sizeof(xdg_rom_path))
            rom_add_path(xdg_rom_path);
    } else {
        const char *home = getenv("HOME");
        if (!home) {
            struct passwd *pw = getpwuid(getuid());
            if (pw)
                home = pw->pw_dir;
        }

        if (home) {
            char   home_rom_path[TMP_PATH_BUFSIZE] = { 0 };
            size_t used                            = snprintf(home_rom_path, sizeof(home_rom_path),
                                                              "%s/.local/share/" EMU_NAME "/", home);
            if (used < sizeof(home_rom_path) && !plat_dir_check(home_rom_path))
                plat_dir_create(home_rom_path);
            if (used < sizeof(home_rom_path))
                used += snprintf(home_rom_path + used,
                                 sizeof(home_rom_path) - used, "roms/");
            if (used < sizeof(home_rom_path) && !plat_dir_check(home_rom_path))
                plat_dir_create(home_rom_path);
            if (used < sizeof(home_rom_path))
                rom_add_path(home_rom_path);
        }
    }

    const char *xdg_data_dirs = getenv("XDG_DATA_DIRS");
    if (xdg_data_dirs) {
        char *xdg_rom_paths = strdup(xdg_data_dirs);
        if (xdg_rom_paths) {
            // Trim trailing colons
            size_t len = strlen(xdg_rom_paths);
            while (len > 0 && xdg_rom_paths[len - 1] == ':')
                xdg_rom_paths[--len] = '\0';

            char *saveptr = NULL;
            char *cur_xdg = strtok_r(xdg_rom_paths, ":", &saveptr);
            while (cur_xdg) {
                char   real_xdg_rom_path[TMP_PATH_BUFSIZE] = { 0 };
                size_t used                                = snprintf(real_xdg_rom_path,
                                                                      sizeof(real_xdg_rom_path),
                                                                      "%s/" EMU_NAME "/roms/", cur_xdg);
                if (used < sizeof(real_xdg_rom_path))
                    rom_add_path(real_xdg_rom_path);
                cur_xdg = strtok_r(NULL, ":", &saveptr);
            }

            free(xdg_rom_paths);
        }
    } else {
        rom_add_path("/usr/local/share/" EMU_NAME "/roms/");
        rom_add_path("/usr/share/" EMU_NAME "/roms/");
    }
#else
    char default_rom_path[TMP_PATH_BUFSIZE] = { 0 };
    getDefaultROMPath(default_rom_path);
    rom_add_path(default_rom_path);
#endif
}

void
plat_init_asset_paths(void)
{
#ifndef __APPLE__
    const char *xdg_data_home = getenv("XDG_DATA_HOME");
    if (xdg_data_home) {
        char   xdg_asset_path[TMP_PATH_BUFSIZE] = { 0 };
        size_t used                             = snprintf(xdg_asset_path, sizeof(xdg_asset_path), "%s/", xdg_data_home);
        if (used < sizeof(xdg_asset_path))
            used += snprintf(xdg_asset_path + used, sizeof(xdg_asset_path) - used, EMU_NAME "/");
        if (used < sizeof(xdg_asset_path) && !plat_dir_check(xdg_asset_path))
            plat_dir_create(xdg_asset_path);
        if (used < sizeof(xdg_asset_path))
            used += snprintf(xdg_asset_path + used, sizeof(xdg_asset_path) - used, "assets/");
        if (used < sizeof(xdg_asset_path) && !plat_dir_check(xdg_asset_path))
            plat_dir_create(xdg_asset_path);
        if (used < sizeof(xdg_asset_path))
            asset_add_path(xdg_asset_path);
    } else {
        const char *home = getenv("HOME");
        if (!home) {
            struct passwd *pw = getpwuid(getuid());
            if (pw)
                home = pw->pw_dir;
        }

        if (home) {
            char   home_asset_path[TMP_PATH_BUFSIZE] = { 0 };
            size_t used                              = snprintf(home_asset_path, sizeof(home_asset_path),
                                                                "%s/.local/share/" EMU_NAME "/", home);
            if (used < sizeof(home_asset_path) && !plat_dir_check(home_asset_path))
                plat_dir_create(home_asset_path);
            if (used < sizeof(home_asset_path))
                used += snprintf(home_asset_path + used,
                                 sizeof(home_asset_path) - used, "assets/");
            if (used < sizeof(home_asset_path) && !plat_dir_check(home_asset_path))
                plat_dir_create(home_asset_path);
            if (used < sizeof(home_asset_path))
                asset_add_path(home_asset_path);
        }
    }

    const char *xdg_data_dirs = getenv("XDG_DATA_DIRS");
    if (xdg_data_dirs) {
        char *xdg_asset_paths = strdup(xdg_data_dirs);
        if (xdg_asset_paths) {
            // Trim trailing colons
            size_t len = strlen(xdg_asset_paths);
            while (len > 0 && xdg_asset_paths[len - 1] == ':')
                xdg_asset_paths[--len] = '\0';

            char *saveptr = NULL;
            char *cur_xdg = strtok_r(xdg_asset_paths, ":", &saveptr);
            while (cur_xdg) {
                char   real_xdg_asset_path[TMP_PATH_BUFSIZE] = { 0 };
                size_t used                                  = snprintf(real_xdg_asset_path,
                                                                        sizeof(real_xdg_asset_path),
                                                                        "%s/" EMU_NAME "/assets/", cur_xdg);
                if (used < sizeof(real_xdg_asset_path))
                    asset_add_path(real_xdg_asset_path);
                cur_xdg = strtok_r(NULL, ":", &saveptr);
            }

            free(xdg_asset_paths);
        }
    } else {
        asset_add_path("/usr/local/share/" EMU_NAME "/assets/");
        asset_add_path("/usr/share/" EMU_NAME "/assets/");
    }
#else
    char default_asset_path[TMP_PATH_BUFSIZE] = { 0 };
    getDefaultROMPath(default_asset_path);
    asset_add_path(default_asset_path);
#endif
}
#undef TMP_PATH_BUFSIZE

/*
 * Path manipulation
 */

int
path_abs(char *path)
{
    return path[0] == '/';
}

void
path_normalize(UNUSED(char *path))
{
    /* No-op. */
}

/*
 * Block device handling
 */

int
plat_is_block_device(const char *path)
{
    struct stat stats;
    if (stat(path, &stats) < 0)
        return 0;
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
    /* BSD systems use character devices for disk access, not block devices */
    return S_ISCHR(stats.st_mode) ? 1 : 0;
#else
    return S_ISBLK(stats.st_mode) ? 1 : 0;
#endif
}

int64_t
plat_get_block_device_size(const char *path)
{
#ifdef __linux__
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    uint64_t size = 0;
    if (ioctl(fd, BLKGETSIZE64, &size) < 0) {
        close(fd);
        return -1;
    }
    close(fd);
    return (int64_t) size;

#elif defined(__APPLE__)
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    uint64_t block_count = 0;
    uint32_t block_size  = 0;

    if (ioctl(fd, DKIOCGETBLOCKCOUNT, &block_count) < 0) {
        close(fd);
        return -1;
    }
    if (ioctl(fd, DKIOCGETBLOCKSIZE, &block_size) < 0) {
        close(fd);
        return -1;
    }

    close(fd);
    return (int64_t) (block_count * block_size);

#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    off_t size = 0;
    if (ioctl(fd, DIOCGMEDIASIZE, &size) < 0) {
        close(fd);
        return -1;
    }
    close(fd);
    return (int64_t) size;

#else
    /* Unsupported platform */
    (void) path;
    return -1;
#endif
}

/*
 * Memory management
 */

void *
plat_mmap(size_t size, uint8_t executable)
{
#    if defined __APPLE__ && defined MAP_JIT
    void *ret = mmap(0, size, PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0), MAP_ANON | MAP_PRIVATE | (executable ? MAP_JIT : 0), -1, 0);
#    elif defined(PROT_MPROTECT)
    void *ret = mmap(0, size, PROT_MPROTECT(PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0)), MAP_ANON | MAP_PRIVATE, -1, 0);
#    else
    void *ret = mmap(0, size, PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0), MAP_ANON | MAP_PRIVATE, -1, 0);
#    endif
    return (ret == MAP_FAILED) ? NULL : ret;
}

void
plat_munmap(void *ptr, size_t size)
{
    munmap(ptr, size);
}

/*
 * Threads
 */

void
plat_set_thread_name(void *thread, const char *name)
{
#ifdef __APPLE__
    if (thread) /* macOS pthread can only set self's name */
        return;
    char truncated[64];
#elif defined(__NetBSD__)
    char truncated[64];
#elif defined(__HAIKU__)
    if (thread) /* BeOS threads can only easily set self's name */
        return;
    char truncated[32];
#else
    char truncated[16];
#endif
    strncpy(truncated, name, sizeof(truncated) - 1);
#ifdef __APPLE__
    pthread_setname_np(truncated);
#elif defined(__NetBSD__)
    pthread_setname_np(thread ? *((pthread_t *) thread) : pthread_self(), truncated, "%s");
#elif defined(__HAIKU__)
    rename_thread(find_thread(NULL), truncated);
#elif defined(__OpenBSD__)
    pthread_set_name_np(thread ? *((pthread_t *) thread) : pthread_self(), truncated);
#else
    pthread_setname_np(thread ? *((pthread_t *) thread) : pthread_self(), truncated);
#endif
}

/*
 * Command execution
 */

int
plat_run_command(const char *cmd, const char **env, const char *title)
{
    /* Append environment variables to the existing environment. */
    extern char **environ;
    const char **new_env = NULL;
    if (env && env[0] && env[0][0]) {
        int c = 0;
        while (environ[c])
            c++;
        int d = 0;
        while (env[d] && env[d][0])
            d++;
        new_env = (const char **) malloc((c + d + 1) * sizeof(char *));
        for (c = 0; environ[c]; c++)
            new_env[c] = environ[c];
        for (d = 0; env[d] && env[d][0]; d++)
            new_env[c++] = env[d];
        new_env[c] = NULL;
    }

    /* Execute command under a terminal emulator if requested. */
    int ret;
    pid_t pid;
    const char *args[] = {NULL, NULL, NULL, NULL, "/bin/sh", "-c", cmd, NULL};
    if (title) {
        /* Set arguments for xdg-terminal-exec. */
        int len = strlen(title) + 9;
        args[1] = malloc(len);
        snprintf((char *) args[1], len, "--title=%s", title);
        len = strlen(usr_path) + 7;
        args[2] = malloc(len);
        snprintf((char *) args[2], len, "--dir=%s", usr_path);
        args[3] = "--";

        /* Try terminals. */
        static const char *terminals[] = {"xdg-terminal-exec", "x-terminal-emulator", "xterm", "urxvt", "rxvt"};
        for (int i = 0; i < (sizeof(terminals) / sizeof(terminals[0])); i++) {
            args[0] = terminals[i];
            ret = !posix_spawnp(&pid, args[0], NULL, NULL, (char * const *) args, new_env ? (char * const *) new_env : (char * const *) environ);
            if (len) {
                /* Set arguments for other terminals. */
                len = 0;
                free((void *) args[1]);
                free((void *) args[2]);
                args[1] = "-T";
                args[2] = title;
                args[3] = "-e";
            }
            if (ret)
                goto end;
        }
    }

    /* Execute command directly. */
    ret = !posix_spawn(&pid, args[4], NULL, NULL, (char * const *) &args[4], new_env ? (char * const *) new_env : (char * const *) environ);
end:
    if (new_env)
        free(new_env);
    return ret;
}
