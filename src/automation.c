/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Automation control socket.
 *
 *          A listener thread accepts one client at a time on a UNIX domain
 *          socket. Each request is one line of space-separated words; each
 *          response is one line of JSON. Requests are handed to the CPU
 *          thread, which executes them between pc_run() calls, so every
 *          command observes the machine at a quantum boundary. Commands
 *          that consume emulated time (step, wait) complete on a later
 *          quantum. See doc/specifications/automation.md for the protocol.
 *
 * Authors: Justin Akers, <5621217+dafugg@users.noreply.github.com>
 *
 *          Copyright 2026 Justin Akers.
 */
#ifdef _WIN32
#    define _CRT_RAND_S /* rand_s(): the OS cryptographic RNG */
#endif
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <png.h>
#include <errno.h>
#ifdef _WIN32
#    include <winsock2.h>
#    include <ws2tcpip.h>
typedef SOCKET sock_t;
#    define SOCK_INVALID INVALID_SOCKET
#    define sock_close   closesocket
#    define SOCK_SHUT_RW SD_BOTH
#else
#    include <fcntl.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <sys/stat.h>
#    include <sys/un.h>
#    include <unistd.h>
typedef int sock_t;
#    define SOCK_INVALID (-1)
#    define sock_close   close
#    define SOCK_SHUT_RW SHUT_RDWR
#endif
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/cdrom.h>
#include <86box/hdd.h>
#include <86box/machine.h>
#include <86box/sound.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/mouse.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/ui.h>
#include <86box/video.h>
#include <86box/version.h>
#include <86box/nvr.h>
#include <86box/acpi.h>
#include <86box/automation.h>

#define AUTOMATION_PROTOCOL 1
#define REQ_MAX             4096
#define MEM_MAX             65536
#define KEY_QUEUE_MAX       64
/* Emulated ms between injected key events. One event can be up to six bytes
   in scan code set 2 (fake shifts around extended keys); delivering many at
   the same instant overflows the keyboard's 16-byte FIFO and loses keys. */
#define KEY_GAP_MS 2

#ifdef ENABLE_AUTOMATION_LOG
int automation_do_log = ENABLE_AUTOMATION_LOG;

static void
automation_log(const char *fmt, ...)
{
    va_list ap;

    if (automation_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define automation_log(fmt, ...)
#endif

enum {
    CMD_NONE = 0,
    CMD_STEP,
    CMD_WAIT_TEXT,
    CMD_WAIT_MEM,
    CMD_KEYS
};

typedef struct {
    char  *buf;
    size_t len;
    size_t size;
} strbuf_t;

char automation_path[1024]       = "";
char automation_token_file[1024] = "";
int  automation_start_held       = 0;
int  automation_allow_remote     = 0;

static int       enabled;
static thread_t *listen_thread;
static sock_t    listen_fd = SOCK_INVALID;
static sock_t    client_fd = SOCK_INVALID;
static event_t  *req_done;

/* Transport: a UNIX domain socket (owner-only file permissions) or TCP,
   which any local process can reach, so TCP clients must present a token. */
static int  use_tcp;
static char unix_path[1024];
static char tcp_host[256];
static char tcp_port[16];
static char token[65]; /* 32 random bytes, hex */

/* Mailbox between the listener thread and the CPU thread. */
static char       req_line[REQ_MAX];
static atomic_int req_pending;
static strbuf_t   resp;

/* CPU thread state. */
static int      held;
extern bool     fast_forward; /* the UI's (qt_main.cpp, sdl_main.c) */
static int      cmd;          /* long-running command in progress */
static int      cmd_was_held; /* hold again once the command finishes */
static uint64_t cmd_deadline; /* emulated ms at which step/wait ends */
static char     wait_text[REQ_MAX];
static uint32_t wait_addr;
static uint8_t  wait_bytes[256];
static int      wait_len;
static uint64_t emu_ms; /* emulated ms executed since startup */

static struct {
    uint16_t scan;
    uint8_t  down;
} key_queue[KEY_QUEUE_MAX];
static int      key_head;
static int      key_count;
static uint64_t key_next_ms; /* emulated ms at which the next key event is due */

/* Resets seen by the CPU, including ones the guest triggers itself. */
static uint32_t hard_resets;
static uint32_t soft_resets;
static uint64_t last_reset_ms;

/* Emulation speed, sampled over each second of emulated time. */
static uint32_t speed_wall_start;
static uint64_t speed_emu_start;
static int      speed_percent = -1;

/* Most recent frame handed to the renderer for monitor 0. */
static mutex_t  *frame_mutex;
static uint32_t *frame_buf;
static int       frame_w;
static int       frame_h;
static int       frame_alloc;
static uint64_t  frame_count;

/* Recording: the displayed frame every rec_interval ms of emulated time. */
static char     rec_dir[1024];
static uint32_t rec_interval;
static uint64_t rec_next_ms;
static uint32_t rec_frames;

static void
sb_printf(strbuf_t *sb, const char *fmt, ...)
{
    va_list ap;
    int     n;

    for (;;) {
        va_start(ap, fmt);
        n = vsnprintf(sb->buf + sb->len, sb->size - sb->len, fmt, ap);
        va_end(ap);
        if ((n >= 0) && ((size_t) n < (sb->size - sb->len))) {
            sb->len += n;
            return;
        }
        sb->size = (sb->size + n + 1) * 2;
        sb->buf  = realloc(sb->buf, sb->size);
    }
}

static void
sb_hex(strbuf_t *sb, const uint8_t *data, int len)
{
    static const char digits[] = "0123456789abcdef";

    if ((sb->len + (len * 2) + 1) > sb->size) {
        sb->size = (sb->len + (len * 2) + 1) * 2;
        sb->buf  = realloc(sb->buf, sb->size);
    }
    for (int i = 0; i < len; i++) {
        sb->buf[sb->len++] = digits[data[i] >> 4];
        sb->buf[sb->len++] = digits[data[i] & 0x0f];
    }
    sb->buf[sb->len] = '\0';
}

static void
sb_json_string(strbuf_t *sb, const char *s)
{
    sb_printf(sb, "\"");
    for (; *s; s++) {
        if ((*s == '"') || (*s == '\\'))
            sb_printf(sb, "\\%c", *s);
        else if ((uint8_t) *s < 0x20)
            sb_printf(sb, "\\u%04x", (uint8_t) *s);
        else
            sb_printf(sb, "%c", *s);
    }
    sb_printf(sb, "\"");
}

static void
respond_error(const char *msg)
{
    resp.len = 0;
    sb_printf(&resp, "{\"ok\":false,\"error\":");
    sb_json_string(&resp, msg);
    sb_printf(&resp, "}");
}

/* Finish the request in the mailbox and wake the listener thread. */
static void
complete(void)
{
    atomic_store(&req_pending, 0);
    thread_set_event(req_done);
}

static int
parse_hex_bytes(const char *s, uint8_t *out, int max)
{
    int n = 0;

    while (s[0] && s[1] && (n < max)) {
        unsigned int v;
        if (sscanf(s, "%2x", &v) != 1)
            return -1;
        out[n++] = v;
        s += 2;
    }
    return s[0] ? -1 : n;
}

/* ---- Text screen, via the BIOS data area ---- */

typedef struct {
    int      mode;
    int      cols;
    int      rows;
    int      cur_row;
    int      cur_col;
    uint32_t base;
} text_info_t;

static int
text_info(text_info_t *ti)
{
    int page;

    ti->mode = mem_readb_phys(0x449) & 0x7f;
    if ((ti->mode > 3) && (ti->mode != 7))
        return 0;
    ti->cols = mem_readb_phys(0x44a) | (mem_readb_phys(0x44b) << 8);
    ti->rows = mem_readb_phys(0x484) + 1;
    if ((ti->cols < 40) || (ti->cols > 132))
        ti->cols = 80;
    if ((ti->rows < 25) || (ti->rows > 60))
        ti->rows = 25;
    page        = mem_readb_phys(0x462) & 7;
    ti->cur_col = mem_readb_phys(0x450 + (page * 2));
    ti->cur_row = mem_readb_phys(0x451 + (page * 2));
    ti->base    = ((ti->mode == 7) ? 0xb0000 : 0xb8000) + (mem_readb_phys(0x44e) | (mem_readb_phys(0x44f) << 8));
    return 1;
}

static int
text_contains(const char *needle)
{
    text_info_t ti;
    char        line[133];
    size_t      nlen = strlen(needle);

    if (!text_info(&ti))
        return 0;
    /* Matches within a single row only. */
    for (int r = 0; r < ti.rows; r++) {
        for (int c = 0; c < ti.cols; c++)
            line[c] = mem_readb_phys(ti.base + (((r * ti.cols) + c) * 2));
        line[ti.cols] = '\0';
        for (size_t c = 0; (c + nlen) <= (size_t) ti.cols; c++) {
            if (!memcmp(line + c, needle, nlen))
                return 1;
        }
    }
    return 0;
}

static void
cmd_text(void)
{
    text_info_t ti;
    uint8_t     chars[132] = { 0 };
    uint8_t     attrs[132] = { 0 };

    if (!text_info(&ti)) {
        resp.len = 0;
        sb_printf(&resp, "{\"ok\":false,\"error\":\"not a text mode\",\"mode\":%d}", ti.mode);
        return;
    }
    resp.len = 0;
    sb_printf(&resp, "{\"ok\":true,\"mode\":%d,\"cols\":%d,\"rows\":%d,\"cursor\":[%d,%d],\"chars\":[",
              ti.mode, ti.cols, ti.rows, ti.cur_row, ti.cur_col);
    for (int r = 0; r < ti.rows; r++) {
        for (int c = 0; c < ti.cols; c++)
            chars[c] = mem_readb_phys(ti.base + (((r * ti.cols) + c) * 2));
        sb_printf(&resp, r ? ",\"" : "\"");
        sb_hex(&resp, chars, ti.cols);
        sb_printf(&resp, "\"");
    }
    sb_printf(&resp, "],\"attrs\":[");
    for (int r = 0; r < ti.rows; r++) {
        for (int c = 0; c < ti.cols; c++)
            attrs[c] = mem_readb_phys(ti.base + (((r * ti.cols) + c) * 2) + 1);
        sb_printf(&resp, r ? ",\"" : "\"");
        sb_hex(&resp, attrs, ti.cols);
        sb_printf(&resp, "\"");
    }
    sb_printf(&resp, "]}");
}

/* ---- Machine information ---- */

static void
cmd_info(void)
{
    int first;

    resp.len = 0;
    sb_printf(&resp, "{\"ok\":true,\"protocol\":%d,\"version\":", AUTOMATION_PROTOCOL);
    sb_json_string(&resp, EMU_VERSION_FULL);
    sb_printf(&resp, ",\"machine\":");
    sb_json_string(&resp, machine_get_internal_name());
    sb_printf(&resp, ",\"machine_name\":");
    sb_json_string(&resp, machine_getname(machine));
    sb_printf(&resp, ",\"cpu_family\":");
    sb_json_string(&resp, cpu_f->name);
    sb_printf(&resp, ",\"cpu\":");
    sb_json_string(&resp, cpu_s->name);
    sb_printf(&resp, ",\"cpu_hz\":%u,\"mem_kb\":%u,\"video\":", cpu_s->rspeed, mem_size);
    sb_json_string(&resp, video_get_internal_name(gfxcard[0]));
    sb_printf(&resp, ",\"sound\":");
    sb_json_string(&resp, sound_card_get_internal_name(sound_card_current[0]));

    sb_printf(&resp, ",\"hdd\":[");
    first = 1;
    for (int i = 0; i < HDD_NUM; i++) {
        if (hdd[i].bus_type == HDD_BUS_DISABLED)
            continue;
        sb_printf(&resp, "%s{\"index\":%d,\"file\":", first ? "" : ",", i);
        sb_json_string(&resp, hdd[i].fn);
        sb_printf(&resp, ",\"cylinders\":%u,\"heads\":%u,\"sectors\":%u}", hdd[i].tracks, hdd[i].hpc, hdd[i].spt);
        first = 0;
    }
    sb_printf(&resp, "],\"floppy\":[");
    first = 1;
    for (int i = 0; i < FDD_NUM; i++) {
        if (!drives[i].image_path[0])
            continue;
        sb_printf(&resp, "%s{\"drive\":%d,\"file\":", first ? "" : ",", i);
        sb_json_string(&resp, drives[i].image_path);
        sb_printf(&resp, "}");
        first = 0;
    }
    sb_printf(&resp, "],\"cdrom\":[");
    first = 1;
    for (int i = 0; i < CDROM_NUM; i++) {
        if ((cdrom[i].bus_type == CDROM_BUS_DISABLED) || !cdrom[i].image_path[0])
            continue;
        sb_printf(&resp, "%s{\"drive\":%d,\"file\":", first ? "" : ",", i);
        sb_json_string(&resp, cdrom[i].image_path);
        sb_printf(&resp, "}");
        first = 0;
    }
    sb_printf(&resp, "],\"hard_resets\":%u,\"soft_resets\":%u,\"last_reset_ms\":%llu,\"speed_percent\":%d,"
                     "\"ms\":%llu,\"frames\":%llu,\"held\":%s,\"paused\":%s,\"w\":%d,\"h\":%d}",
              hard_resets, soft_resets, (unsigned long long) last_reset_ms, speed_percent,
              (unsigned long long) emu_ms, (unsigned long long) frame_count, held ? "true" : "false",
              dopause ? "true" : "false", frame_w, frame_h);
}

void
automation_reset(int hard)
{
    if (hard)
        hard_resets++;
    else
        soft_resets++;
    last_reset_ms = emu_ms;
}

/* ---- Screenshots ---- */

static int write_png(const char *fn);

/* Called with frame_mutex held, right after a new frame was captured. */
static void
record_frame(void)
{
    char fn[1100];

    if (!rec_interval || (emu_ms < rec_next_ms))
        return;
    snprintf(fn, sizeof(fn), "%s/%010llu.png", rec_dir, (unsigned long long) emu_ms);
    if (write_png(fn))
        rec_frames++;
    rec_next_ms = emu_ms + rec_interval;
}

static void
png_mem_write(png_structp png, png_bytep data, png_size_t len)
{
    strbuf_t *sb = png_get_io_ptr(png);

    if ((sb->len + len) > sb->size) {
        sb->size = (sb->len + len) * 2;
        sb->buf  = realloc(sb->buf, sb->size);
    }
    memcpy(sb->buf + sb->len, data, len);
    sb->len += len;
}

static void
png_mem_flush(UNUSED(png_structp png))
{
}

/* Encode the last captured frame as PNG into out. Caller holds frame_mutex. */
static int
encode_png(strbuf_t *out)
{
    png_structp png;
    png_infop   info;
    png_bytep   row;

    if (!frame_buf || !frame_w || !frame_h)
        return 0;

    png  = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info = png ? png_create_info_struct(png) : NULL;
    if (!info || setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, info ? &info : NULL);
        return 0;
    }

    out->len = 0;
    png_set_write_fn(png, out, png_mem_write, png_mem_flush);
    /* Speed matters more than size for a socket consumer. */
    png_set_compression_level(png, 1);
    png_set_IHDR(png, info, frame_w, frame_h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_write_info(png, info);

    row = malloc(frame_w * 3);
    for (int y = 0; y < frame_h; y++) {
        const uint32_t *src = &frame_buf[y * frame_w];
        for (int x = 0; x < frame_w; x++) {
            row[(x * 3)]     = (src[x] >> 16) & 0xff;
            row[(x * 3) + 1] = (src[x] >> 8) & 0xff;
            row[(x * 3) + 2] = src[x] & 0xff;
        }
        png_write_row(png, row);
    }
    free(row);

    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    return 1;
}

static strbuf_t png_out;

static int
write_png(const char *fn)
{
    FILE *fp;
    int   ok;

    if (!encode_png(&png_out))
        return 0;
    fp = plat_fopen(fn, "wb");
    if (!fp)
        return 0;
    ok = fwrite(png_out.buf, 1, png_out.len, fp) == png_out.len;
    fclose(fp);
    return ok;
}

static void
sb_base64(strbuf_t *sb, const uint8_t *data, size_t len)
{
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t            need  = sb->len + ((len + 2) / 3) * 4 + 1;

    if (need > sb->size) {
        sb->size = need * 2;
        sb->buf  = realloc(sb->buf, sb->size);
    }
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = data[i] << 16;
        if ((i + 1) < len)
            v |= data[i + 1] << 8;
        if ((i + 2) < len)
            v |= data[i + 2];
        sb->buf[sb->len++] = tbl[(v >> 18) & 63];
        sb->buf[sb->len++] = tbl[(v >> 12) & 63];
        sb->buf[sb->len++] = ((i + 1) < len) ? tbl[(v >> 6) & 63] : '=';
        sb->buf[sb->len++] = ((i + 2) < len) ? tbl[v & 63] : '=';
    }
    sb->buf[sb->len] = '\0';
}

void
automation_frame(int x, int y, int w, int h, int monitor_index)
{
    bitmap_t *target;

    if (!enabled || (monitor_index != 0) || (w <= 0) || (h <= 0))
        return;

    target = monitors[0].target_buffer;
    if (!target)
        return;

    thread_wait_mutex(frame_mutex);
    if ((w * h) > frame_alloc) {
        frame_alloc = w * h;
        frame_buf   = realloc(frame_buf, frame_alloc * sizeof(uint32_t));
    }
    for (int row = 0; row < h; row++)
        memcpy(&frame_buf[row * w], &target->line[y + row][x], w * sizeof(uint32_t));
    frame_w = w;
    frame_h = h;
    frame_count++;
    record_frame();
    thread_release_mutex(frame_mutex);
}

/* ---- Request handling (CPU thread) ---- */

static void
respond_ok_time(void)
{
    resp.len = 0;
    sb_printf(&resp, "{\"ok\":true,\"ms\":%llu,\"frames\":%llu,\"held\":%s}",
              (unsigned long long) emu_ms, (unsigned long long) frame_count, held ? "true" : "false");
}

/* Start a command that consumes emulated time. */
static void
begin_timed(int which, uint64_t ms)
{
    cmd          = which;
    cmd_was_held = held;
    cmd_deadline = emu_ms + ms;
    held         = 0;
}

static void
end_timed(int matched)
{
    cmd      = CMD_NONE;
    held     = cmd_was_held;
    resp.len = 0;
    sb_printf(&resp, "{\"ok\":true,\"ms\":%llu,\"frames\":%llu,\"held\":%s",
              (unsigned long long) emu_ms, (unsigned long long) frame_count, held ? "true" : "false");
    if (matched >= 0)
        sb_printf(&resp, ",\"matched\":%s", matched ? "true" : "false");
    sb_printf(&resp, "}");
    complete();
}

/* Returns 1 when the condition of a wait command is satisfied. */
static int
wait_satisfied(void)
{
    if (cmd == CMD_WAIT_TEXT)
        return text_contains(wait_text);

    if (cmd == CMD_WAIT_MEM) {
        for (int i = 0; i < wait_len; i++) {
            if (mem_readb_phys(wait_addr + i) != wait_bytes[i])
                return 0;
        }
        return 1;
    }

    return 0;
}

static void
handle_request(char *line)
{
    static char copy[REQ_MAX];
    char       *argv[16];
    char       *rest[16]; /* rest[i] = unsplit text from argv[i] onwards */
    int         argc = 0;
    char       *p    = line;

    strcpy(copy, line);
    while (*p && (argc < 16)) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        argv[argc] = p;
        rest[argc] = copy + (p - line);
        argc++;
        while (*p && (*p != ' '))
            p++;
        if (*p)
            *p++ = '\0';
    }

    automation_log("Automation: %s\n", copy);

    if (argc == 0) {
        respond_error("empty request");
        complete();
        return;
    }

    if (!strcmp(argv[0], "status")) {
        int mode = mem_readb_phys(0x449);
        resp.len = 0;
        sb_printf(&resp, "{\"ok\":true,\"protocol\":%d,\"version\":", AUTOMATION_PROTOCOL);
        sb_json_string(&resp, EMU_VERSION_FULL);
        sb_printf(&resp, ",\"ms\":%llu,\"frames\":%llu,\"held\":%s,\"paused\":%s,\"ff\":%s,\"w\":%d,\"h\":%d,\"bios_mode\":%d,\"quantum_ms\":%d}",
                  (unsigned long long) emu_ms, (unsigned long long) frame_count, held ? "true" : "false",
                  dopause ? "true" : "false", fast_forward ? "true" : "false", frame_w, frame_h, mode, force_10ms ? 10 : 1);
    } else if (!strcmp(argv[0], "hold")) {
        held = 1;
        respond_ok_time();
    } else if (!strcmp(argv[0], "release")) {
        held = 0;
        respond_ok_time();
    } else if (!strcmp(argv[0], "ff") && (argc <= 2)) {
        /* ff [on|off]: fast forward, as the menu's; with no argument, what
           it is. */
        if ((argc == 2) && strcmp(argv[1], "on") && strcmp(argv[1], "off")) {
            respond_error("ff takes on or off");
        } else {
            if (argc == 2)
                plat_fast_forward(!strcmp(argv[1], "on"));
            resp.len = 0;
            sb_printf(&resp, "{\"ok\":true,\"ff\":%s,\"ms\":%llu,\"frames\":%llu,\"held\":%s}",
                      fast_forward ? "true" : "false", (unsigned long long) emu_ms,
                      (unsigned long long) frame_count, held ? "true" : "false");
        }
    } else if (!strcmp(argv[0], "step") && (argc == 2)) {
        uint64_t ms = strtoull(argv[1], NULL, 0);
        if (!ms) {
            respond_ok_time();
        } else {
            begin_timed(CMD_STEP, ms);
            return; /* completed by automation_quantum() */
        }
    } else if (!strcmp(argv[0], "wait") && (argc >= 4) && !strcmp(argv[1], "text")) {
        /* wait text <timeout_ms> <text...> */
        strncpy(wait_text, rest[3], sizeof(wait_text) - 1);
        begin_timed(CMD_WAIT_TEXT, strtoull(argv[2], NULL, 0));
        if (wait_satisfied())
            end_timed(1);
        return;
    } else if (!strcmp(argv[0], "wait") && (argc == 5) && !strcmp(argv[1], "mem")) {
        /* wait mem <timeout_ms> <addr> <hexbytes> */
        wait_addr = strtoul(argv[3], NULL, 0);
        wait_len  = parse_hex_bytes(argv[4], wait_bytes, sizeof(wait_bytes));
        if (wait_len <= 0) {
            respond_error("bad hex bytes");
        } else {
            begin_timed(CMD_WAIT_MEM, strtoull(argv[2], NULL, 0));
            if (wait_satisfied())
                end_timed(1);
            return;
        }
    } else if (!strcmp(argv[0], "key") && (argc >= 3)) {
        /* key down|up <scancode>... ; scancodes in 86Box form, 0x1xx = E0 prefix.
           Events are delivered KEY_GAP_MS apart; the reply comes once the last
           one has had its gap, so back-to-back requests cannot flood the FIFO. */
        int down = !strcmp(argv[1], "down");
        if (!down && strcmp(argv[1], "up")) {
            respond_error("expected down or up");
        } else if ((argc - 2) > KEY_QUEUE_MAX) {
            respond_error("too many keys in one request");
        } else {
            key_head  = 0;
            key_count = 0;
            for (int i = 2; i < argc; i++) {
                key_queue[key_count].scan   = (uint16_t) strtoul(argv[i], NULL, 0);
                key_queue[key_count++].down = down;
            }
            begin_timed(CMD_KEYS, UINT64_MAX - emu_ms);
            keyboard_input(key_queue[0].down, key_queue[0].scan);
            key_head    = 1;
            key_next_ms = emu_ms + KEY_GAP_MS;
            return; /* completed by automation_quantum() */
        }
    } else if (!strcmp(argv[0], "mouse") && (argc == 4)) {
        /* mouse <dx> <dy> <buttons> */
        /* Mouse devices discard input unless the host UI has captured the
           pointer; mouse_automation lets ours through without touching the
           UI's capture state, so a visible window keeps working normally. */
        mouse_automation = 1;
        mouse_scale(atoi(argv[1]), atoi(argv[2]));
        mouse_set_buttons_ex(atoi(argv[3]));
        respond_ok_time();
    } else if (!strcmp(argv[0], "screenshot")) {
        /* screenshot <path>: write a PNG file on this machine.
           screenshot: return the PNG base64-encoded in the reply (works over TCP). */
        int ok;
        thread_wait_mutex(frame_mutex);
        ok       = (argc >= 2) ? write_png(rest[1]) : encode_png(&png_out);
        resp.len = 0;
        if (ok) {
            sb_printf(&resp, "{\"ok\":true,\"w\":%d,\"h\":%d,\"ms\":%llu,\"frames\":%llu",
                      frame_w, frame_h, (unsigned long long) emu_ms, (unsigned long long) frame_count);
            if (argc < 2) {
                sb_printf(&resp, ",\"png\":\"");
                sb_base64(&resp, (const uint8_t *) png_out.buf, png_out.len);
                sb_printf(&resp, "\"");
            }
            sb_printf(&resp, "}");
        }
        thread_release_mutex(frame_mutex);
        if (!ok)
            respond_error(frame_buf ? "could not write file" : "no frame rendered yet");
    } else if (!strcmp(argv[0], "record") && (argc >= 2)) {
        /* record start <interval_ms> <dir> | record stop */
        if (!strcmp(argv[1], "stop")) {
            thread_wait_mutex(frame_mutex);
            rec_interval = 0;
            resp.len     = 0;
            sb_printf(&resp, "{\"ok\":true,\"frames\":%u}", rec_frames);
            thread_release_mutex(frame_mutex);
        } else if (!strcmp(argv[1], "start") && (argc >= 4) && (atoi(argv[2]) > 0)) {
            thread_wait_mutex(frame_mutex);
            snprintf(rec_dir, sizeof(rec_dir), "%s", rest[3]);
            rec_interval = atoi(argv[2]);
            rec_next_ms  = emu_ms;
            rec_frames   = 0;
            thread_release_mutex(frame_mutex);
            respond_ok_time();
        } else
            respond_error("usage: record start <interval_ms> <dir> | record stop");
    } else if (!strcmp(argv[0], "info")) {
        cmd_info();
    } else if (!strcmp(argv[0], "text")) {
        cmd_text();
    } else if (!strcmp(argv[0], "memr") && (argc == 3)) {
        uint32_t addr = strtoul(argv[1], NULL, 0);
        int      len  = atoi(argv[2]);
        uint8_t *buf;
        if ((len <= 0) || (len > MEM_MAX)) {
            respond_error("length out of range");
        } else {
            buf = malloc(len);
            for (int i = 0; i < len; i++)
                buf[i] = mem_readb_phys(addr + i);
            resp.len = 0;
            sb_printf(&resp, "{\"ok\":true,\"addr\":%u,\"data\":\"", addr);
            sb_hex(&resp, buf, len);
            sb_printf(&resp, "\"}");
            free(buf);
        }
    } else if (!strcmp(argv[0], "memw") && (argc == 3)) {
        uint32_t addr = strtoul(argv[1], NULL, 0);
        uint8_t *buf  = malloc(strlen(argv[2]) / 2 + 1);
        int      len  = parse_hex_bytes(argv[2], buf, strlen(argv[2]) / 2 + 1);
        if (len <= 0) {
            respond_error("bad hex bytes");
        } else {
            for (int i = 0; i < len; i++)
                mem_writeb_phys(addr + i, buf[i]);
            respond_ok_time();
        }
        free(buf);
    } else if (!strcmp(argv[0], "regs")) {
        static const char *names[8] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
        resp.len                    = 0;
        sb_printf(&resp, "{\"ok\":true");
        for (int i = 0; i < 8; i++)
            sb_printf(&resp, ",\"%s\":%u", names[i], cpu_state.regs[i].l);
        sb_printf(&resp, ",\"eip\":%u,\"eflags\":%u,\"cs\":%u,\"ds\":%u,\"es\":%u,\"ss\":%u,\"fs\":%u,\"gs\":%u,\"cs_base\":%u,\"cr0\":%u}",
                  cpu_state.pc, ((uint32_t) cpu_state.eflags << 16) | cpu_state.flags,
                  cpu_state.seg_cs.seg, cpu_state.seg_ds.seg, cpu_state.seg_es.seg,
                  cpu_state.seg_ss.seg, cpu_state.seg_fs.seg, cpu_state.seg_gs.seg,
                  cpu_state.seg_cs.base, cr0);
    } else if (!strcmp(argv[0], "fdd") && (argc >= 3)) {
        /* fdd load <drive> <path> | fdd eject <drive> */
        int id = atoi(argv[2]);
        if ((id < 0) || (id >= FDD_NUM)) {
            respond_error("bad drive");
        } else if (!strcmp(argv[1], "eject")) {
            fdd_close(&drives[id]);
            ui_sb_update_icon_state(SB_FLOPPY | id, 1);
            respond_ok_time();
        } else if (!strcmp(argv[1], "load") && (argc >= 4)) {
            fdd_close(&drives[id]);
            drives[id].read_only = 0;
            fdd_load(&drives[id], rest[3]);
            ui_sb_update_icon_state(SB_FLOPPY | id, drives[id].image_path[0] ? 0 : 1);
            respond_ok_time();
        } else
            respond_error("usage: fdd load <drive> <path> | fdd eject <drive>");
    } else if (!strcmp(argv[0], "cd") && (argc >= 3)) {
        /* cd load <drive> <path> | cd eject <drive> */
        int id = atoi(argv[2]);
        if ((id < 0) || (id >= CDROM_NUM)) {
            respond_error("bad drive");
        } else if (!strcmp(argv[1], "eject")) {
            cdrom_eject(id);
            respond_ok_time();
        } else if (!strcmp(argv[1], "load") && (argc >= 4)) {
            int was_empty = cdrom_is_empty(id);
            cdrom_exit(id);
            memset(cdrom[id].image_path, 0, sizeof(cdrom[id].image_path));
            cdrom_load(&cdrom[id], rest[3], 1);
            /* Signal media change to the emulated machine. */
            if (cdrom[id].insert) {
                cdrom[id].insert(cdrom[id].priv);
                if (was_empty)
                    cdrom[id].insert(cdrom[id].priv);
            }
            ui_sb_update_icon_state(SB_CDROM | id, cdrom[id].image_path[0] ? 0 : 1);
            respond_ok_time();
        } else
            respond_error("usage: cd load <drive> <path> | cd eject <drive>");
    } else if (!strcmp(argv[0], "reset") && (argc == 2) && !strcmp(argv[1], "hard")) {
        pc_reset_hard();
        respond_ok_time();
    } else if (!strcmp(argv[0], "reset") && (argc == 2) && !strcmp(argv[1], "cad")) {
        pc_send_cad();
        respond_ok_time();
    } else if (!strcmp(argv[0], "quit") && (argc == 1)) {
        respond_ok_time();
        complete();
        plat_power_off();
        return;
    } else if (!strcmp(argv[0], "quit") && (argc == 2) && !strcmp(argv[1], "soft")) {
        respond_ok_time();
        complete();
        if (acpi_enabled)
            acpi_pwrbut_pressed = 1;
        else if (device_has_power_button())
            device_power_button();
        else
            plat_power_off();
        return;
    } else {
        respond_error("unknown command or wrong arguments");
    }

    complete();
}

void
automation_poll(void)
{
    if (!enabled || (cmd != CMD_NONE) || !atomic_load(&req_pending))
        return;

    handle_request(req_line);
}

int
automation_run_state(void)
{
    if (!enabled)
        return AUTOMATION_FREE;
    if (cmd != CMD_NONE)
        return cmd_was_held ? AUTOMATION_RUN : AUTOMATION_FREE;
    return held ? AUTOMATION_HOLD : AUTOMATION_FREE;
}

void
automation_quantum(void)
{
    if (!enabled)
        return;

    emu_ms += force_10ms ? 10 : 1;

    /* Speed while running, held steps included: emulated vs host time per
       emulated second. Wall time spent held between requests is excluded. */
    if (!speed_wall_start || (emu_ms - speed_emu_start) > 1000) {
        speed_wall_start = plat_get_ticks();
        speed_emu_start  = emu_ms;
    } else if ((emu_ms - speed_emu_start) == 1000) {
        uint32_t wall    = plat_get_ticks() - speed_wall_start;
        speed_percent    = wall ? (int) (100000 / wall) : -1;
        speed_wall_start = 0;
    }

    if (cmd == CMD_NONE)
        return;

    if (cmd == CMD_KEYS) {
        if (emu_ms < key_next_ms)
            return;
        if (key_head == key_count) {
            end_timed(-1);
            return;
        }
        keyboard_input(key_queue[key_head].down, key_queue[key_head].scan);
        key_head++;
        key_next_ms = emu_ms + KEY_GAP_MS;
    } else if ((cmd == CMD_WAIT_TEXT) || (cmd == CMD_WAIT_MEM)) {
        if (wait_satisfied())
            end_timed(1);
        else if (emu_ms >= cmd_deadline)
            end_timed(0);
    } else if (emu_ms >= cmd_deadline)
        end_timed(-1);
}

/* ---- Listener thread ---- */

static int
write_all(sock_t fd, const char *buf, size_t len)
{
    while (len) {
        int n = send(fd, buf, (int) len, 0);
        if (n < 0) {
#ifndef _WIN32
            if (errno == EINTR)
                continue;
#endif
            return 0;
        }
        buf += n;
        len -= n;
    }
    return 1;
}

/* Compare without an early exit, so timing reveals nothing about the token. */
static int
token_matches(const char *given)
{
    size_t        len  = strlen(token);
    unsigned char diff = (strlen(given) != len);

    for (size_t i = 0; i < len; i++)
        diff |= (unsigned char) (given[i] ^ token[i]) | (unsigned char) !given[i];
    return !diff;
}

static void
automation_thread(UNUSED(void *priv))
{
    static char inbuf[REQ_MAX];
    size_t      inlen;
    int         authed;

    while (enabled) {
        client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd == SOCK_INVALID) {
#ifndef _WIN32
            if (errno == EINTR)
                continue;
#endif
            break;
        }
        automation_log("Automation: client connected\n");
        inlen  = 0;
        authed = !use_tcp;

        while (enabled) {
            char *nl;
            int   n = recv(client_fd, inbuf + inlen, (int) (sizeof(inbuf) - 1 - inlen), 0);
            if (n <= 0) {
#ifndef _WIN32
                if ((n < 0) && (errno == EINTR))
                    continue;
#endif
                break;
            }
            inlen += n;
            inbuf[inlen] = '\0';

            while ((nl = memchr(inbuf, '\n', inlen))) {
                size_t linelen = nl - inbuf;
                *nl            = '\0';
                if (linelen && (inbuf[linelen - 1] == '\r'))
                    inbuf[linelen - 1] = '\0';

                if (!authed) {
                    /* TCP: the first line must be "auth <token>"; anything else ends the connection. */
                    if (!strncmp(inbuf, "auth ", 5) && token_matches(inbuf + 5)) {
                        authed                 = 1;
                        static const char ok[] = "{\"ok\":true,\"auth\":true}\n";
                        if (!write_all(client_fd, ok, strlen(ok)))
                            goto disconnect;
                    } else {
                        pclog("Automation: rejected a TCP client without a valid token\n");
                        static const char no[] = "{\"ok\":false,\"error\":\"authentication required\"}\n";
                        write_all(client_fd, no, strlen(no));
                        goto disconnect;
                    }
                } else {
                    strncpy(req_line, inbuf, sizeof(req_line) - 1);
                    req_line[sizeof(req_line) - 1] = '\0';
                    thread_reset_event(req_done);
                    atomic_store(&req_pending, 1);
                    thread_wait_event(req_done, -1);

                    if (!write_all(client_fd, resp.buf, resp.len) || !write_all(client_fd, "\n", 1))
                        goto disconnect;
                }

                inlen -= linelen + 1;
                memmove(inbuf, nl + 1, inlen);
            }

            if (inlen >= (sizeof(inbuf) - 1)) {
                /* Overlong request: drop it. */
                inlen = 0;
            }
        }
disconnect:
        automation_log("Automation: client disconnected\n");
        sock_close(client_fd);
        client_fd = SOCK_INVALID;
    }
}

/* "unix:<path>", "tcp:[host:]port", or a bare value: a UNIX socket path, or
   a TCP port on Windows, which has no UNIX sockets here. */
static int
parse_endpoint(const char *spec)
{
    const char *rest = spec;
    const char *colon;

#ifdef _WIN32
    use_tcp = 1;
#else
    use_tcp = 0;
#endif
    if (!strncmp(spec, "unix:", 5)) {
        use_tcp = 0;
        rest    = spec + 5;
    } else if (!strncmp(spec, "tcp:", 4)) {
        use_tcp = 1;
        rest    = spec + 4;
    }

    if (!use_tcp) {
#ifdef _WIN32
        pclog("Automation: UNIX sockets are not supported on Windows; use tcp:[host:]port\n");
        return 0;
#else
        snprintf(unix_path, sizeof(unix_path), "%s", rest);
        return unix_path[0] != '\0';
#endif
    }

    colon                = strrchr(rest, ':');
    const char *port     = colon ? colon + 1 : rest;
    size_t      host_len = colon ? (size_t) (colon - rest) : 0;

    if ((strlen(port) >= sizeof(tcp_port)) || (host_len >= sizeof(tcp_host)))
        return 0;
    strcpy(tcp_port, port);
    if (colon) {
        memcpy(tcp_host, rest, host_len);
        tcp_host[host_len] = '\0';
    } else
        strcpy(tcp_host, "127.0.0.1");
    return tcp_host[0] && atoi(tcp_port) > 0;
}

static int
is_loopback(const struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
        return (ntohl(((const struct sockaddr_in *) sa)->sin_addr.s_addr) >> 24) == 127;
    if (sa->sa_family == AF_INET6) {
        static const unsigned char lo[16] = { [15] = 1 };
        return !memcmp(&((const struct sockaddr_in6 *) sa)->sin6_addr, lo, 16);
    }
    return 0;
}

static int
make_token(void)
{
    unsigned char bytes[32];

#ifdef _WIN32
    for (int i = 0; i < 32; i += 4) {
        unsigned int v;
        if (rand_s(&v))
            return 0;
        memcpy(bytes + i, &v, 4);
    }
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f)
        return 0;
    size_t got = fread(bytes, 1, sizeof(bytes), f);
    fclose(f);
    if (got != sizeof(bytes))
        return 0;
#endif
    for (int i = 0; i < 32; i++)
        sprintf(token + (i * 2), "%02x", bytes[i]);
    return 1;
}

/* Owner-only on POSIX; on Windows the file inherits the folder's permissions. */
static int
write_token_file(void)
{
    FILE *f;

    if (!automation_token_file[0]) {
        static const char name[] = "automation.token";
        if ((strlen(usr_path) + sizeof(name)) > sizeof(automation_token_file))
            return 0;
        strcpy(automation_token_file, usr_path);
        strcat(automation_token_file, name);
    }
#ifdef _WIN32
    f = plat_fopen(automation_token_file, "wb");
#else
    int fd = open(automation_token_file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return 0;
    fchmod(fd, 0600);
    f = fdopen(fd, "wb");
#endif
    if (!f)
        return 0;
    fprintf(f, "%s\n", token);
    fclose(f);
    return 1;
}

static int
listen_tcp(void)
{
    struct addrinfo  hints;
    struct addrinfo *res = NULL;
    int              one = 1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;
    if (getaddrinfo(tcp_host, tcp_port, &hints, &res) || !res) {
        pclog("Automation: cannot resolve %s\n", tcp_host);
        return 0;
    }
    if (!is_loopback(res->ai_addr) && !automation_allow_remote) {
        pclog("Automation: refusing to listen on non-loopback address %s; "
              "pass --automation-allow-remote if you really want that\n",
              tcp_host);
        freeaddrinfo(res);
        return 0;
    }
    if (!is_loopback(res->ai_addr))
        pclog("Automation: WARNING: listening on %s; anyone who can reach it and has the token "
              "fully controls this machine, including its memory\n",
              tcp_host);

    listen_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (listen_fd == SOCK_INVALID) {
        freeaddrinfo(res);
        return 0;
    }
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &one, sizeof(one));
    if (bind(listen_fd, res->ai_addr, (int) res->ai_addrlen) || listen(listen_fd, 1)) {
        pclog("Automation: cannot listen on %s:%s\n", tcp_host, tcp_port);
        sock_close(listen_fd);
        listen_fd = SOCK_INVALID;
        freeaddrinfo(res);
        return 0;
    }
    freeaddrinfo(res);

    if (!make_token() || !write_token_file()) {
        pclog("Automation: cannot create the token file %s\n", automation_token_file);
        sock_close(listen_fd);
        listen_fd = SOCK_INVALID;
        return 0;
    }
    return 1;
}

#ifndef _WIN32
static int
listen_unix(void)
{
    struct sockaddr_un addr;

    if (strlen(unix_path) >= sizeof(addr.sun_path)) {
        pclog("Automation: socket path too long: %s\n", unix_path);
        return 0;
    }
    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0)
        return 0;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, unix_path);
    unlink(unix_path);

    /* Only this user may connect: create the socket owner-only, so there is
       no moment when it has the default permissions. */
    mode_t old_umask = umask(077);
    int    bound     = bind(listen_fd, (struct sockaddr *) &addr, sizeof(addr));
    umask(old_umask);
    if ((bound < 0) || (listen(listen_fd, 1) < 0)) {
        pclog("Automation: cannot listen on %s: %s\n", unix_path, strerror(errno));
        close(listen_fd);
        listen_fd = -1;
        return 0;
    }
    chmod(unix_path, 0600);
    return 1;
}
#endif

void
automation_init(void)
{
    if (!automation_path[0] || enabled)
        return;

    if (!parse_endpoint(automation_path)) {
        pclog("Automation: bad endpoint \"%s\"; use unix:<path> or tcp:[host:]port\n", automation_path);
        return;
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) {
        pclog("Automation: WSAStartup failed\n");
        return;
    }
    if (!listen_tcp())
        return;
#else
    if (use_tcp ? !listen_tcp() : !listen_unix())
        return;
#endif

    resp.size   = 4096;
    resp.buf    = malloc(resp.size);
    resp.len    = 0;
    req_done    = thread_create_event();
    frame_mutex = thread_create_mutex();
    held        = automation_start_held;
    enabled     = 1;

    listen_thread = thread_create_named(automation_thread, NULL, "automation");
    if (use_tcp)
        pclog("Automation: listening on tcp:%s:%s (token %.8s..., in %s)%s\n", tcp_host, tcp_port, token,
              automation_token_file, held ? " (held)" : "");
    else
        pclog("Automation: listening on unix:%s%s\n", unix_path, held ? " (held)" : "");
}

void
automation_close(void)
{
    if (!enabled)
        return;

    enabled = 0;
    shutdown(listen_fd, SOCK_SHUT_RW);
    sock_close(listen_fd);
    listen_fd = SOCK_INVALID;
    if (client_fd != SOCK_INVALID)
        shutdown(client_fd, SOCK_SHUT_RW);
    /* Release a listener that is waiting on the CPU thread, then let it finish. */
    thread_set_event(req_done);
    thread_wait(listen_thread);
    listen_thread = NULL;
#ifndef _WIN32
    if (!use_tcp)
        unlink(unix_path);
#endif
    if (use_tcp)
        remove(automation_token_file);
}
