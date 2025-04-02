// Part of SAASound copyright 1998-2018 Dave Hooper <dave@beermex.com>
//
// **********
// * PUBLIC *
// **********
//
// SAASndC.h: "C-style" interface for the CSAASound class.
//
//////////////////////////////////////////////////////////////////////

#ifndef SAASNDC_H_INCLUDED
#define SAASNDC_H_INCLUDED

#ifdef _MSC_VER
#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
#endif

#ifndef SAASOUND_H_INCLUDED

// Parameters for use with SetSoundParameters, for example,
// SetSoundParameters(SAAP_NOFILTER | SAAP_44100 | SAAP_16BIT | SAAP_STEREO);
#define SAAP_FILTER_HIGHPASS_SIMPLE 0x00000400
#define SAAP_FILTER_OVERSAMPLE64x 0x00000300
#define SAAP_FILTER_OVERSAMPLE2x 0x00000200
#define SAAP_FILTER SAAP_FILTER_OVERSAMPLE2x
#define SAAP_NOFILTER 0x00000100
#define SAAP_44100 0x00000030
#define SAAP_22050 0x00000020
#define SAAP_11025 0x00000010
#define SAAP_16BIT 0x0000000c
#define SAAP_8BIT 0x00000004
#define SAAP_STEREO 0x00000003
#define SAAP_MONO 0x00000001

// Bitmasks for use with GetCurrentSoundParameters, for example,
// unsigned long CurrentSampleRateParameter = GetCurrentSoundParameters()
#define SAAP_MASK_FILTER 0x00000f00
#define SAAP_MASK_FILTER_HIGHPASS 0x00000c00
#define SAAP_MASK_FILTER_OVERSAMPLE 0x00000300
#define SAAP_MASK_SAMPLERATE 0x000000030
#define SAAP_MASK_BITDEPTH 0x0000000c
#define SAAP_MASK_CHANNELS 0x00000003

typedef unsigned long SAAPARAM;


#ifndef BYTE
#define BYTE unsigned char
#endif

#ifdef WIN32
#ifndef WINAPI
#define WINAPI __stdcall
#endif
#define EXTAPI __declspec(dllexport) WINAPI
#else // Win32
#ifndef WINAPI
#define WINAPI /**/
#endif
#define EXTAPI /**/
#endif // Win32

#endif // SAASOUND_H_INCLUDED

typedef void * SAASND;

// the following are implemented as calls, etc, to a class.

#ifdef __cplusplus
extern "C" {
#endif

SAASND EXTAPI newSAASND(void);
void EXTAPI deleteSAASND(SAASND object);

void EXTAPI SAASNDSetSoundParameters(SAASND object, SAAPARAM uParam);
void EXTAPI SAASNDWriteAddress(SAASND object, BYTE nReg);
void EXTAPI SAASNDWriteData(SAASND object, BYTE nData);
void EXTAPI SAASNDWriteAddressData(SAASND object, BYTE nReg, BYTE nData);
void EXTAPI SAASNDClear(SAASND object);
BYTE EXTAPI SAASNDReadAddress(SAASND object);

SAAPARAM EXTAPI SAASNDGetCurrentSoundParameters(SAASND object);
unsigned short EXTAPI SAASNDGetCurrentBytesPerSample(SAASND object);
unsigned short EXTAPI SAASNDGetBytesPerSample(SAAPARAM uParam);
unsigned long EXTAPI SAASNDGetCurrentSampleRate(SAASND object);
unsigned long EXTAPI SAASNDGetSampleRate(SAAPARAM uParam);

void EXTAPI SAASNDGenerateMany(SAASND object, BYTE * pBuffer, unsigned long nSamples);

void EXTAPI SAASNDSetClockRate(SAASND object, unsigned int nClockRate);
void EXTAPI SAASNDSetSampleRate(SAASND object, unsigned int nSampleRate);
void EXTAPI SAASNDSetOversample(SAASND object, unsigned int nOversample);


#ifdef __cplusplus
}; // extern "C"
#endif

#endif	// SAASNDC_H_INCLUDED
