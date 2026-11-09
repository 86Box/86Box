/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation run control: CPU thread lifecycle, pause reasons
 *          and pacing mode.
 *
 * Authors: David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *          Copyright 2026 David Hrdlička.
 */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/runctl.h>

/* Everything below is guarded by a spinlock; critical sections are a few
   stores. The counters are published with a seqlock so readers never block
   the CPU thread. */
static atomic_flag lock = ATOMIC_FLAG_INIT;

static run_state_t state = RUN_STOPPED; /* lifecycle, without RUN_PAUSED */
static uint32_t    reasons;
static uint32_t    epoch;
static uint64_t    emu_us;
static uint64_t    host_us;

static atomic_int mode = RUN_MODE_REALTIME; /* written under the lock */

static atomic_uint seq;  /* odd while the snapshot is being written */
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

static uint64_t
host_now_us(void)
{
    return timer_freq ? (uint64_t) ((double) plat_timer_read() * 1000000.0 / (double) timer_freq) : 0;
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

static int
running_locked(void)
{
    return (state == RUN_RUNNING) && !reasons;
}

/* Apply a change to the lifecycle state or the pause reasons, keep the
   compatibility globals in sync and announce the change. */
static void
change(uint32_t set, uint32_t clear, int new_state)
{
    lock_acquire();

    const uint32_t old_reasons = reasons;
    const int      old_state   = state;
    const int      was_running = running_locked();

    const uint32_t new_reasons = (reasons | set) & ~clear;
    const int      next_state  = (new_state >= 0) ? new_state : old_state;
    const int      will_run    = (next_state == RUN_RUNNING) && !new_reasons;

    if ((new_reasons == old_reasons) && (next_state == old_state)) {
        lock_release();
        return;
    }

    /* Resync the guest clock before the CPU thread can run again. */
    if (!was_running && will_run && (old_state == RUN_RUNNING) && (time_sync & TIME_SYNC_ENABLED))
        nvr_time_sync();

    write_begin();
    reasons = new_reasons;
    state   = next_state;
    epoch++;
    write_end();

    if (was_running && !will_run)
        atomic_store(&acked, 0);
    dopause = !will_run;

    lock_release();

    ui_notify(UI_NOTIFY_RUN_STATE);
}

void
runctl_pause(run_pause_t reason)
{
    change(reason, 0, -1);
}

void
runctl_pause_wait(run_pause_t reason)
{
    change(reason, 0, -1);

    if (is_cpu_thread)
        return;

    /* Wait for the CPU thread to finish its quantum and park. */
    while (!atomic_load(&acked) && (runctl_state() == RUN_PAUSED))
        plat_delay_ms(1);
}

void
runctl_resume(run_pause_t reason)
{
    change(0, reason, -1);
}

void
runctl_pause_user_toggle(void)
{
    if (runctl_paused())
        runctl_resume(RUN_PAUSE_USER_RESUMABLE);
    else
        runctl_pause(RUN_PAUSE_USER);
}

void
runctl_set_mode(run_mode_t new_mode)
{
    /* This is the user's mode. Unpaced will be a separate override owned by
       automation, so a step never overwrites (or restores) the user's mode. */
    assert((new_mode == RUN_MODE_REALTIME) || (new_mode == RUN_MODE_FAST));

    lock_acquire();
    if (atomic_load_explicit(&mode, memory_order_relaxed) == (int) new_mode) {
        lock_release();
        return;
    }

    /* A new epoch, so no speed sample spans two modes. */
    write_begin();
    atomic_store_explicit(&mode, new_mode, memory_order_relaxed);
    epoch++;
    write_end();
    lock_release();

    ui_notify(UI_NOTIFY_RUN_STATE);
}

run_mode_t
runctl_mode(void)
{
    return (run_mode_t) atomic_load_explicit(&mode, memory_order_relaxed);
}

void
runctl_fast_forward_toggle(void)
{
    runctl_set_mode((runctl_mode() == RUN_MODE_FAST) ? RUN_MODE_REALTIME : RUN_MODE_FAST);
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
    return runctl_pause_reasons() != 0;
}

run_state_t
runctl_state(void)
{
    lock_acquire();
    const run_state_t ret = ((state == RUN_RUNNING) && reasons) ? RUN_PAUSED : state;
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
        change(0, 0, RUN_STOPPING);
}

void
runctl_thread_starting(void)
{
    change(0, 0, RUN_STARTING);
}

void
runctl_thread_started(void)
{
    lock_acquire();
    const int stopping = (state == RUN_STOPPING);
    lock_release();

    /* A stop may have been requested before the thread got going. */
    if (!stopping)
        change(0, 0, RUN_RUNNING);
}

void
runctl_thread_exiting(void)
{
    change(0, 0, RUN_STOPPED);
}

void
runctl_safe_point(void)
{
    if (dopause && !atomic_load(&acked))
        atomic_store(&acked, 1);
}

void
runctl_quantum_done(uint32_t us)
{
    lock_acquire();
    write_begin();
    emu_us += us;
    host_us = host_now_us();
    write_end();
    lock_release();
}

void
runctl_get_status(run_status_t *out)
{
    uint32_t s;

    do {
        while ((s = atomic_load_explicit(&seq, memory_order_acquire)) & 1)
            ;
        out->state         = ((state == RUN_RUNNING) && reasons) ? RUN_PAUSED : state;
        out->pause_reasons = reasons;
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

    return (int) (((newer->emu_us - older->emu_us) * 100) / (newer->host_us - older->host_us));
}
