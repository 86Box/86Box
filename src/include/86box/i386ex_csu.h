/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Intel 386EX Chip-Select Unit.
 *
 * Authors: Nathan Lewis, <git@nrlewis.dev>
 *
 *          Copyright 2026 Nathan Lewis.
 */
#ifndef EMU_I386EX_CSU_H
#define EMU_I386EX_CSU_H

#define I386EX_CSU_CHANNELS 8
#define I386EX_CSU_UCS      7 /* The upper chip select is channel 7. */

struct _mem_mapping_;

typedef struct i386ex_csu_t i386ex_csu_t;

/*
 * Called once per address block a channel selects: with set = 0 for each
 * block of the old decode, then with set = 1 for each block of the new one.
 * is_mem tells memory (26-bit) from I/O (16-bit) blocks. What responds there,
 * and what happens where several channels overlap, is up to the board.
 */
typedef void (*i386ex_cs_func_t)(void *priv, int set, int is_mem, uint32_t base, uint32_t size);

/*
 * A memory device on a channel, which the CSU maps in every block the
 * channel selects: map() adds a mapping of it, remap() moves one map() added
 * before. A block can be bigger than the device, which then repeats.
 */
typedef struct i386ex_cs_mem_t {
    void (*map)(void *priv, struct _mem_mapping_ *mapping, uint32_t base, uint32_t size);
    void (*remap)(void *priv, struct _mem_mapping_ *mapping, uint32_t base, uint32_t size);
    void *priv;
} i386ex_cs_mem_t;

extern void i386ex_csu_attach(i386ex_csu_t *csu, int channel, i386ex_cs_func_t func, void *priv);
extern void i386ex_csu_attach_mem(i386ex_csu_t *csu, int channel, const i386ex_cs_mem_t *mem);
extern void i386ex_csu_handler(i386ex_csu_t *csu, int set);

#ifdef EMU_DEVICE_H
extern const device_t i386ex_csu_device;
#endif

#endif /*EMU_I386EX_CSU_H*/
