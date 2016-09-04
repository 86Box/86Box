#include "timer.h"

void sound_add_handler(void (*get_buffer)(int32_t *buffer, int len, void *p), void *p);

extern int sbtype;

extern int sound_card_current;

int sound_card_available(int card);
char *sound_card_getname(int card);
struct device_t *sound_card_getdevice(int card);
int sound_card_has_config(int card);
void sound_card_init();
void sound_set_cd_volume(unsigned int vol_l, unsigned int vol_r);

#define CD_FREQ 44100
#define CD_BUFLEN (CD_FREQ / 10)

extern int sound_pos_global;
void sound_speed_changed();
