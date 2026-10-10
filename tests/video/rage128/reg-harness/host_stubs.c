/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- register harness, host-service stubs.
 *
 *          Only 86Box core plumbing lives here, no device behavior. The
 *          device sources are the production files; everything they call
 *          outward lands here, or in src/unix/unix_thread.c for the
 *          thread primitives. The replay harness builds this file too.
 *
 *          The vectors rely on two deliberate stub behaviors:
 *          - The legacy VGA core is a shell: VRAM is allocated and the
 *            fields the Rage 128 owns work, but VGA plane and timing
 *            behavior does not exist here. Vectors must not test generic
 *            VGA behavior.
 *          - Bus-master reads and writes resolve against a declared fake
 *            system-memory window; stray accesses are counted so a
 *            vector can pin them at zero.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <sys/mman.h>
#endif
#include <time.h>
#include <unistd.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/plat.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/dma.h>
#include <86box/i2c.h>
#include <86box/ui.h>
#include <86box/vid_ddc.h>

/* ---- core globals the device references ---- */
uint64_t  TIMER_USEC = 10ULL << 32;
uint64_t  tsc;
void     *cpu_f = NULL; /* no guest CPU: the telemetry header skips its line */
void     *cpu_s = NULL;
double    cpuclock = 100000000.0;
uint32_t  mem_size = 8192; /* KB; GART tables and rings live in ram[] */
uint8_t  *ram;
monitor_t monitors[MONITORS_NUM];
int       monitor_index_global = 0;
uint32_t *video_15to32;
uint32_t *video_16to32;

/* video.c owns these ramps in the emulator; nothing in the harness pulls
   that TU in, so any scanout path reaching rage128_conv_16to32 would read
   a null pointer. Same formulas as video.c calc_15to32/calc_16to32. */
void
harness_video_luts_init(void)
{
    if (video_16to32 != NULL)
        return;
    video_15to32 = calloc(4, 65536);
    video_16to32 = calloc(4, 65536);
    for (uint32_t c = 0; c < 65536; c++) {
        uint32_t c15 = c & 0x7fff;

        video_15to32[c] = 0xff000000
            | ((uint32_t) (((double) ((c15 >> 10) & 31) / 31.0) * 255.0) << 16)
            | ((uint32_t) (((double) ((c15 >> 5) & 31) / 31.0) * 255.0) << 8)
            | (uint32_t) (((double) (c15 & 31) / 31.0) * 255.0);
        video_16to32[c] = 0xff000000
            | ((uint32_t) (((double) ((c >> 11) & 31) / 31.0) * 255.0) << 16)
            | ((uint32_t) (((double) ((c >> 5) & 63) / 63.0) * 255.0) << 8)
            | (uint32_t) (((double) (c & 31) / 31.0) * 255.0);
    }
}

/* ---- harness-controlled device configuration ---- */
int harness_cfg_memory_mb      = 32;
int harness_cfg_render_threads = 1;
int harness_cfg_recompiler     = 0;
int harness_cfg_instance       = 1;

int
device_get_config_int(const char *s)
{
    if (!strcmp(s, "memory"))
        return harness_cfg_memory_mb;
    if (!strcmp(s, "render_threads"))
        return harness_cfg_render_threads;
    if (!strcmp(s, "recompiler"))
        return harness_cfg_recompiler;
    if (!strcmp(s, "gpu_fold"))
        return 1; /* the device default: folded pipelines on */
    return 0; /* realtime_pacing, gpu_raster, gpu_2d_verify */
}

int
device_get_instance(void)
{
    return harness_cfg_instance;
}

/* ---- user-data path: the GPU telemetry log, the census and the MPEG
   dump resolve a relative file name against it. The register harness
   turns none of them on; a replay honors the same environment switches
   as the emulator, and its files then land in the current directory.
   The absolute-path test follows the 86Box frontends: on Windows a
   drive letter or a leading slash or backslash, elsewhere a leading
   slash ---- */
char usr_path[1024] = "./";

int
path_abs(char *path)
{
#ifdef _WIN32
    return path[0] && (path[1] == ':' || path[0] == '\\' || path[0] == '/');
#else
    return path[0] == '/';
#endif
}

FILE *
plat_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

void
path_append_filename(char *dest, const char *s1, const char *s2)
{
    snprintf(dest, 1024 + 32, "%s%s", s1, s2);
}

/* ---- logging ---- */
int harness_log_quiet = 1;

/* Device log tail. Self-announcing one-shots ([r128 TEXFMT], [r128 MC], ...)
   are the device's only output for a dropped-but-noticed operation, so a
   vector that exercises one has nothing else to assert on. Capture happens
   whether or not the run is quiet -- quiet governs the console, not the tap.
   Oldest text is dropped, never the newest: the assertion always follows the
   write that produced the line. */
char   harness_log_tail[8192];
size_t harness_log_tail_len;

void
harness_log_tail_reset(void)
{
    harness_log_tail_len = 0;
    harness_log_tail[0]  = '\0';
}

void
pclog_ex(const char *fmt, va_list ap)
{
    char    line[1024];
    va_list ap2;
    int     n;

    va_copy(ap2, ap);
    n = vsnprintf(line, sizeof(line), fmt, ap2);
    va_end(ap2);
    if (n > 0) {
        size_t len = (size_t) n < sizeof(line) - 1 ? (size_t) n : sizeof(line) - 1;

        if (len >= sizeof(harness_log_tail)) {
            memcpy(harness_log_tail, line + len - (sizeof(harness_log_tail) - 1),
                   sizeof(harness_log_tail) - 1);
            harness_log_tail_len = sizeof(harness_log_tail) - 1;
        } else {
            if (harness_log_tail_len + len >= sizeof(harness_log_tail)) {
                size_t drop = harness_log_tail_len + len - (sizeof(harness_log_tail) - 1);

                memmove(harness_log_tail, harness_log_tail + drop,
                        harness_log_tail_len - drop);
                harness_log_tail_len -= drop;
            }
            memcpy(harness_log_tail + harness_log_tail_len, line, len);
            harness_log_tail_len += len;
        }
        harness_log_tail[harness_log_tail_len] = '\0';
    }
    if (!harness_log_quiet)
        vfprintf(stderr, fmt, ap);
}

void
pclog(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    pclog_ex(fmt, ap);
    va_end(ap);
}

/* ---- guest system memory ----
   The device reads GART tables/rings straight from the ram[] array
   (rage128_guest_ram_readl) and bus-masters through dma_bm_read/write;
   both must resolve against the same backing store, exactly as in the
   emulator. Out-of-bounds bus-master accesses are counted, never
   satisfied, so an invented address cannot hide. */
uint64_t harness_sysmem_stray;
/* The write-only share of the count above, so a replay can tell a
   device->guest write that had no backing store from a read that did. */
uint64_t harness_sysmem_stray_wr;

/* Replay hook: when set, bus-master READS are answered from a recorded
   stream instead of ram[]. Returning 0 leaves the access on the ordinary
   RAM path. null for the register harness, which has real backing store
   for everything it masters. */
int (*harness_bm_read_hook)(uint32_t addr, uint8_t *dst, uint32_t len);

/* Observation-only hook: every bus-master write the device issues, with
   the absolute address and length, before the backing-store check. Never
   satisfies or blocks the write. null for the register harness. */
void (*harness_bm_write_hook)(uint32_t addr, uint32_t len);

/* Pacing vectors install a barrier around the activity sample to control
   producer timing. null lets other harness runs sample without a barrier. */
void (*harness_pace_sample_hook)(void *dev, int phase) = NULL;

void
harness_ram_alloc(void)
{
    if (!ram)
        ram = calloc((size_t) mem_size << 10, 1);
}

static int
harness_ram_ok(uint32_t addr, uint32_t len)
{
    if ((ram == NULL) || ((uint64_t) addr + len > ((uint64_t) mem_size << 10))) {
        harness_sysmem_stray++;
        return 0;
    }
    return 1;
}

void
dma_bm_read(uint32_t PhysAddress, uint8_t *DataRead, uint32_t TotalSize, int TransferSize)
{
    (void) TransferSize;
    if (harness_bm_read_hook && harness_bm_read_hook(PhysAddress, DataRead, TotalSize))
        return;
    if (!harness_ram_ok(PhysAddress, TotalSize)) {
        memset(DataRead, 0xff, TotalSize);
        return;
    }
    memcpy(DataRead, ram + PhysAddress, TotalSize);
}

void
dma_bm_write(uint32_t PhysAddress, const uint8_t *DataWrite, uint32_t TotalSize, int TransferSize)
{
    (void) TransferSize;
    if (harness_bm_write_hook)
        harness_bm_write_hook(PhysAddress, TotalSize);
    if (!harness_ram_ok(PhysAddress, TotalSize)) {
        harness_sysmem_stray_wr++;
        return;
    }
    memcpy(ram + PhysAddress, DataWrite, TotalSize);
}

/* ---- I/O registration: the harness calls the device's handlers directly,
   so only WHICH ports carry a handler is recorded (a per-port count, the
   real table is a list). Memory-mapping registration stays inert. ---- */
static uint8_t harness_io_map[0x10000];

int
harness_io_count(uint16_t port)
{
    return harness_io_map[port];
}

void
io_sethandler(uint16_t base, uint16_t size,
              uint8_t (*inb)(uint16_t, void *), uint16_t (*inw)(uint16_t, void *),
              uint32_t (*inl)(uint16_t, void *),
              void (*outb)(uint16_t, uint8_t, void *), void (*outw)(uint16_t, uint16_t, void *),
              void (*outl)(uint16_t, uint32_t, void *), void *priv)
{
    (void) inb; (void) inw; (void) inl;
    (void) outb; (void) outw; (void) outl; (void) priv;
    for (uint32_t p = base; p < (uint32_t) base + size && p < 0x10000; p++)
        harness_io_map[p]++;
}

void
io_removehandler(uint16_t base, uint16_t size,
                 uint8_t (*inb)(uint16_t, void *), uint16_t (*inw)(uint16_t, void *),
                 uint32_t (*inl)(uint16_t, void *),
                 void (*outb)(uint16_t, uint8_t, void *), void (*outw)(uint16_t, uint16_t, void *),
                 void (*outl)(uint16_t, uint32_t, void *), void *priv)
{
    (void) inb; (void) inw; (void) inl;
    (void) outb; (void) outw; (void) outl; (void) priv;
    for (uint32_t p = base; p < (uint32_t) base + size && p < 0x10000; p++)
        if (harness_io_map[p])
            harness_io_map[p]--;
}

void
mem_mapping_add(mem_mapping_t *mapping, uint32_t base, uint32_t size,
                uint8_t (*read_b)(uint32_t, void *), uint16_t (*read_w)(uint32_t, void *),
                uint32_t (*read_l)(uint32_t, void *),
                void (*write_b)(uint32_t, uint8_t, void *), void (*write_w)(uint32_t, uint16_t, void *),
                void (*write_l)(uint32_t, uint32_t, void *),
                uint8_t *exec, uint32_t flags, void *priv)
{
    (void) exec; (void) flags;
    if (mapping) {
        mapping->base    = base;
        mapping->size    = size;
        mapping->read_b  = read_b;
        mapping->read_w  = read_w;
        mapping->read_l  = read_l;
        mapping->write_b = write_b;
        mapping->write_w = write_w;
        mapping->write_l = write_l;
        mapping->priv    = priv;
    }
}

void
mem_mapping_set_handler(mem_mapping_t *mapping,
                        uint8_t (*read_b)(uint32_t, void *), uint16_t (*read_w)(uint32_t, void *),
                        uint32_t (*read_l)(uint32_t, void *),
                        void (*write_b)(uint32_t, uint8_t, void *), void (*write_w)(uint32_t, uint16_t, void *),
                        void (*write_l)(uint32_t, uint32_t, void *))
{
    if (mapping) {
        mapping->read_b  = read_b;
        mapping->read_w  = read_w;
        mapping->read_l  = read_l;
        mapping->write_b = write_b;
        mapping->write_w = write_w;
        mapping->write_l = write_l;
    }
}

void
mem_mapping_set_p(mem_mapping_t *mapping, void *priv)
{
    if (mapping)
        mapping->priv = priv;
}

void
mem_mapping_set_addr(mem_mapping_t *mapping, uint32_t base, uint32_t size)
{
    if (mapping) {
        mapping->base    = base;
        mapping->size    = size;
        mapping->enable  = 1;
    }
}

void
mem_mapping_enable(mem_mapping_t *mapping)
{
    if (mapping)
        mapping->enable = 1;
}

void
mem_mapping_disable(mem_mapping_t *mapping)
{
    if (mapping)
        mapping->enable = 0;
}

/* ---- PCI: capture the registration so vectors drive config space
   through the same read/write pair the bus would. ---- */
uint8_t (*harness_pci_read)(int func, int addr, int len, void *priv);
void (*harness_pci_write)(int func, int addr, int len, uint8_t val, void *priv);
void   *harness_pci_priv;
int     harness_irq_level; /* current INTA line state */

void
pci_add_card(uint8_t add_type,
             uint8_t (*read)(int func, int addr, int len, void *priv),
             void (*write)(int func, int addr, int len, uint8_t val, void *priv),
             void *priv, uint8_t *slot)
{
    (void) add_type;
    harness_pci_read  = read;
    harness_pci_write = write;
    harness_pci_priv  = priv;
    *slot             = 1;
}

/* The MAXX board asks for a second slot on the AGP bus; there is no
   bus here, so the request always succeeds and is counted. */
int harness_sibling_slots;

int
pci_register_sibling_slot(int type)
{
    (void) type;
    harness_sibling_slots++;
    return 1;
}

void
pci_irq(uint8_t slot, uint8_t pci_int, int level, int set, uint8_t *irq_state)
{
    (void) slot; (void) pci_int; (void) level;
    harness_irq_level = !!set;
    *irq_state        = !!set;
}

/* ---- platform services ---- */
void
plat_delay_ms(uint32_t count)
{
    usleep(count * 1000);
}

static char harness_cache_dir[1024];

int
harness_gpu_cache_begin(void)
{
    if (harness_cache_dir[0])
        return 0;
#ifdef _WIN32
    char temp[MAX_PATH], path[MAX_PATH];

    if (!GetTempPathA(sizeof(temp), temp)
        || !GetTempFileNameA(temp, "r128", 0, path))
        return 0;
    DeleteFileA(path);
    if (!CreateDirectoryA(path, NULL))
        return 0;
    snprintf(harness_cache_dir, sizeof(harness_cache_dir), "%s", path);
#else
    snprintf(harness_cache_dir, sizeof(harness_cache_dir),
             "/tmp/rage128-harness-XXXXXX");
    if (!mkdtemp(harness_cache_dir)) {
        harness_cache_dir[0] = '\0';
        return 0;
    }
#endif
    return 1;
}

void
harness_gpu_cache_end(void)
{
    static const char *names[] = { "r128gpu.tuples", "r128gpu.plcache" };
    char path[1088];

    if (!harness_cache_dir[0])
        return;
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", harness_cache_dir, names[i]);
        remove(path);
    }
    rmdir(harness_cache_dir);
    harness_cache_dir[0] = '\0';
}

/* Persisted-cache paths use an empty directory by default, keeping the
   harness away from the user's real config. Loader probes temporarily
   select their own private directory. */
void
plat_get_global_config_dir(char *outbuf, size_t len)
{
    snprintf(outbuf, len, "%s", harness_cache_dir);
}

void
fatal(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    abort();
}

/* ---- UI: the device reports progress and status text; nothing here ---- */
void
ui_progress_wait(const char *message, int total, int (*poll)(void *arg),
                 void *arg)
{
    (void) message; (void) total; (void) poll; (void) arg;
}

void
ui_sb_set_text(char *str)
{
    (void) str;
}

/* A controlled clock makes pacing windows independent of host timing.
   Other vectors keep the monotonic host clock. */
int      harness_ticks_override;
uint32_t harness_ticks_ms;

uint32_t
plat_get_ticks(void)
{
    struct timespec ts;

    if (harness_ticks_override)
        return harness_ticks_ms;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t) (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void *
plat_mmap(size_t size, uint8_t executable, uint8_t *large)
{
    if (large)
        *large = 0;
#ifdef _WIN32
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE,
                        executable ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE);
#else
    int flags = MAP_ANON | MAP_PRIVATE;

#    if defined(__APPLE__) && defined(MAP_JIT)
    if (executable)
        flags |= MAP_JIT;
#    endif
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0),
                   flags, -1, 0);
    return (p == MAP_FAILED) ? NULL : p;
#endif
}

void
plat_munmap(void *ptr, size_t size)
{
#ifdef _WIN32
    /* VirtualFree releases the whole MEM_RESERVE region; size must be 0 */
    (void) size;
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, size);
#endif
}

void
plat_set_thread_name(void *thread, const char *name)
{
    (void) thread; (void) name;
}

/* ---- timers: storage only; the harness never runs the timer loop ---- */
void
timer_add(pc_timer_t *timer, void (*callback)(void *priv), void *priv, int start_timer)
{
    (void) start_timer;
    memset(timer, 0, sizeof(*timer));
    timer->callback = callback;
    timer->priv     = priv;
}

void
timer_enable(pc_timer_t *timer)
{
    (void) timer;
}

void
timer_disable(pc_timer_t *timer)
{
    (void) timer;
}

/* ---- ROM/BIOS: no ROM file in the harness ---- */
int
rom_init(rom_t *rom, const char *fn, uint32_t address, int size, int mask,
         int file_offset, uint32_t flags)
{
    (void) fn; (void) address; (void) size; (void) mask;
    (void) file_offset; (void) flags;
    memset(rom, 0, sizeof(*rom));
    return -1;
}

int
rom_present(const char *fn)
{
    (void) fn;
    return 1;
}

/* ---- DDC / I2C: the real bus (src/device/i2c.c, i2c_gpio.c,
   src/mem/i2c_eeprom.c, src/video/vid_ddc.c) is linked, so the GPIO_MONID
   pad drive reaches an EDID EEPROM at 0x50 exactly as in the emulator.
   Only vid_ddc.c's custom-EDID plumbing is stubbed: the built-in default
   EDID is always used. ---- */
int  monitor_edid            = 0;
char monitor_edid_path[1024] = { 0 };

char *
plat_get_string(int id)
{
    (void) id;
    return "";
}

size_t
ddc_load_edid(char *path, uint8_t *buf, size_t size)
{
    (void) path; (void) buf; (void) size;
    return 0;
}

int
ui_msgbox_header(int flags, char *header, char *message)
{
    (void) flags; (void) header; (void) message;
    return 0;
}

/* ---- video core ---- */
void
video_inform_monitor(int type, const video_timings_t *ptr, int monitor_index)
{
    (void) type; (void) ptr; (void) monitor_index;
}

/* ---- SVGA core shell. Allocates what the device owns and wires the
   callbacks; implements NO VGA semantics. ---- */
static svga_t *harness_svga_pri;

/* The core's remap variant 0 (byte mode): identity. The device's scan
   wrapper saves and calls whatever remap_func the core chose, so the
   stub must provide the extended-mode default rather than null. */
static uint32_t
harness_remap_identity(svga_t *svga, uint32_t in_addr)
{
    (void) svga;
    return in_addr;
}

int
svga_init(const device_t *info, svga_t *svga, void *priv, int memsize,
          void (*recalctimings_ex)(struct svga_t *svga),
          uint8_t (*video_in)(uint16_t addr, void *priv),
          void (*video_out)(uint16_t addr, uint8_t val, void *priv),
          void (*hwcursor_draw)(struct svga_t *svga, int displine),
          void (*overlay_draw)(struct svga_t *svga, int displine))
{
    (void) info;
    svga->priv              = priv;
    svga->monitor_index     = monitor_index_global;
    svga->monitor           = &monitors[svga->monitor_index];
    svga->vram              = calloc(memsize + 4096, 1);
    svga->vram_max          = memsize;
    svga->vram_display_mask = svga->vram_mask = memsize - 1;
    svga->decode_mask       = 0x7fffff;
    svga->changedvram       = calloc((memsize >> 12) + 1, 1);
    svga->recalctimings_ex  = recalctimings_ex;
    svga->video_in          = video_in;
    svga->video_out         = video_out;
    svga->hwcursor_draw     = hwcursor_draw;
    svga->overlay_draw      = overlay_draw;
    svga->render            = svga_render_blank;
    svga->remap_func        = harness_remap_identity;
    svga->map8              = svga->pallook;
    svga->bpp               = 8;
    svga->dispontime        = 1000ULL << 32;
    svga->dispofftime       = 1000ULL << 32;
    harness_svga_pri        = svga;
    return 0;
}

/* The core's override: nothing renders or blits while set. Only the
   flag matters to the device (the poll timer is not modeled here). */
void
svga_set_override(svga_t *svga, int val)
{
    if (svga->override && !val)
        svga->fullchange = svga->monitor->mon_changeframecount;
    svga->override = val;
}

void
svga_close(svga_t *svga)
{
    free(svga->changedvram);
    free(svga->vram);
    if (harness_svga_pri == svga)
        harness_svga_pri = NULL;
}

void
svga_recalctimings(svga_t *svga)
{
    if (svga->recalctimings_ex)
        svga->recalctimings_ex(svga);

    /* The core's own tail, for the hoverride path the device takes: it
       splits the overscan totals evenly across the two edges after the
       device hook has set them. Reproduced here so vectors see the same
       ordering the emulator does. */
    if (svga->hoverride || svga->override) {
        if (svga->hdisp >= 2048)
            svga->monitor->mon_overscan_x = 0;

        svga->y_add         = svga->monitor->mon_overscan_y >> 1;
        svga->left_overscan = svga->x_add = svga->monitor->mon_overscan_x >> 1;
        svga->hblank_sub    = 0;
    }
}

void
video_force_resize_set_monitor(uint8_t res, int monitor_index)
{
    monitors[monitor_index].mon_force_resize = res;
}

uint8_t
video_force_resize_get_monitor(int monitor_index)
{
    return monitors[monitor_index].mon_force_resize;
}

void
svga_out(uint16_t addr, uint8_t val, void *priv)
{
    (void) addr; (void) val; (void) priv;
}

uint8_t
svga_in(uint16_t addr, void *priv)
{
    (void) addr; (void) priv;
    return 0xff;
}

void
svga_poll(void *priv)
{
    (void) priv;
}

void
svga_set_ramdac_type(svga_t *svga, int type)
{
    (void) svga; (void) type;
}

/* Banked A0000 access: raw vram bytes only (no plane logic). */
uint8_t
svga_read(uint32_t addr, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    return svga->vram[addr & svga->vram_mask];
}

uint16_t
svga_readw(uint32_t addr, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    return *(uint16_t *) &svga->vram[addr & svga->vram_mask];
}

uint32_t
svga_readl(uint32_t addr, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    return *(uint32_t *) &svga->vram[addr & svga->vram_mask];
}

void
svga_write(uint32_t addr, uint8_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    svga->vram[addr & svga->vram_mask] = val;
}

void
svga_writew(uint32_t addr, uint16_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    *(uint16_t *) &svga->vram[addr & svga->vram_mask] = val;
}

void
svga_writel(uint32_t addr, uint32_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    *(uint32_t *) &svga->vram[addr & svga->vram_mask] = val;
}

uint8_t
svga_read_linear(uint32_t addr, void *priv)
{
    return svga_read(addr, priv);
}

uint16_t
svga_readw_linear(uint32_t addr, void *priv)
{
    return svga_readw(addr, priv);
}

uint32_t
svga_readl_linear(uint32_t addr, void *priv)
{
    return svga_readl(addr, priv);
}

void
svga_write_linear(uint32_t addr, uint8_t val, void *priv)
{
    svga_write(addr, val, priv);
}

void
svga_writew_linear(uint32_t addr, uint16_t val, void *priv)
{
    svga_writew(addr, val, priv);
}

void
svga_writel_linear(uint32_t addr, uint32_t val, void *priv)
{
    svga_writel(addr, val, priv);
}

/* Scanline renderers: named by recalctimings, never run here. */
void
svga_render_blank(svga_t *svga)
{
    (void) svga;
}

void
svga_render_8bpp_clone_highres(svga_t *svga)
{
    (void) svga;
}

void
svga_render_15bpp_highres(svga_t *svga)
{
    (void) svga;
}

void
svga_render_16bpp_highres(svga_t *svga)
{
    /* Minimal core mirror (no remap, no scrollcache): per-pixel
       conv_16to32, the device's LUT-aware converter -- enough for the
       OV0 key legs, which need a real composed 16bpp line. */
    uint32_t *p;

    if ((svga->displine + svga->y_add) < 0 || svga->monitor->target_buffer == NULL
        || svga->monitor->target_buffer->line[svga->displine + svga->y_add] == NULL
        || svga->conv_16to32 == NULL)
        return;
    p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];
    for (int x = 0; x <= svga->hdisp; x++) {
        uint16_t v = *(uint16_t *) &svga->vram[(svga->memaddr + (uint32_t) x * 2)
                                               & svga->vram_display_mask];
        p[x] = svga->conv_16to32(svga, v, 16);
    }
    if (svga->firstline_draw == 2000)
        svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;
}

void
svga_render_24bpp_highres(svga_t *svga)
{
    (void) svga;
}

void
svga_render_32bpp_highres(svga_t *svga)
{
    (void) svga;
}
