/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VM debug snapshot: capture reproducible emulator state for
 *          offline analysis when the VM is paused.
 *
 *          Generates under <usr_path>/debug_snapshots/snapshot_YYYYMMDD_HHMMSS/:
 *            manifest.txt   – metadata
 *            cpu.txt        – registers, segments, hexdump around CS:IP
 *            cpu_code.bin   – raw bytes around CS:IP
 *            ram640.bin     – conventional memory 0x00000–0x9FFFF
 *            io_trace.txt   – last VM_DEBUG_IO_TRACE_LEN port accesses
 *            devices/       – optional per-device files (provider API)
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#    include <fcntl.h>
#    include <sys/stat.h>
#    include <sys/types.h>
#    include <unistd.h>
#endif

#include <86box/86box.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/mem.h>
#include <86box/machine.h>
#include <86box/device.h>
#include <86box/debug_snapshot.h>

/* cpu.h lives in src/cpu/ which is added to the include path by CMake */
#include "cpu.h"

/* ------------------------------------------------------------------ */
/* IO trace ring buffer (written by io.c via io_trace_record)          */
/* ------------------------------------------------------------------ */

io_trace_entry_t      io_trace_buf[VM_DEBUG_IO_TRACE_LEN];
volatile unsigned int io_trace_head = 0;
volatile unsigned int io_trace_seq  = 0;

void
io_trace_record(uint8_t dir, uint16_t port, uint8_t width, uint32_t value)
{
    unsigned int      idx = io_trace_head & (VM_DEBUG_IO_TRACE_LEN - 1);
    io_trace_entry_t *e   = &io_trace_buf[idx];

    e->seq     = ++io_trace_seq;
    e->cs_sel  = CS;
    e->ip_off  = cpu_state.pc;
    e->phys_ip = (uint32_t) (cpu_state.seg_cs.base + cpu_state.pc);
    e->dir     = dir;
    e->port    = port;
    e->width   = width;
    e->value   = value;
    e->ax      = AX;
    e->bx      = BX;
    e->cx      = CX;
    e->dx      = DX;
    e->eflags  = cpu_state.flags;

    io_trace_head = (io_trace_head + 1) & (VM_DEBUG_IO_TRACE_LEN - 1);
}

/* ------------------------------------------------------------------ */
/* Provider registry                                                    */
/* ------------------------------------------------------------------ */

#define MAX_PROVIDERS 32

typedef struct {
    char                       name[64];
    debug_snapshot_provider_fn fn;
    void                      *priv;
    int                        enabled; /* 1 = include in snapshot (default) */
} provider_entry_t;

static provider_entry_t providers[MAX_PROVIDERS];
static int              provider_count = 0;
static int              snapshot_options[DEBUG_SNAPSHOT_OPT_COUNT] = {
    1, 1, 1, 1, 1
};

void
debug_snapshot_register_provider(const char *name, debug_snapshot_provider_fn fn,
                                  void *priv)
{
    if (provider_count >= MAX_PROVIDERS)
        return;
    snprintf(providers[provider_count].name,
             sizeof(providers[provider_count].name), "%s", name ? name : "");
    providers[provider_count].fn      = fn;
    providers[provider_count].priv    = priv;
    providers[provider_count].enabled = 1;
    provider_count++;
}

int
debug_snapshot_provider_count(void)
{
    return provider_count;
}

const char *
debug_snapshot_provider_name(int idx)
{
    if (idx < 0 || idx >= provider_count)
        return "";
    return providers[idx].name;
}

int
debug_snapshot_provider_enabled(int idx)
{
    if (idx < 0 || idx >= provider_count)
        return 0;
    return providers[idx].enabled;
}

void
debug_snapshot_provider_set_enabled(int idx, int enabled)
{
    if (idx < 0 || idx >= provider_count)
        return;
    providers[idx].enabled = enabled ? 1 : 0;
}

const char *
debug_snapshot_option_name(int idx)
{
    switch (idx) {
        case DEBUG_SNAPSHOT_OPT_CPU:
            return "CPU registers and code";
        case DEBUG_SNAPSHOT_OPT_RAM640:
            return "Conventional RAM (up to 640 KB)";
        case DEBUG_SNAPSHOT_OPT_IO_TRACE:
            return "IO port trace";
        case DEBUG_SNAPSHOT_OPT_DEVICE_LIST:
            return "Device list";
        case DEBUG_SNAPSHOT_OPT_DEVICE_DETAILS:
            return "Detailed active device dumps";
        default:
            return "";
    }
}

int
debug_snapshot_option_enabled(int idx)
{
    if (idx < 0 || idx >= DEBUG_SNAPSHOT_OPT_COUNT)
        return 0;
    return snapshot_options[idx];
}

void
debug_snapshot_option_set_enabled(int idx, int enabled)
{
    if (idx < 0 || idx >= DEBUG_SNAPSHOT_OPT_COUNT)
        return;
    snapshot_options[idx] = enabled ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static FILE *
open_in_dir(const char *dir, const char *filename)
{
    return debug_snapshot_fopen_write(dir, filename);
}

FILE *
debug_snapshot_fopen_write(const char *dir, const char *filename)
{
    char path[1024];

    if (!dir || !filename || !filename[0])
        return NULL;
    if (strchr(filename, '/') || strchr(filename, '\\'))
        return NULL;

    path_append_filename(path, dir, filename);

#ifdef _WIN32
    return plat_fopen(path, "wb");
#else
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0)
        return NULL;
    FILE *f = fdopen(fd, "wb");
    if (!f)
        close(fd);
    return f;
#endif
}

void
debug_snapshot_sanitize_component(char *dest, size_t dest_size, const char *src)
{
    size_t pos = 0;

    if (!dest || !dest_size)
        return;

    if (!src || !src[0])
        src = "unknown";

    while (*src && pos + 1 < dest_size) {
        unsigned char ch = (unsigned char) *src++;

        if ((ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '-' || ch == '.')
            dest[pos++] = (char) ch;
        else
            dest[pos++] = '_';
    }

    if (!pos) {
        snprintf(dest, dest_size, "%s", "unknown");
        return;
    }

    dest[pos] = '\0';
    if (!strcmp(dest, ".") || !strcmp(dest, ".."))
        snprintf(dest, dest_size, "%s", "unknown");
}

/* Simple hex dump: 16 bytes per row, annotated with ASCII. */
static void
hexdump(FILE *f, const uint8_t *buf, size_t len, uint32_t base_addr)
{
    for (size_t i = 0; i < len; i += 16) {
        fprintf(f, "%08X  ", (unsigned) (base_addr + i));
        for (size_t j = 0; j < 16; j++) {
            if (i + j < len)
                fprintf(f, "%02X ", buf[i + j]);
            else
                fprintf(f, "   ");
            if (j == 7)
                fputc(' ', f);
        }
        fputs(" |", f);
        for (size_t j = 0; j < 16 && i + j < len; j++) {
            uint8_t c = buf[i + j];
            fputc((c >= 0x20 && c < 0x7f) ? c : '.', f);
        }
        fputs("|\n", f);
    }
}

static void
read_phys_block(uint8_t *dest, uint32_t addr, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++)
        dest[i] = mem_readb_phys(addr + i);
}

static uint32_t
snapshot_conventional_ram_size(void)
{
    uint32_t ram_kb = mem_size;

    if (ram_kb > 640)
        ram_kb = 640;

    return ram_kb << 10;
}

/* ------------------------------------------------------------------ */
/* Individual dump functions                                            */
/* ------------------------------------------------------------------ */

static void
write_manifest(const char *snap_dir, const char *timestamp)
{
    FILE *f = open_in_dir(snap_dir, "manifest.txt");
    if (!f)
        return;

    fprintf(f, "[SNAPSHOT]\r\n");
    fprintf(f, "version=1\r\n");
    fprintf(f, "timestamp_local=%s\r\n", timestamp);
    fprintf(f, "paused=1\r\n");
    fprintf(f, "86box_version=%s\r\n", emu_version[0] ? emu_version : "unknown");
    fprintf(f, "machine=%s\r\n",
            (machine >= 0) ? machine_getname(machine) : "unknown");

    /* Extract config file leaf name from full cfg_path. */
    char cfg_copy[1024];
    snprintf(cfg_copy, sizeof(cfg_copy), "%s", cfg_path);
    fprintf(f, "config_name=%s\r\n", path_get_filename(cfg_copy));

    fprintf(f, "\r\n[FILES]\r\n");
    if (debug_snapshot_option_enabled(DEBUG_SNAPSHOT_OPT_CPU)) {
        fprintf(f, "cpu=cpu.txt\r\n");
        fprintf(f, "cpu_code=cpu_code.bin\r\n");
    }
    if (debug_snapshot_option_enabled(DEBUG_SNAPSHOT_OPT_RAM640))
        fprintf(f, "ram640=ram640.bin\r\n");
    if (debug_snapshot_option_enabled(DEBUG_SNAPSHOT_OPT_IO_TRACE))
        fprintf(f, "io_trace=io_trace.txt\r\n");
    if (debug_snapshot_option_enabled(DEBUG_SNAPSHOT_OPT_DEVICE_LIST) ||
        debug_snapshot_option_enabled(DEBUG_SNAPSHOT_OPT_DEVICE_DETAILS))
        fprintf(f, "devices=devices/\r\n");

    fprintf(f, "\r\n[RAM]\r\n");
    fprintf(f, "configured_kb=%u\r\n", mem_size);
    fprintf(f, "conventional_dump_limit_kb=640\r\n");
    fprintf(f, "conventional_dump_bytes=%u\r\n", snapshot_conventional_ram_size());

    fclose(f);
}

static void
write_cpu(const char *snap_dir)
{
    FILE *f = open_in_dir(snap_dir, "cpu.txt");
    if (!f)
        return;

    uint32_t cs_base = cpu_state.seg_cs.base;
    uint32_t ip      = cpu_state.pc;
    uint32_t phys_ip = cs_base + ip;

    fprintf(f, "[CPU]\r\n");
    fprintf(f, "CS:IP=%04X:%04X\r\n", CS, (unsigned) (ip & 0xFFFF));
    fprintf(f, "PHYS_IP=%08X\r\n", phys_ip);
    fprintf(f, "AX=%04X BX=%04X CX=%04X DX=%04X\r\n", AX, BX, CX, DX);
    fprintf(f, "SI=%04X DI=%04X BP=%04X SP=%04X\r\n", SI, DI, BP, SP);
    fprintf(f, "DS=%04X ES=%04X SS=%04X FLAGS=%04X\r\n", DS, ES, SS,
            cpu_state.flags);

    fprintf(f, "\r\n[SEGMENTS]\r\n");
    fprintf(f, "CS_BASE=%08X\r\n", cs_base);
    fprintf(f, "DS_BASE=%08X\r\n", (uint32_t) cpu_state.seg_ds.base);
    fprintf(f, "ES_BASE=%08X\r\n", (uint32_t) cpu_state.seg_es.base);
    fprintf(f, "SS_BASE=%08X\r\n", (uint32_t) cpu_state.seg_ss.base);

    /* Clamp bytes_before so we don't underflow the segment. */
    uint32_t bytes_before = (ip >= 32) ? 32 : ip;
    uint32_t bytes_total  = bytes_before + 96;
    uint32_t start_phys   = cs_base + ip - bytes_before;

    fprintf(f, "\r\n[CPU_CODE]\r\n");
    fprintf(f, "start_phys=%08X\r\n", start_phys);
    fprintf(f, "bytes_before_ip=%u\r\n", bytes_before);
    fprintf(f, "bytes_after_ip=96\r\n");

    /* Inline hexdump around CS:IP */
    uint8_t buf[128];
    read_phys_block(buf, start_phys, bytes_total);
    fprintf(f, "\r\n[HEXDUMP]\r\n");
    hexdump(f, buf, bytes_total, start_phys);

    fclose(f);
}

static void
write_cpu_code(const char *snap_dir)
{
    uint32_t cs_base      = cpu_state.seg_cs.base;
    uint32_t ip           = cpu_state.pc;
    uint32_t bytes_before = (ip >= 32) ? 32 : ip;
    uint32_t bytes_total  = bytes_before + 96;
    uint32_t start_phys   = cs_base + ip - bytes_before;

    uint8_t buf[128];
    read_phys_block(buf, start_phys, bytes_total);

    FILE *f = open_in_dir(snap_dir, "cpu_code.bin");
    if (!f)
        return;
    fwrite(buf, 1, bytes_total, f);
    fclose(f);
}

static void
write_ram640(const char *snap_dir)
{
#define RAM_CHUNK 0x10000u /* 64 KiB */

    FILE *f = open_in_dir(snap_dir, "ram640.bin");
    if (!f)
        return;

    uint32_t ram_size = snapshot_conventional_ram_size();

    uint8_t *chunk = (uint8_t *) malloc(RAM_CHUNK);
    if (!chunk) {
        fclose(f);
        return;
    }

    for (uint32_t addr = 0; addr < ram_size; addr += RAM_CHUNK) {
        uint32_t sz = RAM_CHUNK;
        if (addr + sz > ram_size)
            sz = ram_size - addr;
        read_phys_block(chunk, addr, sz);
        fwrite(chunk, 1, sz, f);
    }

    free(chunk);
    fclose(f);

#undef RAM_CHUNK
}

static void
write_io_trace(const char *snap_dir)
{
    FILE *f = open_in_dir(snap_dir, "io_trace.txt");
    if (!f)
        return;

    fprintf(f, "[IO_TRACE]\r\n");
    fprintf(f,
            "columns=seq cs ip phys_ip dir port width value ax bx cx dx flags\r\n");

    /*
     * Walk the ring buffer from oldest to newest entry.
     * io_trace_head points at the slot that will be overwritten NEXT,
     * so it is also the oldest slot when the buffer is full.
     */
    unsigned int start = io_trace_head & (VM_DEBUG_IO_TRACE_LEN - 1);
    for (unsigned int i = 0; i < VM_DEBUG_IO_TRACE_LEN; i++) {
        unsigned int          idx = (start + i) & (VM_DEBUG_IO_TRACE_LEN - 1);
        const io_trace_entry_t *e = &io_trace_buf[idx];

        if (e->seq == 0)
            continue; /* never written */

        fprintf(f, "%08X %04X %04X %08X %s %04X %u %0*X %04X %04X %04X %04X %04X\r\n",
                e->seq,
                e->cs_sel,
                (unsigned) (e->ip_off & 0xFFFF),
                e->phys_ip,
                e->dir ? "OUT" : "IN ",
                e->port,
                e->width,
                e->width * 2, /* hex digits */
                e->value,
                e->ax, e->bx, e->cx, e->dx,
                e->eflags);
    }

    fclose(f);
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                   */
/* ------------------------------------------------------------------ */

static char last_snap_dir[1024];

const char *
debug_snapshot_dump_now(void)
{
    if (!dopause)
        return NULL;

    /* Build timestamp string. */
    time_t    now = time(NULL);
    struct tm time_buf;
    struct tm *tm;
    char       ts_dir[32];   /* snapshot_YYYYMMDD_HHMMSS */
    char       ts_human[32]; /* YYYY-MM-DD HH:MM:SS      */
#ifdef _WIN32
    tm = (localtime_s(&time_buf, &now) == 0) ? &time_buf : NULL;
#else
    tm = localtime_r(&now, &time_buf);
#endif
    if (!tm)
        return NULL;

    strftime(ts_dir,   sizeof(ts_dir),   "snapshot_%Y%m%d_%H%M%S", tm);
    strftime(ts_human, sizeof(ts_human), "%Y-%m-%d %H:%M:%S",      tm);

    /* Ensure <usr_path>/debug_snapshots/ exists. */
    char snapshots_root[1024];
    path_append_filename(snapshots_root, usr_path, "debug_snapshots");
    plat_dir_create(snapshots_root);

    /* Create the per-snapshot directory. */
    char snap_dir[1024];
    path_append_filename(snap_dir, snapshots_root, ts_dir);
    if (plat_dir_create(snap_dir) != 0)
        return NULL;

    /* Create devices/ subdirectory for optional providers. */
    char devices_dir[1024];
    path_append_filename(devices_dir, snap_dir, "devices");
    plat_dir_create(devices_dir);

    /* Write core files. */
    write_manifest(snap_dir, ts_human);
    if (debug_snapshot_option_enabled(DEBUG_SNAPSHOT_OPT_CPU)) {
        write_cpu(snap_dir);
        write_cpu_code(snap_dir);
    }
    if (debug_snapshot_option_enabled(DEBUG_SNAPSHOT_OPT_RAM640))
        write_ram640(snap_dir);
    if (debug_snapshot_option_enabled(DEBUG_SNAPSHOT_OPT_IO_TRACE))
        write_io_trace(snap_dir);

    /* Call device snapshots and registered providers. */
    debug_snapshot_writer_t w;
    snprintf(w.dir,         sizeof(w.dir),         "%s", snap_dir);
    snprintf(w.devices_dir, sizeof(w.devices_dir), "%s", devices_dir);

    device_debug_snapshot(&w,
                          debug_snapshot_option_enabled(DEBUG_SNAPSHOT_OPT_DEVICE_LIST),
                          debug_snapshot_option_enabled(DEBUG_SNAPSHOT_OPT_DEVICE_DETAILS));

    for (int i = 0; i < provider_count; i++) {
        if (providers[i].enabled)
            providers[i].fn(&w, providers[i].priv);
    }

    snprintf(last_snap_dir, sizeof(last_snap_dir), "%s", snap_dir);
    return last_snap_dir;
}
