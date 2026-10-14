// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef M_AT_IBM_PC700_H
#define M_AT_IBM_PC700_H

#include <stdint.h>

/*
 * The Rapid Resume power controller at ports 48h-49h (52H3381, I/O address
 * map, page 9) is a preprogrammed Philips 83C750, as in IBM US patent
 * 5,530,879. Per the patent, commands reach it as nibbles on SD0-SD3, valid
 * only while SD4 is high, and SD0-SD4 are the only data lines it sees. In the
 * LQKT47A BIOS (upper flash bank, E000:1831) every command is two nibbles
 * except Dh and Eh, which carry a data byte: E000:18E8 sends D 03h and then
 * three E writes holding the minutes-to-wake count, and E000:14BD sends 0Eh
 * to switch the supply off. Bit 7 of the BIOS's first D/E write never reaches
 * the controller, so the length comes from the first nibble alone.
 *
 * Decode one write to port 48h. Returns the command byte when a two-nibble
 * command completes, otherwise -1.
 */
static inline int
ibm_pc700_rapid_decode(uint8_t *command, uint8_t *nibbles, uint8_t val)
{
    if (!(val & 0x10))
        return -1;

    if (*nibbles == 0) {
        *command = (val & 0x0f) << 4;
        *nibbles = 1;
        return -1;
    }

    if ((*command == 0xd0) || (*command == 0xe0)) {
        if (++(*nibbles) == 3)
            *nibbles = 0;
        return -1;
    }

    *command |= val & 0x0f;
    *nibbles = 0;
    return *command;
}

#endif
