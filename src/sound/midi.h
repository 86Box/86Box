#ifndef EMU_SOUND_MIDI_H
# define EMU_SOUND_MIDI_H


extern int midi_device_current;


int midi_device_available(int card);
char *midi_device_getname(int card);
#ifdef EMU_DEVICE_H
device_t *midi_device_getdevice(int card);
#endif
int midi_device_has_config(int card);
char *midi_device_get_internal_name(int card);
int midi_device_get_from_internal_name(char *s);
void midi_device_init();

typedef struct midi_device_t
{
        void (*play_sysex)(uint8_t *sysex, unsigned int len);
        void (*play_msg)(uint8_t *msg);
        void (*poll)();
        int (*write)(uint8_t val);
} midi_device_t;

void midi_init(midi_device_t* device);
void midi_close();
void midi_write(uint8_t val);
void midi_poll();

#if 0
#ifdef _WIN32
#define SYSTEM_MIDI_NAME "Windows MIDI"
#define SYSTEM_MIDI_INTERNAL_NAME "windows_midi"
#else
#define SYSTEM_MIDI_NAME "System MIDI"
#define SYSTEM_MIDI_INTERNAL_NAME "system_midi"
#endif
#else
#define SYSTEM_MIDI_NAME "System MIDI"
#define SYSTEM_MIDI_INTERNAL_NAME "system_midi"
#endif


#endif	/*EMU_SOUND_MIDI_H*/
