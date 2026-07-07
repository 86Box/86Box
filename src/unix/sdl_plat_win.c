#include <stdio.h>
#include <stdint.h>

#include <windows.h>

#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/path.h>
#include <86box/rom.h>
#include <86box/version.h>

/* SetThreadDescription was added in 14393. Revisit if we ever start requiring 10. */
static HRESULT(WINAPI *pSetThreadDescription)(HANDLE hThread, PCWSTR lpThreadDescription) = NULL;

static void    *kernel32_handle    = NULL;
static dllimp_t kernel32_imports[] = {
    // clang-format off
    { "SetThreadDescription",  &pSetThreadDescription  },
    { NULL,                    NULL                    }
    // clang-format on
};

void
plat_unlock_volumes(plat_device_vol_locked_t* vol)
{
    DWORD bytesRet = 0;
    uintptr_t i = 0;
    for (i = 0; i < vol->vol_nums; i++) {
        if (vol->handles_vols[i] != ((uintptr_t) (intptr_t) -1)) {
            DeviceIoControl((HANDLE)vol->handles_vols[i], FSCTL_DISMOUNT_VOLUME, 0, 0, 0, 0, &bytesRet, nullptr);
            DeviceIoControl((HANDLE)vol->handles_vols[i], FSCTL_UNLOCK_VOLUME, 0, 0, 0, 0, &bytesRet, nullptr);
        }
    }
    DeviceIoControl((HANDLE)vol->handle_disk, IOCTL_DISK_UPDATE_PROPERTIES, 0, 0, 0, 0, &bytesRet, nullptr);
    (void)GetLogicalDrives();
    for (i = 0; i < vol->vol_nums; i++) {
        if (vol->handles_vols[i] != ((uintptr_t) (intptr_t) -1)) {
            CloseHandle((HANDLE)vol->handles_vols[i]);
        }
    }
    free(vol);
}

plat_device_vol_locked_t*
plat_lock_volumes(FILE* file)
{
    HANDLE filehandle = (HANDLE)_get_osfhandle(fileno(file));
    if (filehandle == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    DWORD bytesRet = 0;

    STORAGE_DEVICE_NUMBER storage_num;
    if (!DeviceIoControl(filehandle, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &storage_num, sizeof(STORAGE_DEVICE_NUMBER), &bytesRet, NULL)) {
        return 0;
    }

    // Excessive, but needed.
    DRIVE_LAYOUT_INFORMATION* layout_info = (DRIVE_LAYOUT_INFORMATION*)calloc(81920, 1);
    if (DeviceIoControl(filehandle, IOCTL_DISK_GET_DRIVE_LAYOUT, NULL, 0, layout_info, 81920, &bytesRet, NULL)) {
        //auto partCount = layout_info->PartitionCount;
        //layout_info = (DRIVE_LAYOUT_INFORMATION_EX*)realloc(layout_info, sizeof(PARTITION_INFORMATION_EX) * (partCount + 1) + sizeof(DRIVE_LAYOUT_INFORMATION_EX));
        plat_device_vol_locked_t* locked_list = (plat_device_vol_locked_t*)calloc(1, sizeof(plat_device_vol_locked_t) + layout_info->PartitionCount * sizeof(uintptr_t));
        if (locked_list) {
            locked_list->handle_disk = (uintptr_t)filehandle;
            locked_list->vol_nums = layout_info->PartitionCount;
            for (DWORD i = 0; i < layout_info->PartitionCount; i++) {
                char path_name[256] = { 0 };
                snprintf(path_name, sizeof(path_name) - 1, "\\\\?\\Harddisk%uPartition%lu", storage_num.DeviceNumber, i);
                locked_list->handles_vols[i] = (uintptr_t)CreateFileA(path_name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, 0);
                if (locked_list->handles_vols[i] != -1) {
                    if (DeviceIoControl((HANDLE)locked_list->handles_vols[i], FSCTL_LOCK_VOLUME, 0, 0, 0, 0, &bytesRet, NULL)) {
                    } else {
                        warning("Failed to lock partition %lu on disk %d.", i, storage_num.DeviceNumber);
                    }
                }
            }
        }
    } else {
        pclog("Failed to get drive layout information (%ld).\n", GetLastError());
    }
    free(layout_info);
    return NULL;
}

/*
 * Common filesystem locations
 */

void
plat_tempfile(char *bufp, char *prefix, char *suffix)
{
    SYSTEMTIME SystemTime;

    if (prefix != NULL)
        sprintf(bufp, "%s-", prefix);
    else
        strcpy(bufp, "");

    GetLocalTime(&SystemTime);
    sprintf(&bufp[strlen(bufp)], "%d%02d%02d-%02d%02d%02d-%03d%s",
            SystemTime.wYear, SystemTime.wMonth, SystemTime.wDay,
            SystemTime.wHour, SystemTime.wMinute, SystemTime.wSecond,
            SystemTime.wMilliseconds,
            suffix);
}

void
plat_get_global_data_dir(char *outbuf, const size_t len)
{
    const char *appdata = getenv("LOCALAPPDATA");

    if (len == 0)
        return;

    if (portable_mode) {
        strncpy(outbuf, exe_path, len);
    } else {
        if (appdata) {
            path_append_filename(outbuf, appdata, EMU_NAME);

            if (!plat_dir_check(outbuf))
                plat_dir_create(outbuf);
        } else
            strncpy(outbuf, exe_path, len);
    }

    outbuf[len - 1] = '\0';
    path_slash(outbuf);
}

void
plat_get_temp_dir(char *outbuf, uint8_t len)
{
    GetTempPathA((DWORD) len, outbuf);
}

void
plat_get_system_directory(char *outbuf)
{
    if (outbuf == NULL)
        return;

    GetSystemWindowsDirectoryA(outbuf, MAX_PATH);
    path_normalize(outbuf);
}

void
plat_init_rom_paths(void)
{
    char appdata[1024] = { 0 };
    char path[1024]    = { 0 };

    plat_get_global_data_dir(appdata, sizeof(appdata));
    path_append_filename(path, appdata, "roms");

    if (!plat_dir_check(path))
        plat_dir_create(path);

    rom_add_path(path);
}

void
plat_init_asset_paths(void)
{
    char appdata[1024] = { 0 };
    char path[1024]    = { 0 };

    plat_get_global_data_dir(appdata, sizeof(appdata));
    path_append_filename(path, appdata, "assets");

    if (!plat_dir_check(path))
        plat_dir_create(path);

    asset_add_path(path);
}

/*
 * Path manipulation - need to handle backslashes on Windows
 */

int
path_abs(char *path)
{
    return (path[1] == ':') || (path[0] == '\\') || (path[0] == '/') || (strstr(path, "ioctl://") == path);
}

void
path_normalize(char *path)
{
    if (strstr(path, "ioctl://") != path) {
        while (*path != '\0') {
            if (*path == '\\')
                *path = '/';
            path++;
        }
    }
}

/*
 * Block device handling
 */

int
plat_is_block_device(const char *path)
{
    return 0;
}

int64_t
plat_get_block_device_size(UNUSED(const char *path))
{
    /* Unsupported platform */
    return -1;
}

/*
 * Memory management
 */

void *
plat_mmap(size_t size, uint8_t executable)
{
    return VirtualAlloc(NULL, size, MEM_COMMIT, executable ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE);
}

void
plat_munmap(void *ptr, UNUSED(size_t size))
{
    VirtualFree(ptr, 0, MEM_RELEASE);
}

/*
 * Threads
 */

void
plat_set_thread_name(void *thread, const char *name)
{
    if (!kernel32_handle) {
        kernel32_handle = dynld_module("kernel32.dll", kernel32_imports);
        if (!kernel32_handle) {
            kernel32_handle        = kernel32_imports; /* store dummy pointer to avoid trying again */
            pSetThreadDescription  = NULL;
        }
    }

    if (pSetThreadDescription) {
        size_t  len = strlen(name) + 1;
        wchar_t wname[2048];
        mbstoc16s(wname, name, (len >= 1024) ? 1024 : len);
        pSetThreadDescription(thread ? (HANDLE) thread : GetCurrentThread(), wname);
    }
}

/*
 * Command execution
 */

int
plat_run_command(const char *cmd, const char **env, const char *title)
{
    /* Set up direct or console execution. */
    STARTUPINFOA si        = { .cb = sizeof(STARTUPINFOA) };
    DWORD flags            = 0;
    if (title) {
        flags |= CREATE_NEW_CONSOLE;
        if (title[0])
            si.lpTitle = (char *) title;
    } else {
        si.dwFlags   |= STARTF_USESTDHANDLES;
        si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE); 
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    }

    /* Build command line arguments. */
    size_t len  = strlen(cmd) + 4;
    char  *args = (char *) malloc(len);
    snprintf(args, len, "/c %s", cmd);

    /* Append environment variables to the existing environment. */
    char *new_env = NULL;
    if (env) {
        char  *existing_env = GetEnvironmentStrings();
        size_t c            = 0;
        while (existing_env[c++])
            while (existing_env[c++]);
        len   = c;
        int d = 0;
        while (env[d])
            len += strlen(env[d++]) + 1;
        new_env = (char *) malloc(len);
        memcpy(new_env, existing_env, --c);
        FreeEnvironmentStrings(existing_env);
        for (d = 0; env[d]; d++) {
            if (env[d][0]) {
                strncpy(&new_env[c], env[d], len - c);
                c += strlen(env[d]) + 1;
            }
        }
        new_env[c] = '\0';
    }

    /* Execute command. */
    PROCESS_INFORMATION pi  = { 0 };
    int                 ret = !!CreateProcessA(getenv("ComSpec"), args, NULL, NULL, FALSE, flags, new_env, usr_path, &si, &pi);

    free(args);
    if (new_env)
        free(new_env);
    return ret;
}

/*
 * UTF-8 aware widechar conversion functions
 */

size_t
mbstoc16s(uint16_t dst[], const char src[], int len)
{
    if (src == NULL || len < 0)
        return 0;

    size_t ret = MultiByteToWideChar(CP_UTF8, 0, src, -1,
                                        (LPWSTR) dst, dst == NULL ? 0 : len);

    return ret ? ret : (size_t) -1;
}

size_t
c16stombs(char dst[], const uint16_t src[], int len)
{
    if (src == NULL || len < 0)
        return 0;

    size_t ret = WideCharToMultiByte(CP_UTF8, 0, (LPCWCH) src, -1,
                                        dst, dst == NULL ? 0 : len, NULL, NULL);

    return ret ? ret : (size_t) -1;
}
