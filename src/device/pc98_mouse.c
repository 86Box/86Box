/*
 * PC-98 two-button bus mouse counter and latch wiring.
 * Port C: PC7 latches and clears counters on its rising edge; PC6 selects Y;
 * PC5 selects the high nibble; PC4 inhibits the periodic INT6 (IRQ13).
 * Port A: active-low left/right buttons on PA7/PA5; PA6 is an unused button
 * input pulled high, PA4 is low, PA3..0 carry the selected counter nibble.
 * The counter continues running while the guest reads the latched snapshot.
 */
#include <string.h>
#include <86box/pc98_mouse.h>

void
pc98_mouse_reset(pc98_mouse_t *m)
{
    memset(m, 0, sizeof(*m));
    m->control = 0x10; /* interrupts inhibited until firmware configures PPI */
}

void
pc98_mouse_motion(pc98_mouse_t *m, int x, int y, unsigned buttons)
{
    /* The physical quadrature counters are eight bits and wrap at 256. */
    m->x = (uint8_t)(m->x + (uint8_t)x);
    m->y = (uint8_t)(m->y + (uint8_t)y);
    m->buttons = buttons & 3u;
}

void
pc98_mouse_control(pc98_mouse_t *m, uint8_t control)
{
    if ((control & 0x80u) && !(m->control & 0x80u)) {
        m->latched_x = m->x;
        m->latched_y = m->y;
        m->x = m->y = 0;
    }
    m->control = control;
}

uint8_t
pc98_mouse_data(const pc98_mouse_t *m)
{
    uint8_t counter;
    uint8_t buttons = 0xe0;

    if (m->buttons & 1u) buttons &= (uint8_t)~0x80u;
    if (m->buttons & 2u) buttons &= (uint8_t)~0x20u;
    if (m->control & 0x40u)
        counter = (m->control & 0x80u) ? m->latched_y : m->y;
    else
        counter = (m->control & 0x80u) ? m->latched_x : m->x;
    if (m->control & 0x20u) counter >>= 4;
    return buttons | (counter & 0x0fu);
}
