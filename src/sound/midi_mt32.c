#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/midi.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/rom.h>
#include <86box/sound.h>
#include <86box/ui.h>
#include <mt32emu/c_interface/c_interface.h>

#define MT32_OLD_CTRL_ROM "roms/sound/mt32/MT32_CONTROL.ROM"
#define MT32_OLD_PCM_ROM  "roms/sound/mt32/MT32_PCM.ROM"
#define MT32_NEW_CTRL_ROM "roms/sound/mt32_new/MT32_CONTROL.ROM"
#define MT32_NEW_PCM_ROM  "roms/sound/mt32_new/MT32_PCM.ROM"
#define CM32L_CTRL_ROM    "roms/sound/cm32l/CM32L_CONTROL.ROM"
#define CM32L_PCM_ROM     "roms/sound/cm32l/CM32L_PCM.ROM"
#define CM32LN_CTRL_ROM   "roms/sound/cm32ln/CM32LN_CONTROL.ROM"
#define CM32LN_PCM_ROM    "roms/sound/cm32ln/CM32LN_PCM.ROM"

extern void givealbuffer_midi(void *buf, uint32_t size);
extern void al_set_midi(int freq, int buf_size);

static mt32emu_report_handler_version get_mt32_report_handler_version(mt32emu_report_handler_i i);
static void                           display_mt32_message(void *instance_data, const char *message);

static const mt32emu_report_handler_i_v0 handler_mt32_v0 = {
    /** Returns the actual interface version ID */
    get_mt32_report_handler_version, // mt32emu_report_handler_version (*getVersionID)(mt32emu_report_handler_i i);

    /** Callback for debug messages, in vprintf() format */
    NULL, // void (*printDebug)(void *instance_data, const char *fmt, va_list list);
    /** Callbacks for reporting errors */
    NULL, // void (*onErrorControlROM)(void *instance_data);
    NULL, // void (*onErrorPCMROM)(void *instance_data);
    /** Callback for reporting about displaying a new custom message on LCD */
    display_mt32_message, // void (*showLCDMessage)(void *instance_data, const char *message);
    /** Callback for reporting actual processing of a MIDI message */
    NULL, // void (*onMIDIMessagePlayed)(void *instance_data);
    /**
     * Callback for reporting an overflow of the input MIDI queue.
     * Returns MT32EMU_BOOL_TRUE if a recovery action was taken
     * and yet another attempt to enqueue the MIDI event is desired.
     */
    NULL, // mt32emu_boolean (*onMIDIQueueOverflow)(void *instance_data);
    /**
     * Callback invoked when a System Realtime MIDI message is detected in functions
     * mt32emu_parse_stream and mt32emu_play_short_message and the likes.
     */
    NULL, // void (*onMIDISystemRealtime)(void *instance_data, mt32emu_bit8u system_realtime);
    /** Callbacks for reporting system events */
    NULL, // void (*onDeviceReset)(void *instance_data);
    NULL, // void (*onDeviceReconfig)(void *instance_data);
    /** Callbacks for reporting changes of reverb settings */
    NULL, // void (*onNewReverbMode)(void *instance_data, mt32emu_bit8u mode);
    NULL, // void (*onNewReverbTime)(void *instance_data, mt32emu_bit8u time);
    NULL, // void (*onNewReverbLevel)(void *instance_data, mt32emu_bit8u level);
    /** Callbacks for reporting various information */
    NULL, // void (*onPolyStateChanged)(void *instance_data, mt32emu_bit8u part_num);
    NULL, // void (*onProgramChanged)(void *instance_data, mt32emu_bit8u part_num, const char *sound_group_name, const char *patch_name);
};

/** Alternate report handler for Roland CM-32L/CM-32LN */
static const mt32emu_report_handler_i_v0 handler_cm32l_v0 = {
    /** Returns the actual interface version ID */
    get_mt32_report_handler_version, // mt32emu_report_handler_version (*getVersionID)(mt32emu_report_handler_i i);

    /** Callback for debug messages, in vprintf() format */
    NULL, // void (*printDebug)(void *instance_data, const char *fmt, va_list list);
    /** Callbacks for reporting errors */
    NULL, // void (*onErrorControlROM)(void *instance_data);
    NULL, // void (*onErrorPCMROM)(void *instance_data);
    /** Callback for reporting about displaying a new custom message on LCD */
    NULL, // void (*showLCDMessage)(void *instance_data, const char *message);
    /** Callback for reporting actual processing of a MIDI message */
    NULL, // void (*onMIDIMessagePlayed)(void *instance_data);
    /**
     * Callback for reporting an overflow of the input MIDI queue.
     * Returns MT32EMU_BOOL_TRUE if a recovery action was taken
     * and yet another attempt to enqueue the MIDI event is desired.
     */
    NULL, // mt32emu_boolean (*onMIDIQueueOverflow)(void *instance_data);
    /**
     * Callback invoked when a System Realtime MIDI message is detected in functions
     * mt32emu_parse_stream and mt32emu_play_short_message and the likes.
     */
    NULL, // void (*onMIDISystemRealtime)(void *instance_data, mt32emu_bit8u system_realtime);
    /** Callbacks for reporting system events */
    NULL, // void (*onDeviceReset)(void *instance_data);
    NULL, // void (*onDeviceReconfig)(void *instance_data);
    /** Callbacks for reporting changes of reverb settings */
    NULL, // void (*onNewReverbMode)(void *instance_data, mt32emu_bit8u mode);
    NULL, // void (*onNewReverbTime)(void *instance_data, mt32emu_bit8u time);
    NULL, // void (*onNewReverbLevel)(void *instance_data, mt32emu_bit8u level);
    /** Callbacks for reporting various information */
    NULL, // void (*onPolyStateChanged)(void *instance_data, mt32emu_bit8u part_num);
    NULL, // void (*onProgramChanged)(void *instance_data, mt32emu_bit8u part_num, const char *sound_group_name, const char *patch_name);
};

static const mt32emu_report_handler_i handler_mt32  = { &handler_mt32_v0 };
static const mt32emu_report_handler_i handler_cm32l = { &handler_cm32l_v0 };

static mt32emu_context context         = NULL;
static int             roms_present[2] = { -1, -1 };

mt32emu_return_code
mt32_check(UNUSED(const char *func), mt32emu_return_code ret, mt32emu_return_code expected)
{
    if (ret != expected) {
        return 0;
    }
    return 1;
}

int
mt32_old_available(void)
{
    if (roms_present[0] < 0)
        roms_present[0] = (rom_present(MT32_OLD_CTRL_ROM) && rom_present(MT32_OLD_PCM_ROM));
    return roms_present[0];
}

int
mt32_new_available(void)
{
    if (roms_present[0] < 0)
        roms_present[0] = (rom_present(MT32_NEW_CTRL_ROM) && rom_present(MT32_NEW_PCM_ROM));
    return roms_present[0];
}

int
cm32l_available(void)
{
    if (roms_present[1] < 0)
        roms_present[1] = (rom_present(CM32L_CTRL_ROM) && rom_present(CM32L_PCM_ROM));
    return roms_present[1];
}

int
cm32ln_available(void)
{
    if (roms_present[1] < 0)
        roms_present[1] = (rom_present(CM32LN_CTRL_ROM) && rom_present(CM32LN_PCM_ROM));
    return roms_present[1];
}

static thread_t *thread_h    = NULL;
static event_t  *event       = NULL;
static event_t  *start_event = NULL;
static int       mt32_on     = 0;

#define RENDER_RATE     100
#define BUFFER_SEGMENTS 10

static uint32_t samplerate   = 44100;
static int      buf_size     = 0;
static float   *buffer       = NULL;
static int16_t *buffer_int16 = NULL;
static int      midi_pos     = 0;

static mt32emu_report_handler_version
get_mt32_report_handler_version(UNUSED(mt32emu_report_handler_i i))
{
    return MT32EMU_REPORT_HANDLER_VERSION_0;
}

static void
display_mt32_message(UNUSED(void *instance_data), const char *message)
{
    int   sz     = 0;
    char *ui_msg = NULL;

    sz     = snprintf(NULL, 0, "MT-32: %s", message);
    ui_msg = calloc(sz + 1, 1);
    if (ui_msg) {
        snprintf(ui_msg, sz, "MT-32: %s", message);
        ui_sb_mt32lcd(ui_msg);
    }
}

void
mt32_stream(float *stream, int len)
{
    if (context)
        mt32emu_render_float(context, stream, len);
}

void
mt32_stream_int16(int16_t *stream, int len)
{
    if (context)
        mt32emu_render_bit16s(context, stream, len);
}

void
mt32_poll(void)
{
    midi_pos++;
    if (midi_pos == SOUND_FREQ / RENDER_RATE) {
        midi_pos = 0;
        thread_set_event(event);
    }
}

static void
mt32_thread(UNUSED(void *param))
{
    int      buf_pos = 0;
    int      bsize   = buf_size / BUFFER_SEGMENTS;
    float   *buf;
    int16_t *buf16;

    thread_set_event(start_event);

    while (mt32_on) {
        thread_wait_event(event, -1);
        thread_reset_event(event);

        if (sound_is_float) {
            buf = (float *) ((uint8_t *) buffer + buf_pos);
            memset(buf, 0, bsize);
            mt32_stream(buf, bsize / (2 * sizeof(float)));
            buf_pos += bsize;
            if (buf_pos >= buf_size) {
                givealbuffer_midi(buffer, buf_size / sizeof(float));
                buf_pos = 0;
            }
        } else {
            buf16 = (int16_t *) ((uint8_t *) buffer_int16 + buf_pos);
            memset(buf16, 0, bsize);
            mt32_stream_int16(buf16, bsize / (2 * sizeof(int16_t)));
            buf_pos += bsize;
            if (buf_pos >= buf_size) {
                givealbuffer_midi(buffer_int16, buf_size / sizeof(int16_t));
                buf_pos = 0;
            }
        }
    }
}

void
mt32_msg(uint8_t *val)
{
    if (context)
        mt32_check("mt32emu_play_msg", mt32emu_play_msg(context, *(uint32_t *) val), MT32EMU_RC_OK);
}

void
mt32_sysex(uint8_t *data, unsigned int len)
{
    if (context)
        mt32_check("mt32emu_play_sysex", mt32emu_play_sysex(context, data, len), MT32EMU_RC_OK);
}

void *
mt32emu_init(char *control_rom, char *pcm_rom)
{
    midi_device_t *dev;
    char           fn[512];

    context = mt32emu_create_context(strstr(control_rom, "MT32_CONTROL.ROM") ? handler_mt32 : handler_cm32l, NULL);

    if (!rom_getfile(control_rom, fn, 512))
        return 0;
    if (!mt32_check("mt32emu_add_rom_file", mt32emu_add_rom_file(context, fn), MT32EMU_RC_ADDED_CONTROL_ROM))
        return 0;
    if (!rom_getfile(pcm_rom, fn, 512))
        return 0;
    if (!mt32_check("mt32emu_add_rom_file", mt32emu_add_rom_file(context, fn), MT32EMU_RC_ADDED_PCM_ROM))
        return 0;

    if (!mt32_check("mt32emu_open_synth", mt32emu_open_synth(context), MT32EMU_RC_OK))
        return 0;

    samplerate = mt32emu_get_actual_stereo_output_samplerate(context);
    /* buf_size = samplerate/RENDER_RATE*2; */
    if (sound_is_float) {
        buf_size     = (samplerate / RENDER_RATE) * 2 * BUFFER_SEGMENTS * sizeof(float);
        buffer       = malloc(buf_size);
        buffer_int16 = NULL;
    } else {
        buf_size     = (samplerate / RENDER_RATE) * 2 * BUFFER_SEGMENTS * sizeof(int16_t);
        buffer       = NULL;
        buffer_int16 = malloc(buf_size);
    }

    mt32emu_set_output_gain(context, device_get_config_int("output_gain") / 100.0f);
    mt32emu_set_reverb_enabled(context, device_get_config_int("reverb"));
    mt32emu_set_reverb_output_gain(context, device_get_config_int("reverb_output_gain") / 100.0f);
    mt32emu_set_reversed_stereo_enabled(context, device_get_config_int("reversed_stereo"));
    mt32emu_set_nice_amp_ramp_enabled(context, device_get_config_int("nice_ramp"));

    al_set_midi(samplerate, buf_size);

    dev = malloc(sizeof(midi_device_t));
    memset(dev, 0, sizeof(midi_device_t));

    dev->play_msg   = mt32_msg;
    dev->play_sysex = mt32_sysex;
    dev->poll       = mt32_poll;

    midi_out_init(dev);

    mt32_on = 1;

    start_event = thread_create_event();

    event    = thread_create_event();
    thread_h = thread_create(mt32_thread, 0);

    thread_wait_event(start_event, -1);
    thread_reset_event(start_event);

    return dev;
}

void *
mt32_old_init(UNUSED(const device_t *info))
{
    return mt32emu_init(MT32_OLD_CTRL_ROM, MT32_OLD_PCM_ROM);
}

void *
mt32_new_init(UNUSED(const device_t *info))
{
    return mt32emu_init(MT32_NEW_CTRL_ROM, MT32_NEW_PCM_ROM);
}

void *
cm32l_init(UNUSED(const device_t *info))
{
    return mt32emu_init(CM32L_CTRL_ROM, CM32L_PCM_ROM);
}

void *
cm32ln_init(UNUSED(const device_t *info))
{
    return mt32emu_init(CM32LN_CTRL_ROM, CM32LN_PCM_ROM);
}

void
mt32_close(void *priv)
{
    if (!priv)
        return;

    mt32_on = 0;
    thread_set_event(event);
    thread_wait(thread_h);

    event       = NULL;
    start_event = NULL;
    thread_h    = NULL;

    if (context) {
        mt32emu_close_synth(context);
        mt32emu_free_context(context);
    }
    context = NULL;

    ui_sb_mt32lcd("");

    if (buffer)
        free(buffer);
    buffer = NULL;

    if (buffer_int16)
        free(buffer_int16);
    buffer_int16 = NULL;
}

static const device_config_t mt32_config[] = {
  // clang-format off
    {
        .name = "output_gain",
        .description = "Output Gain",
        .type = CONFIG_SPINNER,
        .spinner = {
            .min = 0,
            .max = 100
        },
        .default_int = 100
    },
    {
        .name = "reverb",
        .description = "Reverb",
        .type = CONFIG_BINARY,
        .default_int = 1
    },
    {
        .name = "reverb_output_gain",
        .description = "Reverb Output Gain",
        .type = CONFIG_SPINNER,
        .spinner = {
            .min = 0,
            .max = 100
        },
        .default_int = 100
    },
    {
        .name = "reversed_stereo",
        .description = "Reversed stereo",
        .type = CONFIG_BINARY,
        .default_int = 0
    },
    {
        .name = "nice_ramp",
        .description = "Nice ramp",
        .type = CONFIG_BINARY,
        .default_int = 1
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t mt32_old_device = {
    .name          = "Roland MT-32 Emulation",
    .internal_name = "mt32",
    .flags         = 0,
    .local         = 0,
    .init          = mt32_old_init,
    .close         = mt32_close,
    .reset         = NULL,
    { .available = mt32_old_available },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = mt32_config
};

const device_t mt32_new_device = {
    .name          = "Roland MT-32 (New) Emulation",
    .internal_name = "mt32_new",
    .flags         = 0,
    .local         = 0,
    .init          = mt32_new_init,
    .close         = mt32_close,
    .reset         = NULL,
    { .available = mt32_new_available },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = mt32_config
};

const device_t cm32l_device = {
    .name          = "Roland CM-32L Emulation",
    .internal_name = "cm32l",
    .flags         = 0,
    .local         = 0,
    .init          = cm32l_init,
    .close         = mt32_close,
    .reset         = NULL,
    { .available = cm32l_available },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = mt32_config
};

const device_t cm32ln_device = {
    .name          = "Roland CM-32LN Emulation",
    .internal_name = "cm32ln",
    .flags         = 0,
    .local         = 0,
    .init          = cm32ln_init,
    .close         = mt32_close,
    .reset         = NULL,
    { .available = cm32ln_available },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = mt32_config
};
