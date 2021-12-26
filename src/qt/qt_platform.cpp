#include <cstdio>

#include <mutex>
#include <thread>
#include <memory>
#include <algorithm>

#include <QDebug>

#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QCoreApplication>
#include <QDateTime>

#include <QLibrary>
#include <QElapsedTimer>

#include "qt_mainwindow.hpp"

#ifdef Q_OS_UNIX
#include <sys/mman.h>
#endif

// static QByteArray buf;
extern QElapsedTimer elapsed_timer;
extern MainWindow* main_window;
QElapsedTimer elapsed_timer;

static std::atomic_int blitmx_contention = 0;
static std::mutex blitmx;

class CharPointer {
public:
    CharPointer(char* buf, int size) : b(buf), s(size) {}
    CharPointer& operator=(const QByteArray &ba) {
        if (s > 0) {
            strncpy(b, ba.data(), s-1);
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
    char* b;
    int s;
};

extern "C" {
#ifdef Q_OS_WINDOWS
#define NOMINMAX
#include <windows.h>
#else
#include <strings.h>
#endif
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/plat_dynld.h>
#include <86box/config.h>
#include <86box/ui.h>

#include "../cpu/cpu.h"
#include <86box/plat.h>

volatile int cpu_thread_run = 1;
int mouse_capture = 0;
int fixed_size_x = 640;
int fixed_size_y = 480;
int rctrl_is_lalt = 0;
int	update_icons = 0;
int	kbd_req_capture = 0;
int hide_status_bar = 0;
uint32_t lang_id = 0x0409, lang_sys = 0x0409; // Multilangual UI variables, for now all set to LCID of en-US

int stricmp(const char* s1, const char* s2)
{
#ifdef Q_OS_WINDOWS
    return _stricmp(s1, s2);
#else
    return strcasecmp(s1, s2);
#endif
}

int strnicmp(const char *s1, const char *s2, size_t n)
{
#ifdef Q_OS_WINDOWS
    return _strnicmp(s1, s2, n);
#else
    return strncasecmp(s1, s2, n);
#endif
}

void
do_stop(void)
{
    QCoreApplication::quit();
}

void plat_get_exe_name(char *s, int size)
{
    QByteArray exepath_temp = QCoreApplication::applicationDirPath().toLocal8Bit();

    memcpy(s, exepath_temp.data(), std::min(exepath_temp.size(),size));

    plat_path_slash(s);
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
    /*
    QString filepath(path);
    if (filepath.isEmpty()) {
        return nullptr;
    }

    qWarning() << "plat_fopen" << filepath;
    bool ok = false;
    QFile file(filepath);
    auto mode_len = strlen(mode);
    for (size_t i = 0; i < mode_len; ++i) {
        switch (mode[i]) {
        case 'r':
            ok = file.open(QIODevice::ReadOnly);
            break;
        case 'w':
            ok = file.open(QIODevice::ReadWrite);
            break;
        case 'b':
        case 't':
            break;
        default:
            qWarning() << "Unhandled open mode" << mode[i];
        }
    }

    if (ok) {
        qDebug() << "filehandle" << file.handle();
        QFile returned;
        qDebug() << "\t" << returned.open(file.handle(), file.openMode(), QFileDevice::FileHandleFlag::DontCloseHandle);
        return fdopen(returned.handle(), mode);
    } else {
        return nullptr;
    }
    */

/* Not sure if any this is necessary, fopen seems to work on Windows -Manaatti
#ifdef Q_OS_WINDOWS
    wchar_t *pathw, *modew;
    int len;
    FILE *fp;

    if (acp_utf8)
        return fopen(path, mode);
    else {
        len = mbstoc16s(NULL, path, 0) + 1;
        pathw = malloc(sizeof(wchar_t) * len);
        mbstoc16s(pathw, path, len);

        len = mbstoc16s(NULL, mode, 0) + 1;
        modew = malloc(sizeof(wchar_t) * len);
        mbstoc16s(modew, mode, len);

        fp = _wfopen(pathw, modew);

        free(pathw);
        free(modew);

        return fp;
    }
#endif
#ifdef Q_OS_UNIX
*/
    return fopen(path, mode);
//#endif
}

FILE *
plat_fopen64(const char *path, const char *mode)
{
    return fopen(path, mode);
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
    CharPointer(bufp, max) = QDir::currentPath().toUtf8();
    return 0;
}

void
plat_get_dirname(char *dest, const char *path)
{
    QFileInfo fi(path);
    CharPointer(dest, -1) = fi.dir().path().toUtf8();
}

char *
plat_get_extension(char *s)
{
    auto len = strlen(s);
    auto idx = QByteArray::fromRawData(s, len).lastIndexOf('.');
    if (idx >= 0) {
        return s+idx+1;
    }
    return s+len;
}

char *
plat_get_filename(char *s)
{
#ifdef Q_OS_WINDOWS
    int c = strlen(s) - 1;

    while (c > 0) {
	if (s[c] == '/' || s[c] == '\\')
	   return(&s[c+1]);
       c--;
    }

    return(s);
#else
    auto idx = QByteArray::fromRawData(s, strlen(s)).lastIndexOf(QDir::separator().toLatin1());
    if (idx >= 0) {
        return s+idx+1;
    }
    return s;
#endif
}

int
plat_path_abs(char *path)
{
    QFileInfo fi(path);
    return fi.isAbsolute() ? 1 : 0;
}

void
plat_path_slash(char *path)
{
    auto len = strlen(path);
    auto separator = QDir::separator().toLatin1();
    if (path[len-1] != separator) {
        path[len] = separator;
        path[len+1] = 0;
    }
}

void
plat_append_filename(char *dest, const char *s1, const char *s2)
{
    strcpy(dest, s1);
    plat_path_slash(dest);
    strcat(dest, s2);
}

void
plat_tempfile(char *bufp, char *prefix, char *suffix)
{
    QString name;

    if (prefix != nullptr) {
        name.append(QString("%1-").arg(prefix));
    }

     name.append(QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss-zzzz"));
     if (suffix) name.append(suffix);
     sprintf(&bufp[strlen(bufp)], "%s", name.toUtf8().data());
}

void plat_remove(char* path)
{
    QFile(path).remove();
}

void *
plat_mmap(size_t size, uint8_t executable)
{
#if defined Q_OS_WINDOWS
    return VirtualAlloc(NULL, size, MEM_COMMIT, executable ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE);
#elif defined Q_OS_UNIX
#if defined Q_OS_DARWIN && defined MAP_JIT
    void *ret = mmap(0, size, PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0), MAP_ANON | MAP_PRIVATE | (executable ? MAP_JIT : 0), 0, 0);
#else
    void *ret = mmap(0, size, PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0), MAP_ANON | MAP_PRIVATE, 0, 0);
#endif
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
    wchar_t title[512];

    if ((p == 0) && (time_sync & TIME_SYNC_ENABLED))
    nvr_time_sync();

    dopause = p;
    if (p) {
        wcsncpy(oldtitle, ui_window_title(NULL), sizeof_w(oldtitle) - 1);
        wcscpy(title, oldtitle);
        wcscat(title, L" - PAUSED -");
        ui_window_title(title);
    } else {
        ui_window_title(oldtitle);
    }
}

// because we can't include nvr.h because it's got fields named new
extern int	nvr_save(void);

void
plat_power_off(void)
{
    confirm_exit = 0;
    nvr_save();
    config_save();

    /* Deduct a sufficiently large number of cycles that no instructions will
       run before the main thread is terminated */
    cycles -= 99999999;

    cpu_thread_run = 0;
    main_window->close();
}

void set_language(uint32_t id) {
    lang_id = id;
}

/* Sets up the program language before initialization. */
uint32_t plat_language_code(char* langcode) {
    /* or maybe not */
    return 0;
}

/* Converts back the language code to LCID */
void plat_language_code_r(uint32_t lcid, char* outbuf, int len) {
    /* or maybe not */
    return;
}

void* dynld_module(const char *name, dllimp_t *table)
{
    QString libraryName = name;
    QFileInfo fi(libraryName);
    QStringList removeSuffixes = {"dll", "dylib", "so"};
    if (removeSuffixes.contains(fi.suffix())) {
        libraryName = fi.completeBaseName();
    }

    auto lib = std::unique_ptr<QLibrary>(new QLibrary(libraryName));
    if (lib->load()) {
        for (auto imp = table; imp->name != nullptr; imp++)
        {
            auto ptr = lib->resolve(imp->name);
            if (ptr == nullptr) {
                return nullptr;
            }
            auto imp_ptr = reinterpret_cast<void**>(imp->func);
            *imp_ptr = reinterpret_cast<void*>(ptr);
        }
    } else {
        return nullptr;
    }

    return lib.release();
}

void dynld_close(void *handle)
{
    delete reinterpret_cast<QLibrary*>(handle);
}

void startblit()
{
    blitmx_contention++;
    if (blitmx.try_lock()) {
        return;
    }

    blitmx.lock();
}

void endblit()
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
size_t mbstoc16s(uint16_t dst[], const char src[], int len)
{
    if (src == NULL) return 0;
    if (len < 0) return 0;

    size_t ret = MultiByteToWideChar(CP_UTF8, 0, src, -1, reinterpret_cast<LPWSTR>(dst), dst == NULL ? 0 : len);

    if (!ret) {
	return -1;
    }

    return ret;
}

size_t c16stombs(char dst[], const uint16_t src[], int len)
{
    if (src == NULL) return 0;
    if (len < 0) return 0;

    size_t ret = WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<LPCWCH>(src), -1, dst, dst == NULL ? 0 : len, NULL, NULL);

    if (!ret) {
	return -1;
    }

    return ret;
}
#endif

int
plat_chdir(char *path)
{
    return QDir::setCurrent(QString(path)) ? 0 : -1;
}
