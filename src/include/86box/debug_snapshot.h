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
 */
#ifndef EMU_DEBUG_SNAPSHOT_H
#define EMU_DEBUG_SNAPSHOT_H

#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Provider API                                                         */
/* ------------------------------------------------------------------ */

typedef struct debug_snapshot_writer_t {
    char dir[1024];         /* snapshot root directory              */
    char devices_dir[1024]; /* <dir>/devices/                       */
} debug_snapshot_writer_t;

typedef void (*debug_snapshot_provider_fn)(debug_snapshot_writer_t *w, void *priv);

/* Register an optional device provider (called at device init time). */
extern void debug_snapshot_register_provider(const char              *name,
                                             debug_snapshot_provider_fn fn,
                                             void                    *priv);

/* Provider enumeration and enable/disable (UI use). */
extern int         debug_snapshot_provider_count(void);
extern const char *debug_snapshot_provider_name(int idx);
extern int         debug_snapshot_provider_enabled(int idx);
extern void        debug_snapshot_provider_set_enabled(int idx, int enabled);

/* Core snapshot options controlled by the UI. */
#define DEBUG_SNAPSHOT_OPT_CPU            0
#define DEBUG_SNAPSHOT_OPT_RAM640         1
#define DEBUG_SNAPSHOT_OPT_IO_TRACE       2
#define DEBUG_SNAPSHOT_OPT_DEVICE_LIST    3
#define DEBUG_SNAPSHOT_OPT_DEVICE_DETAILS 4
#define DEBUG_SNAPSHOT_OPT_COUNT          5

extern const char *debug_snapshot_option_name(int idx);
extern int         debug_snapshot_option_enabled(int idx);
extern void        debug_snapshot_option_set_enabled(int idx, int enabled);

/* Category flags for UI grouping */
#define SNAPSHOT_CAT_CORE    0 /* cpu.txt, ram640.bin, io_trace.txt, devices.txt (always active) */
#define SNAPSHOT_CAT_DEVICE  1 /* Device-specific data (toggleable) */
#define SNAPSHOT_CAT_CUSTOM  2 /* Manual providers registered by code (toggleable) */

/*
 * Trigger a full snapshot.  Must be called while the VM is paused.
 * Returns a pointer to a static string with the snapshot directory
 * path on success, or NULL on failure.
 */
extern const char *debug_snapshot_dump_now(void);

/* ------------------------------------------------------------------ */
/* IO trace ring buffer                                                 */
/* ------------------------------------------------------------------ */

#define VM_DEBUG_IO_TRACE_LEN 4096 /* must be a power of two */

typedef struct {
    uint32_t seq;    /* monotone sequence number (1-based; 0 = empty slot) */
    uint16_t cs_sel; /* CS selector at time of access                       */
    uint32_t ip_off; /* IP (offset) at time of access                       */
    uint32_t phys_ip;/* linear CS:IP                                        */
    uint8_t  dir;    /* 0 = IN, 1 = OUT                                     */
    uint16_t port;
    uint8_t  width;  /* 1, 2, or 4 bytes                                    */
    uint32_t value;
    uint16_t ax, bx, cx, dx;
    uint16_t eflags; /* cpu_state.flags at time of access                   */
} io_trace_entry_t;

extern io_trace_entry_t      io_trace_buf[VM_DEBUG_IO_TRACE_LEN];
extern volatile unsigned int io_trace_head; /* index of NEXT write slot    */
extern volatile unsigned int io_trace_seq;  /* last sequence number used   */

/* Called from inb/outb/inw/outw/inl/outl in io.c. */
extern void io_trace_record(uint8_t dir, uint16_t port, uint8_t width,
                            uint32_t value);

#endif /* EMU_DEBUG_SNAPSHOT_H */
