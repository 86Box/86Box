
#include <RtMidi.h>
#include <cstdint>
#include <cstring>
extern "C"
{
#include <86box/86box.h>
#include <86box/midi.h>
#include <86box/plat_midi.h>
#include <86box/config.h>
}

extern "C" {

static RtMidiOut* midiout = nullptr;
static RtMidiIn* midiin = nullptr;
static int midi_out_id = 0, midi_in_id = 0;
static const int midi_lengths[8] = {3, 3, 3, 3, 2, 2, 3, 1};

int plat_midi_write(uint8_t val)
{ return 0; }

void plat_midi_init()
{
    try
    {
        if (!midiout) midiout = new RtMidiOut;
    }
    catch (RtMidiError& error)
    {
        pclog("Failed to initialize MIDI output: %s\n", error.getMessage().c_str());
        return;
    }
    midi_out_id = config_get_int((char*)SYSTEM_MIDI_NAME, (char*)"midi", 0);
    try
    {
        midiout->openPort(midi_out_id);
    }
    catch (RtMidiError& error)
    {
        pclog("Fallback to default MIDI output port: %s\n", error.getMessage().c_str());
        try
        {
            midiout->openPort(0);
        }
        catch (RtMidiError& error)
        {
            pclog("Failed to initialize MIDI output: %s\n", error.getMessage().c_str());
            delete midiout;
            midiout = nullptr;
            return;
        }
    }
}

void plat_midi_close()
{
    if (!midiout) return;
    midiout->closePort();
    delete midiout;
    midiout = nullptr;
}

int plat_midi_get_num_devs()
{
    if (!midiout)
    {
        try
        {
            midiout = new RtMidiOut;
        }
        catch (RtMidiError& error)
        {
            pclog("Failed to initialize MIDI output: %s\n", error.getMessage().c_str());
        }
    }
    return midiout ? midiout->getPortCount() : 0;
}

void plat_midi_play_msg(uint8_t *msg)
{
    if (midiout) midiout->sendMessage(msg, midi_lengths[(msg[0] >> 4) & 7]);
}

void plat_midi_get_dev_name(int num, char *s)
{
	strcpy(s, midiout->getPortName(num).c_str());
}

void plat_midi_play_sysex(uint8_t *sysex, unsigned int len)
{
    if (midiout) midiout->sendMessage(sysex, len);
}

static void plat_midi_callback(double timeStamp, std::vector<unsigned char> *message, void *userData)
{
    if (message->size() <= 3) midi_in_msg(message->data());
    else midi_in_sysex(message->data(), message->size());
}

void plat_midi_input_init(void)
{
    try
    {
        if (!midiin) midiin = new RtMidiIn;
    }
    catch (RtMidiError& error)
    {
        pclog("Failed to initialize MIDI input: %s\n", error.getMessage().c_str());
        return;
    }
    midi_in_id = config_get_int((char*)SYSTEM_MIDI_NAME, (char*)"midi_input", 0);
    try
    {
        midiin->openPort(midi_in_id);
    }
    catch (RtMidiError& error)
    {
        pclog("Fallback to default MIDI input port: %s\n", error.getMessage().c_str());
        try
        {
            midiin->openPort(0);
        }
        catch (RtMidiError& error)
        {
            pclog("Failed to initialize MIDI input: %s\n", error.getMessage().c_str());
            delete midiin;
            midiin = nullptr;
            return;
        }
    }
    midiin->setCallback(plat_midi_callback);
}

void plat_midi_input_close(void)
{
    midiin->cancelCallback();
    midiin->closePort();
    delete midiin;
    midiin = nullptr;
    return;
}

int plat_midi_in_get_num_devs(void)
{
    if (!midiin)
    {
        try
        {
            midiin = new RtMidiIn;
        }
        catch (RtMidiError& error)
        {
            pclog("Failed to initialize MIDI input: %s\n", error.getMessage().c_str());
        }
    }
    return midiin ? midiin->getPortCount() : 0;
}

void plat_midi_in_get_dev_name(int num, char *s)
{
	strcpy(s, midiin->getPortName(num).c_str());
}

}
