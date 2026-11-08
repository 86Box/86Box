/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation run control: CPU thread lifecycle and pause reasons.
 *
 *          The machine runs only while the CPU thread is running and no
 *          pause reason is set. Each owner sets and clears only its own
 *          reason, so e.g. closing a dialog never resumes a machine the
 *          user paused. Every change is announced with
 *          ui_notify(UI_NOTIFY_RUN_STATE); frontends react on their UI
 *          thread and read the state with runctl_get_status().
 *
 * Authors: David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *          Copyright 2026 David Hrdlička.
 */
#ifndef EMU_RUNCTL_H
#define EMU_RUNCTL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lifecycle of the CPU thread. RUN_PAUSED is RUNNING with a pause reason set. */
typedef enum {
    RUN_STOPPED = 0, /* no CPU thread */
    RUN_STARTING,    /* thread being created */
    RUN_RUNNING,
    RUN_PAUSED,
    RUN_STOPPING     /* stop requested, thread winding down */
} run_state_t;

/* Why the machine is paused. A bit mask; each owner touches only its own bit. */
typedef enum {
    RUN_PAUSE_USER    = 1 << 0, /* menu, pause key, SDL monitor, VM manager */
    RUN_PAUSE_DIALOG  = 1 << 1, /* modal dialog or message box over the main window */
    RUN_PAUSE_FOCUS   = 1 << 2, /* auto-pause while the window is inactive */
    RUN_PAUSE_REMOTE  = 1 << 3, /* VNC renderer with no client connected */
    RUN_PAUSE_MESSAGE = 1 << 4, /* core warning() message box */
    RUN_PAUSE_FATAL   = 1 << 5, /* fatal() error; never cleared */
    RUN_PAUSE_SYNC    = 1 << 6, /* quiesce the CPU around a device state change */
    RUN_PAUSE_HOLD    = 1 << 7, /* automation hold (not wired up yet) */
    RUN_PAUSE_DEBUG   = 1 << 8, /* GDB stub stop (not wired up yet) */
} run_pause_t;

/* Reasons the user's Pause/Resume control clears. */
#define RUN_PAUSE_USER_RESUMABLE (RUN_PAUSE_USER | RUN_PAUSE_FOCUS)

typedef struct {
    run_state_t state;
    uint32_t    pause_reasons;
    uint32_t    epoch;   /* bumps on every run state change */
    uint64_t    emu_us;  /* emulated time executed since startup */
    uint64_t    host_us; /* host monotonic time when emu_us was sampled */
    uint32_t    seq;     /* snapshot sequence number */
} run_status_t;

/* Pause reasons; any thread. runctl_pause() returns at once. runctl_pause_wait()
   also waits until the CPU thread is parked at a safe point, so the caller can
   touch machine state; it doesn't wait on the CPU thread itself or when the CPU
   thread isn't running. */
extern void     runctl_pause(run_pause_t reason);
extern void     runctl_pause_wait(run_pause_t reason);
extern void     runctl_resume(run_pause_t reason);
extern void     runctl_pause_user_toggle(void); /* pause key and menu: USER on, or USER_RESUMABLE off */
extern uint32_t runctl_pause_reasons(void);
extern int      runctl_paused(void);

/* Lifecycle. */
extern run_state_t runctl_state(void);
extern void        runctl_request_stop(void); /* also clears cpu_thread_run */

/* Called by the frontend that owns the CPU thread. */
extern void runctl_thread_starting(void); /* before creating it */
extern void runctl_thread_started(void);  /* first thing on the CPU thread */
extern void runctl_thread_exiting(void);  /* last thing on the CPU thread */
extern void runctl_safe_point(void);      /* CPU thread, between quanta, while not running a quantum */
extern void runctl_quantum_done(uint32_t emu_us); /* CPU thread, after each quantum */

/* Status snapshot; any thread. */
extern void runctl_get_status(run_status_t *out);
/* Percent of real time between two snapshots, or -1 if not meaningful. */
extern int runctl_speed_between(const run_status_t *older, const run_status_t *newer);

#ifdef __cplusplus
}
#endif

#endif /*EMU_RUNCTL_H*/
