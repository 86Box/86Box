/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Common platform functions.
 *
 *
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *          Teemu Korhonen
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2021-2022 Cacodemon345
 *          Copyright 2021-2022 Teemu Korhonen
 */
#include <cstdio>

#include <mutex>
#include <thread>
#include <memory>
#include <algorithm>
#include <map>

#include <QDebug>

#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDateTime>
#include <QLocalSocket>
#include <QTimer>
#include <QProcess>
#include <QRegularExpression>

#include <QLibrary>
#include <QElapsedTimer>

#include <QScreen>

#include "qt_rendererstack.hpp"
#include "qt_mainwindow.hpp"
#include "qt_progsettings.hpp"
#include "qt_util.hpp"

#ifdef Q_OS_UNIX
#    include <pthread.h>
#    include <sys/mman.h>
#endif

#if 0
static QByteArray buf;
#endif
extern QElapsedTimer elapsed_timer;
extern MainWindow   *main_window;
QElapsedTimer        elapsed_timer;

static std::atomic_int      blitmx_contention = 0;
static std::recursive_mutex blitmx;

class CharPointer {
public:
    CharPointer(char *buf, int size)
        : b(buf)
        , s(size)
    {
    }
    CharPointer &operator=(const QByteArray &ba)
    {
        if (s > 0) {
            strncpy(b, ba.data(), s - 1);
            b[s] = 0;
        } else {
            // if we haven't been told the length of b, just assume enough
            // because we didn't get it from emulator code
            strcpy(b, ba.data());
            b[ba.size()] = 0;
        }
        return *this;
    }

private:
    char *b;
    int   s;
};

extern "C" {
#ifdef Q_OS_WINDOWS
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#    include <86box/win.h>
#else
#    include <strings.h>
#endif
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/path.h>
#include <86box/plat_dynld.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/config.h>
#include <86box/ui.h>
#ifdef DISCORD
#   include <86box/discord.h>
#endif

#include "../cpu/cpu.h"
#include <86box/plat.h>

volatile int cpu_thread_run  = 1;
int          mouse_capture   = 0;
int          fixed_size_x    = 640;
int          fixed_size_y    = 480;
int          rctrl_is_lalt   = 0;
int          update_icons    = 1;
int          kbd_req_capture = 0;
int          hide_status_bar = 0;
int          hide_tool_bar   = 0;
uint32_t     lang_id = 0x0409, lang_sys = 0x0409; // Multilangual UI variables, for now all set to LCID of en-US

int
stricmp(const char *s1, const char *s2)
{
#ifdef Q_OS_WINDOWS
    return _stricmp(s1, s2);
#else
    return strcasecmp(s1, s2);
#endif
}

int
strnicmp(const char *s1, const char *s2, size_t n)
{
#ifdef Q_OS_WINDOWS
    return _strnicmp(s1, s2, n);
#else
    return strncasecmp(s1, s2, n);
#endif
}

void
do_start(void)
{
}

void
do_stop(void)
{
    cpu_thread_run = 0;
#if 0
    main_window->close();
#endif
}

void
plat_get_exe_name(char *s, int size)
{
    QByteArray exepath_temp = QCoreApplication::applicationDirPath().toLocal8Bit();

    memcpy(s, exepath_temp.data(), std::min((qsizetype) exepath_temp.size(), (qsizetype) size));

    path_slash(s);
}

uint32_t
plat_get_ticks(void)
{
    return elapsed_timer.elapsed();
}

uint64_t
plat_timer_read(void)
{
    return elapsed_timer.elapsed();
}

FILE *
plat_fopen(const char *path, const char *mode)
{
#if defined(Q_OS_MACOS) or defined(Q_OS_LINUX)
    QFileInfo fi(path);
    QString   filename = (fi.isRelative() && !fi.filePath().isEmpty()) ? usr_path + fi.filePath() : fi.filePath();
    return fopen(filename.toUtf8().constData(), mode);
#else
    return fopen(QString::fromUtf8(path).toLocal8Bit(), mode);
#endif
}

FILE *
plat_fopen64(const char *path, const char *mode)
{
#if defined(Q_OS_MACOS) or defined(Q_OS_LINUX)
    QFileInfo fi(path);
    QString   filename = (fi.isRelative() && !fi.filePath().isEmpty()) ? usr_path + fi.filePath() : fi.filePath();
    return fopen(filename.toUtf8().constData(), mode);
#else
    return fopen(QString::fromUtf8(path).toLocal8Bit(), mode);
#endif
}

int
plat_dir_create(char *path)
{
    return QDir().mkdir(path) ? 0 : -1;
}

int
plat_dir_check(char *path)
{
    QFileInfo fi(path);
    return fi.isDir() ? 1 : 0;
}

int
plat_getcwd(char *bufp, int max)
{
#ifdef __APPLE__
    /* Working directory for .app bundles is undefined. */
    strncpy(bufp, exe_path, max);
#else
    CharPointer(bufp, max) = QDir::currentPath().toUtf8();
#endif
    return 0;
}

void
path_get_dirname(char *dest, const char *path)
{
    QFileInfo fi(path);
    CharPointer(dest, -1) = fi.dir().path().toUtf8();
}

char *
path_get_extension(char *s)
{
    auto len = strlen(s);
    auto idx = QByteArray::fromRawData(s, len).lastIndexOf('.');
    if (idx >= 0) {
        return s + idx + 1;
    }
    return s + len;
}

char *
path_get_filename(char *s)
{
#ifdef Q_OS_WINDOWS
    int c = strlen(s) - 1;

    while (c > 0) {
        if (s[c] == '/' || s[c] == '\\')
            return (&s[c + 1]);
        c--;
    }

    return s;
#else
    auto idx               = QByteArray::fromRawData(s, strlen(s)).lastIndexOf(QDir::separator().toLatin1());
    if (idx >= 0) {
        return s + idx + 1;
    }
    return s;
#endif
}

int
path_abs(char *path)
{
#ifdef Q_OS_WINDOWS
    if ((path[1] == ':') || (path[0] == '\\') || (path[0] == '/') || (strstr(path, "ioctl://") == path))
        return 1;

    return 0;
#else
    return path[0] == '/';
#endif
}

void
path_normalize(char *path)
{
#ifdef Q_OS_WINDOWS
    if (strstr(path, "ioctl://") != path) {
        while (*path++ != 0) {
            if (*path == '\\')
                *path = '/';
        }
    } else
        path[8] = path[9] = path[11] = '\\';
#endif
}

void
path_slash(char *path)
{
    auto len       = strlen(path);
    auto separator = '/';
    if (path[len - 1] != separator) {
        path[len]     = separator;
        path[len + 1] = 0;
    }
    path_normalize(path);
}

const char *
path_get_slash(char *path)
{
    return QString(path).endsWith("/") ? "" : "/";
}

void
path_append_filename(char *dest, const char *s1, const char *s2)
{
    strcpy(dest, s1);
    path_slash(dest);
    strcat(dest, s2);
}

void
plat_tempfile(char *bufp, char *prefix, char *suffix)
{
    QString name;

    if (prefix != nullptr) {
        name.append(QString("%1-").arg(prefix));
    }

    name.append(QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss-zzz"));
    if (suffix)
        name.append(suffix);
    strcpy(bufp, name.toUtf8().data());
}

void
plat_remove(char *path)
{
    QFile(path).remove();
}

void *
plat_mmap(size_t size, uint8_t executable)
{
#if defined Q_OS_WINDOWS
    return VirtualAlloc(NULL, size, MEM_COMMIT, executable ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE);
#elif defined Q_OS_UNIX
#    if defined Q_OS_DARWIN && defined MAP_JIT
    void *ret = mmap(0, size, PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0), MAP_ANON | MAP_PRIVATE | (executable ? MAP_JIT : 0), -1, 0);
#    else
    void *ret = mmap(0, size, PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0), MAP_ANON | MAP_PRIVATE, -1, 0);
#    endif
    return (ret == MAP_FAILED) ? nullptr : ret;
#endif
}

void
plat_munmap(void *ptr, size_t size)
{
#if defined Q_OS_WINDOWS
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, size);
#endif
}

void
plat_pause(int p)
{
    static wchar_t oldtitle[512];
    wchar_t        title[1024];
    wchar_t        paused_msg[512];

    if ((!!p) == dopause) {
#ifdef Q_OS_WINDOWS
        if (source_hwnd)
            PostMessage((HWND) (uintptr_t) source_hwnd, WM_SENDSTATUS, (WPARAM) !!p, (LPARAM) (HWND) main_window->winId());
#endif
        return;
    }

    if ((p == 0) && (time_sync & TIME_SYNC_ENABLED))
        nvr_time_sync();

    do_pause(p);
    if (p) {
        if (mouse_capture)
            plat_mouse_capture(0);

        wcsncpy(oldtitle, ui_window_title(NULL), sizeof_w(oldtitle) - 1);
        wcscpy(title, oldtitle);
        paused_msg[QObject::tr(" - PAUSED").toWCharArray(paused_msg)] = 0;
        wcscat(title, paused_msg);
        ui_window_title(title);
    } else {
        ui_window_title(oldtitle);
    }

#ifdef DISCORD
    discord_update_activity(dopause);
#endif

    QTimer::singleShot(0, main_window, &MainWindow::updateUiPauseState);

#ifdef Q_OS_WINDOWS
    if (source_hwnd)
        PostMessage((HWND) (uintptr_t) source_hwnd, WM_SENDSTATUS, (WPARAM) !!p, (LPARAM) (HWND) main_window->winId());
#endif
}

void
plat_power_off(void)
{
    plat_mouse_capture(0);
    confirm_exit_cmdl = 0;
    nvr_save();
    config_save();

    /* Deduct a sufficiently large number of cycles that no instructions will
       run before the main thread is terminated */
    cycles -= 99999999;

    cpu_thread_run = 0;
    QTimer::singleShot(0, (const QWidget *) main_window, &QMainWindow::close);
}

extern "C++" {
QMap<uint32_t, QPair<QString, QString>> ProgSettings::lcid_langcode = {
    { 0x0403, { "ca-ES", "Catalan (Spain)" }         },
    { 0x0804, { "zh-CN", "Chinese (Simplified)" }    },
    { 0x0404, { "zh-TW", "Chinese (Traditional)" }   },
    { 0x041A, { "hr-HR", "Croatian (Croatia)" }      },
    { 0x0405, { "cs-CZ", "Czech (Czech Republic)" }  },
    { 0x0407, { "de-DE", "German (Germany)" }        },
    { 0x0809, { "en-GB", "English (United Kingdom)" }},
    { 0x0409, { "en-US", "English (United States)" } },
    { 0x040B, { "fi-FI", "Finnish (Finland)" }       },
    { 0x040C, { "fr-FR", "French (France)" }         },
    { 0x040E, { "hu-HU", "Hungarian (Hungary)" }     },
    { 0x0410, { "it-IT", "Italian (Italy)" }         },
    { 0x0411, { "ja-JP", "Japanese (Japan)" }        },
    { 0x0412, { "ko-KR", "Korean (Korea)" }          },
    { 0x0415, { "pl-PL", "Polish (Poland)" }         },
    { 0x0416, { "pt-BR", "Portuguese (Brazil)" }     },
    { 0x0816, { "pt-PT", "Portuguese (Portugal)" }   },
    { 0x0419, { "ru-RU", "Russian (Russia)" }        },
    { 0x041B, { "sk-SK", "Slovak (Slovakia)" }       },
    { 0x0424, { "sl-SI", "Slovenian (Slovenia)" }    },
    { 0x0C0A, { "es-ES", "Spanish (Spain, Modern Sort)" } },
    { 0x041F, { "tr-TR", "Turkish (Turkey)" }        },
    { 0x0422, { "uk-UA", "Ukrainian (Ukraine)" }     },
    { 0x042A, { "vi-VN", "Vietnamese (Vietnam)" }    },
    { 0xFFFF, { "system", "(System Default)" }       },
};
}

/* Sets up the program language before initialization. */
uint32_t
plat_language_code(char *langcode)
{
    for (auto &curKey : ProgSettings::lcid_langcode.keys()) {
        if (ProgSettings::lcid_langcode[curKey].first == langcode) {
            return curKey;
        }
    }
    return 0xFFFF;
}

/* Converts back the language code to LCID */
void
plat_language_code_r(uint32_t lcid, char *outbuf, int len)
{
    if (!ProgSettings::lcid_langcode.contains(lcid)) {
        qstrncpy(outbuf, "system", len);
        return;
    }
    qstrncpy(outbuf, ProgSettings::lcid_langcode[lcid].first.toUtf8().constData(), len);
    return;
}

#ifndef Q_OS_WINDOWS
void *
dynld_module(const char *name, dllimp_t *table)
{
    QString     libraryName = name;
    QFileInfo   fi(libraryName);
    QStringList removeSuffixes = { "dll", "dylib", "so" };
    if (removeSuffixes.contains(fi.suffix())) {
        libraryName = fi.completeBaseName();
    }

    auto lib = std::unique_ptr<QLibrary>(new QLibrary(libraryName));
    if (lib->load()) {
        for (auto imp = table; imp->name != nullptr; imp++) {
            auto ptr = lib->resolve(imp->name);
            if (ptr == nullptr) {
                return nullptr;
            }
            auto imp_ptr = reinterpret_cast<void **>(imp->func);
            *imp_ptr     = reinterpret_cast<void *>(ptr);
        }
    } else {
        return nullptr;
    }

    return lib.release();
}

void
dynld_close(void *handle)
{
    delete reinterpret_cast<QLibrary *>(handle);
}
#endif

void
startblit()
{
    blitmx_contention++;
    if (blitmx.try_lock()) {
        return;
    }

    blitmx.lock();
}

void
endblit()
{
    blitmx_contention--;
    blitmx.unlock();
    if (blitmx_contention > 0) {
        // a deadlock has been observed on linux when toggling via video_toggle_option
        // because the mutex is typically unfair on linux
        // => sleep if there's contention
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
}

#ifdef Q_OS_WINDOWS
size_t
mbstoc16s(uint16_t dst[], const char src[], int len)
{
    if (src == NULL)
        return 0;
    if (len < 0)
        return 0;

    size_t ret = MultiByteToWideChar(CP_UTF8, 0, src, -1, reinterpret_cast<LPWSTR>(dst), dst == NULL ? 0 : len);

    if (!ret) {
        return -1;
    }

    return ret;
}

size_t
c16stombs(char dst[], const uint16_t src[], int len)
{
    if (src == NULL)
        return 0;
    if (len < 0)
        return 0;

    size_t ret = WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<LPCWCH>(src), -1, dst, dst == NULL ? 0 : len, NULL, NULL);

    if (!ret) {
        return -1;
    }

    return ret;
}
#endif

#ifdef _WIN32
#    if defined(__amd64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
#        define LIB_NAME_GS   "gsdll64.dll"
#        define LIB_NAME_GPCL "gpcl6dll64.dll"
#    else
#        define LIB_NAME_GS   "gsdll32.dll"
#        define LIB_NAME_GPCL "gpcl6dll32.dll"
#    endif
#    define LIB_NAME_PCAP        "Npcap"
#    define MOUSE_CAPTURE_KEYSEQ "F8+F12"
#else
#    define LIB_NAME_GS          "libgs"
#    define LIB_NAME_GPCL        "libgpcl6"
#    define LIB_NAME_PCAP        "libpcap"
#    define MOUSE_CAPTURE_KEYSEQ "Ctrl+End"
#endif

QMap<int, std::wstring> ProgSettings::translatedstrings;

void
ProgSettings::reloadStrings()
{
    translatedstrings.clear();
    translatedstrings[STRING_MOUSE_CAPTURE]             = QCoreApplication::translate("", "Click to capture mouse").toStdWString();
    translatedstrings[STRING_MOUSE_RELEASE]             = QCoreApplication::translate("", "Press %1 to release mouse").arg(QCoreApplication::translate("", MOUSE_CAPTURE_KEYSEQ)).toStdWString();
    translatedstrings[STRING_MOUSE_RELEASE_MMB]         = QCoreApplication::translate("", "Press %1 or middle button to release mouse").arg(QCoreApplication::translate("", MOUSE_CAPTURE_KEYSEQ)).toStdWString();
    translatedstrings[STRING_INVALID_CONFIG]            = QCoreApplication::translate("", "Invalid configuration").toStdWString();
    translatedstrings[STRING_NO_ST506_ESDI_CDROM]       = QCoreApplication::translate("", "MFM/RLL or ESDI CD-ROM drives never existed").toStdWString();
    translatedstrings[STRING_PCAP_ERROR_NO_DEVICES]     = QCoreApplication::translate("", "No PCap devices found").toStdWString();
    translatedstrings[STRING_PCAP_ERROR_INVALID_DEVICE] = QCoreApplication::translate("", "Invalid PCap device").toStdWString();
    translatedstrings[STRING_PCAP_ERROR_DESC]           = QCoreApplication::translate("", "Make sure %1 is installed and that you are on a %1-compatible network connection.").arg(LIB_NAME_PCAP).toStdWString();
    translatedstrings[STRING_GHOSTSCRIPT_ERROR_TITLE]   = QCoreApplication::translate("", "Unable to initialize Ghostscript").toStdWString();
    translatedstrings[STRING_GHOSTSCRIPT_ERROR_DESC]    = QCoreApplication::translate("", "%1 is required for automatic conversion of PostScript files to PDF.\n\nAny documents sent to the generic PostScript printer will be saved as PostScript (.ps) files.").arg(LIB_NAME_GS).toStdWString();
    translatedstrings[STRING_GHOSTPCL_ERROR_TITLE]      = QCoreApplication::translate("", "Unable to initialize GhostPCL").toStdWString();
    translatedstrings[STRING_GHOSTPCL_ERROR_DESC]       = QCoreApplication::translate("", "%1 is required for automatic conversion of PCL files to PDF.\n\nAny documents sent to the generic PCL printer will be saved as Printer Command Language (.pcl) files.").arg(LIB_NAME_GPCL).toStdWString();
    translatedstrings[STRING_HW_NOT_AVAILABLE_MACHINE]  = QCoreApplication::translate("", "Machine \"%hs\" is not available due to missing ROMs in the roms/machines directory. Switching to an available machine.").toStdWString();
    translatedstrings[STRING_HW_NOT_AVAILABLE_VIDEO]    = QCoreApplication::translate("", "Video card \"%hs\" is not available due to missing ROMs in the roms/video directory. Switching to an available video card.").toStdWString();
    translatedstrings[STRING_HW_NOT_AVAILABLE_VIDEO2]   = QCoreApplication::translate("", "Video card #2 \"%hs\"  is not available due to missing ROMs in the roms/video directory. Disabling the second video card.").toStdWString();
    translatedstrings[STRING_HW_NOT_AVAILABLE_TITLE]    = QCoreApplication::translate("", "Hardware not available").toStdWString();
    translatedstrings[STRING_MONITOR_SLEEP]             = QCoreApplication::translate("", "Monitor in sleep mode").toStdWString();
    translatedstrings[STRING_NET_ERROR]                 = QCoreApplication::translate("", "Failed to initialize network driver").toStdWString();
    translatedstrings[STRING_NET_ERROR_DESC]            = QCoreApplication::translate("", "The network configuration will be switched to the null driver").toStdWString();
}

wchar_t *
plat_get_string(int i)
{
    if (ProgSettings::translatedstrings.empty())
        ProgSettings::reloadStrings();
    return ProgSettings::translatedstrings[i].data();
}

int
plat_chdir(char *path)
{
    return QDir::setCurrent(QString(path)) ? 0 : -1;
}

void
plat_get_global_config_dir(char *outbuf, const uint8_t len)
{
    const auto dir = QDir(QStandardPaths::standardLocations(QStandardPaths::AppConfigLocation)[0]);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qWarning("Failed to create global configuration directory %s", dir.absolutePath().toUtf8().constData());
        }
    }
    strncpy(outbuf, dir.canonicalPath().toUtf8().constData(), len);
}

void
plat_get_global_data_dir(char *outbuf, const uint8_t len)
{
    const auto dir = QDir(QStandardPaths::standardLocations(QStandardPaths::AppDataLocation)[0]);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qWarning("Failed to create global data directory %s", dir.absolutePath().toUtf8().constData());
        }
    }
    strncpy(outbuf, dir.canonicalPath().toUtf8().constData(), len);
}

void
plat_get_temp_dir(char *outbuf, const uint8_t len)
{
    const auto dir = QDir(QStandardPaths::standardLocations(QStandardPaths::TempLocation)[0]);
    strncpy(outbuf, dir.canonicalPath().toUtf8().constData(), len);
}

void
plat_init_rom_paths(void)
{
    auto paths = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);

#ifdef _WIN32
    // HACK: The standard locations returned for GenericDataLocation include
    // the EXE path and a `data` directory within it as the last two entries.

    // Remove the entries as we don't need them.
    paths.removeLast();
    paths.removeLast();
#endif

    for (auto &path : paths) {
#ifdef __APPLE__
        rom_add_path(QDir(path).filePath("net.86Box.86Box/roms").toUtf8().constData());
        rom_add_path(QDir(path).filePath("86Box/roms").toUtf8().constData());
#else
        rom_add_path(QDir(path).filePath("86Box/roms").toUtf8().constData());
#endif
    }
}

void
plat_get_cpu_string(char *outbuf, uint8_t len) {
    auto cpu_string = QString("Unknown");
    /* Write the default string now in case we have to exit early from an error */
    qstrncpy(outbuf, cpu_string.toUtf8().constData(), len);

#if defined(Q_OS_MACOS)
    auto *process = new QProcess(nullptr);
    QStringList arguments;
    QString program = "/usr/sbin/sysctl";
    arguments << "machdep.cpu.brand_string";
    process->start(program, arguments);
    if (!process->waitForStarted()) {
        return;
    }
    if (!process->waitForFinished()) {
        return;
    }
    QByteArray result = process->readAll();
    auto command_result = QString(result).split(": ").last().trimmed();
    if(!command_result.isEmpty()) {
        cpu_string = command_result;
    }
#elif defined(Q_OS_WINDOWS)
    const LPCSTR  keyName   = "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
    const LPCSTR  valueName = "ProcessorNameString";
    unsigned char buf[32768];
    DWORD         bufSize;
    HKEY          hKey;
    bufSize = 32768;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, keyName, 0, 1, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExA(hKey, valueName, NULL, NULL, buf, &bufSize) == ERROR_SUCCESS) {
            cpu_string = reinterpret_cast<const char*>(buf);
        }
        RegCloseKey(hKey);
    }
#elif defined(Q_OS_LINUX)
    auto cpuinfo = QString("/proc/cpuinfo");
    auto cpuinfo_fi = QFileInfo(cpuinfo);
    if(!cpuinfo_fi.isReadable()) {
        return;
    }
    QFile file(cpuinfo);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream textStream(&file);
        while(true) {
            QString line = textStream.readLine();
            if (line.isNull()) {
                break;
            }
            if(QRegularExpression("model name.*:").match(line).hasMatch()) {
                auto list = line.split(": ");
                if(!list.last().isEmpty()) {
                    cpu_string = list.last();
                    break;
                }
            }

        }
    }
#endif

    qstrncpy(outbuf, cpu_string.toUtf8().constData(), len);

}

void
plat_set_thread_name(void *thread, const char *name)
{
#ifdef Q_OS_WINDOWS
    /* SetThreadDescription was added in 14393. Revisit if we ever start requiring 10. */
    static void *kernel32_handle = NULL;
    static HRESULT(WINAPI *pSetThreadDescription)(HANDLE hThread, PCWSTR lpThreadDescription) = NULL;
    static dllimp_t kernel32_imports[] = {
      // clang-format off
        { "SetThreadDescription", &pSetThreadDescription },
        { NULL,                   NULL                   }
      // clang-format on
    };

    if (!kernel32_handle) {
        kernel32_handle = dynld_module("kernel32.dll", kernel32_imports);
        if (!kernel32_handle) {
            kernel32_handle = kernel32_imports; /* store dummy pointer to avoid trying again */
            pSetThreadDescription = NULL;
        }
    }

    if (pSetThreadDescription) {
        size_t len = strlen(name) + 1;
        wchar_t wname[len + 1];
        mbstowcs(wname, name, len);
        pSetThreadDescription(thread ? (HANDLE) thread : GetCurrentThread(), wname);
    }
#else
#    ifdef Q_OS_DARWIN
    if (thread) /* Apple pthread can only set self's name */
        return;
    char truncated[64];
#    else
    char truncated[16];
#    endif
    strncpy(truncated, name, sizeof(truncated) - 1);
#    ifdef Q_OS_DARWIN
    pthread_setname_np(truncated);
#    else
    pthread_setname_np(thread ? *((pthread_t *) thread) : pthread_self(), truncated);
#    endif
#endif
}
