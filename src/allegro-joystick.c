#include "allegro-main.h"
#include "plat-joystick.h"
#include "device.h"
#include "gameport.h"

plat_joystick_t plat_joystick_state[MAX_PLAT_JOYSTICKS];
joystick_t joystick_state[MAX_JOYSTICKS];

int joysticks_present;

void joystick_init()
{
        install_joystick(JOY_TYPE_AUTODETECT);
        joysticks_present = num_joysticks;
}
void joystick_close()
{
}
void joystick_poll()
{
        int c, d;
        
        poll_joystick();

        for (c = 0; c < num_joysticks; c++)
        {
                plat_joystick_state[c].a[0] = joy[c].stick[0].axis[0].pos * 256;
                plat_joystick_state[c].a[1] = joy[c].stick[0].axis[1].pos * 256;
                for (d = 0; d < MAX_JOYSTICK_BUTTONS; d++)
                        plat_joystick_state[c].b[d] = joy[c].button[d].b;
        }
        for (c = 0; c < joystick_get_max_joysticks(joystick_type); c++)
        {
                if (c < num_joysticks)
                {
                        for (d = 0; d < joystick_get_axis_count(joystick_type); d++)
                                joystick_state[c].axis[d] = plat_joystick_state[c].a[d];
                        for (d = 0; d < joystick_get_button_count(joystick_type); d++)
                                joystick_state[c].button[d] = plat_joystick_state[c].b[d];
                }
                else
                {
                        for (d = 0; d < joystick_get_axis_count(joystick_type); d++)
                                joystick_state[c].axis[d] = 0;
                        for (d = 0; d < joystick_get_button_count(joystick_type); d++)
                                joystick_state[c].button[d] = 0;
                }
        }
}
