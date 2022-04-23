/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          MIDI backend implemented using the RtMidi library.
 *
 * Author:  Cacodemon345,
 *          Miran Grca, <mgrca8@gmail.com>
 *          Copyright 2021 Cacodemon345.
 *          Copyright 2021 Miran Grca.
 */

#if defined __has_include
#  if __has_include (<RtMidi.h>)
#    include <RtMidi.h>
#  endif
#  if __has_include (<rtmidi/RtMidi.h>)
#    include <rtmidi/RtMidi.h>
#  endif
#endif

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C"
{
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/midi.h>
#include <86box/midi_rtmidi.h>
#include <86box/config.h>

// Disable c99-designator to avoid the warnings in rtmidi_*_device
#ifdef __clang__
#if __has_warning("-Wc99-designator")
#pragma clang diagnostic ignored "-Wc99-designator"
#endif
#endif

static RtMidiOut *  midiout = nullptr;
static RtMidiIn *   midiin = nullptr;
static int          midi_out_id = 0, midi_in_id = 0;
static const int    midi_lengths[8] = {3, 3, 3, 3, 2, 2, 3, 1};


int
rtmidi_write(uint8_t val)
{
    return 0;
}


void
rtmidi_play_msg(uint8_t *msg)
{
    if (midiout)
    midiout->sendMessage(msg, midi_lengths[(msg[0] >> 4) & 7]);
}


void
rtmidi_play_sysex(uint8_t *sysex, unsigned int len)
{
    if (midiout)
    midiout->sendMessage(sysex, len);
}


void*
rtmidi_output_init(const device_t *info)
{
    midi_device_t* dev = (midi_device_t*)malloc(sizeof(midi_device_t));
    memset(dev, 0, sizeof(midi_device_t));

    dev->play_msg = rtmidi_play_msg;
    dev->play_sysex = rtmidi_play_sysex;
    dev->write = rtmidi_write;

    try {
    if (!midiout) midiout = new RtMidiOut;
    } catch (RtMidiError& error) {
    pclog("Failed to initialize MIDI output: %s\n", error.getMessage().c_str());
    return nullptr;
    }

    midi_out_id = config_get_int((char*)SYSTEM_MIDI_NAME, (char*)"midi", 0);

    try {
    midiout->openPort(midi_out_id);
    } catch (RtMidiError& error) {
    pclog("Fallback to default MIDI output port: %s\n", error.getMessage().c_str());

    try {
        midiout->openPort(0);
    } catch (RtMidiError& error) {
        pclog("Failed to initialize MIDI output: %s\n", error.getMessage().c_str());
        delete midiout;
        midiout = nullptr;
        return nullptr;
    }
    }

    midi_out_init(dev);

    return dev;
}


void
rtmidi_output_close(void *p)
{
    if (!midiout)
    return;

    midiout->closePort();

    delete midiout;
    midiout = nullptr;

    midi_out_close();
}


int
rtmidi_out_get_num_devs(void)
{
    if (!midiout) {
    try {
        midiout = new RtMidiOut;
    } catch (RtMidiError& error) {
        pclog("Failed to initialize MIDI output: %s\n", error.getMessage().c_str());
    }
    }

    return midiout ? midiout->getPortCount() : 0;
}


void
rtmidi_out_get_dev_name(int num, char *s)
{
    strcpy(s, midiout->getPortName(num).c_str());
}


void
rtmidi_input_callback(double timeStamp, std::vector<unsigned char> *message, void *userData)
{
    if (message->front() == 0xF0)
        midi_in_sysex(message->data(), message->size());
    else
    midi_in_msg(message->data(), message->size());
}


void*
rtmidi_input_init(const device_t *info)
{
    midi_device_t* dev = (midi_device_t*)malloc(sizeof(midi_device_t));
    memset(dev, 0, sizeof(midi_device_t));

    try {
    if (!midiin)
        midiin = new RtMidiIn;
    } catch (RtMidiError& error) {
    pclog("Failed to initialize MIDI input: %s\n", error.getMessage().c_str());
    return nullptr;
    }

    midi_in_id = config_get_int((char*)MIDI_INPUT_NAME, (char*)"midi_input", 0);

    try {
    midiin->openPort(midi_in_id);
    } catch (RtMidiError& error) {
    pclog("Fallback to default MIDI input port: %s\n", error.getMessage().c_str());

    try {
        midiin->openPort(0);
    } catch (RtMidiError& error) {
        pclog("Failed to initialize MIDI input: %s\n", error.getMessage().c_str());
        delete midiin;
        midiin = nullptr;
        return nullptr;
    }
    }

    midiin->setCallback(&rtmidi_input_callback);

    // Don't ignore sysex, timing, or active sensing messages.
    midiin->ignoreTypes(false, false, false);

    midi_in_init(dev, &midi_in);

    midi_in->midi_realtime = device_get_config_int("realtime");
    midi_in->thruchan = device_get_config_int("thruchan");
    midi_in->midi_clockout = device_get_config_int("clockout");

    return dev;
}


void
rtmidi_input_close(void* p)
{
    midiin->cancelCallback();
    midiin->closePort();

    delete midiin;
    midiin = nullptr;

    midi_out_close();
}


int
rtmidi_in_get_num_devs(void)
{
    if (!midiin) {
    try {
        midiin = new RtMidiIn;
    } catch (RtMidiError& error) {
        pclog("Failed to initialize MIDI input: %s\n", error.getMessage().c_str());
    }
    }

    return midiin ? midiin->getPortCount() : 0;
}


void
rtmidi_in_get_dev_name(int num, char *s)
{
    strcpy(s, midiin->getPortName(num).c_str());
}

static const device_config_t system_midi_config[] = {
    {
        .name = "midi",
        .description = "MIDI out device",
        .type = CONFIG_MIDI_OUT,
        .default_string = "",
        .default_int = 0
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t midi_input_config[] = {
    {
        .name = "midi_input",
        .description = "MIDI in device",
        .type = CONFIG_MIDI_IN,
        .default_string = "",
        .default_int = 0
    },
    {
        .name = "realtime",
        .description = "MIDI Real time",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 0
    },
    {
        .name = "thruchan",
        .description = "MIDI Thru",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1
    },
    {
        .name = "clockout",
        .description = "MIDI Clockout",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t rtmidi_output_device = {
    .name = SYSTEM_MIDI_NAME,
    .internal_name = SYSTEM_MIDI_INTERNAL_NAME,
    .flags = 0,
    .local = 0,
    .init = rtmidi_output_init,
    .close = rtmidi_output_close,
    .reset = NULL,
    { .available = rtmidi_out_get_num_devs },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = system_midi_config
};

const device_t rtmidi_input_device = {
    .name = MIDI_INPUT_NAME,
    .internal_name = MIDI_INPUT_INTERNAL_NAME,
    .flags = 0,
    .local = 0,
    .init = rtmidi_input_init,
    .close = rtmidi_input_close,
    .reset = NULL,
    { .available = rtmidi_in_get_num_devs },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = midi_input_config
};

}
