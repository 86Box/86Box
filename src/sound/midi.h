#ifndef EMU_SOUND_MIDI_H
# define EMU_SOUND_MIDI_H


#define SYSEX_SIZE 8192

extern uint8_t MIDI_InSysexBuf[SYSEX_SIZE];
extern uint8_t MIDI_evt_len[256];

extern int midi_device_current;
extern int midi_input_device_current;

extern void (*input_msg)(void *p, uint8_t *msg);
extern int (*input_sysex)(void *p, uint8_t *buffer, uint32_t len, int abort);
extern void *midi_in_p;

int midi_device_available(int card);
int midi_in_device_available(int card);
char *midi_device_getname(int card);
char *midi_in_device_getname(int card);
#ifdef EMU_DEVICE_H
const device_t *midi_device_getdevice(int card);
const device_t *midi_in_device_getdevice(int card);
#endif
int midi_device_has_config(int card);
int midi_in_device_has_config(int card);
char *midi_device_get_internal_name(int card);
char *midi_in_device_get_internal_name(int card);
int midi_device_get_from_internal_name(char *s);
int midi_in_device_get_from_internal_name(char *s);
void midi_device_init();
void midi_in_device_init();


typedef struct midi_device_t
{
        void (*play_sysex)(uint8_t *sysex, unsigned int len);
        void (*play_msg)(uint8_t *msg);
        void (*poll)();
        int (*write)(uint8_t val);
} midi_device_t;

typedef struct midi_t
{
    uint8_t midi_rt_buf[8], midi_cmd_buf[8],
	    midi_status, midi_sysex_data[SYSEX_SIZE];
    int midi_cmd_pos, midi_cmd_len, midi_cmd_r,
		midi_realtime, thruchan, midi_clockout;
    unsigned int midi_sysex_start, midi_sysex_delay,
		 midi_pos;
		 midi_device_t *m_out_device, *m_in_device;
} midi_t;

extern midi_t *midi, *midi_in;

void midi_init(midi_device_t* device);
void midi_in_init(midi_device_t* device, midi_t **mididev);
void midi_close();
void midi_in_close(void);
void midi_raw_out_rt_byte(uint8_t val);
void midi_raw_out_thru_rt_byte(uint8_t val);
void midi_raw_out_byte(uint8_t val);
void midi_clear_buffer(void);
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

#define MIDI_INPUT_NAME "MIDI Input Device"
#define MIDI_INPUT_INTERNAL_NAME "midi_in"

#endif	/*EMU_SOUND_MIDI_H*/
