/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          C interface to the ymfm YM2203 (OPN) and YM2608 (OPNA) cores,
 *          for the PC-9801-26K and PC-9801-86 sound boards.
 *
 *          86Box already builds and links ymfm for the OPL family; this
 *          adds the two OPN parts without disturbing snd_opl_ymfm.cpp,
 *          which is specialised around the OPL chips' single-timer,
 *          no-ADPCM shape.
 */
#ifndef EMU_SND_PC98_OPN_H
#define EMU_SND_PC98_OPN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pc98_opn_t pc98_opn_t;

typedef enum pc98_opn_type_t {
    PC98_OPN_YM2203 = 0, /* PC-9801-26K */
    PC98_OPN_YM2608 = 1  /* PC-9801-86  */
} pc98_opn_type_t;

/* The YM2608's ADPCM-B RAM.  The -86 carries 256 KiB of it. */
#define PC98_OPNA_RAM_SIZE 0x40000u

/*
 * irq is called whenever the chip's own interrupt line changes.  The board
 * decides which C-BUS INT level that becomes.
 */
pc98_opn_t *pc98_opn_create(pc98_opn_type_t type, uint32_t clock,
                            void (*irq)(void *priv, int level), void *priv);
void        pc98_opn_destroy(pc98_opn_t *chip);
void        pc98_opn_reset(pc98_opn_t *chip);

/* Register file: addr is the chip-relative offset, 0-1 on OPN, 0-3 on OPNA. */
uint8_t pc98_opn_read(pc98_opn_t *chip, uint32_t addr);
void    pc98_opn_write(pc98_opn_t *chip, uint32_t addr, uint8_t data);

/*
 * The ADPCM-A sample ROM (YM2608 only, 8 KiB, ym2608_adpcm_rom.bin).  The
 * chip's rhythm section is silent without it, so the board loads it and
 * hands it over here; the buffer must outlive the chip.
 */
void pc98_opn_set_adpcm_rom(pc98_opn_t *chip, const uint8_t *rom, uint32_t len);

/* Add `frames` stereo frames at SOUND_FREQ into buf, which is interleaved. */
void pc98_opn_generate(pc98_opn_t *chip, int32_t *buf, uint32_t frames);

/* OPN port A/B, the joystick and jumper lines on both boards. */
void pc98_opn_set_port_a(pc98_opn_t *chip, uint8_t value);
uint8_t pc98_opn_get_port_b(const pc98_opn_t *chip);

#ifdef __cplusplus
}
#endif

#endif /*EMU_SND_PC98_OPN_H*/
