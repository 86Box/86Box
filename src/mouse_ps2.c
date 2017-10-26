#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "ibm.h"
#include "keyboard_at.h"
#include "mouse.h"
#include "plat_mouse.h"


#define MOUSE_ENABLE 0x20
#define MOUSE_SCALE  0x10


enum {
    MOUSE_STREAM,
    MOUSE_REMOTE,
    MOUSE_ECHO
};

typedef struct {
    int mode;

    uint8_t flags;
    uint8_t resolution;
    uint8_t sample_rate;

    uint8_t command;

    int cd;

    int x, y, z, b;

    int is_intellimouse;
    int intellimouse_mode;

    uint8_t last_data[6];
} mouse_ps2_t;


int mouse_scan = 0;


static void
mouse_ps2_write(uint8_t val, void *priv)
{
    mouse_ps2_t *ms = (mouse_ps2_t *)priv;
	
    if (ms->cd) {
	ms->cd = 0;
	switch (ms->command) {
		case 0xe8: /*Set mouse resolution*/
			ms->resolution = val;
			keyboard_at_adddata_mouse(0xfa);
			break;
			
		case 0xf3: /*Set sample rate*/
			ms->sample_rate = val;
			keyboard_at_adddata_mouse(0xfa);
			break;

		default:
			keyboard_at_adddata_mouse(0xfc);
	}
    } else {
	uint8_t temp;

	ms->command = val;
	switch (ms->command) {
		case 0xe6: /*Set scaling to 1:1*/
			ms->flags &= ~MOUSE_SCALE;
			keyboard_at_adddata_mouse(0xfa);
			break;

		case 0xe7: /*Set scaling to 2:1*/
			ms->flags |= MOUSE_SCALE;
			keyboard_at_adddata_mouse(0xfa);
			break;

		case 0xe8: /*Set mouse resolution*/
			ms->cd = 1;
			keyboard_at_adddata_mouse(0xfa);
			break;

		case 0xe9: /*Status request*/
			keyboard_at_adddata_mouse(0xfa);
			temp = ms->flags;
			if (mouse_buttons & 1)
				temp |= 1;
			if (mouse_buttons & 2)
				temp |= 2;
			if (mouse_buttons & 4)
				temp |= 3;
			keyboard_at_adddata_mouse(temp);
			keyboard_at_adddata_mouse(ms->resolution);
			keyboard_at_adddata_mouse(ms->sample_rate);
			break;

		case 0xf2: /*Read ID*/
			keyboard_at_adddata_mouse(0xfa);
			if (ms->intellimouse_mode)
				keyboard_at_adddata_mouse(0x03);
			else
				keyboard_at_adddata_mouse(0x00);
			break;

		case 0xf3: /*Set command mode*/
			ms->cd = 1;
			keyboard_at_adddata_mouse(0xfa);
			break;

		case 0xf4: /*Enable*/
			ms->flags |= MOUSE_ENABLE;
			keyboard_at_adddata_mouse(0xfa);
			break;

		case 0xf5: /*Disable*/
			ms->flags &= ~MOUSE_ENABLE;
			keyboard_at_adddata_mouse(0xfa);
			break;

		case 0xff: /*Reset*/
			ms->mode  = MOUSE_STREAM;
			ms->flags = 0;
			ms->intellimouse_mode = 0;
			keyboard_at_adddata_mouse(0xfa);
			keyboard_at_adddata_mouse(0xaa);
			keyboard_at_adddata_mouse(0x00);
			break;

		default:
			keyboard_at_adddata_mouse(0xfe);
	}
    }

    if (ms->is_intellimouse) {
	int c;

	for (c = 0; c < 5; c++)	
		ms->last_data[c] = ms->last_data[c+1];
	ms->last_data[5] = val;

	if (ms->last_data[0] == 0xf3 && ms->last_data[1] == 0xc8 &&
	    ms->last_data[2] == 0xf3 && ms->last_data[3] == 0x64 &&
	    ms->last_data[4] == 0xf3 && ms->last_data[5] == 0x50)
		ms->intellimouse_mode = 1;
    }
}


static uint8_t
mouse_ps2_poll(int x, int y, int z, int b, void *priv)
{
    mouse_ps2_t *ms = (mouse_ps2_t *)priv;
    uint8_t packet[3] = {0x08, 0, 0};

    if (!x && !y && !z && b == ms->b) return(0xff);

    if (! mouse_scan) return(0xff);

    ms->x += x;
    ms->y -= y;
    ms->z -= z;
    if (ms->mode == MOUSE_STREAM && (ms->flags & MOUSE_ENABLE) &&
	((mouse_queue_end - mouse_queue_start) & 0xf) < 13) {
	ms->b = b;
//	pclog("Send packet : %i %i\n", ps2_x, ps2_y);
	if (ms->x > 255)
		ms->x = 255;
	if (ms->x < -256)
		ms->x = -256;
	if (ms->y > 255)
		ms->y = 255;
	if (ms->y < -256)
		ms->y = -256;
	if (ms->z < -8)
		ms->z = -8;
	if (ms->z > 7)
		ms->z = 7;
	if (ms->x < 0)
		packet[0] |= 0x10;
	if (ms->y < 0)
		packet[0] |= 0x20;
	if (mouse_buttons & 1)
		packet[0] |= 1;
	if (mouse_buttons & 2)
		packet[0] |= 2;
	if ((mouse_buttons & 4) &&
	    (mouse_get_type(mouse_type) & MOUSE_TYPE_3BUTTON)) packet[0] |= 4;
	packet[1] = ms->x & 0xff;
	packet[2] = ms->y & 0xff;

	keyboard_at_adddata_mouse(packet[0]);
	keyboard_at_adddata_mouse(packet[1]);
	keyboard_at_adddata_mouse(packet[2]);
	if (ms->intellimouse_mode)
		keyboard_at_adddata_mouse(ms->z);

	ms->x = ms->y = ms->z = 0;		
    }

    return(0);
}


/* We also get called from the various machines. */
void *
mouse_ps2_init(void *arg)
{
    mouse_ps2_t *ms = (mouse_ps2_t *)malloc(sizeof(mouse_ps2_t));
    mouse_t *info = (mouse_t *)arg;

    memset(ms, 0x00, sizeof(mouse_ps2_t));

    ms->cd = 0;
    ms->flags = 0;
    ms->mode = MOUSE_STREAM;
    if ((info != NULL) && (info->type & MOUSE_TYPE_3BUTTON))
	ms->is_intellimouse = 1;

    keyboard_at_set_mouse(mouse_ps2_write, ms);

    return(ms);
}


void
mouse_ps2_close(void *priv)
{
    mouse_ps2_t *ms = (mouse_ps2_t *)priv;

    free(ms);
}


mouse_t mouse_ps2_2button = {
    "Standard 2-button mouse (PS/2)",
    "ps2",
    MOUSE_TYPE_PS2,
    (void *(*)(mouse_t *))mouse_ps2_init,
    mouse_ps2_close,
    mouse_ps2_poll
};

mouse_t mouse_ps2_intellimouse = {
    "Microsoft Intellimouse (PS/2)",
    "intellimouse",
    MOUSE_TYPE_PS2 | MOUSE_TYPE_3BUTTON,
    (void *(*)(mouse_t *))mouse_ps2_init,
    mouse_ps2_close,
    mouse_ps2_poll
};
