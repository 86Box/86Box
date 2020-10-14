#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/timer.h>


uint64_t TIMER_USEC;
uint32_t timer_target;

/*Enabled timers are stored in a linked list, with the first timer to expire at
  the head.*/
pc_timer_t *timer_head = NULL;

/* Are we initialized? */
int timer_inited = 0;


void
timer_enable(pc_timer_t *timer)
{
    pc_timer_t *timer_node = timer_head;

    if (!timer_inited || (timer == NULL))
	return;

    if (timer->flags & TIMER_ENABLED)
	timer_disable(timer);

    if (timer->next || timer->prev)
	fatal("timer_enable - timer->next\n");

    timer->flags |= TIMER_ENABLED;

    /*List currently empty - add to head*/
    if (!timer_head) {
	timer_head = timer;
	timer->next = timer->prev = NULL;
	timer_target = timer_head->ts.ts32.integer;
	return;
    }

    timer_node = timer_head;

    while(1) {
	/*Timer expires before timer_node. Add to list in front of timer_node*/
	if (TIMER_LESS_THAN(timer, timer_node)) {
		timer->next = timer_node;
		timer->prev = timer_node->prev;
		timer_node->prev = timer;
		if (timer->prev)
			timer->prev->next = timer;
		else {
			timer_head = timer;
			timer_target = timer_head->ts.ts32.integer;
		}
		return;
	}

	/*timer_node is last in the list. Add timer to end of list*/
	if (!timer_node->next) {
		timer_node->next = timer;
		timer->prev = timer_node;
		return;
	}

	timer_node = timer_node->next;
    }
}


void
timer_disable(pc_timer_t *timer)
{
    if (!timer_inited || (timer == NULL) || !(timer->flags & TIMER_ENABLED))
	return;

    if (!timer->next && !timer->prev && timer != timer_head)
	fatal("timer_disable - !timer->next\n");

    timer->flags &= ~TIMER_ENABLED;

    if (timer->prev)
	timer->prev->next = timer->next;
    else
	timer_head = timer->next;
    if (timer->next)
	timer->next->prev = timer->prev;
    timer->prev = timer->next = NULL;
}


void
timer_remove_head(void)
{
    pc_timer_t *timer;

    if (!timer_inited)
	return;

    if (timer_head) {
	timer = timer_head;
	timer_head = timer->next;
	if (timer_head) {
		timer_head->prev = NULL;
		timer->next->prev = NULL;
	}
	timer->next = timer->prev = NULL;
	timer->flags &= ~TIMER_ENABLED;
    }
}


void
timer_process(void)
{
    pc_timer_t *timer;

    if (!timer_inited || !timer_head)
	return;

    while(1) {
	timer = timer_head;

	if (!TIMER_LESS_THAN_VAL(timer, (uint32_t)tsc))
		break;

	timer_remove_head();

	if (timer->flags & TIMER_SPLIT)
		timer_advance_ex(timer, 0);	/* We're splitting a > 1 s period into multiple <= 1 s periods. */
	else if (timer->callback != NULL)	/* Make sure it's no NULL, so that we can have a NULL callback when no operation is needed. */
		timer->callback(timer->p);
    }

    timer_target = timer_head->ts.ts32.integer;
}


void
timer_close(void)
{
    pc_timer_t *t = timer_head, *r;

    /* Set all timers' prev and next to NULL so it is assured that
       timers that are not in malloc'd structs don't keep pointing
       to timers that may be in malloc'd structs. */
    while (t != NULL) {
	r = t;
	r->prev = r->next = NULL;
	t = r->next;
    }

    timer_head = NULL;

    timer_inited = 0;
}


void
timer_init(void)
{
    timer_target = 0ULL;
    tsc = 0;

    timer_inited = 1;
}


void
timer_add(pc_timer_t *timer, void (*callback)(void *p), void *p, int start_timer)
{
    memset(timer, 0, sizeof(pc_timer_t));

    timer->callback = callback;
    timer->p = p;
    timer->flags = 0;
    timer->prev = timer->next = NULL;
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


void
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


void
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
	timer_on(timer, period, (timer->period == 0.0));
    else
	timer_stop(timer);
}
