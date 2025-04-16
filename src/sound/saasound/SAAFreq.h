// Part of SAASound copyright 1998-2018 Dave Hooper <dave@beermex.com>
//
// SAAFreq.h: interface for the CSAAFreq class.
// Note about Samplerates: 0=44100, 1=22050; 2=11025
//
//////////////////////////////////////////////////////////////////////

#ifndef SAAFREQ_H_INCLUDE
#define SAAFREQ_H_INCLUDE

#include "defns.h"

class CSAAFreq  
{
private:
#ifdef SAAFREQ_FIXED_CLOCKRATE
	// 'load in' the data for the static frequency lookup table
	// precomputed for a fixed clockrate
	// See: tools/freqdat.py
	const static unsigned long m_FreqTable[2048];
#else
	// we'll calculate the frequency lookup table at runtime.
	static unsigned long m_FreqTable[2048];
	static unsigned long m_nClockRate;
#endif

	unsigned long m_nCounter;
	unsigned long m_nAdd;
	unsigned long m_nCounter_low;
	unsigned int m_nOversample;
	unsigned long m_nCounterLimit_low;
	int m_nLevel;

	int m_nCurrentOffset;
	int m_nCurrentOctave;
	int m_nNextOffset;
	int m_nNextOctave;
	bool m_bIgnoreOffsetData;
	bool m_bNewData;
	bool m_bSync;

	unsigned long m_nSampleRate;
	CSAANoise * const m_pcConnectedNoiseGenerator;
	CSAAEnv * const m_pcConnectedEnvGenerator;
	const int m_nConnectedMode; // 0 = nothing; 1 = envgenerator; 2 = noisegenerator

	void UpdateOctaveOffsetData(void);
	void SetAdd(void);

public:
	CSAAFreq(CSAANoise * const pcNoiseGenerator, CSAAEnv * const pcEnvGenerator);
	~CSAAFreq();
	void SetFreqOffset(BYTE nOffset);
	void SetFreqOctave(BYTE nOctave);
	void _SetSampleRate(unsigned int nSampleRate);
	void _SetOversample(unsigned int oversample);
	void _SetClockRate(int nClockRate);
	void Sync(bool bSync);
	int Tick(void);
	int Level(void) const;

};

inline int CSAAFreq::Level(void) const
{
	if (m_bSync)
		return 1;

	return m_nLevel;
}

#endif	// SAAFREQ_H_INCLUDE
