extern int midi_device_current;

int midi_device_available(int card);
char *midi_device_getname(int card);
struct device_t *midi_device_getdevice(int card);
int midi_device_has_config(int card);
char *midi_device_get_internal_name(int card);
int midi_device_get_from_internal_name(char *s);
void midi_device_init();

typedef struct midi_device_t
{
        void (*play_sysex)(struct midi_device_t* p, uint8_t *sysex, unsigned int len);
        void (*play_msg)(struct midi_device_t* p, uint8_t *msg);
        void (*poll)(struct midi_device_t* p);
        int (*write)(struct midi_device_t* p, uint8_t val);
        void* data;
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