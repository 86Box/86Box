/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation run control: CPU thread lifecycle, pause reasons,
 *          pacing loop, pacing mode and speed measurement.
 *
 * Authors: David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *          Copyright 2026 David Hrdlička.
 */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/automation.h>
#include <86box/debugger.h>
#include <86box/gdbstub.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/ui.h>
#include <86box/runctl.h>

/* Everything below is guarded by a spinlock; critical sections are a few
   stores. The counters are published with a seqlock so readers never block
   the CPU thread. */
static atomic_flag lock = ATOMIC_FLAG_INIT;

static run_state_t state = RUN_STOPPED; /* lifecycle, without RUN_PAUSED */
static uint32_t    reasons;              /* raw, including ignored ones */
static uint32_t    claims;
static int         user_mode = RUN_MODE_REALTIME; /* shared by the user and automation's ff */
static int         step_fast;                     /* automation's step override */
static uint32_t    epoch;
static uint64_t    emu_us;
static uint64_t    host_us;

static atomic_int mode = RUN_MODE_REALTIME; /* effective; written under the lock */

/* What each claim ignores and owns (see runctl.h). */
#define AUTOMATION_IGNORES (RUN_PAUSE_USER | RUN_PAUSE_FOCUS | RUN_PAUSE_REMOTE | RUN_PAUSE_DIALOG | RUN_PAUSE_MESSAGE)
#define AUTOMATION_OWNS    (RUN_PAUSE_HOLD)

static uint32_t
ignored_by(uint32_t c)
{
    return (c & RUN_CLAIM_AUTOMATION) ? AUTOMATION_IGNORES : 0;
}

static uint32_t
effective(uint32_t r, uint32_t c)
{
    return r & ~ignored_by(c);
}

static int
can_toggle_pause(uint32_t r, uint32_t c)
{
    return !c && !(r & RUN_PAUSE_BLOCKING);
}

static int
can_toggle_mode(uint32_t c)
{
    return !c;
}

/* Requests for the CPU thread, oldest first; guarded by the lock. */
typedef struct request_t {
    struct request_t *next;
    void            (*fn)(void *arg);
    void             *arg;
    event_t          *done; /* runctl_call_wait(): set once fn has returned */
} request_t;

static request_t *req_head;
static request_t *req_tail;
static atomic_int req_pending; /* lets the CPU thread skip the lock when empty */
static int        req_closed = 1; /* no CPU thread to drain the queue */

static atomic_uint seq;   /* odd while the snapshot is being written */
static atomic_int  acked; /* CPU thread passed a safe point since it last ran */

static void
lock_acquire(void)
{
    while (atomic_flag_test_and_set_explicit(&lock, memory_order_acquire))
        ;
}

static void
lock_release(void)
{
    atomic_flag_clear_explicit(&lock, memory_order_release);
}

/* Called with the lock held. */
static request_t *
queue_take_locked(void)
{
    request_t *list = req_head;

    req_head = req_tail = NULL;
    atomic_store_explicit(&req_pending, 0, memory_order_relaxed);
    return list;
}

static void
queue_run(request_t *list)
{
    while (list) {
        request_t *next = list->next;

        list->fn(list->arg);
        if (list->done)
            thread_set_event(list->done); /* the waiter owns the node */
        else
            free(list);
        list = next;
    }
}

/* Queue req, or return 0 if the caller should run it inline: on the CPU
   thread, or when there is none. A request queued while the thread is
   starting or stopping still runs, as the thread drains the queue on its way
   out. */
static int
queue_add(request_t *req)
{
    if (is_cpu_thread)
        return 0;

    lock_acquire();
    if (req_closed) {
        lock_release();
        return 0;
    }

    req->next = NULL;
    if (req_tail)
        req_tail->next = req;
    else
        req_head = req;
    req_tail = req;
    atomic_store_explicit(&req_pending, 1, memory_order_release);
    lock_release();

    return 1;
}

static uint64_t
host_now_us(void)
{
    /* Millisecond ticks: plat_timer_read() has no known frequency on Qt
       (timer_freq is only set by the SDL frontend). A millisecond is plenty
       for one-second speed samples. */
    return (uint64_t) plat_get_ticks() * 1000;
}

/* Called with the lock held. */
static void
write_begin(void)
{
    atomic_fetch_add_explicit(&seq, 1, memory_order_acq_rel);
}

static void
write_end(void)
{
    atomic_fetch_add_explicit(&seq, 1, memory_order_release);
}

/* One change to the run state, applied atomically. Fields left at KEEP
   (or 0 for the masks) don't change. */
#define KEEP (-1)

typedef struct {
    uint32_t set;          /* pause reasons */
    uint32_t clear;
    int      state;        /* lifecycle, or KEEP */
    uint32_t claim_set;
    uint32_t claim_clear;
    int      user_mode;    /* or KEEP */
    int      step_fast;    /* or KEEP */
    int      toggle_pause; /* the user's Pause/Resume, decided under the lock */
    int      toggle_mode;  /* the user's fast forward, decided under the lock */
} change_t;

/* Apply a change, keep the compatibility globals in sync and announce it.
   Returns the pause reasons that took effect. */
static uint32_t
change(change_t c)
{
    lock_acquire();

    if (c.toggle_pause) {
        if (!can_toggle_pause(reasons, claims)) {
            lock_release();
            return 0;
        }
        if (effective(reasons, claims))
            c.clear |= RUN_PAUSE_USER_CLEARABLE;
        else
            c.set |= RUN_PAUSE_USER;
    }
    if (c.toggle_mode) {
        if (!can_toggle_mode(claims)) {
            lock_release();
            return 0;
        }
        c.user_mode = (user_mode == RUN_MODE_FAST) ? RUN_MODE_REALTIME : RUN_MODE_FAST;
    }

    const uint32_t new_claims = (claims | c.claim_set) & ~c.claim_clear;

    /* Ending a claim clears what it owns. */
    if (claims & ~new_claims & RUN_CLAIM_AUTOMATION) {
        c.clear |= AUTOMATION_OWNS;
        c.step_fast = 0;
    }

    const uint32_t new_reasons   = (reasons | c.set) & ~c.clear;
    const int      next_state    = (c.state != KEEP) ? c.state : (int) state;
    const int      new_user_mode = (c.user_mode != KEEP) ? c.user_mode : user_mode;
    const int      new_step_fast = (c.step_fast != KEEP) ? c.step_fast : step_fast;

    const uint32_t old_eff = effective(reasons, claims);
    const uint32_t new_eff = effective(new_reasons, new_claims);

    const int was_running = (state == RUN_RUNNING) && !old_eff;
    const int will_run    = (next_state == RUN_RUNNING) && !new_eff;

    const int old_mode = step_fast ? RUN_MODE_FAST : user_mode;
    const int new_mode = new_step_fast ? RUN_MODE_FAST : new_user_mode;

    if ((new_reasons == reasons) && (next_state == (int) state) && (new_claims == claims) &&
        (new_user_mode == user_mode) && (new_step_fast == step_fast)) {
        lock_release();
        return 0;
    }

    /* Automation holds only while it has its claim. */
    assert(!(new_reasons & RUN_PAUSE_HOLD) || (new_claims & RUN_CLAIM_AUTOMATION));
    assert(!new_step_fast || (new_claims & RUN_CLAIM_AUTOMATION));

    /* A user mode change hidden by the step override isn't visible. */
    const int visible = (new_reasons != reasons) || (next_state != (int) state) ||
                        (new_claims != claims) || (new_mode != old_mode);

    /* Resync the guest clock before the CPU thread can run again. */
    if (!was_running && will_run && (state == RUN_RUNNING) && (time_sync & TIME_SYNC_ENABLED))
        nvr_time_sync();

    write_begin();
    reasons   = new_reasons;
    state     = next_state;
    claims    = new_claims;
    user_mode = new_user_mode;
    step_fast = new_step_fast;
    atomic_store_explicit(&mode, new_mode, memory_order_relaxed);
    if (visible)
        epoch++;
    write_end();

    if (was_running && !will_run)
        atomic_store(&acked, 0);
    dopause = !will_run;

    lock_release();

    if (visible)
        ui_notify(UI_NOTIFY_RUN_STATE);

    return new_eff & ~old_eff;
}

void
runctl_pause(run_pause_t reason)
{
    change((change_t) { .set = reason, .state = KEEP, .user_mode = KEEP, .step_fast = KEEP });
}

void
runctl_pause_wait(run_pause_t reason)
{
    runctl_pause(reason);

    if (is_cpu_thread)
        return;

    /* Wait for the CPU thread to finish its quantum and park. */
    while (!atomic_load(&acked) && (runctl_state() == RUN_PAUSED))
        plat_delay_ms(1);
}

void
runctl_resume(run_pause_t reason)
{
    change((change_t) { .clear = reason, .state = KEEP, .user_mode = KEEP, .step_fast = KEEP });
}

void
runctl_set_mode(run_mode_t new_mode)
{
    assert((new_mode == RUN_MODE_REALTIME) || (new_mode == RUN_MODE_FAST));
    change((change_t) { .state = KEEP, .user_mode = new_mode, .step_fast = KEEP });
}

void
runctl_claim(run_claim_t owner)
{
    change((change_t) { .claim_set = owner, .state = KEEP, .user_mode = KEEP, .step_fast = KEEP });
}

uint32_t
runctl_unclaim(run_claim_t owner)
{
    return change((change_t) { .claim_clear = owner, .state = KEEP, .user_mode = KEEP, .step_fast = KEEP });
}

void
runctl_set_hold(int hold, int fast)
{
    change((change_t) { .set       = hold ? RUN_PAUSE_HOLD : 0,
                        .clear     = hold ? 0 : RUN_PAUSE_HOLD,
                        .state     = KEEP,
                        .user_mode = KEEP,
                        .step_fast = !!fast });
}

void
runctl_toggle_pause(void)
{
    change((change_t) { .state = KEEP, .user_mode = KEEP, .step_fast = KEEP, .toggle_pause = 1 });
}

void
runctl_toggle_fast_forward(void)
{
    change((change_t) { .state = KEEP, .user_mode = KEEP, .step_fast = KEEP, .toggle_mode = 1 });
}

int
runctl_can_toggle_pause(const run_status_t *st)
{
    return can_toggle_pause(st->pause_reasons, st->claims);
}

int
runctl_can_toggle_fast_forward(const run_status_t *st)
{
    return can_toggle_mode(st->claims);
}

run_mode_t
runctl_mode(void)
{
    return (run_mode_t) atomic_load_explicit(&mode, memory_order_relaxed);
}

uint32_t
runctl_pause_reasons(void)
{
    lock_acquire();
    const uint32_t ret = reasons;
    lock_release();
    return ret;
}

int
runctl_paused(void)
{
    lock_acquire();
    const int ret = effective(reasons, claims) != 0;
    lock_release();
    return ret;
}

run_state_t
runctl_state(void)
{
    lock_acquire();
    const run_state_t ret = ((state == RUN_RUNNING) && effective(reasons, claims)) ? RUN_PAUSED : state;
    lock_release();
    return ret;
}

void
runctl_request_stop(void)
{
    cpu_thread_run = 0;

    lock_acquire();
    const int active = (state == RUN_STARTING) || (state == RUN_RUNNING);
    lock_release();

    if (active)
        change((change_t) { .state = RUN_STOPPING, .user_mode = KEEP, .step_fast = KEEP });
}

void
runctl_thread_starting(void)
{
    lock_acquire();
    req_closed = 0;
    lock_release();

    change((change_t) { .state = RUN_STARTING, .user_mode = KEEP, .step_fast = KEEP });
}

void
runctl_thread_started(void)
{
    lock_acquire();
    const int stopping = (state == RUN_STOPPING);
    lock_release();

    /* A stop may have been requested before the thread got going. */
    if (!stopping)
        change((change_t) { .state = RUN_RUNNING, .user_mode = KEEP, .step_fast = KEEP });
}

void
runctl_thread_exiting(void)
{
    /* Run what is still queued, and close the queue in the same critical
       section that finds it empty, so no request is left behind. From then
       on callers run their requests inline. */
    request_t *list;

    do {
        lock_acquire();
        list       = queue_take_locked();
        req_closed = !list;
        lock_release();

        queue_run(list);
    } while (list);

    change((change_t) { .state = RUN_STOPPED, .user_mode = KEEP, .step_fast = KEEP });
}

void
runctl_thread_safe_point(void)
{
    if (dopause && !atomic_load(&acked))
        atomic_store(&acked, 1);

    if (atomic_load_explicit(&req_pending, memory_order_acquire)) {
        lock_acquire();
        request_t *list = queue_take_locked();
        lock_release();

        queue_run(list);
    }
}

void
runctl_call(void (*fn)(void *arg), void *arg)
{
    request_t *req = malloc(sizeof(request_t));

    req->fn   = fn;
    req->arg  = arg;
    req->done = NULL;

    if (!queue_add(req)) {
        free(req);
        fn(arg);
    }
}

void
runctl_call_wait(void (*fn)(void *arg), void *arg)
{
    request_t req = { .fn = fn, .arg = arg };

    req.done = thread_create_event();
    if (queue_add(&req))
        thread_wait_event(req.done, -1);
    else
        fn(arg);
    thread_destroy_event(req.done);
}

void
runctl_thread_quantum_done(uint32_t us)
{
    lock_acquire();
    write_begin();
    emu_us += us;
    host_us = host_now_us();
    write_end();
    lock_release();
}

/* Pacing: the CPU thread owes the guest the host time that has passed, and
   pays it back one quantum (1 ms, or 10 ms with force_10ms) per pass. */
#define MAX_DEBT_NS 50000000LL

static void
idle_hard_reset(void)
{
    /* Trigger a hard reset if one is pending. */
    if (hard_reset_pending) {
        hard_reset_pending = 0;
        debugger_reset_begin();
        pc_reset_hard_close();
        pc_reset_hard_init();
        debugger_reset_end();
    }
}

void
runctl_thread_run_loop(void (*pass)(void))
{
    int64_t old_ns          = (int64_t) plat_get_ns();
    int64_t debt_ns         = 0;
    int     turbo_ok_passes = 0;
    int     nvr_elapsed_ms  = 0;

    is_cpu_thread = 1;
    framecountx   = 0;
    runctl_thread_started();

    while (!is_quit && cpu_thread_run) {
        /* See if it is time to run a quantum. */
        const int64_t new_ns = (int64_t) plat_get_ns();
        debt_ns += (new_ns - old_ns);
        old_ns = new_ns;
        if (debt_ns > MAX_DEBT_NS)
            debt_ns = MAX_DEBT_NS;

        automation_poll();
        /* Settings can change the block length while the CPU thread is parked.
           Read it after observing the resumed state and use it for the entire
           pass, including accounting after the next safe point. */
        const int     quantum_ms = (runctl_state() == RUN_RUNNING) ? (force_10ms ? 10 : 1) : 0;
        const int64_t quantum_ns = quantum_ms * 1000000LL;
        if (gdbstub_next_asap && (debt_ns < quantum_ns))
            debt_ns = quantum_ns;

        if (quantum_ms && ((debt_ns >= quantum_ns) || (runctl_mode() == RUN_MODE_FAST))) {
            /* One pc_run() per pass. No burst catch-up (a loop of quanta): it
               overshoots after dips and amplifies <100/>100 speed oscillation. */
#ifdef USE_INSTRUMENT
            const uint64_t start_ns = plat_get_ns();
#endif
            pc_run();
            automation_quantum();
            runctl_thread_safe_point(); /* fast forward never idles */

#ifdef USE_INSTRUMENT
            if (instru_enabled) {
                uint64_t elapsed_us       = (plat_get_ns() - start_ns) / 1000;
                uint64_t total_elapsed_ms = (uint64_t) ((double) tsc / cpu_s->rspeed * 1000);
                printf("[instrument] %llu, %llu\n", total_elapsed_ms, elapsed_us);
                if (instru_run_ms && total_elapsed_ms >= instru_run_ms)
                    debt_ns = 0;
            }
#endif
            /* Every 2 emulated seconds we save the machine status. */
            if (nvr_elapsed_ms < 2000)
                nvr_elapsed_ms += quantum_ms;
            if (nvr_elapsed_ms >= 2000 && nvr_dosave) {
                nvr_save();
                nvr_dosave     = 0;
                nvr_elapsed_ms = 0;
            }

            if ((runctl_mode() != RUN_MODE_FAST) && (debt_ns >= quantum_ns))
                debt_ns -= quantum_ns;
            else
                debt_ns = 0; /* fast forward, including unpaced automation steps, must not cause catch-up later */

            /* Drop the Turbo IDE string-I/O shortcut while the host is behind, and bring it
               back only after it has kept up for a while, so it cannot flap every frame. */
            if (debt_ns >= (MAX_DEBT_NS / 2)) {
                io_turbo_throttle = 1;
                turbo_ok_passes   = 0;
            } else if (io_turbo_throttle && (debt_ns < quantum_ns)) {
                if (++turbo_ok_passes >= 500)
                    io_turbo_throttle = 0;
            }
        } else {
            idle_hard_reset();
            runctl_thread_safe_point();

            /* Keep answering a GDB client while the machine isn't running. */
            gdbstub_poll();

            /* Just so we don't overload the host OS. */
            plat_delay_ms(1);
        }

        if (pass)
            pass();
    }

    is_quit = 1;
    runctl_thread_exiting();
}

void
runctl_status(run_status_t *out)
{
    uint32_t s;

    do {
        while ((s = atomic_load_explicit(&seq, memory_order_acquire)) & 1)
            ;
        out->state         = ((state == RUN_RUNNING) && effective(reasons, claims)) ? RUN_PAUSED : state;
        out->pause_reasons = reasons;
        out->claims        = claims;
        out->mode          = (run_mode_t) atomic_load_explicit(&mode, memory_order_relaxed);
        out->epoch         = epoch;
        out->emu_us        = emu_us;
        out->host_us       = host_us;
        atomic_thread_fence(memory_order_acquire);
    } while (atomic_load_explicit(&seq, memory_order_relaxed) != s);

    out->seq = s;
}

int
runctl_speed_between(const run_status_t *older, const run_status_t *newer)
{
    if ((older->state != RUN_RUNNING) || (newer->state != RUN_RUNNING))
        return -1;
    if (older->epoch != newer->epoch)
        return -1;
    if (newer->host_us < (older->host_us + 100000))
        return -1;

    const uint64_t host = newer->host_us - older->host_us;

    return (int) (((newer->emu_us - older->emu_us) * 100 + host / 2) / host);
}

/* The published speed. Both ends of a sample are the host times at which a
   quantum finished, not the times the sampling timer fired, so a late timer
   callback doesn't show up as a dip followed by a rebound. */
static atomic_int   speed = -1;
static run_status_t speed_base; /* only touched by runctl_speed_update()'s caller */
static int          speed_base_valid;

void
runctl_speed_update(void)
{
    run_status_t now;
    int          pct = -1;

    runctl_status(&now);
    if (speed_base_valid)
        pct = runctl_speed_between(&speed_base, &now);
    speed_base       = now;
    speed_base_valid = 1;

    /* Paused, held, just resumed or switched modes: keep the last value. */
    if (pct < 0)
        return;

#ifdef SCREENSHOT_MODE
    pct = ((pct + 2) / 5) * 5;
#endif

    atomic_store_explicit(&speed, pct, memory_order_relaxed);
    ui_notify(UI_NOTIFY_SPEED);
}

int
runctl_speed(void)
{
    return atomic_load_explicit(&speed, memory_order_relaxed);
}
