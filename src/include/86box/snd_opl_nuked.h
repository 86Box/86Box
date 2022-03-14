/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the NukedOPL3 driver.
 *
 * Version:	@(#)snd_opl_nuked.h	1.0.5	2020/07/16
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2017-2020 Fred N. van Kempen.
 *		Copyright 2016-2019 Miran Grca.
 */

#ifndef SOUND_OPL_NUKED_H
#define SOUND_OPL_NUKED_H

extern void *nuked_init(uint32_t sample_rate);
extern void  nuked_close(void *);

extern uint16_t nuked_write_addr(void *, uint16_t port, uint8_t val);
extern void     nuked_write_reg(void *, uint16_t reg, uint8_t v);
extern void     nuked_write_reg_buffered(void *, uint16_t reg, uint8_t v);

extern void nuked_generate(void *, int32_t *buf);
extern void nuked_generate_resampled(void *, int32_t *buf);
extern void nuked_generate_stream(void *, int32_t *sndptr, uint32_t num);

#endif /*SOUND_OPL_NUKED_H*/
