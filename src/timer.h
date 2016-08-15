/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#ifndef _TIMER_H_
#define _TIMER_H_

#include "cpu.h"

extern int64_t timer_start;

#define timer_start_period(cycles)                      \
        timer_start = cycles;

#define timer_end_period(cycles) 			\
	do 						\
	{						\
                int64_t diff = timer_start - ((uint64_t) cycles);  \
		timer_count -= diff;			\
                timer_start = cycles;                   \
		if (timer_count <= 0)			\
		{					\
			timer_process();		\
			timer_update_outstanding();	\
		}					\
	} while (0)

#define timer_clock()                                                   \
	do 						                \
	{						                \
                int64_t diff;                                           \
                if (AT)                                                 \
                {                                                       \
                        diff = timer_start - (((uint64_t) cycles) << TIMER_SHIFT);   \
                        timer_start = ((uint64_t) cycles) << TIMER_SHIFT;            \
                }                                                       \
                else                                                    \
                {                                                       \
                        diff = timer_start - (((uint64_t) cycles) * xt_cpu_multi);   \
                        timer_start = ((uint64_t) cycles) * xt_cpu_multi;            \
                }                                                       \
		timer_count -= diff;			                \
		timer_process();		                        \
        	timer_update_outstanding();	                        \
	} while (0)

void timer_process();
void timer_update_outstanding();
void timer_reset();
int timer_add(void (*callback)(void *priv), int64_t *count, int *enable, void *priv);
void timer_set_callback(int timer, void (*callback)(void *priv));

#define TIMER_ALWAYS_ENABLED &timer_one

extern int64_t timer_count;
extern int timer_one;

#define TIMER_SHIFT 6

extern int TIMER_USEC;

#endif /*_TIMER_H_*/
