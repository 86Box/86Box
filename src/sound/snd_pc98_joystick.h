/* Digital active-low joystick lines shared by OPN C-BUS boards.
 * The board's PSG Port B bit 6 disconnects the connector; Port A bits 7:6
 * report the interrupt jumpers. Host axes use the existing signed range. */
#ifndef SND_PC98_JOYSTICK_H
#define SND_PC98_JOYSTICK_H

static inline uint8_t
pc98_sound_joystick(uint8_t select, uint8_t irq_bits)
{
    uint8_t value = 0x3fu;
    if (!(select & 0x40u) && JOYSTICK_PRESENT(0, 0)) {
        const joystick_state_t *js = &joystick_state[0][0];
        if (js->axis[1] < -16384) value &= ~0x01u;
        if (js->axis[1] > 16384) value &= ~0x02u;
        if (js->axis[0] < -16384) value &= ~0x04u;
        if (js->axis[0] > 16384) value &= ~0x08u;
        if (js->button[0]) value &= ~0x10u;
        if (js->button[1]) value &= ~0x20u;
    }
    return value | irq_bits;
}

#endif
