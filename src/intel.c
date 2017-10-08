/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "ibm.h"
#include "cpu/cpu.h"
#include "io.h"
#include "mem.h"
#include "pit.h"
#include "timer.h"
#include "intel.h"


uint8_t batman_brdconfig(uint16_t port, void *p)
{
        switch (port)
        {
                case 0x73:
                return 0xff;
                case 0x75:
                return 0xdf;
        }
        return 0;
}

static uint16_t batman_timer_latch;
static int64_t batman_timer = 0;
static void batman_timer_over(void *p)
{
        batman_timer = 0;
}

static void batman_timer_write(uint16_t addr, uint8_t val, void *p)
{
        if (addr & 1)
                batman_timer_latch = (batman_timer_latch & 0xff) | (val << 8);
        else
                batman_timer_latch = (batman_timer_latch & 0xff00) | val;
        batman_timer = batman_timer_latch * TIMER_USEC;
}

static uint8_t batman_timer_read(uint16_t addr, void *p)
{
        uint16_t batman_timer_latch;
        
        cycles -= (int)PITCONST;
        
        timer_clock();

        if (batman_timer < 0)
                return 0;
        
        batman_timer_latch = batman_timer / TIMER_USEC;

        if (addr & 1)
                return batman_timer_latch >> 8;
        return batman_timer_latch & 0xff;
}

void intel_batman_init()
{
        io_sethandler(0x0073, 0x0001, batman_brdconfig, NULL, NULL, NULL, NULL, NULL, NULL);
        io_sethandler(0x0075, 0x0001, batman_brdconfig, NULL, NULL, NULL, NULL, NULL, NULL);

        io_sethandler(0x0078, 0x0002, batman_timer_read, NULL, NULL, batman_timer_write, NULL, NULL, NULL);
        timer_add(batman_timer_over, &batman_timer, &batman_timer, NULL);
}
