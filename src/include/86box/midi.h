#ifndef EMU_SOUND_MIDI_H
#define EMU_SOUND_MIDI_H

#define SYSEX_SIZE 8192

extern uint8_t MIDI_InSysexBuf[SYSEX_SIZE];
extern uint8_t MIDI_evt_len[256];

extern int midi_output_device_current;
extern int midi_input_device_current;

extern void (*input_msg)(void *p, uint8_t *msg, uint32_t len);
extern int (*input_sysex)(void *p, uint8_t *buf, uint32_t len, int abort);
extern void *midi_in_p;

extern int midi_out_device_available(int card);
extern int midi_in_device_available(int card);
#ifdef EMU_DEVICE_H
const device_t *midi_out_device_getdevice(int card);
const device_t *midi_in_device_getdevice(int card);
#endif
extern int   midi_out_device_has_config(int card);
extern int   midi_in_device_has_config(int card);
extern char *midi_out_device_get_internal_name(int card);
extern char *midi_in_device_get_internal_name(int card);
extern int   midi_out_device_get_from_internal_name(char *s);
extern int   midi_in_device_get_from_internal_name(char *s);
extern void  midi_out_device_init();
extern void  midi_in_device_init();

typedef struct midi_device_t {
    void (*play_sysex)(uint8_t *sysex, unsigned int len);
    void (*play_msg)(uint8_t *msg);
    void (*poll)();
    int (*write)(uint8_t val);
} midi_device_t;

typedef struct midi_in_handler_t {
    uint8_t *buf;
    int      cnt;
    uint32_t len;

    void (*msg)(void *p, uint8_t *msg, uint32_t len);
    int (*sysex)(void *p, uint8_t *buffer, uint32_t len, int abort);
    struct midi_in_handler_t *p;
    struct midi_in_handler_t *prev, *next;
} midi_in_handler_t;

typedef struct midi_t {
    uint8_t midi_rt_buf[8], midi_cmd_buf[8],
        midi_status, midi_sysex_data[SYSEX_SIZE];
    int midi_cmd_pos, midi_cmd_len, midi_cmd_r,
        midi_realtime, thruchan, midi_clockout;
    unsigned int midi_sysex_start, midi_sysex_delay,
        midi_pos;
    midi_device_t *m_out_device, *m_in_device;
} midi_t;

extern midi_t *midi_out, *midi_in;

extern void midi_out_init(midi_device_t *device);
extern void midi_in_init(midi_device_t *device, midi_t **mididev);
extern void midi_out_close();
extern void midi_in_close(void);
extern void midi_raw_out_rt_byte(uint8_t val);
extern void midi_raw_out_thru_rt_byte(uint8_t val);
extern void midi_raw_out_byte(uint8_t val);
extern void midi_clear_buffer(void);
extern void midi_poll();

extern void midi_in_handler(int set, void (*msg)(void *p, uint8_t *msg, uint32_t len), int (*sysex)(void *p, uint8_t *buffer, uint32_t len, int abort), void *p);
extern void midi_in_handlers_clear(void);
extern void midi_in_msg(uint8_t *msg, uint32_t len);
extern void midi_in_sysex(uint8_t *buffer, uint32_t len);

#if 0
#    ifdef _WIN32
#        define SYSTEM_MIDI_NAME          "Windows MIDI"
#        define SYSTEM_MIDI_INTERNAL_NAME "windows_midi"
#    else
#        define SYSTEM_MIDI_NAME          "System MIDI"
#        define SYSTEM_MIDI_INTERNAL_NAME "system_midi"
#    endif
#else
#    define SYSTEM_MIDI_NAME          "System MIDI"
#    define SYSTEM_MIDI_INTERNAL_NAME "system_midi"
#endif

#define MIDI_INPUT_NAME          "MIDI Input Device"
#define MIDI_INPUT_INTERNAL_NAME "midi_in"

#ifdef EMU_DEVICE_H
extern const device_t rtmidi_output_device;
extern const device_t rtmidi_input_device;
#    ifdef USE_FLUIDSYNTH
extern const device_t fluidsynth_device;
#    endif
#    ifdef USE_MUNT
extern const device_t mt32_old_device;
extern const device_t mt32_new_device;
extern const device_t cm32l_device;
extern const device_t cm32ln_device;
#    endif
#endif

#endif /*EMU_SOUND_MIDI_H*/
