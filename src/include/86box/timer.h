#ifndef _TIMER_H_
#define _TIMER_H_

#ifndef int128_t
#define int128_t __int128
#endif

#ifndef uint128_t
#define uint128_t unsigned __int128
#endif

extern uint64_t tsc;

/* Maximum period, currently 1 second. */
#define MAX_USEC64    1000000ULL
#define MAX_USEC      1000000.0

#define TIMER_PROCESS 4
#define TIMER_SPLIT   2
#define TIMER_ENABLED 1

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
typedef struct pc_timer_t {
    uint64_t ts_integer;
    uint32_t ts_frac;
    int    flags;  /* The flags are defined above. */
    int    in_callback;
    double period; /* This is used for large period timers to count
                      the microseconds and split the period. */

    void (*callback)(void *priv);
    void *priv;

    struct pc_timer_t *prev;
    struct pc_timer_t *next;
} pc_timer_t;

#ifdef __cplusplus
extern "C" {
#endif

/*Timestamp of nearest enabled timer. CPU emulation must call timer_process()
  when TSC matches or exceeds this.*/
extern uint64_t timer_target;

/*Enable timer, without updating timestamp*/
extern void timer_enable(pc_timer_t *timer);
/*Disable timer*/
extern void timer_disable(pc_timer_t *timer);

/*Process any pending timers*/
extern void timer_process(void);

/*Reset timer system*/
extern void timer_close(void);
extern void timer_init(void);

/*Add new timer. If start_timer is set, timer will be enabled with a zero
  timestamp - this is useful for permanently enabled timers*/
extern void timer_add(pc_timer_t *timer, void (*callback)(void *priv), void *priv, int start_timer);

/*1us in 32:32 format*/
extern uint64_t TIMER_USEC;

/*True if timer a expires before timer b*/
#define TIMER_LESS_THAN(a, b) ((int64_t) ((a)->ts_integer - (b)->ts_integer) <= 0)
/*True if timer a expires before 32 bit integer timestamp b*/
#define TIMER_LESS_THAN_VAL(a, b) ((int64_t) ((a)->ts_integer - (b)) <= 0)
/*True if 32 bit integer timestamp a expires before 32 bit integer timestamp b*/
#define TIMER_VAL_LESS_THAN_VAL(a, b) ((int64_t) ((a) - (b)) <= 0)

#ifndef printf
#include <stdio.h>
#endif

/*Advance timer by delay, specified in 32:32 format. This should be used to
  resume a recurring timer in a callback routine*/
static __inline void
timer_advance_u64(pc_timer_t *timer, uint64_t delay)
{
    uint64_t int_delay = delay >> 32;
    uint32_t frac_delay = delay & 0xffffffff;

    if (int_delay & 0x0000000080000000ULL)
        int_delay |= 0xffffffff00000000ULL;

    if ((frac_delay + timer->ts_frac) < frac_delay)
            timer->ts_integer++;
    timer->ts_frac += frac_delay;
    timer->ts_integer += int_delay;

    timer_enable(timer);
}

/*Set a timer to the given delay, specified in 32:32 format. This should be used
  when starting a timer*/
static __inline void
timer_set_delay_u64(pc_timer_t *timer, uint64_t delay)
{
    uint64_t int_delay = delay >> 32;
    uint32_t frac_delay = delay & 0xffffffff;

    if (int_delay & 0x0000000080000000ULL)
        int_delay |= 0xffffffff00000000ULL;

    timer->ts_frac = frac_delay;
    timer->ts_integer = int_delay + (uint64_t)tsc;

    timer_enable(timer);
}

/*True if timer currently enabled*/
static __inline int
timer_is_enabled(pc_timer_t *timer)
{
    return !!(timer->flags & TIMER_ENABLED);
}

/*True if timer currently on*/
static __inline int
timer_is_on(pc_timer_t *timer)
{
    return ((timer->flags & TIMER_SPLIT) && (timer->flags & TIMER_ENABLED));
}

/*Return integer timestamp of timer*/
static __inline uint64_t
timer_get_ts_int(pc_timer_t *timer)
{
    return timer->ts_integer;
}

/*Return remaining time before timer expires, in us. If the timer has already
  expired then return 0*/
static __inline uint64_t
timer_get_remaining_us(pc_timer_t *timer)
{
    if (timer->flags & TIMER_ENABLED) {
        int128_t remaining = (((uint128_t)timer->ts_integer << 32) | timer->ts_frac) - ((uint128_t)tsc << 32);

        if (remaining < 0)
            return 0;
        return remaining / TIMER_USEC;
    }

    return 0;
}

/*Return remaining time before timer expires, in 32:32 timestamp format. If the
  timer has already expired then return 0*/
static __inline uint128_t
timer_get_remaining_u64(pc_timer_t *timer)
{
    if (timer->flags & TIMER_ENABLED) {
        int128_t remaining = (((uint128_t)timer->ts_integer << 32) | timer->ts_frac) - ((uint128_t)tsc << 32);

        if (remaining < 0)
            return 0;
        return remaining;
    }

    return 0;
}

/*Set timer callback function*/
static __inline void
timer_set_callback(pc_timer_t *timer, void (*callback)(void *priv))
{
    timer->callback = callback;
}

/*Set timer private data*/
static __inline void
timer_set_p(pc_timer_t *timer, void *priv)
{
    timer->priv = priv;
}

/* The API for big timer periods starts here. */
extern void timer_stop(pc_timer_t *timer);
extern void timer_on_auto(pc_timer_t *timer, double period);

/* Change TSC, taking into account the timers. */
extern void timer_set_new_tsc(uint64_t new_tsc);

#ifdef __cplusplus
}
#endif

#endif /*_TIMER_H_*/
