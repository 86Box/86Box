#ifndef _TIMER_H_
#define _TIMER_H_

extern uint64_t tsc;

/* Maximum period, currently 1 second. */
#define MAX_USEC64    1000000ULL
#define MAX_USEC      1000000.0

#define TIMER_PROCESS 4
#define TIMER_SPLIT   2
#define TIMER_ENABLED 1

#pragma pack(push, 1)
typedef struct ts_struct_t {
    uint32_t frac;
    uint32_t integer;
} ts_struct_t;
#pragma pack(pop)

typedef union ts_t {
    uint64_t    ts64;
    ts_struct_t ts32;
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
typedef struct pc_timer_t {
#ifdef USE_PCEM_TIMER
    uint32_t ts_integer;
    uint32_t ts_frac;
#else
    ts_t ts;
#endif
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
extern uint32_t timer_target;

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
#define TIMER_LESS_THAN(a, b) ((int64_t) ((a)->ts.ts64 - (b)->ts.ts64) <= 0)
/*True if timer a expires before 32 bit integer timestamp b*/
#define TIMER_LESS_THAN_VAL(a, b) ((int32_t) ((a)->ts.ts32.integer - (b)) <= 0)
/*True if 32 bit integer timestamp a expires before 32 bit integer timestamp b*/
#define TIMER_VAL_LESS_THAN_VAL(a, b) ((int32_t) ((a) - (b)) <= 0)

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
    timer->ts.ts64         = 0ULL;
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

/*True if timer currently on*/
static __inline int
timer_is_on(pc_timer_t *timer)
{
    return ((timer->flags & TIMER_SPLIT) && (timer->flags & TIMER_ENABLED));
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
        remaining = (int64_t) (timer->ts.ts64 - (uint64_t) (tsc << 32));

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
        remaining = (int64_t) (timer->ts.ts64 - (uint64_t) (tsc << 32));

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
