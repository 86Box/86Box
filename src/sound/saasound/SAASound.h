// Part of SAASound copyright 1998-2018 Dave Hooper <dave@beermex.com>
//
// SAASound.h: interface for the CSAASound class.
//
// This corresponds to the public (exported) DLL interface, so all
// APIs and client factory methods belong here.
//
// Compatibility notes : the intention is for this to be fully backwards
// compatible across minor and patch versions.  Any backwards breaking changes
// should be reflected as a major version increment.  New functionality can be added
// in minor versions so long as backwards compatiblity is maintained
// 
// Version 3.3.0 (4th Dec 2018)
//
//////////////////////////////////////////////////////////////////////

#ifndef SAASOUND_H_INCLUDED
#define SAASOUND_H_INCLUDED

// define this if you want to output diagnostic text and PCM files
//#define DEBUGSAA

// Parameters for use with SetSoundParameters, for example,
// SetSoundParameters(SAAP_NOFILTER | SAAP_44100 | SAA_16BIT | SAA_STEREO);
// SAAP_FILTER_HIGHPASS_SIMPLE can be ORd with SAAP_FILTER_OVERSAMPLE64x/2x
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

#ifdef _WIN32
#define SAAAPI _stdcall
#else
#define SAAAPI
#endif


#ifdef __cplusplus

class CSAASound
{
public:
	virtual ~CSAASound() { }

	virtual void SetSoundParameters (SAAPARAM uParam) = 0;
	virtual void WriteAddress (BYTE nReg) = 0;
	virtual void WriteData (BYTE nData) = 0;
	virtual void WriteAddressData (BYTE nReg, BYTE nData) = 0;
	virtual void Clear () = 0;
	virtual BYTE ReadAddress () = 0;

	virtual SAAPARAM GetCurrentSoundParameters () = 0;
	virtual unsigned long GetCurrentSampleRate () = 0;
	static unsigned long GetSampleRate (SAAPARAM uParam);
	virtual unsigned short GetCurrentBytesPerSample () = 0;
	static unsigned short GetBytesPerSample (SAAPARAM uParam);

	virtual void GenerateMany (BYTE * pBuffer, unsigned long nSamples) = 0;

	virtual void SetClockRate(unsigned int nClockRate) = 0;
	virtual void SetSampleRate(unsigned int nSampleRate) = 0;
	virtual void SetOversample(unsigned int nOversample) = 0;
};

typedef class CSAASound * LPCSAASOUND;

LPCSAASOUND SAAAPI CreateCSAASound(void);
void SAAAPI DestroyCSAASound(LPCSAASOUND object);

#endif	// __cplusplus


#ifdef __cplusplus
extern "C" {
#endif

typedef void * SAASND;

// "C-style" interface for the CSAASound class
SAASND SAAAPI newSAASND(void);
void SAAAPI deleteSAASND(SAASND object);

void SAAAPI SAASNDSetSoundParameters(SAASND object, SAAPARAM uParam);
void SAAAPI SAASNDWriteAddress(SAASND object, BYTE nReg);
void SAAAPI SAASNDWriteData(SAASND object, BYTE nData);
void SAAAPI SAASNDWriteAddressData(SAASND object, BYTE nReg, BYTE nData);
void SAAAPI SAASNDClear(SAASND object);

SAAPARAM SAAAPI SAASNDGetCurrentSoundParameters(SAASND object);
unsigned short SAAAPI SAASNDGetCurrentBytesPerSample(SAASND object);
unsigned short SAAAPI SAASNDGetBytesPerSample(SAAPARAM uParam);
unsigned long SAAAPI SAASNDGetCurrentSampleRate(SAASND object);
unsigned long SAAAPI SAASNDGetSampleRate(SAAPARAM uParam);

void SAAAPI SAASNDGenerateMany(SAASND object, BYTE * pBuffer, unsigned long nSamples);
void SAAAPI SAASNDSetClockRate(SAASND object, unsigned int nClockRate);
void SAAAPI SAASNDSetSampleRate(SAASND object, unsigned int nSampleRate);
void SAAAPI SAASNDSetOversample(SAASND object, unsigned int nOversample);

BYTE SAAAPI SAASNDReadAddress(SAASND object);

#ifdef __cplusplus
}; // extern "C"
#endif

#endif	// SAASOUND_H_INCLUDED
