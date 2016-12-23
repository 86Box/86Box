#include "ibm.h"
#include "mouse.h"
#include "amstrad.h"
#include "mouse_ps2.h"
#include "mouse_serial.h"
#include "keyboard_olim24.h"

static mouse_t *mouse_list[] =
{
        &mouse_serial_microsoft,
        &mouse_ps2_2_button,
        &mouse_intellimouse,
        &mouse_amstrad,
        &mouse_olim24,
        NULL
};

static mouse_t *cur_mouse;
static void *mouse_p;
int mouse_type = 0;

void mouse_emu_init()
{
        cur_mouse = mouse_list[mouse_type];
        mouse_p = cur_mouse->init();
}

void mouse_emu_close()
{
        if (cur_mouse)
                cur_mouse->close(mouse_p);
        cur_mouse = NULL;
}

void mouse_poll(int x, int y, int z, int b)
{
        if (cur_mouse)
                cur_mouse->poll(x, y, z, b, mouse_p);
}

char *mouse_get_name(int mouse)
{
        if (!mouse_list[mouse])
                return NULL;
        return mouse_list[mouse]->name;
}
int mouse_get_type(int mouse)
{
        return mouse_list[mouse]->type;
}
