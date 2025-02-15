#ifndef SOUND_RESID_H
#define SOUND_RESID_H

#ifdef __cplusplus
extern "C" {
#endif
void   *sid_init(uint8_t type);
void    sid_close(void *priv);
void    sid_reset(void *priv);
uint8_t sid_read(uint16_t addr, void *priv);
void    sid_write(uint16_t addr, uint8_t val, void *priv);
void    sid_fillbuf(int16_t *buf, int len, void *priv);
#ifdef __cplusplus
}
#endif

#endif /*SOUND_RESID_H*/
