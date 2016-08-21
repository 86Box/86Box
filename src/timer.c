/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include "ibm.h"

/*#include "sound_opl.h"
#include "adlibgold.h"
#include "sound_pas16.h"
#include "sound_sb.h"
#include "sound_sb_dsp.h"
#include "sound_wss.h"*/
#include "timer.h"

#define TIMERS_MAX 32

int TIMER_USEC;

static struct
{
	int present;
	void (*callback)(void *priv);
	void *priv;
	int64_t *enable;
	int64_t *count;
} timers[TIMERS_MAX];

int timers_present = 0;
int64_t timer_one = 1;
	
int64_t timer_count = 0, timer_latch = 0;
int64_t timer_start = 0;

void timer_process()
{
	int c;
	int retry;
	int process = 0;
	/*Get actual elapsed time*/
	int64_t diff = timer_latch - timer_count;
	int enable[TIMERS_MAX];

	timer_latch = 0;

        for (c = 0; c < timers_present; c++)
        {
                enable[c] = *timers[c].enable;
                if (*timers[c].enable)
                {
                        *timers[c].count = *timers[c].count - diff;
                        if (*timers[c].count <= 0)
                                process = 1;
                }
        }
        
        if (!process)
                return;

        while (1)
        {
                int lowest = 1, lowest_c;
                
                for (c = 0; c < timers_present; c++)
                {
                        if (enable[c])
                        {
                                if (*timers[c].count < lowest)
                                {
                                        lowest = *timers[c].count;
                                        lowest_c = c;
                                }
                        }
                }
                
                if (lowest > 0)
                        break;

                timers[lowest_c].callback(timers[lowest_c].priv);
                enable[lowest_c] = *timers[lowest_c].enable;
        }              
}

void timer_update_outstanding()
{
	int c;
	timer_latch = 0x7fffffffffffffff;
	for (c = 0; c < timers_present; c++)
	{
		if (*timers[c].enable && *timers[c].count < timer_latch)
			timer_latch = *timers[c].count;
	}
	timer_count = timer_latch = (timer_latch + ((1 << TIMER_SHIFT) - 1)) >> TIMER_SHIFT;
}

void timer_reset()
{
	pclog("timer_reset\n");
	timers_present = 0;
	timer_latch = timer_count = 0;
//	timer_process();
}

int timer_add(void (*callback)(void *priv), int64_t *count, int64_t *enable, void *priv)
{
	if (timers_present < TIMERS_MAX)
	{
//		pclog("timer_add : adding timer %i\n", timers_present);
		timers[timers_present].present = 1;
		timers[timers_present].callback = callback;
		timers[timers_present].priv = priv;
		timers[timers_present].count = count;
		timers[timers_present].enable = enable;
		timers_present++;
		return timers_present - 1;
	}
	return -1;
}

void timer_set_callback(int timer, void (*callback)(void *priv))
{
	timers[timer].callback = callback;
}
