#include <sys/time.h>
#include <time.h>
#include "ibm.h"
#include "nvr.h"

void time_get(char *nvrram)
{
        int c,d;
        uint8_t baknvr[10];
	time_t cur_time;
	struct tm cur_time_tm;

        memcpy(baknvr,nvrram,10);

	cur_time = time(NULL);
	localtime_r(&cur_time, &cur_time_tm);

        d = cur_time_tm.tm_sec % 10;
        c = cur_time_tm.tm_sec / 10;
        nvrram[0] = d | (c << 4);
        d = cur_time_tm.tm_min % 10;
        c = cur_time_tm.tm_min / 10;
        nvrram[2] = d | (c << 4);
        d = cur_time_tm.tm_hour % 10;
        c = cur_time_tm.tm_hour / 10;
        nvrram[4] = d | (c << 4);
        d = cur_time_tm.tm_wday % 10;
        c = cur_time_tm.tm_wday / 10;
        nvrram[6] = d | (c << 4);
        d = cur_time_tm.tm_mday % 10;
        c = cur_time_tm.tm_mday / 10;
        nvrram[7] = d | (c << 4);
        d = cur_time_tm.tm_mon % 10;
        c = cur_time_tm.tm_mon / 10;
        nvrram[8] = d | (c << 4);
        d = cur_time_tm.tm_year % 10;
        c = (cur_time_tm.tm_year / 10) % 10;
        nvrram[9] = d | (c << 4);
        if (baknvr[0] != nvrram[0] ||
            baknvr[2] != nvrram[2] ||
            baknvr[4] != nvrram[4] ||
            baknvr[6] != nvrram[6] ||
            baknvr[7] != nvrram[7] ||
            baknvr[8] != nvrram[8] ||
            baknvr[9] != nvrram[9])
		nvrram[0xA] |= 0x80;
}

