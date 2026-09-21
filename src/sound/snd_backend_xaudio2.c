/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Interface to the OpenAL sound processing library.
 *
 * Authors: Cacodemon345,
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2022-2026 Cacodemon345.
 *          Copyright 2026 Miran Grca.
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
#    include <mmdeviceapi.h>
#else
#    include <FAudio.h>
#    include <FAudio_compat.h>
#endif

#include <86box/86box.h>
#include <86box/plat_dynld.h>
#include <86box/sound.h>
#include <86box/plat_unused.h>

#define FREQ   SOUND_FREQ
#define BUFLEN SOUNDBUFLEN

static IXAudio2 *xaudio2 = NULL;

typedef struct _xaudio2l_ {
    IXAudio2MasteringVoice *mastervoice;
} xaudio2_t;

typedef struct _xaudio2_source_ {
    IXAudio2SourceVoice *   srcvoice;
    int                     sample_rate;
} xaudio2_source_t;

#if defined(_WIN32) && !defined(USE_FAUDIO)
static void *xaudio2_handle = NULL;
static HRESULT(WINAPI *pXAudio2Create)(IXAudio2 **ppXAudio2, uint32_t Flags, XAUDIO2_PROCESSOR XAudio2Processor);
static dllimp_t xaudio2_imports[] = {
    {"XAudio2Create", &pXAudio2Create },
    { NULL,           NULL            },
};
#    define XAudio2Create pXAudio2Create
#endif

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

#if defined(_WIN32) && !defined(USE_FAUDIO)
/* GUIDs defined locally to avoid INITGUID/linking complications. */
static const GUID   xa2_CLSID_MMDeviceEnumerator = { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
static const GUID   xa2_IID_IMMDeviceEnumerator  = { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
static const PROPERTYKEY xa2_FriendlyNameKey     = { { 0xA45C254E, 0xDF1C, 0x4EFD, { 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0 } }, 14 };

static char    xa2_dev_list[8192]; /* double-null-terminated list of UTF-8 friendly names */
static LPWSTR  xa2_dev_ids[64];   /* parallel array of device IDs (CoTaskMemAlloc'd) */
static int     xa2_dev_count = 0;

static void
xa2_free_dev_ids(void)
{
    for (int i = 0; i < xa2_dev_count; i++) {
        CoTaskMemFree(xa2_dev_ids[i]);
        xa2_dev_ids[i] = NULL;
    }
    xa2_dev_count = 0;
}

static void
xa2_build_dev_list(void)
{
    IMMDeviceEnumerator *penum = NULL;
    IMMDeviceCollection *pcoll = NULL;
    UINT                 count = 0;
    char                *p    = xa2_dev_list;
    size_t               rem  = sizeof(xa2_dev_list);
    HRESULT              co_hr;

    xa2_free_dev_ids();
    memset(xa2_dev_list, 0, sizeof(xa2_dev_list));

    co_hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    if (FAILED(CoCreateInstance(&xa2_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                                &xa2_IID_IMMDeviceEnumerator, (void **) &penum)))
        goto done;

    if (FAILED(IMMDeviceEnumerator_EnumAudioEndpoints(penum, eRender,
                                                       DEVICE_STATE_ACTIVE, &pcoll))) {
        IMMDeviceEnumerator_Release(penum);
        goto done;
    }

    IMMDeviceCollection_GetCount(pcoll, &count);

    for (UINT i = 0; i < count && xa2_dev_count < 64; i++) {
        IMMDevice      *pdev   = NULL;
        IPropertyStore *pprops = NULL;
        PROPVARIANT     var;
        LPWSTR          pwszId = NULL;

        if (FAILED(IMMDeviceCollection_Item(pcoll, i, &pdev)))
            continue;

        if (FAILED(IMMDevice_GetId(pdev, &pwszId))) {
            IMMDevice_Release(pdev);
            continue;
        }

        memset(&var, 0, sizeof(var));
        if (FAILED(IMMDevice_OpenPropertyStore(pdev, STGM_READ, &pprops))) {
            CoTaskMemFree(pwszId);
            IMMDevice_Release(pdev);
            continue;
        }

        if (SUCCEEDED(IPropertyStore_GetValue(pprops, &xa2_FriendlyNameKey, &var))
            && var.vt == VT_LPWSTR) {
            char name_u8[512];
            int  len = WideCharToMultiByte(CP_UTF8, 0, var.pwszVal, -1,
                                           name_u8, (int) sizeof(name_u8), NULL, NULL);
            if (len > 1 && (size_t) len < rem) {
                memcpy(p, name_u8, len); /* len includes the null terminator */
                p   += len;
                rem -= len;
                xa2_dev_ids[xa2_dev_count++] = pwszId;
                pwszId = NULL;           /* ownership transferred */
            }
            CoTaskMemFree(var.pwszVal);
        }

        IPropertyStore_Release(pprops);
        if (pwszId)
            CoTaskMemFree(pwszId);
        IMMDevice_Release(pdev);
    }

    if (rem > 0)
        *p = '\0'; /* double-null terminator */

    IMMDeviceCollection_Release(pcoll);
    IMMDeviceEnumerator_Release(penum);

done:
    if (SUCCEEDED(co_hr))
        CoUninitialize();
}

/* Returns the device ID (LPWSTR) matching the given UTF-8 friendly name,
   or NULL if not found or name is empty (caller should use system default). */
static LPWSTR
xa2_find_dev_id(const char *friendly_name)
{
    if (friendly_name[0] == '\0')
        return NULL;

    xa2_build_dev_list();

    const char *p   = xa2_dev_list;
    int         idx = 0;
    while (*p && idx < xa2_dev_count) {
        if (strcmp(p, friendly_name) == 0)
            return xa2_dev_ids[idx];
        p += strlen(p) + 1;
        idx++;
    }
    return NULL; /* not found — fall back to system default */
}
#endif /* _WIN32 && !USE_FAUDIO */

static void
xaudio2_give_buffer(void *priv, void *src, const void *buf, const int size, UNUSED(int gain))
{
    const xaudio2_t *       dev    = (xaudio2_t *) priv;
    const xaudio2_source_t *source = (xaudio2_source_t *) src;

    if (fast_forward && (dev != NULL) && (source != NULL)) {
        (void) IXAudio2MasteringVoice_SetVolume(dev->mastervoice,
                                                sound_muted ? 0.0 :
                                                    pow(10.0, (double) sound_gain / 20.0),
                                                XAUDIO2_COMMIT_NOW);
        XAUDIO2_BUFFER buffer = { 0 };
        buffer.Flags          = 0;
        if (sound_is_float) {
            buffer.pAudioData = calloc(size, sizeof(float));
            buffer.AudioBytes = size * sizeof(float);
        } else {
            buffer.pAudioData = calloc(size, sizeof(int16_t));
            buffer.AudioBytes = size * sizeof(int16_t);
        }
        if (buffer.pAudioData == NULL) {
            fatal("xaudio2: Out Of Memory!");
        }
        memcpy((void *) buffer.pAudioData, buf, buffer.AudioBytes);
        buffer.PlayBegin = buffer.PlayLength = 0;
        buffer.PlayLength                    = (uint32_t) (size >> 1);
        buffer.pContext                      = (void *) buffer.pAudioData;
        (void) IXAudio2SourceVoice_SubmitSourceBuffer(source->srcvoice, &buffer, NULL);
    }
}

static void *
xaudio2_init_source(void *priv, int sample_rate, int buffer_size)
{
    xaudio2_source_t *source = (xaudio2_source_t *) calloc(1, sizeof(xaudio2_source_t));

    WAVEFORMATEX fmt;
    fmt.nChannels = 2;

    if (sound_is_float) {
        fmt.wFormatTag     = WAVE_FORMAT_IEEE_FLOAT;
        fmt.wBitsPerSample = 32;
    } else {
        fmt.wFormatTag     = WAVE_FORMAT_PCM;
        fmt.wBitsPerSample = 16;
    }

    source->sample_rate = sample_rate;

    fmt.nSamplesPerSec  = sample_rate;
    fmt.nBlockAlign     = fmt.nChannels * fmt.wBitsPerSample / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    fmt.cbSize          = 0;

    if (IXAudio2_CreateSourceVoice(xaudio2, &source->srcvoice, &fmt, 0, 2.0f, &callbacks, NULL, NULL))
        return NULL;

    (void) IXAudio2SourceVoice_SetVolume(source->srcvoice, 1, XAUDIO2_COMMIT_NOW);
    (void) IXAudio2SourceVoice_Start(source->srcvoice, 0, XAUDIO2_COMMIT_NOW);

    return source;
}

static void
xaudio2_close_source(UNUSED(void *priv), void *src)
{
    xaudio2_source_t *source = (xaudio2_source_t *) src;

    (void) IXAudio2SourceVoice_Stop(source->srcvoice, 0, XAUDIO2_COMMIT_NOW);
    (void) IXAudio2SourceVoice_FlushSourceBuffers(source->srcvoice);

    IXAudio2SourceVoice_DestroyVoice(source->srcvoice);

    source->srcvoice = NULL;

    free(source);
}

static const char *
xaudio2_get_output_devices(void)
{
#if defined(_WIN32) && !defined(USE_FAUDIO)
    xa2_build_dev_list();
    return (xa2_dev_count > 0) ? xa2_dev_list : NULL;
#else
    return NULL; /* FAudio: device enumeration not supported */
#endif
}

static void
xaudio2_close(void *priv)
{
    xaudio2_t *dev = (xaudio2_t *) priv;

    if (dev != NULL) {
        IXAudio2MasteringVoice_DestroyVoice(dev->mastervoice);
        IXAudio2_Release(xaudio2);

        dev->mastervoice = NULL;
        xaudio2          = NULL;
    }

#if defined(_WIN32) && !defined(USE_FAUDIO)
    if (xaudio2_handle != NULL) {
        dynld_close(xaudio2_handle);
        xaudio2_handle = NULL;
    }
#endif
}

static void *
xaudio2_init(void)
{
    xaudio2_t *dev = (xaudio2_t *) calloc(1, sizeof(xaudio2_t));

#if defined(_WIN32) && !defined(USE_FAUDIO)
    if (xaudio2_handle == NULL)
        xaudio2_handle = dynld_module("xaudio2_9.dll", xaudio2_imports);

    if (xaudio2_handle == NULL)
        xaudio2_handle = dynld_module("xaudio2_9redist.dll", xaudio2_imports);

    if (xaudio2_handle == NULL)
        return NULL;
#endif

    if (XAudio2Create(&xaudio2, 0, XAUDIO2_DEFAULT_PROCESSOR))
        return NULL;

#if defined(_WIN32) && !defined(USE_FAUDIO)
    LPWSTR dev_id = xa2_find_dev_id(sound_output_device);
    if (IXAudio2_CreateMasteringVoice(xaudio2, &dev->mastervoice, 2, FREQ, 0, dev_id, NULL, 0)) {
#else
    if (IXAudio2_CreateMasteringVoice(xaudio2, &dev->mastervoice, 2, FREQ, 0, 0, NULL, 0)) {
#endif
        IXAudio2_Release(xaudio2);
        xaudio2 = NULL;
        return NULL;
    }

    return dev;
}

const sound_backend_t sound_backend_openal = {
#if defined(_WIN32) && !defined(USE_FAUDIO)
    .name               = "XAudio2",
#else
    .name               = "FAudio",
#endif
    .internal_name      = "xaudio2",
    .give_buffer        = xaudio2_give_buffer,
    .init_source        = xaudio2_init_source,
    .close_source       = xaudio2_close_source,
    .get_output_devices = xaudio2_get_output_devices,
    .init               = xaudio2_init,
    .close              = xaudio2_close
};
