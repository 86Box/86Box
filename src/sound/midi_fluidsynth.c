/* some code borrowed from scummvm */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#ifdef __unix__
#    include <unistd.h>
#endif
#include <fluidsynth.h>

#include <86box/86box.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/midi.h>
#include <86box/thread.h>
#include <86box/sound.h>
#include <86box/plat_unused.h>

#define RENDER_RATE                100
#define BUFFER_SEGMENTS            10

/* Check the FluidSynth version to determine wheteher to use the older reverb/chorus
   control functions that were deprecated in 2.2.0, or their newer replacements */
#if (FLUIDSYNTH_VERSION_MAJOR < 2) || ((FLUIDSYNTH_VERSION_MAJOR == 2) && (FLUIDSYNTH_VERSION_MINOR < 2))
#    define USE_OLD_FLUIDSYNTH_API
#endif

extern void givealbuffer_midi(void *buf, uint32_t size);
extern void al_set_midi(int freq, int buf_size);

typedef struct fluidsynth {
    fluid_settings_t *settings;
    fluid_synth_t    *synth;
    int               samplerate;
    int               sound_font;

    thread_t *thread_h;
    event_t  *event, *start_event;
    int       buf_size;
    float    *buffer;
    int16_t  *buffer_int16;
    int       midi_pos;

    int on;
} fluidsynth_t;

fluidsynth_t fsdev;

int
fluidsynth_available(void)
{
    return 1;
}

void
fluidsynth_poll(void)
{
    fluidsynth_t *data = &fsdev;
    data->midi_pos++;
    if (data->midi_pos == SOUND_FREQ / RENDER_RATE) {
        data->midi_pos = 0;
        thread_set_event(data->event);
    }
}

static void
fluidsynth_thread(void *param)
{
    fluidsynth_t *data     = (fluidsynth_t *) param;
    int           buf_pos  = 0;
    int           buf_size = data->buf_size / BUFFER_SEGMENTS;

    thread_set_event(data->start_event);

    while (data->on) {
        thread_wait_event(data->event, -1);
        thread_reset_event(data->event);

        if (sound_is_float) {
            float *buf = (float *) ((uint8_t *) data->buffer + buf_pos);
            memset(buf, 0, buf_size);
            if (data->synth)
                fluid_synth_write_float(data->synth, buf_size / (2 * sizeof(float)), buf, 0, 2, buf, 1, 2);
            buf_pos += buf_size;
            if (buf_pos >= data->buf_size) {
                givealbuffer_midi(data->buffer, data->buf_size / sizeof(float));
                buf_pos = 0;
            }
        } else {
            int16_t *buf = (int16_t *) ((uint8_t *) data->buffer_int16 + buf_pos);
            memset(buf, 0, buf_size);
            if (data->synth)
                fluid_synth_write_s16(data->synth, buf_size / (2 * sizeof(int16_t)), buf, 0, 2, buf, 1, 2);
            buf_pos += buf_size;
            if (buf_pos >= data->buf_size) {
                givealbuffer_midi(data->buffer_int16, data->buf_size / sizeof(int16_t));
                buf_pos = 0;
            }
        }
    }
}

void
fluidsynth_msg(uint8_t *msg)
{
    fluidsynth_t *data = &fsdev;

    uint32_t val = *((uint32_t *) msg);

    uint32_t param2 = (uint8_t) ((val >> 16) & 0xFF);
    uint32_t param1 = (uint8_t) ((val >> 8) & 0xFF);
    uint8_t  cmd    = (uint8_t) (val & 0xF0);
    uint8_t  chan   = (uint8_t) (val & 0x0F);

    switch (cmd) {
        case 0x80: /* Note Off */
            fluid_synth_noteoff(data->synth, chan, param1);
            break;
        case 0x90: /* Note On */
            fluid_synth_noteon(data->synth, chan, param1, param2);
            break;
        case 0xA0: /* Aftertouch */
            break;
        case 0xB0: /* Control Change */
            fluid_synth_cc(data->synth, chan, param1, param2);
            break;
        case 0xC0: /* Program Change */
            fluid_synth_program_change(data->synth, chan, param1);
            break;
        case 0xD0: /* Channel Pressure */
            fluid_synth_channel_pressure(data->synth, chan, param1);
            break;
        case 0xE0: /* Pitch Bend */
            fluid_synth_pitch_bend(data->synth, chan, (param2 << 7) | param1);
            break;
        case 0xF0: /* SysEx */
            break;
        default:
            break;
    }
}

void
fluidsynth_sysex(uint8_t *data, unsigned int len)
{
    fluidsynth_t *d = &fsdev;

    fluid_synth_sysex(d->synth, (const char *) data, len, 0, 0, 0, 0);
}

void *
fluidsynth_init(UNUSED(const device_t *info))
{
    fluidsynth_t  *data = &fsdev;
    midi_device_t *dev;

    memset(data, 0, sizeof(fluidsynth_t));

    data->settings = new_fluid_settings();

    fluid_settings_setnum(data->settings, "synth.sample-rate", 44100);
    fluid_settings_setnum(data->settings, "synth.gain", device_get_config_int("output_gain") / 100.0f);

    data->synth = new_fluid_synth(data->settings);

    const char *sound_font = device_get_config_string("sound_font");
#ifdef __unix__
    if (!sound_font || sound_font[0] == 0)
        sound_font = (access("/usr/share/sounds/sf2/FluidR3_GM.sf2", F_OK) == 0 ? "/usr/share/sounds/sf2/FluidR3_GM.sf2" :
                      (access("/usr/share/soundfonts/default.sf2", F_OK) == 0 ? "/usr/share/soundfonts/default.sf2" : ""));
#endif
    data->sound_font = fluid_synth_sfload(data->synth, sound_font, 1);

    if (device_get_config_int("chorus")) {
#ifndef USE_OLD_FLUIDSYNTH_API
        fluid_synth_chorus_on(data->synth, -1, 1);
#else
        fluid_synth_set_chorus_on(data->synth, 1);
#endif

        int    chorus_voices = device_get_config_int("chorus_voices");
        double chorus_level  = device_get_config_int("chorus_level") / 100.0;
        double chorus_speed  = device_get_config_int("chorus_speed") / 100.0;
        double chorus_depth  = device_get_config_int("chorus_depth") / 10.0;

        int chorus_waveform = FLUID_CHORUS_MOD_SINE;
        if (device_get_config_int("chorus_waveform") == 0)
            chorus_waveform = FLUID_CHORUS_MOD_SINE;
        else
            chorus_waveform = FLUID_CHORUS_MOD_TRIANGLE;

#ifndef USE_OLD_FLUIDSYNTH_API
        fluid_synth_set_chorus_group_nr(data->synth, -1, chorus_voices);
        fluid_synth_set_chorus_group_level(data->synth, -1, chorus_level);
        fluid_synth_set_chorus_group_speed(data->synth, -1, chorus_speed);
        fluid_synth_set_chorus_group_depth(data->synth, -1, chorus_depth);
        fluid_synth_set_chorus_group_type(data->synth, -1, chorus_waveform);
#else
        fluid_synth_set_chorus(data->synth, chorus_voices, chorus_level, chorus_speed, chorus_depth, chorus_waveform);
#endif
    } else
#ifndef USE_OLD_FLUIDSYNTH_API
        fluid_synth_chorus_on(data->synth, -1, 0);
#else
        fluid_synth_set_chorus_on(data->synth, 0);
#endif

    if (device_get_config_int("reverb")) {
#ifndef USE_OLD_FLUIDSYNTH_API
        fluid_synth_reverb_on(data->synth, -1, 1);
#else
        fluid_synth_set_reverb_on(data->synth, 1);
#endif

        double reverb_room_size = device_get_config_int("reverb_room_size") / 100.0;
        double reverb_damping   = device_get_config_int("reverb_damping") / 100.0;
        double reverb_width     = device_get_config_int("reverb_width") / 10.0;
        double reverb_level     = device_get_config_int("reverb_level") / 100.0;

#ifndef USE_OLD_FLUIDSYNTH_API
        fluid_synth_set_reverb_group_roomsize(data->synth, -1, reverb_room_size);
        fluid_synth_set_reverb_group_damp(data->synth, -1, reverb_damping);
        fluid_synth_set_reverb_group_width(data->synth, -1, reverb_width);
        fluid_synth_set_reverb_group_level(data->synth, -1, reverb_level);
#else
        fluid_synth_set_reverb(data->synth, reverb_room_size, reverb_damping, reverb_width, reverb_level);
#endif
    } else
#ifndef USE_OLD_FLUIDSYNTH_API
        fluid_synth_reverb_on(data->synth, -1, 0);
#else
        fluid_synth_set_reverb_on(data->synth, 0);
#endif

    int interpolation    = device_get_config_int("interpolation");
    int fs_interpolation = FLUID_INTERP_4THORDER;

    if (interpolation == 0)
        fs_interpolation = FLUID_INTERP_NONE;
    else if (interpolation == 1)
        fs_interpolation = FLUID_INTERP_LINEAR;
    else if (interpolation == 2)
        fs_interpolation = FLUID_INTERP_4THORDER;
    else if (interpolation == 3)
        fs_interpolation = FLUID_INTERP_7THORDER;

    fluid_synth_set_interp_method(data->synth, -1, fs_interpolation);

    double samplerate;
    fluid_settings_getnum(data->settings, "synth.sample-rate", &samplerate);
    data->samplerate = (int) samplerate;
    if (sound_is_float) {
        data->buf_size     = (data->samplerate / RENDER_RATE) * 2 * sizeof(float) * BUFFER_SEGMENTS;
        data->buffer       = malloc(data->buf_size);
        data->buffer_int16 = NULL;
    } else {
        data->buf_size     = (data->samplerate / RENDER_RATE) * 2 * sizeof(int16_t) * BUFFER_SEGMENTS;
        data->buffer       = NULL;
        data->buffer_int16 = malloc(data->buf_size);
    }

    al_set_midi(data->samplerate, data->buf_size);

    dev = calloc(1, sizeof(midi_device_t));

    dev->play_msg   = fluidsynth_msg;
    dev->play_sysex = fluidsynth_sysex;
    dev->poll       = fluidsynth_poll;

    midi_out_init(dev);

    data->on = 1;

    data->start_event = thread_create_event();

    data->event    = thread_create_event();
    data->thread_h = thread_create(fluidsynth_thread, data);

    thread_wait_event(data->start_event, -1);
    thread_reset_event(data->start_event);

    return dev;
}

void
fluidsynth_close(void *priv)
{
    if (!priv)
        return;

    fluidsynth_t *data = &fsdev;

    data->on = 0;
    thread_set_event(data->event);
    thread_wait(data->thread_h);

    if (data->synth) {
        delete_fluid_synth(data->synth);
        data->synth = NULL;
    }

    if (data->settings) {
        delete_fluid_settings(data->settings);
        data->settings = NULL;
    }

    if (data->buffer) {
        free(data->buffer);
        data->buffer = NULL;
    }

    if (data->buffer_int16) {
        free(data->buffer_int16);
        data->buffer_int16 = NULL;
    }
}

static const device_config_t fluidsynth_config[] = {
  // clang-format off
    {
        .name           = "sound_font",
        .description    = "SoundFont",
        .type           = CONFIG_FNAME,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = "SF2 Sound Fonts (*.sf2)|*.sf2",
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "output_gain",
        .description    = "Output Gain",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 100,
        .file_filter    = NULL,
        .spinner        = {
            .min =   0,
            .max = 100
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "chorus",
        .description    = "Chorus",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "chorus_voices",
        .description    = "Chorus Voices",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 3,
        .file_filter    = NULL,
        .spinner = {
            .min =  0,
            .max = 99
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "chorus_level",
        .description    = "Chorus Level",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 20,
        .file_filter    = NULL,
        .spinner        = {
            .min =   0,
            .max = 100
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "chorus_speed",
        .description    = "Chorus Speed",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 30,
        .file_filter    = NULL,
        .spinner = {
            .min =  10,
            .max = 500
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "chorus_depth",
        .description    = "Chorus Depth",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 80,
        .file_filter    = NULL,
        .spinner = {
            .min =    0,
            .max = 2560
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "chorus_waveform",
        .description    = "Chorus Waveform",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Sine",     .value = 0 },
            { .description = "Triangle", .value = 1 },
            { .description = ""                     }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "reverb",
        .description    = "Reverb",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "reverb_room_size",
        .description    = "Reverb Room Size",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 20,
        .file_filter    = NULL,
        .spinner = {
            .min =   0,
            .max = 100
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "reverb_damping",
        .description    = "Reverb Damping",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner = {
            .min =   0,
            .max = 100
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "reverb_width",
        .description    = "Reverb Width",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 5,
        .file_filter    = NULL,
        .spinner        = {
            .min =    0,
            .max = 1000
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "reverb_level",
        .description    = "Reverb Level",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 90,
        .file_filter    = NULL,
        .spinner        = {
            .min =   0,
            .max = 100
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "interpolation",
        .description    = "Interpolation Method",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 2,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "None",      .value = 0 },
            { .description = "Linear",    .value = 1 },
            { .description = "4th Order", .value = 2 },
            { .description = "7th Order", .value = 3 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t fluidsynth_device = {
    .name          = "FluidSynth",
    .internal_name = "fluidsynth",
    .flags         = 0,
    .local         = 0,
    .init          = fluidsynth_init,
    .close         = fluidsynth_close,
    .reset         = NULL,
    .available     = fluidsynth_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = fluidsynth_config
};
