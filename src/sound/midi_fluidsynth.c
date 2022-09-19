/* some code borrowed from scummvm */
#ifdef USE_FLUIDSYNTH
#    include <stdint.h>
#    include <stdio.h>
#    include <stdlib.h>
#    include <string.h>
#    include <wchar.h>
#    ifdef __unix__
#        include <unistd.h>
#    endif

#    include <86box/86box.h>
#    include <86box/config.h>
#    include <86box/device.h>
#    include <86box/midi.h>
#    include <86box/plat.h>
#    include <86box/plat_dynld.h>
#    include <86box/thread.h>
#    include <86box/sound.h>
#    include <86box/ui.h>

#    define FLUID_CHORUS_DEFAULT_N     3
#    define FLUID_CHORUS_DEFAULT_LEVEL 2.0f
#    define FLUID_CHORUS_DEFAULT_SPEED 0.3f
#    define FLUID_CHORUS_DEFAULT_DEPTH 8.0f
#    define FLUID_CHORUS_DEFAULT_TYPE  FLUID_CHORUS_MOD_SINE

#    define RENDER_RATE                100
#    define BUFFER_SEGMENTS            10

enum fluid_chorus_mod {
    FLUID_CHORUS_MOD_SINE     = 0,
    FLUID_CHORUS_MOD_TRIANGLE = 1
};

enum fluid_interp {
    FLUID_INTERP_NONE     = 0,
    FLUID_INTERP_LINEAR   = 1,
    FLUID_INTERP_DEFAULT  = 4,
    FLUID_INTERP_4THORDER = 4,
    FLUID_INTERP_7THORDER = 7,
    FLUID_INTERP_HIGHEST  = 7
};

extern void givealbuffer_midi(void *buf, uint32_t size);
extern void al_set_midi(int freq, int buf_size);

static void *fluidsynth_handle; /* handle to FluidSynth DLL */

/* Pointers to the real functions. */
// clang-format off
static void *(*f_new_fluid_settings)(void);
static void  (*f_delete_fluid_settings)(void *settings);
static int   (*f_fluid_settings_setnum)(void *settings, const char *name, double val);
static int   (*f_fluid_settings_getnum)(void *settings, const char *name, double *val);
static void *(*f_new_fluid_synth)(void *settings);
static int   (*f_delete_fluid_synth)(void *synth);
static int   (*f_fluid_synth_noteon)(void *synth, int chan, int key, int vel);
static int   (*f_fluid_synth_noteoff)(void *synth, int chan, int key);
static int   (*f_fluid_synth_cc)(void *synth, int chan, int ctrl, int val);
static int   (*f_fluid_synth_channel_pressure)(void *synth, int chan, int val);
static int   (*f_fluid_synth_sysex)(void *synth, const char *data, int len, char *response, int *response_len, int *handled, int dryrun);
static int   (*f_fluid_synth_pitch_bend)(void *synth, int chan, int val);
static int   (*f_fluid_synth_program_change)(void *synth, int chan, int program);
static int   (*f_fluid_synth_sfload)(void *synth, const char *filename, int reset_presets);
static int   (*f_fluid_synth_set_interp_method)(void *synth, int chan, int interp_method);
static void  (*f_fluid_synth_set_reverb)(void *synth, double roomsize, double damping, double width, double level);
static void  (*f_fluid_synth_set_reverb_on)(void *synth, int on);
static void  (*f_fluid_synth_set_chorus)(void *synth, int nr, double level, double speed, double depth_ms, int type);
static void  (*f_fluid_synth_set_chorus_on)(void *synth, int on);
static int   (*f_fluid_synth_write_s16)(void *synth, int len, void *lout, int loff, int lincr, void *rout, int roff, int rincr);
static int   (*f_fluid_synth_write_float)(void *synth, int len, void *lout, int loff, int lincr, void *rout, int roff, int rincr);
static char *(*f_fluid_version_str)(void);
// clang-format on

static dllimp_t fluidsynth_imports[] = {
  // clang-format off
  { "new_fluid_settings",            &f_new_fluid_settings            },
  { "delete_fluid_settings",         &f_delete_fluid_settings         },
  { "fluid_settings_setnum",         &f_fluid_settings_setnum         },
  { "fluid_settings_getnum",         &f_fluid_settings_getnum         },
  { "new_fluid_synth",               &f_new_fluid_synth               },
  { "delete_fluid_synth",            &f_delete_fluid_synth            },
  { "fluid_synth_noteon",            &f_fluid_synth_noteon            },
  { "fluid_synth_noteoff",           &f_fluid_synth_noteoff           },
  { "fluid_synth_cc",                &f_fluid_synth_cc                },
  { "fluid_synth_channel_pressure",  &f_fluid_synth_channel_pressure  },
  { "fluid_synth_sysex",             &f_fluid_synth_sysex             },
  { "fluid_synth_pitch_bend",        &f_fluid_synth_pitch_bend        },
  { "fluid_synth_program_change",    &f_fluid_synth_program_change    },
  { "fluid_synth_sfload",            &f_fluid_synth_sfload            },
  { "fluid_synth_set_interp_method", &f_fluid_synth_set_interp_method },
  { "fluid_synth_set_reverb",        &f_fluid_synth_set_reverb        },
  { "fluid_synth_set_reverb_on",     &f_fluid_synth_set_reverb_on     },
  { "fluid_synth_set_chorus",        &f_fluid_synth_set_chorus        },
  { "fluid_synth_set_chorus_on",     &f_fluid_synth_set_chorus_on     },
  { "fluid_synth_write_s16",         &f_fluid_synth_write_s16         },
  { "fluid_synth_write_float",       &f_fluid_synth_write_float       },
  { "fluid_version_str",             &f_fluid_version_str             },
  { NULL,                            NULL                             },
  // clang-format on
};

typedef struct fluidsynth {
    void *settings;
    void *synth;
    int   samplerate;
    int   sound_font;

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
    if (data->midi_pos == 48000 / RENDER_RATE) {
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
                f_fluid_synth_write_float(data->synth, buf_size / (2 * sizeof(float)), buf, 0, 2, buf, 1, 2);
            buf_pos += buf_size;
            if (buf_pos >= data->buf_size) {
                givealbuffer_midi(data->buffer, data->buf_size / sizeof(float));
                buf_pos = 0;
            }
        } else {
            int16_t *buf = (int16_t *) ((uint8_t *) data->buffer_int16 + buf_pos);
            memset(buf, 0, buf_size);
            if (data->synth)
                f_fluid_synth_write_s16(data->synth, buf_size / (2 * sizeof(int16_t)), buf, 0, 2, buf, 1, 2);
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
            f_fluid_synth_noteoff(data->synth, chan, param1);
            break;
        case 0x90: /* Note On */
            f_fluid_synth_noteon(data->synth, chan, param1, param2);
            break;
        case 0xA0: /* Aftertouch */
            break;
        case 0xB0: /* Control Change */
            f_fluid_synth_cc(data->synth, chan, param1, param2);
            break;
        case 0xC0: /* Program Change */
            f_fluid_synth_program_change(data->synth, chan, param1);
            break;
        case 0xD0: /* Channel Pressure */
            f_fluid_synth_channel_pressure(data->synth, chan, param1);
            break;
        case 0xE0: /* Pitch Bend */
            f_fluid_synth_pitch_bend(data->synth, chan, (param2 << 7) | param1);
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

    f_fluid_synth_sysex(d->synth, (const char *) data, len, 0, 0, 0, 0);
}

void *
fluidsynth_init(const device_t *info)
{
    fluidsynth_t  *data = &fsdev;
    midi_device_t *dev;

    memset(data, 0, sizeof(fluidsynth_t));

    /* Try loading the DLL. */
#    ifdef _WIN32
#        if (!(defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64))
    fluidsynth_handle = dynld_module("libfluidsynth.dll", fluidsynth_imports);
#        else
    fluidsynth_handle = dynld_module("libfluidsynth64.dll", fluidsynth_imports);
#        endif
#    elif defined __APPLE__
    fluidsynth_handle = dynld_module("libfluidsynth.dylib", fluidsynth_imports);
#    else
    fluidsynth_handle = dynld_module("libfluidsynth.so.3", fluidsynth_imports);
    if (fluidsynth_handle == NULL)
        fluidsynth_handle = dynld_module("libfluidsynth.so.2", fluidsynth_imports);
#    endif
    if (fluidsynth_handle == NULL) {
        ui_msgbox_header(MBX_ERROR, (wchar_t *) IDS_2080, (wchar_t *) IDS_2133);
        return NULL;
    }

    data->settings = f_new_fluid_settings();

    f_fluid_settings_setnum(data->settings, "synth.sample-rate", 44100);
    f_fluid_settings_setnum(data->settings, "synth.gain", device_get_config_int("output_gain") / 100.0f);

    data->synth = f_new_fluid_synth(data->settings);

    const char *sound_font = (char *) device_get_config_string("sound_font");
#    ifdef __unix__
    if (!sound_font || sound_font[0] == 0)
        sound_font = (access("/usr/share/sounds/sf2/FluidR3_GM.sf2", F_OK) == 0 ? "/usr/share/sounds/sf2/FluidR3_GM.sf2" :
                      (access("/usr/share/soundfonts/default.sf2", F_OK) == 0 ? "/usr/share/soundfonts/default.sf2" : ""));
#    endif
    data->sound_font = f_fluid_synth_sfload(data->synth, sound_font, 1);

    if (device_get_config_int("chorus")) {
        f_fluid_synth_set_chorus_on(data->synth, 1);

        int    chorus_voices = device_get_config_int("chorus_voices");
        double chorus_level  = device_get_config_int("chorus_level") / 100.0;
        double chorus_speed  = device_get_config_int("chorus_speed") / 100.0;
        double chorus_depth  = device_get_config_int("chorus_depth") / 10.0;

        int chorus_waveform = FLUID_CHORUS_MOD_SINE;
        if (device_get_config_int("chorus_waveform") == 0)
            chorus_waveform = FLUID_CHORUS_MOD_SINE;
        else
            chorus_waveform = FLUID_CHORUS_MOD_TRIANGLE;

        f_fluid_synth_set_chorus(data->synth, chorus_voices, chorus_level, chorus_speed, chorus_depth, chorus_waveform);
    } else
        f_fluid_synth_set_chorus_on(data->synth, 0);

    if (device_get_config_int("reverb")) {
        f_fluid_synth_set_reverb_on(data->synth, 1);

        double reverb_room_size = device_get_config_int("reverb_room_size") / 100.0;
        double reverb_damping   = device_get_config_int("reverb_damping") / 100.0;
        int    reverb_width     = device_get_config_int("reverb_width");
        double reverb_level     = device_get_config_int("reverb_level") / 100.0;

        f_fluid_synth_set_reverb(data->synth, reverb_room_size, reverb_damping, reverb_width, reverb_level);
    } else
        f_fluid_synth_set_reverb_on(data->synth, 0);

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

    f_fluid_synth_set_interp_method(data->synth, -1, fs_interpolation);

    double samplerate;
    f_fluid_settings_getnum(data->settings, "synth.sample-rate", &samplerate);
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

    dev = malloc(sizeof(midi_device_t));
    memset(dev, 0, sizeof(midi_device_t));

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
fluidsynth_close(void *p)
{
    if (!p)
        return;

    fluidsynth_t *data = &fsdev;

    data->on = 0;
    thread_set_event(data->event);
    thread_wait(data->thread_h);

    if (data->synth) {
        f_delete_fluid_synth(data->synth);
        data->synth = NULL;
    }

    if (data->settings) {
        f_delete_fluid_settings(data->settings);
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

    /* Unload the DLL if possible. */
    if (fluidsynth_handle != NULL) {
        dynld_close(fluidsynth_handle);
        fluidsynth_handle = NULL;
    }
}

static const device_config_t fluidsynth_config[] = {
  // clang-format off
    {
        .name = "sound_font",
        .description = "Sound Font",
        .type = CONFIG_FNAME,
        .default_string = "",
        .file_filter = "SF2 Sound Fonts (*.sf2)|*.sf2"
    },
    {
        .name = "output_gain",
        .description = "Output Gain",
        .type = CONFIG_SPINNER,
        .spinner =
        {
            .min = 0,
            .max = 100
        },
        .default_int = 100
    },
    {
        .name = "chorus",
        .description = "Chorus",
        .type = CONFIG_BINARY,
        .default_int = 0
    },
    {
        .name = "chorus_voices",
        .description = "Chorus Voices",
        .type = CONFIG_SPINNER,
        .spinner =
        {
            .min = 0,
            .max = 99
        },
        .default_int = 3
    },
    {
        .name = "chorus_level",
        .description = "Chorus Level",
        .type = CONFIG_SPINNER,
        .spinner =
        {
            .min = 0,
            .max = 100
        },
        .default_int = 100
    },
    {
        .name = "chorus_speed",
        .description = "Chorus Speed",
        .type = CONFIG_SPINNER,
        .spinner =
        {
            .min = 30,
            .max = 500
        },
        .default_int = 30
    },
    {
        .name = "chorus_depth",
        .description = "Chorus Depth",
        .type = CONFIG_SPINNER,
        .spinner =
        {
            .min = 0,
            .max = 210
        },
        .default_int = 80
    },
    {
        .name = "chorus_waveform",
        .description = "Chorus Waveform",
        .type = CONFIG_SELECTION,
        .selection =
        {
            {
                .description = "Sine",
                .value = 0
            },
            {
                .description = "Triangle",
                .value = 1
            }
        },
        .default_int = 0
    },
    {
        .name = "reverb",
        .description = "Reverb",
        .type = CONFIG_BINARY,
        .default_int = 0
    },
    {
        .name = "reverb_room_size",
        .description = "Reverb Room Size",
        .type = CONFIG_SPINNER,
        .spinner =
        {
            .min = 0,
            .max = 120
        },
        .default_int = 20
    },
    {
        .name = "reverb_damping",
        .description = "Reverb Damping",
        .type = CONFIG_SPINNER,
        .spinner =
        {
            .min = 0,
            .max = 100
        },
        .default_int = 0
    },
    {
        .name = "reverb_width",
        .description = "Reverb Width",
        .type = CONFIG_SPINNER,
        .spinner =
        {
            .min = 0,
            .max = 100
        },
        .default_int = 1
    },
    {
        .name = "reverb_level",
        .description = "Reverb Level",
        .type = CONFIG_SPINNER,
        .spinner =
        {
            .min = 0,
            .max = 100
        },
        .default_int = 90
    },
    {
        .name = "interpolation",
        .description = "Interpolation Method",
        .type = CONFIG_SELECTION,
        .selection =
        {
            {
                .description = "None",
                .value = 0
            },
            {
                .description = "Linear",
                .value = 1
            },
            {
                .description = "4th Order",
                .value = 2
            },
            {
                .description = "7th Order",
                .value = 3
            }
        },
        .default_int = 2
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
    { .available = fluidsynth_available },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = fluidsynth_config
};

#endif/*USE_FLUIDSYNTH*/
