#ifndef _TIMER_H_
#define _TIMER_H_

#include "cpu.h"

/* Maximum period, currently 1 second. */
#define	MAX_USEC64	1000000ULL
#define	MAX_USEC	1000000.0

#define TIMER_SPLIT	2
#define TIMER_ENABLED	1


#pragma pack(push,1)
typedef struct
{
    uint32_t frac;
    uint32_t integer;
} ts_struct_t;
#pragma pack(pop)

typedef union
{
    uint64_t	ts64;
    ts_struct_t	ts32;
} ts_t;


/*Timers are based on the CPU Time Stamp Counter. Timer timestamps are in a
  32:32 fixed point format, with the integer part compared against the TSC. The
  fractional part is used when advancing the timestamp to ensure a more accurate
  period.

  As the timer only stores 32 bits of integer timestamp, and the TSC is 64 bits,
  the timer period can only be at most 0x7fffffff CPU cycles. To allow room for
  (optimistic) CPU frequency growth, timer period must be at most 1 second.

  When a timer callback is called, the timer has been disabled. If the timer is
  to repeat, the callback must call timer_advance_u64(). This is a change from
  the old timer API.*/
typedef struct pc_timer_t
{
#ifdef USE_PCEM_TIMER
    uint32_t	ts_integer;
    uint32_t	ts_frac;
#else
    ts_t	ts;
#endif
    int		flags, pad;		/* The flags are defined above. */
    double	period;			/* This is used for large period timers to count
					   the microseconds and split the period. */

    void	(*callback)(void *p);
    void	*p;

    struct	pc_timer_t *prev, *next;
} pc_timer_t;

/*Timestamp of nearest enabled timer. CPU emulation must call timer_process()
  when TSC matches or exceeds this.*/
extern uint32_t	timer_target;

/*Enable timer, without updating timestamp*/
extern void	timer_enable(pc_timer_t *timer);
/*Disable timer*/
extern void	timer_disable(pc_timer_t *timer);

/*Process any pending timers*/
extern void	timer_process(void);

/*Reset timer system*/
extern void	timer_close(void);
extern void	timer_init(void);

/*Add new timer. If start_timer is set, timer will be enabled with a zero
  timestamp - this is useful for permanently enabled timers*/
extern void	timer_add(pc_timer_t *timer, void (*callback)(void *p), void *p, int start_timer);

/*1us in 32:32 format*/
extern uint64_t	TIMER_USEC;

/*True if timer a expires before timer b*/
#define TIMER_LESS_THAN(a, b) ((int64_t)((a)->ts.ts64 - (b)->ts.ts64) <= 0)
/*True if timer a expires before 32 bit integer timestamp b*/
#define TIMER_LESS_THAN_VAL(a, b) ((int32_t)((a)->ts.ts32.integer - (b)) <= 0)
/*True if 32 bit integer timestamp a expires before 32 bit integer timestamp b*/
#define TIMER_VAL_LESS_THAN_VAL(a, b) ((int32_t)((a) - (b)) <= 0)


/*Advance timer by delay, specified in 32:32 format. This should be used to
  resume a recurring timer in a callback routine*/
static __inline void
timer_advance_u64(pc_timer_t *timer, uint64_t delay)
{
    timer->ts.ts64 += delay;

    timer_enable(timer);
}


/*Set a timer to the given delay, specified in 32:32 format. This should be used
  when starting a timer*/
static __inline void
timer_set_delay_u64(pc_timer_t *timer, uint64_t delay)
{
    timer->ts.ts64 = 0ULL;
    timer->ts.ts32.integer = tsc;
    timer->ts.ts64 += delay;

    timer_enable(timer);
}


/*True if timer currently enabled*/
static __inline int
timer_is_enabled(pc_timer_t *timer)
{
    return !!(timer->flags & TIMER_ENABLED);
}


/*Return integer timestamp of timer*/
static __inline uint32_t
timer_get_ts_int(pc_timer_t *timer)
{
    return timer->ts.ts32.integer;
}


/*Return remaining time before timer expires, in us. If the timer has already
  expired then return 0*/
static __inline uint32_t
timer_get_remaining_us(pc_timer_t *timer)
{
    int64_t remaining;

    if (timer->flags & TIMER_ENABLED) {
	remaining = (int64_t) (timer->ts.ts64 - (uint64_t)(tsc << 32));

	if (remaining < 0)
		return 0;
	return remaining / TIMER_USEC;
    }

    return 0;
}


/*Return remaining time before timer expires, in 32:32 timestamp format. If the
  timer has already expired then return 0*/
static __inline uint64_t
timer_get_remaining_u64(pc_timer_t *timer)
{
    int64_t remaining;

    if (timer->flags & TIMER_ENABLED) {
	remaining = (int64_t) (timer->ts.ts64 - (uint64_t)(tsc << 32));

	if (remaining < 0)
		return 0;
	return remaining;
    }

    return 0;
}


/*Set timer callback function*/
static __inline void
timer_set_callback(pc_timer_t *timer, void (*callback)(void *p))
{
    timer->callback = callback;
}


/*Set timer private data*/
static __inline void
timer_set_p(pc_timer_t *timer, void *p)
{
    timer->p = p;
}


/* The API for big timer periods starts here. */
extern void	timer_stop(pc_timer_t *timer);
extern void	timer_advance_ex(pc_timer_t *timer, int start);
extern void	timer_on(pc_timer_t *timer, double period, int start);
extern void	timer_on_auto(pc_timer_t *timer, double period);

extern void	timer_remove_head(void);


extern pc_timer_t *	timer_head;
extern int		timer_inited;


static __inline void
timer_remove_head_inline(void)
{
    pc_timer_t *timer;

    if (timer_inited && timer_head) {
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


static __inline void
timer_process_inline(void)
{
    pc_timer_t *timer;

    if (!timer_inited || !timer_head)
	return;

    while(1) {
	timer = timer_head;

	if (!TIMER_LESS_THAN_VAL(timer, (uint32_t)tsc))
		break;

	timer_remove_head_inline();

	if (timer->flags & TIMER_SPLIT)
		timer_advance_ex(timer, 0);	/* We're splitting a > 1 s period into multiple <= 1 s periods. */
	else if (timer->callback != NULL)	/* Make sure it's no NULL, so that we can have a NULL callback when no operation is needed. */
		timer->callback(timer->p);
    }

    timer_target = timer_head->ts.ts32.integer;
}

#endif /*_TIMER_H_*/
