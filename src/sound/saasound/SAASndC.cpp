// Part of SAASound copyright 1998-2018 Dave Hooper <dave@beermex.com>
//
// Thanks to this file (and associated header file) you can now
// use CSAASound from within a standard 'C' program
//
//////////////////////////////////////////////////////////////////////

#include "SAASound.h"
#include "types.h"
#include "SAAEnv.h"
#include "SAANoise.h"
#include "SAAFreq.h"
#include "SAAAmp.h"
#include "SAASound.h"
#include "SAAImpl.h"

SAASND SAAAPI newSAASND(void)
{
	return (SAASND)(new CSAASoundInternal());
}

void SAAAPI deleteSAASND(SAASND object)
{
	delete (LPCSAASOUND)(object);
}

void SAAAPI SAASNDSetClockRate(SAASND object, unsigned int nClockRate)
{
	((LPCSAASOUND)(object))->SetClockRate(nClockRate);
}

void SAAAPI SAASNDSetSoundParameters(SAASND object, SAAPARAM uParam)
{
	((LPCSAASOUND)(object))->SetSoundParameters(uParam);
}

void SAAAPI SAASNDWriteAddress(SAASND object, BYTE nReg)
{
	((LPCSAASOUND)(object))->WriteAddress(nReg);
}

void SAAAPI SAASNDWriteData(SAASND object, BYTE nData)
{
	((LPCSAASOUND)(object))->WriteData(nData);
}

void SAAAPI SAASNDWriteAddressData(SAASND object, BYTE nReg, BYTE nData)
{
	((LPCSAASOUND)(object))->WriteAddressData(nReg, nData);
}

void SAAAPI SAASNDClear(SAASND object)
{
	((LPCSAASOUND)(object))->Clear();
}

SAAPARAM SAAAPI SAASNDGetCurrentSoundParameters(SAASND object)
{
	return ((LPCSAASOUND)(object))->GetCurrentSoundParameters();
}

unsigned short SAAAPI SAASNDGetCurrentBytesPerSample(SAASND object)
{
	return ((LPCSAASOUND)(object))->GetCurrentBytesPerSample();
}

unsigned short SAAAPI SAASNDGetBytesPerSample(SAAPARAM uParam)
{
	return CSAASound::GetBytesPerSample(uParam);
}

unsigned long SAAAPI SAASNDGetCurrentSampleRate(SAASND object)
{
	return ((LPCSAASOUND)(object))->GetCurrentSampleRate();
}

unsigned long SAAAPI SAASNDGetSampleRate(SAAPARAM uParam)
{
	return CSAASound::GetSampleRate(uParam);
}

void SAAAPI SAASNDGenerateMany(SAASND object, BYTE * pBuffer, unsigned long nSamples)
{
	((LPCSAASOUND)(object))->GenerateMany(pBuffer, nSamples);
}

void SAAAPI SAASNDSetSampleRate(SAASND object, unsigned int nSampleRate)
{
	return ((LPCSAASOUND)(object))->SetSampleRate(nSampleRate);
}

void SAAAPI SAASNDSetOversample(SAASND object, unsigned int nOversample)
{
	return ((LPCSAASOUND)(object))->SetOversample(nOversample);
}

BYTE SAAAPI SAASNDReadAddress(SAASND object)
{
	return ((LPCSAASOUND)(object))->ReadAddress();
}
