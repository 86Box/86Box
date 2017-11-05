/*Sidewinder game pad notes :
        
        - Write to 0x201 starts packet transfer (5*N or 15*N bits)
        - Currently alternates between Mode A and Mode B (is there any way of
          actually controlling which is used?)
          - Windows 9x drivers require Mode B when more than 1 pad connected
        - Packet preceeded by high data (currently 50us), and followed by low
          data (currently 160us) - timings are probably wrong, but good enough
          for everything I've tried
        - Analogue inputs are only used to time ID packet request. If A0 timing
          out is followed after ~64us by another 0x201 write then an ID packet
          is triggered
        - Sidewinder game pad ID is 'H0003'
        - ID is sent in Mode A (1 bit per clock), but data bit 2 must change
          during ID packet transfer, or Windows 9x drivers won't use Mode B. I
          don't know if it oscillates, mirrors the data transfer, or something
          else - the drivers only check that it changes at least 10 times during
          the transfer
        - Some DOS stuff will write to 0x201 while a packet is being transferred.
          This seems to be ignored.
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../device.h"
#include "../timer.h"
#include "../plat_joystick.h"
#include "gameport.h"
#include "joystick_sw_pad.h"


typedef struct
{
        int64_t poll_time;
        int64_t poll_left;
        int64_t poll_clock;
        uint64_t poll_data;
        int64_t poll_mode;
                
        int64_t trigger_time;
        int64_t data_mode;
} sw_data;

static void sw_timer_over(void *p)
{
        sw_data *sw = (sw_data *)p;
        
        while (sw->poll_time <= 0 && sw->poll_left)
        {
                sw->poll_clock = !sw->poll_clock;
        
                if (sw->poll_clock)
                {
                        sw->poll_data >>= (sw->poll_mode ? 3 : 1);
                        sw->poll_left--;
                }

                if (sw->poll_left == 1 && !sw->poll_clock)
                        sw->poll_time += TIMER_USEC * 160LL;
                else if (sw->poll_left)
                        sw->poll_time += TIMER_USEC * 5;
                else
                        sw->poll_time = 0;
        }
        
        if (!sw->poll_left)
                sw->poll_time = 0;
}

static void sw_trigger_timer_over(void *p)
{
        sw_data *sw = (sw_data *)p;

        sw->trigger_time = 0;
}

static int sw_parity(uint16_t data)
{
        int bits_set = 0;
        
        while (data)
        {
                bits_set++;
                data &= (data - 1);
        }
        
        return bits_set & 1;
}

static void *sw_init()
{
        sw_data *sw = (sw_data *)malloc(sizeof(sw_data));
        memset(sw, 0, sizeof(sw_data));

        timer_add(sw_timer_over, &sw->poll_time, &sw->poll_time, sw);
        timer_add(sw_trigger_timer_over, &sw->trigger_time, &sw->trigger_time, sw);
                
        return sw;
}

static void sw_close(void *p)
{
        sw_data *sw = (sw_data *)p;
        
        free(sw);
}

static uint8_t sw_read(void *p)
{
        sw_data *sw = (sw_data *)p;
        uint8_t temp = 0;

        if (!JOYSTICK_PRESENT(0))
                return 0xff;

        if (sw->poll_time)
        {        
                if (sw->poll_clock)
                        temp |= 0x10;
        
                if (sw->poll_mode)
                        temp |= (sw->poll_data & 7) << 5;
                else
                {
                        temp |= ((sw->poll_data & 1) << 5) | 0xc0;
                        if (sw->poll_left > 31 && !(sw->poll_left & 1))
                                temp &= ~0x80;
                }
        }
        else
                temp |= 0xf0;

        return temp;
}

static void sw_write(void *p)
{
        sw_data *sw = (sw_data *)p;
        int time_since_last = sw->trigger_time / TIMER_USEC;

        if (!JOYSTICK_PRESENT(0))
                return;
        
        timer_process();

        if (!sw->poll_left)
        {
                sw->poll_clock = 1;
                sw->poll_time = TIMER_USEC * 50;
                
                if (time_since_last > 9900 && time_since_last < 9940)
                {
                        sw->poll_mode = 0;
                        sw->poll_left = 49;
                        sw->poll_data = 0x2400ull | (0x1830ull << 15) | (0x19b0ull << 30);                
                }
                else
                {
                        int c;

                        sw->poll_mode = sw->data_mode;
                        sw->data_mode = !sw->data_mode;
                        
                        if (sw->poll_mode)
                        {
                                sw->poll_left = 1;
                                sw->poll_data = 7;
                        }
                        else
                        {
                                sw->poll_left = 1;
                                sw->poll_data = 1;
                        }

                        for (c = 0; c < 4; c++)
                        {
                                uint64_t data = 0x3fff;
                                int b;
                                
                                if (!JOYSTICK_PRESENT(c))
                                        break;

                                if (joystick_state[c].axis[1] < -16383)
                                        data &= ~1;
                                if (joystick_state[c].axis[1] > 16383)
                                        data &= ~2;
                                if (joystick_state[c].axis[0] > 16383)
                                        data &= ~4;
                                if (joystick_state[c].axis[0] < -16383)
                                        data &= ~8;

                                for (b = 0; b < 10; b++)
                                {
                                        if (joystick_state[c].button[b])
                                                data &= ~(1 << (b + 4));
                                }

                                if (sw_parity(data))
                                        data |= 0x4000;

                                if (sw->poll_mode)
                                {
                                        sw->poll_left += 5;
                                        sw->poll_data |= (data << (c*15 + 3));
                                }
                                else
                                {
                                        sw->poll_left += 15;
                                        sw->poll_data |= (data << (c*15 + 1));
                                }
                        }
                }
        }
        
        sw->trigger_time = 0;
        
        timer_update_outstanding();
}

static int sw_read_axis(void *p, int axis)
{
        if (!JOYSTICK_PRESENT(0))
                return AXIS_NOT_PRESENT;
                
        return 0LL; /*No analogue support on Sidewinder game pad*/
}

static void sw_a0_over(void *p)
{
        sw_data *sw = (sw_data *)p;

        sw->trigger_time = TIMER_USEC * 10000;
}
        
joystick_if_t joystick_sw_pad =
{
        "Microsoft SideWinder Pad",
        sw_init,
        sw_close,
        sw_read,
        sw_write,
        sw_read_axis,
        sw_a0_over,
        4,
        2,
        10,
	0,
        {"X axis", "Y axis"},
        {"A", "B", "C", "X", "Y", "Z", "L", "R", "Start", "M"}
};
