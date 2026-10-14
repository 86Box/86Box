// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef M_AT_IBM_PC700_H
#define M_AT_IBM_PC700_H

#include <stdint.h>
#include <string.h>

/*
 * The Rapid Resume power controller at ports 48h-49h (52H3381, I/O address
 * map, page 9) is a preprogrammed Philips 83C750, as in IBM US patent
 * 5,530,879: an 80C51 with 64 bytes of RAM (83C750/87C750 data sheet). Its
 * program is masked into the chip, so the command set below is inferred from
 * how the BIOS uses it (code locations are offsets into LQKT47A.BIN) and from
 * the patent, which says the CPU can read and change almost every variable in
 * the controller.
 *
 * Per the patent, commands arrive as nibbles on SD0-SD3, valid only while SD4
 * is high, and replies come back one bit at a time on SD0. Every command the
 * BIOS sends (21831h) is two nibbles except Dh and Eh, which carry a data
 * byte; bit 7 of the BIOS's first D/E write never reaches the controller.
 *
 *   00nnnnnn  clear flag n
 *   01nnnnnn  set flag n
 *   10nnnnnn  read flag n
 *   D xx      select variable xx
 *   E xx      write xx to the selected variable and move to the next one
 *
 * The flags are the 80C51's bit-addressable RAM at 20h-27h. The BIOS checks
 * the scheme at 216ACh: it clears flag 0Ah and reads it back as 0, then
 * sets it and reads it back as 1. It sends 4Eh at power-on (217A4h) and
 * 0Eh to switch the supply off (214BDh), so flag 0Eh keeps the power on.
 * 218E8h writes the three-byte minutes-to-wake count with D 03h and E.
 */

/* Flag 0Eh: the supply stays on while it is set. */
#define IBM_PC700_RAPID_POWER 0x0e

/*
 * Flags 30h-33h report why the supply came on (2171Dh-21772h). After 30h the
 * BIOS switches off again if the CMOS bit that its power-off path sets
 * (214ACh) is still set (2178Ah), which suits a return of mains power. After
 * 31h it boots normally, so that is the one reported for the power switch.
 * 32h and 33h are only read with Wake Up on Alarm or ring detection enabled.
 */
#define IBM_PC700_RAPID_WAKE_SWITCH 0x31

typedef struct ibm_pc700_rapid_t {
    uint8_t ram[64];
    uint8_t pointer;
    uint8_t command;
    uint8_t arg;
    uint8_t nibbles;
    uint8_t reply;
} ibm_pc700_rapid_t;

static inline int
ibm_pc700_rapid_flag(const ibm_pc700_rapid_t *rr, uint8_t flag)
{
    return !!(rr->ram[0x20 + ((flag >> 3) & 0x07)] & (1 << (flag & 0x07)));
}

/* State after the power switch turns the supply on. */
static inline void
ibm_pc700_rapid_power_on(ibm_pc700_rapid_t *rr)
{
    memset(rr, 0, sizeof(ibm_pc700_rapid_t));
    rr->ram[0x20 + (IBM_PC700_RAPID_POWER >> 3)] |= 1 << (IBM_PC700_RAPID_POWER & 0x07);
    rr->ram[0x20 + (IBM_PC700_RAPID_WAKE_SWITCH >> 3)] |= 1 << (IBM_PC700_RAPID_WAKE_SWITCH & 0x07);
}

/*
 * Decode one write to port 48h. Returns -1 until a command is complete, then
 * the command byte (00h-FFh), or D00h-EFFh for Dh/Eh with their data byte.
 */
static inline int
ibm_pc700_rapid_decode(ibm_pc700_rapid_t *rr, uint8_t val)
{
    if (!(val & 0x10))
        return -1;

    val &= 0x0f;
    switch (rr->nibbles) {
        case 0:
            rr->command = val << 4;
            rr->nibbles = 1;
            return -1;

        case 1:
            if ((rr->command == 0xd0) || (rr->command == 0xe0)) {
                rr->arg     = val << 4;
                rr->nibbles = 2;
                return -1;
            }
            rr->nibbles = 0;
            return rr->command | val;

        default:
            rr->nibbles = 0;
            return (rr->command << 4) | rr->arg | val;
    }
}

/*
 * Run a command returned by ibm_pc700_rapid_decode(). Returns 1 when it
 * switches the supply off.
 */
static inline int
ibm_pc700_rapid_execute(ibm_pc700_rapid_t *rr, int cmd)
{
    uint8_t *flags = &rr->ram[0x20 + ((cmd >> 3) & 0x07)];
    uint8_t  mask  = 1 << (cmd & 0x07);

    if (cmd >= 0xe00) {
        rr->ram[rr->pointer] = cmd & 0xff;
        rr->pointer          = (rr->pointer + 1) & 0x3f;
    } else if (cmd >= 0xd00)
        rr->pointer = cmd & 0x3f;
    else
        switch (cmd & 0xc0) {
            case 0x00:
                *flags &= ~mask;
                return cmd == IBM_PC700_RAPID_POWER;

            case 0x40:
                *flags |= mask;
                break;

            case 0x80:
                rr->reply = !!(*flags & mask);
                break;

            default:
                /* The BIOS sends C4h after 41h, 43h and 57h once POST is
                   done; what it does is not known. */
                break;
        }

    return 0;
}

#endif
