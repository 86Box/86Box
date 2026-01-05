#ifndef SOUND_AZT2316A_H
#define SOUND_AZT2316A_H

extern void azt2316a_enable_wss(uint8_t enable, void *priv);
extern void aztpr16_update_mixer(void *priv);
extern void aztpr16_wss_mode(uint8_t mode, void *priv);

#endif /*SOUND_AZT2316A*/
