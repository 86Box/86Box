#include <windows.h>
#include "ibm.h"
#include "nvr.h"

void time_sleep(int count)
{
	Sleep(count);
}

void time_get(char *nvrram)
{
	SYSTEMTIME systemtime;
	int c, d;
	uint8_t baknvr[10];

        memcpy(baknvr,nvrram,10);
        GetLocalTime(&systemtime);

        d = systemtime.wSecond % 10;
        c = systemtime.wSecond / 10;
        nvrram[0] = d | (c << 4);
        d = systemtime.wMinute % 10;
        c = systemtime.wMinute / 10;
        nvrram[2] = d | (c << 4);
        d = systemtime.wHour % 10;
        c = systemtime.wHour / 10;
        nvrram[4] = d | (c << 4);
        d = systemtime.wDayOfWeek % 10;
        c = systemtime.wDayOfWeek / 10;
        nvrram[6] = d | (c << 4);
        d = systemtime.wDay % 10;
        c = systemtime.wDay / 10;
        nvrram[7] = d | (c << 4);
        d = systemtime.wMonth % 10;
        c = systemtime.wMonth / 10;
        nvrram[8] = d | (c << 4);
        d = systemtime.wYear % 10;
        c = (systemtime.wYear / 10) % 10;
        nvrram[9] = d | (c << 4);
        if (baknvr[0] != nvrram[0] ||
            baknvr[2] != nvrram[2] ||
            baknvr[4] != nvrram[4] ||
            baknvr[6] != nvrram[6] ||
            baknvr[7] != nvrram[7] ||
            baknvr[8] != nvrram[8] ||
            baknvr[9] != nvrram[9])
		nvrram[0xA]|=0x80;
}
