#include <stdint.h>
#include <86box/hdd.h>

extern void hdd_audio_init(void);
extern void hdd_audio_callback(int16_t *buffer, int length);
extern void hdd_audio_seek(hard_disk_t *hdd, uint32_t new_cylinder);