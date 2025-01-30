#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/nv/vid_nv_rivatimer.h>

uint64_t TIMER_USEC;
uint32_t timer_target;

/*Enabled timers are stored in a linked list, with the first timer to expire at
  the head.*/
pc_timer_t *timer_head = NULL;

/* Are we initialized? */
int timer_inited = 0;

static void timer_advance_ex(pc_timer_t *timer, int start);

void
timer_enable(pc_timer_t *timer)
{
    pc_timer_t *timer_node = timer_head;
    int ret = 0;

    if (!timer_inited || (timer == NULL))
        return;

    if (timer->flags & TIMER_ENABLED)
        timer_disable(timer);

    if (timer->next || timer->prev)
        fatal("timer_disable(): Attempting to enable a non-isolated "
              "timer incorrectly marked as disabled\n");

    /*List currently empty - add to head*/
    if (!timer_head) {
        timer_head  = timer;
        timer->next = timer->prev = NULL;
        timer_target              = timer_head->ts.ts32.integer;
        ret = 1;
    } else if (TIMER_LESS_THAN(timer, timer_head)) {
        timer->next      = timer_head;
        timer->prev      = NULL;
        timer_head->prev = timer;
        timer_head       = timer;
        timer_target     = timer_head->ts.ts32.integer;
        ret = 1;
    } else if (!timer_head->next) {
        timer_head->next = timer;
        timer->prev      = timer_head;
        ret = 1;
    }

    if (ret == 0) {
        pc_timer_t *prev = timer_head;
        timer_node       = timer_head->next;

        while (1) {
            /*Timer expires before timer_node. Add to list in front of timer_node*/
            if (TIMER_LESS_THAN(timer, timer_node)) {
                timer->next      = timer_node;
                timer->prev      = prev;
                timer_node->prev = timer;
                prev->next       = timer;
                ret = 1;
                break;
            }

            /*timer_node is last in the list. Add timer to end of list*/
            if (!timer_node->next) {
                timer_node->next = timer;
                timer->prev      = timer_node;
                ret = 1;
                break;
            }

            prev       = timer_node;
            timer_node = timer_node->next;
        }
    }

    /* Do not mark it as enabled if it has failed every single condition. */
    if (ret == 1)
        timer->flags |= TIMER_ENABLED;
}

void
timer_disable(pc_timer_t *timer)
{
    if (!timer_inited || (timer == NULL) || !(timer->flags & TIMER_ENABLED))
        return;

    if (!timer->next && !timer->prev && timer != timer_head)
        fatal("timer_disable(): Attempting to disable an isolated "
              "non-head timer incorrectly marked as enabled\n");

    timer->flags &= ~TIMER_ENABLED;
    timer->in_callback = 0;

    if (timer->prev)
        timer->prev->next = timer->next;
    else
        timer_head = timer->next;
    if (timer->next)
        timer->next->prev = timer->prev;
    timer->prev = timer->next = NULL;
}

void
timer_process(void)
{
    pc_timer_t *timer;

    if (!timer_head)
        return;

    while (1) {
        timer = timer_head;

        if (!TIMER_LESS_THAN_VAL(timer, (uint32_t) tsc))
            break;

        timer_head = timer->next;
        if (timer_head)
            timer_head->prev = NULL;

        timer->next = timer->prev = NULL;
        timer->flags &= ~TIMER_ENABLED;

        if (timer->flags & TIMER_SPLIT)
            timer_advance_ex(timer, 0);   /* We're splitting a > 1 s period into
                                                 multiple <= 1 s periods. */
        else if (timer->callback != NULL) {
            /* Make sure it's not NULL, so that we can
               have a NULL callback when no operation
               is needed. */
            timer->in_callback = 1;
            timer->callback(timer->priv);
            timer->in_callback = 0;
        }
    }

    timer_target = timer_head->ts.ts32.integer;
}

void
timer_close(void)
{
    pc_timer_t *t = timer_head;
    pc_timer_t *r;

    /* Set all timers' prev and next to NULL so it is assured that
       timers that are not in malloc'd structs don't keep pointing
       to timers that may be in malloc'd structs. */
    while (t != NULL) {
        r       = t;
        r->prev = r->next = NULL;
        t                 = r->next;
    }

    timer_head = NULL;

    timer_inited = 0;
}

void
timer_init(void)
{
    timer_target = 0ULL;
    tsc          = 0;

    /* Initialise the CPU-independent timer */
    rivatimer_init();

    timer_inited = 1;
}

void
timer_add(pc_timer_t *timer, void (*callback)(void *priv), void *priv, int start_timer)
{
    memset(timer, 0, sizeof(pc_timer_t));

    timer->callback    = callback;
    timer->in_callback = 0;
    timer->priv        = priv;
    timer->flags       = 0;
    timer->prev        = timer->next = NULL;
    if (start_timer)
        timer_set_delay_u64(timer, 0);
}

/* The API for big timer periods starts here. */
void
timer_stop(pc_timer_t *timer)
{
    if (!timer_inited || (timer == NULL))
        return;

    timer->period = 0.0;
    timer_disable(timer);
    timer->flags &= ~TIMER_SPLIT;
    timer->in_callback = 0;
}

static void
timer_do_period(pc_timer_t *timer, uint64_t period, int start)
{
    if (!timer_inited || (timer == NULL))
        return;

    if (start)
        timer_set_delay_u64(timer, period);
    else
        timer_advance_u64(timer, period);
}

static void
timer_advance_ex(pc_timer_t *timer, int start)
{
    if (!timer_inited || (timer == NULL))
        return;

    if (timer->period > MAX_USEC) {
        timer_do_period(timer, MAX_USEC64 * TIMER_USEC, start);
        timer->period -= MAX_USEC;
        timer->flags |= TIMER_SPLIT;
    } else {
        if (timer->period > 0.0)
            timer_do_period(timer, (uint64_t) (timer->period * ((double) TIMER_USEC)), start);
        else
            timer_disable(timer);
        timer->period = 0.0;
        timer->flags &= ~TIMER_SPLIT;
    }
}

static void
timer_on(pc_timer_t *timer, double period, int start)
{
    if (!timer_inited || (timer == NULL))
        return;

    timer->period = period;
    timer_advance_ex(timer, start);
}

void
timer_on_auto(pc_timer_t *timer, double period)
{
    if (!timer_inited || (timer == NULL))
        return;

    if (period > 0.0)
        /* If the timer is in the callback, signal that, so that timer_advance_u64()
           is used instead of timer_set_delay_u64(). */
        timer_on(timer, period, (timer->period <= 0.0) && !timer->in_callback);
    else
        timer_stop(timer);
}

void
timer_set_new_tsc(uint64_t new_tsc)
{
    pc_timer_t *timer = NULL;
    /* Run timers already expired. */
#ifdef USE_DYNAREC
    if (cpu_use_dynarec)
        update_tsc();
#endif

    if (!timer_head) {
        tsc = new_tsc;
        return;
    }

    timer = timer_head;
    timer_target = new_tsc + (int32_t)(timer_get_ts_int(timer_head) - (uint32_t)tsc);

    while (timer) {
        int32_t offset_from_current_tsc = (int32_t)(timer_get_ts_int(timer) - (uint32_t)tsc);
        timer->ts.ts32.integer = new_tsc + offset_from_current_tsc;

        timer = timer->next;
    }

    tsc = new_tsc;
}
