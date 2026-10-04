/* PC-98 motherboard mouse counters, multiplexed through the mouse 8255. */
#ifndef EMU_PC98_MOUSE_H
#define EMU_PC98_MOUSE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pc98_mouse_t {
    uint8_t x, y;
    uint8_t latched_x, latched_y;
    uint8_t buttons;
    uint8_t control;
    uint8_t divider;
} pc98_mouse_t;

void pc98_mouse_reset(pc98_mouse_t *mouse);
void pc98_mouse_motion(pc98_mouse_t *mouse, int x, int y, unsigned buttons);
void pc98_mouse_control(pc98_mouse_t *mouse, uint8_t control);
uint8_t pc98_mouse_data(const pc98_mouse_t *mouse);

#ifdef __cplusplus
}
#endif

#endif
