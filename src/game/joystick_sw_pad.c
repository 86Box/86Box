/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of a Side Winder GamePad.
 *
 * Notes:	- Write to 0x201 starts packet transfer (5*N or 15*N bits)
 *		- Currently alternates between Mode A and Mode B (is there
 *		  any way of actually controlling which is used?)
 *		- Windows 9x drivers require Mode B when more than 1 pad
 *		  connected
 *		- Packet preceeded by high data (currently 50us), and
 *		  followed by low data (currently 160us) - timings are
 *		  probably wrong, but good enough for everything I've tried
 *		- Analog inputs are only used to time ID packet request.
 *		  If A0 timing out is followed after ~64us by another 0x201
 *		  write then an ID packet is triggered
 *		- Sidewinder game pad ID is 'H0003'
 *		- ID is sent in Mode A (1 bit per clock), but data bit 2
 *		  must change during ID packet transfer, or Windows 9x
 *		  drivers won't use Mode B. I don't know if it oscillates,
 *		  mirrors the data transfer, or something else - the drivers
 *		  only check that it changes at least 10 times during the
 *		  transfer
 *		- Some DOS stuff will write to 0x201 while a packet is
 *		  being transferred. This seems to be ignored.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2008-2018 Sarah Walker.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/gameport.h>
#include <86box/joystick_sw_pad.h>


typedef struct
{
        pc_timer_t poll_timer;
        int poll_left;
        int poll_clock;
        uint64_t poll_data;
        int poll_mode;

        pc_timer_t trigger_timer;
        int data_mode;
} sw_data;

static void sw_timer_over(void *p)
{
        sw_data *sw = (sw_data *)p;

		sw->poll_clock = !sw->poll_clock;

		if (sw->poll_clock)
		{
				sw->poll_data >>= (sw->poll_mode ? 3 : 1);
				sw->poll_left--;
		}

		if (sw->poll_left == 1 && !sw->poll_clock)
				timer_advance_u64(&sw->poll_timer, TIMER_USEC * 160);
		else if (sw->poll_left)
				timer_advance_u64(&sw->poll_timer, TIMER_USEC * 5);
		else
				timer_disable(&sw->poll_timer);
}

static void sw_trigger_timer_over(void *p)
{
        sw_data *sw = (sw_data *)p;

        timer_disable(&sw->trigger_timer);
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

static void *sw_init(void)
{
        sw_data *sw = (sw_data *)malloc(sizeof(sw_data));
        memset(sw, 0, sizeof(sw_data));

		timer_add(&sw->poll_timer, sw_timer_over, sw, 0);
		timer_add(&sw->trigger_timer, sw_trigger_timer_over, sw, 0);

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

        if (timer_is_enabled(&sw->poll_timer))
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
        int64_t time_since_last = timer_get_remaining_us(&sw->trigger_timer);

        if (!JOYSTICK_PRESENT(0))
                return;

        timer_process();

        if (!sw->poll_left)
        {
                sw->poll_clock = 1;
				timer_set_delay_u64(&sw->poll_timer, TIMER_USEC * 50);

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
                                uint16_t data = 0x3fff;
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

        timer_disable(&sw->trigger_timer);
}

static int sw_read_axis(void *p, int axis)
{
        if (!JOYSTICK_PRESENT(0))
                return AXIS_NOT_PRESENT;

        return 0; /*No analogue support on Sidewinder game pad*/
}

static void sw_a0_over(void *p)
{
        sw_data *sw = (sw_data *)p;

        timer_set_delay_u64(&sw->trigger_timer, TIMER_USEC * 10000);
}

const joystick_if_t joystick_sw_pad = {
    .name = "Microsoft SideWinder Pad",
    .internal_name = "sidewinder_pad",
    .init = sw_init,
    .close = sw_close,
    .read = sw_read,
    .write = sw_write,
    .read_axis = sw_read_axis,
    .a0_over = sw_a0_over,
    .axis_count = 2,
    .button_count = 10,
    .pov_count = 0,
    .max_joysticks = 4,
    .axis_names = { "X axis", "Y axis" },
    .button_names = { "A", "B", "C", "X", "Y", "Z", "L", "R", "Start", "M" },
    .pov_names = { NULL }
};
