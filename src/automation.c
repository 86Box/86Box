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
#include <math.h>
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
#    define SOCK_SHUT_RD SD_RECEIVE
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
#    define SOCK_SHUT_RD SHUT_RD
#endif
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x87_sf.h"
#include "x87.h"
#include "x87_ops_conv.h"
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
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/ui.h>
#include <86box/video.h>
#include <86box/version.h>
#include <86box/nvr.h>
#include <86box/acpi.h>
#include <86box/char.h>
#include <86box/serial.h>
#include <86box/automation.h>
#include <zstd.h>
#ifdef USE_GDBSTUB
#    include <86box/gdbstub.h>
#endif

#define AUTOMATION_PROTOCOL 1
#define REQ_MAX             4096
#define MEM_MAX             65536
#define KEY_QUEUE_MAX       64
#define LOG_RING            65536              /* the log's last bytes, for log */
#define MEDIA_DIR           "automation-media" /* in the VM directory, for media put */
#define MEDIA_NAME_MAX      (REQ_MAX - 1) /* any name the request line can hold */
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
    CMD_WAIT_SERIAL,
    CMD_KEYS,
    CMD_WAIT_SOUND,
    CMD_WAIT_SILENCE
};

typedef struct {
    char  *buf;
    size_t len;
    size_t size;
} strbuf_t;

char automation_path[1024]       = "";
char automation_token_file[1024] = "";
int  automation_start_held       = 0;
int  automation_seed_set         = 0;
uint32_t automation_seed         = 0;
int  automation_allow_remote     = 0;
int  automation_insecure         = 0;

static int       enabled;
static thread_t *listen_thread;
static sock_t    listen_fd = SOCK_INVALID;
static sock_t    client_fd = SOCK_INVALID;
static event_t  *req_done;
static char      fatal_msg[1024]; /* for the client, on a fatal error */

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
static char     timed_extra[256]; /* more fields for the reply that ends a timed command */

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
static FILE    *rec_log;  /* <dir>/frames.jsonl: one line per frame saved */
static uint64_t rec_tsc0; /* the CPU cycle count when recording started */

/* Recording without a directory: frames wait here with their stamps, for a
   client to fetch with record pull (over TCP, from another machine). Each is
   the frame as RGB24, XORed with the frame queued before it unless it is a
   key frame (the first, after a size change, after a gap), compressed with
   zstd: an unchanged screen costs almost nothing, and zstd encodes several
   times faster than PNG. Past REC_QUEUE_MAX bytes new frames are skipped and
   counted as lost (queued ones are kept: the next frame is XORed with them),
   and the next one queued is a key frame. */
#define REC_QUEUE_MAX (256u << 20)
typedef struct {
    char    *stamp; /* the frame's frames.jsonl fields, without the braces */
    uint8_t *data;  /* zstd */
    size_t   len;
    int      w;
    int      h;
    int      key;
} rec_item_t;
static rec_item_t *rec_q;
static size_t      rec_q_head;
static size_t      rec_q_len;
static size_t      rec_q_cap;
static size_t      rec_q_bytes;
static uint64_t    rec_lost;
static uint8_t    *rec_prev; /* RGB24 of the last frame queued */
static int         rec_prev_w;
static int         rec_prev_h;
static int         rec_need_key;
static uint32_t rec_frames;

/* COM ports with the "automation" backend. Positions count every byte since
   startup, so a client can read on from where it left off; the oldest bytes
   are dropped once more than SERIAL_OUT_MAX are waiting. */
#define SERIAL_OUT_MAX 65536 /* a power of two */
#define SERIAL_IN_MAX  65536

typedef struct {
    int      attached;
    uint8_t  out[SERIAL_OUT_MAX]; /* guest to client */
    uint64_t out_pos;             /* bytes the guest has sent */
    uint64_t out_last_ms;         /* emulated ms of the last one */
    uint8_t  in[SERIAL_IN_MAX];   /* client to guest */
    int      in_head;
    int      in_count;
    uint64_t in_pos;  /* bytes the guest has taken */
    uint64_t in_tsc;  /* CPU timestamp when it took the last one */
    uint32_t lines;  /* CTS, DSR, DCD and RI, as the guest sees them */
    uint32_t control; /* DTR, RTS and break, as the guest sets them */
} serial_port_state_t;

static serial_port_state_t *serial_ports[SERIAL_MAX];
static int                  wait_port;
static uint64_t             wait_scan;  /* where the next search starts */
static uint64_t             wait_found; /* where the text was found */

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

/* Read guest memory for a client without charging the emulated CPU for it.
   Device handlers bill the CPU for each access: VGA charges its wait states
   (svga_read_common subtracts mon_video_timing_read_b from cycles). wait text
   reads the whole text screen after every quantum, 2000 reads per emulated
   millisecond, which on a 33 MHz 386 used up the cycles of the next quantum,
   so the guest never ran while the wait went on. */
static uint8_t
peek_phys(uint32_t addr)
{
    int     saved = cycles;
    uint8_t val   = mem_readb_phys(addr);

    cycles = saved;
    return val;
}

/* The same for a client's writes (memw). */
static void
poke_phys(uint32_t addr, uint8_t val)
{
    int saved = cycles;

    mem_writeb_phys(addr, val);
    cycles = saved;
}

static int
text_info(text_info_t *ti)
{
    int page;

    ti->mode = peek_phys(0x449) & 0x7f;
    if ((ti->mode > 3) && (ti->mode != 7))
        return 0;
    ti->cols = peek_phys(0x44a) | (peek_phys(0x44b) << 8);
    ti->rows = peek_phys(0x484) + 1;
    if ((ti->cols < 40) || (ti->cols > 132))
        ti->cols = 80;
    if ((ti->rows < 25) || (ti->rows > 60))
        ti->rows = 25;
    page        = peek_phys(0x462) & 7;
    ti->cur_col = peek_phys(0x450 + (page * 2));
    ti->cur_row = peek_phys(0x451 + (page * 2));
    ti->base    = ((ti->mode == 7) ? 0xb0000 : 0xb8000) + (peek_phys(0x44e) | (peek_phys(0x44f) << 8));
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
            line[c] = peek_phys(ti.base + (((r * ti.cols) + c) * 2));
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
            chars[c] = peek_phys(ti.base + (((r * ti.cols) + c) * 2));
        sb_printf(&resp, r ? ",\"" : "\"");
        sb_hex(&resp, chars, ti.cols);
        sb_printf(&resp, "\"");
    }
    sb_printf(&resp, "],\"attrs\":[");
    for (int r = 0; r < ti.rows; r++) {
        for (int c = 0; c < ti.cols; c++)
            attrs[c] = peek_phys(ti.base + (((r * ti.cols) + c) * 2) + 1);
        sb_printf(&resp, r ? ",\"" : "\"");
        sb_hex(&resp, attrs, ti.cols);
        sb_printf(&resp, "\"");
    }
    sb_printf(&resp, "]}");
}

/* ---- Serial ports (CPU thread) ---- */

static serial_port_state_t *
serial_state(int port)
{
    return ((port >= 1) && (port <= SERIAL_MAX)) ? serial_ports[port - 1] : NULL;
}

void
automation_serial_attach(int port, int attached)
{
    serial_port_state_t *s;

    if ((port < 1) || (port > SERIAL_MAX))
        return;
    s = serial_ports[port - 1];
    if (!s) {
        if (!attached)
            return;
        s        = (serial_port_state_t *) calloc(1, sizeof(serial_port_state_t));
        s->lines = CHAR_COM_CTS | CHAR_COM_DSR | CHAR_COM_DCD;
        serial_ports[port - 1] = s;
    }
    s->attached = attached;
}

void
automation_serial_out(int port, uint8_t val)
{
    serial_port_state_t *s = serial_state(port);

    if (!s)
        return;
    s->out[s->out_pos & (SERIAL_OUT_MAX - 1)] = val;
    s->out_pos++;
    s->out_last_ms = emu_ms;
}

int
automation_serial_in(int port, uint8_t *val, double byte_us)
{
    serial_port_state_t *s = serial_state(port);

    if (!s || !s->in_count)
        return 0;
    if (byte_us > 0.0) {
        /* TIMER_USEC is CPU cycles per microsecond, in 32.32 fixed point. A
           timestamp that went backwards (the guest wrote the TSC) restarts. */
        uint64_t gap = (uint64_t) (byte_us * ((double) TIMER_USEC / 4294967296.0));
        if ((tsc >= s->in_tsc) && ((tsc - s->in_tsc) < gap))
            return 0;
        s->in_tsc = tsc;
    }
    *val       = s->in[s->in_head];
    s->in_head = (s->in_head + 1) % SERIAL_IN_MAX;
    s->in_count--;
    s->in_pos++;
    return 1;
}

uint32_t
automation_serial_lines(int port)
{
    serial_port_state_t *s = serial_state(port);

    return s ? s->lines : 0;
}

void
automation_serial_control(int port, uint32_t control)
{
    serial_port_state_t *s = serial_state(port);

    if (s)
        s->control = control;
}

static uint64_t
serial_oldest(const serial_port_state_t *s)
{
    return (s->out_pos > SERIAL_OUT_MAX) ? (s->out_pos - SERIAL_OUT_MAX) : 0;
}

/* The port named in a request, or NULL after replying with an error. */
static serial_port_state_t *
serial_arg(const char *arg, int *port)
{
    serial_port_state_t *s;
    char                 msg[64];

    *port = atoi(arg);
    s     = serial_state(*port);
    if (!s) {
        snprintf(msg, sizeof(msg), "COM%d has no automation backend", *port);
        respond_error(msg);
    }
    return s;
}

/* Search what the guest sent, from wait_scan on, for wait_text. */
static int
serial_contains(void)
{
    serial_port_state_t *s    = serial_state(wait_port);
    size_t               nlen = strlen(wait_text);
    uint64_t             p;

    if (!s)
        return 0;
    if (wait_scan < serial_oldest(s))
        wait_scan = serial_oldest(s);
    for (p = wait_scan; (p + nlen) <= s->out_pos; p++) {
        size_t i;
        for (i = 0; i < nlen; i++) {
            if (s->out[(p + i) & (SERIAL_OUT_MAX - 1)] != (uint8_t) wait_text[i])
                break;
        }
        if (i == nlen) {
            wait_found = p;
            return 1;
        }
    }
    /* A match could still start in the last nlen - 1 bytes. */
    wait_scan = p;
    return 0;
}

static void
sb_serial_lines(strbuf_t *sb, const serial_port_state_t *s)
{
    sb_printf(sb, ",\"dtr\":%d,\"rts\":%d,\"break\":%d,\"cts\":%d,\"dsr\":%d,\"dcd\":%d,\"ri\":%d",
              !!(s->control & CHAR_COM_DTR), !!(s->control & CHAR_COM_RTS), !!(s->control & CHAR_COM_BREAK),
              !!(s->lines & CHAR_COM_CTS), !!(s->lines & CHAR_COM_DSR), !!(s->lines & CHAR_COM_DCD),
              !!(s->lines & CHAR_COM_RI));
}

/* serial read <port> [<from> [<max>]]: what the guest sent from position
   <from> on (default: the oldest byte still kept). */
static void
cmd_serial_read(int argc, char **argv)
{
    serial_port_state_t *s;
    int                  port;
    uint64_t             from, lost = 0, oldest;
    uint64_t             max = SERIAL_OUT_MAX;
    uint8_t             *buf;
    int                  n;

    if (!(s = serial_arg(argv[2], &port)))
        return;
    from = (argc >= 4) ? strtoull(argv[3], NULL, 0) : 0;
    if (argc >= 5) {
        max = strtoull(argv[4], NULL, 0);
        if (!max || (max > SERIAL_OUT_MAX)) {
            respond_error("max out of range");
            return;
        }
    }
    oldest = serial_oldest(s);
    if (from < oldest) {
        lost = oldest - from;
        from = oldest;
    }
    if (from > s->out_pos)
        from = s->out_pos;
    n   = (int) (((s->out_pos - from) < max) ? (s->out_pos - from) : max);
    buf = malloc(n ? n : 1);
    for (int i = 0; i < n; i++)
        buf[i] = s->out[(from + i) & (SERIAL_OUT_MAX - 1)];
    resp.len = 0;
    sb_printf(&resp, "{\"ok\":true,\"port\":%d,\"attached\":%s,\"from\":%llu,\"next\":%llu,\"pos\":%llu,\"lost\":%llu,"
                     "\"last_ms\":%llu,\"pending\":%d,\"taken\":%llu,\"ms\":%llu",
              port, s->attached ? "true" : "false", (unsigned long long) from, (unsigned long long) (from + n),
              (unsigned long long) s->out_pos, (unsigned long long) lost, (unsigned long long) s->out_last_ms,
              s->in_count, (unsigned long long) s->in_pos, (unsigned long long) emu_ms);
    sb_serial_lines(&resp, s);
    sb_printf(&resp, ",\"data\":\"");
    sb_hex(&resp, buf, n);
    sb_printf(&resp, "\"}");
    free(buf);
}

/* serial write <port> <hex>: bytes for the guest, delivered at the rate
   the guest's UART is set to as emulated time passes. */
static void
cmd_serial_write(char **argv)
{
    serial_port_state_t *s;
    int                  port, len;
    uint8_t             *buf;

    if (!(s = serial_arg(argv[2], &port)))
        return;
    buf = malloc(strlen(argv[3]) / 2 + 1);
    len = parse_hex_bytes(argv[3], buf, strlen(argv[3]) / 2 + 1);
    if (len <= 0)
        respond_error("bad hex bytes");
    else if ((s->in_count + len) > SERIAL_IN_MAX)
        respond_error("input queue full");
    else {
        for (int i = 0; i < len; i++)
            s->in[(s->in_head + s->in_count + i) % SERIAL_IN_MAX] = buf[i];
        s->in_count += len;
        resp.len = 0;
        sb_printf(&resp, "{\"ok\":true,\"port\":%d,\"queued\":%d,\"pending\":%d,\"ms\":%llu}",
                  port, len, s->in_count, (unsigned long long) emu_ms);
    }
    free(buf);
}

/* serial lines <port> <cts> <dsr> <dcd> <ri>: the modem status lines the
   guest sees, each 0 or 1. */
static void
cmd_serial_lines(char **argv)
{
    serial_port_state_t *s;
    int                  port;

    if (!(s = serial_arg(argv[2], &port)))
        return;
    s->lines = (atoi(argv[3]) ? CHAR_COM_CTS : 0) | (atoi(argv[4]) ? CHAR_COM_DSR : 0) |
               (atoi(argv[5]) ? CHAR_COM_DCD : 0) | (atoi(argv[6]) ? CHAR_COM_RI : 0);
    resp.len = 0;
    sb_printf(&resp, "{\"ok\":true,\"port\":%d,\"ms\":%llu", port, (unsigned long long) emu_ms);
    sb_serial_lines(&resp, s);
    sb_printf(&resp, "}");
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
    sb_printf(&resp, "],\"serial\":[");
    first = 1;
    for (int i = 0; i < SERIAL_MAX; i++) {
        serial_port_state_t *s = serial_ports[i];
        if (!com_ports[i].enabled)
            continue;
        sb_printf(&resp, "%s{\"port\":%d,\"device\":", first ? "" : ",", i + 1);
        sb_json_string(&resp, com_ports[i].device ? char_get_device(com_ports[i].device)->internal_name : "none");
        if (com_ports[i].serial && com_ports[i].serial->char_port.attached)
            sb_printf(&resp, ",\"baud\":%u,\"data_bits\":%u,\"parity\":%u,\"stop_bits\":%u",
                      com_ports[i].serial->char_port.com.baud, com_ports[i].serial->char_port.com.data_bits,
                      com_ports[i].serial->char_port.com.parity, com_ports[i].serial->char_port.com.stop_bits);
        if (s) {
            sb_printf(&resp, ",\"pos\":%llu,\"oldest\":%llu,\"last_ms\":%llu,\"pending\":%d",
                      (unsigned long long) s->out_pos, (unsigned long long) serial_oldest(s),
                      (unsigned long long) s->out_last_ms, s->in_count);
            sb_serial_lines(&resp, s);
        }
        sb_printf(&resp, "}");
        first = 0;
    }
    sb_printf(&resp, "]");
#ifdef USE_GDBSTUB
    sb_printf(&resp, ",\"gdbstub_port\":%d", gdbstub_port);
#endif
    sb_printf(&resp, ",\"hard_resets\":%u,\"soft_resets\":%u,\"last_reset_ms\":%llu,\"speed_percent\":%d,"
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

/* ---- Audio ---- */

/* Every block each sound stream hands to the host backend comes here first,
   on whichever thread made it. The streams the sound core polls on emulated
   timers (main, music, wavetable, ym2151) are sample-exact: frame n of a
   recording is at start_ms + n / rate. The CD, floppy, hard disk and MIDI
   synth streams are rendered on their own threads, so their timing is only
   as good as the moment each block arrives. */

static void sb_base64(strbuf_t *sb, const uint8_t *data, size_t len);

#define AUD_RING_SECONDS 30
#define AUD_MAX_SEGS     16

static const char *aud_names[I_MAX] = { "main", "music", "wavetable", "cd", "fdd", "hdd", "ym2151", "midi" };

typedef struct {
    int16_t *ring; /* stereo frames, the last AUD_RING_SECONDS */
    uint32_t ring_frames;
    uint32_t ring_pos;
    uint64_t total;     /* frames received */
    uint64_t ring_base; /* total when the ring was (re)made: nothing older is in it */
    int      rate;
    uint64_t last_ms; /* emu_ms when the last block arrived */

    int      rec;  /* recording this stream */
    int      open; /* a segment is being recorded (to a file, or in memory) */
    FILE    *wav;
    uint32_t skip; /* frames of the next blocks that precede the start */
    uint64_t frames;
    double   first_ms; /* emulated time of the first frame written */
    int      exact;    /* first_ms is sample-exact */
    char     file[32];
    /* A rate change (another MIDI synth, a new sample rate setting) closes
       the file and starts another: each segment has one rate and start. */
    struct {
        char     file[32];
        int      rate;
        uint64_t frames;
        double   start_ms;
        int      exact;
    } segs[AUD_MAX_SEGS];
    int nsegs;
    /* The current (or last) segment in received frames (total), for audio
       pull: position 0 is seg_origin; it ends at seg_end once closed. */
    uint64_t seg_origin;
    uint64_t seg_end;
    int      seg_no;

    uint64_t loud_ms; /* wait silence: time of the last frame above the level */
} aud_t;

static aud_t    aud[I_MAX];
static mutex_t *aud_mutex;
static int16_t *aud_scratch;
static int      aud_scratch_len;
static int      aud_recording;
static uint64_t aud_start_ms;
static char     aud_dir[1024]; /* empty: record in memory only (audio pull) */

static int      aud_wait;       /* CMD_WAIT_SOUND or CMD_WAIT_SILENCE while one runs */
static int      aud_wait_level; /* threshold, in 16-bit sample units */
static uint64_t aud_wait_for;   /* wait silence: ms that must stay below it */
static int      aud_hit = -1;   /* wait sound: the stream that crossed it */
static double   aud_onset_ms;

static int
aud_exact(int src)
{
    return (src == I_NORMAL) || (src == I_MUSIC) || (src == I_WT) || (src == I_YM2151);
}

static void
wav_header(FILE *f, int rate, uint64_t frames)
{
    uint32_t data = (uint32_t) (frames * 4);
    uint8_t  h[44];

    memcpy(h, "RIFF", 4);
    h[4]  = (36 + data) & 0xff;
    h[5]  = ((36 + data) >> 8) & 0xff;
    h[6]  = ((36 + data) >> 16) & 0xff;
    h[7]  = ((36 + data) >> 24) & 0xff;
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16, h[17] = h[18] = h[19] = 0;
    h[20] = 1, h[21] = 0; /* PCM */
    h[22] = 2, h[23] = 0; /* stereo */
    for (int i = 0; i < 4; i++) {
        h[24 + i] = (rate >> (8 * i)) & 0xff;
        h[28 + i] = ((rate * 4) >> (8 * i)) & 0xff;
        h[40 + i] = (data >> (8 * i)) & 0xff;
    }
    h[32] = 4, h[33] = 0;  /* block align */
    h[34] = 16, h[35] = 0; /* bits */
    memcpy(h + 36, "data", 4);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, 44, f);
}

/* Close the stream's current file and remember it as a segment. */
static void
aud_close_seg(aud_t *a)
{
    if (!a->open)
        return;
    if (a->wav) {
        wav_header(a->wav, a->rate, a->frames);
        fclose(a->wav);
        a->wav = NULL;
    }
    a->open    = 0;
    a->seg_end = a->seg_origin + a->frames;
    if (a->nsegs < AUD_MAX_SEGS) {
        memcpy(a->segs[a->nsegs].file, a->file, sizeof(a->file));
        a->segs[a->nsegs].rate     = a->rate;
        a->segs[a->nsegs].frames   = a->frames;
        a->segs[a->nsegs].start_ms = a->first_ms;
        a->segs[a->nsegs].exact    = a->exact;
        a->nsegs++;
    }
    a->frames = 0;
}

void
automation_audio(int src, const void *buf, int size, int is_float)
{
    aud_t *a;
    int    n = size / 2;
    int    rate;
    int    first = -1;
    int    last  = -1;

    if (!enabled || (src < 0) || (src >= I_MAX) || (n <= 0) || !aud_mutex)
        return;
    rate = sound_stream_rate(src);
    if (rate <= 0)
        return;

    thread_wait_mutex(aud_mutex);
    a = &aud[src];
    if (aud_scratch_len < size) {
        aud_scratch     = realloc(aud_scratch, size * sizeof(int16_t));
        aud_scratch_len = size;
    }
    if (is_float) {
        const float *f = (const float *) buf;
        for (int i = 0; i < size; i++) {
            float v = f[i] * 32768.0f;
            aud_scratch[i] = (v >= 32767.0f) ? 32767 : ((v <= -32768.0f) ? -32768 : (int16_t) v);
        }
    } else
        memcpy(aud_scratch, buf, size * sizeof(int16_t));

    if (!a->ring || (a->rate != rate)) {
        if (a->open) {
            aud_close_seg(a);
            a->skip = 0;
        }
        free(a->ring);
        a->rate        = rate;
        a->ring_frames = (uint32_t) rate * AUD_RING_SECONDS;
        a->ring        = calloc(a->ring_frames, 2 * sizeof(int16_t));
        a->ring_pos    = 0;
        a->ring_base   = a->total;
    }
    for (int i = 0; i < n; i++) {
        a->ring[a->ring_pos * 2]     = aud_scratch[i * 2];
        a->ring[a->ring_pos * 2 + 1] = aud_scratch[i * 2 + 1];
        if (++a->ring_pos == a->ring_frames)
            a->ring_pos = 0;
    }
    a->total += n;
    a->last_ms = emu_ms;

    if (a->rec && aud_recording) {
        int from = (a->skip >= (uint32_t) n) ? n : (int) a->skip;

        a->skip -= from;
        if (!a->open && (from < n) && (a->nsegs < AUD_MAX_SEGS)) {
            a->open       = 1;
            a->seg_no     = a->nsegs;
            a->seg_origin = a->total - n + from;
            a->file[0]    = '\0';
            if (aud_dir[0]) {
                char fn[1100];

                if (a->nsegs)
                    snprintf(a->file, sizeof(a->file), "%s-%d.wav", aud_names[src], a->nsegs + 1);
                else
                    snprintf(a->file, sizeof(a->file), "%s.wav", aud_names[src]);
                snprintf(fn, sizeof(fn), "%s/%s", aud_dir, a->file);
                a->wav = plat_fopen(fn, "wb");
                if (a->wav)
                    wav_header(a->wav, rate, 0);
            }
            /* The polled streams' first segment starts exactly at the start;
               otherwise a block covers the time just before it arrived. */
            a->exact    = aud_exact(src) && !a->nsegs;
            a->first_ms = a->exact ? (double) aud_start_ms : (double) emu_ms - (n * 1000.0 / rate);
        }
        if (a->open && (from < n)) {
            if (a->wav)
                fwrite(aud_scratch + from * 2, 2 * sizeof(int16_t), n - from, a->wav);
            a->frames += n - from;
        }
    }

    if (aud_wait) {
        for (int i = 0; i < size; i++) {
            if (abs(aud_scratch[i]) >= aud_wait_level) {
                if (first < 0)
                    first = i / 2;
                last = i / 2;
            }
        }
        /* The block ends at about emu_ms: date frames back from there. */
        if ((aud_wait == CMD_WAIT_SOUND) && (first >= 0) && (aud_hit < 0)) {
            aud_hit      = src;
            aud_onset_ms = (double) emu_ms - ((n - first) * 1000.0 / rate);
        }
        if ((aud_wait == CMD_WAIT_SILENCE) && (last >= 0))
            a->loud_ms = emu_ms - (uint64_t) (((n - 1 - last) * 1000LL) / rate);
    }
    thread_release_mutex(aud_mutex);
}

static double
aud_db(double v)
{
    return (v > 0.0) ? 20.0 * log10(v / 32768.0) : -120.0;
}

static int
aud_level_from_db(double db)
{
    double v = 32768.0 * pow(10.0, db / 20.0);

    return (v < 1.0) ? 1 : ((v > 32767.0) ? 32767 : (int) v);
}

/* Stop recording: finish the WAV headers and describe the segments. */
static void
aud_stop(strbuf_t *sb)
{
    int first = 1;

    thread_wait_mutex(aud_mutex);
    if (sb)
        sb_printf(sb, "{\"ok\":true,\"start_ms\":%llu,\"ms\":%llu,\"streams\":[",
                  (unsigned long long) aud_start_ms, (unsigned long long) emu_ms);
    for (int i = 0; i < I_MAX; i++) {
        aud_t *a = &aud[i];

        aud_close_seg(a);
        for (int k = 0; sb && (k < a->nsegs); k++) {
            sb_printf(sb, "%s{\"name\":\"%s\",\"segment\":%d,\"rate\":%d,\"frames\":%llu,\"start_ms\":%.3f,\"exact\":%s",
                      first ? "" : ",", aud_names[i], k, a->segs[k].rate,
                      (unsigned long long) a->segs[k].frames, a->segs[k].start_ms, a->segs[k].exact ? "true" : "false");
            if (a->segs[k].file[0])
                sb_printf(sb, ",\"file\":\"%s\"", a->segs[k].file);
            if (a->segs[k].exact && (a->nsegs == 1)) {
                /* Frames written plus frames ticked but not yet handed over,
                   against the emulated time elapsed: 0 means no gap. */
                double want = (double) (emu_ms - aud_start_ms) * a->segs[k].rate / 1000.0;
                sb_printf(sb, ",\"drift_frames\":%.1f",
                          (double) (a->segs[k].frames + sound_pending_frames(i)) - want);
            }
            sb_printf(sb, "}");
            first = 0;
        }
        a->nsegs = 0;
        a->rec   = 0;
    }
    aud_recording = 0;
    thread_release_mutex(aud_mutex);
    if (sb)
        sb_printf(sb, "]}");
}

/* audio start <dir> | audio stop | audio level <ms> | audio read <stream> <ms> */
static void
handle_audio(int argc, char **argv, char **rest)
{
    if ((argc >= 2) && !strcmp(argv[1], "start")) {
        if (aud_recording) {
            respond_error("already recording audio");
            return;
        }
        /* With a directory, each stream is also written there as WAV files;
           without one, it is kept for audio pull only (a client elsewhere). */
        snprintf(aud_dir, sizeof(aud_dir), "%s", (argc >= 3) ? rest[2] : "");
        thread_wait_mutex(aud_mutex);
        for (int i = 0; i < I_MAX; i++) {
            aud[i].rec    = 1;
            aud[i].frames = 0;
            aud[i].nsegs  = 0;
            aud[i].skip   = (uint32_t) sound_pending_frames(i);
        }
        aud_start_ms  = emu_ms;
        aud_recording = 1;
        thread_release_mutex(aud_mutex);
        resp.len = 0;
        sb_printf(&resp, "{\"ok\":true,\"start_ms\":%llu}", (unsigned long long) emu_ms);
    } else if ((argc == 2) && !strcmp(argv[1], "stop")) {
        if (!aud_recording) {
            respond_error("not recording audio");
            return;
        }
        resp.len = 0;
        aud_stop(&resp);
    } else if ((argc == 3) && !strcmp(argv[1], "level")) {
        uint64_t ms    = strtoull(argv[2], NULL, 0);
        int      first = 1;

        resp.len = 0;
        sb_printf(&resp, "{\"ok\":true,\"ms\":%llu,\"streams\":{", (unsigned long long) emu_ms);
        thread_wait_mutex(aud_mutex);
        for (int i = 0; i < I_MAX; i++) {
            aud_t   *a = &aud[i];
            uint64_t n;
            double   peak = 0.0;
            double   sum  = 0.0;

            if (!a->ring || !a->total)
                continue;
            n = (ms * a->rate) / 1000;
            if (n > a->total)
                n = a->total;
            if (n > a->ring_frames)
                n = a->ring_frames;
            for (uint64_t k = 0; k < n; k++) {
                uint32_t f = (a->ring_pos + a->ring_frames - 1 - (uint32_t) k) % a->ring_frames;
                for (int c = 0; c < 2; c++) {
                    double v = abs(a->ring[f * 2 + c]);
                    if (v > peak)
                        peak = v;
                    sum += v * v;
                }
            }
            sb_printf(&resp, "%s\"%s\":{\"peak_db\":%.1f,\"rms_db\":%.1f,\"frames\":%llu,\"rate\":%d,\"last_ms\":%llu}",
                      first ? "" : ",", aud_names[i], aud_db(peak), aud_db(n ? sqrt(sum / (2.0 * n)) : 0.0),
                      (unsigned long long) n, a->rate, (unsigned long long) a->last_ms);
            first = 0;
        }
        thread_release_mutex(aud_mutex);
        sb_printf(&resp, "}}");
    } else if ((argc == 4) && !strcmp(argv[1], "read")) {
        int      src = -1;
        uint64_t ms  = strtoull(argv[3], NULL, 0);

        for (int i = 0; i < I_MAX; i++) {
            if (!strcmp(argv[2], aud_names[i]))
                src = i;
        }
        if (src < 0) {
            respond_error("unknown stream");
            return;
        }
        thread_wait_mutex(aud_mutex);
        aud_t *a = &aud[src];
        if (!a->ring || !a->total) {
            thread_release_mutex(aud_mutex);
            respond_error("no audio from that stream yet");
            return;
        }
        uint64_t n = (ms * a->rate) / 1000;
        if (n > a->total)
            n = a->total;
        if (n > a->ring_frames)
            n = a->ring_frames;
        int16_t *out = malloc(n * 2 * sizeof(int16_t) + 4);
        for (uint64_t k = 0; k < n; k++) {
            uint32_t f = (a->ring_pos + a->ring_frames - (uint32_t) n + (uint32_t) k) % a->ring_frames;
            out[k * 2]     = a->ring[f * 2];
            out[k * 2 + 1] = a->ring[f * 2 + 1];
        }
        resp.len = 0;
        sb_printf(&resp, "{\"ok\":true,\"ms\":%llu,\"stream\":\"%s\",\"rate\":%d,\"frames\":%llu,\"end_ms\":%llu,\"data\":\"",
                  (unsigned long long) emu_ms, aud_names[src], a->rate, (unsigned long long) n,
                  (unsigned long long) a->last_ms);
        thread_release_mutex(aud_mutex);
        sb_base64(&resp, (const uint8_t *) out, n * 2 * sizeof(int16_t));
        sb_printf(&resp, "\"}");
        free(out);
    } else if (((argc == 4) || (argc == 5)) && !strcmp(argv[1], "pull")) {
        /* audio pull <stream> <from> [max_frames]: the recording's samples from
           position <from> (frames since its start), up to now or to where it
           stopped. Successive pulls from each reply's next join exactly; lost
           counts frames that left the ring before they were pulled. */
        int      src  = -1;
        uint64_t from = strtoull(argv[3], NULL, 0);
        uint64_t max  = (argc == 5) ? strtoull(argv[4], NULL, 0) : (uint64_t) 1 << 20;

        for (int i = 0; i < I_MAX; i++) {
            if (!strcmp(argv[2], aud_names[i]))
                src = i;
        }
        if (src < 0) {
            respond_error("unknown stream");
            return;
        }
        thread_wait_mutex(aud_mutex);
        aud_t *a = &aud[src];
        if (!a->ring || (!a->open && (a->seg_end == a->seg_origin))) {
            thread_release_mutex(aud_mutex);
            respond_error("nothing recorded from that stream");
            return;
        }
        uint64_t hi     = a->open ? a->total : a->seg_end;
        uint64_t oldest = (a->total > a->ring_frames) ? (a->total - a->ring_frames) : 0;
        uint64_t lo     = a->seg_origin;
        uint64_t at     = a->seg_origin + from;
        uint64_t lost   = 0;

        if (oldest > lo)
            lo = oldest;
        if (a->ring_base > lo)
            lo = a->ring_base;
        if (at < lo) {
            lost = lo - at;
            at   = lo;
        }
        if (at > hi)
            at = hi;
        uint64_t n = hi - at;
        if (n > max)
            n = max;
        int16_t *out = malloc(n * 2 * sizeof(int16_t) + 4);
        for (uint64_t k = 0; k < n; k++) {
            uint32_t f     = (uint32_t) ((a->ring_pos + a->ring_frames - ((a->total - (at + k)) % a->ring_frames)) % a->ring_frames);
            out[k * 2]     = a->ring[f * 2];
            out[k * 2 + 1] = a->ring[f * 2 + 1];
        }
        resp.len = 0;
        sb_printf(&resp, "{\"ok\":true,\"ms\":%llu,\"stream\":\"%s\",\"segment\":%d,\"rate\":%d,\"from\":%llu,"
                  "\"frames\":%llu,\"next\":%llu,\"lost\":%llu,\"recording\":%s,\"data\":\"",
                  (unsigned long long) emu_ms, aud_names[src], a->seg_no, a->rate,
                  (unsigned long long) (at - a->seg_origin), (unsigned long long) n,
                  (unsigned long long) (at - a->seg_origin + n), (unsigned long long) lost, a->open ? "true" : "false");
        thread_release_mutex(aud_mutex);
        sb_base64(&resp, (const uint8_t *) out, n * 2 * sizeof(int16_t));
        sb_printf(&resp, "\"}");
        free(out);
    } else
        respond_error("usage: audio start [<dir>] | audio stop | audio level <ms> | audio read <stream> <ms> | "
                      "audio pull <stream> <from> [max_frames]");
}

/* wait sound / wait silence: arm the tap, or check it each quantum. */
static void
aud_wait_begin(int which, double db, uint64_t for_ms, uint64_t since_ms)
{
    thread_wait_mutex(aud_mutex);
    aud_wait       = which;
    aud_wait_level = aud_level_from_db(db);
    aud_wait_for   = for_ms;
    aud_hit        = -1;
    for (int i = 0; i < I_MAX; i++)
        aud[i].loud_ms = since_ms;
    thread_release_mutex(aud_mutex);
}

static int
aud_wait_done(void)
{
    int done = 0;

    thread_wait_mutex(aud_mutex);
    if (aud_wait == CMD_WAIT_SOUND) {
        done = (aud_hit >= 0);
        if (done)
            snprintf(timed_extra, sizeof(timed_extra), ",\"stream\":\"%s\",\"onset_ms\":%.1f",
                     aud_names[aud_hit], aud_onset_ms);
    } else {
        uint64_t loud = 0;
        for (int i = 0; i < I_MAX; i++) {
            if (aud[i].loud_ms > loud)
                loud = aud[i].loud_ms;
        }
        done = (emu_ms >= loud + aud_wait_for);
        /* Reported on a timeout too, so a client can go on waiting from there. */
        snprintf(timed_extra, sizeof(timed_extra), ",\"quiet_since_ms\":%llu", (unsigned long long) loud);
    }
    thread_release_mutex(aud_mutex);
    return done;
}

static void
aud_wait_end(void)
{
    thread_wait_mutex(aud_mutex);
    aud_wait = 0;
    thread_release_mutex(aud_mutex);
}

/* ---- Screenshots ---- */

static int write_png(const char *fn);

static int encode_png(strbuf_t *out);
static strbuf_t png_out;

/* A frame's stamp: when it was captured, in the CPU's own cycles (the time
   base every timer runs on), and where each recorded sound stream was then,
   in samples since the recording started. The fields, without braces. */
static void
rec_stamp(strbuf_t *sb)
{
    uint64_t ran   = (uint64_t) tsc - rec_tsc0;
    int      first = 1;

    sb_printf(sb, "\"ms\":%llu,\"cycles\":%llu,\"us\":%.3f,\"frame\":%llu,\"audio\":{",
              (unsigned long long) emu_ms, (unsigned long long) ran,
              cpuclock ? (double) ran * 1000000.0 / cpuclock : 0.0, (unsigned long long) frame_count);
    thread_wait_mutex(aud_mutex);
    for (int i = 0; aud_recording && (i < I_MAX); i++) {
        const aud_t *a = &aud[i];

        if (!a->rec || !aud_exact(i) || a->nsegs)
            continue;
        sb_printf(sb, "%s\"%s\":%lld", first ? "" : ",", aud_names[i],
                  (long long) a->frames + sound_pending_frames(i) - (long long) a->skip);
        first = 0;
    }
    thread_release_mutex(aud_mutex);
    sb_printf(sb, "}");
}

static void
rec_q_drop_oldest(void)
{
    rec_item_t *it = &rec_q[rec_q_head];

    rec_q_bytes -= it->len;
    free(it->stamp);
    free(it->data);
    rec_q_head = (rec_q_head + 1) % rec_q_cap;
    rec_q_len--;
}

static void
rec_q_clear(void)
{
    while (rec_q_len)
        rec_q_drop_oldest();
    rec_q_head   = 0;
    rec_lost     = 0;
    rec_need_key = 1;
}

static void
rec_q_push(rec_item_t item)
{
    if (rec_q_len == rec_q_cap) {
        size_t      cap = rec_q_cap ? rec_q_cap * 2 : 256;
        rec_item_t *q   = calloc(cap, sizeof(rec_item_t));

        for (size_t i = 0; i < rec_q_len; i++)
            q[i] = rec_q[(rec_q_head + i) % rec_q_cap];
        free(rec_q);
        rec_q      = q;
        rec_q_cap  = cap;
        rec_q_head = 0;
    }
    rec_q[(rec_q_head + rec_q_len) % rec_q_cap] = item;
    rec_q_len++;
    rec_q_bytes += item.len;
}

/* Queue the current frame (frame_mutex held). */
static void
rec_q_frame(void)
{
    const size_t n     = (size_t) frame_w * frame_h * 3;
    uint8_t     *rgb   = malloc(n);
    uint8_t     *x     = malloc(n);
    size_t       bound = ZSTD_compressBound(n);
    rec_item_t   item  = { 0 };
    strbuf_t     stamp = { 0 };

    if (rec_q_bytes > REC_QUEUE_MAX) {
        /* The client is behind: skip this frame, start again with a key frame. */
        rec_lost++;
        rec_need_key = 1;
        free(rgb);
        free(x);
        return;
    }
    for (int y = 0; y < frame_h; y++) {
        const uint32_t *src = &frame_buf[y * frame_w];
        uint8_t        *row = &rgb[(size_t) y * frame_w * 3];

        for (int i = 0; i < frame_w; i++) {
            row[i * 3]     = (src[i] >> 16) & 0xff;
            row[i * 3 + 1] = (src[i] >> 8) & 0xff;
            row[i * 3 + 2] = src[i] & 0xff;
        }
    }
    item.key = rec_need_key || !rec_prev || (rec_prev_w != frame_w) || (rec_prev_h != frame_h);
    for (size_t i = 0; i < n; i++)
        x[i] = item.key ? rgb[i] : (rgb[i] ^ rec_prev[i]);
    item.data = malloc(bound);
    item.len  = ZSTD_compress(item.data, bound, x, n, 1);
    free(x);
    if (ZSTD_isError(item.len)) {
        free(item.data);
        free(rgb);
        rec_lost++;
        rec_need_key = 1;
        return;
    }
    item.w = frame_w;
    item.h = frame_h;
    rec_stamp(&stamp);
    item.stamp = stamp.buf;
    rec_q_push(item);
    free(rec_prev);
    rec_prev     = rgb;
    rec_prev_w   = frame_w;
    rec_prev_h   = frame_h;
    rec_need_key = 0;
    rec_frames++;
}

/* Called with frame_mutex held, right after a new frame was captured. */
static void
record_frame(void)
{
    strbuf_t stamp = { 0 };

    if (!rec_interval || (emu_ms < rec_next_ms))
        return;
    rec_next_ms = emu_ms + rec_interval;
    if (!rec_dir[0]) {
        /* In memory, for record pull. */
        if (frame_buf && frame_w && frame_h)
            rec_q_frame();
        return;
    }
    {
        char fn[1100];

        snprintf(fn, sizeof(fn), "%s/%010llu.png", rec_dir, (unsigned long long) emu_ms);
        if (!write_png(fn))
            return;
    }
    rec_frames++;
    if (rec_log) {
        rec_stamp(&stamp);
        fprintf(rec_log, "{%.*s}\n", (int) stamp.len, stamp.buf);
        free(stamp.buf);
    }
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

    /* A half programmed CRTC can describe a frame that runs off the bitmap. */
    if ((x < 0) || (y < 0) || (x >= target->w) || (y >= target->h))
        return;
    if ((x + w) > target->w)
        w = target->w - x;
    if ((y + h) > target->h)
        h = target->h - y;

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
end_timed_stopped(int matched, const char *stopped)
{
    int which = cmd;

    cmd      = CMD_NONE;
    held     = cmd_was_held;
    resp.len = 0;
    sb_printf(&resp, "{\"ok\":true,\"ms\":%llu,\"frames\":%llu,\"held\":%s",
              (unsigned long long) emu_ms, (unsigned long long) frame_count, held ? "true" : "false");
    if (matched >= 0)
        sb_printf(&resp, ",\"matched\":%s", matched ? "true" : "false");
    if (stopped)
        sb_printf(&resp, ",\"stopped\":\"%s\"", stopped);
    if ((which == CMD_WAIT_SERIAL) && serial_state(wait_port)) {
        if (matched > 0)
            sb_printf(&resp, ",\"at\":%llu", (unsigned long long) wait_found);
        sb_printf(&resp, ",\"next\":%llu", (unsigned long long) serial_state(wait_port)->out_pos);
    }
    sb_printf(&resp, "%s}", timed_extra);
    timed_extra[0] = '\0';
    if (aud_wait)
        aud_wait_end();
    complete();
}

static void
end_timed(int matched)
{
    end_timed_stopped(matched, NULL);
}

/* Returns 1 when the condition of a wait command is satisfied. */
static int
wait_satisfied(void)
{
    if (cmd == CMD_WAIT_TEXT)
        return text_contains(wait_text);

    if (cmd == CMD_WAIT_SERIAL)
        return serial_contains();

    if (cmd == CMD_WAIT_MEM) {
        for (int i = 0; i < wait_len; i++) {
            if (peek_phys(wait_addr + i) != wait_bytes[i])
                return 0;
        }
        return 1;
    }

    return 0;
}

/* A segment register or descriptor table register: the selector (when it
   has one) and what the CPU holds of its descriptor. "ar" is the access
   byte and, above it, the flags byte (G, D/B, AVL and limit 19:16), as
   bytes 5 and 6 of the descriptor. */
static void
sb_seg(strbuf_t *sb, const char *name, const x86seg *s, int sel, int ar)
{
    if (sel)
        sb_printf(sb, ",\"%s\":%u", name, s->seg);
    sb_printf(sb, ",\"%s_base\":%u,\"%s_limit\":%u", name, s->base, name, s->limit);
    if (ar)
        sb_printf(sb, ",\"%s_ar\":%u", name, s->access | (s->ar_high << 8));
}

/* Physical x87 register r as the 80-bit value FSAVE would store: sign and
   exponent, then the 64-bit significand. */
static void
fpu_reg80(int r, uint16_t *se, uint64_t *signif)
{
    if (fpu_softfloat) {
        *se     = fpu_state.st_space[r].signExp;
        *signif = fpu_state.st_space[r].signif;
    } else if (cpu_state.ismmx) {
        *se     = *MMEP[r];
        *signif = MMP[r]->q;
    } else if (cpu_state.tag[r] & TAG_UINT64) {
        /* An integer FILD loaded, kept exactly beside its double. */
        int64_t  i = (int64_t) cpu_state.MM[r].q;
        uint64_t m = (i < 0) ? (0 - (uint64_t) i) : (uint64_t) i;
        int      e = 16383 + 63;

        if (m == 0)
            e = 0;
        while (m && !(m & 0x8000000000000000ULL)) {
            m <<= 1;
            e--;
        }
        *se     = ((i < 0) ? 0x8000 : 0) | e;
        *signif = m;
    } else {
        x87_conv_t c;

        x87_to80(cpu_state.ST[r], &c);
        *se     = (uint16_t) c.begin;
        *signif = c.eind.ll;
    }
}

/* The log's last LOG_RING bytes, for a client that can't read the log file
   (log). 86Box logs from any thread, and before automation_init(). */
static char        log_ring[LOG_RING];
static uint64_t    log_pos; /* bytes logged since startup */
static atomic_flag log_lock = ATOMIC_FLAG_INIT;

static void
log_ring_add(const char *text, size_t len)
{
    for (size_t i = 0; i < len; i++)
        log_ring[(log_pos + i) % LOG_RING] = text[i];
    log_pos += len;
}

void
automation_log_text(const char *text, int newline)
{
    while (atomic_flag_test_and_set_explicit(&log_lock, memory_order_acquire))
        ;
    log_ring_add(text, strlen(text));
    if (newline)
        log_ring_add("\n", 1);
    atomic_flag_clear_explicit(&log_lock, memory_order_release);
}

/* log [<from> [<max>]]: what 86Box has logged from position `from` on. */
static void
cmd_log(int argc, char **argv)
{
    static char text[LOG_RING + 1];
    uint64_t    from = (argc >= 2) ? strtoull(argv[1], NULL, 0) : 0;
    uint64_t    max  = (argc >= 3) ? strtoull(argv[2], NULL, 0) : LOG_RING;
    uint64_t    end;
    uint64_t    lost = 0;
    size_t      n;

    while (atomic_flag_test_and_set_explicit(&log_lock, memory_order_acquire))
        ;
    end = log_pos;
    if ((end > LOG_RING) && (from < (end - LOG_RING))) {
        lost = end - LOG_RING - from;
        from = end - LOG_RING;
    }
    if (from > end)
        from = end;
    n = (size_t) (((end - from) < max) ? (end - from) : max);
    if (n > LOG_RING)
        n = LOG_RING;
    for (size_t i = 0; i < n; i++)
        text[i] = log_ring[(from + i) % LOG_RING];
    atomic_flag_clear_explicit(&log_lock, memory_order_release);
    text[n] = '\0';

    resp.len = 0;
    sb_printf(&resp, "{\"ok\":true,\"from\":%llu,\"next\":%llu,\"pos\":%llu,\"lost\":%llu,\"text\":",
              (unsigned long long) from, (unsigned long long) (from + n), (unsigned long long) end,
              (unsigned long long) lost);
    sb_json_string(&resp, text);
    sb_printf(&resp, "}");
}

/* Files a client sends (media put) go in MEDIA_DIR. The name only has to
   stay in it, which the OS doesn't see to; anything else it refuses (too
   long, characters it doesn't allow) comes back as its error. */
static int
media_name_ok(const char *name)
{
    return name[0] && strcmp(name, ".") && strcmp(name, "..") && !strpbrk(name, "/\\:");
}

static void
media_path(char *out, const char *name)
{
    path_append_filename(out, usr_path, MEDIA_DIR);
    if (name) {
        path_slash(out);
        strcat(out, name);
    }
}

/* media remove <name> */
static void
cmd_media_remove(const char *name)
{
    char path[1024 + MEDIA_NAME_MAX + 2];

    if (!media_name_ok(name)) {
        respond_error("bad name");
        return;
    }
    media_path(path, name);
    if (remove(path))
        respond_error(strerror(errno));
    else
        respond_ok_time();
}

static int write_all(sock_t fd, const char *buf, size_t len);

static const char *
media_why(char *why, const char *what)
{
    snprintf(why, 256, "%s: %s", what, strerror(errno));
    return why;
}

/* media put <name> <bytes>, then the bytes: runs on the listener thread,
   which takes them off the connection itself, while the machine runs on.
   `buf` holds the `have` bytes that arrived with the request line; *used
   is set to how many of them were the file's. Returns 0 if the connection
   must close: it ended early, or the size was unreadable, so the bytes
   that follow can't be told apart from the next request. */
static int
media_put(const char *args, const char *buf, size_t have, size_t *used)
{
    static char        chunk[65536];
    char               name[MEDIA_NAME_MAX + 1];
    char               path[1024 + MEDIA_NAME_MAX + 2];
    char               part[sizeof(path) + 5];
    const char        *sp = strchr(args, ' ');
    const char        *err = NULL;
    char              *end;
    unsigned long long size;
    unsigned long long left;
    size_t             n;
    FILE              *fp = NULL;
    strbuf_t           sb  = { 0 };
    int                ret = 1;
    char               why[256]; /* err, with the system's reason */

    *used = 0;
    if (!sp || ((size_t) (sp - args) > MEDIA_NAME_MAX) || (sp[1] < '0') || (sp[1] > '9')) {
        static const char usage[] = "{\"ok\":false,\"error\":\"usage: media put <name> <bytes>, then the bytes\"}\n";
        write_all(client_fd, usage, strlen(usage));
        return 0;
    }
    size = strtoull(sp + 1, &end, 0);
    if (*end) {
        static const char usage[] = "{\"ok\":false,\"error\":\"usage: media put <name> <bytes>, then the bytes\"}\n";
        write_all(client_fd, usage, strlen(usage));
        return 0;
    }
    memcpy(name, args, sp - args);
    name[sp - args] = '\0';

    if (!media_name_ok(name))
        err = "bad name: it must not hold '/', '\\' or ':', nor be '.' or '..'";
    else {
        media_path(path, NULL);
        if (!plat_dir_check(path) && plat_dir_create(path))
            err = "cannot create the " MEDIA_DIR " directory";
        else {
            media_path(path, name);
            snprintf(part, sizeof(part), "%s.part", path);
            if (!(fp = plat_fopen(part, "wb")))
                err = media_why(why, "cannot create the file");
        }
    }

    /* Take all the bytes, even when they can't be kept, so the next request
       is read from the right place. */
    left = size;
    n    = (have < left) ? have : (size_t) left;
    if (fp && n && (fwrite(buf, 1, n, fp) != n))
        err = media_why(why, "cannot write the file");
    *used = n;
    left -= n;
    while (left) {
        int r = recv(client_fd, chunk, (int) ((left < sizeof(chunk)) ? left : sizeof(chunk)), 0);
        if (r <= 0) {
#ifndef _WIN32
            if ((r < 0) && (errno == EINTR))
                continue;
#endif
            ret = 0;
            err = "the connection closed";
            break;
        }
        if (fp && !err && (fwrite(chunk, 1, r, fp) != (size_t) r))
            err = media_why(why, "cannot write the file");
        left -= r;
    }

    if (fp) {
        if (fclose(fp) && !err)
            err = media_why(why, "cannot write the file");
        if (err)
            remove(part);
        else {
            /* Replace the old file only when the new one is complete. POSIX
               rename() replaces it in one step; Windows' won't replace. */
#ifdef _WIN32
            remove(path);
#endif
            if (rename(part, path)) {
                err = media_why(why, "cannot replace the file");
                remove(part);
            }
        }
    }
    if (!ret)
        return 0;

    if (err) {
        sb_printf(&sb, "{\"ok\":false,\"error\":");
        sb_json_string(&sb, err);
    } else {
        automation_log("Automation: received %llu bytes as %s\n", size, path);
        sb_printf(&sb, "{\"ok\":true,\"bytes\":%llu,\"path\":", size);
        sb_json_string(&sb, path);
    }
    sb_printf(&sb, "}\n");
    ret = write_all(client_fd, sb.buf, sb.len);
    free(sb.buf);
    return ret;
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
        int mode = peek_phys(0x449);
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
    } else if (!strcmp(argv[0], "wait") && (argc == 4) && !strcmp(argv[1], "sound")) {
        /* wait sound <timeout_ms> <dBFS> */
        aud_wait_begin(CMD_WAIT_SOUND, strtod(argv[3], NULL), 0, emu_ms);
        begin_timed(CMD_WAIT_SOUND, strtoull(argv[2], NULL, 0));
        return;
    } else if (!strcmp(argv[0], "wait") && ((argc == 5) || (argc == 6)) && !strcmp(argv[1], "silence")) {
        /* wait silence <timeout_ms> <dBFS> <for_ms> [quiet_since_ms] */
        aud_wait_begin(CMD_WAIT_SILENCE, strtod(argv[3], NULL), strtoull(argv[4], NULL, 0),
                       (argc == 6) ? strtoull(argv[5], NULL, 0) : emu_ms);
        begin_timed(CMD_WAIT_SILENCE, strtoull(argv[2], NULL, 0));
        return;
    } else if (!strcmp(argv[0], "audio") && (argc >= 2)) {
        handle_audio(argc, argv, rest);
    } else if (!strcmp(argv[0], "wait") && (argc >= 6) && !strcmp(argv[1], "serial")) {
        /* wait serial <timeout_ms> <port> <from> <text...>: run until <text>
           is in what the guest sent on the port, from position <from> on. */
        serial_port_state_t *s = serial_arg(argv[3], &wait_port);
        if (s) {
            strncpy(wait_text, rest[5], sizeof(wait_text) - 1);
            wait_scan = strtoull(argv[4], NULL, 0);
            begin_timed(CMD_WAIT_SERIAL, strtoull(argv[2], NULL, 0));
            if (wait_satisfied())
                end_timed(1);
            return;
        }
    } else if (!strcmp(argv[0], "serial") && (argc >= 3) && (argc <= 5) && !strcmp(argv[1], "read")) {
        cmd_serial_read(argc, argv);
    } else if (!strcmp(argv[0], "serial") && (argc == 4) && !strcmp(argv[1], "write")) {
        cmd_serial_write(argv);
    } else if (!strcmp(argv[0], "serial") && (argc == 7) && !strcmp(argv[1], "lines")) {
        cmd_serial_lines(argv);
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
        /* With a tablet configured as well, input goes to one device at a
           time: this one is for the mouse. */
        if (mouse_both_enabled())
            mouse_input_mode = 0;
        mouse_scale(atoi(argv[1]), atoi(argv[2]));
        mouse_set_buttons_ex(atoi(argv[3]));
        respond_ok_time();
    } else if (!strcmp(argv[0], "tablet") && ((argc == 4) || ((argc == 2) && !strcmp(argv[1], "out")))) {
        /* tablet <x> <y> <buttons>: the pen (or finger) at pixel x,y of the
           last frame, as screenshots have it; tablet out: lifted away. For
           the configured tablet, touch screen or light pen; a mouse, if one
           is configured too, keeps getting the mouse command. */
        if (!tablet_type)
            respond_error("no tablet configured");
        else if (argc == 2) {
            mouse_set_buttons_ex(0);
            mouse_tablet_in_proximity = 0;
            respond_ok_time();
        } else if (!frame_w || !frame_h)
            respond_error("no frame rendered yet");
        else {
            double x = (atof(argv[1]) + 0.5) / frame_w;
            double y = (atof(argv[2]) + 0.5) / frame_h;

            mouse_automation = 1;
            if (mouse_input_mode == 0)
                mouse_input_mode = 1;
            mouse_x_abs               = (x < 0.0) ? 0.0 : ((x > 1.0) ? 1.0 : x);
            mouse_y_abs               = (y < 0.0) ? 0.0 : ((y > 1.0) ? 1.0 : y);
            mouse_tablet_in_proximity = 1; /* over monitor 0 */
            mouse_set_buttons_ex(atoi(argv[3]));
            respond_ok_time();
        }
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
        /* record start <interval_ms> [<dir>] | record stop | record pull [<max_frames>] */
        if (!strcmp(argv[1], "stop")) {
            thread_wait_mutex(frame_mutex);
            rec_interval = 0;
            if (rec_log) {
                fclose(rec_log);
                rec_log = NULL;
            }
            resp.len     = 0;
            sb_printf(&resp, "{\"ok\":true,\"frames\":%u}", rec_frames);
            thread_release_mutex(frame_mutex);
        } else if (!strcmp(argv[1], "start") && (argc >= 3) && (atoi(argv[2]) > 0)) {
            /* With a directory, frames are written there; without one, they
               wait in memory for record pull. */
            thread_wait_mutex(frame_mutex);
            snprintf(rec_dir, sizeof(rec_dir), "%s", (argc >= 4) ? rest[3] : "");
            rec_interval = atoi(argv[2]);
            rec_next_ms  = emu_ms;
            rec_frames   = 0;
            rec_tsc0     = (uint64_t) tsc;
            rec_q_clear();
            if (rec_log) {
                fclose(rec_log);
                rec_log = NULL;
            }
            if (rec_dir[0]) {
                char fn[1100];

                snprintf(fn, sizeof(fn), "%s/frames.jsonl", rec_dir);
                rec_log = plat_fopen(fn, "w");
            }
            thread_release_mutex(frame_mutex);
            respond_ok_time();
        } else if (!strcmp(argv[1], "pull") && (argc <= 3)) {
            /* The frames recorded in memory since the last pull, oldest first,
               each with its stamps, size, key flag and zstd data (RGB24, XORed
               with the frame before unless key); lost counts frames skipped
               because the queue was full. Works after record stop. */
            size_t n = (argc == 3) ? strtoul(argv[2], NULL, 0) : 32;

            thread_wait_mutex(frame_mutex);
            if (n > rec_q_len)
                n = rec_q_len;
            resp.len = 0;
            sb_printf(&resp, "{\"ok\":true,\"ms\":%llu,\"recording\":%s,\"lost\":%llu,\"pending\":%llu,\"frames\":[",
                      (unsigned long long) emu_ms, rec_interval ? "true" : "false", (unsigned long long) rec_lost,
                      (unsigned long long) (rec_q_len - n));
            for (size_t i = 0; i < n; i++) {
                rec_item_t *it = &rec_q[rec_q_head];

                sb_printf(&resp, "%s{%s,\"w\":%d,\"h\":%d,\"key\":%s,\"zstd\":\"", i ? "," : "", it->stamp, it->w,
                          it->h, it->key ? "true" : "false");
                sb_base64(&resp, it->data, it->len);
                sb_printf(&resp, "\"}");
                rec_q_drop_oldest();
            }
            sb_printf(&resp, "]}");
            thread_release_mutex(frame_mutex);
        } else
            respond_error("usage: record start <interval_ms> [<dir>] | record stop | record pull [<max_frames>]");
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
                buf[i] = peek_phys(addr + i);
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
                poke_phys(addr + i, buf[i]);
            respond_ok_time();
        }
        free(buf);
    } else if (!strcmp(argv[0], "regs")) {
        static const char *names[8] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
        resp.len                    = 0;
        sb_printf(&resp, "{\"ok\":true");
        for (int i = 0; i < 8; i++)
            sb_printf(&resp, ",\"%s\":%u", names[i], cpu_state.regs[i].l);
        sb_printf(&resp, ",\"eip\":%u,\"eflags\":%u,\"cs\":%u,\"ds\":%u,\"es\":%u,\"ss\":%u,\"fs\":%u,\"gs\":%u,\"cs_base\":%u,\"cr0\":%u",
                  cpu_state.pc, ((uint32_t) cpu_state.eflags << 16) | cpu_state.flags,
                  cpu_state.seg_cs.seg, cpu_state.seg_ds.seg, cpu_state.seg_es.seg,
                  cpu_state.seg_ss.seg, cpu_state.seg_fs.seg, cpu_state.seg_gs.seg,
                  cpu_state.seg_cs.base, cr0);
        if (is286) {
            /* The descriptor caches: what the CPU uses, which may no longer
               match the descriptor tables (unreal mode, say). */
            sb_printf(&resp, ",\"cs_limit\":%u,\"cs_ar\":%u", cpu_state.seg_cs.limit,
                      cpu_state.seg_cs.access | (cpu_state.seg_cs.ar_high << 8));
            sb_seg(&resp, "ds", &cpu_state.seg_ds, 0, 1);
            sb_seg(&resp, "es", &cpu_state.seg_es, 0, 1);
            sb_seg(&resp, "ss", &cpu_state.seg_ss, 0, 1);
            if (is386) {
                sb_seg(&resp, "fs", &cpu_state.seg_fs, 0, 1);
                sb_seg(&resp, "gs", &cpu_state.seg_gs, 0, 1);
            }
            sb_seg(&resp, "gdtr", &gdt, 0, 0);
            sb_seg(&resp, "idtr", &idt, 0, 0);
            sb_seg(&resp, "ldtr", &ldt, 1, 1);
            sb_seg(&resp, "tr", &tr, 1, 1);
            sb_printf(&resp, ",\"cpl\":%u", (cr0 & 1) ? ((cpu_state.eflags & VM_FLAG) ? 3 : CPL) : 0);
        }
        if (is386) {
            sb_printf(&resp, ",\"cr2\":%u,\"cr3\":%u", cr2, cr3);
            for (int i = 0; i < 8; i++) {
                if ((i != 4) && (i != 5))
                    sb_printf(&resp, ",\"dr%d\":%u", i, dr[i]);
            }
            sb_printf(&resp, ",\"smm\":%d", !!in_smm);
        }
        if (cpu_has_feature(CPU_FEATURE_CR4))
            sb_printf(&resp, ",\"cr4\":%u", cr4);
        if (cpu_has_feature(CPU_FEATURE_RDTSC))
            sb_printf(&resp, ",\"tsc\":%llu", (unsigned long long) tsc);
        sb_printf(&resp, "}");
    } else if (!strcmp(argv[0], "fpu")) {
        if (!hasfpu) {
            respond_error("no FPU");
        } else {
            uint16_t cw, sw, tw;
            int      top;

            if (fpu_softfloat) {
                cw  = i387_get_control_word();
                sw  = i387_get_status_word();
                tw  = fpu_state.tag;
                top = fpu_state.tos & 7;
            } else {
                cw  = cpu_state.npxc;
                sw  = (cpu_state.npxs & 0xc7ff) | ((cpu_state.TOP & 7) << 11);
                tw  = x87_gettag();
                top = cpu_state.TOP & 7;
            }
            resp.len = 0;
            sb_printf(&resp, "{\"ok\":true,\"fcw\":%u,\"fsw\":%u,\"ftw\":%u,\"top\":%d", cw, sw, tw, top);
            for (int i = 0; i < 8; i++) {
                uint16_t se;
                uint64_t signif;

                fpu_reg80((top + i) & 7, &se, &signif);
                sb_printf(&resp, ",\"st%d\":\"%04x%016llx\"", i, se, (unsigned long long) signif);
            }
            if (cpu_has_feature(CPU_FEATURE_MMX)) {
                for (int i = 0; i < 8; i++) {
                    uint16_t se;
                    uint64_t signif;

                    fpu_reg80(i, &se, &signif);
                    sb_printf(&resp, ",\"mm%d\":\"%016llx\"", i, (unsigned long long) signif);
                }
            }
            sb_printf(&resp, "}");
        }
    } else if (!strcmp(argv[0], "log") && (argc <= 3)) {
        cmd_log(argc, argv);
    } else if (!strcmp(argv[0], "media") && (argc == 3) && !strcmp(argv[1], "remove")) {
        cmd_media_remove(argv[2]);
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
            if (cdrom[id].image_path[0])
                respond_ok_time();
            else
                respond_error("cannot load the image; the log says why");
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

#ifdef USE_GDBSTUB
    /* While the GDB stub has the CPU stopped, no emulated time passes. A timed
       command would never finish, so it ends here. */
    static uint64_t last_tsc;
    if ((gdbstub_step != GDBSTUB_EXEC) && (tsc == last_tsc)) {
        if (cmd != CMD_NONE)
            end_timed_stopped(((cmd == CMD_WAIT_TEXT) || (cmd == CMD_WAIT_MEM) || (cmd == CMD_WAIT_SOUND) ||
                               (cmd == CMD_WAIT_SILENCE) || (cmd == CMD_WAIT_SERIAL)) ? 0 : -1, "debugger");
        return;
    }
    last_tsc = tsc;
#endif

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
    } else if ((cmd == CMD_WAIT_TEXT) || (cmd == CMD_WAIT_MEM) || (cmd == CMD_WAIT_SERIAL)) {
        if (wait_satisfied())
            end_timed(1);
        else if (emu_ms >= cmd_deadline)
            end_timed(0);
    } else if ((cmd == CMD_WAIT_SOUND) || (cmd == CMD_WAIT_SILENCE)) {
        if (aud_wait_done())
            end_timed(1);
        else if (emu_ms >= cmd_deadline)
            end_timed(0);
        else
            timed_extra[0] = '\0';
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
        authed = !use_tcp || automation_insecure;

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
                } else if (automation_insecure && !strncmp(inbuf, "auth ", 5)) {
                    /* No token to check: accept the clients that send one anyway. */
                    static const char ok[] = "{\"ok\":true,\"auth\":true}\n";
                    if (!write_all(client_fd, ok, strlen(ok)))
                        goto disconnect;
                } else if (!strncmp(inbuf, "media put ", 10)) {
                    /* The file's bytes follow the line. */
                    size_t used;
                    if (!media_put(inbuf + 10, nl + 1, inlen - linelen - 1, &used))
                        goto disconnect;
                    linelen += used;
                } else {
                    if (!enabled)
                        goto disconnect;
                    strncpy(req_line, inbuf, sizeof(req_line) - 1);
                    req_line[sizeof(req_line) - 1] = '\0';
                    thread_reset_event(req_done);
                    atomic_store(&req_pending, 1);
                    thread_wait_event(req_done, -1);

                    /* Closing (on a fatal error, say) wakes us without a reply. */
                    if (!enabled || !write_all(client_fd, resp.buf, resp.len) || !write_all(client_fd, "\n", 1))
                        goto disconnect;
                }

                inlen -= linelen + 1;
                memmove(inbuf, inbuf + linelen + 1, inlen);
            }

            if (inlen >= (sizeof(inbuf) - 1)) {
                /* Overlong request: drop it. */
                inlen = 0;
            }
        }
disconnect:
        if (fatal_msg[0]) {
            /* The reply to a request still waiting, or news for an idle client. */
            strbuf_t sb = { 0 };
            sb_printf(&sb, "{\"ok\":false,\"fatal\":true,\"error\":");
            sb_json_string(&sb, fatal_msg);
            sb_printf(&sb, "}\n");
            write_all(client_fd, sb.buf, sb.len);
            free(sb.buf);
        }
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
        pclog("Automation: WARNING: listening on %s; anyone who can reach it%s "
              "fully controls this machine, including its memory\n",
              tcp_host, automation_insecure ? "" : " and has the token");

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

    if (automation_insecure) {
        pclog("Automation: WARNING: insecure mode: TCP clients need no token\n");
        return 1;
    }
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
    aud_mutex   = thread_create_mutex();
    held        = automation_start_held;
    enabled     = 1;

    listen_thread = thread_create_named(automation_thread, NULL, "automation");
    if (use_tcp && automation_insecure)
        pclog("Automation: listening on tcp:%s:%s (insecure: no token)%s\n", tcp_host, tcp_port,
              held ? " (held)" : "");
    else if (use_tcp)
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

    if (aud_recording)
        aud_stop(NULL);
    enabled = 0;
    shutdown(listen_fd, SOCK_SHUT_RW);
    sock_close(listen_fd);
    listen_fd = SOCK_INVALID;
    if (client_fd != SOCK_INVALID)
        /* After a fatal error, leave the listener the way out, to send it. */
        shutdown(client_fd, fatal_msg[0] ? SOCK_SHUT_RD : SOCK_SHUT_RW);
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

void
automation_fatal(const char *msg)
{
    if (!enabled)
        return;

    snprintf(fatal_msg, sizeof(fatal_msg), "%s", msg);
    automation_close();
}
