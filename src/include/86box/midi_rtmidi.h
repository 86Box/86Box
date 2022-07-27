#ifndef EMU_SOUND_RTMIDI_H
#define EMU_SOUND_RTMIDI_H

#ifdef __cplusplus
extern "C" {
#endif

extern int  rtmidi_out_get_num_devs(void);
extern void rtmidi_out_get_dev_name(int num, char *s);
extern int  rtmidi_in_get_num_devs(void);
extern void rtmidi_in_get_dev_name(int num, char *s);

#ifdef __cplusplus
}
#endif

#endif /*EMU_SOUND_RTMIDI*/
