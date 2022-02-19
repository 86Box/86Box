/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Interface to the FAudio audio processing library.
 *
 *
 *
 * Authors:	Cacodemon345
 *
 *		Copyright 2022 Cacodemon345.
 */
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <FAudio.h>

extern "C"
{
#include <86box/86box.h>
#include <86box/sound.h>
#include <86box/midi.h>

static int midi_freq = 44100;
static int midi_buf_size = 4410;
static int initialized = 0;
static FAudio* faudio = nullptr;
static FAudioMasteringVoice* mastervoice = nullptr;
static FAudioSourceVoice* srcvoice = nullptr;
static FAudioSourceVoice* srcvoicemidi = nullptr;
static FAudioSourceVoice* srcvoicecd = nullptr;

#define FREQ	48000
#define BUFLEN	SOUNDBUFLEN

static void FAUDIOCALL
onBufferFinished(
	FAudioVoiceCallback *callback,
	void *pBufferContext)
{
    if (sound_is_float) delete[] (float*)(pBufferContext);
    else delete[] (int16_t*)(pBufferContext);
    
}

static FAudioVoiceCallback callbacks = 
{
    onBufferFinished
};

void
inital()
{
    if (FAudioCreate(&faudio, 0, FAUDIO_DEFAULT_PROCESSOR))
    {
        return;
    }

    if (FAudio_CreateMasteringVoice(faudio, &mastervoice, 2, FREQ, 0, 0, nullptr))
    {
        FAudio_Release(faudio);
        faudio = nullptr;
        return;
    }
    FAudioWaveFormatEx fmt;
    fmt.nChannels = 2;
    if (sound_is_float)
    {
        fmt.wFormatTag = FAUDIO_FORMAT_IEEE_FLOAT;
        fmt.wBitsPerSample = 32;
    }
    else
    {
        fmt.wFormatTag = FAUDIO_FORMAT_PCM;
        fmt.wBitsPerSample = 16;
    }
    fmt.nSamplesPerSec = FREQ;
    fmt.nBlockAlign = fmt.nChannels * fmt.wBitsPerSample / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    fmt.cbSize = 0;
    if (FAudio_CreateSourceVoice(faudio, &srcvoice, &fmt, 0, 2.0f, &callbacks, nullptr, nullptr))
    {
        FAudioVoice_DestroyVoice(mastervoice);
        FAudio_Release(faudio);
        faudio = nullptr;
        mastervoice = nullptr;
        return;
    }
    fmt.nSamplesPerSec = CD_FREQ;
    fmt.nBlockAlign = fmt.nChannels * fmt.wBitsPerSample / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    FAudio_CreateSourceVoice(faudio, &srcvoicecd, &fmt, 0, 2.0f, &callbacks, nullptr, nullptr);
    FAudioVoice_SetVolume(srcvoice, 1, FAUDIO_COMMIT_NOW);
    FAudioSourceVoice_Start(srcvoice, 0, FAUDIO_COMMIT_NOW);
    FAudioSourceVoice_Start(srcvoicecd, 0, FAUDIO_COMMIT_NOW);
    auto mdn = midi_device_get_internal_name(midi_device_current);
    if (strcmp(mdn, "none") && strcmp(mdn, SYSTEM_MIDI_INTERNAL_NAME))
	{
        fmt.nSamplesPerSec = midi_freq;
        fmt.nBlockAlign = fmt.nChannels * fmt.wBitsPerSample / 8;
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
        FAudio_CreateSourceVoice(faudio, &srcvoicemidi, &fmt, 0, 2.0f, &callbacks, nullptr, nullptr);
        FAudioSourceVoice_Start(srcvoicemidi, 0, FAUDIO_COMMIT_NOW);
    }
    initialized = 1;
}

void
closeal()
{
    if (!initialized) return;
    initialized = 0;
    FAudioSourceVoice_Stop(srcvoice, 0, FAUDIO_COMMIT_NOW);
    FAudioSourceVoice_FlushSourceBuffers(srcvoice);
    FAudioSourceVoice_Stop(srcvoicecd, 0, FAUDIO_COMMIT_NOW);
    FAudioSourceVoice_FlushSourceBuffers(srcvoicecd);
    if (srcvoicemidi)
    {
        FAudioSourceVoice_Stop(srcvoicemidi, 0, FAUDIO_COMMIT_NOW);
        FAudioSourceVoice_FlushSourceBuffers(srcvoicemidi);
        FAudioVoice_DestroyVoice(srcvoicemidi);
    }
    FAudioVoice_DestroyVoice(srcvoice);
    FAudioVoice_DestroyVoice(srcvoicecd);
    FAudioVoice_DestroyVoice(mastervoice);
    FAudio_Release(faudio);
    srcvoice = srcvoicecd = srcvoicemidi = nullptr;
    mastervoice = nullptr;
    faudio = nullptr;
}

void
givealbuffer_common(void *buf, FAudioSourceVoice* sourcevoice, size_t buflen)
{
    if (!initialized) return;
    
    FAudioVoice_SetVolume(mastervoice, pow(10.0, (double)sound_gain / 20.0), FAUDIO_COMMIT_NOW);
    FAudioBuffer buffer{};
    buffer.Flags = 0;
    if (sound_is_float)
    {
        buffer.pAudioData = (uint8_t*)new float[buflen];
        buffer.AudioBytes = (buflen) * sizeof(float);
    }
    else
    {
        buffer.pAudioData = (uint8_t*)new int16_t[buflen];
        buffer.AudioBytes = (buflen) * sizeof(int16_t);
    }
    if (buffer.pAudioData == nullptr)
    {
        fatal("faudio: Out Of Memory!");
    }
    memcpy((void*)buffer.pAudioData, buf, buffer.AudioBytes);
    buffer.PlayBegin = buffer.PlayLength = 0;
    buffer.PlayLength = buflen >> 1;
    buffer.pContext = (void*)buffer.pAudioData;
    FAudioSourceVoice_SubmitSourceBuffer(sourcevoice, &buffer, nullptr);
}

void
givealbuffer(void *buf)
{
    givealbuffer_common(buf, srcvoice, BUFLEN << 1);
}

void
givealbuffer_cd(void *buf)
{
    if (srcvoicecd) givealbuffer_common(buf, srcvoicecd, CD_BUFLEN << 1);
}

void
al_set_midi(int freq, int buf_size)
{
    midi_freq = freq;
    midi_buf_size = buf_size;

    if (initialized && srcvoicemidi)
    {
        FAudioSourceVoice_Stop(srcvoicemidi, 0, FAUDIO_COMMIT_NOW);
        FAudioSourceVoice_FlushSourceBuffers(srcvoicemidi);
        FAudioVoice_DestroyVoice(srcvoicemidi);
        srcvoicemidi = nullptr;
        FAudioWaveFormatEx fmt;
        fmt.nChannels = 2;
        if (sound_is_float)
        {
            fmt.wFormatTag = FAUDIO_FORMAT_IEEE_FLOAT;
            fmt.wBitsPerSample = 32;
        }
        else
        {
            fmt.wFormatTag = FAUDIO_FORMAT_PCM;
            fmt.wBitsPerSample = 16;
        }
        fmt.nSamplesPerSec = midi_freq;
        fmt.nBlockAlign = fmt.nChannels * fmt.wBitsPerSample / 8;
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
        fmt.cbSize = 0;
        FAudio_CreateSourceVoice(faudio, &srcvoicemidi, &fmt, 0, 2.0f, &callbacks, nullptr, nullptr);
        FAudioSourceVoice_Start(srcvoicemidi, 0, FAUDIO_COMMIT_NOW);
    }
}

void
givealbuffer_midi(void *buf, uint32_t size)
{
    givealbuffer_common(buf, srcvoicemidi, size);
}

}