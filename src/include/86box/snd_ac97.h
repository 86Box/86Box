/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for AC'97 audio emulation.
 *
 *
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2021 RichardG.
 */
#ifndef EMU_SND_AC97_H
# define EMU_SND_AC97_H


typedef struct {
    uint32_t	vendor_id;
    uint8_t	codec_id, regs[128];
} ac97_codec_t;


extern uint8_t	ac97_codec_read(ac97_codec_t *dev, uint8_t reg);
extern void	ac97_codec_write(ac97_codec_t *dev, uint8_t reg, uint8_t val);
extern void	ac97_codec_reset(void *priv);
extern void	ac97_codec_getattn(void *priv, uint8_t reg, int *l, int *r);
extern uint32_t	ac97_codec_getrate(void *priv, uint8_t reg);

extern void	ac97_via_set_slot(void *priv, int slot, int irq_pin);
extern uint8_t	ac97_via_read_status(void *priv, uint8_t modem);
extern void	ac97_via_write_control(void *priv, uint8_t modem, uint8_t val);
extern void	ac97_via_remap_audio_sgd(void *priv, uint16_t new_io_base, uint8_t enable);
extern void	ac97_via_remap_modem_sgd(void *priv, uint16_t new_io_base, uint8_t enable);
extern void	ac97_via_remap_audio_codec(void *priv, uint16_t new_io_base, uint8_t enable);
extern void	ac97_via_remap_modem_codec(void *priv, uint16_t new_io_base, uint8_t enable);


#ifdef EMU_DEVICE_H
extern ac97_codec_t	**ac97_codec, **ac97_modem_codec;
extern int		ac97_codec_count, ac97_modem_codec_count,
			ac97_codec_id, ac97_modem_codec_id;

extern const device_t	ad1881_device;
extern const device_t	alc100_device;
extern const device_t	cs4297_device;
extern const device_t	cs4297a_device;
extern const device_t	wm9701a_device;

extern const device_t	ac97_via_device;
#endif


#endif
