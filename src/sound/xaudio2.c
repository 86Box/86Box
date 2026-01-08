/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Interface to the XAudio2 audio processing library.
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2022 Cacodemon345.
 */
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#if defined(_WIN32) && !defined(USE_FAUDIO)
#    define COBJMACROS
#    include <xaudio2.h>
#else
#    include <FAudio.h>
#    include <FAudio_compat.h>
#endif

#include <86box/86box.h>
#include <86box/midi.h>
#include <86box/plat_dynld.h>
#include <86box/sound.h>
#include <86box/plat_unused.h>

#if defined(_WIN32) && !defined(USE_FAUDIO)
static void *xaudio2_handle = NULL;
static HRESULT(WINAPI *pXAudio2Create)(IXAudio2 **ppXAudio2, uint32_t Flags, XAUDIO2_PROCESSOR XAudio2Processor);
static dllimp_t xaudio2_imports[] = {
    {"XAudio2Create", &pXAudio2Create },
    { NULL,           NULL            },
};
#    define XAudio2Create pXAudio2Create
#endif

static int                     midi_freq     = FREQ_44100;
static int                     midi_buf_size = 4410;
static int                     initialized   = 0;
static IXAudio2               *xaudio2       = NULL;
static IXAudio2MasteringVoice *mastervoice   = NULL;
static IXAudio2SourceVoice    *srcvoice      = NULL;
static IXAudio2SourceVoice    *srcvoicemusic = NULL;
static IXAudio2SourceVoice    *srcvoicewt    = NULL;
static IXAudio2SourceVoice    *srcvoicemidi  = NULL;
static IXAudio2SourceVoice    *srcvoicecd    = NULL;
static IXAudio2SourceVoice    *srcvoicefdd   = NULL;
static IXAudio2SourceVoice    *srcvoicehdd   = NULL;

extern bool fast_forward;

#define FREQ   SOUND_FREQ
#define BUFLEN SOUNDBUFLEN

static void WINAPI
OnVoiceProcessingPassStart(UNUSED(IXAudio2VoiceCallback *callback), UNUSED(uint32_t bytesRequired))
{
    //
}
static void WINAPI
OnVoiceProcessingPassEnd(UNUSED(IXAudio2VoiceCallback *callback))
{
    //
}
static void WINAPI
OnStreamEnd(UNUSED(IXAudio2VoiceCallback *callback))
{
    //
}
static void WINAPI
OnBufferStart(UNUSED(IXAudio2VoiceCallback *callback), UNUSED(void *pBufferContext))
{
    //
}
static void WINAPI
OnLoopEnd(UNUSED(IXAudio2VoiceCallback *callback), UNUSED(void *pBufferContext))
{
    //
}
static void WINAPI
OnVoiceError(UNUSED(IXAudio2VoiceCallback *callback), UNUSED(void *pBufferContext), UNUSED(HRESULT error))
{
    //
}

static void WINAPI
OnBufferEnd(UNUSED(IXAudio2VoiceCallback *callback), UNUSED(void *pBufferContext))
{
    free(pBufferContext);
}

#if defined(_WIN32) && !defined(USE_FAUDIO)
static IXAudio2VoiceCallbackVtbl callbacksVtbl =
#else
static FAudioVoiceCallback callbacks =
#endif
    {
        .OnVoiceProcessingPassStart = OnVoiceProcessingPassStart,
        .OnVoiceProcessingPassEnd   = OnVoiceProcessingPassEnd,
        .OnStreamEnd                = OnStreamEnd,
        .OnBufferStart              = OnBufferStart,
        .OnBufferEnd                = OnBufferEnd,
        .OnLoopEnd                  = OnLoopEnd,
        .OnVoiceError               = OnVoiceError
    };

#if defined(_WIN32) && !defined(USE_FAUDIO)
static IXAudio2VoiceCallback callbacks = { &callbacksVtbl };
#endif

void
inital(void)
{
#if defined(_WIN32) && !defined(USE_FAUDIO)
    if (xaudio2_handle == NULL)
        xaudio2_handle = dynld_module("xaudio2_9.dll", xaudio2_imports);

    if (xaudio2_handle == NULL)
        xaudio2_handle = dynld_module("xaudio2_9redist.dll", xaudio2_imports);

    if (xaudio2_handle == NULL)
        return;
#endif

    if (XAudio2Create(&xaudio2, 0, XAUDIO2_DEFAULT_PROCESSOR))
        return;

    if (IXAudio2_CreateMasteringVoice(xaudio2, &mastervoice, 2, FREQ, 0, 0, NULL, 0)) {
        IXAudio2_Release(xaudio2);
        xaudio2 = NULL;
        return;
    }

    WAVEFORMATEX fmt;
    fmt.nChannels = 2;

    if (sound_is_float) {
        fmt.wFormatTag     = WAVE_FORMAT_IEEE_FLOAT;
        fmt.wBitsPerSample = 32;
    } else {
        fmt.wFormatTag     = WAVE_FORMAT_PCM;
        fmt.wBitsPerSample = 16;
    }

    fmt.nSamplesPerSec  = FREQ;
    fmt.nBlockAlign     = fmt.nChannels * fmt.wBitsPerSample / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    fmt.cbSize          = 0;

    if (IXAudio2_CreateSourceVoice(xaudio2, &srcvoice, &fmt, 0, 2.0f, &callbacks, NULL, NULL)) {
        IXAudio2MasteringVoice_DestroyVoice(mastervoice);
        IXAudio2_Release(xaudio2);
        xaudio2     = NULL;
        mastervoice = NULL;
        return;
    }

    fmt.nSamplesPerSec  = MUSIC_FREQ;
    fmt.nBlockAlign     = fmt.nChannels * fmt.wBitsPerSample / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    (void) IXAudio2_CreateSourceVoice(xaudio2, &srcvoicemusic, &fmt, 0, 2.0f, &callbacks, NULL, NULL);

    fmt.nSamplesPerSec  = WT_FREQ;
    fmt.nBlockAlign     = fmt.nChannels * fmt.wBitsPerSample / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    (void) IXAudio2_CreateSourceVoice(xaudio2, &srcvoicewt, &fmt, 0, 2.0f, &callbacks, NULL, NULL);

    fmt.nSamplesPerSec  = CD_FREQ;
    fmt.nBlockAlign     = fmt.nChannels * fmt.wBitsPerSample / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    (void) IXAudio2_CreateSourceVoice(xaudio2, &srcvoicecd, &fmt, 0, 2.0f, &callbacks, NULL, NULL);

    fmt.nSamplesPerSec  = FREQ;
    fmt.nBlockAlign     = fmt.nChannels * fmt.wBitsPerSample / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    (void) IXAudio2_CreateSourceVoice(xaudio2, &srcvoicefdd, &fmt, 0, 2.0f, &callbacks, NULL, NULL);
    (void) IXAudio2_CreateSourceVoice(xaudio2, &srcvoicehdd, &fmt, 0, 2.0f, &callbacks, NULL, NULL);

    (void) IXAudio2SourceVoice_SetVolume(srcvoice, 1, XAUDIO2_COMMIT_NOW);
    (void) IXAudio2SourceVoice_Start(srcvoice, 0, XAUDIO2_COMMIT_NOW);
    (void) IXAudio2SourceVoice_Start(srcvoicecd, 0, XAUDIO2_COMMIT_NOW);
    (void) IXAudio2SourceVoice_Start(srcvoicemusic, 0, XAUDIO2_COMMIT_NOW);
    (void) IXAudio2SourceVoice_Start(srcvoicewt, 0, XAUDIO2_COMMIT_NOW);
    (void) IXAudio2SourceVoice_Start(srcvoicefdd, 0, XAUDIO2_COMMIT_NOW);
    (void) IXAudio2SourceVoice_Start(srcvoicehdd, 0, XAUDIO2_COMMIT_NOW);

    const char *mdn = midi_out_device_get_internal_name(midi_output_device_current);

    if ((strcmp(mdn, "none") != 0) && (strcmp(mdn, SYSTEM_MIDI_INTERNAL_NAME) != 0)) {
        fmt.nSamplesPerSec  = midi_freq;
        fmt.nBlockAlign     = fmt.nChannels * fmt.wBitsPerSample / 8;
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
        (void) IXAudio2_CreateSourceVoice(xaudio2, &srcvoicemidi, &fmt, 0, 2.0f, &callbacks, NULL, NULL);
        (void) IXAudio2SourceVoice_Start(srcvoicemidi, 0, XAUDIO2_COMMIT_NOW);
    }

    initialized = 1;
    atexit(closeal);
}

void
closeal(void)
{
    if (!initialized)
        return;

    initialized = 0;
    (void) IXAudio2SourceVoice_Stop(srcvoice, 0, XAUDIO2_COMMIT_NOW);
    (void) IXAudio2SourceVoice_FlushSourceBuffers(srcvoice);
    (void) IXAudio2SourceVoice_Stop(srcvoicemusic, 0, XAUDIO2_COMMIT_NOW);
    (void) IXAudio2SourceVoice_FlushSourceBuffers(srcvoicemusic);
    (void) IXAudio2SourceVoice_Stop(srcvoicewt, 0, XAUDIO2_COMMIT_NOW);
    (void) IXAudio2SourceVoice_FlushSourceBuffers(srcvoicewt);
    (void) IXAudio2SourceVoice_Stop(srcvoicecd, 0, XAUDIO2_COMMIT_NOW);
    (void) IXAudio2SourceVoice_FlushSourceBuffers(srcvoicecd);
    (void) IXAudio2SourceVoice_Stop(srcvoicefdd, 0, XAUDIO2_COMMIT_NOW);
    (void) IXAudio2SourceVoice_FlushSourceBuffers(srcvoicefdd);
    (void) IXAudio2SourceVoice_Stop(srcvoicehdd, 0, XAUDIO2_COMMIT_NOW);
    (void) IXAudio2SourceVoice_FlushSourceBuffers(srcvoicehdd);
    if (srcvoicemidi) {
        (void) IXAudio2SourceVoice_Stop(srcvoicemidi, 0, XAUDIO2_COMMIT_NOW);
        (void) IXAudio2SourceVoice_FlushSourceBuffers(srcvoicemidi);
        IXAudio2SourceVoice_DestroyVoice(srcvoicemidi);
    }
    IXAudio2SourceVoice_DestroyVoice(srcvoicewt);
    IXAudio2SourceVoice_DestroyVoice(srcvoicecd);
    IXAudio2SourceVoice_DestroyVoice(srcvoicefdd);
    IXAudio2SourceVoice_DestroyVoice(srcvoicehdd);
    IXAudio2SourceVoice_DestroyVoice(srcvoicemusic);
    IXAudio2SourceVoice_DestroyVoice(srcvoice);
    IXAudio2MasteringVoice_DestroyVoice(mastervoice);
    IXAudio2_Release(xaudio2);
    srcvoice     = NULL;
    srcvoicecd   = NULL;
    srcvoicemidi = NULL;
    srcvoicefdd  = NULL;
    srcvoicehdd  = NULL;
    mastervoice  = NULL;
    xaudio2      = NULL;

#if defined(_WIN32) && !defined(USE_FAUDIO)
    dynld_close(xaudio2_handle);
    xaudio2_handle = NULL;
#endif
}

void
givealbuffer_common(const void *buf, IXAudio2SourceVoice *sourcevoice, const size_t buflen)
{
    if (!initialized || fast_forward)
        return;

    (void) IXAudio2MasteringVoice_SetVolume(mastervoice, sound_muted ? 0.0 : pow(10.0, (double) sound_gain / 20.0),
                                            XAUDIO2_COMMIT_NOW);
    XAUDIO2_BUFFER buffer = { 0 };
    buffer.Flags          = 0;
    if (sound_is_float) {
        buffer.pAudioData = calloc(buflen, sizeof(float));
        buffer.AudioBytes = buflen * sizeof(float);
    } else {
        buffer.pAudioData = calloc(buflen, sizeof(int16_t));
        buffer.AudioBytes = buflen * sizeof(int16_t);
    }
    if (buffer.pAudioData == NULL) {
        fatal("xaudio2: Out Of Memory!");
    }
    memcpy((void *) buffer.pAudioData, buf, buffer.AudioBytes);
    buffer.PlayBegin = buffer.PlayLength = 0;
    buffer.PlayLength                    = buflen >> 1;
    buffer.pContext                      = (void *) buffer.pAudioData;
    (void) IXAudio2SourceVoice_SubmitSourceBuffer(sourcevoice, &buffer, NULL);
}

void
givealbuffer(const void *buf)
{
    givealbuffer_common(buf, srcvoice, BUFLEN << 1);
}

void
givealbuffer_music(const void *buf)
{
    givealbuffer_common(buf, srcvoicemusic, MUSICBUFLEN << 1);
}

void
givealbuffer_wt(const void *buf)
{
    givealbuffer_common(buf, srcvoicewt, WTBUFLEN << 1);
}

void
givealbuffer_cd(const void *buf)
{
    if (srcvoicecd)
        givealbuffer_common(buf, srcvoicecd, CD_BUFLEN << 1);
}

void
givealbuffer_fdd(const void *buf, const uint32_t size)
{
    if (!initialized)
        return;
    
    if (!srcvoicefdd)
        return;

    givealbuffer_common(buf, srcvoicefdd, size);
}

void
givealbuffer_hdd(const void *buf, const uint32_t size)
{
    if (!initialized)
        return;
    
    if (!srcvoicefdd)
        return;

    givealbuffer_common(buf, srcvoicehdd, size);
}

void
al_set_midi(const int freq, const int buf_size)
{
    midi_freq     = freq;
    midi_buf_size = buf_size;

    if (initialized && srcvoicemidi) {
        (void) IXAudio2SourceVoice_Stop(srcvoicemidi, 0, XAUDIO2_COMMIT_NOW);
        (void) IXAudio2SourceVoice_FlushSourceBuffers(srcvoicemidi);
        IXAudio2SourceVoice_DestroyVoice(srcvoicemidi);
        srcvoicemidi = NULL;
        WAVEFORMATEX fmt;
        fmt.nChannels = 2;
        if (sound_is_float) {
            fmt.wFormatTag     = WAVE_FORMAT_IEEE_FLOAT;
            fmt.wBitsPerSample = 32;
        } else {
            fmt.wFormatTag     = WAVE_FORMAT_PCM;
            fmt.wBitsPerSample = 16;
        }
        fmt.nSamplesPerSec  = midi_freq;
        fmt.nBlockAlign     = fmt.nChannels * fmt.wBitsPerSample / 8;
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
        fmt.cbSize          = 0;
        (void) IXAudio2_CreateSourceVoice(xaudio2, &srcvoicemidi, &fmt, 0, 2.0f, &callbacks, NULL, NULL);
        (void) IXAudio2SourceVoice_Start(srcvoicemidi, 0, XAUDIO2_COMMIT_NOW);
    }
}

void
givealbuffer_midi(const void *buf, const uint32_t size)
{
    givealbuffer_common(buf, srcvoicemidi, size);
}