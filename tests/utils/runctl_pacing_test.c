/* Exercise the shared pacing loop with a deterministic host clock. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/debugger.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/runctl.h>
#include <86box/thread.h>
#include <86box/ui.h>

volatile int cpu_thread_run = 1;
volatile int is_quit;
__thread int is_cpu_thread;
int          dopause;
int          force_10ms;
int          framecountx;
int          hard_reset_pending;
int          io_turbo_throttle;
int          nvr_dosave = 1;
int          time_sync;

static uint32_t host_ms;
static uint32_t guest_ms;
static uint32_t nvr_saved_ms;
static int      failed;

uint64_t plat_get_ns(void) { return host_ms * 1000000ULL; }
uint32_t plat_get_ticks(void) { return host_ms; }
void plat_delay_ms(uint32_t count) { host_ms += count; }

void
pc_run(void)
{
    const uint32_t block_ms = force_10ms ? 10 : 1;
    guest_ms += block_ms;
    runctl_thread_quantum_done(block_ms * 1000);
}

int
nvr_save(void)
{
    nvr_saved_ms = guest_ms;
    return 0;
}

void debugger_reset_begin(void) {}
void debugger_reset_end(void) {}
void nvr_time_sync(void) {}
void pc_reset_hard_close(void) {}
void pc_reset_hard_init(void) {}
event_t *thread_create_event(void) { return NULL; }
void thread_destroy_event(event_t *event) { (void) event; }
void thread_set_event(event_t *event) { (void) event; }
int thread_wait_event(event_t *event, int timeout) { (void) event; (void) timeout; return 0; }
void ui_notify(int what) { (void) what; }

static void
check_time(const char *phase, uint32_t expected_ms)
{
    if (guest_ms != expected_ms) {
        fprintf(stderr, "%s: expected %u ms of guest time, got %u ms\n", phase, expected_ms, guest_ms);
        failed = 1;
    }
}

static void
pass(void)
{
    /* The delay advances the clock before the pass hook, so the CPU has
       paid the debt through host_ms - 1 when we sample here. */
    if (host_ms == 1001) {
        check_time("before block-size change", 1000);
        runctl_pause(RUN_PAUSE_SYNC);
    } else if ((host_ms == 1002) && runctl_paused()) {
        check_time("while paused", 1000);
        force_10ms = !force_10ms;
        runctl_resume(RUN_PAUSE_SYNC);
    } else if (host_ms == 2001) {
        check_time("after block-size change", 2000);
        if (nvr_saved_ms != 2000) {
            fprintf(stderr, "expected NVR save at 2000 ms, got %u ms\n", nvr_saved_ms);
            failed = 1;
        }
        runctl_request_stop();
    }
}

int
main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    force_10ms = !strcmp(argv[1], "large");
    runctl_thread_starting();
    runctl_thread_run_loop(pass);
    return failed;
}
