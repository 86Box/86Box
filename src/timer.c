#include "ibm.h"
#if 0
//FIXME: Kotori, do we need to keep this? --FvK
#include "sound_opl.h"
#include "adlibgold.h"
#include "sound_pas16.h"
#include "sound_sb.h"
#include "sound_sb_dsp.h"
#include "sound_wss.h"
#endif
#include "timer.h"


#define TIMERS_MAX 64


static struct
{
	int present;
	void (*callback)(void *priv);
	void *priv;
	int *enable;
	int *count;
} timers[TIMERS_MAX];


int TIMER_USEC;
int timers_present = 0;
int timer_one = 1;
	
int timer_count = 0, timer_latch = 0;
int timer_start = 0;


void timer_process(void)
{
	int c;
	int process = 0;
	/*Get actual elapsed time*/
	int diff = timer_latch - timer_count;
	int enable[TIMERS_MAX];

	timer_latch = 0;

        for (c = 0; c < timers_present; c++)
        {
		/* This is needed to avoid timer crashes on hard reset. */
		if ((timers[c].enable == NULL) || (timers[c].count == NULL))
		{
			continue;
		}
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


void timer_update_outstanding(void)
{
	int c;
	timer_latch = 0x7fffffff;
	for (c = 0; c < timers_present; c++)
	{
		if (*timers[c].enable && *timers[c].count < timer_latch)
			timer_latch = *timers[c].count;
	}
	timer_count = timer_latch = (timer_latch + ((1 << TIMER_SHIFT) - 1)) >> TIMER_SHIFT;
}


void timer_reset(void)
{
	pclog("timer_reset\n");
	timers_present = 0;
	timer_latch = timer_count = 0;
}


int timer_add(void (*callback)(void *priv), int *count, int *enable, void *priv)
{
	int i = 0;

	if (timers_present < TIMERS_MAX)
	{
		if (timers_present != 0)
		{
			/* This is the sanity check - it goes through all present timers and makes sure we're not adding a timer that already exists. */
			for (i = 0; i < timers_present; i++)
			{
				if (timers[i].present && (timers[i].callback == callback) && (timers[i].priv == priv) && (timers[i].count == count) && (timers[i].enable == enable))
				{
					return 0;
				}
			}
		}

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
